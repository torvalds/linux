/*************************************************************************/ /*!
@File
@Title          Device specific start/stop routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific start/stop routines
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

/* The routines implemented here are built on top of an abstraction layer to
 * hide DDK/OS-specific details in case they are used outside of the DDK
 * (e.g. when trusted device is enabled).
 * Any new dependency should be added to rgxlayer.h.
 * Any new code should be built on top of the existing abstraction layer,
 * which should be extended when necessary. */
#include "rgxstartstop.h"
#include "rgxfwutils.h"

/*
 * Specific fields for RGX_CR_IDLE must not be polled in pdumps
 * (technical reasons)
 */
#define CR_IDLE_UNSELECTED_MASK ((~RGX_CR_SLC_IDLE_ACE_CONVERTERS_CLRMSK) | \
								 (~RGX_CR_SLC_IDLE_OWDB_CLRMSK) |		\
								 (RGX_CR_SLC_IDLE_FBCDC_ARB_EN))

static PVRSRV_ERROR RGXWriteMetaCoreRegThoughSP(const void *hPrivate,
                                                IMG_UINT32 ui32CoreReg,
                                                IMG_UINT32 ui32Value)
{
	IMG_UINT32 i = 0;

	RGXWriteMetaRegThroughSP(hPrivate, META_CR_TXUXXRXDT_OFFSET, ui32Value);
	RGXWriteMetaRegThroughSP(hPrivate, META_CR_TXUXXRXRQ_OFFSET, ui32CoreReg & ~META_CR_TXUXXRXRQ_RDnWR_BIT);

	do
	{
		RGXReadMetaRegThroughSP(hPrivate, META_CR_TXUXXRXRQ_OFFSET, &ui32Value);
	} while (((ui32Value & META_CR_TXUXXRXRQ_DREADY_BIT) != META_CR_TXUXXRXRQ_DREADY_BIT) && (i++ < 1000));

	if (i == 1000)
	{
		RGXCommentLog(hPrivate, "RGXWriteMetaCoreRegThoughSP: Timeout");
		return PVRSRV_ERROR_TIMEOUT;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR RGXStartFirmware(const void *hPrivate)
{
	PVRSRV_ERROR eError;

	/* Give privilege to debug and slave port */
	RGXWriteMetaRegThroughSP(hPrivate, META_CR_SYSC_JTAG_THREAD, META_CR_SYSC_JTAG_THREAD_PRIV_EN);

	/* Point Meta to the bootloader address, global (uncached) range */
	eError = RGXWriteMetaCoreRegThoughSP(hPrivate,
	                                     PC_ACCESS(0),
	                                     RGXFW_BOOTLDR_META_ADDR | META_MEM_GLOBAL_RANGE_BIT);

	if (eError != PVRSRV_OK)
	{
		RGXCommentLog(hPrivate, "RGXStart: RGX Firmware Slave boot Start failed!");
		return eError;
	}

	/* Enable minim encoding */
	RGXWriteMetaRegThroughSP(hPrivate, META_CR_TXPRIVEXT, META_CR_TXPRIVEXT_MINIM_EN);

	/* Enable Meta thread */
	RGXWriteMetaRegThroughSP(hPrivate, META_CR_T0ENABLE_OFFSET, META_CR_TXENABLE_ENABLE_BIT);

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function      RGXInitMetaProcWrapper

 @Description   Configures the hardware wrapper of the META processor

 @Input         hPrivate  : Implementation specific data

 @Return        void

******************************************************************************/
static void RGXInitMetaProcWrapper(const void *hPrivate)
{
	IMG_UINT64 ui64GartenConfig;

	/* Garten IDLE bit controlled by META */
	ui64GartenConfig = RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META;

	RGXCommentLog(hPrivate, "RGXStart: Configure META wrapper");
	RGXWriteReg64(hPrivate, RGX_CR_MTS_GARTEN_WRAPPER_CONFIG, ui64GartenConfig);
}


/*!
*******************************************************************************

 @Function      RGXInitRiscvProcWrapper

 @Description   Configures the hardware wrapper of the RISCV processor

 @Input         hPrivate  : Implementation specific data

 @Return        void

******************************************************************************/
static void RGXInitRiscvProcWrapper(const void *hPrivate)
{
	IMG_DEV_VIRTADDR sTmp;

	RGXCommentLog(hPrivate, "RGXStart: Configure RISCV wrapper");

	RGXCommentLog(hPrivate, "RGXStart: Write boot code remap");
	RGXAcquireBootCodeAddr(hPrivate, &sTmp);
	RGXWriteReg64(hPrivate,
	              RGXRISCVFW_BOOTLDR_CODE_REMAP,
	              sTmp.uiAddr |
	              (IMG_UINT64) (RGX_FIRMWARE_RAW_HEAP_SIZE >> FWCORE_ADDR_REMAP_CONFIG0_SIZE_ALIGNSHIFT)
	                << RGX_CR_FWCORE_ADDR_REMAP_CONFIG0_SIZE_SHIFT |
	              (IMG_UINT64) MMU_CONTEXT_MAPPING_FWPRIV << FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_SHIFT |
	              RGX_CR_FWCORE_ADDR_REMAP_CONFIG0_FETCH_EN_EN);

	RGXCommentLog(hPrivate, "RGXStart: Write boot data remap");
	RGXAcquireBootDataAddr(hPrivate, &sTmp);
	RGXWriteReg64(hPrivate,
	              RGXRISCVFW_BOOTLDR_DATA_REMAP,
	              sTmp.uiAddr |
	              (IMG_UINT64) (RGX_FIRMWARE_RAW_HEAP_SIZE >> FWCORE_ADDR_REMAP_CONFIG0_SIZE_ALIGNSHIFT)
	                << RGX_CR_FWCORE_ADDR_REMAP_CONFIG0_SIZE_SHIFT |
	              (IMG_UINT64) MMU_CONTEXT_MAPPING_FWPRIV << FWCORE_ADDR_REMAP_CONFIG0_MMU_CONTEXT_SHIFT |
	              RGX_CR_FWCORE_ADDR_REMAP_CONFIG0_LOAD_STORE_EN_EN);

	/* Garten IDLE bit controlled by RISCV */
	RGXCommentLog(hPrivate, "RGXStart: Set GARTEN_IDLE type to RISCV");
	RGXWriteReg64(hPrivate, RGX_CR_MTS_GARTEN_WRAPPER_CONFIG, RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META);
}


/*!
*******************************************************************************

 @Function      RGXInitBIF

 @Description   Initialise RGX BIF

 @Input         hPrivate : Implementation specific data

 @Return        void

******************************************************************************/
static void RGXInitBIF(const void *hPrivate)
{
	IMG_DEV_PHYADDR sPCAddr;
	IMG_UINT32 uiPCAddr;
	IMG_UINT32 ui32CBaseMapCtxReg;
	RGX_LAYER_PARAMS *psParams = (RGX_LAYER_PARAMS*)hPrivate;
	PVRSRV_RGXDEV_INFO *psDevInfo = psParams->psDevInfo;

	/*
	 * Acquire the address of the Kernel Page Catalogue.
	 */
	RGXAcquireKernelMMUPC(hPrivate, &sPCAddr);

	if (RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1)
	{
		uiPCAddr = (((sPCAddr.uiAddr >> RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_ALIGNSHIFT)
		             << RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_SHIFT)
		            & ~RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_CLRMSK);

		/*
		 * Write the kernel catalogue base.
		 */
		RGXCommentLog(hPrivate, "RGX firmware MMU Page Catalogue");

		ui32CBaseMapCtxReg = RGX_CR_MMU_CBASE_MAPPING_CONTEXT__HOST_SECURITY_GT1_AND_MH_PASID_WIDTH_LT6_AND_MMU_GE4;

		/* Set the mapping context */
		RGXWriteReg32(hPrivate, ui32CBaseMapCtxReg, MMU_CONTEXT_MAPPING_FWPRIV);
		(void)RGXReadReg32(hPrivate, ui32CBaseMapCtxReg); /* Fence write */

		/* Write the cat-base address */
		RGXWriteKernelMMUPC32(hPrivate,
		                      RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1,
		                      RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_ALIGNSHIFT,
		                      RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_SHIFT,
		                      uiPCAddr);

#if (MMU_CONTEXT_MAPPING_FWIF != MMU_CONTEXT_MAPPING_FWPRIV)
		/* Set-up different MMU ID mapping to the same PC used above */
		RGXWriteReg32(hPrivate, ui32CBaseMapCtxReg, MMU_CONTEXT_MAPPING_FWIF);
		(void)RGXReadReg32(hPrivate, ui32CBaseMapCtxReg); /* Fence write */

		RGXWriteKernelMMUPC32(hPrivate,
		                      RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1,
		                      RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_ALIGNSHIFT,
		                      RGX_CR_MMU_CBASE_MAPPING__HOST_SECURITY_GT1__BASE_ADDR_SHIFT,
		                      uiPCAddr);
#endif
	}
	else
	{
		uiPCAddr = (((sPCAddr.uiAddr >> RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT)
		             << RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT)
		            & ~RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_CLRMSK);

		/*
		 * Write the kernel catalogue base.
		 */
		RGXCommentLog(hPrivate, "RGX firmware MMU Page Catalogue");

		ui32CBaseMapCtxReg = RGX_CR_MMU_CBASE_MAPPING_CONTEXT;

		/* Set the mapping context */
		RGXWriteReg32(hPrivate, ui32CBaseMapCtxReg, MMU_CONTEXT_MAPPING_FWPRIV);
		(void)RGXReadReg32(hPrivate, ui32CBaseMapCtxReg); /* Fence write */

		/* Write the cat-base address */
		RGXWriteKernelMMUPC32(hPrivate,
		                      RGX_CR_MMU_CBASE_MAPPING,
		                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT,
		                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT,
		                      uiPCAddr);

#if (MMU_CONTEXT_MAPPING_FWIF != MMU_CONTEXT_MAPPING_FWPRIV)
		/* Set-up different MMU ID mapping to the same PC used above */
		RGXWriteReg32(hPrivate, ui32CBaseMapCtxReg, MMU_CONTEXT_MAPPING_FWIF);
		(void)RGXReadReg32(hPrivate, ui32CBaseMapCtxReg); /* Fence write */

		RGXWriteKernelMMUPC32(hPrivate,
		                      RGX_CR_MMU_CBASE_MAPPING,
		                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT,
		                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT,
		                      uiPCAddr);
#endif
	}
}


/**************************************************************************/ /*!
@Function       RGXInitMMURangeRegisters
@Description    Initialises MMU range registers for Non4K pages.
@Input          hPrivate           Implementation specific data
@Return         void
 */ /**************************************************************************/
static void RGXInitMMURangeRegisters(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams = (RGX_LAYER_PARAMS*)hPrivate;
	PVRSRV_RGXDEV_INFO *psDevInfo = psParams->psDevInfo;
	IMG_UINT32 ui32RegAddr = RGX_CR_MMU_PAGE_SIZE_RANGE_ONE;
	IMG_UINT32 i;

	for (i = 0; i < ARRAY_SIZE(psDevInfo->aui64MMUPageSizeRangeValue); ++i, ui32RegAddr += sizeof(IMG_UINT64))
	{
		RGXWriteReg64(hPrivate, ui32RegAddr, psDevInfo->aui64MMUPageSizeRangeValue[i]);
	}
}


/**************************************************************************/ /*!
@Function       RGXInitAXIACE
@Description    Initialises AXI ACE registers
@Input          hPrivate           Implementation specific data
@Return         void
 */ /**************************************************************************/
static void RGXInitAXIACE(const void *hPrivate)
{
	IMG_UINT64 ui64RegVal;

	/**
	 * The below configuration is only applicable for RGX core's supporting
	 * ACE/ACE-lite protocol and connected to ACE coherent interconnect.
	 */

	/**
	 * Configure AxDomain and AxCache for MMU transactions.
	 * AxDomain set to non sharable (0x0).
	 */
	ui64RegVal = RGX_CR_ACE_CTRL_MMU_AWCACHE_WRITE_BACK_WRITE_ALLOCATE |
				 RGX_CR_ACE_CTRL_MMU_ARCACHE_WRITE_BACK_READ_ALLOCATE;

	/**
	 * Configure AxCache for PM/MMU transactions.
	 * Set to same value (i.e WBRWALLOC caching, rgxmmunit.c:RGXDerivePTEProt8)
	 * as non-coherent PTEs
	 */
	ui64RegVal |= (IMG_UINT64_C(0xF)) << RGX_CR_ACE_CTRL_PM_MMU_AXCACHE_SHIFT;

	/**
	 * Configure AxDomain for non MMU transactions.
	 */
	ui64RegVal |= (IMG_UINT64)(RGX_CR_ACE_CTRL_COH_DOMAIN_OUTER_SHAREABLE |
							   RGX_CR_ACE_CTRL_NON_COH_DOMAIN_NON_SHAREABLE);

	RGXCommentLog(hPrivate, "Init AXI-ACE interface");
	RGXWriteReg64(hPrivate, RGX_CR_ACE_CTRL, ui64RegVal);
}

static void RGXMercerSoftResetSet(const void *hPrivate, IMG_UINT64 ui32MercerFlags)
{
	RGXWriteReg64(hPrivate, RGX_CR_MERCER_SOFT_RESET, ui32MercerFlags & RGX_CR_MERCER_SOFT_RESET_MASKFULL);

	/* Read soft-reset to fence previous write in order to clear the SOCIF pipeline */
	(void) RGXReadReg64(hPrivate, RGX_CR_MERCER_SOFT_RESET);
}

static void RGXSPUSoftResetAssert(const void *hPrivate)
{
	/* Assert Mercer0 */
	RGXMercerSoftResetSet(hPrivate, RGX_CR_MERCER0_SOFT_RESET_SPU_EN);
	/* Assert Mercer1 */
	RGXMercerSoftResetSet(hPrivate, RGX_CR_MERCER0_SOFT_RESET_SPU_EN | RGX_CR_MERCER1_SOFT_RESET_SPU_EN);
	/* Assert Mercer2 */
	RGXMercerSoftResetSet(hPrivate, RGX_CR_MERCER0_SOFT_RESET_SPU_EN | RGX_CR_MERCER1_SOFT_RESET_SPU_EN | RGX_CR_MERCER2_SOFT_RESET_SPU_EN);

	RGXWriteReg32(hPrivate, RGX_CR_SWIFT_SOFT_RESET, RGX_CR_SWIFT_SOFT_RESET_MASKFULL);
	/* Fence the previous write */
	(void) RGXReadReg32(hPrivate, RGX_CR_SWIFT_SOFT_RESET);

	RGXWriteReg32(hPrivate, RGX_CR_TEXAS_SOFT_RESET, RGX_CR_TEXAS_SOFT_RESET_MASKFULL);
	/* Fence the previous write */
	(void) RGXReadReg32(hPrivate, RGX_CR_TEXAS_SOFT_RESET);
}

static void RGXSPUSoftResetDeAssert(const void *hPrivate)
{
	RGXWriteReg32(hPrivate, RGX_CR_TEXAS_SOFT_RESET, 0);
	/* Fence the previous write */
	(void) RGXReadReg32(hPrivate, RGX_CR_TEXAS_SOFT_RESET);


	RGXWriteReg32(hPrivate, RGX_CR_SWIFT_SOFT_RESET, 0);
	/* Fence the previous write */
	(void) RGXReadReg32(hPrivate, RGX_CR_SWIFT_SOFT_RESET);

	/* Deassert Mercer2 */
	RGXMercerSoftResetSet(hPrivate, RGX_CR_MERCER0_SOFT_RESET_SPU_EN | RGX_CR_MERCER1_SOFT_RESET_SPU_EN);
	/* Deassert Mercer1 */
	RGXMercerSoftResetSet(hPrivate, RGX_CR_MERCER0_SOFT_RESET_SPU_EN);
	/* Deassert Mercer0 */
	RGXMercerSoftResetSet(hPrivate, 0);
}

static void RGXResetSequence(const void *hPrivate, const IMG_CHAR *pcRGXFW_PROCESSOR)
{
	/* Set RGX in soft-reset */
	if (RGX_DEVICE_HAS_FEATURE(hPrivate, RISCV_FW_PROCESSOR))
	{
		RGXCommentLog(hPrivate, "RGXStart: soft reset cpu core");
		RGXWriteReg32(hPrivate, RGX_CR_FWCORE_BOOT, 0);
	}

	RGXCommentLog(hPrivate, "RGXStart: soft reset assert step 1");
	RGXSPUSoftResetAssert(hPrivate);

	RGXCommentLog(hPrivate, "RGXStart: soft reset assert step 2");
	RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_SOFT_RESET_JONES_ALL);

	/* Read soft-reset to fence previous write in order to clear the SOCIF pipeline */
	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

	RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_SOFT_RESET_JONES_ALL | RGX_SOFT_RESET_EXTRA);

	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

	/* Take everything out of reset but the FW processor */
	RGXCommentLog(hPrivate, "RGXStart: soft reset de-assert step 1 excluding %s", pcRGXFW_PROCESSOR);
	RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_SOFT_RESET_EXTRA | RGX_CR_SOFT_RESET_GARTEN_EN);

	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

	RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_GARTEN_EN);

	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

	RGXCommentLog(hPrivate, "RGXStart: soft reset de-assert step 2 excluding %s", pcRGXFW_PROCESSOR);
	RGXSPUSoftResetDeAssert(hPrivate);

	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);
}

static void DeassertMetaReset(const void *hPrivate)
{
	/* Need to wait for at least 16 cycles before taking the FW processor out of reset ... */
	RGXWaitCycles(hPrivate, 32, 3);

	RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, 0x0);
	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

	/* ... and afterwards */
	RGXWaitCycles(hPrivate, 32, 3);
}

static PVRSRV_ERROR InitJonesECCRAM(const void *hPrivate)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32Value;
	IMG_BOOL bMetaFW = RGX_DEVICE_HAS_FEATURE_VALUE(hPrivate, META);
	IMG_UINT32 ui32Mask;

	if (RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, ECC_RAMS) == 0)
	{
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	if (bMetaFW)
	{
		/* META must be taken out of reset (without booting) during Coremem initialization. */
		RGXWriteReg32(hPrivate, RGX_CR_META_BOOT, 0);
		DeassertMetaReset(hPrivate);
	}

	/* Clocks must be set to "on" during RAMs initialization. */
	RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL0, RGX_CR_CLK_CTRL0_ALL_ON);
	RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL1, RGX_CR_CLK_CTRL1_ALL_ON);

	if (bMetaFW)
	{
		RGXWriteMetaRegThroughSP(hPrivate, META_CR_SYSC_JTAG_THREAD, META_CR_SYSC_JTAG_THREAD_PRIV_EN);
		RGXWriteMetaRegThroughSP(hPrivate, META_CR_TXCLKCTRL, META_CR_TXCLKCTRL_ALL_ON);
		RGXReadMetaRegThroughSP(hPrivate, META_CR_TXCLKCTRL, &ui32Value);
	}

	ui32Mask = bMetaFW ?
		RGX_CR_JONES_RAM_INIT_KICK_MASKFULL
		: RGX_CR_JONES_RAM_INIT_KICK_MASKFULL & ~RGX_CR_JONES_RAM_INIT_KICK_GARTEN_EN;
	RGXWriteReg64(hPrivate, RGX_CR_JONES_RAM_INIT_KICK, ui32Mask);
	eError = RGXPollReg64(hPrivate, RGX_CR_JONES_RAM_STATUS, ui32Mask, ui32Mask);

	if (bMetaFW)
	{
		RGXWriteMetaRegThroughSP(hPrivate, META_CR_TXCLKCTRL, META_CR_TXCLKCTRL_ALL_AUTO);
		RGXReadMetaRegThroughSP(hPrivate, META_CR_TXCLKCTRL, &ui32Value);
	}

	RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL0, RGX_CR_CLK_CTRL0_ALL_AUTO);
	RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL1, RGX_CR_CLK_CTRL1_ALL_AUTO);

	if (bMetaFW)
	{
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_GARTEN_EN);
		RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);
	}

	return eError;
}

PVRSRV_ERROR RGXStart(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams = (RGX_LAYER_PARAMS*)hPrivate;
	PVRSRV_RGXDEV_INFO *psDevInfo = psParams->psDevInfo;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bDoFWSlaveBoot = IMG_FALSE;
	IMG_CHAR *pcRGXFW_PROCESSOR;
	IMG_BOOL bMetaFW = IMG_FALSE;

	if (RGX_DEVICE_HAS_FEATURE(hPrivate, RISCV_FW_PROCESSOR))
	{
		pcRGXFW_PROCESSOR = RGXFW_PROCESSOR_RISCV;
		bMetaFW = IMG_FALSE;
		bDoFWSlaveBoot = IMG_FALSE;
	}
	else
	{
		pcRGXFW_PROCESSOR = RGXFW_PROCESSOR_META;
		bMetaFW = IMG_TRUE;
		bDoFWSlaveBoot = RGXDoFWSlaveBoot(hPrivate);
	}

	/* Disable the default sys_bus_secure protection to perform minimal setup */
	RGXWriteReg32(hPrivate, RGX_CR_SYS_BUS_SECURE, 0);
	(void) RGXReadReg32(hPrivate, RGX_CR_SYS_BUS_SECURE);

	/* Only bypass HMMU if the module is present */
	if (RGXDeviceHasFeature(hPrivate, RGX_FEATURE_HYPERVISOR_MMU_BIT_MASK))
	{
		if (PVRSRV_VZ_MODE_IS(NATIVE))
		{
			/* Always set HMMU in bypass mode */
			RGXWriteReg32(hPrivate, RGX_CR_HMMU_BYPASS, RGX_CR_HMMU_BYPASS_MASKFULL);
			(void) RGXReadReg32(hPrivate, RGX_CR_HMMU_BYPASS);
		}
#if defined(PVRSRV_VZ_BYPASS_HMMU)
		if (PVRSRV_VZ_MODE_IS(HOST))
		{
			/* Also set HMMU in bypass mode */
			RGXWriteReg32(hPrivate, RGX_CR_HMMU_BYPASS, RGX_CR_HMMU_BYPASS_MASKFULL);
			(void) RGXReadReg32(hPrivate, RGX_CR_HMMU_BYPASS);
		}
#endif
	}

#if defined(SUPPORT_VALIDATION)
#if !defined(RGX_CR_FIRMWARE_PROCESSOR_LS)
#define RGX_CR_FIRMWARE_PROCESSOR_LS                      (0x01A0U)
#define RGX_CR_FIRMWARE_PROCESSOR_LS_ENABLE_EN            (0x00000001U)
#endif
	{
		if (psDevInfo->ui32ValidationFlags & RGX_VAL_LS_EN)
		{
			/* Set the dual LS mode */
			RGXWriteReg32(hPrivate, RGX_CR_FIRMWARE_PROCESSOR_LS, RGX_CR_FIRMWARE_PROCESSOR_LS_ENABLE_EN);
			(void) RGXReadReg32(hPrivate, RGX_CR_FIRMWARE_PROCESSOR_LS);
		}
	}
#endif

	/*!
	 * Start series8 FW init sequence
	 */
	RGXResetSequence(hPrivate, pcRGXFW_PROCESSOR);

	if (RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, ECC_RAMS) > 0)
	{
		RGXCommentLog(hPrivate, "RGXStart: Init Jones ECC RAM");
		eError = InitJonesECCRAM(hPrivate);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_MULTICORE_SUPPORT))
		{
			/* Set OR reduce for ECC faults to ensure faults are not missed during early boot stages */
			RGXWriteReg32(hPrivate, RGX_CR_MULTICORE_EVENT_REDUCE, RGX_CR_MULTICORE_EVENT_REDUCE_FAULT_FW_EN | RGX_CR_MULTICORE_EVENT_REDUCE_FAULT_GPU_EN);
		}

		/* Route fault events to the host */
		RGXWriteReg32(hPrivate, RGX_CR_EVENT_ENABLE, RGX_CR_EVENT_ENABLE_FAULT_FW_EN);
	}

	if (RGX_DEVICE_HAS_BRN(hPrivate, BRN_66927))
	{
		IMG_UINT64 ui64ClockCtrl;

		ui64ClockCtrl = RGXReadReg64(hPrivate, RGX_CR_CLK_CTRL0);
		CLK_CTRL_FORCE_ON(ui64ClockCtrl, CLK_CTRL0_MCU_L0);
		CLK_CTRL_FORCE_ON(ui64ClockCtrl, CLK_CTRL0_PM);
		CLK_CTRL_FORCE_ON(ui64ClockCtrl, CLK_CTRL0_FBDC);
		RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL0, ui64ClockCtrl);

		ui64ClockCtrl = RGXReadReg64(hPrivate, RGX_CR_CLK_CTRL1);
		CLK_CTRL_FORCE_ON(ui64ClockCtrl, CLK_CTRL1_PIXEL);
		CLK_CTRL_FORCE_ON(ui64ClockCtrl, CLK_CTRL1_GEO_VERTEX);
		RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL1, ui64ClockCtrl);
	}

	if (bMetaFW)
	{
		if (bDoFWSlaveBoot)
		{
			/* Configure META to Slave boot */
			RGXCommentLog(hPrivate, "RGXStart: META Slave boot");
			RGXWriteReg32(hPrivate, RGX_CR_META_BOOT, 0);
		}
		else
		{
			/* Configure META to Master boot */
			RGXCommentLog(hPrivate, "RGXStart: META Master boot");
			RGXWriteReg32(hPrivate, RGX_CR_META_BOOT, RGX_CR_META_BOOT_MODE_EN);
		}
	}

	/*
	 * Initialise Firmware wrapper
	 */
	if (bMetaFW)
	{
		RGXInitMetaProcWrapper(hPrivate);
	}
	else
	{
		RGXInitRiscvProcWrapper(hPrivate);
	}

	if (RGX_GET_FEATURE_VALUE(psDevInfo, MMU_VERSION) >= 4)
	{
		// initialise the MMU range based config registers for Non4K pages.
		RGXInitMMURangeRegisters(hPrivate);
	}

	RGXInitAXIACE(hPrivate);
	/*
	 * Initialise BIF.
	 */
	RGXInitBIF(hPrivate);

	RGXCommentLog(hPrivate, "RGXStart: Take %s out of reset", pcRGXFW_PROCESSOR);
	DeassertMetaReset(hPrivate);

	if (bMetaFW)
	{
		if (bDoFWSlaveBoot)
		{
			eError = RGXFabricCoherencyTest(hPrivate);
			if (eError != PVRSRV_OK) return eError;

			RGXCommentLog(hPrivate, "RGXStart: RGX Firmware Slave boot Start");
			eError = RGXStartFirmware(hPrivate);
			if (eError != PVRSRV_OK) return eError;
		}
		else
		{
			RGXCommentLog(hPrivate, "RGXStart: RGX Firmware Master boot Start");
		}
	}
	else
	{
		/* Bring Debug Module out of reset */
		RGXWriteReg32(hPrivate, RGX_CR_FWCORE_DMI_DMCONTROL, RGX_CR_FWCORE_DMI_DMCONTROL_DMACTIVE_EN);

		/* Boot the FW */
		RGXCommentLog(hPrivate, "RGXStart: RGX Firmware Master boot Start");
		RGXWriteReg32(hPrivate, RGX_CR_FWCORE_BOOT, 1);
		RGXWaitCycles(hPrivate, 32, 3);
	}

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(SUPPORT_SECURITY_VALIDATION)
	RGXCommentLog(hPrivate, "RGXStart: Enable sys_bus_secure");
	RGXWriteReg32(hPrivate, RGX_CR_SYS_BUS_SECURE, RGX_CR_SYS_BUS_SECURE_ENABLE_EN);
	(void) RGXReadReg32(hPrivate, RGX_CR_SYS_BUS_SECURE); /* Fence write */
#endif

	/*!
	 * End series8 FW init sequence
	 */

	return eError;
}

PVRSRV_ERROR RGXStop(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams = (RGX_LAYER_PARAMS*)hPrivate;
	PVRSRV_RGXDEV_INFO *psDevInfo = psParams->psDevInfo;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bMetaFW = RGX_DEVICE_HAS_FEATURE_VALUE(hPrivate, META);
	IMG_UINT32 ui32JonesIdleMask = RGX_CR_JONES_IDLE_MASKFULL^RGX_CR_JONES_IDLE_AXI2IMG_EN;

	RGXDeviceAckIrq(hPrivate);

#if defined(SUPPORT_VALIDATION) && !defined(TC_MEMORY_CONFIG)
#if !defined(RGX_CR_POWER_EVENT)
#define RGX_CR_POWER_EVENT                                (0x0038U)
#define RGX_CR_POWER_EVENT_GPU_MASK_CLRMSK                (IMG_UINT64_C(0x00FFFFFFFFFFFFFF))
#define RGX_CR_POWER_EVENT_GPU_ID_CLRMSK                  (IMG_UINT64_C(0xFFFFFFFFFFFFFF1F))
#define RGX_CR_POWER_EVENT_DOMAIN_SPU0_SHIFT              (9U)
#define RGX_CR_POWER_EVENT_DOMAIN_CLUSTER0_SHIFT          (8U)
#define RGX_CR_POWER_EVENT_DOMAIN_CLUSTER_CLUSTER0_SHIFT  (32U)
#define RGX_CR_POWER_EVENT_TYPE_SHIFT                     (0U)
#define RGX_CR_POWER_EVENT_TYPE_POWER_DOWN                (0x00000000U)
#define RGX_CR_POWER_EVENT_REQ_EN                         (0x00000002U)
#endif

	/* Power off any enabled SPUs */
	if (BITMASK_HAS(psDevInfo->ui32DeviceFlags, RGXKM_DEVICE_STATE_ENABLE_SPU_UNITS_POWER_MASK_CHANGE_EN))
	{
		if (RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, POWER_ISLAND_VERSION) == 3)
		{
			IMG_UINT64 ui64PowUnitOffMask;
			IMG_UINT64 ui64RegVal;

			ui64PowUnitOffMask = (1 << RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, NUM_CLUSTERS)) -1;
			ui64RegVal = (~RGX_CR_POWER_EVENT_GPU_MASK_CLRMSK) | // GPU_MASK specifies all cores
			             (~RGX_CR_POWER_EVENT_GPU_ID_CLRMSK) | // GPU_ID all set means use the GPU_MASK
			             (ui64PowUnitOffMask << RGX_CR_POWER_EVENT_DOMAIN_CLUSTER_CLUSTER0_SHIFT) |
			             RGX_CR_POWER_EVENT_TYPE_POWER_DOWN;

			RGXWriteReg64(hPrivate,
			              RGX_CR_POWER_EVENT,
			              ui64RegVal);

			RGXWriteReg64(hPrivate,
			              RGX_CR_POWER_EVENT,
			              ui64RegVal | RGX_CR_POWER_EVENT_REQ_EN);
		}
		else if (RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, POWER_ISLAND_VERSION) == 2)
		{
			IMG_UINT64 ui64PowUnitOffMask;
			IMG_UINT64 ui64RegVal;

			ui64PowUnitOffMask = (1 << RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, NUM_CLUSTERS)) -1;
			ui64RegVal = (~RGX_CR_POWER_EVENT_GPU_MASK_CLRMSK) | // GPU_MASK specifies all cores
			             (~RGX_CR_POWER_EVENT_GPU_ID_CLRMSK) | // GPU_ID all set means use the GPU_MASK
			             (ui64PowUnitOffMask << RGX_CR_POWER_EVENT_DOMAIN_CLUSTER0_SHIFT) |
			             RGX_CR_POWER_EVENT_TYPE_POWER_DOWN;

			RGXWriteReg64(hPrivate,
			              RGX_CR_POWER_EVENT,
			              ui64RegVal);

			RGXWriteReg64(hPrivate,
			              RGX_CR_POWER_EVENT,
			              ui64RegVal | RGX_CR_POWER_EVENT_REQ_EN);
		}
		else
		{
			IMG_UINT32 ui32PowUnitOffMask;
			IMG_UINT32 ui32RegVal;

			ui32PowUnitOffMask = (1 << RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, NUM_SPU)) -1;
			ui32RegVal = (ui32PowUnitOffMask << RGX_CR_POWER_EVENT_DOMAIN_SPU0_SHIFT) |
			             RGX_CR_POWER_EVENT_TYPE_POWER_DOWN;

			RGXWriteReg32(hPrivate,
			              RGX_CR_POWER_EVENT,
			              ui32RegVal);

			RGXWriteReg32(hPrivate,
			              RGX_CR_POWER_EVENT,
			              ui32RegVal | RGX_CR_POWER_EVENT_REQ_EN);
		}

		/* Poll on complete */
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_EVENT_STATUS,
		                      RGX_CR_EVENT_STATUS_POWER_COMPLETE_EN,
		                      RGX_CR_EVENT_STATUS_POWER_COMPLETE_EN);
		if (eError != PVRSRV_OK) return eError;

		/* Update the SPU_ENABLE mask */
		if (RGX_DEVICE_GET_FEATURE_VALUE(hPrivate, POWER_ISLAND_VERSION) == 1)
		{
			RGXWriteReg32(hPrivate, RGX_CR_SPU_ENABLE, 0);
		}
		RGXWriteReg32(hPrivate, 0xF020, 0);
	}
#endif

	/* Wait for Sidekick/Jones to signal IDLE except for the Garten Wrapper */
	if (!RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, RAY_TRACING_ARCH) ||
	    RGX_GET_FEATURE_VALUE(psDevInfo, RAY_TRACING_ARCH) < 2)
	{
		ui32JonesIdleMask ^= (RGX_CR_JONES_IDLE_ASC_EN|RGX_CR_JONES_IDLE_RCE_EN);
	}

	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_JONES_IDLE,
	                      ui32JonesIdleMask^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN),
	                      ui32JonesIdleMask^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN));

	if (eError != PVRSRV_OK) return eError;


	/* Wait for SLC to signal IDLE */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_SLC_IDLE,
	                      RGX_CR_SLC_IDLE_MASKFULL^(CR_IDLE_UNSELECTED_MASK),
	                      RGX_CR_SLC_IDLE_MASKFULL^(CR_IDLE_UNSELECTED_MASK));
	if (eError != PVRSRV_OK) return eError;


	/* Unset MTS DM association with threads */
	RGXWriteReg32(hPrivate,
	              RGX_CR_MTS_INTCTX_THREAD0_DM_ASSOC,
	              RGX_CR_MTS_INTCTX_THREAD0_DM_ASSOC_DM_ASSOC_CLRMSK
	              & RGX_CR_MTS_INTCTX_THREAD0_DM_ASSOC_MASKFULL);
	RGXWriteReg32(hPrivate,
	              RGX_CR_MTS_BGCTX_THREAD0_DM_ASSOC,
	              RGX_CR_MTS_BGCTX_THREAD0_DM_ASSOC_DM_ASSOC_CLRMSK
	              & RGX_CR_MTS_BGCTX_THREAD0_DM_ASSOC_MASKFULL);
	RGXWriteReg32(hPrivate,
	              RGX_CR_MTS_INTCTX_THREAD1_DM_ASSOC,
	              RGX_CR_MTS_INTCTX_THREAD1_DM_ASSOC_DM_ASSOC_CLRMSK
	              & RGX_CR_MTS_INTCTX_THREAD1_DM_ASSOC_MASKFULL);
	RGXWriteReg32(hPrivate,
	              RGX_CR_MTS_BGCTX_THREAD1_DM_ASSOC,
	              RGX_CR_MTS_BGCTX_THREAD1_DM_ASSOC_DM_ASSOC_CLRMSK
	              & RGX_CR_MTS_BGCTX_THREAD1_DM_ASSOC_MASKFULL);


#if defined(PDUMP)
	if (bMetaFW)
	{
		/* Disabling threads is only required for pdumps to stop the fw gracefully */

		/* Disable thread 0 */
		eError = RGXWriteMetaRegThroughSP(hPrivate,
		                                  META_CR_T0ENABLE_OFFSET,
		                                  ~META_CR_TXENABLE_ENABLE_BIT);
		if (eError != PVRSRV_OK) return eError;

		/* Disable thread 1 */
		eError = RGXWriteMetaRegThroughSP(hPrivate,
		                                  META_CR_T1ENABLE_OFFSET,
		                                  ~META_CR_TXENABLE_ENABLE_BIT);
		if (eError != PVRSRV_OK) return eError;

		/* Wait for the Slave Port to finish all the transactions */
		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, META_REGISTER_UNPACKED_ACCESSES))
		{
			/* Clear down any irq raised by META (done after disabling the FW
			 * threads to avoid a race condition).
			 * This is only really needed for PDumps but we do it anyway driver-live.
			 */
			if (RGX_GET_FEATURE_VALUE(psDevInfo, HOST_SECURITY_VERSION) > 1)
			{
				RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_GT1_AND_METAREG_UNPACKED, 0x0);
				(void)RGXReadReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_GT1_AND_METAREG_UNPACKED); /* Fence write */

				eError = RGXPollReg32(hPrivate,
									  RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED,
									  RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED__READY_EN
									  | RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED__GBLPORT_IDLE_EN,
									  RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED__READY_EN
									  | RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_GT1_AND_METAREG_UNPACKED__GBLPORT_IDLE_EN);
			}
			else
			{
				RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_V1_AND_METAREG_UNPACKED, 0x0);
				(void)RGXReadReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS__HOST_SECURITY_V1_AND_METAREG_UNPACKED); /* Fence write */

				eError = RGXPollReg32(hPrivate,
									  RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED,
									  RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED__READY_EN
									  | RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED__GBLPORT_IDLE_EN,
									  RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED__READY_EN
									  | RGX_CR_META_SP_MSLVCTRL1__HOST_SECURITY_V1_AND_METAREG_UNPACKED__GBLPORT_IDLE_EN);
			}
		}
		else
		{
			/* Clear down any irq raised by META (done after disabling the FW
			 * threads to avoid a race condition).
			 * This is only really needed for PDumps but we do it anyway driver-live.
			 */
			RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS, 0x0);
			(void)RGXReadReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS); /* Fence write */

			eError = RGXPollReg32(hPrivate,
								  RGX_CR_META_SP_MSLVCTRL1,
								  RGX_CR_META_SP_MSLVCTRL1_READY_EN | RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
								  RGX_CR_META_SP_MSLVCTRL1_READY_EN | RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
		}
		if (eError != PVRSRV_OK) return eError;
	}
#endif


	eError = RGXPollReg64(hPrivate,
	                      RGX_CR_SLC_STATUS1,
	                      0,
	                      (~RGX_CR_SLC_STATUS1_BUS0_OUTSTANDING_READS_CLRMSK |
	                       ~RGX_CR_SLC_STATUS1_BUS1_OUTSTANDING_READS_CLRMSK |
	                       ~RGX_CR_SLC_STATUS1_BUS0_OUTSTANDING_WRITES_CLRMSK |
	                       ~RGX_CR_SLC_STATUS1_BUS1_OUTSTANDING_WRITES_CLRMSK));
	if (eError != PVRSRV_OK) return eError;

	eError = RGXPollReg64(hPrivate,
	                      RGX_CR_SLC_STATUS2,
	                      0,
	                      (~RGX_CR_SLC_STATUS2_BUS2_OUTSTANDING_READS_CLRMSK |
	                       ~RGX_CR_SLC_STATUS2_BUS3_OUTSTANDING_READS_CLRMSK |
	                       ~RGX_CR_SLC_STATUS2_BUS2_OUTSTANDING_WRITES_CLRMSK |
	                       ~RGX_CR_SLC_STATUS2_BUS3_OUTSTANDING_WRITES_CLRMSK));
	if (eError != PVRSRV_OK) return eError;


	/* Wait for SLC to signal IDLE */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_SLC_IDLE,
	                      RGX_CR_SLC_IDLE_MASKFULL^(CR_IDLE_UNSELECTED_MASK),
	                      RGX_CR_SLC_IDLE_MASKFULL^(CR_IDLE_UNSELECTED_MASK));
	if (eError != PVRSRV_OK) return eError;


	/* Wait for Jones to signal IDLE except for the Garten Wrapper */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_JONES_IDLE,
	                      ui32JonesIdleMask^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN),
	                      ui32JonesIdleMask^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN));

	if (eError != PVRSRV_OK) return eError;


	if (bMetaFW)
	{
		IMG_UINT32 ui32RegValue;

		eError = RGXReadMetaRegThroughSP(hPrivate,
		                                 META_CR_TxVECINT_BHALT,
		                                 &ui32RegValue);
		if (eError != PVRSRV_OK) return eError;

		if ((ui32RegValue & 0xFFFFFFFFU) == 0x0)
		{
			/* Wait for Sidekick/Jones to signal IDLE including
			 * the Garten Wrapper if there is no debugger attached
			 * (TxVECINT_BHALT = 0x0) */
			eError = RGXPollReg32(hPrivate,
			                      RGX_CR_JONES_IDLE,
			                      ui32JonesIdleMask^RGX_CR_JONES_IDLE_SOCIF_EN,
			                      ui32JonesIdleMask^RGX_CR_JONES_IDLE_SOCIF_EN);
			if (eError != PVRSRV_OK) return eError;
		}
	}
	else
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_JONES_IDLE,
		                      ui32JonesIdleMask^RGX_CR_JONES_IDLE_SOCIF_EN,
		                      ui32JonesIdleMask^RGX_CR_JONES_IDLE_SOCIF_EN);
		if (eError != PVRSRV_OK) return eError;
	}

	return eError;
}
