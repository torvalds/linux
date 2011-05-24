/*
 * OMAP4 Clock Management (CM) definitions
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

/* Function prototypes */
# ifndef __ASSEMBLER__

extern int omap4_cm_wait_module_ready(void __iomem *clkctrl_reg);

# endif
#endif
