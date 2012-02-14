/*
 * arch/arm/plat-omap/include/mach/mcbsp.h
 *
 * Defines for Multi-Channel Buffered Serial Port
 *
 * Copyright (C) 2002 RidgeRun, Inc.
 * Author: Steve Johnson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef __ASM_ARCH_OMAP_MCBSP_H
#define __ASM_ARCH_OMAP_MCBSP_H

#include <linux/spinlock.h>
#include <linux/clk.h>

/* macro for building platform_device for McBSP ports */
#define OMAP_MCBSP_PLATFORM_DEVICE(port_nr)		\
static struct platform_device omap_mcbsp##port_nr = {	\
	.name	= "omap-mcbsp-dai",			\
	.id	= port_nr - 1,			\
}

#define MCBSP_CONFIG_TYPE2	0x2
#define MCBSP_CONFIG_TYPE3	0x3
#define MCBSP_CONFIG_TYPE4	0x4

/* CLKR signal muxing options */
#define CLKR_SRC_CLKR		0
#define CLKR_SRC_CLKX		1

/* FSR signal muxing options */
#define FSR_SRC_FSR		0
#define FSR_SRC_FSX		1

/* McBSP functional clock sources */
#define MCBSP_CLKS_PRCM_SRC	0
#define MCBSP_CLKS_PAD_SRC	1

/* Platform specific configuration */
struct omap_mcbsp_ops {
	void (*request)(unsigned int);
	void (*free)(unsigned int);
};

struct omap_mcbsp_platform_data {
	struct omap_mcbsp_ops *ops;
	u16 buffer_size;
	u8 reg_size;
	u8 reg_step;

	/* McBSP platform and instance specific features */
	bool has_wakeup; /* Wakeup capability */
	bool has_ccr; /* Transceiver has configuration control registers */
	int (*enable_st_clock)(unsigned int, bool);
	int (*set_clk_src)(struct device *dev, struct clk *clk, const char *src);
	int (*mux_signal)(struct device *dev, const char *signal, const char *src);
};

/**
 * omap_mcbsp_dev_attr - OMAP McBSP device attributes for omap_hwmod
 * @sidetone: name of the sidetone device
 */
struct omap_mcbsp_dev_attr {
	const char *sidetone;
};

#endif
