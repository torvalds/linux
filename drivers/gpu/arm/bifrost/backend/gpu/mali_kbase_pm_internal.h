/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
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





/*
 * Power management API definitions used internally by GPU backend
 */

#ifndef _KBASE_BACKEND_PM_INTERNAL_H_
#define _KBASE_BACKEND_PM_INTERNAL_H_

#include <mali_kbase_hwaccess_pm.h>

#include "mali_kbase_pm_ca.h"
#include "mali_kbase_pm_policy.h"


/**
 * kbase_pm_dev_idle - The GPU is idle.
 *
 * The OS may choose to turn off idle devices
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_dev_idle(struct kbase_device *kbdev);

/**
 * kbase_pm_dev_activate - The GPU is active.
 *
 * The OS should avoid opportunistically turning off the GPU while it is active
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_dev_activate(struct kbase_device *kbdev);

/**
 * kbase_pm_get_present_cores - Get details of the cores that are present in
 *                              the device.
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) present in the GPU device and also a count of
 * the number of cores.
 *
 * @kbdev: The kbase device structure for the device (must be a valid
 *         pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * Return: The bit mask of cores present
 */
u64 kbase_pm_get_present_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_get_active_cores - Get details of the cores that are currently
 *                             active in the device.
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) that are actively processing work (i.e.
 * turned on *and* busy).
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * Return: The bit mask of active cores
 */
u64 kbase_pm_get_active_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_get_trans_cores - Get details of the cores that are currently
 *                            transitioning between power states.
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) that are currently transitioning between
 * power states.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * Return: The bit mask of transitioning cores
 */
u64 kbase_pm_get_trans_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_get_ready_cores - Get details of the cores that are currently
 *                            powered and ready for jobs.
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) that are powered and ready for jobs (they may
 * or may not be currently executing jobs).
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * Return: The bit mask of ready cores
 */
u64 kbase_pm_get_ready_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_clock_on - Turn the clock for the device on, and enable device
 *                     interrupts.
 *
 * This function can be used by a power policy to turn the clock for the GPU on.
 * It should be modified during integration to perform the necessary actions to
 * ensure that the GPU is fully powered and clocked.
 *
 * @kbdev:     The kbase device structure for the device (must be a valid
 *             pointer)
 * @is_resume: true if clock on due to resume after suspend, false otherwise
 */
void kbase_pm_clock_on(struct kbase_device *kbdev, bool is_resume);

/**
 * kbase_pm_clock_off - Disable device interrupts, and turn the clock for the
 *                      device off.
 *
 * This function can be used by a power policy to turn the clock for the GPU
 * off. It should be modified during integration to perform the necessary
 * actions to turn the clock off (if this is possible in the integration).
 *
 * @kbdev:      The kbase device structure for the device (must be a valid
 *              pointer)
 * @is_suspend: true if clock off due to suspend, false otherwise
 *
 * Return: true  if clock was turned off, or
 *         false if clock can not be turned off due to pending page/bus fault
 *               workers. Caller must flush MMU workqueues and retry
 */
bool kbase_pm_clock_off(struct kbase_device *kbdev, bool is_suspend);

/**
 * kbase_pm_enable_interrupts - Enable interrupts on the device.
 *
 * Interrupts are also enabled after a call to kbase_pm_clock_on().
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_enable_interrupts(struct kbase_device *kbdev);

/**
 * kbase_pm_disable_interrupts - Disable interrupts on the device.
 *
 * This prevents delivery of Power Management interrupts to the CPU so that
 * kbase_pm_check_transitions_nolock() will not be called from the IRQ handler
 * until kbase_pm_enable_interrupts() or kbase_pm_clock_on() is called.
 *
 * Interrupts are also disabled after a call to kbase_pm_clock_off().
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_disable_interrupts(struct kbase_device *kbdev);

/**
 * kbase_pm_disable_interrupts_nolock - Version of kbase_pm_disable_interrupts()
 *                                      that does not take the hwaccess_lock
 *
 * Caller must hold the hwaccess_lock.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_disable_interrupts_nolock(struct kbase_device *kbdev);

/**
 * kbase_pm_init_hw - Initialize the hardware.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @flags: Flags specifying the type of PM init
 *
 * This function checks the GPU ID register to ensure that the GPU is supported
 * by the driver and performs a reset on the device so that it is in a known
 * state before the device is used.
 *
 * Return: 0 if the device is supported and successfully reset.
 */
int kbase_pm_init_hw(struct kbase_device *kbdev, unsigned int flags);

/**
 * kbase_pm_reset_done - The GPU has been reset successfully.
 *
 * This function must be called by the GPU interrupt handler when the
 * RESET_COMPLETED bit is set. It signals to the power management initialization
 * code that the GPU has been successfully reset.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_reset_done(struct kbase_device *kbdev);


/**
 * kbase_pm_check_transitions_nolock - Check if there are any power transitions
 *                                     to make, and if so start them.
 *
 * This function will check the desired_xx_state members of
 * struct kbase_pm_device_data and the actual status of the hardware to see if
 * any power transitions can be made at this time to make the hardware state
 * closer to the state desired by the power policy.
 *
 * The return value can be used to check whether all the desired cores are
 * available, and so whether it's worth submitting a job (e.g. from a Power
 * Management IRQ).
 *
 * Note that this still returns true when desired_xx_state has no
 * cores. That is: of the no cores desired, none were *un*available. In
 * this case, the caller may still need to try submitting jobs. This is because
 * the Core Availability Policy might have taken us to an intermediate state
 * where no cores are powered, before powering on more cores (e.g. for core
 * rotation)
 *
 * The caller must hold kbase_device.pm.power_change_lock
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return:      non-zero when all desired cores are available. That is,
 *              it's worthwhile for the caller to submit a job.
 *              false otherwise
 */
bool kbase_pm_check_transitions_nolock(struct kbase_device *kbdev);

/**
 * kbase_pm_check_transitions_sync - Synchronous and locking variant of
 *                                   kbase_pm_check_transitions_nolock()
 *
 * On returning, the desired state at the time of the call will have been met.
 *
 * There is nothing to stop the core being switched off by calls to
 * kbase_pm_release_cores() or kbase_pm_unrequest_cores(). Therefore, the
 * caller must have already made a call to
 * kbase_pm_request_cores()/kbase_pm_request_cores_sync() previously.
 *
 * The usual use-case for this is to ensure cores are 'READY' after performing
 * a GPU Reset.
 *
 * Unlike kbase_pm_check_transitions_nolock(), the caller must not hold
 * kbase_device.pm.power_change_lock, because this function will take that
 * lock itself.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_check_transitions_sync(struct kbase_device *kbdev);

/**
 * kbase_pm_update_cores_state_nolock - Variant of kbase_pm_update_cores_state()
 *                                      where the caller must hold
 *                                      kbase_device.pm.power_change_lock
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev);

/**
 * kbase_pm_update_cores_state - Update the desired state of shader cores from
 *                               the Power Policy, and begin any power
 *                               transitions.
 *
 * This function will update the desired_xx_state members of
 * struct kbase_pm_device_data by calling into the current Power Policy. It will
 * then begin power transitions to make the hardware acheive the desired shader
 * core state.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_update_cores_state(struct kbase_device *kbdev);

/**
 * kbase_pm_cancel_deferred_poweroff - Cancel any pending requests to power off
 *                                     the GPU and/or shader cores.
 *
 * This should be called by any functions which directly power off the GPU.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_cancel_deferred_poweroff(struct kbase_device *kbdev);

/**
 * kbasep_pm_init_core_use_bitmaps - Initialise data tracking the required
 *                                   and used cores.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_init_core_use_bitmaps(struct kbase_device *kbdev);

/**
 * kbasep_pm_metrics_init - Initialize the metrics gathering framework.
 *
 * This must be called before other metric gathering APIs are called.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: 0 on success, error code on error
 */
int kbasep_pm_metrics_init(struct kbase_device *kbdev);

/**
 * kbasep_pm_metrics_term - Terminate the metrics gathering framework.
 *
 * This must be called when metric gathering is no longer required. It is an
 * error to call any metrics gathering function (other than
 * kbasep_pm_metrics_init()) after calling this function.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_metrics_term(struct kbase_device *kbdev);

/**
 * kbase_pm_report_vsync - Function to be called by the frame buffer driver to
 *                         update the vsync metric.
 *
 * This function should be called by the frame buffer driver to update whether
 * the system is hitting the vsync target or not. buffer_updated should be true
 * if the vsync corresponded with a new frame being displayed, otherwise it
 * should be false. This function does not need to be called every vsync, but
 * only when the value of @buffer_updated differs from a previous call.
 *
 * @kbdev:          The kbase device structure for the device (must be a
 *                  valid pointer)
 * @buffer_updated: True if the buffer has been updated on this VSync,
 *                  false otherwise
 */
void kbase_pm_report_vsync(struct kbase_device *kbdev, int buffer_updated);

/**
 * kbase_pm_get_dvfs_action - Determine whether the DVFS system should change
 *                            the clock speed of the GPU.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function should be called regularly by the DVFS system to check whether
 * the clock speed of the GPU needs updating.
 */
void kbase_pm_get_dvfs_action(struct kbase_device *kbdev);

/**
 * kbase_pm_request_gpu_cycle_counter - Mark that the GPU cycle counter is
 *                                      needed
 *
 * If the caller is the first caller then the GPU cycle counters will be enabled
 * along with the l2 cache
 *
 * The GPU must be powered when calling this function (i.e.
 * kbase_pm_context_active() must have been called).
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_request_gpu_cycle_counter(struct kbase_device *kbdev);

/**
 * kbase_pm_request_gpu_cycle_counter_l2_is_on - Mark GPU cycle counter is
 *                                               needed (l2 cache already on)
 *
 * This is a version of the above function
 * (kbase_pm_request_gpu_cycle_counter()) suitable for being called when the
 * l2 cache is known to be on and assured to be on until the subsequent call of
 * kbase_pm_release_gpu_cycle_counter() such as when a job is submitted. It does
 * not sleep and can be called from atomic functions.
 *
 * The GPU must be powered when calling this function (i.e.
 * kbase_pm_context_active() must have been called) and the l2 cache must be
 * powered on.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_request_gpu_cycle_counter_l2_is_on(struct kbase_device *kbdev);

/**
 * kbase_pm_release_gpu_cycle_counter - Mark that the GPU cycle counter is no
 *                                      longer in use
 *
 * If the caller is the last caller then the GPU cycle counters will be
 * disabled. A request must have been made before a call to this.
 *
 * Caller must not hold the hwaccess_lock, as it will be taken in this function.
 * If the caller is already holding this lock then
 * kbase_pm_release_gpu_cycle_counter_nolock() must be used instead.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_release_gpu_cycle_counter(struct kbase_device *kbdev);

/**
 * kbase_pm_release_gpu_cycle_counter_nolock - Version of kbase_pm_release_gpu_cycle_counter()
 *                                             that does not take hwaccess_lock
 *
 * Caller must hold the hwaccess_lock.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_release_gpu_cycle_counter_nolock(struct kbase_device *kbdev);

/**
 * kbase_pm_wait_for_poweroff_complete - Wait for the poweroff workqueue to
 *                                       complete
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_wait_for_poweroff_complete(struct kbase_device *kbdev);

/**
 * kbase_pm_register_access_enable - Enable access to GPU registers
 *
 * Enables access to the GPU registers before power management has powered up
 * the GPU with kbase_pm_powerup().
 *
 * Access to registers should be done using kbase_os_reg_read()/write() at this
 * stage, not kbase_reg_read()/write().
 *
 * This results in the power management callbacks provided in the driver
 * configuration to get called to turn on power and/or clocks to the GPU. See
 * kbase_pm_callback_conf.
 *
 * This should only be used before power management is powered up with
 * kbase_pm_powerup()
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_register_access_enable(struct kbase_device *kbdev);

/**
 * kbase_pm_register_access_disable - Disable early register access
 *
 * Disables access to the GPU registers enabled earlier by a call to
 * kbase_pm_register_access_enable().
 *
 * This results in the power management callbacks provided in the driver
 * configuration to get called to turn off power and/or clocks to the GPU. See
 * kbase_pm_callback_conf
 *
 * This should only be used before power management is powered up with
 * kbase_pm_powerup()
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_register_access_disable(struct kbase_device *kbdev);

/* NOTE: kbase_pm_is_suspending is in mali_kbase.h, because it is an inline
 * function */

/**
 * kbase_pm_metrics_is_active - Check if the power management metrics
 *                              collection is active.
 *
 * Note that this returns if the power management metrics collection was
 * active at the time of calling, it is possible that after the call the metrics
 * collection enable may have changed state.
 *
 * The caller must handle the consequence that the state may have changed.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * Return: true if metrics collection was active else false.
 */
bool kbase_pm_metrics_is_active(struct kbase_device *kbdev);

/**
 * kbase_pm_do_poweron - Power on the GPU, and any cores that are requested.
 *
 * @kbdev:     The kbase device structure for the device (must be a valid
 *             pointer)
 * @is_resume: true if power on due to resume after suspend,
 *             false otherwise
 */
void kbase_pm_do_poweron(struct kbase_device *kbdev, bool is_resume);

/**
 * kbase_pm_do_poweroff - Power off the GPU, and any cores that have been
 *                        requested.
 *
 * @kbdev:      The kbase device structure for the device (must be a valid
 *              pointer)
 * @is_suspend: true if power off due to suspend,
 *              false otherwise
 */
void kbase_pm_do_poweroff(struct kbase_device *kbdev, bool is_suspend);

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS)
void kbase_pm_get_dvfs_utilisation(struct kbase_device *kbdev,
		unsigned long *total, unsigned long *busy);
void kbase_pm_reset_dvfs_utilisation(struct kbase_device *kbdev);
#endif /* defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS) */

#ifdef CONFIG_MALI_BIFROST_DVFS

/**
 * kbase_platform_dvfs_event - Report utilisation to DVFS code
 *
 * Function provided by platform specific code when DVFS is enabled to allow
 * the power management metrics system to report utilisation.
 *
 * @kbdev:         The kbase device structure for the device (must be a
 *                 valid pointer)
 * @utilisation:   The current calculated utilisation by the metrics system.
 * @util_gl_share: The current calculated gl share of utilisation.
 * @util_cl_share: The current calculated cl share of utilisation per core
 *                 group.
 * Return:         Returns 0 on failure and non zero on success.
 */

int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation,
	u32 util_gl_share, u32 util_cl_share[2]);
#endif

void kbase_pm_power_changed(struct kbase_device *kbdev);

/**
 * kbase_pm_metrics_update - Inform the metrics system that an atom is either
 *                           about to be run or has just completed.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @now:   Pointer to the timestamp of the change, or NULL to use current time
 *
 * Caller must hold hwaccess_lock
 */
void kbase_pm_metrics_update(struct kbase_device *kbdev,
				ktime_t *now);

/**
 * kbase_pm_cache_snoop_enable - Allow CPU snoops on the GPU
 * If the GPU does not have coherency this is a no-op
 * @kbdev:	Device pointer
 *
 * This function should be called after L2 power up.
 */

void kbase_pm_cache_snoop_enable(struct kbase_device *kbdev);

/**
 * kbase_pm_cache_snoop_disable - Prevent CPU snoops on the GPU
 * If the GPU does not have coherency this is a no-op
 * @kbdev:	Device pointer
 *
 * This function should be called before L2 power off.
 */
void kbase_pm_cache_snoop_disable(struct kbase_device *kbdev);

#endif /* _KBASE_BACKEND_PM_INTERNAL_H_ */
