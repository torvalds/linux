/* original parser id follows */
/* yysccsid[] = "@(#)yaccpar	1.9 (Berkeley) 02/21/93" */
/* (use YYMAJOR/YYMINOR for ifdefs dependent on parser version) */

#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define YYCHECK "yyyymmdd"

#define YYEMPTY        (-1)
#define yyclearin      (yychar = YYEMPTY)
#define yyerrok        (yyerrflag = 0)
#define YYRECOVERING() (yyerrflag != 0)
#define YYENOMEM       (-2)
#define YYEOF          0
#undef YYBTYACC
#define YYBTYACC 0
#define YYDEBUGSTR YYPREFIX "debug"

#ifndef yyparse
#define yyparse    grammar_parse
#endif /* yyparse */

#ifndef yylex
#define yylex      grammar_lex
#endif /* yylex */

#ifndef yyerror
#define yyerror    grammar_error
#endif /* yyerror */

#ifndef yychar
#define yychar     grammar_char
#endif /* yychar */

#ifndef yyval
#define yyval      grammar_val
#endif /* yyval */

#ifndef yylval
#define yylval     grammar_lval
#endif /* yylval */

#ifndef yydebug
#define yydebug    grammar_debug
#endif /* yydebug */

#ifndef yynerrs
#define yynerrs    grammar_nerrs
#endif /* yynerrs */

#ifndef yyerrflag
#define yyerrflag  grammar_errflag
#endif /* yyerrflag */

#ifndef yylhs
#define yylhs      grammar_lhs
#endif /* yylhs */

#ifndef yylen
#define yylen      grammar_len
#endif /* yylen */

#ifndef yydefred
#define yydefred   grammar_defred
#endif /* yydefred */

#ifndef yystos
#define yystos     grammar_stos
#endif /* yystos */

#ifndef yydgoto
#define yydgoto    grammar_dgoto
#endif /* yydgoto */

#ifndef yysindex
#define yysindex   grammar_sindex
#endif /* yysindex */

#ifndef yyrindex
#define yyrindex   grammar_rindex
#endif /* yyrindex */

#ifndef yygindex
#define yygindex   grammar_gindex
#endif /* yygindex */

#ifndef yytable
#define yytable    grammar_table
#endif /* yytable */

#ifndef yycheck
#define yycheck    grammar_check
#endif /* yycheck */

#ifndef yyname
#define yyname     grammar_name
#endif /* yyname */

#ifndef yyrule
#define yyrule     grammar_rule
#endif /* yyrule */

#if YYBTYACC

#ifndef yycindex
#define yycindex   grammar_cindex
#endif /* yycindex */

#ifndef yyctable
#define yyctable   grammar_ctable
#endif /* yyctable */

#endif /* YYBTYACC */

#define YYPREFIX "grammar_"

#define YYPURE 0

#line 9 "grammar.y"
#ifdef YYBISON
#include <stdlib.h>
#define YYSTYPE_IS_DECLARED
#define yyerror yaccError
#endif

#if defined(YYBISON) || !defined(YYBYACC)
static void yyerror(const char *s);
#endif
#line 81 "grammar.y"
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
#line 408 "grammar.tab.c"

/* compatibility with bison */
#ifdef YYPARSE_PARAM
/* compatibility with FreeBSD */
# ifdef YYPARSE_PARAM_TYPE
#  define YYPARSE_DECL() yyparse(YYPARSE_PARAM_TYPE YYPARSE_PARAM)
# else
#  define YYPARSE_DECL() yyparse(void *YYPARSE_PARAM)
# endif
#else
# define YYPARSE_DECL() yyparse(void)
#endif

/* Parameters sent to lex. */
#ifdef YYLEX_PARAM
# define YYLEX_DECL() yylex(void *YYLEX_PARAM)
# define YYLEX yylex(YYLEX_PARAM)
#else
# define YYLEX_DECL() yylex(void)
# define YYLEX yylex()
#endif

/* Parameters sent to yyerror. */
#ifndef YYERROR_DECL
#define YYERROR_DECL() yyerror(const char *s)
#endif
#ifndef YYERROR_CALL
#define YYERROR_CALL(msg) yyerror(msg)
#endif

extern int YYPARSE_DECL();

#define T_IDENTIFIER 257
#define T_TYPEDEF_NAME 258
#define T_DEFINE_NAME 259
#define T_AUTO 260
#define T_EXTERN 261
#define T_REGISTER 262
#define T_STATIC 263
#define T_TYPEDEF 264
#define T_INLINE 265
#define T_EXTENSION 266
#define T_CHAR 267
#define T_DOUBLE 268
#define T_FLOAT 269
#define T_INT 270
#define T_VOID 271
#define T_LONG 272
#define T_SHORT 273
#define T_SIGNED 274
#define T_UNSIGNED 275
#define T_ENUM 276
#define T_STRUCT 277
#define T_UNION 278
#define T_Bool 279
#define T_Complex 280
#define T_Imaginary 281
#define T_TYPE_QUALIFIER 282
#define T_BRACKETS 283
#define T_LBRACE 284
#define T_MATCHRBRACE 285
#define T_ELLIPSIS 286
#define T_INITIALIZER 287
#define T_STRING_LITERAL 288
#define T_ASM 289
#define T_ASMARG 290
#define T_VA_DCL 291
#define YYERRCODE 256
typedef short YYINT;
static const YYINT grammar_lhs[] = {                     -1,
    0,    0,   26,   26,   27,   27,   27,   27,   27,   27,
   27,   31,   30,   30,   28,   28,   34,   28,   32,   32,
   33,   33,   35,   35,   37,   38,   29,   39,   29,   36,
   36,   36,   40,   40,    1,    1,    2,    2,    2,    3,
    3,    3,    3,    3,    3,    4,    4,    4,    4,    4,
    4,    4,    4,    4,    4,    4,    4,    4,    4,    4,
    5,    5,    6,    6,    6,   19,   19,    8,    8,    9,
   41,    9,    7,    7,    7,   25,   23,   23,   10,   10,
   11,   11,   11,   11,   11,   20,   20,   21,   21,   22,
   22,   14,   14,   15,   15,   16,   16,   16,   17,   17,
   18,   18,   24,   24,   12,   12,   12,   13,   13,   13,
   13,   13,   13,   13,
};
static const YYINT grammar_len[] = {                      2,
    0,    1,    1,    2,    1,    1,    1,    1,    3,    2,
    2,    2,    3,    3,    2,    3,    0,    5,    2,    1,
    0,    1,    1,    3,    0,    0,    7,    0,    5,    0,
    1,    1,    1,    2,    1,    2,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    3,    2,    2,    1,    1,    1,    3,    1,
    0,    4,    3,    2,    2,    1,    1,    1,    2,    1,
    1,    3,    2,    4,    4,    2,    3,    0,    1,    1,
    2,    1,    3,    1,    3,    2,    2,    1,    0,    1,
    1,    3,    1,    2,    1,    2,    1,    3,    2,    1,
    4,    3,    3,    2,
};
static const YYINT grammar_defred[] = {                   0,
    0,    0,    0,    0,   77,    0,   62,   40,    0,   42,
   43,   20,   44,    0,   46,   47,   48,   49,   54,   50,
   51,   52,   53,   76,   66,   67,   55,   56,   57,   61,
    0,    7,    0,    0,   35,   37,   38,   39,   59,   60,
   28,    0,    0,    0,  103,   81,    0,    0,    3,    5,
    6,    8,    0,   10,   11,   78,    0,   90,    0,    0,
  104,    0,   19,    0,   41,   45,   15,   36,    0,   68,
    0,    0,    0,   83,    0,    0,   64,    0,    0,   74,
    4,   58,    0,   82,   87,   91,    0,   14,   13,    9,
   16,    0,   71,    0,   31,   33,    0,    0,    0,    0,
    0,   94,    0,    0,  101,   12,   63,   73,    0,    0,
   69,    0,    0,    0,   34,    0,  110,   96,   97,    0,
    0,   84,    0,   85,    0,   23,    0,    0,   72,   26,
   29,  114,    0,    0,    0,  109,    0,   93,   95,  102,
   18,    0,    0,  108,  113,  112,    0,   24,   27,  111,
};
#if defined(YYDESTRUCT_CALL) || defined(YYSTYPE_TOSTRING)
static const YYINT grammar_stos[] = {                     0,
  256,   40,   42,   38,  257,  258,  259,  260,  261,  262,
  263,  264,  265,  266,  267,  268,  269,  270,  271,  272,
  273,  274,  275,  276,  277,  278,  279,  280,  281,  282,
  289,   59,  293,  294,  295,  296,  297,  298,  299,  300,
  303,  304,  312,  313,  316,  317,  318,  319,  320,  321,
  322,  323,  325,  285,   59,  258,  303,  298,  314,  315,
  316,  288,  264,  290,  261,  266,   59,  295,  301,  302,
  303,  332,   40,  283,  284,  316,  324,  304,  316,  324,
  320,  258,  294,   41,  313,  298,  294,  321,  324,   59,
   59,   44,   61,  330,  291,  321,  329,  333,  294,  307,
  308,  309,  310,  311,  316,  285,  324,  324,  327,  303,
  302,  334,  329,  284,  321,   40,  283,  303,  305,  306,
  313,   41,   44,   41,   44,  303,  326,  328,  287,  284,
  285,   41,  305,  307,   40,  283,  306,  286,  309,  316,
   59,   44,  331,   41,   41,   41,  307,  303,  285,   41,
};
#endif /* YYDESTRUCT_CALL || YYSTYPE_TOSTRING */
static const YYINT grammar_dgoto[] = {                   33,
   87,   35,   36,   37,   38,   39,   40,   69,   70,   41,
   42,  119,  120,  100,  101,  102,  103,  104,   43,   44,
   59,   60,   45,   46,   47,   48,   49,   50,   51,   52,
   77,   53,  127,  109,  128,   97,   94,  143,   72,   98,
  112,
};
static const YYINT grammar_sindex[] = {                  -2,
   -3,   27, -239, -177,    0,    0,    0,    0, -274,    0,
    0,    0,    0, -246,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
 -266,    0,    0,  455,    0,    0,    0,    0,    0,    0,
    0,  -35, -245,  128,    0,    0, -245,   -2,    0,    0,
    0,    0,  642,    0,    0,    0,  -15,    0,  -12, -239,
    0,  590,    0,  -27,    0,    0,    0,    0,  -10,    0,
  -11,  534,  -72,    0, -237, -232,    0,  -35, -232,    0,
    0,    0,  642,    0,    0,    0,  455,    0,    0,    0,
    0,   27,    0,  534,    0,    0, -222,  617,  209,   34,
   39,    0,   44,   42,    0,    0,    0,    0,   27,  -11,
    0, -200, -196, -195,    0,  174,    0,    0,    0,  -33,
  243,    0,  561,    0, -177,    0,   33,   49,    0,    0,
    0,    0,   53,   55,  417,    0,  -33,    0,    0,    0,
    0,   27, -188,    0,    0,    0,   57,    0,    0,    0,
};
static const YYINT grammar_rindex[] = {                  99,
    0,    0,  275,    0,    0,  -38,    0,    0,  481,    0,
    0,    0,    0,  509,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,   30,    0,    0,    0,    0,    0,  101,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,  343,  309,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   98, -182,   62,    0,    0,  133,    0,   64,  379,    0,
    0,    0,   -5,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0, -182,    0,    0,    0, -180,  -19,    0,
   65,    0,    0,   68,    0,    0,    0,    0,   51,    9,
    0,    0,    0,    0,    0,    0,    0,    0,    0,  -13,
   19,    0,    0,    0,    0,    0,    0,   52,    0,    0,
    0,    0,    0,    0,    0,    0,   35,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
#if YYBTYACC
static const YYINT grammar_cindex[] = {                   0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
#endif
static const YYINT grammar_gindex[] = {                   0,
   11,  -17,    0,    0,   13,    0,    0,    0,   20,    8,
  -43,   -1,   -8,  -89,    0,   -9,    0,    0,    0,  -44,
    0,    0,    4,    0,    0,    0,   70,  -53,    0,    0,
  -18,    0,    0,    0,    0,   22,    0,    0,    0,    0,
    0,
};
#define YYTABLESIZE 924
static const YYINT grammar_table[] = {                   58,
   78,   58,   58,   58,   73,   58,  135,   61,   88,   57,
   34,    5,   56,   62,   85,   58,   68,   63,   96,    7,
   58,   98,   78,   64,   98,   84,  134,  107,   80,    3,
  107,   90,   17,   92,   17,    4,   17,    2,   75,    3,
   96,   71,   30,   89,  115,  147,   76,  106,   91,   93,
   79,   75,   70,   17,  121,   55,   32,  107,   34,  105,
  108,  114,  105,   83,    4,   68,    2,   70,    3,   68,
   80,  121,   86,   80,  122,  106,  105,   78,  106,    5,
   56,   68,  123,   99,  124,  125,  129,  130,   80,  131,
   80,  141,  142,  144,  110,  145,  149,  150,    1,  110,
    2,   30,   99,   32,   79,   92,  118,   79,  100,   21,
   22,  111,  137,  139,  133,  113,  126,   81,    0,    0,
    0,    0,   79,   57,   79,    0,   99,    0,  140,    0,
    0,    0,    0,   99,    0,    0,    0,    0,    0,    0,
    0,   70,    0,    0,    0,   99,    0,    0,    0,  148,
    0,    0,    0,    0,    0,    0,   70,    0,    0,    0,
    0,    0,    0,    0,    0,    4,    0,    2,    0,    0,
   65,    0,   65,   65,   65,    0,   65,    0,    0,    0,
    0,    0,    0,    0,    5,    6,    7,    8,   65,   10,
   11,   65,   13,   66,   15,   16,   17,   18,   19,   20,
   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,
    0,    4,    0,  116,  132,    3,    0,    0,   58,   58,
   58,   58,   58,   58,   58,   78,   58,   58,   58,   58,
   58,   58,   58,   58,   58,   58,   58,   58,   58,   58,
   58,   58,   58,   58,   58,   78,    4,   74,  116,  136,
    3,   17,   78,    1,    5,    6,    7,    8,    9,   10,
   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,
    4,   54,  116,    5,   56,    0,   31,   80,   80,   80,
   80,   80,   80,   80,   80,   80,   80,   80,   80,   80,
   80,   80,   80,   80,   80,   80,   80,   80,   80,   80,
   80,   80,   88,   80,   88,   88,   88,    0,   88,    0,
   80,   79,   79,   79,   79,   79,   79,   79,   79,   79,
   79,   79,   79,   79,   79,   79,   79,   79,   79,   79,
   79,   79,   79,   79,   79,   79,   89,   79,   89,   89,
   89,    0,   89,    0,   79,   25,   25,   25,   25,   25,
   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,
   25,   25,   25,   25,   25,   25,   25,   25,   25,   25,
   86,   25,   86,   86,    5,   56,   86,    0,   25,   65,
   65,   65,   65,   65,   65,   65,    0,   65,   65,   65,
   65,   65,   65,   65,   65,   65,   65,   65,   65,   65,
   65,   65,   65,   65,   65,   65,   75,    0,   75,   75,
   75,    0,   75,    0,    0,    0,    0,    0,    0,    0,
    5,    6,    7,    8,   65,   10,   11,   75,   13,   66,
   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
   25,   26,   27,   28,   29,   30,  117,  146,    0,    0,
    0,    0,    0,    0,    0,    5,    6,    7,    8,   65,
   10,   11,    0,   13,   66,   15,   16,   17,   18,   19,
   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,
   30,  117,    4,    0,    2,    0,    3,    0,    0,    5,
   56,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,   67,    0,    0,    0,    0,   41,    0,
   41,    0,   41,    0,    0,  117,    0,    0,    0,    0,
    0,   88,   88,    0,    0,    0,    0,    0,    0,   41,
    0,    0,    0,    0,    0,    0,   45,    0,   45,    0,
   45,    0,    0,    0,    0,    0,    0,   88,    0,    0,
    0,    0,    0,    0,    0,   89,   89,   45,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,   89,    0,    0,    0,    0,    0,    0,    0,   86,
   86,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,   86,    0,    0,    0,    0,
    0,    0,    0,    0,    0,   75,   75,   75,   75,   75,
   75,   75,    0,   75,   75,   75,   75,   75,   75,   75,
   75,   75,   75,   75,   75,   75,   75,   75,   75,   75,
   75,   75,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,   82,    7,    8,   65,   10,   11,
    0,   13,   66,   15,   16,   17,   18,   19,   20,   21,
   22,   23,   24,   25,   26,   27,   28,   29,   30,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    5,    6,    7,    8,   65,   10,   11,    0,   13,
   66,   15,   16,   17,   18,   19,   20,   21,   22,   23,
   24,   25,   26,   27,   28,   29,   30,   41,   41,   41,
   41,   41,   41,   41,    0,   41,   41,   41,   41,   41,
   41,   41,   41,   41,   41,   41,   41,   41,   41,   41,
   41,   41,   41,    0,    0,   45,   45,   45,   45,   45,
   45,   45,    0,   45,   45,   45,   45,   45,   45,   45,
   45,   45,   45,   45,   45,   45,   45,   45,   45,   45,
   45,   82,    7,    8,   65,   10,   11,   12,   13,   14,
   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,
   25,   26,   27,   28,   29,   30,    0,    0,   82,    7,
    8,   65,   10,   11,   95,   13,   66,   15,   16,   17,
   18,   19,   20,   21,   22,   23,   24,   25,   26,   27,
   28,   29,   30,    0,    0,    0,  138,   82,    7,    8,
   65,   10,   11,   12,   13,   14,   15,   16,   17,   18,
   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
   29,   30,    0,   75,   82,    7,    8,   65,   10,   11,
   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,
   22,   23,   24,   25,   26,   27,   28,   29,   30,   82,
    7,    8,   65,   10,   11,    0,   13,   66,   15,   16,
   17,   18,   19,   20,   21,   22,   23,   24,   25,   26,
   27,   28,   29,   30,
};
static const YYINT grammar_check[] = {                   38,
   44,   40,   41,   42,   40,   44,   40,    4,   62,    2,
    0,  257,  258,  288,   59,    3,   34,  264,   72,  259,
   59,   41,   61,  290,   44,   41,  116,   41,   47,   42,
   44,   59,   38,   44,   40,   38,   42,   40,  284,   42,
   94,   34,  282,   62,   98,  135,   43,  285,   59,   61,
   47,  284,   44,   59,   99,   59,   59,   76,   48,   41,
   79,  284,   44,   53,   38,   83,   40,   59,   42,   87,
   41,  116,   60,   44,   41,   41,   73,  121,   44,  257,
  258,   99,   44,   73,   41,   44,  287,  284,   59,  285,
   61,   59,   44,   41,   87,   41,  285,   41,    0,   92,
    0,  284,   41,  284,   41,   41,   99,   44,   41,   59,
   59,   92,  121,  123,  116,   94,  109,   48,   -1,   -1,
   -1,   -1,   59,  116,   61,   -1,  116,   -1,  125,   -1,
   -1,   -1,   -1,  123,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   44,   -1,   -1,   -1,  135,   -1,   -1,   -1,  142,
   -1,   -1,   -1,   -1,   -1,   -1,   59,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   38,   -1,   40,   -1,   -1,
   38,   -1,   40,   41,   42,   -1,   44,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,  257,  258,  259,  260,  261,  262,
  263,   59,  265,  266,  267,  268,  269,  270,  271,  272,
  273,  274,  275,  276,  277,  278,  279,  280,  281,  282,
   -1,   38,   -1,   40,   41,   42,   -1,   -1,  257,  258,
  259,  260,  261,  262,  263,  264,  265,  266,  267,  268,
  269,  270,  271,  272,  273,  274,  275,  276,  277,  278,
  279,  280,  281,  282,  283,  284,   38,  283,   40,  283,
   42,  257,  291,  256,  257,  258,  259,  260,  261,  262,
  263,  264,  265,  266,  267,  268,  269,  270,  271,  272,
  273,  274,  275,  276,  277,  278,  279,  280,  281,  282,
   38,  285,   40,  257,  258,   -1,  289,  258,  259,  260,
  261,  262,  263,  264,  265,  266,  267,  268,  269,  270,
  271,  272,  273,  274,  275,  276,  277,  278,  279,  280,
  281,  282,   38,  284,   40,   41,   42,   -1,   44,   -1,
  291,  258,  259,  260,  261,  262,  263,  264,  265,  266,
  267,  268,  269,  270,  271,  272,  273,  274,  275,  276,
  277,  278,  279,  280,  281,  282,   38,  284,   40,   41,
   42,   -1,   44,   -1,  291,  258,  259,  260,  261,  262,
  263,  264,  265,  266,  267,  268,  269,  270,  271,  272,
  273,  274,  275,  276,  277,  278,  279,  280,  281,  282,
   38,  284,   40,   41,  257,  258,   44,   -1,  291,  257,
  258,  259,  260,  261,  262,  263,   -1,  265,  266,  267,
  268,  269,  270,  271,  272,  273,  274,  275,  276,  277,
  278,  279,  280,  281,  282,  283,   38,   -1,   40,   41,
   42,   -1,   44,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
  257,  258,  259,  260,  261,  262,  263,   59,  265,  266,
  267,  268,  269,  270,  271,  272,  273,  274,  275,  276,
  277,  278,  279,  280,  281,  282,  283,   41,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  257,  258,  259,  260,  261,
  262,  263,   -1,  265,  266,  267,  268,  269,  270,  271,
  272,  273,  274,  275,  276,  277,  278,  279,  280,  281,
  282,  283,   38,   -1,   40,   -1,   42,   -1,   -1,  257,
  258,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   59,   -1,   -1,   -1,   -1,   38,   -1,
   40,   -1,   42,   -1,   -1,  283,   -1,   -1,   -1,   -1,
   -1,  257,  258,   -1,   -1,   -1,   -1,   -1,   -1,   59,
   -1,   -1,   -1,   -1,   -1,   -1,   38,   -1,   40,   -1,
   42,   -1,   -1,   -1,   -1,   -1,   -1,  283,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  257,  258,   59,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,  283,   -1,   -1,   -1,   -1,   -1,   -1,   -1,  257,
  258,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  283,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  257,  258,  259,  260,  261,
  262,  263,   -1,  265,  266,  267,  268,  269,  270,  271,
  272,  273,  274,  275,  276,  277,  278,  279,  280,  281,
  282,  283,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,  258,  259,  260,  261,  262,  263,
   -1,  265,  266,  267,  268,  269,  270,  271,  272,  273,
  274,  275,  276,  277,  278,  279,  280,  281,  282,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,  257,  258,  259,  260,  261,  262,  263,   -1,  265,
  266,  267,  268,  269,  270,  271,  272,  273,  274,  275,
  276,  277,  278,  279,  280,  281,  282,  257,  258,  259,
  260,  261,  262,  263,   -1,  265,  266,  267,  268,  269,
  270,  271,  272,  273,  274,  275,  276,  277,  278,  279,
  280,  281,  282,   -1,   -1,  257,  258,  259,  260,  261,
  262,  263,   -1,  265,  266,  267,  268,  269,  270,  271,
  272,  273,  274,  275,  276,  277,  278,  279,  280,  281,
  282,  258,  259,  260,  261,  262,  263,  264,  265,  266,
  267,  268,  269,  270,  271,  272,  273,  274,  275,  276,
  277,  278,  279,  280,  281,  282,   -1,   -1,  258,  259,
  260,  261,  262,  263,  291,  265,  266,  267,  268,  269,
  270,  271,  272,  273,  274,  275,  276,  277,  278,  279,
  280,  281,  282,   -1,   -1,   -1,  286,  258,  259,  260,
  261,  262,  263,  264,  265,  266,  267,  268,  269,  270,
  271,  272,  273,  274,  275,  276,  277,  278,  279,  280,
  281,  282,   -1,  284,  258,  259,  260,  261,  262,  263,
  264,  265,  266,  267,  268,  269,  270,  271,  272,  273,
  274,  275,  276,  277,  278,  279,  280,  281,  282,  258,
  259,  260,  261,  262,  263,   -1,  265,  266,  267,  268,
  269,  270,  271,  272,  273,  274,  275,  276,  277,  278,
  279,  280,  281,  282,
};
#if YYBTYACC
static const YYINT grammar_ctable[] = {                  -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,
};
#endif
#define YYFINAL 33
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 291
#define YYUNDFTOKEN 335
#define YYTRANSLATE(a) ((a) > YYMAXTOKEN ? YYUNDFTOKEN : (a))
#if YYDEBUG
static const char *const grammar_name[] = {

"$end",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,"'&'",0,"'('","')'","'*'",0,"','",0,0,0,0,0,0,0,0,0,0,0,0,0,0,"';'",0,"'='",0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"error",
"T_IDENTIFIER","T_TYPEDEF_NAME","T_DEFINE_NAME","T_AUTO","T_EXTERN",
"T_REGISTER","T_STATIC","T_TYPEDEF","T_INLINE","T_EXTENSION","T_CHAR",
"T_DOUBLE","T_FLOAT","T_INT","T_VOID","T_LONG","T_SHORT","T_SIGNED",
"T_UNSIGNED","T_ENUM","T_STRUCT","T_UNION","T_Bool","T_Complex","T_Imaginary",
"T_TYPE_QUALIFIER","T_BRACKETS","T_LBRACE","T_MATCHRBRACE","T_ELLIPSIS",
"T_INITIALIZER","T_STRING_LITERAL","T_ASM","T_ASMARG","T_VA_DCL","$accept",
"program","decl_specifiers","decl_specifier","storage_class","type_specifier",
"type_qualifier","struct_or_union_specifier","enum_specifier",
"init_declarator_list","init_declarator","declarator","direct_declarator",
"abs_declarator","direct_abs_declarator","parameter_type_list","parameter_list",
"parameter_declaration","opt_identifier_list","identifier_list",
"struct_or_union","pointer","opt_type_qualifiers","type_qualifier_list",
"any_id","identifier_or_ref","enumeration","translation_unit",
"external_declaration","declaration","function_definition",
"linkage_specification","braces","any_typedef","opt_declarator_list","$$1",
"declarator_list","opt_declaration_list","$$2","$$3","$$4","declaration_list",
"$$5","illegal-symbol",
};
static const char *const grammar_rule[] = {
"$accept : program",
"program :",
"program : translation_unit",
"translation_unit : external_declaration",
"translation_unit : translation_unit external_declaration",
"external_declaration : declaration",
"external_declaration : function_definition",
"external_declaration : ';'",
"external_declaration : linkage_specification",
"external_declaration : T_ASM T_ASMARG ';'",
"external_declaration : error T_MATCHRBRACE",
"external_declaration : error ';'",
"braces : T_LBRACE T_MATCHRBRACE",
"linkage_specification : T_EXTERN T_STRING_LITERAL braces",
"linkage_specification : T_EXTERN T_STRING_LITERAL declaration",
"declaration : decl_specifiers ';'",
"declaration : decl_specifiers init_declarator_list ';'",
"$$1 :",
"declaration : any_typedef decl_specifiers $$1 opt_declarator_list ';'",
"any_typedef : T_EXTENSION T_TYPEDEF",
"any_typedef : T_TYPEDEF",
"opt_declarator_list :",
"opt_declarator_list : declarator_list",
"declarator_list : declarator",
"declarator_list : declarator_list ',' declarator",
"$$2 :",
"$$3 :",
"function_definition : decl_specifiers declarator $$2 opt_declaration_list T_LBRACE $$3 T_MATCHRBRACE",
"$$4 :",
"function_definition : declarator $$4 opt_declaration_list T_LBRACE T_MATCHRBRACE",
"opt_declaration_list :",
"opt_declaration_list : T_VA_DCL",
"opt_declaration_list : declaration_list",
"declaration_list : declaration",
"declaration_list : declaration_list declaration",
"decl_specifiers : decl_specifier",
"decl_specifiers : decl_specifiers decl_specifier",
"decl_specifier : storage_class",
"decl_specifier : type_specifier",
"decl_specifier : type_qualifier",
"storage_class : T_AUTO",
"storage_class : T_EXTERN",
"storage_class : T_REGISTER",
"storage_class : T_STATIC",
"storage_class : T_INLINE",
"storage_class : T_EXTENSION",
"type_specifier : T_CHAR",
"type_specifier : T_DOUBLE",
"type_specifier : T_FLOAT",
"type_specifier : T_INT",
"type_specifier : T_LONG",
"type_specifier : T_SHORT",
"type_specifier : T_SIGNED",
"type_specifier : T_UNSIGNED",
"type_specifier : T_VOID",
"type_specifier : T_Bool",
"type_specifier : T_Complex",
"type_specifier : T_Imaginary",
"type_specifier : T_TYPEDEF_NAME",
"type_specifier : struct_or_union_specifier",
"type_specifier : enum_specifier",
"type_qualifier : T_TYPE_QUALIFIER",
"type_qualifier : T_DEFINE_NAME",
"struct_or_union_specifier : struct_or_union any_id braces",
"struct_or_union_specifier : struct_or_union braces",
"struct_or_union_specifier : struct_or_union any_id",
"struct_or_union : T_STRUCT",
"struct_or_union : T_UNION",
"init_declarator_list : init_declarator",
"init_declarator_list : init_declarator_list ',' init_declarator",
"init_declarator : declarator",
"$$5 :",
"init_declarator : declarator '=' $$5 T_INITIALIZER",
"enum_specifier : enumeration any_id braces",
"enum_specifier : enumeration braces",
"enum_specifier : enumeration any_id",
"enumeration : T_ENUM",
"any_id : T_IDENTIFIER",
"any_id : T_TYPEDEF_NAME",
"declarator : pointer direct_declarator",
"declarator : direct_declarator",
"direct_declarator : identifier_or_ref",
"direct_declarator : '(' declarator ')'",
"direct_declarator : direct_declarator T_BRACKETS",
"direct_declarator : direct_declarator '(' parameter_type_list ')'",
"direct_declarator : direct_declarator '(' opt_identifier_list ')'",
"pointer : '*' opt_type_qualifiers",
"pointer : '*' opt_type_qualifiers pointer",
"opt_type_qualifiers :",
"opt_type_qualifiers : type_qualifier_list",
"type_qualifier_list : type_qualifier",
"type_qualifier_list : type_qualifier_list type_qualifier",
"parameter_type_list : parameter_list",
"parameter_type_list : parameter_list ',' T_ELLIPSIS",
"parameter_list : parameter_declaration",
"parameter_list : parameter_list ',' parameter_declaration",
"parameter_declaration : decl_specifiers declarator",
"parameter_declaration : decl_specifiers abs_declarator",
"parameter_declaration : decl_specifiers",
"opt_identifier_list :",
"opt_identifier_list : identifier_list",
"identifier_list : any_id",
"identifier_list : identifier_list ',' any_id",
"identifier_or_ref : any_id",
"identifier_or_ref : '&' any_id",
"abs_declarator : pointer",
"abs_declarator : pointer direct_abs_declarator",
"abs_declarator : direct_abs_declarator",
"direct_abs_declarator : '(' abs_declarator ')'",
"direct_abs_declarator : direct_abs_declarator T_BRACKETS",
"direct_abs_declarator : T_BRACKETS",
"direct_abs_declarator : direct_abs_declarator '(' parameter_type_list ')'",
"direct_abs_declarator : direct_abs_declarator '(' ')'",
"direct_abs_declarator : '(' parameter_type_list ')'",
"direct_abs_declarator : '(' ')'",

};
#endif

int      yydebug;
int      yynerrs;

int      yyerrflag;
int      yychar;
YYSTYPE  yyval;
YYSTYPE  yylval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
YYLTYPE  yyloc; /* position returned by actions */
YYLTYPE  yylloc; /* position from the lexer */
#endif

#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
#ifndef YYLLOC_DEFAULT
#define YYLLOC_DEFAULT(loc, rhs, n) \
do \
{ \
    if (n == 0) \
    { \
        (loc).first_line   = ((rhs)[-1]).last_line; \
        (loc).first_column = ((rhs)[-1]).last_column; \
        (loc).last_line    = ((rhs)[-1]).last_line; \
        (loc).last_column  = ((rhs)[-1]).last_column; \
    } \
    else \
    { \
        (loc).first_line   = ((rhs)[ 0 ]).first_line; \
        (loc).first_column = ((rhs)[ 0 ]).first_column; \
        (loc).last_line    = ((rhs)[n-1]).last_line; \
        (loc).last_column  = ((rhs)[n-1]).last_column; \
    } \
} while (0)
#endif /* YYLLOC_DEFAULT */
#endif /* defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED) */
#if YYBTYACC

#ifndef YYLVQUEUEGROWTH
#define YYLVQUEUEGROWTH 32
#endif
#endif /* YYBTYACC */

/* define the initial stack-sizes */
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH  YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 10000
#define YYMAXDEPTH  10000
#endif
#endif

#ifndef YYINITSTACKSIZE
#define YYINITSTACKSIZE 200
#endif

typedef struct {
    unsigned stacksize;
    YYINT    *s_base;
    YYINT    *s_mark;
    YYINT    *s_last;
    YYSTYPE  *l_base;
    YYSTYPE  *l_mark;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    YYLTYPE  *p_base;
    YYLTYPE  *p_mark;
#endif
} YYSTACKDATA;
#if YYBTYACC

struct YYParseState_s
{
    struct YYParseState_s *save;    /* Previously saved parser state */
    YYSTACKDATA            yystack; /* saved parser stack */
    int                    state;   /* saved parser state */
    int                    errflag; /* saved error recovery status */
    int                    lexeme;  /* saved index of the conflict lexeme in the lexical queue */
    YYINT                  ctry;    /* saved index in yyctable[] for this conflict */
};
typedef struct YYParseState_s YYParseState;
#endif /* YYBTYACC */
/* variables for the parser stack */
static YYSTACKDATA yystack;
#if YYBTYACC

/* Current parser state */
static YYParseState *yyps = 0;

/* yypath != NULL: do the full parse, starting at *yypath parser state. */
static YYParseState *yypath = 0;

/* Base of the lexical value queue */
static YYSTYPE *yylvals = 0;

/* Current position at lexical value queue */
static YYSTYPE *yylvp = 0;

/* End position of lexical value queue */
static YYSTYPE *yylve = 0;

/* The last allocated position at the lexical value queue */
static YYSTYPE *yylvlim = 0;

#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
/* Base of the lexical position queue */
static YYLTYPE *yylpsns = 0;

/* Current position at lexical position queue */
static YYLTYPE *yylpp = 0;

/* End position of lexical position queue */
static YYLTYPE *yylpe = 0;

/* The last allocated position at the lexical position queue */
static YYLTYPE *yylplim = 0;
#endif

/* Current position at lexical token queue */
static YYINT  *yylexp = 0;

static YYINT  *yylexemes = 0;
#endif /* YYBTYACC */
#line 1014 "grammar.y"

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
#line 1347 "grammar.tab.c"

/* For use in generated program */
#define yydepth (int)(yystack.s_mark - yystack.s_base)
#if YYBTYACC
#define yytrial (yyps->save)
#endif /* YYBTYACC */

#if YYDEBUG
#include <stdio.h>	/* needed for printf */
#endif

#include <stdlib.h>	/* needed for malloc, etc */
#include <string.h>	/* needed for memset */

/* allocate initial stack or double stack size, up to YYMAXDEPTH */
static int yygrowstack(YYSTACKDATA *data)
{
    int i;
    unsigned newsize;
    YYINT *newss;
    YYSTYPE *newvs;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    YYLTYPE *newps;
#endif

    if ((newsize = data->stacksize) == 0)
        newsize = YYINITSTACKSIZE;
    else if (newsize >= YYMAXDEPTH)
        return YYENOMEM;
    else if ((newsize *= 2) > YYMAXDEPTH)
        newsize = YYMAXDEPTH;

    i = (int) (data->s_mark - data->s_base);
    newss = (YYINT *)realloc(data->s_base, newsize * sizeof(*newss));
    if (newss == 0)
        return YYENOMEM;

    data->s_base = newss;
    data->s_mark = newss + i;

    newvs = (YYSTYPE *)realloc(data->l_base, newsize * sizeof(*newvs));
    if (newvs == 0)
        return YYENOMEM;

    data->l_base = newvs;
    data->l_mark = newvs + i;

#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    newps = (YYLTYPE *)realloc(data->p_base, newsize * sizeof(*newps));
    if (newps == 0)
        return YYENOMEM;

    data->p_base = newps;
    data->p_mark = newps + i;
#endif

    data->stacksize = newsize;
    data->s_last = data->s_base + newsize - 1;

#if YYDEBUG
    if (yydebug)
        fprintf(stderr, "%sdebug: stack size increased to %d\n", YYPREFIX, newsize);
#endif
    return 0;
}

#if YYPURE || defined(YY_NO_LEAKS)
static void yyfreestack(YYSTACKDATA *data)
{
    free(data->s_base);
    free(data->l_base);
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    free(data->p_base);
#endif
    memset(data, 0, sizeof(*data));
}
#else
#define yyfreestack(data) /* nothing */
#endif /* YYPURE || defined(YY_NO_LEAKS) */
#if YYBTYACC

static YYParseState *
yyNewState(unsigned size)
{
    YYParseState *p = (YYParseState *) malloc(sizeof(YYParseState));
    if (p == NULL) return NULL;

    p->yystack.stacksize = size;
    if (size == 0)
    {
        p->yystack.s_base = NULL;
        p->yystack.l_base = NULL;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        p->yystack.p_base = NULL;
#endif
        return p;
    }
    p->yystack.s_base    = (YYINT *) malloc(size * sizeof(YYINT));
    if (p->yystack.s_base == NULL) return NULL;
    p->yystack.l_base    = (YYSTYPE *) malloc(size * sizeof(YYSTYPE));
    if (p->yystack.l_base == NULL) return NULL;
    memset(p->yystack.l_base, 0, size * sizeof(YYSTYPE));
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    p->yystack.p_base    = (YYLTYPE *) malloc(size * sizeof(YYLTYPE));
    if (p->yystack.p_base == NULL) return NULL;
    memset(p->yystack.p_base, 0, size * sizeof(YYLTYPE));
#endif

    return p;
}

static void
yyFreeState(YYParseState *p)
{
    yyfreestack(&p->yystack);
    free(p);
}
#endif /* YYBTYACC */

#define YYABORT  goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR  goto yyerrlab
#if YYBTYACC
#define YYVALID        do { if (yyps->save)            goto yyvalid; } while(0)
#define YYVALID_NESTED do { if (yyps->save && \
                                yyps->save->save == 0) goto yyvalid; } while(0)
#endif /* YYBTYACC */

int
YYPARSE_DECL()
{
    int yym, yyn, yystate, yyresult;
#if YYBTYACC
    int yynewerrflag;
    YYParseState *yyerrctx = NULL;
#endif /* YYBTYACC */
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    YYLTYPE  yyerror_loc_range[2]; /* position of error start & end */
#endif
#if YYDEBUG
    const char *yys;

    if ((yys = getenv("YYDEBUG")) != 0)
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
    if (yydebug)
        fprintf(stderr, "%sdebug[<# of symbols on state stack>]\n", YYPREFIX);
#endif
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    memset(yyerror_loc_range, 0, sizeof(yyerror_loc_range));
#endif

#if YYBTYACC
    yyps = yyNewState(0); if (yyps == 0) goto yyenomem;
    yyps->save = 0;
#endif /* YYBTYACC */
    yym = 0;
    yyn = 0;
    yynerrs = 0;
    yyerrflag = 0;
    yychar = YYEMPTY;
    yystate = 0;

#if YYPURE
    memset(&yystack, 0, sizeof(yystack));
#endif

    if (yystack.s_base == NULL && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
    yystack.s_mark = yystack.s_base;
    yystack.l_mark = yystack.l_base;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    yystack.p_mark = yystack.p_base;
#endif
    yystate = 0;
    *yystack.s_mark = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
#if YYBTYACC
        do {
        if (yylvp < yylve)
        {
            /* we're currently re-reading tokens */
            yylval = *yylvp++;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            yylloc = *yylpp++;
#endif
            yychar = *yylexp++;
            break;
        }
        if (yyps->save)
        {
            /* in trial mode; save scanner results for future parse attempts */
            if (yylvp == yylvlim)
            {   /* Enlarge lexical value queue */
                size_t p = (size_t) (yylvp - yylvals);
                size_t s = (size_t) (yylvlim - yylvals);

                s += YYLVQUEUEGROWTH;
                if ((yylexemes = realloc(yylexemes, s * sizeof(YYINT))) == NULL) goto yyenomem;
                if ((yylvals   = realloc(yylvals, s * sizeof(YYSTYPE))) == NULL) goto yyenomem;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                if ((yylpsns   = realloc(yylpsns, s * sizeof(YYLTYPE))) == NULL) goto yyenomem;
#endif
                yylvp   = yylve = yylvals + p;
                yylvlim = yylvals + s;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                yylpp   = yylpe = yylpsns + p;
                yylplim = yylpsns + s;
#endif
                yylexp  = yylexemes + p;
            }
            *yylexp = (YYINT) YYLEX;
            *yylvp++ = yylval;
            yylve++;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            *yylpp++ = yylloc;
            yylpe++;
#endif
            yychar = *yylexp++;
            break;
        }
        /* normal operation, no conflict encountered */
#endif /* YYBTYACC */
        yychar = YYLEX;
#if YYBTYACC
        } while (0);
#endif /* YYBTYACC */
        if (yychar < 0) yychar = YYEOF;
#if YYDEBUG
        if (yydebug)
        {
            if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];
            fprintf(stderr, "%s[%d]: state %d, reading token %d (%s)",
                            YYDEBUGSTR, yydepth, yystate, yychar, yys);
#ifdef YYSTYPE_TOSTRING
#if YYBTYACC
            if (!yytrial)
#endif /* YYBTYACC */
                fprintf(stderr, " <%s>", YYSTYPE_TOSTRING(yychar, yylval));
#endif
            fputc('\n', stderr);
        }
#endif
    }
#if YYBTYACC

    /* Do we have a conflict? */
    if (((yyn = yycindex[yystate]) != 0) && (yyn += yychar) >= 0 &&
        yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yychar)
    {
        YYINT ctry;

        if (yypath)
        {
            YYParseState *save;
#if YYDEBUG
            if (yydebug)
                fprintf(stderr, "%s[%d]: CONFLICT in state %d: following successful trial parse\n",
                                YYDEBUGSTR, yydepth, yystate);
#endif
            /* Switch to the next conflict context */
            save = yypath;
            yypath = save->save;
            save->save = NULL;
            ctry = save->ctry;
            if (save->state != yystate) YYABORT;
            yyFreeState(save);

        }
        else
        {

            /* Unresolved conflict - start/continue trial parse */
            YYParseState *save;
#if YYDEBUG
            if (yydebug)
            {
                fprintf(stderr, "%s[%d]: CONFLICT in state %d. ", YYDEBUGSTR, yydepth, yystate);
                if (yyps->save)
                    fputs("ALREADY in conflict, continuing trial parse.\n", stderr);
                else
                    fputs("Starting trial parse.\n", stderr);
            }
#endif
            save                  = yyNewState((unsigned)(yystack.s_mark - yystack.s_base + 1));
            if (save == NULL) goto yyenomem;
            save->save            = yyps->save;
            save->state           = yystate;
            save->errflag         = yyerrflag;
            save->yystack.s_mark  = save->yystack.s_base + (yystack.s_mark - yystack.s_base);
            memcpy (save->yystack.s_base, yystack.s_base, (size_t) (yystack.s_mark - yystack.s_base + 1) * sizeof(YYINT));
            save->yystack.l_mark  = save->yystack.l_base + (yystack.l_mark - yystack.l_base);
            memcpy (save->yystack.l_base, yystack.l_base, (size_t) (yystack.l_mark - yystack.l_base + 1) * sizeof(YYSTYPE));
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            save->yystack.p_mark  = save->yystack.p_base + (yystack.p_mark - yystack.p_base);
            memcpy (save->yystack.p_base, yystack.p_base, (size_t) (yystack.p_mark - yystack.p_base + 1) * sizeof(YYLTYPE));
#endif
            ctry                  = yytable[yyn];
            if (yyctable[ctry] == -1)
            {
#if YYDEBUG
                if (yydebug && yychar >= YYEOF)
                    fprintf(stderr, "%s[%d]: backtracking 1 token\n", YYDEBUGSTR, yydepth);
#endif
                ctry++;
            }
            save->ctry = ctry;
            if (yyps->save == NULL)
            {
                /* If this is a first conflict in the stack, start saving lexemes */
                if (!yylexemes)
                {
                    yylexemes = malloc((YYLVQUEUEGROWTH) * sizeof(YYINT));
                    if (yylexemes == NULL) goto yyenomem;
                    yylvals   = (YYSTYPE *) malloc((YYLVQUEUEGROWTH) * sizeof(YYSTYPE));
                    if (yylvals == NULL) goto yyenomem;
                    yylvlim   = yylvals + YYLVQUEUEGROWTH;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                    yylpsns   = (YYLTYPE *) malloc((YYLVQUEUEGROWTH) * sizeof(YYLTYPE));
                    if (yylpsns == NULL) goto yyenomem;
                    yylplim   = yylpsns + YYLVQUEUEGROWTH;
#endif
                }
                if (yylvp == yylve)
                {
                    yylvp  = yylve = yylvals;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                    yylpp  = yylpe = yylpsns;
#endif
                    yylexp = yylexemes;
                    if (yychar >= YYEOF)
                    {
                        *yylve++ = yylval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                        *yylpe++ = yylloc;
#endif
                        *yylexp  = (YYINT) yychar;
                        yychar   = YYEMPTY;
                    }
                }
            }
            if (yychar >= YYEOF)
            {
                yylvp--;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                yylpp--;
#endif
                yylexp--;
                yychar = YYEMPTY;
            }
            save->lexeme = (int) (yylvp - yylvals);
            yyps->save   = save;
        }
        if (yytable[yyn] == ctry)
        {
#if YYDEBUG
            if (yydebug)
                fprintf(stderr, "%s[%d]: state %d, shifting to state %d\n",
                                YYDEBUGSTR, yydepth, yystate, yyctable[ctry]);
#endif
            if (yychar < 0)
            {
                yylvp++;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                yylpp++;
#endif
                yylexp++;
            }
            if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM)
                goto yyoverflow;
            yystate = yyctable[ctry];
            *++yystack.s_mark = (YYINT) yystate;
            *++yystack.l_mark = yylval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            *++yystack.p_mark = yylloc;
#endif
            yychar  = YYEMPTY;
            if (yyerrflag > 0) --yyerrflag;
            goto yyloop;
        }
        else
        {
            yyn = yyctable[ctry];
            goto yyreduce;
        }
    } /* End of code dealing with conflicts */
#endif /* YYBTYACC */
    if (((yyn = yysindex[yystate]) != 0) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yychar)
    {
#if YYDEBUG
        if (yydebug)
            fprintf(stderr, "%s[%d]: state %d, shifting to state %d\n",
                            YYDEBUGSTR, yydepth, yystate, yytable[yyn]);
#endif
        if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
        yystate = yytable[yyn];
        *++yystack.s_mark = yytable[yyn];
        *++yystack.l_mark = yylval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        *++yystack.p_mark = yylloc;
#endif
        yychar = YYEMPTY;
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if (((yyn = yyrindex[yystate]) != 0) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag != 0) goto yyinrecovery;
#if YYBTYACC

    yynewerrflag = 1;
    goto yyerrhandler;
    goto yyerrlab; /* redundant goto avoids 'unused label' warning */

yyerrlab:
    /* explicit YYERROR from an action -- pop the rhs of the rule reduced
     * before looking for error recovery */
    yystack.s_mark -= yym;
    yystate = *yystack.s_mark;
    yystack.l_mark -= yym;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    yystack.p_mark -= yym;
#endif

    yynewerrflag = 0;
yyerrhandler:
    while (yyps->save)
    {
        int ctry;
        YYParseState *save = yyps->save;
#if YYDEBUG
        if (yydebug)
            fprintf(stderr, "%s[%d]: ERROR in state %d, CONFLICT BACKTRACKING to state %d, %d tokens\n",
                            YYDEBUGSTR, yydepth, yystate, yyps->save->state,
                    (int)(yylvp - yylvals - yyps->save->lexeme));
#endif
        /* Memorize most forward-looking error state in case it's really an error. */
        if (yyerrctx == NULL || yyerrctx->lexeme < yylvp - yylvals)
        {
            /* Free old saved error context state */
            if (yyerrctx) yyFreeState(yyerrctx);
            /* Create and fill out new saved error context state */
            yyerrctx                 = yyNewState((unsigned)(yystack.s_mark - yystack.s_base + 1));
            if (yyerrctx == NULL) goto yyenomem;
            yyerrctx->save           = yyps->save;
            yyerrctx->state          = yystate;
            yyerrctx->errflag        = yyerrflag;
            yyerrctx->yystack.s_mark = yyerrctx->yystack.s_base + (yystack.s_mark - yystack.s_base);
            memcpy (yyerrctx->yystack.s_base, yystack.s_base, (size_t) (yystack.s_mark - yystack.s_base + 1) * sizeof(YYINT));
            yyerrctx->yystack.l_mark = yyerrctx->yystack.l_base + (yystack.l_mark - yystack.l_base);
            memcpy (yyerrctx->yystack.l_base, yystack.l_base, (size_t) (yystack.l_mark - yystack.l_base + 1) * sizeof(YYSTYPE));
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            yyerrctx->yystack.p_mark = yyerrctx->yystack.p_base + (yystack.p_mark - yystack.p_base);
            memcpy (yyerrctx->yystack.p_base, yystack.p_base, (size_t) (yystack.p_mark - yystack.p_base + 1) * sizeof(YYLTYPE));
#endif
            yyerrctx->lexeme         = (int) (yylvp - yylvals);
        }
        yylvp          = yylvals   + save->lexeme;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        yylpp          = yylpsns   + save->lexeme;
#endif
        yylexp         = yylexemes + save->lexeme;
        yychar         = YYEMPTY;
        yystack.s_mark = yystack.s_base + (save->yystack.s_mark - save->yystack.s_base);
        memcpy (yystack.s_base, save->yystack.s_base, (size_t) (yystack.s_mark - yystack.s_base + 1) * sizeof(YYINT));
        yystack.l_mark = yystack.l_base + (save->yystack.l_mark - save->yystack.l_base);
        memcpy (yystack.l_base, save->yystack.l_base, (size_t) (yystack.l_mark - yystack.l_base + 1) * sizeof(YYSTYPE));
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        yystack.p_mark = yystack.p_base + (save->yystack.p_mark - save->yystack.p_base);
        memcpy (yystack.p_base, save->yystack.p_base, (size_t) (yystack.p_mark - yystack.p_base + 1) * sizeof(YYLTYPE));
#endif
        ctry           = ++save->ctry;
        yystate        = save->state;
        /* We tried shift, try reduce now */
        if ((yyn = yyctable[ctry]) >= 0) goto yyreduce;
        yyps->save     = save->save;
        save->save     = NULL;
        yyFreeState(save);

        /* Nothing left on the stack -- error */
        if (!yyps->save)
        {
#if YYDEBUG
            if (yydebug)
                fprintf(stderr, "%sdebug[%d,trial]: trial parse FAILED, entering ERROR mode\n",
                                YYPREFIX, yydepth);
#endif
            /* Restore state as it was in the most forward-advanced error */
            yylvp          = yylvals   + yyerrctx->lexeme;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            yylpp          = yylpsns   + yyerrctx->lexeme;
#endif
            yylexp         = yylexemes + yyerrctx->lexeme;
            yychar         = yylexp[-1];
            yylval         = yylvp[-1];
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            yylloc         = yylpp[-1];
#endif
            yystack.s_mark = yystack.s_base + (yyerrctx->yystack.s_mark - yyerrctx->yystack.s_base);
            memcpy (yystack.s_base, yyerrctx->yystack.s_base, (size_t) (yystack.s_mark - yystack.s_base + 1) * sizeof(YYINT));
            yystack.l_mark = yystack.l_base + (yyerrctx->yystack.l_mark - yyerrctx->yystack.l_base);
            memcpy (yystack.l_base, yyerrctx->yystack.l_base, (size_t) (yystack.l_mark - yystack.l_base + 1) * sizeof(YYSTYPE));
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            yystack.p_mark = yystack.p_base + (yyerrctx->yystack.p_mark - yyerrctx->yystack.p_base);
            memcpy (yystack.p_base, yyerrctx->yystack.p_base, (size_t) (yystack.p_mark - yystack.p_base + 1) * sizeof(YYLTYPE));
#endif
            yystate        = yyerrctx->state;
            yyFreeState(yyerrctx);
            yyerrctx       = NULL;
        }
        yynewerrflag = 1;
    }
    if (yynewerrflag == 0) goto yyinrecovery;
#endif /* YYBTYACC */

    YYERROR_CALL("syntax error");
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    yyerror_loc_range[0] = yylloc; /* lookahead position is error start position */
#endif

#if !YYBTYACC
    goto yyerrlab; /* redundant goto avoids 'unused label' warning */
yyerrlab:
#endif
    ++yynerrs;

yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if (((yyn = yysindex[*yystack.s_mark]) != 0) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    fprintf(stderr, "%s[%d]: state %d, error recovery shifting to state %d\n",
                                    YYDEBUGSTR, yydepth, *yystack.s_mark, yytable[yyn]);
#endif
                if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
                yystate = yytable[yyn];
                *++yystack.s_mark = yytable[yyn];
                *++yystack.l_mark = yylval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                /* lookahead position is error end position */
                yyerror_loc_range[1] = yylloc;
                YYLLOC_DEFAULT(yyloc, yyerror_loc_range, 2); /* position of error span */
                *++yystack.p_mark = yyloc;
#endif
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    fprintf(stderr, "%s[%d]: error recovery discarding state %d\n",
                                    YYDEBUGSTR, yydepth, *yystack.s_mark);
#endif
                if (yystack.s_mark <= yystack.s_base) goto yyabort;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                /* the current TOS position is the error start position */
                yyerror_loc_range[0] = *yystack.p_mark;
#endif
#if defined(YYDESTRUCT_CALL)
#if YYBTYACC
                if (!yytrial)
#endif /* YYBTYACC */
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                    YYDESTRUCT_CALL("error: discarding state",
                                    yystos[*yystack.s_mark], yystack.l_mark, yystack.p_mark);
#else
                    YYDESTRUCT_CALL("error: discarding state",
                                    yystos[*yystack.s_mark], yystack.l_mark);
#endif /* defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED) */
#endif /* defined(YYDESTRUCT_CALL) */
                --yystack.s_mark;
                --yystack.l_mark;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                --yystack.p_mark;
#endif
            }
        }
    }
    else
    {
        if (yychar == YYEOF) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];
            fprintf(stderr, "%s[%d]: state %d, error recovery discarding token %d (%s)\n",
                            YYDEBUGSTR, yydepth, yystate, yychar, yys);
        }
#endif
#if defined(YYDESTRUCT_CALL)
#if YYBTYACC
        if (!yytrial)
#endif /* YYBTYACC */
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
            YYDESTRUCT_CALL("error: discarding token", yychar, &yylval, &yylloc);
#else
            YYDESTRUCT_CALL("error: discarding token", yychar, &yylval);
#endif /* defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED) */
#endif /* defined(YYDESTRUCT_CALL) */
        yychar = YYEMPTY;
        goto yyloop;
    }

yyreduce:
    yym = yylen[yyn];
#if YYDEBUG
    if (yydebug)
    {
        fprintf(stderr, "%s[%d]: state %d, reducing by rule %d (%s)",
                        YYDEBUGSTR, yydepth, yystate, yyn, yyrule[yyn]);
#ifdef YYSTYPE_TOSTRING
#if YYBTYACC
        if (!yytrial)
#endif /* YYBTYACC */
            if (yym > 0)
            {
                int i;
                fputc('<', stderr);
                for (i = yym; i > 0; i--)
                {
                    if (i != yym) fputs(", ", stderr);
                    fputs(YYSTYPE_TOSTRING(yystos[yystack.s_mark[1-i]],
                                           yystack.l_mark[1-i]), stderr);
                }
                fputc('>', stderr);
            }
#endif
        fputc('\n', stderr);
    }
#endif
    if (yym > 0)
        yyval = yystack.l_mark[1-yym];
    else
        memset(&yyval, 0, sizeof yyval);
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)

    /* Perform position reduction */
    memset(&yyloc, 0, sizeof(yyloc));
#if YYBTYACC
    if (!yytrial)
#endif /* YYBTYACC */
    {
        YYLLOC_DEFAULT(yyloc, &yystack.p_mark[1-yym], yym);
        /* just in case YYERROR is invoked within the action, save
           the start of the rhs as the error start position */
        yyerror_loc_range[0] = yystack.p_mark[1-yym];
    }
#endif

    switch (yyn)
    {
case 10:
#line 377 "grammar.y"
	{
	    yyerrok;
	}
break;
case 11:
#line 381 "grammar.y"
	{
	    yyerrok;
	}
break;
case 13:
#line 392 "grammar.y"
	{
	    /* Provide an empty action here so bison will not complain about
	     * incompatible types in the default action it normally would
	     * have generated.
	     */
	}
break;
case 14:
#line 399 "grammar.y"
	{
	    /* empty */
	}
break;
case 15:
#line 406 "grammar.y"
	{
#if OPT_LINTLIBRARY
	    if (types_out && want_typedef()) {
		gen_declarations(&yystack.l_mark[-1].decl_spec, (DeclaratorList *)0);
		flush_varargs();
	    }
#endif
	    free_decl_spec(&yystack.l_mark[-1].decl_spec);
	    end_typedef();
	}
break;
case 16:
#line 417 "grammar.y"
	{
	    if (func_params != NULL) {
		set_param_types(func_params, &yystack.l_mark[-2].decl_spec, &yystack.l_mark[-1].decl_list);
	    } else {
		gen_declarations(&yystack.l_mark[-2].decl_spec, &yystack.l_mark[-1].decl_list);
#if OPT_LINTLIBRARY
		flush_varargs();
#endif
		free_decl_list(&yystack.l_mark[-1].decl_list);
	    }
	    free_decl_spec(&yystack.l_mark[-2].decl_spec);
	    end_typedef();
	}
break;
case 17:
#line 431 "grammar.y"
	{
	    cur_decl_spec_flags = yystack.l_mark[0].decl_spec.flags;
	    free_decl_spec(&yystack.l_mark[0].decl_spec);
	}
break;
case 18:
#line 436 "grammar.y"
	{
	    end_typedef();
	}
break;
case 19:
#line 443 "grammar.y"
	{
	    begin_typedef();
	}
break;
case 20:
#line 447 "grammar.y"
	{
	    begin_typedef();
	}
break;
case 23:
#line 459 "grammar.y"
	{
	    int flags = cur_decl_spec_flags;

	    /* If the typedef is a pointer type, then reset the short type
	     * flags so it does not get promoted.
	     */
	    if (strcmp(yystack.l_mark[0].declarator->text, yystack.l_mark[0].declarator->name) != 0)
		flags &= ~(DS_CHAR | DS_SHORT | DS_FLOAT);
	    new_symbol(typedef_names, yystack.l_mark[0].declarator->name, NULL, flags);
	    free_declarator(yystack.l_mark[0].declarator);
	}
break;
case 24:
#line 471 "grammar.y"
	{
	    int flags = cur_decl_spec_flags;

	    if (strcmp(yystack.l_mark[0].declarator->text, yystack.l_mark[0].declarator->name) != 0)
		flags &= ~(DS_CHAR | DS_SHORT | DS_FLOAT);
	    new_symbol(typedef_names, yystack.l_mark[0].declarator->name, NULL, flags);
	    free_declarator(yystack.l_mark[0].declarator);
	}
break;
case 25:
#line 483 "grammar.y"
	{
	    check_untagged(&yystack.l_mark[-1].decl_spec);
	    if (yystack.l_mark[0].declarator->func_def == FUNC_NONE) {
		yyerror("syntax error");
		YYERROR;
	    }
	    func_params = &(yystack.l_mark[0].declarator->head->params);
	    func_params->begin_comment = cur_file->begin_comment;
	    func_params->end_comment = cur_file->end_comment;
	}
break;
case 26:
#line 494 "grammar.y"
	{
	    /* If we're converting to K&R and we've got a nominally K&R
	     * function which has a parameter which is ANSI (i.e., a prototyped
	     * function pointer), then we must override the deciphered value of
	     * 'func_def' so that the parameter will be converted.
	     */
	    if (func_style == FUNC_TRADITIONAL
	     && haveAnsiParam()
	     && yystack.l_mark[-3].declarator->head->func_def == func_style) {
		yystack.l_mark[-3].declarator->head->func_def = FUNC_BOTH;
	    }

	    func_params = NULL;

	    if (cur_file->convert)
		gen_func_definition(&yystack.l_mark[-4].decl_spec, yystack.l_mark[-3].declarator);
	    gen_prototype(&yystack.l_mark[-4].decl_spec, yystack.l_mark[-3].declarator);
#if OPT_LINTLIBRARY
	    flush_varargs();
#endif
	    free_decl_spec(&yystack.l_mark[-4].decl_spec);
	    free_declarator(yystack.l_mark[-3].declarator);
	}
break;
case 28:
#line 519 "grammar.y"
	{
	    if (yystack.l_mark[0].declarator->func_def == FUNC_NONE) {
		yyerror("syntax error");
		YYERROR;
	    }
	    func_params = &(yystack.l_mark[0].declarator->head->params);
	    func_params->begin_comment = cur_file->begin_comment;
	    func_params->end_comment = cur_file->end_comment;
	}
break;
case 29:
#line 529 "grammar.y"
	{
	    DeclSpec decl_spec;

	    func_params = NULL;

	    new_decl_spec(&decl_spec, dft_decl_spec(), yystack.l_mark[-4].declarator->begin, DS_NONE);
	    if (cur_file->convert)
		gen_func_definition(&decl_spec, yystack.l_mark[-4].declarator);
	    gen_prototype(&decl_spec, yystack.l_mark[-4].declarator);
#if OPT_LINTLIBRARY
	    flush_varargs();
#endif
	    free_decl_spec(&decl_spec);
	    free_declarator(yystack.l_mark[-4].declarator);
	}
break;
case 36:
#line 560 "grammar.y"
	{
	    join_decl_specs(&yyval.decl_spec, &yystack.l_mark[-1].decl_spec, &yystack.l_mark[0].decl_spec);
	    free(yystack.l_mark[-1].decl_spec.text);
	    free(yystack.l_mark[0].decl_spec.text);
	}
break;
case 40:
#line 575 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 41:
#line 579 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_EXTERN);
	}
break;
case 42:
#line 583 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 43:
#line 587 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_STATIC);
	}
break;
case 44:
#line 591 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_INLINE);
	}
break;
case 45:
#line 595 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_JUNK);
	}
break;
case 46:
#line 602 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_CHAR);
	}
break;
case 47:
#line 606 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 48:
#line 610 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_FLOAT);
	}
break;
case 49:
#line 614 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 50:
#line 618 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 51:
#line 622 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_SHORT);
	}
break;
case 52:
#line 626 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 53:
#line 630 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 54:
#line 634 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 55:
#line 638 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_CHAR);
	}
break;
case 56:
#line 642 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 57:
#line 646 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 58:
#line 650 "grammar.y"
	{
	    Symbol *s;
	    s = find_symbol(typedef_names, yystack.l_mark[0].text.text);
	    if (s != NULL)
		new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, s->flags);
	}
break;
case 61:
#line 662 "grammar.y"
	{
	    new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, DS_NONE);
	}
break;
case 62:
#line 666 "grammar.y"
	{
	    /* This rule allows the <pointer> nonterminal to scan #define
	     * names as if they were type modifiers.
	     */
	    Symbol *s;
	    s = find_symbol(define_names, yystack.l_mark[0].text.text);
	    if (s != NULL)
		new_decl_spec(&yyval.decl_spec, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin, s->flags);
	}
break;
case 63:
#line 679 "grammar.y"
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
	        (void)sprintf(s = buf, "%s %s", yystack.l_mark[-2].text.text, yystack.l_mark[-1].text.text);
	    new_decl_spec(&yyval.decl_spec, s, yystack.l_mark[-2].text.begin, DS_NONE);
	}
break;
case 64:
#line 686 "grammar.y"
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
		(void)sprintf(s = buf, "%s {}", yystack.l_mark[-1].text.text);
	    new_decl_spec(&yyval.decl_spec, s, yystack.l_mark[-1].text.begin, DS_NONE);
	}
break;
case 65:
#line 693 "grammar.y"
	{
	    (void)sprintf(buf, "%s %s", yystack.l_mark[-1].text.text, yystack.l_mark[0].text.text);
	    new_decl_spec(&yyval.decl_spec, buf, yystack.l_mark[-1].text.begin, DS_NONE);
	}
break;
case 66:
#line 701 "grammar.y"
	{
	    imply_typedef(yyval.text.text);
	}
break;
case 67:
#line 705 "grammar.y"
	{
	    imply_typedef(yyval.text.text);
	}
break;
case 68:
#line 712 "grammar.y"
	{
	    new_decl_list(&yyval.decl_list, yystack.l_mark[0].declarator);
	}
break;
case 69:
#line 716 "grammar.y"
	{
	    add_decl_list(&yyval.decl_list, &yystack.l_mark[-2].decl_list, yystack.l_mark[0].declarator);
	}
break;
case 70:
#line 723 "grammar.y"
	{
	    if (yystack.l_mark[0].declarator->func_def != FUNC_NONE && func_params == NULL &&
		func_style == FUNC_TRADITIONAL && cur_file->convert) {
		gen_func_declarator(yystack.l_mark[0].declarator);
		fputs(cur_text(), cur_file->tmp_file);
	    }
	    cur_declarator = yyval.declarator;
	}
break;
case 71:
#line 732 "grammar.y"
	{
	    if (yystack.l_mark[-1].declarator->func_def != FUNC_NONE && func_params == NULL &&
		func_style == FUNC_TRADITIONAL && cur_file->convert) {
		gen_func_declarator(yystack.l_mark[-1].declarator);
		fputs(" =", cur_file->tmp_file);
	    }
	}
break;
case 73:
#line 744 "grammar.y"
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
		(void)sprintf(s = buf, "enum %s", yystack.l_mark[-1].text.text);
	    new_decl_spec(&yyval.decl_spec, s, yystack.l_mark[-2].text.begin, DS_NONE);
	}
break;
case 74:
#line 751 "grammar.y"
	{
	    char *s;
	    if ((s = implied_typedef()) == 0)
		(void)sprintf(s = buf, "%s {}", yystack.l_mark[-1].text.text);
	    new_decl_spec(&yyval.decl_spec, s, yystack.l_mark[-1].text.begin, DS_NONE);
	}
break;
case 75:
#line 758 "grammar.y"
	{
	    (void)sprintf(buf, "enum %s", yystack.l_mark[0].text.text);
	    new_decl_spec(&yyval.decl_spec, buf, yystack.l_mark[-1].text.begin, DS_NONE);
	}
break;
case 76:
#line 766 "grammar.y"
	{
	    imply_typedef("enum");
	    yyval.text = yystack.l_mark[0].text;
	}
break;
case 79:
#line 779 "grammar.y"
	{
	    yyval.declarator = yystack.l_mark[0].declarator;
	    (void)sprintf(buf, "%s%s", yystack.l_mark[-1].text.text, yyval.declarator->text);
	    free(yyval.declarator->text);
	    yyval.declarator->text = xstrdup(buf);
	    yyval.declarator->begin = yystack.l_mark[-1].text.begin;
	    yyval.declarator->pointer = TRUE;
	}
break;
case 81:
#line 792 "grammar.y"
	{
	    yyval.declarator = new_declarator(yystack.l_mark[0].text.text, yystack.l_mark[0].text.text, yystack.l_mark[0].text.begin);
	}
break;
case 82:
#line 796 "grammar.y"
	{
	    yyval.declarator = yystack.l_mark[-1].declarator;
	    (void)sprintf(buf, "(%s)", yyval.declarator->text);
	    free(yyval.declarator->text);
	    yyval.declarator->text = xstrdup(buf);
	    yyval.declarator->begin = yystack.l_mark[-2].text.begin;
	}
break;
case 83:
#line 804 "grammar.y"
	{
	    yyval.declarator = yystack.l_mark[-1].declarator;
	    (void)sprintf(buf, "%s%s", yyval.declarator->text, yystack.l_mark[0].text.text);
	    free(yyval.declarator->text);
	    yyval.declarator->text = xstrdup(buf);
	}
break;
case 84:
#line 811 "grammar.y"
	{
	    yyval.declarator = new_declarator("%s()", yystack.l_mark[-3].declarator->name, yystack.l_mark[-3].declarator->begin);
	    yyval.declarator->params = yystack.l_mark[-1].param_list;
	    yyval.declarator->func_stack = yystack.l_mark[-3].declarator;
	    yyval.declarator->head = (yystack.l_mark[-3].declarator->func_stack == NULL) ? yyval.declarator : yystack.l_mark[-3].declarator->head;
	    yyval.declarator->func_def = FUNC_ANSI;
	}
break;
case 85:
#line 819 "grammar.y"
	{
	    yyval.declarator = new_declarator("%s()", yystack.l_mark[-3].declarator->name, yystack.l_mark[-3].declarator->begin);
	    yyval.declarator->params = yystack.l_mark[-1].param_list;
	    yyval.declarator->func_stack = yystack.l_mark[-3].declarator;
	    yyval.declarator->head = (yystack.l_mark[-3].declarator->func_stack == NULL) ? yyval.declarator : yystack.l_mark[-3].declarator->head;
	    yyval.declarator->func_def = FUNC_TRADITIONAL;
	}
break;
case 86:
#line 830 "grammar.y"
	{
	    (void)sprintf(yyval.text.text, "*%s", yystack.l_mark[0].text.text);
	    yyval.text.begin = yystack.l_mark[-1].text.begin;
	}
break;
case 87:
#line 835 "grammar.y"
	{
	    (void)sprintf(yyval.text.text, "*%s%s", yystack.l_mark[-1].text.text, yystack.l_mark[0].text.text);
	    yyval.text.begin = yystack.l_mark[-2].text.begin;
	}
break;
case 88:
#line 843 "grammar.y"
	{
	    strcpy(yyval.text.text, "");
	    yyval.text.begin = 0L;
	}
break;
case 90:
#line 852 "grammar.y"
	{
	    (void)sprintf(yyval.text.text, "%s ", yystack.l_mark[0].decl_spec.text);
	    yyval.text.begin = yystack.l_mark[0].decl_spec.begin;
	    free(yystack.l_mark[0].decl_spec.text);
	}
break;
case 91:
#line 858 "grammar.y"
	{
	    (void)sprintf(yyval.text.text, "%s%s ", yystack.l_mark[-1].text.text, yystack.l_mark[0].decl_spec.text);
	    yyval.text.begin = yystack.l_mark[-1].text.begin;
	    free(yystack.l_mark[0].decl_spec.text);
	}
break;
case 93:
#line 868 "grammar.y"
	{
	    add_ident_list(&yyval.param_list, &yystack.l_mark[-2].param_list, "...");
	}
break;
case 94:
#line 875 "grammar.y"
	{
	    new_param_list(&yyval.param_list, yystack.l_mark[0].parameter);
	}
break;
case 95:
#line 879 "grammar.y"
	{
	    add_param_list(&yyval.param_list, &yystack.l_mark[-2].param_list, yystack.l_mark[0].parameter);
	}
break;
case 96:
#line 886 "grammar.y"
	{
	    check_untagged(&yystack.l_mark[-1].decl_spec);
	    yyval.parameter = new_parameter(&yystack.l_mark[-1].decl_spec, yystack.l_mark[0].declarator);
	}
break;
case 97:
#line 891 "grammar.y"
	{
	    check_untagged(&yystack.l_mark[-1].decl_spec);
	    yyval.parameter = new_parameter(&yystack.l_mark[-1].decl_spec, yystack.l_mark[0].declarator);
	}
break;
case 98:
#line 896 "grammar.y"
	{
	    check_untagged(&yystack.l_mark[0].decl_spec);
	    yyval.parameter = new_parameter(&yystack.l_mark[0].decl_spec, (Declarator *)0);
	}
break;
case 99:
#line 904 "grammar.y"
	{
	    new_ident_list(&yyval.param_list);
	}
break;
case 101:
#line 912 "grammar.y"
	{
	    new_ident_list(&yyval.param_list);
	    add_ident_list(&yyval.param_list, &yyval.param_list, yystack.l_mark[0].text.text);
	}
break;
case 102:
#line 917 "grammar.y"
	{
	    add_ident_list(&yyval.param_list, &yystack.l_mark[-2].param_list, yystack.l_mark[0].text.text);
	}
break;
case 103:
#line 924 "grammar.y"
	{
	    yyval.text = yystack.l_mark[0].text;
	}
break;
case 104:
#line 928 "grammar.y"
	{
#if OPT_LINTLIBRARY
	    if (lintLibrary()) { /* Lint doesn't grok C++ ref variables */
		yyval.text = yystack.l_mark[0].text;
	    } else
#endif
		(void)sprintf(yyval.text.text, "&%s", yystack.l_mark[0].text.text);
	    yyval.text.begin = yystack.l_mark[-1].text.begin;
	}
break;
case 105:
#line 941 "grammar.y"
	{
	    yyval.declarator = new_declarator(yystack.l_mark[0].text.text, "", yystack.l_mark[0].text.begin);
	}
break;
case 106:
#line 945 "grammar.y"
	{
	    yyval.declarator = yystack.l_mark[0].declarator;
	    (void)sprintf(buf, "%s%s", yystack.l_mark[-1].text.text, yyval.declarator->text);
	    free(yyval.declarator->text);
	    yyval.declarator->text = xstrdup(buf);
	    yyval.declarator->begin = yystack.l_mark[-1].text.begin;
	}
break;
case 108:
#line 957 "grammar.y"
	{
	    yyval.declarator = yystack.l_mark[-1].declarator;
	    (void)sprintf(buf, "(%s)", yyval.declarator->text);
	    free(yyval.declarator->text);
	    yyval.declarator->text = xstrdup(buf);
	    yyval.declarator->begin = yystack.l_mark[-2].text.begin;
	}
break;
case 109:
#line 965 "grammar.y"
	{
	    yyval.declarator = yystack.l_mark[-1].declarator;
	    (void)sprintf(buf, "%s%s", yyval.declarator->text, yystack.l_mark[0].text.text);
	    free(yyval.declarator->text);
	    yyval.declarator->text = xstrdup(buf);
	}
break;
case 110:
#line 972 "grammar.y"
	{
	    yyval.declarator = new_declarator(yystack.l_mark[0].text.text, "", yystack.l_mark[0].text.begin);
	}
break;
case 111:
#line 976 "grammar.y"
	{
	    yyval.declarator = new_declarator("%s()", "", yystack.l_mark[-3].declarator->begin);
	    yyval.declarator->params = yystack.l_mark[-1].param_list;
	    yyval.declarator->func_stack = yystack.l_mark[-3].declarator;
	    yyval.declarator->head = (yystack.l_mark[-3].declarator->func_stack == NULL) ? yyval.declarator : yystack.l_mark[-3].declarator->head;
	    yyval.declarator->func_def = FUNC_ANSI;
	}
break;
case 112:
#line 984 "grammar.y"
	{
	    yyval.declarator = new_declarator("%s()", "", yystack.l_mark[-2].declarator->begin);
	    yyval.declarator->func_stack = yystack.l_mark[-2].declarator;
	    yyval.declarator->head = (yystack.l_mark[-2].declarator->func_stack == NULL) ? yyval.declarator : yystack.l_mark[-2].declarator->head;
	    yyval.declarator->func_def = FUNC_ANSI;
	}
break;
case 113:
#line 991 "grammar.y"
	{
	    Declarator *d;

	    d = new_declarator("", "", yystack.l_mark[-2].text.begin);
	    yyval.declarator = new_declarator("%s()", "", yystack.l_mark[-2].text.begin);
	    yyval.declarator->params = yystack.l_mark[-1].param_list;
	    yyval.declarator->func_stack = d;
	    yyval.declarator->head = yyval.declarator;
	    yyval.declarator->func_def = FUNC_ANSI;
	}
break;
case 114:
#line 1002 "grammar.y"
	{
	    Declarator *d;

	    d = new_declarator("", "", yystack.l_mark[-1].text.begin);
	    yyval.declarator = new_declarator("%s()", "", yystack.l_mark[-1].text.begin);
	    yyval.declarator->func_stack = d;
	    yyval.declarator->head = yyval.declarator;
	    yyval.declarator->func_def = FUNC_ANSI;
	}
break;
#line 2691 "grammar.tab.c"
    default:
        break;
    }
    yystack.s_mark -= yym;
    yystate = *yystack.s_mark;
    yystack.l_mark -= yym;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    yystack.p_mark -= yym;
#endif
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
        {
            fprintf(stderr, "%s[%d]: after reduction, ", YYDEBUGSTR, yydepth);
#ifdef YYSTYPE_TOSTRING
#if YYBTYACC
            if (!yytrial)
#endif /* YYBTYACC */
                fprintf(stderr, "result is <%s>, ", YYSTYPE_TOSTRING(yystos[YYFINAL], yyval));
#endif
            fprintf(stderr, "shifting from state 0 to final state %d\n", YYFINAL);
        }
#endif
        yystate = YYFINAL;
        *++yystack.s_mark = YYFINAL;
        *++yystack.l_mark = yyval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        *++yystack.p_mark = yyloc;
#endif
        if (yychar < 0)
        {
#if YYBTYACC
            do {
            if (yylvp < yylve)
            {
                /* we're currently re-reading tokens */
                yylval = *yylvp++;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                yylloc = *yylpp++;
#endif
                yychar = *yylexp++;
                break;
            }
            if (yyps->save)
            {
                /* in trial mode; save scanner results for future parse attempts */
                if (yylvp == yylvlim)
                {   /* Enlarge lexical value queue */
                    size_t p = (size_t) (yylvp - yylvals);
                    size_t s = (size_t) (yylvlim - yylvals);

                    s += YYLVQUEUEGROWTH;
                    if ((yylexemes = realloc(yylexemes, s * sizeof(YYINT))) == NULL)
                        goto yyenomem;
                    if ((yylvals   = realloc(yylvals, s * sizeof(YYSTYPE))) == NULL)
                        goto yyenomem;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                    if ((yylpsns   = realloc(yylpsns, s * sizeof(YYLTYPE))) == NULL)
                        goto yyenomem;
#endif
                    yylvp   = yylve = yylvals + p;
                    yylvlim = yylvals + s;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                    yylpp   = yylpe = yylpsns + p;
                    yylplim = yylpsns + s;
#endif
                    yylexp  = yylexemes + p;
                }
                *yylexp = (YYINT) YYLEX;
                *yylvp++ = yylval;
                yylve++;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
                *yylpp++ = yylloc;
                yylpe++;
#endif
                yychar = *yylexp++;
                break;
            }
            /* normal operation, no conflict encountered */
#endif /* YYBTYACC */
            yychar = YYLEX;
#if YYBTYACC
            } while (0);
#endif /* YYBTYACC */
            if (yychar < 0) yychar = YYEOF;
#if YYDEBUG
            if (yydebug)
            {
                if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];
                fprintf(stderr, "%s[%d]: state %d, reading token %d (%s)\n",
                                YYDEBUGSTR, yydepth, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == YYEOF) goto yyaccept;
        goto yyloop;
    }
    if (((yyn = yygindex[yym]) != 0) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
    {
        fprintf(stderr, "%s[%d]: after reduction, ", YYDEBUGSTR, yydepth);
#ifdef YYSTYPE_TOSTRING
#if YYBTYACC
        if (!yytrial)
#endif /* YYBTYACC */
            fprintf(stderr, "result is <%s>, ", YYSTYPE_TOSTRING(yystos[yystate], yyval));
#endif
        fprintf(stderr, "shifting from state %d to state %d\n", *yystack.s_mark, yystate);
    }
#endif
    if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
    *++yystack.s_mark = (YYINT) yystate;
    *++yystack.l_mark = yyval;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    *++yystack.p_mark = yyloc;
#endif
    goto yyloop;
#if YYBTYACC

    /* Reduction declares that this path is valid. Set yypath and do a full parse */
yyvalid:
    if (yypath) YYABORT;
    while (yyps->save)
    {
        YYParseState *save = yyps->save;
        yyps->save = save->save;
        save->save = yypath;
        yypath = save;
    }
#if YYDEBUG
    if (yydebug)
        fprintf(stderr, "%s[%d]: state %d, CONFLICT trial successful, backtracking to state %d, %d tokens\n",
                        YYDEBUGSTR, yydepth, yystate, yypath->state, (int)(yylvp - yylvals - yypath->lexeme));
#endif
    if (yyerrctx)
    {
        yyFreeState(yyerrctx);
        yyerrctx = NULL;
    }
    yylvp          = yylvals + yypath->lexeme;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    yylpp          = yylpsns + yypath->lexeme;
#endif
    yylexp         = yylexemes + yypath->lexeme;
    yychar         = YYEMPTY;
    yystack.s_mark = yystack.s_base + (yypath->yystack.s_mark - yypath->yystack.s_base);
    memcpy (yystack.s_base, yypath->yystack.s_base, (size_t) (yystack.s_mark - yystack.s_base + 1) * sizeof(YYINT));
    yystack.l_mark = yystack.l_base + (yypath->yystack.l_mark - yypath->yystack.l_base);
    memcpy (yystack.l_base, yypath->yystack.l_base, (size_t) (yystack.l_mark - yystack.l_base + 1) * sizeof(YYSTYPE));
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
    yystack.p_mark = yystack.p_base + (yypath->yystack.p_mark - yypath->yystack.p_base);
    memcpy (yystack.p_base, yypath->yystack.p_base, (size_t) (yystack.p_mark - yystack.p_base + 1) * sizeof(YYLTYPE));
#endif
    yystate        = yypath->state;
    goto yyloop;
#endif /* YYBTYACC */

yyoverflow:
    YYERROR_CALL("yacc stack overflow");
#if YYBTYACC
    goto yyabort_nomem;
yyenomem:
    YYERROR_CALL("memory exhausted");
yyabort_nomem:
#endif /* YYBTYACC */
    yyresult = 2;
    goto yyreturn;

yyabort:
    yyresult = 1;
    goto yyreturn;

yyaccept:
#if YYBTYACC
    if (yyps->save) goto yyvalid;
#endif /* YYBTYACC */
    yyresult = 0;

yyreturn:
#if defined(YYDESTRUCT_CALL)
    if (yychar != YYEOF && yychar != YYEMPTY)
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        YYDESTRUCT_CALL("cleanup: discarding token", yychar, &yylval, &yylloc);
#else
        YYDESTRUCT_CALL("cleanup: discarding token", yychar, &yylval);
#endif /* defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED) */

    {
        YYSTYPE *pv;
#if defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED)
        YYLTYPE *pp;

        for (pv = yystack.l_base, pp = yystack.p_base; pv <= yystack.l_mark; ++pv, ++pp)
             YYDESTRUCT_CALL("cleanup: discarding state",
                             yystos[*(yystack.s_base + (pv - yystack.l_base))], pv, pp);
#else
        for (pv = yystack.l_base; pv <= yystack.l_mark; ++pv)
             YYDESTRUCT_CALL("cleanup: discarding state",
                             yystos[*(yystack.s_base + (pv - yystack.l_base))], pv);
#endif /* defined(YYLTYPE) || defined(YYLTYPE_IS_DECLARED) */
    }
#endif /* defined(YYDESTRUCT_CALL) */

#if YYBTYACC
    if (yyerrctx)
    {
        yyFreeState(yyerrctx);
        yyerrctx = NULL;
    }
    while (yyps)
    {
        YYParseState *save = yyps;
        yyps = save->save;
        save->save = NULL;
        yyFreeState(save);
    }
    while (yypath)
    {
        YYParseState *save = yypath;
        yypath = save->save;
        save->save = NULL;
        yyFreeState(save);
    }
#endif /* YYBTYACC */
    yyfreestack(&yystack);
    return (yyresult);
}
