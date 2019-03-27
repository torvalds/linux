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
     NULL_PTR = 259,
     CHARLIT = 260,
     FLOAT = 261,
     TYPENAME = 262,
     BLOCKNAME = 263,
     STRING = 264,
     NAME = 265,
     DOT_ID = 266,
     OBJECT_RENAMING = 267,
     DOT_ALL = 268,
     LAST = 269,
     REGNAME = 270,
     INTERNAL_VARIABLE = 271,
     ASSIGN = 272,
     ELSE = 273,
     THEN = 274,
     XOR = 275,
     OR = 276,
     _AND_ = 277,
     DOTDOT = 278,
     IN = 279,
     GEQ = 280,
     LEQ = 281,
     NOTEQUAL = 282,
     UNARY = 283,
     REM = 284,
     MOD = 285,
     NOT = 286,
     ABS = 287,
     STARSTAR = 288,
     TICK_LENGTH = 289,
     TICK_LAST = 290,
     TICK_FIRST = 291,
     TICK_ADDRESS = 292,
     TICK_ACCESS = 293,
     TICK_MODULUS = 294,
     TICK_MIN = 295,
     TICK_MAX = 296,
     TICK_VAL = 297,
     TICK_TAG = 298,
     TICK_SIZE = 299,
     TICK_RANGE = 300,
     TICK_POS = 301,
     ARROW = 302,
     NEW = 303
   };
#endif
#define INT 258
#define NULL_PTR 259
#define CHARLIT 260
#define FLOAT 261
#define TYPENAME 262
#define BLOCKNAME 263
#define STRING 264
#define NAME 265
#define DOT_ID 266
#define OBJECT_RENAMING 267
#define DOT_ALL 268
#define LAST 269
#define REGNAME 270
#define INTERNAL_VARIABLE 271
#define ASSIGN 272
#define ELSE 273
#define THEN 274
#define XOR 275
#define OR 276
#define _AND_ 277
#define DOTDOT 278
#define IN 279
#define GEQ 280
#define LEQ 281
#define NOTEQUAL 282
#define UNARY 283
#define REM 284
#define MOD 285
#define NOT 286
#define ABS 287
#define STARSTAR 288
#define TICK_LENGTH 289
#define TICK_LAST 290
#define TICK_FIRST 291
#define TICK_ADDRESS 292
#define TICK_ACCESS 293
#define TICK_MODULUS 294
#define TICK_MIN 295
#define TICK_MAX 296
#define TICK_VAL 297
#define TICK_TAG 298
#define TICK_SIZE 299
#define TICK_RANGE 300
#define TICK_POS 301
#define ARROW 302
#define NEW 303




/* Copy the first part of user declarations.  */
#line 38 "ada-exp.y"


#include "defs.h"
#include <string.h>
#include <ctype.h>
#include "expression.h"
#include "value.h"
#include "parser-defs.h"
#include "language.h"
#include "ada-lang.h"
#include "bfd.h" /* Required by objfiles.h.  */
#include "symfile.h" /* Required by objfiles.h.  */
#include "objfiles.h" /* For have_full_symbols and have_partial_symbols */
#include "frame.h"
#include "block.h"

/* Remap normal yacc parser interface names (yyparse, yylex, yyerror, etc),
   as well as gratuitiously global symbol names, so we can have multiple
   yacc generated parsers in gdb.  These are only the variables
   produced by yacc.  If other parser generators (bison, byacc, etc) produce
   additional global names that conflict at link time, then those parser
   generators need to be fixed instead of adding those names to this list. */

/* NOTE: This is clumsy, especially since BISON and FLEX provide --prefix  
   options.  I presume we are maintaining it to accommodate systems
   without BISON?  (PNH) */

#define	yymaxdepth ada_maxdepth
#define	yyparse	_ada_parse	/* ada_parse calls this after  initialization */
#define	yylex	ada_lex
#define	yyerror	ada_error
#define	yylval	ada_lval
#define	yychar	ada_char
#define	yydebug	ada_debug
#define	yypact	ada_pact	
#define	yyr1	ada_r1			
#define	yyr2	ada_r2			
#define	yydef	ada_def		
#define	yychk	ada_chk		
#define	yypgo	ada_pgo		
#define	yyact	ada_act		
#define	yyexca	ada_exca
#define yyerrflag ada_errflag
#define yynerrs	ada_nerrs
#define	yyps	ada_ps
#define	yypv	ada_pv
#define	yys	ada_s
#define	yy_yys	ada_yys
#define	yystate	ada_state
#define	yytmp	ada_tmp
#define	yyv	ada_v
#define	yy_yyv	ada_yyv
#define	yyval	ada_val
#define	yylloc	ada_lloc
#define yyreds	ada_reds		/* With YYDEBUG defined */
#define yytoks	ada_toks		/* With YYDEBUG defined */
#define yyname	ada_name		/* With YYDEBUG defined */
#define yyrule	ada_rule		/* With YYDEBUG defined */

#ifndef YYDEBUG
#define	YYDEBUG	1		/* Default to yydebug support */
#endif

#define YYFPRINTF parser_fprintf

struct name_info {
  struct symbol* sym;
  struct minimal_symbol* msym;
  struct block* block;
  struct stoken stoken;
};

/* If expression is in the context of TYPE'(...), then TYPE, else
 * NULL. */
static struct type* type_qualifier;

int yyparse (void);

static int yylex (void);

void yyerror (char *);

static struct stoken string_to_operator (struct stoken);

static void write_attribute_call0 (enum ada_attribute);

static void write_attribute_call1 (enum ada_attribute, LONGEST);

static void write_attribute_calln (enum ada_attribute, int);

static void write_object_renaming (struct block*, struct symbol*);

static void write_var_from_name (struct block*, struct name_info);

static LONGEST
convert_char_literal (struct type*, LONGEST);


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
#line 137 "ada-exp.y"
typedef union YYSTYPE {
    LONGEST lval;
    struct {
      LONGEST val;
      struct type *type;
    } typed_val;
    struct {
      DOUBLEST dval;
      struct type *type;
    } typed_val_float;
    struct type *tval;
    struct stoken sval;
    struct name_info ssym;
    int voidval;
    struct block *bval;
    struct internalvar *ivar;

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
#define YYFINAL  44
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1067

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  68
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  15
/* YYNRULES -- Number of rules. */
#define YYNRULES  98
/* YYNRULES -- Number of states. */
#define YYNSTATES  184

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
       2,     2,     2,     2,     2,     2,     2,     2,    34,    63,
      57,    62,    36,    32,    64,    33,    56,    37,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    61,
      24,    23,    25,     2,    31,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    58,     2,    67,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    65,     2,    66,     2,     2,     2,     2,
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
      15,    16,    17,    18,    19,    20,    21,    22,    26,    27,
      28,    29,    30,    35,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    59,    60
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short yyprhs[] =
{
       0,     0,     3,     5,     7,     9,    13,    16,    19,    24,
      29,    30,    38,    39,    46,    50,    52,    54,    56,    58,
      60,    64,    67,    70,    73,    76,    77,    79,    83,    87,
      93,    98,   102,   106,   110,   114,   118,   122,   126,   130,
     134,   138,   142,   146,   152,   158,   162,   169,   176,   181,
     185,   189,   193,   197,   202,   206,   211,   215,   218,   221,
     225,   229,   233,   236,   239,   247,   255,   261,   265,   269,
     273,   279,   282,   283,   287,   289,   291,   292,   294,   296,
     298,   300,   302,   305,   307,   310,   312,   315,   317,   319,
     321,   323,   326,   328,   331,   334,   338,   341,   344
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      69,     0,    -1,    70,    -1,    82,    -1,    74,    -1,    70,
      61,    74,    -1,    71,    13,    -1,    71,    11,    -1,    71,
      57,    75,    62,    -1,    82,    57,    74,    62,    -1,    -1,
      82,    63,    73,    72,    57,    74,    62,    -1,    -1,    71,
      57,    74,    26,    74,    62,    -1,    57,    70,    62,    -1,
      79,    -1,    15,    -1,    16,    -1,    71,    -1,    14,    -1,
      74,    17,    74,    -1,    33,    74,    -1,    32,    74,    -1,
      40,    74,    -1,    41,    74,    -1,    -1,    74,    -1,    80,
      59,    74,    -1,    75,    64,    74,    -1,    75,    64,    80,
      59,    74,    -1,    65,    82,    66,    74,    -1,    74,    42,
      74,    -1,    74,    36,    74,    -1,    74,    37,    74,    -1,
      74,    38,    74,    -1,    74,    39,    74,    -1,    74,    31,
      74,    -1,    74,    32,    74,    -1,    74,    34,    74,    -1,
      74,    33,    74,    -1,    74,    23,    74,    -1,    74,    30,
      74,    -1,    74,    29,    74,    -1,    74,    27,    74,    26,
      74,    -1,    74,    27,    74,    54,    76,    -1,    74,    27,
       7,    -1,    74,    40,    27,    74,    26,    74,    -1,    74,
      40,    27,    74,    54,    76,    -1,    74,    40,    27,     7,
      -1,    74,    28,    74,    -1,    74,    24,    74,    -1,    74,
      25,    74,    -1,    74,    22,    74,    -1,    74,    22,    19,
      74,    -1,    74,    21,    74,    -1,    74,    21,    18,    74,
      -1,    74,    20,    74,    -1,    71,    47,    -1,    71,    46,
      -1,    71,    45,    76,    -1,    71,    44,    76,    -1,    71,
      43,    76,    -1,    71,    53,    -1,    71,    52,    -1,    78,
      49,    57,    74,    64,    74,    62,    -1,    78,    50,    57,
      74,    64,    74,    62,    -1,    78,    55,    57,    74,    62,
      -1,    77,    45,    76,    -1,    77,    44,    76,    -1,    77,
      43,    76,    -1,    77,    51,    57,    74,    62,    -1,    77,
      48,    -1,    -1,    57,     3,    62,    -1,     7,    -1,    77,
      -1,    -1,     3,    -1,     5,    -1,     6,    -1,     4,    -1,
       9,    -1,    60,     7,    -1,    10,    -1,    81,    10,    -1,
      12,    -1,    81,    12,    -1,    10,    -1,     7,    -1,    12,
      -1,     8,    -1,    81,     8,    -1,     7,    -1,    81,     7,
      -1,     7,    47,    -1,    81,     7,    47,    -1,    36,    74,
      -1,    34,    74,    -1,    74,    58,    74,    67,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   208,   208,   209,   215,   216,   221,   225,   232,   240,
     248,   248,   259,   263,   267,   270,   273,   280,   288,   291,
     298,   302,   306,   310,   314,   318,   321,   323,   325,   327,
     331,   341,   345,   349,   353,   357,   361,   365,   369,   373,
     377,   381,   385,   389,   393,   399,   406,   411,   419,   429,
     433,   437,   441,   445,   449,   453,   457,   461,   463,   469,
     471,   473,   475,   477,   479,   481,   483,   485,   487,   489,
     491,   493,   497,   499,   504,   511,   513,   519,   527,   539,
     547,   555,   582,   586,   587,   589,   590,   594,   595,   596,
     599,   601,   606,   607,   608,   610,   617,   619,   621
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "INT", "NULL_PTR", "CHARLIT", "FLOAT", 
  "TYPENAME", "BLOCKNAME", "STRING", "NAME", "DOT_ID", "OBJECT_RENAMING", 
  "DOT_ALL", "LAST", "REGNAME", "INTERNAL_VARIABLE", "ASSIGN", "ELSE", 
  "THEN", "XOR", "OR", "_AND_", "'='", "'<'", "'>'", "DOTDOT", "IN", 
  "GEQ", "LEQ", "NOTEQUAL", "'@'", "'+'", "'-'", "'&'", "UNARY", "'*'", 
  "'/'", "REM", "MOD", "NOT", "ABS", "STARSTAR", "TICK_LENGTH", 
  "TICK_LAST", "TICK_FIRST", "TICK_ADDRESS", "TICK_ACCESS", 
  "TICK_MODULUS", "TICK_MIN", "TICK_MAX", "TICK_VAL", "TICK_TAG", 
  "TICK_SIZE", "TICK_RANGE", "TICK_POS", "'.'", "'('", "'['", "ARROW", 
  "NEW", "';'", "')'", "'''", "','", "'{'", "'}'", "']'", "$accept", 
  "start", "exp1", "simple_exp", "@1", "save_qualifier", "exp", "arglist", 
  "tick_arglist", "type_prefix", "opt_type_prefix", "variable", 
  "any_name", "block", "type", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,    61,    60,    62,   278,   279,   280,   281,
     282,    64,    43,    45,    38,   283,    42,    47,   284,   285,
     286,   287,   288,   289,   290,   291,   292,   293,   294,   295,
     296,   297,   298,   299,   300,   301,    46,    40,    91,   302,
     303,    59,    41,    39,    44,   123,   125,    93
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    68,    69,    69,    70,    70,    71,    71,    71,    71,
      72,    71,    73,    71,    71,    71,    71,    71,    74,    71,
      74,    74,    74,    74,    74,    75,    75,    75,    75,    75,
      74,    74,    74,    74,    74,    74,    74,    74,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    74,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    71,    71,    71,
      71,    71,    71,    71,    71,    71,    71,    71,    71,    71,
      71,    71,    76,    76,    77,    78,    78,    74,    74,    74,
      74,    74,    74,    79,    79,    79,    79,    80,    80,    80,
      81,    81,    82,    82,    82,    82,    74,    74,    74
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     1,     3,     2,     2,     4,     4,
       0,     7,     0,     6,     3,     1,     1,     1,     1,     1,
       3,     2,     2,     2,     2,     0,     1,     3,     3,     5,
       4,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     5,     5,     3,     6,     6,     4,     3,
       3,     3,     3,     4,     3,     4,     3,     2,     2,     3,
       3,     3,     2,     2,     7,     7,     5,     3,     3,     3,
       5,     2,     0,     3,     1,     1,     0,     1,     1,     1,
       1,     1,     2,     1,     2,     1,     2,     1,     1,     1,
       1,     2,     1,     2,     2,     3,     2,     2,     4
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
      76,    77,    80,    78,    79,    74,    90,    81,    83,    85,
      19,    16,    17,    76,    76,    76,    76,    76,    76,    76,
       0,     0,     0,     2,    18,     4,    75,     0,    15,     0,
       3,    94,    22,     0,    21,    97,    96,    23,    24,     0,
      82,    92,     0,     0,     1,    76,     7,     6,    72,    72,
      72,    58,    57,    63,    62,    76,    76,    76,    76,    76,
      76,    76,    76,    76,    76,    76,    76,    76,    76,    76,
      76,    76,    76,    76,    76,     0,    76,    76,    72,    72,
      72,    71,     0,     0,     0,     0,    93,    91,    84,    86,
      76,    12,    14,    76,     5,     0,    61,    60,    59,    74,
      83,    85,    26,     0,     0,    20,    56,    76,    54,    76,
      52,    40,    50,    51,    45,     0,    49,    42,    41,    36,
      37,    39,    38,    32,    33,    34,    35,    76,    31,     0,
      69,    68,    67,    76,    76,    76,    76,    95,     0,    10,
      30,     0,    76,     8,    76,    76,    55,    53,    76,    72,
      48,     0,    98,     0,     0,     0,     0,     9,     0,    73,
       0,    28,     0,    27,    43,    44,    76,    72,    70,    76,
      76,    66,    76,    13,    76,    46,    47,     0,     0,     0,
      29,    64,    65,    11
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short yydefgoto[] =
{
      -1,    22,    23,    24,   158,   139,    25,   103,    96,    26,
      27,    28,   104,    29,    33
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -44
static const short yypact[] =
{
     251,   -44,   -44,   -44,   -44,    15,   -44,   -44,   -44,   -44,
     -44,   -44,   -44,   251,   251,   251,   251,   251,   251,   251,
      17,     2,    31,    10,    55,   947,   -18,    20,   -44,   118,
     -29,   -44,   374,   -29,   374,    18,    18,   374,   374,   -21,
     -44,    32,    66,    16,   -44,   251,   -44,   -44,    24,    24,
      24,   -44,   -44,   -44,   -44,   133,   251,   251,   173,   212,
     251,   251,   251,   290,   251,   251,   251,   251,   251,   251,
     251,   251,   251,   251,   251,    59,   251,   251,    24,    24,
      24,   -44,    35,    38,    40,    47,    58,   -44,   -44,   -44,
     251,   -44,   -44,   251,   947,   107,   -44,   -44,   -44,    56,
      52,    57,   915,     3,    68,   979,  1002,   251,  1002,   251,
    1002,   -20,   -20,   -20,  1004,   837,   -20,   -20,   -20,    51,
     374,   374,   374,   -19,   -19,   -19,   -19,   329,   -19,   414,
     -44,   -44,   -44,   251,   251,   251,   251,   -44,   536,   -44,
      18,    71,   251,   -44,   368,   251,  1002,  1002,   251,    24,
    1004,   876,   -44,   579,   446,   491,   622,   -44,    60,   -44,
     665,   947,    75,   947,   -20,   -44,   251,    24,   -44,   251,
     251,   -44,   251,   -44,   251,   -20,   -44,   708,   751,   794,
     947,   -44,   -44,   -44
};

/* YYPGOTO[NTERM-NUM].  */
static const yysigned_char yypgoto[] =
{
     -44,   -44,    99,   -44,   -44,   -44,   -13,   -44,   -43,   -44,
     -44,   -44,     0,   125,     8
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -93
static const short yytable[] =
{
      32,    34,    35,    36,    37,    38,    97,    98,    30,    41,
       6,    67,    68,    69,    70,   -92,    71,    72,    73,    74,
      75,    75,    76,    76,    40,    78,    79,    80,    90,    43,
      81,    44,    94,    82,    91,   130,   131,   132,    77,    77,
      45,    92,   102,   105,   106,   108,   110,   111,   112,   113,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,    31,   128,   129,   143,    46,   144,    47,    83,
      84,    45,   -92,    86,    87,    85,    77,   138,   -92,    31,
     140,    95,    93,    68,    69,    70,   127,    71,    72,    73,
      74,    75,   133,    76,   146,   134,   147,   135,    48,    49,
      50,    51,    52,    31,   136,   137,   165,    53,    54,    77,
     141,   -87,    55,   -92,   151,   -88,   -89,   172,    39,   -92,
     153,   154,   155,   156,   176,    86,    87,   145,    88,   160,
      89,   161,   163,   159,   174,   164,     1,     2,     3,     4,
      99,     6,     7,   100,   162,   101,    42,    10,    11,    12,
       0,     0,     0,   175,     0,     0,   177,   178,     0,   179,
       0,   180,     0,     0,     0,    13,    14,    15,     0,    16,
       0,     0,     0,    17,    18,     0,     1,     2,     3,     4,
       5,     6,     7,     8,     0,     9,     0,    10,    11,    12,
      19,   107,     0,    20,     0,   -25,     0,   -25,    21,     0,
       0,     0,     0,     0,     0,    13,    14,    15,     0,    16,
       0,     0,     0,    17,    18,     1,     2,     3,     4,     5,
       6,     7,     8,     0,     9,     0,    10,    11,    12,     0,
      19,   109,     0,    20,     0,     0,     0,     0,    21,     0,
       0,     0,     0,     0,    13,    14,    15,     0,    16,     0,
       0,     0,    17,    18,     1,     2,     3,     4,     5,     6,
       7,     8,     0,     9,     0,    10,    11,    12,     0,    19,
       0,     0,    20,     0,     0,     0,     0,    21,     0,     0,
       0,     0,     0,    13,    14,    15,     0,    16,     0,     0,
       0,    17,    18,     1,     2,     3,     4,   114,     6,     7,
       8,     0,     9,     0,    10,    11,    12,     0,    19,     0,
       0,    20,     0,     0,     0,     0,    21,     0,     0,     0,
       0,     0,    13,    14,    15,     0,    16,     0,     0,     0,
      17,    18,     1,     2,     3,     4,   150,     6,     7,     8,
       0,     9,     0,    10,    11,    12,     0,    19,     0,     0,
      20,     0,     0,     0,     0,    21,     0,     0,     0,     0,
       0,    13,    14,    15,     0,    16,     0,     0,     0,    17,
      18,     1,     2,     3,     4,    99,     6,     7,   100,     0,
     101,     0,    10,    11,    12,     0,    19,     0,     0,    20,
       0,     0,     0,     0,    21,     0,     0,     0,     0,     0,
      13,    14,    15,     0,    16,     0,     0,     0,    17,    18,
      71,    72,    73,    74,    75,     0,    76,     0,     0,     0,
       0,     0,     0,     0,     0,    19,     0,     0,    20,     0,
       0,    56,    77,    21,    57,    58,    59,    60,    61,    62,
       0,    63,    64,    65,    66,    67,    68,    69,    70,     0,
      71,    72,    73,    74,    75,     0,    76,     0,     0,     0,
       0,     0,     0,    56,     0,     0,    57,    58,    59,    60,
      61,    62,    77,    63,    64,    65,    66,    67,    68,    69,
      70,   152,    71,    72,    73,    74,    75,     0,    76,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    77,     0,     0,     0,    56,     0,
     169,    57,    58,    59,    60,    61,    62,     0,    63,    64,
      65,    66,    67,    68,    69,    70,     0,    71,    72,    73,
      74,    75,     0,    76,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    77,
       0,     0,     0,    56,     0,   170,    57,    58,    59,    60,
      61,    62,     0,    63,    64,    65,    66,    67,    68,    69,
      70,     0,    71,    72,    73,    74,    75,     0,    76,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    77,     0,    56,     0,   157,    57,
      58,    59,    60,    61,    62,     0,    63,    64,    65,    66,
      67,    68,    69,    70,     0,    71,    72,    73,    74,    75,
       0,    76,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    77,     0,    56,
       0,   168,    57,    58,    59,    60,    61,    62,     0,    63,
      64,    65,    66,    67,    68,    69,    70,     0,    71,    72,
      73,    74,    75,     0,    76,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      77,     0,    56,     0,   171,    57,    58,    59,    60,    61,
      62,     0,    63,    64,    65,    66,    67,    68,    69,    70,
       0,    71,    72,    73,    74,    75,     0,    76,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    77,     0,    56,     0,   173,    57,    58,
      59,    60,    61,    62,     0,    63,    64,    65,    66,    67,
      68,    69,    70,     0,    71,    72,    73,    74,    75,     0,
      76,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    77,     0,    56,     0,
     181,    57,    58,    59,    60,    61,    62,     0,    63,    64,
      65,    66,    67,    68,    69,    70,     0,    71,    72,    73,
      74,    75,     0,    76,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    77,
       0,    56,     0,   182,    57,    58,    59,    60,    61,    62,
       0,    63,    64,    65,    66,    67,    68,    69,    70,     0,
      71,    72,    73,    74,    75,     0,    76,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    77,     0,    56,     0,   183,    57,    58,    59,
      60,    61,    62,   148,    63,    64,    65,    66,    67,    68,
      69,    70,     0,    71,    72,    73,    74,    75,     0,    76,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   149,     0,    56,     0,    77,    57,    58,    59,    60,
      61,    62,   166,    63,    64,    65,    66,    67,    68,    69,
      70,     0,    71,    72,    73,    74,    75,     0,    76,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     167,     0,    56,     0,    77,    57,    58,    59,    60,    61,
      62,   142,    63,    64,    65,    66,    67,    68,    69,    70,
       0,    71,    72,    73,    74,    75,     0,    76,     0,     0,
       0,     0,     0,     0,    56,     0,     0,    57,    58,    59,
      60,    61,    62,    77,    63,    64,    65,    66,    67,    68,
      69,    70,     0,    71,    72,    73,    74,    75,     0,    76,
       0,     0,     0,     0,     0,     0,   -93,     0,     0,    57,
      58,    59,    60,    61,    62,    77,    63,    64,    65,    66,
      67,    68,    69,    70,     0,    71,    72,    73,    74,    75,
       0,    76,     0,     0,     0,    60,    61,    62,     0,    63,
      64,    65,    66,    67,    68,    69,    70,    77,    71,    72,
      73,    74,    75,     0,    76,     0,     0,   -74,   -74,   -74,
       0,    31,   -74,   -74,   -74,   -74,     0,     0,     0,   -74,
      77,   -92,     0,     0,     0,     0,     0,   -92
};

static const short yycheck[] =
{
      13,    14,    15,    16,    17,    18,    49,    50,     0,     7,
       8,    31,    32,    33,    34,     0,    36,    37,    38,    39,
      40,    40,    42,    42,     7,    43,    44,    45,    57,    21,
      48,     0,    45,    51,    63,    78,    79,    80,    58,    58,
      61,    62,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    47,    76,    77,    62,    11,    64,    13,    49,
      50,    61,    57,     7,     8,    55,    58,    90,    63,    47,
      93,    57,    66,    32,    33,    34,    27,    36,    37,    38,
      39,    40,    57,    42,   107,    57,   109,    57,    43,    44,
      45,    46,    47,    47,    57,    47,   149,    52,    53,    58,
       3,    59,    57,    57,   127,    59,    59,    57,    19,    63,
     133,   134,   135,   136,   167,     7,     8,    59,    10,   142,
      12,   144,   145,    62,    59,   148,     3,     4,     5,     6,
       7,     8,     9,    10,   144,    12,    21,    14,    15,    16,
      -1,    -1,    -1,   166,    -1,    -1,   169,   170,    -1,   172,
      -1,   174,    -1,    -1,    -1,    32,    33,    34,    -1,    36,
      -1,    -1,    -1,    40,    41,    -1,     3,     4,     5,     6,
       7,     8,     9,    10,    -1,    12,    -1,    14,    15,    16,
      57,    18,    -1,    60,    -1,    62,    -1,    64,    65,    -1,
      -1,    -1,    -1,    -1,    -1,    32,    33,    34,    -1,    36,
      -1,    -1,    -1,    40,    41,     3,     4,     5,     6,     7,
       8,     9,    10,    -1,    12,    -1,    14,    15,    16,    -1,
      57,    19,    -1,    60,    -1,    -1,    -1,    -1,    65,    -1,
      -1,    -1,    -1,    -1,    32,    33,    34,    -1,    36,    -1,
      -1,    -1,    40,    41,     3,     4,     5,     6,     7,     8,
       9,    10,    -1,    12,    -1,    14,    15,    16,    -1,    57,
      -1,    -1,    60,    -1,    -1,    -1,    -1,    65,    -1,    -1,
      -1,    -1,    -1,    32,    33,    34,    -1,    36,    -1,    -1,
      -1,    40,    41,     3,     4,     5,     6,     7,     8,     9,
      10,    -1,    12,    -1,    14,    15,    16,    -1,    57,    -1,
      -1,    60,    -1,    -1,    -1,    -1,    65,    -1,    -1,    -1,
      -1,    -1,    32,    33,    34,    -1,    36,    -1,    -1,    -1,
      40,    41,     3,     4,     5,     6,     7,     8,     9,    10,
      -1,    12,    -1,    14,    15,    16,    -1,    57,    -1,    -1,
      60,    -1,    -1,    -1,    -1,    65,    -1,    -1,    -1,    -1,
      -1,    32,    33,    34,    -1,    36,    -1,    -1,    -1,    40,
      41,     3,     4,     5,     6,     7,     8,     9,    10,    -1,
      12,    -1,    14,    15,    16,    -1,    57,    -1,    -1,    60,
      -1,    -1,    -1,    -1,    65,    -1,    -1,    -1,    -1,    -1,
      32,    33,    34,    -1,    36,    -1,    -1,    -1,    40,    41,
      36,    37,    38,    39,    40,    -1,    42,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    57,    -1,    -1,    60,    -1,
      -1,    17,    58,    65,    20,    21,    22,    23,    24,    25,
      -1,    27,    28,    29,    30,    31,    32,    33,    34,    -1,
      36,    37,    38,    39,    40,    -1,    42,    -1,    -1,    -1,
      -1,    -1,    -1,    17,    -1,    -1,    20,    21,    22,    23,
      24,    25,    58,    27,    28,    29,    30,    31,    32,    33,
      34,    67,    36,    37,    38,    39,    40,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    58,    -1,    -1,    -1,    17,    -1,
      64,    20,    21,    22,    23,    24,    25,    -1,    27,    28,
      29,    30,    31,    32,    33,    34,    -1,    36,    37,    38,
      39,    40,    -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    58,
      -1,    -1,    -1,    17,    -1,    64,    20,    21,    22,    23,
      24,    25,    -1,    27,    28,    29,    30,    31,    32,    33,
      34,    -1,    36,    37,    38,    39,    40,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    58,    -1,    17,    -1,    62,    20,
      21,    22,    23,    24,    25,    -1,    27,    28,    29,    30,
      31,    32,    33,    34,    -1,    36,    37,    38,    39,    40,
      -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    58,    -1,    17,
      -1,    62,    20,    21,    22,    23,    24,    25,    -1,    27,
      28,    29,    30,    31,    32,    33,    34,    -1,    36,    37,
      38,    39,    40,    -1,    42,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      58,    -1,    17,    -1,    62,    20,    21,    22,    23,    24,
      25,    -1,    27,    28,    29,    30,    31,    32,    33,    34,
      -1,    36,    37,    38,    39,    40,    -1,    42,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    58,    -1,    17,    -1,    62,    20,    21,
      22,    23,    24,    25,    -1,    27,    28,    29,    30,    31,
      32,    33,    34,    -1,    36,    37,    38,    39,    40,    -1,
      42,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    58,    -1,    17,    -1,
      62,    20,    21,    22,    23,    24,    25,    -1,    27,    28,
      29,    30,    31,    32,    33,    34,    -1,    36,    37,    38,
      39,    40,    -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    58,
      -1,    17,    -1,    62,    20,    21,    22,    23,    24,    25,
      -1,    27,    28,    29,    30,    31,    32,    33,    34,    -1,
      36,    37,    38,    39,    40,    -1,    42,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    58,    -1,    17,    -1,    62,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    -1,    36,    37,    38,    39,    40,    -1,    42,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    54,    -1,    17,    -1,    58,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    -1,    36,    37,    38,    39,    40,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      54,    -1,    17,    -1,    58,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      -1,    36,    37,    38,    39,    40,    -1,    42,    -1,    -1,
      -1,    -1,    -1,    -1,    17,    -1,    -1,    20,    21,    22,
      23,    24,    25,    58,    27,    28,    29,    30,    31,    32,
      33,    34,    -1,    36,    37,    38,    39,    40,    -1,    42,
      -1,    -1,    -1,    -1,    -1,    -1,    17,    -1,    -1,    20,
      21,    22,    23,    24,    25,    58,    27,    28,    29,    30,
      31,    32,    33,    34,    -1,    36,    37,    38,    39,    40,
      -1,    42,    -1,    -1,    -1,    23,    24,    25,    -1,    27,
      28,    29,    30,    31,    32,    33,    34,    58,    36,    37,
      38,    39,    40,    -1,    42,    -1,    -1,    43,    44,    45,
      -1,    47,    48,    49,    50,    51,    -1,    -1,    -1,    55,
      58,    57,    -1,    -1,    -1,    -1,    -1,    63
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    12,
      14,    15,    16,    32,    33,    34,    36,    40,    41,    57,
      60,    65,    69,    70,    71,    74,    77,    78,    79,    81,
      82,    47,    74,    82,    74,    74,    74,    74,    74,    70,
       7,     7,    81,    82,     0,    61,    11,    13,    43,    44,
      45,    46,    47,    52,    53,    57,    17,    20,    21,    22,
      23,    24,    25,    27,    28,    29,    30,    31,    32,    33,
      34,    36,    37,    38,    39,    40,    42,    58,    43,    44,
      45,    48,    51,    49,    50,    55,     7,     8,    10,    12,
      57,    63,    62,    66,    74,    57,    76,    76,    76,     7,
      10,    12,    74,    75,    80,    74,    74,    18,    74,    19,
      74,    74,    74,    74,     7,    74,    74,    74,    74,    74,
      74,    74,    74,    74,    74,    74,    74,    27,    74,    74,
      76,    76,    76,    57,    57,    57,    57,    47,    74,    73,
      74,     3,    26,    62,    64,    59,    74,    74,    26,    54,
       7,    74,    67,    74,    74,    74,    74,    62,    72,    62,
      74,    74,    80,    74,    74,    76,    26,    54,    62,    64,
      64,    62,    57,    62,    59,    74,    76,    74,    74,    74,
      74,    62,    62,    62
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
        case 3:
#line 209 "ada-exp.y"
    { write_exp_elt_opcode (OP_TYPE);
			  write_exp_elt_type (yyvsp[0].tval);
 			  write_exp_elt_opcode (OP_TYPE); }
    break;

  case 5:
#line 217 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_COMMA); }
    break;

  case 6:
#line 222 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_IND); }
    break;

  case 7:
#line 226 "ada-exp.y"
    { write_exp_elt_opcode (STRUCTOP_STRUCT);
			  write_exp_string (yyvsp[0].ssym.stoken);
			  write_exp_elt_opcode (STRUCTOP_STRUCT); 
			  }
    break;

  case 8:
#line 233 "ada-exp.y"
    {
			  write_exp_elt_opcode (OP_FUNCALL);
			  write_exp_elt_longcst (yyvsp[-1].lval);
			  write_exp_elt_opcode (OP_FUNCALL);
		        }
    break;

  case 9:
#line 241 "ada-exp.y"
    {
			  write_exp_elt_opcode (UNOP_CAST);
			  write_exp_elt_type (yyvsp[-3].tval);
			  write_exp_elt_opcode (UNOP_CAST); 
			}
    break;

  case 10:
#line 248 "ada-exp.y"
    { type_qualifier = yyvsp[-2].tval; }
    break;

  case 11:
#line 249 "ada-exp.y"
    {
			  /*			  write_exp_elt_opcode (UNOP_QUAL); */
			  /* FIXME: UNOP_QUAL should be defined in expression.h */
			  write_exp_elt_type (yyvsp[-6].tval);
			  /* write_exp_elt_opcode (UNOP_QUAL); */
			  /* FIXME: UNOP_QUAL should be defined in expression.h */
			  type_qualifier = yyvsp[-4].tval;
			}
    break;

  case 12:
#line 259 "ada-exp.y"
    { yyval.tval = type_qualifier; }
    break;

  case 13:
#line 264 "ada-exp.y"
    { write_exp_elt_opcode (TERNOP_SLICE); }
    break;

  case 14:
#line 267 "ada-exp.y"
    { }
    break;

  case 16:
#line 274 "ada-exp.y"
    { write_exp_elt_opcode (OP_REGISTER);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].lval);
			  write_exp_elt_opcode (OP_REGISTER); 
			}
    break;

  case 17:
#line 281 "ada-exp.y"
    { write_exp_elt_opcode (OP_INTERNALVAR);
			  write_exp_elt_intern (yyvsp[0].ivar);
			  write_exp_elt_opcode (OP_INTERNALVAR); 
			}
    break;

  case 19:
#line 292 "ada-exp.y"
    { write_exp_elt_opcode (OP_LAST);
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].lval);
			  write_exp_elt_opcode (OP_LAST); 
			 }
    break;

  case 20:
#line 299 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_ASSIGN); }
    break;

  case 21:
#line 303 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_NEG); }
    break;

  case 22:
#line 307 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_PLUS); }
    break;

  case 23:
#line 311 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_LOGICAL_NOT); }
    break;

  case 24:
#line 315 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_ABS); }
    break;

  case 25:
#line 318 "ada-exp.y"
    { yyval.lval = 0; }
    break;

  case 26:
#line 322 "ada-exp.y"
    { yyval.lval = 1; }
    break;

  case 27:
#line 324 "ada-exp.y"
    { yyval.lval = 1; }
    break;

  case 28:
#line 326 "ada-exp.y"
    { yyval.lval = yyvsp[-2].lval + 1; }
    break;

  case 29:
#line 328 "ada-exp.y"
    { yyval.lval = yyvsp[-4].lval + 1; }
    break;

  case 30:
#line 333 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_MEMVAL);
			  write_exp_elt_type (yyvsp[-2].tval);
			  write_exp_elt_opcode (UNOP_MEMVAL); 
			}
    break;

  case 31:
#line 342 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_EXP); }
    break;

  case 32:
#line 346 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_MUL); }
    break;

  case 33:
#line 350 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_DIV); }
    break;

  case 34:
#line 354 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_REM); }
    break;

  case 35:
#line 358 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_MOD); }
    break;

  case 36:
#line 362 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_REPEAT); }
    break;

  case 37:
#line 366 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_ADD); }
    break;

  case 38:
#line 370 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_CONCAT); }
    break;

  case 39:
#line 374 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_SUB); }
    break;

  case 40:
#line 378 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_EQUAL); }
    break;

  case 41:
#line 382 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_NOTEQUAL); }
    break;

  case 42:
#line 386 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_LEQ); }
    break;

  case 43:
#line 390 "ada-exp.y"
    { /*write_exp_elt_opcode (TERNOP_MBR); */ }
    break;

  case 44:
#line 394 "ada-exp.y"
    { /*write_exp_elt_opcode (BINOP_MBR); */
			  /* FIXME: BINOP_MBR should be defined in expression.h */
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].lval);
			  /*write_exp_elt_opcode (BINOP_MBR); */
			}
    break;

  case 45:
#line 400 "ada-exp.y"
    { /*write_exp_elt_opcode (UNOP_MBR); */
			  /* FIXME: UNOP_QUAL should be defined in expression.h */			  
		          write_exp_elt_type (yyvsp[0].tval);
			  /*		          write_exp_elt_opcode (UNOP_MBR); */
			  /* FIXME: UNOP_MBR should be defined in expression.h */			  
			}
    break;

  case 46:
#line 407 "ada-exp.y"
    { /*write_exp_elt_opcode (TERNOP_MBR); */
			  /* FIXME: TERNOP_MBR should be defined in expression.h */			  			  
		          write_exp_elt_opcode (UNOP_LOGICAL_NOT); 
			}
    break;

  case 47:
#line 412 "ada-exp.y"
    { /* write_exp_elt_opcode (BINOP_MBR); */
			  /* FIXME: BINOP_MBR should be defined in expression.h */
			  write_exp_elt_longcst ((LONGEST) yyvsp[0].lval);
			  /*write_exp_elt_opcode (BINOP_MBR);*/
			  /* FIXME: BINOP_MBR should be defined in expression.h */			  
		          write_exp_elt_opcode (UNOP_LOGICAL_NOT); 
			}
    break;

  case 48:
#line 420 "ada-exp.y"
    { /*write_exp_elt_opcode (UNOP_MBR);*/
			  /* FIXME: UNOP_MBR should be defined in expression.h */			  
		          write_exp_elt_type (yyvsp[0].tval);
			  /*		          write_exp_elt_opcode (UNOP_MBR);*/
			  /* FIXME: UNOP_MBR should be defined in expression.h */			  			  
		          write_exp_elt_opcode (UNOP_LOGICAL_NOT); 
			}
    break;

  case 49:
#line 430 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_GEQ); }
    break;

  case 50:
#line 434 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_LESS); }
    break;

  case 51:
#line 438 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_GTR); }
    break;

  case 52:
#line 442 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_BITWISE_AND); }
    break;

  case 53:
#line 446 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_LOGICAL_AND); }
    break;

  case 54:
#line 450 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_BITWISE_IOR); }
    break;

  case 55:
#line 454 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_LOGICAL_OR); }
    break;

  case 56:
#line 458 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_BITWISE_XOR); }
    break;

  case 57:
#line 462 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_ADDR); }
    break;

  case 58:
#line 464 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_ADDR);
			  write_exp_elt_opcode (UNOP_CAST);
			  write_exp_elt_type (builtin_type_ada_system_address);
			  write_exp_elt_opcode (UNOP_CAST);
			}
    break;

  case 59:
#line 470 "ada-exp.y"
    { write_attribute_call1 (ATR_FIRST, yyvsp[0].lval); }
    break;

  case 60:
#line 472 "ada-exp.y"
    { write_attribute_call1 (ATR_LAST, yyvsp[0].lval); }
    break;

  case 61:
#line 474 "ada-exp.y"
    { write_attribute_call1 (ATR_LENGTH, yyvsp[0].lval); }
    break;

  case 62:
#line 476 "ada-exp.y"
    { write_attribute_call0 (ATR_SIZE); }
    break;

  case 63:
#line 478 "ada-exp.y"
    { write_attribute_call0 (ATR_TAG); }
    break;

  case 64:
#line 480 "ada-exp.y"
    { write_attribute_calln (ATR_MIN, 2); }
    break;

  case 65:
#line 482 "ada-exp.y"
    { write_attribute_calln (ATR_MAX, 2); }
    break;

  case 66:
#line 484 "ada-exp.y"
    { write_attribute_calln (ATR_POS, 1); }
    break;

  case 67:
#line 486 "ada-exp.y"
    { write_attribute_call1 (ATR_FIRST, yyvsp[0].lval); }
    break;

  case 68:
#line 488 "ada-exp.y"
    { write_attribute_call1 (ATR_LAST, yyvsp[0].lval); }
    break;

  case 69:
#line 490 "ada-exp.y"
    { write_attribute_call1 (ATR_LENGTH, yyvsp[0].lval); }
    break;

  case 70:
#line 492 "ada-exp.y"
    { write_attribute_calln (ATR_VAL, 1); }
    break;

  case 71:
#line 494 "ada-exp.y"
    { write_attribute_call0 (ATR_MODULUS); }
    break;

  case 72:
#line 498 "ada-exp.y"
    { yyval.lval = 1; }
    break;

  case 73:
#line 500 "ada-exp.y"
    { yyval.lval = yyvsp[-1].typed_val.val; }
    break;

  case 74:
#line 505 "ada-exp.y"
    { write_exp_elt_opcode (OP_TYPE);
			  write_exp_elt_type (yyvsp[0].tval);
			  write_exp_elt_opcode (OP_TYPE); }
    break;

  case 76:
#line 513 "ada-exp.y"
    { write_exp_elt_opcode (OP_TYPE);
			  write_exp_elt_type (builtin_type_void);
			  write_exp_elt_opcode (OP_TYPE); }
    break;

  case 77:
#line 520 "ada-exp.y"
    { write_exp_elt_opcode (OP_LONG);
			  write_exp_elt_type (yyvsp[0].typed_val.type);
			  write_exp_elt_longcst ((LONGEST)(yyvsp[0].typed_val.val));
			  write_exp_elt_opcode (OP_LONG); 
			}
    break;

  case 78:
#line 528 "ada-exp.y"
    { write_exp_elt_opcode (OP_LONG);
			  if (type_qualifier == NULL) 
			    write_exp_elt_type (yyvsp[0].typed_val.type);
			  else
			    write_exp_elt_type (type_qualifier);
			  write_exp_elt_longcst 
			    (convert_char_literal (type_qualifier, yyvsp[0].typed_val.val));
			  write_exp_elt_opcode (OP_LONG); 
			}
    break;

  case 79:
#line 540 "ada-exp.y"
    { write_exp_elt_opcode (OP_DOUBLE);
			  write_exp_elt_type (yyvsp[0].typed_val_float.type);
			  write_exp_elt_dblcst (yyvsp[0].typed_val_float.dval);
			  write_exp_elt_opcode (OP_DOUBLE); 
			}
    break;

  case 80:
#line 548 "ada-exp.y"
    { write_exp_elt_opcode (OP_LONG);
			  write_exp_elt_type (builtin_type_int);
			  write_exp_elt_longcst ((LONGEST)(0));
			  write_exp_elt_opcode (OP_LONG); 
			 }
    break;

  case 81:
#line 556 "ada-exp.y"
    { /* Ada strings are converted into array constants 
			     a lower bound of 1.  Thus, the array upper bound 
			     is the string length. */
			  char *sp = yyvsp[0].sval.ptr; int count;
			  if (yyvsp[0].sval.length == 0) 
			    { /* One dummy character for the type */
			      write_exp_elt_opcode (OP_LONG);
			      write_exp_elt_type (builtin_type_ada_char);
			      write_exp_elt_longcst ((LONGEST)(0));
			      write_exp_elt_opcode (OP_LONG);
			    }
			  for (count = yyvsp[0].sval.length; count > 0; count -= 1)
			    {
			      write_exp_elt_opcode (OP_LONG);
			      write_exp_elt_type (builtin_type_ada_char);
			      write_exp_elt_longcst ((LONGEST)(*sp));
			      sp += 1;
			      write_exp_elt_opcode (OP_LONG);
			    }
			  write_exp_elt_opcode (OP_ARRAY);
			  write_exp_elt_longcst ((LONGEST) 1);
			  write_exp_elt_longcst ((LONGEST) (yyvsp[0].sval.length));
			  write_exp_elt_opcode (OP_ARRAY); 
			 }
    break;

  case 82:
#line 583 "ada-exp.y"
    { error ("NEW not implemented."); }
    break;

  case 83:
#line 586 "ada-exp.y"
    { write_var_from_name (NULL, yyvsp[0].ssym); }
    break;

  case 84:
#line 588 "ada-exp.y"
    { write_var_from_name (yyvsp[-1].bval, yyvsp[0].ssym); }
    break;

  case 85:
#line 589 "ada-exp.y"
    { write_object_renaming (NULL, yyvsp[0].ssym.sym); }
    break;

  case 86:
#line 591 "ada-exp.y"
    { write_object_renaming (yyvsp[-1].bval, yyvsp[0].ssym.sym); }
    break;

  case 87:
#line 594 "ada-exp.y"
    { }
    break;

  case 88:
#line 595 "ada-exp.y"
    { }
    break;

  case 89:
#line 596 "ada-exp.y"
    { }
    break;

  case 90:
#line 600 "ada-exp.y"
    { yyval.bval = yyvsp[0].bval; }
    break;

  case 91:
#line 602 "ada-exp.y"
    { yyval.bval = yyvsp[0].bval; }
    break;

  case 92:
#line 606 "ada-exp.y"
    { yyval.tval = yyvsp[0].tval; }
    break;

  case 93:
#line 607 "ada-exp.y"
    { yyval.tval = yyvsp[0].tval; }
    break;

  case 94:
#line 609 "ada-exp.y"
    { yyval.tval = lookup_pointer_type (yyvsp[-1].tval); }
    break;

  case 95:
#line 611 "ada-exp.y"
    { yyval.tval = lookup_pointer_type (yyvsp[-1].tval); }
    break;

  case 96:
#line 618 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_IND); }
    break;

  case 97:
#line 620 "ada-exp.y"
    { write_exp_elt_opcode (UNOP_ADDR); }
    break;

  case 98:
#line 622 "ada-exp.y"
    { write_exp_elt_opcode (BINOP_SUBSCRIPT); }
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


#line 625 "ada-exp.y"


/* yylex defined in ada-lex.c: Reads one token, getting characters */
/* through lexptr.  */

/* Remap normal flex interface names (yylex) as well as gratuitiously */
/* global symbol names, so we can have multiple flex-generated parsers */
/* in gdb.  */

/* (See note above on previous definitions for YACC.) */

#define yy_create_buffer ada_yy_create_buffer
#define yy_delete_buffer ada_yy_delete_buffer
#define yy_init_buffer ada_yy_init_buffer
#define yy_load_buffer_state ada_yy_load_buffer_state
#define yy_switch_to_buffer ada_yy_switch_to_buffer
#define yyrestart ada_yyrestart
#define yytext ada_yytext
#define yywrap ada_yywrap

/* The following kludge was found necessary to prevent conflicts between */
/* defs.h and non-standard stdlib.h files.  */
#define qsort __qsort__dummy
#include "ada-lex.c"

int
ada_parse ()
{
  lexer_init (yyin);		/* (Re-)initialize lexer. */
  left_block_context = NULL;
  type_qualifier = NULL;
  
  return _ada_parse ();
}

void
yyerror (msg)
     char *msg;
{
  error ("A %s in expression, near `%s'.", (msg ? msg : "error"), lexptr);
}

/* The operator name corresponding to operator symbol STRING (adds 
   quotes and maps to lower-case).  Destroys the previous contents of
   the array pointed to by STRING.ptr.  Error if STRING does not match
   a valid Ada operator.  Assumes that STRING.ptr points to a
   null-terminated string and that, if STRING is a valid operator
   symbol, the array pointed to by STRING.ptr contains at least
   STRING.length+3 characters. */ 

static struct stoken
string_to_operator (string)
     struct stoken string;
{
  int i;

  for (i = 0; ada_opname_table[i].mangled != NULL; i += 1)
    {
      if (string.length == strlen (ada_opname_table[i].demangled)-2
	  && strncasecmp (string.ptr, ada_opname_table[i].demangled+1,
			  string.length) == 0)
	{
	  strncpy (string.ptr, ada_opname_table[i].demangled,
		   string.length+2);
	  string.length += 2;
	  return string;
	}
    }
  error ("Invalid operator symbol `%s'", string.ptr);
}

/* Emit expression to access an instance of SYM, in block BLOCK (if
 * non-NULL), and with :: qualification ORIG_LEFT_CONTEXT. */
static void
write_var_from_sym (orig_left_context, block, sym)
     struct block* orig_left_context;
     struct block* block;
     struct symbol* sym;
{
  if (orig_left_context == NULL && symbol_read_needs_frame (sym))
    {
      if (innermost_block == 0 ||
	  contained_in (block, innermost_block))
	innermost_block = block;
    }

  write_exp_elt_opcode (OP_VAR_VALUE);
  /* We want to use the selected frame, not another more inner frame
     which happens to be in the same block */
  write_exp_elt_block (NULL);
  write_exp_elt_sym (sym);
  write_exp_elt_opcode (OP_VAR_VALUE);
}

/* Emit expression to access an instance of NAME. */
static void
write_var_from_name (orig_left_context, name)
     struct block* orig_left_context;
     struct name_info name;
{
  if (name.msym != NULL)
    {
      write_exp_msymbol (name.msym, 
			 lookup_function_type (builtin_type_int),
			 builtin_type_int);
    }
  else if (name.sym == NULL) 
    {
      /* Multiple matches: record name and starting block for later 
         resolution by ada_resolve. */
      /*      write_exp_elt_opcode (OP_UNRESOLVED_VALUE); */
      /* FIXME: OP_UNRESOLVED_VALUE should be defined in expression.h */      
      write_exp_elt_block (name.block);
      /*      write_exp_elt_name (name.stoken.ptr); */
      /* FIXME: write_exp_elt_name should be defined in defs.h, located in parse.c */      
      /*      write_exp_elt_opcode (OP_UNRESOLVED_VALUE); */
      /* FIXME: OP_UNRESOLVED_VALUE should be defined in expression.h */      
    }
  else
    write_var_from_sym (orig_left_context, name.block, name.sym);
}

/* Write a call on parameterless attribute ATR.  */

static void
write_attribute_call0 (atr)
     enum ada_attribute atr;
{
  /*  write_exp_elt_opcode (OP_ATTRIBUTE); */
  /* FIXME: OP_ATTRIBUTE should be defined in expression.h */      
  write_exp_elt_longcst ((LONGEST) 0);
  write_exp_elt_longcst ((LONGEST) atr);
  /*  write_exp_elt_opcode (OP_ATTRIBUTE); */
  /* FIXME: OP_ATTRIBUTE should be defined in expression.h */      
}

/* Write a call on an attribute ATR with one constant integer
 * parameter. */

static void
write_attribute_call1 (atr, arg)
     enum ada_attribute atr;
     LONGEST arg;
{
  write_exp_elt_opcode (OP_LONG);
  write_exp_elt_type (builtin_type_int);
  write_exp_elt_longcst (arg);
  write_exp_elt_opcode (OP_LONG);
  /*write_exp_elt_opcode (OP_ATTRIBUTE);*/
  /* FIXME: OP_ATTRIBUTE should be defined in expression.h */
  write_exp_elt_longcst ((LONGEST) 1);
  write_exp_elt_longcst ((LONGEST) atr);
  /*write_exp_elt_opcode (OP_ATTRIBUTE);*/
  /* FIXME: OP_ATTRIBUTE should be defined in expression.h */        
}  

/* Write a call on an attribute ATR with N parameters, whose code must have
 * been generated previously. */

static void
write_attribute_calln (atr, n)
     enum ada_attribute atr;
     int n;
{
  /*write_exp_elt_opcode (OP_ATTRIBUTE);*/
  /* FIXME: OP_ATTRIBUTE should be defined in expression.h */      
  write_exp_elt_longcst ((LONGEST) n);
  write_exp_elt_longcst ((LONGEST) atr);
  /*  write_exp_elt_opcode (OP_ATTRIBUTE);*/
  /* FIXME: OP_ATTRIBUTE should be defined in expression.h */        
}  

/* Emit expression corresponding to the renamed object designated by 
 * the type RENAMING, which must be the referent of an object renaming
 * type, in the context of ORIG_LEFT_CONTEXT (?). */
static void
write_object_renaming (orig_left_context, renaming)
     struct block* orig_left_context;
     struct symbol* renaming;
{
  const char* qualification = DEPRECATED_SYMBOL_NAME (renaming);
  const char* simple_tail;
  const char* expr = TYPE_FIELD_NAME (SYMBOL_TYPE (renaming), 0);
  const char* suffix;
  char* name;
  struct symbol* sym;
  enum { SIMPLE_INDEX, LOWER_BOUND, UPPER_BOUND } slice_state;

  /* if orig_left_context is null, then use the currently selected
     block, otherwise we might fail our symbol lookup below */
  if (orig_left_context == NULL)
    orig_left_context = get_selected_block (NULL);

  for (simple_tail = qualification + strlen (qualification); 
       simple_tail != qualification; simple_tail -= 1)
    {
      if (*simple_tail == '.')
	{
	  simple_tail += 1;
	  break;
	} 
      else if (DEPRECATED_STREQN (simple_tail, "__", 2))
	{
	  simple_tail += 2;
	  break;
	}
    }

  suffix = strstr (expr, "___XE");
  if (suffix == NULL)
    goto BadEncoding;

  name = (char*) xmalloc (suffix - expr + 1);
  /*  add_name_string_cleanup (name); */
  /* FIXME: add_name_string_cleanup should be defined in
     parser-defs.h, implemented in parse.c */    
  strncpy (name, expr, suffix-expr);
  name[suffix-expr] = '\000';
  sym = lookup_symbol (name, orig_left_context, VAR_DOMAIN, 0, NULL);
  /*  if (sym == NULL) 
    error ("Could not find renamed variable: %s", ada_demangle (name));
  */
  /* FIXME: ada_demangle should be defined in defs.h, implemented in ada-lang.c */  
  write_var_from_sym (orig_left_context, block_found, sym);

  suffix += 5;
  slice_state = SIMPLE_INDEX;
  while (*suffix == 'X') 
    {
      suffix += 1;

      switch (*suffix) {
      case 'L':
	slice_state = LOWER_BOUND;
      case 'S':
	suffix += 1;
	if (isdigit (*suffix)) 
	  {
	    char* next;
	    long val = strtol (suffix, &next, 10);
	    if (next == suffix) 
	      goto BadEncoding;
	    suffix = next;
	    write_exp_elt_opcode (OP_LONG);
	    write_exp_elt_type (builtin_type_ada_int);
	    write_exp_elt_longcst ((LONGEST) val);
	    write_exp_elt_opcode (OP_LONG);
	  } 
	else
	  {
	    const char* end;
	    char* index_name;
	    int index_len;
	    struct symbol* index_sym;

	    end = strchr (suffix, 'X');
	    if (end == NULL) 
	      end = suffix + strlen (suffix);
	    
	    index_len = simple_tail - qualification + 2 + (suffix - end) + 1;
	    index_name = (char*) xmalloc (index_len);
	    memset (index_name, '\000', index_len);
	    /*	    add_name_string_cleanup (index_name);*/
	    /* FIXME: add_name_string_cleanup should be defined in
	       parser-defs.h, implemented in parse.c */    	    
	    strncpy (index_name, qualification, simple_tail - qualification);
	    index_name[simple_tail - qualification] = '\000';
	    strncat (index_name, suffix, suffix-end);
	    suffix = end;

	    index_sym = 
	      lookup_symbol (index_name, NULL, VAR_DOMAIN, 0, NULL);
	    if (index_sym == NULL)
	      error ("Could not find %s", index_name);
	    write_var_from_sym (NULL, block_found, sym);
	  }
	if (slice_state == SIMPLE_INDEX)
	  { 
	    write_exp_elt_opcode (OP_FUNCALL);
	    write_exp_elt_longcst ((LONGEST) 1);
	    write_exp_elt_opcode (OP_FUNCALL);
	  }
	else if (slice_state == LOWER_BOUND)
	  slice_state = UPPER_BOUND;
	else if (slice_state == UPPER_BOUND)
	  {
	    write_exp_elt_opcode (TERNOP_SLICE);
	    slice_state = SIMPLE_INDEX;
	  }
	break;

      case 'R':
	{
	  struct stoken field_name;
	  const char* end;
	  suffix += 1;
	  
	  if (slice_state != SIMPLE_INDEX)
	    goto BadEncoding;
	  end = strchr (suffix, 'X');
	  if (end == NULL) 
	    end = suffix + strlen (suffix);
	  field_name.length = end - suffix;
	  field_name.ptr = (char*) xmalloc (end - suffix + 1);
	  strncpy (field_name.ptr, suffix, end - suffix);
	  field_name.ptr[end - suffix] = '\000';
	  suffix = end;
	  write_exp_elt_opcode (STRUCTOP_STRUCT);
	  write_exp_string (field_name);
	  write_exp_elt_opcode (STRUCTOP_STRUCT); 	  
	  break;
	}
	  
      default:
	goto BadEncoding;
      }
    }
  if (slice_state == SIMPLE_INDEX)
    return;

 BadEncoding:
  error ("Internal error in encoding of renaming declaration: %s",
	 DEPRECATED_SYMBOL_NAME (renaming));
}

/* Convert the character literal whose ASCII value would be VAL to the
   appropriate value of type TYPE, if there is a translation.
   Otherwise return VAL.  Hence, in an enumeration type ('A', 'B'), 
   the literal 'A' (VAL == 65), returns 0. */
static LONGEST
convert_char_literal (struct type* type, LONGEST val)
{
  char name[7];
  int f;

  if (type == NULL || TYPE_CODE (type) != TYPE_CODE_ENUM)
    return val;
  sprintf (name, "QU%02x", (int) val);
  for (f = 0; f < TYPE_NFIELDS (type); f += 1) 
    {
      if (DEPRECATED_STREQ (name, TYPE_FIELD_NAME (type, f)))
	return TYPE_FIELD_BITPOS (type, f);
    }
  return val;
}


