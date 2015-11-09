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
