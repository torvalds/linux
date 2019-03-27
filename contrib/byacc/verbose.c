/* $Id: verbose.c,v 1.12 2016/06/07 00:22:05 tom Exp $ */

#include "defs.h"

static void log_conflicts(void);
static void log_unused(void);
static void print_actions(int stateno);
static void print_conflicts(int state);
static void print_core(int state);
static void print_gotos(int stateno);
static void print_nulls(int state);
static void print_shifts(action *p);
static void print_state(int state);
static void print_reductions(action *p, int defred2);

static Value_t *null_rules;

void
verbose(void)
{
    int i;

    if (!vflag)
	return;

    null_rules = TMALLOC(Value_t, nrules);
    NO_SPACE(null_rules);

    fprintf(verbose_file, "\f\n");
    for (i = 0; i < nstates; i++)
	print_state(i);
    FREE(null_rules);

    if (nunused)
	log_unused();
    if (SRtotal || RRtotal)
	log_conflicts();

    fprintf(verbose_file, "\n\n%d terminals, %d nonterminals\n", ntokens,
	    nvars);
    fprintf(verbose_file, "%d grammar rules, %d states\n", nrules - 2, nstates);
#if defined(YYBTYACC)
    {				/* print out the grammar symbol # and parser internal symbol # for each
				   symbol as an aide to writing the implementation for YYDESTRUCT_CALL()
				   and YYSTYPE_TOSTRING() */
	int maxtok = 0;

	fputs("\ngrammar parser grammar\n", verbose_file);
	fputs("symbol# value# symbol\n", verbose_file);
	for (i = 0; i < ntokens; ++i)
	{
	    fprintf(verbose_file, " %5d  %5d  %s\n",
		    i, symbol_value[i], symbol_name[i]);
	    if (symbol_value[i] > maxtok)
		maxtok = symbol_value[i];
	}
	for (i = ntokens; i < nsyms; ++i)
	    fprintf(verbose_file, " %5d  %5d  %s\n",
		    i, (maxtok + 1) + symbol_value[i] + 1, symbol_name[i]);
    }
#endif
}

static void
log_unused(void)
{
    int i;
    Value_t *p;

    fprintf(verbose_file, "\n\nRules never reduced:\n");
    for (i = 3; i < nrules; ++i)
    {
	if (!rules_used[i])
	{
	    fprintf(verbose_file, "\t%s :", symbol_name[rlhs[i]]);
	    for (p = ritem + rrhs[i]; *p >= 0; ++p)
		fprintf(verbose_file, " %s", symbol_name[*p]);
	    fprintf(verbose_file, "  (%d)\n", i - 2);
	}
    }
}

static void
log_conflicts(void)
{
    int i;

    fprintf(verbose_file, "\n\n");
    for (i = 0; i < nstates; i++)
    {
	if (SRconflicts[i] || RRconflicts[i])
	{
	    fprintf(verbose_file, "State %d contains ", i);
	    if (SRconflicts[i] > 0)
		fprintf(verbose_file, "%d shift/reduce conflict%s",
			SRconflicts[i],
			PLURAL(SRconflicts[i]));
	    if (SRconflicts[i] && RRconflicts[i])
		fprintf(verbose_file, ", ");
	    if (RRconflicts[i] > 0)
		fprintf(verbose_file, "%d reduce/reduce conflict%s",
			RRconflicts[i],
			PLURAL(RRconflicts[i]));
	    fprintf(verbose_file, ".\n");
	}
    }
}

static void
print_state(int state)
{
    if (state)
	fprintf(verbose_file, "\n\n");
    if (SRconflicts[state] || RRconflicts[state])
	print_conflicts(state);
    fprintf(verbose_file, "state %d\n", state);
    print_core(state);
    print_nulls(state);
    print_actions(state);
}

static void
print_conflicts(int state)
{
    int symbol, act, number;
    action *p;

    act = 0;			/* not shift/reduce... */
    number = -1;
    symbol = -1;
    for (p = parser[state]; p; p = p->next)
    {
	if (p->suppressed == 2)
	    continue;

	if (p->symbol != symbol)
	{
	    symbol = p->symbol;
	    number = p->number;
	    if (p->action_code == SHIFT)
		act = SHIFT;
	    else
		act = REDUCE;
	}
	else if (p->suppressed == 1)
	{
	    if (state == final_state && symbol == 0)
	    {
		fprintf(verbose_file, "%d: shift/reduce conflict \
(accept, reduce %d) on $end\n", state, p->number - 2);
	    }
	    else
	    {
		if (act == SHIFT)
		{
		    fprintf(verbose_file, "%d: shift/reduce conflict \
(shift %d, reduce %d) on %s\n", state, number, p->number - 2,
			    symbol_name[symbol]);
		}
		else
		{
		    fprintf(verbose_file, "%d: reduce/reduce conflict \
(reduce %d, reduce %d) on %s\n", state, number - 2, p->number - 2,
			    symbol_name[symbol]);
		}
	    }
	}
    }
}

static void
print_core(int state)
{
    int i;
    int k;
    int rule;
    core *statep;
    Value_t *sp;
    Value_t *sp1;

    statep = state_table[state];
    k = statep->nitems;

    for (i = 0; i < k; i++)
    {
	sp1 = sp = ritem + statep->items[i];

	while (*sp >= 0)
	    ++sp;
	rule = -(*sp);
	fprintf(verbose_file, "\t%s : ", symbol_name[rlhs[rule]]);

	for (sp = ritem + rrhs[rule]; sp < sp1; sp++)
	    fprintf(verbose_file, "%s ", symbol_name[*sp]);

	putc('.', verbose_file);

	while (*sp >= 0)
	{
	    fprintf(verbose_file, " %s", symbol_name[*sp]);
	    sp++;
	}
	fprintf(verbose_file, "  (%d)\n", -2 - *sp);
    }
}

static void
print_nulls(int state)
{
    action *p;
    Value_t i, j, k, nnulls;

    nnulls = 0;
    for (p = parser[state]; p; p = p->next)
    {
	if (p->action_code == REDUCE &&
	    (p->suppressed == 0 || p->suppressed == 1))
	{
	    i = p->number;
	    if (rrhs[i] + 1 == rrhs[i + 1])
	    {
		for (j = 0; j < nnulls && i > null_rules[j]; ++j)
		    continue;

		if (j == nnulls)
		{
		    ++nnulls;
		    null_rules[j] = i;
		}
		else if (i != null_rules[j])
		{
		    ++nnulls;
		    for (k = (Value_t)(nnulls - 1); k > j; --k)
			null_rules[k] = null_rules[k - 1];
		    null_rules[j] = i;
		}
	    }
	}
    }

    for (i = 0; i < nnulls; ++i)
    {
	j = null_rules[i];
	fprintf(verbose_file, "\t%s : .  (%d)\n", symbol_name[rlhs[j]],
		j - 2);
    }
    fprintf(verbose_file, "\n");
}

static void
print_actions(int stateno)
{
    action *p;
    shifts *sp;
    int as;

    if (stateno == final_state)
	fprintf(verbose_file, "\t$end  accept\n");

    p = parser[stateno];
    if (p)
    {
	print_shifts(p);
	print_reductions(p, defred[stateno]);
    }

    sp = shift_table[stateno];
    if (sp && sp->nshifts > 0)
    {
	as = accessing_symbol[sp->shift[sp->nshifts - 1]];
	if (ISVAR(as))
	    print_gotos(stateno);
    }
}

static void
print_shifts(action *p)
{
    int count;
    action *q;

    count = 0;
    for (q = p; q; q = q->next)
    {
	if (q->suppressed < 2 && q->action_code == SHIFT)
	    ++count;
    }

    if (count > 0)
    {
	for (; p; p = p->next)
	{
	    if (p->action_code == SHIFT && p->suppressed == 0)
		fprintf(verbose_file, "\t%s  shift %d\n",
			symbol_name[p->symbol], p->number);
#if defined(YYBTYACC)
	    if (backtrack && p->action_code == SHIFT && p->suppressed == 1)
		fprintf(verbose_file, "\t%s  [trial] shift %d\n",
			symbol_name[p->symbol], p->number);
#endif
	}
    }
}

static void
print_reductions(action *p, int defred2)
{
    int k, anyreds;
    action *q;

    anyreds = 0;
    for (q = p; q; q = q->next)
    {
	if (q->action_code == REDUCE && q->suppressed < 2)
	{
	    anyreds = 1;
	    break;
	}
    }

    if (anyreds == 0)
	fprintf(verbose_file, "\t.  error\n");
    else
    {
	for (; p; p = p->next)
	{
	    if (p->action_code == REDUCE && p->number != defred2)
	    {
		k = p->number - 2;
		if (p->suppressed == 0)
		    fprintf(verbose_file, "\t%s  reduce %d\n",
			    symbol_name[p->symbol], k);
#if defined(YYBTYACC)
		if (backtrack && p->suppressed == 1)
		    fprintf(verbose_file, "\t%s  [trial] reduce %d\n",
			    symbol_name[p->symbol], k);
#endif
	    }
	}

	if (defred2 > 0)
	    fprintf(verbose_file, "\t.  reduce %d\n", defred2 - 2);
    }
}

static void
print_gotos(int stateno)
{
    int i, k;
    int as;
    Value_t *to_state2;
    shifts *sp;

    putc('\n', verbose_file);
    sp = shift_table[stateno];
    to_state2 = sp->shift;
    for (i = 0; i < sp->nshifts; ++i)
    {
	k = to_state2[i];
	as = accessing_symbol[k];
	if (ISVAR(as))
	    fprintf(verbose_file, "\t%s  goto %d\n", symbol_name[as], k);
    }
}
