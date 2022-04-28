/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
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

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/thermal.h>
#if defined(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif

#include "physheap.h"
#include "pvrsrv_device.h"
#include "rgxdevice.h"
#include "syscommon.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif

#include "mt8173_mfgsys.h"

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS 10
#define RGX_HW_CORE_CLOCK_SPEED 395000000

/* Setup RGX specific timing data */
static RGX_TIMING_INFORMATION gsRGXTimingInfo = {
	.ui32CoreClockSpeed = RGX_HW_CORE_CLOCK_SPEED,
	.bEnableActivePM = IMG_TRUE,
	.ui32ActivePMLatencyms = SYS_RGX_ACTIVE_POWER_LATENCY_MS,
	.bEnableRDPowIsland = IMG_TRUE,
};

static RGX_DATA gsRGXData = {
	.psRGXTimingInfo = &gsRGXTimingInfo,
};

static PVRSRV_DEVICE_CONFIG	gsDevice;

typedef struct
{
	IMG_UINT32 ui32IRQ;
	PFN_LISR pfnLISR;
	void *pvLISRData;
} LISR_WRAPPER_DATA;

static irqreturn_t MTKLISRWrapper(int iIrq, void *pvData)
{
	LISR_WRAPPER_DATA *psWrapperData = pvData;

	if (psWrapperData->pfnLISR(psWrapperData->pvLISRData))
	{
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * CPU to Device physical address translation
 */
static
void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				   IMG_UINT32 ui32NumOfAddr,
				   IMG_DEV_PHYADDR *psDevPAddr,
				   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
	}
}

/*
 * Device to CPU physical address translation
 */
static
void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				   IMG_UINT32 ui32NumOfAddr,
				   IMG_CPU_PHYADDR *psCpuPAddr,
				   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
	}
}

static PHYS_HEAP_FUNCTIONS gsPhysHeapFuncs = {
	.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr,
};

static PHYS_HEAP_CONFIG gsPhysHeapConfig = {
	.pszPDumpMemspaceName = "SYSMEM",
	.eType = PHYS_HEAP_TYPE_UMA,
	.psMemFuncs = &gsPhysHeapFuncs,
	.hPrivData = NULL,
	.ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL,
};

static PVRSRV_ERROR MTKSysDevPrePowerState(
		IMG_HANDLE hSysData,
		PVRSRV_SYS_POWER_STATE eNewPowerState,
		PVRSRV_SYS_POWER_STATE eCurrentPowerState,
		IMG_BOOL bForced)
{
	struct mtk_mfg *mfg = hSysData;

	mtk_mfg_debug("MTKSysDevPrePowerState (%d->%d), bForced = %d\n",
		      eCurrentPowerState, eNewPowerState, bForced);

	mutex_lock(&mfg->set_power_state);

	if ((PVRSRV_SYS_POWER_STATE_OFF == eNewPowerState) &&
	    (PVRSRV_SYS_POWER_STATE_ON == eCurrentPowerState))
		mtk_mfg_disable(mfg);

	mutex_unlock(&mfg->set_power_state);
	return PVRSRV_OK;
}

static PVRSRV_ERROR MTKSysDevPostPowerState(
		IMG_HANDLE hSysData,
		PVRSRV_SYS_POWER_STATE eNewPowerState,
		PVRSRV_SYS_POWER_STATE eCurrentPowerState,
		IMG_BOOL bForced)
{
	struct mtk_mfg *mfg = hSysData;
	PVRSRV_ERROR ret;

	mtk_mfg_debug("MTKSysDevPostPowerState (%d->%d)\n",
		      eCurrentPowerState, eNewPowerState);

	mutex_lock(&mfg->set_power_state);

	if ((PVRSRV_SYS_POWER_STATE_ON == eNewPowerState) &&
	    (PVRSRV_SYS_POWER_STATE_OFF == eCurrentPowerState)) {
		if (mtk_mfg_enable(mfg)) {
			ret = PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE;
			goto done;
		}
	}

	ret = PVRSRV_OK;
done:
	mutex_unlock(&mfg->set_power_state);

	return ret;
}

#ifdef SUPPORT_LINUX_DVFS
#if defined(CONFIG_DEVFREQ_THERMAL)

#define FALLBACK_STATIC_TEMPERATURE 65000

/* Temperatures on power over-temp-and-voltage curve (C) */
static const int vt_temperatures[] = { 25, 45, 65, 85, 105 };

/* Voltages on power over-temp-and-voltage curve (mV) */
static const int vt_voltages[] = { 900, 1000, 1130 };

#define POWER_TABLE_NUM_TEMP ARRAY_SIZE(vt_temperatures)
#define POWER_TABLE_NUM_VOLT ARRAY_SIZE(vt_voltages)

static const unsigned int
power_table[POWER_TABLE_NUM_VOLT][POWER_TABLE_NUM_TEMP] = {
	/*   25     45      65      85     105 */
	{ 14540, 35490,  60420, 120690, 230000 },  /*  900 mV */
	{ 21570, 41910,  82380, 159140, 298620 },  /* 1000 mV */
	{ 32320, 72950, 111320, 209290, 382700 },  /* 1130 mV */
};

/** Frequency and Power in Khz and mW respectively */
static const int f_range[] = {253500, 299000, 396500, 455000, 494000, 598000};
static const IMG_UINT32 max_dynamic_power[] = {612, 722, 957, 1100, 1194, 1445};

static u32 interpolate(int value, const int *x, const unsigned int *y, int len)
{
	u64 tmp64;
	u32 dx;
	u32 dy;
	int i, ret;

	if (value <= x[0])
		return y[0];
	if (value >= x[len - 1])
		return y[len - 1];

	for (i = 1; i < len - 1; i++) {
		/* If value is identical, no need to interpolate */
		if (value == x[i])
			return y[i];
		if (value < x[i])
			break;
	}

	/* Linear interpolation between the two (x,y) points */
	dy = y[i] - y[i - 1];
	dx = x[i] - x[i - 1];

	tmp64 = value - x[i - 1];
	tmp64 *= dy;
	do_div(tmp64, dx);
	ret = y[i - 1] + tmp64;

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static unsigned long mtk_mfg_get_static_power(struct devfreq *df,
					      unsigned long voltage)
#else
static unsigned long mtk_mfg_get_static_power(unsigned long voltage)
#endif
{
	struct mtk_mfg *mfg = gsDevice.hSysData;
	struct thermal_zone_device *tz = mfg->tz;
	unsigned long power;
#if !defined(CHROMIUMOS_KERNEL) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	unsigned long temperature = FALLBACK_STATIC_TEMPERATURE;
#else
	int temperature = FALLBACK_STATIC_TEMPERATURE;
#endif
	int low_idx = 0, high_idx = POWER_TABLE_NUM_VOLT - 1;
	int i;

	if (!tz)
		return 0;

	if (tz->ops->get_temp(tz, &temperature))
		dev_warn(mfg->dev, "Failed to read temperature\n");
	do_div(temperature, 1000);

	for (i = 0; i < POWER_TABLE_NUM_VOLT; i++) {
		if (voltage <= vt_voltages[POWER_TABLE_NUM_VOLT - 1 - i])
			high_idx = POWER_TABLE_NUM_VOLT - 1 - i;

		if (voltage >= vt_voltages[i])
			low_idx = i;
	}

	if (low_idx == high_idx) {
		power = interpolate(temperature,
				    vt_temperatures,
				    &power_table[low_idx][0],
				    POWER_TABLE_NUM_TEMP);
	} else {
		unsigned long dvt =
				vt_voltages[high_idx] - vt_voltages[low_idx];
		unsigned long power1, power2;

		power1 = interpolate(temperature,
				     vt_temperatures,
				     &power_table[high_idx][0],
				     POWER_TABLE_NUM_TEMP);

		power2 = interpolate(temperature,
				     vt_temperatures,
				     &power_table[low_idx][0],
				     POWER_TABLE_NUM_TEMP);

		power = (power1 - power2) * (voltage - vt_voltages[low_idx]);
		do_div(power, dvt);
		power += power2;
	}

	/* convert to mw */
	do_div(power, 1000);

	mtk_mfg_debug("mtk_mfg_get_static_power: %lu at Temperature %d\n",
		      power, temperature);
	return power;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static unsigned long mtk_mfg_get_dynamic_power(struct devfreq *df,
					       unsigned long freq,
					       unsigned long voltage)
#else
static unsigned long mtk_mfg_get_dynamic_power(unsigned long freq,
					       unsigned long voltage)
#endif
{
	#define NUM_RANGE  ARRAY_SIZE(f_range)
	/** Frequency and Power in Khz and mW respectively */
	IMG_INT32 i, low_idx = 0, high_idx = NUM_RANGE - 1;
	IMG_UINT32 power;

	for (i = 0; i < NUM_RANGE; i++) {
		if (freq <= f_range[NUM_RANGE - 1 - i])
			high_idx = NUM_RANGE - 1 - i;

		if (freq >= f_range[i])
			low_idx = i;
	}

	if (low_idx == high_idx) {
		power = max_dynamic_power[low_idx];
	} else {
		IMG_UINT32 f_interval = f_range[high_idx] - f_range[low_idx];
		IMG_UINT32 p_interval = max_dynamic_power[high_idx] -
				max_dynamic_power[low_idx];

		power = p_interval * (freq - f_range[low_idx]);
		do_div(power, f_interval);
		power += max_dynamic_power[low_idx];
	}

	power = (IMG_UINT32)div_u64((IMG_UINT64)power * voltage * voltage,
				    1000000UL);

	return power;
	#undef NUM_RANGE
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0))
static int mtk_mfg_get_real_power(struct devfreq *df,
						   u32 *power,
					       unsigned long freq,
					       unsigned long voltage)
{
	if (!df || !power)
		return -EINVAL;

	*power = mtk_mfg_get_static_power(df, voltage) +
		     mtk_mfg_get_dynamic_power(df, freq, voltage);

	return 0;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0) */

static struct devfreq_cooling_power sPowerOps = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0))
	.get_static_power = mtk_mfg_get_static_power,
	.get_dynamic_power = mtk_mfg_get_dynamic_power,
#else
	.get_real_power = mtk_mfg_get_real_power,
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) */
};
#endif

static void SetFrequency(IMG_UINT32 freq)
{
	struct mtk_mfg *mfg = gsDevice.hSysData;

	/* freq is in Hz */
	mtk_mfg_freq_set(mfg, freq);
}

static void SetVoltage(IMG_UINT32 volt)
{
	struct mtk_mfg *mfg = gsDevice.hSysData;

	mtk_mfg_volt_set(mfg, volt);
}
#endif

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	struct device *dev = pvOSDevice;
	struct mtk_mfg *mfg;

	if (gsDevice.pvOSDevice)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	mfg = mtk_mfg_create(dev);
	if (IS_ERR(mfg)) {
		if (PTR_ERR(mfg) == -EPROBE_DEFER)
			return PVRSRV_ERROR_PROBE_DEFER;
		else
			return PVRSRV_ERROR_INIT_FAILURE;
	}

	dma_set_mask(dev, DMA_BIT_MASK(33));

	/* Make sure everything we don't care about is set to 0 */
	memset(&gsDevice, 0, sizeof(gsDevice));

	/* Setup RGX device */
	gsDevice.pvOSDevice = pvOSDevice;
	gsDevice.pszName = "mt8173";
	gsDevice.pszVersion = NULL;

	/* Device's physical heaps */
	gsDevice.pasPhysHeaps = &gsPhysHeapConfig;
	gsDevice.ui32PhysHeapCount = 1;

	gsDevice.ui32IRQ = mfg->rgx_irq;

	gsDevice.sRegsCpuPBase.uiAddr = mfg->rgx_start;
	gsDevice.ui32RegsSize = mfg->rgx_size;

#ifdef SUPPORT_LINUX_DVFS
	gsDevice.sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_TRUE;
	gsDevice.sDVFS.sDVFSDeviceCfg.pfnSetFrequency = SetFrequency;
	gsDevice.sDVFS.sDVFSDeviceCfg.pfnSetVoltage = SetVoltage;
	gsDevice.sDVFS.sDVFSDeviceCfg.ui32PollMs = MTK_DVFS_SWITCH_INTERVAL;
#if defined(CONFIG_DEVFREQ_THERMAL)
	gsDevice.sDVFS.sDVFSDeviceCfg.psPowerOps = &sPowerOps;
#endif

	gsDevice.sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 90;
	gsDevice.sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif

	/* power management on HW system */
	gsDevice.pfnPrePowerState = MTKSysDevPrePowerState;
	gsDevice.pfnPostPowerState = MTKSysDevPostPowerState;

	/* clock frequency */
	gsDevice.pfnClockFreqGet = NULL;

	gsDevice.hDevData = &gsRGXData;
	gsDevice.hSysData = mfg;

	gsDevice.bHasFBCDCVersion31 = IMG_FALSE;
	gsDevice.bDevicePA0IsValid  = IMG_FALSE;

	/* device error notify callback function */
	gsDevice.pfnSysDevErrorNotify = NULL;

	*ppsDevConfig = &gsDevice;

#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	return PVRSRV_OK;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	struct mtk_mfg *mfg = psDevConfig->hSysData;

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	mtk_mfg_destroy(mfg);

	psDevConfig->pvOSDevice = NULL;
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData)
{
	LISR_WRAPPER_DATA *psWrapperData;

	PVR_UNREFERENCED_PARAMETER(hSysData);

	psWrapperData = kmalloc(sizeof(*psWrapperData), GFP_KERNEL);
	if (!psWrapperData)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psWrapperData->ui32IRQ = ui32IRQ;
	psWrapperData->pfnLISR = pfnLISR;
	psWrapperData->pvLISRData = pvData;

	if (request_irq(ui32IRQ, MTKLISRWrapper, IRQF_TRIGGER_LOW, pszName,
					psWrapperData))
	{
		kfree(psWrapperData);

		return PVRSRV_ERROR_UNABLE_TO_REGISTER_ISR_HANDLER;
	}

	*phLISRData = (IMG_HANDLE) psWrapperData;

	return PVRSRV_OK;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	LISR_WRAPPER_DATA *psWrapperData = hLISRData;

	free_irq(psWrapperData->ui32IRQ, psWrapperData);

	OSFreeMem(psWrapperData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
			  DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			  void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);

	return PVRSRV_OK;
}
