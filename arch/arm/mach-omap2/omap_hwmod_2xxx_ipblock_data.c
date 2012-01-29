/*
 * omap_hwmod_2xxx_ipblock_data.c - common IP block data for OMAP2xxx
 *
 * Copyright (C) 2011 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <plat/omap_hwmod.h>
#include <plat/serial.h>
#include <plat/dma.h>
#include <plat/dmtimer.h>
#include <plat/mcspi.h>

#include <mach/irqs.h>

#include "omap_hwmod_common_data.h"
#include "wd_timer.h"

struct omap_hwmod_irq_info omap2xxx_timer12_mpu_irqs[] = {
	{ .irq = 48, },
	{ .irq = -1 }
};

struct omap_hwmod_dma_info omap2xxx_dss_sdma_chs[] = {
	{ .name = "dispc", .dma_req = 5 },
	{ .dma_req = -1 }
};

/*
 * 'dispc' class
 * display controller
 */

static struct omap_hwmod_class_sysconfig omap2_dispc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2_dispc_hwmod_class = {
	.name	= "dispc",
	.sysc	= &omap2_dispc_sysc,
};

/* OMAP2xxx Timer Common */
static struct omap_hwmod_class_sysconfig omap2xxx_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_timer_hwmod_class = {
	.name	= "timer",
	.sysc	= &omap2xxx_timer_sysc,
	.rev	= OMAP_TIMER_IP_VERSION_1,
};

/*
 * 'wd_timer' class
 * 32-bit watchdog upward counter that generates a pulse on the reset pin on
 * overflow condition
 */

static struct omap_hwmod_class_sysconfig omap2xxx_wd_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_EMUFREE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_wd_timer_hwmod_class = {
	.name		= "wd_timer",
	.sysc		= &omap2xxx_wd_timer_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable
};

/*
 * 'gpio' class
 * general purpose io module
 */
static struct omap_hwmod_class_sysconfig omap2xxx_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_gpio_hwmod_class = {
	.name = "gpio",
	.sysc = &omap2xxx_gpio_sysc,
	.rev = 0,
};

/* system dma */
static struct omap_hwmod_class_sysconfig omap2xxx_dma_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x002c,
	.syss_offs	= 0x0028,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_EMUFREE |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_dma_hwmod_class = {
	.name	= "dma",
	.sysc	= &omap2xxx_dma_sysc,
};

/*
 * 'mailbox' class
 * mailbox module allowing communication between the on-chip processors
 * using a queued mailbox-interrupt mechanism.
 */

static struct omap_hwmod_class_sysconfig omap2xxx_mailbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_mailbox_hwmod_class = {
	.name	= "mailbox",
	.sysc	= &omap2xxx_mailbox_sysc,
};

/*
 * 'mcspi' class
 * multichannel serial port interface (mcspi) / master/slave synchronous serial
 * bus
 */

static struct omap_hwmod_class_sysconfig omap2xxx_mcspi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_mcspi_class = {
	.name	= "mcspi",
	.sysc	= &omap2xxx_mcspi_sysc,
	.rev	= OMAP2_MCSPI_REV,
};
