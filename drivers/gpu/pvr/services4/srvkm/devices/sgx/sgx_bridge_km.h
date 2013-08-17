/*************************************************************************/ /*!
@Title          SGX Bridge Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the SGX Bridge code
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

#if !defined(__SGX_BRIDGE_KM_H__)
#define __SGX_BRIDGE_KM_H__

#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "sgx_bridge.h"
#include "pvr_bridge.h"
#include "perproc.h"

#if defined (__cplusplus)
extern "C" {
#endif

IMG_IMPORT
PVRSRV_ERROR SGXSubmitTransferKM(IMG_HANDLE hDevHandle, PVRSRV_TRANSFER_SGX_KICK *psKick);

#if defined(SGX_FEATURE_2D_HARDWARE)
IMG_IMPORT
PVRSRV_ERROR SGXSubmit2DKM(IMG_HANDLE hDevHandle, PVRSRV_2D_SGX_KICK *psKick);
#endif

IMG_IMPORT
PVRSRV_ERROR SGXDoKickKM(IMG_HANDLE hDevHandle,
						 SGX_CCB_KICK *psCCBKick);

IMG_IMPORT
PVRSRV_ERROR SGXGetPhysPageAddrKM(IMG_HANDLE hDevMemHeap,
								  IMG_DEV_VIRTADDR sDevVAddr,
								  IMG_DEV_PHYADDR *pDevPAddr,
								  IMG_CPU_PHYADDR *pCpuPAddr);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV SGXGetMMUPDAddrKM(IMG_HANDLE		hDevCookie,
											IMG_HANDLE		hDevMemContext,
											IMG_DEV_PHYADDR	*psPDDevPAddr);

IMG_IMPORT
PVRSRV_ERROR SGXGetClientInfoKM(IMG_HANDLE				hDevCookie,
								SGX_CLIENT_INFO*	psClientInfo);

IMG_IMPORT
PVRSRV_ERROR SGXGetMiscInfoKM(PVRSRV_SGXDEV_INFO	*psDevInfo,
							  SGX_MISC_INFO			*psMiscInfo,
							  PVRSRV_DEVICE_NODE 	*psDeviceNode,
							  IMG_HANDLE 			 hDevMemContext);

IMG_IMPORT
PVRSRV_ERROR SGXReadHWPerfCBKM(IMG_HANDLE					hDevHandle,
							   IMG_UINT32					ui32ArraySize,
							   PVRSRV_SGX_HWPERF_CB_ENTRY	*psHWPerfCBData,
							   IMG_UINT32					*pui32DataCount,
							   IMG_UINT32					*pui32ClockSpeed,
							   IMG_UINT32					*pui32HostTimeStamp);

IMG_IMPORT
PVRSRV_ERROR SGX2DQueryBlitsCompleteKM(PVRSRV_SGXDEV_INFO		*psDevInfo,
									   PVRSRV_KERNEL_SYNC_INFO	*psSyncInfo,
									   IMG_BOOL bWaitForComplete);

IMG_IMPORT
PVRSRV_ERROR SGXGetInfoForSrvinitKM(IMG_HANDLE hDevHandle,
									SGX_BRIDGE_INFO_FOR_SRVINIT *psInitInfo);

IMG_IMPORT
PVRSRV_ERROR DevInitSGXPart2KM(PVRSRV_PER_PROCESS_DATA *psPerProc,
							   IMG_HANDLE hDevHandle,
							   SGX_BRIDGE_INIT_INFO *psInitInfo);

/*!
 * *****************************************************************************
 * @brief Looks for a parameter buffer description that corresponds to
 *        a buffer of size ui32TotalPBSize, optionally taking the lock
 *        needed for SharedPBCreation on failure.
 *
 *        Note if a PB Desc is found then its internal reference counter
 *        is automatically incremented. It is your responsability to call
 *        SGXUnrefSharedPBDesc to decrement this reference and free associated
 *        resources when you are done.
 *
 * 	  If bLockOnFailure is set, and a suitable shared PB isn't found,
 * 	  an internal flag is set, allowing this process to create a
 * 	  shared PB.  Any other process calling this function with
 * 	  bLockOnFailure set, will receive the return code
 * 	  PVRSRV_ERROR_PROCESSING_BLOCKED, indicating that it needs
 * 	  to retry the function call.  The internal flag is cleared
 * 	  when this process creates a shared PB.
 *
 *	  Note: You are responsible for freeing the list returned in
 *	  pppsSharedPBDescSubKernelMemInfos
 *	  via OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
 *					sizeof(PVRSRV_KERNEL_MEM_INFO *)
 *					  * ui32SharedPBDescSubKernelMemInfosCount,
 *					ppsSharedPBDescSubKernelMemInfos,
 *					NULL);
 *
 * @param[in] psPerProc
 * @param[in] hDevCookie
 * @param[in] bLockOnError
 * @param[in] ui32TotalPBSize
 * @param[in] phSharedPBDesc
 * @param[out] ppsSharedPBDescKernelMemInfo
 * @param[out] ppsHWPBDescKernelMemInfo
 * @param[out] pppsSharedPBDescSubKernelMemInfos  A list of integral sub meminfos.
 * @param[out] ui32SharedPBDescSubKernelMemInfosCount
 *
 * @return PVRSRV_ERROR
 ********************************************************************************/
/* disable QAC pointer level check for over 2 */
/* PRQA S 5102++ */
IMG_IMPORT PVRSRV_ERROR
SGXFindSharedPBDescKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
					  IMG_HANDLE				hDevCookie,
					  IMG_BOOL				bLockOnFailure,
					  IMG_UINT32				ui32TotalPBSize,
					  IMG_HANDLE				*phSharedPBDesc,
					  PVRSRV_KERNEL_MEM_INFO	**ppsSharedPBDescKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO	**ppsHWPBDescKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO	**ppsBlockKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO	**ppsHWBlockKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO	***pppsSharedPBDescSubKernelMemInfos,
					  IMG_UINT32				*ui32SharedPBDescSubKernelMemInfosCount);

/*!
 * *****************************************************************************
 * @brief Decrements the reference counter and frees all userspace resources
 *        associated with a SharedPBDesc.
 *
 * @param hSharedPBDesc
 *
 * @return PVRSRV_ERROR
 ********************************************************************************/
IMG_IMPORT PVRSRV_ERROR
SGXUnrefSharedPBDescKM(IMG_HANDLE hSharedPBDesc);

/*!
 * *****************************************************************************
 * @brief Links a new SharedPBDesc into a kernel managed list that can
 *        then be queried by other clients.
 *
 *        As a side affect this function also dissociates the SharedPBDesc
 *        from the calling process so that the memory won't be freed if the
 *        process dies/exits. (The kernel assumes responsability over the
 *        memory at the same time)
 *
 *        As well as the psSharedPBDescKernelMemInfo you must also pass
 *        a complete list of other meminfos that are integral to the
 *        shared PB description. (Although the kernel doesn't have direct
 *        access to the shared PB desc it still needs to be able to
 *        clean up all the associated resources when it is no longer
 *        in use.)
 *
 *        If the dissociation fails then all the memory associated with
 *	  the psSharedPBDescKernelMemInfo and all entries in psKernelMemInfos
 *	  will be freed by kernel services! Because of this, you are
 *	  responsible for freeing the corresponding client meminfos _before_
 *	  calling SGXAddSharedPBDescKM.
 *
 * 	  This function will return an error unless a succesful call to
 * 	  SGXFindSharedPBDesc, with bLockOnFailure set, has been made.
 *
 * @param psPerProc
 * @param hDevCookie
 * @param psSharedPBDescKernelMemInfo
 * @param psHWPBDescKernelMemInfo
 * @param psBlockKernelMemInfo
 * @param ui32TotalPBSize  The size of the associated parameter buffer
 * @param ppsSharedPBDescSubKernelMemInfos  A list of other meminfos integral to
 * 										    the shared PB description.
 * @param ui32SharedPBDescSubKernelMemInfosCount  The number of entires in
 * 												  psKernelMemInfos
 * @param sHWPBDescDevVAddr The device virtual address of the HWPBDesc
 *
 * @return PVRSRV_ERROR
 ********************************************************************************/
IMG_IMPORT PVRSRV_ERROR
SGXAddSharedPBDescKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
					 IMG_HANDLE 				hDevCookie,
					 PVRSRV_KERNEL_MEM_INFO		*psSharedPBDescKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psHWPBDescKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psBlockKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psHWBlockKernelMemInfo,
					 IMG_UINT32					ui32TotalPBSize,
					 IMG_HANDLE					*phSharedPBDesc,
					 PVRSRV_KERNEL_MEM_INFO		**psSharedPBDescSubKernelMemInfos,
					 IMG_UINT32					ui32SharedPBDescSubKernelMemInfosCount,
					 IMG_DEV_VIRTADDR			sHWPBDescDevVAddr);


/*!
 * *****************************************************************************
 * @brief Gets device information that is not intended to be passed
		  on beyond the srvclient libs.
 *
 * @param[in] hDevCookie
 * @param[out] psSGXInternalDevInfo
 *
 * @return
 ********************************************************************************/
IMG_IMPORT PVRSRV_ERROR
SGXGetInternalDevInfoKM(IMG_HANDLE hDevCookie,
						SGX_INTERNAL_DEVINFO *psSGXInternalDevInfo);

#if defined (__cplusplus)
}
#endif

#endif /* __SGX_BRIDGE_KM_H__ */

/******************************************************************************
 End of file (sgx_bridge_km.h)
******************************************************************************/
