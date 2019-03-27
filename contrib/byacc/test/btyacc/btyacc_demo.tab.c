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
#define YYBTYACC 1
#define YYDEBUGSTR (yytrial ? YYPREFIX "debug(trial)" : YYPREFIX "debug")

#ifndef yyparse
#define yyparse    demo_parse
#endif /* yyparse */

#ifndef yylex
#define yylex      demo_lex
#endif /* yylex */

#ifndef yyerror
#define yyerror    demo_error
#endif /* yyerror */

#ifndef yychar
#define yychar     demo_char
#endif /* yychar */

#ifndef yyval
#define yyval      demo_val
#endif /* yyval */

#ifndef yylval
#define yylval     demo_lval
#endif /* yylval */

#ifndef yydebug
#define yydebug    demo_debug
#endif /* yydebug */

#ifndef yynerrs
#define yynerrs    demo_nerrs
#endif /* yynerrs */

#ifndef yyerrflag
#define yyerrflag  demo_errflag
#endif /* yyerrflag */

#ifndef yylhs
#define yylhs      demo_lhs
#endif /* yylhs */

#ifndef yylen
#define yylen      demo_len
#endif /* yylen */

#ifndef yydefred
#define yydefred   demo_defred
#endif /* yydefred */

#ifndef yystos
#define yystos     demo_stos
#endif /* yystos */

#ifndef yydgoto
#define yydgoto    demo_dgoto
#endif /* yydgoto */

#ifndef yysindex
#define yysindex   demo_sindex
#endif /* yysindex */

#ifndef yyrindex
#define yyrindex   demo_rindex
#endif /* yyrindex */

#ifndef yygindex
#define yygindex   demo_gindex
#endif /* yygindex */

#ifndef yytable
#define yytable    demo_table
#endif /* yytable */

#ifndef yycheck
#define yycheck    demo_check
#endif /* yycheck */

#ifndef yyname
#define yyname     demo_name
#endif /* yyname */

#ifndef yyrule
#define yyrule     demo_rule
#endif /* yyrule */

#ifndef yyloc
#define yyloc      demo_loc
#endif /* yyloc */

#ifndef yylloc
#define yylloc     demo_lloc
#endif /* yylloc */

#if YYBTYACC

#ifndef yycindex
#define yycindex   demo_cindex
#endif /* yycindex */

#ifndef yyctable
#define yyctable   demo_ctable
#endif /* yyctable */

#endif /* YYBTYACC */

#define YYPREFIX "demo_"

#define YYPURE 0

#line 15 "btyacc_demo.y"
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
#ifdef YYSTYPE
#undef  YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#endif
#ifndef YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#line 36 "btyacc_demo.y"
typedef union {
    Scope	*scope;
    Expr	*expr;
    Expr_List	*elist;
    Type	*type;
    Decl	*decl;
    Decl_List	*dlist;
    Code	*code;
    char	*id;
    } YYSTYPE;
#endif /* !YYSTYPE_IS_DECLARED */
#line 167 "btyacc_demo.tab.c"

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
/* Default: YYLTYPE is the text position type. */
typedef struct YYLTYPE
{
    int first_line;
    int first_column;
    int last_line;
    int last_column;
    unsigned source;
} YYLTYPE;
#define YYLTYPE_IS_DECLARED 1
#endif
#define YYRHSLOC(rhs, k) ((rhs)[k])

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
#define YYERROR_DECL() yyerror(YYLTYPE *loc, const char *s)
#endif
#ifndef YYERROR_CALL
#define YYERROR_CALL(msg) yyerror(&yylloc, msg)
#endif

#ifndef YYDESTRUCT_DECL
#define YYDESTRUCT_DECL() yydestruct(const char *msg, int psymb, YYSTYPE *val, YYLTYPE *loc)
#endif
#ifndef YYDESTRUCT_CALL
#define YYDESTRUCT_CALL(msg, psymb, val, loc) yydestruct(msg, psymb, val, loc)
#endif

extern int YYPARSE_DECL();

#define PREFIX 257
#define POSTFIX 258
#define ID 259
#define CONSTANT 260
#define EXTERN 261
#define REGISTER 262
#define STATIC 263
#define CONST 264
#define VOLATILE 265
#define IF 266
#define THEN 267
#define ELSE 268
#define CLCL 269
#define YYERRCODE 256
typedef short YYINT;
static const YYINT demo_lhs[] = {                        -1,
   15,   15,   15,   12,   18,    0,    4,   19,    4,    2,
   20,    2,   10,   10,   13,   13,   11,   11,   11,   11,
   11,   14,   14,   21,   22,    3,    3,    8,    8,   23,
   24,    8,    8,    8,    8,   16,   16,   17,   17,    9,
    1,    1,    1,    1,    1,    1,    1,    1,    5,    5,
   25,   26,    5,    5,   27,    5,    6,    6,    7,
};
static const YYINT demo_len[] = {                         2,
    0,    1,    3,    2,    0,    2,    0,    0,    3,    3,
    0,    4,    1,    3,    0,    2,    1,    1,    1,    1,
    1,    1,    1,    0,    0,    5,    1,    0,    1,    0,
    0,    5,    5,    5,    6,    0,    1,    4,    1,    2,
    4,    4,    4,    4,    4,    3,    1,    1,    1,    2,
    0,    0,   11,    8,    0,    2,    0,    3,    4,
};
static const YYINT demo_defred[] = {                      5,
    0,    7,    0,    0,   19,   20,   21,   22,   23,    2,
    9,    0,   13,   18,   17,    0,   15,   30,   29,    0,
    0,    0,    0,    0,   31,   10,   24,   24,   24,    0,
   14,    3,   16,   25,    0,   25,    0,    0,    8,   12,
    0,    0,    0,   39,    0,    0,    0,    8,   47,   48,
    0,   57,    0,   32,    0,    0,   15,   30,    0,   30,
   30,   30,   30,   30,   34,    0,    0,    0,   46,    0,
    0,    0,    0,    0,   59,    0,   38,    0,    0,   43,
   45,   44,    0,    0,   49,   58,    0,   30,   50,   56,
    0,    0,    0,   51,    0,    0,   52,    0,   53,
};
#if defined(YYDESTRUCT_CALL) || defined(YYSTYPE_TOSTRING)
static const YYINT demo_stos[] = {                        0,
  271,  289,  275,  290,  261,  262,  263,  264,  265,  269,
  273,  281,  282,  283,  285,  286,   42,   40,  259,  274,
  279,  290,  259,  284,  294,   59,   44,   40,   91,  291,
  282,  269,  285,  292,  295,  292,  292,  292,  123,  278,
  293,  279,  293,  280,  281,  287,  288,   42,  259,  260,
  272,  290,  279,   41,  279,  279,   41,   44,  290,   43,
   45,   42,   47,   37,   93,  277,  284,  294,  272,  294,
  294,  294,  294,  294,  125,  290,  280,  272,  272,  272,
  272,  272,  266,  272,  273,  276,  298,   40,   59,  278,
  294,  272,   41,  267,  296,  276,  268,  297,  276,
};
#endif /* YYDESTRUCT_CALL || YYSTYPE_TOSTRING */
static const YYINT demo_dgoto[] = {                       1,
   84,   85,   20,    3,   86,   66,   40,   21,   44,   12,
   13,   14,   24,   15,   16,   46,   47,    2,   22,   30,
   34,   41,   25,   35,   95,   98,   87,
};
static const YYINT demo_sindex[] = {                      0,
    0,    0,    0, -103,    0,    0,    0,    0,    0,    0,
    0,  -31,    0,    0,    0, -238,    0,    0,    0,    4,
  -36, -103,    0, -133,    0,    0,    0,    0,    0,  -94,
    0,    0,    0,    0,  -40,    0, -103,  -33,    0,    0,
  -40,  -25,  -40,    0,  -31,    8,   15,    0,    0,    0,
   -2,    0,  -36,    0,  -36,  -36,    0,    0,  -33,    0,
    0,    0,    0,    0,    0,  -92, -133, -103,    0,  -33,
  -33,  -33,  -33,  -33,    0,   -8,    0,   23,   23,    0,
    0,    0,   11,   75,    0,    0,  -94,    0,    0,    0,
  -33,   96, -194,    0,   -8,    0,    0,   -8,    0,
};
static const YYINT demo_rindex[] = {                      0,
    0,    0,    1, -181,    0,    0,    0,    0,    0,    0,
    0,   17,    0,    0,    0,    0,    0,    0,    0,    0,
  -39, -181,   12,  -34,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,   -5,    0,  -11,    0,    0,    0,
  -17,    0,   28,    0,  -41,    0,   47,    0,    0,    0,
    0,    0,  -13,    0,   18,   70,    0,    0,    0,    0,
    0,    0,    0,    0,    0,  -19,  -27, -181,    0,    0,
    0,    0,    0,    0,    0,  -29,    0,   56,   64,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,  -29,  -30,    0,  -29,    0,
};
#if YYBTYACC
static const YYINT demo_cindex[] = {                      0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,  -22,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0, -179,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,   52,    0,    0,    0,    0,    0,
   58,    0,   62,    0,  -21,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0, -146,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0, -143, -147,    0, -134,    0,
};
#endif
static const YYINT demo_gindex[] = {                      0,
    9,  143,    0,    0,   50,    0,   63,  101,   83,    7,
  130,    0,   98,    2,    0,    0,    0,    0,   19,    0,
   10,  117,   66,    0,    0,    0,    0,
};
#define YYTABLESIZE 286
static const YYINT demo_table[] = {                      28,
    6,   17,   28,   28,   27,   24,   24,   24,   48,   24,
   17,   54,   35,   35,   28,   54,   35,    0,    0,   27,
   23,    4,    8,   28,   24,   33,   28,   33,   39,   36,
   33,   35,   75,   48,   64,   28,   36,   37,   38,   62,
   60,   28,   61,   45,   63,   33,   51,   27,   57,   28,
   88,    4,    4,    4,   29,    4,   24,   52,   58,   64,
   28,   26,   26,   35,   62,   29,   59,   69,   33,   63,
    4,   28,   94,   28,   45,   28,   26,    1,   78,   79,
   80,   81,   82,   11,   76,   28,   28,   37,   24,    6,
   65,    0,   54,   55,   54,   35,   41,    0,   41,   92,
   41,    0,    4,    8,   42,   28,   42,   28,   42,   33,
   40,   64,    9,   40,   41,    9,   62,   60,   28,   61,
   12,   63,   42,   68,    9,   70,   71,   72,   73,   74,
    8,    9,   64,   89,    4,   42,   93,   62,   60,   28,
   61,   53,   63,   55,   96,   56,   11,   99,   41,   90,
   77,   31,   43,   91,   67,    0,   42,    5,    6,    7,
    8,    9,    0,    0,    0,   10,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,   19,    8,
    8,    8,    8,    8,   24,   49,   50,    8,   54,   54,
   54,   54,   54,   54,   54,   54,    3,    3,   54,    8,
    8,    8,    8,    8,    8,    8,    8,    1,    0,    8,
    0,   50,    5,    6,    7,    8,    9,   83,    0,    8,
   10,    8,    8,    8,    8,    8,    0,    0,    0,    8,
    4,    0,    4,    4,    4,    4,    4,    8,    8,    8,
    8,    8,    0,    0,    0,    8,
};
static const YYINT demo_check[] = {                      41,
    0,   42,   44,   40,   44,   40,   41,   42,   42,   44,
   42,   42,   40,   41,   40,   41,   44,   40,   40,   59,
  259,    3,   42,   41,   59,   24,   44,   41,  123,   41,
   44,   59,  125,   42,   37,   41,   27,   28,   29,   42,
   43,   59,   45,   37,   47,   59,   38,   44,   41,   91,
   40,   40,   41,   42,   91,   44,   91,   39,   44,   37,
   44,   44,   59,   91,   42,   91,   48,   59,   67,   47,
   59,   44,  267,   91,   68,   59,   59,  259,   70,   71,
   72,   73,   74,  123,   66,   91,   59,   41,  123,  269,
   93,   40,  123,  123,  125,  123,   41,   40,   43,   91,
   45,   40,   91,  123,   41,  123,   43,   91,   45,  123,
   41,   37,  259,   44,   59,  259,   42,   43,   91,   45,
  268,   47,   59,   58,  259,   60,   61,   62,   63,   64,
  264,  265,   37,   59,  123,   35,   41,   42,   43,  123,
   45,   41,   47,   43,   95,   45,    4,   98,   93,   87,
   68,   22,   36,   88,   57,   -1,   93,  261,  262,  263,
  264,  265,   -1,   -1,   -1,  269,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,  259,  261,
  262,  263,  264,  265,  259,  259,  260,  269,  259,  260,
  261,  262,  263,  264,  265,  266,  259,  259,  269,  259,
  260,  261,  262,  263,  264,  265,  266,  259,   -1,  269,
   -1,  260,  261,  262,  263,  264,  265,  266,   -1,  259,
  269,  261,  262,  263,  264,  265,   -1,   -1,   -1,  269,
  259,   -1,  261,  262,  263,  264,  265,  261,  262,  263,
  264,  265,   -1,   -1,   -1,  269,
};
#if YYBTYACC
static const YYINT demo_ctable[] = {                     18,
   28,   -1,   19,    8,   -1,   32,    4,   -1,   49,    1,
   -1,   97,   54,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
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
   -1,   -1,   -1,   -1,   -1,
};
#endif
#define YYFINAL 1
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 269
#define YYUNDFTOKEN 299
#define YYTRANSLATE(a) ((a) > YYMAXTOKEN ? YYUNDFTOKEN : (a))
#if YYDEBUG
static const char *const demo_name[] = {

"$end",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
"'%'",0,0,"'('","')'","'*'","'+'","','","'-'","'.'","'/'",0,0,0,0,0,0,0,0,0,0,0,
"';'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"'['",0,
"']'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"'{'",0,"'}'",0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,"error","PREFIX","POSTFIX","ID","CONSTANT","EXTERN",
"REGISTER","STATIC","CONST","VOLATILE","IF","THEN","ELSE","CLCL","$accept",
"input","expr","decl","declarator_list","decl_list","statement",
"statement_list","block_statement","declarator","formal_arg","decl_specs",
"decl_spec","typename","cv_quals","cv_qual","opt_scope","formal_arg_list",
"nonempty_formal_arg_list","$$1","$$2","$$3","$$4","$$5","$$6","$$7","$$8",
"$$9","$$10","illegal-symbol",
};
static const char *const demo_rule[] = {
"$accept : input",
"opt_scope :",
"opt_scope : CLCL",
"opt_scope : opt_scope ID CLCL",
"typename : opt_scope ID",
"$$1 :",
"input : $$1 decl_list",
"decl_list :",
"$$2 :",
"decl_list : decl_list $$2 decl",
"decl : decl_specs declarator_list ';'",
"$$3 :",
"decl : decl_specs declarator $$3 block_statement",
"decl_specs : decl_spec",
"decl_specs : decl_specs $$2 decl_spec",
"cv_quals :",
"cv_quals : cv_quals cv_qual",
"decl_spec : cv_qual",
"decl_spec : typename",
"decl_spec : EXTERN",
"decl_spec : REGISTER",
"decl_spec : STATIC",
"cv_qual : CONST",
"cv_qual : VOLATILE",
"$$4 :",
"$$5 :",
"declarator_list : declarator_list ',' $$4 $$5 declarator",
"declarator_list : declarator",
"declarator :",
"declarator : ID",
"$$6 :",
"$$7 :",
"declarator : '(' $$6 $$7 declarator ')'",
"declarator : '*' cv_quals $$4 $$5 declarator",
"declarator : declarator '[' $$4 expr ']'",
"declarator : declarator '(' $$4 formal_arg_list ')' cv_quals",
"formal_arg_list :",
"formal_arg_list : nonempty_formal_arg_list",
"nonempty_formal_arg_list : nonempty_formal_arg_list ',' $$6 formal_arg",
"nonempty_formal_arg_list : formal_arg",
"formal_arg : decl_specs declarator",
"expr : expr '+' $$6 expr",
"expr : expr '-' $$6 expr",
"expr : expr '*' $$6 expr",
"expr : expr '%' $$6 expr",
"expr : expr '/' $$6 expr",
"expr : '*' $$2 expr",
"expr : ID",
"expr : CONSTANT",
"statement : decl",
"statement : expr ';'",
"$$8 :",
"$$9 :",
"statement : IF '(' $$6 expr ')' THEN $$8 statement ELSE $$9 statement",
"statement : IF '(' $$6 expr ')' THEN $$8 statement",
"$$10 :",
"statement : $$10 block_statement",
"statement_list :",
"statement_list : statement_list $$2 statement",
"block_statement : '{' $$2 statement_list '}'",

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
#line 200 "btyacc_demo.y"

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
#line 664 "btyacc_demo.tab.c"

/* Release memory associated with symbol. */
#if ! defined YYDESTRUCT_IS_DECLARED
static void
YYDESTRUCT_DECL()
{
    switch (psymb)
    {
	case 43:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 681 "btyacc_demo.tab.c"
	case 45:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 690 "btyacc_demo.tab.c"
	case 42:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 699 "btyacc_demo.tab.c"
	case 47:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 708 "btyacc_demo.tab.c"
	case 37:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 717 "btyacc_demo.tab.c"
	case 257:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 726 "btyacc_demo.tab.c"
	case 258:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 735 "btyacc_demo.tab.c"
	case 40:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 744 "btyacc_demo.tab.c"
	case 91:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 753 "btyacc_demo.tab.c"
	case 46:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 762 "btyacc_demo.tab.c"
	case 259:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).id); }
	break;
#line 771 "btyacc_demo.tab.c"
	case 260:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).expr); }
	break;
#line 780 "btyacc_demo.tab.c"
	case 261:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 789 "btyacc_demo.tab.c"
	case 262:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 798 "btyacc_demo.tab.c"
	case 263:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 807 "btyacc_demo.tab.c"
	case 264:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 816 "btyacc_demo.tab.c"
	case 265:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 825 "btyacc_demo.tab.c"
	case 266:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 834 "btyacc_demo.tab.c"
	case 267:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 843 "btyacc_demo.tab.c"
	case 268:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 852 "btyacc_demo.tab.c"
	case 269:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 861 "btyacc_demo.tab.c"
	case 59:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 870 "btyacc_demo.tab.c"
	case 44:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 879 "btyacc_demo.tab.c"
	case 41:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 888 "btyacc_demo.tab.c"
	case 93:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 897 "btyacc_demo.tab.c"
	case 123:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 906 "btyacc_demo.tab.c"
	case 125:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 915 "btyacc_demo.tab.c"
	case 270:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 924 "btyacc_demo.tab.c"
	case 271:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 933 "btyacc_demo.tab.c"
	case 272:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).expr); }
	break;
#line 942 "btyacc_demo.tab.c"
	case 273:
#line 67 "btyacc_demo.y"
	{ /* 'msg' is a 'char *' indicating the context of destructor invocation*/
		  printf("%s accessed by symbol \"decl\" (case s.b. 273) @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).decl->scope); free((*val).decl->type); }
	break;
#line 952 "btyacc_demo.tab.c"
	case 274:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 961 "btyacc_demo.tab.c"
	case 275:
#line 83 "btyacc_demo.y"
	{ printf("%s accessed by symbol with no type @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  /* in this example, we don't know what to do here */ }
	break;
#line 970 "btyacc_demo.tab.c"
	case 276:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).code); }
	break;
#line 979 "btyacc_demo.tab.c"
	case 277:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).code); }
	break;
#line 988 "btyacc_demo.tab.c"
	case 278:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).code); }
	break;
#line 997 "btyacc_demo.tab.c"
	case 279:
#line 73 "btyacc_demo.y"
	{ printf("%s accessed by symbol with type <decl> (case s.b. 279 & 280) @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).decl); }
	break;
#line 1006 "btyacc_demo.tab.c"
	case 280:
#line 73 "btyacc_demo.y"
	{ printf("%s accessed by symbol with type <decl> (case s.b. 279 & 280) @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).decl); }
	break;
#line 1015 "btyacc_demo.tab.c"
	case 281:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1024 "btyacc_demo.tab.c"
	case 282:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1033 "btyacc_demo.tab.c"
	case 283:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1042 "btyacc_demo.tab.c"
	case 284:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1051 "btyacc_demo.tab.c"
	case 285:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1060 "btyacc_demo.tab.c"
	case 286:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1069 "btyacc_demo.tab.c"
	case 287:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).dlist); }
	break;
#line 1078 "btyacc_demo.tab.c"
	case 288:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).dlist); }
	break;
#line 1087 "btyacc_demo.tab.c"
	case 289:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1096 "btyacc_demo.tab.c"
	case 290:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1105 "btyacc_demo.tab.c"
	case 291:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1114 "btyacc_demo.tab.c"
	case 292:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1123 "btyacc_demo.tab.c"
	case 293:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1132 "btyacc_demo.tab.c"
	case 294:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1141 "btyacc_demo.tab.c"
	case 295:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).type); }
	break;
#line 1150 "btyacc_demo.tab.c"
	case 296:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1159 "btyacc_demo.tab.c"
	case 297:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1168 "btyacc_demo.tab.c"
	case 298:
#line 78 "btyacc_demo.y"
	{ printf("%s accessed by symbol of any type other than <decl>  @ position[%d,%d..%d,%d]\n",
			 msg,
			 (*loc).first_line, (*loc).first_column,
			 (*loc).last_line, (*loc).last_column);
		  free((*val).scope); }
	break;
#line 1177 "btyacc_demo.tab.c"
    }
}
#define YYDESTRUCT_IS_DECLARED 1
#endif

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
case 1:
#line 93 "btyacc_demo.y"
{ yyval.scope = yystack.l_mark[0].scope; }
break;
case 2:
#line 94 "btyacc_demo.y"
{ yyval.scope = global_scope; }
break;
case 3:
#line 95 "btyacc_demo.y"
{ Decl *d = lookup(yystack.l_mark[-2].scope, yystack.l_mark[-1].id);
			  if (!d || !d->scope) YYERROR;
			  yyval.scope = d->scope; }
break;
case 4:
#line 101 "btyacc_demo.y"
{ Decl *d = lookup(yystack.l_mark[-1].scope, yystack.l_mark[0].id);
	if (d == NULL || d->istype() == 0) YYERROR;
	yyval.type = d->type; }
break;
case 5:
#line 106 "btyacc_demo.y"
yyval.scope = global_scope = new_scope(0);
break;
case 8:
#line 107 "btyacc_demo.y"
yyval.scope = yystack.l_mark[-1].scope;
break;
case 10:
#line 109 "btyacc_demo.y"
{YYVALID;}
break;
case 11:
#line 110 "btyacc_demo.y"
yyval.scope = start_fn_def(yystack.l_mark[-2].scope, yystack.l_mark[0].decl);
break;
case 12:
  if (!yytrial)
#line 111 "btyacc_demo.y"
	{ /* demonstrate use of @$ & @N, although this is just the
	   default computation and so is not necessary */
	yyloc.first_line   = yystack.p_mark[-3].first_line;
	yyloc.first_column = yystack.p_mark[-3].first_column;
	yyloc.last_line    = yystack.p_mark[0].last_line;
	yyloc.last_column  = yystack.p_mark[0].last_column;
	finish_fn_def(yystack.l_mark[-2].decl, yystack.l_mark[0].code); }
break;
case 13:
#line 121 "btyacc_demo.y"
{ yyval.type = yystack.l_mark[0].type; }
break;
case 14:
#line 122 "btyacc_demo.y"
{ yyval.type = type_combine(yystack.l_mark[-2].type, yystack.l_mark[0].type); }
break;
case 15:
#line 125 "btyacc_demo.y"
{ yyval.type = 0; }
break;
case 16:
#line 126 "btyacc_demo.y"
{ yyval.type = type_combine(yystack.l_mark[-1].type, yystack.l_mark[0].type); }
break;
case 17:
#line 130 "btyacc_demo.y"
{ yyval.type = yystack.l_mark[0].type; }
break;
case 18:
#line 131 "btyacc_demo.y"
{ yyval.type = yystack.l_mark[0].type; }
break;
case 19:
#line 132 "btyacc_demo.y"
{ yyval.type = bare_extern(); }
break;
case 20:
#line 133 "btyacc_demo.y"
{ yyval.type = bare_register(); }
break;
case 21:
#line 134 "btyacc_demo.y"
{ yyval.type = bare_static(); }
break;
case 22:
#line 138 "btyacc_demo.y"
{ yyval.type = bare_const(); }
break;
case 23:
#line 139 "btyacc_demo.y"
{ yyval.type = bare_volatile(); }
break;
case 24:
#line 143 "btyacc_demo.y"
yyval.scope = yystack.l_mark[-3].scope;
break;
case 25:
#line 143 "btyacc_demo.y"
yyval.type =  yystack.l_mark[-3].type;
break;
case 28:
#line 148 "btyacc_demo.y"
{ if (!yystack.l_mark[0].type) YYERROR; }  if (!yytrial)
#line 149 "btyacc_demo.y"
{ yyval.decl = declare(yystack.l_mark[-1].scope, 0, yystack.l_mark[0].type); }
break;
case 29:
  if (!yytrial)
#line 150 "btyacc_demo.y"
	{ yyval.decl = declare(yystack.l_mark[-2].scope, yystack.l_mark[0].id, yystack.l_mark[-1].type); }
break;
case 30:
#line 151 "btyacc_demo.y"
yyval.scope = yystack.l_mark[-2].scope;
break;
case 31:
#line 151 "btyacc_demo.y"
yyval.type =  yystack.l_mark[-2].type;
break;
case 32:
  if (!yytrial)
#line 151 "btyacc_demo.y"
	{ yyval.decl = yystack.l_mark[-1].decl; }
break;
case 33:
  if (!yytrial)
#line 153 "btyacc_demo.y"
	{ yyval.decl = make_pointer(yystack.l_mark[0].decl, yystack.l_mark[-3].type); }
break;
case 34:
  if (!yytrial)
#line 155 "btyacc_demo.y"
	{ yyval.decl = make_array(yystack.l_mark[-4].decl->type, yystack.l_mark[-1].expr); }
break;
case 35:
  if (!yytrial)
#line 157 "btyacc_demo.y"
	{ yyval.decl = build_function(yystack.l_mark[-5].decl, yystack.l_mark[-2].dlist, yystack.l_mark[0].type); }
break;
case 36:
  if (!yytrial)
#line 160 "btyacc_demo.y"
	{ yyval.dlist = 0; }
break;
case 37:
  if (!yytrial)
#line 161 "btyacc_demo.y"
	{ yyval.dlist = yystack.l_mark[0].dlist; }
break;
case 38:
  if (!yytrial)
#line 164 "btyacc_demo.y"
	{ yyval.dlist = append_dlist(yystack.l_mark[-3].dlist, yystack.l_mark[0].decl); }
break;
case 39:
  if (!yytrial)
#line 165 "btyacc_demo.y"
	{ yyval.dlist = build_dlist(yystack.l_mark[0].decl); }
break;
case 40:
  if (!yytrial)
#line 168 "btyacc_demo.y"
	{ yyval.decl = yystack.l_mark[0].decl; }
break;
case 41:
  if (!yytrial)
#line 172 "btyacc_demo.y"
	{ yyval.expr = build_expr(yystack.l_mark[-3].expr, ADD, yystack.l_mark[0].expr); }
break;
case 42:
  if (!yytrial)
#line 173 "btyacc_demo.y"
	{ yyval.expr = build_expr(yystack.l_mark[-3].expr, SUB, yystack.l_mark[0].expr); }
break;
case 43:
  if (!yytrial)
#line 174 "btyacc_demo.y"
	{ yyval.expr = build_expr(yystack.l_mark[-3].expr, MUL, yystack.l_mark[0].expr); }
break;
case 44:
  if (!yytrial)
#line 175 "btyacc_demo.y"
	{ yyval.expr = build_expr(yystack.l_mark[-3].expr, MOD, yystack.l_mark[0].expr); }
break;
case 45:
  if (!yytrial)
#line 176 "btyacc_demo.y"
	{ yyval.expr = build_expr(yystack.l_mark[-3].expr, DIV, yystack.l_mark[0].expr); }
break;
case 46:
  if (!yytrial)
#line 177 "btyacc_demo.y"
	{ yyval.expr = build_expr(0, DEREF, yystack.l_mark[0].expr); }
break;
case 47:
  if (!yytrial)
#line 178 "btyacc_demo.y"
	{ yyval.expr = var_expr(yystack.l_mark[-1].scope, yystack.l_mark[0].id); }
break;
case 48:
  if (!yytrial)
#line 179 "btyacc_demo.y"
	{ yyval.expr = yystack.l_mark[0].expr; }
break;
case 49:
  if (!yytrial)
#line 183 "btyacc_demo.y"
	{ yyval.code = 0; }
break;
case 50:
#line 184 "btyacc_demo.y"
{YYVALID;}  if (!yytrial)
#line 184 "btyacc_demo.y"
{ yyval.code = build_expr_code(yystack.l_mark[-1].expr); }
break;
case 51:
#line 185 "btyacc_demo.y"
yyval.scope = yystack.l_mark[-6].scope;
break;
case 52:
#line 185 "btyacc_demo.y"
yyval.scope = yystack.l_mark[-9].scope;
break;
case 53:
#line 185 "btyacc_demo.y"
{YYVALID;}  if (!yytrial)
#line 186 "btyacc_demo.y"
{ yyval.code = build_if(yystack.l_mark[-7].expr, yystack.l_mark[-3].code, yystack.l_mark[0].code); }
break;
case 54:
#line 187 "btyacc_demo.y"
{YYVALID;}  if (!yytrial)
#line 188 "btyacc_demo.y"
{ yyval.code = build_if(yystack.l_mark[-4].expr, yystack.l_mark[0].code, 0); }
break;
case 55:
#line 189 "btyacc_demo.y"
yyval.scope = new_scope(yystack.l_mark[0].scope);
break;
case 56:
#line 189 "btyacc_demo.y"
{YYVALID;}  if (!yytrial)
#line 189 "btyacc_demo.y"
{ yyval.code = yystack.l_mark[0].code; }
break;
case 57:
  if (!yytrial)
#line 192 "btyacc_demo.y"
	{ yyval.code = 0; }
break;
case 58:
  if (!yytrial)
#line 193 "btyacc_demo.y"
	{ yyval.code = code_append(yystack.l_mark[-2].code, yystack.l_mark[0].code); }
break;
case 59:
  if (!yytrial)
#line 197 "btyacc_demo.y"
	{ yyval.code = yystack.l_mark[-1].code; }
break;
#line 2110 "btyacc_demo.tab.c"
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
