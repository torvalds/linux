/*
 * Clock domain register offsets for TI81XX.
 *
 * Copyright (C) 2010 Texas Instruments, Inc. - http://www.ti.com/
 * Copyright (C) 2013 SKTB SKiT, http://www.skitlab.ru/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CM_TI81XX_H
#define __ARCH_ARM_MACH_OMAP2_CM_TI81XX_H

/* TI81XX common CM module offsets */
#define TI81XX_CM_ALWON_MOD			0x1400	/* 1KB */

/* TI816X CM module offsets */
#define TI816X_CM_ACTIVE_MOD			0x0400	/* 256B */
#define TI816X_CM_DEFAULT_MOD			0x0500	/* 256B */
#define TI816X_CM_IVAHD0_MOD			0x0600	/* 256B */
#define TI816X_CM_IVAHD1_MOD			0x0700	/* 256B */
#define TI816X_CM_IVAHD2_MOD			0x0800	/* 256B */
#define TI816X_CM_SGX_MOD			0x0900	/* 256B */

/* ALWON */
#define TI81XX_CM_ALWON_L3_SLOW_CLKDM		0x0000
#define TI81XX_CM_ALWON_L3_MED_CLKDM		0x0004
#define TI81XX_CM_ETHERNET_CLKDM		0x0004
#define TI81XX_CM_MMU_CLKDM			0x000C
#define TI81XX_CM_MMUCFG_CLKDM			0x0010
#define TI81XX_CM_ALWON_MPU_CLKDM		0x001C
#define TI81XX_CM_ALWON_L3_FAST_CLKDM		0x0030

/* ACTIVE */
#define TI816X_CM_ACTIVE_GEM_CLKDM		0x0000

/* IVAHD0 */
#define TI816X_CM_IVAHD0_CLKDM			0x0000

/* IVAHD1 */
#define TI816X_CM_IVAHD1_CLKDM			0x0000

/* IVAHD2 */
#define TI816X_CM_IVAHD2_CLKDM			0x0000

/* SGX */
#define TI816X_CM_SGX_CLKDM			0x0000

/* DEFAULT */
#define TI816X_CM_DEFAULT_L3_MED_CLKDM		0x0004
#define TI816X_CM_DEFAULT_PCI_CLKDM		0x0010
#define TI816X_CM_DEFAULT_L3_SLOW_CLKDM		0x0014
#define TI816X_CM_DEFAULT_DUCATI_CLKDM		0x0018

#endif
