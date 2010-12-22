/*
 * OMAP2+ powerdomain prototypes
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARCH_ARM_MACH_OMAP2_POWERDOMAINS
#define ARCH_ARM_MACH_OMAP2_POWERDOMAINS

#include <plat/powerdomain.h>

extern struct pwrdm_ops omap2_pwrdm_operations;
extern struct pwrdm_ops omap3_pwrdm_operations;
extern struct pwrdm_ops omap4_pwrdm_operations;

#endif /* ARCH_ARM_MACH_OMAP2_POWERDOMAINS */
