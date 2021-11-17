// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-pxa/pxa2xx.c
 *
 * code specific to pxa2xx
 *
 * Copyright (C) 2008 Dmitry Baryshkov
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/pxa2xx-regs.h>
#include "mfp-pxa25x.h"
#include <mach/reset.h>
#include <linux/platform_data/irda-pxaficp.h>

void pxa2xx_clear_reset_status(unsigned int mask)
{
	/* RESET_STATUS_* has a 1:1 mapping with RCSR */
	RCSR = mask;
}

static unsigned long pxa2xx_mfp_fir[] = {
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,
};

static unsigned long pxa2xx_mfp_sir[] = {
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,
};

static unsigned long pxa2xx_mfp_off[] = {
	GPIO46_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO47_GPIO | MFP_LPM_DRIVE_LOW,
};

void pxa2xx_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF) {
		pxa2xx_mfp_config(pxa2xx_mfp_off, ARRAY_SIZE(pxa2xx_mfp_off));
	} else if (mode & IR_SIRMODE) {
		pxa2xx_mfp_config(pxa2xx_mfp_sir, ARRAY_SIZE(pxa2xx_mfp_sir));
	} else if (mode & IR_FIRMODE) {
		pxa2xx_mfp_config(pxa2xx_mfp_fir, ARRAY_SIZE(pxa2xx_mfp_fir));
	} else
		BUG();
}
EXPORT_SYMBOL_GPL(pxa2xx_transceiver_mode);
