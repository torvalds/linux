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

#include <linux/devfreq.h>
#if defined(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#include <rk_init_v2.h>
#endif
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0))
#include <linux/pm_opp.h>
#define OPP_GET_OPP_COUNT dev_pm_opp_get_opp_count
#define OPP_GET_FREQ dev_pm_opp_get_freq
#define OPP_GET_VOLTAGE dev_pm_opp_get_voltage
#define OPP_ADD dev_pm_opp_add
#define OPP_FIND_FREQ_CEIL dev_pm_opp_find_freq_ceil
#define OPP_FIND_FREQ_FLOOR dev_pm_opp_find_freq_floor
#define OPP_STRUCT dev_pm_opp
#else
#include <linux/opp.h>
#define OPP_GET_OPP_COUNT opp_get_opp_count
#define OPP_GET_FREQ opp_get_freq
#define OPP_GET_VOLTAGE opp_get_voltage
#define OPP_ADD opp_add
#define OPP_FIND_FREQ_CEIL opp_find_freq_ceil
#define OPP_FIND_FREQ_FLOOR opp_find_freq_floor
#define OPP_STRUCT opp
#endif

#include "pvrsrv_device.h"
#include "syscommon.h"
#include "rgxinit.h"
#include "rgxdevice.h"
#include "pvr_dvfs_device.h"
#include "power.h"

#include <linux/device.h>

#if 0
#define dev_pm_opp_of_add_table of_init_opp_table
#define dev_pm_opp_of_remove_table of_free_opp_table
#endif


static PVRSRV_DEVICE_NODE* gpsDeviceNode = NULL;

static IMG_INT32 devfreq_target(struct device *dev, long unsigned *requested_freq, IMG_UINT32 flags)
{
	RGX_DATA		*psRGXData = (RGX_DATA*) gpsDeviceNode->psDevConfig->hDevData;
	IMG_DVFS_DEVICE		*psDVFSDevice = &gpsDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &gpsDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	RGX_TIMING_INFORMATION	*psRGXTimingInfo = psRGXData->psRGXTimingInfo;
	IMG_UINT32		ui32Freq, ui32CurFreq, ui32Volt, ui32CurVolt;
	struct OPP_STRUCT	*opp;


	if (!psDVFSDevice->bEnabled && !IsSystemStatePowered(gpsDeviceNode))
	{
		return 0;
	}

	if (g_gpu_performance == 1)
		*requested_freq = psDVFSDevice->psDevFreq->max_freq;

	opp = devfreq_recommended_opp(dev, requested_freq, flags);
	if (IS_ERR(opp)) {
		PVR_DPF((PVR_DBG_ERROR, "Invalid OPP"));
		return PTR_ERR(opp);
	}

	ui32Freq = OPP_GET_FREQ(opp);
	ui32Volt = OPP_GET_VOLTAGE(opp);
	dev_pm_opp_put(opp);

	ui32CurFreq = psRGXTimingInfo->ui32CoreClockSpeed;
	ui32CurVolt = psRGXTimingInfo->ui32CoreVoltage;

	if (ui32CurFreq == ui32Freq)
	{
		if(ui32CurVolt == ui32Volt)
			return 0;
		psDVFSDeviceCfg->pfnSetVoltage(ui32Volt);
		psRGXTimingInfo->ui32CoreVoltage = ui32Volt;
		return 0;
	}

	if (PVRSRV_OK != PVRSRVDevicePreClockSpeedChange(gpsDeviceNode,
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
	psRGXTimingInfo->ui32CoreVoltage = ui32Volt;

	PVRSRVDevicePostClockSpeedChange(gpsDeviceNode, psDVFSDeviceCfg->bIdleReq,
									 NULL);

	return 0;
}

static int devfreq_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	PVRSRV_RGXDEV_INFO      *psDevInfo = gpsDeviceNode->pvDevice;
	IMG_DVFS_DEVICE         *psDVFSDevice = &gpsDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	RGX_DATA                *psRGXData = (RGX_DATA*) gpsDeviceNode->psDevConfig->hDevData;
	RGX_TIMING_INFORMATION  *psRGXTimingInfo = psRGXData->psRGXTimingInfo;
	RGXFWIF_GPU_UTIL_STATS   sGpuUtilStats;
	PVRSRV_ERROR             eError = PVRSRV_OK;

	stat->current_frequency = psRGXTimingInfo->ui32CoreClockSpeed;

	if (psDevInfo->pfnGetGpuUtilStats == NULL)
	{
		/* Not yet ready.  So set times to something sensible. */
		stat->busy_time = 0;
		stat->total_time = 0;
		return 0;
	}

	eError = psDevInfo->pfnGetGpuUtilStats(psDevInfo->psDeviceNode,
						psDVFSDevice->hGpuUtilUserDVFS,
						&sGpuUtilStats);

	if (eError != PVRSRV_OK)
	{
		return -EAGAIN;
	}

	stat->busy_time = sGpuUtilStats.ui64GpuStatActiveHigh + sGpuUtilStats.ui64GpuStatActiveLow;
	stat->total_time = sGpuUtilStats.ui64GpuStatCumulative;

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
static IMG_INT32 devfreq_cur_freq(struct device *dev, unsigned long *freq)
{
	RGX_DATA *psRGXData = (RGX_DATA*) gpsDeviceNode->psDevConfig->hDevData;

	*freq = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	return 0;
}
#endif

static struct devfreq_dev_profile img_devfreq_dev_profile = {
	.target             = devfreq_target,
	.get_dev_status     = devfreq_get_dev_status,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.get_cur_freq       = devfreq_cur_freq,
#endif
};

static int FillOPPTable(struct device *dev)
{
	IMG_DVFS_DEVICE_CFG *psDVFSDeviceCfg =
		&gpsDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	const IMG_OPP *iopp;
	int i, err = 0;

	for (i = 0, iopp = psDVFSDeviceCfg->pasOPPTable;
	     i < psDVFSDeviceCfg->ui32OPPTableSize;
	     i++, iopp++)
	{
		err = OPP_ADD(dev, iopp->ui32Freq, iopp->ui32Volt);
		if (err) {
			dev_err(dev, "Could not add OPP entry, %d\n", err);
			return err;
		}
	}

	return 0;
}

static int GetOPPValues(struct device *dev,
                        unsigned long *min_freq,
                        unsigned long *min_volt,
                        unsigned long *max_freq)
{
	struct OPP_STRUCT *opp;
	int count, i, err = 0;
	unsigned long freq;

	/* ChromiumOS kernels are carrying a fix which changes the type of
	 * freq_table in struct devfreq_dev_profile to 'unsigned long'.
	 * However, this change has not been merged upstream, so we need
	 * to support using the older 'unsigned int' type too.
	 */
#if defined(CHROMIUMOS_WORKAROUNDS_KERNEL318)
	unsigned long *freq_table;
#else
	unsigned long *freq_table;
#endif

	/* Start RCU read-side critical section to access device opp_list. */
	count = OPP_GET_OPP_COUNT(dev);
	if (count < 0) {
		dev_err(dev, "Could not fetch OPP count, %d\n", count);
		return count;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
	freq_table = devm_kcalloc(dev, count, sizeof(*freq_table), GFP_ATOMIC);
#else
	freq_table = kcalloc(count, sizeof(*freq_table), GFP_ATOMIC);
#endif

	if (!freq_table) {
		return -ENOMEM;
	}

	/*
	 * Iterate over OPP table.
	 * Iteration 0 finds "opp w/ freq >= 0 Hz".
	 */
	freq = 0;
	opp = OPP_FIND_FREQ_CEIL(dev, &freq);
	if (IS_ERR(opp)) {
		err = PTR_ERR(opp);
		dev_err(dev, "Couldn't find lowest frequency, %d\n", err);
		goto exit;
	}

	freq_table[0] = freq;
	*min_freq = freq;
	*min_volt = OPP_GET_VOLTAGE(opp);
	dev_pm_opp_put(opp);
	dev_info(dev, "opp[%d/%d]: (%lu Hz, %lu uV)\n", 1, count,
		freq, *min_volt);

	/*
	 * Iteration i > 0 finds "opp w/ freq >= (opp[i-1].freq + 1)".
	 */
	for (i = 1; i < count; i++) {
		freq++;
		opp = OPP_FIND_FREQ_CEIL(dev, &freq);
		if (IS_ERR(opp)) {
			err = PTR_ERR(opp);
			dev_err(dev, "Couldn't find %dth frequency, %d\n", i, err);
			goto exit;
		}

		freq_table[i] = freq;
		*max_freq = freq;
		dev_info(dev, "opp[%d/%d]: (%lu Hz, %lu uV)\n", i + 1, count,
			freq, OPP_GET_VOLTAGE(opp));
		dev_pm_opp_put(opp);
	}

exit:

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

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	/* Not supported in GuestOS drivers */
	PVR_UNREFERENCED_PARAMETER(dev);
	PVR_UNREFERENCED_PARAMETER(psDVFSDevice);
	PVR_UNREFERENCED_PARAMETER(powerOps);
	PVR_UNREFERENCED_PARAMETER(of_node);
#else
	if (!powerOps)
	{
		dev_info(dev, "Cooling: power ops not registered, not enabling cooling");
		return 0;
	}

	err = rk_power_model_simple_init(dev);
	if (err && err != -ENODEV && err != -EPROBE_DEFER) {
		dev_err(dev,
			"Failed to initialize simple power model (%d)\n",
			err);
		err = 0;
		//return err;
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
#endif

	return err;
}
#endif

#define TO_IMG_ERR(err) ((err == -EPROBE_DEFER) ? PVRSRV_ERROR_PROBE_DEFER : PVRSRV_ERROR_INIT_FAILURE)

PVRSRV_ERROR InitDVFS(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_DVFS_DEVICE        *psDVFSDevice = NULL;
	IMG_DVFS_DEVICE_CFG    *psDVFSDeviceCfg = NULL;
	IMG_DVFS_GOVERNOR_CFG  *psDVFSGovernorCfg = NULL;
	RGX_TIMING_INFORMATION *psRGXTimingInfo = NULL;
	struct device          *psDev = psDeviceNode->psDevConfig->pvOSDevice;
	unsigned long           min_freq = 0, max_freq = 0, min_volt = 0;
	PVRSRV_ERROR            eError;
	int                     err;

	if (gpsDeviceNode)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "DVFS already initialised for device node %p",
				 gpsDeviceNode));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	gpsDeviceNode = psDeviceNode;
	psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	psDVFSDeviceCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSDeviceCfg;
	psDVFSGovernorCfg = &psDeviceNode->psDevConfig->sDVFS.sDVFSGovernorCfg;
	psRGXTimingInfo = ((RGX_DATA *)psDeviceNode->psDevConfig->hDevData)->psRGXTimingInfo;

	eError = RGXRegisterGpuUtilStats(&psDVFSDevice->hGpuUtilUserDVFS);
	if (eError != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR, "Failed to register to the GPU utilisation stats, %d", eError));
		return eError;
	}


#if defined(CONFIG_OF) && defined(CONFIG_PM_OPP)
	/* Register the OPPs if they are available in device tree */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) \
	|| defined(LSK_OPPV2_BACKPORT)
	err = dev_pm_opp_of_add_table(psDev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	err = of_init_opp_table(psDev);
#else
	err = 0;
#endif /* LINUX_VERSION_CODE */
#endif
	if (err) {
		PVR_DPF((PVR_DBG_ERROR, "Failed to init opp table from devicetree, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}

	if (psDVFSDeviceCfg->pasOPPTable) {
		err = FillOPPTable(psDev);
		if (err) {
			PVR_DPF((PVR_DBG_ERROR, "Failed to fill OPP table with data, %d", err));
			eError = TO_IMG_ERR(err);
			goto err_exit;
		}
	}

	err = GetOPPValues(psDev, &min_freq, &min_volt, &max_freq);
	if (err) {
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
			&img_devfreq_dev_profile, "simple_ondemand",
			&psDVFSDevice->data);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	psDVFSDevice->psDevFreq = devfreq_add_device(psDev,
			&img_devfreq_dev_profile, "simple_ondemand",
			&psDVFSDevice->data);
#else
	psDVFSDevice->psDevFreq = devfreq_add_device(psDev,
			&img_devfreq_dev_profile, &devfreq_simple_ondemand,
			&psDVFSDevice->data);
#endif

	if (IS_ERR(psDVFSDevice->psDevFreq))
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to add as devfreq device %p, %ld",
			psDVFSDevice->psDevFreq, PTR_ERR(psDVFSDevice->psDevFreq)));
		eError = TO_IMG_ERR(PTR_ERR(psDVFSDevice->psDevFreq));
		goto err_exit;
	}

	eError = SuspendDVFS();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInit: Failed to suspend DVFS"));
		goto err_exit;
	}

	psDVFSDevice->psDevFreq->min_freq = min_freq;
	psDVFSDevice->psDevFreq->max_freq = max_freq;

	err = devfreq_register_opp_notifier(psDev, psDVFSDevice->psDevFreq);
	if (err) {
		PVR_DPF((PVR_DBG_ERROR, "Failed to register opp notifier, %d", err));
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}

#if defined(CONFIG_DEVFREQ_THERMAL)
	err = RegisterCoolingDevice(psDev, psDVFSDevice,
			psDVFSDeviceCfg->psPowerOps);
	if (err) {
		eError = TO_IMG_ERR(err);
		goto err_exit;
	}
#endif

	PVR_TRACE(("PVR DVFS activated: %lu-%lu Hz, Period: %ums", min_freq,
			max_freq, psDVFSDeviceCfg->ui32PollMs));

	return PVRSRV_OK;

err_exit:
	DeinitDVFS(psDeviceNode);
	return eError;
}

void DeinitDVFS(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_DVFS_DEVICE      *psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	struct device        *psDev = psDeviceNode->psDevConfig->pvOSDevice;
	IMG_INT32            i32Error;

	PVR_ASSERT(psDeviceNode == gpsDeviceNode);

	if (!psDVFSDevice)
		return;

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
#endif

		psDVFSDevice->psDevFreq = NULL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) && \
     LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	kfree(img_devfreq_dev_profile.freq_table);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	dev_pm_opp_of_remove_table(psDev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	of_free_opp_table(psDev);
#endif

	RGXUnregisterGpuUtilStats(psDVFSDevice->hGpuUtilUserDVFS);
	psDVFSDevice->hGpuUtilUserDVFS = NULL;

	gpsDeviceNode = NULL;
}

PVRSRV_ERROR SuspendDVFS(void)
{
	IMG_DVFS_DEVICE		*psDVFSDevice = &gpsDeviceNode->psDevConfig->sDVFS.sDVFSDevice;

	psDVFSDevice->bEnabled = IMG_FALSE;

	return PVRSRV_OK;
}

PVRSRV_ERROR ResumeDVFS(void)
{
	IMG_DVFS_DEVICE		*psDVFSDevice = &gpsDeviceNode->psDevConfig->sDVFS.sDVFSDevice;

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
	/* Not supported in GuestOS drivers */
	psDVFSDevice->bEnabled = IMG_FALSE;
#else
	psDVFSDevice->bEnabled = IMG_TRUE;
#endif

	return PVRSRV_OK;
}

