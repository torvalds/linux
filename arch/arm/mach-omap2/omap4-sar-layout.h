/*
 * omap4-sar-layout.h: OMAP4 SAR RAM layout header file
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OMAP_ARCH_OMAP4_SAR_LAYOUT_H
#define OMAP_ARCH_OMAP4_SAR_LAYOUT_H

/*
 * SAR BANK offsets from base address OMAP44XX_SAR_RAM_BASE
 */
#define SAR_BANK1_OFFSET		0x0000
#define SAR_BANK2_OFFSET		0x1000
#define SAR_BANK3_OFFSET		0x2000
#define SAR_BANK4_OFFSET		0x3000

/* Scratch pad memory offsets from SAR_BANK1 */
#define SCU_OFFSET0				0xd00
#define SCU_OFFSET1				0xd04
#define OMAP_TYPE_OFFSET			0xd10

/* CPUx Wakeup Non-Secure Physical Address offsets in SAR_BANK3 */
#define CPU0_WAKEUP_NS_PA_ADDR_OFFSET		0xa04
#define CPU1_WAKEUP_NS_PA_ADDR_OFFSET		0xa08

#endif
