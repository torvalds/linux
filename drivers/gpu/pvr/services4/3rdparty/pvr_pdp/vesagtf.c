/*************************************************************************/ /*!
@Title          PVRPDP VESA driver functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.
 
This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS 
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR 
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  
*/ /**************************************************************************/

#if !defined(__linux__)
#include <math.h>
#else
#include "linux/kernel.h"
#include "asm/string.h"
#endif

/* IMG services headers */
#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"

#include "pvrpdp.h"
#include "vesagtf.h"
#include "tcfdefs.h"

#include "pvr_debug.h"

typedef struct _SCB_WRITE_BLK
{
	unsigned long ulPreCmnd;
	unsigned long ulDataAddr;
	unsigned long ulData;
} SCB_WRITE_BLK, *PSCB_WRITE_BLK;


typedef struct _COEFF_STRUCT
{
	unsigned long PreDivider;
	unsigned long Multiplier;
	unsigned long PostDivider;
	unsigned long VCO;
} COEFF_STRUCT, *PCOEFF_STRUCT;


#define	PLL_ADDR					0x6A

#define	SRCBLOCK1					0x34
#define	SRCBLOCK2					0x35

/* General Control settings */
#define SCBHOLD						0x00
#define SCBRESET					0xFF

#define SCBSTATUS_OK				PDP_FALSE
#define SCBSTATUS_ERROR				PDP_TRUE

static const unsigned long aD_config[3][4] = {{0x10, 0x11, 0x12, 0x13},
											  {0x28, 0x29, 0x2A, 0x2B},
											  {0x40, 0x41, 0x42, 0x43}};

static const unsigned long aN_config_lower[3][4] = {{0x14, 0x15, 0x16, 0x17},
													{0x2C, 0x2D, 0x2E, 0x2F},
													{0x44, 0x45, 0x46, 0x47}};

static const unsigned long aN_config_upper[3][4] = {{0x18, 0x19, 0x1A, 0x1B},
													{0x30, 0x31, 0x32, 0x33},
													{0x48, 0x49, 0x4A, 0x4B}};

static const unsigned long aRZ_Config[3][4] = {{0x08, 0x09, 0x0A, 0x0B},
											   {0x20, 0x21, 0x22, 0x23},
											   {0x38, 0x39, 0x3A, 0x3B}};

static const unsigned long aCPCZ_Config[3][4] = {{0x0C, 0x0D, 0x0E, 0x0F},
												 {0x24, 0x25, 0x26, 0x27},
												 {0x3C, 0x3D, 0x3E, 0x3F}};

static const unsigned long aOutConf[7] = {0x00, 0x00, 0x4D, 0x51, 0x55, 0x59, 0x5D};

static const unsigned long aPLL2VAL[3] = {1, 2, 3};

#define DIVBY1			0x11
#define DIVBY2			0x22
#define DIVBY3			0x33


void InitSCB(PVRPDP_DEVINFO *psDevInfo)
{
	unsigned long ulResetState;
	
	ulResetState = ReadTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL);
	WriteTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL, ulResetState | TCF_CR_CLK_AND_RST_CTRL_SCB_RESETN_MASK);
	
	WriteTCFReg(psDevInfo, TCF_CR_SCB_GENERAL_CONTROL, SCBHOLD);
	WriteTCFReg(psDevInfo, TCF_CR_SCB_GENERAL_CONTROL, SCBRESET);
}


void UnInitSCB(PVRPDP_DEVINFO *psDevInfo)
{
	unsigned long ulResetState;
	
	ulResetState = ReadTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL);
	WriteTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL, ulResetState & ~TCF_CR_CLK_AND_RST_CTRL_SCB_RESETN_MASK);
	
	WriteTCFReg(psDevInfo, TCF_CR_SCB_GENERAL_CONTROL, SCBHOLD);
}


void ResetPDP1(PVRPDP_DEVINFO *psDevInfo)
{
	unsigned long ulResetState;
	
	ulResetState = ReadTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL);
	WriteTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL, ulResetState & ~TCF_CR_CLK_AND_RST_CTRL_PDP1_RESETN_MASK);
	PDPWaitus(100);
	WriteTCFReg(psDevInfo, TCF_CR_CLK_AND_RST_CTRL, ulResetState | TCF_CR_CLK_AND_RST_CTRL_PDP1_RESETN_MASK);
}


void SelectBus(PVRPDP_DEVINFO *psDevInfo, unsigned long ulBus)
{
	if (ulBus == 0)
	{
		WriteTCFReg(psDevInfo, TCF_CR_SCB_BUS_SELECT, 0);
	}
	else if (ulBus == 1)
	{
		WriteTCFReg(psDevInfo, TCF_CR_SCB_BUS_SELECT, 1);
	}
	else if (ulBus == 2)
	{
		WriteTCFReg(psDevInfo, TCF_CR_SCB_BUS_SELECT, 2);
	}
}


void SetSCBAddress (PVRPDP_DEVINFO *psDevInfo, unsigned long i2cAddr)
{
	WriteTCFReg(psDevInfo, TCF_CR_SCB_MASTER_WRITE_ADDRESS, i2cAddr);
}


PDP_BOOL WriteManyBytes (PVRPDP_DEVINFO *psDevInfo, unsigned long i2cAddr, unsigned long ulHowMany, unsigned long *paWriteBlk)
{
	unsigned long ulIndex;
	unsigned long ulClock;
	
	ulClock = PDPClockus() + 1000000;
	do
	{
		PDPReleaseThreadQuanta();
		if (PDPClockus() > ulClock)
		{
			return (SCBSTATUS_ERROR);
		}
	} while ((ReadTCFReg(psDevInfo, TCF_CR_SCB_MASTER_FILL_STATUS) & TCF_CR_SCB_MASTER_FILL_STATUS_MASTER_WRITE_FIFO_EMPTY_SHIFT) == 0);
	
	SetSCBAddress(psDevInfo, i2cAddr);
	
	WriteTCFReg(psDevInfo, TCF_CR_SCB_MASTER_WRITE_COUNT, ulHowMany);
	
	/* must have a delay here. A read, too soon, lockup up the scb */
	PDPWaitus(1000);
	
	for (ulIndex = 0; ulIndex < ulHowMany; ulIndex++)
	{
		ulClock = PDPClockus() + 1000000;
		do
		{
			PDPReleaseThreadQuanta();
			if (PDPClockus() > ulClock)
			{
				return (SCBSTATUS_ERROR);
			}
		} while ((ReadTCFReg(psDevInfo, TCF_CR_SCB_MASTER_FILL_STATUS) & TCF_CR_SCB_MASTER_FILL_STATUS_MASTER_WRITE_FIFO_FULL_MASK) != 0);
		
		WriteTCFReg(psDevInfo, TCF_CR_SCB_MASTER_WRITE_DATA, paWriteBlk[ulIndex]);
		/* added a delay here to ensure no reads follow too soon, otherwise a scb lockup might occur */
		PDPWaitus(1000);
	}
	
	return (SCBSTATUS_OK);
}


PDP_BOOL ReadManyBytes(PVRPDP_DEVINFO *psDevInfo, unsigned long ulHowMany, unsigned long *paReadBlk)
{
	unsigned long ulIndex;
	unsigned long ulClock;
	
	ulClock = PDPClockus() + 1000000;
	do
	{
		PDPReleaseThreadQuanta();
		if (PDPClockus() > ulClock)
		{
			return (SCBSTATUS_ERROR);
		}
	} while ((ReadTCFReg(psDevInfo, TCF_CR_SCB_MASTER_FILL_STATUS) & TCF_CR_SCB_MASTER_FILL_STATUS_MASTER_WRITE_FIFO_EMPTY_MASK) == 0);
	
	WriteTCFReg(psDevInfo, TCF_CR_SCB_MASTER_READ_COUNT, ulHowMany);
	/* must have a delay here. A read, too soon, lockup up the scb */
	PDPWaitus(1000);
	
	for (ulIndex = 0; ulIndex < ulHowMany; ulIndex++)
	{
		ulClock = PDPClockus() + 1000000;
		do
		{
			PDPReleaseThreadQuanta();
			if (PDPClockus() > ulClock)
			{
				return (SCBSTATUS_ERROR);
			}
		} while ((ReadTCFReg(psDevInfo, TCF_CR_SCB_MASTER_FILL_STATUS) & TCF_CR_SCB_MASTER_FILL_STATUS_MASTER_READ_FIFO_EMPTY_MASK) != 0);
		
		paReadBlk[ulIndex] = ReadTCFReg(psDevInfo, TCF_CR_SCB_MASTER_READ_DATA);
		/* added a delay here to ensure no reads follow too soon, otherwise a scb lockup might occur */
		PDPWaitus(1000);
	}
	
	return (SCBSTATUS_OK);
}


PDP_BOOL ProgRead (PVRPDP_DEVINFO *psDevInfo, unsigned long uiDataAddr, unsigned long *puiData)
{
	unsigned long sGlobalData[2];
	unsigned long sToRead[2];
	
	memset(&sGlobalData, 0, 2);
	
	sToRead[0] = 0;
	sToRead[1] = uiDataAddr;
	
	WriteManyBytes(psDevInfo, PLL_ADDR, 2, (unsigned long *)&sToRead);
	ReadManyBytes(psDevInfo, 2, (unsigned long *)&sGlobalData);
	
	WriteManyBytes(psDevInfo, PLL_ADDR, 2, (unsigned long *)&sToRead);
	ReadManyBytes(psDevInfo, 2, (unsigned long *)&sGlobalData);
	
	*puiData = sGlobalData[0];
	
	return (SCBSTATUS_OK);
}


PDP_BOOL ProgWrite(PVRPDP_DEVINFO *psDevInfo, unsigned long uiDataAddr, unsigned long uiData)
{
	unsigned long sDataToSend[3];
	unsigned long uiRtnData;
	PDP_BOOL bRtnStatus = SCBSTATUS_ERROR;
	
	sDataToSend[0] = 0;
	sDataToSend[1] = uiDataAddr;
	sDataToSend[2] = uiData;
	
	if ( WriteManyBytes(psDevInfo, PLL_ADDR, 3, (unsigned long *)&sDataToSend) == SCBSTATUS_OK)
	{
		ProgRead (psDevInfo, uiDataAddr, &uiRtnData);
		if (uiRtnData == uiData)
		{
			bRtnStatus = SCBSTATUS_OK;
		}
	}
	
	return (bRtnStatus);
}


void SetPLLtoOut(PVRPDP_DEVINFO *psDevInfo, unsigned long uiPLL, unsigned long uiOutput)
{
	unsigned long uiPllVal = aPLL2VAL[uiPLL];
	unsigned long uiToKeep, uiToWrite;
	unsigned long uiShift;
	
	if (uiOutput < 3)
	{
		ProgRead(psDevInfo, SRCBLOCK1, &uiToKeep);
		uiShift = (uiOutput - 2 + 3) * 2;
		uiToWrite =(uiPllVal << uiShift) | (uiToKeep & (0xFF ^ (0x03 << uiShift)));
		ProgWrite(psDevInfo, SRCBLOCK1, uiToWrite);
	}
	else
	{
		ProgRead(psDevInfo, SRCBLOCK2, &uiToKeep);
		uiShift = (uiOutput - 3) * 2;
		uiToWrite =(uiPllVal << uiShift) | (uiToKeep & (0xFF ^ (0x03 << uiShift)));
		ProgWrite(psDevInfo, SRCBLOCK2, uiToWrite);
	}
}


void SetOutputDiv(PVRPDP_DEVINFO *psDevInfo, unsigned long uiOutput, unsigned long uiValue)
{
	if (uiValue == 1)
	{
		ProgWrite(psDevInfo, aOutConf[uiOutput], DIVBY1);
	}
	else if (uiValue == 2)
	{
		ProgWrite(psDevInfo, aOutConf[uiOutput], DIVBY2);
	}
	else
	{
		unsigned long uiLowerVal = (uiValue - 2) & 0x03;
		unsigned long uiUpperVal = (uiValue - 2) & 0xFC;
		
		ProgWrite(psDevInfo, aOutConf[uiOutput], (uiLowerVal << 6) | (uiLowerVal << 2) | DIVBY3);
		ProgWrite(psDevInfo, aOutConf[uiOutput] + 1, (uiUpperVal >> 2));
		ProgWrite(psDevInfo, aOutConf[uiOutput] + 2, (uiUpperVal >> 2));
	}
}


void SetDconfig(PVRPDP_DEVINFO *psDevInfo, unsigned long uiNum, unsigned long uiValue)
{
	unsigned long uiCount;
	
	for (uiCount = 0; uiCount < 4; uiCount++)
	{
		ProgWrite(psDevInfo, aD_config[uiNum][uiCount], uiValue);
	}
}


void SetNconfigLower(PVRPDP_DEVINFO *psDevInfo, unsigned long uiNum, unsigned long uiValue)
{
	unsigned long uiCount;
	
	for (uiCount = 0; uiCount < 4; uiCount++)
	{
		ProgWrite(psDevInfo, aN_config_lower[uiNum][uiCount], uiValue);
	}
}


void SetNconfigUpper(PVRPDP_DEVINFO *psDevInfo, unsigned long uiNum, unsigned long uiValue)
{
	unsigned long uiCount;
	
	for (uiCount = 0; uiCount < 4; uiCount++)
	{
		ProgWrite(psDevInfo, aN_config_upper[uiNum][uiCount], uiValue);
	}
}


void SetRCC(PVRPDP_DEVINFO *psDevInfo, unsigned long uiInnerPLL, unsigned long uiRz, unsigned long uiCp, unsigned long uiCz)
{
	unsigned long uiCount;
	unsigned long uiCpCz;
	
	uiRz &= 0x0F;
	for (uiCount = 0; uiCount < 4; uiCount++)
	{
		ProgWrite(psDevInfo, aRZ_Config[uiInnerPLL][uiCount], uiRz);
	}
	
	uiCpCz = (uiCp << 4) + uiCz;
	
	for (uiCount = 0; uiCount < 4; uiCount++)
	{
		ProgWrite(psDevInfo, aCPCZ_Config[uiInnerPLL][uiCount], uiCpCz);
	}
}


void SetCoeffs(PVRPDP_DEVINFO *psDevInfo, unsigned long uiPLL, unsigned long uiD, unsigned long uiM)
{
	unsigned long uiNlower, uiNupper, uiA;
	
	if (uiPLL == 2)
	{
		uiNlower = uiM & 0xFF;
		uiNupper = (uiM & 0xF00) >> 8;
		SetDconfig(psDevInfo, 2, uiD);
		SetNconfigLower(psDevInfo, 2, uiNlower);
		SetNconfigUpper(psDevInfo, 2, uiNupper);
		SetRCC(psDevInfo, 2, 0xF, 0xF, 0xF);
	}
	else
	{
		if ((uiM % 2) == 2)
		{
			uiM /= 2;
			uiA = 0;
		}
		else
		{
			uiM = (uiM - 3)/2;
			uiA = 2;
		}
		
		uiNlower = uiM & 0xFF;
		uiNupper = ((uiM & 0xF00) >> 8) | (uiA << 4);
		
		if (uiPLL == 1)
		{
			SetDconfig(psDevInfo, 1, uiD);
			SetNconfigLower(psDevInfo, 1, uiNlower);
			SetNconfigUpper(psDevInfo, 1, uiNupper);
			SetRCC(psDevInfo, 1, 0xF, 0xF, 0xF);
		}
		else if (uiPLL == 0)
		{
			SetDconfig(psDevInfo, 0, uiD);
			SetNconfigLower(psDevInfo, 0, uiNlower);
			SetNconfigUpper(psDevInfo, 0, uiNupper);
			SetRCC(psDevInfo, 0, 0xF, 0xF, 0xF);
		}
	}
}


static void FindCoeffs(double fOutput, COEFF_STRUCT *psPossCoeffs)
{
	double fInput = 27.0;
	double fMaxMulti = 1100.0, fMinMulti = 720.0;
	unsigned long uiN, uiM = 1;
	unsigned long uiPost;
	double fTryInput;
	double fMinInput = 1.0;
	double fErr, fMaxErr = 0.1;
	double fStartMulti, fPotMulti;
	
	PDP_FPU_BEGIN();

	if (fOutput == 0.0)
	{
		fOutput = fInput;
	}
	
	/* to compensate for built in div by 2 */
	fOutput *= 2.0;
	
	uiPost = (unsigned long)(fMaxMulti / fOutput);
	fStartMulti = fOutput * uiPost;
	
	fTryInput = fInput;
	
	psPossCoeffs->PreDivider  = 0;
	psPossCoeffs->Multiplier  = 0;
	psPossCoeffs->PostDivider = 0;
	psPossCoeffs->VCO         = 0;
	
	while (fTryInput >= fMinInput)
	{
		fPotMulti = fStartMulti;
		uiM++;
		uiN = 1;
		
		while (fPotMulti > fMinMulti)
		{
			fErr = fabs((fPotMulti / fTryInput) - floor((fPotMulti / fTryInput) + 0.5));
			if (fErr < fMaxErr)
			{
				psPossCoeffs->PreDivider  = uiM - 1;								/* Pre-Divider */
				psPossCoeffs->Multiplier  = (unsigned long)(fPotMulti / fTryInput);	/* Multiplier */
				psPossCoeffs->PostDivider = uiPost - uiN + 1;						/* Post Divider */
				psPossCoeffs->VCO         = (unsigned long)fPotMulti;					/* VCO */
				
				fMaxErr = fErr;
			}
			
			fPotMulti = (uiPost - uiN) * fOutput;
			uiN++;
		}
		
		fTryInput = fInput / uiM;
	}

	PDP_FPU_END();
}

void SysSetPDP1Clk (PVRPDP_DEVINFO *psDevInfo, unsigned long uiClkInHz)
{
	IMG_UINT32	 ui32TCFRevision;
	
	PDP_FPU_BEGIN();

	ui32TCFRevision = ReadTCFReg(psDevInfo, TCF_CR_TCF_CORE_REV_REG);
	
	if ((ui32TCFRevision & TCF_CR_TCF_CORE_REV_REG_TCF_CORE_REV_REG_MAJOR_MASK) >= (3UL << TCF_CR_TCF_CORE_REV_REG_TCF_CORE_REV_REG_MAJOR_SHIFT))
	{
		IMG_UINT32 uiClkInMHz = (uiClkInHz + 500000) / 1000000;
#if 1 /* PLL workaround */
		IMG_UINT32 uiCurrentClk = ReadTCFReg(psDevInfo, TCF_CR_PLL_PDP_CLK0);
		
		if (uiClkInMHz > (uiCurrentClk + 8))
		{
			do
			{
				uiCurrentClk += 8;
				WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_CLK0, uiCurrentClk);
				WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_DRP_GO , 1);
				PDPWaitus(1000);
				WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_DRP_GO , 0);
			
			} while (uiClkInMHz > (uiCurrentClk + 8));
		}
#endif
		/* set phase 0, ratio 50:50, freq in MHz */
		WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_CLK0, uiClkInMHz);
		
		/* setup TCF_CR_PLL_PDP_CLK1TO5 based on the main clock speed */
		if(uiClkInMHz >= 50)
		{
			/* same freq as clk0*/
			WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_CLK1TO5, 0x0);
		}
		else
		{
			/* double freq of clk0*/
			WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_CLK1TO5, 0x3);
		}
		
		/* action changes */
		WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_DRP_GO , 1);
		PDPWaitus(1000);
		WriteTCFReg(psDevInfo, TCF_CR_PLL_PDP_DRP_GO , 0);
	}
	else
	{
		COEFF_STRUCT sCoeffs;
		
		InitSCB(psDevInfo);
		SelectBus(psDevInfo, 1);
		
		FindCoeffs((PDP_DOUBLE)((PDP_DOUBLE)uiClkInHz / 1000000.0), &sCoeffs);
		
		SetCoeffs(psDevInfo, 0, sCoeffs.PreDivider, sCoeffs.Multiplier);
		SetPLLtoOut(psDevInfo, 0, 2);
		SetOutputDiv(psDevInfo, 2, sCoeffs.PostDivider);
		SetRCC(psDevInfo, 0, 0xF, 0xF, 0xF);
		
		/* the PDP is not guaranteed to be glich free so we do a PDP reset */
		ResetPDP1(psDevInfo);
	}
	
	PDP_FPU_END();
}

/*-----------------------------------------pdp1 timing section ------------------------*/

#define	fGTF_MARGIN			1.8f		// Top & bottom margin %
#define fGTF_HSYNC_MARGIN	8.0f		// HSync margin %
#define fGTF_MIN_VSYNCBP	550.0f		// Min VSync + Back Porch (us)
#define	fGTF_MIN_PORCH		1.0f		// Minimum porch line / char cell
#define fGTF_CELL_GRAN		8.0f		// assumed pixel granularity (cell)
#define fGTF_CELL_GRAN2		16.0f		// assumed pixel granularity * 2 (cell)

#define fGTF_M				600.0f		// Blanking Formula Gradient %/KHz
#define	fGTF_C				40.0f		// Blanking Formula Offset %
#define fGTF_K				128.0f		// Blanking Formula scale factor
#define fGTF_J				20.0f		// Blanking Formula weighting %

#define GTF_VSYNC_WIDTH			7		// Width of VSync lines
#define	GTF_MIN_PORCH			1		// Minimum porch line / char cell
#define	GTF_MIN_PORCH_PIXELS	8		// Minimum porch Pixels cell
#define GTF_CELL_GRAN			8		// assumed pixel granularity (cell)
#define GTF_CELL_GRAN2			16		// assumed pixel granularity * 2 (cell)

/*****************************************************************************
	GTFCalcFromRefresh:	Calculate a GTF Mode from refresh rate
	
	Parameters:	psUser:
				
				dwXres			X Resolution
				dwYRes			Y Resolution
				dwFrequency 	Vertical Frame Rate (Hz)
	
	Returns:	psCRTRegs & psHVTRegs


*****************************************************************************/
void GTFCalcFromRefresh(PDLMODE psUser, PHVTIMEREGS psHVTRegs)
{
	PDP_DOUBLE	fVtFreq, fHzFreq;
	PDP_DOUBLE	fFieldPeriod, fHzPeriod, fVtFreqEst, fDutyCycle, fClockFreq;
	PDP_DOUBLE	fGTF_Mdash, fGTF_Cdash;
	
	unsigned long	ulVTotal, ulHTotal;
	unsigned long	ulVSyncBP, ulVBP;
	unsigned long	ulHSyncWidth, ulHFrontPorch, ulHBlank;
	unsigned long	ulVSyncMin;
	
	PDP_FPU_BEGIN();

	if ((psUser->ulXExt * 3UL) == (psUser->ulYExt * 4UL))
	{
		ulVSyncMin = 4;
	}
	else if ((psUser->ulXExt * 9UL) == (psUser->ulYExt * 16UL))
	{
		ulVSyncMin = 5;
	}
	else if ((psUser->ulXExt * 10UL) == (psUser->ulYExt * 16UL))
	{
		ulVSyncMin = 6;
	}
	else if ((psUser->ulXExt * 4UL) == (psUser->ulYExt * 5UL))
	{
		ulVSyncMin = 7;
	}
	else if ((psUser->ulXExt * 9UL) == (psUser->ulYExt * 15UL))
	{
		ulVSyncMin = 7;
	}
	else
	{
		ulVSyncMin = GTF_VSYNC_WIDTH;
	}
	
	fGTF_Mdash = (fGTF_K * fGTF_M) / 256.0;
	fGTF_Cdash = (((fGTF_C - fGTF_J) * fGTF_K) / 256)+ fGTF_J;
	
	fVtFreq = (PDP_DOUBLE)psUser->ulRefresh;		// comes in as Hz
	
	/* Refresh rates higher than 250Hz are nonsense */
	if(fVtFreq > 250.0)
	{
		fVtFreq = 250.0;
	}
	
	/* Make sure we haven't been given a bad refresh rate */
	if(!fVtFreq)
	{
		fVtFreq = 60.0;
	}
	
	/*
		Calculate Field Vertical Period in uS
	*/
	fFieldPeriod = 1000000.0f / fVtFreq;
	
	/*
		Estimate Hz Period (in uS)
		& hence lines in (VSync + BP) & lines in Vt BP
	*/
	fHzPeriod = (fFieldPeriod - fGTF_MIN_VSYNCBP) / (psUser->ulYExt + fGTF_MIN_PORCH);
	
	ulVSyncBP = (unsigned long)(fGTF_MIN_VSYNCBP / fHzPeriod);
	
	ulVBP = ulVSyncBP - ulVSyncMin;
	ulVTotal  = psUser->ulYExt + ulVSyncBP + GTF_MIN_PORCH;
	
	/*
		Estimate Vt frequency (in Hz)
	*/
	fVtFreqEst = 1000000.0f / (fHzPeriod * ulVTotal);
	
	/*
		Find Actual Hz Period (in uS)
	*/
	fHzPeriod = (fHzPeriod * fVtFreqEst) / fVtFreq;
	
	/*
		Find actual Vt frequency (in Hz)
	*/
	fVtFreq = 1000000.0f / (fHzPeriod * ulVTotal);
	
	/*
		Find ideal blanking duty cycle from duty cycle equation
		and therefore number of pixels in blanking period (to nearest dbl char cell)
	*/
	fDutyCycle = fGTF_Cdash - ((fGTF_Mdash * fHzPeriod) / 1000.0f);
	
	ulHBlank = (unsigned long)(((psUser->ulXExt * fDutyCycle) / (100.0f - fDutyCycle) / fGTF_CELL_GRAN2) + 0.5);
	ulHBlank = ulHBlank * GTF_CELL_GRAN2;
	
	/*
		Find HTotal
		& pixel clock(MHz)
		& Hz Frequency (KHz)
	*/
	ulHTotal = psUser->ulXExt + ulHBlank;
	fClockFreq = (PDP_DOUBLE)ulHTotal / fHzPeriod;
	fHzFreq = 1000.0f / fHzPeriod;
	
	ulHSyncWidth = (unsigned long)(((ulHTotal * fGTF_HSYNC_MARGIN) / (100.0 * fGTF_CELL_GRAN)) + 0.5);
	ulHSyncWidth *= GTF_CELL_GRAN;
	
	ulHFrontPorch = (ulHBlank >> 1) - ulHSyncWidth;
	if (ulHFrontPorch < GTF_MIN_PORCH_PIXELS)
	{
		ulHFrontPorch = GTF_MIN_PORCH_PIXELS;
	}
	
	psHVTRegs->ulHBackPorch   = ulHSyncWidth;
	psHVTRegs->ulHTotal       = ulHTotal;
	psHVTRegs->ulHActiveStart = ulHBlank - ulHFrontPorch;
	psHVTRegs->ulHLeftBorder  = psHVTRegs->ulHActiveStart;
	psHVTRegs->ulHRightBorder = psHVTRegs->ulHLeftBorder + psUser->ulXExt;
	psHVTRegs->ulHFrontPorch  = psHVTRegs->ulHRightBorder;
	
	psHVTRegs->ulVBackPorch    = GTF_VSYNC_WIDTH;
	psHVTRegs->ulVTotal        = ulVTotal;
	psHVTRegs->ulVActiveStart  = ulVSyncBP;
	psHVTRegs->ulVTopBorder    = psHVTRegs->ulVActiveStart;
	psHVTRegs->ulVBottomBorder = psHVTRegs->ulVTopBorder + psUser->ulYExt;
	psHVTRegs->ulVFrontPorch   = psHVTRegs->ulVBottomBorder;
	
	/* And also store results back into PDLMODE->CRTMODE structure */
	psUser->ulRefresh = (unsigned long)fVtFreq;
	
	/* scale, round and then scale */
	psHVTRegs->ulClockFreq = ((unsigned long)(fClockFreq * 1000.0)) * 1000UL;

	PDP_FPU_END();
}
