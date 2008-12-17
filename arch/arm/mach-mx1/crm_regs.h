/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (c) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */

#ifndef __ARCH_ARM_MACH_MX1_CRM_REGS_H__
#define __ARCH_ARM_MACH_MX1_CRM_REGS_H__

#define CCM_BASE	IO_ADDRESS(CCM_BASE_ADDR)
#define SCM_BASE	IO_ADDRESS(SCM_BASE_ADDR)

/* CCM register addresses */
#define CCM_CSCR	(CCM_BASE + 0x0)
#define CCM_MPCTL0	(CCM_BASE + 0x4)
#define CCM_MPCTL1	(CCM_BASE + 0x8)
#define CCM_SPCTL0	(CCM_BASE + 0xC)
#define CCM_SPCTL1	(CCM_BASE + 0x10)
#define CCM_PCDR	(CCM_BASE + 0x20)

#define CCM_CSCR_CLKO_OFFSET	29
#define CCM_CSCR_CLKO_MASK	(0x7 << 29)
#define CCM_CSCR_USB_OFFSET	26
#define CCM_CSCR_USB_MASK	(0x7 << 26)
#define CCM_CSCR_SPLL_RESTART	(1 << 22)
#define CCM_CSCR_MPLL_RESTART	(1 << 21)
#define CCM_CSCR_OSC_EN_SHIFT	17
#define CCM_CSCR_SYSTEM_SEL	(1 << 16)
#define CCM_CSCR_BCLK_OFFSET	10
#define CCM_CSCR_BCLK_MASK	(0xF << 10)
#define CCM_CSCR_PRESC		(1 << 15)
#define CCM_CSCR_SPEN		(1 << 1)
#define CCM_CSCR_MPEN		(1 << 0)

#define CCM_PCDR_PCLK3_OFFSET	16
#define CCM_PCDR_PCLK3_MASK	(0x7F << 16)
#define CCM_PCDR_PCLK2_OFFSET	4
#define CCM_PCDR_PCLK2_MASK	(0xF << 4)
#define CCM_PCDR_PCLK1_OFFSET	0
#define CCM_PCDR_PCLK1_MASK	0xF

/* SCM register addresses */
#define SCM_SIDR	(SCM_BASE + 0x0)
#define SCM_FMCR	(SCM_BASE + 0x4)
#define SCM_GPCR	(SCM_BASE + 0x8)
#define SCM_GCCR	(SCM_BASE + 0xC)

#define SCM_GCCR_DMA_CLK_EN_OFFSET	3
#define SCM_GCCR_CSI_CLK_EN_OFFSET	2
#define SCM_GCCR_MMA_CLK_EN_OFFSET	1
#define SCM_GCCR_USBD_CLK_EN_OFFSET	0

#endif /* __ARCH_ARM_MACH_MX2_CRM_REGS_H__ */
