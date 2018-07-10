/*
 * Arithmetic code ripped out of ash shell for code sharing.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Original BSD copyright notice is retained at the end of this file.
 *
 * Copyright (c) 1989, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 1997-2005 Herbert Xu <herbert@gondor.apana.org.au>
 * was re-ported from NetBSD and debianized.
 *
 * rewrite arith.y to micro stack based cryptic algorithm by
 * Copyright (c) 2001 Aaron Lehmann <aaronl@vitelus.com>
 *
 * Modified by Paul Mundt <lethal@linux-sh.org> (c) 2004 to support
 * dynamic variables.
 *
 * Modified by Vladimir Oleynik <dzo@simtreas.ru> (c) 2001-2005 to be
 * used in busybox and size optimizations,
 * rewrote arith (see notes to this), added locale support,
 * rewrote dynamic variables.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Copyright (c) 2001 Aaron Lehmann <aaronl@vitelus.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* This is my infix parser/evaluator. It is optimized for size, intended
 * as a replacement for yacc-based parsers. However, it may well be faster
 * than a comparable parser written in yacc. The supported operators are
 * listed in #defines below. Parens, order of operations, and error handling
 * are supported. This code is thread safe. The exact expression format should
 * be that which POSIX specifies for shells.
 *
 * The code uses a simple two-stack algorithm. See
 * http://www.onthenet.com.au/~grahamis/int2008/week02/lect02.html
 * for a detailed explanation of the infix-to-postfix algorithm on which
 * this is based (this code differs in that it applies operators immediately
 * to the stack instead of adding them to a queue to end up with an
 * expression).
 */

/*
 * Aug 24, 2001              Manuel Novoa III
 *
 * Reduced the generated code size by about 30% (i386) and fixed several bugs.
 *
 * 1) In arith_apply():
 *    a) Cached values of *numptr and &(numptr[-1]).
 *    b) Removed redundant test for zero denominator.
 *
 * 2) In arith():
 *    a) Eliminated redundant code for processing operator tokens by moving
 *       to a table-based implementation.  Also folded handling of parens
 *       into the table.
 *    b) Combined all 3 loops which called arith_apply to reduce generated
 *       code size at the cost of speed.
 *
 * 3) The following expressions were treated as valid by the original code:
 *       1()  ,    0!  ,    1 ( *3 )   .
 *    These bugs have been fixed by internally enclosing the expression in
 *    parens and then checking that all binary ops and right parens are
 *    preceded by a valid expression (NUM_TOKEN).
 *
 * Note: It may be desirable to replace Aaron's test for whitespace with
 * ctype's isspace() if it is used by another busybox applet or if additional
 * whitespace chars should be considered.  Look below the "#include"s for a
 * precompiler test.
 */
/*
 * Aug 26, 2001              Manuel Novoa III
 *
 * Return 0 for null expressions.  Pointed out by Vladimir Oleynik.
 *
 * Merge in Aaron's comments previously posted to the busybox list,
 * modified slightly to take account of my changes to the code.
 *
 */
/*
 *  (C) 2003 Vladimir Oleynik <dzo@simtreas.ru>
 *
 * - allow access to variable,
 *   use recursive value indirection: c="2*2"; a="c"; echo $((a+=2)) produce 6
 * - implement assign syntax (VAR=expr, +=, *= etc)
 * - implement exponentiation (** operator)
 * - implement comma separated - expr, expr
 * - implement ++expr --expr expr++ expr--
 * - implement expr ? expr : expr (but second expr is always calculated)
 * - allow hexadecimal and octal numbers
 * - restore lost XOR operator
 * - protect $((num num)) as true zero expr (Manuel's error)
 * - always use special isspace(), see comment from bash ;-)
 */
#include "libbb.h"
#include "math.h"

#define lookupvar (math_state->lookupvar)
#define setvar    (math_state->setvar   )
//#define endofname (math_state->endofname)

typedef unsigned char operator;

/* An operator's token id is a bit of a bitfield. The lower 5 bits are the
 * precedence, and 3 high bits are an ID unique across operators of that
 * precedence. The ID portion is so that multiple operators can have the
 * same precedence, ensuring that the leftmost one is evaluated first.
 * Consider * and /
 */
#define tok_decl(prec,id)       (((id)<<5) | (prec))
#define PREC(op)                ((op) & 0x1F)

#define TOK_LPAREN              tok_decl(0,0)

#define TOK_COMMA               tok_decl(1,0)

/* All assignments are right associative and have the same precedence,
 * but there are 11 of them, which doesn't fit into 3 bits for unique id.
 * Abusing another precedence level:
 */
#define TOK_ASSIGN              tok_decl(2,0)
#define TOK_AND_ASSIGN          tok_decl(2,1)
#define TOK_OR_ASSIGN           tok_decl(2,2)
#define TOK_XOR_ASSIGN          tok_decl(2,3)
#define TOK_PLUS_ASSIGN         tok_decl(2,4)
#define TOK_MINUS_ASSIGN        tok_decl(2,5)
#define TOK_LSHIFT_ASSIGN       tok_decl(2,6)
#define TOK_RSHIFT_ASSIGN       tok_decl(2,7)

#define TOK_MUL_ASSIGN          tok_decl(3,0)
#define TOK_DIV_ASSIGN          tok_decl(3,1)
#define TOK_REM_ASSIGN          tok_decl(3,2)

#define fix_assignment_prec(prec) do { if (prec == 3) prec = 2; } while (0)

/* Ternary conditional operator is right associative too */
#define TOK_CONDITIONAL         tok_decl(4,0)
#define TOK_CONDITIONAL_SEP     tok_decl(4,1)

#define TOK_OR                  tok_decl(5,0)

#define TOK_AND                 tok_decl(6,0)

#define TOK_BOR                 tok_decl(7,0)

#define TOK_BXOR                tok_decl(8,0)

#define TOK_BAND                tok_decl(9,0)

#define TOK_EQ                  tok_decl(10,0)
#define TOK_NE                  tok_decl(10,1)

#define TOK_LT                  tok_decl(11,0)
#define TOK_GT                  tok_decl(11,1)
#define TOK_GE                  tok_decl(11,2)
#define TOK_LE                  tok_decl(11,3)

#define TOK_LSHIFT              tok_decl(12,0)
#define TOK_RSHIFT              tok_decl(12,1)

#define TOK_ADD                 tok_decl(13,0)
#define TOK_SUB                 tok_decl(13,1)

#define TOK_MUL                 tok_decl(14,0)
#define TOK_DIV                 tok_decl(14,1)
#define TOK_REM                 tok_decl(14,2)

/* Exponent is right associative */
#define TOK_EXPONENT            tok_decl(15,1)

/* Unary operators */
#define UNARYPREC               16
#define TOK_BNOT                tok_decl(UNARYPREC,0)
#define TOK_NOT                 tok_decl(UNARYPREC,1)

#define TOK_UMINUS              tok_decl(UNARYPREC+1,0)
#define TOK_UPLUS               tok_decl(UNARYPREC+1,1)

#define PREC_PRE                (UNARYPREC+2)

#define TOK_PRE_INC             tok_decl(PREC_PRE, 0)
#define TOK_PRE_DEC             tok_decl(PREC_PRE, 1)

#define PREC_POST               (UNARYPREC+3)

#define TOK_POST_INC            tok_decl(PREC_POST, 0)
#define TOK_POST_DEC            tok_decl(PREC_POST, 1)

#define SPEC_PREC               (UNARYPREC+4)

#define TOK_NUM                 tok_decl(SPEC_PREC, 0)
#define TOK_RPAREN              tok_decl(SPEC_PREC, 1)

static int
is_assign_op(operator op)
{
	operator prec = PREC(op);
	fix_assignment_prec(prec);
	return prec == PREC(TOK_ASSIGN)
	|| prec == PREC_PRE
	|| prec == PREC_POST;
}

static int
is_right_associative(operator prec)
{
	return prec == PREC(TOK_ASSIGN)
	|| prec == PREC(TOK_EXPONENT)
	|| prec == PREC(TOK_CONDITIONAL);
}


typedef struct {
	arith_t val;
	/* We acquire second_val only when "expr1 : expr2" part
	 * of ternary ?: op is evaluated.
	 * We treat ?: as two binary ops: (expr ? (expr1 : expr2)).
	 * ':' produces a new value which has two parts, val and second_val;
	 * then '?' selects one of them based on its left side.
	 */
	arith_t second_val;
	char second_val_present;
	/* If NULL then it's just a number, else it's a named variable */
	char *var;
} var_or_num_t;

typedef struct remembered_name {
	struct remembered_name *next;
	const char *var;
} remembered_name;


static arith_t FAST_FUNC
evaluate_string(arith_state_t *math_state, const char *expr);

static const char*
arith_lookup_val(arith_state_t *math_state, var_or_num_t *t)
{
	if (t->var) {
		const char *p = lookupvar(t->var);
		if (p) {
			remembered_name *cur;
			remembered_name cur_save;

			/* did we already see this name?
			 * testcase: a=b; b=a; echo $((a))
			 */
			for (cur = math_state->list_of_recursed_names; cur; cur = cur->next) {
				if (strcmp(cur->var, t->var) == 0) {
					/* Yes */
					return "expression recursion loop detected";
				}
			}

			/* push current var name */
			cur = math_state->list_of_recursed_names;
			cur_save.var = t->var;
			cur_save.next = cur;
			math_state->list_of_recursed_names = &cur_save;

			/* recursively evaluate p as expression */
			t->val = evaluate_string(math_state, p);

			/* pop current var name */
			math_state->list_of_recursed_names = cur;

			return math_state->errmsg;
		}
		/* treat undefined var as 0 */
		t->val = 0;
	}
	return 0;
}

/* "Applying" a token means performing it on the top elements on the integer
 * stack. For an unary operator it will only change the top element, but a
 * binary operator will pop two arguments and push the result */
static NOINLINE const char*
arith_apply(arith_state_t *math_state, operator op, var_or_num_t *numstack, var_or_num_t **numstackptr)
{
#define NUMPTR (*numstackptr)

	var_or_num_t *top_of_stack;
	arith_t rez;
	const char *err;

	/* There is no operator that can work without arguments */
	if (NUMPTR == numstack)
		goto err;

	top_of_stack = NUMPTR - 1;

	/* Resolve name to value, if needed */
	err = arith_lookup_val(math_state, top_of_stack);
	if (err)
		return err;

	rez = top_of_stack->val;
	if (op == TOK_UMINUS)
		rez = -rez;
	else if (op == TOK_NOT)
		rez = !rez;
	else if (op == TOK_BNOT)
		rez = ~rez;
	else if (op == TOK_POST_INC || op == TOK_PRE_INC)
		rez++;
	else if (op == TOK_POST_DEC || op == TOK_PRE_DEC)
		rez--;
	else if (op != TOK_UPLUS) {
		/* Binary operators */
		arith_t right_side_val;
		char bad_second_val;

		/* Binary operators need two arguments */
		if (top_of_stack == numstack)
			goto err;
		/* ...and they pop one */
		NUMPTR = top_of_stack; /* this decrements NUMPTR */

		bad_second_val = top_of_stack->second_val_present;
		if (op == TOK_CONDITIONAL) { /* ? operation */
			/* Make next if (...) protect against
			 * $((expr1 ? expr2)) - that is, missing ": expr" */
			bad_second_val = !bad_second_val;
		}
		if (bad_second_val) {
			/* Protect against $((expr <not_?_op> expr1 : expr2)) */
			return "malformed ?: operator";
		}

		top_of_stack--; /* now points to left side */

		if (op != TOK_ASSIGN) {
			/* Resolve left side value (unless the op is '=') */
			err = arith_lookup_val(math_state, top_of_stack);
			if (err)
				return err;
		}

		right_side_val = rez;
		rez = top_of_stack->val;
		if (op == TOK_CONDITIONAL) /* ? operation */
			rez = (rez ? right_side_val : top_of_stack[1].second_val);
		else if (op == TOK_CONDITIONAL_SEP) { /* : operation */
			if (top_of_stack == numstack) {
				/* Protect against $((expr : expr)) */
				return "malformed ?: operator";
			}
			top_of_stack->second_val_present = op;
			top_of_stack->second_val = right_side_val;
		}
		else if (op == TOK_BOR || op == TOK_OR_ASSIGN)
			rez |= right_side_val;
		else if (op == TOK_OR)
			rez = right_side_val || rez;
		else if (op == TOK_BAND || op == TOK_AND_ASSIGN)
			rez &= right_side_val;
		else if (op == TOK_BXOR || op == TOK_XOR_ASSIGN)
			rez ^= right_side_val;
		else if (op == TOK_AND)
			rez = rez && right_side_val;
		else if (op == TOK_EQ)
			rez = (rez == right_side_val);
		else if (op == TOK_NE)
			rez = (rez != right_side_val);
		else if (op == TOK_GE)
			rez = (rez >= right_side_val);
		else if (op == TOK_RSHIFT || op == TOK_RSHIFT_ASSIGN)
			rez >>= right_side_val;
		else if (op == TOK_LSHIFT || op == TOK_LSHIFT_ASSIGN)
			rez <<= right_side_val;
		else if (op == TOK_GT)
			rez = (rez > right_side_val);
		else if (op == TOK_LT)
			rez = (rez < right_side_val);
		else if (op == TOK_LE)
			rez = (rez <= right_side_val);
		else if (op == TOK_MUL || op == TOK_MUL_ASSIGN)
			rez *= right_side_val;
		else if (op == TOK_ADD || op == TOK_PLUS_ASSIGN)
			rez += right_side_val;
		else if (op == TOK_SUB || op == TOK_MINUS_ASSIGN)
			rez -= right_side_val;
		else if (op == TOK_ASSIGN || op == TOK_COMMA)
			rez = right_side_val;
		else if (op == TOK_EXPONENT) {
			arith_t c;
			if (right_side_val < 0)
				return "exponent less than 0";
			c = 1;
			while (--right_side_val >= 0)
				c *= rez;
			rez = c;
		}
		else if (right_side_val == 0)
			return "divide by zero";
		else if (op == TOK_DIV || op == TOK_DIV_ASSIGN
		      || op == TOK_REM || op == TOK_REM_ASSIGN) {
			/*
			 * bash 4.2.45 x86 64bit: SEGV on 'echo $((2**63 / -1))'
			 *
			 * MAX_NEGATIVE_INT / -1 = MAX_POSITIVE_INT+1
			 * and thus is not representable.
			 * Some CPUs segfault trying such op.
			 * Others overflow MAX_POSITIVE_INT+1 to
			 * MAX_NEGATIVE_INT (0x7fff+1 = 0x8000).
			 * Make sure to at least not SEGV here:
			 */
			if (right_side_val == -1
			 && rez << 1 == 0 /* MAX_NEGATIVE_INT or 0 */
			) {
				right_side_val = 1;
			}
			if (op == TOK_DIV || op == TOK_DIV_ASSIGN)
				rez /= right_side_val;
			else {
				rez %= right_side_val;
			}
		}
	}

	if (is_assign_op(op)) {
		char buf[sizeof(arith_t)*3 + 2];

		if (top_of_stack->var == NULL) {
			/* Hmm, 1=2 ? */
//TODO: actually, bash allows ++7 but for some reason it evals to 7, not 8
			goto err;
		}
		/* Save to shell variable */
		sprintf(buf, ARITH_FMT, rez);
		setvar(top_of_stack->var, buf);
		/* After saving, make previous value for v++ or v-- */
		if (op == TOK_POST_INC)
			rez--;
		else if (op == TOK_POST_DEC)
			rez++;
	}

	top_of_stack->val = rez;
	/* Erase var name, it is just a number now */
	top_of_stack->var = NULL;
	return NULL;
 err:
	return "arithmetic syntax error";
#undef NUMPTR
}

/* longest must be first */
static const char op_tokens[] ALIGN1 = {
	'<','<','=',0, TOK_LSHIFT_ASSIGN,
	'>','>','=',0, TOK_RSHIFT_ASSIGN,
	'<','<',    0, TOK_LSHIFT,
	'>','>',    0, TOK_RSHIFT,
	'|','|',    0, TOK_OR,
	'&','&',    0, TOK_AND,
	'!','=',    0, TOK_NE,
	'<','=',    0, TOK_LE,
	'>','=',    0, TOK_GE,
	'=','=',    0, TOK_EQ,
	'|','=',    0, TOK_OR_ASSIGN,
	'&','=',    0, TOK_AND_ASSIGN,
	'*','=',    0, TOK_MUL_ASSIGN,
	'/','=',    0, TOK_DIV_ASSIGN,
	'%','=',    0, TOK_REM_ASSIGN,
	'+','=',    0, TOK_PLUS_ASSIGN,
	'-','=',    0, TOK_MINUS_ASSIGN,
	'-','-',    0, TOK_POST_DEC,
	'^','=',    0, TOK_XOR_ASSIGN,
	'+','+',    0, TOK_POST_INC,
	'*','*',    0, TOK_EXPONENT,
	'!',        0, TOK_NOT,
	'<',        0, TOK_LT,
	'>',        0, TOK_GT,
	'=',        0, TOK_ASSIGN,
	'|',        0, TOK_BOR,
	'&',        0, TOK_BAND,
	'*',        0, TOK_MUL,
	'/',        0, TOK_DIV,
	'%',        0, TOK_REM,
	'+',        0, TOK_ADD,
	'-',        0, TOK_SUB,
	'^',        0, TOK_BXOR,
	/* uniq */
	'~',        0, TOK_BNOT,
	',',        0, TOK_COMMA,
	'?',        0, TOK_CONDITIONAL,
	':',        0, TOK_CONDITIONAL_SEP,
	')',        0, TOK_RPAREN,
	'(',        0, TOK_LPAREN,
	0
};
#define ptr_to_rparen (&op_tokens[sizeof(op_tokens)-7])

static arith_t FAST_FUNC
evaluate_string(arith_state_t *math_state, const char *expr)
{
	operator lasttok;
	const char *errmsg;
	const char *start_expr = expr = skip_whitespace(expr);
	unsigned expr_len = strlen(expr) + 2;
	/* Stack of integers */
	/* The proof that there can be no more than strlen(startbuf)/2+1
	 * integers in any given correct or incorrect expression
	 * is left as an exercise to the reader. */
	var_or_num_t *const numstack = alloca((expr_len / 2) * sizeof(numstack[0]));
	var_or_num_t *numstackptr = numstack;
	/* Stack of operator tokens */
	operator *const stack = alloca(expr_len * sizeof(stack[0]));
	operator *stackptr = stack;

	/* Start with a left paren */
	*stackptr++ = lasttok = TOK_LPAREN;
	errmsg = NULL;

	while (1) {
		const char *p;
		operator op;
		operator prec;
		char arithval;

		expr = skip_whitespace(expr);
		arithval = *expr;
		if (arithval == '\0') {
			if (expr == start_expr) {
				/* Null expression */
				numstack->val = 0;
				goto ret;
			}

			/* This is only reached after all tokens have been extracted from the
			 * input stream. If there are still tokens on the operator stack, they
			 * are to be applied in order. At the end, there should be a final
			 * result on the integer stack */

			if (expr != ptr_to_rparen + 1) {
				/* If we haven't done so already,
				 * append a closing right paren
				 * and let the loop process it */
				expr = ptr_to_rparen;
				continue;
			}
			/* At this point, we're done with the expression */
			if (numstackptr != numstack + 1) {
				/* ...but if there isn't, it's bad */
				goto err;
			}
			if (numstack->var) {
				/* expression is $((var)) only, lookup now */
				errmsg = arith_lookup_val(math_state, numstack);
			}
			goto ret;
		}

		p = endofname(expr);
		if (p != expr) {
			/* Name */
			size_t var_name_size = (p-expr) + 1;  /* +1 for NUL */
			numstackptr->var = alloca(var_name_size);
			safe_strncpy(numstackptr->var, expr, var_name_size);
			expr = p;
 num:
			numstackptr->second_val_present = 0;
			numstackptr++;
			lasttok = TOK_NUM;
			continue;
		}

		if (isdigit(arithval)) {
			/* Number */
			numstackptr->var = NULL;
			errno = 0;
			numstackptr->val = strto_arith_t(expr, (char**) &expr, 0);
			if (errno)
				numstackptr->val = 0; /* bash compat */
			goto num;
		}

		/* Should be an operator */

		/* Special case: NUM-- and NUM++ are not recognized if NUM
		 * is a literal number, not a variable. IOW:
		 * "a+++v" is a++ + v.
		 * "7+++v" is 7 + ++v, not 7++ + v.
		 */
		if (lasttok == TOK_NUM && !numstackptr[-1].var /* number literal */
		 && (expr[0] == '+' || expr[0] == '-')
		 && (expr[1] == expr[0])
		) {
			//bb_error_msg("special %c%c", expr[0], expr[0]);
			op = (expr[0] == '+' ? TOK_ADD : TOK_SUB);
			expr += 1;
			goto tok_found1;
		}

		p = op_tokens;
		while (1) {
			/* Compare expr to current op_tokens[] element */
			const char *e = expr;
			while (1) {
				if (*p == '\0') {
					/* Match: operator is found */
					expr = e;
					goto tok_found;
				}
				if (*p != *e)
					break;
				p++;
				e++;
			}
			/* No match, go to next element of op_tokens[] */
			while (*p)
				p++;
			p += 2; /* skip NUL and TOK_foo bytes */
			if (*p == '\0') {
				/* No next element, operator not found */
				//math_state->syntax_error_at = expr;
				goto err;
			}
		}
 tok_found:
		op = p[1]; /* fetch TOK_foo value */
 tok_found1:
		/* NB: expr now points past the operator */

		/* post grammar: a++ reduce to num */
		if (lasttok == TOK_POST_INC || lasttok == TOK_POST_DEC)
			lasttok = TOK_NUM;

		/* Plus and minus are binary (not unary) _only_ if the last
		 * token was a number, or a right paren (which pretends to be
		 * a number, since it evaluates to one). Think about it.
		 * It makes sense. */
		if (lasttok != TOK_NUM) {
			switch (op) {
			case TOK_ADD:
				op = TOK_UPLUS;
				break;
			case TOK_SUB:
				op = TOK_UMINUS;
				break;
			case TOK_POST_INC:
				op = TOK_PRE_INC;
				break;
			case TOK_POST_DEC:
				op = TOK_PRE_DEC;
				break;
			}
		}
		/* We don't want an unary operator to cause recursive descent on the
		 * stack, because there can be many in a row and it could cause an
		 * operator to be evaluated before its argument is pushed onto the
		 * integer stack.
		 * But for binary operators, "apply" everything on the operator
		 * stack until we find an operator with a lesser priority than the
		 * one we have just extracted. If op is right-associative,
		 * then stop "applying" on the equal priority too.
		 * Left paren is given the lowest priority so it will never be
		 * "applied" in this way.
		 */
		prec = PREC(op);
		if ((prec > 0 && prec < UNARYPREC) || prec == SPEC_PREC) {
			/* not left paren or unary */
			if (lasttok != TOK_NUM) {
				/* binary op must be preceded by a num */
				goto err;
			}
			while (stackptr != stack) {
				operator prev_op = *--stackptr;
				if (op == TOK_RPAREN) {
					/* The algorithm employed here is simple: while we don't
					 * hit an open paren nor the bottom of the stack, pop
					 * tokens and apply them */
					if (prev_op == TOK_LPAREN) {
						/* Any operator directly after a
						 * close paren should consider itself binary */
						lasttok = TOK_NUM;
						goto next;
					}
				} else {
					operator prev_prec = PREC(prev_op);
					fix_assignment_prec(prec);
					fix_assignment_prec(prev_prec);
					if (prev_prec < prec
					 || (prev_prec == prec && is_right_associative(prec))
					) {
						stackptr++;
						break;
					}
				}
				errmsg = arith_apply(math_state, prev_op, numstack, &numstackptr);
				if (errmsg)
					goto err_with_custom_msg;
			}
			if (op == TOK_RPAREN)
				goto err;
		}

		/* Push this operator to the stack and remember it */
		*stackptr++ = lasttok = op;
 next: ;
	} /* while (1) */

 err:
	errmsg = "arithmetic syntax error";
 err_with_custom_msg:
	numstack->val = -1;
 ret:
	math_state->errmsg = errmsg;
	return numstack->val;
}

arith_t FAST_FUNC
arith(arith_state_t *math_state, const char *expr)
{
	math_state->errmsg = NULL;
	math_state->list_of_recursed_names = NULL;
	return evaluate_string(math_state, expr);
}

/*
 * Copyright (c) 1989, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
