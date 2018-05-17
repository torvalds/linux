#ifndef __ARCH_ARM_MACH_OMAP2_CM_REGBITS_24XX_H
#define __ARCH_ARM_MACH_OMAP2_CM_REGBITS_24XX_H

/*
 * OMAP24XX Clock Management register bits
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define OMAP24XX_AUTOSTATE_MPU_MASK			(1 << 0)
#define OMAP24XX_EN_DSS1_MASK				(1 << 0)
#define OMAP24XX_ST_MAILBOXES_SHIFT			30
#define OMAP24XX_ST_HDQ_SHIFT				23
#define OMAP2420_ST_I2C2_SHIFT				20
#define OMAP2430_ST_I2CHS1_SHIFT			19
#define OMAP2420_ST_I2C1_SHIFT				19
#define OMAP2430_ST_I2CHS2_SHIFT			20
#define OMAP24XX_ST_MCBSP2_SHIFT			16
#define OMAP24XX_ST_MCBSP1_SHIFT			15
#define OMAP2430_ST_MCBSP5_SHIFT			5
#define OMAP2430_ST_MCBSP4_SHIFT			4
#define OMAP2430_ST_MCBSP3_SHIFT			3
#define OMAP24XX_ST_AES_SHIFT				3
#define OMAP24XX_ST_RNG_SHIFT				2
#define OMAP24XX_ST_SHA_SHIFT				1
#define OMAP24XX_CLKSEL_DSS2_MASK			(0x1 << 13)
#define OMAP24XX_AUTOSTATE_DSS_MASK			(1 << 2)
#define OMAP24XX_AUTOSTATE_L4_MASK			(1 << 1)
#define OMAP24XX_AUTOSTATE_L3_MASK			(1 << 0)
#define OMAP24XX_AUTOSTATE_GFX_MASK			(1 << 0)
#define OMAP24XX_ST_MPU_WDT_SHIFT			3
#define OMAP24XX_ST_32KSYNC_SHIFT			1
#define OMAP24XX_EN_54M_PLL_SHIFT			6
#define OMAP24XX_EN_96M_PLL_SHIFT			2
#define OMAP24XX_ST_54M_APLL_SHIFT			9
#define OMAP24XX_ST_96M_APLL_SHIFT			8
#define OMAP24XX_AUTO_54M_MASK				(0x3 << 6)
#define OMAP24XX_AUTO_96M_MASK				(0x3 << 2)
#define OMAP24XX_AUTO_DPLL_SHIFT			0
#define OMAP24XX_AUTO_DPLL_MASK				(0x3 << 0)
#define OMAP24XX_CORE_CLK_SRC_MASK			(0x3 << 0)
#define OMAP2420_AUTOSTATE_IVA_MASK			(1 << 8)
#define OMAP24XX_AUTOSTATE_DSP_MASK			(1 << 0)
#define OMAP2430_AUTOSTATE_MDM_MASK			(1 << 0)
#define OMAP24XX_CLKSTCTRL_DISABLE_AUTO		0x0
#define OMAP24XX_CLKSTCTRL_ENABLE_AUTO		0x1
#endif
