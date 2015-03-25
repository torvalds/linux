/*************************************************************************/ /*!
@File
@Title          Physcial heap management header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the interface for the physical heap management
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

#include "img_types.h"
#include "pvrsrv_error.h"

#ifndef _PHYSHEAP_H_
#define _PHYSHEAP_H_

typedef struct _PHYS_HEAP_ PHYS_HEAP;

typedef IMG_VOID (*CpuPAddrToDevPAddr)(IMG_HANDLE hPrivData,
									   IMG_DEV_PHYADDR *psDevPAddr,
									   IMG_CPU_PHYADDR *psCpuPAddr);

typedef IMG_VOID (*DevPAddrToCpuPAddr)(IMG_HANDLE hPrivData,
									   IMG_CPU_PHYADDR *psCpuPAddr,
									   IMG_DEV_PHYADDR *psDevPAddr);

typedef struct _PHYS_HEAP_FUNCTIONS_
{
	/*! Translate CPU physical address to device physical address */
	CpuPAddrToDevPAddr	pfnCpuPAddrToDevPAddr;
	/*! Translate device physical address to CPU physical address */
	DevPAddrToCpuPAddr	pfnDevPAddrToCpuPAddr;
} PHYS_HEAP_FUNCTIONS;

typedef enum _PHYS_HEAP_TYPE_
{
	PHYS_HEAP_TYPE_UNKNOWN = 0,
	PHYS_HEAP_TYPE_UMA,
	PHYS_HEAP_TYPE_LMA,
} PHYS_HEAP_TYPE;

typedef struct _PHYS_HEAP_CONFIG_
{
	IMG_UINT32				ui32PhysHeapID;
	PHYS_HEAP_TYPE			eType;
	/*
		Note:
		sStartAddr and uiSize are only required for LMA heaps
	*/
	IMG_CPU_PHYADDR			sStartAddr;
	IMG_UINT64				uiSize;
	IMG_CHAR				*pszPDumpMemspaceName;
	PHYS_HEAP_FUNCTIONS		*psMemFuncs;
	IMG_HANDLE				hPrivData;
} PHYS_HEAP_CONFIG;

PVRSRV_ERROR PhysHeapRegister(PHYS_HEAP_CONFIG *psConfig,
							  PHYS_HEAP **ppsPhysHeap);

IMG_VOID PhysHeapUnregister(PHYS_HEAP *psPhysHeap);

PVRSRV_ERROR PhysHeapAcquire(IMG_UINT32 ui32PhysHeapID,
							 PHYS_HEAP **ppsPhysHeap);

IMG_VOID PhysHeapRelease(PHYS_HEAP *psPhysHeap);

PHYS_HEAP_TYPE PhysHeapGetType(PHYS_HEAP *psPhysHeap);

PVRSRV_ERROR PhysHeapGetAddress(PHYS_HEAP *psPhysHeap,
								IMG_CPU_PHYADDR *psCpuPAddr);

PVRSRV_ERROR PhysHeapGetSize(PHYS_HEAP *psPhysHeap,
						     IMG_UINT64 *puiSize);

IMG_VOID PhysHeapCpuPAddrToDevPAddr(PHYS_HEAP *psPhysHeap,
									IMG_DEV_PHYADDR *psDevPAddr,
									IMG_CPU_PHYADDR *psCpuPAddr);
IMG_VOID PhysHeapDevPAddrToCpuPAddr(PHYS_HEAP *psPhysHeap,
									IMG_CPU_PHYADDR *psCpuPAddr,
									IMG_DEV_PHYADDR *psDevPAddr);

IMG_CHAR *PhysHeapPDumpMemspaceName(PHYS_HEAP *psPhysHeap);

PVRSRV_ERROR PhysHeapInit(IMG_VOID);
PVRSRV_ERROR PhysHeapDeinit(IMG_VOID);

#endif /* _PHYSHEAP_H_ */
