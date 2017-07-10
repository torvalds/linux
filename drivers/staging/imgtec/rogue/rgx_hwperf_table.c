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

#include "rgx_fwif_hwperf.h"
#include "rgxdefs_km.h"
#include "rgx_hwperf_table.h"

/* Includes needed for PVRSRVKM (Server) context */
#if defined(SUPPORT_KERNEL_SRVINIT)
#	include "rgx_bvnc_defs_km.h"
#	if defined(__KERNEL__)
#		include "rgxdevice.h"
#	endif
#endif

/* Shared compile-time context ASSERT macro */
#if defined(RGX_FIRMWARE)
#	include "rgxfw_utils.h"
/*  firmware context */
#	define DBG_ASSERT(_c) RGXFW_ASSERT((_c))
#else
#	include "pvr_debug.h"
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

static IMG_BOOL rgxfw_hwperf_pow_st_direct(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	PVR_UNREFERENCED_PARAMETER(eBlkType);
	PVR_UNREFERENCED_PARAMETER(ui8UnitId);

#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	/* S7XT: JONES */
	return (eBlkType == RGX_CNTBLK_ID_JONES) ? IMG_TRUE : IMG_FALSE;
#elif defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
	/* S6XT: TA, TORNADO */
	return IMG_TRUE;
#else
	/* S6  : TA, HUB, RASTER (RASCAL) */
	return (gsPowCtl.ePowState & RGXFW_POW_ST_RD_ON) ? IMG_TRUE : IMG_FALSE;
#endif
}

/* Only use conditional compilation when counter blocks appear in different
 * islands for different Rogue families.
 */
static IMG_BOOL rgxfw_hwperf_pow_st_indirect(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	IMG_UINT32 ui32NumDustsEnabled = rgxfw_pow_get_enabled_dusts_num();

	if ((gsPowCtl.ePowState & RGXFW_POW_ST_RD_ON) &&
			(ui32NumDustsEnabled > 0))
	{
#if defined(RGX_FEATURE_DYNAMIC_DUST_POWER)
		IMG_UINT32 ui32NumUscEnabled = ui32NumDustsEnabled*2;

		switch (eBlkType)
		{
		case RGX_CNTBLK_ID_TPU_MCU0:                   /* S6 and S6XT */
#if defined (RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
		case RGX_CNTBLK_ID_TEXAS0:                     /* S7 */
#endif
			if (ui8UnitId >= ui32NumDustsEnabled)
			{
				return IMG_FALSE;
			}
			break;
		case RGX_CNTBLK_ID_USC0:                       /* S6, S6XT, S7 */
		case RGX_CNTBLK_ID_PBE0:                       /* S7 */
			/* Handle single cluster cores */
			if (ui8UnitId >= ((ui32NumUscEnabled > RGX_FEATURE_NUM_CLUSTERS) ? RGX_FEATURE_NUM_CLUSTERS : ui32NumUscEnabled))
			{
				return IMG_FALSE;
			}
			break;
		case RGX_CNTBLK_ID_BLACKPEARL0:                /* S7 */
		case RGX_CNTBLK_ID_RASTER0:                    /* S6XT */
#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE)
		case RGX_CNTBLK_ID_TEXAS0:                     /* S6XT */
#endif
			if (ui8UnitId >= (RGX_REQ_NUM_PHANTOMS(ui32NumUscEnabled)))
			{
				return IMG_FALSE;
			}
			break;
		default:
			RGXFW_ASSERT(IMG_FALSE);  /* should never get here, table error */
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
		return IMG_FALSE;
	}
	return IMG_TRUE;
}

#else /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_PERFBUS) */

# define rgxfw_hwperf_pow_st_direct   ((void*)NULL)
# define rgxfw_hwperf_pow_st_indirect ((void*)NULL)
# define rgxfw_hwperf_pow_st_gandalf  ((void*)NULL)

#endif /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_PERFBUS) */

#if defined(RGX_FIRMWARE) && defined(RGX_FEATURE_RAY_TRACING)

/* Currently there is no power island control in the firmware for ray tracing
 * so we currently assume these blocks are always powered. */
static IMG_BOOL rgxfw_hwperf_pow_st_gandalf(RGX_HWPERF_CNTBLK_ID eBlkType, IMG_UINT8 ui8UnitId)
{
	PVR_UNREFERENCED_PARAMETER(eBlkType);
	PVR_UNREFERENCED_PARAMETER(ui8UnitId);

	return IMG_TRUE;
}

#else /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_RAY_TRACING) */

# define rgxfw_hwperf_pow_st_gandalf  ((void*)NULL)

#endif /* !defined(RGX_FIRMWARE) || !defined(RGX_FEATURE_RAY_TRACING) */

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
static IMG_BOOL rgx_hwperf_blk_present_perfbus(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);
	DBG_ASSERT(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_USC0);

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
		if ((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_PERFBUS_BIT_MASK) != 0)
		{
			psRtInfo->uiBitSelectPreserveMask = 0x0000;
			psRtInfo->uiNumUnits = psDevInfo->sDevFeatureCfg.ui32NumClusters;
			return IMG_TRUE;
		}
	}
    PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
#else /* FW or Client context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
# if defined(RGX_FEATURE_PERFBUS)
	psRtInfo->uiBitSelectPreserveMask = 0x0000;
	psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
	return IMG_TRUE;
# else
    PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
    PVR_UNREFERENCED_PARAMETER(psRtInfo);
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: Direct RASTERISATION, HUB */
static IMG_BOOL rgx_hwperf_blk_present_not_clustergrouping(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);
	DBG_ASSERT((psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_RASTER) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_HUB));

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
		if (((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_CLUSTER_GROUPING_BIT_MASK) == 0) &&
				((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_PERFBUS_BIT_MASK) != 0))
		{
			psRtInfo->uiNumUnits = 1;
			if (((psDevInfo->sDevFeatureCfg.ui64ErnsBrns & HW_ERN_44885_BIT_MASK) != 0) &&
				(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_RASTER))
			{
				psRtInfo->uiBitSelectPreserveMask = 0X7c00;
			}
			else
			{
				psRtInfo->uiBitSelectPreserveMask = 0x0000;
			}
			return IMG_TRUE;
		}
	}
#else /* FW or Client context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
# if !defined(RGX_FEATURE_CLUSTER_GROUPING) && defined(RGX_FEATURE_PERFBUS)
	psRtInfo->uiNumUnits = 1;
#  if defined(HW_ERN_44885)
	if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_RASTER)
	{
		psRtInfo->uiBitSelectPreserveMask = 0x7C00;
	}
	else
#  endif
	{
		psRtInfo->uiBitSelectPreserveMask = 0x0000;
	}
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	return IMG_TRUE;
# else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(psRtInfo);
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: BF, BT, RT, SH, BX_TU */
static IMG_BOOL rgx_hwperf_blk_present_raytracing(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);
	DBG_ASSERT((psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_BF) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_BT) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_RT) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_SH) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_BX_TU0));

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
		if ((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_RAY_TRACING_BIT_MASK) != 0)
		{
			psRtInfo->uiBitSelectPreserveMask = 0x0000;
			psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
			return IMG_TRUE;
		}
	}
#else /* FW or Client context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
# if defined(RGX_FEATURE_RAY_TRACING)
	psRtInfo->uiBitSelectPreserveMask = 0x0000;
	psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
	DBG_ASSERT(psBlkTypeDesc->uiPerfReg != 0); /* Check for broken config */
	return IMG_TRUE;
# else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(psRtInfo);
# endif
#endif
	return IMG_FALSE;
}

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
static INLINE IMG_UINT32 rgx_units_indirect_by_phantom(PVRSRV_DEVICE_FEATURE_CONFIG *psFeatCfg)
{
	/* Run-time math for RGX_HWPERF_INDIRECT_BY_PHANTOM */
	return ((psFeatCfg->ui64Features & RGX_FEATURE_CLUSTER_GROUPING_BIT_MASK) == 0) ? 1
		: (psFeatCfg->ui32NumClusters+3)/4;
}

static INLINE IMG_UINT32 rgx_units_phantom_indirect_by_dust(PVRSRV_DEVICE_FEATURE_CONFIG *psFeatCfg)
{
	/* Run-time math for RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST */
	return MAX((psFeatCfg->ui32NumClusters>>1),1);
}

static INLINE IMG_UINT32 rgx_units_phantom_indirect_by_cluster(PVRSRV_DEVICE_FEATURE_CONFIG *psFeatCfg)
{
	/* Run-time math for RGX_HWPERF_PHANTOM_INDIRECT_BY_CLUSTER */
	return psFeatCfg->ui32NumClusters;
}
#endif /* defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) */

/* Used for block types: TORNADO, TEXAS, Indirect RASTERISATION */
static IMG_BOOL rgx_hwperf_blk_present_xttop(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);
	DBG_ASSERT((psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TORNADO) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TEXAS0) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_RASTER0));

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
		if ((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_XT_TOP_INFRASTRUCTURE_BIT_MASK) != 0)
		{
			psRtInfo->uiBitSelectPreserveMask = 0x0000;
			psRtInfo->uiNumUnits =
				(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TORNADO) ? 1
					: rgx_units_indirect_by_phantom(&psDevInfo->sDevFeatureCfg); // Texas, Ind. Raster
			return IMG_TRUE;
		}
	}
#else /* FW or Client context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
# if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE) && defined(RGX_FEATURE_PERFBUS)
	psRtInfo->uiBitSelectPreserveMask = 0x0000;
	psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
	return IMG_TRUE;
# else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(psRtInfo);
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: JONES, TPU_MCU, TEXAS, BLACKPERL, PBE */
static IMG_BOOL rgx_hwperf_blk_present_s7top(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);
	DBG_ASSERT((psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_JONES) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TEXAS0) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_BLACKPEARL0) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_PBE0));

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
		if ((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK) != 0)
		{
			if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0)
			{
				psRtInfo->uiBitSelectPreserveMask =
						((psDevInfo->sDevFeatureCfg.ui64ErnsBrns & HW_ERN_41805_BIT_MASK) != 0)
						? 0x8000 : 0x0000;
				psRtInfo->uiNumUnits = rgx_units_phantom_indirect_by_dust(&psDevInfo->sDevFeatureCfg);
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TEXAS0)
			{
				psRtInfo->uiBitSelectPreserveMask = 0x0000;
				psRtInfo->uiNumUnits = rgx_units_phantom_indirect_by_dust(&psDevInfo->sDevFeatureCfg);
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_BLACKPEARL0)
			{
				psRtInfo->uiBitSelectPreserveMask = 0x0000;
				psRtInfo->uiNumUnits = rgx_units_indirect_by_phantom(&psDevInfo->sDevFeatureCfg);
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_PBE0)
			{
				psRtInfo->uiBitSelectPreserveMask = 0x0000;
				psRtInfo->uiNumUnits = rgx_units_phantom_indirect_by_cluster(&psDevInfo->sDevFeatureCfg);
				return IMG_TRUE;
			}
			else if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_JONES)
			{
				psRtInfo->uiBitSelectPreserveMask = 0x0000;
				psRtInfo->uiNumUnits = 1;
				return IMG_TRUE;
			}
		}
	}
#else /* FW or Client context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
# if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
#  if defined(HW_ERN_41805)
	if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0)
	{
		psRtInfo->uiBitSelectPreserveMask = 0x8000;
	}
	else
#  endif
	{
		psRtInfo->uiBitSelectPreserveMask = 0x0000;
	}
	return IMG_TRUE;
# else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(psRtInfo);
# endif
#endif
	return IMG_FALSE;
}

/* Used for block types: TA, TPU_MCU */
static IMG_BOOL rgx_hwperf_blk_present_not_s7top(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	DBG_ASSERT(psBlkTypeDesc != NULL);
	DBG_ASSERT(psRtInfo != NULL);
	DBG_ASSERT((psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TA) ||
		(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0));

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(__KERNEL__) /* Server context */
	PVR_ASSERT(pvDev_km != NULL);
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)pvDev_km;
		if (((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK) == 0) &&
				((psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_PERFBUS_BIT_MASK) != 0))
		{
			if (((psDevInfo->sDevFeatureCfg.ui64ErnsBrns & HW_ERN_41805_BIT_MASK) != 0) &&
				(psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0))
			{
				psRtInfo->uiBitSelectPreserveMask = 0X8000;
			}
			else
			{
				psRtInfo->uiBitSelectPreserveMask = 0x0000;
			}
			psRtInfo->uiNumUnits = (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TA) ? 1
				: rgx_units_phantom_indirect_by_dust(&psDevInfo->sDevFeatureCfg); // TPU_MCU0
			return IMG_TRUE;
		}
	}
#else /* FW or Client context */
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
# if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) && defined(RGX_FEATURE_PERFBUS)
	psRtInfo->uiNumUnits = psBlkTypeDesc->uiNumUnits;
#  if defined(HW_ERN_41805)
	if (psBlkTypeDesc->uiCntBlkIdBase == RGX_CNTBLK_ID_TPU_MCU0)
	{
		psRtInfo->uiBitSelectPreserveMask = 0x8000;
	}
	else
#  endif
	{
		psRtInfo->uiBitSelectPreserveMask = 0x0000;
	}
	return IMG_TRUE;
# else
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(psRtInfo);
# endif
#endif
	return IMG_FALSE;
}

#if !defined(__KERNEL__) /* Firmware or User-mode context */
static IMG_BOOL rgx_hwperf_blk_present_false(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL* psBlkTypeDesc, void *pvDev_km, RGX_HWPERF_CNTBLK_RT_INFO *psRtInfo)
{
	PVR_UNREFERENCED_PARAMETER(psBlkTypeDesc);
	PVR_UNREFERENCED_PARAMETER(pvDev_km);
	PVR_UNREFERENCED_PARAMETER(psRtInfo);

	/* Some functions not used on some BVNCs, silence compiler warnings */
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_perfbus);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_not_clustergrouping);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_raytracing);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_xttop);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_s7top);
	PVR_UNREFERENCED_PARAMETER(rgx_hwperf_blk_present_not_s7top);

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

/*****************************************************************************
 RGXFW_HWPERF_CNTBLK_TYPE_MODEL gasCntBlkTypeModel[] table

 This table holds the entries for the performance counter block type model.
 Where the block is not present on an RGX device in question the ()
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
/*   uiCntBlkIdBase,         iIndirectReg,                  uiPerfReg,                  uiSelect0BaseReg,                    uiCounter0BaseReg                   uiNumCounters,  uiNumUnits**,                  uiSelectRegModeShift, uiSelectRegOffsetShift,            pfnIsBlkPowered               pfnIsBlkPresent
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

	/*RGX_CNTBLK_ID_BF RGX_CNTBLK_ID_BT RGX_CNTBLK_ID_RT RGX_CNTBLK_ID_SH*/
	/* Conditional for rgxsrvinit.c UM build where CR defs not unconditional in any context and multi BVNC is not operational */
#if defined(RGX_FEATURE_RAY_TRACING ) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_BF,      0, /* direct */                 DPX_CR_BF_PERF,             DPX_CR_BF_PERF_SELECT0,              DPX_CR_BF_PERF_COUNTER_0,             4,              1,                              21,                  3,  "RGX_CR_BF_PERF",              rgxfw_hwperf_pow_st_gandalf, rgx_hwperf_blk_present_raytracing },
    {RGX_CNTBLK_ID_BT,      0, /* direct */                 DPX_CR_BT_PERF,             DPX_CR_BT_PERF_SELECT0,              DPX_CR_BT_PERF_COUNTER_0,             4,              1,                              21,                  3,  "RGX_CR_BT_PERF",              rgxfw_hwperf_pow_st_gandalf, rgx_hwperf_blk_present_raytracing },
    {RGX_CNTBLK_ID_RT,      0, /* direct */                 DPX_CR_RT_PERF,             DPX_CR_RT_PERF_SELECT0,              DPX_CR_RT_PERF_COUNTER_0,             4,              1,                              21,                  3,  "RGX_CR_RT_PERF",              rgxfw_hwperf_pow_st_gandalf, rgx_hwperf_blk_present_raytracing },
    {RGX_CNTBLK_ID_SH,      0, /* direct */                 RGX_CR_SH_PERF,             RGX_CR_SH_PERF_SELECT0,              RGX_CR_SH_PERF_COUNTER_0,             4,              1,                              21,                  3,  "RGX_CR_SH_PERF",              rgxfw_hwperf_pow_st_gandalf, rgx_hwperf_blk_present_raytracing },
#else
	RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_BF),
    RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_BT),
    RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_RT),
    RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_SH),
#endif

    /*RGX_CNTBLK_ID_TPU_MCU0*/
#if defined(RGX_FEATURE_PERFBUS) && !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) && defined(RGX_FEATURE_PERFBUS) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_TPU_MCU0, RGX_CR_TPU_MCU_L0_PERF_INDIRECT, RGX_CR_TPU_MCU_L0_PERF,   RGX_CR_TPU_MCU_L0_PERF_SELECT0,     RGX_CR_TPU_MCU_L0_PERF_COUNTER_0,     4,              RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST,    21,          3,  "RGX_CR_TPU_MCU_L0_PERF",      rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_not_s7top },
#else
	RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_TPU_MCU0),
#endif
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_TPU_MCU0, RGX_CR_TPU_PERF_INDIRECT,      RGX_CR_TPU_MCU_L0_PERF,     RGX_CR_TPU_MCU_L0_PERF_SELECT0,     RGX_CR_TPU_MCU_L0_PERF_COUNTER_0,     4,              RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST,    21,          3,  "RGX_CR_TPU_MCU_L0_PERF",      rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_s7top },
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
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_TEXAS0,  RGX_CR_TEXAS3_PERF_INDIRECT,    RGX_CR_TEXAS_PERF,          RGX_CR_TEXAS_PERF_SELECT0,          RGX_CR_TEXAS_PERF_COUNTER_0,          6,              RGX_HWPERF_PHANTOM_INDIRECT_BY_DUST,    31,          3,  "RGX_CR_TEXAS_PERF",           rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_s7top },
#else
    RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_TEXAS0),
#endif
#if defined(RGX_FEATURE_XT_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_TEXAS0,  RGX_CR_TEXAS_PERF_INDIRECT,     RGX_CR_TEXAS_PERF,          RGX_CR_TEXAS_PERF_SELECT0,          RGX_CR_TEXAS_PERF_COUNTER_0,          6,              RGX_HWPERF_INDIRECT_BY_PHANTOM,         31,          3,  "RGX_CR_TEXAS_PERF",           rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_xttop },
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
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_PBE0,    RGX_CR_PBE_PERF_INDIRECT, RGX_CR_PBE_PERF,                  RGX_CR_PBE_PERF_SELECT0,            RGX_CR_PBE_PERF_COUNTER_0,            4,              RGX_HWPERF_PHANTOM_INDIRECT_BY_CLUSTER, 21,          3,  "RGX_CR_PBE_PERF",             rgxfw_hwperf_pow_st_indirect, rgx_hwperf_blk_present_s7top },
#else
    RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_PBE0),
#endif

    /*RGX_CNTBLK_ID_BX_TU0*/
    /* Conditional for rgxsrvinit.c UM build where CR defs not unconditional in any context and multi BVNC is not operational */
#if defined (RGX_FEATURE_RAY_TRACING) || defined(__KERNEL__)
    {RGX_CNTBLK_ID_BX_TU0, RGX_CR_BX_TU_PERF_INDIRECT,       DPX_CR_BX_TU_PERF,           DPX_CR_BX_TU_PERF_SELECT0,        DPX_CR_BX_TU_PERF_COUNTER_0,          4,              RGX_HWPERF_DOPPLER_BX_TU_BLKS,          21,          3,  "RGX_CR_BX_TU_PERF",           rgxfw_hwperf_pow_st_gandalf,  rgx_hwperf_blk_present_raytracing },
#else
    RGXFW_HWPERF_CNTBLK_TYPE_UNSUPPORTED(RGX_CNTBLK_ID_BX_TU0),
#endif
	};


IMG_INTERNAL IMG_UINT32
RGXGetHWPerfBlockConfig(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL **ppsModel)
{
	*ppsModel = gasCntBlkTypeModel;
	return IMG_ARR_NUM_ELEMS(gasCntBlkTypeModel);
}

/******************************************************************************
 End of file (rgx_hwperf_table.c)
******************************************************************************/
