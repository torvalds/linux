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

#ifndef yyparse
#define yyparse    calc1_parse
#endif /* yyparse */

#ifndef yylex
#define yylex      calc1_lex
#endif /* yylex */

#ifndef yyerror
#define yyerror    calc1_error
#endif /* yyerror */

#ifndef yychar
#define yychar     calc1_char
#endif /* yychar */

#ifndef yyval
#define yyval      calc1_val
#endif /* yyval */

#ifndef yylval
#define yylval     calc1_lval
#endif /* yylval */

#ifndef yydebug
#define yydebug    calc1_debug
#endif /* yydebug */

#ifndef yynerrs
#define yynerrs    calc1_nerrs
#endif /* yynerrs */

#ifndef yyerrflag
#define yyerrflag  calc1_errflag
#endif /* yyerrflag */

#ifndef yylhs
#define yylhs      calc1_lhs
#endif /* yylhs */

#ifndef yylen
#define yylen      calc1_len
#endif /* yylen */

#ifndef yydefred
#define yydefred   calc1_defred
#endif /* yydefred */

#ifndef yydgoto
#define yydgoto    calc1_dgoto
#endif /* yydgoto */

#ifndef yysindex
#define yysindex   calc1_sindex
#endif /* yysindex */

#ifndef yyrindex
#define yyrindex   calc1_rindex
#endif /* yyrindex */

#ifndef yygindex
#define yygindex   calc1_gindex
#endif /* yygindex */

#ifndef yytable
#define yytable    calc1_table
#endif /* yytable */

#ifndef yycheck
#define yycheck    calc1_check
#endif /* yycheck */

#ifndef yyname
#define yyname     calc1_name
#endif /* yyname */

#ifndef yyrule
#define yyrule     calc1_rule
#endif /* yyrule */
#define YYPREFIX "calc1_"

#define YYPURE 0

#line 2 "calc1.y"

/* http://dinosaur.compilertools.net/yacc/index.html */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

typedef struct interval
{
    double lo, hi;
}
INTERVAL;

INTERVAL vmul(double, double, INTERVAL);
INTERVAL vdiv(double, double, INTERVAL);

extern int yylex(void);
static void yyerror(const char *s);

int dcheck(INTERVAL);

double dreg[26];
INTERVAL vreg[26];

#ifdef YYSTYPE
#undef  YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#endif
#ifndef YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#line 31 "calc1.y"
typedef union
{
	int ival;
	double dval;
	INTERVAL vval;
} YYSTYPE;
#endif /* !YYSTYPE_IS_DECLARED */
#line 141 "calc1.tab.c"

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

#define DREG 257
#define VREG 258
#define CONST 259
#define UMINUS 260
#define YYERRCODE 256
typedef int YYINT;
static const YYINT calc1_lhs[] = {                       -1,
    3,    3,    0,    0,    0,    0,    0,    1,    1,    1,
    1,    1,    1,    1,    1,    2,    2,    2,    2,    2,
    2,    2,    2,    2,    2,    2,    2,    2,
};
static const YYINT calc1_len[] = {                        2,
    0,    2,    2,    2,    4,    4,    2,    1,    1,    3,
    3,    3,    3,    2,    3,    1,    5,    1,    3,    3,
    3,    3,    3,    3,    3,    3,    2,    3,
};
static const YYINT calc1_defred[] = {                     0,
    0,    0,    0,    8,    0,    0,    0,    0,    0,    7,
    0,    0,    9,   18,   14,   27,    0,    0,    0,    0,
    0,    0,    3,    0,    0,    0,    0,    4,    0,    0,
    0,    0,    0,   15,    0,   28,    0,    0,    0,    0,
   12,   24,   13,   26,    0,    0,   23,   25,   14,    0,
    0,    0,    0,    0,    5,    6,    0,    0,    0,   12,
   13,   17,
};
static const YYINT calc1_dgoto[] = {                      7,
   32,    9,    0,
};
static const YYINT calc1_sindex[] = {                   -40,
   -8,  -48,  -47,    0,  -37,  -37,    0,    2,   17,    0,
  -34,  -37,    0,    0,    0,    0,  -25,   90,  -37,  -37,
  -37,  -37,    0,  -37,  -37,  -37,  -37,    0,  -34,  -34,
   25,  125,   31,    0,  -34,    0,  -11,   37,  -11,   37,
    0,    0,    0,    0,   37,   37,    0,    0,    0,  111,
  -34,  -34,  -34,  -34,    0,    0,  118,   69,   69,    0,
    0,    0,
};
static const YYINT calc1_rindex[] = {                     0,
    0,   38,   44,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,   -9,    0,    0,    0,    0,   51,   -3,   56,   61,
    0,    0,    0,    0,   67,   72,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,   78,   83,    0,
    0,    0,
};
static const YYINT calc1_gindex[] = {                     0,
    4,  124,    0,
};
#define YYTABLESIZE 225
static const YYINT calc1_table[] = {                      6,
   16,   10,    6,    8,    5,   30,   20,    5,   15,   17,
   29,   23,   11,   12,   31,   34,   21,   19,   35,   20,
    0,   22,   37,   39,   41,   43,   28,    0,    0,    0,
   21,   16,   49,   50,   55,   22,    0,   20,   57,   20,
   56,   20,    0,   21,   19,    0,   20,    9,   22,    0,
    0,    0,    0,   18,   58,   59,   60,   61,   26,   24,
   10,   25,    0,   27,    0,   11,   53,   51,    0,   52,
   22,   54,   26,   24,    0,   25,   19,   27,   26,    9,
    9,   21,    9,   27,    9,   18,   18,   10,   18,    0,
   18,   10,   11,   10,   10,   10,   11,    0,   11,   11,
   11,   22,    0,   22,    0,   22,    0,   19,    0,   19,
   53,   19,   21,    0,   21,   54,   21,    0,   10,    0,
   10,    0,   10,   11,    0,   11,    0,   11,   16,   18,
   36,   26,   24,    0,   25,   33,   27,    0,    0,    0,
    0,    0,   38,   40,   42,   44,    0,   45,   46,   47,
   48,   34,   53,   51,    0,   52,    0,   54,   62,   53,
   51,    0,   52,    0,   54,    0,   21,   19,    0,   20,
    0,   22,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    1,    2,    3,    4,   13,
   14,    4,   13,    0,    4,
};
static const YYINT calc1_check[] = {                     40,
   10,   10,   40,    0,   45,   40,   10,   45,    5,    6,
   45,   10,   61,   61,   11,   41,   42,   43,   44,   45,
   -1,   47,   19,   20,   21,   22,   10,   -1,   -1,   -1,
   42,   41,   29,   30,   10,   47,   -1,   41,   35,   43,
   10,   45,   -1,   42,   43,   -1,   45,   10,   47,   -1,
   -1,   -1,   -1,   10,   51,   52,   53,   54,   42,   43,
   10,   45,   -1,   47,   -1,   10,   42,   43,   -1,   45,
   10,   47,   42,   43,   -1,   45,   10,   47,   42,   42,
   43,   10,   45,   47,   47,   42,   43,   10,   45,   -1,
   47,   41,   10,   43,   44,   45,   41,   -1,   43,   44,
   45,   41,   -1,   43,   -1,   45,   -1,   41,   -1,   43,
   42,   45,   41,   -1,   43,   47,   45,   -1,   41,   -1,
   43,   -1,   45,   41,   -1,   43,   -1,   45,    5,    6,
   41,   42,   43,   -1,   45,   12,   47,   -1,   -1,   -1,
   -1,   -1,   19,   20,   21,   22,   -1,   24,   25,   26,
   27,   41,   42,   43,   -1,   45,   -1,   47,   41,   42,
   43,   -1,   45,   -1,   47,   -1,   42,   43,   -1,   45,
   -1,   47,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  256,  257,  258,  259,  257,
  258,  259,  257,   -1,  259,
};
#define YYFINAL 7
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 260
#define YYUNDFTOKEN 266
#define YYTRANSLATE(a) ((a) > YYMAXTOKEN ? YYUNDFTOKEN : (a))
#if YYDEBUG
static const char *const calc1_name[] = {

"end-of-file",0,0,0,0,0,0,0,0,0,"'\\n'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,"'('","')'","'*'","'+'","','","'-'",0,"'/'",0,0,0,0,0,0,0,0,0,
0,0,0,0,"'='",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,"DREG","VREG","CONST","UMINUS",0,0,0,0,0,"illegal-symbol",
};
static const char *const calc1_rule[] = {
"$accept : line",
"lines :",
"lines : lines line",
"line : dexp '\\n'",
"line : vexp '\\n'",
"line : DREG '=' dexp '\\n'",
"line : VREG '=' vexp '\\n'",
"line : error '\\n'",
"dexp : CONST",
"dexp : DREG",
"dexp : dexp '+' dexp",
"dexp : dexp '-' dexp",
"dexp : dexp '*' dexp",
"dexp : dexp '/' dexp",
"dexp : '-' dexp",
"dexp : '(' dexp ')'",
"vexp : dexp",
"vexp : '(' dexp ',' dexp ')'",
"vexp : VREG",
"vexp : vexp '+' vexp",
"vexp : dexp '+' vexp",
"vexp : vexp '-' vexp",
"vexp : dexp '-' vexp",
"vexp : vexp '*' vexp",
"vexp : dexp '*' vexp",
"vexp : vexp '/' vexp",
"vexp : dexp '/' vexp",
"vexp : '-' vexp",
"vexp : '(' vexp ')'",

};
#endif

int      yydebug;
int      yynerrs;

int      yyerrflag;
int      yychar;
YYSTYPE  yyval;
YYSTYPE  yylval;

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

#define YYINITSTACKSIZE 200

typedef struct {
    unsigned stacksize;
    YYINT    *s_base;
    YYINT    *s_mark;
    YYINT    *s_last;
    YYSTYPE  *l_base;
    YYSTYPE  *l_mark;
} YYSTACKDATA;
/* variables for the parser stack */
static YYSTACKDATA yystack;
#line 176 "calc1.y"
	/* beginning of subroutines section */

#define BSZ 50			/* buffer size for floating point numbers */

	/* lexical analysis */

static void
yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
}

int
yylex(void)
{
    int c;

    while ((c = getchar()) == ' ')
    {				/* skip over blanks */
    }

    if (isupper(c))
    {
	yylval.ival = c - 'A';
	return (VREG);
    }
    if (islower(c))
    {
	yylval.ival = c - 'a';
	return (DREG);
    }

    if (isdigit(c) || c == '.')
    {
	/* gobble up digits, points, exponents */
	char buf[BSZ + 1], *cp = buf;
	int dot = 0, expr = 0;

	for (; (cp - buf) < BSZ; ++cp, c = getchar())
	{

	    *cp = (char) c;
	    if (isdigit(c))
		continue;
	    if (c == '.')
	    {
		if (dot++ || expr)
		    return ('.');	/* will cause syntax error */
		continue;
	    }

	    if (c == 'e')
	    {
		if (expr++)
		    return ('e');	/*  will  cause  syntax  error  */
		continue;
	    }

	    /*  end  of  number  */
	    break;
	}
	*cp = '\0';

	if ((cp - buf) >= BSZ)
	    printf("constant  too  long:  truncated\n");
	else
	    ungetc(c, stdin);	/*  push  back  last  char  read  */
	yylval.dval = atof(buf);
	return (CONST);
    }
    return (c);
}

static INTERVAL
hilo(double a, double b, double c, double d)
{
    /*  returns  the  smallest  interval  containing  a,  b,  c,  and  d  */
    /*  used  by  *,  /  routines  */
    INTERVAL v;

    if (a > b)
    {
	v.hi = a;
	v.lo = b;
    }
    else
    {
	v.hi = b;
	v.lo = a;
    }

    if (c > d)
    {
	if (c > v.hi)
	    v.hi = c;
	if (d < v.lo)
	    v.lo = d;
    }
    else
    {
	if (d > v.hi)
	    v.hi = d;
	if (c < v.lo)
	    v.lo = c;
    }
    return (v);
}

INTERVAL
vmul(double a, double b, INTERVAL v)
{
    return (hilo(a * v.hi, a * v.lo, b * v.hi, b * v.lo));
}

int
dcheck(INTERVAL v)
{
    if (v.hi >= 0. && v.lo <= 0.)
    {
	printf("divisor  interval  contains  0.\n");
	return (1);
    }
    return (0);
}

INTERVAL
vdiv(double a, double b, INTERVAL v)
{
    return (hilo(a / v.hi, a / v.lo, b / v.hi, b / v.lo));
}
#line 491 "calc1.tab.c"

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

    data->stacksize = newsize;
    data->s_last = data->s_base + newsize - 1;
    return 0;
}

#if YYPURE || defined(YY_NO_LEAKS)
static void yyfreestack(YYSTACKDATA *data)
{
    free(data->s_base);
    free(data->l_base);
    memset(data, 0, sizeof(*data));
}
#else
#define yyfreestack(data) /* nothing */
#endif

#define YYABORT  goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR  goto yyerrlab

int
YYPARSE_DECL()
{
    int yym, yyn, yystate;
#if YYDEBUG
    const char *yys;

    if ((yys = getenv("YYDEBUG")) != 0)
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

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
    yystate = 0;
    *yystack.s_mark = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
        yychar = YYLEX;
        if (yychar < 0) yychar = YYEOF;
#if YYDEBUG
        if (yydebug)
        {
            if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if (((yyn = yysindex[yystate]) != 0) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
        yystate = yytable[yyn];
        *++yystack.s_mark = yytable[yyn];
        *++yystack.l_mark = yylval;
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

    YYERROR_CALL("syntax error");

    goto yyerrlab; /* redundant goto avoids 'unused label' warning */
yyerrlab:
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
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yystack.s_mark, yytable[yyn]);
#endif
                if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
                yystate = yytable[yyn];
                *++yystack.s_mark = yytable[yyn];
                *++yystack.l_mark = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yystack.s_mark);
#endif
                if (yystack.s_mark <= yystack.s_base) goto yyabort;
                --yystack.s_mark;
                --yystack.l_mark;
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
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = YYEMPTY;
        goto yyloop;
    }

yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    if (yym > 0)
        yyval = yystack.l_mark[1-yym];
    else
        memset(&yyval, 0, sizeof yyval);

    switch (yyn)
    {
case 3:
#line 57 "calc1.y"
	{
		(void) printf("%15.8f\n", yystack.l_mark[-1].dval);
	}
break;
case 4:
#line 61 "calc1.y"
	{
		(void) printf("(%15.8f, %15.8f)\n", yystack.l_mark[-1].vval.lo, yystack.l_mark[-1].vval.hi);
	}
break;
case 5:
#line 65 "calc1.y"
	{
		dreg[yystack.l_mark[-3].ival] = yystack.l_mark[-1].dval;
	}
break;
case 6:
#line 69 "calc1.y"
	{
		vreg[yystack.l_mark[-3].ival] = yystack.l_mark[-1].vval;
	}
break;
case 7:
#line 73 "calc1.y"
	{
		yyerrok;
	}
break;
case 9:
#line 80 "calc1.y"
	{
		yyval.dval = dreg[yystack.l_mark[0].ival];
	}
break;
case 10:
#line 84 "calc1.y"
	{
		yyval.dval = yystack.l_mark[-2].dval + yystack.l_mark[0].dval;
	}
break;
case 11:
#line 88 "calc1.y"
	{
		yyval.dval = yystack.l_mark[-2].dval - yystack.l_mark[0].dval;
	}
break;
case 12:
#line 92 "calc1.y"
	{
		yyval.dval = yystack.l_mark[-2].dval * yystack.l_mark[0].dval;
	}
break;
case 13:
#line 96 "calc1.y"
	{
		yyval.dval = yystack.l_mark[-2].dval / yystack.l_mark[0].dval;
	}
break;
case 14:
#line 100 "calc1.y"
	{
		yyval.dval = -yystack.l_mark[0].dval;
	}
break;
case 15:
#line 104 "calc1.y"
	{
		yyval.dval = yystack.l_mark[-1].dval;
	}
break;
case 16:
#line 110 "calc1.y"
	{
		yyval.vval.hi = yyval.vval.lo = yystack.l_mark[0].dval;
	}
break;
case 17:
#line 114 "calc1.y"
	{
		yyval.vval.lo = yystack.l_mark[-3].dval;
		yyval.vval.hi = yystack.l_mark[-1].dval;
		if ( yyval.vval.lo > yyval.vval.hi ) 
		{
			(void) printf("interval out of order\n");
			YYERROR;
		}
	}
break;
case 18:
#line 124 "calc1.y"
	{
		yyval.vval = vreg[yystack.l_mark[0].ival];
	}
break;
case 19:
#line 128 "calc1.y"
	{
		yyval.vval.hi = yystack.l_mark[-2].vval.hi + yystack.l_mark[0].vval.hi;
		yyval.vval.lo = yystack.l_mark[-2].vval.lo + yystack.l_mark[0].vval.lo;
	}
break;
case 20:
#line 133 "calc1.y"
	{
		yyval.vval.hi = yystack.l_mark[-2].dval + yystack.l_mark[0].vval.hi;
		yyval.vval.lo = yystack.l_mark[-2].dval + yystack.l_mark[0].vval.lo;
	}
break;
case 21:
#line 138 "calc1.y"
	{
		yyval.vval.hi = yystack.l_mark[-2].vval.hi - yystack.l_mark[0].vval.lo;
		yyval.vval.lo = yystack.l_mark[-2].vval.lo - yystack.l_mark[0].vval.hi;
	}
break;
case 22:
#line 143 "calc1.y"
	{
		yyval.vval.hi = yystack.l_mark[-2].dval - yystack.l_mark[0].vval.lo;
		yyval.vval.lo = yystack.l_mark[-2].dval - yystack.l_mark[0].vval.hi;
	}
break;
case 23:
#line 148 "calc1.y"
	{
		yyval.vval = vmul( yystack.l_mark[-2].vval.lo, yystack.l_mark[-2].vval.hi, yystack.l_mark[0].vval );
	}
break;
case 24:
#line 152 "calc1.y"
	{
		yyval.vval = vmul (yystack.l_mark[-2].dval, yystack.l_mark[-2].dval, yystack.l_mark[0].vval );
	}
break;
case 25:
#line 156 "calc1.y"
	{
		if (dcheck(yystack.l_mark[0].vval)) YYERROR;
		yyval.vval = vdiv ( yystack.l_mark[-2].vval.lo, yystack.l_mark[-2].vval.hi, yystack.l_mark[0].vval );
	}
break;
case 26:
#line 161 "calc1.y"
	{
		if (dcheck ( yystack.l_mark[0].vval )) YYERROR;
		yyval.vval = vdiv (yystack.l_mark[-2].dval, yystack.l_mark[-2].dval, yystack.l_mark[0].vval );
	}
break;
case 27:
#line 166 "calc1.y"
	{
		yyval.vval.hi = -yystack.l_mark[0].vval.lo;
		yyval.vval.lo = -yystack.l_mark[0].vval.hi;
	}
break;
case 28:
#line 171 "calc1.y"
	{
		yyval.vval = yystack.l_mark[-1].vval;
	}
break;
#line 853 "calc1.tab.c"
    }
    yystack.s_mark -= yym;
    yystate = *yystack.s_mark;
    yystack.l_mark -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yystack.s_mark = YYFINAL;
        *++yystack.l_mark = yyval;
        if (yychar < 0)
        {
            yychar = YYLEX;
            if (yychar < 0) yychar = YYEOF;
#if YYDEBUG
            if (yydebug)
            {
                if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
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
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yystack.s_mark, yystate);
#endif
    if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;
    *++yystack.s_mark = (YYINT) yystate;
    *++yystack.l_mark = yyval;
    goto yyloop;

yyoverflow:
    YYERROR_CALL("yacc stack overflow");

yyabort:
    yyfreestack(&yystack);
    return (1);

yyaccept:
    yyfreestack(&yystack);
    return (0);
}
