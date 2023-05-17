/*******************************************************************************
@File
@Title          Common bridge header for dma
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for dma
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

#ifndef COMMON_DMA_BRIDGE_H
#define COMMON_DMA_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_DMA_CMD_FIRST			0
#define PVRSRV_BRIDGE_DMA_DMATRANSFER			PVRSRV_BRIDGE_DMA_CMD_FIRST+0
#define PVRSRV_BRIDGE_DMA_DMASPARSEMAPPINGTABLE			PVRSRV_BRIDGE_DMA_CMD_FIRST+1
#define PVRSRV_BRIDGE_DMA_DMADEVICEPARAMS			PVRSRV_BRIDGE_DMA_CMD_FIRST+2
#define PVRSRV_BRIDGE_DMA_CMD_LAST			(PVRSRV_BRIDGE_DMA_CMD_FIRST+2)

/*******************************************
            DmaTransfer
 *******************************************/

/* Bridge in structure for DmaTransfer */
typedef struct PVRSRV_BRIDGE_IN_DMATRANSFER_TAG
{
	IMG_UINT64 *pui64Address;
	IMG_DEVMEM_OFFSET_T *puiOffset;
	IMG_DEVMEM_SIZE_T *puiSize;
	IMG_HANDLE *phPMR;
	PVRSRV_TIMELINE hUpdateTimeline;
	IMG_UINT32 ui32NumDMAs;
	IMG_UINT32 ui32uiFlags;
} __packed PVRSRV_BRIDGE_IN_DMATRANSFER;

/* Bridge out structure for DmaTransfer */
typedef struct PVRSRV_BRIDGE_OUT_DMATRANSFER_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_DMATRANSFER;

/*******************************************
            DmaSparseMappingTable
 *******************************************/

/* Bridge in structure for DmaSparseMappingTable */
typedef struct PVRSRV_BRIDGE_IN_DMASPARSEMAPPINGTABLE_TAG
{
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_HANDLE hPMR;
	IMG_BOOL *pbTable;
	IMG_UINT32 ui32SizeInPages;
} __packed PVRSRV_BRIDGE_IN_DMASPARSEMAPPINGTABLE;

/* Bridge out structure for DmaSparseMappingTable */
typedef struct PVRSRV_BRIDGE_OUT_DMASPARSEMAPPINGTABLE_TAG
{
	IMG_BOOL *pbTable;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_DMASPARSEMAPPINGTABLE;

/*******************************************
            DmaDeviceParams
 *******************************************/

/* Bridge in structure for DmaDeviceParams */
typedef struct PVRSRV_BRIDGE_IN_DMADEVICEPARAMS_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_DMADEVICEPARAMS;

/* Bridge out structure for DmaDeviceParams */
typedef struct PVRSRV_BRIDGE_OUT_DMADEVICEPARAMS_TAG
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32DmaBuffAlign;
	IMG_UINT32 ui32DmaTransferMult;
} __packed PVRSRV_BRIDGE_OUT_DMADEVICEPARAMS;

#endif /* COMMON_DMA_BRIDGE_H */
