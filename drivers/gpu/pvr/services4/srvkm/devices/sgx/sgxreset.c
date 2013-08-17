/*************************************************************************/ /*!
@Title          Device specific reset routines
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

#include "sgxdefs.h"
#include "sgxmmu.h"
#include "services_headers.h"
#include "sgxinfokm.h"
#include "sgxconfig.h"
#include "sgxutils.h"

#include "pdump_km.h"


/*!
*******************************************************************************

 @Function	SGXInitClocks

 @Description
	Initialise the SGX clocks

 @Input psDevInfo - device info. structure
 @Input ui32PDUMPFlags - flags to control PDUMP output

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID SGXInitClocks(PVRSRV_SGXDEV_INFO	*psDevInfo,
					   IMG_UINT32			ui32PDUMPFlags)
{
	IMG_UINT32	ui32RegVal;
	
#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */
	
	ui32RegVal = psDevInfo->ui32ClkGateCtl;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_CLKGATECTL, ui32RegVal, ui32PDUMPFlags);

#if defined(EUR_CR_CLKGATECTL2)
	ui32RegVal = psDevInfo->ui32ClkGateCtl2;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL2, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_CLKGATECTL2, ui32RegVal, ui32PDUMPFlags);
#endif
}


/*!
*******************************************************************************

 @Function	SGXResetInitBIFContexts

 @Description
	Initialise the BIF memory contexts

 @Input psDevInfo - SGX Device Info

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID SGXResetInitBIFContexts(PVRSRV_SGXDEV_INFO	*psDevInfo,
										IMG_UINT32			ui32PDUMPFlags)
{
	IMG_UINT32	ui32RegVal;

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */

	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the BIF bank settings\r\n");
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK_SET, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_BANK_SET, ui32RegVal, ui32PDUMPFlags);
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK0, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_BANK0, ui32RegVal, ui32PDUMPFlags);
#endif /* SGX_FEATURE_MULTIPLE_MEM_CONTEXTS */

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the BIF directory list\r\n");
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal, ui32PDUMPFlags);

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	{
		IMG_UINT32	ui32DirList, ui32DirListReg;

		for (ui32DirList = 1;
			 ui32DirList < SGX_FEATURE_BIF_NUM_DIRLISTS;
			 ui32DirList++)
		{
			ui32DirListReg = EUR_CR_BIF_DIR_LIST_BASE1 + 4 * (ui32DirList - 1);
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32DirListReg, ui32RegVal);
			PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, ui32DirListReg, ui32RegVal, ui32PDUMPFlags);
		}
	}
#endif /* SGX_FEATURE_MULTIPLE_MEM_CONTEXTS */
}


/*!
*******************************************************************************

 @Function	SGXResetSetupBIFContexts

 @Description
	Configure the BIF for the EDM context

 @Input psDevInfo - SGX Device Info

 @Return   IMG_VOID

******************************************************************************/
static IMG_VOID SGXResetSetupBIFContexts(PVRSRV_SGXDEV_INFO	*psDevInfo,
										 IMG_UINT32			ui32PDUMPFlags)
{
	IMG_UINT32	ui32RegVal;

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */
	
	#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	/* Set up EDM for bank 0 to point at kernel context */
	ui32RegVal = (SGX_BIF_DIR_LIST_INDEX_EDM << EUR_CR_BIF_BANK0_INDEX_EDM_SHIFT);

	#if defined(SGX_FEATURE_2D_HARDWARE) && !defined(SGX_FEATURE_PTLA)
	/* Set up 2D core for bank 0 to point at kernel context */
	ui32RegVal |= (SGX_BIF_DIR_LIST_INDEX_EDM << EUR_CR_BIF_BANK0_INDEX_2D_SHIFT);
	#endif /* SGX_FEATURE_2D_HARDWARE */

	#if defined(FIX_HW_BRN_23410)
	/* Set up TA core for bank 0 to point at kernel context to guarantee it is a valid context */
	ui32RegVal |= (SGX_BIF_DIR_LIST_INDEX_EDM << EUR_CR_BIF_BANK0_INDEX_TA_SHIFT);
	#endif /* FIX_HW_BRN_23410 */

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK0, ui32RegVal);
	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Set up EDM requestor page table in BIF\r\n");
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_BANK0, ui32RegVal, ui32PDUMPFlags);
	#endif /* defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS) */

	{
		IMG_UINT32	ui32EDMDirListReg;

		/* Set up EDM context with kernel page directory */
		#if (SGX_BIF_DIR_LIST_INDEX_EDM == 0)
		ui32EDMDirListReg = EUR_CR_BIF_DIR_LIST_BASE0;
		#else
		/* Bases 0 and 1 are not necessarily contiguous */
		ui32EDMDirListReg = EUR_CR_BIF_DIR_LIST_BASE1 + 4 * (SGX_BIF_DIR_LIST_INDEX_EDM - 1);
		#endif /* SGX_BIF_DIR_LIST_INDEX_EDM */

		ui32RegVal = (IMG_UINT32)(psDevInfo->sKernelPDDevPAddr.uiAddr >> SGX_MMU_PDE_ADDR_ALIGNSHIFT);
		
#if defined(FIX_HW_BRN_28011)
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal);
		PDUMPPDREGWITHFLAGS(&psDevInfo->sMMUAttrib, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal, ui32PDUMPFlags, PDUMP_PD_UNIQUETAG);
#endif

		OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32EDMDirListReg, ui32RegVal);
		PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the EDM's directory list base\r\n");
		PDUMPPDREGWITHFLAGS(&psDevInfo->sMMUAttrib, ui32EDMDirListReg, ui32RegVal, ui32PDUMPFlags, PDUMP_PD_UNIQUETAG);
	}
}


/*!
*******************************************************************************

 @Function	SGXResetSleep

 @Description

 Sleep for a short time to allow reset register writes to complete.
 Required because no status registers are available to poll on.

 @Input psDevInfo - SGX Device Info
 @Input ui32PDUMPFlags - flags to control PDUMP output
 @Input bPDump - Pdump the sleep

 @Return   Nothing

******************************************************************************/
static IMG_VOID SGXResetSleep(PVRSRV_SGXDEV_INFO	*psDevInfo,
							  IMG_UINT32			ui32PDUMPFlags,
							  IMG_BOOL				bPDump)
{
#if defined(PDUMP) || defined(EMULATOR)
	IMG_UINT32	ui32ReadRegister;

	#if defined(SGX_FEATURE_MP)
	ui32ReadRegister = EUR_CR_MASTER_SOFT_RESET;
	#else
	ui32ReadRegister = EUR_CR_SOFT_RESET;
	#endif /* SGX_FEATURE_MP */
#endif

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */

	/* Sleep for 100 SGX clocks */
	SGXWaitClocks(psDevInfo, 100);
	if (bPDump)
	{
		PDUMPIDLWITHFLAGS(30, ui32PDUMPFlags);
#if defined(PDUMP)
		PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Read back to flush the register writes\r\n");
		PDumpRegRead(SGX_PDUMPREG_NAME, ui32ReadRegister, ui32PDUMPFlags);
#endif
	}

#if defined(EMULATOR)
	/*
		Read a register to make sure we wait long enough on the emulator...
	*/
	OSReadHWReg(psDevInfo->pvRegsBaseKM, ui32ReadRegister);
#endif
}


#if !defined(SGX_FEATURE_MP)
/*!
*******************************************************************************

 @Function	SGXResetSoftReset

 @Description

 Write to the SGX soft reset register.

 @Input psDevInfo - SGX Device Info
 @Input bResetBIF - Include the BIF in the soft reset
 @Input ui32PDUMPFlags - flags to control PDUMP output
 @Input bPDump - Pdump the sleep

 @Return   Nothing

******************************************************************************/
static IMG_VOID SGXResetSoftReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
								  IMG_BOOL				bResetBIF,
								  IMG_UINT32			ui32PDUMPFlags,
								  IMG_BOOL				bPDump)
{
	IMG_UINT32 ui32SoftResetRegVal;

	ui32SoftResetRegVal =
					/* add common reset bits: */
					EUR_CR_SOFT_RESET_DPM_RESET_MASK |
					EUR_CR_SOFT_RESET_TA_RESET_MASK  |
					EUR_CR_SOFT_RESET_USE_RESET_MASK |
					EUR_CR_SOFT_RESET_ISP_RESET_MASK |
					EUR_CR_SOFT_RESET_TSP_RESET_MASK;

/* add conditional reset bits: */
#ifdef EUR_CR_SOFT_RESET_TWOD_RESET_MASK
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TWOD_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_TE_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TE_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_MTE_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_MTE_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_ISP2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_ISP2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_PDS_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_PDS_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_PBE_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_PBE_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_CACHEL2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_CACHEL2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_TCU_L2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TCU_L2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_UCACHEL2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_UCACHEL2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_MADD_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_MADD_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_ITR_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_ITR_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_TEX_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_TEX_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_IDXFIFO_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_IDXFIFO_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_VDM_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_VDM_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_DCU_L2_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_DCU_L2_RESET_MASK;
#endif
#if defined(EUR_CR_SOFT_RESET_DCU_L0L1_RESET_MASK)
	ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_DCU_L0L1_RESET_MASK;
#endif

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */

	if (bResetBIF)
	{
		ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_BIF_RESET_MASK;
	}

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_SOFT_RESET, ui32SoftResetRegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_SOFT_RESET, ui32SoftResetRegVal, ui32PDUMPFlags);
	}
}


/*!
*******************************************************************************

 @Function	SGXResetInvalDC

 @Description

 Invalidate the BIF Directory Cache and wait for the operation to complete.

 @Input psDevInfo - SGX Device Info
 @Input ui32PDUMPFlags - flags to control PDUMP output

 @Return   Nothing

******************************************************************************/
static IMG_VOID SGXResetInvalDC(PVRSRV_SGXDEV_INFO	*psDevInfo,
							    IMG_UINT32			ui32PDUMPFlags,
								IMG_BOOL			bPDump)
{
	IMG_UINT32 ui32RegVal;

	/* Invalidate BIF Directory cache. */
#if defined(EUR_CR_BIF_CTRL_INVAL)
	ui32RegVal = EUR_CR_BIF_CTRL_INVAL_ALL_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL_INVAL, ui32RegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL_INVAL, ui32RegVal, ui32PDUMPFlags);
	}
#else
	ui32RegVal = EUR_CR_BIF_CTRL_INVALDC_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);
	}

	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);
	}
#endif
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, bPDump);

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	{
		/*
			Wait for the DC invalidate to complete - indicated by
			outstanding reads reaching zero.
		*/
		if (PollForValueKM((IMG_UINT32 *)((IMG_UINT8*)psDevInfo->pvRegsBaseKM + EUR_CR_BIF_MEM_REQ_STAT),
							0,
							EUR_CR_BIF_MEM_REQ_STAT_READS_MASK,
							MAX_HW_TIME_US,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							IMG_FALSE) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Wait for DC invalidate failed."));
			PVR_DBG_BREAK;
		}

		if (bPDump)
		{
			PDUMPREGPOLWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_MEM_REQ_STAT, 0, EUR_CR_BIF_MEM_REQ_STAT_READS_MASK, ui32PDUMPFlags, PDUMP_POLL_OPERATOR_EQUAL);
		}
	}
#endif /* SGX_FEATURE_MULTIPLE_MEM_CONTEXTS */
}
#endif /* SGX_FEATURE_MP */


/*!
*******************************************************************************

 @Function	SGXReset

 @Description

 Reset chip

 @Input psDevInfo - device info. structure
 @Input bHardwareRecovery - true if recovering powered hardware,
 							false if powering up
 @Input ui32PDUMPFlags - flags to control PDUMP output

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID SGXReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
				  IMG_BOOL				bHardwareRecovery,
				  IMG_UINT32			ui32PDUMPFlags)
#if !defined(SGX_FEATURE_MP)
{
	IMG_UINT32 ui32RegVal;
#if defined(EUR_CR_BIF_INT_STAT_FAULT_REQ_MASK)
	const IMG_UINT32 ui32BifFaultMask = EUR_CR_BIF_INT_STAT_FAULT_REQ_MASK;
#else
	const IMG_UINT32 ui32BifFaultMask = EUR_CR_BIF_INT_STAT_FAULT_MASK;
#endif

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Start of SGX reset sequence\r\n");

#if defined(FIX_HW_BRN_23944)
	/* Pause the BIF. */
	ui32RegVal = EUR_CR_BIF_CTRL_PAUSE_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_INT_STAT);
	if (ui32RegVal & ui32BifFaultMask)
	{
		/* Page fault needs to be cleared before resetting the BIF. */
		ui32RegVal = EUR_CR_BIF_CTRL_PAUSE_MASK | EUR_CR_BIF_CTRL_CLEAR_FAULT_MASK;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

		ui32RegVal = EUR_CR_BIF_CTRL_PAUSE_MASK;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);
	}
#endif /* defined(FIX_HW_BRN_23944) */

	/* Reset all including BIF */
	SGXResetSoftReset(psDevInfo, IMG_TRUE, ui32PDUMPFlags, IMG_TRUE);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	/*
		Initialise the BIF state.
	*/
#if defined(SGX_FEATURE_36BIT_MMU)
	/* enable 36bit addressing mode if the MMU supports it*/
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_36BIT_ADDRESSING, EUR_CR_BIF_36BIT_ADDRESSING_ENABLE_MASK);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_36BIT_ADDRESSING, EUR_CR_BIF_36BIT_ADDRESSING_ENABLE_MASK, ui32PDUMPFlags);
#else
	#if defined(EUR_CR_BIF_36BIT_ADDRESSING)
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, 
				EUR_CR_BIF_36BIT_ADDRESSING, 
				0);
		PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, 
				EUR_CR_BIF_36BIT_ADDRESSING, 
				0, 
				ui32PDUMPFlags);
	#endif
#endif

	SGXResetInitBIFContexts(psDevInfo, ui32PDUMPFlags);

#if defined(EUR_CR_BIF_MEM_ARB_CONFIG)
	/*
		Initialise the memory arbiter to its default state
	*/
	ui32RegVal	= (12UL << EUR_CR_BIF_MEM_ARB_CONFIG_PAGE_SIZE_SHIFT) |
				  (7UL << EUR_CR_BIF_MEM_ARB_CONFIG_BEST_CNT_SHIFT) |
				  (12UL << EUR_CR_BIF_MEM_ARB_CONFIG_TTE_THRESH_SHIFT);
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_MEM_ARB_CONFIG, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_MEM_ARB_CONFIG, ui32RegVal, ui32PDUMPFlags);
#endif /* EUR_CR_BIF_MEM_ARB_CONFIG */

#if defined(SGX_FEATURE_SYSTEM_CACHE)
	#if defined(SGX_BYPASS_SYSTEM_CACHE)
		/* set the SLC to bypass all accesses */
		ui32RegVal = MNE_CR_CTRL_BYPASS_ALL_MASK;
	#else
		#if defined(FIX_HW_BRN_26620)
			ui32RegVal = 0;
		#else
			/* set the SLC to bypass cache-coherent accesses */
			ui32RegVal = MNE_CR_CTRL_BYP_CC_MASK;
		#endif
		#if defined(FIX_HW_BRN_34028)
			/* Bypass the MNE for the USEC requester */
			ui32RegVal |= (8 << MNE_CR_CTRL_BYPASS_SHIFT);
		#endif
	#endif /* SGX_BYPASS_SYSTEM_CACHE */
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, MNE_CR_CTRL, ui32RegVal);
	PDUMPREG(SGX_PDUMPREG_NAME, MNE_CR_CTRL, ui32RegVal);
#endif /* SGX_FEATURE_SYSTEM_CACHE */

	if (bHardwareRecovery)
	{
		/*
			Set all requestors to the dummy PD which forces all memory
			accesses to page fault.
			This enables us to flush out BIF requests from parts of SGX
			which do not have their own soft reset.
			Note: sBIFResetPDDevPAddr.uiAddr is a relative address (2GB max)
			MSB is the bus master flag; 1 == enabled
		*/
		ui32RegVal = (IMG_UINT32)psDevInfo->sBIFResetPDDevPAddr.uiAddr;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal);

		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

		/* Bring BIF out of reset. */
		SGXResetSoftReset(psDevInfo, IMG_FALSE, ui32PDUMPFlags, IMG_TRUE);
		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

		SGXResetInvalDC(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

		/*
			Check for a page fault from parts of SGX which do not have a reset.
		*/
		for (;;)
		{
			IMG_UINT32 ui32BifIntStat = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_INT_STAT);
			IMG_DEV_VIRTADDR sBifFault;
			IMG_UINT32 ui32PDIndex, ui32PTIndex;

			if ((ui32BifIntStat & ui32BifFaultMask) == 0)
			{
				break;
			}

			/*
				There is a page fault, so reset the BIF again, map in the dummy page,
				bring the BIF up and invalidate the Directory Cache.
			*/
			sBifFault.uiAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_FAULT);
			PVR_DPF((PVR_DBG_WARNING, "SGXReset: Page fault 0x%x/0x%x", ui32BifIntStat, sBifFault.uiAddr));
			ui32PDIndex = sBifFault.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
			ui32PTIndex = (sBifFault.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

			/* Put the BIF into reset. */
			SGXResetSoftReset(psDevInfo, IMG_TRUE, ui32PDUMPFlags, IMG_FALSE);

			/* Map in the dummy page. */
			psDevInfo->pui32BIFResetPD[ui32PDIndex] = (IMG_UINT32)(psDevInfo->sBIFResetPTDevPAddr.uiAddr
													>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
													| SGX_MMU_PDE_PAGE_SIZE_4K
													| SGX_MMU_PDE_VALID;
			psDevInfo->pui32BIFResetPT[ui32PTIndex] = (IMG_UINT32)(psDevInfo->sBIFResetPageDevPAddr.uiAddr
													>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
													| SGX_MMU_PTE_VALID;

			/* Clear outstanding events. */
			ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS);
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR, ui32RegVal);
			ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS2);
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR2, ui32RegVal);

			SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

			/* Bring the BIF out of reset. */
			SGXResetSoftReset(psDevInfo, IMG_FALSE, ui32PDUMPFlags, IMG_FALSE);
			SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

			/* Invalidate Directory Cache. */
			SGXResetInvalDC(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

			/* Unmap the dummy page and try again. */
			psDevInfo->pui32BIFResetPD[ui32PDIndex] = 0;
			psDevInfo->pui32BIFResetPT[ui32PTIndex] = 0;
		}
	}
	else
	{
		/* Bring BIF out of reset. */
		SGXResetSoftReset(psDevInfo, IMG_FALSE, ui32PDUMPFlags, IMG_TRUE);
		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);
	}	

	/*
		Initialise the BIF memory contexts before bringing the rest of SGX out of reset.
	*/
	SGXResetSetupBIFContexts(psDevInfo, ui32PDUMPFlags);

#if defined(SGX_FEATURE_2D_HARDWARE) && !defined(SGX_FEATURE_PTLA)
	/* check that the heap base has the right alignment (1Mb) */
	#if ((SGX_2D_HEAP_BASE & ~EUR_CR_BIF_TWOD_REQ_BASE_ADDR_MASK) != 0)
		#error "SGXReset: SGX_2D_HEAP_BASE doesn't match EUR_CR_BIF_TWOD_REQ_BASE_ADDR_MASK alignment"
	#endif
	/* Set up 2D requestor base */
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_TWOD_REQ_BASE, SGX_2D_HEAP_BASE);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_BIF_TWOD_REQ_BASE, SGX_2D_HEAP_BASE, ui32PDUMPFlags);
#endif

	/* Invalidate BIF Directory cache. */
	SGXResetInvalDC(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	PVR_DPF((PVR_DBG_MESSAGE,"Soft Reset of SGX"));

	/* Take chip out of reset */
	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_SOFT_RESET, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_SOFT_RESET, ui32RegVal, ui32PDUMPFlags);

	/* wait a bit */
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "End of SGX reset sequence\r\n");
}

#else

{
	IMG_UINT32 ui32RegVal;
	
	PVR_UNREFERENCED_PARAMETER(bHardwareRecovery);

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif /* PDUMP */

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Start of SGX MP reset sequence\r\n");

	/* Put hydra into soft reset */
	ui32RegVal = EUR_CR_MASTER_SOFT_RESET_BIF_RESET_MASK  |
				 EUR_CR_MASTER_SOFT_RESET_IPF_RESET_MASK  |
				 EUR_CR_MASTER_SOFT_RESET_DPM_RESET_MASK  |
				 EUR_CR_MASTER_SOFT_RESET_VDM_RESET_MASK;

	if (bHardwareRecovery)
	{
		ui32RegVal |= EUR_CR_MASTER_SOFT_RESET_MCI_RESET_MASK;
	}

#if defined(SGX_FEATURE_PTLA)
	ui32RegVal |= EUR_CR_MASTER_SOFT_RESET_PTLA_RESET_MASK;
#endif
#if defined(SGX_FEATURE_SYSTEM_CACHE)
	ui32RegVal |= EUR_CR_MASTER_SOFT_RESET_SLC_RESET_MASK;
#endif

	/* Hard reset the slave cores */
	ui32RegVal |= EUR_CR_MASTER_SOFT_RESET_CORE_RESET_MASK(0)  |
				  EUR_CR_MASTER_SOFT_RESET_CORE_RESET_MASK(1)  |
				  EUR_CR_MASTER_SOFT_RESET_CORE_RESET_MASK(2)  |
				  EUR_CR_MASTER_SOFT_RESET_CORE_RESET_MASK(3);

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_SOFT_RESET, ui32RegVal);
	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Soft reset hydra partition, hard reset the cores\r\n");
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_MASTER_SOFT_RESET, ui32RegVal, ui32PDUMPFlags);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_BIF_CTRL, ui32RegVal);
	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the hydra BIF control\r\n");
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_MASTER_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

#if defined(SGX_FEATURE_SYSTEM_CACHE)
	#if defined(SGX_BYPASS_SYSTEM_CACHE)
		ui32RegVal = EUR_CR_MASTER_SLC_CTRL_BYPASS_ALL_MASK;
	#else
		ui32RegVal = EUR_CR_MASTER_SLC_CTRL_USSE_INVAL_REQ0_MASK |
		#if defined(FIX_HW_BRN_30954)
						EUR_CR_MASTER_SLC_CTRL_DISABLE_REORDERING_MASK |
		#endif
		#if defined(PVR_SLC_8KB_ADDRESS_MODE)
						(4 << EUR_CR_MASTER_SLC_CTRL_ADDR_DECODE_MODE_SHIFT) |
		#endif
		#if defined(FIX_HW_BRN_33809)
			(2 << EUR_CR_MASTER_SLC_CTRL_ADDR_DECODE_MODE_SHIFT) |
		#endif
						(0xC << EUR_CR_MASTER_SLC_CTRL_ARB_PAGE_SIZE_SHIFT);
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_SLC_CTRL, ui32RegVal);
		PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the hydra SLC control\r\n");
		PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_MASTER_SLC_CTRL, ui32RegVal);

		ui32RegVal = EUR_CR_MASTER_SLC_CTRL_BYPASS_BYP_CC_MASK;
	#if defined(FIX_HW_BRN_31620)
		ui32RegVal |= EUR_CR_MASTER_SLC_CTRL_BYPASS_REQ_MMU_MASK;
	#endif
	#if defined(FIX_HW_BRN_31195)
		ui32RegVal |= EUR_CR_MASTER_SLC_CTRL_BYPASS_REQ_USE0_MASK |
				EUR_CR_MASTER_SLC_CTRL_BYPASS_REQ_USE1_MASK |
				EUR_CR_MASTER_SLC_CTRL_BYPASS_REQ_USE2_MASK |
				EUR_CR_MASTER_SLC_CTRL_BYPASS_REQ_USE3_MASK |
				EUR_CR_MASTER_SLC_CTRL_BYPASS_REQ_TA_MASK;
	#endif
	#endif /* SGX_BYPASS_SYSTEM_CACHE */
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_SLC_CTRL_BYPASS, ui32RegVal);
		PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the hydra SLC bypass control\r\n");
		PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_MASTER_SLC_CTRL_BYPASS, ui32RegVal);
#endif /* SGX_FEATURE_SYSTEM_CACHE */

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	/* Remove the resets */
	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_SOFT_RESET, ui32RegVal);
	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Remove the resets from all of SGX\r\n");
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_MASTER_SOFT_RESET, ui32RegVal, ui32PDUMPFlags);
	
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Turn on the slave cores' clock gating\r\n");
	SGXInitClocks(psDevInfo, ui32PDUMPFlags);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Initialise the slave BIFs\r\n");

#if defined(FIX_HW_BRN_31278) || defined(FIX_HW_BRN_31620) || defined(FIX_HW_BRN_31671) || defined(FIX_HW_BRN_32085)
	#if defined(FIX_HW_BRN_31278) || defined(FIX_HW_BRN_32085)
	/* disable prefetch */
	ui32RegVal = (1<<EUR_CR_MASTER_BIF_MMU_CTRL_ADDR_HASH_MODE_SHIFT);
	#else
	ui32RegVal = (1<<EUR_CR_MASTER_BIF_MMU_CTRL_ADDR_HASH_MODE_SHIFT) | EUR_CR_MASTER_BIF_MMU_CTRL_PREFETCHING_ON_MASK; 
	#endif
	#if !defined(FIX_HW_BRN_31620) && !defined(FIX_HW_BRN_31671)
	/* enable the DC TLB */
	ui32RegVal |= EUR_CR_MASTER_BIF_MMU_CTRL_ENABLE_DC_TLB_MASK;
	#endif

	/* Master bank */
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MASTER_BIF_MMU_CTRL, ui32RegVal);
	PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, EUR_CR_MASTER_BIF_MMU_CTRL, ui32RegVal, ui32PDUMPFlags);

	#if defined(FIX_HW_BRN_31278) || defined(FIX_HW_BRN_32085)
	/* disable prefetch */
	ui32RegVal = (1<<EUR_CR_BIF_MMU_CTRL_ADDR_HASH_MODE_SHIFT);
	#else
	ui32RegVal = (1<<EUR_CR_BIF_MMU_CTRL_ADDR_HASH_MODE_SHIFT) | EUR_CR_BIF_MMU_CTRL_PREFETCHING_ON_MASK; 
	#endif
	#if !defined(FIX_HW_BRN_31620) && !defined(FIX_HW_BRN_31671)
	/* enable the DC TLB */
	ui32RegVal |= EUR_CR_BIF_MMU_CTRL_ENABLE_DC_TLB_MASK;
	#endif

	/* Per-core */
	{
		IMG_UINT32 ui32Core;

		for (ui32Core=0;ui32Core<SGX_FEATURE_MP_CORE_COUNT;ui32Core++)
		{
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, SGX_MP_CORE_SELECT(EUR_CR_BIF_MMU_CTRL, ui32Core), ui32RegVal);
			PDUMPREGWITHFLAGS(SGX_PDUMPREG_NAME, SGX_MP_CORE_SELECT(EUR_CR_BIF_MMU_CTRL, ui32Core), ui32RegVal, ui32PDUMPFlags);
		}
	}
#endif

	SGXResetInitBIFContexts(psDevInfo, ui32PDUMPFlags);
	SGXResetSetupBIFContexts(psDevInfo, ui32PDUMPFlags);
	
	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "End of SGX MP reset sequence\r\n");
}	
#endif /* SGX_FEATURE_MP */


/******************************************************************************
 End of file (sgxreset.c)
******************************************************************************/
