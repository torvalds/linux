/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
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
 * Juno Protected Mode integration
 */

/* SMC Function Numbers */
#define JUNO_SMC_PROTECTED_ENTER_FUNC  0xff06
#define JUNO_SMC_PROTECTED_RESET_FUNC 0xff07

static int juno_protected_mode_enter(struct kbase_device *kbdev)
{
	/* T62X in SoC detected */
	u64 ret = kbase_invoke_smc(SMC_OEN_SIP,
		JUNO_SMC_PROTECTED_ENTER_FUNC, false,
		0, 0, 0);
	return ret;
}

/* TODO: Remove these externs, reset should should be done by the firmware */
extern void kbase_reg_write(struct kbase_device *kbdev, u16 offset, u32 value,
						struct kbase_context *kctx);

extern u32 kbase_reg_read(struct kbase_device *kbdev, u16 offset,
						struct kbase_context *kctx);

static int juno_protected_mode_reset(struct kbase_device *kbdev)
{

	/* T62X in SoC detected */
	u64 ret = kbase_invoke_smc(SMC_OEN_SIP,
		JUNO_SMC_PROTECTED_RESET_FUNC, false,
		0, 0, 0);

	/* TODO: Remove this reset, it should be done by the firmware */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
						GPU_COMMAND_HARD_RESET, NULL);

	while ((kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL)
			& RESET_COMPLETED) != RESET_COMPLETED)
		;

	return ret;
}

static bool juno_protected_mode_supported(struct kbase_device *kbdev)
{
	u32 gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;

	/*
	 * Protected mode is only supported for the built in GPU
	 * _and_ only if the right firmware is running.
	 *
	 * Given that at init time the GPU is not powered up the
	 * juno_protected_mode_reset function can't be used as
	 * is needs to access GPU registers.
	 * However, although we don't want the GPU to boot into
	 * protected mode we know a GPU reset will be done after
	 * this function is called so although we set the GPU to
	 * protected mode it will exit protected mode before the
	 * driver is ready to run work.
	 */
	if (gpu_id == GPU_ID_MAKE(GPU_ID_PI_T62X, 0, 1, 0) &&
			(kbdev->reg_start == 0x2d000000))
		return juno_protected_mode_enter(kbdev) == 0;

	return false;
}

struct kbase_protected_ops juno_protected_ops = {
	.protected_mode_enter = juno_protected_mode_enter,
	.protected_mode_reset = juno_protected_mode_reset,
	.protected_mode_supported = juno_protected_mode_supported,
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
