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





/**
 * @file mali_kbase_pm.h
 * Power management API definitions
 */

#ifndef _KBASE_PM_H_
#define _KBASE_PM_H_

#include <mali_midg_regmap.h>
#include <linux/atomic.h>

/* Forward definition - see mali_kbase.h */
struct kbase_device;

#include "mali_kbase_pm_ca.h"
#include "mali_kbase_pm_policy.h"

#include "mali_kbase_pm_ca_fixed.h"
#if MALI_CUSTOMER_RELEASE == 0
#include "mali_kbase_pm_ca_random.h"
#endif

#include "mali_kbase_pm_always_on.h"
#include "mali_kbase_pm_coarse_demand.h"
#include "mali_kbase_pm_demand.h"
#if MALI_CUSTOMER_RELEASE == 0
#include "mali_kbase_pm_demand_always_powered.h"
#include "mali_kbase_pm_fast_start.h"
#endif

/** The types of core in a GPU.
 *
 * These enumerated values are used in calls to:
 * - @ref kbase_pm_get_present_cores
 * - @ref kbase_pm_get_active_cores
 * - @ref kbase_pm_get_trans_cores
 * - @ref kbase_pm_get_ready_cores.
 *
 * They specify which type of core should be acted on.  These values are set in
 * a manner that allows @ref core_type_to_reg function to be simpler and more
 * efficient.
 */
typedef enum kbase_pm_core_type {
	KBASE_PM_CORE_L3 = L3_PRESENT_LO,	    /**< The L3 cache */
	KBASE_PM_CORE_L2 = L2_PRESENT_LO,	    /**< The L2 cache */
	KBASE_PM_CORE_SHADER = SHADER_PRESENT_LO,   /**< Shader cores */
	KBASE_PM_CORE_TILER = TILER_PRESENT_LO	    /**< Tiler cores */
} kbase_pm_core_type;

/** Initialize the power management framework.
 *
 * Must be called before any other power management function
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return MALI_ERROR_NONE if the power management framework was successfully initialized.
 */
mali_error kbase_pm_init(struct kbase_device *kbdev);

/** Power up GPU after all modules have been initialized and interrupt handlers installed.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return MALI_ERROR_NONE if powerup was successful.
 */
mali_error kbase_pm_powerup(struct kbase_device *kbdev);

/**
 * Halt the power management framework.
 * Should ensure that no new interrupts are generated,
 * but allow any currently running interrupt handlers to complete successfully.
 * The GPU is forced off by the time this function returns, regardless of
 * whether or not the active power policy asks for the GPU to be powered off.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_halt(struct kbase_device *kbdev);

/** Terminate the power management framework.
 *
 * No power management functions may be called after this
 * (except @ref kbase_pm_init)
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_term(struct kbase_device *kbdev);

/** Metrics data collected for use by the power management framework.
 *
 */
typedef struct kbasep_pm_metrics_data {
	int vsync_hit;
	int utilisation;
	ktime_t time_period_start;
	u32 time_busy;
	u32 time_idle;
	mali_bool gpu_active;

	spinlock_t lock;

	struct hrtimer timer;
	mali_bool timer_active;

	void *platform_data;
	struct kbase_device *kbdev;
} kbasep_pm_metrics_data;

/** Actions for DVFS.
 *
 * kbase_pm_get_dvfs_action will return one of these enumerated values to
 * describe the action that the DVFS system should take.
 */
typedef enum kbase_pm_dvfs_action {
	KBASE_PM_DVFS_NOP,	    /**< No change in clock frequency is requested */
	KBASE_PM_DVFS_CLOCK_UP,	    /**< The clock frequency should be increased if possible */
	KBASE_PM_DVFS_CLOCK_DOWN    /**< The clock frequency should be decreased if possible */
} kbase_pm_dvfs_action;

typedef union kbase_pm_policy_data {
	kbasep_pm_policy_always_on always_on;
	kbasep_pm_policy_coarse_demand coarse_demand;
	kbasep_pm_policy_demand demand;
#if MALI_CUSTOMER_RELEASE == 0 	
	kbasep_pm_policy_demand_always_powered demand_always_powered;
	kbasep_pm_policy_fast_start fast_start;
#endif
} kbase_pm_policy_data;

typedef union kbase_pm_ca_policy_data {
	kbasep_pm_ca_policy_fixed fixed;
#if MALI_CUSTOMER_RELEASE == 0
	kbasep_pm_ca_policy_random random;
#endif
} kbase_pm_ca_policy_data;

/** Data stored per device for power management.
 *
 * This structure contains data for the power management framework. There is one instance of this structure per device
 * in the system.
 */
typedef struct kbase_pm_device_data {
	/** The lock protecting Power Management structures accessed
	 * outside of IRQ.
	 *
	 * This lock must also be held whenever the GPU is being powered on or off.
	 */
	struct mutex lock;

	/** The policy that is currently actively controlling core availability.
	 *
	 * @note: During an IRQ, this can be NULL when the policy is being changed
	 * with kbase_pm_ca_set_policy(). The change is protected under
	 * kbase_device::pm::power_change_lock. Direct access to this from IRQ
	 * context must therefore check for NULL. If NULL, then
	 * kbase_pm_ca_set_policy() will re-issue the policy functions that would've
	 * been done under IRQ.
	 */
	const kbase_pm_ca_policy *ca_current_policy;

	/** The policy that is currently actively controlling the power state.
	 *
	 * @note: During an IRQ, this can be NULL when the policy is being changed
	 * with kbase_pm_set_policy(). The change is protected under
	 * kbase_device::pm::power_change_lock. Direct access to this from IRQ
	 * context must therefore check for NULL. If NULL, then
	 * kbase_pm_set_policy() will re-issue the policy functions that would've
	 * been done under IRQ.
	 */
	const kbase_pm_policy *pm_current_policy;

	/** Private data for current CA policy */
	kbase_pm_ca_policy_data ca_policy_data;

	/** Private data for current PM policy */
	kbase_pm_policy_data pm_policy_data;

	/** Flag indicating when core availability policy is transitioning cores.
	 * The core availability policy must set this when a change in core availability
	 * is occuring.
	 *
	 * power_change_lock must be held when accessing this. */
	mali_bool ca_in_transition;

	/** Waiting for reset and a queue to wait for changes */
	mali_bool reset_done;
	wait_queue_head_t reset_done_wait;

	/** Wait queue for whether the l2 cache has been powered as requested */
	wait_queue_head_t l2_powered_wait;
	/** State indicating whether all the l2 caches are powered.
	 * Non-zero indicates they're *all* powered
	 * Zero indicates that some (or all) are not powered */
	int l2_powered;

	/** The reference count of active contexts on this device. */
	int active_count;
	/** Flag indicating suspending/suspended */
	mali_bool suspending;
	/* Wait queue set when active_count == 0 */
	wait_queue_head_t zero_active_count_wait;

	/** The reference count of active gpu cycle counter users */
	int gpu_cycle_counter_requests;
	/** Lock to protect gpu_cycle_counter_requests */
	spinlock_t gpu_cycle_counter_requests_lock;

	/** A bit mask identifying the shader cores that the power policy would like to be on.
	 * The current state of the cores may be different, but there should be transitions in progress that will
	 * eventually achieve this state (assuming that the policy doesn't change its mind in the mean time.
	 */
	u64 desired_shader_state;
	/** bit mask indicating which shader cores are currently in a power-on transition */
	u64 powering_on_shader_state;
	/** A bit mask identifying the tiler cores that the power policy would like to be on.
	 * @see kbase_pm_device_data:desired_shader_state */
	u64 desired_tiler_state;
	/** bit mask indicating which tiler core are currently in a power-on transition */
	u64 powering_on_tiler_state;

	/** bit mask indicating which l2-caches are currently in a power-on transition */
	u64 powering_on_l2_state;
	/** bit mask indicating which l3-caches are currently in a power-on transition */
	u64 powering_on_l3_state;

	/** Lock protecting the power state of the device.
	 *
	 * This lock must be held when accessing the shader_available_bitmap, tiler_available_bitmap, l2_available_bitmap,
	 * shader_inuse_bitmap and tiler_inuse_bitmap fields of kbase_device, and the ca_in_transition and shader_poweroff_pending
	 * fields of kbase_pm_device_data. It is also held when the hardware power registers are being written to, to ensure
	 * that two threads do not conflict over the power transitions that the hardware should make.
	 */
	spinlock_t power_change_lock;

	/** This flag is set iff the GPU is powered as requested by the
	 * desired_xxx_state variables */
	mali_bool gpu_in_desired_state;
	/* Wait queue set when gpu_in_desired_state != 0 */
	wait_queue_head_t gpu_in_desired_state_wait;

	/** Set to true when the GPU is powered and register accesses are possible, false otherwise */
	mali_bool gpu_powered;

	/** A bit mask identifying the available shader cores that are specified via sysfs */
	u64 debug_core_mask;

	/** Set to true when instrumentation is enabled, false otherwise */
	mali_bool instr_enabled;

	mali_bool cg1_disabled;

#ifdef CONFIG_MALI_DEBUG
	/** Debug state indicating whether sufficient initialization of the driver
	 * has occurred to handle IRQs */
	mali_bool driver_ready_for_irqs;
#endif /* CONFIG_MALI_DEBUG */

	/** Spinlock that must be held when:
	 * - writing gpu_powered
	 * - accessing driver_ready_for_irqs (in CONFIG_MALI_DEBUG builds) */
	spinlock_t gpu_powered_lock;

	/** Time in milliseconds between each dvfs sample */

	u32 platform_dvfs_frequency;

	/** Structure to hold metrics for the GPU */

	kbasep_pm_metrics_data metrics;

	/** Set to the number of poweroff timer ticks until the GPU is powered off */
	int gpu_poweroff_pending;

	/** Set to the number of poweroff timer ticks until shaders are powered off */
	int shader_poweroff_pending_time;

	/** Timer for powering off GPU */
	struct hrtimer gpu_poweroff_timer;

	struct workqueue_struct *gpu_poweroff_wq;

	struct work_struct gpu_poweroff_work;

	/** Period of GPU poweroff timer */
	ktime_t gpu_poweroff_time;

	/** Bit mask of shaders to be powered off on next timer callback */
	u64 shader_poweroff_pending;

	/** Set to MALI_TRUE if the poweroff timer is currently running, MALI_FALSE otherwise */
	mali_bool poweroff_timer_running;

	int poweroff_shader_ticks;

	int poweroff_gpu_ticks;

	/** Callback when the GPU needs to be turned on. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 *
	 * @return 1 if GPU state was lost, 0 otherwise
	 */
	int (*callback_power_on) (struct kbase_device *kbdev);

	/** Callback when the GPU may be turned off. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_off) (struct kbase_device *kbdev);

	/** Callback when a suspend occurs and the GPU needs to be turned off.
	 *  See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_suspend) (struct kbase_device *kbdev);

	/** Callback when a resume occurs and the GPU needs to be turned on.
	 *  See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_resume) (struct kbase_device *kbdev);

	/** Callback for initializing the runtime power management.
	 *
	 * @param kbdev         The kbase device
	 *
	 * @return MALI_ERROR_NONE on success, else error code
	 */
	 mali_error(*callback_power_runtime_init) (struct kbase_device *kbdev);

	/** Callback for terminating the runtime power management.
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_runtime_term) (struct kbase_device *kbdev);

	/** Callback when the GPU needs to be turned on. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 *
	 * @return 1 if GPU state was lost, 0 otherwise
	 */
	int (*callback_power_runtime_on) (struct kbase_device *kbdev);

	/** Callback when the GPU may be turned off. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_runtime_off) (struct kbase_device *kbdev);

} kbase_pm_device_data;

/** The GPU is idle.
 *
 * The OS may choose to turn off idle devices
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_dev_idle(struct kbase_device *kbdev);

/** The GPU is active.
 *
 * The OS should avoid opportunistically turning off the GPU while it is active
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_dev_activate(struct kbase_device *kbdev);

/** Get details of the cores that are present in the device.
 *
 * This function can be called by the active power policy to return a bitmask of the cores (of a specified type)
 * present in the GPU device and also a count of the number of cores.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param type      The type of core (see the @ref kbase_pm_core_type enumeration)
 *
 * @return          The bit mask of cores present
 */
u64 kbase_pm_get_present_cores(struct kbase_device *kbdev, kbase_pm_core_type type);

/** Get details of the cores that are currently active in the device.
 *
 * This function can be called by the active power policy to return a bitmask of the cores (of a specified type) that
 * are actively processing work (i.e. turned on *and* busy).
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param type      The type of core (see the @ref kbase_pm_core_type enumeration)
 *
 * @return          The bit mask of active cores
 */
u64 kbase_pm_get_active_cores(struct kbase_device *kbdev, kbase_pm_core_type type);

/** Get details of the cores that are currently transitioning between power states.
 *
 * This function can be called by the active power policy to return a bitmask of the cores (of a specified type) that
 * are currently transitioning between power states.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param type      The type of core (see the @ref kbase_pm_core_type enumeration)
 *
 * @return          The bit mask of transitioning cores
 */
u64 kbase_pm_get_trans_cores(struct kbase_device *kbdev, kbase_pm_core_type type);

/** Get details of the cores that are currently powered and ready for jobs.
 *
 * This function can be called by the active power policy to return a bitmask of the cores (of a specified type) that
 * are powered and ready for jobs (they may or may not be currently executing jobs).
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param type      The type of core (see the @ref kbase_pm_core_type enumeration)
 *
 * @return          The bit mask of ready cores
 */
u64 kbase_pm_get_ready_cores(struct kbase_device *kbdev, kbase_pm_core_type type);

/** Turn the clock for the device on, and enable device interrupts.
 *
 * This function can be used by a power policy to turn the clock for the GPU on. It should be modified during
 * integration to perform the necessary actions to ensure that the GPU is fully powered and clocked.
 *
 * @param kbdev       The kbase device structure for the device (must be a valid pointer)
 * @param is_resume   MALI_TRUE if clock on due to resume after suspend,
 *                    MALI_FALSE otherwise
 */
void kbase_pm_clock_on(struct kbase_device *kbdev, mali_bool is_resume);

/** Disable device interrupts, and turn the clock for the device off.
 *
 * This function can be used by a power policy to turn the clock for the GPU off. It should be modified during
 * integration to perform the necessary actions to turn the clock off (if this is possible in the integration).
 *
 * @param kbdev       The kbase device structure for the device (must be a valid pointer)
 * @param is_suspend  MALI_TRUE if clock off due to suspend, MALI_FALSE otherwise
 */
void kbase_pm_clock_off(struct kbase_device *kbdev, mali_bool is_suspend);

/** Enable interrupts on the device.
 *
 * Interrupts are also enabled after a call to kbase_pm_clock_on().
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_enable_interrupts(struct kbase_device *kbdev);

/** Disable interrupts on the device.
 *
 * This prevents delivery of Power Management interrupts to the CPU so that
 * kbase_pm_check_transitions_nolock() will not be called from the IRQ handler
 * until @ref kbase_pm_enable_interrupts or kbase_pm_clock_on() is called.
 *
 * Interrupts are also disabled after a call to kbase_pm_clock_off().
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_disable_interrupts(struct kbase_device *kbdev);

/** Initialize the hardware
 *
 * This function checks the GPU ID register to ensure that the GPU is supported by the driver and performs a reset on
 * the device so that it is in a known state before the device is used.
 *
 * @param kbdev        The kbase device structure for the device (must be a valid pointer)
 * @param enable_irqs  When set to MALI_TRUE gpu irqs will be enabled after this call, else
 *                     they will be left disabled.
 *
 * @return MALI_ERROR_NONE if the device is supported and successfully reset.
 */
mali_error kbase_pm_init_hw(struct kbase_device *kbdev, mali_bool enable_irqs );

/** The GPU has been reset successfully.
 *
 * This function must be called by the GPU interrupt handler when the RESET_COMPLETED bit is set. It signals to the
 * power management initialization code that the GPU has been successfully reset.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_reset_done(struct kbase_device *kbdev);

/** Increment the count of active contexts.
 *
 * This function should be called when a context is about to submit a job. It informs the active power policy that the
 * GPU is going to be in use shortly and the policy is expected to start turning on the GPU.
 *
 * This function will block until the GPU is available.
 *
 * This function ASSERTS if a suspend is occuring/has occurred whilst this is
 * in use. Use kbase_pm_contect_active_unless_suspending() instead.
 *
 * @note a Suspend is only visible to Kernel threads; user-space threads in a
 * syscall cannot witness a suspend, because they are frozen before the suspend
 * begins.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_context_active(struct kbase_device *kbdev);


/** Handler codes for doing kbase_pm_context_active_handle_suspend() */
typedef enum {
	/** A suspend is not expected/not possible - this is the same as
	 * kbase_pm_context_active() */
	KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE,
	/** If we're suspending, fail and don't increase the active count */
	KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE,
	/** If we're suspending, succeed and allow the active count to increase iff
	 * it didn't go from 0->1 (i.e., we didn't re-activate the GPU).
	 *
	 * This should only be used when there is a bounded time on the activation
	 * (e.g. guarantee it's going to be idled very soon after) */
	KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE
} kbase_pm_suspend_handler;

/** Suspend 'safe' variant of kbase_pm_context_active()
 *
 * If a suspend is in progress, this allows for various different ways of
 * handling the suspend. Refer to @ref kbase_pm_suspend_handler for details.
 *
 * We returns a status code indicating whether we're allowed to keep the GPU
 * active during the suspend, depending on the handler code. If the status code
 * indicates a failure, the caller must abort whatever operation it was
 * attempting, and potentially queue it up for after the OS has resumed.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param suspend_handler The handler code for how to handle a suspend that might occur
 * @return zero     Indicates success
 * @return non-zero Indicates failure due to the system being suspending/suspended.
 */
int kbase_pm_context_active_handle_suspend(struct kbase_device *kbdev, kbase_pm_suspend_handler suspend_handler);

/** Decrement the reference count of active contexts.
 *
 * This function should be called when a context becomes idle. After this call the GPU may be turned off by the power
 * policy so the calling code should ensure that it does not access the GPU's registers.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_context_idle(struct kbase_device *kbdev);

/** Check if there are any power transitions to make, and if so start them.
 *
 * This function will check the desired_xx_state members of kbase_pm_device_data and the actual status of the
 * hardware to see if any power transitions can be made at this time to make the hardware state closer to the state
 * desired by the power policy.
 *
 * The return value can be used to check whether all the desired cores are
 * available, and so whether it's worth submitting a job (e.g. from a Power
 * Management IRQ).
 *
 * Note that this still returns MALI_TRUE when desired_xx_state has no
 * cores. That is: of the no cores desired, none were <em>un</em>available. In
 * this case, the caller may still need to try submitting jobs. This is because
 * the Core Availability Policy might have taken us to an intermediate state
 * where no cores are powered, before powering on more cores (e.g. for core
 * rotation)
 *
 * The caller must hold kbase_device::pm::power_change_lock
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @return          non-zero when all desired cores are available. That is,
 *                  it's worthwhile for the caller to submit a job.
 * @return          MALI_FALSE otherwise
 */
mali_bool kbase_pm_check_transitions_nolock(struct kbase_device *kbdev);

/** Synchronous and locking variant of kbase_pm_check_transitions_nolock()
 *
 * On returning, the desired state at the time of the call will have been met.
 *
 * @note There is nothing to stop the core being switched off by calls to
 * kbase_pm_release_cores() or kbase_pm_unrequest_cores(). Therefore, the
 * caller must have already made a call to
 * kbase_pm_request_cores()/kbase_pm_request_cores_sync() previously.
 *
 * The usual use-case for this is to ensure cores are 'READY' after performing
 * a GPU Reset.
 *
 * Unlike kbase_pm_check_transitions_nolock(), the caller must not hold
 * kbase_device::pm::power_change_lock, because this function will take that
 * lock itself.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_check_transitions_sync(struct kbase_device *kbdev);

/** Variant of kbase_pm_update_cores_state() where the caller must hold
 * kbase_device::pm::power_change_lock
 *
 * @param kbdev       The kbase device structure for the device (must be a valid
 *                    pointer)
 */
void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev);

/** Update the desired state of shader cores from the Power Policy, and begin
 * any power transitions.
 *
 * This function will update the desired_xx_state members of
 * kbase_pm_device_data by calling into the current Power Policy. It will then
 * begin power transitions to make the hardware acheive the desired shader core
 * state.
 *
 * @param kbdev       The kbase device structure for the device (must be a valid
 *                    pointer)
 */
void kbase_pm_update_cores_state(struct kbase_device *kbdev);

/** Cancel any pending requests to power off the GPU and/or shader cores.
 *
 * This should be called by any functions which directly power off the GPU.
 *
 * @param kbdev       The kbase device structure for the device (must be a valid
 *                    pointer)
 */
void kbase_pm_cancel_deferred_poweroff(struct kbase_device *kbdev);

/** Read the bitmasks of present cores.
 *
 * This information is cached to avoid having to perform register reads whenever the information is required.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_read_present_cores(struct kbase_device *kbdev);

/** Initialize the metrics gathering framework.
 *
 * This must be called before other metric gathering APIs are called.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return MALI_ERROR_NONE on success, MALI_ERROR_FUNCTION_FAILED on error
 */
mali_error kbasep_pm_metrics_init(struct kbase_device *kbdev);

/** Terminate the metrics gathering framework.
 *
 * This must be called when metric gathering is no longer required. It is an error to call any metrics gathering
 * function (other than kbasep_pm_metrics_init) after calling this function.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_metrics_term(struct kbase_device *kbdev);

/** Record that the GPU is active.
 *
 * This records that the GPU is now active. The previous GPU state must have been idle, the function will assert if
 * this is not true in a debug build.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_record_gpu_active(struct kbase_device *kbdev);

/** Record that the GPU is idle.
 *
 * This records that the GPU is now idle. The previous GPU state must have been active, the function will assert if
 * this is not true in a debug build.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_record_gpu_idle(struct kbase_device *kbdev);

/** Function to be called by the frame buffer driver to update the vsync metric.
 *
 * This function should be called by the frame buffer driver to update whether the system is hitting the vsync target
 * or not. buffer_updated should be true if the vsync corresponded with a new frame being displayed, otherwise it
 * should be false. This function does not need to be called every vsync, but only when the value of buffer_updated
 * differs from a previous call.
 *
 * @param kbdev             The kbase device structure for the device (must be a valid pointer)
 * @param buffer_updated    True if the buffer has been updated on this VSync, false otherwise
 */
void kbase_pm_report_vsync(struct kbase_device *kbdev, int buffer_updated);

/** Configure the frame buffer device to set the vsync callback.
 *
 * This function should do whatever is necessary for this integration to ensure that kbase_pm_report_vsync is
 * called appropriately.
 *
 * This function will need porting as part of the integration for a device.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_register_vsync_callback(struct kbase_device *kbdev);

/** Free any resources that kbase_pm_register_vsync_callback allocated.
 *
 * This function should perform any cleanup required from the call to kbase_pm_register_vsync_callback.
 * No call backs should occur after this function has returned.
 *
 * This function will need porting as part of the integration for a device.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_unregister_vsync_callback(struct kbase_device *kbdev);

/** Determine whether the DVFS system should change the clock speed of the GPU.
 *
 * This function should be called regularly by the DVFS system to check whether the clock speed of the GPU needs
 * updating. It will return one of three enumerated values of kbase_pm_dvfs_action:
 *
 * @param kbdev                     The kbase device structure for the device (must be a valid pointer)
 * @retval KBASE_PM_DVFS_NOP        The clock does not need changing
 * @retval KBASE_PM_DVFS_CLOCK_UP,  The clock frequency should be increased if possible.
 * @retval KBASE_PM_DVFS_CLOCK_DOWN The clock frequency should be decreased if possible.
 */
kbase_pm_dvfs_action kbase_pm_get_dvfs_action(struct kbase_device *kbdev);

/** Mark that the GPU cycle counter is needed, if the caller is the first caller
 *  then the GPU cycle counters will be enabled.
 *
 * The GPU must be powered when calling this function (i.e. @ref kbase_pm_context_active must have been called).
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */

void kbase_pm_request_gpu_cycle_counter(struct kbase_device *kbdev);

/** Mark that the GPU cycle counter is no longer in use, if the caller is the last
 *  caller then the GPU cycle counters will be disabled. A request must have been made
 *  before a call to this.
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */

void kbase_pm_release_gpu_cycle_counter(struct kbase_device *kbdev);

/** Enables access to the GPU registers before power management has powered up the GPU
 *  with kbase_pm_powerup().
 *
 *  Access to registers should be done using kbase_os_reg_read/write() at this stage,
 *  not kbase_reg_read/write().
 *
 *  This results in the power management callbacks provided in the driver configuration
 *  to get called to turn on power and/or clocks to the GPU.
 *  See @ref kbase_pm_callback_conf.
 *
 * This should only be used before power management is powered up with kbase_pm_powerup()
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_register_access_enable(struct kbase_device *kbdev);

/** Disables access to the GPU registers enabled earlier by a call to
 *  kbase_pm_register_access_enable().
 *
 *  This results in the power management callbacks provided in the driver configuration
 *  to get called to turn off power and/or clocks to the GPU.
 *  See @ref kbase_pm_callback_conf
 *
 * This should only be used before power management is powered up with kbase_pm_powerup()
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_register_access_disable(struct kbase_device *kbdev);

/**
 * Suspend the GPU and prevent any further register accesses to it from Kernel
 * threads.
 *
 * This is called in response to an OS suspend event, and calls into the various
 * kbase components to complete the suspend.
 *
 * @note the mechanisms used here rely on all user-space threads being frozen
 * by the OS before we suspend. Otherwise, an IOCTL could occur that powers up
 * the GPU e.g. via atom submission.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_suspend(struct kbase_device *kbdev);

/**
 * Resume the GPU, allow register accesses to it, and resume running atoms on
 * the GPU.
 *
 * This is called in response to an OS resume event, and calls into the various
 * kbase components to complete the resume.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_resume(struct kbase_device *kbdev);

/* NOTE: kbase_pm_is_suspending is in mali_kbase.h, because it is an inline function */

/**
 * Check if the power management metrics collection is active.
 *
 * Note that this returns if the power management metrics collection was
 * active at the time of calling, it is possible that after the call the metrics
 * collection enable may have changed state.
 *
 * The caller must handle the consequence that the state may have changed.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @return          MALI_TRUE if metrics collection was active else MALI_FALSE.
 */

mali_bool kbase_pm_metrics_is_active(struct kbase_device *kbdev);

/**
 * Power on the GPU, and any cores that are requested.
 *
 * @param kbdev        The kbase device structure for the device (must be a valid pointer)
 * @param is_resume    MALI_TRUE if power on due to resume after suspend,
 *                     MALI_FALSE otherwise
 */
void kbase_pm_do_poweron(struct kbase_device *kbdev, mali_bool is_resume);

/**
 * Power off the GPU, and any cores that have been requested.
 *
 * @param kbdev        The kbase device structure for the device (must be a valid pointer)
 * @param is_suspend   MALI_TRUE if power off due to suspend,
 *                     MALI_FALSE otherwise
 */
void kbase_pm_do_poweroff(struct kbase_device *kbdev, mali_bool is_suspend);

#ifdef CONFIG_MALI_MIDGARD_DVFS

/**
 * Function provided by platform specific code when DVFS is enabled to allow
 * the power management metrics system to report utilisation.
 *
 * @param kbdev        The kbase device structure for the device (must be a valid pointer)
 * @param utilisation  The current calculated utilisation by the metrics system.
 * @return             Returns 0 on failure and non zero on success.
 */

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation);
#endif
#endif				/* _KBASE_PM_H_ */
