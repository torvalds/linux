/*************************************************************************/ /*!
@Title          PVR Bridge Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the PVR Bridge code
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

#ifndef __PVR_BRIDGE_KM_H_
#define __PVR_BRIDGE_KM_H_

#if defined (__cplusplus)
extern "C" {
#endif

#include "pvr_bridge.h"
#include "perproc.h"

/******************************************************************************
 * Function prototypes
 *****************************************************************************/
#if defined(__linux__)
PVRSRV_ERROR LinuxBridgeInit(IMG_VOID);
IMG_VOID LinuxBridgeDeInit(IMG_VOID);

#if defined(SUPPORT_MEMINFO_IDS)
extern IMG_UINT64 g_ui64MemInfoID;
#endif

#endif

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevicesKM(IMG_UINT32 *pui32NumDevices,
												   PVRSRV_DEVICE_IDENTIFIER *psDevIdList);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceDataKM(IMG_UINT32			uiDevIndex,
													PVRSRV_DEVICE_TYPE	eDeviceType,
													IMG_HANDLE			*phDevCookie);
							
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateCommandQueueKM(IMG_SIZE_T uQueueSize,
													 PVRSRV_QUEUE_INFO **ppsQueueInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyCommandQueueKM(PVRSRV_QUEUE_INFO *psQueueInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapsKM(IMG_HANDLE hDevCookie,
													PVRSRV_HEAP_INFO *psHeapInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDeviceMemContextKM(IMG_HANDLE					hDevCookie,
														 PVRSRV_PER_PROCESS_DATA	*psPerProc,
														 IMG_HANDLE					*phDevMemContext,
														 IMG_UINT32					*pui32ClientHeapCount,
														 PVRSRV_HEAP_INFO			*psHeapInfo,
														 IMG_BOOL					*pbCreated,
														 IMG_BOOL					*pbShared);


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDeviceMemContextKM(IMG_HANDLE hDevCookie,
														  IMG_HANDLE hDevMemContext,
														  IMG_BOOL *pbDestroyed);


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapInfoKM(IMG_HANDLE				hDevCookie,
															IMG_HANDLE			hDevMemContext,
															IMG_UINT32			*pui32ClientHeapCount,
															PVRSRV_HEAP_INFO	*psHeapInfo,
															IMG_BOOL 			*pbShared
					);


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV _PVRSRVAllocDeviceMemKM(IMG_HANDLE					hDevCookie,
												 PVRSRV_PER_PROCESS_DATA	*psPerProc,
												 IMG_HANDLE					hDevMemHeap,
												 IMG_UINT32					ui32Flags,
												 IMG_SIZE_T					ui32Size,
												 IMG_SIZE_T					ui32Alignment,
												 IMG_PVOID					pvPrivData,
												 IMG_UINT32					ui32PrivDataLength,
												 IMG_UINT32					ui32ChunkSize,
												 IMG_UINT32					ui32NumVirtChunks,
												 IMG_UINT32					ui32NumPhysChunks,
												 IMG_BOOL					*pabMapChunk,
												 PVRSRV_KERNEL_MEM_INFO		**ppsMemInfo);


#if defined(PVRSRV_LOG_MEMORY_ALLOCS)
	#define PVRSRVAllocDeviceMemKM(devCookie, perProc, devMemHeap, flags, size, alignment, privdata, privdatalength, \
								   chunksize, numvirtchunks, numphyschunks, mapchunk, memInfo, logStr) \
		(PVR_TRACE(("PVRSRVAllocDeviceMemKM(" #devCookie ", " #perProc ", " #devMemHeap ", " #flags ", " #size \
			", " #alignment "," #memInfo "): " logStr " (size = 0x%x)", size)),\
			_PVRSRVAllocDeviceMemKM(devCookie, perProc, devMemHeap, flags, size, alignment, privdata, privdatalength, \
									chunksize, numvirtchunks, numphyschunks, mapchunk, memInfo))
#else
	#define PVRSRVAllocDeviceMemKM(devCookie, perProc, devMemHeap, flags, size, alignment, privdata, privdatalength, \
								   chunksize, numvirtchunks, numphyschunks, mapchunk, memInfo, logStr) \
			_PVRSRVAllocDeviceMemKM(devCookie, perProc, devMemHeap, flags, size, alignment, privdata, privdatalength, \
									chunksize, numvirtchunks, numphyschunks, mapchunk, memInfo)
#endif


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceMemKM(IMG_HANDLE			hDevCookie,
												PVRSRV_KERNEL_MEM_INFO	*psMemInfo);

#if defined(SUPPORT_ION)
IMG_IMPORT
PVRSRV_ERROR PVRSRVMapIonHandleKM(PVRSRV_PER_PROCESS_DATA *psPerProc,
								  IMG_HANDLE hDevCookie,
								  IMG_HANDLE hDevMemHeap,
								  IMG_HANDLE hIon,
								  IMG_UINT32 ui32Flags,
								  IMG_UINT32 ui32ChunkCount,
								  IMG_SIZE_T *pauiOffset,
								  IMG_SIZE_T *pauiSize,
								  IMG_SIZE_T *puiIonBufferSize,
								  PVRSRV_KERNEL_MEM_INFO **ppsKernelMemInfo,
								  IMG_UINT64 *pui64Stamp);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapIonHandleKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo);
#endif /* SUPPORT_ION */

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDissociateDeviceMemKM(IMG_HANDLE			hDevCookie,
												PVRSRV_KERNEL_MEM_INFO	*psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVReserveDeviceVirtualMemKM(IMG_HANDLE		hDevMemHeap,
														 IMG_DEV_VIRTADDR	*psDevVAddr,
														 IMG_SIZE_T			ui32Size,
														 IMG_SIZE_T			ui32Alignment,
														 PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceVirtualMemKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
												  PVRSRV_KERNEL_MEM_INFO	*psSrcMemInfo,
												  IMG_HANDLE				hDstDevMemHeap,
												  PVRSRV_KERNEL_MEM_INFO	**ppsDstMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceMemoryKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWrapExtMemoryKM(IMG_HANDLE				hDevCookie,
												PVRSRV_PER_PROCESS_DATA	*psPerProc,
												IMG_HANDLE				hDevMemContext,
												IMG_SIZE_T 				ui32ByteSize, 
												IMG_SIZE_T				ui32PageOffset,
												IMG_BOOL				bPhysContig,
												IMG_SYS_PHYADDR	 		*psSysAddr,
												IMG_VOID 				*pvLinAddr,
												IMG_UINT32				ui32Flags,
												PVRSRV_KERNEL_MEM_INFO **ppsMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnwrapExtMemoryKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR PVRSRVEnumerateDCKM(PVRSRV_DEVICE_CLASS DeviceClass,
								 IMG_UINT32 *pui32DevCount,
								 IMG_UINT32 *pui32DevID );

IMG_IMPORT
PVRSRV_ERROR PVRSRVOpenDCDeviceKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
								  IMG_UINT32				ui32DeviceID,
								  IMG_HANDLE 				hDevCookie,
								  IMG_HANDLE 				*phDeviceKM);

IMG_IMPORT
PVRSRV_ERROR PVRSRVCloseDCDeviceKM(IMG_HANDLE hDeviceKM);

IMG_IMPORT
PVRSRV_ERROR PVRSRVEnumDCFormatsKM(IMG_HANDLE hDeviceKM,
								   IMG_UINT32 *pui32Count,
								   DISPLAY_FORMAT *psFormat);

IMG_IMPORT
PVRSRV_ERROR PVRSRVEnumDCDimsKM(IMG_HANDLE hDeviceKM,
								DISPLAY_FORMAT *psFormat,
								IMG_UINT32 *pui32Count,
								DISPLAY_DIMS *psDim);

IMG_IMPORT
PVRSRV_ERROR PVRSRVGetDCSystemBufferKM(IMG_HANDLE hDeviceKM,
									   IMG_HANDLE *phBuffer);

IMG_IMPORT
PVRSRV_ERROR PVRSRVGetDCInfoKM(IMG_HANDLE hDeviceKM,
							   DISPLAY_INFO *psDisplayInfo);
IMG_IMPORT
PVRSRV_ERROR PVRSRVCreateDCSwapChainKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
									   IMG_HANDLE				hDeviceKM,
									   IMG_UINT32				ui32Flags,
									   DISPLAY_SURF_ATTRIBUTES	*psDstSurfAttrib,
									   DISPLAY_SURF_ATTRIBUTES	*psSrcSurfAttrib,
									   IMG_UINT32				ui32BufferCount,
									   IMG_UINT32				ui32OEMFlags,
									   IMG_HANDLE				*phSwapChain,
									   IMG_UINT32				*pui32SwapChainID);
IMG_IMPORT
PVRSRV_ERROR PVRSRVDestroyDCSwapChainKM(IMG_HANDLE	hSwapChain);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSetDCDstRectKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChain,
								  IMG_RECT	*psRect);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSetDCSrcRectKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChain,
								  IMG_RECT	*psRect);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSetDCDstColourKeyKM(IMG_HANDLE	hDeviceKM,
									   IMG_HANDLE	hSwapChain,
									   IMG_UINT32	ui32CKColour);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSetDCSrcColourKeyKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE		hSwapChain,
									IMG_UINT32		ui32CKColour);
IMG_IMPORT
PVRSRV_ERROR PVRSRVGetDCBuffersKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChain,
								  IMG_UINT32	*pui32BufferCount,
								  IMG_HANDLE	*phBuffer,
								  IMG_SYS_PHYADDR *psPhyAddr);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSwapToDCBufferKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE	hBuffer,
									IMG_UINT32	ui32SwapInterval,
									IMG_HANDLE	hPrivateTag,
									IMG_UINT32	ui32ClipRectCount,
									IMG_RECT	*psClipRect);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSwapToDCBuffer2KM(IMG_HANDLE	hDeviceKM,
									 IMG_HANDLE	hBuffer,
									 IMG_UINT32	ui32SwapInterval,
									 PVRSRV_KERNEL_MEM_INFO **ppsMemInfos,
									 PVRSRV_KERNEL_SYNC_INFO **ppsSyncInfos,
									 IMG_UINT32	ui32NumMemSyncInfos,
									 IMG_PVOID	pvPrivData,
									 IMG_UINT32	ui32PrivDataLength);
IMG_IMPORT
PVRSRV_ERROR PVRSRVSwapToDCSystemKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE	hSwapChain);

IMG_IMPORT
PVRSRV_ERROR PVRSRVOpenBCDeviceKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
								  IMG_UINT32				ui32DeviceID,
								  IMG_HANDLE				hDevCookie,
								  IMG_HANDLE				*phDeviceKM);
IMG_IMPORT
PVRSRV_ERROR PVRSRVCloseBCDeviceKM(IMG_HANDLE hDeviceKM);

IMG_IMPORT
PVRSRV_ERROR PVRSRVGetBCInfoKM(IMG_HANDLE	hDeviceKM,
							   BUFFER_INFO	*psBufferInfo);
IMG_IMPORT
PVRSRV_ERROR PVRSRVGetBCBufferKM(IMG_HANDLE	hDeviceKM,
								 IMG_UINT32	ui32BufferIndex,
								 IMG_HANDLE	*phBuffer);


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceClassMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
													   IMG_HANDLE				hDevMemContext,
													   IMG_HANDLE				hDeviceClassBuffer,
													   PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo,
													   IMG_HANDLE				*phOSMapInfo);

IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVChangeDeviceMemoryAttributesKM(IMG_HANDLE hKernelMemInfo,
															   IMG_UINT32 ui32Attribs);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceClassMemoryKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetFreeDeviceMemKM(IMG_UINT32 ui32Flags,
												   IMG_SIZE_T *pui32Total,
												   IMG_SIZE_T *pui32Free,
												   IMG_SIZE_T *pui32LargestBlock);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocSyncInfoKM(IMG_HANDLE					hDevCookie,
												IMG_HANDLE					hDevMemContext,
												PVRSRV_KERNEL_SYNC_INFO	**ppsKernelSyncInfo);
IMG_IMPORT
IMG_VOID IMG_CALLCONV PVRSRVAcquireSyncInfoKM(PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo);
IMG_IMPORT
IMG_VOID IMG_CALLCONV PVRSRVReleaseSyncInfoKM(PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetMiscInfoKM(PVRSRV_MISC_INFO *psMiscInfo);

/*!
 * *****************************************************************************
 * @brief Allocates memory on behalf of a userspace process that is addressable
 *        by ther kernel. The memory is suitable for mapping into
 *        user space and it is possible to entirely dissociate the memory
 *        from the userspace process via PVRSRVDissociateSharedSysMemoryKM.
 *
 * @param psPerProc
 * @param ui32Flags
 * @param ui32Size
 * @param ppsKernelMemInfo
 *
 * @return PVRSRV_ERROR
 ********************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVAllocSharedSysMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
							 IMG_UINT32 				ui32Flags,
							 IMG_SIZE_T 				ui32Size,
							 PVRSRV_KERNEL_MEM_INFO		**ppsKernelMemInfo);

/*!
 * *****************************************************************************
 * @brief Frees memory allocated via PVRSRVAllocSharedSysMemoryKM (Note you must
 *        be sure any additional kernel references you created have been
 *        removed before freeing the memory)
 *
 * @param psKernelMemInfo
 *
 * @return PVRSRV_ERROR
 ********************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVFreeSharedSysMemoryKM(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);

/*!
******************************************************************************

 @brief	Dissociates memory from the process that allocates it.  Intended for
 transfering the ownership of system memory from a particular process
 to the kernel.  Unlike PVRSRVDissociateDeviceMemKM, ownership is not
 transfered to the kernel context, so the Resource Manager will not
 automatically clean up such memory.

 @param	   psKernelMemInfo:

 @return   PVRSRV_ERROR:
******************************************************************************/
IMG_IMPORT PVRSRV_ERROR
PVRSRVDissociateMemFromResmanKM(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo);

#if defined (__cplusplus)
}
#endif

#endif /* __PVR_BRIDGE_KM_H_ */

/******************************************************************************
 End of file (pvr_bridge_km.h)
******************************************************************************/
