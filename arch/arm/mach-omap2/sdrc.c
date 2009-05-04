/*
 * SMS/SDRC (SDRAM controller) common code for OMAP2/3
 *
 * Copyright (C) 2005, 2008 Texas Instruments Inc.
 * Copyright (C) 2005, 2008 Nokia Corporation
 *
 * Tony Lindgren <tony@atomide.com>
 * Paul Walmsley
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <mach/common.h>
#include <mach/clock.h>
#include <mach/sram.h>

#include "prm.h"

#include <mach/sdrc.h>
#include "sdrc.h"

static struct omap_sdrc_params *sdrc_init_params;

void __iomem *omap2_sdrc_base;
void __iomem *omap2_sms_base;


/**
 * omap2_sdrc_get_params - return SDRC register values for a given clock rate
 * @r: SDRC clock rate (in Hz)
 *
 * Return pre-calculated values for the SDRC_ACTIM_CTRLA,
 * SDRC_ACTIM_CTRLB, SDRC_RFR_CTRL, and SDRC_MR registers, for a given
 * SDRC clock rate 'r'.  These parameters control various timing
 * delays in the SDRAM controller that are expressed in terms of the
 * number of SDRC clock cycles to wait; hence the clock rate
 * dependency. Note that sdrc_init_params must be sorted rate
 * descending.  Also assumes that both chip-selects use the same
 * timing parameters.  Returns a struct omap_sdrc_params * upon
 * success, or NULL upon failure.
 */
struct omap_sdrc_params *omap2_sdrc_get_params(unsigned long r)
{
	struct omap_sdrc_params *sp;

	sp = sdrc_init_params;

	while (sp->rate != r)
		sp++;

	if (!sp->rate)
		return NULL;

	return sp;
}


void __init omap2_set_globals_sdrc(struct omap_globals *omap2_globals)
{
	omap2_sdrc_base = omap2_globals->sdrc;
	omap2_sms_base = omap2_globals->sms;
}

/* turn on smart idle modes for SDRAM scheduler and controller */
void __init omap2_sdrc_init(struct omap_sdrc_params *sp)
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

	sdrc_init_params = sp;
}
