/*
 * demonstrate enhancements derived from btyacc:
 * backtracking to resolve conflicts
 * semantic disambiguation via []-actions invoking YYVALID & YYERROR
 * %locations
 * @$ & @N to refer to lhs & rhs symbol location
 * %destructor
 * syntactic suger for inherited attributes
 * extension to %type to define inherited attribute type
 */

%LOCATIONS

%{
/* dummy types just for compile check */
typedef int Code;
typedef int Decl_List;
typedef int Expr;
typedef int Expr_List;
typedef int Scope;
typedef int Type;
enum Operator { ADD, SUB, MUL, MOD, DIV, DEREF };

typedef unsigned char bool;
typedef struct Decl {
    Scope *scope;
    Type  *type;
    bool (*istype)(void);
} Decl;

#include "btyacc_demo.tab.h"
#include <stdlib.h>
#include <stdio.h>
%}

%union {
    Scope	*scope;
    Expr	*expr;
    Expr_List	*elist;
    Type	*type;
    Decl	*decl;
    Decl_List	*dlist;
    Code	*code;
    char	*id;
    };

%left '+' '-'
%left '*' '/' '%'
%nonassoc PREFIX
%nonassoc POSTFIX '(' '[' '.'

%token <id>	ID
%token <expr>	CONSTANT
%token		EXTERN REGISTER STATIC CONST VOLATILE IF THEN ELSE CLCL

%type <expr>	expr(<scope>)
%type		decl(<scope>) declarator_list(<scope>, <type>)
		decl_list(<scope>)
%type <code>	statement(<scope>) statement_list(<scope>)
		block_statement(<scope>)
%type <decl>	declarator(<scope>, <type>) formal_arg(<scope>)
%type <type>	decl_specs(<scope>) decl_spec(<scope>) typename(<scope>)
		cv_quals cv_qual
%type <scope>	opt_scope(<scope>)
%type <dlist>	formal_arg_list(<scope>) nonempty_formal_arg_list(<scope>)

%destructor	{ // 'msg' is a 'char *' indicating the context of destructor invocation
		  printf("%s accessed by symbol \"decl\" (case s.b. 273) @ position[%d,%d..%d,%d]\n",
			 msg,
			 @$.first_line, @$.first_column,
			 @$.last_line, @$.last_column);
		  free($<decl>$->scope); free($<decl>$->type); } decl
%destructor	{ printf("%s accessed by symbol with type <decl> (case s.b. 279 & 280) @ position[%d,%d..%d,%d]\n",
			 msg,
			 @$.first_line, @$.first_column,
			 @$.last_line, @$.last_column);
		  free($$); } <decl>
%destructor	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 @$.first_line, @$.first_column,
			 @$.last_line, @$.last_column);
		  free($$); } <*>
%destructor	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 @$.first_line, @$.first_column,
			 @$.last_line, @$.last_column);
		  /* in this example, we don't know what to do here */ } <>

%start input

%%

opt_scope($e):		[ $$ = $e; ]
  | CLCL		[ $$ = global_scope; ]
  | opt_scope ID CLCL	[ Decl *d = lookup($1, $2);
			  if (!d || !d->scope) YYERROR;
			  $$ = d->scope; ]
  ;

typename($e): opt_scope ID
      [ Decl *d = lookup($1, $2);
	if (d == NULL || d->istype() == 0) YYERROR;
	$$ = d->type; ]
  ;

input: decl_list(global_scope = new_scope(0)) ;
decl_list($e): | decl_list decl($e) ;
decl($e):
    decl_specs declarator_list($e,$1) ';' [YYVALID;]
  | decl_specs declarator($e,$1) block_statement(start_fn_def($e, $2))
      { /* demonstrate use of @$ & @N, although this is just the
	   default computation and so is not necessary */
	@$.first_line   = @1.first_line;
	@$.first_column = @1.first_column;
	@$.last_line    = @3.last_line;
	@$.last_column  = @3.last_column;
	finish_fn_def($2, $3); }
  ;

decl_specs($e):	
    decl_spec			[ $$ = $1; ]
  | decl_specs decl_spec($e)	[ $$ = type_combine($1, $2); ]
  ;

cv_quals:			[ $$ = 0; ]
  | cv_quals cv_qual		[ $$ = type_combine($1, $2); ]
  ;

decl_spec($e):
    cv_qual		[ $$ = $1; ]
  | typename		[ $$ = $1; ]
  | EXTERN		[ $$ = bare_extern(); ]
  | REGISTER		[ $$ = bare_register(); ]
  | STATIC		[ $$ = bare_static(); ]
  ;

cv_qual:
    CONST		[ $$ = bare_const(); ]
  | VOLATILE		[ $$ = bare_volatile(); ]
  ;

declarator_list($e, $t):
    declarator_list ',' declarator($e, $t)
  | declarator
  ;

declarator($e, $t):
    /* empty */			[ if (!$t) YYERROR; ]	
				{ $$ = declare($e, 0, $t); }
  | ID				{ $$ = declare($e, $1, $t); }
  | '(' declarator($e, $t) ')'	{ $$ = $2; }
  | '*' cv_quals declarator($e, $t) %prec PREFIX
	  { $$ = make_pointer($3, $2); }
  | declarator '[' expr($e) ']'
	  { $$ = make_array($1->type, $3); }
  | declarator '(' formal_arg_list($e) ')' cv_quals
	  { $$ = build_function($1, $3, $5); }
  ;

formal_arg_list($e):		{ $$ = 0; }
  | nonempty_formal_arg_list	{ $$ = $1; }
  ;
nonempty_formal_arg_list($e):
    nonempty_formal_arg_list ',' formal_arg($e)	{ $$ = append_dlist($1, $3); }
  | formal_arg					{ $$ = build_dlist($1); }
  ;
formal_arg($e):
    decl_specs declarator($e,$1)	{ $$ = $2; }
  ;

expr($e):
    expr '+' expr($e)		{ $$ = build_expr($1, ADD, $3); }
  | expr '-' expr($e)		{ $$ = build_expr($1, SUB, $3); }
  | expr '*' expr($e)		{ $$ = build_expr($1, MUL, $3); }
  | expr '%' expr($e)		{ $$ = build_expr($1, MOD, $3); }
  | expr '/' expr($e)		{ $$ = build_expr($1, DIV, $3); }
  | '*' expr($e) %prec PREFIX	{ $$ = build_expr(0, DEREF, $2); }
  | ID				{ $$ = var_expr($e, $1); }
  | CONSTANT			{ $$ = $1; }
  ;

statement($e):
    decl			{ $$ = 0; }
  | expr($e) ';' [YYVALID;]	{ $$ = build_expr_code($1); }
  | IF '(' expr($e) ')' THEN statement($e) ELSE statement($e) [YYVALID;]
    { $$ = build_if($3, $6, $8); }
  | IF '(' expr($e) ')' THEN statement($e) [YYVALID;]
    { $$ = build_if($3, $6, 0); }
  | block_statement(new_scope($e)) [YYVALID;]{ $$ = $1; }
  ;

statement_list($e):			{ $$ = 0; }
  | statement_list statement($e)	{ $$ = code_append($1, $2); }
  ;

block_statement($e):
    '{' statement_list($e) '}' { $$ = $2; }
  ;
%%

extern int YYLEX_DECL();
extern void YYERROR_DECL();

extern Scope *global_scope;

extern Decl * lookup(Scope *scope, char *id);
extern Scope * new_scope(Scope *outer_scope);
extern Scope * start_fn_def(Scope *scope, Decl *fn_decl);
extern void finish_fn_def(Decl *fn_decl, Code *block);
extern Type * type_combine(Type *specs, Type *spec);
extern Type * bare_extern(void);
extern Type * bare_register(void);
extern Type * bare_static(void);
extern Type * bare_const(void);
extern Type * bare_volatile(void);
extern Decl * declare(Scope *scope, char *id, Type *type);
extern Decl * make_pointer(Decl *decl, Type *type);
extern Decl * make_array(Type *type, Expr *expr);
extern Decl * build_function(Decl *decl, Decl_List *dlist, Type *type);
extern Decl_List * append_dlist(Decl_List *dlist, Decl *decl);
extern Decl_List * build_dlist(Decl *decl);
extern Expr * build_expr(Expr *left, enum Operator op, Expr *right);
extern Expr * var_expr(Scope *scope, char *id);
extern Code * build_expr_code(Expr *expr);
extern Code * build_if(Expr *cond_expr, Code *then_stmt, Code *else_stmt);
extern Code * code_append(Code *stmt_list, Code *stmt);
