/*************************************************************************/ /*!
@File
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
#include "pvr_bridge.h"
#include "connection_server.h"
#include "syscommon.h"
#include "pvr_debug.h"
#include "pvr_debugfs.h"
#include "private_data.h"
#include "linkage.h"
#include "driverlock.h"

#if defined(SUPPORT_DRM)
#include <drm/drmP.h>
#include "pvr_drm.h"
#if defined(PVR_DRM_SECURE_AUTH_EXPORT)
#include "env_connection.h"
#endif
#endif /* defined(SUPPORT_DRM) */

/* RGX: */
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif

#include "srvcore.h"
#include "common_srvcore_bridge.h"
#include "cache_defines.h"

#if defined(MODULE_TEST)
/************************************************************************/
// additional includes for services testing
/************************************************************************/
#include "pvr_test_bridge.h"
#include "kern_test.h"
/************************************************************************/
// end of additional includes
/************************************************************************/
#endif


#if defined(SUPPORT_DRM)
#define	PRIVATE_DATA(pFile) ((pFile)->driver_priv)
#else
#define	PRIVATE_DATA(pFile) ((pFile)->private_data)
#endif

#if defined(DEBUG_BRIDGE_KM)
static struct dentry *gpsPVRDebugFSBridgeStatsEntry = NULL;
static struct seq_operations gsBridgeStatsReadOps;
#endif

PVRSRV_ERROR RegisterPDUMPFunctions(void);
#if defined(SUPPORT_DISPLAY_CLASS)
PVRSRV_ERROR RegisterDCFunctions(void);
#endif
PVRSRV_ERROR RegisterMMFunctions(void);
PVRSRV_ERROR RegisterCMMFunctions(void);
PVRSRV_ERROR RegisterPDUMPMMFunctions(void);
PVRSRV_ERROR RegisterPDUMPCMMFunctions(void);
PVRSRV_ERROR RegisterSRVCOREFunctions(void);
PVRSRV_ERROR RegisterSYNCFunctions(void);
#if defined(SUPPORT_INSECURE_EXPORT)
PVRSRV_ERROR RegisterSYNCEXPORTFunctions(void);
#endif
#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR RegisterSYNCSEXPORTFunctions(void);
#endif
#if defined (SUPPORT_RGX)
PVRSRV_ERROR RegisterRGXINITFunctions(void);
PVRSRV_ERROR RegisterRGXTA3DFunctions(void);
PVRSRV_ERROR RegisterRGXTQFunctions(void);
PVRSRV_ERROR RegisterRGXCMPFunctions(void);
PVRSRV_ERROR RegisterBREAKPOINTFunctions(void);
PVRSRV_ERROR RegisterDEBUGMISCFunctions(void);
PVRSRV_ERROR RegisterRGXPDUMPFunctions(void);
PVRSRV_ERROR RegisterRGXHWPERFFunctions(void);
#if defined(RGX_FEATURE_RAY_TRACING)
PVRSRV_ERROR RegisterRGXRAYFunctions(void);
#endif /* RGX_FEATURE_RAY_TRACING */
PVRSRV_ERROR RegisterREGCONFIGFunctions(void);
PVRSRV_ERROR RegisterTIMERQUERYFunctions(void);
#endif /* SUPPORT_RGX */
#if (CACHEFLUSH_TYPE == CACHEFLUSH_GENERIC)
PVRSRV_ERROR RegisterCACHEGENERICFunctions(void);
#endif
#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR RegisterSMMFunctions(void);
#endif
#if defined(SUPPORT_PMMIF)
PVRSRV_ERROR RegisterPMMIFFunctions(void);
#endif
PVRSRV_ERROR RegisterPVRTLFunctions(void);
#if defined(PVR_RI_DEBUG)
PVRSRV_ERROR RegisterRIFunctions(void);
#endif
#if defined(SUPPORT_ION)
PVRSRV_ERROR RegisterDMABUFFunctions(void);
#endif

/* These and their friends above will go when full bridge gen comes in */
PVRSRV_ERROR
LinuxBridgeInit(void);
void
LinuxBridgeDeInit(void);

PVRSRV_ERROR
LinuxBridgeInit(void)
{
	PVRSRV_ERROR eError;
#if defined(DEBUG_BRIDGE_KM)
	IMG_INT iResult;

	iResult = PVRDebugFSCreateEntry("bridge_stats",
					NULL,
					&gsBridgeStatsReadOps,
					NULL,
					&g_BridgeDispatchTable[0],
					&gpsPVRDebugFSBridgeStatsEntry);
	if (iResult != 0)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
#endif

	eError = RegisterSRVCOREFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterSYNCFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_INSECURE_EXPORT)
	eError = RegisterSYNCEXPORTFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif
#if defined(SUPPORT_SECURE_EXPORT)
	eError = RegisterSYNCSEXPORTFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = RegisterPDUMPFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	eError = RegisterMMFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	eError = RegisterCMMFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	eError = RegisterPDUMPMMFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	eError = RegisterPDUMPCMMFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(SUPPORT_PMMIF)
	eError = RegisterPMMIFFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(SUPPORT_ION)
	eError = RegisterDMABUFFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
	eError = RegisterDCFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if (CACHEFLUSH_TYPE == CACHEFLUSH_GENERIC)
	eError = RegisterCACHEGENERICFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

#if defined(SUPPORT_SECURE_EXPORT)
	eError = RegisterSMMFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = RegisterPVRTLFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	#if defined(PVR_RI_DEBUG)
	eError = RegisterRIFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	#endif

	#if defined (SUPPORT_RGX)
	eError = RegisterRGXTQFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterRGXCMPFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterRGXINITFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterRGXTA3DFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterBREAKPOINTFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterDEBUGMISCFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
	
	eError = RegisterRGXPDUMPFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterRGXHWPERFFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(RGX_FEATURE_RAY_TRACING)
	eError = RegisterRGXRAYFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif /* RGX_FEATURE_RAY_TRACING */

	eError = RegisterREGCONFIGFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RegisterTIMERQUERYFunctions();
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#endif /* SUPPORT_RGX */

	return eError;
}

void
LinuxBridgeDeInit(void)
{
#if defined(DEBUG_BRIDGE_KM)
	PVRDebugFSRemoveEntry(gpsPVRDebugFSBridgeStatsEntry);
	gpsPVRDebugFSBridgeStatsEntry = NULL;
#endif
}

#if defined(DEBUG_BRIDGE_KM)
static void *BridgeStatsSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psDispatchTable = (PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *)psSeqFile->private;

	OSAcquireBridgeLock();

	if (psDispatchTable == NULL || (*puiPosition) > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		return NULL;
	}

	if ((*puiPosition) == 0) 
	{
		return SEQ_START_TOKEN;
	}

	return &(psDispatchTable[(*puiPosition) - 1]);
}

static void BridgeStatsSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);

	OSReleaseBridgeLock();
}

static void *BridgeStatsSeqNext(struct seq_file *psSeqFile,
			       void *pvData,
			       loff_t *puiPosition)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psDispatchTable = (PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *)psSeqFile->private;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*puiPosition)++;

	if ((*puiPosition) > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		return NULL;
	}

	return &(psDispatchTable[(*puiPosition) - 1]);
}

static int BridgeStatsSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	if (pvData == SEQ_START_TOKEN)
	{
		seq_printf(psSeqFile,
			   "Total ioctl call count = %u\n"
			   "Total number of bytes copied via copy_from_user = %u\n"
			   "Total number of bytes copied via copy_to_user = %u\n"
			   "Total number of bytes copied via copy_*_user = %u\n\n"
			   "%-60s | %-48s | %10s | %20s | %10s\n",
			   g_BridgeGlobalStats.ui32IOCTLCount,
			   g_BridgeGlobalStats.ui32TotalCopyFromUserBytes,
			   g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
			   g_BridgeGlobalStats.ui32TotalCopyFromUserBytes + g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
			   "Bridge Name",
			   "Wrapper Function",
			   "Call Count",
			   "copy_from_user Bytes",
			   "copy_to_user Bytes");
	}
	else if (pvData != NULL)
	{
		PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psEntry = (	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *)pvData;

		seq_printf(psSeqFile,
			   "%-60s   %-48s   %-10u   %-20u   %-10u\n",
			   psEntry->pszIOCName,
			   psEntry->pszFunctionName,
			   psEntry->ui32CallCount,
			   psEntry->ui32CopyFromUserTotalBytes,
			   psEntry->ui32CopyToUserTotalBytes);
	}

	return 0;
}

static struct seq_operations gsBridgeStatsReadOps =
{
	.start = BridgeStatsSeqStart,
	.stop = BridgeStatsSeqStop,
	.next = BridgeStatsSeqNext,
	.show = BridgeStatsSeqShow,
};
#endif /* defined(DEBUG_BRIDGE_KM) */


#if defined(SUPPORT_DRM)
int
PVRSRV_BridgeDispatchKM(struct drm_device *dev, void *arg, struct drm_file *pFile)
#else
long
PVRSRV_BridgeDispatchKM(struct file *pFile, unsigned int unref__ ioctlCmd, unsigned long arg)
#endif
{
#if !defined(SUPPORT_DRM)
	PVRSRV_BRIDGE_PACKAGE *psBridgePackageUM = (PVRSRV_BRIDGE_PACKAGE *)arg;
	PVRSRV_BRIDGE_PACKAGE sBridgePackageKM;
#endif
	PVRSRV_BRIDGE_PACKAGE *psBridgePackageKM;
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(pFile);
	IMG_INT err = -EFAULT;

	OSAcquireBridgeLock();

#if defined(SUPPORT_DRM)
	PVR_UNREFERENCED_PARAMETER(dev);

	psBridgePackageKM = (PVRSRV_BRIDGE_PACKAGE *)arg;
	PVR_ASSERT(psBridgePackageKM != IMG_NULL);
#else
	PVR_UNREFERENCED_PARAMETER(ioctlCmd);

	psBridgePackageKM = &sBridgePackageKM;

	if(!OSAccessOK(PVR_VERIFY_WRITE,
				   psBridgePackageUM,
				   sizeof(PVRSRV_BRIDGE_PACKAGE)))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Received invalid pointer to function arguments",
				 __FUNCTION__));

		goto unlock_and_return;
	}
	
	
	if(OSCopyFromUser(IMG_NULL,
					  psBridgePackageKM,
					  psBridgePackageUM,
					  sizeof(PVRSRV_BRIDGE_PACKAGE))
	  != PVRSRV_OK)
	{
		goto unlock_and_return;
	}
#endif

#if defined(DEBUG_BRIDGE_CALLS)
	{
		IMG_UINT32 mangledID;
		mangledID = psBridgePackageKM->ui32BridgeID;

		psBridgePackageKM->ui32BridgeID = PVRSRV_GET_BRIDGE_ID(psBridgePackageKM->ui32BridgeID);

		PVR_DPF((PVR_DBG_WARNING, "Bridge ID (x%8x) %8u (mangled: x%8x) ", psBridgePackageKM->ui32BridgeID, psBridgePackageKM->ui32BridgeID, mangledID));
	}
#else
		psBridgePackageKM->ui32BridgeID = PVRSRV_GET_BRIDGE_ID(psBridgePackageKM->ui32BridgeID);
#endif

	err = BridgedDispatchKM(psConnection, psBridgePackageKM);

#if !defined(SUPPORT_DRM)
unlock_and_return:
#endif
	OSReleaseBridgeLock();
	return err;
}


#if defined(CONFIG_COMPAT)
#if defined(SUPPORT_DRM)
int
#else
long
#endif
PVRSRV_BridgeCompatDispatchKM(struct file *pFile,
			      unsigned int unref__ ioctlCmd,
			      unsigned long arg)
{
	struct bridge_package_from_32
	{
		IMG_UINT32				bridge_id;			/*!< ioctl/drvesc index */
		IMG_UINT32				size;				/*!< size of structure */
		IMG_UINT32				addr_param_in;		/*!< input data buffer */ 
		IMG_UINT32				in_buffer_size;		/*!< size of input data buffer */
		IMG_UINT32				addr_param_out;		/*!< output data buffer */
		IMG_UINT32				out_buffer_size;	/*!< size of output data buffer */
	};

	IMG_INT err = -EFAULT;
	PVRSRV_BRIDGE_PACKAGE params_for_64;
	struct bridge_package_from_32 params;
 	struct bridge_package_from_32 * const params_addr = &params;
#if !defined(SUPPORT_DRM)
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(pFile);
#else
	struct drm_file *file_priv = pFile->private_data;
	CONNECTION_DATA *psConnection = LinuxConnectionFromFile(file_priv);
#endif
	// make sure there is no padding inserted by compiler
	PVR_ASSERT(sizeof(struct bridge_package_from_32) == 6 * sizeof(IMG_UINT32));

	OSAcquireBridgeLock();

	if(!OSAccessOK(PVR_VERIFY_READ, (void *) arg,
				   sizeof(struct bridge_package_from_32)))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Received invalid pointer to function arguments",
				 __FUNCTION__));

		goto unlock_and_return;
	}
	
	if(OSCopyFromUser(NULL, params_addr, (void*) arg,
					  sizeof(struct bridge_package_from_32))
	   != PVRSRV_OK)
	{
		goto unlock_and_return;
	}

	PVR_ASSERT(params_addr->size == sizeof(struct bridge_package_from_32));

	params_addr->bridge_id = PVRSRV_GET_BRIDGE_ID(params_addr->bridge_id);

#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF((PVR_DBG_MESSAGE, "ioctl %s -> func %s",
		g_BridgeDispatchTable[params_addr->bridge_id].pszIOCName,
		g_BridgeDispatchTable[params_addr->bridge_id].pszFunctionName));
#endif

	params_for_64.ui32BridgeID = params_addr->bridge_id;
	params_for_64.ui32Size = sizeof(params_for_64);
	params_for_64.pvParamIn = (void*) ((size_t) params_addr->addr_param_in);
	params_for_64.pvParamOut = (void*) ((size_t) params_addr->addr_param_out);
	params_for_64.ui32InBufferSize = params_addr->in_buffer_size;
	params_for_64.ui32OutBufferSize = params_addr->out_buffer_size;

	err = BridgedDispatchKM(psConnection, &params_for_64);
	
unlock_and_return:
	OSReleaseBridgeLock();
	return err;
}
#endif /* defined(CONFIG_COMPAT) */
