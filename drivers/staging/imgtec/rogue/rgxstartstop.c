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
 * Any new dependency should be added to rgxlayer_km.h.
 * Any new code should be built on top of the existing abstraction layer,
 * which should be extended when necessary. */
#include "rgxstartstop.h"

#if defined(SUPPORT_SHARED_SLC)
#include "rgxapi_km.h"
#include "rgxdevice.h"
#endif

#define SOC_FEATURE_STRICT_SAME_ADDRESS_WRITE_ORDERING


#if !defined(FIX_HW_BRN_37453)
/*!
*******************************************************************************

 @Function      RGXEnableClocks

 @Description   Enable RGX Clocks

 @Input         hPrivate  : Implementation specific data

 @Return        void

******************************************************************************/
static void RGXEnableClocks(const void *hPrivate)
{
	RGXCommentLogPower(hPrivate, "RGX clock: use default (automatic clock gating)");
}
#endif


static PVRSRV_ERROR RGXWriteMetaRegThroughSP(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Wait for Slave Port to be Ready */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_META_SP_MSLVCTRL1,
	                      RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	                      RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
	if (eError != PVRSRV_OK) return eError;

	/* Issue a Write */
	RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVCTRL0, ui32RegAddr);
	RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVDATAT, ui32RegValue);

	return eError;
}

static PVRSRV_ERROR RGXReadMetaRegThroughSP(const void *hPrivate,
                                            IMG_UINT32 ui32RegAddr,
                                            IMG_UINT32* ui32RegValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Wait for Slave Port to be Ready */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_META_SP_MSLVCTRL1,
	                      RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	                      RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
	if (eError != PVRSRV_OK) return eError;

	/* Issue a Read */
	RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVCTRL0, ui32RegAddr | RGX_CR_META_SP_MSLVCTRL0_RD_EN);

	/* Wait for Slave Port to be Ready */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_META_SP_MSLVCTRL1,
	                      RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	                      RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
	if (eError != PVRSRV_OK) return eError;

#if !defined(NO_HARDWARE)
	*ui32RegValue = RGXReadReg32(hPrivate, RGX_CR_META_SP_MSLVDATAX);
#else
	*ui32RegValue = 0xFFFFFFFF;
#endif

	return eError;
}

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
		RGXCommentLogPower(hPrivate, "RGXWriteMetaCoreRegThoughSP: Timeout");
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
		RGXCommentLogPower(hPrivate, "RGXStart: RGX Firmware Slave boot Start failed!");
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

	/* Set Garten IDLE to META idle and Set the Garten Wrapper BIF Fence address */

	/* Garten IDLE bit controlled by META */
	ui64GartenConfig = RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META;

	/* The fence addr is set at the fw init sequence */

	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		/* Set PC = 0 for fences */
		ui64GartenConfig &= RGX_CR_MTS_GARTEN_WRAPPER_CONFIG__S7_TOP__FENCE_PC_BASE_CLRMSK;
		ui64GartenConfig |= (IMG_UINT64)META_MMU_CONTEXT_MAPPING
		                    << RGX_CR_MTS_GARTEN_WRAPPER_CONFIG__S7_TOP__FENCE_PC_BASE_SHIFT;

		if (!RGXDeviceHasErnBrnPower(hPrivate, FIX_HW_BRN_51281_BIT_MASK))
		{
			/* Ensure the META fences go all the way to external memory */
			ui64GartenConfig |= RGX_CR_MTS_GARTEN_WRAPPER_CONFIG__S7_TOP__FENCE_SLC_COHERENT_EN;    /* SLC Coherent 1 */
			ui64GartenConfig &= RGX_CR_MTS_GARTEN_WRAPPER_CONFIG__S7_TOP__FENCE_PERSISTENCE_CLRMSK; /* SLC Persistence 0 */
		}
	}
	else
	{
		/* Set PC = 0 for fences */
		ui64GartenConfig &= RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_FENCE_PC_BASE_CLRMSK;
		ui64GartenConfig |= (IMG_UINT64)META_MMU_CONTEXT_MAPPING
		                    << RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_FENCE_PC_BASE_SHIFT;

		/* Set SLC DM=META */
		ui64GartenConfig |= ((IMG_UINT64) RGXFW_SEGMMU_META_DM_ID) << RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_FENCE_DM_SHIFT;
	}

	RGXCommentLogPower(hPrivate, "RGXStart: Configure META wrapper");
	RGXWriteReg64(hPrivate, RGX_CR_MTS_GARTEN_WRAPPER_CONFIG, ui64GartenConfig);
}


/*!
*******************************************************************************

 @Function      RGXInitMipsProcWrapper

 @Description   Configures the hardware wrapper of the MIPS processor

 @Input         hPrivate  : Implementation specific data

 @Return        void

******************************************************************************/
static void RGXInitMipsProcWrapper(const void *hPrivate)
{
	IMG_DEV_PHYADDR sPhyAddr;
	IMG_UINT64 ui64RemapSettings = RGXMIPSFW_BOOT_REMAP_LOG2_SEGMENT_SIZE; /* Same for all remap registers */

	RGXCommentLogPower(hPrivate, "RGXStart: Configure MIPS wrapper");

	/*
	 * MIPS wrapper (registers transaction ID and ISA mode) setup
	 */

	RGXAcquireGPURegsAddr(hPrivate, &sPhyAddr);

	RGXCommentLogPower(hPrivate, "RGXStart: Write wrapper config register");
	RGXMIPSWrapperConfig(hPrivate,
	                     RGX_CR_MIPS_WRAPPER_CONFIG,
	                     sPhyAddr.uiAddr,
	                     RGXMIPSFW_WRAPPER_CONFIG_REGBANK_ADDR_ALIGN,
	                     RGX_CR_MIPS_WRAPPER_CONFIG_BOOT_ISA_MODE_MICROMIPS);

	/*
	 * Boot remap setup
	 */

	RGXAcquireBootRemapAddr(hPrivate, &sPhyAddr);

#if defined(SUPPORT_TRUSTED_DEVICE)
	/* Do not mark accesses to a FW code remap region as DRM accesses */
	ui64RemapSettings &= RGX_CR_MIPS_ADDR_REMAP1_CONFIG2_TRUSTED_CLRMSK;
#endif

	RGXCommentLogPower(hPrivate, "RGXStart: Write boot remap registers");
	RGXBootRemapConfig(hPrivate,
	                   RGX_CR_MIPS_ADDR_REMAP1_CONFIG1,
	                   RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN | RGX_CR_MIPS_ADDR_REMAP1_CONFIG1_MODE_ENABLE_EN,
	                   RGX_CR_MIPS_ADDR_REMAP1_CONFIG2,
	                   sPhyAddr.uiAddr,
	                   ~RGX_CR_MIPS_ADDR_REMAP1_CONFIG2_ADDR_OUT_CLRMSK,
	                   ui64RemapSettings);

	/*
	 * Data remap setup
	 */

	RGXAcquireDataRemapAddr(hPrivate, &sPhyAddr);

#if defined(SUPPORT_TRUSTED_DEVICE)
	/* Remapped data in non-secure memory */
	ui64RemapSettings &= RGX_CR_MIPS_ADDR_REMAP1_CONFIG2_TRUSTED_CLRMSK;
#endif

	RGXCommentLogPower(hPrivate, "RGXStart: Write data remap registers");
	RGXDataRemapConfig(hPrivate,
	                   RGX_CR_MIPS_ADDR_REMAP2_CONFIG1,
	                   RGXMIPSFW_DATA_REMAP_PHYS_ADDR_IN | RGX_CR_MIPS_ADDR_REMAP2_CONFIG1_MODE_ENABLE_EN,
	                   RGX_CR_MIPS_ADDR_REMAP2_CONFIG2,
	                   sPhyAddr.uiAddr,
	                   ~RGX_CR_MIPS_ADDR_REMAP2_CONFIG2_ADDR_OUT_CLRMSK,
	                   ui64RemapSettings);

	/*
	 * Code remap setup
	 */

	RGXAcquireCodeRemapAddr(hPrivate, &sPhyAddr);

#if defined(SUPPORT_TRUSTED_DEVICE)
	/* Do not mark accesses to a FW code remap region as DRM accesses */
	ui64RemapSettings &= RGX_CR_MIPS_ADDR_REMAP1_CONFIG2_TRUSTED_CLRMSK;
#endif

	RGXCommentLogPower(hPrivate, "RGXStart: Write exceptions remap registers");
	RGXCodeRemapConfig(hPrivate,
	                   RGX_CR_MIPS_ADDR_REMAP3_CONFIG1,
	                   RGXMIPSFW_CODE_REMAP_PHYS_ADDR_IN | RGX_CR_MIPS_ADDR_REMAP3_CONFIG1_MODE_ENABLE_EN,
	                   RGX_CR_MIPS_ADDR_REMAP3_CONFIG2,
	                   sPhyAddr.uiAddr,
	                   ~RGX_CR_MIPS_ADDR_REMAP3_CONFIG2_ADDR_OUT_CLRMSK,
	                   ui64RemapSettings);

	/*
	 * Trampoline remap setup
	 */

	RGXAcquireTrampolineRemapAddr(hPrivate, &sPhyAddr);
	ui64RemapSettings = RGXMIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE;

#if defined(SUPPORT_TRUSTED_DEVICE)
	/* Remapped data in non-secure memory */
	ui64RemapSettings &= RGX_CR_MIPS_ADDR_REMAP1_CONFIG2_TRUSTED_CLRMSK;
#endif

	RGXCommentLogPower(hPrivate, "RGXStart: Write trampoline remap registers");
	RGXTrampolineRemapConfig(hPrivate,
	                   RGX_CR_MIPS_ADDR_REMAP4_CONFIG1,
	                   sPhyAddr.uiAddr | RGX_CR_MIPS_ADDR_REMAP4_CONFIG1_MODE_ENABLE_EN,
	                   RGX_CR_MIPS_ADDR_REMAP4_CONFIG2,
	                   RGXMIPSFW_TRAMPOLINE_TARGET_PHYS_ADDR,
	                   ~RGX_CR_MIPS_ADDR_REMAP4_CONFIG2_ADDR_OUT_CLRMSK,
	                   ui64RemapSettings);

	/* Garten IDLE bit controlled by MIPS */
	RGXCommentLogPower(hPrivate, "RGXStart: Set GARTEN_IDLE type to MIPS");
	RGXWriteReg64(hPrivate, RGX_CR_MTS_GARTEN_WRAPPER_CONFIG, RGX_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META);

	/* Turn on the EJTAG probe (only useful driver live) */
	RGXWriteReg32(hPrivate, RGX_CR_MIPS_DEBUG_CONFIG, 0);
}


/*!
*******************************************************************************

 @Function      __RGXInitSLC

 @Description   Initialise RGX SLC

 @Input         hPrivate  : Implementation specific data

 @Return        void

******************************************************************************/
static void __RGXInitSLC(const void *hPrivate)
{
	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_CACHE_HIERARCHY_BIT_MASK))
	{
		IMG_UINT32 ui32Reg;
		IMG_UINT32 ui32RegVal;

		if (RGXDeviceHasErnBrnPower(hPrivate, HW_ERN_51468_BIT_MASK))
		{
			/*
			 * SLC control
			 */
			ui32Reg = RGX_CR_SLC3_CTRL_MISC;
			ui32RegVal = RGX_CR_SLC3_CTRL_MISC_ADDR_DECODE_MODE_WEAVED_HASH |
			             RGX_CR_SLC3_CTRL_MISC_WRITE_COMBINER_EN;
			RGXWriteReg32(hPrivate, ui32Reg, ui32RegVal);
		}
		else
		{
			/*
			 * SLC control
			 */
			ui32Reg = RGX_CR_SLC3_CTRL_MISC;
			ui32RegVal = RGX_CR_SLC3_CTRL_MISC_ADDR_DECODE_MODE_SCRAMBLE_PVR_HASH |
			             RGX_CR_SLC3_CTRL_MISC_WRITE_COMBINER_EN;
			RGXWriteReg32(hPrivate, ui32Reg, ui32RegVal);

			/*
			 * SLC scramble bits
			 */
			{
				IMG_UINT32 i;
				IMG_UINT32 ui32Count=0;
				IMG_UINT32 ui32SLCBanks = RGXGetDeviceSLCBanks(hPrivate);
				IMG_UINT64 aui64ScrambleValues[4];
				IMG_UINT32 aui32ScrambleRegs[] = {
					RGX_CR_SLC3_SCRAMBLE,
					RGX_CR_SLC3_SCRAMBLE2,
					RGX_CR_SLC3_SCRAMBLE3,
					RGX_CR_SLC3_SCRAMBLE4
				};

				if (2 == ui32SLCBanks)
				{
					aui64ScrambleValues[0] = IMG_UINT64_C(0x6965a99a55696a6a);
					aui64ScrambleValues[1] = IMG_UINT64_C(0x6aa9aa66959aaa9a);
					aui64ScrambleValues[2] = IMG_UINT64_C(0x9a5665965a99a566);
					aui64ScrambleValues[3] = IMG_UINT64_C(0x5aa69596aa66669a);
					ui32Count = 4;
				}
				else if (4 == ui32SLCBanks)
				{
					aui64ScrambleValues[0] = IMG_UINT64_C(0xc6788d722dd29ce4);
					aui64ScrambleValues[1] = IMG_UINT64_C(0x7272e4e11b279372);
					aui64ScrambleValues[2] = IMG_UINT64_C(0x87d872d26c6c4be1);
					aui64ScrambleValues[3] = IMG_UINT64_C(0xe1b4878d4b36e478);
					ui32Count = 4;

				}
				else if (8 == ui32SLCBanks)
				{
					aui64ScrambleValues[0] = IMG_UINT64_C(0x859d6569e8fac688);
					aui64ScrambleValues[1] = IMG_UINT64_C(0xf285e1eae4299d33);
					aui64ScrambleValues[2] = IMG_UINT64_C(0x1e1af2be3c0aa447);
					ui32Count = 3;
				}

				for (i = 0; i < ui32Count; i++)
				{
					IMG_UINT32 ui32Reg = aui32ScrambleRegs[i];
					IMG_UINT64 ui64Value = aui64ScrambleValues[i];
					RGXWriteReg64(hPrivate, ui32Reg, ui64Value);
				}
			}
		}

		if (RGXDeviceHasErnBrnPower(hPrivate, HW_ERN_45914_BIT_MASK))
		{
			/* Disable the forced SLC coherency which the hardware enables for compatibility with older pdumps */
			RGXCommentLogPower(hPrivate, "Disable forced SLC coherency");
			RGXWriteReg64(hPrivate, RGX_CR_GARTEN_SLC, 0);
		}
	}
	else
	{
		IMG_UINT32 ui32Reg;
		IMG_UINT32 ui32RegVal;

#if defined(FIX_HW_BRN_36492)
		/* Because the WA for this BRN forbids using SLC reset, need to inval it instead */
		RGXCommentLogPower(hPrivate, "Invalidate the SLC");
		RGXWriteReg32(hPrivate, RGX_CR_SLC_CTRL_FLUSH_INVAL, RGX_CR_SLC_CTRL_FLUSH_INVAL_ALL_EN);

		/* Poll for completion */
		RGXPollReg32(hPrivate, RGX_CR_SLC_STATUS0, 0x0, RGX_CR_SLC_STATUS0_MASKFULL);
#endif

		/*
		 * SLC Bypass control
		 */
		ui32Reg = RGX_CR_SLC_CTRL_BYPASS;
		ui32RegVal = 0;

		if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_SLCSIZE8_BIT_MASK)  ||
		    RGXDeviceHasErnBrnPower(hPrivate, FIX_HW_BRN_61450_BIT_MASK))
		{
			RGXCommentLogPower(hPrivate, "Bypass SLC for IPF_OBJ and IPF_CPF");
			ui32RegVal |= RGX_CR_SLC_CTRL_BYPASS_REQ_IPF_OBJ_EN | RGX_CR_SLC_CTRL_BYPASS_REQ_IPF_CPF_EN;
		}

		if (RGXGetDeviceSLCSize(hPrivate) < (128*1024))
		{
			/* Bypass SLC for textures if the SLC size is less than 128kB */
			RGXCommentLogPower(hPrivate, "Bypass SLC for TPU");
			ui32RegVal |= RGX_CR_SLC_CTRL_BYPASS_REQ_TPU_EN;
		}

		if (ui32RegVal != 0)
		{
			RGXWriteReg32(hPrivate, ui32Reg, ui32RegVal);
		}

		/*
		 * SLC Misc control.
		 *
		 * Note: This is a 64bit register and we set only the lower 32bits leaving the top
		 *       32bits (RGX_CR_SLC_CTRL_MISC_SCRAMBLE_BITS) unchanged from the HW default.
		 */
		ui32Reg = RGX_CR_SLC_CTRL_MISC;
		ui32RegVal = (RGXReadReg32(hPrivate, ui32Reg) & RGX_CR_SLC_CTRL_MISC_ENABLE_PSG_HAZARD_CHECK_EN) |		
		             RGX_CR_SLC_CTRL_MISC_ADDR_DECODE_MODE_PVR_HASH1;
		if (RGXDeviceHasErnBrnPower(hPrivate, FIX_HW_BRN_60084_BIT_MASK))
		{
#if !defined(SOC_FEATURE_STRICT_SAME_ADDRESS_WRITE_ORDERING)
			ui32RegVal |= RGX_CR_SLC_CTRL_MISC_ENABLE_PSG_HAZARD_CHECK_EN;
#else
			if (RGXDeviceHasErnBrnPower(hPrivate, HW_ERN_61389_BIT_MASK))
			{
				ui32RegVal |= RGX_CR_SLC_CTRL_MISC_ENABLE_PSG_HAZARD_CHECK_EN;
			}
#endif
		}
		/* Bypass burst combiner if SLC line size is smaller than 1024 bits */
		if (RGXGetDeviceCacheLineSize(hPrivate) < 1024)
		{
			ui32RegVal |= RGX_CR_SLC_CTRL_MISC_BYPASS_BURST_COMBINER_EN;
		}

		RGXWriteReg32(hPrivate, ui32Reg, ui32RegVal);
	}
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
	if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_MIPS_BIT_MASK))
	{
		IMG_DEV_PHYADDR sPCAddr;

		/*
		 * Acquire the address of the Kernel Page Catalogue.
		 */
		RGXAcquireKernelMMUPC(hPrivate, &sPCAddr);

		/*
		 * Write the kernel catalogue base.
		 */
		RGXCommentLogPower(hPrivate, "RGX firmware MMU Page Catalogue");

		if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_SLC_VIVT_BIT_MASK))
		{
			/* Write the cat-base address */
			RGXWriteKernelMMUPC64(hPrivate,
			                      RGX_CR_BIF_CAT_BASE0,
			                      RGX_CR_BIF_CAT_BASE0_ADDR_ALIGNSHIFT,
			                      RGX_CR_BIF_CAT_BASE0_ADDR_SHIFT,
			                      ((sPCAddr.uiAddr
			                      >> RGX_CR_BIF_CAT_BASE0_ADDR_ALIGNSHIFT)
			                      << RGX_CR_BIF_CAT_BASE0_ADDR_SHIFT)
			                      & ~RGX_CR_BIF_CAT_BASE0_ADDR_CLRMSK);
			/*
			 * Trusted Firmware boot
			 */
#if defined(SUPPORT_TRUSTED_DEVICE)
			RGXCommentLogPower(hPrivate, "RGXInitBIF: Trusted Device enabled");
			RGXWriteReg32(hPrivate, RGX_CR_BIF_TRUST, RGX_CR_BIF_TRUST_ENABLE_EN);
#endif
		}
		else
		{
			IMG_UINT32 uiPCAddr;
			uiPCAddr = (((sPCAddr.uiAddr >> RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT)
			             << RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT)
			            & ~RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_CLRMSK);
			/* Set the mapping context */
			RGXWriteReg32(hPrivate, RGX_CR_MMU_CBASE_MAPPING_CONTEXT, 0);

			/* Write the cat-base address */
			RGXWriteKernelMMUPC32(hPrivate,
			                      RGX_CR_MMU_CBASE_MAPPING,
			                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT,
			                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT,
			                      uiPCAddr);
#if defined(SUPPORT_TRUSTED_DEVICE)
			/* Set-up MMU ID 1 mapping to the same PC used by MMU ID 0 */
			RGXWriteReg32(hPrivate, RGX_CR_MMU_CBASE_MAPPING_CONTEXT, 1);
			RGXWriteKernelMMUPC32(hPrivate,
			                      RGX_CR_MMU_CBASE_MAPPING,
			                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT,
			                      RGX_CR_MMU_CBASE_MAPPING_BASE_ADDR_SHIFT,
			                      uiPCAddr);
#endif /* SUPPORT_TRUSTED_DEVICE */
		}
	}
	else
	{
		/*
		 * Trusted Firmware boot
		 */
#if defined(SUPPORT_TRUSTED_DEVICE)
		RGXCommentLogPower(hPrivate, "RGXInitBIF: Trusted Device enabled");
		RGXWriteReg32(hPrivate, RGX_CR_BIF_TRUST, RGX_CR_BIF_TRUST_ENABLE_EN);
#endif
	}
}


/*!
*******************************************************************************

 @Function      RGXAXIACELiteInit

 @Description   Initialise AXI-ACE Lite interface

 @Input         hPrivate : Implementation specific data

 @Return        void

******************************************************************************/
static void RGXAXIACELiteInit(const void *hPrivate)
{
	IMG_UINT32 ui32RegAddr;
	IMG_UINT64 ui64RegVal;

	ui32RegAddr = RGX_CR_AXI_ACE_LITE_CONFIGURATION;

	/* Setup AXI-ACE config. Set everything to outer cache */
	ui64RegVal = (3U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_AWDOMAIN_NON_SNOOPING_SHIFT) |
	             (3U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_NON_SNOOPING_SHIFT) |
	             (2U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_CACHE_MAINTENANCE_SHIFT)  |
	             (2U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_AWDOMAIN_COHERENT_SHIFT) |
	             (2U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_COHERENT_SHIFT) |
	             (2U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_AWCACHE_COHERENT_SHIFT) |
	             (2U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_ARCACHE_COHERENT_SHIFT) |
	             (2U << RGX_CR_AXI_ACE_LITE_CONFIGURATION_ARCACHE_CACHE_MAINTENANCE_SHIFT);

	if (RGXDeviceHasErnBrnPower(hPrivate, FIX_HW_BRN_42321_BIT_MASK))
	{
		ui64RegVal |= (((IMG_UINT64) 1) << RGX_CR_AXI_ACE_LITE_CONFIGURATION_DISABLE_COHERENT_WRITELINEUNIQUE_SHIFT);
	}

#if defined(SUPPORT_TRUSTED_DEVICE)
	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_SLC_VIVT_BIT_MASK))
	{
		RGXCommentLogPower(hPrivate, "OSID 0 and 1 are trusted");
		ui64RegVal |= IMG_UINT64_C(0xFC)
	              << RGX_CR_AXI_ACE_LITE_CONFIGURATION_OSID_SECURITY_SHIFT;
	}
#endif

	RGXCommentLogPower(hPrivate, "Init AXI-ACE interface");
	RGXWriteReg64(hPrivate, ui32RegAddr, ui64RegVal);
}


PVRSRV_ERROR RGXStart(const void *hPrivate)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bDoFWSlaveBoot;
	IMG_CHAR *pcRGXFW_PROCESSOR;
	IMG_BOOL bMetaFW;

	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_MIPS_BIT_MASK))
	{
		pcRGXFW_PROCESSOR = RGXFW_PROCESSOR_MIPS;
		bMetaFW = IMG_FALSE;
		bDoFWSlaveBoot = IMG_FALSE;
	}
	else
	{
		pcRGXFW_PROCESSOR = RGXFW_PROCESSOR_META;
		bMetaFW = IMG_TRUE;
		bDoFWSlaveBoot = RGXDoFWSlaveBoot(hPrivate);
	}

	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_SYS_BUS_SECURE_RESET_BIT_MASK))
	{
		/* Disable the default sys_bus_secure protection to perform minimal setup */
		RGXCommentLogPower(hPrivate, "RGXStart: Disable sys_bus_secure");
		RGXWriteReg32(hPrivate, RGX_CR_SYS_BUS_SECURE, 0);
		(void) RGXReadReg32(hPrivate, RGX_CR_SYS_BUS_SECURE); /* Fence write */
	}

#if defined(FIX_HW_BRN_37453)
	/* Force all clocks on*/
	RGXCommentLogPower(hPrivate, "RGXStart: force all clocks on");
	RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL, RGX_CR_CLK_CTRL_ALL_ON);
#endif

#if defined(SUPPORT_SHARED_SLC) && !defined(FIX_HW_BRN_36492)
	/* When the SLC is shared, the SLC reset is performed by the System layer when calling
	 * RGXInitSLC (before any device uses it), therefore mask out the SLC bit to avoid
	 * soft_resetting it here. If HW_BRN_36492, the bit is already masked out.
	 */
#define RGX_CR_SOFT_RESET_ALL  (RGX_CR_SOFT_RESET_MASKFULL ^ RGX_CR_SOFT_RESET_SLC_EN)
	RGXCommentLogPower(hPrivate, "RGXStart: Shared SLC (don't reset SLC as part of RGX reset)");
#else
#define RGX_CR_SOFT_RESET_ALL  (RGX_CR_SOFT_RESET_MASKFULL)
#endif

	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		/* Set RGX in soft-reset */
		RGXCommentLogPower(hPrivate, "RGXStart: soft reset assert step 1");
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_S7_SOFT_RESET_DUSTS);

		RGXCommentLogPower(hPrivate, "RGXStart: soft reset assert step 2");
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_S7_SOFT_RESET_JONES_ALL | RGX_S7_SOFT_RESET_DUSTS);
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET2, RGX_S7_SOFT_RESET2);

		/* Read soft-reset to fence previous write in order to clear the SOCIF pipeline */
		(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

		/* Take everything out of reset but META/MIPS */
		RGXCommentLogPower(hPrivate, "RGXStart: soft reset de-assert step 1 excluding %s", pcRGXFW_PROCESSOR);
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_S7_SOFT_RESET_DUSTS | RGX_CR_SOFT_RESET_GARTEN_EN);
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET2, 0x0);

		(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

		RGXCommentLogPower(hPrivate, "RGXStart: soft reset de-assert step 2 excluding %s", pcRGXFW_PROCESSOR);
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_GARTEN_EN);

		(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);
	}
	else
	{
		/* Set RGX in soft-reset */
		RGXCommentLogPower(hPrivate, "RGXStart: soft reset everything");
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_ALL);

		/* Take Rascal and Dust out of reset */
		RGXCommentLogPower(hPrivate, "RGXStart: Rascal and Dust out of reset");
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_ALL ^ RGX_CR_SOFT_RESET_RASCALDUSTS_EN);

		/* Read soft-reset to fence previous write in order to clear the SOCIF pipeline */
		(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

		/* Take everything out of reset but META/MIPS */
		RGXCommentLogPower(hPrivate, "RGXStart: Take everything out of reset but %s", pcRGXFW_PROCESSOR);
		RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_GARTEN_EN);
	}


#if !defined(FIX_HW_BRN_37453)
	/* Enable clocks */
	RGXEnableClocks(hPrivate);
#endif

	/*
	 * Initialise SLC.
	 */
#if !defined(SUPPORT_SHARED_SLC)
	__RGXInitSLC(hPrivate);
#endif

	if (bMetaFW)
	{
		if (bDoFWSlaveBoot)
		{
			/* Configure META to Slave boot */
			RGXCommentLogPower(hPrivate, "RGXStart: META Slave boot");
			RGXWriteReg32(hPrivate, RGX_CR_META_BOOT, 0);

		}
		else
		{
			/* Configure META to Master boot */
			RGXCommentLogPower(hPrivate, "RGXStart: META Master boot");
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
		RGXInitMipsProcWrapper(hPrivate);
	}

	if (RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_AXI_ACELITE_BIT_MASK))
	{
		/* We must init the AXI-ACE interface before 1st BIF transaction */
		RGXAXIACELiteInit(hPrivate);
	}

	/*
	 * Initialise BIF.
	 */
	RGXInitBIF(hPrivate);

	RGXCommentLogPower(hPrivate, "RGXStart: Take %s out of reset", pcRGXFW_PROCESSOR);

	/* Need to wait for at least 16 cycles before taking META/MIPS out of reset ... */
	RGXWaitCycles(hPrivate, 32, 3);

	RGXWriteReg64(hPrivate, RGX_CR_SOFT_RESET, 0x0);
	(void) RGXReadReg64(hPrivate, RGX_CR_SOFT_RESET);

	/* ... and afterwards */
	RGXWaitCycles(hPrivate, 32, 3);

#if defined(FIX_HW_BRN_37453)
	/* We rely on the 32 clk sleep from above */

	/* Switch clocks back to auto */
	RGXCommentLogPower(hPrivate, "RGXStart: set clocks back to auto");
	RGXWriteReg64(hPrivate, RGX_CR_CLK_CTRL, RGX_CR_CLK_CTRL_ALL_AUTO);
#endif

	if (bMetaFW && bDoFWSlaveBoot)
	{
		eError = RGXIOCoherencyTest(hPrivate);
		if (eError != PVRSRV_OK) return eError;

		RGXCommentLogPower(hPrivate, "RGXStart: RGX Firmware Slave boot Start");
		eError = RGXStartFirmware(hPrivate);
		if (eError != PVRSRV_OK) return eError;
	}
	else
	{
		RGXCommentLogPower(hPrivate, "RGXStart: RGX Firmware Master boot Start");
	}

	/* Enable Sys Bus security */
#if defined(SUPPORT_TRUSTED_DEVICE)
	RGXCommentLogPower(hPrivate, "RGXStart: Enable sys_bus_secure");
	RGXWriteReg32(hPrivate, RGX_CR_SYS_BUS_SECURE, RGX_CR_SYS_BUS_SECURE_ENABLE_EN);
	(void) RGXReadReg32(hPrivate, RGX_CR_SYS_BUS_SECURE); /* Fence write */
#endif

	return eError;
}

static INLINE void ClearIRQStatusRegister(const void *hPrivate, IMG_BOOL bMetaFW)
{
	IMG_UINT32 ui32IRQClearReg;
	IMG_UINT32 ui32IRQClearMask;

	if(bMetaFW)
	{
		ui32IRQClearReg = RGX_CR_META_SP_MSLVIRQSTATUS;
		ui32IRQClearMask = RGX_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_CLRMSK;
	}
	else
	{
		ui32IRQClearReg = RGX_CR_MIPS_WRAPPER_IRQ_CLEAR;
		ui32IRQClearMask = RGX_CR_MIPS_WRAPPER_IRQ_CLEAR_EVENT_EN;
	}

	RGXWriteReg32(hPrivate, ui32IRQClearReg, ui32IRQClearMask);

#if defined(RGX_FEATURE_OCPBUS)
	RGXWriteReg32(hPrivate, RGX_CR_OCP_IRQSTATUS_2, RGX_CR_OCP_IRQSTATUS_2_RGX_IRQ_STATUS_EN);
#endif
}

PVRSRV_ERROR RGXStop(const void *hPrivate)
{
	IMG_BOOL bMetaFW = !RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_MIPS_BIT_MASK);
	PVRSRV_ERROR eError;

	ClearIRQStatusRegister(hPrivate, bMetaFW);

	/* Wait for Sidekick/Jones to signal IDLE except for the Garten Wrapper */
	if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SIDEKICK_IDLE,
		                      RGX_CR_SIDEKICK_IDLE_MASKFULL^(RGX_CR_SIDEKICK_IDLE_GARTEN_EN|RGX_CR_SIDEKICK_IDLE_SOCIF_EN|RGX_CR_SIDEKICK_IDLE_HOSTIF_EN),
		                      RGX_CR_SIDEKICK_IDLE_MASKFULL^(RGX_CR_SIDEKICK_IDLE_GARTEN_EN|RGX_CR_SIDEKICK_IDLE_SOCIF_EN|RGX_CR_SIDEKICK_IDLE_HOSTIF_EN));
	}
	else
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_JONES_IDLE,
		                      RGX_CR_JONES_IDLE_MASKFULL^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN|RGX_CR_JONES_IDLE_HOSTIF_EN),
		                      RGX_CR_JONES_IDLE_MASKFULL^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN|RGX_CR_JONES_IDLE_HOSTIF_EN));
	}

	if (eError != PVRSRV_OK) return eError;


#if !defined(SUPPORT_SHARED_SLC)
	/* Wait for SLC to signal IDLE */
	if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SLC_IDLE,
		                      RGX_CR_SLC_IDLE_MASKFULL,
		                      RGX_CR_SLC_IDLE_MASKFULL);
	}
	else
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SLC3_IDLE,
		                      RGX_CR_SLC3_IDLE_MASKFULL,
		                      RGX_CR_SLC3_IDLE_MASKFULL);
	}
#endif /* SUPPORT_SHARED_SLC */
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

		/* Clear down any irq raised by META (done after disabling the FW
		 * threads to avoid a race condition).
		 * This is only really needed for PDumps but we do it anyway driver-live.
		 */
		RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVIRQSTATUS, 0x0);

		/* Wait for the Slave Port to finish all the transactions */
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_META_SP_MSLVCTRL1,
		                      RGX_CR_META_SP_MSLVCTRL1_READY_EN | RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
		                      RGX_CR_META_SP_MSLVCTRL1_READY_EN | RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
		if (eError != PVRSRV_OK) return eError;
	}
#endif


	/* Extra Idle checks */
	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_BIF_STATUS_MMU,
	                      0,
	                      RGX_CR_BIF_STATUS_MMU_MASKFULL);
	if (eError != PVRSRV_OK) return eError;

	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_BIFPM_STATUS_MMU,
	                      0,
	                      RGX_CR_BIFPM_STATUS_MMU_MASKFULL);
	if (eError != PVRSRV_OK) return eError;

	if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK) &&
	    !RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_XT_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_BIF_READS_EXT_STATUS,
		                      0,
		                      RGX_CR_BIF_READS_EXT_STATUS_MASKFULL);
		if (eError != PVRSRV_OK) return eError;
	}


	eError = RGXPollReg32(hPrivate,
	                      RGX_CR_BIFPM_READS_EXT_STATUS,
	                      0,
	                      RGX_CR_BIFPM_READS_EXT_STATUS_MASKFULL);
	if (eError != PVRSRV_OK) return eError;

	{
		IMG_UINT64 ui64SLCMask = RGX_CR_SLC_STATUS1_MASKFULL;
		eError = RGXPollReg64(hPrivate,
		                      RGX_CR_SLC_STATUS1,
		                      0,
		                      ui64SLCMask);
		if (eError != PVRSRV_OK) return eError;
	}

	if (4 == RGXGetDeviceSLCBanks(hPrivate))
	{
		eError = RGXPollReg64(hPrivate,
		                      RGX_CR_SLC_STATUS2,
		                      0,
		                      RGX_CR_SLC_STATUS2_MASKFULL);
		if (eError != PVRSRV_OK) return eError;
	}

#if !defined(SUPPORT_SHARED_SLC)
	/* Wait for SLC to signal IDLE */
	if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SLC_IDLE,
		                      RGX_CR_SLC_IDLE_MASKFULL,
		                      RGX_CR_SLC_IDLE_MASKFULL);
	}
	else
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SLC3_IDLE,
		                      RGX_CR_SLC3_IDLE_MASKFULL,
		                      RGX_CR_SLC3_IDLE_MASKFULL);
	}
#endif /* SUPPORT_SHARED_SLC */
	if (eError != PVRSRV_OK) return eError;


	/* Wait for Sidekick/Jones to signal IDLE except for the Garten Wrapper */
	if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SIDEKICK_IDLE,
		                      RGX_CR_SIDEKICK_IDLE_MASKFULL^(RGX_CR_SIDEKICK_IDLE_GARTEN_EN|RGX_CR_SIDEKICK_IDLE_SOCIF_EN|RGX_CR_SIDEKICK_IDLE_HOSTIF_EN),
		                      RGX_CR_SIDEKICK_IDLE_MASKFULL^(RGX_CR_SIDEKICK_IDLE_GARTEN_EN|RGX_CR_SIDEKICK_IDLE_SOCIF_EN|RGX_CR_SIDEKICK_IDLE_HOSTIF_EN));
	}
	else
	{
		if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			eError = RGXPollReg32(hPrivate,
			                      RGX_CR_JONES_IDLE,
			                      RGX_CR_JONES_IDLE_MASKFULL^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN|RGX_CR_JONES_IDLE_HOSTIF_EN),
			                      RGX_CR_JONES_IDLE_MASKFULL^(RGX_CR_JONES_IDLE_GARTEN_EN|RGX_CR_JONES_IDLE_SOCIF_EN|RGX_CR_JONES_IDLE_HOSTIF_EN));
		}
	}

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
			if (!RGXDeviceHasFeaturePower(hPrivate, RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK))
			{
				eError = RGXPollReg32(hPrivate,
				                      RGX_CR_SIDEKICK_IDLE,
				                      RGX_CR_SIDEKICK_IDLE_GARTEN_EN,
				                      RGX_CR_SIDEKICK_IDLE_GARTEN_EN);
				if (eError != PVRSRV_OK) return eError;
			}
			else
			{
				eError = RGXPollReg32(hPrivate,
				                      RGX_CR_JONES_IDLE,
				                      RGX_CR_JONES_IDLE_GARTEN_EN,
				                      RGX_CR_JONES_IDLE_GARTEN_EN);
				if (eError != PVRSRV_OK) return eError;
			}
		}
	}
	else
	{
		eError = RGXPollReg32(hPrivate,
		                      RGX_CR_SIDEKICK_IDLE,
		                      RGX_CR_SIDEKICK_IDLE_GARTEN_EN,
		                      RGX_CR_SIDEKICK_IDLE_GARTEN_EN);
		if (eError != PVRSRV_OK) return eError;
	}

	return eError;
}


/*
 * RGXInitSLC
 */
#if defined(SUPPORT_SHARED_SLC)
PVRSRV_ERROR RGXInitSLC(IMG_HANDLE hDevHandle)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvPowerParams;

	if (psDeviceNode == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psDevInfo = psDeviceNode->pvDevice;
	pvPowerParams = &psDevInfo->sPowerParams;

#if !defined(FIX_HW_BRN_36492)
	/* reset the SLC */
	RGXCommentLogPower(pvPowerParams, "RGXInitSLC: soft reset SLC");
	RGXWriteReg64(pvPowerParams, RGX_CR_SOFT_RESET, RGX_CR_SOFT_RESET_SLC_EN);

	/* Read soft-reset to fence previous write in order to clear the SOCIF pipeline */
	(void) RGXReadReg64(pvPowerParams, RGX_CR_SOFT_RESET);

	/* Take everything out of reset */
	RGXWriteReg64(pvPowerParams, RGX_CR_SOFT_RESET, 0x0);
#endif

	__RGXInitSLC(pvPowerParams);

	return PVRSRV_OK;
}
#endif

