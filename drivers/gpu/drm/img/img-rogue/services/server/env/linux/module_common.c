/*************************************************************************/ /*!
@File
@Title          Common Linux module setup
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

#include <linux/module.h>

#if defined(CONFIG_DEBUG_FS)
#include "pvr_debugfs.h"
#endif /* defined(CONFIG_DEBUG_FS) */
#if defined(CONFIG_PROC_FS)
#include "pvr_procfs.h"
#endif /* defined(CONFIG_PROC_FS) */
#include "di_server.h"
#include "private_data.h"
#include "linkage.h"
#include "power.h"
#include "env_connection.h"
#include "process_stats.h"
#include "module_common.h"
#include "pvrsrv.h"
#include "srvcore.h"
#if defined(SUPPORT_RGX)
#include "rgxdevice.h"
#endif
#include "pvrsrv_error.h"
#include "pvr_drv.h"
#include "pvr_bridge_k.h"

#include "pvr_fence.h"

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#if !defined(USE_PVRSYNC_DEVNODE)
#include "pvr_sync_ioctl_drm.h"
#endif
#endif

#include "ospvr_gputrace.h"

#include "km_apphint.h"
#include "srvinit.h"

#include "pvr_ion_stats.h"
#include "sysconfig.h"

#if defined(SUPPORT_DISPLAY_CLASS)
/* Display class interface */
#include "kerneldisplay.h"
EXPORT_SYMBOL(DCRegisterDevice);
EXPORT_SYMBOL(DCUnregisterDevice);
EXPORT_SYMBOL(DCDisplayConfigurationRetired);
EXPORT_SYMBOL(DCDisplayHasPendingCommand);
EXPORT_SYMBOL(DCImportBufferAcquire);
EXPORT_SYMBOL(DCImportBufferRelease);

EXPORT_SYMBOL(PVRSRVGetDriverStatus);
EXPORT_SYMBOL(PVRSRVSystemInstallDeviceLISR);
EXPORT_SYMBOL(PVRSRVSystemUninstallDeviceLISR);

#include "pvr_notifier.h"
EXPORT_SYMBOL(PVRSRVCheckStatus);
#endif /* defined(SUPPORT_DISPLAY_CLASS) */

#if defined(SUPPORT_EXTERNAL_PHYSHEAP_INTERFACE)
/*
 * Physmem interface.
 * Required by LMA DC drivers, and some non-DC LMA display drivers.
 */
#include "physheap.h"
EXPORT_SYMBOL(PhysHeapAcquireByID);
EXPORT_SYMBOL(PhysHeapRelease);
EXPORT_SYMBOL(PhysHeapGetType);
EXPORT_SYMBOL(PhysHeapGetCpuPAddr);
EXPORT_SYMBOL(PhysHeapGetSize);
EXPORT_SYMBOL(PhysHeapCpuPAddrToDevPAddr);

#include "pvr_debug.h"
EXPORT_SYMBOL(PVRSRVGetErrorString);
EXPORT_SYMBOL(PVRSRVGetDeviceInstance);
#endif

#if defined(SUPPORT_RGX)
#include "rgxapi_km.h"
#if defined(SUPPORT_SHARED_SLC)
EXPORT_SYMBOL(RGXInitSLC);
#endif
EXPORT_SYMBOL(RGXHWPerfConnect);
EXPORT_SYMBOL(RGXHWPerfDisconnect);
EXPORT_SYMBOL(RGXHWPerfControl);
#if defined(RGX_FEATURE_HWPERF_VOLCANIC)
EXPORT_SYMBOL(RGXHWPerfConfigureCounters);
#else
EXPORT_SYMBOL(RGXHWPerfConfigMuxCounters);
EXPORT_SYMBOL(RGXHWPerfConfigureAndEnableCustomCounters);
#endif
EXPORT_SYMBOL(RGXHWPerfDisableCounters);
EXPORT_SYMBOL(RGXHWPerfAcquireEvents);
EXPORT_SYMBOL(RGXHWPerfReleaseEvents);
EXPORT_SYMBOL(RGXHWPerfConvertCRTimeStamp);
#if defined(SUPPORT_KERNEL_HWPERF_TEST)
EXPORT_SYMBOL(OSAddTimer);
EXPORT_SYMBOL(OSEnableTimer);
EXPORT_SYMBOL(OSDisableTimer);
EXPORT_SYMBOL(OSRemoveTimer);
#endif
#endif

static int PVRSRVDeviceSyncOpen(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
                                struct drm_file *psDRMFile);

CONNECTION_DATA *LinuxServicesConnectionFromFile(struct file *pFile)
{
	if (pFile)
	{
		struct drm_file *psDRMFile;
		PVRSRV_CONNECTION_PRIV *psConnectionPriv;

		psDRMFile = pFile->private_data;
		PVR_LOG_RETURN_IF_FALSE(psDRMFile != NULL, "psDRMFile is NULL", NULL);

		psConnectionPriv = (PVRSRV_CONNECTION_PRIV*)psDRMFile->driver_priv;
		PVR_LOG_RETURN_IF_FALSE(psConnectionPriv != NULL, "psConnectionPriv is NULL", NULL);

		return (CONNECTION_DATA*)psConnectionPriv->pvConnectionData;
	}

	return NULL;
}

CONNECTION_DATA *LinuxSyncConnectionFromFile(struct file *pFile)
{
	if (pFile)
	{
		struct drm_file *psDRMFile = pFile->private_data;
		PVRSRV_CONNECTION_PRIV *psConnectionPriv = (PVRSRV_CONNECTION_PRIV*)psDRMFile->driver_priv;

#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
		return (CONNECTION_DATA*)psConnectionPriv->pvConnectionData;
#else
		return (CONNECTION_DATA*)psConnectionPriv->pvSyncConnectionData;
#endif
	}

	return NULL;
}

/**************************************************************************/ /*!
@Function     PVRSRVDriverInit
@Description  Common one time driver initialisation
@Return       int           0 on success and a Linux error code otherwise
*/ /***************************************************************************/
int PVRSRVDriverInit(void)
{
	PVRSRV_ERROR error;
	int os_err;

	error = PVROSFuncInit();
	if (error != PVRSRV_OK)
	{
		return -ENOMEM;
	}

	error = PVRSRVCommonDriverInit();
	if (error != PVRSRV_OK)
	{
		return -ENODEV;
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	error = pvr_sync_register_functions();
	if (error != PVRSRV_OK)
	{
		return -EPERM;
	}

	os_err = pvr_sync_init();
	if (os_err != 0)
	{
		return os_err;
	}
#endif

	os_err = pvr_apphint_init();
	if (os_err != 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: failed AppHint setup(%d)", __func__,
			     os_err));
	}

#if defined(SUPPORT_RGX)
	error = PVRGpuTraceSupportInit();
	if (error != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#endif

#if defined(ANDROID)
#if defined(CONFIG_PROC_FS)
	error = PVRProcFsRegister();
	if (error != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#elif defined(CONFIG_DEBUG_FS)
	error = PVRDebugFsRegister();
	if (error != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#endif /* defined(CONFIG_PROC_FS) || defined(CONFIG_DEBUG_FS) */
#else
#if defined(CONFIG_DEBUG_FS)
	error = PVRDebugFsRegister();
	if (error != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#elif defined(CONFIG_PROC_FS)
	error = PVRProcFsRegister();
	if (error != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#endif /* defined(CONFIG_DEBUG_FS) || defined(CONFIG_PROC_FS) */
#endif /* defined(ANDROID) */

	error = PVRSRVIonStatsInitialise();
	if (error != PVRSRV_OK)
	{
		return -ENODEV;
	}

#if defined(SUPPORT_RGX)
	/* calling here because we need to handle input from the file even
	 * before the devices are initialised
	 * note: we're not passing a device node because apphint callbacks don't
	 * need it */
	PVRGpuTraceInitAppHintCallbacks(NULL);
#endif

	return 0;
}

/**************************************************************************/ /*!
@Function     PVRSRVDriverDeinit
@Description  Common one time driver de-initialisation
@Return       void
*/ /***************************************************************************/
void PVRSRVDriverDeinit(void)
{
	pvr_apphint_deinit();

	PVRSRVIonStatsDestroy();

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	pvr_sync_deinit();
#endif

	PVRSRVCommonDriverDeInit();

#if defined(SUPPORT_RGX)
	PVRGpuTraceSupportDeInit();
#endif

	PVROSFuncDeInit();
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceInit
@Description  Common device related initialisation.
@Input        psDeviceNode  The device node for which initialisation should be
                            performed
@Return       int           0 on success and a Linux error code otherwise
*/ /***************************************************************************/
int PVRSRVDeviceInit(PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	{
		PVRSRV_ERROR eError = pvr_sync_device_init(psDeviceNode->psDevConfig->pvOSDevice);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: unable to create sync (%d)",
					 __func__, eError));
			return -EBUSY;
		}
	}
#endif

#if defined(SUPPORT_RGX)
	{
		int error = PVRGpuTraceInitDevice(psDeviceNode);
		if (error != 0)
		{
			PVR_DPF((PVR_DBG_WARNING,
				 "%s: failed to initialise PVR GPU Tracing on device%d (%d)",
				 __func__, psDeviceNode->sDevId.i32KernelDeviceID, error));
		}
	}
#endif

	return 0;
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceDeinit
@Description  Common device related de-initialisation.
@Input        psDeviceNode  The device node for which de-initialisation should
                            be performed
@Return       void
*/ /***************************************************************************/
void PVRSRVDeviceDeinit(PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if defined(SUPPORT_RGX)
	PVRGpuTraceDeInitDevice(psDeviceNode);
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	pvr_sync_device_deinit(psDeviceNode->psDevConfig->pvOSDevice);
#endif

#if defined(SUPPORT_DMA_TRANSFER)
	PVRSRVDeInitialiseDMA(psDeviceNode);
#endif

	pvr_fence_cleanup();
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceShutdown
@Description  Common device shutdown.
@Input        psDev  The device node representing the device that should
                     be shutdown
@Return       void
*/ /***************************************************************************/

void PVRSRVDeviceShutdown(struct drm_device *psDev)
{
	struct pvr_drm_private *psDevPriv = psDev->dev_private;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevPriv->dev_node;

	/* Since this is a shutdown request ignore the returned error and try to
	 * to power off the device. This is done because there is no way of
	 * signalling the OS that this call failed. */
	(void) LinuxBridgeBlockClientsAccess(psDevPriv, IMG_TRUE);

	/* Passing PVRSRV_POWER_FLAGS_NONE as there are no special actions required
	 * from the shutdown call beside the regular device power off. */
	(void) PVRSRVSetDeviceSystemPowerState(psDeviceNode,
	                                       PVRSRV_SYS_POWER_STATE_OFF,
	                                       PVRSRV_POWER_FLAGS_NONE);
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceSuspend
@Description  Common device suspend.
@Input        psDev  The device node representing the device that should
                     be suspended
@Return       int    0 on success and a Linux error code otherwise
*/ /***************************************************************************/
int PVRSRVDeviceSuspend(struct drm_device *psDev)
{
	struct pvr_drm_private *psDevPriv = psDev->dev_private;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevPriv->dev_node;
	PVRSRV_ERROR eError;

	/* LinuxBridgeBlockClientsAccess prevents processes from using the driver
	 * while it's suspended (this is needed for Android). */
	eError = LinuxBridgeBlockClientsAccess(psDevPriv, IMG_FALSE);
	PVR_LOG_RETURN_IF_FALSE_VA(eError == PVRSRV_OK, -EFAULT,
	                           "LinuxBridgeBlockClientsAccess() failed with error %u",
	                           eError);

#if defined(SUPPORT_AUTOVZ)
	/* To allow the driver to power down the GPU under AutoVz, the firmware must
	 * declared as offline, otherwise all power requests will be ignored. */
	psDeviceNode->bAutoVzFwIsUp = IMG_FALSE;
#endif

	if (PVRSRVSetDeviceSystemPowerState(psDeviceNode,
										PVRSRV_SYS_POWER_STATE_OFF,
										PVRSRV_POWER_FLAGS_SUSPEND_REQ) != PVRSRV_OK)
	{
		/* Ignore return error as we're already returning an error here. */
		(void) LinuxBridgeUnblockClientsAccess(psDevPriv);
		return -EINVAL;
	}

	return 0;
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceResume
@Description  Common device resume.
@Input        psDev  The device node representing the device that should
                     be resumed
@Return       int    0 on success and a Linux error code otherwise
*/ /***************************************************************************/
int PVRSRVDeviceResume(struct drm_device *psDev)
{
	struct pvr_drm_private *psDevPriv = psDev->dev_private;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevPriv->dev_node;

	if (PVRSRVSetDeviceSystemPowerState(psDeviceNode,
										PVRSRV_SYS_POWER_STATE_ON,
										PVRSRV_POWER_FLAGS_RESUME_REQ) != PVRSRV_OK)
	{
		return -EINVAL;
	}

	/* Ignore return error. We should proceed even if this fails. */
	(void) LinuxBridgeUnblockClientsAccess(psDevPriv);

	/*
	 * Reprocess the device queues in case commands were blocked during
	 * suspend.
	 */
	if (psDeviceNode->eDevState == PVRSRV_DEVICE_STATE_ACTIVE)
	{
		PVRSRVCheckStatus(NULL);
	}

	return 0;
}

int sPVRSRVDeviceSuspend(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	struct sf7110_cfg *sft = sys_get_privdata();

	if (sft->runtime_suspend != NULL)
		sft->runtime_suspend(NULL);

	return 0;
}

int sPVRSRVDeviceResume(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	struct sf7110_cfg *sft = sys_get_privdata();

	if (sft->runtime_resume != NULL)
		sft->runtime_resume(NULL);

	return 0;
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceServicesOpen
@Description  Services device open.
@Input        psDeviceNode  The device node representing the device being
                            opened by a user mode process
@Input        psDRMFile     The DRM file data that backs the file handle
                            returned to the user mode process
@Return       int           0 on success and a Linux error code otherwise
*/ /***************************************************************************/
int PVRSRVDeviceServicesOpen(PVRSRV_DEVICE_NODE *psDeviceNode,
                             struct drm_file *psDRMFile)
{
	static DEFINE_MUTEX(sDeviceInitMutex);
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	ENV_CONNECTION_PRIVATE_DATA sPrivData;
	PVRSRV_CONNECTION_PRIV *psConnectionPriv;
	PVRSRV_ERROR eError;
	int iErr = 0;

	if (!psPVRSRVData)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: No device data", __func__));
		iErr = -ENODEV;
		goto out;
	}

	mutex_lock(&sDeviceInitMutex);
	/*
	 * If the first attempt already set the state to bad,
	 * there is no point in going the second time, so get out
	 */
	if (psDeviceNode->eDevState == PVRSRV_DEVICE_STATE_BAD)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Driver already in bad state. Device open failed.",
				 __func__));
		iErr = -ENODEV;
		mutex_unlock(&sDeviceInitMutex);
		goto out;
	}

	if (psDRMFile->driver_priv == NULL)
	{
		/* Allocate psConnectionPriv (stores private data and release pfn under driver_priv) */
		psConnectionPriv = kzalloc(sizeof(*psConnectionPriv), GFP_KERNEL);
		if (!psConnectionPriv)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: No memory to allocate driver_priv data", __func__));
			iErr = -ENOMEM;
			mutex_unlock(&sDeviceInitMutex);
			goto fail_alloc_connection_priv;
		}
	}
	else
	{
		psConnectionPriv = (PVRSRV_CONNECTION_PRIV*)psDRMFile->driver_priv;
	}

	if (psDeviceNode->eDevState == PVRSRV_DEVICE_STATE_CREATED)
	{
		PVRSRVSetSystemPowerState(psDeviceNode->psDevConfig, PVRSRV_SYS_POWER_STATE_ON);
		eError = PVRSRVCommonDeviceInitialise(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to initialise device (%s)",
					 __func__, PVRSRVGetErrorString(eError)));
			iErr = -ENODEV;
			mutex_unlock(&sDeviceInitMutex);
			goto fail_device_init;
		}

#if defined(SUPPORT_RGX)
		PVRGpuTraceInitIfEnabled(psDeviceNode);
#endif
	}
	mutex_unlock(&sDeviceInitMutex);

	sPrivData.psDevNode = psDeviceNode;

	/*
	 * Here we pass the file pointer which will passed through to our
	 * OSConnectionPrivateDataInit function where we can save it so
	 * we can back reference the file structure from its connection
	 */
	eError = PVRSRVCommonConnectionConnect(&psConnectionPriv->pvConnectionData,
	                                       (void *)&sPrivData);
	if (eError != PVRSRV_OK)
	{
		iErr = -ENOMEM;
		goto fail_connect;
	}

#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
	psConnectionPriv->pfDeviceRelease = PVRSRVCommonConnectionDisconnect;
#endif
	psDRMFile->driver_priv = (void*)psConnectionPriv;

#if defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD)
	eError = PVRSRVGpuTraceWorkPeriodEventStatsRegister(
			&psConnectionPriv->pvGpuWorkPeriodEventStats);
	if (eError != PVRSRV_OK)
	{
		iErr = -ENOMEM;
		goto fail_connect;
	}
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) */
	goto out;

fail_connect:
fail_device_init:
	kfree(psConnectionPriv);
fail_alloc_connection_priv:
out:
	return iErr;
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceSyncOpen
@Description  Sync device open.
@Input        psDeviceNode  The device node representing the device being
                            opened by a user mode process
@Input        psDRMFile     The DRM file data that backs the file handle
                            returned to the user mode process
@Return       int           0 on success and a Linux error code otherwise
*/ /***************************************************************************/
static int PVRSRVDeviceSyncOpen(PVRSRV_DEVICE_NODE *psDeviceNode,
                                struct drm_file *psDRMFile)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	CONNECTION_DATA *psConnection = NULL;
	ENV_CONNECTION_PRIVATE_DATA sPrivData;
	PVRSRV_CONNECTION_PRIV *psConnectionPriv;
	PVRSRV_ERROR eError;
	int iErr = 0;

	if (!psPVRSRVData)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: No device data", __func__));
		iErr = -ENODEV;
		goto out;
	}

	if (psDRMFile->driver_priv == NULL)
	{
		/* Allocate psConnectionPriv (stores private data and release pfn under driver_priv) */
		psConnectionPriv = kzalloc(sizeof(*psConnectionPriv), GFP_KERNEL);
		if (!psConnectionPriv)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: No memory to allocate driver_priv data", __func__));
			iErr = -ENOMEM;
			goto out;
		}
	}
	else
	{
		psConnectionPriv = (PVRSRV_CONNECTION_PRIV*)psDRMFile->driver_priv;
	}

	/* Allocate connection data area, no stats since process not registered yet */
	psConnection = kzalloc(sizeof(*psConnection), GFP_KERNEL);
	if (!psConnection)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: No memory to allocate connection data", __func__));
		iErr = -ENOMEM;
		goto fail_alloc_connection;
	}
#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
	psConnectionPriv->pvConnectionData = (void*)psConnection;
#else
	psConnectionPriv->pvSyncConnectionData = (void*)psConnection;
#endif

	sPrivData.psDevNode = psDeviceNode;

	/* Call environment specific connection data init function */
	eError = OSConnectionPrivateDataInit(&psConnection->hOsPrivateData, &sPrivData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSConnectionPrivateDataInit() failed (%s)",
		        __func__, PVRSRVGetErrorString(eError)));
		goto fail_private_data_init;
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC) && !defined(USE_PVRSYNC_DEVNODE)
#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
	iErr = pvr_sync_open(psConnectionPriv->pvConnectionData, psDRMFile);
#else
	iErr = pvr_sync_open(psConnectionPriv->pvSyncConnectionData, psDRMFile);
#endif
	if (iErr)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: pvr_sync_open() failed(%d)",
				__func__, iErr));
		goto fail_pvr_sync_open;
	}
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC) && !defined(USE_PVRSYNC_DEVNODE)
#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
	psConnectionPriv->pfDeviceRelease = pvr_sync_close;
#endif
#endif
	psDRMFile->driver_priv = psConnectionPriv;
	goto out;

#if defined(SUPPORT_NATIVE_FENCE_SYNC) && !defined(USE_PVRSYNC_DEVNODE)
fail_pvr_sync_open:
	OSConnectionPrivateDataDeInit(psConnection->hOsPrivateData);
#endif
fail_private_data_init:
	kfree(psConnection);
fail_alloc_connection:
	kfree(psConnectionPriv);
out:
	return iErr;
}

/**************************************************************************/ /*!
@Function     PVRSRVDeviceRelease
@Description  Common device release.
@Input        psDeviceNode  The device node for the device that the given file
                            represents
@Input        psDRMFile     The DRM file data that's being released
@Return       void
*/ /***************************************************************************/
void PVRSRVDeviceRelease(PVRSRV_DEVICE_NODE *psDeviceNode,
                         struct drm_file *psDRMFile)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);

	if (psDRMFile->driver_priv)
	{
		PVRSRV_CONNECTION_PRIV *psConnectionPriv = (PVRSRV_CONNECTION_PRIV*)psDRMFile->driver_priv;

		if (psConnectionPriv->pvConnectionData)
		{
#if defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD)
			if (psConnectionPriv->pvGpuWorkPeriodEventStats)
			{
				PVRSRVGpuTraceWorkPeriodEventStatsUnregister(
						psConnectionPriv->pvGpuWorkPeriodEventStats);
				psConnectionPriv->pvGpuWorkPeriodEventStats = NULL;
			}
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) */

#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
			if (psConnectionPriv->pfDeviceRelease)
			{
				psConnectionPriv->pfDeviceRelease(psConnectionPriv->pvConnectionData);
			}
#else
			if (psConnectionPriv->pvConnectionData)
				PVRSRVCommonConnectionDisconnect(psConnectionPriv->pvConnectionData);

#if defined(SUPPORT_NATIVE_FENCE_SYNC) && !defined(USE_PVRSYNC_DEVNODE)
			if (psConnectionPriv->pvSyncConnectionData)
				pvr_sync_close(psConnectionPriv->pvSyncConnectionData);
#endif
#endif
		}

		kfree(psDRMFile->driver_priv);
		psDRMFile->driver_priv = NULL;
	}
}

int
drm_pvr_srvkm_init(struct drm_device *dev, void *arg, struct drm_file *psDRMFile)
{
	struct drm_pvr_srvkm_init_data *data = arg;
	struct pvr_drm_private *priv = dev->dev_private;
	int iErr = 0;

	switch (data->init_module)
	{
		case PVR_SRVKM_SYNC_INIT:
		{
			iErr = PVRSRVDeviceSyncOpen(priv->dev_node, psDRMFile);
			break;
		}
		case PVR_SRVKM_SERVICES_INIT:
		{
			iErr = PVRSRVDeviceServicesOpen(priv->dev_node, psDRMFile);
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: invalid init_module (%d)",
			        __func__, data->init_module));
			iErr = -EINVAL;
		}
	}

	return iErr;
}
