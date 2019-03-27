/*
 * Copyright (c) 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hx_locl.h"

struct hx_expr *
_hx509_make_expr(enum hx_expr_op op, void *arg1, void *arg2)
{
    struct hx_expr *expr;

    expr = malloc(sizeof(*expr));
    if (expr == NULL)
	return NULL;
    expr->op = op;
    expr->arg1 = arg1;
    expr->arg2 = arg2;

    return expr;
}

static const char *
eval_word(hx509_context context, hx509_env env, struct hx_expr *word)
{
    switch (word->op) {
    case expr_STRING:
	return word->arg1;
    case expr_VAR:
	if (word->arg2 == NULL)
	    return hx509_env_find(context, env, word->arg1);

	env = hx509_env_find_binding(context, env, word->arg1);
	if (env == NULL)
	    return NULL;

	return eval_word(context, env, word->arg2);
    default:
	return NULL;
    }
}

static hx509_env
find_variable(hx509_context context, hx509_env env, struct hx_expr *word)
{
    assert(word->op == expr_VAR);

    if (word->arg2 == NULL)
	return hx509_env_find_binding(context, env, word->arg1);

    env = hx509_env_find_binding(context, env, word->arg1);
    if (env == NULL)
	return NULL;
    return find_variable(context, env, word->arg2);
}

static int
eval_comp(hx509_context context, hx509_env env, struct hx_expr *expr)
{
    switch (expr->op) {
    case comp_NE:
    case comp_EQ:
    case comp_TAILEQ: {
	const char *s1, *s2;
	int ret;

	s1 = eval_word(context, env, expr->arg1);
	s2 = eval_word(context, env, expr->arg2);

	if (s1 == NULL || s2 == NULL)
	    return FALSE;

	if (expr->op == comp_TAILEQ) {
	    size_t len1 = strlen(s1);
	    size_t len2 = strlen(s2);

	    if (len1 < len2)
		return 0;
	    ret = strcmp(s1 + (len1 - len2), s2) == 0;
	} else {
	    ret = strcmp(s1, s2) == 0;
	    if (expr->op == comp_NE)
		ret = !ret;
	}
	return ret;
    }
    case comp_IN: {
	struct hx_expr *subexpr;
	const char *w, *s1;

	w = eval_word(context, env, expr->arg1);

	subexpr = expr->arg2;

	if (subexpr->op == expr_WORDS) {
	    while (subexpr) {
		s1 = eval_word(context, env, subexpr->arg1);
		if (strcmp(w, s1) == 0)
		    return TRUE;
		subexpr = subexpr->arg2;
	    }
	} else if (subexpr->op == expr_VAR) {
	    hx509_env subenv;

	    subenv = find_variable(context, env, subexpr);
	    if (subenv == NULL)
		return FALSE;

	    while (subenv) {
		if (subenv->type != env_string)
		    continue;
		if (strcmp(w, subenv->name) == 0)
		    return TRUE;
		if (strcmp(w, subenv->u.string) == 0)
		    return TRUE;
		subenv = subenv->next;
	    }

	} else
	    _hx509_abort("hx509 eval IN unknown op: %d", (int)subexpr->op);

	return FALSE;
    }
    default:
	_hx509_abort("hx509 eval expr with unknown op: %d", (int)expr->op);
    }
    return FALSE;
}

int
_hx509_expr_eval(hx509_context context, hx509_env env, struct hx_expr *expr)
{
    switch (expr->op) {
    case op_TRUE:
	return 1;
    case op_FALSE:
	return 0;
    case op_NOT:
	return ! _hx509_expr_eval(context, env, expr->arg1);
    case op_AND:
	return _hx509_expr_eval(context, env, expr->arg1) &&
	    _hx509_expr_eval(context, env, expr->arg2);
    case op_OR:
	return _hx509_expr_eval(context, env, expr->arg1) ||
	    _hx509_expr_eval(context, env, expr->arg2);
    case op_COMP:
	return eval_comp(context, env, expr->arg1);
    default:
	_hx509_abort("hx509 eval expr with unknown op: %d", (int)expr->op);
	UNREACHABLE(return 0);
    }
}

void
_hx509_expr_free(struct hx_expr *expr)
{
    switch (expr->op) {
    case expr_STRING:
    case expr_NUMBER:
	free(expr->arg1);
	break;
    case expr_WORDS:
    case expr_FUNCTION:
    case expr_VAR:
	free(expr->arg1);
	if (expr->arg2)
	    _hx509_expr_free(expr->arg2);
	break;
    default:
	if (expr->arg1)
	    _hx509_expr_free(expr->arg1);
	if (expr->arg2)
	    _hx509_expr_free(expr->arg2);
	break;
    }
    free(expr);
}

struct hx_expr *
_hx509_expr_parse(const char *buf)
{
    _hx509_expr_input.buf = buf;
    _hx509_expr_input.length = strlen(buf);
    _hx509_expr_input.offset = 0;
    _hx509_expr_input.expr = NULL;

    if (_hx509_expr_input.error) {
	free(_hx509_expr_input.error);
	_hx509_expr_input.error = NULL;
    }

    yyparse();

    return _hx509_expr_input.expr;
}

void
_hx509_sel_yyerror (const char *s)
{
     if (_hx509_expr_input.error)
         free(_hx509_expr_input.error);

     _hx509_expr_input.error = strdup(s);
}

