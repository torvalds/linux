/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/ioport.h>
#ifdef CONFIG_DEVFREQ_THERMAL
#include <linux/devfreq_cooling.h>
#endif
#include <linux/thermal.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include <mali_kbase_smc.h>

/* Versatile Express (VE) Juno Development Platform */

#define HARD_RESET_AT_POWER_OFF 0

#ifndef CONFIG_OF
static struct kbase_io_resources io_resources = {
	.job_irq_number = 65,
	.mmu_irq_number = 66,
	.gpu_irq_number = 64,
	.io_memory_region = {
			     .start = 0x2D000000,
			     .end = 0x2D000000 + (4096 * 4) - 1}
};
#endif

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	/* Nothing is needed on VExpress, but we may have destroyed GPU state (if the below HARD_RESET code is active) */
	return 1;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
#if HARD_RESET_AT_POWER_OFF
	/* Cause a GPU hard reset to test whether we have actually idled the GPU
	 * and that we properly reconfigure the GPU on power up.
	 * Usually this would be dangerous, but if the GPU is working correctly it should
	 * be completely safe as the GPU should not be active at this point.
	 * However this is disabled normally because it will most likely interfere with
	 * bus logging etc.
	 */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_HARD_RESET, NULL, NULL, 0u, 0);
	kbase_os_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_HARD_RESET);
#endif
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback  = NULL,
	.power_resume_callback = NULL
};

#ifdef CONFIG_DEVFREQ_THERMAL

#define FALLBACK_STATIC_TEMPERATURE 55000

static unsigned long juno_model_static_power(unsigned long voltage)
{
	struct thermal_zone_device *tz;
	unsigned long temperature, temp;
	unsigned long temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long coefficient = (410UL << 20) / (729000000UL >> 10);
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	tz = thermal_zone_get_zone_by_name("gpu");
	if (IS_ERR(tz)) {
		pr_warn_ratelimited("Error getting gpu thermal zone (%ld), not yet ready?\n",
				PTR_ERR(tz));
		temperature = FALLBACK_STATIC_TEMPERATURE;
	} else {
		int ret;

		ret = tz->ops->get_temp(tz, &temperature);
		if (ret) {
			pr_warn_ratelimited("Error reading temperature for gpu thermal zone: %d\n",
					ret);
			temperature = FALLBACK_STATIC_TEMPERATURE;
		}
	}

	/* Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power.
	 */
	temp = temperature / 1000;
	temp_squared = temp * temp;
	temp_cubed = temp_squared * temp;
	temp_scaling_factor =
			(2 * temp_cubed)
			- (80 * temp_squared)
			+ (4700 * temp)
			+ 32000;

	return (((coefficient * voltage_cubed) >> 20)
			* temp_scaling_factor)
				/ 1000000;
}

static unsigned long juno_model_dynamic_power(unsigned long freq,
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
	const unsigned long coefficient = 3600; /* mW/(MHz*mV*mV) */

	return (coefficient * v2 * f_mhz) / 1000000; /* mW */
}

struct devfreq_cooling_ops juno_model_ops = {
	.get_static_power = juno_model_static_power,
	.get_dynamic_power = juno_model_dynamic_power,
};

#endif /* CONFIG_DEVFREQ_THERMAL */

/*
 * Juno Secure Mode integration
 */

/* SMC Function Numbers */
#define JUNO_SMC_SECURE_ENABLE_FUNC  0xff06
#define JUNO_SMC_SECURE_DISABLE_FUNC 0xff07

static int juno_secure_mode_enable(struct kbase_device *kbdev)
{
	u32 gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;

	if (gpu_id == GPU_ID_MAKE(GPU_ID_PI_T62X, 0, 1, 0) &&
			kbdev->reg_start == 0x2d000000) {
		/* T62X in SoC detected */
		u64 ret = kbase_invoke_smc(SMC_OEN_SIP,
			JUNO_SMC_SECURE_ENABLE_FUNC, false,
			0, 0, 0);
		return ret;
	}

	return -EINVAL; /* Not supported */
}

static int juno_secure_mode_disable(struct kbase_device *kbdev)
{
	u32 gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;

	if (gpu_id == GPU_ID_MAKE(GPU_ID_PI_T62X, 0, 1, 0) &&
			kbdev->reg_start == 0x2d000000) {
		/* T62X in SoC detected */
		u64 ret = kbase_invoke_smc(SMC_OEN_SIP,
			JUNO_SMC_SECURE_DISABLE_FUNC, false,
			0, 0, 0);
		return ret;
	}

	return -EINVAL; /* Not supported */
}

struct kbase_secure_ops juno_secure_ops = {
	.secure_mode_enable = juno_secure_mode_enable,
	.secure_mode_disable = juno_secure_mode_disable,
};

static struct kbase_platform_config versatile_platform_config = {
#ifndef CONFIG_OF
	.io_resources = &io_resources
#endif
};

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &versatile_platform_config;
}

int kbase_platform_early_init(void)
{
	/* Nothing needed at this stage */
	return 0;
}
