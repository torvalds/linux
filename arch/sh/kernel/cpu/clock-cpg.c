#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clock.h>

static struct clk master_clk = {
	.flags		= CLK_ENABLE_ON_INIT,
	.rate		= CONFIG_SH_PCLK_FREQ,
};

static struct clk peripheral_clk = {
	.parent		= &master_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk bus_clk = {
	.parent		= &master_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk cpu_clk = {
	.parent		= &master_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

/*
 * The ordering of these clocks matters, do not change it.
 */
static struct clk *onchip_clocks[] = {
	&master_clk,
	&peripheral_clk,
	&bus_clk,
	&cpu_clk,
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("master_clk", &master_clk),
	CLKDEV_CON_ID("peripheral_clk", &peripheral_clk),
	CLKDEV_CON_ID("bus_clk", &bus_clk),
	CLKDEV_CON_ID("cpu_clk", &cpu_clk),
};

int __init __deprecated cpg_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(onchip_clocks); i++) {
		struct clk *clk = onchip_clocks[i];
		arch_init_clk_ops(&clk->ops, i);
		if (clk->ops)
			ret |= clk_register(clk);
	}

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	clk_add_alias("tmu_fck", NULL, "peripheral_clk", NULL);
	clk_add_alias("mtu2_fck", NULL, "peripheral_clk", NULL);
	clk_add_alias("cmt_fck", NULL, "peripheral_clk", NULL);
	clk_add_alias("sci_ick", NULL, "peripheral_clk", NULL);

	return ret;
}

/*
 * Placeholder for compatibility, until the lazy CPUs do this
 * on their own.
 */
int __init __weak arch_clk_init(void)
{
	return cpg_clk_init();
}
