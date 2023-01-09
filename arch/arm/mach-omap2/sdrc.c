// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMS/SDRC (SDRAM controller) common code for OMAP2/3
 *
 * Copyright (C) 2005, 2008 Texas Instruments Inc.
 * Copyright (C) 2005, 2008 Nokia Corporation
 *
 * Tony Lindgren <tony@atomide.com>
 * Paul Walmsley
 * Richard Woodruff <r-woodruff2@ti.com>
 */
#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "common.h"
#include "clock.h"
#include "sdrc.h"

static struct omap_sdrc_params *sdrc_init_params_cs0, *sdrc_init_params_cs1;

void __iomem *omap2_sdrc_base;
void __iomem *omap2_sms_base;

struct omap2_sms_regs {
	u32	sms_sysconfig;
};

static struct omap2_sms_regs sms_context;

/* SDRC_POWER register bits */
#define SDRC_POWER_EXTCLKDIS_SHIFT		3
#define SDRC_POWER_PWDENA_SHIFT			2
#define SDRC_POWER_PAGEPOLICY_SHIFT		0

/**
 * omap2_sms_save_context - Save SMS registers
 *
 * Save SMS registers that need to be restored after off mode.
 */
static void omap2_sms_save_context(void)
{
	sms_context.sms_sysconfig = sms_read_reg(SMS_SYSCONFIG);
}

/**
 * omap2_sms_restore_context - Restore SMS registers
 *
 * Restore SMS registers that need to be Restored after off mode.
 */
void omap2_sms_restore_context(void)
{
	sms_write_reg(sms_context.sms_sysconfig, SMS_SYSCONFIG);
}

void __init omap2_set_globals_sdrc(void __iomem *sdrc, void __iomem *sms)
{
	omap2_sdrc_base = sdrc;
	omap2_sms_base = sms;
}

/**
 * omap2_sdrc_init - initialize SMS, SDRC devices on boot
 * @sdrc_cs[01]: pointers to a null-terminated list of struct omap_sdrc_params
 *  Support for 2 chip selects timings
 *
 * Turn on smart idle modes for SDRAM scheduler and controller.
 * Program a known-good configuration for the SDRC to deal with buggy
 * bootloaders.
 */
void __init omap2_sdrc_init(struct omap_sdrc_params *sdrc_cs0,
			    struct omap_sdrc_params *sdrc_cs1)
{
	u32 l;

	l = sms_read_reg(SMS_SYSCONFIG);
	l &= ~(0x3 << 3);
	l |= (0x2 << 3);
	sms_write_reg(l, SMS_SYSCONFIG);

	l = sdrc_read_reg(SDRC_SYSCONFIG);
	l &= ~(0x3 << 3);
	l |= (0x2 << 3);
	sdrc_write_reg(l, SDRC_SYSCONFIG);

	sdrc_init_params_cs0 = sdrc_cs0;
	sdrc_init_params_cs1 = sdrc_cs1;

	/* XXX Enable SRFRONIDLEREQ here also? */
	/*
	 * PWDENA should not be set due to 34xx erratum 1.150 - PWDENA
	 * can cause random memory corruption
	 */
	l = (1 << SDRC_POWER_EXTCLKDIS_SHIFT) |
		(1 << SDRC_POWER_PAGEPOLICY_SHIFT);
	sdrc_write_reg(l, SDRC_POWER);
	omap2_sms_save_context();
}
