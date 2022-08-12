/*************************************************************************/ /*!
@File
@Title          RGX HW Performance counter table
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX HW Performance counters table
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

#include "img_defs.h"
#include "rgx_fwif_hwperf.h"
#if defined(__KERNEL__)
#include "rgxdefs_km.h"
#else
#include "rgxdefs.h"
#endif
#include "rgx_hwperf_table.h"

/* Includes needed for PVRSRVKM (Server) context */
#	include "rgx_bvnc_defs_km.h"
#	if defined(__KERNEL__)
#		include "rgxdevice.h"
#	endif

/* Shared compile-time context ASSERT macro */
#if defined(RGX_FIRMWARE)
#	include "rgxfw_utils.h"
/* firmware context */
#	define DBG_ASSERT(_c) RGXFW_ASSERT((_c))
#else
#	include "pvr_debug.h"
/* host client/server context */
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

static bool rgxfw_hwperf_pow_st_direct(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	PVR_UNREFERENCED_PARAMETER(eBlkType);
	PVR_UNREFERENCED_PARAMETER(ui8UnitId);

#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	/* S7XT: JONES */
	return (eBlkType == RGX_CNTBLK_ID_JONES);
#elif defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
	/* S6XT: TA, TORNADO */
	return true;
#else
	/* S6  : TA, HUB, RASTER (RASCAL) */
	return (gsPowCtl.eUnitsPowState & RGXFW_POW_ST_RD_ON) != 0U;
#endif
}

/* Only use conditional compilation when counter blocks appear in different
 * islands for different Rogue families.
 */
static bool rgxfw_hwperf_pow_st_indirect(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	IMG_UINT32 ui32NumDustsEnabled = rgxfw_pow_get_enabled_units();

	if (((gsPowCtl.eUnitsPowState & RGXFW_POW_ST_RD_ON) != 0U) &&
			(ui32NumDustsEnabled > 0U))
	{
#if defined(RGX_FEATURE_DYNAMIC_DUST_POWER)
		IMG_UINT32 ui32NumUscEnabled = ui32NumDustsEnabled*2U;

		switch (eBlkType)
		{
		case RGX_CNTBLK_ID_TPU_MCU0:                   /* S6 and S6XT */
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
		case RGX_CNTBLK_ID_TEXAS0:                     /* S7 */
#endif
			if (ui8UnitId >= ui32NumDustsEnabled)
			{
				return false;
			}
			break;
		case RGX_CNTBLK_ID_USC0:                       /* S6, S6XT, S7 */
		case RGX_CNTBLK_ID_PBE0:                       /* S7, PBE2_IN_XE */
			/* Handle single cluster cores */
			if (ui8UnitId >= ((ui32NumUscEnabled > RGX_FEATURE_NUM_CLUSTERS) ? RGX_FEATURE_NUM_CLUSTERS : ui32NumUscEnabled))
			{
				return false;
			}
			break;
		case RGX_CNTBLK_ID_BLACKPEARL0:                /* S7 */
		case RGX_CNTBLK_ID_RASTER0:                    /* S6XT */
#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
		case RGX_CNTBLK_ID_TEXAS0:                     /* S6XT */
#endif
			if (ui8UnitId >= (RGX_REQ_NUM_PHANTOMS(ui32NumUscEnabled)))
			{
				return false;
			}
			break;
		default:
			RGXFW_ASSERT(false); /* should never get here, table error */
			break;
		}
#else
		/* Always true, no fused DUSTs, all powered so do not check unit */
		PVR_UNREFERENCED_PARAMETER(eBlkType);
		PVR_UNREFERENCED_PARAMETER(ui8UnitId);
#endif
	}
	else
	{
		return false;
	}
	return true;
}

#else /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_PERFBUS) */

# define rgxfw_hwperf_pow_st_direct   ((void*)NULL)
# define rgxfw_hwperf_pow_st_indirect ((void*)NULL)

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

/* Used for block types: USC */
static IMG_BOOL rgx_hwperf_blk_present_perfbus(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_USC0);

#if defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	PVR_ASSERT(pvRtInfo != NULL);
	{
		RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo = (RGX_HWPERF_CNTBLK_RT_INFO *) pvRtInfo;
		const PVRSRV_RGXDEV_INFO *psDevInfo = (const PVRSRV_RGXDEV_INFO *)pvDev_km;

		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, PERFBUS))
		{
			psRtInfo->ui32NumUnits = RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, NUM_CLUSTERS) ? RGX_GET_FEATURE_VALUE(psDevInfo, NUM_CLUSTERS) : 0;
			psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
			return IMG_TRUE;
		}
	}
#else /* FW context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
# if defined(RGX_FEATURE_PERFBUS)
	return IMG_TRUE;
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: Direct RASTERISATION, HUB */
static IMG_BOOL rgx_hwperf_blk_present_not_clustergrouping(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT((psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_RASTER) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_HUB));

#if defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	PVR_ASSERT(pvRtInfo != NULL);
	{
		RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo = (RGX_HWPERF_CNTBLK_RT_INFO *) pvRtInfo;
		const PVRSRV_RGXDEV_INFO *psDevInfo = (const PVRSRV_RGXDEV_INFO *)pvDev_km;
		if ((!RGX_IS_FEATURE_SUPPORTED(psDevInfo, CLUSTER_GROUPING)) &&
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, PERFBUS)))
		{
			psRtInfo->ui32NumUnits = 1;
			psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
			return IMG_TRUE;
		}
	}
#else /* FW context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
# if !defined(RGX_FEATURE_CLUSTER_GROUPING) && defined(RGX_FEATURE_PERFBUS)
	return IMG_TRUE;
# endif
#endif
	return IMG_FALSE;
}

#if defined(__KERNEL__) /* Server context */
static IMG_UINT32 rgx_units_indirect_by_phantom(const PVRSRV_DEVICE_FEATURE_CONFIG *psFeatCfg)
{
	/* Run-time math for RGX_HWPERF_INDIRECT_BY_PHANTOM */
	return ((psFeatCfg->ui64Features & RGX_FEATURE_CLUSTER_GROUPING_BIT_MASK) == 0) ? 1
			: (psFeatCfg->ui32FeaturesValues[RGX_FEATURE_NUM_CLUSTERS_IDX]+3)/4;
}

static IMG_UINT32 rgx_units_phantom_indirect_by_dust(const PVRSRV_DEVICE_FEATURE_CONFIG *psFeatCfg)
{
	/* Run-time math for RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST */
	return MAX((psFeatCfg->ui32FeaturesValues[RGX_FEATURE_NUM_CLUSTERS_IDX]>>1),1);
}

static IMG_UINT32 rgx_units_phantom_indirect_by_cluster(const PVRSRV_DEVICE_FEATURE_CONFIG *psFeatCfg)
{
	/* Run-time math for RGX_HWPERF_PHANTOM_INDIRECT_BY_CLUSTER */
	return psFeatCfg->ui32FeaturesValues[RGX_FEATURE_NUM_CLUSTERS_IDX];
}
#endif /* defined(__KERNEL__) */

/* Used for block types: TORNADO, TEXAS, Indirect RASTERISATION */
static IMG_BOOL rgx_hwperf_blk_present_xttop(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT((psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TORNADO) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TEXAS0) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_RASTER0));

#if defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	PVR_ASSERT(pvRtInfo != NULL);
	{
		RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo = (RGX_HWPERF_CNTBLK_RT_INFO *) pvRtInfo;
		const PVRSRV_RGXDEV_INFO *psDevInfo = (const PVRSRV_RGXDEV_INFO *)pvDev_km;
		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, XT_TOP_INFRASTRUCTURE))
		{
			if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TORNADO)
			{
				psRtInfo->ui32NumUnits = 1;
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TEXAS0)
			{
				psRtInfo->ui32NumUnits = rgx_units_indirect_by_phantom(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = RGX_CR_TEXAS_PERF_INDIRECT;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_RASTER0)
			{
				psRtInfo->ui32NumUnits = rgx_units_indirect_by_phantom(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
		}
	}
#else /* FW context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
# if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE) && defined(RGX_FEATURE_PERFBUS)
	return IMG_TRUE;
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: JONES, TPU_MCU, TEXAS, BLACKPERL, PBE */
static IMG_BOOL rgx_hwperf_blk_present_s7top(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT((psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_JONES) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TEXAS0) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_BLACKPEARL0) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_PBE0));

#if defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	PVR_ASSERT(pvRtInfo != NULL);
	{
		RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo = (RGX_HWPERF_CNTBLK_RT_INFO *) pvRtInfo;
		const PVRSRV_RGXDEV_INFO *psDevInfo = (const PVRSRV_RGXDEV_INFO *)pvDev_km;
		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE))
		{
			if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0)
			{
				psRtInfo->ui32NumUnits = rgx_units_phantom_indirect_by_dust(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = RGX_CR_TPU_PERF_INDIRECT;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TEXAS0)
			{
				psRtInfo->ui32NumUnits = rgx_units_phantom_indirect_by_dust(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = RGX_CR_TEXAS3_PERF_INDIRECT;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_BLACKPEARL0)
			{
				psRtInfo->ui32NumUnits = rgx_units_indirect_by_phantom(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_PBE0)
			{
				psRtInfo->ui32NumUnits = rgx_units_phantom_indirect_by_cluster(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_JONES)
			{
				psRtInfo->ui32NumUnits = 1;
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
		}
	}
#else /* FW context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
# if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	return IMG_TRUE;
# else
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: TA, TPU_MCU. Also PBE when PBE2_IN_XE is present */
static IMG_BOOL rgx_hwperf_blk_present_not_s7top(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT((psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TA) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0) ||
			(psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_PBE0));

#if defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	PVR_ASSERT(pvRtInfo != NULL);
	{
		RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo = (RGX_HWPERF_CNTBLK_RT_INFO *) pvRtInfo;
		const PVRSRV_RGXDEV_INFO *psDevInfo = (const PVRSRV_RGXDEV_INFO *)pvDev_km;
		if (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, S7_TOP_INFRASTRUCTURE) &&
				RGX_IS_FEATURE_SUPPORTED(psDevInfo, PERFBUS))
		{
			if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TA)
			{
				psRtInfo->ui32NumUnits = 1;
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_PBE0)
			{
				if (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, PBE2_IN_XE))
				{
					/* PBE counters are not present on this config */
					return IMG_FALSE;
				}
				psRtInfo->ui32NumUnits = rgx_units_phantom_indirect_by_cluster(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = psBlkTypeDesc->ui32IndirectReg;
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0)
			{
				psRtInfo->ui32NumUnits = rgx_units_phantom_indirect_by_dust(&psDevInfo->sDevFeatureCfg);
				psRtInfo->ui32IndirectReg = RGX_CR_TPU_MCU_L0_PERF_INDIRECT;
				return IMG_TRUE;
			}
		}
	}
#else /* FW context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
# if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) && defined(RGX_FEATURE_PERFBUS)
#  if !defined(RGX_FEATURE_PBE2_IN_XE)
	if (psBlkTypeDesc->ui32CntBlkIdBase == RGX_CNTBLK_ID_PBE0)
	{
		/* No support for PBE counters without PBE2_IN_XE */
		return IMG_FALSE;
	}
#  endif
	return IMG_TRUE;
# endif
#endif
	return IMG_FALSE;
}

static IMG_BOOL rgx_hwperf_blk_present_check_s7top_or_not(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
#if defined(__KERNEL__)
	return (rgx_hwperf_blk_present_s7top(psBlkTypeDesc, pvDev_km, pvRtInfo)
	     || rgx_hwperf_blk_present_not_s7top(psBlkTypeDesc, pvDev_km, pvRtInfo));

#elif defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	return rgx_hwperf_blk_present_s7top(psBlkTypeDesc, pvDev_km, pvRtInfo);

#elif defined(RGX_FEATURE_PBE2_IN_XE) || defined(RGX_FEATURE_PERFBUS)
	return rgx_hwperf_blk_present_not_s7top(psBlkTypeDesc, pvDev_km, pvRtInfo);
#else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
	return IMG_FALSE;
#endif
}

static IMG_BOOL rgx_hwperf_blk_present_check_s7top_or_xttop(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
#if defined(__KERNEL__)
	return (rgx_hwperf_blk_present_s7top(psBlkTypeDesc, pvDev_km, pvRtInfo)
	     || rgx_hwperf_blk_present_xttop(psBlkTypeDesc, pvDev_km, pvRtInfo));

#elif defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	return rgx_hwperf_blk_present_s7top(psBlkTypeDesc, pvDev_km, pvRtInfo);

#elif defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
	return rgx_hwperf_blk_present_xttop(psBlkTypeDesc, pvDev_km, pvRtInfo);
#else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);
	return IMG_FALSE;
#endif
}

#if !defined(__KERNEL__) /* Firmware or User-mode context */
static IMG_BOOL rgx_hwperf_blk_present_false(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, const void *pvDev_km, void *pvRtInfo)
{
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(pvRtInfo);

	/* Some functions not used on some BVNCs, silence compiler warnings */
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_perfbus);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_not_clustergrouping);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_xttop);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_s7top);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_not_s7top);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_check_s7top_or_not);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_check_s7top_or_xttop);

	return IMG_FALSE;
}

/* Used to instantiate a null row in the block type model table below where the
 * block is not supported for a given build BVNC in firmware/user mode context.
 * This is needed as the blockid to block type lookup uses the table as well
 * and clients may try to access blocks not in the hardware. */
#define RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(_blkid) {_blkid, 0, 0, 0, 0, 0, 0, 0, 0, #_blkid, NULL, rgx_hwperf_blk_present_false}

#endif


/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL struct PFNs pfnIsBlkPresent() end
 *****************************************************************************/

#if defined(__KERNEL__) /* Values will be calculated at run-time */
#define RGX_HWPERF_NUM_BLOCK_UNITS RGX_HWPERF_NUM_BLOCK_UNITS_RUNTIME_CALC
#define RGX_INDIRECT_REG_TEXAS 0xFFFFFFFF
#define RGX_INDIRECT_REG_TPU 0xFFFFFFFF

#elif defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
#define RGX_HWPERF_NUM_BLOCK_UNITS RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST
#define RGX_INDIRECT_REG_TEXAS RGX_CR_TEXAS3_PERF_INDIRECT
#define RGX_INDIRECT_REG_TPU RGX_CR_TPU_PERF_INDIRECT

#else

#if defined(RGX_FEATURE_PERFBUS)
#define RGX_INDIRECT_REG_TPU RGX_CR_TPU_MCU_L0_PERF_INDIRECT
#endif

#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
#define RGX_HWPERF_NUM_BLOCK_UNITS RGX_HWPERF_INDIRECT_BY_PHANTOM
#define RGX_INDIRECT_REG_TEXAS RGX_CR_TEXAS_PERF_INDIRECT
#endif

#endif


/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL gasCntBlkTypeModel[] table

 This table holds the entries for the performance counter block type model.
 Where the block is not present on an RGX device in question the
 pfnIsBlkPresent() returns false, if valid and present it returns true.
 Columns in the table with a ** indicate the value is a default and the
 value returned in RGX_HWPERF_CNTBLK_RT_INFO when calling pfnIsBlkPresent()
 should be used at runtime by the caller. These columns are only valid for
 compile time BVNC configured contexts.

 Order of table rows must match order of counter block IDs in the enumeration
 RGX_HWPERF_CNTBLK_ID.
 *****************************************************************************/

static const RGXFW_HWPERF_CNTBLK_TYPE_MODEL gasCntBlkTypeModel[] =
{
		/*   ui32CntBlkIdBase,         ui32IndirectReg,                  ui32PerfReg,                  ui32Select0BaseReg,                    ui32Counter0BaseReg                   ui8NumCounters,  ui32NumUnits**,                  ui8SelectRegModeShift, ui8SelectRegOffsetShift,            pfnIsBlkPowered               pfnIsBlkPresent
		 *                                                                                                                                                                                                                                                   pszBlockNameComment,  */
		/*RGX_CNTBLK_ID_TA*/
#if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) && defined(RGX_FEATURE_PERFBUS) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_TA,       0, /* direct */                RGX_CR_TA_PERF,             RGX_CR_TA_PERF_SELECT0,              RGX_CR_TA_PERF_COUNTER_0,             4,              1,                              21,                  3,  "RGX_CR_TA_PERF",              rgxfw_hwperf_pow_st_direct,   rgx_hwperf_blk_present_not_s7top },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_TA),
#endif

		/*RGX_CNTBLK_ID_RASTER*/
#if !defined(RGX_FEATURE_CLUSTER_GROUPING) && defined(RGX_FEATURE_PERFBUS) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_RASTER,   0, /* direct */                RGX_CR_RASTERISATION_PERF,  RGX_CR_RASTERISATION_PERF_SELECT0,   RGX_CR_RASTERISATION_PERF_COUNTER_0,  4,              1,                              21,                  3,  "RGX_CR_RASTERISATION_PERF",   rgxfw_hwperf_pow_st_direct,   rgx_hwperf_blk_present_not_clustergrouping },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_RASTER),
#endif

		/*RGX_CNTBLK_ID_HUB*/
#if !defined(RGX_FEATURE_CLUSTER_GROUPING) && defined(RGX_FEATURE_PERFBUS) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_HUB,      0, /* direct */                RGX_CR_HUB_BIFPMCACHE_PERF, RGX_CR_HUB_BIFPMCACHE_PERF_SELECT0,  RGX_CR_HUB_BIFPMCACHE_PERF_COUNTER_0, 4,              1,                              21,                  3,  "RGX_CR_HUB_BIFPMCACHE_PERF",  rgxfw_hwperf_pow_st_direct,   rgx_hwperf_blk_present_not_clustergrouping },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_HUB),
#endif

		/*RGX_CNTBLK_ID_TORNADO*/
#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_TORNADO,  0, /* direct */                RGX_CR_TORNADO_PERF,        RGX_CR_TORNADO_PERF_SELECT0,         RGX_CR_TORNADO_PERF_COUNTER_0,        4,              1,                              21,                  4,  "RGX_CR_TORNADO_PERF",         rgxfw_hwperf_pow_st_direct,   rgx_hwperf_blk_present_xttop },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_TORNADO),
#endif

		/*RGX_CNTBLK_ID_JONES*/
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_JONES,   0, /* direct */                 RGX_CR_JONES_PERF,          RGX_CR_JONES_PERF_SELECT0,           RGX_CR_JONES_PERF_COUNTER_0,          4,              1,                              21,                  3,  "RGX_CR_JONES_PERF",           rgxfw_hwperf_pow_st_direct,    rgx_hwperf_blk_present_s7top },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_JONES),
#endif

		/*RGX_CNTBLK_ID_TPU_MCU0*/
#if defined(__KERNEL__) || (defined(RGX_FEATURE_PERFBUS) && !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)) || defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
		{RGX_CNTBLK_ID_TPU_MCU0, RGX_INDIRECT_REG_TPU,      RGX_CR_TPU_MCU_L0_PERF,   RGX_CR_TPU_MCU_L0_PERF_SELECT0,     RGX_CR_TPU_MCU_L0_PERF_COUNTER_0,     4,              RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST,    21,          3,  "RGX_CR_TPU_MCU_L0_PERF",      rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_check_s7top_or_not },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_TPU_MCU0),
#endif

		/*RGX_CNTBLK_ID_USC0*/
#if defined(RGX_FEATURE_PERFBUS) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_USC0,    RGX_CR_USC_PERF_INDIRECT,       RGX_CR_USC_PERF,            RGX_CR_USC_PERF_SELECT0,            RGX_CR_USC_PERF_COUNTER_0,            4,              RGX_HWPERF_PHANTOM_INDIRECT_BY_CLUSTER, 21,          3,  "RGX_CR_USC_PERF",             rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_perfbus },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_USC0),
#endif

		/*RGX_CNTBLK_ID_TEXAS0*/
#if defined(__KERNEL__) || defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
		{RGX_CNTBLK_ID_TEXAS0,  RGX_INDIRECT_REG_TEXAS,      RGX_CR_TEXAS_PERF,          RGX_CR_TEXAS_PERF_SELECT0,          RGX_CR_TEXAS_PERF_COUNTER_0,          6,              RGX_HWPERF_NUM_BLOCK_UNITS,             31,          3,  "RGX_CR_TEXAS_PERF",           rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_check_s7top_or_xttop },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_TEXAS0),
#endif

		/*RGX_CNTBLK_ID_RASTER0*/
#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_RASTER0, RGX_CR_RASTERISATION_PERF_INDIRECT, RGX_CR_RASTERISATION_PERF, RGX_CR_RASTERISATION_PERF_SELECT0, RGX_CR_RASTERISATION_PERF_COUNTER_0,  4,            RGX_HWPERF_INDIRECT_BY_PHANTOM,         21,          3,  "RGX_CR_RASTERISATION_PERF",   rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_xttop },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_RASTER0),
#endif

		/*RGX_CNTBLK_ID_BLACKPEARL0*/
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
		{RGX_CNTBLK_ID_BLACKPEARL0, RGX_CR_BLACKPEARL_PERF_INDIRECT, RGX_CR_BLACKPEARL_PERF, RGX_CR_BLACKPEARL_PERF_SELECT0,    RGX_CR_BLACKPEARL_PERF_COUNTER_0,     6,              RGX_HWPERF_INDIRECT_BY_PHANTOM,         21,          3,  "RGX_CR_BLACKPEARL_PERF",      rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_s7top },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_BLACKPEARL0),
#endif

		/*RGX_CNTBLK_ID_PBE0*/
#if defined(__KERNEL__) || defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(RGX_FEATURE_PBE2_IN_XE)
		{RGX_CNTBLK_ID_PBE0,    RGX_CR_PBE_PERF_INDIRECT,        RGX_CR_PBE_PERF,            RGX_CR_PBE_PERF_SELECT0,            RGX_CR_PBE_PERF_COUNTER_0,            4,              RGX_HWPERF_PHANTOM_INDIRECT_BY_CLUSTER, 21,          3,  "RGX_CR_PBE_PERF",             rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_check_s7top_or_not },
#else
		RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_PBE0),
#endif
};


IMG_INTERNAL IMG_UINT32
RGXGetHWPerfBlockConfig(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL **ppsModel)
{
	*ppsModel = gasCntBlkTypeModel;
	return ARRAY_SIZE(gasCntBlkTypeModel);
}

/******************************************************************************
 End of file (rgx_hwperf_table.c)
 ******************************************************************************/
