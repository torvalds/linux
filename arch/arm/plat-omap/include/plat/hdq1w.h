/*
 * Shared macros and function prototypes for the HDQ1W/1-wire IP block
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef ARCH_ARM_MACH_OMAP2_HDQ1W_H
#define ARCH_ARM_MACH_OMAP2_HDQ1W_H

#include <plat/omap_hwmod.h>

/*
 * XXX A future cleanup patch should modify
 * drivers/w1/masters/omap_hdq.c to use these macros
 */
#define HDQ_CTRL_STATUS_OFFSET			0x0c
#define HDQ_CTRL_STATUS_CLOCKENABLE_SHIFT	5


extern int omap_hdq1w_reset(struct omap_hwmod *oh);

#endif
