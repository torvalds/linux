#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>

#include <mach/hardware.h>
#include <mach/platform.h>

#include "clk-icst.h"

/*
 * Implementation of the ARM RealView clock trees.
 */

static void __iomem *sys_lock;
static void __iomem *sys_vcoreg;

/**
 * realview_oscvco_get() - get ICST OSC settings for the RealView
 */
static struct icst_vco realview_oscvco_get(void)
{
	u32 val;
	struct icst_vco vco;

	val = readl(sys_vcoreg);
	vco.v = val & 0x1ff;
	vco.r = (val >> 9) & 0x7f;
	vco.s = (val >> 16) & 03;
	return vco;
}

static void realview_oscvco_set(struct icst_vco vco)
{
	u32 val;

	val = readl(sys_vcoreg) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	/* This magic unlocks the CM VCO so it can be controlled */
	writel(0xa05f, sys_lock);
	writel(val, sys_vcoreg);
	/* This locks the CM again */
	writel(0, sys_lock);
}

static const struct icst_params realview_oscvco_params = {
	.ref		= 24000000,
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

static const struct clk_icst_desc __initdata realview_icst_desc = {
	.params = &realview_oscvco_params,
	.getvco = realview_oscvco_get,
	.setvco = realview_oscvco_set,
};

/*
 * realview_clk_init() - set up the RealView clock tree
 */
void __init realview_clk_init(void __iomem *sysbase, bool is_pb1176)
{
	struct clk *clk;

	sys_lock = sysbase + REALVIEW_SYS_LOCK_OFFSET;
	if (is_pb1176)
		sys_vcoreg = sysbase + REALVIEW_SYS_OSC0_OFFSET;
	else
		sys_vcoreg = sysbase + REALVIEW_SYS_OSC4_OFFSET;


	/* APB clock dummy */
	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT, 0);
	clk_register_clkdev(clk, "apb_pclk", NULL);

	/* 24 MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk24mhz", NULL, CLK_IS_ROOT,
				24000000);
	clk_register_clkdev(clk, NULL, "dev:uart0");
	clk_register_clkdev(clk, NULL, "dev:uart1");
	clk_register_clkdev(clk, NULL, "dev:uart2");
	clk_register_clkdev(clk, NULL, "fpga:kmi0");
	clk_register_clkdev(clk, NULL, "fpga:kmi1");
	clk_register_clkdev(clk, NULL, "fpga:mmc0");
	clk_register_clkdev(clk, NULL, "dev:ssp0");
	if (is_pb1176) {
		/*
		 * UART3 is on the dev chip in PB1176
		 * UART4 only exists in PB1176
		 */
		clk_register_clkdev(clk, NULL, "dev:uart3");
		clk_register_clkdev(clk, NULL, "dev:uart4");
	} else
		clk_register_clkdev(clk, NULL, "fpga:uart3");


	/* 1 MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk1mhz", NULL, CLK_IS_ROOT,
				      1000000);
	clk_register_clkdev(clk, NULL, "sp804");

	/* ICST VCO clock */
	clk = icst_clk_register(NULL, &realview_icst_desc);
	clk_register_clkdev(clk, NULL, "dev:clcd");
	clk_register_clkdev(clk, NULL, "issp:clcd");
}
