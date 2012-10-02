/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    chipcHw_init.c
*
*  @brief   Low level CHIPC PLL configuration functions
*
*  @note
*
*   These routines provide basic PLL controlling functionality only.
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/export.h>

#include <mach/csp/chipcHw_def.h>
#include <mach/csp/chipcHw_inline.h>

#include <mach/csp/reg.h>
#include <linux/delay.h>
/* ---- Private Constants and Types --------------------------------------- */

/*
    Calculation for NDIV_i to obtain VCO frequency
    -----------------------------------------------

	Freq_vco = Freq_ref * (P2 / P1) * (PLL_NDIV_i + PLL_NDIV_f)
	for Freq_vco = VCO_FREQ_MHz
		Freq_ref = chipcHw_XTAL_FREQ_Hz
		PLL_P1 = PLL_P2 = 1
		and
		PLL_NDIV_f = 0

	We get:
		PLL_NDIV_i = Freq_vco / Freq_ref = VCO_FREQ_MHz / chipcHw_XTAL_FREQ_Hz

    Calculation for PLL MDIV to obtain frequency Freq_x for channel x
    -----------------------------------------------------------------
		Freq_x = chipcHw_XTAL_FREQ_Hz * PLL_NDIV_i / PLL_MDIV_x = VCO_FREQ_MHz / PLL_MDIV_x

		PLL_MDIV_x = VCO_FREQ_MHz / Freq_x
*/

/* ---- Private Variables ------------------------------------------------- */
/****************************************************************************/
/**
*  @brief  Initializes the PLL2
*
*  This function initializes the PLL2
*
*/
/****************************************************************************/
void chipcHw_pll2Enable(uint32_t vcoFreqHz)
{
	uint32_t pllPreDivider2 = 0;

	{
		REG_LOCAL_IRQ_SAVE;
		writel(chipcHw_REG_PLL_CONFIG_D_RESET |
		       chipcHw_REG_PLL_CONFIG_A_RESET,
			&pChipcHw->PLLConfig2);

		pllPreDivider2 = chipcHw_REG_PLL_PREDIVIDER_POWER_DOWN |
		    chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_INTEGER |
		    (chipcHw_REG_PLL_PREDIVIDER_NDIV_i(vcoFreqHz) <<
		     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT) |
		    (chipcHw_REG_PLL_PREDIVIDER_P1 <<
		     chipcHw_REG_PLL_PREDIVIDER_P1_SHIFT) |
		    (chipcHw_REG_PLL_PREDIVIDER_P2 <<
		     chipcHw_REG_PLL_PREDIVIDER_P2_SHIFT);

		/* Enable CHIPC registers to control the PLL */
		writel(readl(&pChipcHw->PLLStatus) | chipcHw_REG_PLL_STATUS_CONTROL_ENABLE, &pChipcHw->PLLStatus);

		/* Set pre divider to get desired VCO frequency */
		writel(pllPreDivider2, &pChipcHw->PLLPreDivider2);
		/* Set NDIV Frac */
		writel(chipcHw_REG_PLL_DIVIDER_NDIV_f, &pChipcHw->PLLDivider2);

		/* This has to be removed once the default values are fixed for PLL2. */
		writel(0x38000700, &pChipcHw->PLLControl12);
		writel(0x00000015, &pChipcHw->PLLControl22);

		/* Reset PLL2 */
		if (vcoFreqHz > chipcHw_REG_PLL_CONFIG_VCO_SPLIT_FREQ) {
			writel(chipcHw_REG_PLL_CONFIG_D_RESET |
			    chipcHw_REG_PLL_CONFIG_A_RESET |
			    chipcHw_REG_PLL_CONFIG_VCO_1601_3200 |
			    chipcHw_REG_PLL_CONFIG_POWER_DOWN,
			    &pChipcHw->PLLConfig2);
		} else {
			writel(chipcHw_REG_PLL_CONFIG_D_RESET |
			    chipcHw_REG_PLL_CONFIG_A_RESET |
			    chipcHw_REG_PLL_CONFIG_VCO_800_1600 |
			    chipcHw_REG_PLL_CONFIG_POWER_DOWN,
			    &pChipcHw->PLLConfig2);
		}
		REG_LOCAL_IRQ_RESTORE;
	}

	/* Insert certain amount of delay before deasserting ARESET. */
	udelay(1);

	{
		REG_LOCAL_IRQ_SAVE;
		/* Remove analog reset and Power on the PLL */
		writel(readl(&pChipcHw->PLLConfig2) &
		    ~(chipcHw_REG_PLL_CONFIG_A_RESET |
		      chipcHw_REG_PLL_CONFIG_POWER_DOWN),
		      &pChipcHw->PLLConfig2);

		REG_LOCAL_IRQ_RESTORE;

	}

	/* Wait until PLL is locked */
	while (!(readl(&pChipcHw->PLLStatus2) & chipcHw_REG_PLL_STATUS_LOCKED))
		;

	{
		REG_LOCAL_IRQ_SAVE;
		/* Remove digital reset */
		writel(readl(&pChipcHw->PLLConfig2) &
			~chipcHw_REG_PLL_CONFIG_D_RESET,
			&pChipcHw->PLLConfig2);

		REG_LOCAL_IRQ_RESTORE;
	}
}

EXPORT_SYMBOL(chipcHw_pll2Enable);

/****************************************************************************/
/**
*  @brief  Initializes the PLL1
*
*  This function initializes the PLL1
*
*/
/****************************************************************************/
void chipcHw_pll1Enable(uint32_t vcoFreqHz, chipcHw_SPREAD_SPECTRUM_e ssSupport)
{
	uint32_t pllPreDivider = 0;

	{
		REG_LOCAL_IRQ_SAVE;

		writel(chipcHw_REG_PLL_CONFIG_D_RESET |
		    chipcHw_REG_PLL_CONFIG_A_RESET,
		    &pChipcHw->PLLConfig);
		/* Setting VCO frequency */
		if (ssSupport == chipcHw_SPREAD_SPECTRUM_ALLOW) {
			pllPreDivider =
			    chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASH_1_8 |
			    ((chipcHw_REG_PLL_PREDIVIDER_NDIV_i(vcoFreqHz) -
			      1) << chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT) |
			    (chipcHw_REG_PLL_PREDIVIDER_P1 <<
			     chipcHw_REG_PLL_PREDIVIDER_P1_SHIFT) |
			    (chipcHw_REG_PLL_PREDIVIDER_P2 <<
			     chipcHw_REG_PLL_PREDIVIDER_P2_SHIFT);
		} else {
			pllPreDivider = chipcHw_REG_PLL_PREDIVIDER_POWER_DOWN |
			    chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_INTEGER |
			    (chipcHw_REG_PLL_PREDIVIDER_NDIV_i(vcoFreqHz) <<
			     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT) |
			    (chipcHw_REG_PLL_PREDIVIDER_P1 <<
			     chipcHw_REG_PLL_PREDIVIDER_P1_SHIFT) |
			    (chipcHw_REG_PLL_PREDIVIDER_P2 <<
			     chipcHw_REG_PLL_PREDIVIDER_P2_SHIFT);
		}

		/* Enable CHIPC registers to control the PLL */
		writel(readl(&pChipcHw->PLLStatus) | chipcHw_REG_PLL_STATUS_CONTROL_ENABLE, &pChipcHw->PLLStatus);

		/* Set pre divider to get desired VCO frequency */
		writel(pllPreDivider, &pChipcHw->PLLPreDivider);
		/* Set NDIV Frac */
		if (ssSupport == chipcHw_SPREAD_SPECTRUM_ALLOW) {
			writel(chipcHw_REG_PLL_DIVIDER_M1DIV | chipcHw_REG_PLL_DIVIDER_NDIV_f_SS, &pChipcHw->PLLDivider);
		} else {
			writel(chipcHw_REG_PLL_DIVIDER_M1DIV | chipcHw_REG_PLL_DIVIDER_NDIV_f, &pChipcHw->PLLDivider);
		}

		/* Reset PLL1 */
		if (vcoFreqHz > chipcHw_REG_PLL_CONFIG_VCO_SPLIT_FREQ) {
			writel(chipcHw_REG_PLL_CONFIG_D_RESET | chipcHw_REG_PLL_CONFIG_A_RESET | chipcHw_REG_PLL_CONFIG_VCO_1601_3200 | chipcHw_REG_PLL_CONFIG_POWER_DOWN, &pChipcHw->PLLConfig);
		} else {
			writel(chipcHw_REG_PLL_CONFIG_D_RESET | chipcHw_REG_PLL_CONFIG_A_RESET | chipcHw_REG_PLL_CONFIG_VCO_800_1600 | chipcHw_REG_PLL_CONFIG_POWER_DOWN, &pChipcHw->PLLConfig);
		}

		REG_LOCAL_IRQ_RESTORE;

		/* Insert certain amount of delay before deasserting ARESET. */
		udelay(1);

		{
			REG_LOCAL_IRQ_SAVE;
			/* Remove analog reset and Power on the PLL */
			writel(readl(&pChipcHw->PLLConfig) & ~(chipcHw_REG_PLL_CONFIG_A_RESET | chipcHw_REG_PLL_CONFIG_POWER_DOWN), &pChipcHw->PLLConfig);
			REG_LOCAL_IRQ_RESTORE;
		}

		/* Wait until PLL is locked */
		while (!(readl(&pChipcHw->PLLStatus) & chipcHw_REG_PLL_STATUS_LOCKED)
		       || !(readl(&pChipcHw->PLLStatus2) & chipcHw_REG_PLL_STATUS_LOCKED))
			;

		/* Remove digital reset */
		{
			REG_LOCAL_IRQ_SAVE;
			writel(readl(&pChipcHw->PLLConfig) & ~chipcHw_REG_PLL_CONFIG_D_RESET, &pChipcHw->PLLConfig);
			REG_LOCAL_IRQ_RESTORE;
		}
	}
}

EXPORT_SYMBOL(chipcHw_pll1Enable);

/****************************************************************************/
/**
*  @brief  Initializes the chipc module
*
*  This function initializes the PLLs and core system clocks
*
*/
/****************************************************************************/

void chipcHw_Init(chipcHw_INIT_PARAM_t *initParam	/*  [ IN ] Misc chip initialization parameter */
    ) {
#if !(defined(__KERNEL__) && !defined(STANDALONE))
	delay_init();
#endif

	/* Do not program PLL, when warm reset */
	if (!(chipcHw_getStickyBits() & chipcHw_REG_STICKY_CHIP_WARM_RESET)) {
		chipcHw_pll1Enable(initParam->pllVcoFreqHz,
				   initParam->ssSupport);
		chipcHw_pll2Enable(initParam->pll2VcoFreqHz);
	} else {
		/* Clear sticky bits */
		chipcHw_clearStickyBits(chipcHw_REG_STICKY_CHIP_WARM_RESET);
	}
	/* Clear sticky bits */
	chipcHw_clearStickyBits(chipcHw_REG_STICKY_CHIP_SOFT_RESET);

	/* Before configuring the ARM clock, atleast we need to make sure BUS clock maintains the proper ratio with ARM clock */
	writel((readl(&pChipcHw->ACLKClock) & ~chipcHw_REG_ACLKClock_CLK_DIV_MASK) | (initParam-> armBusRatio & chipcHw_REG_ACLKClock_CLK_DIV_MASK), &pChipcHw->ACLKClock);

	/* Set various core component frequencies. The order in which this is done is important for some. */
	/* The RTBUS (DDR PHY) is derived from the BUS, and the BUS from the ARM, and VPM needs to know BUS */
	/* frequency to find its ratio with the BUS.  Hence we must set the ARM first, followed by the BUS,  */
	/* then VPM and RTBUS. */

	chipcHw_setClockFrequency(chipcHw_CLOCK_ARM,
				  initParam->busClockFreqHz *
				  initParam->armBusRatio);
	chipcHw_setClockFrequency(chipcHw_CLOCK_BUS, initParam->busClockFreqHz);
	chipcHw_setClockFrequency(chipcHw_CLOCK_VPM,
				  initParam->busClockFreqHz *
				  initParam->vpmBusRatio);
	chipcHw_setClockFrequency(chipcHw_CLOCK_DDR,
				  initParam->busClockFreqHz *
				  initParam->ddrBusRatio);
	chipcHw_setClockFrequency(chipcHw_CLOCK_RTBUS,
				  initParam->busClockFreqHz / 2);
}
