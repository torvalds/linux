/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
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
 * Backend-specific Power Manager definitions
 */

#ifndef _KBASE_PM_HWACCESS_DEFS_H_
#define _KBASE_PM_HWACCESS_DEFS_H_

#include "mali_kbase_pm_always_on.h"
#include "mali_kbase_pm_coarse_demand.h"

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM)
#define KBASE_PM_RUNTIME 1
#endif

/* Forward definition - see mali_kbase.h */
struct kbase_device;
struct kbase_jd_atom;

/**
 * enum kbase_pm_core_type - The types of core in a GPU.
 *
 * @KBASE_PM_CORE_L2: The L2 cache
 * @KBASE_PM_CORE_SHADER: Shader cores
 * @KBASE_PM_CORE_TILER: Tiler cores
 * @KBASE_PM_CORE_STACK: Core stacks
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
 */
enum kbase_pm_core_type {
	KBASE_PM_CORE_L2 = L2_PRESENT_LO,
	KBASE_PM_CORE_SHADER = SHADER_PRESENT_LO,
	KBASE_PM_CORE_TILER = TILER_PRESENT_LO,
	KBASE_PM_CORE_STACK = STACK_PRESENT_LO
};

/*
 * enum kbase_l2_core_state - The states used for the L2 cache & tiler power
 *                            state machine.
 */
enum kbase_l2_core_state {
#define KBASEP_L2_STATE(n) KBASE_L2_ ## n,
#include "mali_kbase_pm_l2_states.h"
#undef KBASEP_L2_STATE
};

#if MALI_USE_CSF
/*
 * enum kbase_mcu_state - The states used for the MCU state machine.
 */
enum kbase_mcu_state {
#define KBASEP_MCU_STATE(n) KBASE_MCU_ ## n,
#include "mali_kbase_pm_mcu_states.h"
#undef KBASEP_MCU_STATE
};
#endif

/*
 * enum kbase_shader_core_state - The states used for the shaders' state machine.
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
 *  @time_busy: the amount of time the GPU was busy executing jobs since the
 *          @time_period_start timestamp, in units of 256ns. This also includes
 *          time_in_protm, the time spent in protected mode, since it's assumed
 *          the GPU was busy 100% during this period.
 *  @time_idle: the amount of time the GPU was not executing jobs since the
 *              time_period_start timestamp, measured in units of 256ns.
 *  @time_in_protm: The amount of time the GPU has spent in protected mode since
 *                  the time_period_start timestamp, measured in units of 256ns.
 *  @busy_cl: the amount of time the GPU was busy executing CL jobs. Note that
 *           if two CL jobs were active for 256ns, this value would be updated
 *           with 2 (2x256ns).
 *  @busy_gl: the amount of time the GPU was busy executing GL jobs. Note that
 *           if two GL jobs were active for 256ns, this value would be updated
 *           with 2 (2x256ns).
 */
struct kbasep_pm_metrics {
	u32 time_busy;
	u32 time_idle;
#if MALI_USE_CSF
	u32 time_in_protm;
#else
	u32 busy_cl[2];
	u32 busy_gl;
#endif
};

/**
 * struct kbasep_pm_metrics_state - State required to collect the metrics in
 *                                  struct kbasep_pm_metrics
 *  @time_period_start: time at which busy/idle measurements started
 *  @ipa_control_client: Handle returned on registering DVFS as a
 *                       kbase_ipa_control client
 *  @skip_gpu_active_sanity_check: Decide whether to skip GPU_ACTIVE sanity
 *                                 check in DVFS utilisation calculation
 *  @gpu_active: true when the GPU is executing jobs. false when
 *           not. Updated when the job scheduler informs us a job in submitted
 *           or removed from a GPU slot.
 *  @active_cl_ctx: number of CL jobs active on the GPU. Array is per-device.
 *  @active_gl_ctx: number of GL jobs active on the GPU. Array is per-slot.
 *  @lock: spinlock protecting the kbasep_pm_metrics_state structure
 *  @platform_data: pointer to data controlled by platform specific code
 *  @kbdev: pointer to kbase device for which metrics are collected
 *  @values: The current values of the power management metrics. The
 *           kbase_pm_get_dvfs_metrics() function is used to compare these
 *           current values with the saved values from a previous invocation.
 *  @initialized: tracks whether metrics_state has been initialized or not.
 *  @timer: timer to regularly make DVFS decisions based on the power
 *           management metrics.
 *  @timer_state: atomic indicating current @timer state, on, off, or stopped.
 *  @dvfs_last: values of the PM metrics from the last DVFS tick
 *  @dvfs_diff: different between the current and previous PM metrics.
 */
struct kbasep_pm_metrics_state {
	ktime_t time_period_start;
#if MALI_USE_CSF
	void *ipa_control_client;
	bool skip_gpu_active_sanity_check;
#else
	bool gpu_active;
	u32 active_cl_ctx[2];
	u32 active_gl_ctx[3];
#endif
	spinlock_t lock;

	void *platform_data;
	struct kbase_device *kbdev;

	struct kbasep_pm_metrics values;

#ifdef CONFIG_MALI_BIFROST_DVFS
	bool initialized;
	struct hrtimer timer;
	atomic_t timer_state;
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
 * @default_ticks: User-configured number of ticks to wait after the shader
 *                 power down request is received before turning off the cores
 * @configured_ticks: Power-policy configured number of ticks to wait after the
 *                    shader power down request is received before turning off
 *                    the cores. For simple power policies, this is equivalent
 *                    to @default_ticks.
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
	unsigned int default_ticks;
	unsigned int configured_ticks;
	unsigned int remaining_ticks;

	bool cancel_queued;
	bool needed;
};

union kbase_pm_policy_data {
	struct kbasep_pm_policy_always_on always_on;
	struct kbasep_pm_policy_coarse_demand coarse_demand;
};

/**
 * struct kbase_pm_backend_data - Data stored per device for power management.
 *
 * @pm_current_policy: The policy that is currently actively controlling the
 *                     power state.
 * @pm_policy_data:    Private data for current PM policy. This is automatically
 *                     zeroed when a policy change occurs.
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
 * @gpu_ready:         Indicates whether the GPU is in a state in which it is
 *                     safe to perform PM changes. When false, the PM state
 *                     machine needs to wait before making changes to the GPU
 *                     power policy, DevFreq or core_mask, so as to avoid these
 *                     changing while implicit GPU resets are ongoing.
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
 * @callback_power_runtime_idle: Optional callback invoked by runtime PM core
 *                               when the GPU may be idle. See
 *                               &struct kbase_pm_callback_conf
 * @callback_soft_reset: Optional callback to software reset the GPU. See
 *                       &struct kbase_pm_callback_conf
 * @callback_power_runtime_gpu_idle: Callback invoked by Kbase when GPU has
 *                                   become idle.
 *                                   See &struct kbase_pm_callback_conf.
 * @callback_power_runtime_gpu_active: Callback when GPU has become active and
 *                                     @callback_power_runtime_gpu_idle was
 *                                     called previously.
 *                                     See &struct kbase_pm_callback_conf.
 * @ca_cores_enabled: Cores that are currently available
 * @mcu_state: The current state of the micro-control unit, only applicable
 *             to GPUs that have such a component
 * @l2_state:     The current state of the L2 cache state machine. See
 *                &enum kbase_l2_core_state
 * @l2_desired:   True if the L2 cache should be powered on by the L2 cache state
 *                machine
 * @l2_always_on: If true, disable powering down of l2 cache.
 * @shaders_state: The current state of the shader state machine.
 * @shaders_avail: This is updated by the state machine when it is in a state
 *                 where it can write to the SHADER_PWRON or PWROFF registers
 *                 to have the same set of available cores as specified by
 *                 @shaders_desired_mask. So would precisely indicate the cores
 *                 that are currently available. This is internal to shader
 *                 state machine of JM GPUs and should *not* be modified
 *                 elsewhere.
 * @shaders_desired_mask: This is updated by the state machine when it is in
 *                        a state where it can handle changes to the core
 *                        availability (either by DVFS or sysfs). This is
 *                        internal to the shader state machine and should
 *                        *not* be modified elsewhere.
 * @shaders_desired: True if the PM active count or power policy requires the
 *                   shader cores to be on. This is used as an input to the
 *                   shader power state machine.  The current state of the
 *                   cores may be different, but there should be transitions in
 *                   progress that will eventually achieve this state (assuming
 *                   that the policy doesn't change its mind in the mean time).
 * @mcu_desired: True if the micro-control unit should be powered on
 * @policy_change_clamp_state_to_off: Signaling the backend is in PM policy
 *                change transition, needs the mcu/L2 to be brought back to the
 *                off state and remain in that state until the flag is cleared.
 * @csf_pm_sched_flags: CSF Dynamic PM control flags in accordance to the
 *                current active PM policy. This field is updated whenever a
 *                new policy is activated.
 * @policy_change_lock: Used to serialize the policy change calls. In CSF case,
 *                      the change of policy may involve the scheduler to
 *                      suspend running CSGs and then reconfigure the MCU.
 * @core_idle_wq: Workqueue for executing the @core_idle_work.
 * @core_idle_work: Work item used to wait for undesired cores to become inactive.
 *                  The work item is enqueued when Host controls the power for
 *                  shader cores and down scaling of cores is performed.
 * @gpu_sleep_supported: Flag to indicate that if GPU sleep feature can be
 *                       supported by the kernel driver or not. If this
 *                       flag is not set, then HW state is directly saved
 *                       when GPU idle notification is received.
 * @gpu_sleep_mode_active: Flag to indicate that the GPU needs to be in sleep
 *                         mode. It is set when the GPU idle notification is
 *                         received and is cleared when HW state has been
 *                         saved in the runtime suspend callback function or
 *                         when the GPU power down is aborted if GPU became
 *                         active whilst it was in sleep mode. The flag is
 *                         guarded with hwaccess_lock spinlock.
 * @exit_gpu_sleep_mode: Flag to indicate the GPU can now exit the sleep
 *                       mode due to the submission of work from Userspace.
 *                       The flag is guarded with hwaccess_lock spinlock.
 *                       The @gpu_sleep_mode_active flag is not immediately
 *                       reset when this flag is set, this is to ensure that
 *                       MCU doesn't gets disabled undesirably without the
 *                       suspend of CSGs. That could happen when
 *                       scheduler_pm_active() and scheduler_pm_idle() gets
 *                       called before the Scheduler gets reactivated.
 * @gpu_idled: Flag to ensure that the gpu_idle & gpu_active callbacks are
 *             always called in pair. The flag is guarded with pm.lock mutex.
 * @gpu_wakeup_override: Flag to force the power up of L2 cache & reactivation
 *                       of MCU. This is set during the runtime suspend
 *                       callback function, when GPU needs to exit the sleep
 *                       mode for the saving the HW state before power down.
 * @db_mirror_interrupt_enabled: Flag tracking if the Doorbell mirror interrupt
 *                               is enabled or not.
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
 * This structure contains data for the power management framework. There is one
 * instance of this structure per device in the system.
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
	bool gpu_ready;

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
	void (*callback_power_runtime_gpu_idle)(struct kbase_device *kbdev);
	void (*callback_power_runtime_gpu_active)(struct kbase_device *kbdev);

	u64 ca_cores_enabled;

#if MALI_USE_CSF
	enum kbase_mcu_state mcu_state;
#endif
	enum kbase_l2_core_state l2_state;
	enum kbase_shader_core_state shaders_state;
	u64 shaders_avail;
	u64 shaders_desired_mask;
#if MALI_USE_CSF
	bool mcu_desired;
	bool policy_change_clamp_state_to_off;
	unsigned int csf_pm_sched_flags;
	struct mutex policy_change_lock;
	struct workqueue_struct *core_idle_wq;
	struct work_struct core_idle_work;

#ifdef KBASE_PM_RUNTIME
	bool gpu_sleep_supported;
	bool gpu_sleep_mode_active;
	bool exit_gpu_sleep_mode;
	bool gpu_idled;
	bool gpu_wakeup_override;
	bool db_mirror_interrupt_enabled;
#endif
#endif
	bool l2_desired;
	bool l2_always_on;
	bool shaders_desired;

	bool in_reset;

#if !MALI_USE_CSF
	bool partial_shaderoff;

	bool protected_entry_transition_override;
	bool protected_transition_override;
	int protected_l2_override;
#endif

	bool hwcnt_desired;
	bool hwcnt_disabled;
	struct work_struct hwcnt_disable_work;

	u64 gpu_clock_suspend_freq;
	bool gpu_clock_slow_down_wa;
	bool gpu_clock_slow_down_desired;
	bool gpu_clock_slowed_down;
	struct work_struct gpu_clock_control_work;
};

#if MALI_USE_CSF
/* CSF PM flag, signaling that the MCU shader Core should be kept on */
#define  CSF_DYNAMIC_PM_CORE_KEEP_ON (1 << 0)
/* CSF PM flag, signaling no scheduler suspension on idle groups */
#define CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE (1 << 1)
/* CSF PM flag, signaling no scheduler suspension on no runnable groups */
#define CSF_DYNAMIC_PM_SCHED_NO_SUSPEND (1 << 2)

/* The following flags corresponds to existing defined PM policies */
#define ALWAYS_ON_PM_SCHED_FLAGS (CSF_DYNAMIC_PM_CORE_KEEP_ON | \
				  CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE | \
				  CSF_DYNAMIC_PM_SCHED_NO_SUSPEND)
#define COARSE_ON_DEMAND_PM_SCHED_FLAGS (0)
#if !MALI_CUSTOMER_RELEASE
#define ALWAYS_ON_DEMAND_PM_SCHED_FLAGS (CSF_DYNAMIC_PM_SCHED_IGNORE_IDLE)
#endif
#endif

/* List of policy IDs */
enum kbase_pm_policy_id {
	KBASE_PM_POLICY_ID_COARSE_DEMAND,
#if !MALI_CUSTOMER_RELEASE
	KBASE_PM_POLICY_ID_ALWAYS_ON_DEMAND,
#endif
	KBASE_PM_POLICY_ID_ALWAYS_ON
};

/**
 * enum kbase_pm_policy_event - PM Policy event ID
 */
enum kbase_pm_policy_event {
	/**
	 * @KBASE_PM_POLICY_EVENT_IDLE: Indicates that the GPU power state
	 * model has determined that the GPU has gone idle.
	 */
	KBASE_PM_POLICY_EVENT_IDLE,
	/**
	 * @KBASE_PM_POLICY_EVENT_POWER_ON: Indicates that the GPU state model
	 * is preparing to power on the GPU.
	 */
	KBASE_PM_POLICY_EVENT_POWER_ON,
	/**
	 * @KBASE_PM_POLICY_EVENT_TIMER_HIT: Indicates that the GPU became
	 * active while the Shader Tick Timer was holding the GPU in a powered
	 * on state.
	 */
	KBASE_PM_POLICY_EVENT_TIMER_HIT,
	/**
	 * @KBASE_PM_POLICY_EVENT_TIMER_MISS: Indicates that the GPU did not
	 * become active before the Shader Tick Timer timeout occurred.
	 */
	KBASE_PM_POLICY_EVENT_TIMER_MISS,
};

/**
 * struct kbase_pm_policy - Power policy structure.
 *
 * @name:               The name of this policy
 * @init:               Function called when the policy is selected
 * @term:               Function called when the policy is unselected
 * @shaders_needed:     Function called to find out if shader cores are needed
 * @get_core_active:    Function called to get the current overall GPU power
 *                      state
 * @handle_event:       Function called when a PM policy event occurs. Should be
 *                      set to NULL if the power policy doesn't require any
 *                      event notifications.
 * @id:                 Field indicating an ID for this policy. This is not
 *                      necessarily the same as its index in the list returned
 *                      by kbase_pm_list_policies().
 *                      It is used purely for debugging.
 * @pm_sched_flags: Policy associated with CSF PM scheduling operational flags.
 *                  Pre-defined required flags exist for each of the
 *                  ARM released policies, such as 'always_on', 'coarse_demand'
 *                  and etc.
 * Each power policy exposes a (static) instance of this structure which
 * contains function pointers to the policy's methods.
 */
struct kbase_pm_policy {
	char *name;

	/*
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

	/*
	 * Function called when the policy is unselected.
	 *
	 * @kbdev: The kbase device structure for the device (must be a
	 *         valid pointer)
	 */
	void (*term)(struct kbase_device *kbdev);

	/*
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

	/*
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

	/*
	 * Function called when a power event occurs
	 *
	 * @kbdev: The kbase device structure for the device (must be a
	 *         valid pointer)
	 * @event: The id of the power event that has occurred
	 */
	void (*handle_event)(struct kbase_device *kbdev,
			     enum kbase_pm_policy_event event);

	enum kbase_pm_policy_id id;

#if MALI_USE_CSF
	/* Policy associated with CSF PM scheduling operational flags.
	 * There are pre-defined required flags exist for each of the
	 * ARM released policies, such as 'always_on', 'coarse_demand'
	 * and etc.
	 */
	unsigned int pm_sched_flags;
#endif
};

#endif /* _KBASE_PM_HWACCESS_DEFS_H_ */
