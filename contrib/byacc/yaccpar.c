/* This file generated automatically using
 * @Id: skel2c,v 1.4 2016/06/07 00:26:09 tom Exp @
 */

/* @Id: yaccpar.skel,v 1.8 2016/12/02 21:44:42 tom Exp @ */

#include "defs.h"

/*  If the skeleton is changed, the banner should be changed so that	*/
/*  the altered version can be easily distinguished from the original.	*/
/*									*/
/*  The #defines included with the banner are there because they are	*/
/*  useful in subsequent code.  The macros #defined in the header or	*/
/*  the body either are not useful outside of semantic actions or	*/
/*  are conditional.							*/

const char *const banner[] =
{
    "/* original parser id follows */",
    "/* yysccsid[] = \"@(#)yaccpar	1.9 (Berkeley) 02/21/93\" */",
    "/* (use YYMAJOR/YYMINOR for ifdefs dependent on parser version) */",
    "",
    "#define YYBYACC 1",
    CONCAT1("#define YYMAJOR ", YYMAJOR),
    CONCAT1("#define YYMINOR ", YYMINOR),
#ifdef YYPATCH
    CONCAT1("#define YYPATCH ", YYPATCH),
#endif
    "",
    "#define YYEMPTY        (-1)",
    "#define yyclearin      (yychar = YYEMPTY)",
    "#define yyerrok        (yyerrflag = 0)",
    "#define YYRECOVERING() (yyerrflag != 0)",
    "#define YYENOMEM       (-2)",
    "#define YYEOF          0",
    0
};

const char *const xdecls[] =
{
    "",
    "extern int YYPARSE_DECL();",
    0
};

const char *const tables[] =
{
    "extern YYINT yylhs[];",
    "extern YYINT yylen[];",
    "extern YYINT yydefred[];",
    "extern YYINT yydgoto[];",
    "extern YYINT yysindex[];",
    "extern YYINT yyrindex[];",
    "extern YYINT yygindex[];",
    "extern YYINT yytable[];",
    "extern YYINT yycheck[];",
    "",
    "#if YYDEBUG || defined(yytname)",
    "extern char *yyname[];",
    "#endif",
    "#if YYDEBUG",
    "extern char *yyrule[];",
    "#endif",
    0
};

const char *const global_vars[] =
{
    "",
    "int      yydebug;",
    "int      yynerrs;",
    0
};

const char *const impure_vars[] =
{
    "",
    "int      yyerrflag;",
    "int      yychar;",
    "YYSTYPE  yyval;",
    "YYSTYPE  yylval;",
    0
};

const char *const hdr_defs[] =
{
    "",
    "/* define the initial stack-sizes */",
    "#ifdef YYSTACKSIZE",
    "#undef YYMAXDEPTH",
    "#define YYMAXDEPTH  YYSTACKSIZE",
    "#else",
    "#ifdef YYMAXDEPTH",
    "#define YYSTACKSIZE YYMAXDEPTH",
    "#else",
    "#define YYSTACKSIZE 10000",
    "#define YYMAXDEPTH  10000",
    "#endif",
    "#endif",
    "",
    "#define YYINITSTACKSIZE 200",
    "",
    "typedef struct {",
    "    unsigned stacksize;",
    "    YYINT    *s_base;",
    "    YYINT    *s_mark;",
    "    YYINT    *s_last;",
    "    YYSTYPE  *l_base;",
    "    YYSTYPE  *l_mark;",
    "} YYSTACKDATA;",
    0
};

const char *const hdr_vars[] =
{
    "/* variables for the parser stack */",
    "static YYSTACKDATA yystack;",
    0
};

const char *const body_vars[] =
{
    "    int      yyerrflag;",
    "    int      yychar;",
    "    YYSTYPE  yyval;",
    "    YYSTYPE  yylval;",
    "",
    "    /* variables for the parser stack */",
    "    YYSTACKDATA yystack;",
    0
};

const char *const body_1[] =
{
    "",
    "#if YYDEBUG",
    "#include <stdio.h>	/* needed for printf */",
    "#endif",
    "",
    "#include <stdlib.h>	/* needed for malloc, etc */",
    "#include <string.h>	/* needed for memset */",
    "",
    "/* allocate initial stack or double stack size, up to YYMAXDEPTH */",
    "static int yygrowstack(YYSTACKDATA *data)",
    "{",
    "    int i;",
    "    unsigned newsize;",
    "    YYINT *newss;",
    "    YYSTYPE *newvs;",
    "",
    "    if ((newsize = data->stacksize) == 0)",
    "        newsize = YYINITSTACKSIZE;",
    "    else if (newsize >= YYMAXDEPTH)",
    "        return YYENOMEM;",
    "    else if ((newsize *= 2) > YYMAXDEPTH)",
    "        newsize = YYMAXDEPTH;",
    "",
    "    i = (int) (data->s_mark - data->s_base);",
    "    newss = (YYINT *)realloc(data->s_base, newsize * sizeof(*newss));",
    "    if (newss == 0)",
    "        return YYENOMEM;",
    "",
    "    data->s_base = newss;",
    "    data->s_mark = newss + i;",
    "",
    "    newvs = (YYSTYPE *)realloc(data->l_base, newsize * sizeof(*newvs));",
    "    if (newvs == 0)",
    "        return YYENOMEM;",
    "",
    "    data->l_base = newvs;",
    "    data->l_mark = newvs + i;",
    "",
    "    data->stacksize = newsize;",
    "    data->s_last = data->s_base + newsize - 1;",
    "    return 0;",
    "}",
    "",
    "#if YYPURE || defined(YY_NO_LEAKS)",
    "static void yyfreestack(YYSTACKDATA *data)",
    "{",
    "    free(data->s_base);",
    "    free(data->l_base);",
    "    memset(data, 0, sizeof(*data));",
    "}",
    "#else",
    "#define yyfreestack(data) /* nothing */",
    "#endif",
    "",
    "#define YYABORT  goto yyabort",
    "#define YYREJECT goto yyabort",
    "#define YYACCEPT goto yyaccept",
    "#define YYERROR  goto yyerrlab",
    "",
    "int",
    "YYPARSE_DECL()",
    "{",
    0
};

const char *const body_2[] =
{
    "    int yym, yyn, yystate;",
    "#if YYDEBUG",
    "    const char *yys;",
    "",
    "    if ((yys = getenv(\"YYDEBUG\")) != 0)",
    "    {",
    "        yyn = *yys;",
    "        if (yyn >= '0' && yyn <= '9')",
    "            yydebug = yyn - '0';",
    "    }",
    "#endif",
    "",
    0
};

const char *const init_vars[] =
{
    "    memset(&yyval,  0, sizeof(yyval));",
    "    memset(&yylval, 0, sizeof(yylval));",
    "",
    0
};

const char *const body_3[] =
{
    "    yym = 0;",
    "    yyn = 0;",
    "    yynerrs = 0;",
    "    yyerrflag = 0;",
    "    yychar = YYEMPTY;",
    "    yystate = 0;",
    "",
    "#if YYPURE",
    "    memset(&yystack, 0, sizeof(yystack));",
    "#endif",
    "",
    "    if (yystack.s_base == NULL && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;",
    "    yystack.s_mark = yystack.s_base;",
    "    yystack.l_mark = yystack.l_base;",
    "    yystate = 0;",
    "    *yystack.s_mark = 0;",
    "",
    "yyloop:",
    "    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;",
    "    if (yychar < 0)",
    "    {",
    "        yychar = YYLEX;",
    "        if (yychar < 0) yychar = YYEOF;",
    "#if YYDEBUG",
    "        if (yydebug)",
    "        {",
    "            if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];",
    "            printf(\"%sdebug: state %d, reading %d (%s)\\n\",",
    "                    YYPREFIX, yystate, yychar, yys);",
    "        }",
    "#endif",
    "    }",
    "    if (((yyn = yysindex[yystate]) != 0) && (yyn += yychar) >= 0 &&",
    "            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yychar)",
    "    {",
    "#if YYDEBUG",
    "        if (yydebug)",
    "            printf(\"%sdebug: state %d, shifting to state %d\\n\",",
    "                    YYPREFIX, yystate, yytable[yyn]);",
    "#endif",
    "        if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;",
    "        yystate = yytable[yyn];",
    "        *++yystack.s_mark = yytable[yyn];",
    "        *++yystack.l_mark = yylval;",
    "        yychar = YYEMPTY;",
    "        if (yyerrflag > 0)  --yyerrflag;",
    "        goto yyloop;",
    "    }",
    "    if (((yyn = yyrindex[yystate]) != 0) && (yyn += yychar) >= 0 &&",
    "            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yychar)",
    "    {",
    "        yyn = yytable[yyn];",
    "        goto yyreduce;",
    "    }",
    "    if (yyerrflag != 0) goto yyinrecovery;",
    "",
    "    YYERROR_CALL(\"syntax error\");",
    "",
    "    goto yyerrlab; /* redundant goto avoids 'unused label' warning */",
    "yyerrlab:",
    "    ++yynerrs;",
    "",
    "yyinrecovery:",
    "    if (yyerrflag < 3)",
    "    {",
    "        yyerrflag = 3;",
    "        for (;;)",
    "        {",
    "            if (((yyn = yysindex[*yystack.s_mark]) != 0) && (yyn += YYERRCODE) >= 0 &&",
    "                    yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) YYERRCODE)",
    "            {",
    "#if YYDEBUG",
    "                if (yydebug)",
    "                    printf(\"%sdebug: state %d, error recovery shifting\\",
    " to state %d\\n\", YYPREFIX, *yystack.s_mark, yytable[yyn]);",
    "#endif",
    "                if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;",
    "                yystate = yytable[yyn];",
    "                *++yystack.s_mark = yytable[yyn];",
    "                *++yystack.l_mark = yylval;",
    "                goto yyloop;",
    "            }",
    "            else",
    "            {",
    "#if YYDEBUG",
    "                if (yydebug)",
    "                    printf(\"%sdebug: error recovery discarding state %d\\n\",",
    "                            YYPREFIX, *yystack.s_mark);",
    "#endif",
    "                if (yystack.s_mark <= yystack.s_base) goto yyabort;",
    "                --yystack.s_mark;",
    "                --yystack.l_mark;",
    "            }",
    "        }",
    "    }",
    "    else",
    "    {",
    "        if (yychar == YYEOF) goto yyabort;",
    "#if YYDEBUG",
    "        if (yydebug)",
    "        {",
    "            if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];",
    "            printf(\"%sdebug: state %d, error recovery discards token %d (%s)\\n\",",
    "                    YYPREFIX, yystate, yychar, yys);",
    "        }",
    "#endif",
    "        yychar = YYEMPTY;",
    "        goto yyloop;",
    "    }",
    "",
    "yyreduce:",
    "#if YYDEBUG",
    "    if (yydebug)",
    "        printf(\"%sdebug: state %d, reducing by rule %d (%s)\\n\",",
    "                YYPREFIX, yystate, yyn, yyrule[yyn]);",
    "#endif",
    "    yym = yylen[yyn];",
    "    if (yym > 0)",
    "        yyval = yystack.l_mark[1-yym];",
    "    else",
    "        memset(&yyval, 0, sizeof yyval);",
    "",
    "    switch (yyn)",
    "    {",
    0
};

const char *const trailer[] =
{
    "    }",
    "    yystack.s_mark -= yym;",
    "    yystate = *yystack.s_mark;",
    "    yystack.l_mark -= yym;",
    "    yym = yylhs[yyn];",
    "    if (yystate == 0 && yym == 0)",
    "    {",
    "#if YYDEBUG",
    "        if (yydebug)",
    "            printf(\"%sdebug: after reduction, shifting from state 0 to\\",
    " state %d\\n\", YYPREFIX, YYFINAL);",
    "#endif",
    "        yystate = YYFINAL;",
    "        *++yystack.s_mark = YYFINAL;",
    "        *++yystack.l_mark = yyval;",
    "        if (yychar < 0)",
    "        {",
    "            yychar = YYLEX;",
    "            if (yychar < 0) yychar = YYEOF;",
    "#if YYDEBUG",
    "            if (yydebug)",
    "            {",
    "                if ((yys = yyname[YYTRANSLATE(yychar)]) == NULL) yys = yyname[YYUNDFTOKEN];",
    "                printf(\"%sdebug: state %d, reading %d (%s)\\n\",",
    "                        YYPREFIX, YYFINAL, yychar, yys);",
    "            }",
    "#endif",
    "        }",
    "        if (yychar == YYEOF) goto yyaccept;",
    "        goto yyloop;",
    "    }",
    "    if (((yyn = yygindex[yym]) != 0) && (yyn += yystate) >= 0 &&",
    "            yyn <= YYTABLESIZE && yycheck[yyn] == (YYINT) yystate)",
    "        yystate = yytable[yyn];",
    "    else",
    "        yystate = yydgoto[yym];",
    "#if YYDEBUG",
    "    if (yydebug)",
    "        printf(\"%sdebug: after reduction, shifting from state %d \\",
    "to state %d\\n\", YYPREFIX, *yystack.s_mark, yystate);",
    "#endif",
    "    if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack) == YYENOMEM) goto yyoverflow;",
    "    *++yystack.s_mark = (YYINT) yystate;",
    "    *++yystack.l_mark = yyval;",
    "    goto yyloop;",
    "",
    "yyoverflow:",
    "    YYERROR_CALL(\"yacc stack overflow\");",
    "",
    "yyabort:",
    "    yyfreestack(&yystack);",
    "    return (1);",
    "",
    "yyaccept:",
    "    yyfreestack(&yystack);",
    "    return (0);",
    "}",
    0
};

void
write_section(FILE * fp, const char *const section[])
{
    int i;
    const char *s;

    for (i = 0; (s = section[i]) != 0; ++i)
    {
	if (fp == code_file)
	    ++outline;
	fprintf(fp, "%s\n", s);
    }
}
