%{
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 *
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 */

#include <dt_impl.h>

#define	OP1(op, c)	dt_node_op1(op, c)
#define	OP2(op, l, r)	dt_node_op2(op, l, r)
#define	OP3(x, y, z)	dt_node_op3(x, y, z)
#define	LINK(l, r)	dt_node_link(l, r)
#define	DUP(s)		strdup(s)

%}

%union {
	dt_node_t *l_node;
	dt_decl_t *l_decl;
	char *l_str;
	uintmax_t l_int;
	int l_tok;
}

%token	DT_TOK_COMMA DT_TOK_ELLIPSIS
%token	DT_TOK_ASGN DT_TOK_ADD_EQ DT_TOK_SUB_EQ DT_TOK_MUL_EQ
%token	DT_TOK_DIV_EQ DT_TOK_MOD_EQ DT_TOK_AND_EQ DT_TOK_XOR_EQ DT_TOK_OR_EQ
%token	DT_TOK_LSH_EQ DT_TOK_RSH_EQ DT_TOK_QUESTION DT_TOK_COLON
%token	DT_TOK_LOR DT_TOK_LXOR DT_TOK_LAND
%token	DT_TOK_BOR DT_TOK_XOR DT_TOK_BAND DT_TOK_EQU DT_TOK_NEQ
%token	DT_TOK_LT DT_TOK_LE DT_TOK_GT DT_TOK_GE DT_TOK_LSH DT_TOK_RSH
%token	DT_TOK_ADD DT_TOK_SUB DT_TOK_MUL DT_TOK_DIV DT_TOK_MOD
%token	DT_TOK_LNEG DT_TOK_BNEG DT_TOK_ADDADD DT_TOK_SUBSUB
%token	DT_TOK_PREINC DT_TOK_POSTINC DT_TOK_PREDEC DT_TOK_POSTDEC
%token	DT_TOK_IPOS DT_TOK_INEG DT_TOK_DEREF DT_TOK_ADDROF
%token	DT_TOK_OFFSETOF DT_TOK_SIZEOF DT_TOK_STRINGOF DT_TOK_XLATE
%token	DT_TOK_LPAR DT_TOK_RPAR DT_TOK_LBRAC DT_TOK_RBRAC DT_TOK_PTR DT_TOK_DOT

%token <l_str>	DT_TOK_STRING
%token <l_str>	DT_TOK_IDENT
%token <l_str>	DT_TOK_PSPEC
%token <l_str>	DT_TOK_AGG
%token <l_str>	DT_TOK_TNAME
%token <l_int>	DT_TOK_INT

%token	DT_KEY_AUTO
%token	DT_KEY_BREAK
%token	DT_KEY_CASE
%token	DT_KEY_CHAR
%token	DT_KEY_CONST
%token	DT_KEY_CONTINUE
%token	DT_KEY_COUNTER
%token	DT_KEY_DEFAULT
%token	DT_KEY_DO
%token	DT_KEY_DOUBLE
%token	DT_KEY_ELSE
%token	DT_KEY_ENUM
%token	DT_KEY_EXTERN
%token	DT_KEY_FLOAT
%token	DT_KEY_FOR
%token	DT_KEY_GOTO
%token	DT_KEY_IF
%token	DT_KEY_IMPORT
%token	DT_KEY_INLINE
%token	DT_KEY_INT
%token	DT_KEY_LONG
%token	DT_KEY_PROBE
%token	DT_KEY_PROVIDER
%token	DT_KEY_REGISTER
%token	DT_KEY_RESTRICT
%token	DT_KEY_RETURN
%token	DT_KEY_SELF
%token	DT_KEY_SHORT
%token	DT_KEY_SIGNED
%token	DT_KEY_STATIC
%token	DT_KEY_STRING
%token	DT_KEY_STRUCT
%token	DT_KEY_SWITCH
%token	DT_KEY_THIS
%token	DT_KEY_TYPEDEF
%token	DT_KEY_UNION
%token	DT_KEY_UNSIGNED
%token	DT_KEY_USERLAND
%token	DT_KEY_VOID
%token	DT_KEY_VOLATILE
%token	DT_KEY_WHILE
%token	DT_KEY_XLATOR

%token	DT_TOK_EPRED
%token	DT_CTX_DEXPR
%token	DT_CTX_DPROG
%token	DT_CTX_DTYPE
%token	DT_TOK_EOF	0

%left	DT_TOK_COMMA
%right	DT_TOK_ASGN DT_TOK_ADD_EQ DT_TOK_SUB_EQ DT_TOK_MUL_EQ DT_TOK_DIV_EQ
	DT_TOK_MOD_EQ DT_TOK_AND_EQ DT_TOK_XOR_EQ DT_TOK_OR_EQ DT_TOK_LSH_EQ
	DT_TOK_RSH_EQ
%left	DT_TOK_QUESTION DT_TOK_COLON
%left	DT_TOK_LOR
%left	DT_TOK_LXOR
%left	DT_TOK_LAND
%left	DT_TOK_BOR
%left	DT_TOK_XOR
%left	DT_TOK_BAND
%left	DT_TOK_EQU DT_TOK_NEQ
%left	DT_TOK_LT DT_TOK_LE DT_TOK_GT DT_TOK_GE
%left	DT_TOK_LSH DT_TOK_RSH
%left	DT_TOK_ADD DT_TOK_SUB
%left	DT_TOK_MUL DT_TOK_DIV DT_TOK_MOD
%right	DT_TOK_LNEG DT_TOK_BNEG DT_TOK_ADDADD DT_TOK_SUBSUB
	DT_TOK_IPOS DT_TOK_INEG
%right	DT_TOK_DEREF DT_TOK_ADDROF DT_TOK_SIZEOF DT_TOK_STRINGOF DT_TOK_XLATE
%left	DT_TOK_LPAR DT_TOK_RPAR DT_TOK_LBRAC DT_TOK_RBRAC DT_TOK_PTR DT_TOK_DOT

%type	<l_node>	d_expression
%type	<l_node>	d_program
%type	<l_node>	d_type

%type	<l_node>	translation_unit
%type	<l_node>	external_declaration
%type	<l_node>	inline_definition
%type	<l_node>	translator_definition
%type	<l_node>	translator_member_list
%type	<l_node>	translator_member
%type	<l_node>	provider_definition
%type	<l_node>	provider_probe_list
%type	<l_node>	provider_probe
%type	<l_node>	probe_definition
%type	<l_node>	probe_specifiers
%type	<l_node>	probe_specifier_list
%type	<l_node>	probe_specifier
%type	<l_node>	statement_list
%type	<l_node>	statement_list_impl
%type	<l_node>	statement_or_block
%type	<l_node>	statement
%type	<l_node>	declaration
%type	<l_node>	init_declarator_list
%type	<l_node>	init_declarator

%type	<l_decl>	type_specifier
%type	<l_decl>	type_qualifier
%type	<l_decl>	struct_or_union_specifier
%type	<l_decl>	specifier_qualifier_list
%type	<l_decl>	enum_specifier
%type	<l_decl>	declarator
%type	<l_decl>	direct_declarator
%type	<l_decl>	pointer
%type	<l_decl>	type_qualifier_list
%type	<l_decl>	type_name
%type	<l_decl>	abstract_declarator
%type	<l_decl>	direct_abstract_declarator

%type	<l_node>	parameter_type_list
%type	<l_node>	parameter_list
%type	<l_node>	parameter_declaration

%type	<l_node>	array
%type	<l_node>	array_parameters
%type	<l_node>	function
%type	<l_node>	function_parameters

%type	<l_node>	expression
%type	<l_node>	assignment_expression
%type	<l_node>	conditional_expression
%type	<l_node>	constant_expression
%type	<l_node>	logical_or_expression
%type	<l_node>	logical_xor_expression
%type	<l_node>	logical_and_expression
%type	<l_node>	inclusive_or_expression
%type	<l_node>	exclusive_or_expression
%type	<l_node>	and_expression
%type	<l_node>	equality_expression
%type	<l_node>	relational_expression
%type	<l_node>	shift_expression
%type	<l_node>	additive_expression
%type	<l_node>	multiplicative_expression
%type	<l_node>	cast_expression
%type	<l_node>	unary_expression
%type	<l_node>	postfix_expression
%type	<l_node>	primary_expression
%type	<l_node>	argument_expression_list

%type	<l_tok>		assignment_operator
%type	<l_tok>		unary_operator
%type	<l_tok>		struct_or_union

%type	<l_str>		dtrace_keyword_ident

%%

dtrace_program: d_expression DT_TOK_EOF { return (dt_node_root($1)); }
	|	d_program DT_TOK_EOF { return (dt_node_root($1)); }
	|	d_type DT_TOK_EOF { return (dt_node_root($1)); }
	;

d_expression:	DT_CTX_DEXPR { $$ = NULL; }
	|	DT_CTX_DEXPR expression { $$ = $2; }
	;

d_program:	DT_CTX_DPROG { $$ = dt_node_program(NULL); }
	|	DT_CTX_DPROG translation_unit { $$ = dt_node_program($2); }
	;

d_type:		DT_CTX_DTYPE { $$ = NULL; }
	|	DT_CTX_DTYPE type_name { $$ = (dt_node_t *)$2; }
	;

translation_unit:
		external_declaration
	|	translation_unit external_declaration { $$ = LINK($1, $2); }
	;

external_declaration:
		inline_definition
	|	translator_definition
	|	provider_definition
	|	probe_definition
	|	declaration
	;

inline_definition:
		DT_KEY_INLINE declaration_specifiers declarator
		    { dt_scope_push(NULL, CTF_ERR); } DT_TOK_ASGN
		    assignment_expression ';' {
			/*
			 * We push a new declaration scope before shifting the
			 * assignment_expression in order to preserve ds_class
			 * and ds_ident for use in dt_node_inline().  Once the
			 * entire inline_definition rule is matched, pop the
			 * scope and construct the inline using the saved decl.
			 */
			dt_scope_pop();
			$$ = dt_node_inline($6);
		}
	;

translator_definition:
		DT_KEY_XLATOR type_name DT_TOK_LT type_name
		    DT_TOK_IDENT DT_TOK_GT '{' translator_member_list '}' ';' {
			$$ = dt_node_xlator($2, $4, $5, $8);
		}
	|	DT_KEY_XLATOR type_name DT_TOK_LT type_name
		    DT_TOK_IDENT DT_TOK_GT '{' '}' ';' {
			$$ = dt_node_xlator($2, $4, $5, NULL);
		}
	;

translator_member_list:
		translator_member
	|	translator_member_list translator_member { $$ = LINK($1,$2); }
	;

translator_member:
		DT_TOK_IDENT DT_TOK_ASGN assignment_expression ';' {
			$$ = dt_node_member(NULL, $1, $3);
		}
	;

provider_definition:
		DT_KEY_PROVIDER DT_TOK_IDENT '{' provider_probe_list '}' ';' {
			$$ = dt_node_provider($2, $4);
		}
	|	DT_KEY_PROVIDER DT_TOK_IDENT '{' '}' ';' {
			$$ = dt_node_provider($2, NULL);
		}
	;

provider_probe_list:
		provider_probe
	|	provider_probe_list provider_probe { $$ = LINK($1, $2); }
	;

provider_probe:
		DT_KEY_PROBE DT_TOK_IDENT function DT_TOK_COLON function ';' {
			$$ = dt_node_probe($2, 2, $3, $5);
		}
	|	DT_KEY_PROBE DT_TOK_IDENT function ';' {
			$$ = dt_node_probe($2, 1, $3, NULL);
		}
	;
	

probe_definition:
		probe_specifiers {
			/*
			 * If the input stream is a file, do not permit a probe
			 * specification without / <pred> / or { <act> } after
			 * it.  This can only occur if the next token is EOF or
			 * an ambiguous predicate was slurped up as a comment.
			 * We cannot perform this check if input() is a string
			 * because dtrace(1M) [-fmnP] also use the compiler and
			 * things like dtrace -n BEGIN have to be accepted.
			 */
			if (yypcb->pcb_fileptr != NULL) {
				dnerror($1, D_SYNTAX, "expected predicate and/"
				    "or actions following probe description\n");
			}
			$$ = dt_node_clause($1, NULL, NULL);
			yybegin(YYS_CLAUSE);
		}
	|	probe_specifiers '{' statement_list '}' {
			$$ = dt_node_clause($1, NULL, $3);
			yybegin(YYS_CLAUSE);
		}
	|	probe_specifiers DT_TOK_DIV expression DT_TOK_EPRED {
			dnerror($3, D_SYNTAX, "expected actions { } following "
			    "probe description and predicate\n");
		}
	|	probe_specifiers DT_TOK_DIV expression DT_TOK_EPRED
		    '{' statement_list '}' {
			$$ = dt_node_clause($1, $3, $6);
			yybegin(YYS_CLAUSE);
		}
	;

probe_specifiers:
		probe_specifier_list { yybegin(YYS_EXPR); $$ = $1; }
	;

probe_specifier_list:
		probe_specifier
	|	probe_specifier_list DT_TOK_COMMA probe_specifier {
			$$ = LINK($1, $3);
		}
	;

probe_specifier:
		DT_TOK_PSPEC { $$ = dt_node_pdesc_by_name($1); }
	|	DT_TOK_INT   { $$ = dt_node_pdesc_by_id($1); }
	;

statement_list_impl: /* empty */ { $$ = NULL; }
	|	statement_list_impl statement { $$ = LINK($1, $2); }
	;

statement_list:
		statement_list_impl { $$ = $1; }
	|	statement_list_impl expression {
			$$ = LINK($1, dt_node_statement($2));
		}
	;

statement_or_block:
		statement
	|	'{' statement_list '}' { $$ = $2; }

statement:	';' { $$ = NULL; }
	|	expression ';' { $$ = dt_node_statement($1); }
	|	DT_KEY_IF DT_TOK_LPAR expression DT_TOK_RPAR statement_or_block {
			$$ = dt_node_if($3, $5, NULL);
		}
	|	DT_KEY_IF DT_TOK_LPAR expression DT_TOK_RPAR
		statement_or_block DT_KEY_ELSE statement_or_block {
			$$ = dt_node_if($3, $5, $7);
		}
	;

argument_expression_list:
		assignment_expression
	|	argument_expression_list DT_TOK_COMMA assignment_expression {
			$$ = LINK($1, $3);
		}
	;

primary_expression:
		DT_TOK_IDENT { $$ = dt_node_ident($1); }
	|	DT_TOK_AGG { $$ = dt_node_ident($1); }
	|	DT_TOK_INT { $$ = dt_node_int($1); }
	|	DT_TOK_STRING { $$ = dt_node_string($1); }
	|	DT_KEY_SELF { $$ = dt_node_ident(DUP("self")); }
	|	DT_KEY_THIS { $$ = dt_node_ident(DUP("this")); }
	|	DT_TOK_LPAR expression DT_TOK_RPAR { $$ = $2; }
	;

postfix_expression:
		primary_expression
	|	postfix_expression
		    DT_TOK_LBRAC argument_expression_list DT_TOK_RBRAC {
			$$ = OP2(DT_TOK_LBRAC, $1, $3);
		}
	|	postfix_expression DT_TOK_LPAR DT_TOK_RPAR {
			$$ = dt_node_func($1, NULL);
		}
	|	postfix_expression
		    DT_TOK_LPAR argument_expression_list DT_TOK_RPAR {
			$$ = dt_node_func($1, $3);
		}
	|	postfix_expression DT_TOK_DOT DT_TOK_IDENT {
			$$ = OP2(DT_TOK_DOT, $1, dt_node_ident($3));
		}
	|	postfix_expression DT_TOK_DOT DT_TOK_TNAME {
			$$ = OP2(DT_TOK_DOT, $1, dt_node_ident($3));
		}
	|	postfix_expression DT_TOK_DOT dtrace_keyword_ident {
			$$ = OP2(DT_TOK_DOT, $1, dt_node_ident($3));
		}
	|	postfix_expression DT_TOK_PTR DT_TOK_IDENT {
			$$ = OP2(DT_TOK_PTR, $1, dt_node_ident($3));
		}
	|	postfix_expression DT_TOK_PTR DT_TOK_TNAME {
			$$ = OP2(DT_TOK_PTR, $1, dt_node_ident($3));
		}
	|	postfix_expression DT_TOK_PTR dtrace_keyword_ident {
			$$ = OP2(DT_TOK_PTR, $1, dt_node_ident($3));
		}
	|	postfix_expression DT_TOK_ADDADD {
			$$ = OP1(DT_TOK_POSTINC, $1);
		}
	|	postfix_expression DT_TOK_SUBSUB {
			$$ = OP1(DT_TOK_POSTDEC, $1);
		}
	|	DT_TOK_OFFSETOF DT_TOK_LPAR type_name DT_TOK_COMMA 
		    DT_TOK_IDENT DT_TOK_RPAR {
			$$ = dt_node_offsetof($3, $5);
		}
	|	DT_TOK_OFFSETOF DT_TOK_LPAR type_name DT_TOK_COMMA 
		    DT_TOK_TNAME DT_TOK_RPAR {
			$$ = dt_node_offsetof($3, $5);
		}
	|	DT_TOK_OFFSETOF DT_TOK_LPAR type_name DT_TOK_COMMA
		    dtrace_keyword_ident DT_TOK_RPAR {
			$$ = dt_node_offsetof($3, $5);
		}
	|	DT_TOK_XLATE DT_TOK_LT type_name DT_TOK_GT
		    DT_TOK_LPAR expression DT_TOK_RPAR {
			$$ = OP2(DT_TOK_XLATE, dt_node_type($3), $6);
		}
	;

unary_expression:
		postfix_expression
	|	DT_TOK_ADDADD unary_expression { $$ = OP1(DT_TOK_PREINC, $2); }
	|	DT_TOK_SUBSUB unary_expression { $$ = OP1(DT_TOK_PREDEC, $2); }
	|	unary_operator cast_expression { $$ = OP1($1, $2); }
	|	DT_TOK_SIZEOF unary_expression { $$ = OP1(DT_TOK_SIZEOF, $2); }
	|	DT_TOK_SIZEOF DT_TOK_LPAR type_name DT_TOK_RPAR {
			$$ = OP1(DT_TOK_SIZEOF, dt_node_type($3));
		}
	|	DT_TOK_STRINGOF unary_expression {
			$$ = OP1(DT_TOK_STRINGOF, $2);
		}
	;

unary_operator:	DT_TOK_BAND { $$ = DT_TOK_ADDROF; }
	|	DT_TOK_MUL { $$ = DT_TOK_DEREF; }
	|	DT_TOK_ADD { $$ = DT_TOK_IPOS; }
	|	DT_TOK_SUB { $$ = DT_TOK_INEG; }
	|	DT_TOK_BNEG { $$ = DT_TOK_BNEG; }
	|	DT_TOK_LNEG { $$ = DT_TOK_LNEG; }
	;

cast_expression:
		unary_expression
	|	DT_TOK_LPAR type_name DT_TOK_RPAR cast_expression {
			$$ = OP2(DT_TOK_LPAR, dt_node_type($2), $4);
		}
	;

multiplicative_expression:
		cast_expression
	|	multiplicative_expression DT_TOK_MUL cast_expression {
			$$ = OP2(DT_TOK_MUL, $1, $3);
		}
	|	multiplicative_expression DT_TOK_DIV cast_expression {
			$$ = OP2(DT_TOK_DIV, $1, $3);
		}
	|	multiplicative_expression DT_TOK_MOD cast_expression {
			$$ = OP2(DT_TOK_MOD, $1, $3);
		}
	;

additive_expression:
		multiplicative_expression
	|	additive_expression DT_TOK_ADD multiplicative_expression {
			$$ = OP2(DT_TOK_ADD, $1, $3);
		}
	|	additive_expression DT_TOK_SUB multiplicative_expression {
			$$ = OP2(DT_TOK_SUB, $1, $3);
		}
	;

shift_expression:
		additive_expression
	|	shift_expression DT_TOK_LSH additive_expression {
			$$ = OP2(DT_TOK_LSH, $1, $3);
		}
	|	shift_expression DT_TOK_RSH additive_expression {
			$$ = OP2(DT_TOK_RSH, $1, $3);
		}
	;

relational_expression:
		shift_expression
	|	relational_expression DT_TOK_LT shift_expression {
			$$ = OP2(DT_TOK_LT, $1, $3);
		}
	|	relational_expression DT_TOK_GT shift_expression {
			$$ = OP2(DT_TOK_GT, $1, $3);
		}
	|	relational_expression DT_TOK_LE shift_expression {
			$$ = OP2(DT_TOK_LE, $1, $3);
		}
	|	relational_expression DT_TOK_GE shift_expression {
			$$ = OP2(DT_TOK_GE, $1, $3);
		}
	;

equality_expression:
		relational_expression
	|	equality_expression DT_TOK_EQU relational_expression {
			$$ = OP2(DT_TOK_EQU, $1, $3);
		}
	|	equality_expression DT_TOK_NEQ relational_expression {
			$$ = OP2(DT_TOK_NEQ, $1, $3);
		}
	;

and_expression:
		equality_expression
	|	and_expression DT_TOK_BAND equality_expression {
			$$ = OP2(DT_TOK_BAND, $1, $3);
		}
	;

exclusive_or_expression:
		and_expression
	|	exclusive_or_expression DT_TOK_XOR and_expression {
			$$ = OP2(DT_TOK_XOR, $1, $3);
		}
	;

inclusive_or_expression:
		exclusive_or_expression
	|	inclusive_or_expression DT_TOK_BOR exclusive_or_expression {
			$$ = OP2(DT_TOK_BOR, $1, $3);
		}
	;

logical_and_expression:
		inclusive_or_expression
	|	logical_and_expression DT_TOK_LAND inclusive_or_expression {
			$$ = OP2(DT_TOK_LAND, $1, $3);
		}
	;

logical_xor_expression:
		logical_and_expression
	|	logical_xor_expression DT_TOK_LXOR logical_and_expression {
			$$ = OP2(DT_TOK_LXOR, $1, $3);
		}
	;

logical_or_expression:
		logical_xor_expression
	|	logical_or_expression DT_TOK_LOR logical_xor_expression {
			$$ = OP2(DT_TOK_LOR, $1, $3);
		}
	;

constant_expression: conditional_expression
	;

conditional_expression:
		logical_or_expression
	|	logical_or_expression DT_TOK_QUESTION expression DT_TOK_COLON
		    conditional_expression { $$ = OP3($1, $3, $5); }
	;

assignment_expression:
		conditional_expression
	|	unary_expression assignment_operator assignment_expression {
			$$ = OP2($2, $1, $3);
		}
	;

assignment_operator:
		DT_TOK_ASGN   { $$ = DT_TOK_ASGN; }
	|	DT_TOK_MUL_EQ { $$ = DT_TOK_MUL_EQ; }
	|	DT_TOK_DIV_EQ { $$ = DT_TOK_DIV_EQ; }
	|	DT_TOK_MOD_EQ { $$ = DT_TOK_MOD_EQ; }
	|	DT_TOK_ADD_EQ { $$ = DT_TOK_ADD_EQ; }
	|	DT_TOK_SUB_EQ { $$ = DT_TOK_SUB_EQ; }
	|	DT_TOK_LSH_EQ { $$ = DT_TOK_LSH_EQ; }
	|	DT_TOK_RSH_EQ { $$ = DT_TOK_RSH_EQ; }
	|	DT_TOK_AND_EQ { $$ = DT_TOK_AND_EQ; }
	|	DT_TOK_XOR_EQ { $$ = DT_TOK_XOR_EQ; }
	|	DT_TOK_OR_EQ  { $$ = DT_TOK_OR_EQ; }
	;

expression:	assignment_expression
	|	expression DT_TOK_COMMA assignment_expression {
			$$ = OP2(DT_TOK_COMMA, $1, $3);
		}
	;

declaration:	declaration_specifiers ';' {
			$$ = dt_node_decl();
			dt_decl_free(dt_decl_pop());
			yybegin(YYS_CLAUSE);
		}
	|	declaration_specifiers init_declarator_list ';' {
			$$ = $2;
			dt_decl_free(dt_decl_pop());
			yybegin(YYS_CLAUSE);
		}
	;

declaration_specifiers:
		d_storage_class_specifier
	|	d_storage_class_specifier declaration_specifiers
	|	type_specifier
	|	type_specifier declaration_specifiers
	|	type_qualifier
	|	type_qualifier declaration_specifiers
	;

parameter_declaration_specifiers:
		storage_class_specifier
	|	storage_class_specifier declaration_specifiers
	|	type_specifier
	|	type_specifier declaration_specifiers
	|	type_qualifier
	|	type_qualifier declaration_specifiers
	;

storage_class_specifier:
		DT_KEY_AUTO { dt_decl_class(DT_DC_AUTO); }
	|	DT_KEY_REGISTER { dt_decl_class(DT_DC_REGISTER); }
	|	DT_KEY_STATIC { dt_decl_class(DT_DC_STATIC); }
	|	DT_KEY_EXTERN { dt_decl_class(DT_DC_EXTERN); }
	|	DT_KEY_TYPEDEF { dt_decl_class(DT_DC_TYPEDEF); }
	;

d_storage_class_specifier:
		storage_class_specifier
	|	DT_KEY_SELF { dt_decl_class(DT_DC_SELF); }
	|	DT_KEY_THIS { dt_decl_class(DT_DC_THIS); }
	;

type_specifier:	DT_KEY_VOID { $$ = dt_decl_spec(CTF_K_INTEGER, DUP("void")); }
	|	DT_KEY_CHAR { $$ = dt_decl_spec(CTF_K_INTEGER, DUP("char")); }
	|	DT_KEY_SHORT { $$ = dt_decl_attr(DT_DA_SHORT); }
	|	DT_KEY_INT { $$ = dt_decl_spec(CTF_K_INTEGER, DUP("int")); }
	|	DT_KEY_LONG { $$ = dt_decl_attr(DT_DA_LONG); }
	|	DT_KEY_FLOAT { $$ = dt_decl_spec(CTF_K_FLOAT, DUP("float")); }
	|	DT_KEY_DOUBLE { $$ = dt_decl_spec(CTF_K_FLOAT, DUP("double")); }
	|	DT_KEY_SIGNED { $$ = dt_decl_attr(DT_DA_SIGNED); }
	|	DT_KEY_UNSIGNED { $$ = dt_decl_attr(DT_DA_UNSIGNED); }
	|	DT_KEY_USERLAND { $$ = dt_decl_attr(DT_DA_USER); }
	|	DT_KEY_STRING {
			$$ = dt_decl_spec(CTF_K_TYPEDEF, DUP("string"));
		}
	|	DT_TOK_TNAME { $$ = dt_decl_spec(CTF_K_TYPEDEF, $1); }
	|	struct_or_union_specifier
	|	enum_specifier
	;

type_qualifier:	DT_KEY_CONST { $$ = dt_decl_attr(DT_DA_CONST); }
	|	DT_KEY_RESTRICT { $$ = dt_decl_attr(DT_DA_RESTRICT); }
	|	DT_KEY_VOLATILE { $$ = dt_decl_attr(DT_DA_VOLATILE); }
	;

struct_or_union_specifier:
		struct_or_union_definition struct_declaration_list '}' {
			$$ = dt_scope_pop();
		}
	|	struct_or_union DT_TOK_IDENT { $$ = dt_decl_spec($1, $2); }
	|	struct_or_union DT_TOK_TNAME { $$ = dt_decl_spec($1, $2); }
	;

struct_or_union_definition:
		struct_or_union '{' { dt_decl_sou($1, NULL); }
	|	struct_or_union DT_TOK_IDENT '{' { dt_decl_sou($1, $2); }
	|	struct_or_union DT_TOK_TNAME '{' { dt_decl_sou($1, $2); }
	;

struct_or_union:
		DT_KEY_STRUCT { $$ = CTF_K_STRUCT; }
	|	DT_KEY_UNION { $$ = CTF_K_UNION; }
	;

struct_declaration_list:
		struct_declaration
	|	struct_declaration_list struct_declaration
	;

init_declarator_list:
		init_declarator
	|	init_declarator_list DT_TOK_COMMA init_declarator {
			$$ = LINK($1, $3);
		}
	;

init_declarator:
		declarator {
			$$ = dt_node_decl();
			dt_decl_reset();
		}
	;

struct_declaration:
		specifier_qualifier_list struct_declarator_list ';' {
			dt_decl_free(dt_decl_pop());
		}
	;

specifier_qualifier_list:
		type_specifier
	|	type_specifier specifier_qualifier_list { $$ = $2; }
	|	type_qualifier
	|	type_qualifier specifier_qualifier_list { $$ = $2; }
	;

struct_declarator_list:
		struct_declarator
	|	struct_declarator_list DT_TOK_COMMA struct_declarator
	;

struct_declarator:
		declarator { dt_decl_member(NULL); }
	|	DT_TOK_COLON constant_expression { dt_decl_member($2); }
	|	declarator DT_TOK_COLON constant_expression {
			dt_decl_member($3);
		}
	;

enum_specifier:
		enum_definition enumerator_list '}' { $$ = dt_scope_pop(); }
	|	DT_KEY_ENUM DT_TOK_IDENT { $$ = dt_decl_spec(CTF_K_ENUM, $2); }
	|	DT_KEY_ENUM DT_TOK_TNAME { $$ = dt_decl_spec(CTF_K_ENUM, $2); }
	;

enum_definition:
		DT_KEY_ENUM '{' { dt_decl_enum(NULL); }
	|	DT_KEY_ENUM DT_TOK_IDENT '{' { dt_decl_enum($2); }
	|	DT_KEY_ENUM DT_TOK_TNAME '{' { dt_decl_enum($2); }
	;

enumerator_list:
		enumerator
	|	enumerator_list DT_TOK_COMMA enumerator
	;

enumerator:	DT_TOK_IDENT { dt_decl_enumerator($1, NULL); }
	|	DT_TOK_IDENT DT_TOK_ASGN expression {
			dt_decl_enumerator($1, $3);
		}
	;

declarator:	direct_declarator
	|	pointer direct_declarator
	;

direct_declarator:
		DT_TOK_IDENT { $$ = dt_decl_ident($1); }
	|	lparen declarator DT_TOK_RPAR { $$ = $2; }
	|	direct_declarator array { dt_decl_array($2); }
	|	direct_declarator function { dt_decl_func($1, $2); }
	;

lparen:		DT_TOK_LPAR { dt_decl_top()->dd_attr |= DT_DA_PAREN; }
	;

pointer:	DT_TOK_MUL { $$ = dt_decl_ptr(); }
	|	DT_TOK_MUL type_qualifier_list { $$ = dt_decl_ptr(); }
	|	DT_TOK_MUL pointer { $$ = dt_decl_ptr(); }
	|	DT_TOK_MUL type_qualifier_list pointer { $$ = dt_decl_ptr(); }
	;

type_qualifier_list:
		type_qualifier
	|	type_qualifier_list type_qualifier { $$ = $2; }
	;

parameter_type_list:
		parameter_list
	|	DT_TOK_ELLIPSIS { $$ = dt_node_vatype(); }
	|	parameter_list DT_TOK_COMMA DT_TOK_ELLIPSIS {
			$$ = LINK($1, dt_node_vatype());
		}
	;

parameter_list:	parameter_declaration
	|	parameter_list DT_TOK_COMMA parameter_declaration {
			$$ = LINK($1, $3);
		}
	;

parameter_declaration:
		parameter_declaration_specifiers {
			$$ = dt_node_type(NULL);
		}
	|	parameter_declaration_specifiers declarator {
			$$ = dt_node_type(NULL);
		}
	|	parameter_declaration_specifiers abstract_declarator {
			$$ = dt_node_type(NULL);
		}
	;

type_name:	specifier_qualifier_list {
			$$ = dt_decl_pop();
		}
	|	specifier_qualifier_list abstract_declarator {
			$$ = dt_decl_pop();
		}
	;

abstract_declarator:
		pointer
	|	direct_abstract_declarator
	|	pointer direct_abstract_declarator
	;

direct_abstract_declarator:
		lparen abstract_declarator DT_TOK_RPAR { $$ = $2; }
	|	direct_abstract_declarator array { dt_decl_array($2); }
	|	array { dt_decl_array($1); $$ = NULL; }
	|	direct_abstract_declarator function { dt_decl_func($1, $2); }
	|	function { dt_decl_func(NULL, $1); }
	;

array:		DT_TOK_LBRAC { dt_scope_push(NULL, CTF_ERR); }
		    array_parameters DT_TOK_RBRAC {
			dt_scope_pop();
			$$ = $3;
		}
	;

array_parameters:
		/* empty */ 		{ $$ = NULL; }
	|	constant_expression	{ $$ = $1; }
	|	parameter_type_list	{ $$ = $1; }
	;

function:	DT_TOK_LPAR { dt_scope_push(NULL, CTF_ERR); }
		    function_parameters DT_TOK_RPAR {
			dt_scope_pop();
			$$ = $3;
		}
	;

function_parameters:
		/* empty */ 		{ $$ = NULL; }
	|	parameter_type_list	{ $$ = $1; }
	;

dtrace_keyword_ident:
	  DT_KEY_PROBE { $$ = DUP("probe"); }
	| DT_KEY_PROVIDER { $$ = DUP("provider"); }
	| DT_KEY_SELF { $$ = DUP("self"); }
	| DT_KEY_STRING { $$ = DUP("string"); }
	| DT_TOK_STRINGOF { $$ = DUP("stringof"); }
	| DT_KEY_USERLAND { $$ = DUP("userland"); }
	| DT_TOK_XLATE { $$ = DUP("xlate"); }
	| DT_KEY_XLATOR { $$ = DUP("translator"); }
	;

%%
