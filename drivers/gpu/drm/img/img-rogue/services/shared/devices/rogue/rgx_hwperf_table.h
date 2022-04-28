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
#if defined(__KERNEL__)
#include "rgxdevice.h"
#endif
/*****************************************************************************/

/* Forward declaration */
typedef struct RGXFW_HWPERF_CNTBLK_TYPE_MODEL_ RGXFW_HWPERF_CNTBLK_TYPE_MODEL;

/* Function pointer type for functions to check dynamic power state of
 * counter block instance. Used only in firmware. */
typedef bool (*PFN_RGXFW_HWPERF_CNTBLK_POWERED)(
		RGX_HWPERF_CNTBLK_ID eBlkType,
		IMG_UINT8 ui8UnitId);

#if defined(__KERNEL__)
/* Counter block run-time info */
typedef struct
{
	IMG_UINT32 ui32IndirectReg;          /* 0 if direct type otherwise the indirect control register to select indirect unit */
	IMG_UINT32 ui32NumUnits;             /* Number of instances of this block type in the core */
} RGX_HWPERF_CNTBLK_RT_INFO;
#endif

/* Function pointer type for functions to check block is valid and present
 * on that RGX Device at runtime. It may have compile logic or run-time
 * logic depending on where the code executes: server, srvinit or firmware.
 * Values in the psRtInfo output parameter are only valid if true returned.
 */
typedef IMG_BOOL (*PFN_RGXFW_HWPERF_CNTBLK_PRESENT)(
		const struct RGXFW_HWPERF_CNTBLK_TYPE_MODEL_* psBlkTypeDesc,
		const void *pvDev_km,
		void *pvRtInfo);

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
	/* Could use RGXFW_ALIGN_DCACHEL here but then we would waste 40% of the cache line? */
	IMG_UINT32 ui32CntBlkIdBase;         /* The starting block id for this block type */
	IMG_UINT32 ui32IndirectReg;          /* 0 if direct type otherwise the indirect control register to select indirect unit */
	IMG_UINT32 ui32PerfReg;              /* RGX_CR_*_PERF register for this block type */
	IMG_UINT32 ui32Select0BaseReg;       /* RGX_CR_*_PERF_SELECT0 register for this block type */
	IMG_UINT32 ui32Counter0BaseReg;      /* RGX_CR_*_PERF_COUNTER_0 register for this block type */
	IMG_UINT8  ui8NumCounters;          /* Number of counters in this block type */
	IMG_UINT8  ui8NumUnits;             /* Number of instances of this block type in the core */
	IMG_UINT8  ui8SelectRegModeShift;   /* Mode field shift value of select registers */
	IMG_UINT8  ui8SelectRegOffsetShift; /* Interval between select registers, either 8 bytes or 16, hence << 3 or << 4 */
	const IMG_CHAR *pszBlockNameComment;             /* Name of the PERF register. Used while dumping the perf counters to pdumps */
	PFN_RGXFW_HWPERF_CNTBLK_POWERED pfnIsBlkPowered; /* A function to determine dynamic power state for the block type */
	PFN_RGXFW_HWPERF_CNTBLK_PRESENT pfnIsBlkPresent; /* A function to determine presence on RGX Device at run-time */
};

/*****************************************************************************/

IMG_INTERNAL IMG_UINT32 RGXGetHWPerfBlockConfig(const RGXFW_HWPERF_CNTBLK_TYPE_MODEL **ppsModel);

#endif /* RGX_HWPERF_TABLE_H */

/******************************************************************************
 End of file (rgx_hwperf_table.h)
******************************************************************************/
