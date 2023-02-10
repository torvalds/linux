/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * Power management API definitions used internally by GPU backend
 */

#ifndef _KBASE_BACKEND_PM_INTERNAL_H_
#define _KBASE_BACKEND_PM_INTERNAL_H_

#include <mali_kbase_hwaccess_pm.h>

#include "backend/gpu/mali_kbase_pm_ca.h"
#include "mali_kbase_pm_policy.h"


/**
 * kbase_pm_dev_idle - The GPU is idle.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * The OS may choose to turn off idle devices
 */
void kbase_pm_dev_idle(struct kbase_device *kbdev);

/**
 * kbase_pm_dev_activate - The GPU is active.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * The OS should avoid opportunistically turning off the GPU while it is active
 */
void kbase_pm_dev_activate(struct kbase_device *kbdev);

/**
 * kbase_pm_get_present_cores - Get details of the cores that are present in
 *                              the device.
 *
 * @kbdev: The kbase device structure for the device (must be a valid
 *         pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) present in the GPU device and also a count of
 * the number of cores.
 *
 * Return: The bit mask of cores present
 */
u64 kbase_pm_get_present_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_get_active_cores - Get details of the cores that are currently
 *                             active in the device.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) that are actively processing work (i.e.
 * turned on *and* busy).
 *
 * Return: The bit mask of active cores
 */
u64 kbase_pm_get_active_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_get_trans_cores - Get details of the cores that are currently
 *                            transitioning between power states.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) that are currently transitioning between
 * power states.
 *
 * Return: The bit mask of transitioning cores
 */
u64 kbase_pm_get_trans_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_get_ready_cores - Get details of the cores that are currently
 *                            powered and ready for jobs.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @type:  The type of core (see the enum kbase_pm_core_type enumeration)
 *
 * This function can be called by the active power policy to return a bitmask of
 * the cores (of a specified type) that are powered and ready for jobs (they may
 * or may not be currently executing jobs).
 *
 * Return: The bit mask of ready cores
 */
u64 kbase_pm_get_ready_cores(struct kbase_device *kbdev,
						enum kbase_pm_core_type type);

/**
 * kbase_pm_clock_on - Turn the clock for the device on, and enable device
 *                     interrupts.
 *
 * @kbdev:     The kbase device structure for the device (must be a valid
 *             pointer)
 * @is_resume: true if clock on due to resume after suspend, false otherwise
 *
 * This function can be used by a power policy to turn the clock for the GPU on.
 * It should be modified during integration to perform the necessary actions to
 * ensure that the GPU is fully powered and clocked.
 */
void kbase_pm_clock_on(struct kbase_device *kbdev, bool is_resume);

/**
 * kbase_pm_clock_off - Disable device interrupts, and turn the clock for the
 *                      device off.
 *
 * @kbdev:      The kbase device structure for the device (must be a valid
 *              pointer)
 *
 * This function can be used by a power policy to turn the clock for the GPU
 * off. It should be modified during integration to perform the necessary
 * actions to turn the clock off (if this is possible in the integration).
 *
 * If runtime PM is enabled and @power_runtime_gpu_idle_callback is used
 * then this function would usually be invoked from the runtime suspend
 * callback function.
 *
 * Return: true  if clock was turned off, or
 *         false if clock can not be turned off due to pending page/bus fault
 *               workers. Caller must flush MMU workqueues and retry
 */
bool kbase_pm_clock_off(struct kbase_device *kbdev);

/**
 * kbase_pm_enable_interrupts - Enable interrupts on the device.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Interrupts are also enabled after a call to kbase_pm_clock_on().
 */
void kbase_pm_enable_interrupts(struct kbase_device *kbdev);

/**
 * kbase_pm_disable_interrupts - Disable interrupts on the device.
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This prevents delivery of Power Management interrupts to the CPU so that
 * kbase_pm_update_state() will not be called from the IRQ handler
 * until kbase_pm_enable_interrupts() or kbase_pm_clock_on() is called.
 *
 * Interrupts are also disabled after a call to kbase_pm_clock_off().
 */
void kbase_pm_disable_interrupts(struct kbase_device *kbdev);

/**
 * kbase_pm_disable_interrupts_nolock - Version of kbase_pm_disable_interrupts()
 *                                      that does not take the hwaccess_lock
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Caller must hold the hwaccess_lock.
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
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function must be called by the GPU interrupt handler when the
 * RESET_COMPLETED bit is set. It signals to the power management initialization
 * code that the GPU has been successfully reset.
 */
void kbase_pm_reset_done(struct kbase_device *kbdev);

#if MALI_USE_CSF
/**
 * kbase_pm_wait_for_desired_state - Wait for the desired power state to be
 *                                   reached
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Wait for the L2 and MCU state machines to reach the states corresponding
 * to the values of 'kbase_pm_is_l2_desired' and 'kbase_pm_is_mcu_desired'.
 *
 * The usual use-case for this is to ensure that all parts of GPU have been
 * powered up after performing a GPU Reset.
 *
 * Unlike kbase_pm_update_state(), the caller must not hold hwaccess_lock,
 * because this function will take that lock itself.
 *
 * NOTE: This may not wait until the correct state is reached if there is a
 * power off in progress and kbase_pm_context_active() was called instead of
 * kbase_csf_scheduler_pm_active().
 *
 * Return: 0 on success, error code on error
 */
int kbase_pm_wait_for_desired_state(struct kbase_device *kbdev);
#else
/**
 * kbase_pm_wait_for_desired_state - Wait for the desired power state to be
 *                                   reached
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Wait for the L2 and shader power state machines to reach the states
 * corresponding to the values of 'l2_desired' and 'shaders_desired'.
 *
 * The usual use-case for this is to ensure cores are 'READY' after performing
 * a GPU Reset.
 *
 * Unlike kbase_pm_update_state(), the caller must not hold hwaccess_lock,
 * because this function will take that lock itself.
 *
 * NOTE: This may not wait until the correct state is reached if there is a
 * power off in progress. To correctly wait for the desired state the caller
 * must ensure that this is not the case by, for example, calling
 * kbase_pm_wait_for_poweroff_work_complete()
 *
 * Return: 0 on success, error code on error
 */
int kbase_pm_wait_for_desired_state(struct kbase_device *kbdev);
#endif

/**
 * kbase_pm_wait_for_l2_powered - Wait for the L2 cache to be powered on
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Wait for the L2 to be powered on, and for the L2 and the state machines of
 * its dependent stack components to stabilise.
 *
 * kbdev->pm.active_count must be non-zero when calling this function.
 *
 * Unlike kbase_pm_update_state(), the caller must not hold hwaccess_lock,
 * because this function will take that lock itself.
 *
 * Return: 0 on success, error code on error
 */
int kbase_pm_wait_for_l2_powered(struct kbase_device *kbdev);

#if MALI_USE_CSF
/**
 * kbase_pm_wait_for_cores_down_scale - Wait for the downscaling of shader cores
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function can be called to ensure that the downscaling of cores is
 * effectively complete and it would be safe to lower the voltage.
 * The function assumes that caller had exercised the MCU state machine for the
 * downscale request through the kbase_pm_update_state() function.
 *
 * This function needs to be used by the caller to safely wait for the completion
 * of downscale request, instead of kbase_pm_wait_for_desired_state().
 * The downscale request would trigger a state change in MCU state machine
 * and so when MCU reaches the stable ON state, it can be inferred that
 * downscaling is complete. But it has been observed that the wake up of the
 * waiting thread can get delayed by few milli seconds and by the time the
 * thread wakes up the power down transition could have started (after the
 * completion of downscale request).
 * On the completion of power down transition another wake up signal would be
 * sent, but again by the time thread wakes up the power up transition can begin.
 * And the power up transition could then get blocked inside the platform specific
 * callback_power_on() function due to the thread that called into Kbase (from the
 * platform specific code) to perform the downscaling and then ended up waiting
 * for the completion of downscale request.
 *
 * Return: 0 on success, error code on error or remaining jiffies on timeout.
 */
int kbase_pm_wait_for_cores_down_scale(struct kbase_device *kbdev);
#endif

/**
 * kbase_pm_update_dynamic_cores_onoff - Update the L2 and shader power state
 *                                       machines after changing shader core
 *                                       availability
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * It can be called in any status, so need to check the l2 and shader core
 * power status in this function or it will break shader/l2 state machine
 *
 * Caller must hold hwaccess_lock
 */
void kbase_pm_update_dynamic_cores_onoff(struct kbase_device *kbdev);

/**
 * kbase_pm_update_cores_state_nolock - Variant of kbase_pm_update_cores_state()
 *                                      where the caller must hold
 *                                      kbase_device.hwaccess_lock
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev);

/**
 * kbase_pm_update_state - Update the L2 and shader power state machines
 * @kbdev: Device pointer
 */
void kbase_pm_update_state(struct kbase_device *kbdev);

/**
 * kbase_pm_state_machine_init - Initialize the state machines, primarily the
 *                               shader poweroff timer
 * @kbdev: Device pointer
 *
 * Return: 0 on success, error code on error
 */
int kbase_pm_state_machine_init(struct kbase_device *kbdev);

/**
 * kbase_pm_state_machine_term - Clean up the PM state machines' data
 * @kbdev: Device pointer
 */
void kbase_pm_state_machine_term(struct kbase_device *kbdev);

/**
 * kbase_pm_update_cores_state - Update the desired state of shader cores from
 *                               the Power Policy, and begin any power
 *                               transitions.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function will update the desired_xx_state members of
 * struct kbase_pm_device_data by calling into the current Power Policy. It will
 * then begin power transitions to make the hardware acheive the desired shader
 * core state.
 */
void kbase_pm_update_cores_state(struct kbase_device *kbdev);

/**
 * kbasep_pm_metrics_init - Initialize the metrics gathering framework.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This must be called before other metric gathering APIs are called.
 *
 *
 * Return: 0 on success, error code on error
 */
int kbasep_pm_metrics_init(struct kbase_device *kbdev);

/**
 * kbasep_pm_metrics_term - Terminate the metrics gathering framework.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This must be called when metric gathering is no longer required. It is an
 * error to call any metrics gathering function (other than
 * kbasep_pm_metrics_init()) after calling this function.
 */
void kbasep_pm_metrics_term(struct kbase_device *kbdev);

/**
 * kbase_pm_report_vsync - Function to be called by the frame buffer driver to
 *                         update the vsync metric.
 * @kbdev:          The kbase device structure for the device (must be a
 *                  valid pointer)
 * @buffer_updated: True if the buffer has been updated on this VSync,
 *                  false otherwise
 *
 * This function should be called by the frame buffer driver to update whether
 * the system is hitting the vsync target or not. buffer_updated should be true
 * if the vsync corresponded with a new frame being displayed, otherwise it
 * should be false. This function does not need to be called every vsync, but
 * only when the value of @buffer_updated differs from a previous call.
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
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * If the caller is the first caller then the GPU cycle counters will be enabled
 * along with the l2 cache
 *
 * The GPU must be powered when calling this function (i.e.
 * kbase_pm_context_active() must have been called).
 *
 */
void kbase_pm_request_gpu_cycle_counter(struct kbase_device *kbdev);

/**
 * kbase_pm_request_gpu_cycle_counter_l2_is_on - Mark GPU cycle counter is
 *                                               needed (l2 cache already on)
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
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
 */
void kbase_pm_request_gpu_cycle_counter_l2_is_on(struct kbase_device *kbdev);

/**
 * kbase_pm_release_gpu_cycle_counter - Mark that the GPU cycle counter is no
 *                                      longer in use
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * If the caller is the last caller then the GPU cycle counters will be
 * disabled. A request must have been made before a call to this.
 *
 * Caller must not hold the hwaccess_lock, as it will be taken in this function.
 * If the caller is already holding this lock then
 * kbase_pm_release_gpu_cycle_counter_nolock() must be used instead.
 */
void kbase_pm_release_gpu_cycle_counter(struct kbase_device *kbdev);

/**
 * kbase_pm_release_gpu_cycle_counter_nolock - Version of kbase_pm_release_gpu_cycle_counter()
 *                                             that does not take hwaccess_lock
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Caller must hold the hwaccess_lock.
 */
void kbase_pm_release_gpu_cycle_counter_nolock(struct kbase_device *kbdev);

/**
 * kbase_pm_wait_for_poweroff_work_complete - Wait for the poweroff workqueue to
 *                                            complete
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function effectively just waits for the @gpu_poweroff_wait_work work
 * item to complete, if it was enqueued. GPU may not have been powered down
 * before this function returns.
 */
void kbase_pm_wait_for_poweroff_work_complete(struct kbase_device *kbdev);

/**
 * kbase_pm_wait_for_gpu_power_down - Wait for the GPU power down to complete
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function waits for the actual gpu power down to complete.
 */
void kbase_pm_wait_for_gpu_power_down(struct kbase_device *kbdev);

/**
 * kbase_pm_runtime_init - Initialize runtime-pm for Mali GPU platform device
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Setup the power management callbacks and initialize/enable the runtime-pm
 * for the Mali GPU platform device, using the callback function. This must be
 * called before the kbase_pm_register_access_enable() function.
 *
 * Return: 0 on success, error code on error
 */
int kbase_pm_runtime_init(struct kbase_device *kbdev);

/**
 * kbase_pm_runtime_term - Disable runtime-pm for Mali GPU platform device
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_runtime_term(struct kbase_device *kbdev);

/**
 * kbase_pm_register_access_enable - Enable access to GPU registers
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Enables access to the GPU registers before power management has powered up
 * the GPU with kbase_pm_powerup().
 *
 * This results in the power management callbacks provided in the driver
 * configuration to get called to turn on power and/or clocks to the GPU. See
 * kbase_pm_callback_conf.
 *
 * This should only be used before power management is powered up with
 * kbase_pm_powerup()
 */
void kbase_pm_register_access_enable(struct kbase_device *kbdev);

/**
 * kbase_pm_register_access_disable - Disable early register access
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
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
 */
void kbase_pm_register_access_disable(struct kbase_device *kbdev);

/* NOTE: kbase_pm_is_suspending is in mali_kbase.h, because it is an inline
 * function
 */

/**
 * kbase_pm_metrics_is_active - Check if the power management metrics
 *                              collection is active.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Note that this returns if the power management metrics collection was
 * active at the time of calling, it is possible that after the call the metrics
 * collection enable may have changed state.
 *
 * The caller must handle the consequence that the state may have changed.
 *
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
 */
void kbase_pm_do_poweroff(struct kbase_device *kbdev);

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS)
void kbase_pm_get_dvfs_metrics(struct kbase_device *kbdev,
			       struct kbasep_pm_metrics *last,
			       struct kbasep_pm_metrics *diff);
#endif /* defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS) */

#ifdef CONFIG_MALI_BIFROST_DVFS

#if MALI_USE_CSF
/**
 * kbase_platform_dvfs_event - Report utilisation to DVFS code for CSF GPU
 *
 * @kbdev:         The kbase device structure for the device (must be a
 *                 valid pointer)
 * @utilisation:   The current calculated utilisation by the metrics system.
 *
 * Function provided by platform specific code when DVFS is enabled to allow
 * the power management metrics system to report utilisation.
 *
 * Return:         Returns 0 on failure and non zero on success.
 */
int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation);
#else
/**
 * kbase_platform_dvfs_event - Report utilisation to DVFS code for JM GPU
 *
 * @kbdev:         The kbase device structure for the device (must be a
 *                 valid pointer)
 * @utilisation:   The current calculated utilisation by the metrics system.
 * @util_gl_share: The current calculated gl share of utilisation.
 * @util_cl_share: The current calculated cl share of utilisation per core
 *                 group.
 * Function provided by platform specific code when DVFS is enabled to allow
 * the power management metrics system to report utilisation.
 *
 * Return:         Returns 0 on failure and non zero on success.
 */
int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation,
			      u32 util_gl_share, u32 util_cl_share[2]);
#endif

#endif /* CONFIG_MALI_BIFROST_DVFS */

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

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
/**
 * kbase_devfreq_set_core_mask - Set devfreq core mask
 * @kbdev:     Device pointer
 * @core_mask: New core mask
 *
 * This function is used by devfreq to change the available core mask as
 * required by Dynamic Core Scaling.
 */
void kbase_devfreq_set_core_mask(struct kbase_device *kbdev, u64 core_mask);
#endif

/**
 * kbase_pm_reset_start_locked - Signal that GPU reset has started
 * @kbdev: Device pointer
 *
 * Normal power management operation will be suspended until the reset has
 * completed.
 *
 * Caller must hold hwaccess_lock.
 */
void kbase_pm_reset_start_locked(struct kbase_device *kbdev);

/**
 * kbase_pm_reset_complete - Signal that GPU reset has completed
 * @kbdev: Device pointer
 *
 * Normal power management operation will be resumed. The power manager will
 * re-evaluate what cores are needed and power on or off as required.
 */
void kbase_pm_reset_complete(struct kbase_device *kbdev);

#if !MALI_USE_CSF
/**
 * kbase_pm_protected_override_enable - Enable the protected mode override
 * @kbdev: Device pointer
 *
 * When the protected mode override is enabled, all shader cores are requested
 * to power down, and the L2 power state can be controlled by
 * kbase_pm_protected_l2_override().
 *
 * Caller must hold hwaccess_lock.
 */
void kbase_pm_protected_override_enable(struct kbase_device *kbdev);

/**
 * kbase_pm_protected_override_disable - Disable the protected mode override
 * @kbdev: Device pointer
 *
 * Caller must hold hwaccess_lock.
 */
void kbase_pm_protected_override_disable(struct kbase_device *kbdev);

/**
 * kbase_pm_protected_l2_override - Control the protected mode L2 override
 * @kbdev: Device pointer
 * @override: true to enable the override, false to disable
 *
 * When the driver is transitioning in or out of protected mode, the L2 cache is
 * forced to power off. This can be overridden to force the L2 cache to power
 * on. This is required to change coherency settings on some GPUs.
 */
void kbase_pm_protected_l2_override(struct kbase_device *kbdev, bool override);

/**
 * kbase_pm_protected_entry_override_enable - Enable the protected mode entry
 *                                            override
 * @kbdev: Device pointer
 *
 * Initiate a GPU reset and enable the protected mode entry override flag if
 * l2_always_on WA is enabled and platform is fully coherent. If the GPU
 * reset is already ongoing then protected mode entry override flag will not
 * be enabled and function will have to be called again.
 *
 * When protected mode entry override flag is enabled to power down L2 via GPU
 * reset, the GPU reset handling behavior gets changed. For example call to
 * kbase_backend_reset() is skipped, Hw counters are not re-enabled and L2
 * isn't powered up again post reset.
 * This is needed only as a workaround for a Hw issue where explicit power down
 * of L2 causes a glitch. For entering protected mode on fully coherent
 * platforms L2 needs to be powered down to switch to IO coherency mode, so to
 * avoid the glitch GPU reset is used to power down L2. Hence, this function
 * does nothing on systems where the glitch issue isn't present.
 *
 * Caller must hold hwaccess_lock. Should be only called during the transition
 * to enter protected mode.
 *
 * Return: -EAGAIN if a GPU reset was required for the glitch workaround but
 * was already ongoing, otherwise 0.
 */
int kbase_pm_protected_entry_override_enable(struct kbase_device *kbdev);

/**
 * kbase_pm_protected_entry_override_disable - Disable the protected mode entry
 *                                             override
 * @kbdev: Device pointer
 *
 * This shall be called once L2 has powered down and switch to IO coherency
 * mode has been made. As with kbase_pm_protected_entry_override_enable(),
 * this function does nothing on systems where the glitch issue isn't present.
 *
 * Caller must hold hwaccess_lock. Should be only called during the transition
 * to enter protected mode.
 */
void kbase_pm_protected_entry_override_disable(struct kbase_device *kbdev);
#endif

/* If true, the driver should explicitly control corestack power management,
 * instead of relying on the Power Domain Controller.
 */
extern bool corestack_driver_control;

/**
 * kbase_pm_is_l2_desired - Check whether l2 is desired
 *
 * @kbdev: Device pointer
 *
 * This shall be called to check whether l2 is needed to power on
 *
 * Return: true if l2 need to power on
 */
bool kbase_pm_is_l2_desired(struct kbase_device *kbdev);

#if MALI_USE_CSF
/**
 * kbase_pm_is_mcu_desired - Check whether MCU is desired
 *
 * @kbdev: Device pointer
 *
 * This shall be called to check whether MCU needs to be enabled.
 *
 * Return: true if MCU needs to be enabled.
 */
bool kbase_pm_is_mcu_desired(struct kbase_device *kbdev);

/**
 * kbase_pm_is_mcu_inactive - Check if the MCU is inactive (i.e. either
 *                            it is disabled or it is in sleep)
 *
 * @kbdev: kbase device
 * @state: state of the MCU state machine.
 *
 * This function must be called with hwaccess_lock held.
 * L2 cache can be turned off if this function returns true.
 *
 * Return: true if MCU is inactive
 */
bool kbase_pm_is_mcu_inactive(struct kbase_device *kbdev,
			      enum kbase_mcu_state state);

/**
 * kbase_pm_idle_groups_sched_suspendable - Check whether the scheduler can be
 *                                        suspended to low power state when all
 *                                        the CSGs are idle
 *
 * @kbdev: Device pointer
 *
 * Return: true if allowed to enter the suspended state.
 */
static inline
bool kbase_pm_idle_groups_sched_suspendable(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return !(kbdev->pm.backend.csf_pm_sched_flags &
		 CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE);
}

/**
 * kbase_pm_no_runnables_sched_suspendable - Check whether the scheduler can be
 *                                        suspended to low power state when
 *                                        there are no runnable CSGs.
 *
 * @kbdev: Device pointer
 *
 * Return: true if allowed to enter the suspended state.
 */
static inline
bool kbase_pm_no_runnables_sched_suspendable(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return !(kbdev->pm.backend.csf_pm_sched_flags &
		 CSF_DYNAMIC_PM_SCHED_NO_SUSPEND);
}

/**
 * kbase_pm_no_mcu_core_pwroff - Check whether the PM is required to keep the
 *                               MCU shader Core powered in accordance to the active
 *                               power management policy
 *
 * @kbdev: Device pointer
 *
 * Return: true if the MCU is to retain powered.
 */
static inline bool kbase_pm_no_mcu_core_pwroff(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return kbdev->pm.backend.csf_pm_sched_flags &
		CSF_DYNAMIC_PM_CORE_KEEP_ON;
}

/**
 * kbase_pm_mcu_is_in_desired_state - Check if MCU is in stable ON/OFF state.
 *
 * @kbdev: Device pointer
 *
 * Return: true if MCU is in stable ON/OFF state.
 */
static inline bool kbase_pm_mcu_is_in_desired_state(struct kbase_device *kbdev)
{
	bool in_desired_state = true;

	if (kbase_pm_is_mcu_desired(kbdev) && kbdev->pm.backend.mcu_state != KBASE_MCU_ON)
		in_desired_state = false;
	else if (!kbase_pm_is_mcu_desired(kbdev) &&
		 (kbdev->pm.backend.mcu_state != KBASE_MCU_OFF) &&
		 (kbdev->pm.backend.mcu_state != KBASE_MCU_IN_SLEEP))
		in_desired_state = false;

	return in_desired_state;
}

#endif

/**
 * kbase_pm_l2_is_in_desired_state - Check if L2 is in stable ON/OFF state.
 *
 * @kbdev: Device pointer
 *
 * Return: true if L2 is in stable ON/OFF state.
 */
static inline bool kbase_pm_l2_is_in_desired_state(struct kbase_device *kbdev)
{
	bool in_desired_state = true;

	if (kbase_pm_is_l2_desired(kbdev) && kbdev->pm.backend.l2_state != KBASE_L2_ON)
		in_desired_state = false;
	else if (!kbase_pm_is_l2_desired(kbdev) && kbdev->pm.backend.l2_state != KBASE_L2_OFF)
		in_desired_state = false;

	return in_desired_state;
}

/**
 * kbase_pm_lock - Lock all necessary mutexes to perform PM actions
 *
 * @kbdev: Device pointer
 *
 * This function locks correct mutexes independent of GPU architecture.
 */
static inline void kbase_pm_lock(struct kbase_device *kbdev)
{
#if !MALI_USE_CSF
	mutex_lock(&kbdev->js_data.runpool_mutex);
#endif /* !MALI_USE_CSF */
	mutex_lock(&kbdev->pm.lock);
}

/**
 * kbase_pm_unlock - Unlock mutexes locked by kbase_pm_lock
 *
 * @kbdev: Device pointer
 */
static inline void kbase_pm_unlock(struct kbase_device *kbdev)
{
	mutex_unlock(&kbdev->pm.lock);
#if !MALI_USE_CSF
	mutex_unlock(&kbdev->js_data.runpool_mutex);
#endif /* !MALI_USE_CSF */
}

#if MALI_USE_CSF && defined(KBASE_PM_RUNTIME)
/**
 * kbase_pm_gpu_sleep_allowed - Check if the GPU is allowed to be put in sleep
 *
 * @kbdev: Device pointer
 *
 * This function is called on GPU idle notification and if it returns false then
 * GPU power down will be triggered by suspending the CSGs and halting the MCU.
 *
 * Return: true if the GPU is allowed to be in the sleep state.
 */
static inline bool kbase_pm_gpu_sleep_allowed(struct kbase_device *kbdev)
{
	/* If the autosuspend_delay has been set to 0 then it doesn't make
	 * sense to first put GPU to sleep state and then power it down,
	 * instead would be better to power it down right away.
	 * Also need to do the same when autosuspend_delay is set to a negative
	 * value, which implies that runtime pm is effectively disabled by the
	 * kernel.
	 * A high positive value of autosuspend_delay can be used to keep the
	 * GPU in sleep state for a long time.
	 */
	if (unlikely(!kbdev->dev->power.autosuspend_delay ||
		     (kbdev->dev->power.autosuspend_delay < 0)))
		return false;

	return kbdev->pm.backend.gpu_sleep_supported;
}

/**
 * kbase_pm_enable_db_mirror_interrupt - Enable the doorbell mirror interrupt to
 *                                       detect the User doorbell rings.
 *
 * @kbdev: Device pointer
 *
 * This function is called just before sending the sleep request to MCU firmware
 * so that User doorbell rings can be detected whilst GPU remains in the sleep
 * state.
 *
 */
static inline void kbase_pm_enable_db_mirror_interrupt(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!kbdev->pm.backend.db_mirror_interrupt_enabled) {
		u32 irq_mask = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(GPU_IRQ_MASK));

		WARN_ON(irq_mask & DOORBELL_MIRROR);

		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask | DOORBELL_MIRROR);
		kbdev->pm.backend.db_mirror_interrupt_enabled = true;
	}
}

/**
 * kbase_pm_disable_db_mirror_interrupt - Disable the doorbell mirror interrupt.
 *
 * @kbdev: Device pointer
 *
 * This function is called when doorbell mirror interrupt is received or MCU
 * needs to be reactivated by enabling the doorbell notification.
 */
static inline void kbase_pm_disable_db_mirror_interrupt(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.db_mirror_interrupt_enabled) {
		u32 irq_mask = kbase_reg_read(kbdev,
				GPU_CONTROL_REG(GPU_IRQ_MASK));

		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask & ~DOORBELL_MIRROR);
		kbdev->pm.backend.db_mirror_interrupt_enabled = false;
	}
}
#endif

/**
 * kbase_pm_l2_allow_mmu_page_migration - L2 state allows MMU page migration or not
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Check whether the L2 state is in power transition phase or not. If it is, the MMU
 * page migration should be deferred. The caller must hold hwaccess_lock, and, if MMU
 * page migration is intended, immediately start the MMU migration action without
 * dropping the lock. When page migration begins, a flag is set in kbdev that would
 * prevent the L2 state machine traversing into power transition phases, until
 * the MMU migration action ends.
 *
 * Return: true if MMU page migration is allowed
 */
static inline bool kbase_pm_l2_allow_mmu_page_migration(struct kbase_device *kbdev)
{
	struct kbase_pm_backend_data *backend = &kbdev->pm.backend;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	return (backend->l2_state != KBASE_L2_PEND_ON && backend->l2_state != KBASE_L2_PEND_OFF);
}

#endif /* _KBASE_BACKEND_PM_INTERNAL_H_ */
