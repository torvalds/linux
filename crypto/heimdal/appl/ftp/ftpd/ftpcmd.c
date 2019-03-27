/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

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
     A = 258,
     B = 259,
     C = 260,
     E = 261,
     F = 262,
     I = 263,
     L = 264,
     N = 265,
     P = 266,
     R = 267,
     S = 268,
     T = 269,
     SP = 270,
     CRLF = 271,
     COMMA = 272,
     USER = 273,
     PASS = 274,
     ACCT = 275,
     REIN = 276,
     QUIT = 277,
     PORT = 278,
     PASV = 279,
     TYPE = 280,
     STRU = 281,
     MODE = 282,
     RETR = 283,
     STOR = 284,
     APPE = 285,
     MLFL = 286,
     MAIL = 287,
     MSND = 288,
     MSOM = 289,
     MSAM = 290,
     MRSQ = 291,
     MRCP = 292,
     ALLO = 293,
     REST = 294,
     RNFR = 295,
     RNTO = 296,
     ABOR = 297,
     DELE = 298,
     CWD = 299,
     LIST = 300,
     NLST = 301,
     SITE = 302,
     sTAT = 303,
     HELP = 304,
     NOOP = 305,
     MKD = 306,
     RMD = 307,
     PWD = 308,
     CDUP = 309,
     STOU = 310,
     SMNT = 311,
     SYST = 312,
     SIZE = 313,
     MDTM = 314,
     EPRT = 315,
     EPSV = 316,
     UMASK = 317,
     IDLE = 318,
     CHMOD = 319,
     AUTH = 320,
     ADAT = 321,
     PROT = 322,
     PBSZ = 323,
     CCC = 324,
     MIC = 325,
     CONF = 326,
     ENC = 327,
     KAUTH = 328,
     KLIST = 329,
     KDESTROY = 330,
     KRBTKFILE = 331,
     AFSLOG = 332,
     LOCATE = 333,
     URL = 334,
     FEAT = 335,
     OPTS = 336,
     LEXERR = 337,
     STRING = 338,
     NUMBER = 339
   };
#endif
/* Tokens.  */
#define A 258
#define B 259
#define C 260
#define E 261
#define F 262
#define I 263
#define L 264
#define N 265
#define P 266
#define R 267
#define S 268
#define T 269
#define SP 270
#define CRLF 271
#define COMMA 272
#define USER 273
#define PASS 274
#define ACCT 275
#define REIN 276
#define QUIT 277
#define PORT 278
#define PASV 279
#define TYPE 280
#define STRU 281
#define MODE 282
#define RETR 283
#define STOR 284
#define APPE 285
#define MLFL 286
#define MAIL 287
#define MSND 288
#define MSOM 289
#define MSAM 290
#define MRSQ 291
#define MRCP 292
#define ALLO 293
#define REST 294
#define RNFR 295
#define RNTO 296
#define ABOR 297
#define DELE 298
#define CWD 299
#define LIST 300
#define NLST 301
#define SITE 302
#define sTAT 303
#define HELP 304
#define NOOP 305
#define MKD 306
#define RMD 307
#define PWD 308
#define CDUP 309
#define STOU 310
#define SMNT 311
#define SYST 312
#define SIZE 313
#define MDTM 314
#define EPRT 315
#define EPSV 316
#define UMASK 317
#define IDLE 318
#define CHMOD 319
#define AUTH 320
#define ADAT 321
#define PROT 322
#define PBSZ 323
#define CCC 324
#define MIC 325
#define CONF 326
#define ENC 327
#define KAUTH 328
#define KLIST 329
#define KDESTROY 330
#define KRBTKFILE 331
#define AFSLOG 332
#define LOCATE 333
#define URL 334
#define FEAT 335
#define OPTS 336
#define LEXERR 337
#define STRING 338
#define NUMBER 339




/* Copy the first part of user declarations.  */
#line 43 "ftpcmd.y"


#include "ftpd_locl.h"
RCSID("$Id$");

off_t	restart_point;

static	int hasyyerrored;


static	int cmd_type;
static	int cmd_form;
static	int cmd_bytesz;
char	cbuf[64*1024];
char	*fromname;

struct tab {
	char	*name;
	short	token;
	short	state;
	short	implemented;	/* 1 if command is implemented */
	char	*help;
};

extern struct tab cmdtab[];
extern struct tab sitetab[];

static char		*copy (char *);
static void		 help (struct tab *, char *);
static struct tab *
			 lookup (struct tab *, char *);
static void		 sizecmd (char *);
static RETSIGTYPE	 toolong (int);
static int		 yylex (void);

/* This is for bison */

#if !defined(alloca) && !defined(HAVE_ALLOCA)
#define alloca(x) malloc(x)
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

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 86 "ftpcmd.y"
{
	int	i;
	char   *s;
}
/* Line 193 of yacc.c.  */
#line 312 "ftpcmd.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 325 "ftpcmd.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
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
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  2
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   327

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  85
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  18
/* YYNRULES -- Number of rules.  */
#define YYNRULES  98
/* YYNRULES -- Number of states.  */
#define YYNSTATES  317

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   339

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
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
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     4,     7,    10,    16,    22,    28,    34,
      38,    42,    48,    54,    60,    66,    72,    82,    88,    94,
     100,   104,   110,   114,   120,   126,   130,   136,   142,   146,
     150,   156,   160,   166,   170,   176,   182,   186,   190,   194,
     200,   206,   214,   220,   228,   238,   244,   252,   260,   266,
     272,   280,   286,   294,   302,   308,   314,   318,   324,   330,
     334,   337,   343,   349,   354,   359,   365,   371,   375,   380,
     385,   390,   392,   393,   395,   397,   409,   411,   413,   415,
     417,   421,   423,   427,   429,   431,   435,   438,   440,   442,
     444,   446,   448,   450,   452,   454,   456,   458,   460
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      86,     0,    -1,    -1,    86,    87,    -1,    86,    88,    -1,
      18,    15,    89,    16,   102,    -1,    19,    15,    90,    16,
     102,    -1,    23,    15,    92,    16,   102,    -1,    60,    15,
      83,    16,   102,    -1,    24,    16,   101,    -1,    61,    16,
     101,    -1,    61,    15,    83,    16,   101,    -1,    25,    15,
      94,    16,   102,    -1,    26,    15,    95,    16,   102,    -1,
      27,    15,    96,    16,   102,    -1,    38,    15,    84,    16,
     102,    -1,    38,    15,    84,    15,    12,    15,    84,    16,
     102,    -1,    28,    15,    97,    16,   101,    -1,    29,    15,
      97,    16,   101,    -1,    30,    15,    97,    16,   101,    -1,
      46,    16,   101,    -1,    46,    15,    83,    16,   101,    -1,
      45,    16,   101,    -1,    45,    15,    97,    16,   101,    -1,
      48,    15,    97,    16,   101,    -1,    48,    16,   102,    -1,
      43,    15,    97,    16,   100,    -1,    41,    15,    97,    16,
     100,    -1,    42,    16,   102,    -1,    44,    16,   101,    -1,
      44,    15,    97,    16,   101,    -1,    49,    16,   102,    -1,
      49,    15,    83,    16,   102,    -1,    50,    16,   102,    -1,
      51,    15,    97,    16,   101,    -1,    52,    15,    97,    16,
     100,    -1,    53,    16,   101,    -1,    54,    16,   101,    -1,
      80,    16,   102,    -1,    81,    15,    83,    16,   102,    -1,
      47,    15,    49,    16,   102,    -1,    47,    15,    49,    15,
      83,    16,   102,    -1,    47,    15,    62,    16,   101,    -1,
      47,    15,    62,    15,    99,    16,   100,    -1,    47,    15,
      64,    15,    99,    15,    97,    16,   100,    -1,    47,    15,
      63,    16,   102,    -1,    47,    15,    63,    15,    84,    16,
     102,    -1,    47,    15,    73,    15,    83,    16,   101,    -1,
      47,    15,    74,    16,   101,    -1,    47,    15,    75,    16,
     101,    -1,    47,    15,    76,    15,    83,    16,   101,    -1,
      47,    15,    77,    16,   101,    -1,    47,    15,    77,    15,
      83,    16,   101,    -1,    47,    15,    78,    15,    83,    16,
     101,    -1,    47,    15,    79,    16,   102,    -1,    55,    15,
      97,    16,   101,    -1,    57,    16,   102,    -1,    58,    15,
      97,    16,   101,    -1,    59,    15,    97,    16,   101,    -1,
      22,    16,   102,    -1,     1,    16,    -1,    40,    15,    97,
      16,   100,    -1,    39,    15,    91,    16,   102,    -1,    65,
      15,    83,    16,    -1,    66,    15,    83,    16,    -1,    68,
      15,    84,    16,   102,    -1,    67,    15,    83,    16,   102,
      -1,    69,    16,   102,    -1,    70,    15,    83,    16,    -1,
      71,    15,    83,    16,    -1,    72,    15,    83,    16,    -1,
      83,    -1,    -1,    83,    -1,    84,    -1,    84,    17,    84,
      17,    84,    17,    84,    17,    84,    17,    84,    -1,    10,
      -1,    14,    -1,     5,    -1,     3,    -1,     3,    15,    93,
      -1,     6,    -1,     6,    15,    93,    -1,     8,    -1,     9,
      -1,     9,    15,    91,    -1,     9,    91,    -1,     7,    -1,
      12,    -1,    11,    -1,    13,    -1,     4,    -1,     5,    -1,
      98,    -1,    83,    -1,    84,    -1,   101,    -1,   102,    -1,
      -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   129,   129,   131,   136,   140,   146,   154,   175,   181,
     186,   191,   197,   234,   248,   262,   268,   274,   283,   292,
     301,   306,   315,   320,   326,   333,   338,   345,   359,   364,
     373,   380,   385,   402,   407,   414,   421,   426,   431,   441,
     448,   453,   458,   466,   479,   493,   500,   517,   521,   526,
     530,   534,   545,   558,   565,   570,   577,   595,   612,   640,
     647,   653,   663,   673,   678,   683,   688,   693,   698,   703,
     708,   716,   721,   724,   728,   732,   745,   749,   753,   760,
     765,   770,   775,   780,   784,   789,   795,   803,   807,   811,
     818,   822,   826,   833,   861,   865,   891,   899,   910
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "A", "B", "C", "E", "F", "I", "L", "N",
  "P", "R", "S", "T", "SP", "CRLF", "COMMA", "USER", "PASS", "ACCT",
  "REIN", "QUIT", "PORT", "PASV", "TYPE", "STRU", "MODE", "RETR", "STOR",
  "APPE", "MLFL", "MAIL", "MSND", "MSOM", "MSAM", "MRSQ", "MRCP", "ALLO",
  "REST", "RNFR", "RNTO", "ABOR", "DELE", "CWD", "LIST", "NLST", "SITE",
  "sTAT", "HELP", "NOOP", "MKD", "RMD", "PWD", "CDUP", "STOU", "SMNT",
  "SYST", "SIZE", "MDTM", "EPRT", "EPSV", "UMASK", "IDLE", "CHMOD", "AUTH",
  "ADAT", "PROT", "PBSZ", "CCC", "MIC", "CONF", "ENC", "KAUTH", "KLIST",
  "KDESTROY", "KRBTKFILE", "AFSLOG", "LOCATE", "URL", "FEAT", "OPTS",
  "LEXERR", "STRING", "NUMBER", "$accept", "cmd_list", "cmd", "rcmd",
  "username", "password", "byte_size", "host_port", "form_code",
  "type_code", "struct_code", "mode_code", "pathname", "pathstring",
  "octal_number", "check_login_no_guest", "check_login", "check_secure", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    85,    86,    86,    86,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    87,    87,    87,    87,    87,    87,    87,    87,    87,
      87,    88,    88,    88,    88,    88,    88,    88,    88,    88,
      88,    89,    90,    90,    91,    92,    93,    93,    93,    94,
      94,    94,    94,    94,    94,    94,    94,    95,    95,    95,
      96,    96,    96,    97,    98,    99,   100,   101,   102
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     2,     2,     5,     5,     5,     5,     3,
       3,     5,     5,     5,     5,     5,     9,     5,     5,     5,
       3,     5,     3,     5,     5,     3,     5,     5,     3,     3,
       5,     3,     5,     3,     5,     5,     3,     3,     3,     5,
       5,     7,     5,     7,     9,     5,     7,     7,     5,     5,
       7,     5,     7,     7,     5,     5,     3,     5,     5,     3,
       2,     5,     5,     4,     4,     5,     5,     3,     4,     4,
       4,     1,     0,     1,     1,    11,     1,     1,     1,     1,
       3,     1,     3,     1,     1,     3,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     0
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       2,     0,     1,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     3,     4,
      60,     0,    72,    98,     0,    98,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    98,     0,     0,    98,
       0,    98,     0,    98,     0,     0,    98,     0,    98,    98,
       0,     0,    98,    98,     0,    98,     0,     0,     0,     0,
      98,     0,     0,     0,     0,    98,     0,     0,     0,    98,
       0,    71,     0,    73,     0,    59,     0,     0,     9,    97,
      79,    81,    83,    84,     0,    87,    89,    88,     0,    91,
      92,    90,     0,    94,     0,    93,     0,     0,     0,    74,
       0,     0,     0,    28,     0,     0,    29,     0,    22,     0,
      20,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    25,     0,    31,    33,     0,     0,    36,
      37,     0,    56,     0,     0,     0,     0,    10,     0,     0,
       0,     0,    67,     0,     0,     0,    38,     0,    98,    98,
       0,    98,     0,     0,     0,    86,    98,    98,    98,    98,
      98,    98,     0,    98,    98,    98,    98,    98,    98,    98,
      98,     0,    98,     0,    98,     0,    98,     0,     0,    98,
      98,     0,     0,    98,     0,    98,    98,    98,    98,    98,
      98,    98,    98,    98,    98,    63,    64,    98,    98,    68,
      69,    70,    98,     5,     6,     0,     7,    78,    76,    77,
      80,    82,    85,    12,    13,    14,    17,    18,    19,     0,
      15,    62,    61,    96,    27,    26,    30,    23,    21,     0,
      40,    95,     0,    42,     0,    45,     0,     0,    48,    49,
       0,     0,    51,     0,    54,    24,    32,    34,    35,    55,
      57,    58,     8,    11,    66,    65,    39,     0,     0,    98,
      98,    98,     0,    98,    98,    98,    98,     0,     0,    41,
      43,    46,     0,    47,    50,    52,    53,     0,    98,    98,
       0,    16,    44,     0,     0,     0,    75
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     1,    48,    49,   102,   104,   130,   107,   240,   114,
     118,   122,   124,   125,   262,   252,   253,   109
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -196
static const yytype_int16 yypact[] =
{
    -196,   246,  -196,     3,    13,    20,    11,    24,    21,    26,
      30,    45,    66,    67,    68,    69,    70,    71,    72,    76,
      73,    -7,    -5,    15,    78,    28,    32,    80,    79,    82,
      83,    91,    93,    94,    96,    97,    98,    38,   100,   101,
     102,   103,   104,   106,   107,   108,   111,   109,  -196,  -196,
    -196,   -66,    36,  -196,    14,  -196,    12,    22,     1,    46,
      46,    46,    25,    48,    46,    46,  -196,    46,    46,  -196,
      46,  -196,    53,  -196,    27,    46,  -196,    55,  -196,  -196,
      46,    46,  -196,  -196,    46,  -196,    46,    46,    56,    59,
    -196,    60,    61,    62,    63,  -196,    65,    77,    85,  -196,
      86,  -196,   114,  -196,   115,  -196,   120,   130,  -196,  -196,
     135,   136,  -196,   -11,   138,  -196,  -196,  -196,   139,  -196,
    -196,  -196,   143,  -196,   145,  -196,   147,   156,    47,  -196,
     157,   162,   165,  -196,   166,   168,  -196,   170,  -196,   174,
    -196,    49,    52,    54,   137,   177,   178,   179,   181,    64,
     182,   183,   184,  -196,   185,  -196,  -196,   186,   187,  -196,
    -196,   188,  -196,   189,   190,   191,   192,  -196,   193,   194,
     195,   196,  -196,   197,   198,   199,  -196,   200,  -196,  -196,
     133,  -196,     2,     2,    48,  -196,  -196,  -196,  -196,  -196,
    -196,  -196,   206,  -196,  -196,  -196,  -196,  -196,  -196,  -196,
    -196,   110,  -196,   140,  -196,   141,  -196,   140,   144,  -196,
    -196,   146,   148,  -196,   149,  -196,  -196,  -196,  -196,  -196,
    -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,
    -196,  -196,  -196,  -196,  -196,   202,  -196,  -196,  -196,  -196,
    -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,   205,
    -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,  -196,   207,
    -196,  -196,   210,  -196,   212,  -196,   215,   217,  -196,  -196,
     218,   219,  -196,   221,  -196,  -196,  -196,  -196,  -196,  -196,
    -196,  -196,  -196,  -196,  -196,  -196,  -196,   155,   158,  -196,
    -196,  -196,    46,  -196,  -196,  -196,  -196,   204,   224,  -196,
    -196,  -196,   225,  -196,  -196,  -196,  -196,   159,  -196,  -196,
     227,  -196,  -196,   161,   231,   167,  -196
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -196,  -196,  -196,  -196,  -196,  -196,  -110,  -196,    39,  -196,
    -196,  -196,    -9,  -196,    42,  -195,   -33,   -53
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint16 yytable[] =
{
     105,   254,   255,   185,   184,   119,   120,   237,    68,    69,
      70,    71,   238,   133,   121,   110,   239,   101,   111,    50,
     112,   113,   108,   153,   278,   155,   156,    53,    51,   115,
      72,    73,   162,   116,   117,    52,   136,    55,   138,    54,
     140,    56,   172,    75,    76,    57,   176,    77,    78,   159,
     160,   126,   127,    89,    90,   131,   132,   167,   134,   135,
      58,   137,   192,   193,   201,   202,   152,   203,   204,   205,
     206,   157,   158,   129,   242,   161,   141,   163,   164,   212,
     213,    59,    60,    61,    62,    63,    64,    65,    67,   142,
     143,   144,    66,    74,    80,   300,    79,    81,   106,    82,
     145,   146,   147,   148,   149,   150,   151,    83,    84,   128,
      85,    86,    87,    88,   312,    91,    92,    93,    94,   103,
      95,    96,    97,    98,   100,   233,   234,    99,   236,   123,
     178,   179,   129,   243,   244,   245,   139,   180,   154,   165,
     250,   251,   166,   168,   169,   170,   181,   171,   173,   260,
     182,   183,   207,   265,   186,   187,   246,   247,   248,   188,
     174,   189,   274,   190,   276,   256,   257,   258,   175,   177,
     282,   263,   191,   194,   284,   285,   268,   269,   195,   286,
     272,   196,   197,   275,   198,   277,   199,   279,   280,   281,
     200,   283,   208,   259,   209,   210,   211,   214,     0,   215,
     216,   217,   218,   219,   220,   221,   222,   223,   224,   225,
     226,   227,   228,   229,   230,   231,   232,   235,   249,   287,
     288,   307,   241,   289,   261,   264,   290,   267,   291,   270,
     292,   271,   273,   293,   294,   295,   299,   296,   301,   297,
     308,   309,   298,   310,   313,   314,     2,     3,   315,   266,
       0,   316,     0,     0,     0,   311,     0,     0,     0,     0,
     303,   304,   305,   306,     4,     5,     0,     0,     6,     7,
       8,     9,    10,    11,    12,    13,    14,     0,     0,     0,
       0,     0,     0,   302,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,     0,    33,    34,    35,    36,    37,     0,     0,
       0,    38,    39,    40,    41,    42,    43,    44,    45,     0,
       0,     0,     0,     0,     0,     0,    46,    47
};

static const yytype_int16 yycheck[] =
{
      53,   196,   197,   113,    15,     4,     5,     5,    15,    16,
      15,    16,    10,    66,    13,     3,    14,    83,     6,    16,
       8,     9,    55,    76,   219,    78,    79,    16,    15,     7,
      15,    16,    85,    11,    12,    15,    69,    16,    71,    15,
      73,    15,    95,    15,    16,    15,    99,    15,    16,    82,
      83,    60,    61,    15,    16,    64,    65,    90,    67,    68,
      15,    70,    15,    16,    15,    16,    75,    15,    16,    15,
      16,    80,    81,    84,   184,    84,    49,    86,    87,    15,
      16,    15,    15,    15,    15,    15,    15,    15,    15,    62,
      63,    64,    16,    15,    15,   290,    16,    15,    84,    16,
      73,    74,    75,    76,    77,    78,    79,    16,    15,    84,
      16,    15,    15,    15,   309,    15,    15,    15,    15,    83,
      16,    15,    15,    15,    15,   178,   179,    16,   181,    83,
      16,    16,    84,   186,   187,   188,    83,    17,    83,    83,
     193,   194,    83,    83,    83,    83,    16,    84,    83,   202,
      15,    15,    15,   206,    16,    16,   189,   190,   191,    16,
      83,    16,   215,    16,   217,   198,   199,   200,    83,    83,
     223,   204,    16,    16,   227,   228,   209,   210,    16,   232,
     213,    16,    16,   216,    16,   218,    16,   220,   221,   222,
      16,   224,    15,    83,    16,    16,    15,    15,    -1,    16,
      16,    16,    16,    16,    16,    16,    16,    16,    16,    16,
      16,    16,    16,    16,    16,    16,    16,    84,    12,    17,
      15,    17,   183,    16,    84,    84,    16,    83,    16,    83,
      15,    83,    83,    16,    16,    16,   289,    16,   291,    84,
      16,    16,    84,    84,    17,    84,     0,     1,    17,   207,
      -1,    84,    -1,    -1,    -1,   308,    -1,    -1,    -1,    -1,
     293,   294,   295,   296,    18,    19,    -1,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    -1,    -1,    -1,
      -1,    -1,    -1,   292,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    -1,    57,    58,    59,    60,    61,    -1,    -1,
      -1,    65,    66,    67,    68,    69,    70,    71,    72,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    80,    81
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    86,     0,     1,    18,    19,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    57,    58,    59,    60,    61,    65,    66,
      67,    68,    69,    70,    71,    72,    80,    81,    87,    88,
      16,    15,    15,    16,    15,    16,    15,    15,    15,    15,
      15,    15,    15,    15,    15,    15,    16,    15,    15,    16,
      15,    16,    15,    16,    15,    15,    16,    15,    16,    16,
      15,    15,    16,    16,    15,    16,    15,    15,    15,    15,
      16,    15,    15,    15,    15,    16,    15,    15,    15,    16,
      15,    83,    89,    83,    90,   102,    84,    92,   101,   102,
       3,     6,     8,     9,    94,     7,    11,    12,    95,     4,
       5,    13,    96,    83,    97,    98,    97,    97,    84,    84,
      91,    97,    97,   102,    97,    97,   101,    97,   101,    83,
     101,    49,    62,    63,    64,    73,    74,    75,    76,    77,
      78,    79,    97,   102,    83,   102,   102,    97,    97,   101,
     101,    97,   102,    97,    97,    83,    83,   101,    83,    83,
      83,    84,   102,    83,    83,    83,   102,    83,    16,    16,
      17,    16,    15,    15,    15,    91,    16,    16,    16,    16,
      16,    16,    15,    16,    16,    16,    16,    16,    16,    16,
      16,    15,    16,    15,    16,    15,    16,    15,    15,    16,
      16,    15,    15,    16,    15,    16,    16,    16,    16,    16,
      16,    16,    16,    16,    16,    16,    16,    16,    16,    16,
      16,    16,    16,   102,   102,    84,   102,     5,    10,    14,
      93,    93,    91,   102,   102,   102,   101,   101,   101,    12,
     102,   102,   100,   101,   100,   100,   101,   101,   101,    83,
     102,    84,    99,   101,    84,   102,    99,    83,   101,   101,
      83,    83,   101,    83,   102,   101,   102,   101,   100,   101,
     101,   101,   102,   101,   102,   102,   102,    17,    15,    16,
      16,    16,    15,    16,    16,    16,    16,    84,    84,   102,
     100,   102,    97,   101,   101,   101,   101,    17,    16,    16,
      84,   102,   100,    17,    84,    17,    84
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


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
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
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
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
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
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  YYUSE (yyvaluep);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

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
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
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

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
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
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
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

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

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
#line 132 "ftpcmd.y"
    {
			fromname = (char *) 0;
			restart_point = (off_t) 0;
		}
    break;

  case 5:
#line 141 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			user((yyvsp[(3) - (5)].s));
		    free((yyvsp[(3) - (5)].s));
		}
    break;

  case 6:
#line 147 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			pass((yyvsp[(3) - (5)].s));
		    memset ((yyvsp[(3) - (5)].s), 0, strlen((yyvsp[(3) - (5)].s)));
		    free((yyvsp[(3) - (5)].s));
		}
    break;

  case 7:
#line 155 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			if (paranoid &&
			    (data_dest->sa_family != his_addr->sa_family ||
			     (socket_get_port(data_dest) < IPPORT_RESERVED) ||
			     memcmp(socket_get_address(data_dest),
				    socket_get_address(his_addr),
				    socket_addr_size(his_addr)) != 0)) {
			    usedefault = 1;
			    reply(500, "Illegal PORT range rejected.");
			} else {
			    usedefault = 0;
			    if (pdata >= 0) {
				close(pdata);
				pdata = -1;
			    }
			    reply(200, "PORT command successful.");
			}
		    }
		}
    break;

  case 8:
#line 176 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			eprt ((yyvsp[(3) - (5)].s));
		    free ((yyvsp[(3) - (5)].s));
		}
    break;

  case 9:
#line 182 "ftpcmd.y"
    {
		    if((yyvsp[(3) - (3)].i))
			pasv ();
		}
    break;

  case 10:
#line 187 "ftpcmd.y"
    {
		    if((yyvsp[(3) - (3)].i))
			epsv (NULL);
		}
    break;

  case 11:
#line 192 "ftpcmd.y"
    {
		    if((yyvsp[(5) - (5)].i))
			epsv ((yyvsp[(3) - (5)].s));
		    free ((yyvsp[(3) - (5)].s));
		}
    break;

  case 12:
#line 198 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			switch (cmd_type) {

			case TYPE_A:
				if (cmd_form == FORM_N) {
					reply(200, "Type set to A.");
					type = cmd_type;
					form = cmd_form;
				} else
					reply(504, "Form must be N.");
				break;

			case TYPE_E:
				reply(504, "Type E not implemented.");
				break;

			case TYPE_I:
				reply(200, "Type set to I.");
				type = cmd_type;
				break;

			case TYPE_L:
#if NBBY == 8
				if (cmd_bytesz == 8) {
					reply(200,
					    "Type set to L (byte size 8).");
					type = cmd_type;
				} else
					reply(504, "Byte size must be 8.");
#else /* NBBY == 8 */
				UNIMPLEMENTED for NBBY != 8
#endif /* NBBY == 8 */
			}
		    }
		}
    break;

  case 13:
#line 235 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			switch ((yyvsp[(3) - (5)].i)) {

			case STRU_F:
				reply(200, "STRU F ok.");
				break;

			default:
				reply(504, "Unimplemented STRU type.");
			}
		    }
		}
    break;

  case 14:
#line 249 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			switch ((yyvsp[(3) - (5)].i)) {

			case MODE_S:
				reply(200, "MODE S ok.");
				break;

			default:
				reply(502, "Unimplemented MODE type.");
			}
		    }
		}
    break;

  case 15:
#line 263 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			reply(202, "ALLO command ignored.");
		    }
		}
    break;

  case 16:
#line 269 "ftpcmd.y"
    {
		    if ((yyvsp[(9) - (9)].i)) {
			reply(202, "ALLO command ignored.");
		    }
		}
    break;

  case 17:
#line 275 "ftpcmd.y"
    {
			char *name = (yyvsp[(3) - (5)].s);

			if ((yyvsp[(5) - (5)].i) && name != NULL)
				retrieve(0, name);
			if (name != NULL)
				free(name);
		}
    break;

  case 18:
#line 284 "ftpcmd.y"
    {
			char *name = (yyvsp[(3) - (5)].s);

			if ((yyvsp[(5) - (5)].i) && name != NULL)
				do_store(name, "w", 0);
			if (name != NULL)
				free(name);
		}
    break;

  case 19:
#line 293 "ftpcmd.y"
    {
			char *name = (yyvsp[(3) - (5)].s);

			if ((yyvsp[(5) - (5)].i) && name != NULL)
				do_store(name, "a", 0);
			if (name != NULL)
				free(name);
		}
    break;

  case 20:
#line 302 "ftpcmd.y"
    {
			if ((yyvsp[(3) - (3)].i))
				send_file_list(".");
		}
    break;

  case 21:
#line 307 "ftpcmd.y"
    {
			char *name = (yyvsp[(3) - (5)].s);

			if ((yyvsp[(5) - (5)].i) && name != NULL)
				send_file_list(name);
			if (name != NULL)
				free(name);
		}
    break;

  case 22:
#line 316 "ftpcmd.y"
    {
		    if((yyvsp[(3) - (3)].i))
			list_file(".");
		}
    break;

  case 23:
#line 321 "ftpcmd.y"
    {
		    if((yyvsp[(5) - (5)].i))
			list_file((yyvsp[(3) - (5)].s));
		    free((yyvsp[(3) - (5)].s));
		}
    break;

  case 24:
#line 327 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				statfilecmd((yyvsp[(3) - (5)].s));
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 25:
#line 334 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i))
			statcmd();
		}
    break;

  case 26:
#line 339 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				do_delete((yyvsp[(3) - (5)].s));
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 27:
#line 346 "ftpcmd.y"
    {
			if((yyvsp[(5) - (5)].i)){
				if (fromname) {
					renamecmd(fromname, (yyvsp[(3) - (5)].s));
					free(fromname);
					fromname = (char *) 0;
				} else {
					reply(503, "Bad sequence of commands.");
				}
			}
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 28:
#line 360 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i))
			reply(225, "ABOR command successful.");
		}
    break;

  case 29:
#line 365 "ftpcmd.y"
    {
			if ((yyvsp[(3) - (3)].i)) {
				const char *path = pw->pw_dir;
				if (dochroot || guest)
					path = "/";
				cwd(path);
			}
		}
    break;

  case 30:
#line 374 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				cwd((yyvsp[(3) - (5)].s));
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 31:
#line 381 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i))
			help(cmdtab, (char *) 0);
		}
    break;

  case 32:
#line 386 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			char *cp = (yyvsp[(3) - (5)].s);

			if (strncasecmp(cp, "SITE", 4) == 0) {
				cp = (yyvsp[(3) - (5)].s) + 4;
				if (*cp == ' ')
					cp++;
				if (*cp)
					help(sitetab, cp);
				else
					help(sitetab, (char *) 0);
			} else
				help(cmdtab, (yyvsp[(3) - (5)].s));
		    }
		}
    break;

  case 33:
#line 403 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i))
			reply(200, "NOOP command successful.");
		}
    break;

  case 34:
#line 408 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				makedir((yyvsp[(3) - (5)].s));
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 35:
#line 415 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				removedir((yyvsp[(3) - (5)].s));
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 36:
#line 422 "ftpcmd.y"
    {
			if ((yyvsp[(3) - (3)].i))
				pwd();
		}
    break;

  case 37:
#line 427 "ftpcmd.y"
    {
			if ((yyvsp[(3) - (3)].i))
				cwd("..");
		}
    break;

  case 38:
#line 432 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i)) {
			lreply(211, "Supported features:");
			lreply(0, " MDTM");
			lreply(0, " REST STREAM");
			lreply(0, " SIZE");
			reply(211, "End");
		    }
		}
    break;

  case 39:
#line 442 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			reply(501, "Bad options");
		    free ((yyvsp[(3) - (5)].s));
		}
    break;

  case 40:
#line 449 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			help(sitetab, (char *) 0);
		}
    break;

  case 41:
#line 454 "ftpcmd.y"
    {
		    if ((yyvsp[(7) - (7)].i))
			help(sitetab, (yyvsp[(5) - (7)].s));
		}
    break;

  case 42:
#line 459 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i)) {
				int oldmask = umask(0);
				umask(oldmask);
				reply(200, "Current UMASK is %03o", oldmask);
			}
		}
    break;

  case 43:
#line 467 "ftpcmd.y"
    {
			if ((yyvsp[(7) - (7)].i)) {
				if (((yyvsp[(5) - (7)].i) == -1) || ((yyvsp[(5) - (7)].i) > 0777)) {
					reply(501, "Bad UMASK value");
				} else {
					int oldmask = umask((yyvsp[(5) - (7)].i));
					reply(200,
					      "UMASK set to %03o (was %03o)",
					      (yyvsp[(5) - (7)].i), oldmask);
				}
			}
		}
    break;

  case 44:
#line 480 "ftpcmd.y"
    {
			if ((yyvsp[(9) - (9)].i) && (yyvsp[(7) - (9)].s) != NULL) {
				if ((yyvsp[(5) - (9)].i) > 0777)
					reply(501,
				"CHMOD: Mode value must be between 0 and 0777");
				else if (chmod((yyvsp[(7) - (9)].s), (yyvsp[(5) - (9)].i)) < 0)
					perror_reply(550, (yyvsp[(7) - (9)].s));
				else
					reply(200, "CHMOD command successful.");
			}
			if ((yyvsp[(7) - (9)].s) != NULL)
				free((yyvsp[(7) - (9)].s));
		}
    break;

  case 45:
#line 494 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			reply(200,
			    "Current IDLE time limit is %d seconds; max %d",
				ftpd_timeout, maxtimeout);
		}
    break;

  case 46:
#line 501 "ftpcmd.y"
    {
		    if ((yyvsp[(7) - (7)].i)) {
			if ((yyvsp[(5) - (7)].i) < 30 || (yyvsp[(5) - (7)].i) > maxtimeout) {
				reply(501,
			"Maximum IDLE time must be between 30 and %d seconds",
				    maxtimeout);
			} else {
				ftpd_timeout = (yyvsp[(5) - (7)].i);
				alarm((unsigned) ftpd_timeout);
				reply(200,
				    "Maximum IDLE time set to %d seconds",
				    ftpd_timeout);
			}
		    }
		}
    break;

  case 47:
#line 518 "ftpcmd.y"
    {
			reply(500, "Command not implemented.");
		}
    break;

  case 48:
#line 522 "ftpcmd.y"
    {
		    if((yyvsp[(5) - (5)].i))
			klist();
		}
    break;

  case 49:
#line 527 "ftpcmd.y"
    {
		    reply(500, "Command not implemented.");
		}
    break;

  case 50:
#line 531 "ftpcmd.y"
    {
		    reply(500, "Command not implemented.");
		}
    break;

  case 51:
#line 535 "ftpcmd.y"
    {
#if defined(KRB5)
		    if(guest)
			reply(500, "Can't be done as guest.");
		    else if((yyvsp[(5) - (5)].i))
			afslog(NULL, 0);
#else
		    reply(500, "Command not implemented.");
#endif
		}
    break;

  case 52:
#line 546 "ftpcmd.y"
    {
#if defined(KRB5)
		    if(guest)
			reply(500, "Can't be done as guest.");
		    else if((yyvsp[(7) - (7)].i))
			afslog((yyvsp[(5) - (7)].s), 0);
		    if((yyvsp[(5) - (7)].s))
			free((yyvsp[(5) - (7)].s));
#else
		    reply(500, "Command not implemented.");
#endif
		}
    break;

  case 53:
#line 559 "ftpcmd.y"
    {
		    if((yyvsp[(7) - (7)].i) && (yyvsp[(5) - (7)].s) != NULL)
			find((yyvsp[(5) - (7)].s));
		    if((yyvsp[(5) - (7)].s) != NULL)
			free((yyvsp[(5) - (7)].s));
		}
    break;

  case 54:
#line 566 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			reply(200, "http://www.pdc.kth.se/heimdal/");
		}
    break;

  case 55:
#line 571 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				do_store((yyvsp[(3) - (5)].s), "w", 1);
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 56:
#line 578 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i)) {
#if !defined(WIN32) && !defined(__EMX__) && !defined(__OS2__) && !defined(__CYGWIN32__)
			reply(215, "UNIX Type: L%d", NBBY);
#else
			reply(215, "UNKNOWN Type: L%d", NBBY);
#endif
		    }
		}
    break;

  case 57:
#line 596 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL)
				sizecmd((yyvsp[(3) - (5)].s));
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 58:
#line 613 "ftpcmd.y"
    {
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s) != NULL) {
				struct stat stbuf;
				if (stat((yyvsp[(3) - (5)].s), &stbuf) < 0)
					reply(550, "%s: %s",
					    (yyvsp[(3) - (5)].s), strerror(errno));
				else if (!S_ISREG(stbuf.st_mode)) {
					reply(550,
					      "%s: not a plain file.", (yyvsp[(3) - (5)].s));
				} else {
					struct tm *t;
					time_t mtime = stbuf.st_mtime;

					t = gmtime(&mtime);
					reply(213,
					      "%04d%02d%02d%02d%02d%02d",
					      t->tm_year + 1900,
					      t->tm_mon + 1,
					      t->tm_mday,
					      t->tm_hour,
					      t->tm_min,
					      t->tm_sec);
				}
			}
			if ((yyvsp[(3) - (5)].s) != NULL)
				free((yyvsp[(3) - (5)].s));
		}
    break;

  case 59:
#line 641 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i)) {
			reply(221, "Goodbye.");
			dologout(0);
		    }
		}
    break;

  case 60:
#line 648 "ftpcmd.y"
    {
			yyerrok;
		}
    break;

  case 61:
#line 654 "ftpcmd.y"
    {
			restart_point = (off_t) 0;
			if ((yyvsp[(5) - (5)].i) && (yyvsp[(3) - (5)].s)) {
				fromname = renamefrom((yyvsp[(3) - (5)].s));
				if (fromname == (char *) 0 && (yyvsp[(3) - (5)].s)) {
					free((yyvsp[(3) - (5)].s));
				}
			}
		}
    break;

  case 62:
#line 664 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i)) {
			fromname = (char *) 0;
			restart_point = (yyvsp[(3) - (5)].i);	/* XXX $3 is only "int" */
			reply(350, "Restarting at %ld. %s",
			      (long)restart_point,
			      "Send STORE or RETRIEVE to initiate transfer.");
		    }
		}
    break;

  case 63:
#line 674 "ftpcmd.y"
    {
			auth((yyvsp[(3) - (4)].s));
			free((yyvsp[(3) - (4)].s));
		}
    break;

  case 64:
#line 679 "ftpcmd.y"
    {
			adat((yyvsp[(3) - (4)].s));
			free((yyvsp[(3) - (4)].s));
		}
    break;

  case 65:
#line 684 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			pbsz((yyvsp[(3) - (5)].i));
		}
    break;

  case 66:
#line 689 "ftpcmd.y"
    {
		    if ((yyvsp[(5) - (5)].i))
			prot((yyvsp[(3) - (5)].s));
		}
    break;

  case 67:
#line 694 "ftpcmd.y"
    {
		    if ((yyvsp[(3) - (3)].i))
			ccc();
		}
    break;

  case 68:
#line 699 "ftpcmd.y"
    {
			mec((yyvsp[(3) - (4)].s), prot_safe);
			free((yyvsp[(3) - (4)].s));
		}
    break;

  case 69:
#line 704 "ftpcmd.y"
    {
			mec((yyvsp[(3) - (4)].s), prot_confidential);
			free((yyvsp[(3) - (4)].s));
		}
    break;

  case 70:
#line 709 "ftpcmd.y"
    {
			mec((yyvsp[(3) - (4)].s), prot_private);
			free((yyvsp[(3) - (4)].s));
		}
    break;

  case 72:
#line 721 "ftpcmd.y"
    {
			(yyval.s) = (char *)calloc(1, sizeof(char));
		}
    break;

  case 75:
#line 734 "ftpcmd.y"
    {
			struct sockaddr_in *sin4 = (struct sockaddr_in *)data_dest;

			sin4->sin_family = AF_INET;
			sin4->sin_port = htons((yyvsp[(9) - (11)].i) * 256 + (yyvsp[(11) - (11)].i));
			sin4->sin_addr.s_addr =
			    htonl(((yyvsp[(1) - (11)].i) << 24) | ((yyvsp[(3) - (11)].i) << 16) | ((yyvsp[(5) - (11)].i) << 8) | (yyvsp[(7) - (11)].i));
		}
    break;

  case 76:
#line 746 "ftpcmd.y"
    {
			(yyval.i) = FORM_N;
		}
    break;

  case 77:
#line 750 "ftpcmd.y"
    {
			(yyval.i) = FORM_T;
		}
    break;

  case 78:
#line 754 "ftpcmd.y"
    {
			(yyval.i) = FORM_C;
		}
    break;

  case 79:
#line 761 "ftpcmd.y"
    {
			cmd_type = TYPE_A;
			cmd_form = FORM_N;
		}
    break;

  case 80:
#line 766 "ftpcmd.y"
    {
			cmd_type = TYPE_A;
			cmd_form = (yyvsp[(3) - (3)].i);
		}
    break;

  case 81:
#line 771 "ftpcmd.y"
    {
			cmd_type = TYPE_E;
			cmd_form = FORM_N;
		}
    break;

  case 82:
#line 776 "ftpcmd.y"
    {
			cmd_type = TYPE_E;
			cmd_form = (yyvsp[(3) - (3)].i);
		}
    break;

  case 83:
#line 781 "ftpcmd.y"
    {
			cmd_type = TYPE_I;
		}
    break;

  case 84:
#line 785 "ftpcmd.y"
    {
			cmd_type = TYPE_L;
			cmd_bytesz = NBBY;
		}
    break;

  case 85:
#line 790 "ftpcmd.y"
    {
			cmd_type = TYPE_L;
			cmd_bytesz = (yyvsp[(3) - (3)].i);
		}
    break;

  case 86:
#line 796 "ftpcmd.y"
    {
			cmd_type = TYPE_L;
			cmd_bytesz = (yyvsp[(2) - (2)].i);
		}
    break;

  case 87:
#line 804 "ftpcmd.y"
    {
			(yyval.i) = STRU_F;
		}
    break;

  case 88:
#line 808 "ftpcmd.y"
    {
			(yyval.i) = STRU_R;
		}
    break;

  case 89:
#line 812 "ftpcmd.y"
    {
			(yyval.i) = STRU_P;
		}
    break;

  case 90:
#line 819 "ftpcmd.y"
    {
			(yyval.i) = MODE_S;
		}
    break;

  case 91:
#line 823 "ftpcmd.y"
    {
			(yyval.i) = MODE_B;
		}
    break;

  case 92:
#line 827 "ftpcmd.y"
    {
			(yyval.i) = MODE_C;
		}
    break;

  case 93:
#line 834 "ftpcmd.y"
    {
			/*
			 * Problem: this production is used for all pathname
			 * processing, but only gives a 550 error reply.
			 * This is a valid reply in some cases but not in others.
			 */
			if (logged_in && (yyvsp[(1) - (1)].s) && *(yyvsp[(1) - (1)].s) == '~') {
				glob_t gl;
				int flags =
				 GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE;

				memset(&gl, 0, sizeof(gl));
				if (glob((yyvsp[(1) - (1)].s), flags, NULL, &gl) ||
				    gl.gl_pathc == 0) {
					reply(550, "not found");
					(yyval.s) = NULL;
				} else {
					(yyval.s) = strdup(gl.gl_pathv[0]);
				}
				globfree(&gl);
				free((yyvsp[(1) - (1)].s));
			} else
				(yyval.s) = (yyvsp[(1) - (1)].s);
		}
    break;

  case 95:
#line 866 "ftpcmd.y"
    {
			int ret, dec, multby, digit;

			/*
			 * Convert a number that was read as decimal number
			 * to what it would be if it had been read as octal.
			 */
			dec = (yyvsp[(1) - (1)].i);
			multby = 1;
			ret = 0;
			while (dec) {
				digit = dec%10;
				if (digit > 7) {
					ret = -1;
					break;
				}
				ret += digit * multby;
				multby *= 8;
				dec /= 10;
			}
			(yyval.i) = ret;
		}
    break;

  case 96:
#line 892 "ftpcmd.y"
    {
			(yyval.i) = (yyvsp[(1) - (1)].i) && !guest;
			if((yyvsp[(1) - (1)].i) && !(yyval.i))
				reply(550, "Permission denied");
		}
    break;

  case 97:
#line 900 "ftpcmd.y"
    {
		    if((yyvsp[(1) - (1)].i)) {
			if(((yyval.i) = logged_in) == 0)
			    reply(530, "Please login with USER and PASS.");
		    } else
			(yyval.i) = 0;
		}
    break;

  case 98:
#line 910 "ftpcmd.y"
    {
		    (yyval.i) = 1;
		    if(sec_complete && !ccc_passed && !secure_command()) {
			(yyval.i) = 0;
			reply(533, "Command protection level denied "
			      "for paranoid reasons.");
		    }
		}
    break;


/* Line 1267 of yacc.c.  */
#line 2759 "ftpcmd.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
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
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
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


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

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
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 920 "ftpcmd.y"


#define	CMD	0	/* beginning of command */
#define	ARGS	1	/* expect miscellaneous arguments */
#define	STR1	2	/* expect SP followed by STRING */
#define	STR2	3	/* expect STRING */
#define	OSTR	4	/* optional SP then STRING */
#define	ZSTR1	5	/* SP then optional STRING */
#define	ZSTR2	6	/* optional STRING after SP */
#define	SITECMD	7	/* SITE command */
#define	NSTR	8	/* Number followed by a string */

struct tab cmdtab[] = {		/* In order defined in RFC 765 */
	{ "USER", USER, STR1, 1,	"<sp> username" },
	{ "PASS", PASS, ZSTR1, 1,	"<sp> password" },
	{ "ACCT", ACCT, STR1, 0,	"(specify account)" },
	{ "SMNT", SMNT, ARGS, 0,	"(structure mount)" },
	{ "REIN", REIN, ARGS, 0,	"(reinitialize server state)" },
	{ "QUIT", QUIT, ARGS, 1,	"(terminate service)", },
	{ "PORT", PORT, ARGS, 1,	"<sp> b0, b1, b2, b3, b4" },
	{ "EPRT", EPRT, STR1, 1,	"<sp> string" },
	{ "PASV", PASV, ARGS, 1,	"(set server in passive mode)" },
	{ "EPSV", EPSV, OSTR, 1,	"[<sp> foo]" },
	{ "TYPE", TYPE, ARGS, 1,	"<sp> [ A | E | I | L ]" },
	{ "STRU", STRU, ARGS, 1,	"(specify file structure)" },
	{ "MODE", MODE, ARGS, 1,	"(specify transfer mode)" },
	{ "RETR", RETR, STR1, 1,	"<sp> file-name" },
	{ "STOR", STOR, STR1, 1,	"<sp> file-name" },
	{ "APPE", APPE, STR1, 1,	"<sp> file-name" },
	{ "MLFL", MLFL, OSTR, 0,	"(mail file)" },
	{ "MAIL", MAIL, OSTR, 0,	"(mail to user)" },
	{ "MSND", MSND, OSTR, 0,	"(mail send to terminal)" },
	{ "MSOM", MSOM, OSTR, 0,	"(mail send to terminal or mailbox)" },
	{ "MSAM", MSAM, OSTR, 0,	"(mail send to terminal and mailbox)" },
	{ "MRSQ", MRSQ, OSTR, 0,	"(mail recipient scheme question)" },
	{ "MRCP", MRCP, STR1, 0,	"(mail recipient)" },
	{ "ALLO", ALLO, ARGS, 1,	"allocate storage (vacuously)" },
	{ "REST", REST, ARGS, 1,	"<sp> offset (restart command)" },
	{ "RNFR", RNFR, STR1, 1,	"<sp> file-name" },
	{ "RNTO", RNTO, STR1, 1,	"<sp> file-name" },
	{ "ABOR", ABOR, ARGS, 1,	"(abort operation)" },
	{ "DELE", DELE, STR1, 1,	"<sp> file-name" },
	{ "CWD",  CWD,  OSTR, 1,	"[ <sp> directory-name ]" },
	{ "XCWD", CWD,	OSTR, 1,	"[ <sp> directory-name ]" },
	{ "LIST", LIST, OSTR, 1,	"[ <sp> path-name ]" },
	{ "NLST", NLST, OSTR, 1,	"[ <sp> path-name ]" },
	{ "SITE", SITE, SITECMD, 1,	"site-cmd [ <sp> arguments ]" },
	{ "SYST", SYST, ARGS, 1,	"(get type of operating system)" },
	{ "STAT", sTAT, OSTR, 1,	"[ <sp> path-name ]" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },
	{ "NOOP", NOOP, ARGS, 1,	"" },
	{ "MKD",  MKD,  STR1, 1,	"<sp> path-name" },
	{ "XMKD", MKD,  STR1, 1,	"<sp> path-name" },
	{ "RMD",  RMD,  STR1, 1,	"<sp> path-name" },
	{ "XRMD", RMD,  STR1, 1,	"<sp> path-name" },
	{ "PWD",  PWD,  ARGS, 1,	"(return current directory)" },
	{ "XPWD", PWD,  ARGS, 1,	"(return current directory)" },
	{ "CDUP", CDUP, ARGS, 1,	"(change to parent directory)" },
	{ "XCUP", CDUP, ARGS, 1,	"(change to parent directory)" },
	{ "STOU", STOU, STR1, 1,	"<sp> file-name" },
	{ "SIZE", SIZE, OSTR, 1,	"<sp> path-name" },
	{ "MDTM", MDTM, OSTR, 1,	"<sp> path-name" },

	/* extensions from RFC2228 */
	{ "AUTH", AUTH,	STR1, 1,	"<sp> auth-type" },
	{ "ADAT", ADAT,	STR1, 1,	"<sp> auth-data" },
	{ "PBSZ", PBSZ,	ARGS, 1,	"<sp> buffer-size" },
	{ "PROT", PROT,	STR1, 1,	"<sp> prot-level" },
	{ "CCC",  CCC,	ARGS, 1,	"" },
	{ "MIC",  MIC,	STR1, 1,	"<sp> integrity command" },
	{ "CONF", CONF,	STR1, 1,	"<sp> confidentiality command" },
	{ "ENC",  ENC,	STR1, 1,	"<sp> privacy command" },

	/* RFC2389 */
	{ "FEAT", FEAT, ARGS, 1,	"" },
	{ "OPTS", OPTS, ARGS, 1,	"<sp> command [<sp> options]" },

	{ NULL,   0,    0,    0,	0 }
};

struct tab sitetab[] = {
	{ "UMASK", UMASK, ARGS, 1,	"[ <sp> umask ]" },
	{ "IDLE", IDLE, ARGS, 1,	"[ <sp> maximum-idle-time ]" },
	{ "CHMOD", CHMOD, NSTR, 1,	"<sp> mode <sp> file-name" },
	{ "HELP", HELP, OSTR, 1,	"[ <sp> <string> ]" },

	{ "KAUTH", KAUTH, STR1, 1,	"<sp> principal [ <sp> ticket ]" },
	{ "KLIST", KLIST, ARGS, 1,	"(show ticket file)" },
	{ "KDESTROY", KDESTROY, ARGS, 1, "(destroy tickets)" },
	{ "KRBTKFILE", KRBTKFILE, STR1, 1, "<sp> ticket-file" },
	{ "AFSLOG", AFSLOG, OSTR, 1,	"[<sp> cell]" },

	{ "LOCATE", LOCATE, STR1, 1,	"<sp> globexpr" },
	{ "FIND", LOCATE, STR1, 1,	"<sp> globexpr" },

	{ "URL",  URL,  ARGS, 1,	"?" },

	{ NULL,   0,    0,    0,	0 }
};

static struct tab *
lookup(struct tab *p, char *cmd)
{

	for (; p->name != NULL; p++)
		if (strcmp(cmd, p->name) == 0)
			return (p);
	return (0);
}

/*
 * ftpd_getline - a hacked up version of fgets to ignore TELNET escape codes.
 */
char *
ftpd_getline(char *s, int n)
{
	int c;
	char *cs;

	cs = s;

	/* might still be data within the security MIC/CONF/ENC */
	if(ftp_command){
	    strlcpy(s, ftp_command, n);
	    if (debug)
		syslog(LOG_DEBUG, "command: %s", s);
	    return s;
	}
	while ((c = getc(stdin)) != EOF) {
		c &= 0377;
		if (c == IAC) {
		    if ((c = getc(stdin)) != EOF) {
			c &= 0377;
			switch (c) {
			case WILL:
			case WONT:
				c = getc(stdin);
				printf("%c%c%c", IAC, DONT, 0377&c);
				fflush(stdout);
				continue;
			case DO:
			case DONT:
				c = getc(stdin);
				printf("%c%c%c", IAC, WONT, 0377&c);
				fflush(stdout);
				continue;
			case IAC:
				break;
			default:
				continue;	/* ignore command */
			}
		    }
		}
		*cs++ = c;
		if (--n <= 0 || c == '\n')
			break;
	}
	if (c == EOF && cs == s)
		return (NULL);
	*cs++ = '\0';
	if (debug) {
		if (!guest && strncasecmp("pass ", s, 5) == 0) {
			/* Don't syslog passwords */
			syslog(LOG_DEBUG, "command: %.5s ???", s);
		} else {
			char *cp;
			int len;

			/* Don't syslog trailing CR-LF */
			len = strlen(s);
			cp = s + len - 1;
			while (cp >= s && (*cp == '\n' || *cp == '\r')) {
				--cp;
				--len;
			}
			syslog(LOG_DEBUG, "command: %.*s", len, s);
		}
	}
#ifdef XXX
	fprintf(stderr, "%s\n", s);
#endif
	return (s);
}

static RETSIGTYPE
toolong(int signo)
{

	reply(421,
	    "Timeout (%d seconds): closing control connection.",
	      ftpd_timeout);
	if (logging)
		syslog(LOG_INFO, "User %s timed out after %d seconds",
		    (pw ? pw -> pw_name : "unknown"), ftpd_timeout);
	dologout(1);
	SIGRETURN(0);
}

static int
yylex(void)
{
	static int cpos, state;
	char *cp, *cp2;
	struct tab *p;
	int n;
	char c;

	for (;;) {
		switch (state) {

		case CMD:
			hasyyerrored = 0;

			signal(SIGALRM, toolong);
			alarm((unsigned) ftpd_timeout);
			if (ftpd_getline(cbuf, sizeof(cbuf)-1) == NULL) {
				reply(221, "You could at least say goodbye.");
				dologout(0);
			}
			alarm(0);
#ifdef HAVE_SETPROCTITLE
			if (strncasecmp(cbuf, "PASS", 4) != 0)
				setproctitle("%s: %s", proctitle, cbuf);
#endif /* HAVE_SETPROCTITLE */
			if ((cp = strchr(cbuf, '\r'))) {
				*cp++ = '\n';
				*cp = '\0';
			}
			if ((cp = strpbrk(cbuf, " \n")))
				cpos = cp - cbuf;
			if (cpos == 0)
				cpos = 4;
			c = cbuf[cpos];
			cbuf[cpos] = '\0';
			strupr(cbuf);
			p = lookup(cmdtab, cbuf);
			cbuf[cpos] = c;
			if (p != 0) {
				if (p->implemented == 0) {
					nack(p->name);
					hasyyerrored = 1;
					break;
				}
				state = p->state;
				yylval.s = p->name;
				return (p->token);
			}
			break;

		case SITECMD:
			if (cbuf[cpos] == ' ') {
				cpos++;
				return (SP);
			}
			cp = &cbuf[cpos];
			if ((cp2 = strpbrk(cp, " \n")))
				cpos = cp2 - cbuf;
			c = cbuf[cpos];
			cbuf[cpos] = '\0';
			strupr(cp);
			p = lookup(sitetab, cp);
			cbuf[cpos] = c;
			if (p != 0) {
				if (p->implemented == 0) {
					state = CMD;
					nack(p->name);
					hasyyerrored = 1;
					break;
				}
				state = p->state;
				yylval.s = p->name;
				return (p->token);
			}
			state = CMD;
			break;

		case OSTR:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR1:
		case ZSTR1:
		dostr1:
			if (cbuf[cpos] == ' ') {
				cpos++;
				if(state == OSTR)
				    state = STR2;
				else
				    state++;
				return (SP);
			}
			break;

		case ZSTR2:
			if (cbuf[cpos] == '\n') {
				state = CMD;
				return (CRLF);
			}
			/* FALLTHROUGH */

		case STR2:
			cp = &cbuf[cpos];
			n = strlen(cp);
			cpos += n - 1;
			/*
			 * Make sure the string is nonempty and \n terminated.
			 */
			if (n > 1 && cbuf[cpos] == '\n') {
				cbuf[cpos] = '\0';
				yylval.s = copy(cp);
				cbuf[cpos] = '\n';
				state = ARGS;
				return (STRING);
			}
			break;

		case NSTR:
			if (cbuf[cpos] == ' ') {
				cpos++;
				return (SP);
			}
			if (isdigit((unsigned char)cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit((unsigned char)cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.i = atoi(cp);
				cbuf[cpos] = c;
				state = STR1;
				return (NUMBER);
			}
			state = STR1;
			goto dostr1;

		case ARGS:
			if (isdigit((unsigned char)cbuf[cpos])) {
				cp = &cbuf[cpos];
				while (isdigit((unsigned char)cbuf[++cpos]))
					;
				c = cbuf[cpos];
				cbuf[cpos] = '\0';
				yylval.i = atoi(cp);
				cbuf[cpos] = c;
				return (NUMBER);
			}
			switch (cbuf[cpos++]) {

			case '\n':
				state = CMD;
				return (CRLF);

			case ' ':
				return (SP);

			case ',':
				return (COMMA);

			case 'A':
			case 'a':
				return (A);

			case 'B':
			case 'b':
				return (B);

			case 'C':
			case 'c':
				return (C);

			case 'E':
			case 'e':
				return (E);

			case 'F':
			case 'f':
				return (F);

			case 'I':
			case 'i':
				return (I);

			case 'L':
			case 'l':
				return (L);

			case 'N':
			case 'n':
				return (N);

			case 'P':
			case 'p':
				return (P);

			case 'R':
			case 'r':
				return (R);

			case 'S':
			case 's':
				return (S);

			case 'T':
			case 't':
				return (T);

			}
			break;

		default:
			fatal("Unknown state in scanner.");
		}
		yyerror(NULL);
		state = CMD;
		return (0);
	}
}

/* ARGSUSED */
void
yyerror(char *s)
{
	char *cp;

	if (hasyyerrored)
	    return;

	if ((cp = strchr(cbuf,'\n')))
		*cp = '\0';
	reply(500, "'%s': command not understood.", cbuf);
	hasyyerrored = 1;
}

static char *
copy(char *s)
{
	char *p;

	p = strdup(s);
	if (p == NULL)
		fatal("Ran out of memory.");
	return p;
}

static void
help(struct tab *ctab, char *s)
{
	struct tab *c;
	int width, NCMDS;
	char *t;
	char buf[1024];

	if (ctab == sitetab)
		t = "SITE ";
	else
		t = "";
	width = 0, NCMDS = 0;
	for (c = ctab; c->name != NULL; c++) {
		int len = strlen(c->name);

		if (len > width)
			width = len;
		NCMDS++;
	}
	width = (width + 8) &~ 7;
	if (s == 0) {
		int i, j, w;
		int columns, lines;

		lreply(214, "The following %scommands are recognized %s.",
		    t, "(* =>'s unimplemented)");
		columns = 76 / width;
		if (columns == 0)
			columns = 1;
		lines = (NCMDS + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
		    strlcpy (buf, "   ", sizeof(buf));
		    for (j = 0; j < columns; j++) {
			c = ctab + j * lines + i;
			snprintf (buf + strlen(buf),
				  sizeof(buf) - strlen(buf),
				  "%s%c",
				  c->name,
				  c->implemented ? ' ' : '*');
			if (c + lines >= &ctab[NCMDS])
			    break;
			w = strlen(c->name) + 1;
			while (w < width) {
			    strlcat (buf,
					     " ",
					     sizeof(buf));
			    w++;
			}
		    }
		    lreply(214, "%s", buf);
		}
		reply(214, "Direct comments to kth-krb-bugs@pdc.kth.se");
		return;
	}
	strupr(s);
	c = lookup(ctab, s);
	if (c == (struct tab *)0) {
		reply(502, "Unknown command %s.", s);
		return;
	}
	if (c->implemented)
		reply(214, "Syntax: %s%s %s", t, c->name, c->help);
	else
		reply(214, "%s%-*s\t%s; unimplemented.", t, width,
		    c->name, c->help);
}

static void
sizecmd(char *filename)
{
	switch (type) {
	case TYPE_L:
	case TYPE_I: {
		struct stat stbuf;
		if (stat(filename, &stbuf) < 0 || !S_ISREG(stbuf.st_mode))
			reply(550, "%s: not a plain file.", filename);
		else
			reply(213, "%lu", (unsigned long)stbuf.st_size);
		break;
	}
	case TYPE_A: {
		FILE *fin;
		int c;
		size_t count;
		struct stat stbuf;
		fin = fopen(filename, "r");
		if (fin == NULL) {
			perror_reply(550, filename);
			return;
		}
		if (fstat(fileno(fin), &stbuf) < 0 || !S_ISREG(stbuf.st_mode)) {
			reply(550, "%s: not a plain file.", filename);
			fclose(fin);
			return;
		}

		count = 0;
		while((c=getc(fin)) != EOF) {
			if (c == '\n')	/* will get expanded to \r\n */
				count++;
			count++;
		}
		fclose(fin);

		reply(213, "%lu", (unsigned long)count);
		break;
	}
	default:
		reply(504, "SIZE not implemented for Type %c.", "?AEIL"[type]);
	}
}

