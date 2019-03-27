/* $Id: grammar.y,v 1.5 2012/01/15 20:00:59 tom Exp $
 *
 * yacc grammar for C function prototype generator
 * This was derived from the grammar in Appendix A of
 * "The C Programming Language" by Kernighan and Ritchie.
 */
%expect 1
%{
#ifdef YYBISON
#include <stdlib.h>
#define YYSTYPE_IS_DECLARED
#define yyerror yaccError
#endif

#if defined(YYBISON) || !defined(YYBYACC)
static void yyerror(const char *s);
#endif
%}

%token <text> '(' '*' '&'
	/* identifiers that are not reserved words */
	T_IDENTIFIER T_TYPEDEF_NAME T_DEFINE_NAME

	/* storage class */
	T_AUTO T_EXTERN T_REGISTER T_STATIC T_TYPEDEF
	/* This keyword included for compatibility with C++. */
	T_INLINE
	/* This keyword included for compatibility with GCC */
	T_EXTENSION

	/* type specifiers */
	T_CHAR T_DOUBLE T_FLOAT T_INT T_VOID
	T_LONG T_SHORT T_SIGNED T_UNSIGNED
	T_ENUM T_STRUCT T_UNION
	/* C9X new types */
	T_Bool T_Complex T_Imaginary

	/* type qualifiers */
	T_TYPE_QUALIFIER

	/* paired square brackets and everything between them: [ ... ] */
	T_BRACKETS

%token
	/* left brace */
	T_LBRACE
	/* all input to the matching right brace */
	T_MATCHRBRACE

	/* three periods */
	T_ELLIPSIS

	/* constant expression or paired braces following an equal sign */
	T_INITIALIZER

	/* string literal */
	T_STRING_LITERAL

	/* asm */
	T_ASM
	/* ( "string literal" ) following asm keyword */
	T_ASMARG

	/* va_dcl from <varargs.h> */
	T_VA_DCL

%type <decl_spec> decl_specifiers decl_specifier
%type <decl_spec> storage_class type_specifier type_qualifier
%type <decl_spec> struct_or_union_specifier enum_specifier
%type <decl_list> init_declarator_list
%type <declarator> init_declarator declarator direct_declarator
%type <declarator> abs_declarator direct_abs_declarator
%type <param_list> parameter_type_list parameter_list
%type <parameter> parameter_declaration
%type <param_list> opt_identifier_list identifier_list
%type <text> struct_or_union pointer opt_type_qualifiers type_qualifier_list
	any_id identifier_or_ref
%type <text> enumeration

%{
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define OPT_LINTLIBRARY 1

#ifndef TRUE
#define	TRUE	(1)
#endif

#ifndef FALSE
#define	FALSE	(0)
#endif

/* #include "cproto.h" */
#define MAX_TEXT_SIZE 1024

/* Prototype styles */
#if OPT_LINTLIBRARY
#define PROTO_ANSI_LLIB		-2	/* form ANSI lint-library source */
#define PROTO_LINTLIBRARY	-1	/* form lint-library source */
#endif
#define PROTO_NONE		0	/* do not output any prototypes */
#define PROTO_TRADITIONAL	1	/* comment out parameters */
#define PROTO_ABSTRACT		2	/* comment out parameter names */
#define PROTO_ANSI		3	/* ANSI C prototype */

typedef int PrototypeStyle;

typedef char boolean;

extern boolean types_out;
extern PrototypeStyle proto_style;

#define ansiLintLibrary() (proto_style == PROTO_ANSI_LLIB)
#define knrLintLibrary()  (proto_style == PROTO_LINTLIBRARY)
#define lintLibrary()     (knrLintLibrary() || ansiLintLibrary())

#if OPT_LINTLIBRARY
#define FUNC_UNKNOWN		-1	/* unspecified */
#else
#define FUNC_UNKNOWN		0	/* unspecified (same as FUNC_NONE) */
#endif
#define FUNC_NONE		0	/* not a function definition */
#define FUNC_TRADITIONAL	1	/* traditional style */
#define FUNC_ANSI		2	/* ANSI style */
#define FUNC_BOTH		3	/* both styles */

typedef int FuncDefStyle;

/* Source file text */
typedef struct text {
    char text[MAX_TEXT_SIZE];	/* source text */
    long begin; 		/* offset in temporary file */
} Text;

/* Declaration specifier flags */
#define DS_NONE 	0	/* default */
#define DS_EXTERN	1	/* contains "extern" specifier */
#define DS_STATIC	2	/* contains "static" specifier */
#define DS_CHAR 	4	/* contains "char" type specifier */
#define DS_SHORT	8	/* contains "short" type specifier */
#define DS_FLOAT	16	/* contains "float" type specifier */
#define DS_INLINE	32	/* contains "inline" specifier */
#define DS_JUNK 	64	/* we're not interested in this declaration */

/* This structure stores information about a declaration specifier. */
typedef struct decl_spec {
    unsigned short flags;	/* flags defined above */
    char *text; 		/* source text */
    long begin; 		/* offset in temporary file */
} DeclSpec;

/* This is a list of function parameters. */
typedef struct _ParameterList {
    struct parameter *first;	/* pointer to first parameter in list */
    struct parameter *last;	/* pointer to last parameter in list */  
    long begin_comment; 	/* begin offset of comment */
    long end_comment;		/* end offset of comment */
    char *comment;		/* comment at start of parameter list */
} ParameterList;

/* This structure stores information about a declarator. */
typedef struct _Declarator {
    char *name; 			/* name of variable or function */
    char *text; 			/* source text */
    long begin; 			/* offset in temporary file */
    long begin_comment; 		/* begin offset of comment */
    long end_comment;			/* end offset of comment */
    FuncDefStyle func_def;		/* style of function definition */
    ParameterList params;		/* function parameters */
    boolean pointer;			/* TRUE if it declares a pointer */
    struct _Declarator *head;		/* head function declarator */
    struct _Declarator *func_stack;	/* stack of function declarators */
    struct _Declarator *next;		/* next declarator in list */
} Declarator;

/* This structure stores information about a function parameter. */
typedef struct parameter {
    struct parameter *next;	/* next parameter in list */
    DeclSpec decl_spec;
    Declarator *declarator;
    char *comment;		/* comment following the parameter */
} Parameter;

/* This is a list of declarators. */
typedef struct declarator_list {
    Declarator *first;		/* pointer to first declarator in list */
    Declarator *last;		/* pointer to last declarator in list */  
} DeclaratorList;

/* #include "symbol.h" */
typedef struct symbol {
    struct symbol *next;	/* next symbol in list */
    char *name; 		/* name of symbol */
    char *value;		/* value of symbol (for defines) */
    short flags;		/* symbol attributes */
} Symbol;

/* parser stack entry type */
typedef union {
    Text text;
    DeclSpec decl_spec;
    Parameter *parameter;
    ParameterList param_list;
    Declarator *declarator;
    DeclaratorList decl_list;
} YYSTYPE;

/* The hash table length should be a prime number. */
#define SYM_MAX_HASH 251

typedef struct symbol_table {
    Symbol *bucket[SYM_MAX_HASH];	/* hash buckets */
} SymbolTable;

extern SymbolTable *new_symbol_table	/* Create symbol table */
	(void);
extern void free_symbol_table		/* Destroy symbol table */
	(SymbolTable *s);
extern Symbol *find_symbol		/* Lookup symbol name */
	(SymbolTable *s, const char *n);
extern Symbol *new_symbol		/* Define new symbol */
	(SymbolTable *s, const char *n, const char *v, int f);

/* #include "semantic.h" */
extern void new_decl_spec (DeclSpec *, const char *, long, int);
extern void free_decl_spec (DeclSpec *);
extern void join_decl_specs (DeclSpec *, DeclSpec *, DeclSpec *);
extern void check_untagged (DeclSpec *);
extern Declarator *new_declarator (const char *, const char *, long);
extern void free_declarator (Declarator *);
extern void new_decl_list (DeclaratorList *, Declarator *);
extern void free_decl_list (DeclaratorList *);
extern void add_decl_list (DeclaratorList *, DeclaratorList *, Declarator *);
extern Parameter *new_parameter (DeclSpec *, Declarator *);
extern void free_parameter (Parameter *);
extern void new_param_list (ParameterList *, Parameter *);
extern void free_param_list (ParameterList *);
extern void add_param_list (ParameterList *, ParameterList *, Parameter *);
extern void new_ident_list (ParameterList *);
extern void add_ident_list (ParameterList *, ParameterList *, const char *);
extern void set_param_types (ParameterList *, DeclSpec *, DeclaratorList *);
extern void gen_declarations (DeclSpec *, DeclaratorList *);
extern void gen_prototype (DeclSpec *, Declarator *);
extern void gen_func_declarator (Declarator *);
extern void gen_func_definition (DeclSpec *, Declarator *);

extern void init_parser     (void);
extern void process_file    (FILE *infile, char *name);
extern char *cur_text       (void);
extern char *cur_file_name  (void);
extern char *implied_typedef (void);
extern void include_file    (char *name, int convert);
extern char *supply_parm    (int count);
extern char *xstrdup        (const char *);
extern int already_declared (char *name);
extern int is_actual_func   (Declarator *d);
extern int lint_ellipsis    (Parameter *p);
extern int want_typedef     (void);
extern void begin_tracking  (void);
extern void begin_typedef   (void);
extern void copy_typedef    (char *s);
extern void ellipsis_varargs (Declarator *d);
extern void end_typedef     (void);
extern void flush_varargs   (void);
extern void fmt_library     (int code);
extern void imply_typedef   (const char *s);
extern void indent          (FILE *outf);
extern void put_blankline   (FILE *outf);
extern void put_body        (FILE *outf, DeclSpec *decl_spec, Declarator *declarator);
extern void put_char        (FILE *outf, int c);
extern void put_error       (void);
extern void put_newline     (FILE *outf);
extern void put_padded      (FILE *outf, const char *s);
extern void put_string      (FILE *outf, const char *s);
extern void track_in        (void);

extern boolean file_comments;
extern FuncDefStyle func_style;
extern char base_file[];

extern	int	yylex (void);

/* declaration specifier attributes for the typedef statement currently being
 * scanned
 */
static int cur_decl_spec_flags;

/* pointer to parameter list for the current function definition */
static ParameterList *func_params;

/* A parser semantic action sets this pointer to the current declarator in
 * a function parameter declaration in order to catch any comments following
 * the parameter declaration on the same line.  If the lexer scans a comment
 * and <cur_declarator> is not NULL, then the comment is attached to the
 * declarator.  To ignore subsequent comments, the lexer sets this to NULL
 * after scanning a comment or end of line.
 */
static Declarator *cur_declarator;

/* temporary string buffer */
static char buf[MAX_TEXT_SIZE];

/* table of typedef names */
static SymbolTable *typedef_names;

/* table of define names */
static SymbolTable *define_names;

/* table of type qualifiers */
static SymbolTable *type_qualifiers;

/* information about the current input file */
typedef struct {
    char *base_name;		/* base input file name */
    char *file_name;		/* current file name */
    FILE *file; 		/* input file */
    unsigned line_num;		/* current line number in input file */
    FILE *tmp_file;		/* temporary file */
    long begin_comment; 	/* tmp file offset after last written ) or ; */
    long end_comment;		/* tmp file offset after last comment */
    boolean convert;		/* if TRUE, convert function definitions */
    boolean changed;		/* TRUE if conversion done in this file */
} IncludeStack;

static IncludeStack *cur_file;	/* current input file */

/* #include "yyerror.c" */

static int haveAnsiParam (void);


/* Flags to enable us to find if a procedure returns a value.
 */
static int return_val;	/* nonzero on BRACES iff return-expression found */

static const char *
dft_decl_spec (void)
{
    return (lintLibrary() && !return_val) ? "void" : "int";
}

static int
haveAnsiParam (void)
{
    Parameter *p;
    if (func_params != 0) {
	for (p = func_params->first; p != 0; p = p->next) {
	    if (p->declarator->func_def == FUNC_ANSI) {
		return TRUE;
	    }
	}
    }
    return FALSE;
}
%}
%%

program
	: /* empty */
	| translation_unit
	;

translation_unit
	: external_declaration
	| translation_unit external_declaration
	;

external_declaration
	: declaration
	| function_definition
	| ';'
	| linkage_specification
	| T_ASM T_ASMARG ';'
	| error T_MATCHRBRACE
	{
	    yyerrok;
	}
	| error ';'
	{
	    yyerrok;
	}
	;

braces
	: T_LBRACE T_MATCHRBRACE
	;

linkage_specification
	: T_EXTERN T_STRING_LITERAL braces
	{
	    /* Provide an empty action here so bison will not complain about
	     * incompatible types in the default action it normally would
	     * have generated.
	     */
	}
	| T_EXTERN T_STRING_LITERAL declaration
	{
	    /* empty */
	}
	;

declaration
	: decl_specifiers ';'
	{
#if OPT_LINTLIBRARY
	    if (types_out && want_typedef()) {
		gen_declarations(&$1, (DeclaratorList *)0);
		flush_varargs();
	    }
#endif
	    free_decl_spec(&$1);
	    end_typedef();
	}
	| decl_specifiers init_declarator_list ';'
	{
	    if (func_params != NULL) {
		set_param_types(func_params, &$1, &$2);
	    } else {
		gen_declarations(&$1, &$2);
#if OPT_LINTLIBRARY
		flush_varargs();
#endif
		free_decl_list(&$2);
	    }
	    free_decl_spec(&$1);
	    end_typedef();
	}
	| any_typedef decl_specifiers
	{
	    cur_decl_spec_flags = $2.flags;
	    free_decl_spec(&$2);
	}
	  opt_declarator_list ';'
	{
	    end_typedef();
	}
	;

any_typedef
	: T_EXTENSION T_TYPEDEF
	{
	    begin_typedef();
	}
	| T_TYPEDEF
	{
	    begin_typedef();
	}
	;

opt_declarator_list
	: /* empty */
	| declarator_list
	;

declarator_list
	: declarator
	{
	    int flags = cur_decl_spec_flags;

	    /* If the typedef is a pointer type, then reset the short type
	     * flags so it does not get promoted.
	     */
	    if (strcmp($1->text, $1->name) != 0)
		flags &= ~(DS_CHAR | DS_SHORT | DS_FLOAT);
	    new_symbol(typedef_names, $1->name, NULL, flags);
	    free_declarator($1);
	}
	| declarator_list ',' declarator
	{
	    int flags = cur_decl_spec_flags;

	    if (strcmp($3->text, $3->name) != 0)
		flags &= ~(DS_CHAR | DS_SHORT | DS_FLOAT);
	    new_symbol(typedef_names, $3->name, NULL, flags);
	    free_declarator($3);
	}
	;

function_definition
	: decl_specifiers declarator
	{
	    check_untagged(&$1);
	    if ($2->func_def == FUNC_NONE) {
		yyerror("syntax error");
		YYERROR;
	    }
	    func_params = &($2->head->params);
	    func_params->begin_comment = cur_file->begin_comment;
	    func_params->end_comment = cur_file->end_comment;
	}
	  opt_declaration_list T_LBRACE
	{
	    /* If we're converting to K&R and we've got a nominally K&R
	     * function which has a parameter which is ANSI (i.e., a prototyped
	     * function pointer), then we must override the deciphered value of
	     * 'func_def' so that the parameter will be converted.
	     */
	    if (func_style == FUNC_TRADITIONAL
	     && haveAnsiParam()
	     && $2->head->func_def == func_style) {
		$2->head->func_def = FUNC_BOTH;
	    }

	    func_params = NULL;

	    if (cur_file->convert)
		gen_func_definition(&$1, $2);
	    gen_prototype(&$1, $2);
#if OPT_LINTLIBRARY
	    flush_varargs();
#endif
	    free_decl_spec(&$1);
	    free_declarator($2);
	}
	  T_MATCHRBRACE
	| declarator
	{
	    if ($1->func_def == FUNC_NONE) {
		yyerror("syntax error");
		YYERROR;
	    }
	    func_params = &($1->head->params);
	    func_params->begin_comment = cur_file->begin_comment;
	    func_params->end_comment = cur_file->end_comment;
	}
	  opt_declaration_list T_LBRACE T_MATCHRBRACE
	{
	    DeclSpec decl_spec;

	    func_params = NULL;

	    new_decl_spec(&decl_spec, dft_decl_spec(), $1->begin, DS_NONE);
	    if (cur_file->convert)
		gen_func_definition(&decl_spec, $1);
	    gen_prototype(&decl_spec, $1);
#if OPT_LINTLIBRARY
	    flush_varargs();
#endif
	    free_decl_spec(&decl_spec);
	    free_declarator($1);
	}
	;

opt_declaration_list
	: /* empty */
	| T_VA_DCL
	| declaration_list
	;

declaration_list
	: declaration
	| declaration_list declaration
	;

decl_specifiers
	: decl_specifier
	| decl_specifiers decl_specifier
	{
	    join_decl_specs(&$$, &$1, &$2);
	    free($1.text);
	    free($2.text);
	}
	;

decl_specifier
	: storage_class
	| type_specifier
	| type_qualifier
	;

storage_class
	: T_AUTO
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_EXTERN
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_EXTERN);
	}
	| T_REGISTER
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_STATIC
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_STATIC);
	}
	| T_INLINE
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_INLINE);
	}
	| T_EXTENSION
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_JUNK);
	}
	;

type_specifier
	: T_CHAR
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_CHAR);
	}
	| T_DOUBLE
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_FLOAT
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_FLOAT);
	}
	| T_INT
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_LONG
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_SHORT
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_SHORT);
	}
	| T_SIGNED
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_UNSIGNED
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_VOID
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_Bool
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_CHAR);
	}
	| T_Complex
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_Imaginary
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_TYPEDEF_NAME
	{
	    Symbol *s;
	    s = find_symbol(typedef_names, $1.text);
	    if (s != NULL)
		new_decl_spec(&$$, $1.text, $1.begin, s->flags);
	}
	| struct_or_union_specifier
	| enum_specifier
	;

type_qualifier
	: T_TYPE_QUALIFIER
	{
	    new_decl_spec(&$$, $1.text, $1.begin, DS_NONE);
	}
	| T_DEFINE_NAME
	{
	    /* This rule allows the <pointer> nonterminal to scan #define
	     * names as if they were type modifiers.
	     */
	    Symbol *s;
	    s = find_symbol(define_names, $1.text);
	    if (s != NULL)
		new_decl_spec(&$$, $1.text, $1.begin, s->flags);
	}
	;

struct_or_union_specifier
	: struct_or_union any_id braces
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
	        (void)sprintf(s = buf, "%s %s", $1.text, $2.text);
	    new_decl_spec(&$$, s, $1.begin, DS_NONE);
	}
	| struct_or_union braces
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
		(void)sprintf(s = buf, "%s {}", $1.text);
	    new_decl_spec(&$$, s, $1.begin, DS_NONE);
	}
	| struct_or_union any_id
	{
	    (void)sprintf(buf, "%s %s", $1.text, $2.text);
	    new_decl_spec(&$$, buf, $1.begin, DS_NONE);
	}
	;

struct_or_union
	: T_STRUCT
	{
	    imply_typedef($$.text);
	}
	| T_UNION
	{
	    imply_typedef($$.text);
	}
	;

init_declarator_list
	: init_declarator
	{
	    new_decl_list(&$$, $1);
	}
	| init_declarator_list ',' init_declarator
	{
	    add_decl_list(&$$, &$1, $3);
	}
	;

init_declarator
	: declarator
	{
	    if ($1->func_def != FUNC_NONE && func_params == NULL &&
		func_style == FUNC_TRADITIONAL && cur_file->convert) {
		gen_func_declarator($1);
		fputs(cur_text(), cur_file->tmp_file);
	    }
	    cur_declarator = $$;
	}
	| declarator '='
	{
	    if ($1->func_def != FUNC_NONE && func_params == NULL &&
		func_style == FUNC_TRADITIONAL && cur_file->convert) {
		gen_func_declarator($1);
		fputs(" =", cur_file->tmp_file);
	    }
	}
	  T_INITIALIZER
	;

enum_specifier
	: enumeration any_id braces
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
		(void)sprintf(s = buf, "enum %s", $2.text);
	    new_decl_spec(&$$, s, $1.begin, DS_NONE);
	}
	| enumeration braces
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
		(void)sprintf(s = buf, "%s {}", $1.text);
	    new_decl_spec(&$$, s, $1.begin, DS_NONE);
	}
	| enumeration any_id
	{
	    (void)sprintf(buf, "enum %s", $2.text);
	    new_decl_spec(&$$, buf, $1.begin, DS_NONE);
	}
	;

enumeration
	: T_ENUM
	{
	    imply_typedef("enum");
	    $$ = $1;
	}
	;

any_id
	: T_IDENTIFIER
	| T_TYPEDEF_NAME
	;

declarator
	: pointer direct_declarator
	{
	    $$ = $2;
	    (void)sprintf(buf, "%s%s", $1.text, $$->text);
	    free($$->text);
	    $$->text = xstrdup(buf);
	    $$->begin = $1.begin;
	    $$->pointer = TRUE;
	}
	| direct_declarator
	;

direct_declarator
	: identifier_or_ref
	{
	    $$ = new_declarator($1.text, $1.text, $1.begin);
	}
	| '(' declarator ')'
	{
	    $$ = $2;
	    (void)sprintf(buf, "(%s)", $$->text);
	    free($$->text);
	    $$->text = xstrdup(buf);
	    $$->begin = $1.begin;
	}
	| direct_declarator T_BRACKETS
	{
	    $$ = $1;
	    (void)sprintf(buf, "%s%s", $$->text, $2.text);
	    free($$->text);
	    $$->text = xstrdup(buf);
	}
	| direct_declarator '(' parameter_type_list ')'
	{
	    $$ = new_declarator("%s()", $1->name, $1->begin);
	    $$->params = $3;
	    $$->func_stack = $1;
	    $$->head = ($1->func_stack == NULL) ? $$ : $1->head;
	    $$->func_def = FUNC_ANSI;
	}
	| direct_declarator '(' opt_identifier_list ')'
	{
	    $$ = new_declarator("%s()", $1->name, $1->begin);
	    $$->params = $3;
	    $$->func_stack = $1;
	    $$->head = ($1->func_stack == NULL) ? $$ : $1->head;
	    $$->func_def = FUNC_TRADITIONAL;
	}
	;

pointer
	: '*' opt_type_qualifiers
	{
	    (void)sprintf($$.text, "*%s", $2.text);
	    $$.begin = $1.begin;
	}
	| '*' opt_type_qualifiers pointer
	{
	    (void)sprintf($$.text, "*%s%s", $2.text, $3.text);
	    $$.begin = $1.begin;
	}
	;

opt_type_qualifiers
	: /* empty */
	{
	    strcpy($$.text, "");
	    $$.begin = 0L;
	}
	| type_qualifier_list
	;

type_qualifier_list
	: type_qualifier
	{
	    (void)sprintf($$.text, "%s ", $1.text);
	    $$.begin = $1.begin;
	    free($1.text);
	}
	| type_qualifier_list type_qualifier
	{
	    (void)sprintf($$.text, "%s%s ", $1.text, $2.text);
	    $$.begin = $1.begin;
	    free($2.text);
	}
	;

parameter_type_list
	: parameter_list
	| parameter_list ',' T_ELLIPSIS
	{
	    add_ident_list(&$$, &$1, "...");
	}
	;

parameter_list
	: parameter_declaration
	{
	    new_param_list(&$$, $1);
	}
	| parameter_list ',' parameter_declaration
	{
	    add_param_list(&$$, &$1, $3);
	}
	;

parameter_declaration
	: decl_specifiers declarator
	{
	    check_untagged(&$1);
	    $$ = new_parameter(&$1, $2);
	}
	| decl_specifiers abs_declarator
	{
	    check_untagged(&$1);
	    $$ = new_parameter(&$1, $2);
	}
	| decl_specifiers
	{
	    check_untagged(&$1);
	    $$ = new_parameter(&$1, (Declarator *)0);
	}
	;

opt_identifier_list
	: /* empty */
	{
	    new_ident_list(&$$);
	}
	| identifier_list
	;

identifier_list
	: any_id
	{
	    new_ident_list(&$$);
	    add_ident_list(&$$, &$$, $1.text);
	}
	| identifier_list ',' any_id
	{
	    add_ident_list(&$$, &$1, $3.text);
	}
	;

identifier_or_ref
	: any_id
	{
	    $$ = $1;
	}
	| '&' any_id
	{
#if OPT_LINTLIBRARY
	    if (lintLibrary()) { /* Lint doesn't grok C++ ref variables */
		$$ = $2;
	    } else
#endif
		(void)sprintf($$.text, "&%s", $2.text);
	    $$.begin = $1.begin;
	}
	;

abs_declarator
	: pointer
	{
	    $$ = new_declarator($1.text, "", $1.begin);
	}
	| pointer direct_abs_declarator
	{
	    $$ = $2;
	    (void)sprintf(buf, "%s%s", $1.text, $$->text);
	    free($$->text);
	    $$->text = xstrdup(buf);
	    $$->begin = $1.begin;
	}
	| direct_abs_declarator
	;

direct_abs_declarator
	: '(' abs_declarator ')'
	{
	    $$ = $2;
	    (void)sprintf(buf, "(%s)", $$->text);
	    free($$->text);
	    $$->text = xstrdup(buf);
	    $$->begin = $1.begin;
	}
	| direct_abs_declarator T_BRACKETS
	{
	    $$ = $1;
	    (void)sprintf(buf, "%s%s", $$->text, $2.text);
	    free($$->text);
	    $$->text = xstrdup(buf);
	}
	| T_BRACKETS
	{
	    $$ = new_declarator($1.text, "", $1.begin);
	}
	| direct_abs_declarator '(' parameter_type_list ')'
	{
	    $$ = new_declarator("%s()", "", $1->begin);
	    $$->params = $3;
	    $$->func_stack = $1;
	    $$->head = ($1->func_stack == NULL) ? $$ : $1->head;
	    $$->func_def = FUNC_ANSI;
	}
	| direct_abs_declarator '(' ')'
	{
	    $$ = new_declarator("%s()", "", $1->begin);
	    $$->func_stack = $1;
	    $$->head = ($1->func_stack == NULL) ? $$ : $1->head;
	    $$->func_def = FUNC_ANSI;
	}
	| '(' parameter_type_list ')'
	{
	    Declarator *d;

	    d = new_declarator("", "", $1.begin);
	    $$ = new_declarator("%s()", "", $1.begin);
	    $$->params = $2;
	    $$->func_stack = d;
	    $$->head = $$;
	    $$->func_def = FUNC_ANSI;
	}
	| '(' ')'
	{
	    Declarator *d;

	    d = new_declarator("", "", $1.begin);
	    $$ = new_declarator("%s()", "", $1.begin);
	    $$->func_stack = d;
	    $$->head = $$;
	    $$->func_def = FUNC_ANSI;
	}
	;

%%

/* lex.yy.c */
#define BEGIN yy_start = 1 + 2 *

#define CPP1 1
#define INIT1 2
#define INIT2 3
#define CURLY 4
#define LEXYACC 5
#define ASM 6
#define CPP_INLINE 7

extern char *yytext;
extern FILE *yyin, *yyout;

static int curly;			/* number of curly brace nesting levels */
static int ly_count;			/* number of occurances of %% */
static int inc_depth;			/* include nesting level */
static SymbolTable *included_files;	/* files already included */
static int yy_start = 0;		/* start state number */

#define grammar_error(s) yaccError(s)

static void
yaccError (const char *msg)
{
    func_params = NULL;
    put_error();		/* tell what line we're on, and what file */
    fprintf(stderr, "%s at token '%s'\n", msg, yytext);
}

/* Initialize the table of type qualifier keywords recognized by the lexical
 * analyzer.
 */
void
init_parser (void)
{
    static const char *keywords[] = {
	"const",
	"restrict",
	"volatile",
	"interrupt",
#ifdef vms
	"noshare",
	"readonly",
#endif
#if defined(MSDOS) || defined(OS2)
	"__cdecl",
	"__export",
	"__far",
	"__fastcall",
	"__fortran",
	"__huge",
	"__inline",
	"__interrupt",
	"__loadds",
	"__near",
	"__pascal",
	"__saveregs",
	"__segment",
	"__stdcall",
	"__syscall",
	"_cdecl",
	"_cs",
	"_ds",
	"_es",
	"_export",
	"_far",
	"_fastcall",
	"_fortran",
	"_huge",
	"_interrupt",
	"_loadds",
	"_near",
	"_pascal",
	"_saveregs",
	"_seg",
	"_segment",
	"_ss",
	"cdecl",
	"far",
	"huge",
	"near",
	"pascal",
#ifdef OS2
	"__far16",
#endif
#endif
#ifdef __GNUC__
	/* gcc aliases */
	"__builtin_va_arg",
	"__builtin_va_list",
	"__const",
	"__const__",
	"__inline",
	"__inline__",
	"__restrict",
	"__restrict__",
	"__volatile",
	"__volatile__",
#endif
    };
    unsigned i;

    /* Initialize type qualifier table. */
    type_qualifiers = new_symbol_table();
    for (i = 0; i < sizeof(keywords)/sizeof(keywords[0]); ++i) {
	new_symbol(type_qualifiers, keywords[i], NULL, DS_NONE);
    }
}

/* Process the C source file.  Write function prototypes to the standard
 * output.  Convert function definitions and write the converted source
 * code to a temporary file.
 */
void
process_file (FILE *infile, char *name)
{
    char *s;

    if (strlen(name) > 2) {
	s = name + strlen(name) - 2;
	if (*s == '.') {
	    ++s;
	    if (*s == 'l' || *s == 'y')
		BEGIN LEXYACC;
#if defined(MSDOS) || defined(OS2)
	    if (*s == 'L' || *s == 'Y')
		BEGIN LEXYACC;
#endif
	}
    }

    included_files = new_symbol_table();
    typedef_names = new_symbol_table();
    define_names = new_symbol_table();
    inc_depth = -1;
    curly = 0;
    ly_count = 0;
    func_params = NULL;
    yyin = infile;
    include_file(strcpy(base_file, name), func_style != FUNC_NONE);
    if (file_comments) {
#if OPT_LINTLIBRARY
    	if (lintLibrary()) {
	    put_blankline(stdout);
	    begin_tracking();
	}
#endif
	put_string(stdout, "/* ");
	put_string(stdout, cur_file_name());
	put_string(stdout, " */\n");
    }
    yyparse();
    free_symbol_table(define_names);
    free_symbol_table(typedef_names);
    free_symbol_table(included_files);
}

#ifdef NO_LEAKS
void
free_parser(void)
{
    free_symbol_table (type_qualifiers);
#ifdef FLEX_SCANNER
    if (yy_current_buffer != 0)
	yy_delete_buffer(yy_current_buffer);
#endif
}
#endif
