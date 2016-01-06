/*************************************************************************/ /*!
@File           devicemem_heapcfg.c
@Title          Temporary Device Memory 2 stuff
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device memory management
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
*/ /***************************************************************************/

/* our exported API */
#include "devicemem_heapcfg.h"

#include "device.h"
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "osfunc.h"


PVRSRV_ERROR
HeapCfgHeapConfigCount(
    const PVRSRV_DEVICE_NODE *psDeviceNode,
    IMG_UINT32 *puiNumHeapConfigsOut
)
{

    *puiNumHeapConfigsOut = psDeviceNode->sDevMemoryInfo.uiNumHeapConfigs;

    return PVRSRV_OK;
}

PVRSRV_ERROR
HeapCfgHeapCount(
    const PVRSRV_DEVICE_NODE *psDeviceNode,
    IMG_UINT32 uiHeapConfigIndex,
    IMG_UINT32 *puiNumHeapsOut
)
{
    if (uiHeapConfigIndex >= psDeviceNode->sDevMemoryInfo.uiNumHeapConfigs)
    {
        return PVRSRV_ERROR_DEVICEMEM_INVALID_HEAP_CONFIG_INDEX;
    }

    *puiNumHeapsOut = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeapConfigArray[uiHeapConfigIndex].uiNumHeaps;

    return PVRSRV_OK;
}

PVRSRV_ERROR
HeapCfgHeapConfigName(
    const PVRSRV_DEVICE_NODE *psDeviceNode,
    IMG_UINT32 uiHeapConfigIndex,
    IMG_UINT32 uiHeapConfigNameBufSz,
    IMG_CHAR *pszHeapConfigNameOut
)
{
    if (uiHeapConfigIndex >= psDeviceNode->sDevMemoryInfo.uiNumHeapConfigs)
    {
        return PVRSRV_ERROR_DEVICEMEM_INVALID_HEAP_CONFIG_INDEX;
    }

    OSSNPrintf(pszHeapConfigNameOut, uiHeapConfigNameBufSz, "%s", psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeapConfigArray[uiHeapConfigIndex].pszName);

    return PVRSRV_OK;    
}

PVRSRV_ERROR
HeapCfgHeapDetails(
    const PVRSRV_DEVICE_NODE *psDeviceNode,
    IMG_UINT32 uiHeapConfigIndex,
    IMG_UINT32 uiHeapIndex,
    IMG_UINT32 uiHeapNameBufSz,
    IMG_CHAR *pszHeapNameOut,
    IMG_DEV_VIRTADDR *psDevVAddrBaseOut,
    IMG_DEVMEM_SIZE_T *puiHeapLengthOut,
    IMG_UINT32 *puiLog2DataPageSizeOut,
    IMG_UINT32 *puiLog2ImportAlignmentOut
)
{
    DEVMEM_HEAP_BLUEPRINT *psHeapBlueprint;

    if (uiHeapConfigIndex >= psDeviceNode->sDevMemoryInfo.uiNumHeapConfigs)
    {
        return PVRSRV_ERROR_DEVICEMEM_INVALID_HEAP_CONFIG_INDEX;
    }

    if (uiHeapIndex >= psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeapConfigArray[uiHeapConfigIndex].uiNumHeaps)
    {
        return PVRSRV_ERROR_DEVICEMEM_INVALID_HEAP_INDEX;
    }

    psHeapBlueprint = &psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeapConfigArray[uiHeapConfigIndex].psHeapBlueprintArray[uiHeapIndex];

    OSSNPrintf(pszHeapNameOut, uiHeapNameBufSz, "%s", psHeapBlueprint->pszName);
    *psDevVAddrBaseOut = psHeapBlueprint->sHeapBaseAddr;
    *puiHeapLengthOut = psHeapBlueprint->uiHeapLength;
    *puiLog2DataPageSizeOut = psHeapBlueprint->uiLog2DataPageSize;
    *puiLog2ImportAlignmentOut = psHeapBlueprint->uiLog2ImportAlignment;

    return PVRSRV_OK;    
}
