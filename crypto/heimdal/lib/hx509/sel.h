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

enum hx_expr_op {
    op_TRUE,
    op_FALSE,
    op_NOT,
    op_AND,
    op_OR,
    op_COMP,

    comp_EQ,
    comp_NE,
    comp_IN,
    comp_TAILEQ,

    expr_NUMBER,
    expr_STRING,
    expr_FUNCTION,
    expr_VAR,
    expr_WORDS
};

struct hx_expr {
    enum hx_expr_op	op;
    void		*arg1;
    void		*arg2;
};

struct hx_expr_input {
    const char *buf;
    size_t length;
    size_t offset;
    struct hx_expr *expr;
    char *error;
};

extern struct hx_expr_input _hx509_expr_input;

#define yyparse _hx509_sel_yyparse
#define yylex   _hx509_sel_yylex
#define yyerror _hx509_sel_yyerror
#define yylval  _hx509_sel_yylval
#define yychar  _hx509_sel_yychar
#define yydebug _hx509_sel_yydebug
#define yynerrs _hx509_sel_yynerrs
#define yywrap  _hx509_sel_yywrap

int  _hx509_sel_yyparse(void);
int  _hx509_sel_yylex(void);
void _hx509_sel_yyerror(const char *);

