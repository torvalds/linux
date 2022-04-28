/*************************************************************************/ /*!
@File
@Title          RGX memory context management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for RGX memory context management
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

#if !defined(RGXMEM_H)
#define RGXMEM_H

#include "pvrsrv_error.h"
#include "device.h"
#include "mmu_common.h"
#include "rgxdevice.h"

#define RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME 16

/* this PID denotes the firmware */
#define RGXMEM_SERVER_PID_FIRMWARE 0xFFFFFFFF

/* this PID denotes the PM */
#define RGXMEM_SERVER_PID_PM 0xEFFFFFFF

typedef struct _RGXMEM_PROCESS_INFO_
{
	IMG_PID uiPID;
	IMG_CHAR szProcessName[RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME];
	IMG_BOOL bUnregistered;
} RGXMEM_PROCESS_INFO;

typedef struct SERVER_MMU_CONTEXT_TAG SERVER_MMU_CONTEXT;

IMG_DEV_PHYADDR GetPC(MMU_CONTEXT * psContext);

void RGXSetFWMemContextDevVirtAddr(SERVER_MMU_CONTEXT *psServerMMUContext,
			RGXFWIF_DEV_VIRTADDR	sFWMemContextAddr);

void RGXMMUSyncPrimAlloc(PVRSRV_DEVICE_NODE *psDevNode);
void RGXMMUSyncPrimFree(void);

PVRSRV_ERROR RGXSLCFlushRange(PVRSRV_DEVICE_NODE *psDevNode,
							  MMU_CONTEXT *psMMUContext,
							  IMG_DEV_VIRTADDR sDevVAddr,
							  IMG_DEVMEM_SIZE_T uiLength,
							  IMG_BOOL bInvalidate);

PVRSRV_ERROR RGXInvalidateFBSCTable(PVRSRV_DEVICE_NODE *psDeviceNode,
									MMU_CONTEXT *psMMUContext,
									IMG_UINT64 ui64FBSCEntryMask);

PVRSRV_ERROR RGXExtractFBSCEntryMaskFromMMUContext(PVRSRV_DEVICE_NODE *psDeviceNode,
												   SERVER_MMU_CONTEXT *psServerMMUContext,
												   IMG_UINT64 *pui64FBSCEntryMask);

void RGXMMUCacheInvalidate(PVRSRV_DEVICE_NODE *psDevNode,
						   MMU_CONTEXT *psMMUContext,
						   MMU_LEVEL eMMULevel,
						   IMG_BOOL bUnmap);

/*************************************************************************/ /*!
@Function       RGXMMUCacheInvalidateKick

@Description    Sends a flush command to a particular DM but first takes
                the power lock.

@Input          psDevNode   Device Node pointer
@Input          pui32NextMMUInvalidateUpdate

@Return			PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXMMUCacheInvalidateKick(PVRSRV_DEVICE_NODE *psDevNode,
                                       IMG_UINT32 *pui32NextMMUInvalidateUpdate);

/*************************************************************************/ /*!
@Function       RGXPreKickCacheCommand

@Description    Sends a cache flush command to a particular DM without
                honouring the power lock. It's the caller's responsibility
                to ensure power lock is held before calling this function.

@Input          psDevInfo   Device Info
@Input          eDM			To which DM the cmd is sent.
@Input          pui32MMUInvalidateUpdate

@Return			PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR RGXPreKickCacheCommand(PVRSRV_RGXDEV_INFO *psDevInfo,
									RGXFWIF_DM eDM,
									IMG_UINT32 *pui32MMUInvalidateUpdate);

void RGXUnregisterMemoryContext(IMG_HANDLE hPrivData);
PVRSRV_ERROR RGXRegisterMemoryContext(PVRSRV_DEVICE_NODE	*psDevNode,
									  MMU_CONTEXT			*psMMUContext,
									  IMG_HANDLE			*hPrivData);

DEVMEM_MEMDESC *RGXGetFWMemDescFromMemoryContextHandle(IMG_HANDLE hPriv);

void RGXCheckFaultAddress(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_DEV_VIRTADDR *psDevVAddr,
				IMG_DEV_PHYADDR *psDevPAddr,
				MMU_FAULT_DATA *psOutFaultData);

IMG_BOOL RGXPCAddrToProcessInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_PHYADDR sPCAddress,
								RGXMEM_PROCESS_INFO *psInfo);

IMG_BOOL RGXPCPIDToProcessInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_PID uiPID,
                               RGXMEM_PROCESS_INFO *psInfo);

IMG_PID RGXGetPIDFromServerMMUContext(SERVER_MMU_CONTEXT *psServerMMUContext);

#endif /* RGXMEM_H */
