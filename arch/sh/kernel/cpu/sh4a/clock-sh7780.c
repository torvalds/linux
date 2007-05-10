/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7780.c
 *
 * SH7780 support for the clock framework
 *
 *  Copyright (C) 2005  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <asm/io.h>

static int ifc_divisors[] = { 2, 4 };
static int bfc_divisors[] = { 1, 1, 1, 8, 12, 16, 24, 1 };
static int pfc_divisors[] = { 1, 24, 24, 1 };
static int cfc_divisors[] = { 1, 1, 4, 1, 6, 1, 1, 1 };

static void master_clk_init(struct clk *clk)
{
	clk->rate *= pfc_divisors[ctrl_inl(FRQCR) & 0x0003];
}

static struct clk_ops sh7780_master_clk_ops = {
	.init		= master_clk_init,
};

static void module_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) & 0x0003);
	clk->rate = clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops sh7780_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static void bus_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> 16) & 0x0007);
	clk->rate = clk->parent->rate / bfc_divisors[idx];
}

static struct clk_ops sh7780_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static void cpu_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> 24) & 0x0001);
	clk->rate = clk->parent->rate / ifc_divisors[idx];
}

static struct clk_ops sh7780_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh7780_clk_ops[] = {
	&sh7780_master_clk_ops,
	&sh7780_module_clk_ops,
	&sh7780_bus_clk_ops,
	&sh7780_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7780_clk_ops))
		*ops = sh7780_clk_ops[idx];
}

static void shyway_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> 20) & 0x0007);
	clk->rate = clk->parent->rate / cfc_divisors[idx];
}

static struct clk_ops sh7780_shyway_clk_ops = {
	.recalc		= shyway_clk_recalc,
};

static struct clk sh7780_shyway_clk = {
	.name		= "shyway_clk",
	.flags		= CLK_ALWAYS_ENABLED,
	.ops		= &sh7780_shyway_clk_ops,
};

/*
 * Additional SH7780-specific on-chip clocks that aren't already part of the
 * clock framework
 */
static struct clk *sh7780_onchip_clocks[] = {
	&sh7780_shyway_clk,
};

static int __init sh7780_clk_init(void)
{
	struct clk *clk = clk_get(NULL, "master_clk");
	int i;

	for (i = 0; i < ARRAY_SIZE(sh7780_onchip_clocks); i++) {
		struct clk *clkp = sh7780_onchip_clocks[i];

		clkp->parent = clk;
		clk_register(clkp);
		clk_enable(clkp);
	}

	/*
	 * Now that we have the rest of the clocks registered, we need to
	 * force the parent clock to propagate so that these clocks will
	 * automatically figure out their rate. We cheat by handing the
	 * parent clock its current rate and forcing child propagation.
	 */
	clk_set_rate(clk, clk_get_rate(clk));

	clk_put(clk);

	return 0;
}

arch_initcall(sh7780_clk_init);

