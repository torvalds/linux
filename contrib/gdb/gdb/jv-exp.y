/* YACC parser for Java expressions, for GDB.
   Copyright 1997, 1998, 1999, 2000
   Free Software Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Parse a Java expression from text in a string,
   and return the result as a  struct expression  pointer.
   That structure contains arithmetic operations in reverse polish,
   with constants represented by operations that are followed by special data.
   See expression.h for the details of the format.
   What is important here is that it can be built up sequentially
   during the process of parsing; the lower levels of the tree always
   come first in the result.  Well, almost always; see ArrayAccess.

   Note that malloc's and realloc's in this file are transformed to
   xmalloc and xrealloc respectively by the same sed command in the
   makefile that remaps any other malloc/realloc inserted by the parser
   generator.  Doing this with #defines and trying to control the interaction
   with include files (<malloc.h> and <stdlib.h> for example) just became
   too messy, particularly when such includes can be inserted at random
   times by the parser generator.  */
  
%{

#include "defs.h"
#include "gdb_string.h"
#include <ctype.h>
#include "expression.h"
#include "value.h"
#include "parser-defs.h"
#include "language.h"
#include "jv-lang.h"
#include "bfd.h" /* Required by objfiles.h.  */
#include "symfile.h" /* Required by objfiles.h.  */
#include "objfiles.h" /* For have_full_symbols and have_partial_symbols */
#include "block.h"

/* Remap normal yacc parser interface names (yyparse, yylex, yyerror, etc),
   as well as gratuitiously global symbol names, so we can have multiple
   yacc generated parsers in gdb.  Note that these are only the variables
   produced by yacc.  If other parser generators (bison, byacc, etc) produce
   additional global names that conflict at link time, then those parser
   generators need to be fixed instead of adding those names to this list. */

#define	yymaxdepth java_maxdepth
#define	yyparse	java_parse
#define	yylex	java_lex
#define	yyerror	java_error
#define	yylval	java_lval
#define	yychar	java_char
#define	yydebug	java_debug
#define	yypact	java_pact	
#define	yyr1	java_r1			
#define	yyr2	java_r2			
#define	yydef	java_def		
#define	yychk	java_chk		
#define	yypgo	java_pgo		
#define	yyact	java_act		
#define	yyexca	java_exca
#define yyerrflag java_errflag
#define yynerrs	java_nerrs
#define	yyps	java_ps
#define	yypv	java_pv
#define	yys	java_s
#define	yy_yys	java_yys
#define	yystate	java_state
#define	yytmp	java_tmp
#define	yyv	java_v
#define	yy_yyv	java_yyv
#define	yyval	java_val
#define	yylloc	java_lloc
#define yyreds	java_reds		/* With YYDEBUG defined */
#define yytoks	java_toks		/* With YYDEBUG defined */
#define yyname	java_name		/* With YYDEBUG defined */
#define yyrule	java_rule		/* With YYDEBUG defined */
#define yylhs	java_yylhs
#define yylen	java_yylen
#define yydefred java_yydefred
#define yydgoto	java_yydgoto
#define yysindex java_yysindex
#define yyrindex java_yyrindex
#define yygindex java_yygindex
#define yytable	 java_yytable
#define yycheck	 java_yycheck

#ifndef YYDEBUG
#define	YYDEBUG 1		/* Default to yydebug support */
#endif

#define YYFPRINTF parser_fprintf

int yyparse (void);

static int yylex (void);

void yyerror (char *);

static struct type *java_type_from_name (struct stoken);
static void push_expression_name (struct stoken);
static void push_fieldnames (struct stoken);

static struct expression *copy_exp (struct expression *, int);
static void insert_exp (int, struct expression *);

%}

/* Although the yacc "value" of an expression is not used,
   since the result is stored in the structure being created,
   other node types do have values.  */

%union
  {
    LONGEST lval;
    struct {
      LONGEST val;
      struct type *type;
    } typed_val_int;
    struct {
      DOUBLEST dval;
      struct type *type;
    } typed_val_float;
    struct symbol *sym;
    struct type *tval;
    struct stoken sval;
    struct ttype tsym;
    struct symtoken ssym;
    struct block *bval;
    enum exp_opcode opcode;
    struct internalvar *ivar;
    int *ivec;
  }

%{
/* YYSTYPE gets defined by %union */
static int parse_number (char *, int, int, YYSTYPE *);
%}

%type <lval> rcurly Dims Dims_opt
%type <tval> ClassOrInterfaceType ClassType /* ReferenceType Type ArrayType */
%type <tval> IntegralType FloatingPointType NumericType PrimitiveType ArrayType PrimitiveOrArrayType

%token <typed_val_int> INTEGER_LITERAL
%token <typed_val_float> FLOATING_POINT_LITERAL

%token <sval> IDENTIFIER
%token <sval> STRING_LITERAL
%token <lval> BOOLEAN_LITERAL
%token <tsym> TYPENAME
%type <sval> Name SimpleName QualifiedName ForcedName

/* A NAME_OR_INT is a symbol which is not known in the symbol table,
   but which would parse as a valid number in the current input radix.
   E.g. "c" when input_radix==16.  Depending on the parse, it will be
   turned into a name or into a number.  */

%token <sval> NAME_OR_INT 

%token ERROR

/* Special type cases, put in to allow the parser to distinguish different
   legal basetypes.  */
%token LONG SHORT BYTE INT CHAR BOOLEAN DOUBLE FLOAT

%token VARIABLE

%token <opcode> ASSIGN_MODIFY

%token SUPER NEW

%left ','
%right '=' ASSIGN_MODIFY
%right '?'
%left OROR
%left ANDAND
%left '|'
%left '^'
%left '&'
%left EQUAL NOTEQUAL
%left '<' '>' LEQ GEQ
%left LSH RSH
%left '+' '-'
%left '*' '/' '%'
%right INCREMENT DECREMENT
%right '.' '[' '('


%%

start   :	exp1
	|	type_exp
	;

type_exp:	PrimitiveOrArrayType
		{
		  write_exp_elt_opcode(OP_TYPE);
		  write_exp_elt_type($1);
		  write_exp_elt_opcode(OP_TYPE);
		}
	;

PrimitiveOrArrayType:
		PrimitiveType
	|	ArrayType
	;

StringLiteral:
	STRING_LITERAL
		{
		  write_exp_elt_opcode (OP_STRING);
		  write_exp_string ($1);
		  write_exp_elt_opcode (OP_STRING);
		}
;

Literal:
	INTEGER_LITERAL
		{ write_exp_elt_opcode (OP_LONG);
		  write_exp_elt_type ($1.type);
		  write_exp_elt_longcst ((LONGEST)($1.val));
		  write_exp_elt_opcode (OP_LONG); }
|	NAME_OR_INT
		{ YYSTYPE val;
		  parse_number ($1.ptr, $1.length, 0, &val);
		  write_exp_elt_opcode (OP_LONG);
		  write_exp_elt_type (val.typed_val_int.type);
		  write_exp_elt_longcst ((LONGEST)val.typed_val_int.val);
		  write_exp_elt_opcode (OP_LONG);
		}
|	FLOATING_POINT_LITERAL
		{ write_exp_elt_opcode (OP_DOUBLE);
		  write_exp_elt_type ($1.type);
		  write_exp_elt_dblcst ($1.dval);
		  write_exp_elt_opcode (OP_DOUBLE); }
|	BOOLEAN_LITERAL
		{ write_exp_elt_opcode (OP_LONG);
		  write_exp_elt_type (java_boolean_type);
		  write_exp_elt_longcst ((LONGEST)$1);
		  write_exp_elt_opcode (OP_LONG); }
|	StringLiteral
	;

/* UNUSED:
Type:
	PrimitiveType
|	ReferenceType
;
*/

PrimitiveType:
	NumericType
|	BOOLEAN
		{ $$ = java_boolean_type; }
;

NumericType:
	IntegralType
|	FloatingPointType
;

IntegralType:
	BYTE
		{ $$ = java_byte_type; }
|	SHORT
		{ $$ = java_short_type; }
|	INT
		{ $$ = java_int_type; }
|	LONG
		{ $$ = java_long_type; }
|	CHAR
		{ $$ = java_char_type; }
;

FloatingPointType:
	FLOAT
		{ $$ = java_float_type; }
|	DOUBLE
		{ $$ = java_double_type; }
;

/* UNUSED:
ReferenceType:
	ClassOrInterfaceType
|	ArrayType
;
*/

ClassOrInterfaceType:
	Name
		{ $$ = java_type_from_name ($1); }
;

ClassType:
	ClassOrInterfaceType
;

ArrayType:
	PrimitiveType Dims
		{ $$ = java_array_type ($1, $2); }
|	Name Dims
		{ $$ = java_array_type (java_type_from_name ($1), $2); }
;

Name:
	IDENTIFIER
|	QualifiedName
;

ForcedName:
	SimpleName
|	QualifiedName
;

SimpleName:
	IDENTIFIER
|	NAME_OR_INT
;

QualifiedName:
	Name '.' SimpleName
		{ $$.length = $1.length + $3.length + 1;
		  if ($1.ptr + $1.length + 1 == $3.ptr
		      && $1.ptr[$1.length] == '.')
		    $$.ptr = $1.ptr;  /* Optimization. */
		  else
		    {
		      $$.ptr = (char *) malloc ($$.length + 1);
		      make_cleanup (free, $$.ptr);
		      sprintf ($$.ptr, "%.*s.%.*s",
			       $1.length, $1.ptr, $3.length, $3.ptr);
		} }
;

/*
type_exp:	type
			{ write_exp_elt_opcode(OP_TYPE);
			  write_exp_elt_type($1);
			  write_exp_elt_opcode(OP_TYPE);}
	;
	*/

/* Expressions, including the comma operator.  */
exp1	:	Expression
	|	exp1 ',' Expression
			{ write_exp_elt_opcode (BINOP_COMMA); }
	;

Primary:
	PrimaryNoNewArray
|	ArrayCreationExpression
;

PrimaryNoNewArray:
	Literal
|	'(' Expression ')'
|	ClassInstanceCreationExpression
|	FieldAccess
|	MethodInvocation
|	ArrayAccess
|	lcurly ArgumentList rcurly
		{ write_exp_elt_opcode (OP_ARRAY);
		  write_exp_elt_longcst ((LONGEST) 0);
		  write_exp_elt_longcst ((LONGEST) $3);
		  write_exp_elt_opcode (OP_ARRAY); }
;

lcurly:
	'{'
		{ start_arglist (); }
;

rcurly:
	'}'
		{ $$ = end_arglist () - 1; }
;

ClassInstanceCreationExpression:
	NEW ClassType '(' ArgumentList_opt ')'
		{ internal_error (__FILE__, __LINE__,
				  _("FIXME - ClassInstanceCreationExpression")); }
;

ArgumentList:
	Expression
		{ arglist_len = 1; }
|	ArgumentList ',' Expression
		{ arglist_len++; }
;

ArgumentList_opt:
	/* EMPTY */
		{ arglist_len = 0; }
| ArgumentList
;

ArrayCreationExpression:
	NEW PrimitiveType DimExprs Dims_opt
		{ internal_error (__FILE__, __LINE__,
				  _("FIXME - ArrayCreationExpression")); }
|	NEW ClassOrInterfaceType DimExprs Dims_opt
		{ internal_error (__FILE__, __LINE__,
				  _("FIXME - ArrayCreationExpression")); }
;

DimExprs:
	DimExpr
|	DimExprs DimExpr
;

DimExpr:
	'[' Expression ']'
;

Dims:
	'[' ']'
		{ $$ = 1; }
|	Dims '[' ']'
	{ $$ = $1 + 1; }
;

Dims_opt:
	Dims
|	/* EMPTY */
		{ $$ = 0; }
;

FieldAccess:
	Primary '.' SimpleName
		{ push_fieldnames ($3); }
|	VARIABLE '.' SimpleName
		{ push_fieldnames ($3); }
/*|	SUPER '.' SimpleName { FIXME } */
;

MethodInvocation:
	Name '(' ArgumentList_opt ')'
		{ error (_("Method invocation not implemented")); }
|	Primary '.' SimpleName '(' ArgumentList_opt ')'
		{ error (_("Method invocation not implemented")); }
|	SUPER '.' SimpleName '(' ArgumentList_opt ')'
		{ error (_("Method invocation not implemented")); }
;

ArrayAccess:
	Name '[' Expression ']'
                {
                  /* Emit code for the Name now, then exchange it in the
		     expout array with the Expression's code.  We could
		     introduce a OP_SWAP code or a reversed version of
		     BINOP_SUBSCRIPT, but that makes the rest of GDB pay
		     for our parsing kludges.  */
		  struct expression *name_expr;

		  push_expression_name ($1);
		  name_expr = copy_exp (expout, expout_ptr);
		  expout_ptr -= name_expr->nelts;
		  insert_exp (expout_ptr-length_of_subexp (expout, expout_ptr),
			      name_expr);
		  free (name_expr);
		  write_exp_elt_opcode (BINOP_SUBSCRIPT);
		}
|	VARIABLE '[' Expression ']'
		{ write_exp_elt_opcode (BINOP_SUBSCRIPT); }
|	PrimaryNoNewArray '[' Expression ']'
		{ write_exp_elt_opcode (BINOP_SUBSCRIPT); }
;

PostfixExpression:
	Primary
|	Name
		{ push_expression_name ($1); }
|	VARIABLE
		/* Already written by write_dollar_variable. */
|	PostIncrementExpression
|	PostDecrementExpression
;

PostIncrementExpression:
	PostfixExpression INCREMENT
		{ write_exp_elt_opcode (UNOP_POSTINCREMENT); }
;

PostDecrementExpression:
	PostfixExpression DECREMENT
		{ write_exp_elt_opcode (UNOP_POSTDECREMENT); }
;

UnaryExpression:
	PreIncrementExpression
|	PreDecrementExpression
|	'+' UnaryExpression
|	'-' UnaryExpression
		{ write_exp_elt_opcode (UNOP_NEG); }
|	'*' UnaryExpression 
		{ write_exp_elt_opcode (UNOP_IND); } /*FIXME not in Java  */
|	UnaryExpressionNotPlusMinus
;

PreIncrementExpression:
	INCREMENT UnaryExpression
		{ write_exp_elt_opcode (UNOP_PREINCREMENT); }
;

PreDecrementExpression:
	DECREMENT UnaryExpression
		{ write_exp_elt_opcode (UNOP_PREDECREMENT); }
;

UnaryExpressionNotPlusMinus:
	PostfixExpression
|	'~' UnaryExpression
		{ write_exp_elt_opcode (UNOP_COMPLEMENT); }
|	'!' UnaryExpression
		{ write_exp_elt_opcode (UNOP_LOGICAL_NOT); }
|	CastExpression
	;

CastExpression:
	'(' PrimitiveType Dims_opt ')' UnaryExpression
		{ write_exp_elt_opcode (UNOP_CAST);
		  write_exp_elt_type (java_array_type ($2, $3));
		  write_exp_elt_opcode (UNOP_CAST); }
|	'(' Expression ')' UnaryExpressionNotPlusMinus
		{
		  int exp_size = expout_ptr;
		  int last_exp_size = length_of_subexp(expout, expout_ptr);
		  struct type *type;
		  int i;
		  int base = expout_ptr - last_exp_size - 3;
		  if (base < 0 || expout->elts[base+2].opcode != OP_TYPE)
		    error (_("Invalid cast expression"));
		  type = expout->elts[base+1].type;
		  /* Remove the 'Expression' and slide the
		     UnaryExpressionNotPlusMinus down to replace it. */
		  for (i = 0;  i < last_exp_size;  i++)
		    expout->elts[base + i] = expout->elts[base + i + 3];
		  expout_ptr -= 3;
		  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
		    type = lookup_pointer_type (type);
		  write_exp_elt_opcode (UNOP_CAST);
		  write_exp_elt_type (type);
		  write_exp_elt_opcode (UNOP_CAST);
		}
|	'(' Name Dims ')' UnaryExpressionNotPlusMinus
		{ write_exp_elt_opcode (UNOP_CAST);
		  write_exp_elt_type (java_array_type (java_type_from_name ($2), $3));
		  write_exp_elt_opcode (UNOP_CAST); }
;


MultiplicativeExpression:
	UnaryExpression
|	MultiplicativeExpression '*' UnaryExpression
		{ write_exp_elt_opcode (BINOP_MUL); }
|	MultiplicativeExpression '/' UnaryExpression
		{ write_exp_elt_opcode (BINOP_DIV); }
|	MultiplicativeExpression '%' UnaryExpression
		{ write_exp_elt_opcode (BINOP_REM); }
;

AdditiveExpression:
	MultiplicativeExpression
|	AdditiveExpression '+' MultiplicativeExpression
		{ write_exp_elt_opcode (BINOP_ADD); }
|	AdditiveExpression '-' MultiplicativeExpression
		{ write_exp_elt_opcode (BINOP_SUB); }
;

ShiftExpression:
	AdditiveExpression
|	ShiftExpression LSH AdditiveExpression
		{ write_exp_elt_opcode (BINOP_LSH); }
|	ShiftExpression RSH AdditiveExpression
		{ write_exp_elt_opcode (BINOP_RSH); }
/* |	ShiftExpression >>> AdditiveExpression { FIXME } */
;

RelationalExpression:
	ShiftExpression
|	RelationalExpression '<' ShiftExpression
		{ write_exp_elt_opcode (BINOP_LESS); }
|	RelationalExpression '>' ShiftExpression
		{ write_exp_elt_opcode (BINOP_GTR); }
|	RelationalExpression LEQ ShiftExpression
		{ write_exp_elt_opcode (BINOP_LEQ); }
|	RelationalExpression GEQ ShiftExpression
		{ write_exp_elt_opcode (BINOP_GEQ); }
/* | RelationalExpresion INSTANCEOF ReferenceType { FIXME } */
;

EqualityExpression:
	RelationalExpression
|	EqualityExpression EQUAL RelationalExpression
		{ write_exp_elt_opcode (BINOP_EQUAL); }
|	EqualityExpression NOTEQUAL RelationalExpression
		{ write_exp_elt_opcode (BINOP_NOTEQUAL); }
;

AndExpression:
	EqualityExpression
|	AndExpression '&' EqualityExpression
		{ write_exp_elt_opcode (BINOP_BITWISE_AND); }
;

ExclusiveOrExpression:
	AndExpression
|	ExclusiveOrExpression '^' AndExpression
		{ write_exp_elt_opcode (BINOP_BITWISE_XOR); }
;
InclusiveOrExpression:
	ExclusiveOrExpression
|	InclusiveOrExpression '|' ExclusiveOrExpression
		{ write_exp_elt_opcode (BINOP_BITWISE_IOR); }
;

ConditionalAndExpression:
	InclusiveOrExpression
|	ConditionalAndExpression ANDAND InclusiveOrExpression
		{ write_exp_elt_opcode (BINOP_LOGICAL_AND); }
;

ConditionalOrExpression:
	ConditionalAndExpression
|	ConditionalOrExpression OROR ConditionalAndExpression
		{ write_exp_elt_opcode (BINOP_LOGICAL_OR); }
;

ConditionalExpression:
	ConditionalOrExpression
|	ConditionalOrExpression '?' Expression ':' ConditionalExpression
		{ write_exp_elt_opcode (TERNOP_COND); }
;

AssignmentExpression:
	ConditionalExpression
|	Assignment
;
			  
Assignment:
	LeftHandSide '=' ConditionalExpression
		{ write_exp_elt_opcode (BINOP_ASSIGN); }
|	LeftHandSide ASSIGN_MODIFY ConditionalExpression
		{ write_exp_elt_opcode (BINOP_ASSIGN_MODIFY);
		  write_exp_elt_opcode ($2);
		  write_exp_elt_opcode (BINOP_ASSIGN_MODIFY); }
;

LeftHandSide:
	ForcedName
		{ push_expression_name ($1); }
|	VARIABLE
		/* Already written by write_dollar_variable. */
|	FieldAccess
|	ArrayAccess
;


Expression:
	AssignmentExpression
;

%%
/* Take care of parsing a number (anything that starts with a digit).
   Set yylval and return the token type; update lexptr.
   LEN is the number of characters in it.  */

/*** Needs some error checking for the float case ***/

static int
parse_number (p, len, parsed_float, putithere)
     char *p;
     int len;
     int parsed_float;
     YYSTYPE *putithere;
{
  ULONGEST n = 0;
  ULONGEST limit, limit_div_base;

  int c;
  int base = input_radix;

  struct type *type;

  if (parsed_float)
    {
      /* It's a float since it contains a point or an exponent.  */
      char c;
      int num = 0;	/* number of tokens scanned by scanf */
      char saved_char = p[len];

      p[len] = 0;	/* null-terminate the token */
      if (sizeof (putithere->typed_val_float.dval) <= sizeof (float))
	num = sscanf (p, "%g%c", (float *) &putithere->typed_val_float.dval, &c);
      else if (sizeof (putithere->typed_val_float.dval) <= sizeof (double))
	num = sscanf (p, "%lg%c", (double *) &putithere->typed_val_float.dval, &c);
      else
	{
#ifdef SCANF_HAS_LONG_DOUBLE
	  num = sscanf (p, "%Lg%c", &putithere->typed_val_float.dval, &c);
#else
	  /* Scan it into a double, then assign it to the long double.
	     This at least wins with values representable in the range
	     of doubles. */
	  double temp;
	  num = sscanf (p, "%lg%c", &temp, &c);
	  putithere->typed_val_float.dval = temp;
#endif
	}
      p[len] = saved_char;	/* restore the input stream */
      if (num != 1) 		/* check scanf found ONLY a float ... */
	return ERROR;
      /* See if it has `f' or `d' suffix (float or double).  */

      c = tolower (p[len - 1]);

      if (c == 'f' || c == 'F')
	putithere->typed_val_float.type = builtin_type_float;
      else if (isdigit (c) || c == '.' || c == 'd' || c == 'D')
	putithere->typed_val_float.type = builtin_type_double;
      else
	return ERROR;

      return FLOATING_POINT_LITERAL;
    }

  /* Handle base-switching prefixes 0x, 0t, 0d, 0 */
  if (p[0] == '0')
    switch (p[1])
      {
      case 'x':
      case 'X':
	if (len >= 3)
	  {
	    p += 2;
	    base = 16;
	    len -= 2;
	  }
	break;

      case 't':
      case 'T':
      case 'd':
      case 'D':
	if (len >= 3)
	  {
	    p += 2;
	    base = 10;
	    len -= 2;
	  }
	break;

      default:
	base = 8;
	break;
      }

  c = p[len-1];
  /* A paranoid calculation of (1<<64)-1. */
  limit = (ULONGEST)0xffffffff;
  limit = ((limit << 16) << 16) | limit;
  if (c == 'l' || c == 'L')
    {
      type = java_long_type;
      len--;
    }
  else
    {
      type = java_int_type;
    }
  limit_div_base = limit / (ULONGEST) base;

  while (--len >= 0)
    {
      c = *p++;
      if (c >= '0' && c <= '9')
	c -= '0';
      else if (c >= 'A' && c <= 'Z')
	c -= 'A' - 10;
      else if (c >= 'a' && c <= 'z')
	c -= 'a' - 10;
      else
	return ERROR;	/* Char not a digit */
      if (c >= base)
	return ERROR;
      if (n > limit_div_base
	  || (n *= base) > limit - c)
	error (_("Numeric constant too large"));
      n += c;
	}

  /* If the type is bigger than a 32-bit signed integer can be, implicitly
     promote to long.  Java does not do this, so mark it as builtin_type_uint64
     rather than java_long_type.  0x80000000 will become -0x80000000 instead
     of 0x80000000L, because we don't know the sign at this point.
  */
  if (type == java_int_type && n > (ULONGEST)0x80000000)
    type = builtin_type_uint64;

  putithere->typed_val_int.val = n;
  putithere->typed_val_int.type = type;

  return INTEGER_LITERAL;
}

struct token
{
  char *operator;
  int token;
  enum exp_opcode opcode;
};

static const struct token tokentab3[] =
  {
    {">>=", ASSIGN_MODIFY, BINOP_RSH},
    {"<<=", ASSIGN_MODIFY, BINOP_LSH}
  };

static const struct token tokentab2[] =
  {
    {"+=", ASSIGN_MODIFY, BINOP_ADD},
    {"-=", ASSIGN_MODIFY, BINOP_SUB},
    {"*=", ASSIGN_MODIFY, BINOP_MUL},
    {"/=", ASSIGN_MODIFY, BINOP_DIV},
    {"%=", ASSIGN_MODIFY, BINOP_REM},
    {"|=", ASSIGN_MODIFY, BINOP_BITWISE_IOR},
    {"&=", ASSIGN_MODIFY, BINOP_BITWISE_AND},
    {"^=", ASSIGN_MODIFY, BINOP_BITWISE_XOR},
    {"++", INCREMENT, BINOP_END},
    {"--", DECREMENT, BINOP_END},
    {"&&", ANDAND, BINOP_END},
    {"||", OROR, BINOP_END},
    {"<<", LSH, BINOP_END},
    {">>", RSH, BINOP_END},
    {"==", EQUAL, BINOP_END},
    {"!=", NOTEQUAL, BINOP_END},
    {"<=", LEQ, BINOP_END},
    {">=", GEQ, BINOP_END}
  };

/* Read one token, getting characters through lexptr.  */

static int
yylex ()
{
  int c;
  int namelen;
  unsigned int i;
  char *tokstart;
  char *tokptr;
  int tempbufindex;
  static char *tempbuf;
  static int tempbufsize;
  
 retry:

  prev_lexptr = lexptr;

  tokstart = lexptr;
  /* See if it is a special token of length 3.  */
  for (i = 0; i < sizeof tokentab3 / sizeof tokentab3[0]; i++)
    if (strncmp (tokstart, tokentab3[i].operator, 3) == 0)
      {
	lexptr += 3;
	yylval.opcode = tokentab3[i].opcode;
	return tokentab3[i].token;
      }

  /* See if it is a special token of length 2.  */
  for (i = 0; i < sizeof tokentab2 / sizeof tokentab2[0]; i++)
    if (strncmp (tokstart, tokentab2[i].operator, 2) == 0)
      {
	lexptr += 2;
	yylval.opcode = tokentab2[i].opcode;
	return tokentab2[i].token;
      }

  switch (c = *tokstart)
    {
    case 0:
      return 0;

    case ' ':
    case '\t':
    case '\n':
      lexptr++;
      goto retry;

    case '\'':
      /* We either have a character constant ('0' or '\177' for example)
	 or we have a quoted symbol reference ('foo(int,int)' in C++
	 for example). */
      lexptr++;
      c = *lexptr++;
      if (c == '\\')
	c = parse_escape (&lexptr);
      else if (c == '\'')
	error (_("Empty character constant"));

      yylval.typed_val_int.val = c;
      yylval.typed_val_int.type = java_char_type;

      c = *lexptr++;
      if (c != '\'')
	{
	  namelen = skip_quoted (tokstart) - tokstart;
	  if (namelen > 2)
	    {
	      lexptr = tokstart + namelen;
	      if (lexptr[-1] != '\'')
		error (_("Unmatched single quote"));
	      namelen -= 2;
	      tokstart++;
	      goto tryname;
	    }
	  error (_("Invalid character constant"));
	}
      return INTEGER_LITERAL;

    case '(':
      paren_depth++;
      lexptr++;
      return c;

    case ')':
      if (paren_depth == 0)
	return 0;
      paren_depth--;
      lexptr++;
      return c;

    case ',':
      if (comma_terminates && paren_depth == 0)
	return 0;
      lexptr++;
      return c;

    case '.':
      /* Might be a floating point number.  */
      if (lexptr[1] < '0' || lexptr[1] > '9')
	goto symbol;		/* Nope, must be a symbol. */
      /* FALL THRU into number case.  */

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      {
	/* It's a number.  */
	int got_dot = 0, got_e = 0, toktype;
	char *p = tokstart;
	int hex = input_radix > 10;

	if (c == '0' && (p[1] == 'x' || p[1] == 'X'))
	  {
	    p += 2;
	    hex = 1;
	  }
	else if (c == '0' && (p[1]=='t' || p[1]=='T' || p[1]=='d' || p[1]=='D'))
	  {
	    p += 2;
	    hex = 0;
	  }

	for (;; ++p)
	  {
	    /* This test includes !hex because 'e' is a valid hex digit
	       and thus does not indicate a floating point number when
	       the radix is hex.  */
	    if (!hex && !got_e && (*p == 'e' || *p == 'E'))
	      got_dot = got_e = 1;
	    /* This test does not include !hex, because a '.' always indicates
	       a decimal floating point number regardless of the radix.  */
	    else if (!got_dot && *p == '.')
	      got_dot = 1;
	    else if (got_e && (p[-1] == 'e' || p[-1] == 'E')
		     && (*p == '-' || *p == '+'))
	      /* This is the sign of the exponent, not the end of the
		 number.  */
	      continue;
	    /* We will take any letters or digits.  parse_number will
	       complain if past the radix, or if L or U are not final.  */
	    else if ((*p < '0' || *p > '9')
		     && ((*p < 'a' || *p > 'z')
				  && (*p < 'A' || *p > 'Z')))
	      break;
	  }
	toktype = parse_number (tokstart, p - tokstart, got_dot|got_e, &yylval);
        if (toktype == ERROR)
	  {
	    char *err_copy = (char *) alloca (p - tokstart + 1);

	    memcpy (err_copy, tokstart, p - tokstart);
	    err_copy[p - tokstart] = 0;
	    error (_("Invalid number \"%s\""), err_copy);
	  }
	lexptr = p;
	return toktype;
      }

    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case '|':
    case '&':
    case '^':
    case '~':
    case '!':
    case '<':
    case '>':
    case '[':
    case ']':
    case '?':
    case ':':
    case '=':
    case '{':
    case '}':
    symbol:
      lexptr++;
      return c;

    case '"':

      /* Build the gdb internal form of the input string in tempbuf,
	 translating any standard C escape forms seen.  Note that the
	 buffer is null byte terminated *only* for the convenience of
	 debugging gdb itself and printing the buffer contents when
	 the buffer contains no embedded nulls.  Gdb does not depend
	 upon the buffer being null byte terminated, it uses the length
	 string instead.  This allows gdb to handle C strings (as well
	 as strings in other languages) with embedded null bytes */

      tokptr = ++tokstart;
      tempbufindex = 0;

      do {
	/* Grow the static temp buffer if necessary, including allocating
	   the first one on demand. */
	if (tempbufindex + 1 >= tempbufsize)
	  {
	    tempbuf = (char *) realloc (tempbuf, tempbufsize += 64);
	  }
	switch (*tokptr)
	  {
	  case '\0':
	  case '"':
	    /* Do nothing, loop will terminate. */
	    break;
	  case '\\':
	    tokptr++;
	    c = parse_escape (&tokptr);
	    if (c == -1)
	      {
		continue;
	      }
	    tempbuf[tempbufindex++] = c;
	    break;
	  default:
	    tempbuf[tempbufindex++] = *tokptr++;
	    break;
	  }
      } while ((*tokptr != '"') && (*tokptr != '\0'));
      if (*tokptr++ != '"')
	{
	  error (_("Unterminated string in expression"));
	}
      tempbuf[tempbufindex] = '\0';	/* See note above */
      yylval.sval.ptr = tempbuf;
      yylval.sval.length = tempbufindex;
      lexptr = tokptr;
      return (STRING_LITERAL);
    }

  if (!(c == '_' || c == '$'
	|| (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
    /* We must have come across a bad character (e.g. ';').  */
    error (_("Invalid character '%c' in expression"), c);

  /* It's a name.  See how long it is.  */
  namelen = 0;
  for (c = tokstart[namelen];
       (c == '_'
	|| c == '$'
	|| (c >= '0' && c <= '9')
	|| (c >= 'a' && c <= 'z')
	|| (c >= 'A' && c <= 'Z')
	|| c == '<');
       )
    {
      if (c == '<')
	{
	  int i = namelen;
	  while (tokstart[++i] && tokstart[i] != '>');
	  if (tokstart[i] == '>')
	    namelen = i;
	}
       c = tokstart[++namelen];
     }

  /* The token "if" terminates the expression and is NOT 
     removed from the input stream.  */
  if (namelen == 2 && tokstart[0] == 'i' && tokstart[1] == 'f')
    {
      return 0;
    }

  lexptr += namelen;

  tryname:

  /* Catch specific keywords.  Should be done with a data structure.  */
  switch (namelen)
    {
    case 7:
      if (DEPRECATED_STREQN (tokstart, "boolean", 7))
	return BOOLEAN;
      break;
    case 6:
      if (DEPRECATED_STREQN (tokstart, "double", 6))      
	return DOUBLE;
      break;
    case 5:
      if (DEPRECATED_STREQN (tokstart, "short", 5))
	return SHORT;
      if (DEPRECATED_STREQN (tokstart, "false", 5))
	{
	  yylval.lval = 0;
	  return BOOLEAN_LITERAL;
	}
      if (DEPRECATED_STREQN (tokstart, "super", 5))
	return SUPER;
      if (DEPRECATED_STREQN (tokstart, "float", 5))
	return FLOAT;
      break;
    case 4:
      if (DEPRECATED_STREQN (tokstart, "long", 4))
	return LONG;
      if (DEPRECATED_STREQN (tokstart, "byte", 4))
	return BYTE;
      if (DEPRECATED_STREQN (tokstart, "char", 4))
	return CHAR;
      if (DEPRECATED_STREQN (tokstart, "true", 4))
	{
	  yylval.lval = 1;
	  return BOOLEAN_LITERAL;
	}
      break;
    case 3:
      if (strncmp (tokstart, "int", 3) == 0)
	return INT;
      if (strncmp (tokstart, "new", 3) == 0)
	return NEW;
      break;
    default:
      break;
    }

  yylval.sval.ptr = tokstart;
  yylval.sval.length = namelen;

  if (*tokstart == '$')
    {
      write_dollar_variable (yylval.sval);
      return VARIABLE;
    }

  /* Input names that aren't symbols but ARE valid hex numbers,
     when the input radix permits them, can be names or numbers
     depending on the parse.  Note we support radixes > 16 here.  */
  if (((tokstart[0] >= 'a' && tokstart[0] < 'a' + input_radix - 10) ||
       (tokstart[0] >= 'A' && tokstart[0] < 'A' + input_radix - 10)))
    {
      YYSTYPE newlval;	/* Its value is ignored.  */
      int hextype = parse_number (tokstart, namelen, 0, &newlval);
      if (hextype == INTEGER_LITERAL)
	return NAME_OR_INT;
    }
  return IDENTIFIER;
}

void
yyerror (msg)
     char *msg;
{
  if (prev_lexptr)
    lexptr = prev_lexptr;

  if (msg)
    error (_("%s: near `%s'"), msg, lexptr);
  else
    error (_("error in expression, near `%s'"), lexptr);
}

static struct type *
java_type_from_name (name)
     struct stoken name;
 
{
  char *tmp = copy_name (name);
  struct type *typ = java_lookup_class (tmp);
  if (typ == NULL || TYPE_CODE (typ) != TYPE_CODE_STRUCT)
    error (_("No class named `%s'"), tmp);
  return typ;
}

/* If NAME is a valid variable name in this scope, push it and return 1.
   Otherwise, return 0. */

static int
push_variable (struct stoken name)
{
  char *tmp = copy_name (name);
  int is_a_field_of_this = 0;
  struct symbol *sym;
  sym = lookup_symbol (tmp, expression_context_block, VAR_DOMAIN,
		       &is_a_field_of_this, (struct symtab **) NULL);
  if (sym && SYMBOL_CLASS (sym) != LOC_TYPEDEF)
    {
      if (symbol_read_needs_frame (sym))
	{
	  if (innermost_block == 0 ||
	      contained_in (block_found, innermost_block))
	    innermost_block = block_found;
	}

      write_exp_elt_opcode (OP_VAR_VALUE);
      /* We want to use the selected frame, not another more inner frame
	 which happens to be in the same block.  */
      write_exp_elt_block (NULL);
      write_exp_elt_sym (sym);
      write_exp_elt_opcode (OP_VAR_VALUE);
      return 1;
    }
  if (is_a_field_of_this)
    {
      /* it hangs off of `this'.  Must not inadvertently convert from a
	 method call to data ref.  */
      if (innermost_block == 0 || 
	  contained_in (block_found, innermost_block))
	innermost_block = block_found;
      write_exp_elt_opcode (OP_THIS);
      write_exp_elt_opcode (OP_THIS);
      write_exp_elt_opcode (STRUCTOP_PTR);
      write_exp_string (name);
      write_exp_elt_opcode (STRUCTOP_PTR);
      return 1;
    }
  return 0;
}

/* Assuming a reference expression has been pushed, emit the
   STRUCTOP_STRUCT ops to access the field named NAME.  If NAME is a
   qualified name (has '.'), generate a field access for each part. */

static void
push_fieldnames (name)
     struct stoken name;
{
  int i;
  struct stoken token;
  token.ptr = name.ptr;
  for (i = 0;  ;  i++)
    {
      if (i == name.length || name.ptr[i] == '.')
	{
	  /* token.ptr is start of current field name. */
	  token.length = &name.ptr[i] - token.ptr;
	  write_exp_elt_opcode (STRUCTOP_STRUCT);
	  write_exp_string (token);
	  write_exp_elt_opcode (STRUCTOP_STRUCT);
	  token.ptr += token.length + 1;
	}
      if (i >= name.length)
	break;
    }
}

/* Helper routine for push_expression_name.
   Handle a qualified name, where DOT_INDEX is the index of the first '.' */

static void
push_qualified_expression_name (struct stoken name, int dot_index)
{
  struct stoken token;
  char *tmp;
  struct type *typ;

  token.ptr = name.ptr;
  token.length = dot_index;

  if (push_variable (token))
    {
      token.ptr = name.ptr + dot_index + 1;
      token.length = name.length - dot_index - 1;
      push_fieldnames (token);
      return;
    }

  token.ptr = name.ptr;
  for (;;)
    {
      token.length = dot_index;
      tmp = copy_name (token);
      typ = java_lookup_class (tmp);
      if (typ != NULL)
	{
	  if (dot_index == name.length)
	    {
	      write_exp_elt_opcode(OP_TYPE);
	      write_exp_elt_type(typ);
	      write_exp_elt_opcode(OP_TYPE);
	      return;
	    }
	  dot_index++;  /* Skip '.' */
	  name.ptr += dot_index;
	  name.length -= dot_index;
	  dot_index = 0;
	  while (dot_index < name.length && name.ptr[dot_index] != '.') 
	    dot_index++;
	  token.ptr = name.ptr;
	  token.length = dot_index;
	  write_exp_elt_opcode (OP_SCOPE);
	  write_exp_elt_type (typ);
	  write_exp_string (token);
	  write_exp_elt_opcode (OP_SCOPE); 
	  if (dot_index < name.length)
	    {
	      dot_index++;
	      name.ptr += dot_index;
	      name.length -= dot_index;
	      push_fieldnames (name);
	    }
	  return;
	}
      else if (dot_index >= name.length)
	break;
      dot_index++;  /* Skip '.' */
      while (dot_index < name.length && name.ptr[dot_index] != '.')
	dot_index++;
    }
  error (_("unknown type `%.*s'"), name.length, name.ptr);
}

/* Handle Name in an expression (or LHS).
   Handle VAR, TYPE, TYPE.FIELD1....FIELDN and VAR.FIELD1....FIELDN. */

static void
push_expression_name (name)
     struct stoken name;
{
  char *tmp;
  struct type *typ;
  char *ptr;
  int i;

  for (i = 0;  i < name.length;  i++)
    {
      if (name.ptr[i] == '.')
	{
	  /* It's a Qualified Expression Name. */
	  push_qualified_expression_name (name, i);
	  return;
	}
    }

  /* It's a Simple Expression Name. */
  
  if (push_variable (name))
    return;
  tmp = copy_name (name);
  typ = java_lookup_class (tmp);
  if (typ != NULL)
    {
      write_exp_elt_opcode(OP_TYPE);
      write_exp_elt_type(typ);
      write_exp_elt_opcode(OP_TYPE);
    }
  else
    {
      struct minimal_symbol *msymbol;

      msymbol = lookup_minimal_symbol (tmp, NULL, NULL);
      if (msymbol != NULL)
	{
	  write_exp_msymbol (msymbol,
			     lookup_function_type (builtin_type_int),
			     builtin_type_int);
	}
      else if (!have_full_symbols () && !have_partial_symbols ())
	error (_("No symbol table is loaded.  Use the \"file\" command"));
      else
	error (_("No symbol \"%s\" in current context"), tmp);
    }

}


/* The following two routines, copy_exp and insert_exp, aren't specific to
   Java, so they could go in parse.c, but their only purpose is to support
   the parsing kludges we use in this file, so maybe it's best to isolate
   them here.  */

/* Copy the expression whose last element is at index ENDPOS - 1 in EXPR
   into a freshly malloc'ed struct expression.  Its language_defn is set
   to null.  */
static struct expression *
copy_exp (expr, endpos)
     struct expression *expr;
     int endpos;
{
  int len = length_of_subexp (expr, endpos);
  struct expression *new
    = (struct expression *) malloc (sizeof (*new) + EXP_ELEM_TO_BYTES (len));
  new->nelts = len;
  memcpy (new->elts, expr->elts + endpos - len, EXP_ELEM_TO_BYTES (len));
  new->language_defn = 0;

  return new;
}

/* Insert the expression NEW into the current expression (expout) at POS.  */
static void
insert_exp (pos, new)
     int pos;
     struct expression *new;
{
  int newlen = new->nelts;

  /* Grow expout if necessary.  In this function's only use at present,
     this should never be necessary.  */
  if (expout_ptr + newlen > expout_size)
    {
      expout_size = max (expout_size * 2, expout_ptr + newlen + 10);
      expout = (struct expression *)
	realloc ((char *) expout, (sizeof (struct expression)
				    + EXP_ELEM_TO_BYTES (expout_size)));
    }

  {
    int i;

    for (i = expout_ptr - 1; i >= pos; i--)
      expout->elts[i + newlen] = expout->elts[i];
  }
  
  memcpy (expout->elts + pos, new->elts, EXP_ELEM_TO_BYTES (newlen));
  expout_ptr += newlen;
}
