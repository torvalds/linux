/*
 *  linux/arch/arm/mach-omap2/clock.c
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2010 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/bootmem.h>
#include <asm/cpu.h>

#include <trace/events/power.h>

#include "soc.h"
#include "clockdomain.h"
#include "clock.h"
#include "cm.h"
#include "cm2xxx.h"
#include "cm3xxx.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"
#include "common.h"

u16 cpu_mask;

/* DPLL valid Fint frequency band limits - from 34xx TRM Section 4.7.6.2 */
#define OMAP3430_DPLL_FINT_BAND1_MIN	750000
#define OMAP3430_DPLL_FINT_BAND1_MAX	2100000
#define OMAP3430_DPLL_FINT_BAND2_MIN	7500000
#define OMAP3430_DPLL_FINT_BAND2_MAX	21000000

/*
 * DPLL valid Fint frequency range for OMAP36xx and OMAP4xxx.
 * From device data manual section 4.3 "DPLL and DLL Specifications".
 */
#define OMAP3PLUS_DPLL_FINT_MIN		32000
#define OMAP3PLUS_DPLL_FINT_MAX		52000000

struct clk_iomap {
	struct regmap *regmap;
	void __iomem *mem;
};

static struct clk_iomap *clk_memmaps[CLK_MAX_MEMMAPS];

static void clk_memmap_writel(u32 val, void __iomem *reg)
{
	struct clk_omap_reg *r = (struct clk_omap_reg *)&reg;
	struct clk_iomap *io = clk_memmaps[r->index];

	if (io->regmap)
		regmap_write(io->regmap, r->offset, val);
	else
		writel_relaxed(val, io->mem + r->offset);
}

static u32 clk_memmap_readl(void __iomem *reg)
{
	u32 val;
	struct clk_omap_reg *r = (struct clk_omap_reg *)&reg;
	struct clk_iomap *io = clk_memmaps[r->index];

	if (io->regmap)
		regmap_read(io->regmap, r->offset, &val);
	else
		val = readl_relaxed(io->mem + r->offset);

	return val;
}

static struct ti_clk_ll_ops omap_clk_ll_ops = {
	.clk_readl = clk_memmap_readl,
	.clk_writel = clk_memmap_writel,
	.clkdm_clk_enable = clkdm_clk_enable,
	.clkdm_clk_disable = clkdm_clk_disable,
	.cm_wait_module_ready = omap_cm_wait_module_ready,
	.cm_split_idlest_reg = cm_split_idlest_reg,
};

/**
 * omap2_clk_setup_ll_ops - setup clock driver low-level ops
 *
 * Sets up clock driver low-level platform ops. These are needed
 * for register accesses and various other misc platform operations.
 * Returns 0 on success, -EBUSY if low level ops have been registered
 * already.
 */
int __init omap2_clk_setup_ll_ops(void)
{
	return ti_clk_setup_ll_ops(&omap_clk_ll_ops);
}

/**
 * omap2_clk_provider_init - initialize a clock provider
 * @match_table: DT device table to match for devices to init
 * @np: device node pointer for the this clock provider
 * @index: index for the clock provider
 + @syscon: syscon regmap pointer
 * @mem: iomem pointer for the clock provider memory area, only used if
 *	 syscon is not provided
 *
 * Initializes a clock provider module (CM/PRM etc.), registering
 * the memory mapping at specified index and initializing the
 * low level driver infrastructure. Returns 0 in success.
 */
int __init omap2_clk_provider_init(struct device_node *np, int index,
				   struct regmap *syscon, void __iomem *mem)
{
	struct clk_iomap *io;

	io = kzalloc(sizeof(*io), GFP_KERNEL);

	io->regmap = syscon;
	io->mem = mem;

	clk_memmaps[index] = io;

	ti_dt_clk_init_provider(np, index);

	return 0;
}

/**
 * omap2_clk_legacy_provider_init - initialize a legacy clock provider
 * @index: index for the clock provider
 * @mem: iomem pointer for the clock provider memory area
 *
 * Initializes a legacy clock provider memory mapping.
 */
void __init omap2_clk_legacy_provider_init(int index, void __iomem *mem)
{
	struct clk_iomap *io;

	io = memblock_virt_alloc(sizeof(*io), 0);

	io->mem = mem;

	clk_memmaps[index] = io;
}

/*
 * OMAP2+ specific clock functions
 */

/* Private functions */

/* Public functions */

/**
 * omap2_init_clk_clkdm - look up a clockdomain name, store pointer in clk
 * @clk: OMAP clock struct ptr to use
 *
 * Convert a clockdomain name stored in a struct clk 'clk' into a
 * clockdomain pointer, and save it into the struct clk.  Intended to be
 * called during clk_register().  No return value.
 */
void omap2_init_clk_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct clockdomain *clkdm;
	const char *clk_name;

	if (!clk->clkdm_name)
		return;

	clk_name = __clk_get_name(hw->clk);

	clkdm = clkdm_lookup(clk->clkdm_name);
	if (clkdm) {
		pr_debug("clock: associated clk %s to clkdm %s\n",
			 clk_name, clk->clkdm_name);
		clk->clkdm = clkdm;
	} else {
		pr_debug("clock: could not associate clk %s to clkdm %s\n",
			 clk_name, clk->clkdm_name);
	}
}

static int __initdata mpurate;

/*
 * By default we use the rate set by the bootloader.
 * You can override this with mpurate= cmdline option.
 */
static int __init omap_clk_setup(char *str)
{
	get_option(&str, &mpurate);

	if (!mpurate)
		return 1;

	if (mpurate < 1000)
		mpurate *= 1000000;

	return 1;
}
__setup("mpurate=", omap_clk_setup);

/**
 * omap2_clk_print_new_rates - print summary of current clock tree rates
 * @hfclkin_ck_name: clk name for the off-chip HF oscillator
 * @core_ck_name: clk name for the on-chip CORE_CLK
 * @mpu_ck_name: clk name for the ARM MPU clock
 *
 * Prints a short message to the console with the HFCLKIN oscillator
 * rate, the rate of the CORE clock, and the rate of the ARM MPU clock.
 * Called by the boot-time MPU rate switching code.   XXX This is intended
 * to be handled by the OPP layer code in the near future and should be
 * removed from the clock code.  No return value.
 */
void __init omap2_clk_print_new_rates(const char *hfclkin_ck_name,
				      const char *core_ck_name,
				      const char *mpu_ck_name)
{
	struct clk *hfclkin_ck, *core_ck, *mpu_ck;
	unsigned long hfclkin_rate;

	mpu_ck = clk_get(NULL, mpu_ck_name);
	if (WARN(IS_ERR(mpu_ck), "clock: failed to get %s.\n", mpu_ck_name))
		return;

	core_ck = clk_get(NULL, core_ck_name);
	if (WARN(IS_ERR(core_ck), "clock: failed to get %s.\n", core_ck_name))
		return;

	hfclkin_ck = clk_get(NULL, hfclkin_ck_name);
	if (WARN(IS_ERR(hfclkin_ck), "Failed to get %s.\n", hfclkin_ck_name))
		return;

	hfclkin_rate = clk_get_rate(hfclkin_ck);

	pr_info("Switched to new clocking rate (Crystal/Core/MPU): %ld.%01ld/%ld/%ld MHz\n",
		(hfclkin_rate / 1000000), ((hfclkin_rate / 100000) % 10),
		(clk_get_rate(core_ck) / 1000000),
		(clk_get_rate(mpu_ck) / 1000000));
}

/**
 * ti_clk_init_features - init clock features struct for the SoC
 *
 * Initializes the clock features struct based on the SoC type.
 */
void __init ti_clk_init_features(void)
{
	struct ti_clk_features features = { 0 };
	/* Fint setup for DPLLs */
	if (cpu_is_omap3430()) {
		features.fint_min = OMAP3430_DPLL_FINT_BAND1_MIN;
		features.fint_max = OMAP3430_DPLL_FINT_BAND2_MAX;
		features.fint_band1_max = OMAP3430_DPLL_FINT_BAND1_MAX;
		features.fint_band2_min = OMAP3430_DPLL_FINT_BAND2_MIN;
	} else {
		features.fint_min = OMAP3PLUS_DPLL_FINT_MIN;
		features.fint_max = OMAP3PLUS_DPLL_FINT_MAX;
	}

	/* Bypass value setup for DPLLs */
	if (cpu_is_omap24xx()) {
		features.dpll_bypass_vals |=
			(1 << OMAP2XXX_EN_DPLL_LPBYPASS) |
			(1 << OMAP2XXX_EN_DPLL_FRBYPASS);
	} else if (cpu_is_omap34xx()) {
		features.dpll_bypass_vals |=
			(1 << OMAP3XXX_EN_DPLL_LPBYPASS) |
			(1 << OMAP3XXX_EN_DPLL_FRBYPASS);
	} else if (soc_is_am33xx() || cpu_is_omap44xx() || soc_is_am43xx() ||
		   soc_is_omap54xx() || soc_is_dra7xx()) {
		features.dpll_bypass_vals |=
			(1 << OMAP4XXX_EN_DPLL_LPBYPASS) |
			(1 << OMAP4XXX_EN_DPLL_FRBYPASS) |
			(1 << OMAP4XXX_EN_DPLL_MNBYPASS);
	}

	/* Jitter correction only available on OMAP343X */
	if (cpu_is_omap343x())
		features.flags |= TI_CLK_DPLL_HAS_FREQSEL;

	/* Idlest value for interface clocks.
	 * 24xx uses 0 to indicate not ready, and 1 to indicate ready.
	 * 34xx reverses this, just to keep us on our toes
	 * AM35xx uses both, depending on the module.
	 */
	if (cpu_is_omap24xx())
		features.cm_idlest_val = OMAP24XX_CM_IDLEST_VAL;
	else if (cpu_is_omap34xx())
		features.cm_idlest_val = OMAP34XX_CM_IDLEST_VAL;

	/* On OMAP3430 ES1.0, DPLL4 can't be re-programmed */
	if (omap_rev() == OMAP3430_REV_ES1_0)
		features.flags |= TI_CLK_DPLL4_DENY_REPROGRAM;

	ti_clk_setup_features(&features);
}
