/* $Id: output.c,v 1.81 2017/04/30 23:23:32 tom Exp $ */

#include "defs.h"

#define StaticOrR	(rflag ? "" : "static ")
#define CountLine(fp)   (!rflag || ((fp) == code_file))

#if defined(YYBTYACC)
#define PER_STATE 3
#else
#define PER_STATE 2
#endif

static int nvectors;
static int nentries;
static Value_t **froms;
static Value_t **tos;
#if defined(YYBTYACC)
static Value_t *conflicts = NULL;
static Value_t nconflicts = 0;
#endif
static Value_t *tally;
static Value_t *width;
static Value_t *state_count;
static Value_t *order;
static Value_t *base;
static Value_t *pos;
static int maxtable;
static Value_t *table;
static Value_t *check;
static int lowzero;
static long high;

static void
putc_code(FILE * fp, int c)
{
    if ((c == '\n') && (fp == code_file))
	++outline;
    putc(c, fp);
}

static void
putl_code(FILE * fp, const char *s)
{
    if (fp == code_file)
	++outline;
    fputs(s, fp);
}

static void
puts_code(FILE * fp, const char *s)
{
    fputs(s, fp);
}

static void
puts_param_types(FILE * fp, param *list, int more)
{
    param *p;

    if (list != 0)
    {
	for (p = list; p; p = p->next)
	{
	    size_t len_type = strlen(p->type);
	    fprintf(fp, "%s%s%s%s%s", p->type,
		    (((len_type != 0) && (p->type[len_type - 1] == '*'))
		     ? ""
		     : " "),
		    p->name, p->type2,
		    ((more || p->next) ? ", " : ""));
	}
    }
    else
    {
	if (!more)
	    fprintf(fp, "void");
    }
}

static void
puts_param_names(FILE * fp, param *list, int more)
{
    param *p;

    for (p = list; p; p = p->next)
    {
	fprintf(fp, "%s%s", p->name,
		((more || p->next) ? ", " : ""));
    }
}

static void
write_code_lineno(FILE * fp)
{
    if (!lflag && (fp == code_file))
    {
	++outline;
	fprintf(fp, line_format, outline + 1, code_file_name);
    }
}

static void
write_input_lineno(void)
{
    if (!lflag)
    {
	++outline;
	fprintf(code_file, line_format, lineno, input_file_name);
    }
}

static void
define_prefixed(FILE * fp, const char *name)
{
    int bump_line = CountLine(fp);
    if (bump_line)
	++outline;
    fprintf(fp, "\n");

    if (bump_line)
	++outline;
    fprintf(fp, "#ifndef %s\n", name);

    if (bump_line)
	++outline;
    fprintf(fp, "#define %-10s %s%s\n", name, symbol_prefix, name + 2);

    if (bump_line)
	++outline;
    fprintf(fp, "#endif /* %s */\n", name);
}

static void
output_prefix(FILE * fp)
{
    if (symbol_prefix == NULL)
    {
	symbol_prefix = "yy";
    }
    else
    {
	define_prefixed(fp, "yyparse");
	define_prefixed(fp, "yylex");
	define_prefixed(fp, "yyerror");
	define_prefixed(fp, "yychar");
	define_prefixed(fp, "yyval");
	define_prefixed(fp, "yylval");
	define_prefixed(fp, "yydebug");
	define_prefixed(fp, "yynerrs");
	define_prefixed(fp, "yyerrflag");
	define_prefixed(fp, "yylhs");
	define_prefixed(fp, "yylen");
	define_prefixed(fp, "yydefred");
#if defined(YYBTYACC)
	define_prefixed(fp, "yystos");
#endif
	define_prefixed(fp, "yydgoto");
	define_prefixed(fp, "yysindex");
	define_prefixed(fp, "yyrindex");
	define_prefixed(fp, "yygindex");
	define_prefixed(fp, "yytable");
	define_prefixed(fp, "yycheck");
	define_prefixed(fp, "yyname");
	define_prefixed(fp, "yyrule");
#if defined(YYBTYACC)
	if (locations)
	{
	    define_prefixed(fp, "yyloc");
	    define_prefixed(fp, "yylloc");
	}
	putc_code(fp, '\n');
	putl_code(fp, "#if YYBTYACC\n");

	define_prefixed(fp, "yycindex");
	define_prefixed(fp, "yyctable");

	putc_code(fp, '\n');
	putl_code(fp, "#endif /* YYBTYACC */\n");
	putc_code(fp, '\n');
#endif
    }
    if (CountLine(fp))
	++outline;
    fprintf(fp, "#define YYPREFIX \"%s\"\n", symbol_prefix);
}

static void
output_newline(void)
{
    if (!rflag)
	++outline;
    putc('\n', output_file);
}

static void
output_line(const char *value)
{
    fputs(value, output_file);
    output_newline();
}

static void
output_int(int value)
{
    fprintf(output_file, "%5d,", value);
}

static void
start_int_table(const char *name, int value)
{
    int need = 34 - (int)(strlen(symbol_prefix) + strlen(name));

    if (need < 6)
	need = 6;
    fprintf(output_file,
	    "%sconst YYINT %s%s[] = {%*d,",
	    StaticOrR, symbol_prefix, name, need, value);
}

static void
start_str_table(const char *name)
{
    fprintf(output_file,
	    "%sconst char *const %s%s[] = {",
	    StaticOrR, symbol_prefix, name);
    output_newline();
}

static void
end_table(void)
{
    output_newline();
    output_line("};");
}

static void
output_stype(FILE * fp)
{
    if (!unionized && ntags == 0)
    {
	putc_code(fp, '\n');
	putl_code(fp, "#if "
		  "! defined(YYSTYPE) && "
		  "! defined(YYSTYPE_IS_DECLARED)\n");
	putl_code(fp, "/* Default: YYSTYPE is the semantic value type. */\n");
	putl_code(fp, "typedef int YYSTYPE;\n");
	putl_code(fp, "# define YYSTYPE_IS_DECLARED 1\n");
	putl_code(fp, "#endif\n");
    }
}

#if defined(YYBTYACC)
static void
output_ltype(FILE * fp)
{
    putc_code(fp, '\n');
    putl_code(fp, "#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED\n");
    putl_code(fp, "/* Default: YYLTYPE is the text position type. */\n");
    putl_code(fp, "typedef struct YYLTYPE\n");
    putl_code(fp, "{\n");
    putl_code(fp, "    int first_line;\n");
    putl_code(fp, "    int first_column;\n");
    putl_code(fp, "    int last_line;\n");
    putl_code(fp, "    int last_column;\n");
    putl_code(fp, "    unsigned source;\n");
    putl_code(fp, "} YYLTYPE;\n");
    putl_code(fp, "#define YYLTYPE_IS_DECLARED 1\n");
    putl_code(fp, "#endif\n");
    putl_code(fp, "#define YYRHSLOC(rhs, k) ((rhs)[k])\n");
}
#endif

static void
output_YYINT_typedef(FILE * fp)
{
    /* generate the type used to index the various parser tables */
    if (CountLine(fp))
	++outline;
    fprintf(fp, "typedef %s YYINT;\n", CONCAT1("", YYINT));
}

static void
output_rule_data(void)
{
    int i;
    int j;

    output_YYINT_typedef(output_file);

    start_int_table("lhs", symbol_value[start_symbol]);

    j = 10;
    for (i = 3; i < nrules; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(symbol_value[rlhs[i]]);
    }
    end_table();

    start_int_table("len", 2);

    j = 10;
    for (i = 3; i < nrules; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    j++;

	output_int(rrhs[i + 1] - rrhs[i] - 1);
    }
    end_table();
}

static void
output_yydefred(void)
{
    int i, j;

    start_int_table("defred", (defred[0] ? defred[0] - 2 : 0));

    j = 10;
    for (i = 1; i < nstates; i++)
    {
	if (j < 10)
	    ++j;
	else
	{
	    output_newline();
	    j = 1;
	}

	output_int((defred[i] ? defred[i] - 2 : 0));
    }

    end_table();
}

#if defined(YYBTYACC)
static void
output_accessing_symbols(void)
{
    int i, j;
    int *translate;

    if (nstates != 0)
    {
	translate = TMALLOC(int, nstates);
	NO_SPACE(translate);

	for (i = 0; i < nstates; ++i)
	{
	    int gsymb = accessing_symbol[i];

	    translate[i] = symbol_pval[gsymb];
	}

	putl_code(output_file,
		  "#if defined(YYDESTRUCT_CALL) || defined(YYSTYPE_TOSTRING)\n");
	/* yystos[] may be unused, depending on compile-time defines */
	start_int_table("stos", translate[0]);

	j = 10;
	for (i = 1; i < nstates; ++i)
	{
	    if (j < 10)
		++j;
	    else
	    {
		output_newline();
		j = 1;
	    }

	    output_int(translate[i]);
	}

	end_table();
	FREE(translate);
	putl_code(output_file,
		  "#endif /* YYDESTRUCT_CALL || YYSTYPE_TOSTRING */\n");
    }
}

static Value_t
find_conflict_base(int cbase)
{
    int i, j;

    for (i = 0; i < cbase; i++)
    {
	for (j = 0; j + cbase < nconflicts; j++)
	{
	    if (conflicts[i + j] != conflicts[cbase + j])
		break;
	}
	if (j + cbase >= nconflicts)
	    break;
    }
    return (Value_t)i;
}
#endif

static void
token_actions(void)
{
    int i, j;
    Value_t shiftcount, reducecount;
#if defined(YYBTYACC)
    Value_t conflictcount = 0;
    Value_t csym = -1;
    Value_t cbase = 0;
#endif
    int max, min;
    Value_t *actionrow, *r, *s;
    action *p;

    actionrow = NEW2(PER_STATE * ntokens, Value_t);
    for (i = 0; i < nstates; ++i)
    {
	if (parser[i])
	{
	    for (j = 0; j < PER_STATE * ntokens; ++j)
		actionrow[j] = 0;

	    shiftcount = 0;
	    reducecount = 0;
#if defined(YYBTYACC)
	    if (backtrack)
	    {
		conflictcount = 0;
		csym = -1;
		cbase = nconflicts;
	    }
#endif
	    for (p = parser[i]; p; p = p->next)
	    {
#if defined(YYBTYACC)
		if (backtrack)
		{
		    if (csym != -1 && csym != p->symbol)
		    {
			conflictcount++;
			conflicts[nconflicts++] = -1;
			j = find_conflict_base(cbase);
			actionrow[csym + 2 * ntokens] = (Value_t)(j + 1);
			if (j == cbase)
			{
			    cbase = nconflicts;
			}
			else
			{
			    if (conflicts[cbase] == -1)
				cbase++;
			    nconflicts = cbase;
			}
			csym = -1;
		    }
		}
#endif
		if (p->suppressed == 0)
		{
		    if (p->action_code == SHIFT)
		    {
			++shiftcount;
			actionrow[p->symbol] = p->number;
		    }
		    else if (p->action_code == REDUCE && p->number != defred[i])
		    {
			++reducecount;
			actionrow[p->symbol + ntokens] = p->number;
		    }
		}
#if defined(YYBTYACC)
		else if (backtrack && p->suppressed == 1)
		{
		    csym = p->symbol;
		    if (p->action_code == SHIFT)
		    {
			conflicts[nconflicts++] = p->number;
		    }
		    else if (p->action_code == REDUCE && p->number != defred[i])
		    {
			if (cbase == nconflicts)
			{
			    if (cbase)
				cbase--;
			    else
				conflicts[nconflicts++] = -1;
			}
			conflicts[nconflicts++] = (Value_t)(p->number - 2);
		    }
		}
#endif
	    }
#if defined(YYBTYACC)
	    if (backtrack && csym != -1)
	    {
		conflictcount++;
		conflicts[nconflicts++] = -1;
		j = find_conflict_base(cbase);
		actionrow[csym + 2 * ntokens] = (Value_t)(j + 1);
		if (j == cbase)
		{
		    cbase = nconflicts;
		}
		else
		{
		    if (conflicts[cbase] == -1)
			cbase++;
		    nconflicts = cbase;
		}
	    }
#endif

	    tally[i] = shiftcount;
	    tally[nstates + i] = reducecount;
#if defined(YYBTYACC)
	    if (backtrack)
		tally[2 * nstates + i] = conflictcount;
#endif
	    width[i] = 0;
	    width[nstates + i] = 0;
#if defined(YYBTYACC)
	    if (backtrack)
		width[2 * nstates + i] = 0;
#endif
	    if (shiftcount > 0)
	    {
		froms[i] = r = NEW2(shiftcount, Value_t);
		tos[i] = s = NEW2(shiftcount, Value_t);
		min = MAXYYINT;
		max = 0;
		for (j = 0; j < ntokens; ++j)
		{
		    if (actionrow[j])
		    {
			if (min > symbol_value[j])
			    min = symbol_value[j];
			if (max < symbol_value[j])
			    max = symbol_value[j];
			*r++ = symbol_value[j];
			*s++ = actionrow[j];
		    }
		}
		width[i] = (Value_t)(max - min + 1);
	    }
	    if (reducecount > 0)
	    {
		froms[nstates + i] = r = NEW2(reducecount, Value_t);
		tos[nstates + i] = s = NEW2(reducecount, Value_t);
		min = MAXYYINT;
		max = 0;
		for (j = 0; j < ntokens; ++j)
		{
		    if (actionrow[ntokens + j])
		    {
			if (min > symbol_value[j])
			    min = symbol_value[j];
			if (max < symbol_value[j])
			    max = symbol_value[j];
			*r++ = symbol_value[j];
			*s++ = (Value_t)(actionrow[ntokens + j] - 2);
		    }
		}
		width[nstates + i] = (Value_t)(max - min + 1);
	    }
#if defined(YYBTYACC)
	    if (backtrack && conflictcount > 0)
	    {
		froms[2 * nstates + i] = r = NEW2(conflictcount, Value_t);
		tos[2 * nstates + i] = s = NEW2(conflictcount, Value_t);
		min = MAXYYINT;
		max = 0;
		for (j = 0; j < ntokens; ++j)
		{
		    if (actionrow[2 * ntokens + j])
		    {
			if (min > symbol_value[j])
			    min = symbol_value[j];
			if (max < symbol_value[j])
			    max = symbol_value[j];
			*r++ = symbol_value[j];
			*s++ = (Value_t)(actionrow[2 * ntokens + j] - 1);
		    }
		}
		width[2 * nstates + i] = (Value_t)(max - min + 1);
	    }
#endif
	}
    }
    FREE(actionrow);
}

static int
default_goto(int symbol)
{
    int i;
    int m;
    int n;
    int default_state;
    int max;

    m = goto_map[symbol];
    n = goto_map[symbol + 1];

    if (m == n)
	return (0);

    for (i = 0; i < nstates; i++)
	state_count[i] = 0;

    for (i = m; i < n; i++)
	state_count[to_state[i]]++;

    max = 0;
    default_state = 0;
    for (i = 0; i < nstates; i++)
    {
	if (state_count[i] > max)
	{
	    max = state_count[i];
	    default_state = i;
	}
    }

    return (default_state);
}

static void
save_column(int symbol, int default_state)
{
    int i;
    int m;
    int n;
    Value_t *sp;
    Value_t *sp1;
    Value_t *sp2;
    Value_t count;
    int symno;

    m = goto_map[symbol];
    n = goto_map[symbol + 1];

    count = 0;
    for (i = m; i < n; i++)
    {
	if (to_state[i] != default_state)
	    ++count;
    }
    if (count == 0)
	return;

    symno = symbol_value[symbol] + PER_STATE * nstates;

    froms[symno] = sp1 = sp = NEW2(count, Value_t);
    tos[symno] = sp2 = NEW2(count, Value_t);

    for (i = m; i < n; i++)
    {
	if (to_state[i] != default_state)
	{
	    *sp1++ = from_state[i];
	    *sp2++ = to_state[i];
	}
    }

    tally[symno] = count;
    width[symno] = (Value_t)(sp1[-1] - sp[0] + 1);
}

static void
goto_actions(void)
{
    int i, j, k;

    state_count = NEW2(nstates, Value_t);

    k = default_goto(start_symbol + 1);
    start_int_table("dgoto", k);
    save_column(start_symbol + 1, k);

    j = 10;
    for (i = start_symbol + 2; i < nsyms; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	k = default_goto(i);
	output_int(k);
	save_column(i, k);
    }

    end_table();
    FREE(state_count);
}

static void
sort_actions(void)
{
    Value_t i;
    int j;
    int k;
    int t;
    int w;

    order = NEW2(nvectors, Value_t);
    nentries = 0;

    for (i = 0; i < nvectors; i++)
    {
	if (tally[i] > 0)
	{
	    t = tally[i];
	    w = width[i];
	    j = nentries - 1;

	    while (j >= 0 && (width[order[j]] < w))
		j--;

	    while (j >= 0 && (width[order[j]] == w) && (tally[order[j]] < t))
		j--;

	    for (k = nentries - 1; k > j; k--)
		order[k + 1] = order[k];

	    order[j + 1] = i;
	    nentries++;
	}
    }
}

/*  The function matching_vector determines if the vector specified by	*/
/*  the input parameter matches a previously considered	vector.  The	*/
/*  test at the start of the function checks if the vector represents	*/
/*  a row of shifts over terminal symbols or a row of reductions, or a	*/
/*  column of shifts over a nonterminal symbol.  Berkeley Yacc does not	*/
/*  check if a column of shifts over a nonterminal symbols matches a	*/
/*  previously considered vector.  Because of the nature of LR parsing	*/
/*  tables, no two columns can match.  Therefore, the only possible	*/
/*  match would be between a row and a column.  Such matches are	*/
/*  unlikely.  Therefore, to save time, no attempt is made to see if a	*/
/*  column matches a previously considered vector.			*/
/*									*/
/*  Matching_vector is poorly designed.  The test could easily be made	*/
/*  faster.  Also, it depends on the vectors being in a specific	*/
/*  order.								*/
#if defined(YYBTYACC)
/*									*/
/*  Not really any point in checking for matching conflicts -- it is    */
/*  extremely unlikely to occur, and conflicts are (hopefully) rare.    */
#endif

static int
matching_vector(int vector)
{
    int i;
    int j;
    int k;
    int t;
    int w;
    int match;
    int prev;

    i = order[vector];
    if (i >= 2 * nstates)
	return (-1);

    t = tally[i];
    w = width[i];

    for (prev = vector - 1; prev >= 0; prev--)
    {
	j = order[prev];
	if (width[j] != w || tally[j] != t)
	    return (-1);

	match = 1;
	for (k = 0; match && k < t; k++)
	{
	    if (tos[j][k] != tos[i][k] || froms[j][k] != froms[i][k])
		match = 0;
	}

	if (match)
	    return (j);
    }

    return (-1);
}

static int
pack_vector(int vector)
{
    int i, j, k, l;
    int t;
    int loc;
    int ok;
    Value_t *from;
    Value_t *to;
    int newmax;

    i = order[vector];
    t = tally[i];
    assert(t);

    from = froms[i];
    to = tos[i];

    j = lowzero - from[0];
    for (k = 1; k < t; ++k)
	if (lowzero - from[k] > j)
	    j = lowzero - from[k];
    for (;; ++j)
    {
	if (j == 0)
	    continue;
	ok = 1;
	for (k = 0; ok && k < t; k++)
	{
	    loc = j + from[k];
	    if (loc >= maxtable - 1)
	    {
		if (loc >= MAXTABLE - 1)
		    fatal("maximum table size exceeded");

		newmax = maxtable;
		do
		{
		    newmax += 200;
		}
		while (newmax <= loc);

		table = TREALLOC(Value_t, table, newmax);
		NO_SPACE(table);

		check = TREALLOC(Value_t, check, newmax);
		NO_SPACE(check);

		for (l = maxtable; l < newmax; ++l)
		{
		    table[l] = 0;
		    check[l] = -1;
		}
		maxtable = newmax;
	    }

	    if (check[loc] != -1)
		ok = 0;
	}
	for (k = 0; ok && k < vector; k++)
	{
	    if (pos[k] == j)
		ok = 0;
	}
	if (ok)
	{
	    for (k = 0; k < t; k++)
	    {
		loc = j + from[k];
		table[loc] = to[k];
		check[loc] = from[k];
		if (loc > high)
		    high = loc;
	    }

	    while (check[lowzero] != -1)
		++lowzero;

	    return (j);
	}
    }
}

static void
pack_table(void)
{
    int i;
    Value_t place;
    int state;

    base = NEW2(nvectors, Value_t);
    pos = NEW2(nentries, Value_t);

    maxtable = 1000;
    table = NEW2(maxtable, Value_t);
    check = NEW2(maxtable, Value_t);

    lowzero = 0;
    high = 0;

    for (i = 0; i < maxtable; i++)
	check[i] = -1;

    for (i = 0; i < nentries; i++)
    {
	state = matching_vector(i);

	if (state < 0)
	    place = (Value_t)pack_vector(i);
	else
	    place = base[state];

	pos[i] = place;
	base[order[i]] = place;
    }

    for (i = 0; i < nvectors; i++)
    {
	if (froms[i])
	    FREE(froms[i]);
	if (tos[i])
	    FREE(tos[i]);
    }

    DO_FREE(froms);
    DO_FREE(tos);
    DO_FREE(tally);
    DO_FREE(width);
    DO_FREE(pos);
}

static void
output_base(void)
{
    int i, j;

    start_int_table("sindex", base[0]);

    j = 10;
    for (i = 1; i < nstates; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(base[i]);
    }

    end_table();

    start_int_table("rindex", base[nstates]);

    j = 10;
    for (i = nstates + 1; i < 2 * nstates; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(base[i]);
    }

    end_table();

#if defined(YYBTYACC)
    output_line("#if YYBTYACC");
    start_int_table("cindex", base[2 * nstates]);

    j = 10;
    for (i = 2 * nstates + 1; i < 3 * nstates; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(base[i]);
    }

    end_table();
    output_line("#endif");
#endif

    start_int_table("gindex", base[PER_STATE * nstates]);

    j = 10;
    for (i = PER_STATE * nstates + 1; i < nvectors - 1; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(base[i]);
    }

    end_table();
    FREE(base);
}

static void
output_table(void)
{
    int i;
    int j;

    if (high >= MAXYYINT)
    {
	fprintf(stderr, "YYTABLESIZE: %ld\n", high);
	fprintf(stderr, "Table is longer than %d elements.\n", MAXYYINT);
	done(1);
    }

    ++outline;
    fprintf(code_file, "#define YYTABLESIZE %ld\n", high);
    start_int_table("table", table[0]);

    j = 10;
    for (i = 1; i <= high; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(table[i]);
    }

    end_table();
    FREE(table);
}

static void
output_check(void)
{
    int i;
    int j;

    start_int_table("check", check[0]);

    j = 10;
    for (i = 1; i <= high; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int(check[i]);
    }

    end_table();
    FREE(check);
}

#if defined(YYBTYACC)
static void
output_ctable(void)
{
    int i;
    int j;
    int limit = (conflicts != 0) ? nconflicts : 0;

    if (limit < high)
	limit = (int)high;

    output_line("#if YYBTYACC");
    start_int_table("ctable", conflicts ? conflicts[0] : -1);

    j = 10;
    for (i = 1; i < limit; i++)
    {
	if (j >= 10)
	{
	    output_newline();
	    j = 1;
	}
	else
	    ++j;

	output_int((conflicts != 0 && i < nconflicts) ? conflicts[i] : -1);
    }

    if (conflicts)
	FREE(conflicts);

    end_table();
    output_line("#endif");
}
#endif

static void
output_actions(void)
{
    nvectors = PER_STATE * nstates + nvars;

    froms = NEW2(nvectors, Value_t *);
    tos = NEW2(nvectors, Value_t *);
    tally = NEW2(nvectors, Value_t);
    width = NEW2(nvectors, Value_t);

#if defined(YYBTYACC)
    if (backtrack && (SRtotal + RRtotal) != 0)
	conflicts = NEW2(4 * (SRtotal + RRtotal), Value_t);
#endif

    token_actions();
    FREE(lookaheads);
    FREE(LA);
    FREE(LAruleno);
    FREE(accessing_symbol);

    goto_actions();
    FREE(goto_base);
    FREE(from_state);
    FREE(to_state);

    sort_actions();
    pack_table();
    output_base();
    output_table();
    output_check();
#if defined(YYBTYACC)
    output_ctable();
#endif
}

static int
is_C_identifier(char *name)
{
    char *s;
    int c;

    s = name;
    c = *s;
    if (c == '"')
    {
	c = *++s;
	if (!isalpha(c) && c != '_' && c != '$')
	    return (0);
	while ((c = *++s) != '"')
	{
	    if (!isalnum(c) && c != '_' && c != '$')
		return (0);
	}
	return (1);
    }

    if (!isalpha(c) && c != '_' && c != '$')
	return (0);
    while ((c = *++s) != 0)
    {
	if (!isalnum(c) && c != '_' && c != '$')
	    return (0);
    }
    return (1);
}

#if USE_HEADER_GUARDS
static void
start_defines_file(void)
{
    fprintf(defines_file, "#ifndef _%s_defines_h_\n", symbol_prefix);
    fprintf(defines_file, "#define _%s_defines_h_\n\n", symbol_prefix);
}

static void
end_defines_file(void)
{
    fprintf(defines_file, "\n#endif /* _%s_defines_h_ */\n", symbol_prefix);
}
#else
#define start_defines_file()	/* nothing */
#define end_defines_file()	/* nothing */
#endif

static void
output_defines(FILE * fp)
{
    int c, i;
    char *s;

    for (i = 2; i < ntokens; ++i)
    {
	s = symbol_name[i];
	if (is_C_identifier(s) && (!sflag || *s != '"'))
	{
	    fprintf(fp, "#define ");
	    c = *s;
	    if (c == '"')
	    {
		while ((c = *++s) != '"')
		{
		    putc(c, fp);
		}
	    }
	    else
	    {
		do
		{
		    putc(c, fp);
		}
		while ((c = *++s) != 0);
	    }
	    if (fp == code_file)
		++outline;
	    fprintf(fp, " %d\n", symbol_value[i]);
	}
    }

    if (fp == code_file)
	++outline;
    if (fp != defines_file || iflag)
	fprintf(fp, "#define YYERRCODE %d\n", symbol_value[1]);

    if (token_table && rflag && fp != externs_file)
    {
	if (fp == code_file)
	    ++outline;
	fputs("#undef yytname\n", fp);
	if (fp == code_file)
	    ++outline;
	fputs("#define yytname yyname\n", fp);
    }

    if (fp == defines_file || (iflag && !dflag))
    {
	if (unionized)
	{
	    if (union_file != 0)
	    {
		rewind(union_file);
		while ((c = getc(union_file)) != EOF)
		    putc_code(fp, c);
	    }
	    fprintf(fp, "extern YYSTYPE %slval;\n", symbol_prefix);
	}
#if defined(YYBTYACC)
	if (locations)
	    output_ltype(fp);
#endif
    }
}

static void
output_stored_text(FILE * fp)
{
    int c;
    FILE *in;

    rewind(text_file);
    if (text_file == NULL)
	open_error("text_file");
    in = text_file;
    if ((c = getc(in)) == EOF)
	return;
    putc_code(fp, c);
    while ((c = getc(in)) != EOF)
    {
	putc_code(fp, c);
    }
    write_code_lineno(fp);
}

static void
output_debug(void)
{
    int i, j, k, max, maxtok;
    const char **symnam;
    const char *s;

    ++outline;
    fprintf(code_file, "#define YYFINAL %d\n", final_state);

    putl_code(code_file, "#ifndef YYDEBUG\n");
    ++outline;
    fprintf(code_file, "#define YYDEBUG %d\n", tflag);
    putl_code(code_file, "#endif\n");

    if (rflag)
    {
	fprintf(output_file, "#ifndef YYDEBUG\n");
	fprintf(output_file, "#define YYDEBUG %d\n", tflag);
	fprintf(output_file, "#endif\n");
    }

    maxtok = 0;
    for (i = 0; i < ntokens; ++i)
	if (symbol_value[i] > maxtok)
	    maxtok = symbol_value[i];

    /* symbol_value[$accept] = -1         */
    /* symbol_value[<goal>]  = 0          */
    /* remaining non-terminals start at 1 */
    max = maxtok;
    for (i = ntokens; i < nsyms; ++i)
	if (((maxtok + 1) + (symbol_value[i] + 1)) > max)
	    max = (maxtok + 1) + (symbol_value[i] + 1);

    ++outline;
    fprintf(code_file, "#define YYMAXTOKEN %d\n", maxtok);

    ++outline;
    fprintf(code_file, "#define YYUNDFTOKEN %d\n", max + 1);

    ++outline;
    fprintf(code_file, "#define YYTRANSLATE(a) ((a) > YYMAXTOKEN ? "
	    "YYUNDFTOKEN : (a))\n");

    symnam = TMALLOC(const char *, max + 2);
    NO_SPACE(symnam);

    /* Note that it is not necessary to initialize the element          */
    /* symnam[max].                                                     */
#if defined(YYBTYACC)
    for (i = 0; i < max; ++i)
	symnam[i] = 0;
    for (i = nsyms - 1; i >= 0; --i)
	symnam[symbol_pval[i]] = symbol_name[i];
    symnam[max + 1] = "illegal-symbol";
#else
    for (i = 0; i <= max; ++i)
	symnam[i] = 0;
    for (i = ntokens - 1; i >= 2; --i)
	symnam[symbol_value[i]] = symbol_name[i];
    symnam[0] = "end-of-file";
    symnam[max + 1] = "illegal-symbol";
#endif

    /*
     * bison's yytname[] array is roughly the same as byacc's yyname[] array.
     * The difference is that byacc does not predefine "$undefined".
     *
     * If the grammar declares "%token-table", define symbol "yytname" so
     * an application such as ntpd can build.
     */
    if (token_table)
    {
	if (!rflag)
	{
	    output_line("#undef yytname");
	    output_line("#define yytname yyname");
	}
    }
    else
    {
	output_line("#if YYDEBUG");
    }

    start_str_table("name");
    j = 80;
    for (i = 0; i <= max + 1; ++i)
    {
	if ((s = symnam[i]) != 0)
	{
	    if (s[0] == '"')
	    {
		k = 7;
		while (*++s != '"')
		{
		    ++k;
		    if (*s == '\\')
		    {
			k += 2;
			if (*++s == '\\')
			    ++k;
		    }
		}
		j += k;
		if (j > 80)
		{
		    output_newline();
		    j = k;
		}
		fprintf(output_file, "\"\\\"");
		s = symnam[i];
		while (*++s != '"')
		{
		    if (*s == '\\')
		    {
			fprintf(output_file, "\\\\");
			if (*++s == '\\')
			    fprintf(output_file, "\\\\");
			else
			    putc(*s, output_file);
		    }
		    else
			putc(*s, output_file);
		}
		fprintf(output_file, "\\\"\",");
	    }
	    else if (s[0] == '\'')
	    {
		if (s[1] == '"')
		{
		    j += 7;
		    if (j > 80)
		    {
			output_newline();
			j = 7;
		    }
		    fprintf(output_file, "\"'\\\"'\",");
		}
		else
		{
		    k = 5;
		    while (*++s != '\'')
		    {
			++k;
			if (*s == '\\')
			{
			    k += 2;
			    if (*++s == '\\')
				++k;
			}
		    }
		    j += k;
		    if (j > 80)
		    {
			output_newline();
			j = k;
		    }
		    fprintf(output_file, "\"'");
		    s = symnam[i];
		    while (*++s != '\'')
		    {
			if (*s == '\\')
			{
			    fprintf(output_file, "\\\\");
			    if (*++s == '\\')
				fprintf(output_file, "\\\\");
			    else
				putc(*s, output_file);
			}
			else
			    putc(*s, output_file);
		    }
		    fprintf(output_file, "'\",");
		}
	    }
	    else
	    {
		k = (int)strlen(s) + 3;
		j += k;
		if (j > 80)
		{
		    output_newline();
		    j = k;
		}
		putc('"', output_file);
		do
		{
		    putc(*s, output_file);
		}
		while (*++s);
		fprintf(output_file, "\",");
	    }
	}
	else
	{
	    j += 2;
	    if (j > 80)
	    {
		output_newline();
		j = 2;
	    }
	    fprintf(output_file, "0,");
	}
    }
    end_table();
    FREE(symnam);

    if (token_table)
	output_line("#if YYDEBUG");
    start_str_table("rule");
    for (i = 2; i < nrules; ++i)
    {
	fprintf(output_file, "\"%s :", symbol_name[rlhs[i]]);
	for (j = rrhs[i]; ritem[j] > 0; ++j)
	{
	    s = symbol_name[ritem[j]];
	    if (s[0] == '"')
	    {
		fprintf(output_file, " \\\"");
		while (*++s != '"')
		{
		    if (*s == '\\')
		    {
			if (s[1] == '\\')
			    fprintf(output_file, "\\\\\\\\");
			else
			    fprintf(output_file, "\\\\%c", s[1]);
			++s;
		    }
		    else
			putc(*s, output_file);
		}
		fprintf(output_file, "\\\"");
	    }
	    else if (s[0] == '\'')
	    {
		if (s[1] == '"')
		    fprintf(output_file, " '\\\"'");
		else if (s[1] == '\\')
		{
		    if (s[2] == '\\')
			fprintf(output_file, " '\\\\\\\\");
		    else
			fprintf(output_file, " '\\\\%c", s[2]);
		    s += 2;
		    while (*++s != '\'')
			putc(*s, output_file);
		    putc('\'', output_file);
		}
		else
		    fprintf(output_file, " '%c'", s[1]);
	    }
	    else
		fprintf(output_file, " %s", s);
	}
	fprintf(output_file, "\",");
	output_newline();
    }

    end_table();
    output_line("#endif");
}

#if defined(YYBTYACC)
static void
output_backtracking_parser(FILE * fp)
{
    putl_code(fp, "#undef YYBTYACC\n");
#if defined(YYBTYACC)
    if (backtrack)
    {
	putl_code(fp, "#define YYBTYACC 1\n");
	putl_code(fp,
		  "#define YYDEBUGSTR (yytrial ? YYPREFIX \"debug(trial)\" : YYPREFIX \"debug\")\n");
    }
    else
#endif
    {
	putl_code(fp, "#define YYBTYACC 0\n");
	putl_code(fp, "#define YYDEBUGSTR YYPREFIX \"debug\"\n");
    }
}
#endif

static void
output_pure_parser(FILE * fp)
{
    putc_code(fp, '\n');

    if (fp == code_file)
	++outline;
    fprintf(fp, "#define YYPURE %d\n", pure_parser);
    putc_code(fp, '\n');
}

#if defined(YY_NO_LEAKS)
static void
output_no_leaks(FILE * fp)
{
    putc_code(fp, '\n');

    if (fp == code_file)
	++outline;
    fputs("#define YY_NO_LEAKS 1\n", fp);
    putc_code(fp, '\n');
}
#endif

static void
output_trailing_text(void)
{
    int c, last;
    FILE *in;

    if (line == 0)
	return;

    in = input_file;
    c = *cptr;
    if (c == '\n')
    {
	++lineno;
	if ((c = getc(in)) == EOF)
	    return;
	write_input_lineno();
	putc_code(code_file, c);
	last = c;
    }
    else
    {
	write_input_lineno();
	do
	{
	    putc_code(code_file, c);
	}
	while ((c = *++cptr) != '\n');
	putc_code(code_file, c);
	last = '\n';
    }

    while ((c = getc(in)) != EOF)
    {
	putc_code(code_file, c);
	last = c;
    }

    if (last != '\n')
    {
	putc_code(code_file, '\n');
    }
    write_code_lineno(code_file);
}

static void
output_semantic_actions(void)
{
    int c, last;

    rewind(action_file);
    if ((c = getc(action_file)) == EOF)
	return;

    last = c;
    putc_code(code_file, c);
    while ((c = getc(action_file)) != EOF)
    {
	putc_code(code_file, c);
	last = c;
    }

    if (last != '\n')
    {
	putc_code(code_file, '\n');
    }

    write_code_lineno(code_file);
}

static void
output_parse_decl(FILE * fp)
{
    putc_code(fp, '\n');
    putl_code(fp, "/* compatibility with bison */\n");
    putl_code(fp, "#ifdef YYPARSE_PARAM\n");
    putl_code(fp, "/* compatibility with FreeBSD */\n");
    putl_code(fp, "# ifdef YYPARSE_PARAM_TYPE\n");
    putl_code(fp,
	      "#  define YYPARSE_DECL() yyparse(YYPARSE_PARAM_TYPE YYPARSE_PARAM)\n");
    putl_code(fp, "# else\n");
    putl_code(fp, "#  define YYPARSE_DECL() yyparse(void *YYPARSE_PARAM)\n");
    putl_code(fp, "# endif\n");
    putl_code(fp, "#else\n");

    puts_code(fp, "# define YYPARSE_DECL() yyparse(");
    puts_param_types(fp, parse_param, 0);
    putl_code(fp, ")\n");

    putl_code(fp, "#endif\n");
}

static void
output_lex_decl(FILE * fp)
{
    putc_code(fp, '\n');
    putl_code(fp, "/* Parameters sent to lex. */\n");
    putl_code(fp, "#ifdef YYLEX_PARAM\n");
    if (pure_parser)
    {
	putl_code(fp, "# ifdef YYLEX_PARAM_TYPE\n");
#if defined(YYBTYACC)
	if (locations)
	{
	    putl_code(fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " YYLTYPE *yylloc,"
		      " YYLEX_PARAM_TYPE YYLEX_PARAM)\n");
	}
	else
#endif
	{
	    putl_code(fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " YYLEX_PARAM_TYPE YYLEX_PARAM)\n");
	}
	putl_code(fp, "# else\n");
#if defined(YYBTYACC)
	if (locations)
	{
	    putl_code(fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " YYLTYPE *yylloc,"
		      " void * YYLEX_PARAM)\n");
	}
	else
#endif
	{
	    putl_code(fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " void * YYLEX_PARAM)\n");
	}
	putl_code(fp, "# endif\n");
#if defined(YYBTYACC)
	if (locations)
	    putl_code(fp,
		      "# define YYLEX yylex(&yylval, &yylloc, YYLEX_PARAM)\n");
	else
#endif
	    putl_code(fp, "# define YYLEX yylex(&yylval, YYLEX_PARAM)\n");
    }
    else
    {
	putl_code(fp, "# define YYLEX_DECL() yylex(void *YYLEX_PARAM)\n");
	putl_code(fp, "# define YYLEX yylex(YYLEX_PARAM)\n");
    }
    putl_code(fp, "#else\n");
    if (pure_parser && lex_param)
    {
#if defined(YYBTYACC)
	if (locations)
	    puts_code(fp,
		      "# define YYLEX_DECL() yylex(YYSTYPE *yylval, YYLTYPE *yylloc, ");
	else
#endif
	    puts_code(fp, "# define YYLEX_DECL() yylex(YYSTYPE *yylval, ");
	puts_param_types(fp, lex_param, 0);
	putl_code(fp, ")\n");

#if defined(YYBTYACC)
	if (locations)
	    puts_code(fp, "# define YYLEX yylex(&yylval, &yylloc, ");
	else
#endif
	    puts_code(fp, "# define YYLEX yylex(&yylval, ");
	puts_param_names(fp, lex_param, 0);
	putl_code(fp, ")\n");
    }
    else if (pure_parser)
    {
#if defined(YYBTYACC)
	if (locations)
	{
	    putl_code(fp,
		      "# define YYLEX_DECL() yylex(YYSTYPE *yylval, YYLTYPE *yylloc)\n");
	    putl_code(fp, "# define YYLEX yylex(&yylval, &yylloc)\n");
	}
	else
#endif
	{
	    putl_code(fp, "# define YYLEX_DECL() yylex(YYSTYPE *yylval)\n");
	    putl_code(fp, "# define YYLEX yylex(&yylval)\n");
	}
    }
    else if (lex_param)
    {
	puts_code(fp, "# define YYLEX_DECL() yylex(");
	puts_param_types(fp, lex_param, 0);
	putl_code(fp, ")\n");

	puts_code(fp, "# define YYLEX yylex(");
	puts_param_names(fp, lex_param, 0);
	putl_code(fp, ")\n");
    }
    else
    {
	putl_code(fp, "# define YYLEX_DECL() yylex(void)\n");
	putl_code(fp, "# define YYLEX yylex()\n");
    }
    putl_code(fp, "#endif\n");
}

static void
output_error_decl(FILE * fp)
{
    putc_code(fp, '\n');
    putl_code(fp, "/* Parameters sent to yyerror. */\n");
    putl_code(fp, "#ifndef YYERROR_DECL\n");
    puts_code(fp, "#define YYERROR_DECL() yyerror(");
#if defined(YYBTYACC)
    if (locations)
	puts_code(fp, "YYLTYPE *loc, ");
#endif
    puts_param_types(fp, parse_param, 1);
    putl_code(fp, "const char *s)\n");
    putl_code(fp, "#endif\n");

    putl_code(fp, "#ifndef YYERROR_CALL\n");

    puts_code(fp, "#define YYERROR_CALL(msg) yyerror(");
#if defined(YYBTYACC)
    if (locations)
	puts_code(fp, "&yylloc, ");
#endif
    puts_param_names(fp, parse_param, 1);
    putl_code(fp, "msg)\n");

    putl_code(fp, "#endif\n");
}

#if defined(YYBTYACC)
static void
output_yydestruct_decl(FILE * fp)
{
    putc_code(fp, '\n');
    putl_code(fp, "#ifndef YYDESTRUCT_DECL\n");

    puts_code(fp,
	      "#define YYDESTRUCT_DECL() "
	      "yydestruct(const char *msg, int psymb, YYSTYPE *val");
#if defined(YYBTYACC)
    if (locations)
	puts_code(fp, ", YYLTYPE *loc");
#endif
    if (parse_param)
    {
	puts_code(fp, ", ");
	puts_param_types(fp, parse_param, 0);
    }
    putl_code(fp, ")\n");

    putl_code(fp, "#endif\n");

    putl_code(fp, "#ifndef YYDESTRUCT_CALL\n");

    puts_code(fp, "#define YYDESTRUCT_CALL(msg, psymb, val");
#if defined(YYBTYACC)
    if (locations)
	puts_code(fp, ", loc");
#endif
    puts_code(fp, ") yydestruct(msg, psymb, val");
#if defined(YYBTYACC)
    if (locations)
	puts_code(fp, ", loc");
#endif
    if (parse_param)
    {
	puts_code(fp, ", ");
	puts_param_names(fp, parse_param, 0);
    }
    putl_code(fp, ")\n");

    putl_code(fp, "#endif\n");
}

static void
output_initial_action(void)
{
    if (initial_action)
	fprintf(code_file, "%s\n", initial_action);
}

static void
output_yydestruct_impl(void)
{
    int i;
    char *s, *destructor_code;

    putc_code(code_file, '\n');
    putl_code(code_file, "/* Release memory associated with symbol. */\n");
    putl_code(code_file, "#if ! defined YYDESTRUCT_IS_DECLARED\n");
    putl_code(code_file, "static void\n");
    putl_code(code_file, "YYDESTRUCT_DECL()\n");
    putl_code(code_file, "{\n");
    putl_code(code_file, "    switch (psymb)\n");
    putl_code(code_file, "    {\n");
    for (i = 2; i < nsyms; ++i)
    {
	if ((destructor_code = symbol_destructor[i]) != NULL)
	{
	    ++outline;
	    fprintf(code_file, "\tcase %d:\n", symbol_pval[i]);
	    /* comprehend the number of lines in the destructor code */
	    for (s = destructor_code; (s = strchr(s, '\n')) != NULL; s++)
		++outline;
	    puts_code(code_file, destructor_code);
	    putc_code(code_file, '\n');
	    putl_code(code_file, "\tbreak;\n");
	    write_code_lineno(code_file);
	    FREE(destructor_code);
	}
    }
    putl_code(code_file, "    }\n");
    putl_code(code_file, "}\n");
    putl_code(code_file, "#define YYDESTRUCT_IS_DECLARED 1\n");
    putl_code(code_file, "#endif\n");

    DO_FREE(symbol_destructor);
}
#endif

static void
free_itemsets(void)
{
    core *cp, *next;

    FREE(state_table);
    for (cp = first_state; cp; cp = next)
    {
	next = cp->next;
	FREE(cp);
    }
}

static void
free_shifts(void)
{
    shifts *sp, *next;

    FREE(shift_table);
    for (sp = first_shift; sp; sp = next)
    {
	next = sp->next;
	FREE(sp);
    }
}

static void
free_reductions(void)
{
    reductions *rp, *next;

    FREE(reduction_table);
    for (rp = first_reduction; rp; rp = next)
    {
	next = rp->next;
	FREE(rp);
    }
}

static void
output_externs(FILE * fp, const char *const section[])
{
    int i;
    const char *s;

    for (i = 0; (s = section[i]) != 0; ++i)
    {
	/* prefix non-blank lines that don't start with
	   C pre-processor directives with 'extern ' */
	if (*s && (*s != '#'))
	    fputs("extern\t", fp);
	if (fp == code_file)
	    ++outline;
	fprintf(fp, "%s\n", s);
    }
}

void
output(void)
{
    FILE *fp;

    free_itemsets();
    free_shifts();
    free_reductions();

#if defined(YYBTYACC)
    output_backtracking_parser(output_file);
    if (rflag)
	output_backtracking_parser(code_file);
#endif

    if (iflag)
    {
	write_code_lineno(code_file);
	++outline;
	fprintf(code_file, "#include \"%s\"\n", externs_file_name);
	fp = externs_file;
    }
    else
	fp = code_file;

    output_prefix(fp);
    output_pure_parser(fp);
#if defined(YY_NO_LEAKS)
    output_no_leaks(fp);
#endif
    output_stored_text(fp);
    output_stype(fp);
#if defined(YYBTYACC)
    if (locations)
	output_ltype(fp);
#endif
    output_parse_decl(fp);
    output_lex_decl(fp);
    output_error_decl(fp);
#if defined(YYBTYACC)
    if (destructor)
	output_yydestruct_decl(fp);
#endif
    if (iflag || !rflag)
    {
	write_section(fp, xdecls);
    }

    if (iflag)
    {
	output_externs(externs_file, global_vars);
	if (!pure_parser)
	    output_externs(externs_file, impure_vars);
    }

    if (iflag)
    {
	if (dflag)
	{
	    ++outline;
	    fprintf(code_file, "#include \"%s\"\n", defines_file_name);
	}
	else
	    output_defines(externs_file);
    }
    else
    {
	putc_code(code_file, '\n');
	output_defines(code_file);
    }

    if (dflag)
    {
	start_defines_file();
	output_defines(defines_file);
	end_defines_file();
    }

    output_rule_data();
    output_yydefred();
#if defined(YYBTYACC)
    output_accessing_symbols();
#endif
    output_actions();
    free_parser();
    output_debug();
    if (rflag)
    {
	write_section(code_file, xdecls);
	output_YYINT_typedef(code_file);
	write_section(code_file, tables);
    }
    write_section(code_file, global_vars);
    if (!pure_parser)
    {
	write_section(code_file, impure_vars);
    }
    write_section(code_file, hdr_defs);
    if (!pure_parser)
    {
	write_section(code_file, hdr_vars);
    }
    output_trailing_text();
#if defined(YYBTYACC)
    if (destructor)
	output_yydestruct_impl();
#endif
    write_section(code_file, body_1);
    if (pure_parser)
    {
	write_section(code_file, body_vars);
    }
    write_section(code_file, body_2);
    if (pure_parser)
    {
	write_section(code_file, init_vars);
    }
#if defined(YYBTYACC)
    if (initial_action)
	output_initial_action();
#endif
    write_section(code_file, body_3);
    output_semantic_actions();
    write_section(code_file, trailer);
}

#ifdef NO_LEAKS
void
output_leaks(void)
{
    DO_FREE(tally);
    DO_FREE(width);
    DO_FREE(order);
}
#endif
