/*************************************************************************/ /*!
@Title          System Configuration
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
@Description    System Configuration functions
*/ /**************************************************************************/

#if defined(SUPPORT_ION)
#include "ion_sys.h"
#endif /* defined(SUPPORT_ION) */

#if defined(SUPPORT_PDVFS)
#include "rgxpdvfs.h"
#endif


#include <linux/clkdev.h>
#include <linux/hardirq.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
#include <linux/rockchip/dvfs.h>
#endif
#include "power.h"
#include "rk_init_v2.h"
#include "pvrsrv_device.h"
#include "syscommon.h"
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
#include <linux/clk-private.h>
#else
#include <linux/clk-provider.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#endif
#include <linux/devfreq_cooling.h>
#include <linux/thermal.h>
#include "rgxdevice.h"

#if !defined(PVR_DVFS) && !defined(SUPPORT_PDVFS)
typedef struct
{
	IMG_UINT32			ui32Volt;
	IMG_UINT32			ui32Freq;
} IMG_OPP;
#endif

static const IMG_OPP rkOPPTable[] =
{
#if defined(PVR_DVFS) || defined(SUPPORT_PDVFS)
	{ 925,  100000000},
	{ 925,  160000000},
	{ 1025,  266000000},
	{ 1075,  350000000},
	{ 1125,  400000000},
	{ 1200,  500000000},
#else
	{ 925,  100000000},
	{ 925,  160000000},
	{ 1025,  266000000},
	{ 1075,  350000000},
	{ 1125,  400000000},
	{ 1200,  500000000},
#endif
};

#define RGX_DVFS_STEP (sizeof(rkOPPTable) / sizeof(rkOPPTable[0]))


#if defined(PVR_DVFS)
	#define DEFAULT_MIN_VF_LEVEL 0
#else
	#define DEFAULT_MIN_VF_LEVEL 4
#endif

//static IMG_UINT32 min_vf_level_val     = DEFAULT_MIN_VF_LEVEL;
//static IMG_UINT32 max_vf_level_val     = RGX_DVFS_STEP - 1;

static struct rk_context *g_platform = NULL;


#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
static int rk33_clk_set_normal_node(struct clk* node, unsigned long rate)
{
	int ret = 0;

	if (!node) {
		printk("rk33_clk_set_normal_node error \r\n");
		ret = -1;
	}
	ret = clk_set_rate(node, rate);
	if (ret)
		printk("clk_set_rate error \r\n");

	return ret;
}

static int rk33_clk_set_dvfs_node(struct dvfs_node *node, unsigned long rate)
{
	int ret = 0;

	if (!node) {
		printk("rk33_clk_set_dvfs_node error \r\n");
		ret = -1;
	}
	ret = dvfs_clk_set_rate(node, rate);
	if (ret)
		printk("dvfs_clk_set_rate error \r\n");

	return ret;
}

void rkSetFrequency(IMG_UINT32 ui32Frequency)
{
	int ret = 0;
	unsigned long old_freq;
	PVRSRV_ERROR err;

	if (NULL == g_platform)
		panic("oops");

#if USE_PVR_SPEED_CHANGE
	err = PVRSRVDevicePreClockSpeedChange(g_platform->dev_config->psDevNode, IMG_TRUE, NULL);
	if (err != PVRSRV_OK) {
		return;
	}
#endif

	if (!g_platform->aclk_gpu_mem || !g_platform->aclk_gpu_cfg) {
		printk("aclk_gpu_mem or aclk_gpu_cfg not init\n");
		return;
	}
	if (!g_platform->gpu_clk_node && !g_platform->clk_gpu) {
		pr_err("%s:clk_gpu & gpu_clk_node is null\n", __func__);
		return;
	}

	if (g_platform->gpu_clk_node)
		old_freq = clk_get_rate(g_platform->gpu_clk_node->clk);
	else if (g_platform->clk_gpu)
		old_freq = clk_get_rate(g_platform->clk_gpu);

	if (old_freq > ui32Frequency) {
		if (g_platform->gpu_clk_node)
			rk33_clk_set_dvfs_node(g_platform->gpu_clk_node, ui32Frequency);
		else if (g_platform->clk_gpu)
			rk33_clk_set_normal_node(g_platform->clk_gpu, ui32Frequency);
	}

	rk33_clk_set_normal_node(g_platform->aclk_gpu_mem, ui32Frequency);
	rk33_clk_set_normal_node(g_platform->aclk_gpu_cfg, ui32Frequency);

	if (old_freq < ui32Frequency) {
		if (g_platform->gpu_clk_node)
			rk33_clk_set_dvfs_node(g_platform->gpu_clk_node, ui32Frequency);
		else if (g_platform->clk_gpu)
			rk33_clk_set_normal_node(g_platform->clk_gpu, ui32Frequency);
	}

#if USE_PVR_SPEED_CHANGE
	PVRSRVDevicePostClockSpeedChange(g_platform->dev_config->psDevNode, IMG_TRUE, NULL);
#endif
	
}

//undefine gpu_reg
void rkSetVoltage(IMG_UINT32 ui32Volt)
{
	if (NULL == g_platform)
		panic("oops");

	if(regulator_set_voltage(g_platform->gpu_reg, ui32Volt, ui32Volt) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to set gpu power voltage=%d!",ui32Volt));
	}
}
#else
void rkSetFrequency(IMG_UINT32 ui32Frequency)
{
	int ret = 0;
	unsigned int old_freq, old_volt;

	if (NULL == g_platform)
		panic("oops");

	old_freq = clk_get_rate(g_platform->sclk_gpu_core);
	old_volt = regulator_get_voltage(g_platform->gpu_reg);

	ret = clk_set_rate(g_platform->aclk_gpu_mem, ui32Frequency);
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "failed to set aclk_gpu_mem rate: %d\n", ret));
		if (old_volt > 0)
			regulator_set_voltage(g_platform->gpu_reg, old_volt, INT_MAX);
		return;
	}
	ret = clk_set_rate(g_platform->aclk_gpu_cfg, ui32Frequency);
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "failed to set aclk_gpu_cfg rate: %d\n", ret));
		clk_set_rate(g_platform->aclk_gpu_mem, old_freq);
		if (old_volt > 0)
			regulator_set_voltage(g_platform->gpu_reg, old_volt, INT_MAX);
		return;
	}
	ret = clk_set_rate(g_platform->sclk_gpu_core, ui32Frequency);
	if (ret) {
		PVR_DPF((PVR_DBG_ERROR, "failed to set sclk_gpu_core rate: %d\n", ret));
		clk_set_rate(g_platform->aclk_gpu_mem, old_freq);
		clk_set_rate(g_platform->aclk_gpu_cfg, old_freq);
		if (old_volt > 0)
			regulator_set_voltage(g_platform->gpu_reg, old_volt, INT_MAX);
		return;
	}
}

void rkSetVoltage(IMG_UINT32 ui32Volt)
{
	if (NULL == g_platform)
		panic("oops");

	if(regulator_set_voltage(g_platform->gpu_reg, ui32Volt, INT_MAX) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to set gpu power voltage=%d!",ui32Volt));
	}
}
#endif


static void RgxEnableClock(struct rk_context *platform)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (!platform->gpu_clk_node && !platform->clk_gpu)
	{
		printk("gpu_clk_node and clk_gpu are both null\n");
		return;
	}
#else
	if (!platform->sclk_gpu_core)
	{
		printk("sclk_gpu_core is null\n");
		return;
	}
#endif

	if (platform->aclk_gpu_mem && platform->aclk_gpu_cfg && !platform->gpu_active) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		if (platform->gpu_clk_node)
			dvfs_clk_prepare_enable(platform->gpu_clk_node);
		else if (platform->clk_gpu)
			clk_prepare_enable(platform->clk_gpu);
#else
		clk_prepare_enable(platform->sclk_gpu_core);
#endif
		clk_prepare_enable(platform->aclk_gpu_mem);
		clk_prepare_enable(platform->aclk_gpu_cfg);
		platform->gpu_active = IMG_TRUE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable clock!"));
	}
}

static void RgxDisableClock(struct rk_context *platform)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (!platform->gpu_clk_node && !platform->clk_gpu) {
		printk("gpu_clk_node and clk_gpu are both null\n");
		return;
	}
#else
	if (!platform->sclk_gpu_core) {
		printk("sclk_gpu_core is null");
		return;
	}
#endif

	if (platform->aclk_gpu_mem && platform->aclk_gpu_cfg && platform->gpu_active) {
		clk_disable_unprepare(platform->aclk_gpu_cfg);
		clk_disable_unprepare(platform->aclk_gpu_mem);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
		if (platform->gpu_clk_node)
			dvfs_clk_disable_unprepare(platform->gpu_clk_node);
		else if (platform->clk_gpu)
			clk_disable_unprepare(platform->clk_gpu);
#else
		clk_disable_unprepare(platform->sclk_gpu_core);
#endif
		platform->gpu_active = IMG_FALSE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to disable clock!"));
	}
}


#if OPEN_GPU_PD
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
/*
 * The power management
 * software must power down pd_gpu_1 before power down pd_gpu_0,
 * and power up pd_gpu_1 after power up pd_gpu_0.
 */
static void RgxEnablePower(struct rk_context *platform)
{
	if (!platform->bEnablePd && platform->pd_gpu_0 && platform->pd_gpu_1) {
		clk_prepare_enable(platform->pd_gpu_0);
		clk_prepare_enable(platform->pd_gpu_1);
		platform->bEnablePd = IMG_TRUE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}

static void RgxDisablePower(struct rk_context *platform)
{
	if (platform->bEnablePd && platform->pd_gpu_0 && platform->pd_gpu_1) {
		clk_disable_unprepare(platform->pd_gpu_1);
		clk_disable_unprepare(platform->pd_gpu_0);
		platform->bEnablePd = IMG_FALSE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}
#else
static void RgxEnablePower(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;
	if (!platform->bEnablePd) {
		pm_runtime_get_sync(dev);
		platform->bEnablePd = IMG_TRUE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}

static void RgxDisablePower(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;
	if (platform->bEnablePd) {
		pm_runtime_put(dev);
		platform->bEnablePd = IMG_FALSE;
	} else {
		PVR_DPF((PVR_DBG_WARNING, "Failed to enable gpu_pd clock!"));
	}
}
#endif
#endif //end of OPEN_GPU_PD

void RgxResume(struct rk_context *platform)
{
#if OPEN_GPU_PD
	RgxEnablePower(platform);
#endif
	RgxEnableClock(platform);
 }

void RgxSuspend(struct rk_context *platform)
{
	RgxDisableClock(platform);
#if OPEN_GPU_PD
	RgxDisablePower(platform);
#endif

}

PVRSRV_ERROR RkPrePowerState(IMG_HANDLE hSysData,
							 PVRSRV_DEV_POWER_STATE eNewPowerState,
							 PVRSRV_DEV_POWER_STATE eCurrentPowerState,
							 IMG_BOOL bForced)
{
	struct rk_context *platform = (struct rk_context *)hSysData;

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON)
		RgxResume(platform);
	return PVRSRV_OK;
	
}

PVRSRV_ERROR RkPostPowerState(IMG_HANDLE hSysData,
							  PVRSRV_DEV_POWER_STATE eNewPowerState,
							  PVRSRV_DEV_POWER_STATE eCurrentPowerState,
							  IMG_BOOL bForced)
{
	struct rk_context *platform = (struct rk_context *)hSysData;

	if (eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF)
		RgxSuspend(platform);
	return PVRSRV_OK;
}

#if defined(CONFIG_DEVFREQ_THERMAL) && defined(PVR_DVFS)
/*
 * This model is primarily designed for the Juno platform. It may not be
 * suitable for other platforms.
 */

#define FALLBACK_STATIC_TEMPERATURE 55000

static u32 dynamic_coefficient;
static u32 static_coefficient;
static s32 ts[4];
static struct thermal_zone_device *gpu_tz;

static unsigned long model_static_power(unsigned long voltage)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
	unsigned long temperature;
#else
	int temperature;
#endif
	unsigned long temp;
	unsigned long temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	if (gpu_tz) {
		int ret;

		ret = gpu_tz->ops->get_temp(gpu_tz, &temperature);
		if (ret) {
			pr_warn_ratelimited("Error reading temperature for gpu thermal zone: %d\n",
					ret);
			temperature = FALLBACK_STATIC_TEMPERATURE;
		}
	} else {
		temperature = FALLBACK_STATIC_TEMPERATURE;
	}

	/* Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power.
	 */
	temp = temperature / 1000;
	temp_squared = temp * temp;
	temp_cubed = temp_squared * temp;
	temp_scaling_factor =
			(ts[3] * temp_cubed)
			+ (ts[2] * temp_squared)
			+ (ts[1] * temp)
			+ ts[0];

	return (((static_coefficient * voltage_cubed) >> 20)
			* temp_scaling_factor)
				/ 1000000;
}

static unsigned long model_dynamic_power(unsigned long freq,
		unsigned long voltage)
{
	/* The inputs: freq (f) is in Hz, and voltage (v) in mV.
	 * The coefficient (c) is in mW/(MHz mV mV).
	 *
	 * This function calculates the dynamic power after this formula:
	 * Pdyn (mW) = c (mW/(MHz*mV*mV)) * v (mV) * v (mV) * f (MHz)
	 */
	const unsigned long v2 = (voltage * voltage) / 1000; /* m*(V*V) */
	const unsigned long f_mhz = freq / 1000000; /* MHz */

	return (dynamic_coefficient * v2 * f_mhz) / 1000000; /* mW */
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
struct devfreq_cooling_ops rk_power_model_simple_ops = {
#else
struct devfreq_cooling_power rk_power_model_simple_ops = {
#endif
	.get_static_power = model_static_power,
	.get_dynamic_power = model_dynamic_power,
};

int rk_power_model_simple_init(struct device *dev)
{
	struct device_node *power_model_node;
	const char *tz_name;
	u32 static_power, dynamic_power;
	u32 voltage, voltage_squared, voltage_cubed, frequency;

	power_model_node = of_get_child_by_name(dev->of_node,
			"power_model");
	if (!power_model_node) {
		dev_err(dev, "could not find power_model node\n");
		return -ENODEV;
	}
	if (!of_device_is_compatible(power_model_node,
			"arm,mali-simple-power-model")) {
		dev_err(dev, "power_model incompatible with simple power model\n");
		return -ENODEV;
	}

	if (of_property_read_string(power_model_node, "thermal-zone",
			&tz_name)) {
		dev_err(dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	gpu_tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(gpu_tz)) {
		pr_warn_ratelimited("Error getting gpu thermal zone (%ld), not yet ready?\n",
				PTR_ERR(gpu_tz));
		gpu_tz = NULL;

		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(power_model_node, "static-power",
			&static_power)) {
		dev_err(dev, "static-power in power_model not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "dynamic-power",
			&dynamic_power)) {
		dev_err(dev, "dynamic-power in power_model not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "voltage",
			&voltage)) {
		dev_err(dev, "voltage in power_model not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "frequency",
			&frequency)) {
		dev_err(dev, "frequency in power_model not available\n");
		return -EINVAL;
	}
	voltage_squared = (voltage * voltage) / 1000;
	voltage_cubed = voltage * voltage * voltage;
	static_coefficient = (static_power << 20) / (voltage_cubed >> 10);
	dynamic_coefficient = (((dynamic_power * 1000) / voltage_squared)
			* 1000) / frequency;

	if (of_property_read_u32_array(power_model_node, "ts", (u32 *)ts, 4)) {
		dev_err(dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	return 0;
}
#endif

void RgxRkUnInit(struct rk_context *platform)
{
	struct device *dev = (struct device *)platform->dev_config->pvOSDevice;

	RgxSuspend(platform);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (platform->gpu_clk_node) {
		clk_put_dvfs_node(platform->gpu_clk_node);
		platform->gpu_clk_node = NULL;
	} else if (platform->clk_gpu) {
		devm_clk_put(dev, platform->clk_gpu);
		platform->clk_gpu = NULL;
	}
#else
	if (platform->sclk_gpu_core) {
		devm_clk_put(dev, platform->sclk_gpu_core);
		platform->sclk_gpu_core = NULL;
	}
#endif

	if (platform->aclk_gpu_cfg) {
		devm_clk_put(dev, platform->aclk_gpu_cfg);
		platform->aclk_gpu_cfg = NULL;
	}
	if (platform->aclk_gpu_mem) {
		devm_clk_put(dev, platform->aclk_gpu_mem);
		platform->aclk_gpu_mem = NULL;
	}
#if OPEN_GPU_PD
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	if (platform->pd_gpu_1) {
		devm_clk_put(dev, platform->pd_gpu_1);
		platform->pd_gpu_1 = NULL;
	}
	if (platform->pd_gpu_0) {
		devm_clk_put(dev, platform->pd_gpu_0);
		platform->pd_gpu_0 = NULL;
	}
#else
	pm_runtime_disable(dev);
#endif
#endif
	devm_kfree(dev, platform);

}

struct rk_context *RgxRkInit(PVRSRV_DEVICE_CONFIG* psDevConfig)
{
	struct device *dev = (struct device *)psDevConfig->pvOSDevice;
	struct rk_context *platform;
	RGX_DATA* psRGXData = (RGX_DATA*)psDevConfig->hDevData;

	platform = devm_kzalloc(dev, sizeof(struct rk_context), GFP_KERNEL);
	if (NULL == platform) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to kzalloc rk_context"));
		return NULL;
	}

	g_platform = platform;

	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;

	PVR_DPF((PVR_DBG_ERROR, "%s: dma_mask = %llx", __func__, dev->coherent_dma_mask));

	platform->dev_config = psDevConfig;
	platform->gpu_active = IMG_FALSE;
	
#if defined(PVR_DVFS) || defined(SUPPORT_PDVFS)
	//psDevConfig->sDVFS.sDVFSDeviceCfg.pasOPPTable = rkOPPTable;
	//psDevConfig->sDVFS.sDVFSDeviceCfg.ui32OPPTableSize = RGX_DVFS_STEP;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetFrequency = rkSetFrequency;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetVoltage = rkSetVoltage;
#if defined(CONFIG_DEVFREQ_THERMAL)
	psDevConfig->sDVFS.sDVFSDeviceCfg.psPowerOps = &rk_power_model_simple_ops;
#endif
#endif

#if OPEN_GPU_PD
	platform->bEnablePd = IMG_FALSE;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	platform->pd_gpu_0 = devm_clk_get(dev, "pd_gpu_0");
	if (IS_ERR_OR_NULL(platform->pd_gpu_0)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find pd_gpu_0 clock source"));
		goto fail0;
	}

	platform->pd_gpu_1 = devm_clk_get(dev, "pd_gpu_1");
	if (IS_ERR_OR_NULL(platform->pd_gpu_1)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find pd_gpu_1 clock source"));
		goto fail1;
	}
#else
	pm_runtime_enable(dev);
#endif
#endif

	platform->aclk_gpu_mem = devm_clk_get(dev, "aclk_gpu_mem");
	if (IS_ERR_OR_NULL(platform->aclk_gpu_mem)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find aclk_gpu_mem clock source"));
		goto fail2;
	}

	platform->aclk_gpu_cfg = devm_clk_get(dev, "aclk_gpu_cfg");
	if (IS_ERR_OR_NULL(platform->aclk_gpu_cfg)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find aclk_gpu_cfg clock source"));
		goto fail3;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	platform->gpu_clk_node	= clk_get_dvfs_node("clk_gpu");
	if (IS_ERR_OR_NULL(platform->gpu_clk_node))
	{
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: GPU Dvfs is disabled"));
		platform->clk_gpu = devm_clk_get(dev, "clk_gpu");
		if (IS_ERR_OR_NULL(platform->clk_gpu)) {
			PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find clk_gpu clock source"));
			goto fail4;
		} else {
			rk33_clk_set_normal_node(platform->clk_gpu, RK33_DEFAULT_CLOCK);
		}
	} else {
		rk33_clk_set_dvfs_node(platform->gpu_clk_node, RK33_DEFAULT_CLOCK);
	}
#else
	platform->sclk_gpu_core = devm_clk_get(dev, "sclk_gpu_core");
	if (IS_ERR_OR_NULL(platform->sclk_gpu_core)) {
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: Failed to find sclk_gpu_core clock source"));
		goto fail4;
	}

	platform->gpu_reg = devm_regulator_get_optional(dev, "logic");
	if (IS_ERR_OR_NULL(platform->gpu_reg)) {
		/*if (dev_pm_opp_of_add_table(dev)) {

		} else {
		}*/
		PVR_DPF((PVR_DBG_ERROR, "RgxRkInit: devm_regulator_get_optional failed."));
		goto fail5;
	}

	clk_set_rate(platform->sclk_gpu_core, RK33_DEFAULT_CLOCK * ONE_MHZ);

	if(psRGXData && psRGXData->psRGXTimingInfo)
	{
		psRGXData->psRGXTimingInfo->ui32CoreClockSpeed = clk_get_rate(platform->sclk_gpu_core);
		psRGXData->psRGXTimingInfo->ui32CoreVoltage = regulator_get_voltage(platform->gpu_reg);
	}
#endif
	clk_set_rate(platform->aclk_gpu_mem, RK33_DEFAULT_CLOCK * ONE_MHZ);
	clk_set_rate(platform->aclk_gpu_cfg, RK33_DEFAULT_CLOCK * ONE_MHZ);



	(void) RkPrePowerState(platform,
						   PVRSRV_DEV_POWER_STATE_ON,
						   PVRSRV_DEV_POWER_STATE_DEFAULT,
						   IMG_FALSE);

	RgxResume(platform);

	return platform;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
fail5:
	devm_clk_put(dev, platform->sclk_gpu_core);
	platform->sclk_gpu_core = NULL;
#endif
fail4:
	devm_clk_put(dev, platform->aclk_gpu_cfg);
	platform->aclk_gpu_cfg = NULL;
fail3:
	devm_clk_put(dev, platform->aclk_gpu_mem);
	platform->aclk_gpu_mem = NULL;
fail2:

#if OPEN_GPU_PD && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	devm_clk_put(dev, platform->pd_gpu_1);
	platform->pd_gpu_1 = NULL;
fail1:
	devm_clk_put(dev, platform->pd_gpu_0);
	platform->pd_gpu_0 = NULL;
fail0:
	devm_kfree(dev, platform);
	return NULL;
#else
	devm_kfree(dev, platform);
	return NULL;
#endif //end of OPEN_GPU_PD

}


#if defined(SUPPORT_ION)
struct ion_device *g_psIonDev;

PVRSRV_ERROR IonInit(void *phPrivateData)
{
	g_psIonDev = NULL;
	return PVRSRV_OK;
}

struct ion_device *IonDevAcquire(void)
{
	return g_psIonDev;
}

void IonDevRelease(struct ion_device *psIonDev)
{
	/* Nothing to do, sanity check the pointer we're passed back */
	PVR_ASSERT(psIonDev == g_psIonDev);
}

void IonDeinit(void)
{
	g_psIonDev = NULL;
}
#endif /* defined(SUPPORT_ION) */

