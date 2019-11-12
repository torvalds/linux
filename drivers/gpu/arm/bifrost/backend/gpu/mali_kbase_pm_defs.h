/*
 *
 * (C) COPYRIGHT 2014-2019 ARM Limited. All rights reserved.
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

/*
 * Backend-specific Power Manager definitions
 */

#ifndef _KBASE_PM_HWACCESS_DEFS_H_
#define _KBASE_PM_HWACCESS_DEFS_H_

#include "mali_kbase_pm_always_on.h"
#include "mali_kbase_pm_coarse_demand.h"
#if !MALI_CUSTOMER_RELEASE
#include "mali_kbase_pm_always_on_demand.h"
#endif

/* Forward definition - see mali_kbase.h */
struct kbase_device;
struct kbase_jd_atom;

/**
 * enum kbase_pm_core_type - The types of core in a GPU.
 *
 * These enumerated values are used in calls to
 * - kbase_pm_get_present_cores()
 * - kbase_pm_get_active_cores()
 * - kbase_pm_get_trans_cores()
 * - kbase_pm_get_ready_cores().
 *
 * They specify which type of core should be acted on.  These values are set in
 * a manner that allows core_type_to_reg() function to be simpler and more
 * efficient.
 *
 * @KBASE_PM_CORE_L2: The L2 cache
 * @KBASE_PM_CORE_SHADER: Shader cores
 * @KBASE_PM_CORE_TILER: Tiler cores
 * @KBASE_PM_CORE_STACK: Core stacks
 */
enum kbase_pm_core_type {
	KBASE_PM_CORE_L2 = L2_PRESENT_LO,
	KBASE_PM_CORE_SHADER = SHADER_PRESENT_LO,
	KBASE_PM_CORE_TILER = TILER_PRESENT_LO,
	KBASE_PM_CORE_STACK = STACK_PRESENT_LO
};

/**
 * enum kbase_l2_core_state - The states used for the L2 cache & tiler power
 *                            state machine.
 *
 * @KBASE_L2_OFF: The L2 cache and tiler are off
 * @KBASE_L2_PEND_ON: The L2 cache and tiler are powering on
 * @KBASE_L2_RESTORE_CLOCKS: The GPU clock is restored. Conditionally used.
 * @KBASE_L2_ON_HWCNT_ENABLE: The L2 cache and tiler are on, and hwcnt is being
 *                            enabled
 * @KBASE_L2_ON: The L2 cache and tiler are on, and hwcnt is enabled
 * @KBASE_L2_ON_HWCNT_DISABLE: The L2 cache and tiler are on, and hwcnt is being
 *                             disabled
 * @KBASE_L2_SLOW_DOWN_CLOCKS: The GPU clock is set to appropriate or lowest
 *                             clock. Conditionally used.
 * @KBASE_L2_POWER_DOWN: The L2 cache and tiler are about to be powered off
 * @KBASE_L2_PEND_OFF: The L2 cache and tiler are powering off
 * @KBASE_L2_RESET_WAIT: The GPU is resetting, L2 cache and tiler power state
 *                       are unknown
 */
enum kbase_l2_core_state {
#define KBASEP_L2_STATE(n) KBASE_L2_ ## n,
#include "mali_kbase_pm_l2_states.h"
#undef KBASEP_L2_STATE
};

/**
 * enum kbase_shader_core_state - The states used for the shaders' state machine.
 *
 * @KBASE_SHADERS_OFF_CORESTACK_OFF: The shaders and core stacks are off
 * @KBASE_SHADERS_OFF_CORESTACK_PEND_ON: The shaders are off, core stacks have
 *                                       been requested to power on and hwcnt
 *                                       is being disabled
 * @KBASE_SHADERS_PEND_ON_CORESTACK_ON: Core stacks are on, shaders have been
 *                                      requested to power on. Or after doing
 *                                      partial shader on/off, checking whether
 *                                      it's the desired state.
 * @KBASE_SHADERS_ON_CORESTACK_ON: The shaders and core stacks are on, and hwcnt
 *					already enabled.
 * @KBASE_SHADERS_ON_CORESTACK_ON_RECHECK: The shaders and core stacks
 *                                      are on, hwcnt disabled, and checks
 *                                      to powering down or re-enabling
 *                                      hwcnt.
 * @KBASE_SHADERS_WAIT_OFF_CORESTACK_ON: The shaders have been requested to
 *                                       power off, but they remain on for the
 *                                       duration of the hysteresis timer
 * @KBASE_SHADERS_WAIT_GPU_IDLE: The shaders partial poweroff needs to reach
 *                               a state where jobs on the GPU are finished
 *                               including jobs currently running and in the
 *                               GPU queue because of GPU2017-861
 * @KBASE_SHADERS_WAIT_FINISHED_CORESTACK_ON: The hysteresis timer has expired
 * @KBASE_SHADERS_L2_FLUSHING_CORESTACK_ON: The core stacks are on and the
 *                                          level 2 cache is being flushed.
 * @KBASE_SHADERS_READY_OFF_CORESTACK_ON: The core stacks are on and the shaders
 *                                        are ready to be powered off.
 * @KBASE_SHADERS_PEND_OFF_CORESTACK_ON: The core stacks are on, and the shaders
 *                                       have been requested to power off
 * @KBASE_SHADERS_OFF_CORESTACK_PEND_OFF: The shaders are off, and the core stacks
 *                                        have been requested to power off
 * @KBASE_SHADERS_OFF_CORESTACK_OFF_TIMER_PEND_OFF: Shaders and corestacks are
 *                                                  off, but the tick timer
 *                                                  cancellation is still
 *                                                  pending.
 * @KBASE_SHADERS_RESET_WAIT: The GPU is resetting, shader and core stack power
 *                            states are unknown
 */
enum kbase_shader_core_state {
#define KBASEP_SHADER_STATE(n) KBASE_SHADERS_ ## n,
#include "mali_kbase_pm_shader_states.h"
#undef KBASEP_SHADER_STATE
};

/**
 * struct kbasep_pm_metrics - Metrics data collected for use by the power
 *                            management framework.
 *
 *  @time_busy: number of ns the GPU was busy executing jobs since the
 *          @time_period_start timestamp.
 *  @time_idle: number of ns since time_period_start the GPU was not executing
 *          jobs since the @time_period_start timestamp.
 *  @busy_cl: number of ns the GPU was busy executing CL jobs. Note that
 *           if two CL jobs were active for 400ns, this value would be updated
 *           with 800.
 *  @busy_gl: number of ns the GPU was busy executing GL jobs. Note that
 *           if two GL jobs were active for 400ns, this value would be updated
 *           with 800.
 */
struct kbasep_pm_metrics {
	u32 time_busy;
	u32 time_idle;
	u32 busy_cl[2];
	u32 busy_gl;
};

/**
 * struct kbasep_pm_metrics_state - State required to collect the metrics in
 *                                  struct kbasep_pm_metrics
 *  @time_period_start: time at which busy/idle measurements started
 *  @gpu_active: true when the GPU is executing jobs. false when
 *           not. Updated when the job scheduler informs us a job in submitted
 *           or removed from a GPU slot.
 *  @active_cl_ctx: number of CL jobs active on the GPU. Array is per-device.
 *  @active_gl_ctx: number of GL jobs active on the GPU. Array is per-slot.
 *  @lock: spinlock protecting the kbasep_pm_metrics_data structure
 *  @platform_data: pointer to data controlled by platform specific code
 *  @kbdev: pointer to kbase device for which metrics are collected
 *  @values: The current values of the power management metrics. The
 *           kbase_pm_get_dvfs_metrics() function is used to compare these
 *           current values with the saved values from a previous invocation.
 *  @timer: timer to regularly make DVFS decisions based on the power
 *           management metrics.
 *  @timer_active: boolean indicating @timer is running
 *  @dvfs_last: values of the PM metrics from the last DVFS tick
 *  @dvfs_diff: different between the current and previous PM metrics.
 */
struct kbasep_pm_metrics_state {
	ktime_t time_period_start;
	bool gpu_active;
	u32 active_cl_ctx[2];
	u32 active_gl_ctx[3];
	spinlock_t lock;

	void *platform_data;
	struct kbase_device *kbdev;

	struct kbasep_pm_metrics values;

#ifdef CONFIG_MALI_BIFROST_DVFS
	struct hrtimer timer;
	bool timer_active;
	struct kbasep_pm_metrics dvfs_last;
	struct kbasep_pm_metrics dvfs_diff;
#endif
};

/**
 * struct kbasep_pm_tick_timer_state - State for the shader hysteresis timer
 * @wq: Work queue to wait for the timer to stopped
 * @work: Work item which cancels the timer
 * @timer: Timer for powering off the shader cores
 * @configured_interval: Period of GPU poweroff timer
 * @configured_ticks: User-configured number of ticks to wait after the shader
 *                    power down request is received before turning off the cores
 * @remaining_ticks: Number of remaining timer ticks until shaders are powered off
 * @cancel_queued: True if the cancellation work item has been queued. This is
 *                 required to ensure that it is not queued twice, e.g. after
 *                 a reset, which could cause the timer to be incorrectly
 *                 cancelled later by a delayed workitem.
 * @needed: Whether the timer should restart itself
 */
struct kbasep_pm_tick_timer_state {
	struct workqueue_struct *wq;
	struct work_struct work;
	struct hrtimer timer;

	ktime_t configured_interval;
	unsigned int configured_ticks;
	unsigned int remaining_ticks;

	bool cancel_queued;
	bool needed;
};

union kbase_pm_policy_data {
	struct kbasep_pm_policy_always_on always_on;
	struct kbasep_pm_policy_coarse_demand coarse_demand;
#if !MALI_CUSTOMER_RELEASE
	struct kbasep_pm_policy_always_on_demand always_on_demand;
#endif
};

/**
 * struct kbase_pm_backend_data - Data stored per device for power management.
 *
 * This structure contains data for the power management framework. There is one
 * instance of this structure per device in the system.
 *
 * @pm_current_policy: The policy that is currently actively controlling the
 *                     power state.
 * @pm_policy_data:    Private data for current PM policy
 * @reset_done:        Flag when a reset is complete
 * @reset_done_wait:   Wait queue to wait for changes to @reset_done
 * @gpu_cycle_counter_requests: The reference count of active gpu cycle counter
 *                              users
 * @gpu_cycle_counter_requests_lock: Lock to protect @gpu_cycle_counter_requests
 * @gpu_in_desired_state_wait: Wait queue set when the GPU is in the desired
 *                             state according to the L2 and shader power state
 *                             machines
 * @gpu_powered:       Set to true when the GPU is powered and register
 *                     accesses are possible, false otherwise. Access to this
 *                     variable should be protected by: both the hwaccess_lock
 *                     spinlock and the pm.lock mutex for writes; or at least
 *                     one of either lock for reads.
 * @pm_shaders_core_mask: Shader PM state synchronised shaders core mask. It
 *                     holds the cores enabled in a hardware counters dump,
 *                     and may differ from @shaders_avail when under different
 *                     states and transitions.
 * @cg1_disabled:      Set if the policy wants to keep the second core group
 *                     powered off
 * @driver_ready_for_irqs: Debug state indicating whether sufficient
 *                         initialization of the driver has occurred to handle
 *                         IRQs
 * @metrics:           Structure to hold metrics for the GPU
 * @shader_tick_timer: Structure to hold the shader poweroff tick timer state
 * @poweroff_wait_in_progress: true if a wait for GPU power off is in progress.
 *                             hwaccess_lock must be held when accessing
 * @invoke_poweroff_wait_wq_when_l2_off: flag indicating that the L2 power state
 *                                       machine should invoke the poweroff
 *                                       worker after the L2 has turned off.
 * @poweron_required: true if a GPU power on is required. Should only be set
 *                    when poweroff_wait_in_progress is true, and therefore the
 *                    GPU can not immediately be powered on. pm.lock must be
 *                    held when accessing
 * @gpu_poweroff_wait_wq: workqueue for waiting for GPU to power off
 * @gpu_poweroff_wait_work: work item for use with @gpu_poweroff_wait_wq
 * @poweroff_wait: waitqueue for waiting for @gpu_poweroff_wait_work to complete
 * @callback_power_on: Callback when the GPU needs to be turned on. See
 *                     &struct kbase_pm_callback_conf
 * @callback_power_off: Callback when the GPU may be turned off. See
 *                     &struct kbase_pm_callback_conf
 * @callback_power_suspend: Callback when a suspend occurs and the GPU needs to
 *                          be turned off. See &struct kbase_pm_callback_conf
 * @callback_power_resume: Callback when a resume occurs and the GPU needs to
 *                          be turned on. See &struct kbase_pm_callback_conf
 * @callback_power_runtime_on: Callback when the GPU needs to be turned on. See
 *                             &struct kbase_pm_callback_conf
 * @callback_power_runtime_off: Callback when the GPU may be turned off. See
 *                              &struct kbase_pm_callback_conf
 * @callback_power_runtime_idle: Optional callback when the GPU may be idle. See
 *                              &struct kbase_pm_callback_conf
 * @callback_soft_reset: Optional callback to software reset the GPU. See
 *                       &struct kbase_pm_callback_conf
 * @ca_cores_enabled: Cores that are currently available
 * @l2_state:     The current state of the L2 cache state machine. See
 *                &enum kbase_l2_core_state
 * @l2_desired:   True if the L2 cache should be powered on by the L2 cache state
 *                machine
 * @l2_always_on: If true, disable powering down of l2 cache.
 * @shaders_state: The current state of the shader state machine.
 * @shaders_avail: This is updated by the state machine when it is in a state
 *                 where it can handle changes to the core availability. This
 *                 is internal to the shader state machine and should *not* be
 *                 modified elsewhere.
 * @shaders_desired: True if the PM active count or power policy requires the
 *                   shader cores to be on. This is used as an input to the
 *                   shader power state machine.  The current state of the
 *                   cores may be different, but there should be transitions in
 *                   progress that will eventually achieve this state (assuming
 *                   that the policy doesn't change its mind in the mean time).
 * @in_reset: True if a GPU is resetting and normal power manager operation is
 *            suspended
 * @partial_shaderoff: True if we want to partial power off shader cores,
 *                     it indicates a partial shader core off case,
 *                     do some special operation for such case like flush
 *                     L2 cache because of GPU2017-861
 * @protected_entry_transition_override : True if GPU reset is being used
 *                                  before entering the protected mode and so
 *                                  the reset handling behaviour is being
 *                                  overridden.
 * @protected_transition_override : True if a protected mode transition is in
 *                                  progress and is overriding power manager
 *                                  behaviour.
 * @protected_l2_override : Non-zero if the L2 cache is required during a
 *                          protected mode transition. Has no effect if not
 *                          transitioning.
 * @hwcnt_desired: True if we want GPU hardware counters to be enabled.
 * @hwcnt_disabled: True if GPU hardware counters are not enabled.
 * @hwcnt_disable_work: Work item to disable GPU hardware counters, used if
 *                      atomic disable is not possible.
 * @gpu_clock_suspend_freq: 'opp-mali-errata-1485982' clock in opp table
 *                          for safe L2 power cycle.
 *                          If no opp-mali-errata-1485982 specified,
 *                          the slowest clock will be taken.
 * @gpu_clock_slow_down_wa: If true, slow down GPU clock during L2 power cycle.
 * @gpu_clock_slow_down_desired: True if we want lower GPU clock
 *                             for safe L2 power cycle. False if want GPU clock
 *                             to back to normalized one. This is updated only
 *                             in L2 state machine, kbase_pm_l2_update_state.
 * @gpu_clock_slowed_down: During L2 power cycle,
 *                         True if gpu clock is set at lower frequency
 *                         for safe L2 power down, False if gpu clock gets
 *                         restored to previous speed. This is updated only in
 *                         work function, kbase_pm_gpu_clock_control_worker.
 * @gpu_clock_control_work: work item to set GPU clock during L2 power cycle
 *                          using gpu_clock_control
 *
 * Note:
 * During an IRQ, @pm_current_policy can be NULL when the policy is being
 * changed with kbase_pm_set_policy(). The change is protected under
 * kbase_device.pm.pcower_change_lock. Direct access to this from IRQ context
 * must therefore check for NULL. If NULL, then kbase_pm_set_policy() will
 * re-issue the policy functions that would have been done under IRQ.
 */
struct kbase_pm_backend_data {
	const struct kbase_pm_policy *pm_current_policy;
	union kbase_pm_policy_data pm_policy_data;
	bool reset_done;
	wait_queue_head_t reset_done_wait;
	int gpu_cycle_counter_requests;
	spinlock_t gpu_cycle_counter_requests_lock;

	wait_queue_head_t gpu_in_desired_state_wait;

	bool gpu_powered;

	u64 pm_shaders_core_mask;

	bool cg1_disabled;

#ifdef CONFIG_MALI_BIFROST_DEBUG
	bool driver_ready_for_irqs;
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	struct kbasep_pm_metrics_state metrics;

	struct kbasep_pm_tick_timer_state shader_tick_timer;

	bool poweroff_wait_in_progress;
	bool invoke_poweroff_wait_wq_when_l2_off;
	bool poweron_required;

	struct workqueue_struct *gpu_poweroff_wait_wq;
	struct work_struct gpu_poweroff_wait_work;

	wait_queue_head_t poweroff_wait;

	int (*callback_power_on)(struct kbase_device *kbdev);
	void (*callback_power_off)(struct kbase_device *kbdev);
	void (*callback_power_suspend)(struct kbase_device *kbdev);
	void (*callback_power_resume)(struct kbase_device *kbdev);
	int (*callback_power_runtime_on)(struct kbase_device *kbdev);
	void (*callback_power_runtime_off)(struct kbase_device *kbdev);
	int (*callback_power_runtime_idle)(struct kbase_device *kbdev);
	int (*callback_soft_reset)(struct kbase_device *kbdev);

	u64 ca_cores_enabled;

	enum kbase_l2_core_state l2_state;
	enum kbase_shader_core_state shaders_state;
	u64 shaders_avail;
	bool l2_desired;
	bool l2_always_on;
	bool shaders_desired;

	bool in_reset;

	bool partial_shaderoff;

	bool protected_entry_transition_override;
	bool protected_transition_override;
	int protected_l2_override;

	bool hwcnt_desired;
	bool hwcnt_disabled;
	struct work_struct hwcnt_disable_work;

	u64 gpu_clock_suspend_freq;
	bool gpu_clock_slow_down_wa;
	bool gpu_clock_slow_down_desired;
	bool gpu_clock_slowed_down;
	struct work_struct gpu_clock_control_work;
};


/* List of policy IDs */
enum kbase_pm_policy_id {
	KBASE_PM_POLICY_ID_COARSE_DEMAND,
#if !MALI_CUSTOMER_RELEASE
	KBASE_PM_POLICY_ID_ALWAYS_ON_DEMAND,
#endif
	KBASE_PM_POLICY_ID_ALWAYS_ON
};

/**
 * struct kbase_pm_policy - Power policy structure.
 *
 * Each power policy exposes a (static) instance of this structure which
 * contains function pointers to the policy's methods.
 *
 * @name:               The name of this policy
 * @init:               Function called when the policy is selected
 * @term:               Function called when the policy is unselected
 * @shaders_needed:     Function called to find out if shader cores are needed
 * @get_core_active:    Function called to get the current overall GPU power
 *                      state
 * @id:                 Field indicating an ID for this policy. This is not
 *                      necessarily the same as its index in the list returned
 *                      by kbase_pm_list_policies().
 *                      It is used purely for debugging.
 */
struct kbase_pm_policy {
	char *name;

	/**
	 * Function called when the policy is selected
	 *
	 * This should initialize the kbdev->pm.pm_policy_data structure. It
	 * should not attempt to make any changes to hardware state.
	 *
	 * It is undefined what state the cores are in when the function is
	 * called.
	 *
	 * @kbdev: The kbase device structure for the device (must be a
	 *         valid pointer)
	 */
	void (*init)(struct kbase_device *kbdev);

	/**
	 * Function called when the policy is unselected.
	 *
	 * @kbdev: The kbase device structure for the device (must be a
	 *         valid pointer)
	 */
	void (*term)(struct kbase_device *kbdev);

	/**
	 * Function called to find out if shader cores are needed
	 *
	 * This needs to at least satisfy kbdev->pm.backend.shaders_desired,
	 * and so must never return false when shaders_desired is true.
	 *
	 * @kbdev: The kbase device structure for the device (must be a
	 *         valid pointer)
	 *
	 * Return: true if shader cores are needed, false otherwise
	 */
	bool (*shaders_needed)(struct kbase_device *kbdev);

	/**
	 * Function called to get the current overall GPU power state
	 *
	 * This function must meet or exceed the requirements for power
	 * indicated by kbase_pm_is_active().
	 *
	 * @kbdev: The kbase device structure for the device (must be a
	 *         valid pointer)
	 *
	 * Return: true if the GPU should be powered, false otherwise
	 */
	bool (*get_core_active)(struct kbase_device *kbdev);

	enum kbase_pm_policy_id id;
};

#endif /* _KBASE_PM_HWACCESS_DEFS_H_ */
