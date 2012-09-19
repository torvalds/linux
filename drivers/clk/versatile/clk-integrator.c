#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>

#include <mach/hardware.h>
#include <mach/platform.h>

#include "clk-icst.h"

/*
 * Implementation of the ARM Integrator/AP and Integrator/CP clock tree.
 * Inspired by portions of:
 * plat-versatile/clock.c and plat-versatile/include/plat/clock.h
 */
#define CM_LOCK		(__io_address(INTEGRATOR_HDR_BASE)+INTEGRATOR_HDR_LOCK_OFFSET)
#define CM_AUXOSC	(__io_address(INTEGRATOR_HDR_BASE)+0x1c)

/**
 * cp_auxvco_get() - get ICST VCO settings for the Integrator/CP
 * @vco: ICST VCO parameters to update with hardware status
 */
static struct icst_vco cp_auxvco_get(void)
{
	u32 val;
	struct icst_vco vco;

	val = readl(CM_AUXOSC);
	vco.v = val & 0x1ff;
	vco.r = (val >> 9) & 0x7f;
	vco.s = (val >> 16) & 03;
	return vco;
}

/**
 * cp_auxvco_set() - commit changes to Integrator/CP ICST VCO
 * @vco: ICST VCO parameters to commit
 */
static void cp_auxvco_set(struct icst_vco vco)
{
	u32 val;

	val = readl(CM_AUXOSC) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	/* This magic unlocks the CM VCO so it can be controlled */
	writel(0xa05f, CM_LOCK);
	writel(val, CM_AUXOSC);
	/* This locks the CM again */
	writel(0, CM_LOCK);
}

static const struct icst_params cp_auxvco_params = {
	.ref		= 24000000,
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min 	= 8,
	.vd_max 	= 263,
	.rd_min 	= 3,
	.rd_max 	= 65,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct clk_icst_desc __initdata cp_icst_desc = {
	.params = &cp_auxvco_params,
	.getvco = cp_auxvco_get,
	.setvco = cp_auxvco_set,
};

/*
 * integrator_clk_init() - set up the integrator clock tree
 * @is_cp: pass true if it's the Integrator/CP else AP is assumed
 */
void __init integrator_clk_init(bool is_cp)
{
	struct clk *clk;

	/* APB clock dummy */
	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT, 0);
	clk_register_clkdev(clk, "apb_pclk", NULL);

	/* UART reference clock */
	clk = clk_register_fixed_rate(NULL, "uartclk", NULL, CLK_IS_ROOT,
				14745600);
	clk_register_clkdev(clk, NULL, "uart0");
	clk_register_clkdev(clk, NULL, "uart1");
	if (is_cp)
		clk_register_clkdev(clk, NULL, "mmci");

	/* 24 MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk24mhz", NULL, CLK_IS_ROOT,
				24000000);
	clk_register_clkdev(clk, NULL, "kmi0");
	clk_register_clkdev(clk, NULL, "kmi1");
	if (!is_cp)
		clk_register_clkdev(clk, NULL, "ap_timer");

	if (!is_cp)
		return;

	/* 1 MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk1mhz", NULL, CLK_IS_ROOT,
				1000000);
	clk_register_clkdev(clk, NULL, "sp804");

	/* ICST VCO clock used on the Integrator/CP CLCD */
	clk = icst_clk_register(NULL, &cp_icst_desc);
	clk_register_clkdev(clk, NULL, "clcd");
}
