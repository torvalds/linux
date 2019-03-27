/* $Id: mkpar.c,v 1.15 2016/06/07 00:22:12 tom Exp $ */

#include "defs.h"

#define NotSuppressed(p)	((p)->suppressed == 0)

#if defined(YYBTYACC)
#define MaySuppress(p)		((backtrack ? ((p)->suppressed <= 1) : (p)->suppressed == 0))
    /* suppress the preferred action => enable backtracking */
#define StartBacktrack(p)	if (backtrack && (p) != NULL && NotSuppressed(p)) (p)->suppressed = 1
#else
#define MaySuppress(p)		((p)->suppressed == 0)
#define StartBacktrack(p)	/*nothing */
#endif

static action *add_reduce(action *actions, int ruleno, int symbol);
static action *add_reductions(int stateno, action *actions);
static action *get_shifts(int stateno);
static action *parse_actions(int stateno);
static int sole_reduction(int stateno);
static void defreds(void);
static void find_final_state(void);
static void free_action_row(action *p);
static void remove_conflicts(void);
static void total_conflicts(void);
static void unused_rules(void);

action **parser;

int SRexpect;
int RRexpect;

int SRtotal;
int RRtotal;

Value_t *SRconflicts;
Value_t *RRconflicts;
Value_t *defred;
Value_t *rules_used;
Value_t nunused;
Value_t final_state;

static Value_t SRcount;
static Value_t RRcount;

void
make_parser(void)
{
    int i;

    parser = NEW2(nstates, action *);
    for (i = 0; i < nstates; i++)
	parser[i] = parse_actions(i);

    find_final_state();
    remove_conflicts();
    unused_rules();
    if (SRtotal + RRtotal > 0)
	total_conflicts();
    defreds();
}

static action *
parse_actions(int stateno)
{
    action *actions;

    actions = get_shifts(stateno);
    actions = add_reductions(stateno, actions);
    return (actions);
}

static action *
get_shifts(int stateno)
{
    action *actions, *temp;
    shifts *sp;
    Value_t *to_state2;
    Value_t i, k;
    Value_t symbol;

    actions = 0;
    sp = shift_table[stateno];
    if (sp)
    {
	to_state2 = sp->shift;
	for (i = (Value_t)(sp->nshifts - 1); i >= 0; i--)
	{
	    k = to_state2[i];
	    symbol = accessing_symbol[k];
	    if (ISTOKEN(symbol))
	    {
		temp = NEW(action);
		temp->next = actions;
		temp->symbol = symbol;
		temp->number = k;
		temp->prec = symbol_prec[symbol];
		temp->action_code = SHIFT;
		temp->assoc = symbol_assoc[symbol];
		actions = temp;
	    }
	}
    }
    return (actions);
}

static action *
add_reductions(int stateno, action *actions)
{
    int i, j, m, n;
    int ruleno, tokensetsize;
    unsigned *rowp;

    tokensetsize = WORDSIZE(ntokens);
    m = lookaheads[stateno];
    n = lookaheads[stateno + 1];
    for (i = m; i < n; i++)
    {
	ruleno = LAruleno[i];
	rowp = LA + i * tokensetsize;
	for (j = ntokens - 1; j >= 0; j--)
	{
	    if (BIT(rowp, j))
		actions = add_reduce(actions, ruleno, j);
	}
    }
    return (actions);
}

static action *
add_reduce(action *actions,
	   int ruleno,
	   int symbol)
{
    action *temp, *prev, *next;

    prev = 0;
    for (next = actions; next && next->symbol < symbol; next = next->next)
	prev = next;

    while (next && next->symbol == symbol && next->action_code == SHIFT)
    {
	prev = next;
	next = next->next;
    }

    while (next && next->symbol == symbol &&
	   next->action_code == REDUCE && next->number < ruleno)
    {
	prev = next;
	next = next->next;
    }

    temp = NEW(action);
    temp->next = next;
    temp->symbol = (Value_t)symbol;
    temp->number = (Value_t)ruleno;
    temp->prec = rprec[ruleno];
    temp->action_code = REDUCE;
    temp->assoc = rassoc[ruleno];

    if (prev)
	prev->next = temp;
    else
	actions = temp;

    return (actions);
}

static void
find_final_state(void)
{
    int goal, i;
    Value_t *to_state2;
    shifts *p;

    p = shift_table[0];
    to_state2 = p->shift;
    goal = ritem[1];
    for (i = p->nshifts - 1; i >= 0; --i)
    {
	final_state = to_state2[i];
	if (accessing_symbol[final_state] == goal)
	    break;
    }
}

static void
unused_rules(void)
{
    int i;
    action *p;

    rules_used = TMALLOC(Value_t, nrules);
    NO_SPACE(rules_used);

    for (i = 0; i < nrules; ++i)
	rules_used[i] = 0;

    for (i = 0; i < nstates; ++i)
    {
	for (p = parser[i]; p; p = p->next)
	{
	    if ((p->action_code == REDUCE) && MaySuppress(p))
		rules_used[p->number] = 1;
	}
    }

    nunused = 0;
    for (i = 3; i < nrules; ++i)
	if (!rules_used[i])
	    ++nunused;

    if (nunused)
    {
	if (nunused == 1)
	    fprintf(stderr, "%s: 1 rule never reduced\n", myname);
	else
	    fprintf(stderr, "%s: %d rules never reduced\n", myname, nunused);
    }
}

static void
remove_conflicts(void)
{
    int i;
    int symbol;
    action *p, *pref = 0;

    SRtotal = 0;
    RRtotal = 0;
    SRconflicts = NEW2(nstates, Value_t);
    RRconflicts = NEW2(nstates, Value_t);
    for (i = 0; i < nstates; i++)
    {
	SRcount = 0;
	RRcount = 0;
	symbol = -1;
#if defined(YYBTYACC)
	pref = NULL;
#endif
	for (p = parser[i]; p; p = p->next)
	{
	    if (p->symbol != symbol)
	    {
		/* the first parse action for each symbol is the preferred action */
		pref = p;
		symbol = p->symbol;
	    }
	    /* following conditions handle multiple, i.e., conflicting, parse actions */
	    else if (i == final_state && symbol == 0)
	    {
		SRcount++;
		p->suppressed = 1;
		StartBacktrack(pref);
	    }
	    else if (pref != 0 && pref->action_code == SHIFT)
	    {
		if (pref->prec > 0 && p->prec > 0)
		{
		    if (pref->prec < p->prec)
		    {
			pref->suppressed = 2;
			pref = p;
		    }
		    else if (pref->prec > p->prec)
		    {
			p->suppressed = 2;
		    }
		    else if (pref->assoc == LEFT)
		    {
			pref->suppressed = 2;
			pref = p;
		    }
		    else if (pref->assoc == RIGHT)
		    {
			p->suppressed = 2;
		    }
		    else
		    {
			pref->suppressed = 2;
			p->suppressed = 2;
		    }
		}
		else
		{
		    SRcount++;
		    p->suppressed = 1;
		    StartBacktrack(pref);
		}
	    }
	    else
	    {
		RRcount++;
		p->suppressed = 1;
		StartBacktrack(pref);
	    }
	}
	SRtotal += SRcount;
	RRtotal += RRcount;
	SRconflicts[i] = SRcount;
	RRconflicts[i] = RRcount;
    }
}

static void
total_conflicts(void)
{
    fprintf(stderr, "%s: ", myname);
    if (SRtotal == 1)
	fprintf(stderr, "1 shift/reduce conflict");
    else if (SRtotal > 1)
	fprintf(stderr, "%d shift/reduce conflicts", SRtotal);

    if (SRtotal && RRtotal)
	fprintf(stderr, ", ");

    if (RRtotal == 1)
	fprintf(stderr, "1 reduce/reduce conflict");
    else if (RRtotal > 1)
	fprintf(stderr, "%d reduce/reduce conflicts", RRtotal);

    fprintf(stderr, ".\n");

    if (SRexpect >= 0 && SRtotal != SRexpect)
    {
	fprintf(stderr, "%s: ", myname);
	fprintf(stderr, "expected %d shift/reduce conflict%s.\n",
		SRexpect, PLURAL(SRexpect));
	exit_code = EXIT_FAILURE;
    }
    if (RRexpect >= 0 && RRtotal != RRexpect)
    {
	fprintf(stderr, "%s: ", myname);
	fprintf(stderr, "expected %d reduce/reduce conflict%s.\n",
		RRexpect, PLURAL(RRexpect));
	exit_code = EXIT_FAILURE;
    }
}

static int
sole_reduction(int stateno)
{
    int count, ruleno;
    action *p;

    count = 0;
    ruleno = 0;
    for (p = parser[stateno]; p; p = p->next)
    {
	if (p->action_code == SHIFT && MaySuppress(p))
	    return (0);
	else if ((p->action_code == REDUCE) && MaySuppress(p))
	{
	    if (ruleno > 0 && p->number != ruleno)
		return (0);
	    if (p->symbol != 1)
		++count;
	    ruleno = p->number;
	}
    }

    if (count == 0)
	return (0);
    return (ruleno);
}

static void
defreds(void)
{
    int i;

    defred = NEW2(nstates, Value_t);
    for (i = 0; i < nstates; i++)
	defred[i] = (Value_t)sole_reduction(i);
}

static void
free_action_row(action *p)
{
    action *q;

    while (p)
    {
	q = p->next;
	FREE(p);
	p = q;
    }
}

void
free_parser(void)
{
    int i;

    for (i = 0; i < nstates; i++)
	free_action_row(parser[i]);

    FREE(parser);
}

#ifdef NO_LEAKS
void
mkpar_leaks(void)
{
    DO_FREE(defred);
    DO_FREE(rules_used);
    DO_FREE(SRconflicts);
    DO_FREE(RRconflicts);
}
#endif
