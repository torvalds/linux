/*************************************************************************/ /*!
@File           rgx_fwif_hwperf.h
@Title          RGX HWPerf support
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Shared header between RGX firmware and Init process
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
#ifndef RGX_FWIF_HWPERF_H
#define RGX_FWIF_HWPERF_H

#include "rgx_fwif_shared.h"
#include "rgx_hwperf_km.h"
#include "rgxdefs_km.h"


/*****************************************************************************/

/* Structure to hold a block's parameters for passing between the BG context
 * and the IRQ context when applying a configuration request. */
typedef struct _RGXFWIF_HWPERF_CTL_BLK_
{
	IMG_BOOL                bValid;
	IMG_BOOL                bEnabled;
	IMG_UINT32              eBlockID;
	IMG_UINT32              uiCounterMask;
	IMG_UINT64  RGXFW_ALIGN aui64CounterCfg[RGX_CNTBLK_COUNTERS_MAX];
}  RGXFWIF_HWPERF_CTL_BLK;

/* Structure used to hold the configuration of the non-mux counters blocks */
typedef struct _RGXFW_HWPERF_SELECT_
{
	IMG_UINT32            ui32NumSelectedCounters;
	IMG_UINT32            aui32SelectedCountersIDs[RGX_HWPERF_MAX_CUSTOM_CNTRS];
} RGXFW_HWPERF_SELECT;

/* Structure to hold the whole configuration request details for all blocks
 * The block masks and counts are used to optimise reading of this data. */
typedef struct _RGXFWIF_HWPERF_CTL_
{
	IMG_BOOL                           bResetOrdinal;

	IMG_UINT32                         ui32SelectedCountersBlockMask;
	RGXFW_HWPERF_SELECT RGXFW_ALIGN    SelCntr[RGX_HWPERF_MAX_CUSTOM_BLKS];

	IMG_UINT32                         ui32EnabledBlksCount;
	RGXFWIF_HWPERF_CTL_BLK RGXFW_ALIGN sBlkCfg[RGX_HWPERF_MAX_DEFINED_BLKS];
} UNCACHED_ALIGN RGXFWIF_HWPERF_CTL;

/* NOTE: The switch statement in this function must be kept in alignment with
 * the enumeration RGX_HWPERF_CNTBLK_ID defined in rgx_hwperf_km.h. ASSERTs may
 * result if not.
 * The function provides a hash lookup to get a handle on the global store for
 * a block's configuration store from it's block ID.
 */
#ifdef INLINE_IS_PRAGMA
#pragma inline(rgxfw_hwperf_get_block_ctl)
#endif
static INLINE RGXFWIF_HWPERF_CTL_BLK* rgxfw_hwperf_get_block_ctl(
		RGX_HWPERF_CNTBLK_ID eBlockID, RGXFWIF_HWPERF_CTL *psHWPerfInitData)
{
	IMG_INT32 i32Idx = -1;

	/* Hash the block ID into a control configuration array index */
	switch(eBlockID)
	{
		case RGX_CNTBLK_ID_TA:
		case RGX_CNTBLK_ID_RASTER:
		case RGX_CNTBLK_ID_HUB:
		case RGX_CNTBLK_ID_TORNADO:
		case RGX_CNTBLK_ID_JONES:
		case RGX_CNTBLK_ID_BF:
		case RGX_CNTBLK_ID_BT:
		case RGX_CNTBLK_ID_RT:
		case RGX_CNTBLK_ID_SH:
		{
			i32Idx = eBlockID;
			break;
		}
		case RGX_CNTBLK_ID_TPU_MCU0:
		case RGX_CNTBLK_ID_TPU_MCU1:
		case RGX_CNTBLK_ID_TPU_MCU2:
		case RGX_CNTBLK_ID_TPU_MCU3:
		case RGX_CNTBLK_ID_TPU_MCU4:
		case RGX_CNTBLK_ID_TPU_MCU5:
		case RGX_CNTBLK_ID_TPU_MCU6:
		case RGX_CNTBLK_ID_TPU_MCU7:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		case RGX_CNTBLK_ID_USC0:
		case RGX_CNTBLK_ID_USC1:
		case RGX_CNTBLK_ID_USC2:
		case RGX_CNTBLK_ID_USC3:
		case RGX_CNTBLK_ID_USC4:
		case RGX_CNTBLK_ID_USC5:
		case RGX_CNTBLK_ID_USC6:
		case RGX_CNTBLK_ID_USC7:
		case RGX_CNTBLK_ID_USC8:
		case RGX_CNTBLK_ID_USC9:
		case RGX_CNTBLK_ID_USC10:
		case RGX_CNTBLK_ID_USC11:
		case RGX_CNTBLK_ID_USC12:
		case RGX_CNTBLK_ID_USC13:
		case RGX_CNTBLK_ID_USC14:
		case RGX_CNTBLK_ID_USC15:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						RGX_CNTBLK_INDIRECT_COUNT(TPU_MCU, 7) +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		case RGX_CNTBLK_ID_TEXAS0:
		case RGX_CNTBLK_ID_TEXAS1:
		case RGX_CNTBLK_ID_TEXAS2:
		case RGX_CNTBLK_ID_TEXAS3:
		case RGX_CNTBLK_ID_TEXAS4:
		case RGX_CNTBLK_ID_TEXAS5:
		case RGX_CNTBLK_ID_TEXAS6:
		case RGX_CNTBLK_ID_TEXAS7:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						RGX_CNTBLK_INDIRECT_COUNT(TPU_MCU, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(USC, 15) +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		case RGX_CNTBLK_ID_RASTER0:
		case RGX_CNTBLK_ID_RASTER1:
		case RGX_CNTBLK_ID_RASTER2:
		case RGX_CNTBLK_ID_RASTER3:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						RGX_CNTBLK_INDIRECT_COUNT(TPU_MCU, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(USC, 15) +
						RGX_CNTBLK_INDIRECT_COUNT(TEXAS, 7) +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		case RGX_CNTBLK_ID_BLACKPEARL0:
		case RGX_CNTBLK_ID_BLACKPEARL1:
		case RGX_CNTBLK_ID_BLACKPEARL2:
		case RGX_CNTBLK_ID_BLACKPEARL3:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						RGX_CNTBLK_INDIRECT_COUNT(TPU_MCU, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(USC, 15) +
						RGX_CNTBLK_INDIRECT_COUNT(TEXAS, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(RASTER, 3) +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		case RGX_CNTBLK_ID_PBE0:
		case RGX_CNTBLK_ID_PBE1:
		case RGX_CNTBLK_ID_PBE2:
		case RGX_CNTBLK_ID_PBE3:
		case RGX_CNTBLK_ID_PBE4:
		case RGX_CNTBLK_ID_PBE5:
		case RGX_CNTBLK_ID_PBE6:
		case RGX_CNTBLK_ID_PBE7:
		case RGX_CNTBLK_ID_PBE8:
		case RGX_CNTBLK_ID_PBE9:
		case RGX_CNTBLK_ID_PBE10:
		case RGX_CNTBLK_ID_PBE11:
		case RGX_CNTBLK_ID_PBE12:
		case RGX_CNTBLK_ID_PBE13:
		case RGX_CNTBLK_ID_PBE14:
		case RGX_CNTBLK_ID_PBE15:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						RGX_CNTBLK_INDIRECT_COUNT(TPU_MCU, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(USC, 15) +
						RGX_CNTBLK_INDIRECT_COUNT(TEXAS, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(RASTER, 3) +
						RGX_CNTBLK_INDIRECT_COUNT(BLACKPEARL, 3) +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		case RGX_CNTBLK_ID_BX_TU0:
		case RGX_CNTBLK_ID_BX_TU1:
		case RGX_CNTBLK_ID_BX_TU2:
		case RGX_CNTBLK_ID_BX_TU3:
		{
			i32Idx = RGX_CNTBLK_ID_DIRECT_LAST +
						RGX_CNTBLK_INDIRECT_COUNT(TPU_MCU, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(USC, 15) +
						RGX_CNTBLK_INDIRECT_COUNT(TEXAS, 7) +
						RGX_CNTBLK_INDIRECT_COUNT(RASTER, 3) +
						RGX_CNTBLK_INDIRECT_COUNT(BLACKPEARL, 3) +
						RGX_CNTBLK_INDIRECT_COUNT(PBE, 15) +
						(eBlockID & RGX_CNTBLK_ID_UNIT_MASK);
			break;
		}
		default:
		{
			return NULL;
		}
	}
	if ((i32Idx < 0) || (i32Idx >= RGX_HWPERF_MAX_DEFINED_BLKS))
	{
		return NULL;
	}
	return &psHWPerfInitData->sBlkCfg[i32Idx];
}

#endif
