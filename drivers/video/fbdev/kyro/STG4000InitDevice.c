/*
 *  linux/drivers/video/kyro/STG4000InitDevice.c
 *
 *  Copyright (C) 2000 Imagination Technologies Ltd
 *  Copyright (C) 2002 STMicroelectronics
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "STG4000Reg.h"
#include "STG4000Interface.h"

/* SDRAM fixed settings */
#define SDRAM_CFG_0   0x49A1
#define SDRAM_CFG_1   0xA732
#define SDRAM_CFG_2   0x31
#define SDRAM_ARB_CFG 0xA0
#define SDRAM_REFRESH 0x20

/* Reset values */
#define PMX2_SOFTRESET_DAC_RST		0x0001
#define PMX2_SOFTRESET_C1_RST		0x0004
#define PMX2_SOFTRESET_C2_RST		0x0008
#define PMX2_SOFTRESET_3D_RST		0x0010
#define PMX2_SOFTRESET_VIDIN_RST	0x0020
#define PMX2_SOFTRESET_TLB_RST		0x0040
#define PMX2_SOFTRESET_SD_RST		0x0080
#define PMX2_SOFTRESET_VGA_RST		0x0100
#define PMX2_SOFTRESET_ROM_RST		0x0200	/* reserved bit, do not reset */
#define PMX2_SOFTRESET_TA_RST		0x0400
#define PMX2_SOFTRESET_REG_RST		0x4000
#define PMX2_SOFTRESET_ALL		0x7fff

/* Core clock freq */
#define CORE_PLL_FREQ 1000000

/* Reference Clock freq */
#define REF_FREQ 14318

/* PCI Registers */
static u16 CorePllControl = 0x70;

#define	PCI_CONFIG_SUBSYS_ID	0x2e

/* Misc */
#define CORE_PLL_MODE_REG_0_7      3
#define CORE_PLL_MODE_REG_8_15     2
#define CORE_PLL_MODE_CONFIG_REG   1
#define DAC_PLL_CONFIG_REG         0

#define STG_MAX_VCO 500000
#define STG_MIN_VCO 100000

/* PLL Clock */
#define    STG4K3_PLL_SCALER      8	/* scale numbers by 2^8 for fixed point calc */
#define    STG4K3_PLL_MIN_R       2	/* Minimum multiplier */
#define    STG4K3_PLL_MAX_R       33	/* Max */
#define    STG4K3_PLL_MIN_F       2	/* Minimum divisor */
#define    STG4K3_PLL_MAX_F       513	/* Max */
#define    STG4K3_PLL_MIN_OD      0	/* Min output divider (shift) */
#define    STG4K3_PLL_MAX_OD      2	/* Max */
#define    STG4K3_PLL_MIN_VCO_SC  (100000000 >> STG4K3_PLL_SCALER)	/* Min VCO rate */
#define    STG4K3_PLL_MAX_VCO_SC  (500000000 >> STG4K3_PLL_SCALER)	/* Max VCO rate */
#define    STG4K3_PLL_MINR_VCO_SC (100000000 >> STG4K3_PLL_SCALER)	/* Min VCO rate (restricted) */
#define    STG4K3_PLL_MAXR_VCO_SC (500000000 >> STG4K3_PLL_SCALER)	/* Max VCO rate (restricted) */
#define    STG4K3_PLL_MINR_VCO    100000000	/* Min VCO rate (restricted) */
#define    STG4K3_PLL_MAX_VCO     500000000	/* Max VCO rate */
#define    STG4K3_PLL_MAXR_VCO    500000000	/* Max VCO rate (restricted) */

#define OS_DELAY(X) \
{ \
volatile u32 i,count=0; \
    for(i=0;i<X;i++) count++; \
}

static u32 InitSDRAMRegisters(volatile STG4000REG __iomem *pSTGReg,
			      u32 dwSubSysID, u32 dwRevID)
{
	u32 adwSDRAMArgCfg0[] = { 0xa0, 0x80, 0xa0, 0xa0, 0xa0 };
	u32 adwSDRAMCfg1[] = { 0x8732, 0x8732, 0xa732, 0xa732, 0x8732 };
	u32 adwSDRAMCfg2[] = { 0x87d2, 0x87d2, 0xa7d2, 0x87d2, 0xa7d2 };
	u32 adwSDRAMRsh[] = { 36, 39, 40 };
	u32 adwChipSpeed[] = { 110, 120, 125 };
	u32 dwMemTypeIdx;
	u32 dwChipSpeedIdx;

	/* Get memory tpye and chip speed indexs from the SubSysDevID */
	dwMemTypeIdx = (dwSubSysID & 0x70) >> 4;
	dwChipSpeedIdx = (dwSubSysID & 0x180) >> 7;

	if (dwMemTypeIdx > 4 || dwChipSpeedIdx > 2)
		return 0;

	/* Program SD-RAM interface */
	STG_WRITE_REG(SDRAMArbiterConf, adwSDRAMArgCfg0[dwMemTypeIdx]);
	if (dwRevID < 5) {
		STG_WRITE_REG(SDRAMConf0, 0x49A1);
		STG_WRITE_REG(SDRAMConf1, adwSDRAMCfg1[dwMemTypeIdx]);
	} else {
		STG_WRITE_REG(SDRAMConf0, 0x4DF1);
		STG_WRITE_REG(SDRAMConf1, adwSDRAMCfg2[dwMemTypeIdx]);
	}

	STG_WRITE_REG(SDRAMConf2, 0x31);
	STG_WRITE_REG(SDRAMRefresh, adwSDRAMRsh[dwChipSpeedIdx]);

	return adwChipSpeed[dwChipSpeedIdx] * 10000;
}

u32 ProgramClock(u32 refClock,
		   u32 coreClock,
		   u32 * FOut, u32 * ROut, u32 * POut)
{
	u32 R = 0, F = 0, OD = 0, ODIndex = 0;
	u32 ulBestR = 0, ulBestF = 0, ulBestOD = 0;
	u32 ulBestVCO = 0, ulBestClk = 0, ulBestScore = 0;
	u32 ulScore, ulPhaseScore, ulVcoScore;
	u32 ulTmp = 0, ulVCO;
	u32 ulScaleClockReq, ulMinClock, ulMaxClock;
	u32 ODValues[] = { 1, 2, 0 };

	/* Translate clock in Hz */
	coreClock *= 100;	/* in Hz */
	refClock *= 1000;	/* in Hz */

	/* Work out acceptable clock
	 * The method calculates ~ +- 0.4% (1/256)
	 */
	ulMinClock = coreClock - (coreClock >> 8);
	ulMaxClock = coreClock + (coreClock >> 8);

	/* Scale clock required for use in calculations */
	ulScaleClockReq = coreClock >> STG4K3_PLL_SCALER;

	/* Iterate through post divider values */
	for (ODIndex = 0; ODIndex < 3; ODIndex++) {
		OD = ODValues[ODIndex];
		R = STG4K3_PLL_MIN_R;

		/* loop for pre-divider from min to max  */
		while (R <= STG4K3_PLL_MAX_R) {
			/* estimate required feedback multiplier */
			ulTmp = R * (ulScaleClockReq << OD);

			/* F = ClkRequired * R * (2^OD) / Fref */
			F = (u32)(ulTmp / (refClock >> STG4K3_PLL_SCALER));

			/* compensate for accuracy */
			if (F > STG4K3_PLL_MIN_F)
				F--;


			/*
			 * We should be close to our target frequency (if it's
			 * achievable with current OD & R) let's iterate
			 * through F for best fit
			 */
			while ((F >= STG4K3_PLL_MIN_F) &&
			       (F <= STG4K3_PLL_MAX_F)) {
				/* Calc VCO at full accuracy */
				ulVCO = refClock / R;
				ulVCO = F * ulVCO;

				/*
				 * Check it's within restricted VCO range
				 * unless of course the desired frequency is
				 * above the restricted range, then test
				 * against VCO limit
				 */
				if ((ulVCO >= STG4K3_PLL_MINR_VCO) &&
				    ((ulVCO <= STG4K3_PLL_MAXR_VCO) ||
				     ((coreClock > STG4K3_PLL_MAXR_VCO)
				      && (ulVCO <= STG4K3_PLL_MAX_VCO)))) {
					ulTmp = (ulVCO >> OD);	/* Clock = VCO / (2^OD) */

					/* Is this clock good enough? */
					if ((ulTmp >= ulMinClock)
					    && (ulTmp <= ulMaxClock)) {
						ulPhaseScore = (((refClock / R) - (refClock / STG4K3_PLL_MAX_R))) / ((refClock - (refClock / STG4K3_PLL_MAX_R)) >> 10);

						ulVcoScore = ((ulVCO - STG4K3_PLL_MINR_VCO)) / ((STG4K3_PLL_MAXR_VCO - STG4K3_PLL_MINR_VCO) >> 10);
						ulScore = ulPhaseScore + ulVcoScore;

						if (!ulBestScore) {
							ulBestVCO = ulVCO;
							ulBestOD = OD;
							ulBestF = F;
							ulBestR = R;
							ulBestClk = ulTmp;
							ulBestScore =
							    ulScore;
						}
						/* is this better, ( aim for highest Score) */
			/*--------------------------------------------------------------------------
                             Here we want to use a scoring system which will take account of both the
                            value at the phase comparater and the VCO output
                             to do this we will use a cumulative score between the two
                          The way this ends up is that we choose the first value in the loop anyway
                          but we shall keep this code in case new restrictions come into play
                          --------------------------------------------------------------------------*/
						if ((ulScore >= ulBestScore) && (OD > 0)) {
							ulBestVCO = ulVCO;
							ulBestOD = OD;
							ulBestF = F;
							ulBestR = R;
							ulBestClk = ulTmp;
							ulBestScore =
							    ulScore;
						}
					}
				}
				F++;
			}
			R++;
		}
	}

	/*
	   did we find anything?
	   Then return RFOD
	 */
	if (ulBestScore) {
		*ROut = ulBestR;
		*FOut = ulBestF;

		if ((ulBestOD == 2) || (ulBestOD == 3)) {
			*POut = 3;
		} else
			*POut = ulBestOD;

	}

	return (ulBestClk);
}

int SetCoreClockPLL(volatile STG4000REG __iomem *pSTGReg, struct pci_dev *pDev)
{
	u32 F, R, P;
	u16 core_pll = 0, sub;
	u32 ulCoreClock;
	u32 tmp;
	u32 ulChipSpeed;

	STG_WRITE_REG(IntMask, 0xFFFF);

	/* Disable Primary Core Thread0 */
	tmp = STG_READ_REG(Thread0Enable);
	CLEAR_BIT(0);
	STG_WRITE_REG(Thread0Enable, tmp);

	/* Disable Primary Core Thread1 */
	tmp = STG_READ_REG(Thread1Enable);
	CLEAR_BIT(0);
	STG_WRITE_REG(Thread1Enable, tmp);

	STG_WRITE_REG(SoftwareReset,
		      PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_ROM_RST);
	STG_WRITE_REG(SoftwareReset,
		      PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_TA_RST |
		      PMX2_SOFTRESET_ROM_RST);

	/* Need to play around to reset TA */
	STG_WRITE_REG(TAConfiguration, 0);
	STG_WRITE_REG(SoftwareReset,
		      PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_ROM_RST);
	STG_WRITE_REG(SoftwareReset,
		      PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_TA_RST |
		      PMX2_SOFTRESET_ROM_RST);

	pci_read_config_word(pDev, PCI_CONFIG_SUBSYS_ID, &sub);

	ulChipSpeed = InitSDRAMRegisters(pSTGReg, (u32)sub,
		                         (u32)pDev->revision);

	if (ulChipSpeed == 0)
		return -EINVAL;

	ulCoreClock = ProgramClock(REF_FREQ, CORE_PLL_FREQ, &F, &R, &P);

	core_pll |= ((P) | ((F - 2) << 2) | ((R - 2) << 11));

	/* Set Core PLL Control to Core PLL Mode  */

	/* Send bits 0:7 of the Core PLL Mode register */
	tmp = ((CORE_PLL_MODE_REG_0_7 << 8) | (core_pll & 0x00FF));
	pci_write_config_word(pDev, CorePllControl, tmp);
	/* Without some delay between the PCI config writes the clock does
	   not reliably set when the code is compiled -O3
	 */
	OS_DELAY(1000000);

	tmp |= SET_BIT(14);
	pci_write_config_word(pDev, CorePllControl, tmp);
	OS_DELAY(1000000);

	/* Send bits 8:15 of the Core PLL Mode register */
	tmp =
	    ((CORE_PLL_MODE_REG_8_15 << 8) | ((core_pll & 0xFF00) >> 8));
	pci_write_config_word(pDev, CorePllControl, tmp);
	OS_DELAY(1000000);

	tmp |= SET_BIT(14);
	pci_write_config_word(pDev, CorePllControl, tmp);
	OS_DELAY(1000000);

	STG_WRITE_REG(SoftwareReset, PMX2_SOFTRESET_ALL);

#if 0
	/* Enable Primary Core Thread0 */
	tmp = ((STG_READ_REG(Thread0Enable)) | SET_BIT(0));
	STG_WRITE_REG(Thread0Enable, tmp);

	/* Enable Primary Core Thread1 */
	tmp = ((STG_READ_REG(Thread1Enable)) | SET_BIT(0));
	STG_WRITE_REG(Thread1Enable, tmp);
#endif

	return 0;
}
