/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2004 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * S3C2440/S3C2412 Signal Drive Strength Control
 */


#ifndef __ASM_ARCH_REGS_DSC_H
#define __ASM_ARCH_REGS_DSC_H __FILE__

/* S3C2412 */
#define S3C2412_DSC0	   S3C2410_GPIOREG(0xdc)
#define S3C2412_DSC1	   S3C2410_GPIOREG(0xe0)

/* S3C2440 */
#define S3C2440_DSC0	   S3C2410_GPIOREG(0xc4)
#define S3C2440_DSC1	   S3C2410_GPIOREG(0xc8)

#endif	/* __ASM_ARCH_REGS_DSC_H */

