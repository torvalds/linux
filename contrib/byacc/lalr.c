/* $Id: lalr.c,v 1.12 2016/06/07 00:28:03 tom Exp $ */

#include "defs.h"

typedef struct shorts
{
    struct shorts *next;
    Value_t value;
}
shorts;

static Value_t map_goto(int state, int symbol);
static Value_t **transpose(Value_t **R, int n);
static void add_lookback_edge(int stateno, int ruleno, int gotono);
static void build_relations(void);
static void compute_FOLLOWS(void);
static void compute_lookaheads(void);
static void digraph(Value_t **relation);
static void initialize_F(void);
static void initialize_LA(void);
static void set_accessing_symbol(void);
static void set_goto_map(void);
static void set_maxrhs(void);
static void set_reduction_table(void);
static void set_shift_table(void);
static void set_state_table(void);
static void traverse(int i);

static int tokensetsize;
Value_t *lookaheads;
Value_t *LAruleno;
unsigned *LA;
Value_t *accessing_symbol;
core **state_table;
shifts **shift_table;
reductions **reduction_table;
Value_t *goto_base;
Value_t *goto_map;
Value_t *from_state;
Value_t *to_state;

static Value_t infinity;
static int maxrhs;
static int ngotos;
static unsigned *F;
static Value_t **includes;
static shorts **lookback;
static Value_t **R;
static Value_t *INDEX;
static Value_t *VERTICES;
static Value_t top;

void
lalr(void)
{
    tokensetsize = WORDSIZE(ntokens);

    set_state_table();
    set_accessing_symbol();
    set_shift_table();
    set_reduction_table();
    set_maxrhs();
    initialize_LA();
    set_goto_map();
    initialize_F();
    build_relations();
    compute_FOLLOWS();
    compute_lookaheads();
}

static void
set_state_table(void)
{
    core *sp;

    state_table = NEW2(nstates, core *);
    for (sp = first_state; sp; sp = sp->next)
	state_table[sp->number] = sp;
}

static void
set_accessing_symbol(void)
{
    core *sp;

    accessing_symbol = NEW2(nstates, Value_t);
    for (sp = first_state; sp; sp = sp->next)
	accessing_symbol[sp->number] = sp->accessing_symbol;
}

static void
set_shift_table(void)
{
    shifts *sp;

    shift_table = NEW2(nstates, shifts *);
    for (sp = first_shift; sp; sp = sp->next)
	shift_table[sp->number] = sp;
}

static void
set_reduction_table(void)
{
    reductions *rp;

    reduction_table = NEW2(nstates, reductions *);
    for (rp = first_reduction; rp; rp = rp->next)
	reduction_table[rp->number] = rp;
}

static void
set_maxrhs(void)
{
    Value_t *itemp;
    Value_t *item_end;
    int length;
    int max;

    length = 0;
    max = 0;
    item_end = ritem + nitems;
    for (itemp = ritem; itemp < item_end; itemp++)
    {
	if (*itemp >= 0)
	{
	    length++;
	}
	else
	{
	    if (length > max)
		max = length;
	    length = 0;
	}
    }

    maxrhs = max;
}

static void
initialize_LA(void)
{
    int i, j, k;
    reductions *rp;

    lookaheads = NEW2(nstates + 1, Value_t);

    k = 0;
    for (i = 0; i < nstates; i++)
    {
	lookaheads[i] = (Value_t)k;
	rp = reduction_table[i];
	if (rp)
	    k += rp->nreds;
    }
    lookaheads[nstates] = (Value_t)k;

    LA = NEW2(k * tokensetsize, unsigned);
    LAruleno = NEW2(k, Value_t);
    lookback = NEW2(k, shorts *);

    k = 0;
    for (i = 0; i < nstates; i++)
    {
	rp = reduction_table[i];
	if (rp)
	{
	    for (j = 0; j < rp->nreds; j++)
	    {
		LAruleno[k] = rp->rules[j];
		k++;
	    }
	}
    }
}

static void
set_goto_map(void)
{
    shifts *sp;
    int i;
    int symbol;
    int k;
    Value_t *temp_base;
    Value_t *temp_map;
    Value_t state2;
    Value_t state1;

    goto_base = NEW2(nvars + 1, Value_t);
    temp_base = NEW2(nvars + 1, Value_t);

    goto_map = goto_base - ntokens;
    temp_map = temp_base - ntokens;

    ngotos = 0;
    for (sp = first_shift; sp; sp = sp->next)
    {
	for (i = sp->nshifts - 1; i >= 0; i--)
	{
	    symbol = accessing_symbol[sp->shift[i]];

	    if (ISTOKEN(symbol))
		break;

	    if (ngotos == MAXYYINT)
		fatal("too many gotos");

	    ngotos++;
	    goto_map[symbol]++;
	}
    }

    k = 0;
    for (i = ntokens; i < nsyms; i++)
    {
	temp_map[i] = (Value_t)k;
	k += goto_map[i];
    }

    for (i = ntokens; i < nsyms; i++)
	goto_map[i] = temp_map[i];

    goto_map[nsyms] = (Value_t)ngotos;
    temp_map[nsyms] = (Value_t)ngotos;

    from_state = NEW2(ngotos, Value_t);
    to_state = NEW2(ngotos, Value_t);

    for (sp = first_shift; sp; sp = sp->next)
    {
	state1 = sp->number;
	for (i = sp->nshifts - 1; i >= 0; i--)
	{
	    state2 = sp->shift[i];
	    symbol = accessing_symbol[state2];

	    if (ISTOKEN(symbol))
		break;

	    k = temp_map[symbol]++;
	    from_state[k] = state1;
	    to_state[k] = state2;
	}
    }

    FREE(temp_base);
}

/*  Map_goto maps a state/symbol pair into its numeric representation.	*/

static Value_t
map_goto(int state, int symbol)
{
    int high;
    int low;
    int middle;
    int s;

    low = goto_map[symbol];
    high = goto_map[symbol + 1];

    for (;;)
    {
	assert(low <= high);
	middle = (low + high) >> 1;
	s = from_state[middle];
	if (s == state)
	    return (Value_t)(middle);
	else if (s < state)
	    low = middle + 1;
	else
	    high = middle - 1;
    }
}

static void
initialize_F(void)
{
    int i;
    int j;
    int k;
    shifts *sp;
    Value_t *edge;
    unsigned *rowp;
    Value_t *rp;
    Value_t **reads;
    int nedges;
    int stateno;
    int symbol;
    int nwords;

    nwords = ngotos * tokensetsize;
    F = NEW2(nwords, unsigned);

    reads = NEW2(ngotos, Value_t *);
    edge = NEW2(ngotos + 1, Value_t);
    nedges = 0;

    rowp = F;
    for (i = 0; i < ngotos; i++)
    {
	stateno = to_state[i];
	sp = shift_table[stateno];

	if (sp)
	{
	    k = sp->nshifts;

	    for (j = 0; j < k; j++)
	    {
		symbol = accessing_symbol[sp->shift[j]];
		if (ISVAR(symbol))
		    break;
		SETBIT(rowp, symbol);
	    }

	    for (; j < k; j++)
	    {
		symbol = accessing_symbol[sp->shift[j]];
		if (nullable[symbol])
		    edge[nedges++] = map_goto(stateno, symbol);
	    }

	    if (nedges)
	    {
		reads[i] = rp = NEW2(nedges + 1, Value_t);

		for (j = 0; j < nedges; j++)
		    rp[j] = edge[j];

		rp[nedges] = -1;
		nedges = 0;
	    }
	}

	rowp += tokensetsize;
    }

    SETBIT(F, 0);
    digraph(reads);

    for (i = 0; i < ngotos; i++)
    {
	if (reads[i])
	    FREE(reads[i]);
    }

    FREE(reads);
    FREE(edge);
}

static void
build_relations(void)
{
    int i;
    int j;
    int k;
    Value_t *rulep;
    Value_t *rp;
    shifts *sp;
    int length;
    int nedges;
    int done_flag;
    Value_t state1;
    Value_t stateno;
    int symbol1;
    int symbol2;
    Value_t *shortp;
    Value_t *edge;
    Value_t *states;
    Value_t **new_includes;

    includes = NEW2(ngotos, Value_t *);
    edge = NEW2(ngotos + 1, Value_t);
    states = NEW2(maxrhs + 1, Value_t);

    for (i = 0; i < ngotos; i++)
    {
	nedges = 0;
	state1 = from_state[i];
	symbol1 = accessing_symbol[to_state[i]];

	for (rulep = derives[symbol1]; *rulep >= 0; rulep++)
	{
	    length = 1;
	    states[0] = state1;
	    stateno = state1;

	    for (rp = ritem + rrhs[*rulep]; *rp >= 0; rp++)
	    {
		symbol2 = *rp;
		sp = shift_table[stateno];
		k = sp->nshifts;

		for (j = 0; j < k; j++)
		{
		    stateno = sp->shift[j];
		    if (accessing_symbol[stateno] == symbol2)
			break;
		}

		states[length++] = stateno;
	    }

	    add_lookback_edge(stateno, *rulep, i);

	    length--;
	    done_flag = 0;
	    while (!done_flag)
	    {
		done_flag = 1;
		rp--;
		if (ISVAR(*rp))
		{
		    stateno = states[--length];
		    edge[nedges++] = map_goto(stateno, *rp);
		    if (nullable[*rp] && length > 0)
			done_flag = 0;
		}
	    }
	}

	if (nedges)
	{
	    includes[i] = shortp = NEW2(nedges + 1, Value_t);
	    for (j = 0; j < nedges; j++)
		shortp[j] = edge[j];
	    shortp[nedges] = -1;
	}
    }

    new_includes = transpose(includes, ngotos);

    for (i = 0; i < ngotos; i++)
	if (includes[i])
	    FREE(includes[i]);

    FREE(includes);

    includes = new_includes;

    FREE(edge);
    FREE(states);
}

static void
add_lookback_edge(int stateno, int ruleno, int gotono)
{
    int i, k;
    int found;
    shorts *sp;

    i = lookaheads[stateno];
    k = lookaheads[stateno + 1];
    found = 0;
    while (!found && i < k)
    {
	if (LAruleno[i] == ruleno)
	    found = 1;
	else
	    ++i;
    }
    assert(found);

    sp = NEW(shorts);
    sp->next = lookback[i];
    sp->value = (Value_t)gotono;
    lookback[i] = sp;
}

static Value_t **
transpose(Value_t **R2, int n)
{
    Value_t **new_R;
    Value_t **temp_R;
    Value_t *nedges;
    Value_t *sp;
    int i;
    int k;

    nedges = NEW2(n, Value_t);

    for (i = 0; i < n; i++)
    {
	sp = R2[i];
	if (sp)
	{
	    while (*sp >= 0)
		nedges[*sp++]++;
	}
    }

    new_R = NEW2(n, Value_t *);
    temp_R = NEW2(n, Value_t *);

    for (i = 0; i < n; i++)
    {
	k = nedges[i];
	if (k > 0)
	{
	    sp = NEW2(k + 1, Value_t);
	    new_R[i] = sp;
	    temp_R[i] = sp;
	    sp[k] = -1;
	}
    }

    FREE(nedges);

    for (i = 0; i < n; i++)
    {
	sp = R2[i];
	if (sp)
	{
	    while (*sp >= 0)
		*temp_R[*sp++]++ = (Value_t)i;
	}
    }

    FREE(temp_R);

    return (new_R);
}

static void
compute_FOLLOWS(void)
{
    digraph(includes);
}

static void
compute_lookaheads(void)
{
    int i, n;
    unsigned *fp1, *fp2, *fp3;
    shorts *sp, *next;
    unsigned *rowp;

    rowp = LA;
    n = lookaheads[nstates];
    for (i = 0; i < n; i++)
    {
	fp3 = rowp + tokensetsize;
	for (sp = lookback[i]; sp; sp = sp->next)
	{
	    fp1 = rowp;
	    fp2 = F + tokensetsize * sp->value;
	    while (fp1 < fp3)
		*fp1++ |= *fp2++;
	}
	rowp = fp3;
    }

    for (i = 0; i < n; i++)
	for (sp = lookback[i]; sp; sp = next)
	{
	    next = sp->next;
	    FREE(sp);
	}

    FREE(lookback);
    FREE(F);
}

static void
digraph(Value_t **relation)
{
    int i;

    infinity = (Value_t)(ngotos + 2);
    INDEX = NEW2(ngotos + 1, Value_t);
    VERTICES = NEW2(ngotos + 1, Value_t);
    top = 0;

    R = relation;

    for (i = 0; i < ngotos; i++)
	INDEX[i] = 0;

    for (i = 0; i < ngotos; i++)
    {
	if (INDEX[i] == 0 && R[i])
	    traverse(i);
    }

    FREE(INDEX);
    FREE(VERTICES);
}

static void
traverse(int i)
{
    unsigned *fp1;
    unsigned *fp2;
    unsigned *fp3;
    int j;
    Value_t *rp;

    Value_t height;
    unsigned *base;

    VERTICES[++top] = (Value_t)i;
    INDEX[i] = height = top;

    base = F + i * tokensetsize;
    fp3 = base + tokensetsize;

    rp = R[i];
    if (rp)
    {
	while ((j = *rp++) >= 0)
	{
	    if (INDEX[j] == 0)
		traverse(j);

	    if (INDEX[i] > INDEX[j])
		INDEX[i] = INDEX[j];

	    fp1 = base;
	    fp2 = F + j * tokensetsize;

	    while (fp1 < fp3)
		*fp1++ |= *fp2++;
	}
    }

    if (INDEX[i] == height)
    {
	for (;;)
	{
	    j = VERTICES[top--];
	    INDEX[j] = infinity;

	    if (i == j)
		break;

	    fp1 = base;
	    fp2 = F + j * tokensetsize;

	    while (fp1 < fp3)
		*fp2++ = *fp1++;
	}
    }
}

#ifdef NO_LEAKS
void
lalr_leaks(void)
{
    int i;

    if (includes != 0)
    {
	for (i = 0; i < ngotos; i++)
	{
	    free(includes[i]);
	}
	DO_FREE(includes);
    }
}
#endif
