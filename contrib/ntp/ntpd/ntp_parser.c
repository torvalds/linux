/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

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
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */
#line 11 "ntp_parser.y" /* yacc.c:339  */

  #ifdef HAVE_CONFIG_H
  # include <config.h>
  #endif

  #include "ntp.h"
  #include "ntpd.h"
  #include "ntp_machine.h"
  #include "ntp_stdlib.h"
  #include "ntp_filegen.h"
  #include "ntp_scanner.h"
  #include "ntp_config.h"
  #include "ntp_crypto.h"
  #include "ntp_calendar.h"

  #include "ntpsim.h"		/* HMS: Do we really want this all the time? */
				/* SK: It might be a good idea to always
				   include the simulator code. That way
				   someone can use the same configuration file
				   for both the simulator and the daemon
				*/

  #define YYMALLOC	emalloc
  #define YYFREE	free
  #define YYERROR_VERBOSE
  #define YYMAXDEPTH	1000	/* stop the madness sooner */
  void yyerror(const char *msg);

  #ifdef SIM
  #  define ONLY_SIM(a)	(a)
  #else
  #  define ONLY_SIM(a)	NULL
  #endif

#line 101 "ntp_parser.c" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "y.tab.h".  */
#ifndef YY_YY_NTP_PARSER_H_INCLUDED
# define YY_YY_NTP_PARSER_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    T_Abbrev = 258,
    T_Age = 259,
    T_All = 260,
    T_Allan = 261,
    T_Allpeers = 262,
    T_Auth = 263,
    T_Autokey = 264,
    T_Automax = 265,
    T_Average = 266,
    T_Basedate = 267,
    T_Bclient = 268,
    T_Bcpollbstep = 269,
    T_Beacon = 270,
    T_Broadcast = 271,
    T_Broadcastclient = 272,
    T_Broadcastdelay = 273,
    T_Burst = 274,
    T_Calibrate = 275,
    T_Ceiling = 276,
    T_Clockstats = 277,
    T_Cohort = 278,
    T_ControlKey = 279,
    T_Crypto = 280,
    T_Cryptostats = 281,
    T_Ctl = 282,
    T_Day = 283,
    T_Default = 284,
    T_Digest = 285,
    T_Disable = 286,
    T_Discard = 287,
    T_Dispersion = 288,
    T_Double = 289,
    T_Driftfile = 290,
    T_Drop = 291,
    T_Dscp = 292,
    T_Ellipsis = 293,
    T_Enable = 294,
    T_End = 295,
    T_Epeer = 296,
    T_False = 297,
    T_File = 298,
    T_Filegen = 299,
    T_Filenum = 300,
    T_Flag1 = 301,
    T_Flag2 = 302,
    T_Flag3 = 303,
    T_Flag4 = 304,
    T_Flake = 305,
    T_Floor = 306,
    T_Freq = 307,
    T_Fudge = 308,
    T_Host = 309,
    T_Huffpuff = 310,
    T_Iburst = 311,
    T_Ident = 312,
    T_Ignore = 313,
    T_Incalloc = 314,
    T_Incmem = 315,
    T_Initalloc = 316,
    T_Initmem = 317,
    T_Includefile = 318,
    T_Integer = 319,
    T_Interface = 320,
    T_Intrange = 321,
    T_Io = 322,
    T_Ippeerlimit = 323,
    T_Ipv4 = 324,
    T_Ipv4_flag = 325,
    T_Ipv6 = 326,
    T_Ipv6_flag = 327,
    T_Kernel = 328,
    T_Key = 329,
    T_Keys = 330,
    T_Keysdir = 331,
    T_Kod = 332,
    T_Mssntp = 333,
    T_Leapfile = 334,
    T_Leapsmearinterval = 335,
    T_Limited = 336,
    T_Link = 337,
    T_Listen = 338,
    T_Logconfig = 339,
    T_Logfile = 340,
    T_Loopstats = 341,
    T_Lowpriotrap = 342,
    T_Manycastclient = 343,
    T_Manycastserver = 344,
    T_Mask = 345,
    T_Maxage = 346,
    T_Maxclock = 347,
    T_Maxdepth = 348,
    T_Maxdist = 349,
    T_Maxmem = 350,
    T_Maxpoll = 351,
    T_Mdnstries = 352,
    T_Mem = 353,
    T_Memlock = 354,
    T_Minclock = 355,
    T_Mindepth = 356,
    T_Mindist = 357,
    T_Minimum = 358,
    T_Minpoll = 359,
    T_Minsane = 360,
    T_Mode = 361,
    T_Mode7 = 362,
    T_Monitor = 363,
    T_Month = 364,
    T_Mru = 365,
    T_Multicastclient = 366,
    T_Nic = 367,
    T_Nolink = 368,
    T_Nomodify = 369,
    T_Nomrulist = 370,
    T_None = 371,
    T_Nonvolatile = 372,
    T_Noepeer = 373,
    T_Nopeer = 374,
    T_Noquery = 375,
    T_Noselect = 376,
    T_Noserve = 377,
    T_Notrap = 378,
    T_Notrust = 379,
    T_Ntp = 380,
    T_Ntpport = 381,
    T_NtpSignDsocket = 382,
    T_Orphan = 383,
    T_Orphanwait = 384,
    T_PCEdigest = 385,
    T_Panic = 386,
    T_Peer = 387,
    T_Peerstats = 388,
    T_Phone = 389,
    T_Pid = 390,
    T_Pidfile = 391,
    T_Pool = 392,
    T_Port = 393,
    T_Preempt = 394,
    T_Prefer = 395,
    T_Protostats = 396,
    T_Pw = 397,
    T_Randfile = 398,
    T_Rawstats = 399,
    T_Refid = 400,
    T_Requestkey = 401,
    T_Reset = 402,
    T_Restrict = 403,
    T_Revoke = 404,
    T_Rlimit = 405,
    T_Saveconfigdir = 406,
    T_Server = 407,
    T_Setvar = 408,
    T_Source = 409,
    T_Stacksize = 410,
    T_Statistics = 411,
    T_Stats = 412,
    T_Statsdir = 413,
    T_Step = 414,
    T_Stepback = 415,
    T_Stepfwd = 416,
    T_Stepout = 417,
    T_Stratum = 418,
    T_String = 419,
    T_Sys = 420,
    T_Sysstats = 421,
    T_Tick = 422,
    T_Time1 = 423,
    T_Time2 = 424,
    T_Timer = 425,
    T_Timingstats = 426,
    T_Tinker = 427,
    T_Tos = 428,
    T_Trap = 429,
    T_True = 430,
    T_Trustedkey = 431,
    T_Ttl = 432,
    T_Type = 433,
    T_U_int = 434,
    T_UEcrypto = 435,
    T_UEcryptonak = 436,
    T_UEdigest = 437,
    T_Unconfig = 438,
    T_Unpeer = 439,
    T_Version = 440,
    T_WanderThreshold = 441,
    T_Week = 442,
    T_Wildcard = 443,
    T_Xleave = 444,
    T_Year = 445,
    T_Flag = 446,
    T_EOC = 447,
    T_Simulate = 448,
    T_Beep_Delay = 449,
    T_Sim_Duration = 450,
    T_Server_Offset = 451,
    T_Duration = 452,
    T_Freq_Offset = 453,
    T_Wander = 454,
    T_Jitter = 455,
    T_Prop_Delay = 456,
    T_Proc_Delay = 457
  };
#endif
/* Tokens.  */
#define T_Abbrev 258
#define T_Age 259
#define T_All 260
#define T_Allan 261
#define T_Allpeers 262
#define T_Auth 263
#define T_Autokey 264
#define T_Automax 265
#define T_Average 266
#define T_Basedate 267
#define T_Bclient 268
#define T_Bcpollbstep 269
#define T_Beacon 270
#define T_Broadcast 271
#define T_Broadcastclient 272
#define T_Broadcastdelay 273
#define T_Burst 274
#define T_Calibrate 275
#define T_Ceiling 276
#define T_Clockstats 277
#define T_Cohort 278
#define T_ControlKey 279
#define T_Crypto 280
#define T_Cryptostats 281
#define T_Ctl 282
#define T_Day 283
#define T_Default 284
#define T_Digest 285
#define T_Disable 286
#define T_Discard 287
#define T_Dispersion 288
#define T_Double 289
#define T_Driftfile 290
#define T_Drop 291
#define T_Dscp 292
#define T_Ellipsis 293
#define T_Enable 294
#define T_End 295
#define T_Epeer 296
#define T_False 297
#define T_File 298
#define T_Filegen 299
#define T_Filenum 300
#define T_Flag1 301
#define T_Flag2 302
#define T_Flag3 303
#define T_Flag4 304
#define T_Flake 305
#define T_Floor 306
#define T_Freq 307
#define T_Fudge 308
#define T_Host 309
#define T_Huffpuff 310
#define T_Iburst 311
#define T_Ident 312
#define T_Ignore 313
#define T_Incalloc 314
#define T_Incmem 315
#define T_Initalloc 316
#define T_Initmem 317
#define T_Includefile 318
#define T_Integer 319
#define T_Interface 320
#define T_Intrange 321
#define T_Io 322
#define T_Ippeerlimit 323
#define T_Ipv4 324
#define T_Ipv4_flag 325
#define T_Ipv6 326
#define T_Ipv6_flag 327
#define T_Kernel 328
#define T_Key 329
#define T_Keys 330
#define T_Keysdir 331
#define T_Kod 332
#define T_Mssntp 333
#define T_Leapfile 334
#define T_Leapsmearinterval 335
#define T_Limited 336
#define T_Link 337
#define T_Listen 338
#define T_Logconfig 339
#define T_Logfile 340
#define T_Loopstats 341
#define T_Lowpriotrap 342
#define T_Manycastclient 343
#define T_Manycastserver 344
#define T_Mask 345
#define T_Maxage 346
#define T_Maxclock 347
#define T_Maxdepth 348
#define T_Maxdist 349
#define T_Maxmem 350
#define T_Maxpoll 351
#define T_Mdnstries 352
#define T_Mem 353
#define T_Memlock 354
#define T_Minclock 355
#define T_Mindepth 356
#define T_Mindist 357
#define T_Minimum 358
#define T_Minpoll 359
#define T_Minsane 360
#define T_Mode 361
#define T_Mode7 362
#define T_Monitor 363
#define T_Month 364
#define T_Mru 365
#define T_Multicastclient 366
#define T_Nic 367
#define T_Nolink 368
#define T_Nomodify 369
#define T_Nomrulist 370
#define T_None 371
#define T_Nonvolatile 372
#define T_Noepeer 373
#define T_Nopeer 374
#define T_Noquery 375
#define T_Noselect 376
#define T_Noserve 377
#define T_Notrap 378
#define T_Notrust 379
#define T_Ntp 380
#define T_Ntpport 381
#define T_NtpSignDsocket 382
#define T_Orphan 383
#define T_Orphanwait 384
#define T_PCEdigest 385
#define T_Panic 386
#define T_Peer 387
#define T_Peerstats 388
#define T_Phone 389
#define T_Pid 390
#define T_Pidfile 391
#define T_Pool 392
#define T_Port 393
#define T_Preempt 394
#define T_Prefer 395
#define T_Protostats 396
#define T_Pw 397
#define T_Randfile 398
#define T_Rawstats 399
#define T_Refid 400
#define T_Requestkey 401
#define T_Reset 402
#define T_Restrict 403
#define T_Revoke 404
#define T_Rlimit 405
#define T_Saveconfigdir 406
#define T_Server 407
#define T_Setvar 408
#define T_Source 409
#define T_Stacksize 410
#define T_Statistics 411
#define T_Stats 412
#define T_Statsdir 413
#define T_Step 414
#define T_Stepback 415
#define T_Stepfwd 416
#define T_Stepout 417
#define T_Stratum 418
#define T_String 419
#define T_Sys 420
#define T_Sysstats 421
#define T_Tick 422
#define T_Time1 423
#define T_Time2 424
#define T_Timer 425
#define T_Timingstats 426
#define T_Tinker 427
#define T_Tos 428
#define T_Trap 429
#define T_True 430
#define T_Trustedkey 431
#define T_Ttl 432
#define T_Type 433
#define T_U_int 434
#define T_UEcrypto 435
#define T_UEcryptonak 436
#define T_UEdigest 437
#define T_Unconfig 438
#define T_Unpeer 439
#define T_Version 440
#define T_WanderThreshold 441
#define T_Week 442
#define T_Wildcard 443
#define T_Xleave 444
#define T_Year 445
#define T_Flag 446
#define T_EOC 447
#define T_Simulate 448
#define T_Beep_Delay 449
#define T_Sim_Duration 450
#define T_Server_Offset 451
#define T_Duration 452
#define T_Freq_Offset 453
#define T_Wander 454
#define T_Jitter 455
#define T_Prop_Delay 456
#define T_Proc_Delay 457

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 52 "ntp_parser.y" /* yacc.c:355  */

	char *			String;
	double			Double;
	int			Integer;
	unsigned		U_int;
	gen_fifo *		Generic_fifo;
	attr_val *		Attr_val;
	attr_val_fifo *		Attr_val_fifo;
	int_fifo *		Int_fifo;
	string_fifo *		String_fifo;
	address_node *		Address_node;
	address_fifo *		Address_fifo;
	setvar_node *		Set_var;
	server_info *		Sim_server;
	server_info_fifo *	Sim_server_fifo;
	script_info *		Sim_script;
	script_info_fifo *	Sim_script_fifo;

#line 564 "ntp_parser.c" /* yacc.c:355  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_NTP_PARSER_H_INCLUDED  */

/* Copy the second part of user declarations.  */

#line 581 "ntp_parser.c" /* yacc.c:358  */

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
#else
typedef signed char yytype_int8;
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
# elif ! defined YYSIZE_T
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
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
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
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  216
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   662

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  208
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  107
/* YYNRULES -- Number of rules.  */
#define YYNRULES  324
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  436

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   457

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
     204,   205,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   203,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   206,     2,   207,     2,     2,     2,     2,
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
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   378,   378,   382,   383,   384,   399,   400,   401,   402,
     403,   404,   405,   406,   407,   408,   409,   410,   411,   412,
     420,   430,   431,   432,   433,   434,   438,   439,   444,   449,
     451,   457,   458,   466,   467,   468,   472,   477,   478,   479,
     480,   481,   482,   483,   484,   488,   490,   495,   496,   497,
     498,   499,   500,   504,   509,   518,   528,   529,   539,   541,
     543,   545,   556,   563,   565,   570,   572,   574,   576,   578,
     588,   594,   595,   603,   605,   617,   618,   619,   620,   621,
     630,   635,   640,   648,   650,   652,   654,   659,   660,   661,
     662,   663,   664,   665,   666,   667,   671,   672,   681,   683,
     692,   702,   707,   715,   716,   717,   718,   719,   720,   721,
     722,   727,   728,   736,   746,   755,   770,   775,   776,   780,
     781,   785,   786,   787,   788,   789,   790,   791,   800,   804,
     808,   816,   824,   832,   847,   862,   875,   876,   896,   897,
     905,   906,   907,   908,   909,   910,   911,   912,   913,   914,
     915,   916,   917,   918,   919,   920,   921,   925,   930,   938,
     943,   944,   945,   949,   954,   962,   967,   968,   969,   970,
     971,   972,   973,   974,   982,   992,   997,  1005,  1007,  1009,
    1018,  1020,  1025,  1026,  1030,  1031,  1032,  1033,  1041,  1046,
    1051,  1059,  1064,  1065,  1066,  1075,  1077,  1082,  1087,  1095,
    1097,  1114,  1115,  1116,  1117,  1118,  1119,  1123,  1124,  1125,
    1126,  1127,  1128,  1136,  1141,  1146,  1154,  1159,  1160,  1161,
    1162,  1163,  1164,  1165,  1166,  1167,  1168,  1177,  1178,  1179,
    1186,  1193,  1200,  1216,  1235,  1237,  1239,  1241,  1243,  1245,
    1252,  1257,  1258,  1259,  1263,  1267,  1276,  1277,  1281,  1282,
    1283,  1287,  1298,  1316,  1328,  1333,  1335,  1340,  1341,  1349,
    1351,  1359,  1364,  1372,  1397,  1404,  1414,  1415,  1419,  1420,
    1421,  1422,  1426,  1427,  1428,  1432,  1437,  1442,  1450,  1451,
    1452,  1453,  1454,  1455,  1456,  1466,  1471,  1479,  1484,  1492,
    1494,  1498,  1503,  1508,  1516,  1521,  1529,  1538,  1539,  1543,
    1544,  1548,  1556,  1574,  1578,  1583,  1591,  1596,  1597,  1601,
    1606,  1614,  1619,  1624,  1629,  1634,  1642,  1647,  1652,  1660,
    1665,  1666,  1667,  1668,  1669
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "T_Abbrev", "T_Age", "T_All", "T_Allan",
  "T_Allpeers", "T_Auth", "T_Autokey", "T_Automax", "T_Average",
  "T_Basedate", "T_Bclient", "T_Bcpollbstep", "T_Beacon", "T_Broadcast",
  "T_Broadcastclient", "T_Broadcastdelay", "T_Burst", "T_Calibrate",
  "T_Ceiling", "T_Clockstats", "T_Cohort", "T_ControlKey", "T_Crypto",
  "T_Cryptostats", "T_Ctl", "T_Day", "T_Default", "T_Digest", "T_Disable",
  "T_Discard", "T_Dispersion", "T_Double", "T_Driftfile", "T_Drop",
  "T_Dscp", "T_Ellipsis", "T_Enable", "T_End", "T_Epeer", "T_False",
  "T_File", "T_Filegen", "T_Filenum", "T_Flag1", "T_Flag2", "T_Flag3",
  "T_Flag4", "T_Flake", "T_Floor", "T_Freq", "T_Fudge", "T_Host",
  "T_Huffpuff", "T_Iburst", "T_Ident", "T_Ignore", "T_Incalloc",
  "T_Incmem", "T_Initalloc", "T_Initmem", "T_Includefile", "T_Integer",
  "T_Interface", "T_Intrange", "T_Io", "T_Ippeerlimit", "T_Ipv4",
  "T_Ipv4_flag", "T_Ipv6", "T_Ipv6_flag", "T_Kernel", "T_Key", "T_Keys",
  "T_Keysdir", "T_Kod", "T_Mssntp", "T_Leapfile", "T_Leapsmearinterval",
  "T_Limited", "T_Link", "T_Listen", "T_Logconfig", "T_Logfile",
  "T_Loopstats", "T_Lowpriotrap", "T_Manycastclient", "T_Manycastserver",
  "T_Mask", "T_Maxage", "T_Maxclock", "T_Maxdepth", "T_Maxdist",
  "T_Maxmem", "T_Maxpoll", "T_Mdnstries", "T_Mem", "T_Memlock",
  "T_Minclock", "T_Mindepth", "T_Mindist", "T_Minimum", "T_Minpoll",
  "T_Minsane", "T_Mode", "T_Mode7", "T_Monitor", "T_Month", "T_Mru",
  "T_Multicastclient", "T_Nic", "T_Nolink", "T_Nomodify", "T_Nomrulist",
  "T_None", "T_Nonvolatile", "T_Noepeer", "T_Nopeer", "T_Noquery",
  "T_Noselect", "T_Noserve", "T_Notrap", "T_Notrust", "T_Ntp", "T_Ntpport",
  "T_NtpSignDsocket", "T_Orphan", "T_Orphanwait", "T_PCEdigest", "T_Panic",
  "T_Peer", "T_Peerstats", "T_Phone", "T_Pid", "T_Pidfile", "T_Pool",
  "T_Port", "T_Preempt", "T_Prefer", "T_Protostats", "T_Pw", "T_Randfile",
  "T_Rawstats", "T_Refid", "T_Requestkey", "T_Reset", "T_Restrict",
  "T_Revoke", "T_Rlimit", "T_Saveconfigdir", "T_Server", "T_Setvar",
  "T_Source", "T_Stacksize", "T_Statistics", "T_Stats", "T_Statsdir",
  "T_Step", "T_Stepback", "T_Stepfwd", "T_Stepout", "T_Stratum",
  "T_String", "T_Sys", "T_Sysstats", "T_Tick", "T_Time1", "T_Time2",
  "T_Timer", "T_Timingstats", "T_Tinker", "T_Tos", "T_Trap", "T_True",
  "T_Trustedkey", "T_Ttl", "T_Type", "T_U_int", "T_UEcrypto",
  "T_UEcryptonak", "T_UEdigest", "T_Unconfig", "T_Unpeer", "T_Version",
  "T_WanderThreshold", "T_Week", "T_Wildcard", "T_Xleave", "T_Year",
  "T_Flag", "T_EOC", "T_Simulate", "T_Beep_Delay", "T_Sim_Duration",
  "T_Server_Offset", "T_Duration", "T_Freq_Offset", "T_Wander", "T_Jitter",
  "T_Prop_Delay", "T_Proc_Delay", "'='", "'('", "')'", "'{'", "'}'",
  "$accept", "configuration", "command_list", "command", "server_command",
  "client_type", "address", "ip_address", "address_fam", "option_list",
  "option", "option_flag", "option_flag_keyword", "option_int",
  "option_int_keyword", "option_str", "option_str_keyword",
  "unpeer_command", "unpeer_keyword", "other_mode_command",
  "authentication_command", "crypto_command_list", "crypto_command",
  "crypto_str_keyword", "orphan_mode_command", "tos_option_list",
  "tos_option", "tos_option_int_keyword", "tos_option_dbl_keyword",
  "monitoring_command", "stats_list", "stat", "filegen_option_list",
  "filegen_option", "link_nolink", "enable_disable", "filegen_type",
  "access_control_command", "res_ippeerlimit", "ac_flag_list",
  "access_control_flag", "discard_option_list", "discard_option",
  "discard_option_keyword", "mru_option_list", "mru_option",
  "mru_option_keyword", "fudge_command", "fudge_factor_list",
  "fudge_factor", "fudge_factor_dbl_keyword", "fudge_factor_bool_keyword",
  "rlimit_command", "rlimit_option_list", "rlimit_option",
  "rlimit_option_keyword", "system_option_command", "system_option_list",
  "system_option", "system_option_flag_keyword",
  "system_option_local_flag_keyword", "tinker_command",
  "tinker_option_list", "tinker_option", "tinker_option_keyword",
  "miscellaneous_command", "misc_cmd_dbl_keyword", "misc_cmd_int_keyword",
  "misc_cmd_str_keyword", "misc_cmd_str_lcl_keyword", "drift_parm",
  "variable_assign", "t_default_or_zero", "trap_option_list",
  "trap_option", "log_config_list", "log_config_command",
  "interface_command", "interface_nic", "nic_rule_class",
  "nic_rule_action", "reset_command", "counter_set_list",
  "counter_set_keyword", "integer_list", "integer_list_range",
  "integer_list_range_elt", "integer_range", "string_list", "address_list",
  "boolean", "number", "basedate", "simulate_command", "sim_conf_start",
  "sim_init_statement_list", "sim_init_statement", "sim_init_keyword",
  "sim_server_list", "sim_server", "sim_server_offset", "sim_server_name",
  "sim_act_list", "sim_act", "sim_act_stmt_list", "sim_act_stmt",
  "sim_act_keyword", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
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
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   403,   404,
     405,   406,   407,   408,   409,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,   426,   427,   428,   429,   430,   431,   432,   433,   434,
     435,   436,   437,   438,   439,   440,   441,   442,   443,   444,
     445,   446,   447,   448,   449,   450,   451,   452,   453,   454,
     455,   456,   457,    61,    40,    41,   123,   125
};
# endif

#define YYPACT_NINF -215

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-215)))

#define YYTABLE_NINF -7

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      11,  -175,     2,  -215,  -215,  -215,     3,  -215,    93,     9,
    -138,  -215,    93,  -215,    66,   -40,  -215,   -93,  -215,   -87,
     -82,  -215,  -215,   -81,  -215,  -215,   -40,    20,   210,   -40,
    -215,  -215,   -70,  -215,   -67,  -215,  -215,    34,     6,   -13,
      47,    -6,  -215,  -215,   -48,    66,   -45,  -215,   412,   483,
     -39,   -60,    62,  -215,  -215,  -215,   127,   203,   -63,  -215,
     -40,  -215,   -40,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,   -11,    75,   -24,   -22,  -215,   -18,  -215,
    -215,   -53,  -215,  -215,  -215,    48,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,    93,  -215,
    -215,  -215,  -215,  -215,  -215,     9,  -215,    82,   120,  -215,
      93,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,    86,  -215,     4,   373,  -215,  -215,  -215,
     -81,  -215,  -215,   -40,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,   210,  -215,   106,   -40,  -215,  -215,    15,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,     6,  -215,
     105,   146,   151,   105,   -30,  -215,  -215,  -215,  -215,    -6,
    -215,   117,   -21,  -215,    66,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,   412,  -215,   -11,
      22,  -215,  -215,  -215,   -20,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,   483,  -215,   128,   -11,  -215,  -215,  -215,
     129,   -60,  -215,  -215,  -215,   132,  -215,    10,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
       1,  -133,  -215,  -215,  -215,  -215,  -215,   134,  -215,    41,
    -215,  -215,  -215,  -215,   -28,    42,  -215,  -215,  -215,  -215,
      45,   148,  -215,  -215,    86,  -215,   -11,   -20,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,   150,  -215,   105,   105,
    -215,   -39,  -215,  -215,  -215,    51,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,   -57,   178,  -215,
    -215,  -215,   288,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -115,    25,    23,  -215,  -215,  -215,  -215,    61,  -215,
    -215,    21,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,   477,  -215,  -215,   477,   105,   477,   201,   -39,
     169,  -215,   172,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,   -59,  -215,    77,    36,    52,  -100,  -215,    39,
    -215,   -11,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,   477,
     477,  -215,  -215,  -215,  -215,  -215,    43,  -215,  -215,  -215,
     -40,  -215,  -215,  -215,    55,  -215,   477,  -215,  -215,    49,
      56,   -11,    54,  -166,  -215,    67,   -11,  -215,  -215,  -215,
      70,    63,  -215,  -215,  -215,  -215,  -215,   124,    85,    64,
    -215,    89,  -215,   -11,  -215,  -215
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       0,     0,     0,    24,    58,   241,     0,    71,     0,     0,
     253,   244,     0,   234,     0,     0,   246,     0,   266,     0,
       0,   247,   245,     0,   248,    25,     0,     0,     0,     0,
     267,   242,     0,    23,     0,   249,    22,     0,     0,     0,
       0,     0,   250,    21,     0,     0,     0,   243,     0,     0,
       0,     0,     0,    56,    57,   303,     0,     2,     0,     7,
       0,     8,     0,     9,    10,    13,    11,    12,    14,    15,
      16,    17,    18,     0,     0,     0,     0,   227,     0,   228,
      19,     0,     5,    62,    63,    64,   201,   202,   203,   204,
     207,   205,   206,   208,   209,   210,   211,   212,   196,   198,
     199,   200,   160,   161,   162,   128,   158,     0,   251,   235,
     195,   103,   104,   105,   106,   110,   107,   108,   109,   111,
      29,    30,    28,     0,    26,     0,     6,    65,    66,   263,
     236,   262,   295,    59,    61,   166,   167,   168,   169,   170,
     171,   172,   173,   129,   164,     0,    60,    70,   293,   237,
      67,   278,   279,   280,   281,   282,   283,   284,   275,   277,
     136,    29,    30,   136,   136,    68,   194,   192,   193,   188,
     190,     0,     0,   238,    98,   102,    99,   217,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   213,   215,     0,
       0,    87,    88,    89,     0,    90,    91,    97,    92,    96,
      93,    94,    95,    80,    82,     0,     0,    86,   257,   289,
       0,    69,   288,   290,   286,   240,     1,     0,     4,    31,
      55,   300,   299,   229,   230,   231,   232,   274,   273,   272,
       0,     0,    79,    75,    76,    77,    78,     0,    72,     0,
     197,   157,   159,   252,   100,     0,   184,   185,   186,   187,
       0,     0,   182,   183,   174,   176,     0,     0,    27,   233,
     261,   294,   163,   165,   292,   276,     0,   138,   136,   136,
     138,     0,   138,   189,   191,     0,   101,   214,   216,   301,
     298,   296,   297,    85,    81,    83,    84,   239,     0,   287,
     285,     3,    20,   268,   269,   270,   265,   271,   264,   307,
     308,     0,     0,     0,    74,    73,   120,   119,     0,   117,
     118,     0,   112,   115,   116,   180,   181,   179,   175,   177,
     178,   137,   132,   138,   138,   135,   136,   130,   256,     0,
       0,   258,     0,    37,    38,    39,    54,    47,    49,    48,
      51,    40,    41,    42,    43,    50,    52,    44,    32,    33,
      36,    34,     0,    35,     0,     0,     0,     0,   310,     0,
     305,     0,   113,   127,   123,   125,   121,   122,   124,   126,
     114,   140,   141,   142,   143,   144,   145,   146,   148,   149,
     147,   150,   151,   152,   153,   154,   155,   156,   139,   133,
     134,   138,   255,   254,   260,   259,     0,    45,    46,    53,
       0,   304,   302,   309,     0,   306,   131,   291,   313,     0,
       0,     0,     0,     0,   315,     0,     0,   311,   314,   312,
       0,     0,   320,   321,   322,   323,   324,     0,     0,     0,
     316,     0,   318,     0,   317,   319
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -215,  -215,  -215,   -23,  -215,  -215,   -15,   -49,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,    81,  -215,  -215,  -215,
    -215,   -38,  -215,  -215,  -215,  -215,  -215,  -215,  -154,  -214,
    -215,  -215,   153,  -215,  -215,   142,  -215,  -215,  -215,    32,
    -215,  -215,  -215,  -215,   121,  -215,  -215,   277,   -35,  -215,
    -215,  -215,  -215,   107,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,   163,  -215,  -215,  -215,
    -215,  -215,  -215,   137,  -215,  -215,    87,  -215,  -215,   267,
      53,  -187,  -215,  -215,  -215,  -215,    -2,  -215,  -215,   -55,
    -215,  -215,  -215,  -109,  -215,  -121,  -215
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,    56,    57,    58,    59,    60,   132,   124,   125,   292,
     348,   349,   350,   351,   352,   353,   354,    61,    62,    63,
      64,    85,   238,   239,    65,   203,   204,   205,   206,    66,
     174,   119,   244,   312,   313,   314,   370,    67,   267,   322,
     388,   105,   106,   107,   143,   144,   145,    68,   254,   255,
     256,   257,    69,   169,   170,   171,    70,    98,    99,   100,
     101,    71,   187,   188,   189,    72,    73,    74,    75,    76,
     109,   173,   393,   287,   331,   130,   131,    77,    78,   298,
     230,    79,   158,   159,   215,   211,   212,   213,   149,   133,
     283,   223,   207,    80,    81,   301,   302,   303,   357,   358,
     410,   359,   413,   414,   427,   428,   429
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     123,   208,   278,   306,   209,   397,   293,   175,   329,   270,
     272,   307,     1,   151,   152,   308,   160,    82,   227,   286,
     102,     2,   280,   221,   164,   363,   108,     3,     4,     5,
     120,   412,   121,   153,   217,     6,     7,   355,   266,   166,
     228,   417,     8,     9,   281,   219,    10,   220,    11,   364,
      12,    13,   355,   222,   309,    14,   325,   161,   327,   162,
     271,   299,   300,   240,    15,   229,    83,    84,    16,   319,
     294,   126,   295,   154,    17,   240,    18,   127,   232,   299,
     300,   330,   128,   129,   134,   310,    19,    20,   111,   245,
      21,    22,   112,   167,   147,    23,    24,   148,   150,    25,
      26,    86,   233,   259,   155,   234,    87,   402,    27,   389,
     390,   165,   103,    88,   323,   324,   172,   104,   261,   176,
     398,    28,    29,    30,   122,   122,   214,   216,    31,   218,
     365,   261,   246,   247,   248,   249,   276,   366,    32,   224,
     225,   163,   226,    33,   210,    34,   242,    35,    36,   168,
     311,   122,   113,   231,   243,   282,   367,    37,    38,    39,
      40,    41,    42,    43,    44,   296,    89,    45,   258,    46,
     263,   156,   391,   266,   405,   268,   157,   406,    47,   264,
     269,   274,   275,    48,    49,    50,   279,    51,    52,   297,
     235,   236,   285,   288,    53,    54,   290,   237,   304,   114,
      90,    91,   291,    -6,    55,   305,   315,   115,   368,   316,
     116,   369,   317,     2,   321,   328,   332,   360,    92,     3,
       4,     5,   326,    93,   415,   362,   361,     6,     7,   420,
     392,   250,   117,   395,     8,     9,   396,   118,    10,   400,
      11,   399,    12,    13,   401,   404,   435,    14,   407,   251,
      94,   409,   411,   412,   252,   253,    15,   416,   241,   419,
      16,   422,   423,   424,   425,   426,    17,   433,    18,   135,
     136,   137,   138,    95,    96,    97,   421,   432,    19,    20,
     394,   434,    21,    22,   284,   262,   318,    23,    24,   110,
     273,    25,    26,   260,   277,   265,   146,   333,   289,   356,
      27,   139,   403,   140,   418,   141,   431,   334,     0,     0,
     320,   142,     0,    28,    29,    30,     0,     0,     0,     0,
      31,     0,   422,   423,   424,   425,   426,     0,     0,     0,
      32,   430,     0,     0,     0,    33,     0,    34,     0,    35,
      36,     0,     0,     0,   335,   336,     0,     0,     0,    37,
      38,    39,    40,    41,    42,    43,    44,     0,     0,    45,
       0,    46,   337,     0,     0,     0,     0,     0,     0,     0,
      47,     0,     0,     0,     0,    48,    49,    50,     0,    51,
      52,     0,     0,     2,   338,   408,    53,    54,     0,     3,
       4,     5,   339,     0,   340,    -6,    55,     6,     7,     0,
       0,     0,     0,     0,     8,     9,     0,     0,    10,   341,
      11,     0,    12,    13,     0,     0,     0,    14,   177,     0,
       0,     0,     0,     0,     0,     0,    15,   342,   343,     0,
      16,     0,     0,     0,     0,     0,    17,     0,    18,     0,
       0,     0,     0,     0,     0,   178,     0,     0,    19,    20,
       0,     0,    21,    22,     0,     0,     0,    23,    24,     0,
       0,    25,    26,   344,   179,   345,     0,   180,     0,     0,
      27,     0,     0,   346,     0,     0,     0,   347,     0,     0,
       0,     0,     0,    28,    29,    30,     0,     0,     0,     0,
      31,     0,     0,     0,     0,   190,     0,   191,   192,     0,
      32,     0,     0,     0,   193,    33,   194,    34,     0,    35,
      36,     0,     0,     0,     0,     0,     0,     0,   371,    37,
      38,    39,    40,    41,    42,    43,    44,   372,     0,    45,
       0,    46,     0,     0,   195,   373,     0,     0,     0,     0,
      47,     0,     0,   181,     0,    48,    49,    50,     0,    51,
      52,     0,     0,     0,   374,   375,    53,    54,   376,     0,
       0,     0,     0,     0,   377,     0,    55,     0,     0,     0,
       0,   182,   183,   184,   185,   196,     0,   197,     0,   186,
       0,     0,     0,   198,     0,   199,     0,     0,   200,     0,
       0,   378,   379,     0,     0,   380,   381,   382,     0,   383,
     384,   385,     0,   386,     0,     0,     0,     0,     0,     0,
       0,   201,   202,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   387
};

static const yytype_int16 yycheck[] =
{
      15,    50,   189,    31,    64,    64,     5,    45,    65,   163,
     164,    39,     1,     7,     8,    43,    29,   192,    36,   206,
      11,    10,    42,    34,    39,     4,   164,    16,    17,    18,
      70,   197,    72,    27,    57,    24,    25,   152,    68,    45,
      58,   207,    31,    32,    64,    60,    35,    62,    37,    28,
      39,    40,   152,    64,    82,    44,   270,    70,   272,    72,
      90,   194,   195,    98,    53,    83,    64,    64,    57,   256,
      69,   164,    71,    67,    63,   110,    65,   164,    30,   194,
     195,   138,   164,   164,    64,   113,    75,    76,    22,     3,
      79,    80,    26,    99,   164,    84,    85,   164,    64,    88,
      89,     8,    54,   126,    98,    57,    13,   207,    97,   323,
     324,    64,   103,    20,   268,   269,   164,   108,   133,   164,
     179,   110,   111,   112,   164,   164,    64,     0,   117,   192,
     109,   146,    46,    47,    48,    49,   174,   116,   127,    64,
     164,   154,   164,   132,   204,   134,    64,   136,   137,   155,
     178,   164,    86,   206,    34,   175,   135,   146,   147,   148,
     149,   150,   151,   152,   153,   164,    73,   156,   164,   158,
      64,   165,   326,    68,   361,    29,   170,   391,   167,   164,
      29,    64,   203,   172,   173,   174,   164,   176,   177,   188,
     142,   143,    64,    64,   183,   184,    64,   149,    64,   133,
     107,   108,   192,   192,   193,   164,   164,   141,   187,   164,
     144,   190,    64,    10,    64,   164,    38,   192,   125,    16,
      17,    18,   271,   130,   411,   164,   203,    24,    25,   416,
      29,   145,   166,    64,    31,    32,    64,   171,    35,   203,
      37,   164,    39,    40,   192,   206,   433,    44,   205,   163,
     157,   196,   203,   197,   168,   169,    53,   203,   105,   192,
      57,   198,   199,   200,   201,   202,    63,   203,    65,    59,
      60,    61,    62,   180,   181,   182,   206,   192,    75,    76,
     329,   192,    79,    80,   203,   143,   254,    84,    85,    12,
     169,    88,    89,   130,   187,   158,    29,     9,   211,   301,
      97,    91,   357,    93,   413,    95,   427,    19,    -1,    -1,
     257,   101,    -1,   110,   111,   112,    -1,    -1,    -1,    -1,
     117,    -1,   198,   199,   200,   201,   202,    -1,    -1,    -1,
     127,   207,    -1,    -1,    -1,   132,    -1,   134,    -1,   136,
     137,    -1,    -1,    -1,    56,    57,    -1,    -1,    -1,   146,
     147,   148,   149,   150,   151,   152,   153,    -1,    -1,   156,
      -1,   158,    74,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     167,    -1,    -1,    -1,    -1,   172,   173,   174,    -1,   176,
     177,    -1,    -1,    10,    96,   400,   183,   184,    -1,    16,
      17,    18,   104,    -1,   106,   192,   193,    24,    25,    -1,
      -1,    -1,    -1,    -1,    31,    32,    -1,    -1,    35,   121,
      37,    -1,    39,    40,    -1,    -1,    -1,    44,     6,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    53,   139,   140,    -1,
      57,    -1,    -1,    -1,    -1,    -1,    63,    -1,    65,    -1,
      -1,    -1,    -1,    -1,    -1,    33,    -1,    -1,    75,    76,
      -1,    -1,    79,    80,    -1,    -1,    -1,    84,    85,    -1,
      -1,    88,    89,   175,    52,   177,    -1,    55,    -1,    -1,
      97,    -1,    -1,   185,    -1,    -1,    -1,   189,    -1,    -1,
      -1,    -1,    -1,   110,   111,   112,    -1,    -1,    -1,    -1,
     117,    -1,    -1,    -1,    -1,    12,    -1,    14,    15,    -1,
     127,    -1,    -1,    -1,    21,   132,    23,   134,    -1,   136,
     137,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    41,   146,
     147,   148,   149,   150,   151,   152,   153,    50,    -1,   156,
      -1,   158,    -1,    -1,    51,    58,    -1,    -1,    -1,    -1,
     167,    -1,    -1,   131,    -1,   172,   173,   174,    -1,   176,
     177,    -1,    -1,    -1,    77,    78,   183,   184,    81,    -1,
      -1,    -1,    -1,    -1,    87,    -1,   193,    -1,    -1,    -1,
      -1,   159,   160,   161,   162,    92,    -1,    94,    -1,   167,
      -1,    -1,    -1,   100,    -1,   102,    -1,    -1,   105,    -1,
      -1,   114,   115,    -1,    -1,   118,   119,   120,    -1,   122,
     123,   124,    -1,   126,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   128,   129,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   185
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint16 yystos[] =
{
       0,     1,    10,    16,    17,    18,    24,    25,    31,    32,
      35,    37,    39,    40,    44,    53,    57,    63,    65,    75,
      76,    79,    80,    84,    85,    88,    89,    97,   110,   111,
     112,   117,   127,   132,   134,   136,   137,   146,   147,   148,
     149,   150,   151,   152,   153,   156,   158,   167,   172,   173,
     174,   176,   177,   183,   184,   193,   209,   210,   211,   212,
     213,   225,   226,   227,   228,   232,   237,   245,   255,   260,
     264,   269,   273,   274,   275,   276,   277,   285,   286,   289,
     301,   302,   192,    64,    64,   229,     8,    13,    20,    73,
     107,   108,   125,   130,   157,   180,   181,   182,   265,   266,
     267,   268,    11,   103,   108,   249,   250,   251,   164,   278,
     265,    22,    26,    86,   133,   141,   144,   166,   171,   239,
      70,    72,   164,   214,   215,   216,   164,   164,   164,   164,
     283,   284,   214,   297,    64,    59,    60,    61,    62,    91,
      93,    95,   101,   252,   253,   254,   297,   164,   164,   296,
      64,     7,     8,    27,    67,    98,   165,   170,   290,   291,
      29,    70,    72,   154,   214,    64,    45,    99,   155,   261,
     262,   263,   164,   279,   238,   239,   164,     6,    33,    52,
      55,   131,   159,   160,   161,   162,   167,   270,   271,   272,
      12,    14,    15,    21,    23,    51,    92,    94,   100,   102,
     105,   128,   129,   233,   234,   235,   236,   300,   215,    64,
     204,   293,   294,   295,    64,   292,     0,   211,   192,   214,
     214,    34,    64,   299,    64,   164,   164,    36,    58,    83,
     288,   206,    30,    54,    57,   142,   143,   149,   230,   231,
     266,   250,    64,    34,   240,     3,    46,    47,    48,    49,
     145,   163,   168,   169,   256,   257,   258,   259,   164,   211,
     284,   214,   253,    64,   164,   291,    68,   246,    29,    29,
     246,    90,   246,   262,    64,   203,   239,   271,   299,   164,
      42,    64,   175,   298,   234,    64,   299,   281,    64,   294,
      64,   192,   217,     5,    69,    71,   164,   188,   287,   194,
     195,   303,   304,   305,    64,   164,    31,    39,    43,    82,
     113,   178,   241,   242,   243,   164,   164,    64,   257,   299,
     298,    64,   247,   246,   246,   247,   215,   247,   164,    65,
     138,   282,    38,     9,    19,    56,    57,    74,    96,   104,
     106,   121,   139,   140,   175,   177,   185,   189,   218,   219,
     220,   221,   222,   223,   224,   152,   304,   306,   307,   309,
     192,   203,   164,     4,    28,   109,   116,   135,   187,   190,
     244,    41,    50,    58,    77,    78,    81,    87,   114,   115,
     118,   119,   120,   122,   123,   124,   126,   185,   248,   247,
     247,   246,    29,   280,   215,    64,    64,    64,   179,   164,
     203,   192,   207,   307,   206,   299,   247,   205,   214,   196,
     308,   203,   197,   310,   311,   299,   203,   207,   311,   192,
     299,   206,   198,   199,   200,   201,   202,   312,   313,   314,
     207,   313,   192,   203,   192,   299
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint16 yyr1[] =
{
       0,   208,   209,   210,   210,   210,   211,   211,   211,   211,
     211,   211,   211,   211,   211,   211,   211,   211,   211,   211,
     212,   213,   213,   213,   213,   213,   214,   214,   215,   216,
     216,   217,   217,   218,   218,   218,   219,   220,   220,   220,
     220,   220,   220,   220,   220,   221,   221,   222,   222,   222,
     222,   222,   222,   223,   224,   225,   226,   226,   227,   227,
     227,   227,   228,   228,   228,   228,   228,   228,   228,   228,
     228,   229,   229,   230,   230,   231,   231,   231,   231,   231,
     232,   233,   233,   234,   234,   234,   234,   235,   235,   235,
     235,   235,   235,   235,   235,   235,   236,   236,   237,   237,
     237,   238,   238,   239,   239,   239,   239,   239,   239,   239,
     239,   240,   240,   241,   241,   241,   241,   242,   242,   243,
     243,   244,   244,   244,   244,   244,   244,   244,   245,   245,
     245,   245,   245,   245,   245,   245,   246,   246,   247,   247,
     248,   248,   248,   248,   248,   248,   248,   248,   248,   248,
     248,   248,   248,   248,   248,   248,   248,   249,   249,   250,
     251,   251,   251,   252,   252,   253,   254,   254,   254,   254,
     254,   254,   254,   254,   255,   256,   256,   257,   257,   257,
     257,   257,   258,   258,   259,   259,   259,   259,   260,   261,
     261,   262,   263,   263,   263,   264,   264,   265,   265,   266,
     266,   267,   267,   267,   267,   267,   267,   268,   268,   268,
     268,   268,   268,   269,   270,   270,   271,   272,   272,   272,
     272,   272,   272,   272,   272,   272,   272,   273,   273,   273,
     273,   273,   273,   273,   273,   273,   273,   273,   273,   273,
     273,   274,   274,   274,   275,   275,   276,   276,   277,   277,
     277,   278,   278,   278,   279,   280,   280,   281,   281,   282,
     282,   283,   283,   284,   285,   285,   286,   286,   287,   287,
     287,   287,   288,   288,   288,   289,   290,   290,   291,   291,
     291,   291,   291,   291,   291,   292,   292,   293,   293,   294,
     294,   295,   296,   296,   297,   297,   298,   298,   298,   299,
     299,   300,   301,   302,   303,   303,   304,   305,   305,   306,
     306,   307,   308,   309,   310,   310,   311,   312,   312,   313,
     314,   314,   314,   314,   314
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     3,     2,     2,     0,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     1,     1,     1,     1,     1,     1,     2,     1,     1,
       1,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     2,     1,     1,     1,
       1,     1,     1,     2,     1,     2,     1,     1,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     0,     2,     2,     2,     1,     1,     1,     1,     1,
       2,     2,     1,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     2,
       3,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     0,     2,     2,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     2,     2,
       4,     6,     4,     5,     5,     4,     0,     2,     0,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     1,     2,
       1,     1,     1,     2,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     2,     1,     2,     2,     2,
       2,     2,     1,     1,     1,     1,     1,     1,     2,     2,
       1,     2,     1,     1,     1,     2,     2,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     2,     1,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     2,
       2,     2,     2,     3,     1,     2,     2,     2,     2,     3,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     0,     4,     1,     0,     0,     2,     2,
       2,     2,     1,     1,     3,     3,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     2,     1,     1,
       1,     5,     2,     1,     2,     1,     1,     1,     1,     1,
       1,     2,     5,     1,     3,     2,     3,     1,     1,     2,
       1,     5,     4,     3,     2,     1,     6,     3,     2,     3,
       1,     1,     1,     1,     1
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

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
#ifndef YYINITDEPTH
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
static YYSIZE_T
yystrlen (const char *yystr)
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
static char *
yystpcpy (char *yydest, const char *yysrc)
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

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
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
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
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

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
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
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

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
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 5:
#line 385 "ntp_parser.y" /* yacc.c:1646  */
    {
			/* I will need to incorporate much more fine grained
			 * error messages. The following should suffice for
			 * the time being.
			 */
			struct FILE_INFO * ip_ctx = lex_current();
			msyslog(LOG_ERR,
				"syntax error in %s line %d, column %d",
				ip_ctx->fname,
				ip_ctx->errpos.nline,
				ip_ctx->errpos.ncol);
		}
#line 2144 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 20:
#line 421 "ntp_parser.y" /* yacc.c:1646  */
    {
			peer_node *my_node;

			my_node = create_peer_node((yyvsp[-2].Integer), (yyvsp[-1].Address_node), (yyvsp[0].Attr_val_fifo));
			APPEND_G_FIFO(cfgt.peers, my_node);
		}
#line 2155 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 27:
#line 440 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Address_node) = create_address_node((yyvsp[0].String), (yyvsp[-1].Integer)); }
#line 2161 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 28:
#line 445 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Address_node) = create_address_node((yyvsp[0].String), AF_UNSPEC); }
#line 2167 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 29:
#line 450 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = AF_INET; }
#line 2173 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 30:
#line 452 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = AF_INET6; }
#line 2179 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 31:
#line 457 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val_fifo) = NULL; }
#line 2185 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 32:
#line 459 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2194 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 36:
#line 473 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival(T_Flag, (yyvsp[0].Integer)); }
#line 2200 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 45:
#line 489 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 2206 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 46:
#line 491 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_uval((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 2212 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 53:
#line 505 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String)); }
#line 2218 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 55:
#line 519 "ntp_parser.y" /* yacc.c:1646  */
    {
			unpeer_node *my_node;

			my_node = create_unpeer_node((yyvsp[0].Address_node));
			if (my_node)
				APPEND_G_FIFO(cfgt.unpeers, my_node);
		}
#line 2230 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 58:
#line 540 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.broadcastclient = 1; }
#line 2236 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 59:
#line 542 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.manycastserver, (yyvsp[0].Address_fifo)); }
#line 2242 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 60:
#line 544 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.multicastclient, (yyvsp[0].Address_fifo)); }
#line 2248 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 61:
#line 546 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.mdnstries = (yyvsp[0].Integer); }
#line 2254 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 62:
#line 557 "ntp_parser.y" /* yacc.c:1646  */
    {
			attr_val *atrv;

			atrv = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer));
			APPEND_G_FIFO(cfgt.vars, atrv);
		}
#line 2265 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 63:
#line 564 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.auth.control_key = (yyvsp[0].Integer); }
#line 2271 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 64:
#line 566 "ntp_parser.y" /* yacc.c:1646  */
    {
			cfgt.auth.cryptosw++;
			CONCAT_G_FIFOS(cfgt.auth.crypto_cmd_list, (yyvsp[0].Attr_val_fifo));
		}
#line 2280 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 65:
#line 571 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.auth.keys = (yyvsp[0].String); }
#line 2286 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 66:
#line 573 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.auth.keysdir = (yyvsp[0].String); }
#line 2292 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 67:
#line 575 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.auth.request_key = (yyvsp[0].Integer); }
#line 2298 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 68:
#line 577 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.auth.revoke = (yyvsp[0].Integer); }
#line 2304 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 69:
#line 579 "ntp_parser.y" /* yacc.c:1646  */
    {
			/* [Bug 948] leaves it open if appending or
			 * replacing the trusted key list is the right
			 * way. In any case, either alternative should
			 * be coded correctly!
			 */
			DESTROY_G_FIFO(cfgt.auth.trusted_key_list, destroy_attr_val); /* remove for append */
			CONCAT_G_FIFOS(cfgt.auth.trusted_key_list, (yyvsp[0].Attr_val_fifo));
		}
#line 2318 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 70:
#line 589 "ntp_parser.y" /* yacc.c:1646  */
    { cfgt.auth.ntp_signd_socket = (yyvsp[0].String); }
#line 2324 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 71:
#line 594 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val_fifo) = NULL; }
#line 2330 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 72:
#line 596 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2339 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 73:
#line 604 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String)); }
#line 2345 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 74:
#line 606 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val) = NULL;
			cfgt.auth.revoke = (yyvsp[0].Integer);
			msyslog(LOG_WARNING,
				"'crypto revoke %d' is deprecated, "
				"please use 'revoke %d' instead.",
				cfgt.auth.revoke, cfgt.auth.revoke);
		}
#line 2358 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 80:
#line 631 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.orphan_cmds, (yyvsp[0].Attr_val_fifo)); }
#line 2364 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 81:
#line 636 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2373 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 82:
#line 641 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2382 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 83:
#line 649 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-1].Integer), (double)(yyvsp[0].Integer)); }
#line 2388 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 84:
#line 651 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-1].Integer), (yyvsp[0].Double)); }
#line 2394 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 85:
#line 653 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-1].Integer), (double)(yyvsp[0].Integer)); }
#line 2400 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 86:
#line 655 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival(T_Basedate, (yyvsp[0].Integer)); }
#line 2406 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 98:
#line 682 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.stats_list, (yyvsp[0].Int_fifo)); }
#line 2412 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 99:
#line 684 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				cfgt.stats_dir = (yyvsp[0].String);
			} else {
				YYFREE((yyvsp[0].String));
				yyerror("statsdir remote configuration ignored");
			}
		}
#line 2425 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 100:
#line 693 "ntp_parser.y" /* yacc.c:1646  */
    {
			filegen_node *fgn;

			fgn = create_filegen_node((yyvsp[-1].Integer), (yyvsp[0].Attr_val_fifo));
			APPEND_G_FIFO(cfgt.filegen_opts, fgn);
		}
#line 2436 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 101:
#line 703 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Int_fifo) = (yyvsp[-1].Int_fifo);
			APPEND_G_FIFO((yyval.Int_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 2445 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 102:
#line 708 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Int_fifo) = NULL;
			APPEND_G_FIFO((yyval.Int_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 2454 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 111:
#line 727 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val_fifo) = NULL; }
#line 2460 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 112:
#line 729 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2469 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 113:
#line 737 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				(yyval.Attr_val) = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String));
			} else {
				(yyval.Attr_val) = NULL;
				YYFREE((yyvsp[0].String));
				yyerror("filegen file remote config ignored");
			}
		}
#line 2483 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 114:
#line 747 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				(yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer));
			} else {
				(yyval.Attr_val) = NULL;
				yyerror("filegen type remote config ignored");
			}
		}
#line 2496 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 115:
#line 756 "ntp_parser.y" /* yacc.c:1646  */
    {
			const char *err;

			if (lex_from_file()) {
				(yyval.Attr_val) = create_attr_ival(T_Flag, (yyvsp[0].Integer));
			} else {
				(yyval.Attr_val) = NULL;
				if (T_Link == (yyvsp[0].Integer))
					err = "filegen link remote config ignored";
				else
					err = "filegen nolink remote config ignored";
				yyerror(err);
			}
		}
#line 2515 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 116:
#line 771 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival(T_Flag, (yyvsp[0].Integer)); }
#line 2521 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 128:
#line 801 "ntp_parser.y" /* yacc.c:1646  */
    {
			CONCAT_G_FIFOS(cfgt.discard_opts, (yyvsp[0].Attr_val_fifo));
		}
#line 2529 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 129:
#line 805 "ntp_parser.y" /* yacc.c:1646  */
    {
			CONCAT_G_FIFOS(cfgt.mru_opts, (yyvsp[0].Attr_val_fifo));
		}
#line 2537 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 130:
#line 809 "ntp_parser.y" /* yacc.c:1646  */
    {
			restrict_node *rn;

			rn = create_restrict_node((yyvsp[-2].Address_node), NULL, (yyvsp[-1].Integer), (yyvsp[0].Int_fifo),
						  lex_current()->curpos.nline);
			APPEND_G_FIFO(cfgt.restrict_opts, rn);
		}
#line 2549 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 131:
#line 817 "ntp_parser.y" /* yacc.c:1646  */
    {
			restrict_node *rn;

			rn = create_restrict_node((yyvsp[-4].Address_node), (yyvsp[-2].Address_node), (yyvsp[-1].Integer), (yyvsp[0].Int_fifo),
						  lex_current()->curpos.nline);
			APPEND_G_FIFO(cfgt.restrict_opts, rn);
		}
#line 2561 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 132:
#line 825 "ntp_parser.y" /* yacc.c:1646  */
    {
			restrict_node *rn;

			rn = create_restrict_node(NULL, NULL, (yyvsp[-1].Integer), (yyvsp[0].Int_fifo),
						  lex_current()->curpos.nline);
			APPEND_G_FIFO(cfgt.restrict_opts, rn);
		}
#line 2573 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 133:
#line 833 "ntp_parser.y" /* yacc.c:1646  */
    {
			restrict_node *rn;

			rn = create_restrict_node(
				create_address_node(
					estrdup("0.0.0.0"),
					AF_INET),
				create_address_node(
					estrdup("0.0.0.0"),
					AF_INET),
				(yyvsp[-1].Integer), (yyvsp[0].Int_fifo),
				lex_current()->curpos.nline);
			APPEND_G_FIFO(cfgt.restrict_opts, rn);
		}
#line 2592 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 134:
#line 848 "ntp_parser.y" /* yacc.c:1646  */
    {
			restrict_node *rn;

			rn = create_restrict_node(
				create_address_node(
					estrdup("::"),
					AF_INET6),
				create_address_node(
					estrdup("::"),
					AF_INET6),
				(yyvsp[-1].Integer), (yyvsp[0].Int_fifo),
				lex_current()->curpos.nline);
			APPEND_G_FIFO(cfgt.restrict_opts, rn);
		}
#line 2611 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 135:
#line 863 "ntp_parser.y" /* yacc.c:1646  */
    {
			restrict_node *	rn;

			APPEND_G_FIFO((yyvsp[0].Int_fifo), create_int_node((yyvsp[-2].Integer)));
			rn = create_restrict_node(
				NULL, NULL, (yyvsp[-1].Integer), (yyvsp[0].Int_fifo), lex_current()->curpos.nline);
			APPEND_G_FIFO(cfgt.restrict_opts, rn);
		}
#line 2624 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 136:
#line 875 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = -1; }
#line 2630 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 137:
#line 877 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (((yyvsp[0].Integer) < -1) || ((yyvsp[0].Integer) > 100)) {
				struct FILE_INFO * ip_ctx;

				ip_ctx = lex_current();
				msyslog(LOG_ERR,
					"Unreasonable ippeerlimit value (%d) in %s line %d, column %d.  Using 0.",
					(yyvsp[0].Integer),
					ip_ctx->fname,
					ip_ctx->errpos.nline,
					ip_ctx->errpos.ncol);
				(yyvsp[0].Integer) = 0;
			}
			(yyval.Integer) = (yyvsp[0].Integer);
		}
#line 2650 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 138:
#line 896 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Int_fifo) = NULL; }
#line 2656 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 139:
#line 898 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Int_fifo) = (yyvsp[-1].Int_fifo);
			APPEND_G_FIFO((yyval.Int_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 2665 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 157:
#line 926 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2674 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 158:
#line 931 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2683 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 159:
#line 939 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 2689 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 163:
#line 950 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2698 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 164:
#line 955 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2707 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 165:
#line 963 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 2713 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 174:
#line 983 "ntp_parser.y" /* yacc.c:1646  */
    {
			addr_opts_node *aon;

			aon = create_addr_opts_node((yyvsp[-1].Address_node), (yyvsp[0].Attr_val_fifo));
			APPEND_G_FIFO(cfgt.fudge, aon);
		}
#line 2724 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 175:
#line 993 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2733 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 176:
#line 998 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2742 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 177:
#line 1006 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-1].Integer), (yyvsp[0].Double)); }
#line 2748 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 178:
#line 1008 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 2754 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 179:
#line 1010 "ntp_parser.y" /* yacc.c:1646  */
    {
			if ((yyvsp[0].Integer) >= 0 && (yyvsp[0].Integer) <= 16) {
				(yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer));
			} else {
				(yyval.Attr_val) = NULL;
				yyerror("fudge factor: stratum value not in [0..16], ignored");
			}
		}
#line 2767 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 180:
#line 1019 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String)); }
#line 2773 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 181:
#line 1021 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String)); }
#line 2779 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 188:
#line 1042 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.rlimit, (yyvsp[0].Attr_val_fifo)); }
#line 2785 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 189:
#line 1047 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2794 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 190:
#line 1052 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2803 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 191:
#line 1060 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 2809 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 195:
#line 1076 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.enable_opts, (yyvsp[0].Attr_val_fifo)); }
#line 2815 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 196:
#line 1078 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.disable_opts, (yyvsp[0].Attr_val_fifo)); }
#line 2821 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 197:
#line 1083 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2830 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 198:
#line 1088 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2839 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 199:
#line 1096 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival(T_Flag, (yyvsp[0].Integer)); }
#line 2845 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 200:
#line 1098 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				(yyval.Attr_val) = create_attr_ival(T_Flag, (yyvsp[0].Integer));
			} else {
				char err_str[128];

				(yyval.Attr_val) = NULL;
				snprintf(err_str, sizeof(err_str),
					 "enable/disable %s remote configuration ignored",
					 keyword((yyvsp[0].Integer)));
				yyerror(err_str);
			}
		}
#line 2863 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 213:
#line 1137 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.tinker, (yyvsp[0].Attr_val_fifo)); }
#line 2869 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 214:
#line 1142 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2878 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 215:
#line 1147 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 2887 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 216:
#line 1155 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-1].Integer), (yyvsp[0].Double)); }
#line 2893 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 229:
#line 1180 "ntp_parser.y" /* yacc.c:1646  */
    {
			attr_val *av;

			av = create_attr_dval((yyvsp[-1].Integer), (yyvsp[0].Double));
			APPEND_G_FIFO(cfgt.vars, av);
		}
#line 2904 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 230:
#line 1187 "ntp_parser.y" /* yacc.c:1646  */
    {
			attr_val *av;

			av = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer));
			APPEND_G_FIFO(cfgt.vars, av);
		}
#line 2915 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 231:
#line 1194 "ntp_parser.y" /* yacc.c:1646  */
    {
			attr_val *av;

			av = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String));
			APPEND_G_FIFO(cfgt.vars, av);
		}
#line 2926 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 232:
#line 1201 "ntp_parser.y" /* yacc.c:1646  */
    {
			char error_text[64];
			attr_val *av;

			if (lex_from_file()) {
				av = create_attr_sval((yyvsp[-1].Integer), (yyvsp[0].String));
				APPEND_G_FIFO(cfgt.vars, av);
			} else {
				YYFREE((yyvsp[0].String));
				snprintf(error_text, sizeof(error_text),
					 "%s remote config ignored",
					 keyword((yyvsp[-1].Integer)));
				yyerror(error_text);
			}
		}
#line 2946 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 233:
#line 1217 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (!lex_from_file()) {
				YYFREE((yyvsp[-1].String)); /* avoid leak */
				yyerror("remote includefile ignored");
				break;
			}
			if (lex_level() > MAXINCLUDELEVEL) {
				fprintf(stderr, "getconfig: Maximum include file level exceeded.\n");
				msyslog(LOG_ERR, "getconfig: Maximum include file level exceeded.");
			} else {
				const char * path = FindConfig((yyvsp[-1].String)); /* might return $2! */
				if (!lex_push_file(path, "r")) {
					fprintf(stderr, "getconfig: Couldn't open <%s>\n", path);
					msyslog(LOG_ERR, "getconfig: Couldn't open <%s>", path);
				}
			}
			YYFREE((yyvsp[-1].String)); /* avoid leak */
		}
#line 2969 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 234:
#line 1236 "ntp_parser.y" /* yacc.c:1646  */
    { lex_flush_stack(); }
#line 2975 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 235:
#line 1238 "ntp_parser.y" /* yacc.c:1646  */
    { /* see drift_parm below for actions */ }
#line 2981 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 236:
#line 1240 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.logconfig, (yyvsp[0].Attr_val_fifo)); }
#line 2987 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 237:
#line 1242 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.phone, (yyvsp[0].String_fifo)); }
#line 2993 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 238:
#line 1244 "ntp_parser.y" /* yacc.c:1646  */
    { APPEND_G_FIFO(cfgt.setvar, (yyvsp[0].Set_var)); }
#line 2999 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 239:
#line 1246 "ntp_parser.y" /* yacc.c:1646  */
    {
			addr_opts_node *aon;

			aon = create_addr_opts_node((yyvsp[-1].Address_node), (yyvsp[0].Attr_val_fifo));
			APPEND_G_FIFO(cfgt.trap, aon);
		}
#line 3010 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 240:
#line 1253 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.ttl, (yyvsp[0].Attr_val_fifo)); }
#line 3016 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 245:
#line 1268 "ntp_parser.y" /* yacc.c:1646  */
    {
#ifndef LEAP_SMEAR
			yyerror("Built without LEAP_SMEAR support.");
#endif
		}
#line 3026 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 251:
#line 1288 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				attr_val *av;
				av = create_attr_sval(T_Driftfile, (yyvsp[0].String));
				APPEND_G_FIFO(cfgt.vars, av);
			} else {
				YYFREE((yyvsp[0].String));
				yyerror("driftfile remote configuration ignored");
			}
		}
#line 3041 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 252:
#line 1299 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				attr_val *av;
				av = create_attr_sval(T_Driftfile, (yyvsp[-1].String));
				APPEND_G_FIFO(cfgt.vars, av);
				av = create_attr_dval(T_WanderThreshold, (yyvsp[0].Double));
				APPEND_G_FIFO(cfgt.vars, av);
			msyslog(LOG_WARNING,
				"'driftfile FILENAME WanderValue' is deprecated, "
				"please use separate 'driftfile FILENAME' and "
				"'nonvolatile WanderValue' lines instead.");
			} else {
				YYFREE((yyvsp[-1].String));
				yyerror("driftfile remote configuration ignored");
			}
		}
#line 3062 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 253:
#line 1316 "ntp_parser.y" /* yacc.c:1646  */
    {
			if (lex_from_file()) {
				attr_val *av;
				av = create_attr_sval(T_Driftfile, estrdup(""));
				APPEND_G_FIFO(cfgt.vars, av);
			} else {
				yyerror("driftfile remote configuration ignored");
			}
		}
#line 3076 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 254:
#line 1329 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Set_var) = create_setvar_node((yyvsp[-3].String), (yyvsp[-1].String), (yyvsp[0].Integer)); }
#line 3082 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 256:
#line 1335 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = 0; }
#line 3088 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 257:
#line 1340 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val_fifo) = NULL; }
#line 3094 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 258:
#line 1342 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 3103 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 259:
#line 1350 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival((yyvsp[-1].Integer), (yyvsp[0].Integer)); }
#line 3109 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 260:
#line 1352 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val) = create_attr_sval((yyvsp[-1].Integer), estrdup((yyvsp[0].Address_node)->address));
			destroy_address_node((yyvsp[0].Address_node));
		}
#line 3118 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 261:
#line 1360 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 3127 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 262:
#line 1365 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 3136 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 263:
#line 1373 "ntp_parser.y" /* yacc.c:1646  */
    {
			char	prefix;
			char *	type;

			switch ((yyvsp[0].String)[0]) {

			case '+':
			case '-':
			case '=':
				prefix = (yyvsp[0].String)[0];
				type = (yyvsp[0].String) + 1;
				break;

			default:
				prefix = '=';
				type = (yyvsp[0].String);
			}

			(yyval.Attr_val) = create_attr_sval(prefix, estrdup(type));
			YYFREE((yyvsp[0].String));
		}
#line 3162 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 264:
#line 1398 "ntp_parser.y" /* yacc.c:1646  */
    {
			nic_rule_node *nrn;

			nrn = create_nic_rule_node((yyvsp[0].Integer), NULL, (yyvsp[-1].Integer));
			APPEND_G_FIFO(cfgt.nic_rules, nrn);
		}
#line 3173 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 265:
#line 1405 "ntp_parser.y" /* yacc.c:1646  */
    {
			nic_rule_node *nrn;

			nrn = create_nic_rule_node(0, (yyvsp[0].String), (yyvsp[-1].Integer));
			APPEND_G_FIFO(cfgt.nic_rules, nrn);
		}
#line 3184 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 275:
#line 1433 "ntp_parser.y" /* yacc.c:1646  */
    { CONCAT_G_FIFOS(cfgt.reset_counters, (yyvsp[0].Int_fifo)); }
#line 3190 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 276:
#line 1438 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Int_fifo) = (yyvsp[-1].Int_fifo);
			APPEND_G_FIFO((yyval.Int_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 3199 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 277:
#line 1443 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Int_fifo) = NULL;
			APPEND_G_FIFO((yyval.Int_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 3208 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 285:
#line 1467 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 3217 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 286:
#line 1472 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), create_int_node((yyvsp[0].Integer)));
		}
#line 3226 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 287:
#line 1480 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-1].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 3235 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 288:
#line 1485 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[0].Attr_val));
		}
#line 3244 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 289:
#line 1493 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_ival('i', (yyvsp[0].Integer)); }
#line 3250 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 291:
#line 1499 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_rangeval('-', (yyvsp[-3].Integer), (yyvsp[-1].Integer)); }
#line 3256 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 292:
#line 1504 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.String_fifo) = (yyvsp[-1].String_fifo);
			APPEND_G_FIFO((yyval.String_fifo), create_string_node((yyvsp[0].String)));
		}
#line 3265 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 293:
#line 1509 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.String_fifo) = NULL;
			APPEND_G_FIFO((yyval.String_fifo), create_string_node((yyvsp[0].String)));
		}
#line 3274 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 294:
#line 1517 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Address_fifo) = (yyvsp[-1].Address_fifo);
			APPEND_G_FIFO((yyval.Address_fifo), (yyvsp[0].Address_node));
		}
#line 3283 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 295:
#line 1522 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Address_fifo) = NULL;
			APPEND_G_FIFO((yyval.Address_fifo), (yyvsp[0].Address_node));
		}
#line 3292 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 296:
#line 1530 "ntp_parser.y" /* yacc.c:1646  */
    {
			if ((yyvsp[0].Integer) != 0 && (yyvsp[0].Integer) != 1) {
				yyerror("Integer value is not boolean (0 or 1). Assuming 1");
				(yyval.Integer) = 1;
			} else {
				(yyval.Integer) = (yyvsp[0].Integer);
			}
		}
#line 3305 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 297:
#line 1538 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = 1; }
#line 3311 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 298:
#line 1539 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = 0; }
#line 3317 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 299:
#line 1543 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Double) = (double)(yyvsp[0].Integer); }
#line 3323 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 301:
#line 1549 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Integer) = basedate_eval_string((yyvsp[0].String)); YYFREE((yyvsp[0].String)); }
#line 3329 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 302:
#line 1557 "ntp_parser.y" /* yacc.c:1646  */
    {
			sim_node *sn;

			sn =  create_sim_node((yyvsp[-2].Attr_val_fifo), (yyvsp[-1].Sim_server_fifo));
			APPEND_G_FIFO(cfgt.sim_details, sn);

			/* Revert from ; to \n for end-of-command */
			old_config_style = 1;
		}
#line 3343 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 303:
#line 1574 "ntp_parser.y" /* yacc.c:1646  */
    { old_config_style = 0; }
#line 3349 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 304:
#line 1579 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-2].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[-1].Attr_val));
		}
#line 3358 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 305:
#line 1584 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[-1].Attr_val));
		}
#line 3367 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 306:
#line 1592 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-2].Integer), (yyvsp[0].Double)); }
#line 3373 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 309:
#line 1602 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Sim_server_fifo) = (yyvsp[-1].Sim_server_fifo);
			APPEND_G_FIFO((yyval.Sim_server_fifo), (yyvsp[0].Sim_server));
		}
#line 3382 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 310:
#line 1607 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Sim_server_fifo) = NULL;
			APPEND_G_FIFO((yyval.Sim_server_fifo), (yyvsp[0].Sim_server));
		}
#line 3391 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 311:
#line 1615 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Sim_server) = ONLY_SIM(create_sim_server((yyvsp[-4].Address_node), (yyvsp[-2].Double), (yyvsp[-1].Sim_script_fifo))); }
#line 3397 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 312:
#line 1620 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Double) = (yyvsp[-1].Double); }
#line 3403 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 313:
#line 1625 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Address_node) = (yyvsp[0].Address_node); }
#line 3409 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 314:
#line 1630 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Sim_script_fifo) = (yyvsp[-1].Sim_script_fifo);
			APPEND_G_FIFO((yyval.Sim_script_fifo), (yyvsp[0].Sim_script));
		}
#line 3418 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 315:
#line 1635 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Sim_script_fifo) = NULL;
			APPEND_G_FIFO((yyval.Sim_script_fifo), (yyvsp[0].Sim_script));
		}
#line 3427 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 316:
#line 1643 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Sim_script) = ONLY_SIM(create_sim_script_info((yyvsp[-3].Double), (yyvsp[-1].Attr_val_fifo))); }
#line 3433 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 317:
#line 1648 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = (yyvsp[-2].Attr_val_fifo);
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[-1].Attr_val));
		}
#line 3442 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 318:
#line 1653 "ntp_parser.y" /* yacc.c:1646  */
    {
			(yyval.Attr_val_fifo) = NULL;
			APPEND_G_FIFO((yyval.Attr_val_fifo), (yyvsp[-1].Attr_val));
		}
#line 3451 "ntp_parser.c" /* yacc.c:1646  */
    break;

  case 319:
#line 1661 "ntp_parser.y" /* yacc.c:1646  */
    { (yyval.Attr_val) = create_attr_dval((yyvsp[-2].Integer), (yyvsp[0].Double)); }
#line 3457 "ntp_parser.c" /* yacc.c:1646  */
    break;


#line 3461 "ntp_parser.c" /* yacc.c:1646  */
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
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

  /* Else will try to reuse lookahead token after shifting the error
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

  /* Do not reclaim the symbols of the rule whose action triggered
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
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
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

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


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

#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
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
  return yyresult;
}
#line 1672 "ntp_parser.y" /* yacc.c:1906  */


void
yyerror(
	const char *msg
	)
{
	int retval;
	struct FILE_INFO * ip_ctx;

	ip_ctx = lex_current();
	ip_ctx->errpos = ip_ctx->tokpos;

	msyslog(LOG_ERR, "line %d column %d %s",
		ip_ctx->errpos.nline, ip_ctx->errpos.ncol, msg);
	if (!lex_from_file()) {
		/* Save the error message in the correct buffer */
		retval = snprintf(remote_config.err_msg + remote_config.err_pos,
				  MAXLINE - remote_config.err_pos,
				  "column %d %s",
				  ip_ctx->errpos.ncol, msg);

		/* Increment the value of err_pos */
		if (retval > 0)
			remote_config.err_pos += retval;

		/* Increment the number of errors */
		++remote_config.no_errors;
	}
}


/*
 * token_name - convert T_ token integers to text
 *		example: token_name(T_Server) returns "T_Server"
 */
const char *
token_name(
	int token
	)
{
	return yytname[YYTRANSLATE(token)];
}


/* Initial Testing function -- ignore */
#if 0
int main(int argc, char *argv[])
{
	ip_file = FOPEN(argv[1], "r");
	if (!ip_file)
		fprintf(stderr, "ERROR!! Could not open file: %s\n", argv[1]);
	yyparse();
	return 0;
}
#endif

