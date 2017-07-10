/*************************************************************************/ /*!
@File			devicemem_history_server.h
@Title          Resource Information abstraction
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Devicemem History functions
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

#ifndef _DEVICEMEM_HISTORY_SERVER_H_
#define _DEVICEMEM_HISTORY_SERVER_H_

#include "img_defs.h"
#include "mm_common.h"
#include "pvrsrv_error.h"
#include "rgxmem.h"

extern PVRSRV_ERROR
DevicememHistoryInitKM(void);

extern void
DevicememHistoryDeInitKM(void);

extern PVRSRV_ERROR
DevicememHistoryMapKM(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize, const char szText[DEVICEMEM_HISTORY_TEXT_BUFSZ]);

extern PVRSRV_ERROR
DevicememHistoryUnmapKM(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize, const char szText[DEVICEMEM_HISTORY_TEXT_BUFSZ]);


PVRSRV_ERROR DevicememHistoryMapNewKM(PMR *psPMR,
							IMG_UINT32 ui32Offset,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32PageSize,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *pui32AllocationIndexOut);

PVRSRV_ERROR DevicememHistoryUnmapNewKM(PMR *psPMR,
							IMG_UINT32 ui32Offset,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32PageSize,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *pui32AllocationIndexOut);

PVRSRV_ERROR DevicememHistoryMapVRangeKM(IMG_DEV_VIRTADDR sBaseDevVAddr,
							IMG_UINT32 ui32StartPage,
							IMG_UINT32 ui32NumPages,
							IMG_DEVMEM_SIZE_T uiAllocSize,
							const IMG_CHAR szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32Log2PageSize,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *ui32AllocationIndexOut);

PVRSRV_ERROR DevicememHistoryUnmapVRangeKM(IMG_DEV_VIRTADDR sBaseDevVAddr,
							IMG_UINT32 ui32StartPage,
							IMG_UINT32 ui32NumPages,
							IMG_DEVMEM_SIZE_T uiAllocSize,
							const IMG_CHAR szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32Log2PageSize,
							IMG_UINT32 ui32AllocationIndex,
							IMG_UINT32 *ui32AllocationIndexOut);

PVRSRV_ERROR DevicememHistorySparseChangeKM(PMR *psPMR,
							IMG_UINT32 ui32Offset,
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEVMEM_SIZE_T uiSize,
							const char szName[DEVICEMEM_HISTORY_TEXT_BUFSZ],
							IMG_UINT32 ui32PageSize,
							IMG_UINT32 ui32AllocPageCount,
							IMG_UINT32 *paui32AllocPageIndices,
							IMG_UINT32 ui32FreePageCount,
							IMG_UINT32 *pauiFreePageIndices,
							IMG_UINT32 AllocationIndex,
							IMG_UINT32 *pui32AllocationIndexOut);

/* used when the PID does not matter */
#define DEVICEMEM_HISTORY_PID_ANY 0xFFFFFFFE

typedef struct _DEVICEMEM_HISTORY_QUERY_IN_
{
	IMG_PID uiPID;
	IMG_DEV_VIRTADDR sDevVAddr;
} DEVICEMEM_HISTORY_QUERY_IN;

/* Store up to 4 results for a lookup. In the case of the faulting page being
 * re-mapped between the page fault occurring on HW and the page fault analysis
 * being done, the second result entry will show the allocation being unmapped.
 * A further 2 entries are added to cater for multiple buffers in the same page.
 */
#define DEVICEMEM_HISTORY_QUERY_OUT_MAX_RESULTS 4

typedef struct _DEVICEMEM_HISTORY_QUERY_OUT_RESULT_
{
	IMG_CHAR szString[DEVICEMEM_HISTORY_TEXT_BUFSZ];
	IMG_DEV_VIRTADDR sBaseDevVAddr;
	size_t uiSize;
	IMG_BOOL bMap;
	IMG_BOOL bRange;
	IMG_BOOL bAll;
	IMG_UINT64 ui64When;
	IMG_UINT64 ui64Age;
	/* info for sparse map/unmap operations (i.e. bRange=IMG_TRUE) */
	IMG_UINT32 ui32StartPage;
	IMG_UINT32 ui32PageCount;
	IMG_DEV_VIRTADDR sMapStartAddr;
	IMG_DEV_VIRTADDR sMapEndAddr;
	RGXMEM_PROCESS_INFO sProcessInfo;
} DEVICEMEM_HISTORY_QUERY_OUT_RESULT;

typedef struct _DEVICEMEM_HISTORY_QUERY_OUT_
{
	IMG_UINT32 ui32NumResults;
	/* result 0 is the newest */
	DEVICEMEM_HISTORY_QUERY_OUT_RESULT sResults[DEVICEMEM_HISTORY_QUERY_OUT_MAX_RESULTS];
} DEVICEMEM_HISTORY_QUERY_OUT;

extern IMG_BOOL
DevicememHistoryQuery(DEVICEMEM_HISTORY_QUERY_IN *psQueryIn,
                      DEVICEMEM_HISTORY_QUERY_OUT *psQueryOut,
                      IMG_UINT32 ui32PageSizeBytes,
                      IMG_BOOL bMatchAnyAllocInPage);

#endif
