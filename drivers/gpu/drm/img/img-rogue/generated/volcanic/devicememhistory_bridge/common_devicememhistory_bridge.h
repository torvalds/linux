/*******************************************************************************
@File
@Title          Common bridge header for devicememhistory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for devicememhistory
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
*******************************************************************************/

#ifndef COMMON_DEVICEMEMHISTORY_BRIDGE_H
#define COMMON_DEVICEMEMHISTORY_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "img_types.h"
#include "img_defs.h"
#include "devicemem_typedefs.h"

#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST			0
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYMAP			PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST+0
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYUNMAP			PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST+1
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYMAPVRANGE			PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST+2
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYUNMAPVRANGE			PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST+3
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYSPARSECHANGE			PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST+4
#define PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_LAST			(PVRSRV_BRIDGE_DEVICEMEMHISTORY_CMD_FIRST+4)

/*******************************************
            DevicememHistoryMap
 *******************************************/

/* Bridge in structure for DevicememHistoryMap */
typedef struct PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAP_TAG
{
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEVMEM_SIZE_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_HANDLE hPMR;
	const IMG_CHAR *puiText;
	IMG_UINT32 ui32AllocationIndex;
	IMG_UINT32 ui32Log2PageSize;
} __packed PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAP;

/* Bridge out structure for DevicememHistoryMap */
typedef struct PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAP_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32AllocationIndexOut;
} __packed PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAP;

/*******************************************
            DevicememHistoryUnmap
 *******************************************/

/* Bridge in structure for DevicememHistoryUnmap */
typedef struct PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAP_TAG
{
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEVMEM_SIZE_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_HANDLE hPMR;
	const IMG_CHAR *puiText;
	IMG_UINT32 ui32AllocationIndex;
	IMG_UINT32 ui32Log2PageSize;
} __packed PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAP;

/* Bridge out structure for DevicememHistoryUnmap */
typedef struct PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAP_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32AllocationIndexOut;
} __packed PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAP;

/*******************************************
            DevicememHistoryMapVRange
 *******************************************/

/* Bridge in structure for DevicememHistoryMapVRange */
typedef struct PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAPVRANGE_TAG
{
	IMG_DEV_VIRTADDR sBaseDevVAddr;
	IMG_DEVMEM_SIZE_T uiAllocSize;
	const IMG_CHAR *puiText;
	IMG_UINT32 ui32AllocationIndex;
	IMG_UINT32 ui32Log2PageSize;
	IMG_UINT32 ui32NumPages;
	IMG_UINT32 ui32ui32StartPage;
} __packed PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAPVRANGE;

/* Bridge out structure for DevicememHistoryMapVRange */
typedef struct PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAPVRANGE_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32AllocationIndexOut;
} __packed PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAPVRANGE;

/*******************************************
            DevicememHistoryUnmapVRange
 *******************************************/

/* Bridge in structure for DevicememHistoryUnmapVRange */
typedef struct PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAPVRANGE_TAG
{
	IMG_DEV_VIRTADDR sBaseDevVAddr;
	IMG_DEVMEM_SIZE_T uiAllocSize;
	const IMG_CHAR *puiText;
	IMG_UINT32 ui32AllocationIndex;
	IMG_UINT32 ui32Log2PageSize;
	IMG_UINT32 ui32NumPages;
	IMG_UINT32 ui32ui32StartPage;
} __packed PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAPVRANGE;

/* Bridge out structure for DevicememHistoryUnmapVRange */
typedef struct PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAPVRANGE_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32AllocationIndexOut;
} __packed PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAPVRANGE;

/*******************************************
            DevicememHistorySparseChange
 *******************************************/

/* Bridge in structure for DevicememHistorySparseChange */
typedef struct PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYSPARSECHANGE_TAG
{
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEVMEM_SIZE_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_HANDLE hPMR;
	IMG_UINT32 *pui32AllocPageIndices;
	IMG_UINT32 *pui32FreePageIndices;
	const IMG_CHAR *puiText;
	IMG_UINT32 ui32AllocPageCount;
	IMG_UINT32 ui32AllocationIndex;
	IMG_UINT32 ui32FreePageCount;
	IMG_UINT32 ui32Log2PageSize;
} __packed PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYSPARSECHANGE;

/* Bridge out structure for DevicememHistorySparseChange */
typedef struct PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYSPARSECHANGE_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32AllocationIndexOut;
} __packed PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYSPARSECHANGE;

#endif /* COMMON_DEVICEMEMHISTORY_BRIDGE_H */
