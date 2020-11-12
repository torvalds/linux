/*
 *
 * (C) COPYRIGHT 2010-2017, 2019-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/**
 * @file mali_kbase_config.h
 * Configuration API and Attributes for KBase
 */

#ifndef _KBASE_CONFIG_H_
#define _KBASE_CONFIG_H_

#include <linux/mm.h>
#include <mali_malisw.h>
#include <mali_kbase_backend_config.h>
#include <linux/rbtree.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup kbase_config Configuration API and Attributes
 * @{
 */

/* Forward declaration of struct kbase_device */
struct kbase_device;

/**
 * kbase_platform_funcs_conf - Specifies platform init/term function pointers
 *
 * Specifies the functions pointers for platform specific initialization and
 * termination. By default no functions are required. No additional platform
 * specific control is necessary.
 */
struct kbase_platform_funcs_conf {
	/**
	 * platform_init_func - platform specific init function pointer
	 * @kbdev - kbase_device pointer
	 *
	 * Returns 0 on success, negative error code otherwise.
	 *
	 * Function pointer for platform specific initialization or NULL if no
	 * initialization function is required. At the point this the GPU is
	 * not active and its power and clocks are in unknown (platform specific
	 * state) as kbase doesn't yet have control of power and clocks.
	 *
	 * The platform specific private pointer kbase_device::platform_context
	 * can be accessed (and possibly initialized) in here.
	 */
	int (*platform_init_func)(struct kbase_device *kbdev);
	/**
	 * platform_term_func - platform specific termination function pointer
	 * @kbdev - kbase_device pointer
	 *
	 * Function pointer for platform specific termination or NULL if no
	 * termination function is required. At the point this the GPU will be
	 * idle but still powered and clocked.
	 *
	 * The platform specific private pointer kbase_device::platform_context
	 * can be accessed (and possibly terminated) in here.
	 */
	void (*platform_term_func)(struct kbase_device *kbdev);
};

/*
 * @brief Specifies the callbacks for power management
 *
 * By default no callbacks will be made and the GPU must not be powered off.
 */
struct kbase_pm_callback_conf {
	/** Callback for when the GPU is idle and the power to it can be switched off.
	 *
	 * The system integrator can decide whether to either do nothing, just switch off
	 * the clocks to the GPU, or to completely power down the GPU.
	 * The platform specific private pointer kbase_device::platform_context can be accessed and modified in here. It is the
	 * platform \em callbacks responsibility to initialize and terminate this pointer if used (see @ref kbase_platform_funcs_conf).
	 */
	void (*power_off_callback)(struct kbase_device *kbdev);

	/** Callback for when the GPU is about to become active and power must be supplied.
	 *
	 * This function must not return until the GPU is powered and clocked sufficiently for register access to
	 * succeed.  The return value specifies whether the GPU was powered down since the call to power_off_callback.
	 * If the GPU state has been lost then this function must return 1, otherwise it should return 0.
	 * The platform specific private pointer kbase_device::platform_context can be accessed and modified in here. It is the
	 * platform \em callbacks responsibility to initialize and terminate this pointer if used (see @ref kbase_platform_funcs_conf).
	 *
	 * The return value of the first call to this function is ignored.
	 *
	 * @return 1 if the GPU state may have been lost, 0 otherwise.
	 */
	int (*power_on_callback)(struct kbase_device *kbdev);

	/** Callback for when the system is requesting a suspend and GPU power
	 * must be switched off.
	 *
	 * Note that if this callback is present, then this may be called
	 * without a preceding call to power_off_callback. Therefore this
	 * callback must be able to take any action that might otherwise happen
	 * in power_off_callback.
	 *
	 * The platform specific private pointer kbase_device::platform_context
	 * can be accessed and modified in here. It is the platform \em
	 * callbacks responsibility to initialize and terminate this pointer if
	 * used (see @ref kbase_platform_funcs_conf).
	 */
	void (*power_suspend_callback)(struct kbase_device *kbdev);

	/** Callback for when the system is resuming from a suspend and GPU
	 * power must be switched on.
	 *
	 * Note that if this callback is present, then this may be called
	 * without a following call to power_on_callback. Therefore this
	 * callback must be able to take any action that might otherwise happen
	 * in power_on_callback.
	 *
	 * The platform specific private pointer kbase_device::platform_context
	 * can be accessed and modified in here. It is the platform \em
	 * callbacks responsibility to initialize and terminate this pointer if
	 * used (see @ref kbase_platform_funcs_conf).
	 */
	void (*power_resume_callback)(struct kbase_device *kbdev);

	/** Callback for handling runtime power management initialization.
	 *
	 * The runtime power management callbacks @ref power_runtime_off_callback and @ref power_runtime_on_callback
	 * will become active from calls made to the OS from within this function.
	 * The runtime calls can be triggered by calls from @ref power_off_callback and @ref power_on_callback.
	 * Note: for linux the kernel must have CONFIG_PM_RUNTIME enabled to use this feature.
	 *
	 * @return 0 on success, else int error code.
	 */
	 int (*power_runtime_init_callback)(struct kbase_device *kbdev);

	/** Callback for handling runtime power management termination.
	 *
	 * The runtime power management callbacks @ref power_runtime_off_callback and @ref power_runtime_on_callback
	 * should no longer be called by the OS on completion of this function.
	 * Note: for linux the kernel must have CONFIG_PM_RUNTIME enabled to use this feature.
	 */
	void (*power_runtime_term_callback)(struct kbase_device *kbdev);

	/** Callback for runtime power-off power management callback
	 *
	 * For linux this callback will be called by the kernel runtime_suspend callback.
	 * Note: for linux the kernel must have CONFIG_PM_RUNTIME enabled to use this feature.
	 *
	 * @return 0 on success, else OS error code.
	 */
	void (*power_runtime_off_callback)(struct kbase_device *kbdev);

	/** Callback for runtime power-on power management callback
	 *
	 * For linux this callback will be called by the kernel runtime_resume callback.
	 * Note: for linux the kernel must have CONFIG_PM_RUNTIME enabled to use this feature.
	 */
	int (*power_runtime_on_callback)(struct kbase_device *kbdev);

	/*
	 * Optional callback for checking if GPU can be suspended when idle
	 *
	 * This callback will be called by the runtime power management core
	 * when the reference count goes to 0 to provide notification that the
	 * GPU now seems idle.
	 *
	 * If this callback finds that the GPU can't be powered off, or handles
	 * suspend by powering off directly or queueing up a power off, a
	 * non-zero value must be returned to prevent the runtime PM core from
	 * also triggering a suspend.
	 *
	 * Returning 0 will cause the runtime PM core to conduct a regular
	 * autosuspend.
	 *
	 * This callback is optional and if not provided regular autosuspend
	 * will be triggered.
	 *
	 * Note: The Linux kernel must have CONFIG_PM_RUNTIME enabled to use
	 * this feature.
	 *
	 * Return 0 if GPU can be suspended, positive value if it can not be
	 * suspeneded by runtime PM, else OS error code
	 */
	int (*power_runtime_idle_callback)(struct kbase_device *kbdev);

	/*
	 * Optional callback for software reset
	 *
	 * This callback will be called by the power management core to trigger
	 * a GPU soft reset.
	 *
	 * Return 0 if the soft reset was successful and the RESET_COMPLETED
	 * interrupt will be raised, or a positive value if the interrupt won't
	 * be raised. On error, return the corresponding OS error code.
	 */
	int (*soft_reset_callback)(struct kbase_device *kbdev);
};

/* struct kbase_gpu_clk_notifier_data - Data for clock rate change notifier.
 *
 * Pointer to this structure is supposed to be passed to the gpu clock rate
 * change notifier function. This structure is deliberately aligned with the
 * common clock framework notification structure 'struct clk_notifier_data'
 * and such alignment should be maintained.
 *
 * @gpu_clk_handle: Handle of the GPU clock for which notifier was registered.
 * @old_rate:       Previous rate of this GPU clock.
 * @new_rate:       New rate of this GPU clock.
 */
struct kbase_gpu_clk_notifier_data {
	void *gpu_clk_handle;
	unsigned long old_rate;
	unsigned long new_rate;
};

/**
 * kbase_clk_rate_trace_op_conf - Specifies GPU clock rate trace operations.
 *
 * Specifies the functions pointers for platform specific GPU clock rate trace
 * operations. By default no functions are required.
 */
struct kbase_clk_rate_trace_op_conf {
	/**
	 * enumerate_gpu_clk - Enumerate a GPU clock on the given index
	 * @kbdev - kbase_device pointer
	 * @index - GPU clock index
	 *
	 * Returns a handle unique to the given GPU clock, or NULL if the clock
	 * array has been exhausted at the given index value.
	 *
	 * Kbase will use this function pointer to enumerate the existence of a
	 * GPU clock on the given index.
	 */
	void *(*enumerate_gpu_clk)(struct kbase_device *kbdev,
		unsigned int index);

	/**
	 * get_gpu_clk_rate - Get the current rate for an enumerated clock.
	 * @kbdev          - kbase_device pointer
	 * @gpu_clk_handle - Handle unique to the enumerated GPU clock
	 *
	 * Returns current rate of the GPU clock in unit of Hz.
	 */
	unsigned long (*get_gpu_clk_rate)(struct kbase_device *kbdev,
		void *gpu_clk_handle);

	/**
	 * gpu_clk_notifier_register - Register a clock rate change notifier.
	 * @kbdev          - kbase_device pointer
	 * @gpu_clk_handle - Handle unique to the enumerated GPU clock
	 * @nb             - notifier block containing the callback function
	 *                   pointer
	 *
	 * Returns 0 on success, negative error code otherwise.
	 *
	 * This function pointer is used to register a callback function that
	 * is supposed to be invoked whenever the rate of clock corresponding
	 * to @gpu_clk_handle changes.
	 * @nb contains the pointer to callback function.
	 * The callback function expects the pointer of type
	 * 'struct kbase_gpu_clk_notifier_data' as the third argument.
	 */
	int (*gpu_clk_notifier_register)(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb);

	/**
	 * gpu_clk_notifier_unregister - Unregister clock rate change notifier
	 * @kbdev          - kbase_device pointer
	 * @gpu_clk_handle - Handle unique to the enumerated GPU clock
	 * @nb             - notifier block containing the callback function
	 *                   pointer
	 *
	 * This function pointer is used to unregister a callback function that
	 * was previously registered to get notified of the change in rate
	 * of clock corresponding to @gpu_clk_handle.
	 */
	void (*gpu_clk_notifier_unregister)(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb);
};

#ifdef CONFIG_OF
struct kbase_platform_config {
};
#else

/*
 * @brief Specifies start and end of I/O memory region.
 */
struct kbase_io_memory_region {
	u64 start;
	u64 end;
};

/*
 * @brief Specifies I/O related resources like IRQs and memory region for I/O operations.
 */
struct kbase_io_resources {
	u32                      job_irq_number;
	u32                      mmu_irq_number;
	u32                      gpu_irq_number;
	struct kbase_io_memory_region io_memory_region;
};

struct kbase_platform_config {
	const struct kbase_io_resources *io_resources;
};

#endif /* CONFIG_OF */

/**
 * @brief Gets the pointer to platform config.
 *
 * @return Pointer to the platform config
 */
struct kbase_platform_config *kbase_get_platform_config(void);

/**
 * kbasep_platform_device_init: - Platform specific call to initialize hardware
 * @kbdev: kbase device pointer
 *
 * Function calls a platform defined routine if specified in the configuration
 * attributes.  The routine can initialize any hardware and context state that
 * is required for the GPU block to function.
 *
 * Return: 0 if no errors have been found in the config.
 *         Negative error code otherwise.
 */
int kbasep_platform_device_init(struct kbase_device *kbdev);

/**
 * kbasep_platform_device_term - Platform specific call to terminate hardware
 * @kbdev: Kbase device pointer
 *
 * Function calls a platform defined routine if specified in the configuration
 * attributes. The routine can destroy any platform specific context state and
 * shut down any hardware functionality that are outside of the Power Management
 * callbacks.
 *
 */
void kbasep_platform_device_term(struct kbase_device *kbdev);

#ifndef CONFIG_OF
/**
 * kbase_platform_register - Register a platform device for the GPU
 *
 * This can be used to register a platform device on systems where device tree
 * is not enabled and the platform initialisation code in the kernel doesn't
 * create the GPU device. Where possible device tree should be used instead.
 *
 * Return: 0 for success, any other fail causes module initialisation to fail
 */
int kbase_platform_register(void);

/**
 * kbase_platform_unregister - Unregister a fake platform device
 *
 * Unregister the platform device created with kbase_platform_register()
 */
void kbase_platform_unregister(void);
#endif

	  /** @} *//* end group kbase_config */
	  /** @} *//* end group base_kbase_api */
	  /** @} *//* end group base_api */

#endif				/* _KBASE_CONFIG_H_ */
