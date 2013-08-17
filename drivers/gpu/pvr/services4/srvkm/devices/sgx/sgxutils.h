/*************************************************************************/ /*!
@Title          Device specific utility routines declarations
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Inline functions/structures specific to SGX
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

#include "perproc.h"
#include "sgxinfokm.h"

/* PRQA S 3410 7 */ /* macros require the absence of some brackets */
#define CCB_OFFSET_IS_VALID(type, psCCBMemInfo, psCCBKick, offset) \
	((sizeof(type) <= (psCCBMemInfo)->uAllocSize) && \
	((psCCBKick)->offset <= (psCCBMemInfo)->uAllocSize - sizeof(type)))

#define	CCB_DATA_FROM_OFFSET(type, psCCBMemInfo, psCCBKick, offset) \
	((type *)(((IMG_CHAR *)(psCCBMemInfo)->pvLinAddrKM) + \
		(psCCBKick)->offset))

extern IMG_UINT64 ui64KickCount;


IMG_IMPORT
IMG_VOID SGXTestActivePowerEvent(PVRSRV_DEVICE_NODE	*psDeviceNode,
								 IMG_UINT32			ui32CallerID);

IMG_IMPORT
PVRSRV_ERROR SGXScheduleCCBCommand(PVRSRV_DEVICE_NODE	*psDeviceNode,
								   SGXMKIF_CMD_TYPE		eCommandType,
								   SGXMKIF_COMMAND		*psCommandData,
								   IMG_UINT32			ui32CallerID,
								   IMG_UINT32			ui32PDumpFlags,
								   IMG_HANDLE			hDevMemContext,
								   IMG_BOOL				bLastInScene);
IMG_IMPORT
PVRSRV_ERROR SGXScheduleCCBCommandKM(PVRSRV_DEVICE_NODE		*psDeviceNode,
									 SGXMKIF_CMD_TYPE		eCommandType,
									 SGXMKIF_COMMAND		*psCommandData,
									 IMG_UINT32				ui32CallerID,
									 IMG_UINT32				ui32PDumpFlags,
									 IMG_HANDLE				hDevMemContext,
									 IMG_BOOL				bLastInScene);

IMG_IMPORT
PVRSRV_ERROR SGXScheduleProcessQueuesKM(PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_IMPORT
IMG_BOOL SGXIsDevicePowered(PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_IMPORT
IMG_HANDLE SGXRegisterHWRenderContextKM(IMG_HANDLE				psDeviceNode,
                                        IMG_CPU_VIRTADDR        *psHWRenderContextCpuVAddr,
                                        IMG_UINT32              ui32HWRenderContextSize,
                                        IMG_UINT32              ui32OffsetToPDDevPAddr,
                                        IMG_HANDLE              hDevMemContext,
                                        IMG_DEV_VIRTADDR        *psHWRenderContextDevVAddr,
										PVRSRV_PER_PROCESS_DATA *psPerProc);

IMG_IMPORT
IMG_HANDLE SGXRegisterHWTransferContextKM(IMG_HANDLE		      psDeviceNode,
                                          IMG_CPU_VIRTADDR        *psHWTransferContextCpuVAddr,
                                          IMG_UINT32              ui32HWTransferContextSize,
                                          IMG_UINT32              ui32OffsetToPDDevPAddr,
                                          IMG_HANDLE              hDevMemContext,
                                          IMG_DEV_VIRTADDR        *psHWTransferContextDevVAddr,
										  PVRSRV_PER_PROCESS_DATA *psPerProc);

IMG_IMPORT
PVRSRV_ERROR SGXFlushHWRenderTargetKM(IMG_HANDLE psSGXDevInfo,
									  IMG_DEV_VIRTADDR psHWRTDataSetDevVAddr,
									  IMG_BOOL bForceCleanup);

IMG_IMPORT
PVRSRV_ERROR SGXUnregisterHWRenderContextKM(IMG_HANDLE hHWRenderContext, IMG_BOOL bForceCleanup);

IMG_IMPORT
PVRSRV_ERROR SGXUnregisterHWTransferContextKM(IMG_HANDLE hHWTransferContext, IMG_BOOL bForceCleanup);

IMG_IMPORT
PVRSRV_ERROR SGXSetRenderContextPriorityKM(IMG_HANDLE       hDeviceNode,
                                           IMG_HANDLE       hHWRenderContext,
                                           IMG_UINT32       ui32Priority,
                                           IMG_UINT32       ui32OffsetOfPriorityField);

IMG_IMPORT
PVRSRV_ERROR SGXSetTransferContextPriorityKM(IMG_HANDLE       hDeviceNode,
                                             IMG_HANDLE       hHWTransferContext,
                                             IMG_UINT32       ui32Priority,
                                             IMG_UINT32       ui32OffsetOfPriorityField);

#if defined(SGX_FEATURE_2D_HARDWARE)
IMG_IMPORT
IMG_HANDLE SGXRegisterHW2DContextKM(IMG_HANDLE				psDeviceNode,
                                    IMG_CPU_VIRTADDR        *psHW2DContextCpuVAddr,
                                    IMG_UINT32              ui32HW2DContextSize,
                                    IMG_UINT32              ui32OffsetToPDDevPAddr,
                                    IMG_HANDLE              hDevMemContext,
                                    IMG_DEV_VIRTADDR        *psHW2DContextDevVAddr,
									PVRSRV_PER_PROCESS_DATA *psPerProc);

IMG_IMPORT
PVRSRV_ERROR SGXUnregisterHW2DContextKM(IMG_HANDLE hHW2DContext, IMG_BOOL bForceCleanup);
#endif

IMG_UINT32 SGXConvertTimeStamp(PVRSRV_SGXDEV_INFO	*psDevInfo,
							   IMG_UINT32			ui32TimeWraps,
							   IMG_UINT32			ui32Time);

/*!
*******************************************************************************

 @Function	SGXWaitClocks

 @Description

 Wait for a specified number of SGX clock cycles to elapse.

 @Input psDevInfo - SGX Device Info
 @Input ui32SGXClocks - number of clock cycles to wait

 @Return   IMG_VOID

******************************************************************************/
IMG_VOID SGXWaitClocks(PVRSRV_SGXDEV_INFO	*psDevInfo,
					   IMG_UINT32			ui32SGXClocks);

PVRSRV_ERROR SGXCleanupRequest(PVRSRV_DEVICE_NODE	*psDeviceNode,
							IMG_DEV_VIRTADDR	*psHWDataDevVAddr,
							IMG_UINT32			ui32CleanupType,
							IMG_BOOL			bForceCleanup);

IMG_IMPORT
PVRSRV_ERROR PVRSRVGetSGXRevDataKM(PVRSRV_DEVICE_NODE* psDeviceNode, IMG_UINT32 *pui32SGXCoreRev,
				IMG_UINT32 *pui32SGXCoreID);

/*!
******************************************************************************

 @Function	SGXContextSuspend

 @Description - Interface to the SGX microkernel to instruct it to suspend or
				resume processing on a given context. This will interrupt current
				processing of this context if a task is already running and is
				interruptable.

 @Input psDeviceNode			SGX device node
 @Input psHWContextDevVAddr		SGX virtual address of the context to be suspended
								or resumed. Can be of type SGXMKIF_HWRENDERCONTEXT,
								SGXMKIF_HWTRANSFERCONTEXT or SGXMKIF_HW2DCONTEXT
 @Input bResume					IMG_TRUE to put a context into suspend state,
								IMG_FALSE to resume a previously suspended context

******************************************************************************/
PVRSRV_ERROR SGXContextSuspend(PVRSRV_DEVICE_NODE	*psDeviceNode,
							   IMG_DEV_VIRTADDR		*psHWContextDevVAddr,
							   IMG_BOOL				bResume);

/******************************************************************************
 End of file (sgxutils.h)
******************************************************************************/
