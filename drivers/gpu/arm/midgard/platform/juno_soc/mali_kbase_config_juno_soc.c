/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>

#include "../mali_kbase_power_actor.h"

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

static struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback  = NULL,
	.power_resume_callback = NULL
};

static unsigned long juno_model_static_power(unsigned long voltage, unsigned long temperature)
{
	/* Calculate power, corrected for voltage.
	 * Shifts are done to avoid overflow. */
	const unsigned long coefficient = (410UL << 20) / (729000000UL >> 10);
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	/* Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power. */
	const unsigned long temp = temperature / 1000;
	const unsigned long temp_squared = temp * temp;
	const unsigned long temp_cubed = temp_squared * temp;
	const unsigned long temp_scaling_factor =
			(2 * temp_cubed)
			- (80 * temp_squared)
			+ (4700 * temp)
			+ 32000;

	return (((coefficient * voltage_cubed) >> 20) * temp_scaling_factor) / 1000000;
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

static struct mali_pa_model_ops juno_model_ops = {
	.get_static_power = juno_model_static_power,
	.get_dynamic_power = juno_model_dynamic_power,
};

static struct kbase_attribute config_attributes[] = {
	{ KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS, 500 },
	{ KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS, ((uintptr_t)&pm_callbacks) },
	{ KBASE_CONFIG_ATTR_POWER_MODEL_CALLBACKS, ((uintptr_t)&juno_model_ops) },

	{ KBASE_CONFIG_ATTR_END, 0 }
};

static struct kbase_platform_config versatile_platform_config = {
	.attributes = config_attributes,
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
