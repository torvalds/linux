%{
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#include <dt_impl.h>
#include <dt_grammar.h>
#include <dt_parser.h>
#include <dt_string.h>

/*
 * We need to undefine lex's input and unput macros so that references to these
 * call the functions provided at the end of this source file.
 */
#ifdef illumos
#undef input
#undef unput
#else
/*
 * Define YY_INPUT for flex since input() can't be re-defined.
 */
#define YY_INPUT(buf,result,max_size) \
	if (yypcb->pcb_fileptr != NULL) { \
		if (((result = fread(buf, 1, max_size, yypcb->pcb_fileptr)) == 0) \
		    && ferror(yypcb->pcb_fileptr)) \
			longjmp(yypcb->pcb_jmpbuf, EDT_FIO); \
	} else { \
		int n; \
		for (n = 0; n < max_size && \
		    yypcb->pcb_strptr < yypcb->pcb_string + yypcb->pcb_strlen; n++) \
			buf[n] = *yypcb->pcb_strptr++; \
		result = n; \
	}
/*
 * Do not EOF let tokens to be put back. This does not work with flex.
 * On the other hand, leaving current buffer in same state it was when
 * last EOF was received guarantees that input() will keep returning EOF
 * for all subsequent invocations, which is the effect desired.
 */
#undef  unput
#define unput(c)					\
	do {						\
		int _c = c;				\
		if (_c != EOF)				\
			yyunput(_c, yytext_ptr);	\
	} while(0)
#endif

static int id_or_type(const char *);
#ifdef illumos
static int input(void);
static void unput(int);
#endif

/*
 * We first define a set of labeled states for use in the D lexer and then a
 * set of regular expressions to simplify things below. The lexer states are:
 *
 * S0 - D program clause and expression lexing
 * S1 - D comments (i.e. skip everything until end of comment)
 * S2 - D program outer scope (probe specifiers and declarations)
 * S3 - D control line parsing (i.e. after ^# is seen but before \n)
 * S4 - D control line scan (locate control directives only and invoke S3)
 */
%}

%e 1500		/* maximum nodes */
%p 4900		/* maximum positions */
%n 600		/* maximum states */
%a 3000		/* maximum transitions */

%s S0 S1 S2 S3 S4

RGX_AGG		"@"[a-zA-Z_][0-9a-zA-Z_]*
RGX_PSPEC	[-$:a-zA-Z_.?*\\\[\]!][-$:0-9a-zA-Z_.`?*\\\[\]!]*
RGX_ALTIDENT	[a-zA-Z_][0-9a-zA-Z_]*
RGX_LMID	LM[0-9a-fA-F]+`
RGX_MOD_IDENT	[a-zA-Z_`][0-9a-z.A-Z_`]*`
RGX_IDENT	[a-zA-Z_`][0-9a-zA-Z_`]*
RGX_INT		([0-9]+|0[xX][0-9A-Fa-f]+)[uU]?[lL]?[lL]?
RGX_FP		([0-9]+("."?)[0-9]*|"."[0-9]+)((e|E)("+"|-)?[0-9]+)?[fFlL]?
RGX_WS		[\f\n\r\t\v ]
RGX_STR		([^"\\\n]|\\[^"\n]|\\\")*
RGX_CHR		([^'\\\n]|\\[^'\n]|\\')*
RGX_INTERP	^[\f\t\v ]*#!.*
RGX_CTL		^[\f\t\v ]*#

%%

%{

/*
 * We insert a special prologue into yylex() itself: if the pcb contains a
 * context token, we return that prior to running the normal lexer.  This
 * allows libdtrace to force yacc into one of our three parsing contexts: D
 * expression (DT_CTX_DEXPR), D program (DT_CTX_DPROG) or D type (DT_CTX_DTYPE).
 * Once the token is returned, we clear it so this only happens once.
 */
if (yypcb->pcb_token != 0) {
	int tok = yypcb->pcb_token;
	yypcb->pcb_token = 0;
	return (tok);
}

%}

<S0>auto	return (DT_KEY_AUTO);
<S0>break	return (DT_KEY_BREAK);
<S0>case	return (DT_KEY_CASE);
<S0>char	return (DT_KEY_CHAR);
<S0>const	return (DT_KEY_CONST);
<S0>continue	return (DT_KEY_CONTINUE);
<S0>counter	return (DT_KEY_COUNTER);
<S0>default	return (DT_KEY_DEFAULT);
<S0>do		return (DT_KEY_DO);
<S0>double	return (DT_KEY_DOUBLE);
<S0>else	return (DT_KEY_ELSE);
<S0>enum	return (DT_KEY_ENUM);
<S0>extern	return (DT_KEY_EXTERN);
<S0>float	return (DT_KEY_FLOAT);
<S0>for		return (DT_KEY_FOR);
<S0>goto	return (DT_KEY_GOTO);
<S0>if		return (DT_KEY_IF);
<S0>import	return (DT_KEY_IMPORT);
<S0>inline	return (DT_KEY_INLINE);
<S0>int		return (DT_KEY_INT);
<S0>long	return (DT_KEY_LONG);
<S0>offsetof	return (DT_TOK_OFFSETOF);
<S0>probe	return (DT_KEY_PROBE);
<S0>provider	return (DT_KEY_PROVIDER);
<S0>register	return (DT_KEY_REGISTER);
<S0>restrict	return (DT_KEY_RESTRICT);
<S0>return	return (DT_KEY_RETURN);
<S0>self	return (DT_KEY_SELF);
<S0>short	return (DT_KEY_SHORT);
<S0>signed	return (DT_KEY_SIGNED);
<S0>sizeof	return (DT_TOK_SIZEOF);
<S0>static	return (DT_KEY_STATIC);
<S0>string	return (DT_KEY_STRING);
<S0>stringof	return (DT_TOK_STRINGOF);
<S0>struct	return (DT_KEY_STRUCT);
<S0>switch	return (DT_KEY_SWITCH);
<S0>this	return (DT_KEY_THIS);
<S0>translator	return (DT_KEY_XLATOR);
<S0>typedef	return (DT_KEY_TYPEDEF);
<S0>union	return (DT_KEY_UNION);
<S0>unsigned	return (DT_KEY_UNSIGNED);
<S0>userland	return (DT_KEY_USERLAND);
<S0>void	return (DT_KEY_VOID);
<S0>volatile	return (DT_KEY_VOLATILE);
<S0>while	return (DT_KEY_WHILE);
<S0>xlate	return (DT_TOK_XLATE);

<S2>auto	{ yybegin(YYS_EXPR);	return (DT_KEY_AUTO); }
<S2>char	{ yybegin(YYS_EXPR);	return (DT_KEY_CHAR); }
<S2>const	{ yybegin(YYS_EXPR);	return (DT_KEY_CONST); }
<S2>counter	{ yybegin(YYS_DEFINE);	return (DT_KEY_COUNTER); }
<S2>double	{ yybegin(YYS_EXPR);	return (DT_KEY_DOUBLE); }
<S2>enum	{ yybegin(YYS_EXPR);	return (DT_KEY_ENUM); }
<S2>extern	{ yybegin(YYS_EXPR);	return (DT_KEY_EXTERN); }
<S2>float	{ yybegin(YYS_EXPR);	return (DT_KEY_FLOAT); }
<S2>import	{ yybegin(YYS_EXPR);	return (DT_KEY_IMPORT); }
<S2>inline	{ yybegin(YYS_DEFINE);	return (DT_KEY_INLINE); }
<S2>int		{ yybegin(YYS_EXPR);	return (DT_KEY_INT); }
<S2>long	{ yybegin(YYS_EXPR);	return (DT_KEY_LONG); }
<S2>provider	{ yybegin(YYS_DEFINE);	return (DT_KEY_PROVIDER); }
<S2>register	{ yybegin(YYS_EXPR);	return (DT_KEY_REGISTER); }
<S2>restrict	{ yybegin(YYS_EXPR);	return (DT_KEY_RESTRICT); }
<S2>self	{ yybegin(YYS_EXPR);	return (DT_KEY_SELF); }
<S2>short	{ yybegin(YYS_EXPR);	return (DT_KEY_SHORT); }
<S2>signed	{ yybegin(YYS_EXPR);	return (DT_KEY_SIGNED); }
<S2>static	{ yybegin(YYS_EXPR);	return (DT_KEY_STATIC); }
<S2>string	{ yybegin(YYS_EXPR);	return (DT_KEY_STRING); }
<S2>struct	{ yybegin(YYS_EXPR);	return (DT_KEY_STRUCT); }
<S2>this	{ yybegin(YYS_EXPR);	return (DT_KEY_THIS); }
<S2>translator	{ yybegin(YYS_DEFINE);	return (DT_KEY_XLATOR); }
<S2>typedef	{ yybegin(YYS_EXPR);	return (DT_KEY_TYPEDEF); }
<S2>union	{ yybegin(YYS_EXPR);	return (DT_KEY_UNION); }
<S2>unsigned	{ yybegin(YYS_EXPR);	return (DT_KEY_UNSIGNED); }
<S2>void	{ yybegin(YYS_EXPR);	return (DT_KEY_VOID); }
<S2>volatile	{ yybegin(YYS_EXPR);	return (DT_KEY_VOLATILE); }

<S0>"$$"[0-9]+	{
			int i = atoi(yytext + 2);
			char *v = "";

			/*
			 * A macro argument reference substitutes the text of
			 * an argument in place of the current token.  When we
			 * see $$<d> we fetch the saved string from pcb_sargv
			 * (or use the default argument if the option has been
			 * set and the argument hasn't been specified) and
			 * return a token corresponding to this string.
			 */
			if (i < 0 || (i >= yypcb->pcb_sargc &&
			    !(yypcb->pcb_cflags & DTRACE_C_DEFARG))) {
				xyerror(D_MACRO_UNDEF, "macro argument %s is "
				    "not defined\n", yytext);
			}

			if (i < yypcb->pcb_sargc) {
				v = yypcb->pcb_sargv[i]; /* get val from pcb */
				yypcb->pcb_sflagv[i] |= DT_IDFLG_REF;
			}

			if ((yylval.l_str = strdup(v)) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			(void) stresc2chr(yylval.l_str);
			return (DT_TOK_STRING);
		}

<S0>"$"[0-9]+	{
			int i = atoi(yytext + 1);
			char *p, *v = "0";

			/*
			 * A macro argument reference substitutes the text of
			 * one identifier or integer pattern for another.  When
			 * we see $<d> we fetch the saved string from pcb_sargv
			 * (or use the default argument if the option has been
			 * set and the argument hasn't been specified) and
			 * return a token corresponding to this string.
			 */
			if (i < 0 || (i >= yypcb->pcb_sargc &&
			    !(yypcb->pcb_cflags & DTRACE_C_DEFARG))) {
				xyerror(D_MACRO_UNDEF, "macro argument %s is "
				    "not defined\n", yytext);
			}

			if (i < yypcb->pcb_sargc) {
				v = yypcb->pcb_sargv[i]; /* get val from pcb */
				yypcb->pcb_sflagv[i] |= DT_IDFLG_REF;
			}

			/*
			 * If the macro text is not a valid integer or ident,
			 * then we treat it as a string.  The string may be
			 * optionally enclosed in quotes, which we strip.
			 */
			if (strbadidnum(v)) {
				size_t len = strlen(v);

				if (len != 1 && *v == '"' && v[len - 1] == '"')
					yylval.l_str = strndup(v + 1, len - 2);
				else
					yylval.l_str = strndup(v, len);

				if (yylval.l_str == NULL)
					longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

				(void) stresc2chr(yylval.l_str);
				return (DT_TOK_STRING);
			}

			/*
			 * If the macro text is not a string an begins with a
			 * digit or a +/- sign, process it as an integer token.
			 */
			if (isdigit(v[0]) || v[0] == '-' || v[0] == '+') {
				if (isdigit(v[0]))
					yyintprefix = 0;
				else
					yyintprefix = *v++;

				errno = 0;
				yylval.l_int = strtoull(v, &p, 0);
				(void) strncpy(yyintsuffix, p,
				    sizeof (yyintsuffix));
				yyintdecimal = *v != '0';

				if (errno == ERANGE) {
					xyerror(D_MACRO_OFLOW, "macro argument"
					    " %s constant %s results in integer"
					    " overflow\n", yytext, v);
				}

				return (DT_TOK_INT);
			}

			return (id_or_type(v));
		}

<S0>"$$"{RGX_IDENT} {
			dt_ident_t *idp = dt_idhash_lookup(
			    yypcb->pcb_hdl->dt_macros, yytext + 2);

			char s[16]; /* enough for UINT_MAX + \0 */

			if (idp == NULL) {
				xyerror(D_MACRO_UNDEF, "macro variable %s "
				    "is not defined\n", yytext);
			}

			/*
			 * For the moment, all current macro variables are of
			 * type id_t (refer to dtrace_update() for details).
			 */
			(void) snprintf(s, sizeof (s), "%u", idp->di_id);
			if ((yylval.l_str = strdup(s)) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			return (DT_TOK_STRING);
		}

<S0>"$"{RGX_IDENT} {
			dt_ident_t *idp = dt_idhash_lookup(
			    yypcb->pcb_hdl->dt_macros, yytext + 1);

			if (idp == NULL) {
				xyerror(D_MACRO_UNDEF, "macro variable %s "
				    "is not defined\n", yytext);
			}

			/*
			 * For the moment, all current macro variables are of
			 * type id_t (refer to dtrace_update() for details).
			 */
			yylval.l_int = (intmax_t)(int)idp->di_id;
			yyintprefix = 0;
			yyintsuffix[0] = '\0';
			yyintdecimal = 1;

			return (DT_TOK_INT);
		}

<S0>{RGX_IDENT} |
<S0>{RGX_MOD_IDENT}{RGX_IDENT} |
<S0>{RGX_MOD_IDENT} {
			return (id_or_type(yytext));
		}

<S0>{RGX_AGG}	{
			if ((yylval.l_str = strdup(yytext)) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
			return (DT_TOK_AGG);
		}

<S0>"@"		{
			if ((yylval.l_str = strdup("@_")) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
			return (DT_TOK_AGG);
		}

<S0>{RGX_INT}	|
<S2>{RGX_INT}	|
<S3>{RGX_INT}	{
			char *p;

			errno = 0;
			yylval.l_int = strtoull(yytext, &p, 0);
			yyintprefix = 0;
			(void) strncpy(yyintsuffix, p, sizeof (yyintsuffix));
			yyintdecimal = yytext[0] != '0';

			if (errno == ERANGE) {
				xyerror(D_INT_OFLOW, "constant %s results in "
				    "integer overflow\n", yytext);
			}

			if (*p != '\0' && strchr("uUlL", *p) == NULL) {
				xyerror(D_INT_DIGIT, "constant %s contains "
				    "invalid digit %c\n", yytext, *p);
			}

			if ((YYSTATE) != S3)
				return (DT_TOK_INT);

			yypragma = dt_node_link(yypragma,
			    dt_node_int(yylval.l_int));
		}

<S0>{RGX_FP}	yyerror("floating-point constants are not permitted\n");

<S0>\"{RGX_STR}$ |
<S3>\"{RGX_STR}$ xyerror(D_STR_NL, "newline encountered in string literal");

<S0>\"{RGX_STR}\" |
<S3>\"{RGX_STR}\" {
			/*
			 * Quoted string -- convert C escape sequences and
			 * return the string as a token.
			 */
			yylval.l_str = strndup(yytext + 1, yyleng - 2);

			if (yylval.l_str == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			(void) stresc2chr(yylval.l_str);
			if ((YYSTATE) != S3)
				return (DT_TOK_STRING);

			yypragma = dt_node_link(yypragma,
			    dt_node_string(yylval.l_str));
		}

<S0>'{RGX_CHR}$	xyerror(D_CHR_NL, "newline encountered in character constant");

<S0>'{RGX_CHR}'	{
			char *s, *p, *q;
			size_t nbytes;

			/*
			 * Character constant -- convert C escape sequences and
			 * return the character as an integer immediate value.
			 */
			if (yyleng == 2)
				xyerror(D_CHR_NULL, "empty character constant");

			s = yytext + 1;
			yytext[yyleng - 1] = '\0';
			nbytes = stresc2chr(s);
			yylval.l_int = 0;
			yyintprefix = 0;
			yyintsuffix[0] = '\0';
			yyintdecimal = 1;

			if (nbytes > sizeof (yylval.l_int)) {
				xyerror(D_CHR_OFLOW, "character constant is "
				    "too long");
			}
#if BYTE_ORDER == _LITTLE_ENDIAN
			p = ((char *)&yylval.l_int) + nbytes - 1;
			for (q = s; nbytes != 0; nbytes--)
				*p-- = *q++;
#else
			bcopy(s, ((char *)&yylval.l_int) +
			    sizeof (yylval.l_int) - nbytes, nbytes);
#endif
			return (DT_TOK_INT);
		}

<S0>"/*"	|
<S2>"/*"	{
			yypcb->pcb_cstate = (YYSTATE);
			BEGIN(S1);
		}

<S0>{RGX_INTERP} |
<S2>{RGX_INTERP} ;	/* discard any #! lines */

<S0>{RGX_CTL}	|
<S2>{RGX_CTL}	|
<S4>{RGX_CTL}	{
			assert(yypragma == NULL);
			yypcb->pcb_cstate = (YYSTATE);
			BEGIN(S3);
		}

<S4>.		;	/* discard */
<S4>"\n"	;	/* discard */

<S0>"/"		{
			int c, tok;

			/*
			 * The use of "/" as the predicate delimiter and as the
			 * integer division symbol requires special lookahead
			 * to avoid a shift/reduce conflict in the D grammar.
			 * We look ahead to the next non-whitespace character.
			 * If we encounter EOF, ";", "{", or "/", then this "/"
			 * closes the predicate and we return DT_TOK_EPRED.
			 * If we encounter anything else, it's DT_TOK_DIV.
			 */
			while ((c = input()) != 0) {
				if (strchr("\f\n\r\t\v ", c) == NULL)
					break;
			}

			if (c == 0 || c == ';' || c == '{' || c == '/') {
				if (yypcb->pcb_parens != 0) {
					yyerror("closing ) expected in "
					    "predicate before /\n");
				}
				if (yypcb->pcb_brackets != 0) {
					yyerror("closing ] expected in "
					    "predicate before /\n");
				}
				tok = DT_TOK_EPRED;
			} else
				tok = DT_TOK_DIV;

			unput(c);
			return (tok);
		}

<S0>"("		{
			yypcb->pcb_parens++;
			return (DT_TOK_LPAR);
		}

<S0>")"		{
			if (--yypcb->pcb_parens < 0)
				yyerror("extra ) in input stream\n");
			return (DT_TOK_RPAR);
		}

<S0>"["		{
			yypcb->pcb_brackets++;
			return (DT_TOK_LBRAC);
		}

<S0>"]"		{
			if (--yypcb->pcb_brackets < 0)
				yyerror("extra ] in input stream\n");
			return (DT_TOK_RBRAC);
		}

<S0>"{"		|
<S2>"{"		{
			yypcb->pcb_braces++;
			return ('{');
		}

<S0>"}"		{
			if (--yypcb->pcb_braces < 0)
				yyerror("extra } in input stream\n");
			return ('}');
		}

<S0>"|"		return (DT_TOK_BOR);
<S0>"^"		return (DT_TOK_XOR);
<S0>"&"		return (DT_TOK_BAND);
<S0>"&&"	return (DT_TOK_LAND);
<S0>"^^"	return (DT_TOK_LXOR);
<S0>"||"	return (DT_TOK_LOR);
<S0>"=="	return (DT_TOK_EQU);
<S0>"!="	return (DT_TOK_NEQ);
<S0>"<"		return (DT_TOK_LT);
<S0>"<="	return (DT_TOK_LE);
<S0>">"		return (DT_TOK_GT);
<S0>">="	return (DT_TOK_GE);
<S0>"<<"	return (DT_TOK_LSH);
<S0>">>"	return (DT_TOK_RSH);
<S0>"+"		return (DT_TOK_ADD);
<S0>"-"		return (DT_TOK_SUB);
<S0>"*"		return (DT_TOK_MUL);
<S0>"%"		return (DT_TOK_MOD);
<S0>"~"		return (DT_TOK_BNEG);
<S0>"!"		return (DT_TOK_LNEG);
<S0>"?"		return (DT_TOK_QUESTION);
<S0>":"		return (DT_TOK_COLON);
<S0>"."		return (DT_TOK_DOT);
<S0>"->"	return (DT_TOK_PTR);
<S0>"="		return (DT_TOK_ASGN);
<S0>"+="	return (DT_TOK_ADD_EQ);
<S0>"-="	return (DT_TOK_SUB_EQ);
<S0>"*="	return (DT_TOK_MUL_EQ);
<S0>"/="	return (DT_TOK_DIV_EQ);
<S0>"%="	return (DT_TOK_MOD_EQ);
<S0>"&="	return (DT_TOK_AND_EQ);
<S0>"^="	return (DT_TOK_XOR_EQ);
<S0>"|="	return (DT_TOK_OR_EQ);
<S0>"<<="	return (DT_TOK_LSH_EQ);
<S0>">>="	return (DT_TOK_RSH_EQ);
<S0>"++"	return (DT_TOK_ADDADD);
<S0>"--"	return (DT_TOK_SUBSUB);
<S0>"..."	return (DT_TOK_ELLIPSIS);
<S0>","		return (DT_TOK_COMMA);
<S0>";"		return (';');
<S0>{RGX_WS}	; /* discard */
<S0>"\\"\n	; /* discard */
<S0>.		yyerror("syntax error near \"%c\"\n", yytext[0]);

<S1>"/*"	yyerror("/* encountered inside a comment\n");
<S1>"*/"	BEGIN(yypcb->pcb_cstate);
<S1>.|\n	; /* discard */

<S2>{RGX_PSPEC}	{
			/*
			 * S2 has an ambiguity because RGX_PSPEC includes '*'
			 * as a glob character and '*' also can be DT_TOK_STAR.
			 * Since lex always matches the longest token, this
			 * rule can be matched by an input string like "int*",
			 * which could begin a global variable declaration such
			 * as "int*x;" or could begin a RGX_PSPEC with globbing
			 * such as "int* { trace(timestamp); }".  If C_PSPEC is
			 * not set, we must resolve the ambiguity in favor of
			 * the type and perform lexer pushback if the fragment
			 * before '*' or entire fragment matches a type name.
			 * If C_PSPEC is set, we always return a PSPEC token.
			 * If C_PSPEC is off, the user can avoid ambiguity by
			 * including a ':' delimiter in the specifier, which
			 * they should be doing anyway to specify the provider.
			 */
			if (!(yypcb->pcb_cflags & DTRACE_C_PSPEC) &&
			    strchr(yytext, ':') == NULL) {

				char *p = strchr(yytext, '*');
				char *q = yytext + yyleng - 1;

				if (p != NULL && p > yytext)
					*p = '\0'; /* prune yytext */

				if (dt_type_lookup(yytext, NULL) == 0) {
					yylval.l_str = strdup(yytext);

					if (yylval.l_str == NULL) {
						longjmp(yypcb->pcb_jmpbuf,
						    EDT_NOMEM);
					}

					if (p != NULL && p > yytext) {
						for (*p = '*'; q >= p; q--)
							unput(*q);
					}

					yybegin(YYS_EXPR);
					return (DT_TOK_TNAME);
				}

				if (p != NULL && p > yytext)
					*p = '*'; /* restore yytext */
			}

			if ((yylval.l_str = strdup(yytext)) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			return (DT_TOK_PSPEC);
		}

<S2>"/"		return (DT_TOK_DIV);
<S2>","		return (DT_TOK_COMMA);

<S2>{RGX_WS}	; /* discard */
<S2>.		yyerror("syntax error near \"%c\"\n", yytext[0]);

<S3>\n		{
			dt_pragma(yypragma);
			yypragma = NULL;
			BEGIN(yypcb->pcb_cstate);
		}

<S3>[\f\t\v ]+	; /* discard */

<S3>[^\f\n\t\v "]+ {
			dt_node_t *dnp;

			if ((yylval.l_str = strdup(yytext)) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			/*
			 * We want to call dt_node_ident() here, but we can't
			 * because it will expand inlined identifiers, which we
			 * don't want to do from #pragma context in order to
			 * support pragmas that apply to the ident itself.  We
			 * call dt_node_string() and then reset dn_op instead.
			 */
			dnp = dt_node_string(yylval.l_str);
			dnp->dn_kind = DT_NODE_IDENT;
			dnp->dn_op = DT_TOK_IDENT;
			yypragma = dt_node_link(yypragma, dnp);
		}

<S3>.		yyerror("syntax error near \"%c\"\n", yytext[0]);

%%

/*
 * yybegin provides a wrapper for use from C code around the lex BEGIN() macro.
 * We use two main states for lexing because probe descriptions use a syntax
 * that is incompatible with the normal D tokens (e.g. names can contain "-").
 * yybegin also handles the job of switching between two lists of dt_nodes
 * as we allocate persistent definitions, like inlines, and transient nodes
 * that will be freed once we are done parsing the current program file.
 */
void
yybegin(yystate_t state)
{
#ifdef	YYDEBUG
	yydebug = _dtrace_debug;
#endif
	if (yypcb->pcb_yystate == state)
		return; /* nothing to do if we're in the state already */

	if (yypcb->pcb_yystate == YYS_DEFINE) {
		yypcb->pcb_list = yypcb->pcb_hold;
		yypcb->pcb_hold = NULL;
	}

	switch (state) {
	case YYS_CLAUSE:
		BEGIN(S2);
		break;
	case YYS_DEFINE:
		assert(yypcb->pcb_hold == NULL);
		yypcb->pcb_hold = yypcb->pcb_list;
		yypcb->pcb_list = NULL;
		/*FALLTHRU*/
	case YYS_EXPR:
		BEGIN(S0);
		break;
	case YYS_DONE:
		break;
	case YYS_CONTROL:
		BEGIN(S4);
		break;
	default:
		xyerror(D_UNKNOWN, "internal error -- bad yystate %d\n", state);
	}

	yypcb->pcb_yystate = state;
}

void
yyinit(dt_pcb_t *pcb)
{
	yypcb = pcb;
	yylineno = 1;
	yypragma = NULL;
#ifdef illumos
	yysptr = yysbuf;
#endif
	YY_FLUSH_BUFFER;
}

/*
 * Given a lexeme 's' (typically yytext), set yylval and return an appropriate
 * token to the parser indicating either an identifier or a typedef name.
 * User-defined global variables always take precedence over types, but we do
 * use some heuristics because D programs can look at an ever-changing set of
 * kernel types and also can implicitly instantiate variables by assignment,
 * unlike in C.  The code here is ordered carefully as lookups are not cheap.
 */
static int
id_or_type(const char *s)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_decl_t *ddp = yypcb->pcb_dstack.ds_decl;
	int c0, c1, ttok = DT_TOK_TNAME;
	dt_ident_t *idp;

	if ((s = yylval.l_str = strdup(s)) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	/*
	 * If the lexeme is a global variable or likely identifier or *not* a
	 * type_name, then it is an identifier token.
	 */
	if (dt_idstack_lookup(&yypcb->pcb_globals, s) != NULL ||
	    dt_idhash_lookup(yypcb->pcb_idents, s) != NULL ||
	    dt_type_lookup(s, NULL) != 0)
		return (DT_TOK_IDENT);

	/*
	 * If we're in the midst of parsing a declaration and a type_specifier
	 * has already been shifted, then return DT_TOK_IDENT instead of TNAME.
	 * This semantic is necessary to permit valid ISO C code such as:
	 *
	 * typedef int foo;
	 * struct s { foo foo; };
	 *
	 * without causing shift/reduce conflicts in the direct_declarator part
	 * of the grammar.  The result is that we must check for conflicting
	 * redeclarations of the same identifier as part of dt_node_decl().
	 */
	if (ddp != NULL && ddp->dd_name != NULL)
		return (DT_TOK_IDENT);

	/*
	 * If the lexeme is a type name and we are not in a program clause,
	 * then always interpret it as a type and return DT_TOK_TNAME.
	 */
	if ((YYSTATE) != S0)
		return (DT_TOK_TNAME);

	/*
	 * If the lexeme matches a type name but is in a program clause, then
	 * it could be a type or it could be an undefined variable.  Peek at
	 * the next token to decide.  If we see ++, --, [, or =, we know there
	 * might be an assignment that is trying to create a global variable,
	 * so we optimistically return DT_TOK_IDENT.  There is no harm in being
	 * wrong: a type_name followed by ++, --, [, or = is a syntax error.
	 */
	while ((c0 = input()) != 0) {
		if (strchr("\f\n\r\t\v ", c0) == NULL)
			break;
	}

	switch (c0) {
	case '+':
	case '-':
		if ((c1 = input()) == c0)
			ttok = DT_TOK_IDENT;
		unput(c1);
		break;

	case '=':
		if ((c1 = input()) != c0)
			ttok = DT_TOK_IDENT;
		unput(c1);
		break;
	case '[':
		ttok = DT_TOK_IDENT;
		break;
	}

	if (ttok == DT_TOK_IDENT) {
		idp = dt_idhash_insert(yypcb->pcb_idents, s, DT_IDENT_SCALAR, 0,
		    0, _dtrace_defattr, 0, &dt_idops_thaw, NULL, dtp->dt_gen);

		if (idp == NULL)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
	}

	unput(c0);
	return (ttok);
}

#ifdef illumos
static int
input(void)
{
	int c;

	if (yysptr > yysbuf)
		c = *--yysptr;
	else if (yypcb->pcb_fileptr != NULL)
		c = fgetc(yypcb->pcb_fileptr);
	else if (yypcb->pcb_strptr < yypcb->pcb_string + yypcb->pcb_strlen)
		c = *(unsigned char *)(yypcb->pcb_strptr++);
	else
		c = EOF;

	if (c == '\n')
		yylineno++;

	if (c != EOF)
		return (c);

	if ((YYSTATE) == S1)
		yyerror("end-of-file encountered before matching */\n");

	if ((YYSTATE) == S3)
		yyerror("end-of-file encountered before end of control line\n");

	if (yypcb->pcb_fileptr != NULL && ferror(yypcb->pcb_fileptr))
		longjmp(yypcb->pcb_jmpbuf, EDT_FIO);

	return (0); /* EOF */
}

static void
unput(int c)
{
	if (c == '\n')
		yylineno--;

	*yysptr++ = c;
	yytchar = c;
}
#endif /* illumos */
