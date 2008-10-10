/*
 * arch/arm/plat-omap/include/mach/omap24xx.h
 *
 * This file contains the processor specific definitions
 * of the TI OMAP24XX.
 *
 * Copyright (C) 2007 Texas Instruments.
 * Copyright (C) 2007 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __ASM_ARCH_OMAP24XX_H
#define __ASM_ARCH_OMAP24XX_H

/*
 * Please place only base defines here and put the rest in device
 * specific headers. Note also that some of these defines are needed
 * for omap1 to compile without adding ifdefs.
 */

#define L4_24XX_BASE		0x48000000
#define L4_WK_243X_BASE		0x49000000
#define L3_24XX_BASE		0x68000000

/* interrupt controller */
#define OMAP24XX_IC_BASE	(L4_24XX_BASE + 0xfe000)
#define OMAP24XX_IVA_INTC_BASE	0x40000000

#define OMAP2420_CTRL_BASE	L4_24XX_BASE
#define OMAP2420_32KSYNCT_BASE	(L4_24XX_BASE + 0x4000)
#define OMAP2420_PRCM_BASE	(L4_24XX_BASE + 0x8000)
#define OMAP2420_CM_BASE	(L4_24XX_BASE + 0x8000)
#define OMAP2420_PRM_BASE	OMAP2420_CM_BASE
#define OMAP2420_SDRC_BASE	(L3_24XX_BASE + 0x9000)
#define OMAP2420_SMS_BASE	0x68008000
#define OMAP2420_GPMC_BASE	0x6800a000

#define OMAP2430_32KSYNCT_BASE	(L4_WK_243X_BASE + 0x20000)
#define OMAP2430_PRCM_BASE	(L4_WK_243X_BASE + 0x6000)
#define OMAP2430_CM_BASE	(L4_WK_243X_BASE + 0x6000)
#define OMAP2430_PRM_BASE	OMAP2430_CM_BASE

#define OMAP243X_SMS_BASE	0x6C000000
#define OMAP243X_SDRC_BASE	0x6D000000
#define OMAP243X_GPMC_BASE	0x6E000000
#define OMAP243X_SCM_BASE	(L4_WK_243X_BASE + 0x2000)
#define OMAP243X_CTRL_BASE	OMAP243X_SCM_BASE
#define OMAP243X_HS_BASE	(L4_24XX_BASE + 0x000ac000)

/* DSP SS */
#define OMAP2420_DSP_BASE	0x58000000
#define OMAP2420_DSP_MEM_BASE	(OMAP2420_DSP_BASE + 0x0)
#define OMAP2420_DSP_IPI_BASE	(OMAP2420_DSP_BASE + 0x1000000)
#define OMAP2420_DSP_MMU_BASE	(OMAP2420_DSP_BASE + 0x2000000)

#define OMAP243X_DSP_BASE	0x5C000000
#define OMAP243X_DSP_MEM_BASE	(OMAP243X_DSP_BASE + 0x0)
#define OMAP243X_DSP_MMU_BASE	(OMAP243X_DSP_BASE + 0x1000000)

/* Mailbox */
#define OMAP24XX_MAILBOX_BASE	(L4_24XX_BASE + 0x94000)

/* Camera */
#define OMAP24XX_CAMERA_BASE	(L4_24XX_BASE + 0x52000)

/* Security */
#define OMAP24XX_SEC_BASE	(L4_24XX_BASE + 0xA0000)
#define OMAP24XX_SEC_RNG_BASE	(OMAP24XX_SEC_BASE + 0x0000)
#define OMAP24XX_SEC_DES_BASE	(OMAP24XX_SEC_BASE + 0x2000)
#define OMAP24XX_SEC_SHA1MD5_BASE (OMAP24XX_SEC_BASE + 0x4000)
#define OMAP24XX_SEC_AES_BASE	(OMAP24XX_SEC_BASE + 0x6000)
#define OMAP24XX_SEC_PKA_BASE	(OMAP24XX_SEC_BASE + 0x8000)

#if defined(CONFIG_ARCH_OMAP2420)

#define OMAP2_32KSYNCT_BASE	OMAP2420_32KSYNCT_BASE
#define OMAP2_PRCM_BASE		OMAP2420_PRCM_BASE
#define OMAP2_CM_BASE		OMAP2420_CM_BASE
#define OMAP2_PRM_BASE		OMAP2420_PRM_BASE
#define OMAP2_VA_IC_BASE	IO_ADDRESS(OMAP24XX_IC_BASE)

#elif defined(CONFIG_ARCH_OMAP2430)

#define OMAP2_32KSYNCT_BASE	OMAP2430_32KSYNCT_BASE
#define OMAP2_PRCM_BASE		OMAP2430_PRCM_BASE
#define OMAP2_CM_BASE		OMAP2430_CM_BASE
#define OMAP2_PRM_BASE		OMAP2430_PRM_BASE
#define OMAP2_VA_IC_BASE	IO_ADDRESS(OMAP24XX_IC_BASE)

#endif

#endif /* __ASM_ARCH_OMAP24XX_H */

