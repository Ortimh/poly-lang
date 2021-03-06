#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

#include "poly_vm.h"
#include "poly_value.h"
#include "poly_log.h"

static void throwerr(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "\x1B[1;31mError: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\x1B[0m\n");
	va_end(args);
	exit(EXIT_FAILURE);
}

static void pushvalue(poly_VM *vm, poly_Value *val)
{
	if (vm->stack.size >= POLY_MAX_STACK)
		throwerr("stack overflow");

	vm->stack.val[vm->stack.size++] = val;

#ifdef POLY_DEBUG
	POLY_LOG_START(VMA)

	if (val->type == POLY_VAL_NUM)
	{
		if (ceilf(val->num) == val->num)
			POLY_LOG("%.00f", val->num)
		else
			POLY_LOG("%f", val->num)
	}
	else if (val->type == POLY_VAL_BOOL)
		POLY_LOG("%s", (val->bool ? "true" : "false"))
	else
		POLY_LOG("'%s'", val->str)
	
	POLY_LOG(" pushed. Stack: ")

	for (unsigned int i = 0; i < vm->stack.size; ++i)
	{
		if (vm->stack.val[i]->type == POLY_VAL_NUM)
		{
			if (ceilf(vm->stack.val[i]->num) == vm->stack.val[i]->num)
				POLY_LOG("%.00f ", vm->stack.val[i]->num)
			else
				POLY_LOG("%f ", vm->stack.val[i]->num)
		}
		else if (vm->stack.val[i]->type == POLY_VAL_BOOL)
			POLY_LOG("%s ", (vm->stack.val[i]->bool ? "true" : "false"))
		else
			POLY_LOG("'%s' ", vm->stack.val[i]->str)
	}

	POLY_LOG("\n")
	POLY_LOG_END
#endif
}

static poly_Value *popvalue(poly_VM *vm)
{
	if (vm->stack.size == 0)
		throwerr("stack is empty");
	
	poly_Value *val = vm->stack.val[--vm->stack.size];

#ifdef POLY_DEBUG
	POLY_LOG_START(VMA)

	if (val->type == POLY_VAL_NUM)
	{
		if (ceilf(val->num) == val->num)
			POLY_LOG("%.00f", val->num)
		else
			POLY_LOG("%f", val->num)
	}
	else if (val->type == POLY_VAL_BOOL)
		POLY_LOG("%s", (val->bool ? "true" : "false"))
	else
		POLY_LOG("'%s'", val->str)

	POLY_LOG(" popped. Stack: ")

	for (unsigned int i = 0; i < vm->stack.size; ++i)
	{
		if (vm->stack.val[i]->type == POLY_VAL_NUM)
		{
			if (ceilf(vm->stack.val[i]->num) == vm->stack.val[i]->num)
				POLY_LOG("%.00f ", vm->stack.val[i]->num)
			else
				POLY_LOG("%f ", vm->stack.val[i]->num)
		}
		else if (vm->stack.val[i]->type == POLY_VAL_BOOL)
			POLY_LOG("%s ", (vm->stack.val[i]->bool ? "true" : "false"))
		else
			POLY_LOG("'%s' ", vm->stack.val[i]->str)
	}

	POLY_LOG("\n")
	POLY_LOG_END
#endif

	vm->stack.val[vm->stack.size] = NULL;
	return val;
}

long hash(const char *str)
{
    long hash = 5381;
    int c;

    while ((c = *str++) != '\0')
        hash = ((hash << 5) + hash) + c; // hash  *33 + c

    return hash;
}

long localindex(const char *str)
{
	// FIXME: What should I do when `index` is actually in collision?
	return hash(str) % POLY_MAX_LOCALS;
}

static poly_Value *getvalue(poly_VM *vm, const char *id)
{
	long index = localindex(id);
#ifdef POLY_DEBUG
	POLY_IMM_LOG(VMA, "Reading local '%s' from index %ld...\n", id, index)
#endif
	poly_Scope *scope = vm->scope[vm->curscope];
	poly_Variable *local = scope->local[index];
	return local->val;
}

static void addlocal(poly_VM *vm, const char *id, poly_Value *val)
{
	poly_Variable *local = (poly_Variable*)vm->config->alloc(NULL, sizeof(poly_Variable));
	local->id = id;
	local->val = val;

	long index = localindex(local->id);
	poly_Scope *scope = vm->scope[vm->curscope];
	scope->local[index] = local;

#ifdef POLY_DEBUG
	POLY_IMM_LOG(VMA, "Added local '%s' to index %ld\n", local->id, index)
#endif
}

static const poly_Code *curcode(poly_VM *vm)
{
	return vm->codestream.cur;
}

static void advcode(poly_VM *vm)
{
	vm->codestream.cur++;
}

POLY_LOCAL void interpret(poly_VM *vm)
{
	poly_Scope *scope = (poly_Scope*)vm->config->alloc(NULL, sizeof(poly_Scope));
	vm->scope[vm->curscope] = scope;

	while (curcode(vm)->inst != POLY_INST_END)
	{
#ifdef POLY_DEBUG
		POLY_IMM_LOG(VMA, "Reading instruction 0x%02X...\n", curcode(vm)->inst)
#endif
		switch (curcode(vm)->inst)
		{
		case POLY_INST_LITERAL:
		{
			advcode(vm);
			pushvalue(vm, curcode(vm)->val);
			break;
		}
		case POLY_INST_GET_VALUE:
		{
			poly_Value *id = popvalue(vm);

			if (id->type == POLY_VAL_ID)
			{
				poly_Value *val = getvalue(vm, id->str);
				pushvalue(vm, val);
				vm->config->alloc(id, 0);
			}
			else
				throwerr("identifier expected");

			break;
		}
		case POLY_INST_BIN_ADD:
		case POLY_INST_BIN_SUB:
		case POLY_INST_BIN_MUL:
		case POLY_INST_BIN_DIV:
		case POLY_INST_BIN_MOD:
		case POLY_INST_BIN_EXP:
		{
			poly_Value *rval = popvalue(vm);
			poly_Value *lval = popvalue(vm);

			if (rval->type == POLY_VAL_ID)
				rval = getvalue(vm, rval->str);
			if (lval->type == POLY_VAL_ID)
				lval = getvalue(vm, lval->str);

			poly_Value *val = vm->config->alloc(NULL, sizeof(poly_Value));

			if (lval->type == POLY_VAL_NUM &&
			    rval->type == POLY_VAL_NUM)
			{
				val->type = POLY_VAL_NUM;

				if (curcode(vm)->inst == POLY_INST_BIN_ADD)
					val->num = lval->num + rval->num;
				else if (curcode(vm)->inst == POLY_INST_BIN_SUB)
					val->num = lval->num - rval->num;
				else if (curcode(vm)->inst == POLY_INST_BIN_MUL)
					val->num = lval->num * rval->num;
				else if (curcode(vm)->inst == POLY_INST_BIN_DIV)
					val->num = lval->num / rval->num;
				else if (curcode(vm)->inst == POLY_INST_BIN_MOD)
					val->num = (long)lval->num % (long)rval->num;
				else if (curcode(vm)->inst == POLY_INST_BIN_EXP)
					val->num = pow(lval->num, rval->num);
				else
					throwerr("invalid binary operator");
			}
			else
				throwerr("the operands are illegal");
			
			vm->config->alloc(lval, 0);
			vm->config->alloc(rval, 0);
			pushvalue(vm, val);

			break;
		}
		case POLY_INST_UN_NEG:
		{
			poly_Value *val = popvalue(vm);

			if (val->type == POLY_VAL_ID)
				val = getvalue(vm, val->str);
			
			poly_Value *newval = vm->config->alloc(NULL, sizeof(poly_Value));

			if (val->type == POLY_VAL_NUM)
			{
				newval->type = POLY_VAL_NUM;
				newval->num = -val->num;
			}
			else
				throwerr("the operand is illegal");
			
			vm->config->alloc(val, 0);
			pushvalue(vm, newval);

			break;
		}
		case POLY_INST_BIN_EQEQ:
		case POLY_INST_BIN_UNEQ:
		case POLY_INST_BIN_LTEQ:
		case POLY_INST_BIN_GTEQ:
		{
			poly_Value *rval = popvalue(vm);
			poly_Value *lval = popvalue(vm);

			if (rval->type == POLY_VAL_ID)
				rval = getvalue(vm, rval->str);
			if (lval->type == POLY_VAL_ID)
				lval = getvalue(vm, lval->str);

			poly_Value *val = vm->config->alloc(NULL, sizeof(poly_Value));
			val->type = POLY_VAL_BOOL;

			if (lval->type != rval->type)
				val->bool = POLY_FALSE;
			else
			{
				if (lval->type == POLY_VAL_NUM &&
					rval->type == POLY_VAL_NUM)
				{
					if (curcode(vm)->inst == POLY_INST_BIN_EQEQ)
						val->bool = lval->num == rval->num;
					else if (curcode(vm)->inst == POLY_INST_BIN_UNEQ)
						val->bool = lval->num != rval->num;
					else if (curcode(vm)->inst == POLY_INST_BIN_LTEQ)
						val->bool = lval->num <= rval->num;
					else if (curcode(vm)->inst == POLY_INST_BIN_GTEQ)
						val->bool = lval->num >= rval->num;
					else
						throwerr("invalid binary operator");
				}
				else if (lval->type == POLY_VAL_BOOL &&
						rval->type == POLY_VAL_BOOL)
				{
					if (curcode(vm)->inst == POLY_INST_BIN_EQEQ)
						val->bool = lval->bool == rval->bool;
					else if (curcode(vm)->inst == POLY_INST_BIN_UNEQ)
						val->bool = lval->bool != rval->bool;
					else
						throwerr("invalid binary operator");
				}
				else
					throwerr("the operands are illegal");
			}
			
			vm->config->alloc(lval, 0);
			vm->config->alloc(rval, 0);
			pushvalue(vm, val);

			break;
		}
		case POLY_INST_BIN_AND:
		case POLY_INST_BIN_OR:
		{
			poly_Value *rval = popvalue(vm);
			poly_Value *lval = popvalue(vm);

			if (rval->type == POLY_VAL_ID)
				rval = getvalue(vm, rval->str);
			if (lval->type == POLY_VAL_ID)
				lval = getvalue(vm, lval->str);

			poly_Value *val = vm->config->alloc(NULL, sizeof(poly_Value));
			val->type = POLY_VAL_BOOL;

			poly_Boolean lbool, rbool;

			switch (lval->type)
			{
			case POLY_VAL_NULL:
				lbool = POLY_FALSE; break;
			case POLY_VAL_NUM:
				lbool = POLY_TRUE; break;
			case POLY_VAL_BOOL:
				lbool = lval->bool; break;
			default:
				lbool = POLY_TRUE;
			}

			switch (rval->type)
			{
			case POLY_VAL_NULL:
				rbool = POLY_FALSE; break;
			case POLY_VAL_NUM:
				rbool = POLY_TRUE; break;
			case POLY_VAL_BOOL:
				rbool = rval->bool; break;
			default:
				rbool = POLY_TRUE;
			}

			if (curcode(vm)->inst == POLY_INST_BIN_AND)
				val->bool = lbool && rbool;
			else if (curcode(vm)->inst == POLY_INST_BIN_OR)
				val->bool = lbool || rbool;
			else
				throwerr("invalid binary operator");
			
			vm->config->alloc(lval, 0);
			vm->config->alloc(rval, 0);
			pushvalue(vm, val);

			break;
		}
		case POLY_INST_UN_NOT:
		{
			poly_Value *val = popvalue(vm);

			if (val->type == POLY_VAL_ID)
				val = getvalue(vm, val->str);
			
			poly_Value *newval = vm->config->alloc(NULL, sizeof(poly_Value));

			if (val->type == POLY_VAL_BOOL)
			{
				newval->type = POLY_VAL_BOOL;
				newval->bool = !val->bool;
			}
			else
				throwerr("the operand is illegal");
			
			vm->config->alloc(val, 0);
			pushvalue(vm, newval);

			break;
		}
		case POLY_INST_ASSIGN:
		{
			poly_Value *val = popvalue(vm);
			poly_Value *id = popvalue(vm);

			if (id->type == POLY_VAL_ID)
			{
				addlocal(vm, id->str, val);
				vm->config->alloc(id, 0);
			}
			else
				throwerr("identifier expected");
			
			break;
		}
		default:
			break;
		}

		advcode(vm);
	}
}