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

%{
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <hx_locl.h>


%}

%union {
    char *string;
    struct hx_expr *expr;
}

%token kw_TRUE
%token kw_FALSE
%token kw_AND
%token kw_OR
%token kw_IN
%token kw_TAILMATCH

%type <expr> expr
%type <expr> comp
%type <expr> word words
%type <expr> number
%type <expr> string
%type <expr> function
%type <expr> variable variables

%token <string> NUMBER
%token <string> STRING
%token <string> IDENTIFIER

%start start

%%

start:	expr			{ _hx509_expr_input.expr = $1; }

expr	: kw_TRUE		{ $$ = _hx509_make_expr(op_TRUE, NULL, NULL); }
	| kw_FALSE		{ $$ = _hx509_make_expr(op_FALSE, NULL, NULL); }
	| '!' expr		{ $$ = _hx509_make_expr(op_NOT, $2, NULL); }
	| expr kw_AND expr	{ $$ = _hx509_make_expr(op_AND, $1, $3); }
	| expr kw_OR expr	{ $$ = _hx509_make_expr(op_OR, $1, $3); }
	| '(' expr ')'		{ $$ = $2; }
	| comp			{ $$ = _hx509_make_expr(op_COMP, $1, NULL); }
	;

words	: word			{ $$ = _hx509_make_expr(expr_WORDS, $1, NULL); }
	| word ',' words	{ $$ = _hx509_make_expr(expr_WORDS, $1, $3); }
	;

comp	: word '=' '=' word	{ $$ = _hx509_make_expr(comp_EQ, $1, $4); }
	| word '!' '=' word	{ $$ = _hx509_make_expr(comp_NE, $1, $4); }
	| word kw_TAILMATCH word { $$ = _hx509_make_expr(comp_TAILEQ, $1, $3); }
	| word kw_IN '(' words ')' { $$ = _hx509_make_expr(comp_IN, $1, $4); }
	| word kw_IN variable	{ $$ = _hx509_make_expr(comp_IN, $1, $3); }
	;

word	: number		{ $$ = $1; }
	| string		{ $$ = $1; }
	| function		{ $$ = $1; }
	| variable		{ $$ = $1; }
	;

number	: NUMBER	{ $$ = _hx509_make_expr(expr_NUMBER, $1, NULL); };
string	: STRING	{ $$ = _hx509_make_expr(expr_STRING, $1, NULL); };

function: IDENTIFIER '(' words ')' {
			$$ = _hx509_make_expr(expr_FUNCTION, $1, $3); }
	;
variable: '%' '{' variables '}'	{ $$ = $3; }
	;

variables: IDENTIFIER '.' variables 	{
			$$ = _hx509_make_expr(expr_VAR, $1, $3); }
	| IDENTIFIER			{
			$$ = _hx509_make_expr(expr_VAR, $1, NULL); }
	;
