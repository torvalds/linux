/*************************************************************************/ /*!
@Title          Direct client bridge for mm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include "client_mm_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "pvrsrv_memallocflags.h"
#include "devicemem_typedefs.h"

#include "devicemem.h"
#include "devicemem_server.h"
#include "pmr.h"
#include "devicemem_heapcfg.h"
#include "physmem.h"


IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRExportPMR(IMG_HANDLE hBridge,
							  IMG_HANDLE hPMR,
							  IMG_HANDLE *phPMRExport,
							  IMG_UINT64 *pui64Size,
							  IMG_UINT32 *pui32Log2Contig,
							  IMG_UINT64 *pui64Password)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PMR_EXPORT * psPMRExportInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRExportPMR(
					psPMRInt,
					&psPMRExportInt,
					pui64Size,
					pui32Log2Contig,
					pui64Password);

	*phPMRExport = psPMRExportInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnexportPMR(IMG_HANDLE hBridge,
							    IMG_HANDLE hPMRExport)
{
	PVRSRV_ERROR eError;
	PMR_EXPORT * psPMRExportInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRExportInt = (PMR_EXPORT *) hPMRExport;

	eError =
		PMRUnexportPMR(
					psPMRExportInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRGetUID(IMG_HANDLE hBridge,
						       IMG_HANDLE hPMR,
						       IMG_UINT64 *pui64UID)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRGetUID(
					psPMRInt,
					pui64UID);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRMakeLocalImportHandle(IMG_HANDLE hBridge,
								      IMG_HANDLE hBuffer,
								      IMG_HANDLE *phExtMem)
{
	PVRSRV_ERROR eError;
	PMR * psBufferInt;
	PMR * psExtMemInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psBufferInt = (PMR *) hBuffer;

	eError =
		PMRMakeLocalImportHandle(
					psBufferInt,
					&psExtMemInt);

	*phExtMem = psExtMemInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnmakeLocalImportHandle(IMG_HANDLE hBridge,
									IMG_HANDLE hExtMem)
{
	PVRSRV_ERROR eError;
	PMR * psExtMemInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psExtMemInt = (PMR *) hExtMem;

	eError =
		PMRUnmakeLocalImportHandle(
					psExtMemInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRImportPMR(IMG_HANDLE hBridge,
							  IMG_HANDLE hPMRExport,
							  IMG_UINT64 ui64uiPassword,
							  IMG_UINT64 ui64uiSize,
							  IMG_UINT32 ui32uiLog2Contig,
							  IMG_HANDLE *phPMR)
{
	PVRSRV_ERROR eError;
	PMR_EXPORT * psPMRExportInt;
	PMR * psPMRInt;

	psPMRExportInt = (PMR_EXPORT *) hPMRExport;

	eError =
		PMRImportPMR(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					psPMRExportInt,
					ui64uiPassword,
					ui64uiSize,
					ui32uiLog2Contig,
					&psPMRInt);

	*phPMR = psPMRInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRLocalImportPMR(IMG_HANDLE hBridge,
							       IMG_HANDLE hExtHandle,
							       IMG_HANDLE *phPMR,
							       IMG_DEVMEM_SIZE_T *puiSize,
							       IMG_DEVMEM_ALIGN_T *psAlign)
{
	PVRSRV_ERROR eError;
	PMR * psExtHandleInt;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psExtHandleInt = (PMR *) hExtHandle;

	eError =
		PMRLocalImportPMR(
					psExtHandleInt,
					&psPMRInt,
					puiSize,
					psAlign);

	*phPMR = psPMRInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnrefPMR(IMG_HANDLE hBridge,
							 IMG_HANDLE hPMR)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRUnrefPMR(
					psPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRUnrefUnlockPMR(IMG_HANDLE hBridge,
							       IMG_HANDLE hPMR)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRUnrefUnlockPMR(
					psPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePhysmemNewRamBackedPMR(IMG_HANDLE hBridge,
								    IMG_DEVMEM_SIZE_T uiSize,
								    IMG_DEVMEM_SIZE_T uiChunkSize,
								    IMG_UINT32 ui32NumPhysChunks,
								    IMG_UINT32 ui32NumVirtChunks,
								    IMG_UINT32 *pui32MappingTable,
								    IMG_UINT32 ui32Log2PageSize,
								    PVRSRV_MEMALLOCFLAGS_T uiFlags,
								    IMG_UINT32 ui32AnnotationLength,
								    const IMG_CHAR *puiAnnotation,
								    IMG_HANDLE *phPMRPtr)
{
	PVRSRV_ERROR eError;
	PMR * psPMRPtrInt;


	eError =
		PhysmemNewRamBackedPMR(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					uiSize,
					uiChunkSize,
					ui32NumPhysChunks,
					ui32NumVirtChunks,
					pui32MappingTable,
					ui32Log2PageSize,
					uiFlags,
					ui32AnnotationLength,
					puiAnnotation,
					&psPMRPtrInt);

	*phPMRPtr = psPMRPtrInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePhysmemNewRamBackedLockedPMR(IMG_HANDLE hBridge,
									  IMG_DEVMEM_SIZE_T uiSize,
									  IMG_DEVMEM_SIZE_T uiChunkSize,
									  IMG_UINT32 ui32NumPhysChunks,
									  IMG_UINT32 ui32NumVirtChunks,
									  IMG_UINT32 *pui32MappingTable,
									  IMG_UINT32 ui32Log2PageSize,
									  PVRSRV_MEMALLOCFLAGS_T uiFlags,
									  IMG_UINT32 ui32AnnotationLength,
									  const IMG_CHAR *puiAnnotation,
									  IMG_HANDLE *phPMRPtr)
{
	PVRSRV_ERROR eError;
	PMR * psPMRPtrInt;


	eError =
		PhysmemNewRamBackedLockedPMR(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					uiSize,
					uiChunkSize,
					ui32NumPhysChunks,
					ui32NumVirtChunks,
					pui32MappingTable,
					ui32Log2PageSize,
					uiFlags,
					ui32AnnotationLength,
					puiAnnotation,
					&psPMRPtrInt);

	*phPMRPtr = psPMRPtrInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntPin(IMG_HANDLE hBridge,
							  IMG_HANDLE hPMR)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntPin(
					psPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnpin(IMG_HANDLE hBridge,
							    IMG_HANDLE hPMR)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntUnpin(
					psPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntPinValidate(IMG_HANDLE hBridge,
								  IMG_HANDLE hMapping,
								  IMG_HANDLE hPMR)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_MAPPING * psMappingInt;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psMappingInt = (DEVMEMINT_MAPPING *) hMapping;
	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntPinValidate(
					psMappingInt,
					psPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnpinInvalidate(IMG_HANDLE hBridge,
								      IMG_HANDLE hMapping,
								      IMG_HANDLE hPMR)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_MAPPING * psMappingInt;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psMappingInt = (DEVMEMINT_MAPPING *) hMapping;
	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntUnpinInvalidate(
					psMappingInt,
					psPMRInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntCtxCreate(IMG_HANDLE hBridge,
								IMG_BOOL bbKernelMemoryCtx,
								IMG_HANDLE *phDevMemServerContext,
								IMG_HANDLE *phPrivData,
								IMG_UINT32 *pui32CPUCacheLineSize)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX * psDevMemServerContextInt;
	IMG_HANDLE hPrivDataInt;


	eError =
		DevmemIntCtxCreate(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					bbKernelMemoryCtx,
					&psDevMemServerContextInt,
					&hPrivDataInt,
					pui32CPUCacheLineSize);

	*phDevMemServerContext = psDevMemServerContextInt;
	*phPrivData = hPrivDataInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntCtxDestroy(IMG_HANDLE hBridge,
								 IMG_HANDLE hDevmemServerContext)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX * psDevmemServerContextInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemServerContextInt = (DEVMEMINT_CTX *) hDevmemServerContext;

	eError =
		DevmemIntCtxDestroy(
					psDevmemServerContextInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntHeapCreate(IMG_HANDLE hBridge,
								 IMG_HANDLE hDevmemCtx,
								 IMG_DEV_VIRTADDR sHeapBaseAddr,
								 IMG_DEVMEM_SIZE_T uiHeapLength,
								 IMG_UINT32 ui32Log2DataPageSize,
								 IMG_HANDLE *phDevmemHeapPtr)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX * psDevmemCtxInt;
	DEVMEMINT_HEAP * psDevmemHeapPtrInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemCtxInt = (DEVMEMINT_CTX *) hDevmemCtx;

	eError =
		DevmemIntHeapCreate(
					psDevmemCtxInt,
					sHeapBaseAddr,
					uiHeapLength,
					ui32Log2DataPageSize,
					&psDevmemHeapPtrInt);

	*phDevmemHeapPtr = psDevmemHeapPtrInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntHeapDestroy(IMG_HANDLE hBridge,
								  IMG_HANDLE hDevmemHeap)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_HEAP * psDevmemHeapInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemHeapInt = (DEVMEMINT_HEAP *) hDevmemHeap;

	eError =
		DevmemIntHeapDestroy(
					psDevmemHeapInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntMapPMR(IMG_HANDLE hBridge,
							     IMG_HANDLE hDevmemServerHeap,
							     IMG_HANDLE hReservation,
							     IMG_HANDLE hPMR,
							     PVRSRV_MEMALLOCFLAGS_T uiMapFlags,
							     IMG_HANDLE *phMapping)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_HEAP * psDevmemServerHeapInt;
	DEVMEMINT_RESERVATION * psReservationInt;
	PMR * psPMRInt;
	DEVMEMINT_MAPPING * psMappingInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemServerHeapInt = (DEVMEMINT_HEAP *) hDevmemServerHeap;
	psReservationInt = (DEVMEMINT_RESERVATION *) hReservation;
	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntMapPMR(
					psDevmemServerHeapInt,
					psReservationInt,
					psPMRInt,
					uiMapFlags,
					&psMappingInt);

	*phMapping = psMappingInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnmapPMR(IMG_HANDLE hBridge,
							       IMG_HANDLE hMapping)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_MAPPING * psMappingInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psMappingInt = (DEVMEMINT_MAPPING *) hMapping;

	eError =
		DevmemIntUnmapPMR(
					psMappingInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntReserveRange(IMG_HANDLE hBridge,
								   IMG_HANDLE hDevmemServerHeap,
								   IMG_DEV_VIRTADDR sAddress,
								   IMG_DEVMEM_SIZE_T uiLength,
								   IMG_HANDLE *phReservation)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_HEAP * psDevmemServerHeapInt;
	DEVMEMINT_RESERVATION * psReservationInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemServerHeapInt = (DEVMEMINT_HEAP *) hDevmemServerHeap;

	eError =
		DevmemIntReserveRange(
					psDevmemServerHeapInt,
					sAddress,
					uiLength,
					&psReservationInt);

	*phReservation = psReservationInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnreserveRange(IMG_HANDLE hBridge,
								     IMG_HANDLE hReservation)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_RESERVATION * psReservationInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psReservationInt = (DEVMEMINT_RESERVATION *) hReservation;

	eError =
		DevmemIntUnreserveRange(
					psReservationInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeChangeSparseMem(IMG_HANDLE hBridge,
							     IMG_HANDLE hSrvDevMemHeap,
							     IMG_HANDLE hPMR,
							     IMG_UINT32 ui32AllocPageCount,
							     IMG_UINT32 *pui32AllocPageIndices,
							     IMG_UINT32 ui32FreePageCount,
							     IMG_UINT32 *pui32FreePageIndices,
							     IMG_UINT32 ui32SparseFlags,
							     PVRSRV_MEMALLOCFLAGS_T uiFlags,
							     IMG_DEV_VIRTADDR sDevVAddr,
							     IMG_UINT64 ui64CPUVAddr)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_HEAP * psSrvDevMemHeapInt;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSrvDevMemHeapInt = (DEVMEMINT_HEAP *) hSrvDevMemHeap;
	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntChangeSparse(
					psSrvDevMemHeapInt,
					psPMRInt,
					ui32AllocPageCount,
					pui32AllocPageIndices,
					ui32FreePageCount,
					pui32FreePageIndices,
					ui32SparseFlags,
					uiFlags,
					sDevVAddr,
					ui64CPUVAddr);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntMapPages(IMG_HANDLE hBridge,
							       IMG_HANDLE hReservation,
							       IMG_HANDLE hPMR,
							       IMG_UINT32 ui32PageCount,
							       IMG_UINT32 ui32PhysicalPgOffset,
							       PVRSRV_MEMALLOCFLAGS_T uiFlags,
							       IMG_DEV_VIRTADDR sDevVAddr)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_RESERVATION * psReservationInt;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psReservationInt = (DEVMEMINT_RESERVATION *) hReservation;
	psPMRInt = (PMR *) hPMR;

	eError =
		DevmemIntMapPages(
					psReservationInt,
					psPMRInt,
					ui32PageCount,
					ui32PhysicalPgOffset,
					uiFlags,
					sDevVAddr);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntUnmapPages(IMG_HANDLE hBridge,
								 IMG_HANDLE hReservation,
								 IMG_DEV_VIRTADDR sDevVAddr,
								 IMG_UINT32 ui32PageCount)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_RESERVATION * psReservationInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psReservationInt = (DEVMEMINT_RESERVATION *) hReservation;

	eError =
		DevmemIntUnmapPages(
					psReservationInt,
					sDevVAddr,
					ui32PageCount);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIsVDevAddrValid(IMG_HANDLE hBridge,
								   IMG_HANDLE hDevmemCtx,
								   IMG_DEV_VIRTADDR sAddress)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX * psDevmemCtxInt;

	psDevmemCtxInt = (DEVMEMINT_CTX *) hDevmemCtx;

	eError =
		DevmemIntIsVDevAddrValid(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					psDevmemCtxInt,
					sAddress);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapConfigCount(IMG_HANDLE hBridge,
								    IMG_UINT32 *pui32NumHeapConfigs)
{
	PVRSRV_ERROR eError;


	eError =
		HeapCfgHeapConfigCount(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					pui32NumHeapConfigs);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapCount(IMG_HANDLE hBridge,
							      IMG_UINT32 ui32HeapConfigIndex,
							      IMG_UINT32 *pui32NumHeaps)
{
	PVRSRV_ERROR eError;


	eError =
		HeapCfgHeapCount(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					ui32HeapConfigIndex,
					pui32NumHeaps);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapConfigName(IMG_HANDLE hBridge,
								   IMG_UINT32 ui32HeapConfigIndex,
								   IMG_UINT32 ui32HeapConfigNameBufSz,
								   IMG_CHAR *puiHeapConfigName)
{
	PVRSRV_ERROR eError;


	eError =
		HeapCfgHeapConfigName(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					ui32HeapConfigIndex,
					ui32HeapConfigNameBufSz,
					puiHeapConfigName);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeHeapCfgHeapDetails(IMG_HANDLE hBridge,
								IMG_UINT32 ui32HeapConfigIndex,
								IMG_UINT32 ui32HeapIndex,
								IMG_UINT32 ui32HeapNameBufSz,
								IMG_CHAR *puiHeapNameOut,
								IMG_DEV_VIRTADDR *psDevVAddrBase,
								IMG_DEVMEM_SIZE_T *puiHeapLength,
								IMG_UINT32 *pui32Log2DataPageSizeOut,
								IMG_UINT32 *pui32Log2ImportAlignmentOut)
{
	PVRSRV_ERROR eError;


	eError =
		HeapCfgHeapDetails(NULL, (PVRSRV_DEVICE_NODE *)((void*) hBridge)
		,
					ui32HeapConfigIndex,
					ui32HeapIndex,
					ui32HeapNameBufSz,
					puiHeapNameOut,
					psDevVAddrBase,
					puiHeapLength,
					pui32Log2DataPageSizeOut,
					pui32Log2ImportAlignmentOut);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntRegisterPFNotifyKM(IMG_HANDLE hBridge,
									 IMG_HANDLE hDevmemCtx,
									 IMG_UINT32 ui32PID,
									 IMG_BOOL bRegister)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX * psDevmemCtxInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemCtxInt = (DEVMEMINT_CTX *) hDevmemCtx;

	eError =
		DevmemIntRegisterPFNotifyKM(
					psDevmemCtxInt,
					ui32PID,
					bRegister);

	return eError;
}

