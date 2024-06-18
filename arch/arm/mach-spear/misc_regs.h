/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Miscellaneous registers definitions for SPEAr3xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 */

#ifndef __MACH_MISC_REGS_H
#define __MACH_MISC_REGS_H

#include "spear.h"

#define MISC_BASE		(VA_SPEAR_ICM3_MISC_REG_BASE)
#define DMA_CHN_CFG		(MISC_BASE + 0x0A0)

#endif /* __MACH_MISC_REGS_H */
