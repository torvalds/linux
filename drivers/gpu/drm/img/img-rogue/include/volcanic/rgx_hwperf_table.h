/*************************************************************************/ /*!
@File
@Title          HWPerf counter table header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Utility functions used internally for HWPerf data retrieval
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

#ifndef RGX_HWPERF_TABLE_H
#define RGX_HWPERF_TABLE_H

#include "img_types.h"
#include "img_defs.h"
#include "rgx_fwif_hwperf.h"

/*****************************************************************************/

/* Forward declaration */
typedef struct RGXFW_HWPERF_CNTBLK_TYPE_MODEL_ RGXFW_HWPERF_CNTBLK_TYPE_MODEL;

/* Function pointer type for functions to check dynamic power state of
 * counter block instance. Used only in firmware. */
typedef IMG_BOOL (*PFN_RGXFW_HWPERF_CNTBLK_POWERED)(
		RGX_HWPERF_CNTBLK_ID eBlkType,
		IMG_UINT8 ui8UnitId);

/* Counter block run-time info */
typedef struct
{
	IMG_UINT32 uiNumUnits;             /* Number of instances of this block type in the core */
} RGX_HWPERF_CNTBLK_RT_INFO;

/* Function pointer type for functions to check block is valid and present
 * on that RGX Device at runtime. It may have compile logic or run-time
 * logic depending on where the code executes: server, srvinit or firmware.
 * Values in the psRtInfo output parameter are only valid if true returned.
 */
typedef IMG_BOOL (*PFN_RGXFW_HWPERF_CNTBLK_PRESENT)(
		const struct RGXFW_HWPERF_CNTBLK_TYPE_MODEL_* psBlkTypeDesc,
		const void *pvDev_km,
		RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo);

/* This structure encodes properties of a type of performance counter block.
 * The structure is sometimes referred to as a block type descriptor. These
 * properties contained in this structure represent the columns in the block
 * type model table variable below. These values vary depending on the build
 * BVNC and core type.
 * Each direct block has a unique type descriptor and each indirect group has
 * a type descriptor.
 */
struct RGXFW_HWPERF_CNTBLK_TYPE_MODEL_
{
	IMG_UINT32 uiCntBlkIdBase;         /* The starting block id for this block type */
	IMG_UINT32 uiIndirectReg;          /* 0 if direct type otherwise the indirect register value to select indirect unit */
	IMG_UINT32 uiNumUnits;             /* Number of instances of this block type in the core (compile time use) */
	const IMG_CHAR *pszBlockNameComment;             /* Name of the PERF register. Used while dumping the perf counters to pdumps */
	PFN_RGXFW_HWPERF_CNTBLK_POWERED pfnIsBlkPowered; /* A function to determine dynamic power state for the block type */
	PFN_RGXFW_HWPERF_CNTBLK_PRESENT pfnIsBlkPresent; /* A function to determine presence on RGX Device at run-time */
	IMG_UINT16 *pszBlkCfgValid;        /* Array of supported counters per block type */
};

/*****************************************************************************/

/* Shared compile-time context ASSERT macro */
#if defined(RGX_FIRMWARE)
/*  firmware context */
#	define DBG_ASSERT(_c) RGXFW_ASSERT((_c))
#else
/*  host client/server context */
#	define DBG_ASSERT(_c) PVR_ASSERT((_c))
#endif

/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL struct PFNs pfnIsBlkPowered()

 Referenced in gasCntBlkTypeModel[] table below and only called from
 RGX_FIRMWARE run-time context. Therefore compile time configuration is used.
 *****************************************************************************/

#if defined(RGX_FIRMWARE) && defined(RGX_FEATURE_PERFBUS)
#	include "rgxfw_pow.h"
#	include "rgxfw_utils.h"

static inline IMG_BOOL rgxfw_hwperf_pow_st_direct(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId);
static inline IMG_BOOL rgxfw_hwperf_pow_st_direct(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	PVR_UNREFERENCED_PARAMETER(ui8UnitId);

	switch (eBlkType)
	{
		case RGX_CNTBLK_ID_JONES:
		case RGX_CNTBLK_ID_SLC:
		case RGX_CNTBLK_ID_SLCBANK0:
		case RGX_CNTBLK_ID_FBCDC:
		case RGX_CNTBLK_ID_FW_CUSTOM:
			return IMG_TRUE;

#if !defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE)
		case RGX_CNTBLK_ID_SLCBANK1:
			if (RGX_FEATURE_NUM_MEMBUS > 1U)
			{
				return IMG_TRUE;
			}
			else
			{
				return IMG_FALSE;
			}

		case RGX_CNTBLK_ID_SLCBANK2:
		case RGX_CNTBLK_ID_SLCBANK3:
			if (RGX_FEATURE_NUM_MEMBUS > 2U)
			{
				return IMG_TRUE;
			}
			else
			{
				return IMG_FALSE;
			}
#endif /* !defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE) */

		default:
			return IMG_FALSE;
	}
}

/* Only use conditional compilation when counter blocks appear in different
 * islands for different Rogue families.
 */
static inline IMG_BOOL rgxfw_hwperf_pow_st_indirect(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId);
static inline IMG_BOOL rgxfw_hwperf_pow_st_indirect(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	PVR_UNREFERENCED_PARAMETER(ui8UnitId);

	IMG_UINT32 ui32NumDustsEnabled = rgxfw_pow_get_enabled_units();

	// We don't have any Dusts Enabled until first DC opens the GPU. This makes
	// setting the PDump HWPerf trace buffers very difficult.
	// To work around this we special-case some of the 'have to be there'
	// indirect registers (e.g., TPU0)

	switch (eBlkType)
	{
		case RGX_CNTBLK_ID_TPU0:
			return IMG_TRUE;
			/*NOTREACHED*/
			break;
		default:
			if (((gsPowCtl.eUnitsPowState & RGXFW_POW_ST_RD_ON) != 0U) &&
				(ui32NumDustsEnabled > 0U))
			{
				return IMG_TRUE;
			}
			else
			{
				return IMG_FALSE;
			}
			/*NOTREACHED*/
			break;
	}
	return IMG_TRUE;
}

#else /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_PERFBUS) */

# define rgxfw_hwperf_pow_st_direct   ((void *)NULL)
# define rgxfw_hwperf_pow_st_indirect ((void *)NULL)

#endif /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_PERFBUS) */

/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL struct PFNs pfnIsBlkPowered() end
 *****************************************************************************/

/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL struct PFNs pfnIsBlkPresent() start

 Referenced in gasCntBlkTypeModel[] table below and called from all build
 contexts:
 RGX_FIRMWARE, PVRSRVCTL (UM) and PVRSRVKM (Server).

 Therefore each function has two implementations, one for compile time and one
 run time configuration depending on the context. The functions will inform the
 caller whether this block is valid for this particular RGX device. Other
 run-time dependent data is returned in psRtInfo for the caller to use.
 *****************************************************************************/


/* Used for all block types: Direct and Indirect */
static inline IMG_BOOL rgx_hwperf_blk_present(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
#if defined(__KERNEL__)	/* Server context -- Run-time Only */
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
	PVRSRV_DEVICE_NODE *psNode;
	IMG_UINT32	ui32MaxTPUPerSPU;
	IMG_UINT32	ui32NumMemBus;
	IMG_UINT32	ui32RTArchVal;

	DBG_ASSERT(psDevInfo != NULL);
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);

	if (((psDevInfo == NULL) || (psBlkTypeDesc == NULL)) || (psRtInfo == NULL))
	{
		return IMG_FALSE;
	}

	psNode = psDevInfo->psDeviceNode;
	DBG_ASSERT(psNode != NULL);

	if (psNode == NULL)
	{
		return IMG_FALSE;
	}

	ui32MaxTPUPerSPU =
		PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, MAX_TPU_PER_SPU);

	if (PVRSRV_IS_FEATURE_SUPPORTED(psNode, CATURIX_TOP_INFRASTRUCTURE))
	{
		ui32NumMemBus = 1U;
	}
	else
	{
		ui32NumMemBus =
			PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_MEMBUS);
	}

	ui32RTArchVal =
		PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, RAY_TRACING_ARCH);

	switch (psBlkTypeDesc->uiCntBlkIdBase)
	{
		case RGX_CNTBLK_ID_JONES:
		case RGX_CNTBLK_ID_SLC:
		case RGX_CNTBLK_ID_SLCBANK0:
		case RGX_CNTBLK_ID_FBCDC:
		case RGX_CNTBLK_ID_FW_CUSTOM:
			psRtInfo->uiNumUnits = 1;
			break;

#if !defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE)
		case RGX_CNTBLK_ID_SLCBANK1:
			if (ui32NumMemBus >= 2U)
			{
				psRtInfo->uiNumUnits = 1;
			}
			else
			{
				psRtInfo->uiNumUnits = 0;
			}
			break;

		case RGX_CNTBLK_ID_SLCBANK2:
		case RGX_CNTBLK_ID_SLCBANK3:
			if (ui32NumMemBus > 2U)
			{
				psRtInfo->uiNumUnits = 1;
			}
			else
			{
				psRtInfo->uiNumUnits = 0;
			}
			break;
#endif /* !defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE) */

		case RGX_CNTBLK_ID_TPU0:
		case RGX_CNTBLK_ID_SWIFT0:
			psRtInfo->uiNumUnits =
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_SPU);
			psRtInfo->uiNumUnits *= ui32MaxTPUPerSPU;
			break;

		case RGX_CNTBLK_ID_TEXAS0:
		case RGX_CNTBLK_ID_PBE_SHARED0:

			psRtInfo->uiNumUnits =
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_SPU);
			break;

		case RGX_CNTBLK_ID_RAC0:
			if (ui32RTArchVal > 2U)
			{
				psRtInfo->uiNumUnits =
					PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, SPU0_RAC_PRESENT) +
					PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, SPU1_RAC_PRESENT) +
					PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, SPU2_RAC_PRESENT) +
					PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, SPU3_RAC_PRESENT);
			}
			else
			{
				psRtInfo->uiNumUnits = 0;
			}
			break;

		case RGX_CNTBLK_ID_USC0:
		case RGX_CNTBLK_ID_MERCER0:
			psRtInfo->uiNumUnits =
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_CLUSTERS);
			break;

		case RGX_CNTBLK_ID_PBE0:

			psRtInfo->uiNumUnits =
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, PBE_PER_SPU);
			psRtInfo->uiNumUnits *=
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_SPU);
			break;

		case RGX_CNTBLK_ID_ISP0:

			psRtInfo->uiNumUnits =
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_ISP_PER_SPU);
			/* Adjust by NUM_SPU */

			psRtInfo->uiNumUnits *=
				PVRSRV_GET_DEVICE_FEATURE_VALUE(psNode, NUM_SPU);
			break;

		default:
			return IMG_FALSE;
	}
	/* Verify that we have at least one unit present */
	if (psRtInfo->uiNumUnits > 0U)
	{
		return IMG_TRUE;
	}
	else
	{
		return IMG_FALSE;
	}
#else	/* FW context -- Compile-time only */
	IMG_UINT32	ui32NumMemBus;
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	DBG_ASSERT(psBlkTypeDesc != NULL);

	if (unlikely(psBlkTypeDesc == NULL))
	{
		return IMG_FALSE;
	}

#if !defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE)
	ui32NumMemBus = RGX_FEATURE_NUM_MEMBUS;
#else
	ui32NumMemBus = 1U;
#endif /* !defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE) */

	switch (psBlkTypeDesc->uiCntBlkIdBase)
	{
		/* Handle the dynamic-sized SLC blocks which are only present if
		 * RGX_FEATURE_NUM_MEMBUS is appropriately set.
		 */
		case RGX_CNTBLK_ID_SLCBANK1:
			if (ui32NumMemBus >= 2U)
			{
				psRtInfo->uiNumUnits = 1;
			}
			else
			{
				psRtInfo->uiNumUnits = 0;
			}
			break;

		case RGX_CNTBLK_ID_SLCBANK2:
		case RGX_CNTBLK_ID_SLCBANK3:
			if (ui32NumMemBus > 2U)
			{
				psRtInfo->uiNumUnits = 1;
			}
			else
			{
				psRtInfo->uiNumUnits = 0;
			}
			break;

		default:
			psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
			break;
	}
	if (psRtInfo->uiNumUnits > 0U)
	{
		return IMG_TRUE;
	}
	else
	{
		return IMG_FALSE;
	}
#endif	/* defined(__KERNEL__) */
}

#if !defined(__KERNEL__) /* Firmware or User-mode context */

/* Used to instantiate a null row in the block type model table below where the
 * block is not supported for a given build BVNC in firmware/user mode context.
 * This is needed as the blockid to block type lookup uses the table as well
 * and clients may try to access blocks not in the hardware. */
#define RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(_blkid) X(_blkid, 0, 0, #_blkid, NULL, NULL, NULL)

#endif

/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL struct PFNs pfnIsBlkPresent() end
 *****************************************************************************/

/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL gasCntBlkTypeModel[] table

 This table holds the entries for the performance counter block type model.
 Where the block is not present on an RGX device in question the
 pfnIsBlkPresent() returns false, if valid and present it returns true.
 Columns in the table with a ** indicate the value is a default and the value
 returned in RGX_HWPERF_CNTBLK_RT_INFO when calling pfnIsBlkPresent()should
 be used at runtime by the caller. These columns are only valid for compile
 time BVNC configured contexts.

 Order of table rows must match order of counter block IDs in the enumeration
 RGX_HWPERF_CNTBLK_ID.

 Table contains Xmacro styled entries. Each includer of this file must define
 a gasCntBlkTypeModel[] structure which is local to itself. Only the layout is
 defined here.

 uiCntBlkIdBase      : Block-ID
 uiIndirectReg       : 0 => Direct, non-zero => INDIRECT register address
 uiNumUnits          : Number of units present on the GPU
 pszBlockNameComment : Name of the Performance Block
 pfnIsBlkPowered     : Function to determine power state of block
 pfnIsBlkPresent     : Function to determine block presence on the core
 pszBlkCfgValid      : Array of counters valid within this block type
 *****************************************************************************/

	// Furian 8XT V2 layout:

	/*   uiCntBlkIdBase,  uiIndirectReg,   uiNumUnits**,   pszBlockNameComment, pfnIsBlkPowered,    pfnIsBlkPresent */

	/* RGX_CNTBLK_ID_JONES */
#if defined(RGX_FIRMWARE) || defined(__KERNEL__)

/* Furian 8XT Direct Performance counter blocks */

#define	RGX_CNT_BLK_TYPE_MODEL_DIRECT_LIST \
	/* uiCntBlkIdBase,  uiIndirectReg,  uiNumUnits**, pszBlockNameComment, pfnIsBlkPowered, pfnIsBlkPresent */ \
X(RGX_CNTBLK_ID_JONES, 0, 1, "PERF_BLK_JONES", rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiJONES), \
X(RGX_CNTBLK_ID_SLC,   0, 1, "PERF_BLK_SLC",   rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiSLC), \
X(RGX_CNTBLK_ID_FBCDC, 0, 1, "PERF_BLK_FBCDC", rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiFBCDC), \
X(RGX_CNTBLK_ID_FW_CUSTOM, 0, 1, "PERF_BLK_FW_CUSTOM", rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiFWCUSTOM), \
X(RGX_CNTBLK_ID_SLCBANK0,   0, 1, "PERF_BLK_SLC0",   rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiSLC0), \
X(RGX_CNTBLK_ID_SLCBANK1,   0, 1, "PERF_BLK_SLC1",   rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiSLC1), \
X(RGX_CNTBLK_ID_SLCBANK2,   0, 1, "PERF_BLK_SLC2",   rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiSLC2), \
X(RGX_CNTBLK_ID_SLCBANK3,   0, 1, "PERF_BLK_SLC3",   rgxfw_hwperf_pow_st_direct, rgx_hwperf_blk_present, g_auiSLC3)

/* Furian 8XT Indirect Performance counter blocks */

#if !defined(RGX_CR_RAC_INDIRECT)
#define RGX_CR_RAC_INDIRECT (0x8398U)
#endif

#define RGX_CNT_BLK_TYPE_MODEL_INDIRECT_LIST \
	/* uiCntBlkIdBase,  uiIndirectReg,  uiNumUnits**, pszBlockNameComment, pfnIsBlkPowered, pfnIsBlkPresent */ \
X(RGX_CNTBLK_ID_ISP0, RGX_CR_ISP_INDIRECT, RGX_HWPERF_NUM_SPU * RGX_HWPERF_NUM_ISP_PER_SPU, "PERF_BLK_ISP", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiISP), \
X(RGX_CNTBLK_ID_MERCER0, RGX_CR_MERCER_INDIRECT, RGX_HWPERF_NUM_MERCER, "PERF_BLK_MERCER", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiMERCER), \
X(RGX_CNTBLK_ID_PBE0, RGX_CR_PBE_INDIRECT, RGX_HWPERF_NUM_PBE, "PERF_BLK_PBE", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiPBE), \
X(RGX_CNTBLK_ID_PBE_SHARED0, RGX_CR_PBE_SHARED_INDIRECT, RGX_HWPERF_NUM_PBE_SHARED, "PERF_BLK_PBE_SHARED", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiPBE_SHARED), \
X(RGX_CNTBLK_ID_USC0, RGX_CR_USC_INDIRECT, RGX_HWPERF_NUM_USC, "PERF_BLK_USC", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiUSC), \
X(RGX_CNTBLK_ID_TPU0, RGX_CR_TPU_INDIRECT, RGX_HWPERF_NUM_TPU, "PERF_BLK_TPU", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiTPU), \
X(RGX_CNTBLK_ID_SWIFT0, RGX_CR_SWIFT_INDIRECT, RGX_HWPERF_NUM_SWIFT, "PERF_BLK_SWIFT", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiSWIFT), \
X(RGX_CNTBLK_ID_TEXAS0, RGX_CR_TEXAS_INDIRECT, RGX_HWPERF_NUM_TEXAS, "PERF_BLK_TEXAS", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiTEXAS), \
X(RGX_CNTBLK_ID_RAC0, RGX_CR_RAC_INDIRECT, RGX_HWPERF_NUM_RAC, "PERF_BLK_RAC", rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present, g_auiRAC)

#else /* !defined(RGX_FIRMWARE) && !defined(__KERNEL__) */

#error "RGX_FIRMWARE or __KERNEL__ *MUST* be defined"

#endif	/* defined(RGX_FIRMWARE) || defined(__KERNEL__) */

#endif /* RGX_HWPERF_TABLE_H */

/******************************************************************************
 End of file (rgx_hwperf_table.h)
******************************************************************************/
