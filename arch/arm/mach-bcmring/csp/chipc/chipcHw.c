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
*  @file    chipcHw.c
*
*  @brief   Low level Various CHIP clock controlling routines
*
*  @note
*
*   These routines provide basic clock controlling functionality only.
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <csp/errno.h>
#include <csp/stdint.h>
#include <csp/module.h>

#include <mach/csp/chipcHw_def.h>
#include <mach/csp/chipcHw_inline.h>

#include <csp/reg.h>
#include <csp/delay.h>

/* ---- Private Constants and Types --------------------------------------- */

/* VPM alignment algorithm uses this */
#define MAX_PHASE_ADJUST_COUNT         0xFFFF	/* Max number of times allowed to adjust the phase */
#define MAX_PHASE_ALIGN_ATTEMPTS       10	/* Max number of attempt to align the phase */

/* Local definition of clock type */
#define PLL_CLOCK                      1	/* PLL Clock */
#define NON_PLL_CLOCK                  2	/* Divider clock */

static int chipcHw_divide(int num, int denom)
    __attribute__ ((section(".aramtext")));

/****************************************************************************/
/**
*  @brief   Set clock fequency for miscellaneous configurable clocks
*
*  This function sets clock frequency
*
*  @return  Configured clock frequency in hertz
*
*/
/****************************************************************************/
chipcHw_freq chipcHw_getClockFrequency(chipcHw_CLOCK_e clock	/*  [ IN ] Configurable clock */
    ) {
	volatile uint32_t *pPLLReg = (uint32_t *) 0x0;
	volatile uint32_t *pClockCtrl = (uint32_t *) 0x0;
	volatile uint32_t *pDependentClock = (uint32_t *) 0x0;
	uint32_t vcoFreqPll1Hz = 0;	/* Effective VCO frequency for PLL1 in Hz */
	uint32_t vcoFreqPll2Hz = 0;	/* Effective VCO frequency for PLL2 in Hz */
	uint32_t dependentClockType = 0;
	uint32_t vcoHz = 0;

	/* Get VCO frequencies */
	if ((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASK) != chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_INTEGER) {
		uint64_t adjustFreq = 0;

		vcoFreqPll1Hz = chipcHw_XTAL_FREQ_Hz *
		    chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
		    ((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
		     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT);

		/* Adjusted frequency due to chipcHw_REG_PLL_DIVIDER_NDIV_f_SS */
		adjustFreq = (uint64_t) chipcHw_XTAL_FREQ_Hz *
			(uint64_t) chipcHw_REG_PLL_DIVIDER_NDIV_f_SS *
			chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, (chipcHw_REG_PLL_PREDIVIDER_P2 * (uint64_t) chipcHw_REG_PLL_DIVIDER_FRAC));
		vcoFreqPll1Hz += (uint32_t) adjustFreq;
	} else {
		vcoFreqPll1Hz = chipcHw_XTAL_FREQ_Hz *
		    chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
		    ((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
		     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT);
	}
	vcoFreqPll2Hz =
	    chipcHw_XTAL_FREQ_Hz *
		 chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
	    ((pChipcHw->PLLPreDivider2 & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
	     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT);

	switch (clock) {
	case chipcHw_CLOCK_DDR:
		pPLLReg = &pChipcHw->DDRClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ARM:
		pPLLReg = &pChipcHw->ARMClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ESW:
		pPLLReg = &pChipcHw->ESWClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_VPM:
		pPLLReg = &pChipcHw->VPMClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ESW125:
		pPLLReg = &pChipcHw->ESW125Clock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_UART:
		pPLLReg = &pChipcHw->UARTClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_SDIO0:
		pPLLReg = &pChipcHw->SDIO0Clock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_SDIO1:
		pPLLReg = &pChipcHw->SDIO1Clock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_SPI:
		pPLLReg = &pChipcHw->SPIClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ETM:
		pPLLReg = &pChipcHw->ETMClock;
		vcoHz = vcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_USB:
		pPLLReg = &pChipcHw->USBClock;
		vcoHz = vcoFreqPll2Hz;
		break;
	case chipcHw_CLOCK_LCD:
		pPLLReg = &pChipcHw->LCDClock;
		vcoHz = vcoFreqPll2Hz;
		break;
	case chipcHw_CLOCK_APM:
		pPLLReg = &pChipcHw->APMClock;
		vcoHz = vcoFreqPll2Hz;
		break;
	case chipcHw_CLOCK_BUS:
		pClockCtrl = &pChipcHw->ACLKClock;
		pDependentClock = &pChipcHw->ARMClock;
		vcoHz = vcoFreqPll1Hz;
		dependentClockType = PLL_CLOCK;
		break;
	case chipcHw_CLOCK_OTP:
		pClockCtrl = &pChipcHw->OTPClock;
		break;
	case chipcHw_CLOCK_I2C:
		pClockCtrl = &pChipcHw->I2CClock;
		break;
	case chipcHw_CLOCK_I2S0:
		pClockCtrl = &pChipcHw->I2S0Clock;
		break;
	case chipcHw_CLOCK_RTBUS:
		pClockCtrl = &pChipcHw->RTBUSClock;
		pDependentClock = &pChipcHw->ACLKClock;
		dependentClockType = NON_PLL_CLOCK;
		break;
	case chipcHw_CLOCK_APM100:
		pClockCtrl = &pChipcHw->APM100Clock;
		pDependentClock = &pChipcHw->APMClock;
		vcoHz = vcoFreqPll2Hz;
		dependentClockType = PLL_CLOCK;
		break;
	case chipcHw_CLOCK_TSC:
		pClockCtrl = &pChipcHw->TSCClock;
		break;
	case chipcHw_CLOCK_LED:
		pClockCtrl = &pChipcHw->LEDClock;
		break;
	case chipcHw_CLOCK_I2S1:
		pClockCtrl = &pChipcHw->I2S1Clock;
		break;
	}

	if (pPLLReg) {
		/* Obtain PLL clock frequency */
		if (*pPLLReg & chipcHw_REG_PLL_CLOCK_BYPASS_SELECT) {
			/* Return crystal clock frequency when bypassed */
			return chipcHw_XTAL_FREQ_Hz;
		} else if (clock == chipcHw_CLOCK_DDR) {
			/* DDR frequency is configured in PLLDivider register */
			return chipcHw_divide (vcoHz, (((pChipcHw->PLLDivider & 0xFF000000) >> 24) ? ((pChipcHw->PLLDivider & 0xFF000000) >> 24) : 256));
		} else {
			/* From chip revision number B0, LCD clock is internally divided by 2 */
			if ((pPLLReg == &pChipcHw->LCDClock) && (chipcHw_getChipRevisionNumber() != chipcHw_REV_NUMBER_A0)) {
				vcoHz >>= 1;
			}
			/* Obtain PLL clock frequency using VCO dividers */
			return chipcHw_divide(vcoHz, ((*pPLLReg & chipcHw_REG_PLL_CLOCK_MDIV_MASK) ? (*pPLLReg & chipcHw_REG_PLL_CLOCK_MDIV_MASK) : 256));
		}
	} else if (pClockCtrl) {
		/* Obtain divider clock frequency */
		uint32_t div;
		uint32_t freq = 0;

		if (*pClockCtrl & chipcHw_REG_DIV_CLOCK_BYPASS_SELECT) {
			/* Return crystal clock frequency when bypassed */
			return chipcHw_XTAL_FREQ_Hz;
		} else if (pDependentClock) {
			/* Identify the dependent clock frequency */
			switch (dependentClockType) {
			case PLL_CLOCK:
				if (*pDependentClock & chipcHw_REG_PLL_CLOCK_BYPASS_SELECT) {
					/* Use crystal clock frequency when dependent PLL clock is bypassed */
					freq = chipcHw_XTAL_FREQ_Hz;
				} else {
					/* Obtain PLL clock frequency using VCO dividers */
					div = *pDependentClock & chipcHw_REG_PLL_CLOCK_MDIV_MASK;
					freq = div ? chipcHw_divide(vcoHz, div) : 0;
				}
				break;
			case NON_PLL_CLOCK:
				if (pDependentClock == (uint32_t *) &pChipcHw->ACLKClock) {
					freq = chipcHw_getClockFrequency (chipcHw_CLOCK_BUS);
				} else {
					if (*pDependentClock & chipcHw_REG_DIV_CLOCK_BYPASS_SELECT) {
						/* Use crystal clock frequency when dependent divider clock is bypassed */
						freq = chipcHw_XTAL_FREQ_Hz;
					} else {
						/* Obtain divider clock frequency using XTAL dividers */
						div = *pDependentClock & chipcHw_REG_DIV_CLOCK_DIV_MASK;
						freq = chipcHw_divide (chipcHw_XTAL_FREQ_Hz, (div ? div : 256));
					}
				}
				break;
			}
		} else {
			/* Dependent on crystal clock */
			freq = chipcHw_XTAL_FREQ_Hz;
		}

		div = *pClockCtrl & chipcHw_REG_DIV_CLOCK_DIV_MASK;
		return chipcHw_divide(freq, (div ? div : 256));
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Set clock fequency for miscellaneous configurable clocks
*
*  This function sets clock frequency
*
*  @return  Configured clock frequency in Hz
*
*/
/****************************************************************************/
chipcHw_freq chipcHw_setClockFrequency(chipcHw_CLOCK_e clock,	/*  [ IN ] Configurable clock */
				       uint32_t freq	/*  [ IN ] Clock frequency in Hz */
    ) {
	volatile uint32_t *pPLLReg = (uint32_t *) 0x0;
	volatile uint32_t *pClockCtrl = (uint32_t *) 0x0;
	volatile uint32_t *pDependentClock = (uint32_t *) 0x0;
	uint32_t vcoFreqPll1Hz = 0;	/* Effective VCO frequency for PLL1 in Hz */
	uint32_t desVcoFreqPll1Hz = 0;	/* Desired VCO frequency for PLL1 in Hz */
	uint32_t vcoFreqPll2Hz = 0;	/* Effective VCO frequency for PLL2 in Hz */
	uint32_t dependentClockType = 0;
	uint32_t vcoHz = 0;
	uint32_t desVcoHz = 0;

	/* Get VCO frequencies */
	if ((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_MASK) != chipcHw_REG_PLL_PREDIVIDER_NDIV_MODE_INTEGER) {
		uint64_t adjustFreq = 0;

		vcoFreqPll1Hz = chipcHw_XTAL_FREQ_Hz *
		    chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
		    ((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
		     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT);

		/* Adjusted frequency due to chipcHw_REG_PLL_DIVIDER_NDIV_f_SS */
		adjustFreq = (uint64_t) chipcHw_XTAL_FREQ_Hz *
			(uint64_t) chipcHw_REG_PLL_DIVIDER_NDIV_f_SS *
			chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, (chipcHw_REG_PLL_PREDIVIDER_P2 * (uint64_t) chipcHw_REG_PLL_DIVIDER_FRAC));
		vcoFreqPll1Hz += (uint32_t) adjustFreq;

		/* Desired VCO frequency */
		desVcoFreqPll1Hz = chipcHw_XTAL_FREQ_Hz *
		    chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
		    (((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
		      chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT) + 1);
	} else {
		vcoFreqPll1Hz = desVcoFreqPll1Hz = chipcHw_XTAL_FREQ_Hz *
		    chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
		    ((pChipcHw->PLLPreDivider & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
		     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT);
	}
	vcoFreqPll2Hz = chipcHw_XTAL_FREQ_Hz * chipcHw_divide(chipcHw_REG_PLL_PREDIVIDER_P1, chipcHw_REG_PLL_PREDIVIDER_P2) *
	    ((pChipcHw->PLLPreDivider2 & chipcHw_REG_PLL_PREDIVIDER_NDIV_MASK) >>
	     chipcHw_REG_PLL_PREDIVIDER_NDIV_SHIFT);

	switch (clock) {
	case chipcHw_CLOCK_DDR:
		/* Configure the DDR_ctrl:BUS ratio settings */
		{
			REG_LOCAL_IRQ_SAVE;
			/* Dvide DDR_phy by two to obtain DDR_ctrl clock */
			pChipcHw->DDRClock = (pChipcHw->DDRClock & ~chipcHw_REG_PLL_CLOCK_TO_BUS_RATIO_MASK) | ((((freq / 2) / chipcHw_getClockFrequency(chipcHw_CLOCK_BUS)) - 1)
				<< chipcHw_REG_PLL_CLOCK_TO_BUS_RATIO_SHIFT);
			REG_LOCAL_IRQ_RESTORE;
		}
		pPLLReg = &pChipcHw->DDRClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ARM:
		pPLLReg = &pChipcHw->ARMClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ESW:
		pPLLReg = &pChipcHw->ESWClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_VPM:
		/* Configure the VPM:BUS ratio settings */
		{
			REG_LOCAL_IRQ_SAVE;
			pChipcHw->VPMClock = (pChipcHw->VPMClock & ~chipcHw_REG_PLL_CLOCK_TO_BUS_RATIO_MASK) | ((chipcHw_divide (freq, chipcHw_getClockFrequency(chipcHw_CLOCK_BUS)) - 1)
				<< chipcHw_REG_PLL_CLOCK_TO_BUS_RATIO_SHIFT);
			REG_LOCAL_IRQ_RESTORE;
		}
		pPLLReg = &pChipcHw->VPMClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ESW125:
		pPLLReg = &pChipcHw->ESW125Clock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_UART:
		pPLLReg = &pChipcHw->UARTClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_SDIO0:
		pPLLReg = &pChipcHw->SDIO0Clock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_SDIO1:
		pPLLReg = &pChipcHw->SDIO1Clock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_SPI:
		pPLLReg = &pChipcHw->SPIClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_ETM:
		pPLLReg = &pChipcHw->ETMClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		break;
	case chipcHw_CLOCK_USB:
		pPLLReg = &pChipcHw->USBClock;
		vcoHz = vcoFreqPll2Hz;
		desVcoHz = vcoFreqPll2Hz;
		break;
	case chipcHw_CLOCK_LCD:
		pPLLReg = &pChipcHw->LCDClock;
		vcoHz = vcoFreqPll2Hz;
		desVcoHz = vcoFreqPll2Hz;
		break;
	case chipcHw_CLOCK_APM:
		pPLLReg = &pChipcHw->APMClock;
		vcoHz = vcoFreqPll2Hz;
		desVcoHz = vcoFreqPll2Hz;
		break;
	case chipcHw_CLOCK_BUS:
		pClockCtrl = &pChipcHw->ACLKClock;
		pDependentClock = &pChipcHw->ARMClock;
		vcoHz = vcoFreqPll1Hz;
		desVcoHz = desVcoFreqPll1Hz;
		dependentClockType = PLL_CLOCK;
		break;
	case chipcHw_CLOCK_OTP:
		pClockCtrl = &pChipcHw->OTPClock;
		break;
	case chipcHw_CLOCK_I2C:
		pClockCtrl = &pChipcHw->I2CClock;
		break;
	case chipcHw_CLOCK_I2S0:
		pClockCtrl = &pChipcHw->I2S0Clock;
		break;
	case chipcHw_CLOCK_RTBUS:
		pClockCtrl = &pChipcHw->RTBUSClock;
		pDependentClock = &pChipcHw->ACLKClock;
		dependentClockType = NON_PLL_CLOCK;
		break;
	case chipcHw_CLOCK_APM100:
		pClockCtrl = &pChipcHw->APM100Clock;
		pDependentClock = &pChipcHw->APMClock;
		vcoHz = vcoFreqPll2Hz;
		desVcoHz = vcoFreqPll2Hz;
		dependentClockType = PLL_CLOCK;
		break;
	case chipcHw_CLOCK_TSC:
		pClockCtrl = &pChipcHw->TSCClock;
		break;
	case chipcHw_CLOCK_LED:
		pClockCtrl = &pChipcHw->LEDClock;
		break;
	case chipcHw_CLOCK_I2S1:
		pClockCtrl = &pChipcHw->I2S1Clock;
		break;
	}

	if (pPLLReg) {
		/* Select XTAL as bypass source */
		reg32_modify_and(pPLLReg, ~chipcHw_REG_PLL_CLOCK_SOURCE_GPIO);
		reg32_modify_or(pPLLReg, chipcHw_REG_PLL_CLOCK_BYPASS_SELECT);
		/* For DDR settings use only the PLL divider clock */
		if (pPLLReg == &pChipcHw->DDRClock) {
			/* Set M1DIV for PLL1, which controls the DDR clock */
			reg32_write(&pChipcHw->PLLDivider, (pChipcHw->PLLDivider & 0x00FFFFFF) | ((chipcHw_REG_PLL_DIVIDER_MDIV (desVcoHz, freq)) << 24));
			/* Calculate expected frequency */
			freq = chipcHw_divide(vcoHz, (((pChipcHw->PLLDivider & 0xFF000000) >> 24) ? ((pChipcHw->PLLDivider & 0xFF000000) >> 24) : 256));
		} else {
			/* From chip revision number B0, LCD clock is internally divided by 2 */
			if ((pPLLReg == &pChipcHw->LCDClock) && (chipcHw_getChipRevisionNumber() != chipcHw_REV_NUMBER_A0)) {
				desVcoHz >>= 1;
				vcoHz >>= 1;
			}
			/* Set MDIV to change the frequency */
			reg32_modify_and(pPLLReg, ~(chipcHw_REG_PLL_CLOCK_MDIV_MASK));
			reg32_modify_or(pPLLReg, chipcHw_REG_PLL_DIVIDER_MDIV(desVcoHz, freq));
			/* Calculate expected frequency */
			freq = chipcHw_divide(vcoHz, ((*(pPLLReg) & chipcHw_REG_PLL_CLOCK_MDIV_MASK) ? (*(pPLLReg) & chipcHw_REG_PLL_CLOCK_MDIV_MASK) : 256));
		}
		/* Wait for for atleast 200ns as per the protocol to change frequency */
		udelay(1);
		/* Do not bypass */
		reg32_modify_and(pPLLReg, ~chipcHw_REG_PLL_CLOCK_BYPASS_SELECT);
		/* Return the configured frequency */
		return freq;
	} else if (pClockCtrl) {
		uint32_t divider = 0;

		/* Divider clock should not be bypassed  */
		reg32_modify_and(pClockCtrl,
				 ~chipcHw_REG_DIV_CLOCK_BYPASS_SELECT);

		/* Identify the clock source */
		if (pDependentClock) {
			switch (dependentClockType) {
			case PLL_CLOCK:
				divider = chipcHw_divide(chipcHw_divide (desVcoHz, (*pDependentClock & chipcHw_REG_PLL_CLOCK_MDIV_MASK)), freq);
				break;
			case NON_PLL_CLOCK:
				{
					uint32_t sourceClock = 0;

					if (pDependentClock == (uint32_t *) &pChipcHw->ACLKClock) {
						sourceClock = chipcHw_getClockFrequency (chipcHw_CLOCK_BUS);
					} else {
						uint32_t div = *pDependentClock & chipcHw_REG_DIV_CLOCK_DIV_MASK;
						sourceClock = chipcHw_divide (chipcHw_XTAL_FREQ_Hz, ((div) ? div : 256));
					}
					divider = chipcHw_divide(sourceClock, freq);
				}
				break;
			}
		} else {
			divider = chipcHw_divide(chipcHw_XTAL_FREQ_Hz, freq);
		}

		if (divider) {
			REG_LOCAL_IRQ_SAVE;
			/* Set the divider to obtain the required frequency */
			*pClockCtrl = (*pClockCtrl & (~chipcHw_REG_DIV_CLOCK_DIV_MASK)) | (((divider > 256) ? chipcHw_REG_DIV_CLOCK_DIV_256 : divider) & chipcHw_REG_DIV_CLOCK_DIV_MASK);
			REG_LOCAL_IRQ_RESTORE;
			return freq;
		}
	}

	return 0;
}

EXPORT_SYMBOL(chipcHw_setClockFrequency);

/****************************************************************************/
/**
*  @brief   Set VPM clock in sync with BUS clock for Chip Rev #A0
*
*  This function does the phase adjustment between VPM and BUS clock
*
*  @return >= 0 : On success (# of adjustment required)
*            -1 : On failure
*
*/
/****************************************************************************/
static int vpmPhaseAlignA0(void)
{
	uint32_t phaseControl;
	uint32_t phaseValue;
	uint32_t prevPhaseComp;
	int iter = 0;
	int adjustCount = 0;
	int count = 0;

	for (iter = 0; (iter < MAX_PHASE_ALIGN_ATTEMPTS) && (adjustCount < MAX_PHASE_ADJUST_COUNT); iter++) {
		phaseControl = (pChipcHw->VPMClock & chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK) >> chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT;
		phaseValue = 0;
		prevPhaseComp = 0;

		/* Step 1: Look for falling PH_COMP transition */

		/* Read the contents of VPM Clock resgister */
		phaseValue = pChipcHw->VPMClock;
		do {
			/* Store previous value of phase comparator */
			prevPhaseComp = phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP;
			/* Change the value of PH_CTRL. */
			reg32_write(&pChipcHw->VPMClock, (pChipcHw->VPMClock & (~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK)) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT));
			/* Wait atleast 20 ns */
			udelay(1);
			/* Toggle the LOAD_CH after phase control is written. */
			pChipcHw->VPMClock ^= chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;
			/* Read the contents of  VPM Clock resgister. */
			phaseValue = pChipcHw->VPMClock;

			if ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) == 0x0) {
				phaseControl = (0x3F & (phaseControl - 1));
			} else {
				/* Increment to the Phase count value for next write, if Phase is not stable. */
				phaseControl = (0x3F & (phaseControl + 1));
			}
			/* Count number of adjustment made */
			adjustCount++;
		} while (((prevPhaseComp == (phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP)) ||	/* Look for a transition */
			  ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) != 0x0)) &&	/* Look for a falling edge */
			 (adjustCount < MAX_PHASE_ADJUST_COUNT)	/* Do not exceed the limit while trying */
		    );

		if (adjustCount >= MAX_PHASE_ADJUST_COUNT) {
			/* Failed to align VPM phase after MAX_PHASE_ADJUST_COUNT tries */
			return -1;
		}

		/* Step 2: Keep moving forward to make sure falling PH_COMP transition was valid */

		for (count = 0; (count < 5) && ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) == 0); count++) {
			phaseControl = (0x3F & (phaseControl + 1));
			reg32_write(&pChipcHw->VPMClock, (pChipcHw->VPMClock & (~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK)) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT));
			/* Wait atleast 20 ns */
			udelay(1);
			/* Toggle the LOAD_CH after phase control is written. */
			pChipcHw->VPMClock ^= chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;
			phaseValue = pChipcHw->VPMClock;
			/* Count number of adjustment made */
			adjustCount++;
		}

		if (adjustCount >= MAX_PHASE_ADJUST_COUNT) {
			/* Failed to align VPM phase after MAX_PHASE_ADJUST_COUNT tries */
			return -1;
		}

		if (count != 5) {
			/* Detected false transition */
			continue;
		}

		/* Step 3: Keep moving backward to make sure falling PH_COMP transition was stable */

		for (count = 0; (count < 3) && ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) == 0); count++) {
			phaseControl = (0x3F & (phaseControl - 1));
			reg32_write(&pChipcHw->VPMClock, (pChipcHw->VPMClock & (~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK)) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT));
			/* Wait atleast 20 ns */
			udelay(1);
			/* Toggle the LOAD_CH after phase control is written. */
			pChipcHw->VPMClock ^= chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;
			phaseValue = pChipcHw->VPMClock;
			/* Count number of adjustment made */
			adjustCount++;
		}

		if (adjustCount >= MAX_PHASE_ADJUST_COUNT) {
			/* Failed to align VPM phase after MAX_PHASE_ADJUST_COUNT tries */
			return -1;
		}

		if (count != 3) {
			/* Detected noisy transition */
			continue;
		}

		/* Step 4: Keep moving backward before the original transition took place. */

		for (count = 0; (count < 5); count++) {
			phaseControl = (0x3F & (phaseControl - 1));
			reg32_write(&pChipcHw->VPMClock, (pChipcHw->VPMClock & (~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK)) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT));
			/* Wait atleast 20 ns */
			udelay(1);
			/* Toggle the LOAD_CH after phase control is written. */
			pChipcHw->VPMClock ^= chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;
			phaseValue = pChipcHw->VPMClock;
			/* Count number of adjustment made */
			adjustCount++;
		}

		if (adjustCount >= MAX_PHASE_ADJUST_COUNT) {
			/* Failed to align VPM phase after MAX_PHASE_ADJUST_COUNT tries */
			return -1;
		}

		if ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) == 0) {
			/* Detected false transition */
			continue;
		}

		/* Step 5: Re discover the valid transition */

		do {
			/* Store previous value of phase comparator */
			prevPhaseComp = phaseValue;
			/* Change the value of PH_CTRL. */
			reg32_write(&pChipcHw->VPMClock, (pChipcHw->VPMClock & (~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK)) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT));
			/* Wait atleast 20 ns */
			udelay(1);
			/* Toggle the LOAD_CH after phase control is written. */
			pChipcHw->VPMClock ^=
			    chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;
			/* Read the contents of  VPM Clock resgister. */
			phaseValue = pChipcHw->VPMClock;

			if ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) == 0x0) {
				phaseControl = (0x3F & (phaseControl - 1));
			} else {
				/* Increment to the Phase count value for next write, if Phase is not stable. */
				phaseControl = (0x3F & (phaseControl + 1));
			}

			/* Count number of adjustment made */
			adjustCount++;
		} while (((prevPhaseComp == (phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP)) || ((phaseValue & chipcHw_REG_PLL_CLOCK_PHASE_COMP) != 0x0)) && (adjustCount < MAX_PHASE_ADJUST_COUNT));

		if (adjustCount >= MAX_PHASE_ADJUST_COUNT) {
			/* Failed to align VPM phase after MAX_PHASE_ADJUST_COUNT tries  */
			return -1;
		} else {
			/* Valid phase must have detected */
			break;
		}
	}

	/* For VPM Phase should be perfectly aligned. */
	phaseControl = (((pChipcHw->VPMClock >> chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT) - 1) & 0x3F);
	{
		REG_LOCAL_IRQ_SAVE;

		pChipcHw->VPMClock = (pChipcHw->VPMClock & ~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT);
		/* Load new phase value */
		pChipcHw->VPMClock ^= chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;

		REG_LOCAL_IRQ_RESTORE;
	}
	/* Return the status */
	return (int)adjustCount;
}

/****************************************************************************/
/**
*  @brief   Set VPM clock in sync with BUS clock
*
*  This function does the phase adjustment between VPM and BUS clock
*
*  @return >= 0 : On success (# of adjustment required)
*            -1 : On failure
*
*/
/****************************************************************************/
int chipcHw_vpmPhaseAlign(void)
{

	if (chipcHw_getChipRevisionNumber() == chipcHw_REV_NUMBER_A0) {
		return vpmPhaseAlignA0();
	} else {
		uint32_t phaseControl = chipcHw_getVpmPhaseControl();
		uint32_t phaseValue = 0;
		int adjustCount = 0;

		/* Disable VPM access */
		pChipcHw->Spare1 &= ~chipcHw_REG_SPARE1_VPM_BUS_ACCESS_ENABLE;
		/* Disable HW VPM phase alignment  */
		chipcHw_vpmHwPhaseAlignDisable();
		/* Enable SW VPM phase alignment  */
		chipcHw_vpmSwPhaseAlignEnable();
		/* Adjust VPM phase */
		while (adjustCount < MAX_PHASE_ADJUST_COUNT) {
			phaseValue = chipcHw_getVpmHwPhaseAlignStatus();

			/* Adjust phase control value */
			if (phaseValue > 0xF) {
				/* Increment phase control value */
				phaseControl++;
			} else if (phaseValue < 0xF) {
				/* Decrement phase control value */
				phaseControl--;
			} else {
				/* Enable VPM access */
				pChipcHw->Spare1 |= chipcHw_REG_SPARE1_VPM_BUS_ACCESS_ENABLE;
				/* Return adjust count */
				return adjustCount;
			}
			/* Change the value of PH_CTRL. */
			reg32_write(&pChipcHw->VPMClock, (pChipcHw->VPMClock & (~chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_MASK)) | (phaseControl << chipcHw_REG_PLL_CLOCK_PHASE_CONTROL_SHIFT));
			/* Wait atleast 20 ns */
			udelay(1);
			/* Toggle the LOAD_CH after phase control is written. */
			pChipcHw->VPMClock ^= chipcHw_REG_PLL_CLOCK_PHASE_UPDATE_ENABLE;
			/* Count adjustment */
			adjustCount++;
		}
	}

	/* Disable VPM access */
	pChipcHw->Spare1 &= ~chipcHw_REG_SPARE1_VPM_BUS_ACCESS_ENABLE;
	return -1;
}

/****************************************************************************/
/**
*  @brief   Local Divide function
*
*  This function does the divide
*
*  @return divide value
*
*/
/****************************************************************************/
static int chipcHw_divide(int num, int denom)
{
	int r;
	int t = 1;

	/* Shift denom and t up to the largest value to optimize algorithm */
	/* t contains the units of each divide */
	while ((denom & 0x40000000) == 0) {	/* fails if denom=0 */
		denom = denom << 1;
		t = t << 1;
	}

	/* Initialize the result */
	r = 0;

	do {
		/* Determine if there exists a positive remainder */
		if ((num - denom) >= 0) {
			/* Accumlate t to the result and calculate a new remainder */
			num = num - denom;
			r = r + t;
		}
		/* Continue to shift denom and shift t down to 0 */
		denom = denom >> 1;
		t = t >> 1;
	} while (t != 0);

	return r;
}
