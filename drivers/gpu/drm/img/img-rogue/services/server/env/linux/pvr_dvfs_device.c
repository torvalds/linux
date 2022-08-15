/*************************************************************************/ /*!
@File
@Title          PowerVR devfreq device implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Linux module setup
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

#if !defined(NO_HARDWARE)

#include <linux/devfreq.h>
#if defined(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif
#include <linux/version.h>
#include <linux/device.h>
#include <drm/drm.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#else
#include <drm/drmP.h>
#endif

#include "power.h"
#include "pvrsrv.h"
#include "pvrsrv_device.h"

#include "rgxdevice.h"
#include "rgxinit.h"
#include "sofunc_rgx.h"

#include "syscommon.h"

#include "pvr_dvfs_device.h"

#include "kernel_compatibility.h"

static int _device_get_devid(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	int deviceId;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	/*
	 * Older kernels do not have render drm_minor member in drm_device,
	 * so we fallback to primary node for device identification
	 */
	deviceId = ddev->primary->index;
#else
	if (ddev->render)
		deviceId = ddev->render->index;
	else /* when render node is NULL, fallback to primary node */
		deviceId = ddev->primary->index;
#endif

	return deviceId;
}

static IMG_INT32 devfreq_target(struct device *dev, unsigned long *requested_freq, IMG_UINT32 flags)
{
	int deviceId = _device_get_devid(dev);
	PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetDeviceInstanceByOSId(deviceId);
	RGX_DATA		*psRGXData = NULL;
	IMG_DVFS_DEVICE		*psDVFSDevice = NULL;
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = NULL;
	RGX_TIMING_INFORMATION	*psRGXTimingInfo = NULL;
	IMG_UINT32		ui32Freq, ui32CurFreq, ui32Volt;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	struct opp *opp;
#else
	struct dev_pm_opp *opp;
#endif

	/* Check the device is registered */
	if (!psDeviceNode)
	{
		return -ENODEV;
	}

	psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;
	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

	/* Check the RGX device is initialised */
	if (!psRGXData)
	{
		return -ENODATA;
	}

	psRGXTimingInfo = psRGXData->psRGXTimingInfo;
	if (!psDVFSDevice->bEnabled)
	{
		*requested_freq = psRGXTimingInfo->ui32CoreClockSpeed;
		return 0;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	rcu_read_lock();
#endif

	opp = devfreq_recommended_opp(dev, requested_freq, flags);
	if (IS_ERR(opp)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
		rcu_read_unlock();
#endif
		PVR_DPF((PVR_DBG_ERROR, "Invalid OPP"));
		return PTR_ERR(opp);
	}

	ui32Freq = dev_pm_opp_get_freq(opp);
	ui32Volt = dev_pm_opp_get_voltage(opp);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	rcu_read_unlock();
#else
	dev_pm_opp_put(opp);
#endif

	ui32CurFreq = psRGXTimingInfo->ui32CoreClockSpeed;

	if (ui32CurFreq == ui32Freq)
	{
		return 0;
	}

	if (PVRSRV_OK != PVRSRVDevicePreClockSpeedChange(psDeviceNode,
													 psDVFSDeviceCfg->bIdleReq,
													 NULL))
	{
		dev_err(dev, "PVRSRVDevicePreClockSpeedChange failed\n");
		return -EPERM;
	}

	/* Increasing frequency, change voltage first */
	if (ui32Freq > ui32CurFreq)
	{
		psDVFSDeviceCfg->pfnSetVoltage(ui32Volt);
	}

	psDVFSDeviceCfg->pfnSetFrequency(ui32Freq);

	/* Decreasing frequency, change frequency first */
	if (ui32Freq < ui32CurFreq)
	{
		psDVFSDeviceCfg->pfnSetVoltage(ui32Volt);
	}

	psRGXTimingInfo->ui32CoreClockSpeed = ui32Freq;

	PVRSRVDevicePostClockSpeedChange(psDeviceNode, psDVFSDeviceCfg->bIdleReq,
									 NULL);

	return 0;
}

static int devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	int                      deviceId = _device_get_devid(dev);
	PVRSRV_DEVICE_NODE      *psDeviceNode = PVRSRVGetDeviceInstanceByOSId(deviceId);
	PVRSRV_RGXDEV_INFO      *psDevInfo = NULL;
	IMG_DVFS_DEVICE         *psDVFSDevice = NULL;
	RGX_DATA                *psRGXData = NULL;
	RGX_TIMING_INFORMATION  *psRGXTimingInfo = NULL;
	RGXFWIF_GPU_UTIL_STATS   sGpuUtilStats;
	PVRSRV_ERROR             eError;

	/* Check the device is registered */
	if (!psDeviceNode)
	{
		return -ENODEV;
	}

	psDevInfo = psDeviceNode->pvDevice;
	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;

	/* Check the RGX device is initialised */
	if (!psDevInfo || !psRGXData)
	{
		return -ENODATA;
	}

	psRGXTimingInfo = psRGXData->psRGXTimingInfo;
	stat->current_frequency = psRGXTimingInfo->ui32CoreClockSpeed;

	if (psDevInfo->pfnGetGpuUtilStats == NULL)
	{
		/* Not yet ready. So set times to something sensible. */
		stat->busy_time = 0;
		stat->total_time = 0;
		return 0;
	}

	eError = psDevInfo->pfnGetGpuUtilStats(psDeviceNode,
						psDVFSDevice->hGpuUtilUserDVFS,
						&sGpuUtilStats);

	if (eError != PVRSRV_OK)
	{
		return -EAGAIN;
	}

	stat->busy_time = sGpuUtilStats.ui64GpuStatActive;
	stat->total_time = sGpuUtilStats.ui64GpuStatCumulative;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
static IMG_INT32 devfreq_cur_freq(struct device *dev, unsigned long *freq)
{
	int deviceId = _device_get_devid(dev);
	PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetDeviceInstanceByOSId(deviceId);
	RGX_DATA *psRGXData = NULL;

	/* Check the device is registered */
	if (!psDeviceNode)
	{
		return -ENODEV;
	}

	psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;

	/* Check the RGX device is initialised */
	if (!psRGXData)
	{
		return -ENODATA;
	}

	*freq = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	return 0;
}
#endif

static struct devfreq_dev_profile img_devfreq_dev_profile =
{
	.target             = devfreq_target,
	.get_dev_status     = devfreq_get_dev_status,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.get_cur_freq       = devfreq_cur_freq,
#endif
};

static int FillOPPTable(struct device *dev, PVRSRV_DEVICE_NODE *psDeviceNode)
{
	const IMG_OPP *iopp;
	int i, err = 0;
	IMG_DVFS_DEVICE_CFG *psDVFSDeviceCfg = NULL;

	/* Check the device exists */
	if (!dev || !psDeviceNode)
	{
		return -ENODEV;
	}

	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

	for (i = 0, iopp = psDVFSDeviceCfg->pasOPPTable;
	     i < psDVFSDeviceCfg->ui32OPPTableSize;
	     i++, iopp++)
	{
		err = dev_pm_opp_add(dev, iopp->ui32Freq, iopp->ui32Volt);
		if (err) {
			dev_err(dev, "Could not add OPP entry, %d\n", err);
			return err;
		}
	}

	return 0;
}

static void ClearOPPTable(struct device *dev, PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if (defined(CHROMIUMOS_KERNEL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))) || \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	const IMG_OPP *iopp;
	int i;
	IMG_DVFS_DEVICE_CFG *psDVFSDeviceCfg = NULL;

	/* Check the device exists */
	if (!dev || !psDeviceNode)
	{
		return;
	}

	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;

	for (i = 0, iopp = psDVFSDeviceCfg->pasOPPTable;
	     i < psDVFSDeviceCfg->ui32OPPTableSize;
	     i++, iopp++)
	{
		dev_pm_opp_remove(dev, iopp->ui32Freq);
	}
#endif
}

static int GetOPPValues(struct device *dev,
                        unsigned long *min_freq,
                        unsigned long *min_volt,
                        unsigned long *max_freq)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	struct opp *opp;
#else
	struct dev_pm_opp *opp;
#endif
	int count, i, err = 0;
	unsigned long freq;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)) && \
	(!defined(CHROMIUMOS_KERNEL) || (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))
	unsigned int *freq_table;
#else
	unsigned long *freq_table;
#endif

	count = dev_pm_opp_get_opp_count(dev);
	if (count < 0)
	{
		dev_err(dev, "Could not fetch OPP count, %d\n", count);
		return count;
	}

	dev_info(dev, "Found %d OPP points.\n", count);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
	freq_table = devm_kcalloc(dev, count, sizeof(*freq_table), GFP_ATOMIC);
#else
	freq_table = kcalloc(count, sizeof(*freq_table), GFP_ATOMIC);
#endif
	if (! freq_table)
	{
		return -ENOMEM;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	/* Start RCU read-side critical section to map frequency to OPP */
	rcu_read_lock();
#endif

	/* Iterate over OPP table; Iteration 0 finds "opp w/ freq >= 0 Hz".	 */
	freq = 0;
	opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	if (IS_ERR(opp))
	{
		err = PTR_ERR(opp);
		dev_err(dev, "Couldn't find lowest frequency, %d\n", err);
		goto exit;
	}

	*min_volt = dev_pm_opp_get_voltage(opp);
	*max_freq = *min_freq = freq_table[0] = freq;
	dev_info(dev, "opp[%d/%d]: (%lu Hz, %lu uV)\n", 1, count, freq, *min_volt);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
	dev_pm_opp_put(opp);
#endif

	/* Iteration i > 0 finds "opp w/ freq >= (opp[i-1].freq + 1)". */
	for (i = 1; i < count; i++)
	{
		freq++;
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
		{
			err = PTR_ERR(opp);
			dev_err(dev, "Couldn't find %dth frequency, %d\n", i, err);
			goto exit;
		}

		freq_table[i] = freq;
		*max_freq = freq;
		dev_info(dev,
				 "opp[%d/%d]: (%lu Hz, %lu uV)\n",
				  i + 1,
				  count,
				  freq,
				  dev_pm_opp_get_voltage(opp));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
		dev_pm_opp_put(opp);
#endif
	}

exit:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	rcu_read_unlock();
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	if (!err)
	{
		img_devfreq_dev_profile.freq_table = freq_table;
		img_devfreq_dev_profile.max_state = count;
	}
	else
#endif
	{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
		devm_kfree(dev, freq_table);
#else
		kfree(freq_table);
#endif
	}

	return err;
}

#if defined(CONFIG_DEVFREQ_THERMAL)
static int RegisterCoolingDevice(struct device *dev,
								 IMG_DVFS_DEVICE *psDVFSDevice,
								 struct devfreq_cooling_power *powerOps)
{
	struct device_node *of_node;
	int err = 0;
	PVRSRV_VZ_RET_IF_MODE(GUEST, err);

	if (!psDVFSDevice)
	{
		return -EINVAL;
	}

	if (!powerOps)
	{
		dev_info(dev, "Cooling: power ops not registered, not enabling cooling");
		return 0;
	}

	of_node = of_node_get(dev->of_node);

	psDVFSDevice->psDevfreqCoolingDevice = of_devfreq_cooling_register_power(
		of_node, psDVFSDevice->psDevFreq, powerOps);

	if (IS_ERR(psDVFSDevice->psDevfreqCoolingDevice))
	{
		err = PTR_ERR(psDVFSDevice->psDevfreqCoolingDevice);
		dev_err(dev, "Failed to register as devfreq cooling device %d", err);
	}

	of_node_put(of_node);

	return err;
}
#endif

#define TO_IMG_ERR(err) ((err == -EPROBE_DEFER) ? PVRSRV_ERROR_PROBE_DEFER : PVRSRV_ERROR_INIT_FAILURE)

PVRSRV_ERROR InitDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_DVFS_DEVICE        *psDVFSDevice = NULL;
	IMG_DVFS_DEVICE_CFG    *psDVFSDeviceCfg = NULL;
	struct device          *psDev;
	PVRSRV_ERROR            eError;
	int                     err;

	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);

#if !defined(CONFIG_PM_OPP)
	return PVRSRV_ERROR_NOT_SUPPORTED;
#endif

	if (!psDeviceNode)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psDeviceNode->psDevConfig->sDVFS.sDVFSDevice.bInitPending)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "DVFS initialise pending for device node %p",
				 psDeviceNode));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	psDev = psDeviceNode->psDevConfig->pvOSDevice;
	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	psDeviceNode->psDevConfig->sDVFS.sDVFSDevice.bInitPending = IMG_TRUE;

#if defined(SUPPORT_SOC_TIMER)
	if (! psDeviceNode->psDevConfig->pfnSoCTimerRead)
	{
		PVR_DPF((PVR_DBG_ERROR, "System layer SoC timer callback not implemented"));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}
#endif

	eError = SORgxGpuUtilStatsRegister(&psDVFSDevice->hGpuUtilUserDVFS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to register to the GPU utilisation stats, %d", eError));
		return eError;
	}

#if defined(CONFIG_OF)
	err = dev_pm_opp_of_add_table(psDev);
	if (err)
	{
		/*
		 * If there are no device tree or system layer provided operating points
		 * then return an error
		 */
		if (err != -ENODEV || !psDVFSDeviceCfg->pasOPPTable)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to init opp table from devicetree, %d", err));
			eError = TO_IMG_ERR(err);
			goto err_exit;
		}
	}
#endif

	if (psDVFSDeviceCfg->pasOPPTable)
	{
		err = FillOPPTable(psDev, psDeviceNode);
		if (err)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to fill OPP table with data, %d", err));
			eError = TO_IMG_ERR(err);
			goto err_exit;
		}
	}

	PVR_TRACE(("PVR DVFS init pending: dev = %p, PVR device = %p",
			   psDev, psDeviceNode));

	return PVRSRV_OK;

err_exit:
	DeinitDVFS(psDeviceNode);
	return eError;
}

PVRSRV_ERROR RegisterDVFSDevice(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_DVFS_DEVICE        *psDVFSDevice = NULL;
	IMG_DVFS_DEVICE_CFG    *psDVFSDeviceCfg = NULL;
	IMG_DVFS_GOVERNOR_CFG  *psDVFSGovernorCfg = NULL;
	RGX_TIMING_INFORMATION *psRGXTimingInfo = NULL;
	struct device          *psDev;
	unsigned long           min_freq = 0, max_freq = 0, min_volt = 0;
	PVRSRV_ERROR            eError;
	int                     err;

	if (!psDeviceNode)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (!psDeviceNode->psDevConfig->sDVFS.sDVFSDevice.bInitPending)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "DVFS initialise not yet pending for device node %p",
				 psDeviceNode));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	psDev = psDeviceNode->psDevConfig->pvOSDevice;
	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	psDVFSGovernorCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSGovernorCfg;
	psRGXTimingInfo = ((RGX_DATA *)psDeviceNode->psDevConfig->hDevData)->psRGXTimingInfo;
	psDeviceNode->psDevConfig->sDVFS.sDVFSDevice.bInitPending = IMG_FALSE;
	psDeviceNode->psDevConfig->sDVFS.sDVFSDevice.bReady = IMG_TRUE;

	err = GetOPPValues(psDev, &min_freq, &min_volt, &max_freq);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to read OPP points, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}

	img_devfreq_dev_profile.initial_freq = min_freq;
	img_devfreq_dev_profile.polling_ms = psDVFSDeviceCfg->ui32PollMs;

	psRGXTimingInfo->ui32CoreClockSpeed = min_freq;

	psDVFSDeviceCfg->pfnSetFrequency(min_freq);
	psDVFSDeviceCfg->pfnSetVoltage(min_volt);

	psDVFSDevice->data.upthreshold = psDVFSGovernorCfg->ui32UpThreshold;
	psDVFSDevice->data.downdifferential = psDVFSGovernorCfg->ui32DownDifferential;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	psDVFSDevice->psDevFreq = devm_devfreq_add_device(psDev,
													  &img_devfreq_dev_profile,
													  "simple_ondemand",
													  &psDVFSDevice->data);
#else
	psDVFSDevice->psDevFreq = devfreq_add_device(psDev,
												 &img_devfreq_dev_profile,
												 "simple_ondemand",
												 &psDVFSDevice->data);
#endif

	if (IS_ERR(psDVFSDevice->psDevFreq))
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "Failed to add as devfreq device %p, %ld",
				 psDVFSDevice->psDevFreq,
				 PTR_ERR(psDVFSDevice->psDevFreq)));
		eError = TO_IMG_ERR(PTR_ERR(psDVFSDevice->psDevFreq));
		goto err_exit;
	}

	eError = SuspendDVFS(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVInit: Failed to suspend DVFS"));
		goto err_exit;
	}

#if defined(CHROMIUMOS_KERNEL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	psDVFSDevice->psDevFreq->policy.user.min_freq = min_freq;
	psDVFSDevice->psDevFreq->policy.user.max_freq = max_freq;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
	psDVFSDevice->psDevFreq->scaling_min_freq = min_freq;
	psDVFSDevice->psDevFreq->scaling_max_freq = max_freq;
#else
	psDVFSDevice->psDevFreq->min_freq = min_freq;
	psDVFSDevice->psDevFreq->max_freq = max_freq;
#endif

	err = devfreq_register_opp_notifier(psDev, psDVFSDevice->psDevFreq);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to register opp notifier, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}

#if defined(CONFIG_DEVFREQ_THERMAL)
	err = RegisterCoolingDevice(psDev, psDVFSDevice, psDVFSDeviceCfg->psPowerOps);
	if (err)
	{
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}
#endif

	PVR_TRACE(("PVR DVFS activated: %lu-%lu Hz, Period: %ums",
			   min_freq,
			   max_freq,
			   psDVFSDeviceCfg->ui32PollMs));

	return PVRSRV_OK;

err_exit:
	UnregisterDVFSDevice(psDeviceNode);
	return eError;
}

void UnregisterDVFSDevice(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_DVFS_DEVICE *psDVFSDevice = NULL;
	struct device *psDev = NULL;
	IMG_INT32 i32Error;

	/* Check the device exists */
	if (!psDeviceNode)
	{
		return;
	}

	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDev = psDeviceNode->psDevConfig->pvOSDevice;

	if (! psDVFSDevice)
	{
		return;
	}

#if defined(CONFIG_DEVFREQ_THERMAL)
	if (!IS_ERR_OR_NULL(psDVFSDevice->psDevfreqCoolingDevice))
	{
		devfreq_cooling_unregister(psDVFSDevice->psDevfreqCoolingDevice);
		psDVFSDevice->psDevfreqCoolingDevice = NULL;
	}
#endif

	if (psDVFSDevice->psDevFreq)
	{
		i32Error = devfreq_unregister_opp_notifier(psDev, psDVFSDevice->psDevFreq);
		if (i32Error < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to unregister OPP notifier"));
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0))
		devfreq_remove_device(psDVFSDevice->psDevFreq);
#else
		devm_devfreq_remove_device(psDev, psDVFSDevice->psDevFreq);
#endif

		psDVFSDevice->psDevFreq = NULL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) && \
     LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	kfree(img_devfreq_dev_profile.freq_table);
#endif

	psDVFSDevice->bInitPending = IMG_FALSE;
	psDVFSDevice->bReady = IMG_FALSE;
}

void DeinitDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_DVFS_DEVICE *psDVFSDevice = NULL;
	struct device *psDev = NULL;

	/* Check the device exists */
	if (!psDeviceNode)
	{
		return;
	}

	PVRSRV_VZ_RETN_IF_MODE(GUEST);

	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDev = psDeviceNode->psDevConfig->pvOSDevice;

	/* Remove OPP entries for this device */
	ClearOPPTable(psDev, psDeviceNode);

#if defined(CONFIG_OF)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) || \
	(defined(CHROMIUMOS_KERNEL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)))
	dev_pm_opp_of_remove_table(psDev);
#endif
#endif

	SORgxGpuUtilStatsUnregister(psDVFSDevice->hGpuUtilUserDVFS);
	psDVFSDevice->hGpuUtilUserDVFS = NULL;
	psDVFSDevice->bInitPending = IMG_FALSE;
	psDVFSDevice->bReady = IMG_FALSE;
}

PVRSRV_ERROR SuspendDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_DVFS_DEVICE	*psDVFSDevice = NULL;

	/* Check the device is registered */
	if (!psDeviceNode)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDVFSDevice->bEnabled = IMG_FALSE;

	return PVRSRV_OK;
}

PVRSRV_ERROR ResumeDVFS(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	IMG_DVFS_DEVICE	*psDVFSDevice = NULL;

	/* Check the device is registered */
	if (!psDeviceNode)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;

	/* Not supported in GuestOS drivers */
	psDVFSDevice->bEnabled = !PVRSRV_VZ_MODE_IS(GUEST);

	return PVRSRV_OK;
}

#endif /* !NO_HARDWARE */
