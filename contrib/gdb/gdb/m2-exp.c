/* A Bison parser, made by GNU Bison 1.875.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     INT = 258,
     HEX = 259,
     ERROR = 260,
     UINT = 261,
     M2_TRUE = 262,
     M2_FALSE = 263,
     CHAR = 264,
     FLOAT = 265,
     STRING = 266,
     NAME = 267,
     BLOCKNAME = 268,
     IDENT = 269,
     VARNAME = 270,
     TYPENAME = 271,
     SIZE = 272,
     CAP = 273,
     ORD = 274,
     HIGH = 275,
     ABS = 276,
     MIN_FUNC = 277,
     MAX_FUNC = 278,
     FLOAT_FUNC = 279,
     VAL = 280,
     CHR = 281,
     ODD = 282,
     TRUNC = 283,
     INC = 284,
     DEC = 285,
     INCL = 286,
     EXCL = 287,
     COLONCOLON = 288,
     INTERNAL_VAR = 289,
     ABOVE_COMMA = 290,
     ASSIGN = 291,
     IN = 292,
     NOTEQUAL = 293,
     GEQ = 294,
     LEQ = 295,
     OROR = 296,
     LOGICAL_AND = 297,
     MOD = 298,
     DIV = 299,
     UNARY = 300,
     DOT = 301,
     NOT = 302,
     QID = 303
   };
#endif
#define INT 258
#define HEX 259
#define ERROR 260
#define UINT 261
#define M2_TRUE 262
#define M2_FALSE 263
#define CHAR 264
#define FLOAT 265
#define STRING 266
#define NAME 267
#define BLOCKNAME 268
#define IDENT 269
#define VARNAME 270
#define TYPENAME 271
#define SIZE 272
#define CAP 273
#define ORD 274
#define HIGH 275
#define ABS 276
#define MIN_FUNC 277
#define MAX_FUNC 278
#define FLOAT_FUNC 279
#define VAL 280
#define CHR 281
#define ODD 282
#define TRUNC 283
#define INC 284
#define DEC 285
#define INCL 286
#define EXCL 287
#define COLONCOLON 288
#define INTERNAL_VAR 289
#define ABOVE_COMMA 290
#define ASSIGN 291
#define IN 292
#define NOTEQUAL 293
#define GEQ 294
#define LEQ 295
#define OROR 296
#define LOGICAL_AND 297
#define MOD 298
#define DIV 299
#define UNARY 300
#define DOT 301
#define NOT 302
#define QID 303




/* Copy the first part of user declarations.  */
#line 41 "m2-exp.y"


#include "defs.h"
#include "gdb_string.h"
#include "expression.h"
#include "language.h"
#include "value.h"
#include "parser-defs.h"
#include "m2-lang.h"
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

#define	yymaxdepth m2_maxdepth
#define	yyparse	m2_parse
#define	yylex	m2_lex
#define	yyerror	m2_error
#define	yylval	m2_lval
#define	yychar	m2_char
#define	yydebug	m2_debug
#define	yypact	m2_pact
#define	yyr1	m2_r1
#define	yyr2	m2_r2
#define	yydef	m2_def
#define	yychk	m2_chk
#define	yypgo	m2_pgo
#define	yyact	m2_act
#define	yyexca	m2_exca
#define	yyerrflag m2_errflag
#define	yynerrs	m2_nerrs
#define	yyps	m2_ps
#define	yypv	m2_pv
#define	yys	m2_s
#define	yy_yys	m2_yys
#define	yystate	m2_state
#define	yytmp	m2_tmp
#define	yyv	m2_v
#define	yy_yyv	m2_yyv
#define	yyval	m2_val
#define	yylloc	m2_lloc
#define	yyreds	m2_reds		/* With YYDEBUG defined */
#define	yytoks	m2_toks		/* With YYDEBUG defined */
#define yyname	m2_name		/* With YYDEBUG defined */
#define yyrule	m2_rule		/* With YYDEBUG defined */
#define yylhs	m2_yylhs
#define yylen	m2_yylen
#define yydefred m2_yydefred
#define yydgoto	m2_yydgoto
#define yysindex m2_yysindex
#define yyrindex m2_yyrindex
#define yygindex m2_yygindex
#define yytable	 m2_yytable
#define yycheck	 m2_yycheck

#ifndef YYDEBUG
#define	YYDEBUG 1		/* Default to yydebug support */
#endif

#define YYFPRINTF parser_fprintf

int yyparse (void);

static int yylex (void);

void yyerror (char *);

#if 0
static char *make_qualname (char *, char *);
#endif

static int parse_number (int);

/* The sign of the number being parsed. */
static int number_sign = 1;

/* The block that the module specified by the qualifer on an identifer is
   contained in, */
#if 0
static struct block *modblock=0;
#endif



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 137 "m2-exp.y"
typedef union YYSTYPE {
    LONGEST lval;
    ULONGEST ulval;
    DOUBLEST dval;
    struct symbol *sym;
    struct type *tval;
    struct stoken sval;
    int voidval;
    struct block *bval;
    enum exp_opcode opcode;
    struct internalvar *ivar;

    struct type **tvec;
    int *ivec;
  } YYSTYPE;
/* Line 191 of yacc.c.  */
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 214 of yacc.c.  */

#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or xmalloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC xmalloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE))				\
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  67
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   848

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  68
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  15
/* YYNRULES -- Number of rules. */
#define YYNRULES  80
/* YYNRULES -- Number of states. */
#define YYNSTATES  181

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   303

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,    41,     2,     2,    47,     2,
      59,    64,    52,    50,    35,    51,     2,    53,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      38,    40,    39,     2,    49,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    58,     2,    67,    57,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    65,     2,    66,    61,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      36,    37,    42,    43,    44,    45,    46,    48,    54,    55,
      56,    60,    62,    63
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short yyprhs[] =
{
       0,     0,     3,     5,     7,     9,    12,    13,    17,    20,
      23,    25,    27,    32,    37,    42,    47,    52,    57,    62,
      69,    74,    79,    84,    87,    92,    99,   104,   111,   115,
     117,   121,   128,   135,   139,   144,   145,   151,   152,   158,
     159,   161,   165,   167,   171,   176,   181,   185,   189,   193,
     197,   201,   205,   209,   213,   217,   221,   225,   229,   233,
     237,   241,   245,   249,   253,   255,   257,   259,   261,   263,
     265,   267,   272,   274,   276,   278,   282,   284,   286,   290,
     292
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      69,     0,    -1,    71,    -1,    70,    -1,    82,    -1,    71,
      57,    -1,    -1,    51,    72,    71,    -1,    50,    71,    -1,
      73,    71,    -1,    62,    -1,    61,    -1,    18,    59,    71,
      64,    -1,    19,    59,    71,    64,    -1,    21,    59,    71,
      64,    -1,    20,    59,    71,    64,    -1,    22,    59,    82,
      64,    -1,    23,    59,    82,    64,    -1,    24,    59,    71,
      64,    -1,    25,    59,    82,    35,    71,    64,    -1,    26,
      59,    71,    64,    -1,    27,    59,    71,    64,    -1,    28,
      59,    71,    64,    -1,    17,    71,    -1,    29,    59,    71,
      64,    -1,    29,    59,    71,    35,    71,    64,    -1,    30,
      59,    71,    64,    -1,    30,    59,    71,    35,    71,    64,
      -1,    71,    60,    12,    -1,    74,    -1,    71,    42,    74,
      -1,    31,    59,    71,    35,    71,    64,    -1,    32,    59,
      71,    35,    71,    64,    -1,    65,    77,    66,    -1,    82,
      65,    77,    66,    -1,    -1,    71,    58,    75,    78,    67,
      -1,    -1,    71,    59,    76,    77,    64,    -1,    -1,    71,
      -1,    77,    35,    71,    -1,    71,    -1,    78,    35,    71,
      -1,    65,    82,    66,    71,    -1,    82,    59,    71,    64,
      -1,    59,    71,    64,    -1,    71,    49,    71,    -1,    71,
      52,    71,    -1,    71,    53,    71,    -1,    71,    55,    71,
      -1,    71,    54,    71,    -1,    71,    50,    71,    -1,    71,
      51,    71,    -1,    71,    40,    71,    -1,    71,    43,    71,
      -1,    71,    41,    71,    -1,    71,    45,    71,    -1,    71,
      44,    71,    -1,    71,    38,    71,    -1,    71,    39,    71,
      -1,    71,    48,    71,    -1,    71,    46,    71,    -1,    71,
      37,    71,    -1,     7,    -1,     8,    -1,     3,    -1,     6,
      -1,     9,    -1,    10,    -1,    81,    -1,    17,    59,    82,
      64,    -1,    11,    -1,    80,    -1,    13,    -1,    79,    33,
      13,    -1,    80,    -1,    34,    -1,    79,    33,    12,    -1,
      12,    -1,    16,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   205,   205,   206,   209,   218,   223,   222,   229,   233,
     237,   238,   241,   245,   249,   253,   257,   263,   269,   273,
     279,   283,   287,   291,   296,   300,   306,   310,   316,   322,
     325,   329,   333,   337,   339,   349,   345,   359,   356,   366,
     369,   373,   378,   383,   388,   394,   400,   408,   412,   416,
     420,   424,   428,   432,   436,   440,   442,   446,   450,   454,
     458,   462,   466,   470,   477,   483,   489,   496,   505,   513,
     520,   523,   530,   537,   541,   550,   562,   570,   574,   590,
     641
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "INT", "HEX", "ERROR", "UINT", "M2_TRUE", 
  "M2_FALSE", "CHAR", "FLOAT", "STRING", "NAME", "BLOCKNAME", "IDENT", 
  "VARNAME", "TYPENAME", "SIZE", "CAP", "ORD", "HIGH", "ABS", "MIN_FUNC", 
  "MAX_FUNC", "FLOAT_FUNC", "VAL", "CHR", "ODD", "TRUNC", "INC", "DEC", 
  "INCL", "EXCL", "COLONCOLON", "INTERNAL_VAR", "','", "ABOVE_COMMA", 
  "ASSIGN", "'<'", "'>'", "'='", "'#'", "IN", "NOTEQUAL", "GEQ", "LEQ", 
  "OROR", "'&'", "LOGICAL_AND", "'@'", "'+'", "'-'", "'*'", "'/'", "MOD", 
  "DIV", "UNARY", "'^'", "'['", "'('", "DOT", "'~'", "NOT", "QID", "')'", 
  "'{'", "'}'", "']'", "$accept", "start", "type_exp", "exp", "@1", 
  "not_exp", "set", "@2", "@3", "arglist", "non_empty_arglist", "block", 
  "fblock", "variable", "type", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,    44,   290,   291,    60,    62,
      61,    35,   292,   293,   294,   295,   296,    38,   297,    64,
      43,    45,    42,    47,   298,   299,   300,    94,    91,    40,
     301,   126,   302,   303,    41,   123,   125,    93
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    68,    69,    69,    70,    71,    72,    71,    71,    71,
      73,    73,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    74,    74,    75,    71,    76,    71,    77,
      77,    77,    78,    78,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    79,    80,    80,    81,    81,    81,    81,
      82
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     1,     2,     0,     3,     2,     2,
       1,     1,     4,     4,     4,     4,     4,     4,     4,     6,
       4,     4,     4,     2,     4,     6,     4,     6,     3,     1,
       3,     6,     6,     3,     4,     0,     5,     0,     5,     0,
       1,     3,     1,     3,     4,     4,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     1,     1,     1,     1,     1,     1,
       1,     4,     1,     1,     1,     3,     1,     1,     3,     1,
       1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       0,    66,    67,    64,    65,    68,    69,    72,    79,    74,
      80,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    77,     0,     6,
       0,    11,    10,    39,     0,     3,     2,     0,    29,     0,
      76,    70,     4,     0,    23,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     8,     0,     0,    40,     0,     0,     1,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     5,    35,    37,     0,
       9,     0,     0,    39,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       7,    46,     0,    33,     0,    63,    59,    60,    54,    56,
      39,    30,     0,    55,    58,    57,    62,    61,    47,    52,
      53,    48,    49,    51,    50,     0,    39,    28,    78,    75,
       0,     0,    71,    12,    13,    15,    14,    16,    17,    18,
       0,    20,    21,    22,     0,    24,     0,    26,     0,     0,
      41,    44,    42,     0,     0,    45,    34,     0,     0,     0,
       0,     0,     0,    36,    38,    19,    25,    27,    31,    32,
      43
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short yydefgoto[] =
{
      -1,    34,    35,    64,    62,    37,    38,   135,   136,    65,
     163,    39,    40,    41,    45
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -92
static const short yypact[] =
{
     157,   -92,   -92,   -92,   -92,   -92,   -92,   -92,   -92,   -92,
     -92,   217,   -53,   -27,   -18,   -17,    -8,     2,     8,    14,
      28,    29,    30,    31,    32,    34,    35,   -92,   157,   -92,
     157,   -92,   -92,   157,    44,   -92,   744,   157,   -92,    62,
      64,   -92,   -34,   157,     6,   -34,   157,   157,   157,   157,
      13,    13,   157,    13,   157,   157,   157,   157,   157,   157,
     157,     6,   157,    79,   744,   -30,   -39,   -92,   157,   157,
     157,   157,   157,   -15,   157,   157,   157,   157,   157,   157,
     157,   157,   157,   157,   157,   157,   -92,   -92,   -92,    86,
       6,    -4,   157,   157,   -25,   302,   330,   358,   386,    36,
      37,   414,    67,   442,   470,   498,   246,   274,   694,   720,
       6,   -92,   157,   -92,   157,   768,   -36,   -36,   -36,   -36,
     157,   -92,    40,   -36,   -36,   -36,   144,   203,   779,   788,
     788,     6,     6,     6,     6,   157,   157,   -92,   -92,   -92,
     526,   -28,   -92,   -92,   -92,   -92,   -92,   -92,   -92,   -92,
     157,   -92,   -92,   -92,   157,   -92,   157,   -92,   157,   157,
     744,     6,   744,   -32,   -31,   -92,   -92,   554,   582,   610,
     638,   666,   157,   -92,   -92,   -92,   -92,   -92,   -92,   -92,
     744
};

/* YYPGOTO[NTERM-NUM].  */
static const yysigned_char yypgoto[] =
{
     -92,   -92,   -92,     0,   -92,   -92,    26,   -92,   -92,   -91,
     -92,   -92,   -92,   -92,    53
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -74
static const short yytable[] =
{
      36,    10,   141,   172,   112,   112,    46,   112,   138,   139,
      77,    44,    78,    79,    80,    81,    82,    83,    84,    85,
      92,    86,    87,    88,    89,    92,    93,   114,    61,    10,
      63,    93,    47,   174,    92,   173,   113,    90,   166,   142,
      93,    48,    49,    63,    67,   164,    95,    96,    97,    98,
     120,    50,   101,    42,   103,   104,   105,   106,   107,   108,
     109,    51,   110,    86,    87,    88,    89,    52,   115,   116,
     117,   118,   119,    53,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,    66,    54,    55,    56,
      57,    58,   140,    59,    60,    91,    94,   -73,   137,   121,
     147,   148,   150,    99,   100,    93,   102,     0,     0,     0,
       0,     0,   160,     0,   161,     0,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,   122,    78,    79,    80,
      81,    82,    83,    84,    85,   162,    86,    87,    88,    89,
       0,     0,     0,   111,     0,     0,     0,     0,     0,     0,
     167,     0,     0,     0,   168,     0,   169,     0,   170,   171,
       1,     0,     0,     2,     3,     4,     5,     6,     7,     8,
       9,     0,   180,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
       0,    27,    78,    79,    80,    81,    82,    83,    84,    85,
       0,    86,    87,    88,    89,     0,     0,    28,    29,     0,
       0,     0,     0,     0,     0,     0,    30,     0,    31,    32,
       1,     0,    33,     2,     3,     4,     5,     6,     7,     8,
       9,     0,     0,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
       0,    27,    79,    80,    81,    82,    83,    84,    85,     0,
      86,    87,    88,    89,     0,     0,     0,    28,    29,     0,
       0,     0,     0,     0,     0,     0,    43,     0,    31,    32,
       0,   154,    33,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,     0,    78,    79,    80,    81,    82,    83,
      84,    85,     0,    86,    87,    88,    89,     0,     0,   156,
     155,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,     0,    78,    79,    80,    81,    82,    83,    84,    85,
       0,    86,    87,    88,    89,     0,     0,     0,   157,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,     0,
      78,    79,    80,    81,    82,    83,    84,    85,     0,    86,
      87,    88,    89,     0,     0,     0,   143,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,     0,    78,    79,
      80,    81,    82,    83,    84,    85,     0,    86,    87,    88,
      89,     0,     0,     0,   144,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,     0,    78,    79,    80,    81,
      82,    83,    84,    85,     0,    86,    87,    88,    89,     0,
       0,     0,   145,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,     0,    78,    79,    80,    81,    82,    83,
      84,    85,     0,    86,    87,    88,    89,     0,     0,     0,
     146,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,     0,    78,    79,    80,    81,    82,    83,    84,    85,
       0,    86,    87,    88,    89,     0,     0,     0,   149,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,     0,
      78,    79,    80,    81,    82,    83,    84,    85,     0,    86,
      87,    88,    89,     0,     0,     0,   151,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,     0,    78,    79,
      80,    81,    82,    83,    84,    85,     0,    86,    87,    88,
      89,     0,     0,     0,   152,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,     0,    78,    79,    80,    81,
      82,    83,    84,    85,     0,    86,    87,    88,    89,     0,
       0,     0,   153,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,     0,    78,    79,    80,    81,    82,    83,
      84,    85,     0,    86,    87,    88,    89,     0,     0,     0,
     165,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,     0,    78,    79,    80,    81,    82,    83,    84,    85,
       0,    86,    87,    88,    89,     0,     0,     0,   175,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,     0,
      78,    79,    80,    81,    82,    83,    84,    85,     0,    86,
      87,    88,    89,     0,     0,     0,   176,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,     0,    78,    79,
      80,    81,    82,    83,    84,    85,     0,    86,    87,    88,
      89,     0,     0,     0,   177,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,     0,    78,    79,    80,    81,
      82,    83,    84,    85,     0,    86,    87,    88,    89,     0,
       0,     0,   178,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,     0,    78,    79,    80,    81,    82,    83,
      84,    85,     0,    86,    87,    88,    89,     0,     0,   158,
     179,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,     0,    78,    79,    80,    81,    82,    83,    84,    85,
       0,    86,    87,    88,    89,   159,     0,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,     0,    78,    79,
      80,    81,    82,    83,    84,    85,     0,    86,    87,    88,
      89,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,     0,    78,    79,    80,    81,    82,    83,    84,    85,
       0,    86,    87,    88,    89,   -74,    69,    70,    71,    72,
      73,    74,    75,    76,    77,     0,    78,    79,    80,    81,
      82,    83,    84,    85,     0,    86,    87,    88,    89,    80,
      81,    82,    83,    84,    85,     0,    86,    87,    88,    89,
      82,    83,    84,    85,     0,    86,    87,    88,    89
};

static const short yycheck[] =
{
       0,    16,    93,    35,    35,    35,    59,    35,    12,    13,
      46,    11,    48,    49,    50,    51,    52,    53,    54,    55,
      59,    57,    58,    59,    60,    59,    65,    66,    28,    16,
      30,    65,    59,    64,    59,    67,    66,    37,    66,    64,
      65,    59,    59,    43,     0,   136,    46,    47,    48,    49,
      65,    59,    52,     0,    54,    55,    56,    57,    58,    59,
      60,    59,    62,    57,    58,    59,    60,    59,    68,    69,
      70,    71,    72,    59,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    33,    59,    59,    59,
      59,    59,    92,    59,    59,    33,    43,    33,    12,    73,
      64,    64,    35,    50,    51,    65,    53,    -1,    -1,    -1,
      -1,    -1,   112,    -1,   114,    -1,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    73,    48,    49,    50,
      51,    52,    53,    54,    55,   135,    57,    58,    59,    60,
      -1,    -1,    -1,    64,    -1,    -1,    -1,    -1,    -1,    -1,
     150,    -1,    -1,    -1,   154,    -1,   156,    -1,   158,   159,
       3,    -1,    -1,     6,     7,     8,     9,    10,    11,    12,
      13,    -1,   172,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      -1,    34,    48,    49,    50,    51,    52,    53,    54,    55,
      -1,    57,    58,    59,    60,    -1,    -1,    50,    51,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    59,    -1,    61,    62,
       3,    -1,    65,     6,     7,     8,     9,    10,    11,    12,
      13,    -1,    -1,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      -1,    34,    49,    50,    51,    52,    53,    54,    55,    -1,
      57,    58,    59,    60,    -1,    -1,    -1,    50,    51,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    59,    -1,    61,    62,
      -1,    35,    65,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    -1,    57,    58,    59,    60,    -1,    -1,    35,
      64,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      -1,    57,    58,    59,    60,    -1,    -1,    -1,    64,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    50,    51,    52,    53,    54,    55,    -1,    57,
      58,    59,    60,    -1,    -1,    -1,    64,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    -1,    57,    58,    59,
      60,    -1,    -1,    -1,    64,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    50,    51,
      52,    53,    54,    55,    -1,    57,    58,    59,    60,    -1,
      -1,    -1,    64,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    -1,    57,    58,    59,    60,    -1,    -1,    -1,
      64,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      -1,    57,    58,    59,    60,    -1,    -1,    -1,    64,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    50,    51,    52,    53,    54,    55,    -1,    57,
      58,    59,    60,    -1,    -1,    -1,    64,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    -1,    57,    58,    59,
      60,    -1,    -1,    -1,    64,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    50,    51,
      52,    53,    54,    55,    -1,    57,    58,    59,    60,    -1,
      -1,    -1,    64,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    -1,    57,    58,    59,    60,    -1,    -1,    -1,
      64,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      -1,    57,    58,    59,    60,    -1,    -1,    -1,    64,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    49,    50,    51,    52,    53,    54,    55,    -1,    57,
      58,    59,    60,    -1,    -1,    -1,    64,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    -1,    57,    58,    59,
      60,    -1,    -1,    -1,    64,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    50,    51,
      52,    53,    54,    55,    -1,    57,    58,    59,    60,    -1,
      -1,    -1,    64,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    49,    50,    51,    52,    53,
      54,    55,    -1,    57,    58,    59,    60,    -1,    -1,    35,
      64,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      -1,    57,    58,    59,    60,    35,    -1,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    -1,    48,    49,
      50,    51,    52,    53,    54,    55,    -1,    57,    58,    59,
      60,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    49,    50,    51,    52,    53,    54,    55,
      -1,    57,    58,    59,    60,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    49,    50,    51,
      52,    53,    54,    55,    -1,    57,    58,    59,    60,    50,
      51,    52,    53,    54,    55,    -1,    57,    58,    59,    60,
      52,    53,    54,    55,    -1,    57,    58,    59,    60
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     3,     6,     7,     8,     9,    10,    11,    12,    13,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    34,    50,    51,
      59,    61,    62,    65,    69,    70,    71,    73,    74,    79,
      80,    81,    82,    59,    71,    82,    59,    59,    59,    59,
      59,    59,    59,    59,    59,    59,    59,    59,    59,    59,
      59,    71,    72,    71,    71,    77,    82,     0,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    48,    49,
      50,    51,    52,    53,    54,    55,    57,    58,    59,    60,
      71,    33,    59,    65,    82,    71,    71,    71,    71,    82,
      82,    71,    82,    71,    71,    71,    71,    71,    71,    71,
      71,    64,    35,    66,    66,    71,    71,    71,    71,    71,
      65,    74,    82,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    71,    71,    71,    75,    76,    12,    12,    13,
      71,    77,    64,    64,    64,    64,    64,    64,    64,    64,
      35,    64,    64,    64,    35,    64,    35,    64,    35,    35,
      71,    71,    71,    78,    77,    64,    66,    71,    71,    71,
      71,    71,    35,    67,    64,    64,    64,    64,    64,    64,
      71
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrlab1

/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
#endif

/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Token, Value);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  if (yytype < YYNTOKENS)
    {
      YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yytype, yyvaluep)
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  switch (yytype)
    {

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to xreallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;



#define YYPOPSTACK   (yyvsp--, yyssp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to xreallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %s, ", yytname[yytoken]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;


  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 4:
#line 210 "m2-exp.y"
    { write_exp_elt_opcode(OP_TYPE);
		  write_exp_elt_type(yyvsp[0].tval);
		  write_exp_elt_opcode(OP_TYPE);
		}
    break;

  case 5:
#line 219 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_IND); }
    break;

  case 6:
#line 223 "m2-exp.y"
    { number_sign = -1; }
    break;

  case 7:
#line 225 "m2-exp.y"
    { number_sign = 1;
			  write_exp_elt_opcode (UNOP_NEG); }
    break;

  case 8:
#line 230 "m2-exp.y"
    { write_exp_elt_opcode(UNOP_PLUS); }
    break;

  case 9:
#line 234 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_LOGICAL_NOT); }
    break;

  case 12:
#line 242 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_CAP); }
    break;

  case 13:
#line 246 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_ORD); }
    break;

  case 14:
#line 250 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_ABS); }
    break;

  case 15:
#line 254 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_HIGH); }
    break;

  case 16:
#line 258 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_MIN);
			  write_exp_elt_type (yyvsp[-1].tval);
			  write_exp_elt_opcode (UNOP_MIN); }
    break;

  case 17:
#line 264 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_MAX);
			  write_exp_elt_type (yyvsp[-1].tval);
			  write_exp_elt_opcode (UNOP_MIN); }
    break;

  case 18:
#line 270 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_FLOAT); }
    break;

  case 19:
#line 274 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_VAL);
			  write_exp_elt_type (yyvsp[-3].tval);
			  write_exp_elt_opcode (BINOP_VAL); }
    break;

  case 20:
#line 280 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_CHR); }
    break;

  case 21:
#line 284 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_ODD); }
    break;

  case 22:
#line 288 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_TRUNC); }
    break;

  case 23:
#line 292 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_SIZEOF); }
    break;

  case 24:
#line 297 "m2-exp.y"
    { write_exp_elt_opcode(UNOP_PREINCREMENT); }
    break;

  case 25:
#line 301 "m2-exp.y"
    { write_exp_elt_opcode(BINOP_ASSIGN_MODIFY);
			  write_exp_elt_opcode(BINOP_ADD);
			  write_exp_elt_opcode(BINOP_ASSIGN_MODIFY); }
    break;

  case 26:
#line 307 "m2-exp.y"
    { write_exp_elt_opcode(UNOP_PREDECREMENT);}
    break;

  case 27:
#line 311 "m2-exp.y"
    { write_exp_elt_opcode(BINOP_ASSIGN_MODIFY);
			  write_exp_elt_opcode(BINOP_SUB);
			  write_exp_elt_opcode(BINOP_ASSIGN_MODIFY); }
    break;

  case 28:
#line 317 "m2-exp.y"
    { write_exp_elt_opcode (STRUCTOP_STRUCT);
			  write_exp_string (yyvsp[0].sval);
			  write_exp_elt_opcode (STRUCTOP_STRUCT); }
    break;

  case 30:
#line 326 "m2-exp.y"
    { error("Sets are not implemented.");}
    break;

  case 31:
#line 330 "m2-exp.y"
    { error("Sets are not implemented.");}
    break;

  case 32:
#line 334 "m2-exp.y"
    { error("Sets are not implemented.");}
    break;

  case 33:
#line 338 "m2-exp.y"
    { error("Sets are not implemented.");}
    break;

  case 34:
#line 340 "m2-exp.y"
    { error("Sets are not implemented.");}
    break;

  case 35:
#line 349 "m2-exp.y"
    { start_arglist(); }
    break;

  case 36:
#line 351 "m2-exp.y"
    { write_exp_elt_opcode (MULTI_SUBSCRIPT);
			  write_exp_elt_longcst ((LONGEST) end_arglist());
			  write_exp_elt_opcode (MULTI_SUBSCRIPT); }
    break;

  case 37:
#line 359 "m2-exp.y"
    { start_arglist (); }
    break;

  case 38:
#line 361 "m2-exp.y"
    { write_exp_elt_opcode (OP_FUNCALL);
			  write_exp_elt_longcst ((LONGEST) end_arglist ());
			  write_exp_elt_opcode (OP_FUNCALL); }
    break;

  case 40:
#line 370 "m2-exp.y"
    { arglist_len = 1; }
    break;

  case 41:
#line 374 "m2-exp.y"
    { arglist_len++; }
    break;

  case 42:
#line 379 "m2-exp.y"
    { arglist_len = 1; }
    break;

  case 43:
#line 384 "m2-exp.y"
    { arglist_len++; }
    break;

  case 44:
#line 389 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_MEMVAL);
			  write_exp_elt_type (yyvsp[-2].tval);
			  write_exp_elt_opcode (UNOP_MEMVAL); }
    break;

  case 45:
#line 395 "m2-exp.y"
    { write_exp_elt_opcode (UNOP_CAST);
			  write_exp_elt_type (yyvsp[-3].tval);
			  write_exp_elt_opcode (UNOP_CAST); }
    break;

  case 46:
#line 401 "m2-exp.y"
    { }
    break;

  case 47:
#line 409 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_REPEAT); }
    break;

  case 48:
#line 413 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_MUL); }
    break;

  case 49:
#line 417 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_DIV); }
    break;

  case 50:
#line 421 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_INTDIV); }
    break;

  case 51:
#line 425 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_REM); }
    break;

  case 52:
#line 429 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_ADD); }
    break;

  case 53:
#line 433 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_SUB); }
    break;

  case 54:
#line 437 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_EQUAL); }
    break;

  case 55:
#line 441 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_NOTEQUAL); }
    break;

  case 56:
#line 443 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_NOTEQUAL); }
    break;

  case 57:
#line 447 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_LEQ); }
    break;

  case 58:
#line 451 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_GEQ); }
    break;

  case 59:
#line 455 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_LESS); }
    break;

  case 60:
#line 459 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_GTR); }
    break;

  case 61:
#line 463 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_LOGICAL_AND); }
    break;

  case 62:
#line 467 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_LOGICAL_OR); }
    break;

  case 63:
#line 471 "m2-exp.y"
    { write_exp_elt_opcode (BINOP_ASSIGN); }
    break;

  case 64:
#line 478 "m2-exp.y"
    { write_exp_elt_opcode (OP_BOOL);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].ulval);
			  write_exp_elt_opcode (OP_BOOL); }
    break;

  case 65:
#line 484 "m2-exp.y"
    { write_exp_elt_opcode (OP_BOOL);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].ulval);
			  write_exp_elt_opcode (OP_BOOL); }
    break;

  case 66:
#line 490 "m2-exp.y"
    { write_exp_elt_opcode (OP_LONG);
			  write_exp_elt_type (builtin_type_m2_int);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].lval);
			  write_exp_elt_opcode (OP_LONG); }
    break;

  case 67:
#line 497 "m2-exp.y"
    {
			  write_exp_elt_opcode (OP_LONG);
			  write_exp_elt_type (builtin_type_m2_card);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].ulval);
			  write_exp_elt_opcode (OP_LONG);
			}
    break;

  case 68:
#line 506 "m2-exp.y"
    { write_exp_elt_opcode (OP_LONG);
			  write_exp_elt_type (builtin_type_m2_char);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].ulval);
			  write_exp_elt_opcode (OP_LONG); }
    break;

  case 69:
#line 514 "m2-exp.y"
    { write_exp_elt_opcode (OP_DOUBLE);
			  write_exp_elt_type (builtin_type_m2_real);
			  write_exp_elt_dblcst (yyvsp[0].dval);
			  write_exp_elt_opcode (OP_DOUBLE); }
    break;

  case 71:
#line 524 "m2-exp.y"
    { write_exp_elt_opcode (OP_LONG);
			  write_exp_elt_type (builtin_type_int);
			  write_exp_elt_longcst ((LONGEST) TYPE_LENGTH (yyvsp[-1].tval));
			  write_exp_elt_opcode (OP_LONG); }
    break;

  case 72:
#line 531 "m2-exp.y"
    { write_exp_elt_opcode (OP_M2_STRING);
			  write_exp_string (yyvsp[0].sval);
			  write_exp_elt_opcode (OP_M2_STRING); }
    break;

  case 73:
#line 538 "m2-exp.y"
    { yyval.bval = SYMBOL_BLOCK_VALUE(yyvsp[0].sym); }
    break;

  case 74:
#line 542 "m2-exp.y"
    { struct symbol *sym
			    = lookup_symbol (copy_name (yyvsp[0].sval), expression_context_block,
					     VAR_DOMAIN, 0, NULL);
			  yyval.sym = sym;}
    break;

  case 75:
#line 551 "m2-exp.y"
    { struct symbol *tem
			    = lookup_symbol (copy_name (yyvsp[0].sval), yyvsp[-2].bval,
					     VAR_DOMAIN, 0, NULL);
			  if (!tem || SYMBOL_CLASS (tem) != LOC_BLOCK)
			    error ("No function \"%s\" in specified context.",
				   copy_name (yyvsp[0].sval));
			  yyval.sym = tem;
			}
    break;

  case 76:
#line 563 "m2-exp.y"
    { write_exp_elt_opcode(OP_VAR_VALUE);
			  write_exp_elt_block (NULL);
			  write_exp_elt_sym (yyvsp[0].sym);
			  write_exp_elt_opcode (OP_VAR_VALUE); }
    break;

  case 78:
#line 575 "m2-exp.y"
    { struct symbol *sym;
			  sym = lookup_symbol (copy_name (yyvsp[0].sval), yyvsp[-2].bval,
					       VAR_DOMAIN, 0, NULL);
			  if (sym == 0)
			    error ("No symbol \"%s\" in specified context.",
				   copy_name (yyvsp[0].sval));

			  write_exp_elt_opcode (OP_VAR_VALUE);
			  /* block_found is set by lookup_symbol.  */
			  write_exp_elt_block (block_found);
			  write_exp_elt_sym (sym);
			  write_exp_elt_opcode (OP_VAR_VALUE); }
    break;

  case 79:
#line 591 "m2-exp.y"
    { struct symbol *sym;
			  int is_a_field_of_this;

 			  sym = lookup_symbol (copy_name (yyvsp[0].sval),
					       expression_context_block,
					       VAR_DOMAIN,
					       &is_a_field_of_this,
					       NULL);
			  if (sym)
			    {
			      if (symbol_read_needs_frame (sym))
				{
				  if (innermost_block == 0 ||
				      contained_in (block_found, 
						    innermost_block))
				    innermost_block = block_found;
				}

			      write_exp_elt_opcode (OP_VAR_VALUE);
			      /* We want to use the selected frame, not
				 another more inner frame which happens to
				 be in the same block.  */
			      write_exp_elt_block (NULL);
			      write_exp_elt_sym (sym);
			      write_exp_elt_opcode (OP_VAR_VALUE);
			    }
			  else
			    {
			      struct minimal_symbol *msymbol;
			      char *arg = copy_name (yyvsp[0].sval);

			      msymbol =
				lookup_minimal_symbol (arg, NULL, NULL);
			      if (msymbol != NULL)
				{
				  write_exp_msymbol
				    (msymbol,
				     lookup_function_type (builtin_type_int),
				     builtin_type_int);
				}
			      else if (!have_full_symbols () && !have_partial_symbols ())
				error ("No symbol table is loaded.  Use the \"symbol-file\" command.");
			      else
				error ("No symbol \"%s\" in current context.",
				       copy_name (yyvsp[0].sval));
			    }
			}
    break;

  case 80:
#line 642 "m2-exp.y"
    { yyval.tval = lookup_typename (copy_name (yyvsp[0].sval),
						expression_context_block, 0); }
    break;


    }

/* Line 991 of yacc.c.  */

  yyvsp -= yylen;
  yyssp -= yylen;


  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror ("syntax error");
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval);
      yychar = YYEMPTY;

    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab2;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
yyerrlab1:

  /* Suppress GCC warning that yyerrlab1 is unused when no action
     invokes YYERROR.  Doesn't work in C++ */
#ifndef __cplusplus
#if defined (__GNUC_MINOR__) && 2093 <= (__GNUC__ * 1000 + __GNUC_MINOR__)
  __attribute__ ((__unused__))
#endif
#endif


  goto yyerrlab2;


/*---------------------------------------------------------------.
| yyerrlab2 -- pop states until the error token can be shifted.  |
`---------------------------------------------------------------*/
yyerrlab2:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp);
      yyvsp--;
      yystate = *--yyssp;

      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;


  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 647 "m2-exp.y"


#if 0  /* FIXME! */
int
overflow(a,b)
   long a,b;
{
   return (MAX_OF_TYPE(builtin_type_m2_int) - b) < a;
}

int
uoverflow(a,b)
   unsigned long a,b;
{
   return (MAX_OF_TYPE(builtin_type_m2_card) - b) < a;
}
#endif /* FIXME */

/* Take care of parsing a number (anything that starts with a digit).
   Set yylval and return the token type; update lexptr.
   LEN is the number of characters in it.  */

/*** Needs some error checking for the float case ***/

static int
parse_number (olen)
     int olen;
{
  char *p = lexptr;
  LONGEST n = 0;
  LONGEST prevn = 0;
  int c,i,ischar=0;
  int base = input_radix;
  int len = olen;
  int unsigned_p = number_sign == 1 ? 1 : 0;

  if(p[len-1] == 'H')
  {
     base = 16;
     len--;
  }
  else if(p[len-1] == 'C' || p[len-1] == 'B')
  {
     base = 8;
     ischar = p[len-1] == 'C';
     len--;
  }

  /* Scan the number */
  for (c = 0; c < len; c++)
  {
    if (p[c] == '.' && base == 10)
      {
	/* It's a float since it contains a point.  */
	yylval.dval = atof (p);
	lexptr += len;
	return FLOAT;
      }
    if (p[c] == '.' && base != 10)
       error("Floating point numbers must be base 10.");
    if (base == 10 && (p[c] < '0' || p[c] > '9'))
       error("Invalid digit \'%c\' in number.",p[c]);
 }

  while (len-- > 0)
    {
      c = *p++;
      n *= base;
      if( base == 8 && (c == '8' || c == '9'))
	 error("Invalid digit \'%c\' in octal number.",c);
      if (c >= '0' && c <= '9')
	i = c - '0';
      else
	{
	  if (base == 16 && c >= 'A' && c <= 'F')
	    i = c - 'A' + 10;
	  else
	     return ERROR;
	}
      n+=i;
      if(i >= base)
	 return ERROR;
      if(!unsigned_p && number_sign == 1 && (prevn >= n))
	 unsigned_p=1;		/* Try something unsigned */
      /* Don't do the range check if n==i and i==0, since that special
	 case will give an overflow error. */
      if(RANGE_CHECK && n!=i && i)
      {
	 if((unsigned_p && (unsigned)prevn >= (unsigned)n) ||
	    ((!unsigned_p && number_sign==-1) && -prevn <= -n))
	    range_error("Overflow on numeric constant.");
      }
	 prevn=n;
    }

  lexptr = p;
  if(*p == 'B' || *p == 'C' || *p == 'H')
     lexptr++;			/* Advance past B,C or H */

  if (ischar)
  {
     yylval.ulval = n;
     return CHAR;
  }
  else if ( unsigned_p && number_sign == 1)
  {
     yylval.ulval = n;
     return UINT;
  }
  else if((unsigned_p && (n<0))) {
     range_error("Overflow on numeric constant -- number too large.");
     /* But, this can return if range_check == range_warn.  */
  }
  yylval.lval = n;
  return INT;
}


/* Some tokens */

static struct
{
   char name[2];
   int token;
} tokentab2[] =
{
    { {'<', '>'},    NOTEQUAL 	},
    { {':', '='},    ASSIGN	},
    { {'<', '='},    LEQ	},
    { {'>', '='},    GEQ	},
    { {':', ':'},    COLONCOLON },

};

/* Some specific keywords */

struct keyword {
   char keyw[10];
   int token;
};

static struct keyword keytab[] =
{
    {"OR" ,   OROR	 },
    {"IN",    IN         },/* Note space after IN */
    {"AND",   LOGICAL_AND},
    {"ABS",   ABS	 },
    {"CHR",   CHR	 },
    {"DEC",   DEC	 },
    {"NOT",   NOT	 },
    {"DIV",   DIV    	 },
    {"INC",   INC	 },
    {"MAX",   MAX_FUNC	 },
    {"MIN",   MIN_FUNC	 },
    {"MOD",   MOD	 },
    {"ODD",   ODD	 },
    {"CAP",   CAP	 },
    {"ORD",   ORD	 },
    {"VAL",   VAL	 },
    {"EXCL",  EXCL	 },
    {"HIGH",  HIGH       },
    {"INCL",  INCL	 },
    {"SIZE",  SIZE       },
    {"FLOAT", FLOAT_FUNC },
    {"TRUNC", TRUNC	 },
};


/* Read one token, getting characters through lexptr.  */

/* This is where we will check to make sure that the language and the operators used are
   compatible  */

static int
yylex ()
{
  int c;
  int namelen;
  int i;
  char *tokstart;
  char quote;

 retry:

  prev_lexptr = lexptr;

  tokstart = lexptr;


  /* See if it is a special token of length 2 */
  for( i = 0 ; i < (int) (sizeof tokentab2 / sizeof tokentab2[0]) ; i++)
     if(DEPRECATED_STREQN(tokentab2[i].name, tokstart, 2))
     {
	lexptr += 2;
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
      if (lexptr[1] >= '0' && lexptr[1] <= '9')
	break;			/* Falls into number code.  */
      else
      {
	 lexptr++;
	 return DOT;
      }

/* These are character tokens that appear as-is in the YACC grammar */
    case '+':
    case '-':
    case '*':
    case '/':
    case '^':
    case '<':
    case '>':
    case '[':
    case ']':
    case '=':
    case '{':
    case '}':
    case '#':
    case '@':
    case '~':
    case '&':
      lexptr++;
      return c;

    case '\'' :
    case '"':
      quote = c;
      for (namelen = 1; (c = tokstart[namelen]) != quote && c != '\0'; namelen++)
	if (c == '\\')
	  {
	    c = tokstart[++namelen];
	    if (c >= '0' && c <= '9')
	      {
		c = tokstart[++namelen];
		if (c >= '0' && c <= '9')
		  c = tokstart[++namelen];
	      }
	  }
      if(c != quote)
	 error("Unterminated string or character constant.");
      yylval.sval.ptr = tokstart + 1;
      yylval.sval.length = namelen - 1;
      lexptr += namelen + 1;

      if(namelen == 2)  	/* Single character */
      {
	   yylval.ulval = tokstart[1];
	   return CHAR;
      }
      else
	 return STRING;
    }

  /* Is it a number?  */
  /* Note:  We have already dealt with the case of the token '.'.
     See case '.' above.  */
  if ((c >= '0' && c <= '9'))
    {
      /* It's a number.  */
      int got_dot = 0, got_e = 0;
      char *p = tokstart;
      int toktype;

      for (++p ;; ++p)
	{
	  if (!got_e && (*p == 'e' || *p == 'E'))
	    got_dot = got_e = 1;
	  else if (!got_dot && *p == '.')
	    got_dot = 1;
	  else if (got_e && (p[-1] == 'e' || p[-1] == 'E')
		   && (*p == '-' || *p == '+'))
	    /* This is the sign of the exponent, not the end of the
	       number.  */
	    continue;
	  else if ((*p < '0' || *p > '9') &&
		   (*p < 'A' || *p > 'F') &&
		   (*p != 'H'))  /* Modula-2 hexadecimal number */
	    break;
	}
	toktype = parse_number (p - tokstart);
        if (toktype == ERROR)
	  {
	    char *err_copy = (char *) alloca (p - tokstart + 1);

	    memcpy (err_copy, tokstart, p - tokstart);
	    err_copy[p - tokstart] = 0;
	    error ("Invalid number \"%s\".", err_copy);
	  }
	lexptr = p;
	return toktype;
    }

  if (!(c == '_' || c == '$'
	|| (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
    /* We must have come across a bad character (e.g. ';').  */
    error ("Invalid character '%c' in expression.", c);

  /* It's a name.  See how long it is.  */
  namelen = 0;
  for (c = tokstart[namelen];
       (c == '_' || c == '$' || (c >= '0' && c <= '9')
	|| (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
       c = tokstart[++namelen])
    ;

  /* The token "if" terminates the expression and is NOT
     removed from the input stream.  */
  if (namelen == 2 && tokstart[0] == 'i' && tokstart[1] == 'f')
    {
      return 0;
    }

  lexptr += namelen;

  /*  Lookup special keywords */
  for(i = 0 ; i < (int) (sizeof(keytab) / sizeof(keytab[0])) ; i++)
     if(namelen == strlen(keytab[i].keyw) && DEPRECATED_STREQN(tokstart,keytab[i].keyw,namelen))
	   return keytab[i].token;

  yylval.sval.ptr = tokstart;
  yylval.sval.length = namelen;

  if (*tokstart == '$')
    {
      write_dollar_variable (yylval.sval);
      return INTERNAL_VAR;
    }

  /* Use token-type BLOCKNAME for symbols that happen to be defined as
     functions.  If this is not so, then ...
     Use token-type TYPENAME for symbols that happen to be defined
     currently as names of types; NAME for other symbols.
     The caller is not constrained to care about the distinction.  */
 {


    char *tmp = copy_name (yylval.sval);
    struct symbol *sym;

    if (lookup_partial_symtab (tmp))
      return BLOCKNAME;
    sym = lookup_symbol (tmp, expression_context_block,
			 VAR_DOMAIN, 0, NULL);
    if (sym && SYMBOL_CLASS (sym) == LOC_BLOCK)
      return BLOCKNAME;
    if (lookup_typename (copy_name (yylval.sval), expression_context_block, 1))
      return TYPENAME;

    if(sym)
    {
       switch(sym->aclass)
       {
       case LOC_STATIC:
       case LOC_REGISTER:
       case LOC_ARG:
       case LOC_REF_ARG:
       case LOC_REGPARM:
       case LOC_REGPARM_ADDR:
       case LOC_LOCAL:
       case LOC_LOCAL_ARG:
       case LOC_BASEREG:
       case LOC_BASEREG_ARG:
       case LOC_CONST:
       case LOC_CONST_BYTES:
       case LOC_OPTIMIZED_OUT:
       case LOC_COMPUTED:
       case LOC_COMPUTED_ARG:
	  return NAME;

       case LOC_TYPEDEF:
	  return TYPENAME;

       case LOC_BLOCK:
	  return BLOCKNAME;

       case LOC_UNDEF:
	  error("internal:  Undefined class in m2lex()");

       case LOC_LABEL:
       case LOC_UNRESOLVED:
	  error("internal:  Unforseen case in m2lex()");

       default:
	  error ("unhandled token in m2lex()");
	  break;
       }
    }
    else
    {
       /* Built-in BOOLEAN type.  This is sort of a hack. */
       if(DEPRECATED_STREQN(tokstart,"TRUE",4))
       {
	  yylval.ulval = 1;
	  return M2_TRUE;
       }
       else if(DEPRECATED_STREQN(tokstart,"FALSE",5))
       {
	  yylval.ulval = 0;
	  return M2_FALSE;
       }
    }

    /* Must be another type of name... */
    return NAME;
 }
}

#if 0		/* Unused */
static char *
make_qualname(mod,ident)
   char *mod, *ident;
{
   char *new = xmalloc(strlen(mod)+strlen(ident)+2);

   strcpy(new,mod);
   strcat(new,".");
   strcat(new,ident);
   return new;
}
#endif  /* 0 */

void
yyerror (msg)
     char *msg;
{
  if (prev_lexptr)
    lexptr = prev_lexptr;

  error ("A %s in expression, near `%s'.", (msg ? msg : "error"), lexptr);
}


