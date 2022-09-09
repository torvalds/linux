/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP4 Clock Management (CM) definitions
 *
 * Copyright (C) 2007-2011 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * OMAP4 has two separate CM blocks, CM1 and CM2.  This file contains
 * macros and function prototypes that are applicable to both.
 */
#ifndef __ARCH_ASM_MACH_OMAP2_CM44XX_H
#define __ARCH_ASM_MACH_OMAP2_CM44XX_H


#include "prcm-common.h"
#include "cm.h"

#define OMAP4_CM_CLKSTCTRL				0x0000
#define OMAP4_CM_STATICDEP				0x0004

int omap4_cm_init(const struct omap_prcm_init_data *data);

#endif
