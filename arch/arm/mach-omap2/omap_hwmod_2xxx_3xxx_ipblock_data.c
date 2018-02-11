/*
 * omap_hwmod_2xxx_3xxx_ipblock_data.c - common IP block data for OMAP2/3
 *
 * Copyright (C) 2011 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dmaengine.h>
#include <linux/omap-dma.h>

#include "omap_hwmod.h"
#include "hdq1w.h"

#include "omap_hwmod_common_data.h"

/* UART */

static struct omap_hwmod_class_sysconfig omap2_uart_sysc = {
	.rev_offs	= 0x50,
	.sysc_offs	= 0x54,
	.syss_offs	= 0x58,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2_uart_class = {
	.name	= "uart",
	.sysc	= &omap2_uart_sysc,
};

/*
 * 'venc' class
 * video encoder
 */

struct omap_hwmod_class omap2_venc_hwmod_class = {
	.name = "venc",
};

/*
 * omap_hwmod class data
 */

struct omap_hwmod_class l3_hwmod_class = {
	.name = "l3",
};

struct omap_hwmod_class l4_hwmod_class = {
	.name = "l4",
};

struct omap_hwmod_class mpu_hwmod_class = {
	.name = "mpu",
};

struct omap_hwmod_class iva_hwmod_class = {
	.name = "iva",
};

struct omap_hwmod_class_sysconfig omap2_hdq1w_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x14,
	.syss_offs	= 0x18,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2_hdq1w_class = {
	.name	= "hdq1w",
	.sysc	= &omap2_hdq1w_sysc,
	.reset	= &omap_hdq1w_reset,
};
