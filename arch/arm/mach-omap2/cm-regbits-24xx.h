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

#include "cm.h"

/* Bits shared between registers */

/* CM_FCLKEN1_CORE and CM_ICLKEN1_CORE shared bits */
#define OMAP24XX_EN_CAM_SHIFT				31
#define OMAP24XX_EN_CAM					(1 << 31)
#define OMAP24XX_EN_WDT4_SHIFT				29
#define OMAP24XX_EN_WDT4				(1 << 29)
#define OMAP2420_EN_WDT3_SHIFT				28
#define OMAP2420_EN_WDT3				(1 << 28)
#define OMAP24XX_EN_MSPRO_SHIFT				27
#define OMAP24XX_EN_MSPRO				(1 << 27)
#define OMAP24XX_EN_FAC_SHIFT				25
#define OMAP24XX_EN_FAC					(1 << 25)
#define OMAP2420_EN_EAC_SHIFT				24
#define OMAP2420_EN_EAC					(1 << 24)
#define OMAP24XX_EN_HDQ_SHIFT				23
#define OMAP24XX_EN_HDQ					(1 << 23)
#define OMAP2420_EN_I2C2_SHIFT				20
#define OMAP2420_EN_I2C2				(1 << 20)
#define OMAP2420_EN_I2C1_SHIFT				19
#define OMAP2420_EN_I2C1				(1 << 19)

/* CM_FCLKEN2_CORE and CM_ICLKEN2_CORE shared bits */
#define OMAP2430_EN_MCBSP5_SHIFT			5
#define OMAP2430_EN_MCBSP5				(1 << 5)
#define OMAP2430_EN_MCBSP4_SHIFT			4
#define OMAP2430_EN_MCBSP4				(1 << 4)
#define OMAP2430_EN_MCBSP3_SHIFT			3
#define OMAP2430_EN_MCBSP3				(1 << 3)
#define OMAP24XX_EN_SSI_SHIFT				1
#define OMAP24XX_EN_SSI					(1 << 1)

/* CM_FCLKEN_WKUP and CM_ICLKEN_WKUP shared bits */
#define OMAP24XX_EN_MPU_WDT_SHIFT			3
#define OMAP24XX_EN_MPU_WDT				(1 << 3)

/* Bits specific to each register */

/* CM_IDLEST_MPU */
/* 2430 only */
#define OMAP2430_ST_MPU					(1 << 0)

/* CM_CLKSEL_MPU */
#define OMAP24XX_CLKSEL_MPU_SHIFT			0
#define OMAP24XX_CLKSEL_MPU_MASK			(0x1f << 0)

/* CM_CLKSTCTRL_MPU */
#define OMAP24XX_AUTOSTATE_MPU_SHIFT			0
#define OMAP24XX_AUTOSTATE_MPU_MASK			(1 << 0)

/* CM_FCLKEN1_CORE specific bits*/
#define OMAP24XX_EN_TV_SHIFT				2
#define OMAP24XX_EN_TV					(1 << 2)
#define OMAP24XX_EN_DSS2_SHIFT				1
#define OMAP24XX_EN_DSS2				(1 << 1)
#define OMAP24XX_EN_DSS1_SHIFT				0
#define OMAP24XX_EN_DSS1				(1 << 0)

/* CM_FCLKEN2_CORE specific bits */
#define OMAP2430_EN_I2CHS2_SHIFT			20
#define OMAP2430_EN_I2CHS2				(1 << 20)
#define OMAP2430_EN_I2CHS1_SHIFT			19
#define OMAP2430_EN_I2CHS1				(1 << 19)
#define OMAP2430_EN_MMCHSDB2_SHIFT			17
#define OMAP2430_EN_MMCHSDB2				(1 << 17)
#define OMAP2430_EN_MMCHSDB1_SHIFT			16
#define OMAP2430_EN_MMCHSDB1				(1 << 16)

/* CM_ICLKEN1_CORE specific bits */
#define OMAP24XX_EN_MAILBOXES_SHIFT			30
#define OMAP24XX_EN_MAILBOXES				(1 << 30)
#define OMAP24XX_EN_DSS_SHIFT				0
#define OMAP24XX_EN_DSS					(1 << 0)

/* CM_ICLKEN2_CORE specific bits */

/* CM_ICLKEN3_CORE */
/* 2430 only */
#define OMAP2430_EN_SDRC_SHIFT				2
#define OMAP2430_EN_SDRC				(1 << 2)

/* CM_ICLKEN4_CORE */
#define OMAP24XX_EN_PKA_SHIFT				4
#define OMAP24XX_EN_PKA					(1 << 4)
#define OMAP24XX_EN_AES_SHIFT				3
#define OMAP24XX_EN_AES					(1 << 3)
#define OMAP24XX_EN_RNG_SHIFT				2
#define OMAP24XX_EN_RNG					(1 << 2)
#define OMAP24XX_EN_SHA_SHIFT				1
#define OMAP24XX_EN_SHA					(1 << 1)
#define OMAP24XX_EN_DES_SHIFT				0
#define OMAP24XX_EN_DES					(1 << 0)

/* CM_IDLEST1_CORE specific bits */
#define OMAP24XX_ST_MAILBOXES				(1 << 30)
#define OMAP24XX_ST_WDT4				(1 << 29)
#define OMAP2420_ST_WDT3				(1 << 28)
#define OMAP24XX_ST_MSPRO				(1 << 27)
#define OMAP24XX_ST_FAC					(1 << 25)
#define OMAP2420_ST_EAC					(1 << 24)
#define OMAP24XX_ST_HDQ					(1 << 23)
#define OMAP24XX_ST_I2C2				(1 << 20)
#define OMAP24XX_ST_I2C1				(1 << 19)
#define OMAP24XX_ST_MCBSP2				(1 << 16)
#define OMAP24XX_ST_MCBSP1				(1 << 15)
#define OMAP24XX_ST_DSS					(1 << 0)

/* CM_IDLEST2_CORE */
#define OMAP2430_ST_MCBSP5				(1 << 5)
#define OMAP2430_ST_MCBSP4				(1 << 4)
#define OMAP2430_ST_MCBSP3				(1 << 3)
#define OMAP24XX_ST_SSI					(1 << 1)

/* CM_IDLEST3_CORE */
/* 2430 only */
#define OMAP2430_ST_SDRC				(1 << 2)

/* CM_IDLEST4_CORE */
#define OMAP24XX_ST_PKA					(1 << 4)
#define OMAP24XX_ST_AES					(1 << 3)
#define OMAP24XX_ST_RNG					(1 << 2)
#define OMAP24XX_ST_SHA					(1 << 1)
#define OMAP24XX_ST_DES					(1 << 0)

/* CM_AUTOIDLE1_CORE */
#define OMAP24XX_AUTO_CAM				(1 << 31)
#define OMAP24XX_AUTO_MAILBOXES				(1 << 30)
#define OMAP24XX_AUTO_WDT4				(1 << 29)
#define OMAP2420_AUTO_WDT3				(1 << 28)
#define OMAP24XX_AUTO_MSPRO				(1 << 27)
#define OMAP2420_AUTO_MMC				(1 << 26)
#define OMAP24XX_AUTO_FAC				(1 << 25)
#define OMAP2420_AUTO_EAC				(1 << 24)
#define OMAP24XX_AUTO_HDQ				(1 << 23)
#define OMAP24XX_AUTO_UART2				(1 << 22)
#define OMAP24XX_AUTO_UART1				(1 << 21)
#define OMAP24XX_AUTO_I2C2				(1 << 20)
#define OMAP24XX_AUTO_I2C1				(1 << 19)
#define OMAP24XX_AUTO_MCSPI2				(1 << 18)
#define OMAP24XX_AUTO_MCSPI1				(1 << 17)
#define OMAP24XX_AUTO_MCBSP2				(1 << 16)
#define OMAP24XX_AUTO_MCBSP1				(1 << 15)
#define OMAP24XX_AUTO_GPT12				(1 << 14)
#define OMAP24XX_AUTO_GPT11				(1 << 13)
#define OMAP24XX_AUTO_GPT10				(1 << 12)
#define OMAP24XX_AUTO_GPT9				(1 << 11)
#define OMAP24XX_AUTO_GPT8				(1 << 10)
#define OMAP24XX_AUTO_GPT7				(1 << 9)
#define OMAP24XX_AUTO_GPT6				(1 << 8)
#define OMAP24XX_AUTO_GPT5				(1 << 7)
#define OMAP24XX_AUTO_GPT4				(1 << 6)
#define OMAP24XX_AUTO_GPT3				(1 << 5)
#define OMAP24XX_AUTO_GPT2				(1 << 4)
#define OMAP2420_AUTO_VLYNQ				(1 << 3)
#define OMAP24XX_AUTO_DSS				(1 << 0)

/* CM_AUTOIDLE2_CORE */
#define OMAP2430_AUTO_MDM_INTC				(1 << 11)
#define OMAP2430_AUTO_GPIO5				(1 << 10)
#define OMAP2430_AUTO_MCSPI3				(1 << 9)
#define OMAP2430_AUTO_MMCHS2				(1 << 8)
#define OMAP2430_AUTO_MMCHS1				(1 << 7)
#define OMAP2430_AUTO_USBHS				(1 << 6)
#define OMAP2430_AUTO_MCBSP5				(1 << 5)
#define OMAP2430_AUTO_MCBSP4				(1 << 4)
#define OMAP2430_AUTO_MCBSP3				(1 << 3)
#define OMAP24XX_AUTO_UART3				(1 << 2)
#define OMAP24XX_AUTO_SSI				(1 << 1)
#define OMAP24XX_AUTO_USB				(1 << 0)

/* CM_AUTOIDLE3_CORE */
#define OMAP24XX_AUTO_SDRC				(1 << 2)
#define OMAP24XX_AUTO_GPMC				(1 << 1)
#define OMAP24XX_AUTO_SDMA				(1 << 0)

/* CM_AUTOIDLE4_CORE */
#define OMAP24XX_AUTO_PKA				(1 << 4)
#define OMAP24XX_AUTO_AES				(1 << 3)
#define OMAP24XX_AUTO_RNG				(1 << 2)
#define OMAP24XX_AUTO_SHA				(1 << 1)
#define OMAP24XX_AUTO_DES				(1 << 0)

/* CM_CLKSEL1_CORE */
#define OMAP24XX_CLKSEL_USB_SHIFT			25
#define OMAP24XX_CLKSEL_USB_MASK			(0x7 << 25)
#define OMAP24XX_CLKSEL_SSI_SHIFT			20
#define OMAP24XX_CLKSEL_SSI_MASK			(0x1f << 20)
#define OMAP2420_CLKSEL_VLYNQ_SHIFT			15
#define OMAP2420_CLKSEL_VLYNQ_MASK			(0x1f << 15)
#define OMAP24XX_CLKSEL_DSS2_SHIFT			13
#define OMAP24XX_CLKSEL_DSS2_MASK			(0x1 << 13)
#define OMAP24XX_CLKSEL_DSS1_SHIFT			8
#define OMAP24XX_CLKSEL_DSS1_MASK			(0x1f << 8)
#define OMAP24XX_CLKSEL_L4_SHIFT			5
#define OMAP24XX_CLKSEL_L4_MASK				(0x3 << 5)
#define OMAP24XX_CLKSEL_L3_SHIFT			0
#define OMAP24XX_CLKSEL_L3_MASK				(0x1f << 0)

/* CM_CLKSEL2_CORE */
#define OMAP24XX_CLKSEL_GPT12_SHIFT			22
#define OMAP24XX_CLKSEL_GPT12_MASK			(0x3 << 22)
#define OMAP24XX_CLKSEL_GPT11_SHIFT			20
#define OMAP24XX_CLKSEL_GPT11_MASK			(0x3 << 20)
#define OMAP24XX_CLKSEL_GPT10_SHIFT			18
#define OMAP24XX_CLKSEL_GPT10_MASK			(0x3 << 18)
#define OMAP24XX_CLKSEL_GPT9_SHIFT			16
#define OMAP24XX_CLKSEL_GPT9_MASK			(0x3 << 16)
#define OMAP24XX_CLKSEL_GPT8_SHIFT			14
#define OMAP24XX_CLKSEL_GPT8_MASK			(0x3 << 14)
#define OMAP24XX_CLKSEL_GPT7_SHIFT			12
#define OMAP24XX_CLKSEL_GPT7_MASK			(0x3 << 12)
#define OMAP24XX_CLKSEL_GPT6_SHIFT			10
#define OMAP24XX_CLKSEL_GPT6_MASK			(0x3 << 10)
#define OMAP24XX_CLKSEL_GPT5_SHIFT			8
#define OMAP24XX_CLKSEL_GPT5_MASK			(0x3 << 8)
#define OMAP24XX_CLKSEL_GPT4_SHIFT			6
#define OMAP24XX_CLKSEL_GPT4_MASK			(0x3 << 6)
#define OMAP24XX_CLKSEL_GPT3_SHIFT			4
#define OMAP24XX_CLKSEL_GPT3_MASK			(0x3 << 4)
#define OMAP24XX_CLKSEL_GPT2_SHIFT			2
#define OMAP24XX_CLKSEL_GPT2_MASK			(0x3 << 2)

/* CM_CLKSTCTRL_CORE */
#define OMAP24XX_AUTOSTATE_DSS_SHIFT			2
#define OMAP24XX_AUTOSTATE_DSS_MASK			(1 << 2)
#define OMAP24XX_AUTOSTATE_L4_SHIFT			1
#define OMAP24XX_AUTOSTATE_L4_MASK			(1 << 1)
#define OMAP24XX_AUTOSTATE_L3_SHIFT			0
#define OMAP24XX_AUTOSTATE_L3_MASK			(1 << 0)

/* CM_FCLKEN_GFX */
#define OMAP24XX_EN_3D_SHIFT				2
#define OMAP24XX_EN_3D					(1 << 2)
#define OMAP24XX_EN_2D_SHIFT				1
#define OMAP24XX_EN_2D					(1 << 1)

/* CM_ICLKEN_GFX specific bits */

/* CM_IDLEST_GFX specific bits */

/* CM_CLKSEL_GFX specific bits */

/* CM_CLKSTCTRL_GFX */
#define OMAP24XX_AUTOSTATE_GFX_SHIFT			0
#define OMAP24XX_AUTOSTATE_GFX_MASK			(1 << 0)

/* CM_FCLKEN_WKUP specific bits */

/* CM_ICLKEN_WKUP specific bits */
#define OMAP2430_EN_ICR_SHIFT				6
#define OMAP2430_EN_ICR					(1 << 6)
#define OMAP24XX_EN_OMAPCTRL_SHIFT			5
#define OMAP24XX_EN_OMAPCTRL				(1 << 5)
#define OMAP24XX_EN_WDT1_SHIFT				4
#define OMAP24XX_EN_WDT1				(1 << 4)
#define OMAP24XX_EN_32KSYNC_SHIFT			1
#define OMAP24XX_EN_32KSYNC				(1 << 1)

/* CM_IDLEST_WKUP specific bits */
#define OMAP2430_ST_ICR					(1 << 6)
#define OMAP24XX_ST_OMAPCTRL				(1 << 5)
#define OMAP24XX_ST_WDT1				(1 << 4)
#define OMAP24XX_ST_MPU_WDT				(1 << 3)
#define OMAP24XX_ST_32KSYNC				(1 << 1)

/* CM_AUTOIDLE_WKUP */
#define OMAP24XX_AUTO_OMAPCTRL				(1 << 5)
#define OMAP24XX_AUTO_WDT1				(1 << 4)
#define OMAP24XX_AUTO_MPU_WDT				(1 << 3)
#define OMAP24XX_AUTO_GPIOS				(1 << 2)
#define OMAP24XX_AUTO_32KSYNC				(1 << 1)
#define OMAP24XX_AUTO_GPT1				(1 << 0)

/* CM_CLKSEL_WKUP */
#define OMAP24XX_CLKSEL_GPT1_SHIFT			0
#define OMAP24XX_CLKSEL_GPT1_MASK			(0x3 << 0)

/* CM_CLKEN_PLL */
#define OMAP24XX_EN_54M_PLL_SHIFT			6
#define OMAP24XX_EN_54M_PLL_MASK			(0x3 << 6)
#define OMAP24XX_EN_96M_PLL_SHIFT			2
#define OMAP24XX_EN_96M_PLL_MASK			(0x3 << 2)
#define OMAP24XX_EN_DPLL_SHIFT				0
#define OMAP24XX_EN_DPLL_MASK				(0x3 << 0)

/* CM_IDLEST_CKGEN */
#define OMAP24XX_ST_54M_APLL				(1 << 9)
#define OMAP24XX_ST_96M_APLL				(1 << 8)
#define OMAP24XX_ST_54M_CLK				(1 << 6)
#define OMAP24XX_ST_12M_CLK				(1 << 5)
#define OMAP24XX_ST_48M_CLK				(1 << 4)
#define OMAP24XX_ST_96M_CLK				(1 << 2)
#define OMAP24XX_ST_CORE_CLK_SHIFT			0
#define OMAP24XX_ST_CORE_CLK_MASK			(0x3 << 0)

/* CM_AUTOIDLE_PLL */
#define OMAP24XX_AUTO_54M_SHIFT				6
#define OMAP24XX_AUTO_54M_MASK				(0x3 << 6)
#define OMAP24XX_AUTO_96M_SHIFT				2
#define OMAP24XX_AUTO_96M_MASK				(0x3 << 2)
#define OMAP24XX_AUTO_DPLL_SHIFT			0
#define OMAP24XX_AUTO_DPLL_MASK				(0x3 << 0)

/* CM_CLKSEL1_PLL */
#define OMAP2430_MAXDPLLFASTLOCK_SHIFT			28
#define OMAP2430_MAXDPLLFASTLOCK_MASK			(0x7 << 28)
#define OMAP24XX_APLLS_CLKIN_SHIFT			23
#define OMAP24XX_APLLS_CLKIN_MASK			(0x7 << 23)
#define OMAP24XX_DPLL_MULT_SHIFT			12
#define OMAP24XX_DPLL_MULT_MASK				(0x3ff << 12)
#define OMAP24XX_DPLL_DIV_SHIFT				8
#define OMAP24XX_DPLL_DIV_MASK				(0xf << 8)
#define OMAP24XX_54M_SOURCE_SHIFT			5
#define OMAP24XX_54M_SOURCE				(1 << 5)
#define OMAP2430_96M_SOURCE_SHIFT			4
#define OMAP2430_96M_SOURCE				(1 << 4)
#define OMAP24XX_48M_SOURCE_SHIFT			3
#define OMAP24XX_48M_SOURCE				(1 << 3)
#define OMAP2430_ALTCLK_SOURCE_SHIFT			0
#define OMAP2430_ALTCLK_SOURCE_MASK			(0x7 << 0)

/* CM_CLKSEL2_PLL */
#define OMAP24XX_CORE_CLK_SRC_SHIFT			0
#define OMAP24XX_CORE_CLK_SRC_MASK			(0x3 << 0)

/* CM_FCLKEN_DSP */
#define OMAP2420_EN_IVA_COP_SHIFT			10
#define OMAP2420_EN_IVA_COP				(1 << 10)
#define OMAP2420_EN_IVA_MPU_SHIFT			8
#define OMAP2420_EN_IVA_MPU				(1 << 8)
#define OMAP24XX_CM_FCLKEN_DSP_EN_DSP_SHIFT		0
#define OMAP24XX_CM_FCLKEN_DSP_EN_DSP			(1 << 0)

/* CM_ICLKEN_DSP */
#define OMAP2420_EN_DSP_IPI_SHIFT			1
#define OMAP2420_EN_DSP_IPI				(1 << 1)

/* CM_IDLEST_DSP */
#define OMAP2420_ST_IVA					(1 << 8)
#define OMAP2420_ST_IPI					(1 << 1)
#define OMAP24XX_ST_DSP					(1 << 0)

/* CM_AUTOIDLE_DSP */
#define OMAP2420_AUTO_DSP_IPI				(1 << 1)

/* CM_CLKSEL_DSP */
#define OMAP2420_SYNC_IVA				(1 << 13)
#define OMAP2420_CLKSEL_IVA_SHIFT			8
#define OMAP2420_CLKSEL_IVA_MASK			(0x1f << 8)
#define OMAP24XX_SYNC_DSP				(1 << 7)
#define OMAP24XX_CLKSEL_DSP_IF_SHIFT			5
#define OMAP24XX_CLKSEL_DSP_IF_MASK			(0x3 << 5)
#define OMAP24XX_CLKSEL_DSP_SHIFT			0
#define OMAP24XX_CLKSEL_DSP_MASK			(0x1f << 0)

/* CM_CLKSTCTRL_DSP */
#define OMAP2420_AUTOSTATE_IVA_SHIFT			8
#define OMAP2420_AUTOSTATE_IVA_MASK			(1 << 8)
#define OMAP24XX_AUTOSTATE_DSP_SHIFT			0
#define OMAP24XX_AUTOSTATE_DSP_MASK			(1 << 0)

/* CM_FCLKEN_MDM */
/* 2430 only */
#define OMAP2430_EN_OSC_SHIFT				1
#define OMAP2430_EN_OSC					(1 << 1)

/* CM_ICLKEN_MDM */
/* 2430 only */
#define OMAP2430_CM_ICLKEN_MDM_EN_MDM_SHIFT		0
#define OMAP2430_CM_ICLKEN_MDM_EN_MDM			(1 << 0)

/* CM_IDLEST_MDM specific bits */
/* 2430 only */

/* CM_AUTOIDLE_MDM */
/* 2430 only */
#define OMAP2430_AUTO_OSC				(1 << 1)
#define OMAP2430_AUTO_MDM				(1 << 0)

/* CM_CLKSEL_MDM */
/* 2430 only */
#define OMAP2430_SYNC_MDM				(1 << 4)
#define OMAP2430_CLKSEL_MDM_SHIFT			0
#define OMAP2430_CLKSEL_MDM_MASK			(0xf << 0)

/* CM_CLKSTCTRL_MDM */
/* 2430 only */
#define OMAP2430_AUTOSTATE_MDM_SHIFT			0
#define OMAP2430_AUTOSTATE_MDM_MASK			(1 << 0)

#endif
