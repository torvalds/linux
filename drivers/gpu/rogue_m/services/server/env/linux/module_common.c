/*************************************************************************/ /*!
@File
@Title          Common linux module setup
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

#include <linux/version.h>

#if (!defined(LDM_PLATFORM) && !defined(LDM_PCI)) || \
	(defined(LDM_PLATFORM) && defined(LDM_PCI))
	#error "LDM_PLATFORM or LDM_PCI must be defined"
#endif

#include <linux/module.h>

#include "pvr_debugfs.h"
#include "private_data.h"
#include "linkage.h"
#include "power.h"
#include "env_connection.h"
#include "process_stats.h"
#include "module_common.h"
#include "pvrsrv.h"

#if defined(SUPPORT_DRM)
#include "pvr_drm.h"
#endif

#if defined(SUPPORT_AUTH)
#include "osauth.h"
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#endif

#if defined(SUPPORT_GPUTRACE_EVENTS)
#include "pvr_gputrace.h"
#endif

#if defined(SUPPORT_KERNEL_HWPERF) || defined(SUPPORT_SHARED_SLC)
#include "rgxapi_km.h"
#endif

#if defined(SUPPORT_KERNEL_SRVINIT)
#include "srvinit.h"
#endif

#if defined(PVRSRV_NEED_PVR_DPF) || defined(DEBUG)
#include <linux/moduleparam.h>
#endif /* defined(PVRSRV_NEED_PVR_DPF) || defined(DEBUG) */

#if defined(PVRSRV_NEED_PVR_DPF)
extern IMG_UINT32 gPVRDebugLevel;
module_param(gPVRDebugLevel, uint, 0644);
MODULE_PARM_DESC(gPVRDebugLevel, "Sets the level of debug output (default 0x7)");
#endif /* defined(PVRSRV_NEED_PVR_DPF) */

#if defined(DEBUG)
extern IMG_UINT32 gPMRAllocFail;
module_param(gPMRAllocFail, uint, 0644);
MODULE_PARM_DESC(gPMRAllocFail, "When number of PMR allocs reaches"
        " this value, it will fail (default value is 0 which"
        "means that alloc function will behave normally).");
#endif /* defined(DEBUG) */

/* Export some symbols that may be needed by other drivers */
EXPORT_SYMBOL(PVRSRVCheckStatus);
EXPORT_SYMBOL(PVRSRVGetErrorStringKM);

#if defined(SUPPORT_KERNEL_HWPERF)
#include "rgxapi_km.h"
EXPORT_SYMBOL(RGXHWPerfConnect);
EXPORT_SYMBOL(RGXHWPerfDisconnect);
EXPORT_SYMBOL(RGXHWPerfControl);
EXPORT_SYMBOL(RGXHWPerfConfigureAndEnableCounters);
EXPORT_SYMBOL(RGXHWPerfDisableCounters);
EXPORT_SYMBOL(RGXHWPerfAcquireData);
EXPORT_SYMBOL(RGXHWPerfReleaseData);
#endif

DEFINE_MUTEX(gPVRSRVLock);

static DEFINE_MUTEX(gsPMMutex);
static IMG_BOOL bDriverIsSuspended;
static IMG_BOOL bDriverIsShutdown;

#if !defined(SUPPORT_DRM_EXT)
LDM_DEV *gpsPVRLDMDev;
#endif

/*!
******************************************************************************

 @Function		PVRSRVDriverShutdown

 @Description

 Suspend device operation for system shutdown.  This is called as part of the
 system halt/reboot process.  The driver is put into a quiescent state by
 setting the power state to D3.

 @input pDevice - the device for which shutdown is requested

 @Return nothing

*****************************************************************************/
void PVRSRVDriverShutdown(LDM_DEV *pDevice)
{
	PVR_TRACE(("PVRSRVDriverShutdown (pDevice=%p)", pDevice));

	mutex_lock(&gsPMMutex);

	if (!bDriverIsShutdown && !bDriverIsSuspended)
	{
		/*
		 * Take the bridge mutex, and never release it, to stop
		 * processes trying to use the driver after it has been
		 * shutdown.
		 */
		OSAcquireBridgeLock();

		(void) PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_OFF, IMG_TRUE);
	}

	bDriverIsShutdown = IMG_TRUE;

	/* The bridge mutex is held on exit */
	mutex_unlock(&gsPMMutex);
}

/*!
******************************************************************************

 @Function		PVRSRVDriverSuspend

 @Description

 Suspend device operation.

 @input pDevice - the device for which resume is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
int PVRSRVDriverSuspend(struct device *pDevice)
{
	int res = 0;

	PVR_TRACE(( "PVRSRVDriverSuspend (pDevice=%p)", pDevice));

	mutex_lock(&gsPMMutex);

	if (!bDriverIsSuspended && !bDriverIsShutdown)
	{
		OSAcquireBridgeLock();

		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_OFF, IMG_TRUE) == PVRSRV_OK)
		{
			bDriverIsSuspended = IMG_TRUE;
			OSSetDriverSuspended();
		}
		else
		{
			res = -EINVAL;
		}
		OSReleaseBridgeLock();
	}

	mutex_unlock(&gsPMMutex);

	return res;
}

/*!
******************************************************************************

 @Function		PVRSRVDriverResume

 @Description

 Resume device operation.

 @input pDevice - the device for which resume is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
int PVRSRVDriverResume(struct device *pDevice)
{
	int res = 0;

	PVR_TRACE(("PVRSRVDriverResume (pDevice=%p)", pDevice));

	mutex_lock(&gsPMMutex);

	if (bDriverIsSuspended && !bDriverIsShutdown)
	{
		OSAcquireBridgeLock();

		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_ON, IMG_TRUE) == PVRSRV_OK)
		{
			bDriverIsSuspended = IMG_FALSE;
			OSClearDriverSuspended();
		}
		else
		{
			res = -EINVAL;
		}
		OSReleaseBridgeLock();
	}

	mutex_unlock(&gsPMMutex);

	return res;
}

#if defined(SUPPORT_DRM)
#define PRIVATE_DATA(pFile) (PVR_DRM_FILE_FROM_FILE(pFile)->driver_priv)
#else
#define PRIVATE_DATA(pFile) ((pFile)->private_data)
#endif

/*!
******************************************************************************

 @Function		PVRSRVCommonOpen

 @Description

 Open the PVR services node.

 @input pFile - the file handle data for the actual file being opened

 @Return 0 for success or <0 for an error.

*****************************************************************************/
int PVRSRVCommonOpen(struct file *pFile)
{
	void *pvConnectionData;
	PVRSRV_ERROR eError;

	OSAcquireBridgeLock();

	/*
	 * Here we pass the file pointer which will passed through to our
	 * OSConnectionPrivateDataInit function where we can save it so
	 * we can back reference the file structure from it's connection
	 */
	eError = PVRSRVConnectionConnect(&pvConnectionData, (IMG_PVOID) pFile);
	if (eError != PVRSRV_OK)
	{
		OSReleaseBridgeLock();
		return -ENOMEM;
	}

	PRIVATE_DATA(pFile) = pvConnectionData;
	OSReleaseBridgeLock();

	return 0;
}

/*!
******************************************************************************

 @Function		PVRSRVCommonRelease

 @Description

 Release access the PVR services node - called when a file is closed, whether
 at exit or using close(2) system call.

 @input pFile - the file handle data for the actual file being released

 @Return 0 for success or <0 for an error.

*****************************************************************************/
void PVRSRVCommonRelease(struct file *pFile)
{
	void *pvConnectionData;

	OSAcquireBridgeLock();

	pvConnectionData = PRIVATE_DATA(pFile);
	if (pvConnectionData)
	{
		PVRSRVConnectionDisconnect(pvConnectionData);
		PRIVATE_DATA(pFile) = NULL;
	}

	OSReleaseBridgeLock();
}


CONNECTION_DATA *LinuxConnectionFromFile(struct file *pFile)
{
	return (pFile)? PRIVATE_DATA(pFile): IMG_NULL;
}

struct file *LinuxFileFromConnection(CONNECTION_DATA *psConnection)
{
	ENV_CONNECTION_DATA *psEnvConnection;

	psEnvConnection = PVRSRVConnectionPrivateData(psConnection);
	PVR_ASSERT(psEnvConnection != NULL);

	return psEnvConnection->psFile;
}

#if defined(SUPPORT_AUTH)
PVRSRV_ERROR OSCheckAuthentication(CONNECTION_DATA *psConnection, IMG_UINT32 ui32Level)
{
	if (ui32Level != 0)
	{
		ENV_CONNECTION_DATA *psEnvConnection;

		psEnvConnection = PVRSRVConnectionPrivateData(psConnection);
		if (psEnvConnection == IMG_NULL)
		{
			return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
		}

		if (!PVR_DRM_FILE_FROM_FILE(psEnvConnection->psFile)->authenticated)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: PVR Services Connection not authenticated", __FUNCTION__));
			return PVRSRV_ERROR_NOT_AUTHENTICATED;
		}
	}

	return PVRSRV_OK;
}
#endif /* defined(SUPPORT_AUTH) */

/*!
*****************************************************************************

 @Function		PVRSRVDriverInit

 @Description	Do the driver-specific initialisation (as opposed to
              the device-specific initialisation.)

*****************************************************************************/
int PVRSRVDriverInit(void)
{
	int error = 0;

	error = PVRDebugFSInit();
	if (error != 0)
	{
		return error;
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	if (PVRSRVStatsInitialise() != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#endif

	if (PVROSFuncInit() != PVRSRV_OK)
	{
		return -ENOMEM;
	}

	LinuxBridgeInit();

	return 0;
}

/*!
*****************************************************************************

 @Function		PVRSRVDriverDeinit

 @Description	Unwind PVRSRVDriverInit

*****************************************************************************/
void PVRSRVDriverDeinit(void)
{
	LinuxBridgeDeInit();

	PVROSFuncDeInit();

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsDestroy();
#endif
	PVRDebugFSDeInit();
}

/*!
*****************************************************************************

 @Function		PVRSRVDeviceInit

 @Description	Do the initialisation we have to do after registering the device

*****************************************************************************/
int PVRSRVDeviceInit(void)
{
	int error = 0;

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	{
		PVRSRV_ERROR eError = pvr_sync_init();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create sync (%d)", eError));
			return -EBUSY;
		}
	}
#endif

	error = PVRDebugCreateDebugFSEntries();
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRCore_Init: failed to create default debugfs entries (%d)", error));
	}

#if defined(SUPPORT_GPUTRACE_EVENTS)
	error = PVRGpuTraceInit();
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRCore_Init: failed to initialise PVR GPU Tracing (%d)", error));
	}
#endif

#if defined(SUPPORT_KERNEL_SRVINIT)
	{
		PVRSRV_ERROR eError = SrvInit();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: SrvInit failed (%d)", eError));
			return -ENODEV;
		}
	}
#endif
	return 0;
}

/*!
*****************************************************************************

 @Function		PVRSRVDeviceDeinit

 @Description	Unwind PVRSRVDeviceInit

*****************************************************************************/
void PVRSRVDeviceDeinit(void)
{
#if defined(SUPPORT_GPUTRACE_EVENTS)
	PVRGpuTraceDeInit();
#endif

	PVRDebugRemoveDebugFSEntries();

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	pvr_sync_deinit();
#endif
}
