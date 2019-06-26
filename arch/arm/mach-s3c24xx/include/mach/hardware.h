/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2003 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - hardware
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef __ASSEMBLY__

extern unsigned int s3c2410_modify_misccr(unsigned int clr, unsigned int chg);

#endif /* __ASSEMBLY__ */

#include <linux/sizes.h>
#include <mach/map.h>

#endif /* __ASM_ARCH_HARDWARE_H */
