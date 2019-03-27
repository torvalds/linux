/* $Id: closure.c,v 1.11 2014/09/18 00:40:07 tom Exp $ */

#include "defs.h"

Value_t *itemset;
Value_t *itemsetend;
unsigned *ruleset;

static unsigned *first_base;
static unsigned *first_derives;
static unsigned *EFF;

#ifdef	DEBUG
static void print_closure(int);
static void print_EFF(void);
static void print_first_derives(void);
#endif

static void
set_EFF(void)
{
    unsigned *row;
    int symbol;
    Value_t *sp;
    int rowsize;
    int i;
    int rule;

    rowsize = WORDSIZE(nvars);
    EFF = NEW2(nvars * rowsize, unsigned);

    row = EFF;
    for (i = start_symbol; i < nsyms; i++)
    {
	sp = derives[i];
	for (rule = *sp; rule > 0; rule = *++sp)
	{
	    symbol = ritem[rrhs[rule]];
	    if (ISVAR(symbol))
	    {
		symbol -= start_symbol;
		SETBIT(row, symbol);
	    }
	}
	row += rowsize;
    }

    reflexive_transitive_closure(EFF, nvars);

#ifdef	DEBUG
    print_EFF();
#endif
}

void
set_first_derives(void)
{
    unsigned *rrow;
    unsigned *vrow;
    int j;
    unsigned k;
    unsigned cword = 0;
    Value_t *rp;

    int rule;
    int i;
    int rulesetsize;
    int varsetsize;

    rulesetsize = WORDSIZE(nrules);
    varsetsize = WORDSIZE(nvars);
    first_base = NEW2(nvars * rulesetsize, unsigned);
    first_derives = first_base - ntokens * rulesetsize;

    set_EFF();

    rrow = first_derives + ntokens * rulesetsize;
    for (i = start_symbol; i < nsyms; i++)
    {
	vrow = EFF + ((i - ntokens) * varsetsize);
	k = BITS_PER_WORD;
	for (j = start_symbol; j < nsyms; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		cword = *vrow++;
		k = 0;
	    }

	    if (cword & (unsigned)(1 << k))
	    {
		rp = derives[j];
		while ((rule = *rp++) >= 0)
		{
		    SETBIT(rrow, rule);
		}
	    }
	}

	rrow += rulesetsize;
    }

#ifdef	DEBUG
    print_first_derives();
#endif

    FREE(EFF);
}

void
closure(Value_t *nucleus, int n)
{
    unsigned ruleno;
    unsigned word;
    unsigned i;
    Value_t *csp;
    unsigned *dsp;
    unsigned *rsp;
    int rulesetsize;

    Value_t *csend;
    unsigned *rsend;
    int symbol;
    Value_t itemno;

    rulesetsize = WORDSIZE(nrules);
    rsend = ruleset + rulesetsize;
    for (rsp = ruleset; rsp < rsend; rsp++)
	*rsp = 0;

    csend = nucleus + n;
    for (csp = nucleus; csp < csend; ++csp)
    {
	symbol = ritem[*csp];
	if (ISVAR(symbol))
	{
	    dsp = first_derives + symbol * rulesetsize;
	    rsp = ruleset;
	    while (rsp < rsend)
		*rsp++ |= *dsp++;
	}
    }

    ruleno = 0;
    itemsetend = itemset;
    csp = nucleus;
    for (rsp = ruleset; rsp < rsend; ++rsp)
    {
	word = *rsp;
	if (word)
	{
	    for (i = 0; i < BITS_PER_WORD; ++i)
	    {
		if (word & (unsigned)(1 << i))
		{
		    itemno = rrhs[ruleno + i];
		    while (csp < csend && *csp < itemno)
			*itemsetend++ = *csp++;
		    *itemsetend++ = itemno;
		    while (csp < csend && *csp == itemno)
			++csp;
		}
	    }
	}
	ruleno += BITS_PER_WORD;
    }

    while (csp < csend)
	*itemsetend++ = *csp++;

#ifdef	DEBUG
    print_closure(n);
#endif
}

void
finalize_closure(void)
{
    FREE(itemset);
    FREE(ruleset);
    FREE(first_base);
}

#ifdef	DEBUG

static void
print_closure(int n)
{
    Value_t *isp;

    printf("\n\nn = %d\n\n", n);
    for (isp = itemset; isp < itemsetend; isp++)
	printf("   %d\n", *isp);
}

static void
print_EFF(void)
{
    int i, j;
    unsigned *rowp;
    unsigned word;
    unsigned k;

    printf("\n\nEpsilon Free Firsts\n");

    for (i = start_symbol; i < nsyms; i++)
    {
	printf("\n%s", symbol_name[i]);
	rowp = EFF + ((i - start_symbol) * WORDSIZE(nvars));
	word = *rowp++;

	k = BITS_PER_WORD;
	for (j = 0; j < nvars; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		word = *rowp++;
		k = 0;
	    }

	    if (word & (1 << k))
		printf("  %s", symbol_name[start_symbol + j]);
	}
    }
}

static void
print_first_derives(void)
{
    int i;
    int j;
    unsigned *rp;
    unsigned cword = 0;
    unsigned k;

    printf("\n\n\nFirst Derives\n");

    for (i = start_symbol; i < nsyms; i++)
    {
	printf("\n%s derives\n", symbol_name[i]);
	rp = first_derives + i * WORDSIZE(nrules);
	k = BITS_PER_WORD;
	for (j = 0; j <= nrules; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		cword = *rp++;
		k = 0;
	    }

	    if (cword & (1 << k))
		printf("   %d\n", j);
	}
    }

    fflush(stdout);
}

#endif
