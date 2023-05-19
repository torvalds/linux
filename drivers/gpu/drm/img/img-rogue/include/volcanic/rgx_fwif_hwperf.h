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
#include "rgx_hwperf.h"
#include "rgxdefs_km.h"

/* Server and Firmware definitions only */

/*! The number of HWPerf blocks in the GPU */

#if defined(RGX_FIRMWARE)
#define RGX_HWPERF_NUM_SPU ((RGX_FEATURE_NUM_SPU))
#define RGX_HWPERF_NUM_USC ((RGX_FEATURE_NUM_CLUSTERS))
#define RGX_HWPERF_NUM_ISP_PER_SPU ((RGX_FEATURE_NUM_ISP_PER_SPU))
#define RGX_HWPERF_NUM_PBE ((RGX_FEATURE_PBE_PER_SPU) * (RGX_FEATURE_NUM_SPU))
#define RGX_HWPERF_NUM_MERCER ((RGX_FEATURE_NUM_CLUSTERS))
#define RGX_HWPERF_NUM_PBE_SHARED ((RGX_FEATURE_NUM_SPU))
#define RGX_HWPERF_NUM_SWIFT ((RGX_FEATURE_NUM_SPU * RGX_FEATURE_MAX_TPU_PER_SPU))
#define RGX_HWPERF_NUM_TEXAS ((RGX_FEATURE_NUM_SPU))
#if (RGX_FEATURE_RAY_TRACING_ARCH > 2) && (RGX_FEATURE_SPU0_RAC_PRESENT > 0)
#define RGX_HWPERF_NUM_RAC   ((RGX_NUM_RAC))
#else
#define RGX_HWPERF_NUM_RAC   ((0))
#endif
#define RGX_HWPERF_NUM_TPU   ((RGX_FEATURE_NUM_SPU * RGX_FEATURE_MAX_TPU_PER_SPU))
#define RGX_HWPERF_NUM_ISP   ((RGX_FEATURE_NUM_CLUSTERS))

#define RGX_CNTBLK_INDIRECT_COUNT(_class) ((RGX_HWPERF_NUM_ ## _class))

/*! The number of layout blocks defined with configurable
 * performance counters. Compile time constants.
 * This is for the Series 8XT+ layout.
 */
#define RGX_HWPERF_MAX_DEFINED_BLKS (\
	(IMG_UINT32)RGX_CNTBLK_ID_DIRECT_LAST  +\
	RGX_CNTBLK_INDIRECT_COUNT(ISP)         +\
	RGX_CNTBLK_INDIRECT_COUNT(MERCER)      +\
	RGX_CNTBLK_INDIRECT_COUNT(PBE)         +\
	RGX_CNTBLK_INDIRECT_COUNT(PBE_SHARED)  +\
	RGX_CNTBLK_INDIRECT_COUNT(USC)         +\
	RGX_CNTBLK_INDIRECT_COUNT(TPU)         +\
	RGX_CNTBLK_INDIRECT_COUNT(SWIFT)       +\
	RGX_CNTBLK_INDIRECT_COUNT(TEXAS)       +\
	RGX_CNTBLK_INDIRECT_COUNT(RAC))

#endif	/* RGX_FIRMWARE */

/*****************************************************************************/

/* Structure used in the FW's global control data to hold the performance
 * counters provisioned for a given block. */
typedef struct
{
	IMG_UINT32           uiBlockID;
	IMG_UINT32           uiNumCounters;    // Number of counters held
	                                       // in aui32CounterCfg
	                                       // [0..RGX_CNTBLK_COUNTERS_MAX)
	IMG_UINT32           uiEnabled;        // 1 => enabled, 0=> disabled
	RGXFWIF_DEV_VIRTADDR psModel;          // link to model table for uiBlockID
	IMG_UINT32           aui32CounterCfg[RGX_CNTBLK_COUNTERS_MAX];
} RGXFWIF_HWPERF_CTL_BLK;


/*!
 *****************************************************************************
 * Structure used in the FW's global RGXFW_CTL store, holding HWPerf counter
 * block configuration. It is written to by the Server on FW initialisation
 * (PDUMP=1) and by the FW BG kCCB command processing code. It is read by
 * the FW IRQ register programming and HWPerf event generation routines.
 * Size of the sBlkCfg[] array must be consistent between KM/UM and FW.
 * FW will ASSERT if the sizes are different
 * (ui32NumBlocks != RGX_HWPERF_MAX_DEFINED_BLKS)
 ****************************************************************************/
typedef struct
{
	IMG_UINT32                         ui32Reserved;
	IMG_UINT32                         ui32CtrlWord;
	IMG_UINT32                         ui32EnabledBlksCount;
	IMG_UINT32                         ui32NumBlocks;
	RGXFWIF_HWPERF_CTL_BLK RGXFW_ALIGN sBlkCfg[1];	// First array entry
} UNCACHED_ALIGN RGXFWIF_HWPERF_CTL;
#endif
