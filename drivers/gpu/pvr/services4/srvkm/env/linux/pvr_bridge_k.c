/*************************************************************************/ /*!
@Title          PVR Bridge Module (kernel side)
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Receives calls from the user portion of services and
                despatches them to functions in the kernel portion.
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

#include "img_defs.h"
#include "services.h"
#include "pvr_bridge.h"
#include "perproc.h"
#include "mutex.h"
#include "syscommon.h"
#include "pvr_debug.h"
#include "proc.h"
#include "private_data.h"
#include "linkage.h"
#include "pvr_bridge_km.h"
#include "pvr_uaccess.h"
#include "refcount.h"
#include "buffer_manager.h"

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#include "pvr_drm.h"
#if defined(PVR_SECURE_DRM_AUTH_EXPORT)
#include "env_perproc.h"
#endif
#endif

/* VGX: */
#if defined(SUPPORT_VGX)
#include "vgx_bridge.h"
#endif

/* SGX: */
#if defined(SUPPORT_SGX)
#include "sgx_bridge.h"
#endif

#include "bridged_pvr_bridge.h"

#if defined(SUPPORT_DRI_DRM)
#define	PRIVATE_DATA(pFile) ((pFile)->driver_priv)
#else
#define	PRIVATE_DATA(pFile) ((pFile)->private_data)
#endif

#if defined(DEBUG_BRIDGE_KM)

static struct proc_dir_entry *g_ProcBridgeStats =0;
static void* ProcSeqNextBridgeStats(struct seq_file *sfile,void* el,loff_t off);
static void ProcSeqShowBridgeStats(struct seq_file *sfile,void* el);
static void* ProcSeqOff2ElementBridgeStats(struct seq_file * sfile, loff_t off);
static void ProcSeqStartstopBridgeStats(struct seq_file *sfile,IMG_BOOL start);

#endif

extern PVRSRV_LINUX_MUTEX gPVRSRVLock;

#if defined(SUPPORT_MEMINFO_IDS)
IMG_UINT64 g_ui64MemInfoID;
#endif /* defined(SUPPORT_MEMINFO_IDS) */

PVRSRV_ERROR
LinuxBridgeInit(IMG_VOID)
{
#if defined(DEBUG_BRIDGE_KM)
	{
		g_ProcBridgeStats = CreateProcReadEntrySeq(
												  "bridge_stats", 
												  NULL,
												  ProcSeqNextBridgeStats,
												  ProcSeqShowBridgeStats,
												  ProcSeqOff2ElementBridgeStats,
												  ProcSeqStartstopBridgeStats
						  						 );
		if(!g_ProcBridgeStats)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif
	return CommonBridgeInit();
}

IMG_VOID
LinuxBridgeDeInit(IMG_VOID)
{
#if defined(DEBUG_BRIDGE_KM)
    RemoveProcEntrySeq(g_ProcBridgeStats);
#endif
}

#if defined(DEBUG_BRIDGE_KM)

/*
 * Lock MMap regions list (called on page start/stop while reading /proc/mmap)
 *
 * sfile : seq_file that handles /proc file
 * start : TRUE if it's start, FALSE if it's stop
 *  
 */
static void ProcSeqStartstopBridgeStats(struct seq_file *sfile,IMG_BOOL start) 
{
	if(start) 
	{
		LinuxLockMutex(&gPVRSRVLock);
	}
	else
	{
		LinuxUnLockMutex(&gPVRSRVLock);
	}
}


/*
 * Convert offset (index from KVOffsetTable) to element 
 * (called when reading /proc/mmap file)

 * sfile : seq_file that handles /proc file
 * off : index into the KVOffsetTable from which to print
 *  
 * returns void* : Pointer to element that will be dumped
 *  
*/
static void* ProcSeqOff2ElementBridgeStats(struct seq_file *sfile, loff_t off)
{
	if(!off) 
	{
		return PVR_PROC_SEQ_START_TOKEN;
	}

	if(off > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		return (void*)0;
	}


	return (void*)&g_BridgeDispatchTable[off-1];
}

/*
 * Gets next MMap element to show. (called when reading /proc/mmap file)

 * sfile : seq_file that handles /proc file
 * el : actual element
 * off : index into the KVOffsetTable from which to print
 *  
 * returns void* : Pointer to element to show (0 ends iteration)
*/
static void* ProcSeqNextBridgeStats(struct seq_file *sfile,void* el,loff_t off)
{
	return ProcSeqOff2ElementBridgeStats(sfile,off);
}


/*
 * Show MMap element (called when reading /proc/mmap file)

 * sfile : seq_file that handles /proc file
 * el : actual element
 *  
*/
static void ProcSeqShowBridgeStats(struct seq_file *sfile,void* el)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psEntry = (	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY*)el;

	if(el == PVR_PROC_SEQ_START_TOKEN) 
	{
		seq_printf(sfile,
						  "Total ioctl call count = %u\n"
						  "Total number of bytes copied via copy_from_user = %u\n"
						  "Total number of bytes copied via copy_to_user = %u\n"
						  "Total number of bytes copied via copy_*_user = %u\n\n"
						  "%-45s | %-40s | %10s | %20s | %10s\n",
						  g_BridgeGlobalStats.ui32IOCTLCount,
						  g_BridgeGlobalStats.ui32TotalCopyFromUserBytes,
						  g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
						  g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
						  "Bridge Name",
						  "Wrapper Function",
						  "Call Count",
						  "copy_from_user Bytes",
						  "copy_to_user Bytes"
						 );
		return;
	}

	seq_printf(sfile,
				   "%-45s   %-40s   %-10u   %-20u   %-10u\n",
				   psEntry->pszIOCName,
				   psEntry->pszFunctionName,
				   psEntry->ui32CallCount,
				   psEntry->ui32CopyFromUserTotalBytes,
				   psEntry->ui32CopyToUserTotalBytes);
}

#endif /* DEBUG_BRIDGE_KM */


#if defined(SUPPORT_DRI_DRM)
int
PVRSRV_BridgeDispatchKM(struct drm_device unref__ *dev, void *arg, struct drm_file *pFile)
#else
long
PVRSRV_BridgeDispatchKM(struct file *pFile, unsigned int unref__ ioctlCmd, unsigned long arg)
#endif
{
	IMG_UINT32 cmd;
#if !defined(SUPPORT_DRI_DRM)
	PVRSRV_BRIDGE_PACKAGE *psBridgePackageUM = (PVRSRV_BRIDGE_PACKAGE *)arg;
	PVRSRV_BRIDGE_PACKAGE sBridgePackageKM;
#endif
	PVRSRV_BRIDGE_PACKAGE *psBridgePackageKM;
	IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();
	PVRSRV_PER_PROCESS_DATA *psPerProc;
	IMG_INT err = -EFAULT;

	LinuxLockMutex(&gPVRSRVLock);

#if defined(SUPPORT_DRI_DRM)
	psBridgePackageKM = (PVRSRV_BRIDGE_PACKAGE *)arg;
	PVR_ASSERT(psBridgePackageKM != IMG_NULL);
#else
	psBridgePackageKM = &sBridgePackageKM;

	if(!OSAccessOK(PVR_VERIFY_WRITE,
				   psBridgePackageUM,
				   sizeof(PVRSRV_BRIDGE_PACKAGE)))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Received invalid pointer to function arguments",
				 __FUNCTION__));

		goto unlock_and_return;
	}
	
	/* FIXME - Currently the CopyFromUserWrapper which collects stats about
	 * how much data is shifted to/from userspace isn't available to us
	 * here. */
	if(OSCopyFromUser(IMG_NULL,
					  psBridgePackageKM,
					  psBridgePackageUM,
					  sizeof(PVRSRV_BRIDGE_PACKAGE))
	  != PVRSRV_OK)
	{
		goto unlock_and_return;
	}
#endif

	cmd = psBridgePackageKM->ui32BridgeID;
	
	if(cmd != PVRSRV_BRIDGE_CONNECT_SERVICES)
	{
		PVRSRV_ERROR eError;

		eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
									(IMG_PVOID *)&psPerProc,
									psBridgePackageKM->hKernelServices,
									PVRSRV_HANDLE_TYPE_PERPROC_DATA);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Invalid kernel services handle (%d)",
					 __FUNCTION__, eError));
			goto unlock_and_return;
		}

		if(psPerProc->ui32PID != ui32PID)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Process %d tried to access data "
					 "belonging to process %d", __FUNCTION__, ui32PID,
					 psPerProc->ui32PID));
			goto unlock_and_return;
		}
	}
	else
	{
		/* lookup per-process data for this process */
		psPerProc = PVRSRVPerProcessData(ui32PID);
		if(psPerProc == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRV_BridgeDispatchKM: "
					 "Couldn't create per-process data area"));
			goto unlock_and_return;
		}
	}

	psBridgePackageKM->ui32BridgeID = PVRSRV_GET_BRIDGE_ID(psBridgePackageKM->ui32BridgeID);

	switch(cmd)
	{
		case PVRSRV_BRIDGE_EXPORT_DEVICEMEM_2:
		{
			PVRSRV_FILE_PRIVATE_DATA *psPrivateData = PRIVATE_DATA(pFile);

			if(psPrivateData->hKernelMemInfo)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Can only export one MemInfo "
						 "per file descriptor", __FUNCTION__));
				err = -EINVAL;
				goto unlock_and_return;
			}
			break;
		}

		case PVRSRV_BRIDGE_MAP_DEV_MEMORY_2:
		{
			PVRSRV_BRIDGE_IN_MAP_DEV_MEMORY *psMapDevMemIN =
				(PVRSRV_BRIDGE_IN_MAP_DEV_MEMORY *)psBridgePackageKM->pvParamIn;
			PVRSRV_FILE_PRIVATE_DATA *psPrivateData = PRIVATE_DATA(pFile);

			if(!psPrivateData->hKernelMemInfo)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: File descriptor has no "
						 "associated MemInfo handle", __FUNCTION__));
				err = -EINVAL;
				goto unlock_and_return;
			}

			if (pvr_put_user(psPrivateData->hKernelMemInfo, &psMapDevMemIN->hKernelMemInfo) != 0)
			{
				err = -EFAULT;
				goto unlock_and_return;
			}
			break;
		}

		default:
		{
			PVRSRV_FILE_PRIVATE_DATA *psPrivateData = PRIVATE_DATA(pFile);

			if(psPrivateData->hKernelMemInfo)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Import/Export handle tried "
						 "to use privileged service", __FUNCTION__));
				goto unlock_and_return;
			}
			break;
		}
	}

#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	switch(cmd)
	{
		case PVRSRV_BRIDGE_MAP_DEV_MEMORY:
		case PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY:
		{
			PVRSRV_FILE_PRIVATE_DATA *psPrivateData;
			int authenticated = pFile->authenticated;
			PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc;

			if (authenticated)
			{
				break;
			}

			/*
			 * The DRM file structure we are using for Services
			 * is not one that DRI authentication was done on.
			 * Look for an authenticated file structure for
			 * this process, making sure the DRM master is the
			 * same as ours.
			 */
			psEnvPerProc = (PVRSRV_ENV_PER_PROCESS_DATA *)PVRSRVProcessPrivateData(psPerProc);
			if (psEnvPerProc == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Process private data not allocated", __FUNCTION__));
				err = -EFAULT;
				goto unlock_and_return;
			}

			list_for_each_entry(psPrivateData, &psEnvPerProc->sDRMAuthListHead, sDRMAuthListItem)
			{
				struct drm_file *psDRMFile = psPrivateData->psDRMFile;

				if (pFile->master == psDRMFile->master)
				{
					authenticated |= psDRMFile->authenticated;
					if (authenticated)
					{
						break;
					}
				}
			}

			if (!authenticated)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Not authenticated for mapping device or device class memory", __FUNCTION__));
				err = -EPERM;
				goto unlock_and_return;
			}
			break;
		}
		default:
			break;
	}
#endif /* defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT) */

	err = BridgedDispatchKM(psPerProc, psBridgePackageKM);
	if(err != PVRSRV_OK)
		goto unlock_and_return;

	switch(cmd)
	{
		case PVRSRV_BRIDGE_EXPORT_DEVICEMEM_2:
		{
			PVRSRV_BRIDGE_OUT_EXPORTDEVICEMEM *psExportDeviceMemOUT =
				(PVRSRV_BRIDGE_OUT_EXPORTDEVICEMEM *)psBridgePackageKM->pvParamOut;
			PVRSRV_FILE_PRIVATE_DATA *psPrivateData = PRIVATE_DATA(pFile);
			IMG_HANDLE hMemInfo;
			PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

			if (pvr_get_user(hMemInfo, &psExportDeviceMemOUT->hMemInfo) != 0)
			{
				err = -EFAULT;
				goto unlock_and_return;
			}

			/* Look up the meminfo we just exported */
			if(PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
								  (IMG_PVOID *)&psKernelMemInfo,
								  hMemInfo,
								  PVRSRV_HANDLE_TYPE_MEM_INFO) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to look up export handle", __FUNCTION__));
				err = -EFAULT;
				goto unlock_and_return;
			}

			/* Bump the refcount; decremented on release of the fd */
			PVRSRVKernelMemInfoIncRef(psKernelMemInfo);

			/* Tell the XProc about the export if required */
			if (psKernelMemInfo->sShareMemWorkaround.bInUse)
			{
				BM_XProcIndexAcquire(psKernelMemInfo->sShareMemWorkaround.ui32ShareIndex);
			}

			psPrivateData->hKernelMemInfo = hMemInfo;
#if defined(SUPPORT_MEMINFO_IDS)
			psPrivateData->ui64Stamp = ++g_ui64MemInfoID;

			psKernelMemInfo->ui64Stamp = psPrivateData->ui64Stamp;
			if (pvr_put_user(psPrivateData->ui64Stamp, &psExportDeviceMemOUT->ui64Stamp) != 0)
			{
				err = -EFAULT;
				goto unlock_and_return;
			}
#endif
			break;
		}

#if defined(SUPPORT_MEMINFO_IDS)
		case PVRSRV_BRIDGE_MAP_DEV_MEMORY:
		case PVRSRV_BRIDGE_MAP_DEV_MEMORY_2:
		{
			PVRSRV_BRIDGE_OUT_MAP_DEV_MEMORY *psMapDeviceMemoryOUT =
				(PVRSRV_BRIDGE_OUT_MAP_DEV_MEMORY *)psBridgePackageKM->pvParamOut;
			PVRSRV_FILE_PRIVATE_DATA *psPrivateData = PRIVATE_DATA(pFile);
			if (pvr_put_user(psPrivateData->ui64Stamp, &psMapDeviceMemoryOUT->sDstClientMemInfo.ui64Stamp) != 0)
			{
				err = -EFAULT;
				goto unlock_and_return;
			}
			break;
		}

		case PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY:
		{
			PVRSRV_BRIDGE_OUT_MAP_DEVICECLASS_MEMORY *psDeviceClassMemoryOUT =
				(PVRSRV_BRIDGE_OUT_MAP_DEVICECLASS_MEMORY *)psBridgePackageKM->pvParamOut;
			if (pvr_put_user(++g_ui64MemInfoID, &psDeviceClassMemoryOUT->sClientMemInfo.ui64Stamp) != 0)
			{
				err = -EFAULT;
				goto unlock_and_return;
			}
			break;
		}
#endif /* defined(SUPPORT_MEMINFO_IDS) */

		default:
			break;
	}

unlock_and_return:
	LinuxUnLockMutex(&gPVRSRVLock);
	return err;
}
