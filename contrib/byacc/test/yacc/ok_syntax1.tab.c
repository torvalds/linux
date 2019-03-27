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
#define yyparse    ok_syntax1_parse
#endif /* yyparse */

#ifndef yylex
#define yylex      ok_syntax1_lex
#endif /* yylex */

#ifndef yyerror
#define yyerror    ok_syntax1_error
#endif /* yyerror */

#ifndef yychar
#define yychar     ok_syntax1_char
#endif /* yychar */

#ifndef yyval
#define yyval      ok_syntax1_val
#endif /* yyval */

#ifndef yylval
#define yylval     ok_syntax1_lval
#endif /* yylval */

#ifndef yydebug
#define yydebug    ok_syntax1_debug
#endif /* yydebug */

#ifndef yynerrs
#define yynerrs    ok_syntax1_nerrs
#endif /* yynerrs */

#ifndef yyerrflag
#define yyerrflag  ok_syntax1_errflag
#endif /* yyerrflag */

#ifndef yylhs
#define yylhs      ok_syntax1_lhs
#endif /* yylhs */

#ifndef yylen
#define yylen      ok_syntax1_len
#endif /* yylen */

#ifndef yydefred
#define yydefred   ok_syntax1_defred
#endif /* yydefred */

#ifndef yydgoto
#define yydgoto    ok_syntax1_dgoto
#endif /* yydgoto */

#ifndef yysindex
#define yysindex   ok_syntax1_sindex
#endif /* yysindex */

#ifndef yyrindex
#define yyrindex   ok_syntax1_rindex
#endif /* yyrindex */

#ifndef yygindex
#define yygindex   ok_syntax1_gindex
#endif /* yygindex */

#ifndef yytable
#define yytable    ok_syntax1_table
#endif /* yytable */

#ifndef yycheck
#define yycheck    ok_syntax1_check
#endif /* yycheck */

#ifndef yyname
#define yyname     ok_syntax1_name
#endif /* yyname */

#ifndef yyrule
#define yyrule     ok_syntax1_rule
#endif /* yyrule */
#define YYPREFIX "ok_syntax1_"

#define YYPURE 1

#line 9 "ok_syntax1.y"
# include <stdio.h>
# include <ctype.h>

#ifdef YYBISON
#define YYSTYPE int
#define YYLEX_PARAM base
#define YYLEX_DECL() yylex(YYSTYPE *yylval, int *YYLEX_PARAM)
#define YYERROR_DECL() yyerror(int regs[26], int *base, const char *s)
int YYLEX_DECL();
static void YYERROR_DECL();
#endif

#ifdef YYSTYPE
#undef  YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#endif
#ifndef YYSTYPE_IS_DECLARED
#define YYSTYPE_IS_DECLARED 1
#line 43 "ok_syntax1.y"
typedef union
{
    char *	cval;
    int		ival;
    double	dval;
} YYSTYPE;
#endif /* !YYSTYPE_IS_DECLARED */
#line 128 "ok_syntax1.tab.c"

/* compatibility with bison */
#ifdef YYPARSE_PARAM
/* compatibility with FreeBSD */
# ifdef YYPARSE_PARAM_TYPE
#  define YYPARSE_DECL() yyparse(YYPARSE_PARAM_TYPE YYPARSE_PARAM)
# else
#  define YYPARSE_DECL() yyparse(void *YYPARSE_PARAM)
# endif
#else
# define YYPARSE_DECL() yyparse(int regs[26], int *base)
#endif

/* Parameters sent to lex. */
#ifdef YYLEX_PARAM
# ifdef YYLEX_PARAM_TYPE
#  define YYLEX_DECL() yylex(YYSTYPE *yylval, YYLEX_PARAM_TYPE YYLEX_PARAM)
# else
#  define YYLEX_DECL() yylex(YYSTYPE *yylval, void * YYLEX_PARAM)
# endif
# define YYLEX yylex(&yylval, YYLEX_PARAM)
#else
# define YYLEX_DECL() yylex(YYSTYPE *yylval, int *base)
# define YYLEX yylex(&yylval, base)
#endif

/* Parameters sent to yyerror. */
#ifndef YYERROR_DECL
#define YYERROR_DECL() yyerror(int regs[26], int *base, const char *s)
#endif
#ifndef YYERROR_CALL
#define YYERROR_CALL(msg) yyerror(regs, base, msg)
#endif

extern int YYPARSE_DECL();

#define DIGIT 257
#define LETTER 258
#define OCT1 259
#define HEX1 260
#define HEX2 261
#define HEX3 262
#define STR1 263
#define STR2 265
#define BELL 266
#define BS 267
#define NL 268
#define LF 269
#define CR 270
#define TAB 271
#define VT 272
#define UMINUS 273
#define YYERRCODE 256
typedef int YYINT;
static const YYINT ok_syntax1_lhs[] = {                  -1,
    0,    0,    0,    1,    1,    2,    2,    2,    2,    2,
    2,    2,    2,    2,    2,    2,    3,    3,
};
static const YYINT ok_syntax1_len[] = {                   2,
    0,    3,    3,    1,    3,    3,    3,    3,    3,    3,
    3,    3,    3,    2,    1,    1,    1,    2,
};
static const YYINT ok_syntax1_defred[] = {                1,
    0,    0,   17,    0,    0,    0,    0,    0,    0,    3,
    0,   15,   14,    0,    2,    0,    0,    0,    0,    0,
    0,    0,   18,    0,    6,    0,    0,    0,    0,    9,
   10,   11,
};
static const YYINT ok_syntax1_dgoto[] = {                 1,
    7,    8,    9,
};
static const YYINT ok_syntax1_sindex[] = {                0,
  -40,   -7,    0,  -55,  -38,  -38,    1,  -29, -247,    0,
  -38,    0,    0,   22,    0,  -38,  -38,  -38,  -38,  -38,
  -38,  -38,    0,  -29,    0,   51,   60,  -20,  -20,    0,
    0,    0,
};
static const YYINT ok_syntax1_rindex[] = {                0,
    0,    0,    0,    2,    0,    0,    0,    9,   -9,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,   10,    0,   -6,   14,    5,   13,    0,
    0,    0,
};
static const YYINT ok_syntax1_gindex[] = {                0,
    0,   65,    0,
};
#define YYTABLESIZE 220
static const YYINT ok_syntax1_table[] = {                 6,
   16,    6,   10,   13,    5,   11,    5,   22,   17,   23,
   15,   15,   20,   18,    7,   19,   22,   21,    4,    5,
    0,   20,    8,   12,    0,    0,   21,   16,   16,    0,
    0,   16,   16,   16,   13,   16,    0,   16,   15,   15,
    0,    0,    7,   15,   15,    7,   15,    7,   15,    7,
    8,   12,    0,    8,   12,    8,    0,    8,   22,   17,
    0,    0,   25,   20,   18,    0,   19,    0,   21,   13,
   14,    0,    0,    0,    0,   24,    0,    0,    0,    0,
   26,   27,   28,   29,   30,   31,   32,   22,   17,    0,
    0,    0,   20,   18,   16,   19,   22,   21,    0,    0,
    0,   20,   18,    0,   19,    0,   21,    0,    0,    0,
    0,    0,    0,    0,   16,    0,    0,   13,    0,    0,
    0,    0,    0,    0,    0,   15,    0,    0,    7,    0,
    0,    0,    0,    0,    0,    0,    8,   12,    0,    0,
    0,    0,    0,    0,    0,   16,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    2,    3,    4,    3,   12,
};
static const YYINT ok_syntax1_check[] = {                40,
   10,   40,   10,   10,   45,   61,   45,   37,   38,  257,
   10,   10,   42,   43,   10,   45,   37,   47,   10,   10,
   -1,   42,   10,   10,   -1,   -1,   47,   37,   38,   -1,
   -1,   41,   42,   43,   41,   45,   -1,   47,   37,   38,
   -1,   -1,   38,   42,   43,   41,   45,   43,   47,   45,
   38,   38,   -1,   41,   41,   43,   -1,   45,   37,   38,
   -1,   -1,   41,   42,   43,   -1,   45,   -1,   47,    5,
    6,   -1,   -1,   -1,   -1,   11,   -1,   -1,   -1,   -1,
   16,   17,   18,   19,   20,   21,   22,   37,   38,   -1,
   -1,   -1,   42,   43,  124,   45,   37,   47,   -1,   -1,
   -1,   42,   43,   -1,   45,   -1,   47,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,  124,   -1,   -1,  124,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  124,   -1,   -1,  124,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,  124,  124,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  124,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,  256,  257,  258,  257,  258,
};
#define YYFINAL 1
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 273
#define YYUNDFTOKEN 279
#define YYTRANSLATE(a) ((a) > YYMAXTOKEN ? YYUNDFTOKEN : (a))
#if YYDEBUG
static const char *const ok_syntax1_name[] = {

"end-of-file",0,0,0,0,0,0,"'\\a'","'\\b'","'\\t'","'\\n'","'\\v'","'\\f'",
"'\\r'",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"'#'",0,"'%'","'&'",0,"'('",
"')'","'*'","'+'",0,"'-'",0,"'/'",0,0,0,0,0,0,0,0,0,0,0,0,0,"'='",0,0,"'@'",0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"'^'",0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"'|'",0,"'~'","'\\177'",0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
"'\\377'",0,"DIGIT","LETTER","OCT1","HEX1","HEX2","HEX3","STR1",
"\"\\177\\177\\\\\\n\"","STR2","BELL","BS","NL","LF","CR","TAB","VT","UMINUS",0,
0,0,0,0,"illegal-symbol",
};
static const char *const ok_syntax1_rule[] = {
"$accept : list",
"list :",
"list : list stat '\\n'",
"list : list error '\\n'",
"stat : expr",
"stat : LETTER '=' expr",
"expr : '(' expr ')'",
"expr : expr '+' expr",
"expr : expr '-' expr",
"expr : expr '*' expr",
"expr : expr '/' expr",
"expr : expr '%' expr",
"expr : expr '&' expr",
"expr : expr '|' expr",
"expr : '-' expr",
"expr : LETTER",
"expr : number",
"number : DIGIT",
"number : number DIGIT",

};
#endif

int      yydebug;
int      yynerrs;

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
#line 104 "ok_syntax1.y"
 /* start of programs */

#ifdef YYBYACC
extern int YYLEX_DECL();
#endif

int
main (void)
{
    int regs[26];
    int base = 10;

    while(!feof(stdin)) {
	yyparse(regs, &base);
    }
    return 0;
}

#define UNUSED(x) ((void)(x))

static void
YYERROR_DECL()
{
    UNUSED(regs); /* %parse-param regs is not actually used here */
    UNUSED(base); /* %parse-param base is not actually used here */
    fprintf(stderr, "%s\n", s);
}

int
YYLEX_DECL()
{
	/* lexical analysis routine */
	/* returns LETTER for a lower case letter, yylval = 0 through 25 */
	/* return DIGIT for a digit, yylval = 0 through 9 */
	/* all other characters are returned immediately */

    int c;

    while( (c=getchar()) == ' ' )   { /* skip blanks */ }

    /* c is now nonblank */

    if( islower( c )) {
	yylval->ival = (c - 'a');
	return ( LETTER );
    }
    if( isdigit( c )) {
	yylval->ival = (c - '0') % (*base);
	return ( DIGIT );
    }
    return( c );
}
#line 389 "ok_syntax1.tab.c"

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
    int      yyerrflag;
    int      yychar;
    YYSTYPE  yyval;
    YYSTYPE  yylval;

    /* variables for the parser stack */
    YYSTACKDATA yystack;
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

    memset(&yyval,  0, sizeof(yyval));
    memset(&yylval, 0, sizeof(yylval));

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
#line 66 "ok_syntax1.y"
	{  yyerrok ; }
break;
case 4:
#line 70 "ok_syntax1.y"
	{  printf("%d\n",yystack.l_mark[0].ival);}
break;
case 5:
#line 72 "ok_syntax1.y"
	{  regs[yystack.l_mark[-2].ival] = yystack.l_mark[0].ival; }
break;
case 6:
#line 76 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-1].ival; }
break;
case 7:
#line 78 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival + yystack.l_mark[0].ival; }
break;
case 8:
#line 80 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival - yystack.l_mark[0].ival; }
break;
case 9:
#line 82 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival * yystack.l_mark[0].ival; }
break;
case 10:
#line 84 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival / yystack.l_mark[0].ival; }
break;
case 11:
#line 86 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival % yystack.l_mark[0].ival; }
break;
case 12:
#line 88 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival & yystack.l_mark[0].ival; }
break;
case 13:
#line 90 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[-2].ival | yystack.l_mark[0].ival; }
break;
case 14:
#line 92 "ok_syntax1.y"
	{  yyval.ival = - yystack.l_mark[0].ival; }
break;
case 15:
#line 94 "ok_syntax1.y"
	{  yyval.ival = regs[yystack.l_mark[0].ival]; }
break;
case 17:
#line 99 "ok_syntax1.y"
	{  yyval.ival = yystack.l_mark[0].ival; (*base) = (yystack.l_mark[0].ival==0) ? 8 : 10; }
break;
case 18:
#line 101 "ok_syntax1.y"
	{  yyval.ival = (*base) * yystack.l_mark[-1].ival + yystack.l_mark[0].ival; }
break;
#line 658 "ok_syntax1.tab.c"
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
