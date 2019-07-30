/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared macros and function prototypes for the HDQ1W/1-wire IP block
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 */
#ifndef ARCH_ARM_MACH_OMAP2_HDQ1W_H
#define ARCH_ARM_MACH_OMAP2_HDQ1W_H

#include "omap_hwmod.h"

/*
 * XXX A future cleanup patch should modify
 * drivers/w1/masters/omap_hdq.c to use these macros
 */
#define HDQ_CTRL_STATUS_OFFSET			0x0c
#define HDQ_CTRL_STATUS_CLOCKENABLE_SHIFT	5


extern int omap_hdq1w_reset(struct omap_hwmod *oh);

#endif
