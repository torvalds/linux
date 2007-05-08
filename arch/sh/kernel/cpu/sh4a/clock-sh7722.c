/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7722.c
 *
 * SH7722 support for the clock framework
 *
 * Copyright (c) 2006-2007 Nomad Global Solutions Inc
 * Based on code for sh7343 by Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <asm/clock.h>
#include <asm/freq.h>

#define SH7722_PLL_FREQ (32000000/8)
#define N  (-1)
#define NM (-2)
#define ROUND_NEAREST 0
#define ROUND_DOWN -1
#define ROUND_UP   +1

static int adjust_algos[][3] = {
	{},	/* NO_CHANGE */
	{ NM, N, 1 },   /* N:1, N:1 */
	{ 3, 2, 2 },	/* 3:2:2 */
	{ 5, 2, 2 },    /* 5:2:2 */
	{ N, 1, 1 },	/* N:1:1 */

	{ N, 1 },	/* N:1 */

	{ N, 1 },	/* N:1 */
	{ 3, 2 },
	{ 4, 3 },
	{ 5, 4 },

	{ N, 1 }
};

static unsigned long adjust_pair_of_clocks(unsigned long r1, unsigned long r2,
			int m1, int m2, int round_flag)
{
	unsigned long rem, div;
	int the_one = 0;

	pr_debug( "Actual values: r1 = %ld\n", r1);
	pr_debug( "...............r2 = %ld\n", r2);

	if (m1 == m2) {
		r2 = r1;
		pr_debug( "setting equal rates: r2 now %ld\n", r2);
	} else if ((m2 == N  && m1 == 1) ||
		   (m2 == NM && m1 == N)) { /* N:1 or NM:N */
		pr_debug( "Setting rates as 1:N (N:N*M)\n");
		rem = r2 % r1;
		pr_debug( "...remainder = %ld\n", rem);
		if (rem) {
			div = r2 / r1;
			pr_debug( "...div = %ld\n", div);
			switch (round_flag) {
			case ROUND_NEAREST:
				the_one = rem >= r1/2 ? 1 : 0; break;
			case ROUND_UP:
				the_one = 1; break;
			case ROUND_DOWN:
				the_one = 0; break;
			}

			r2 = r1 * (div + the_one);
			pr_debug( "...setting r2 to %ld\n", r2);
		}
	} else if ((m2 == 1  && m1 == N) ||
		   (m2 == N && m1 == NM)) { /* 1:N or N:NM */
		pr_debug( "Setting rates as N:1 (N*M:N)\n");
		rem = r1 % r2;
		pr_debug( "...remainder = %ld\n", rem);
		if (rem) {
			div = r1 / r2;
			pr_debug( "...div = %ld\n", div);
			switch (round_flag) {
			case ROUND_NEAREST:
				the_one = rem > r2/2 ? 1 : 0; break;
			case ROUND_UP:
				the_one = 0; break;
			case ROUND_DOWN:
				the_one = 1; break;
			}

			r2 = r1 / (div + the_one);
			pr_debug( "...setting r2 to %ld\n", r2);
		}
	} else { /* value:value */
		pr_debug( "Setting rates as %d:%d\n", m1, m2);
		div = r1 / m1;
		r2 = div * m2;
		pr_debug( "...div = %ld\n", div);
		pr_debug( "...setting r2 to %ld\n", r2);
	}

	return r2;
}

static void adjust_clocks(int originate, int *l, unsigned long v[],
			  int n_in_line)
{
	int x;

	pr_debug( "Go down from %d...\n", originate);
	/* go up recalculation clocks */
	for (x = originate; x>0; x -- )
		v[x-1] = adjust_pair_of_clocks(v[x], v[x-1],
					l[x], l[x-1],
					ROUND_UP);

	pr_debug( "Go up from %d...\n", originate);
	/* go down recalculation clocks */
	for (x = originate; x<n_in_line - 1; x ++ )
		v[x+1] = adjust_pair_of_clocks(v[x], v[x+1],
					l[x], l[x+1],
					ROUND_UP);
}


/*
 * SH7722 uses a common set of multipliers and divisors, so this
 * is quite simple..
 */

/*
 * Instead of having two separate multipliers/divisors set, like this:
 *
 * static int multipliers[] = { 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
 * static int divisors[] = { 1, 3, 2, 5, 3, 4, 5, 6, 8, 10, 12, 16, 20 };
 *
 * I created the divisors2 array, which is used to calculate rate like
 *   rate = parent * 2 / divisors2[ divisor ];
*/
static int divisors2[] = { 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40 };

static void master_clk_init(struct clk *clk)
{
	clk_set_rate(clk, clk_get_rate(clk));
}

static void master_clk_recalc(struct clk *clk)
{
	unsigned long frqcr = ctrl_inl(FRQCR);

	clk->rate = CONFIG_SH_PCLK_FREQ * (1 + (frqcr >> 24 & 0xF));
}

static int master_clk_setrate(struct clk *clk, unsigned long rate, int id)
{
	int div = rate / SH7722_PLL_FREQ;
	int master_divs[] = { 2, 3, 4, 6, 8, 16 };
	int index;
	unsigned long frqcr;

	if (rate < SH7722_PLL_FREQ * 2)
		return -EINVAL;

	for (index = 1; index < ARRAY_SIZE(master_divs); index++)
		if (div >= master_divs[index - 1] && div < master_divs[index])
			break;

	if (index >= ARRAY_SIZE(master_divs))
		index = ARRAY_SIZE(master_divs);
	div = master_divs[index - 1];

	frqcr = ctrl_inl(FRQCR);
	frqcr &= ~(0xF << 24);
	frqcr |= ( (div-1) << 24);
	ctrl_outl(frqcr, FRQCR);

	return 0;
}

static struct clk_ops sh7722_master_clk_ops = {
	.init = master_clk_init,
	.recalc = master_clk_recalc,
	.set_rate = master_clk_setrate,
};

struct frqcr_context {
	unsigned mask;
	unsigned shift;
};

struct frqcr_context sh7722_get_clk_context(const char *name)
{
	struct frqcr_context ctx = { 0, };

	if (!strcmp(name, "peripheral_clk")) {
		ctx.shift = 0;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "sdram_clk")) {
		ctx.shift = 4;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "bus_clk")) {
		ctx.shift = 8;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "sh_clk")) {
		ctx.shift = 12;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "umem_clk")) {
		ctx.shift = 16;
		ctx.mask = 0xF;
	} else if (!strcmp(name, "cpu_clk")) {
		ctx.shift = 20;
		ctx.mask = 7;
	}
	return ctx;
}

/**
 * sh7722_find_divisors - find divisor for setting rate
 *
 * All sh7722 clocks use the same set of multipliers/divisors. This function
 * chooses correct divisor to set the rate of clock with parent clock that
 * generates frequency of 'parent_rate'
 *
 * @parent_rate: rate of parent clock
 * @rate: requested rate to be set
 */
static int sh7722_find_divisors(unsigned long parent_rate, unsigned rate)
{
	unsigned div2 = parent_rate * 2 / rate;
	int index;

	if (rate > parent_rate)
		return -EINVAL;

	for (index = 1; index < ARRAY_SIZE(divisors2); index++) {
		if (div2 > divisors2[index] && div2 <= divisors2[index])
			break;
	}
	if (index >= ARRAY_SIZE(divisors2))
		index = ARRAY_SIZE(divisors2) - 1;
	return divisors2[index];
}

static void sh7722_frqcr_recalc(struct clk *clk)
{
	struct frqcr_context ctx = sh7722_get_clk_context(clk->name);
	unsigned long frqcr = ctrl_inl(FRQCR);
	int index;

	index = (frqcr >> ctx.shift) & ctx.mask;
	clk->rate = clk->parent->rate * 2 / divisors2[index];
}

static int sh7722_frqcr_set_rate(struct clk *clk, unsigned long rate,
				 int algo_id)
{
	struct frqcr_context ctx = sh7722_get_clk_context(clk->name);
	unsigned long parent_rate = clk->parent->rate;
	int div;
	unsigned long frqcr;
	int err = 0;

	/* pretty invalid */
	if (parent_rate < rate)
		return -EINVAL;

	/* look for multiplier/divisor pair */
	div = sh7722_find_divisors(parent_rate, rate);
	if (div<0)
		return div;

	/* calculate new value of clock rate */
	clk->rate = parent_rate * 2 / div;
	frqcr = ctrl_inl(FRQCR);

	/* FIXME: adjust as algo_id specifies */
	if (algo_id != NO_CHANGE) {
		int originator;
		char *algo_group_1[] = { "cpu_clk", "umem_clk", "sh_clk" };
		char *algo_group_2[] = { "sh_clk", "bus_clk" };
		char *algo_group_3[] = { "sh_clk", "sdram_clk" };
		char *algo_group_4[] = { "bus_clk", "peripheral_clk" };
		char *algo_group_5[] = { "cpu_clk", "peripheral_clk" };
		char **algo_current = NULL;
		/* 3 is the maximum number of clocks in relation */
		struct clk *ck[3];
		unsigned long values[3]; /* the same comment as above */
		int part_length = -1;
		int i;

		/*
		 * all the steps below only required if adjustion was
		 * requested
		 */
		if (algo_id == IUS_N1_N1 ||
		    algo_id == IUS_322 ||
		    algo_id == IUS_522 ||
		    algo_id == IUS_N11) {
			algo_current = algo_group_1;
			part_length = 3;
		}
		if (algo_id == SB_N1) {
			algo_current = algo_group_2;
			part_length = 2;
		}
		if (algo_id == SB3_N1 ||
		    algo_id == SB3_32 ||
		    algo_id == SB3_43 ||
		    algo_id == SB3_54) {
			algo_current = algo_group_3;
			part_length = 2;
		}
		if (algo_id == BP_N1) {
			algo_current = algo_group_4;
			part_length = 2;
		}
		if (algo_id == IP_N1) {
			algo_current = algo_group_5;
			part_length = 2;
		}
		if (!algo_current)
			goto incorrect_algo_id;

		originator = -1;
		for (i = 0; i < part_length; i ++ ) {
			if (originator >= 0 && !strcmp(clk->name,
						       algo_current[i]))
				originator = i;
			ck[i] = clk_get(NULL, algo_current[i]);
			values[i] = clk_get_rate(ck[i]);
		}

		if (originator >= 0)
			adjust_clocks(originator, adjust_algos[algo_id],
				      values, part_length);

		for (i = 0; i < part_length; i ++ ) {
			struct frqcr_context part_ctx;
			int part_div;

			if (likely(!err)) {
				part_div = sh7722_find_divisors(parent_rate,
								rate);
				if (part_div > 0) {
					part_ctx = sh7722_get_clk_context(
								ck[i]->name);
					frqcr &= ~(part_ctx.mask <<
						   part_ctx.shift);
					frqcr |= part_div << part_ctx.shift;
				} else
					err = part_div;
			}

			ck[i]->ops->recalc(ck[i]);
			clk_put(ck[i]);
		}
	}

	/* was there any error during recalculation ? If so, bail out.. */
	if (unlikely(err!=0))
		goto out_err;

	/* clear FRQCR bits */
	frqcr &= ~(ctx.mask << ctx.shift);
	frqcr |= div << ctx.shift;

	/* ...and perform actual change */
	ctrl_outl(frqcr, FRQCR);
	return 0;

incorrect_algo_id:
	return -EINVAL;
out_err:
	return err;
}

static struct clk_ops sh7722_frqcr_clk_ops = {
	.recalc = sh7722_frqcr_recalc,
	.set_rate = sh7722_frqcr_set_rate,
};

/*
 * clock ops methods for SIU A/B and IrDA clock
 *
 */
static int sh7722_siu_which(struct clk *clk)
{
	if (!strcmp(clk->name, "siu_a_clk"))
		return 0;
	if (!strcmp(clk->name, "siu_b_clk"))
		return 1;
	if (!strcmp(clk->name, "irda_clk"))
		return 2;
	return -EINVAL;
}

static unsigned long sh7722_siu_regs[] = {
	[0] = SCLKACR,
	[1] = SCLKBCR,
	[2] = IrDACLKCR,
};

static int sh7722_siu_start_stop(struct clk *clk, int enable)
{
	int siu = sh7722_siu_which(clk);
	unsigned long r;

	if (siu < 0)
		return siu;
	BUG_ON(siu > 2);
	r = ctrl_inl(sh7722_siu_regs[siu]);
	if (enable)
		ctrl_outl(r & ~(1 << 8), sh7722_siu_regs[siu]);
	else
		ctrl_outl(r | (1 << 8), sh7722_siu_regs[siu]);
	return 0;
}

static void sh7722_siu_enable(struct clk *clk)
{
	sh7722_siu_start_stop(clk, 1);
}

static void sh7722_siu_disable(struct clk *clk)
{
	sh7722_siu_start_stop(clk, 0);
}

static void sh7722_video_enable(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	ctrl_outl( r & ~(1<<8), VCLKCR);
}

static void sh7722_video_disable(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	ctrl_outl( r | (1<<8), VCLKCR);
}

static int sh7722_video_set_rate(struct clk *clk, unsigned long rate,
				 int algo_id)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	r &= ~0x3F;
	r |= ((clk->parent->rate / rate - 1) & 0x3F);
	ctrl_outl(r, VCLKCR);
	return 0;
}

static void sh7722_video_recalc(struct clk *clk)
{
	unsigned long r;

	r = ctrl_inl(VCLKCR);
	clk->rate = clk->parent->rate / ((r & 0x3F) + 1);
}

static int sh7722_siu_set_rate(struct clk *clk, unsigned long rate, int algo_id)
{
	int siu = sh7722_siu_which(clk);
	unsigned long r;
	int div;

	if (siu < 0)
		return siu;
	BUG_ON(siu > 2);
	r = ctrl_inl(sh7722_siu_regs[siu]);
	div = sh7722_find_divisors(clk->parent->rate, rate);
	if (div < 0)
		return div;
	r = (r & ~0xF) | div;
	ctrl_outl(r, sh7722_siu_regs[siu]);
	return 0;
}

static void sh7722_siu_recalc(struct clk *clk)
{
	int siu = sh7722_siu_which(clk);
	unsigned long r;

	if (siu < 0)
		return /* siu */ ;
	BUG_ON(siu > 1);
	r = ctrl_inl(sh7722_siu_regs[siu]);
	clk->rate = clk->parent->rate * 2 / divisors2[r & 0xF];
}

static struct clk_ops sh7722_siu_clk_ops = {
	.recalc = sh7722_siu_recalc,
	.set_rate = sh7722_siu_set_rate,
	.enable = sh7722_siu_enable,
	.disable = sh7722_siu_disable,
};

static struct clk_ops sh7722_video_clk_ops = {
	.recalc = sh7722_video_recalc,
	.set_rate = sh7722_video_set_rate,
	.enable = sh7722_video_enable,
	.disable = sh7722_video_disable,
};
/*
 * and at last, clock definitions themselves
 */
static struct clk sh7722_umem_clock = {
	.name = "umem_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

static struct clk sh7722_sh_clock = {
	.name = "sh_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

static struct clk sh7722_peripheral_clock = {
	.name = "peripheral_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

static struct clk sh7722_sdram_clock = {
	.name = "sdram_clk",
	.ops = &sh7722_frqcr_clk_ops,
};

/*
 * these three clocks - SIU A, SIU B, IrDA - share the same clk_ops
 * methods of clk_ops determine which register they should access by
 * examining clk->name field
 */
static struct clk sh7722_siu_a_clock = {
	.name = "siu_a_clk",
	.ops = &sh7722_siu_clk_ops,
};

static struct clk sh7722_siu_b_clock = {
	.name = "siu_b_clk",
	.ops = &sh7722_siu_clk_ops,
};

static struct clk sh7722_irda_clock = {
	.name = "irda_clk",
	.ops = &sh7722_siu_clk_ops,
};

static struct clk sh7722_video_clock = {
	.name = "video_clk",
	.ops = &sh7722_video_clk_ops,
};

static struct clk *sh7722_clocks[] = {
	&sh7722_umem_clock,
	&sh7722_sh_clock,
	&sh7722_peripheral_clock,
	&sh7722_sdram_clock,
	&sh7722_siu_a_clock,
	&sh7722_siu_b_clock,
	&sh7722_irda_clock,
	&sh7722_video_clock,
};

/*
 * init in order: master, module, bus, cpu
 */
struct clk_ops *onchip_ops[] = {
	&sh7722_master_clk_ops,
	&sh7722_frqcr_clk_ops,
	&sh7722_frqcr_clk_ops,
	&sh7722_frqcr_clk_ops,
};

void __init
arch_init_clk_ops(struct clk_ops **ops, int type)
{
	BUG_ON(type < 0 || type > ARRAY_SIZE(onchip_ops));
	*ops = onchip_ops[type];
}

int __init sh7722_clock_init(void)
{
	struct clk *master;
	int i;

	master = clk_get(NULL, "master_clk");
	for (i = 0; i < ARRAY_SIZE(sh7722_clocks); i++) {
		pr_debug( "Registering clock '%s'\n", sh7722_clocks[i]->name);
		sh7722_clocks[i]->parent = master;
		clk_register(sh7722_clocks[i]);
	}
	clk_put(master);
	return 0;
}
arch_initcall(sh7722_clock_init);
