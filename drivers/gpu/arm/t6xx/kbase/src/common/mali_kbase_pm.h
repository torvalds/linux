/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_pm.h
 * Power management API definitions
 */

#ifndef _KBASE_PM_H_
#define _KBASE_PM_H_

#include <kbase/src/common/mali_midg_regmap.h>
#include <asm/atomic.h>

#include "mali_kbase_pm_always_on.h"
#include "mali_kbase_pm_demand.h"
#include "mali_kbase_pm_coarse_demand.h"

/* Forward definition - see mali_kbase.h */
struct kbase_device;

/** List of policy IDs */
typedef enum kbase_pm_policy_id
{
	KBASE_PM_POLICY_ID_DEMAND = 1,
	KBASE_PM_POLICY_ID_ALWAYS_ON,
	KBASE_PM_POLICY_ID_COARSE_DEMAND
} kbase_pm_policy_id;

/** The types of core in a GPU.
 *
 * These enumerated values are used in calls to @ref kbase_pm_invoke_power_up, @ref kbase_pm_invoke_power_down, @ref
 * kbase_pm_get_present_cores, @ref kbase_pm_get_active_cores, @ref kbase_pm_get_trans_cores, @ref
 * kbase_pm_get_ready_cores. The specify which type of core should be acted on.
 * These values are set in a manner that allows @ref core_type_to_reg function to be simpler and more efficient.
 */
typedef enum kbase_pm_core_type
{
	KBASE_PM_CORE_L3     = L3_PRESENT_LO,       /**< The L3 cache */
	KBASE_PM_CORE_L2     = L2_PRESENT_LO,       /**< The L2 cache */
	KBASE_PM_CORE_SHADER = SHADER_PRESENT_LO,   /**< Shader cores */
	KBASE_PM_CORE_TILER  = TILER_PRESENT_LO     /**< Tiler cores */
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
 * No event can make the pm system turn on the GPU after this function returns.
 * The active policy is sent @ref KBASE_PM_EVENT_SYSTEM_SUSPEND.
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

/** Events that can be sent to a power policy.
 *
 * Power policies are expected to handle all these events, although they may choose to take no action.
 */
typedef enum kbase_pm_event
{
	/* helper for tests */
	KBASEP_PM_EVENT_FIRST,

	/** Initialize the power policy.
	 *
	 * This event is sent immediately after the @ref kbase_pm_policy.init function of the policy returns.
	 *
	 * The policy may decide to transition the cores to its 'normal' state (e.g. an always on policy would turn all
	 * the cores on). The policy should assume that the GPU is in active use (i.e. as if the @ref
	 * KBASE_PM_EVENT_GPU_ACTIVE event had been received), if this is not the case then @ref KBASE_PM_EVENT_GPU_IDLE
	 * will be called after this event has been handled.
	 */
	KBASE_PM_EVENT_POLICY_INIT = KBASEP_PM_EVENT_FIRST,
	/** The power state of the device has changed.
	 *
	 * This event is sent when the GPU raises an interrupt to announce that a power transition has finished. Because
	 * there may be multiple power transitions the power policy must interrogate the state of the GPU to check whether
	 * all expected transitions have finished. If the GPU has just turned on or off then the policy must call @ref
	 * kbase_pm_power_up_done or @ref kbase_pm_power_down_done as appropriate.
	 */
	KBASE_PM_EVENT_GPU_STATE_CHANGED,
	/** The GPU is becoming active.
	 *
	 * This event is sent when the first context is about to use the GPU.
	 *
	 * If the core is turned off then this event must cause the core to turn on. This is done asynchronously and the
	 * policy must call the function kbase_pm_power_up_done to signal that the core is turned on sufficiently to allow
	 * register access.
	 */
	KBASE_PM_EVENT_GPU_ACTIVE,
	/** The GPU is becoming idle.
	 *
	 * This event is sent when the last context has finished using the GPU.
	 *
	 * The power policy may turn the GPU off entirely (e.g. turn the clocks or power off).
	 */
	KBASE_PM_EVENT_GPU_IDLE,
	/** The system has requested a change of power policy.
	 *
	 * The current policy receives this message when a request to change policy occurs. It must ensure that all active
	 * power transitions are completed and then call the @ref kbase_pm_change_policy function.
	 *
	 * This event is only delivered when the policy has been informed that the GPU is 'active' (the power management
	 * code internally increments the context active counter during a policy change).
	 */
	KBASE_PM_EVENT_POLICY_CHANGE,
	/** The system is requesting to suspend the GPU.
	 *
	 * The power policy should ensure that the GPU is shut down sufficiently for the system to suspend the device.
	 * Once the GPU is ready the policy should call @ref kbase_pm_power_down_done.
	 */
	KBASE_PM_EVENT_SYSTEM_SUSPEND,
	/** The system is requesting to resume the GPU.
	 *
	 * The power policy should restore the GPU to the state it was before the previous
	 * @ref KBASE_PM_EVENT_SYSTEM_SUSPEND event. If the GPU is being powered up then it should call
	 * @ref kbase_pm_power_transitioning before changing the state and @ref kbase_pm_power_up_done when
	 * the transition is complete.
	 */
	KBASE_PM_EVENT_SYSTEM_RESUME,
	/** The job scheduler is requesting to power up/down cores.
	 *
	 * This event is sent when:
	 * - powered down cores are needed to complete a job
	 * - powered up cores are not needed anymore
	 */
	KBASE_PM_EVENT_CHANGE_GPU_STATE,

	/* helpers for tests */
	KBASEP_PM_EVENT_LAST = KBASE_PM_EVENT_CHANGE_GPU_STATE,
	KBASEP_PM_EVENT_INVALID
} kbase_pm_event;

/** Flags that give information about Power Policies */
enum
{
	/** This policy does not power up/down cores and L2/L3 caches individually,
	 * outside of KBASE_PM_EVENT_GPU_IDLE and KBASE_PM_EVENT_GPU_ACTIVE events.
	 * That is, the policy guarantees all cores/L2/L3 caches will be powered
	 * after a KBASE_PM_EVENT_GPU_ACTIVE event.
	 *
	 * Hence, it does not need to be sent KBASE_PM_EVENT_CHANGE_GPU_STATE
	 * events.  */
	KBASE_PM_POLICY_FLAG_NO_CORE_TRANSITIONS = (1u << 0)
};

typedef u32 kbase_pm_policy_flags;


typedef union kbase_pm_policy_data
{
	kbasep_pm_policy_always_on  always_on;
	kbasep_pm_policy_demand     demand;
	kbasep_pm_policy_coarse_demand coarse_demand;
} kbase_pm_policy_data;

/** Power policy structure.
 *
 * Each power management policy exposes a (static) instance of this structure which contains function pointers to the
 * policy's methods.
 */
typedef struct kbase_pm_policy
{
	/** The name of this policy */
	char *name;

	/** Function called when the policy is selected
	 *
	 * This should initialize the kbdev->pm.policy_data pointer to the policy's data structure. It should not attempt
	 * to make any changes to hardware state.
	 *
	 * It is undefined what state the cores are in when the function is called, however no power transitions should be
	 * occurring.
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 */
	void (*init)(struct kbase_device *kbdev);
	/** Function called when the policy is unselected.
	 *
	 * This should free any data allocated with \c init
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 */
	void (*term)(struct kbase_device *kbdev);
	/** Function called when there is an event to process
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 * @param event     The event to process
	 */
	void (*event)(struct kbase_device *kbdev, kbase_pm_event event);
	/** Field indicating flags for this policy */
	kbase_pm_policy_flags flags;
	/** Field indicating an ID for this policy. This is not necessarily the
	 * same as its index in the list returned by kbase_pm_list_policies().
	 * It is used purely for debugging. */
	kbase_pm_policy_id id;
} kbase_pm_policy;

/** Metrics data collected for use by the power management framework.
 *
 */
typedef struct kbasep_pm_metrics_data
{
	int                 vsync_hit;
	int                 utilisation;

	ktime_t             time_period_start;
	u32                 time_busy;
	u32                 time_idle;
	mali_bool           gpu_active;

	spinlock_t    lock;

	struct hrtimer      timer;
	mali_bool           timer_active;

	void *              platform_data;
	struct kbase_device * kbdev;
} kbasep_pm_metrics_data;

/** Actions for DVFS.
 *
 * kbase_pm_get_dvfs_action will return one of these enumerated values to
 * describe the action that the DVFS system should take.
 */
typedef enum kbase_pm_dvfs_action
{
	KBASE_PM_DVFS_NOP,          /**< No change in clock frequency is requested */
	KBASE_PM_DVFS_CLOCK_UP,     /**< The clock frequency should be increased if possible */
	KBASE_PM_DVFS_CLOCK_DOWN    /**< The clock frequency should be decreased if possible */
} kbase_pm_dvfs_action;

/** A value for an atomic @ref kbase_pm_device_data::work_active,
 * which tracks whether the work unit has been enqueued.
 */
typedef enum kbase_pm_work_active_state
{
	KBASE_PM_WORK_ACTIVE_STATE_INACTIVE    = 0x00u, /**< There are no work units enqueued and @ref kbase_pm_worker is not running. */
	KBASE_PM_WORK_ACTIVE_STATE_ENQUEUED    = 0x01u, /**< There is a work unit enqueued, but @ref kbase_pm_worker is not running. */
	KBASE_PM_WORK_ACTIVE_STATE_PROCESSING  = 0x02u, /**< @ref kbase_pm_worker is running. */
	KBASE_PM_WORK_ACTIVE_STATE_PENDING_EVT = 0x03u  /**< Processing and there's an event outstanding.
                                                            @ref kbase_pm_worker is running, but @ref kbase_pm_device_data::pending_events
                                                            has been updated since it started so
                                                            it should recheck the list of pending events before exiting. */
} kbase_pm_work_active_state;

/** Data stored per device for power management.
 *
 * This structure contains data for the power management framework. There is one instance of this structure per device
 * in the system.
 */
typedef struct kbase_pm_device_data
{
	/** The policy that is currently actively controlling the power state. */
	const kbase_pm_policy   *current_policy;
	/** The policy that the system is transitioning to. */
	const kbase_pm_policy   *new_policy;
	/** The data needed for the current policy. This is considered private to the policy. */
	kbase_pm_policy_data    policy_data;
	/** The workqueue that the policy callbacks are executed on. */
	struct workqueue_struct *workqueue;
	/** A bit mask of events that are waiting to be delivered to the active policy. */
	atomic_t              pending_events;
	/** The work unit that is enqueued onto the workqueue. */
	struct work_struct      work;
	/** An atomic which tracks whether the work unit has been enqueued.
	 * For list of possible values please refer to @ref kbase_pm_work_active_state.
	 */
	atomic_t                work_active;

	/** Power state and a queue to wait for changes */
	#define PM_POWER_STATE_OFF   1
	#define PM_POWER_STATE_TRANS 2
	#define PM_POWER_STATE_ON    3
	int                     power_state;
	wait_queue_head_t       power_state_wait;

	/** Wait queue for whether the l2 cache has been powered as requested */
	wait_queue_head_t       l2_powered_wait;
	/** State indicating whether all the l2 caches are powered.
	 * Non-zero indicates they're *all* powered
	 * Zero indicates that some (or all) are not powered */
	int                     l2_powered;

	int                     no_outstanding_event;
	wait_queue_head_t       no_outstanding_event_wait;

	/** The reference count of active contexts on this device. */
	int                     active_count;
	/** Lock to protect active_count */
	spinlock_t        active_count_lock;
	/** The reference count of active gpu cycle counter users */
	int                     gpu_cycle_counter_requests;
	/** Lock to protect gpu_cycle_counter_requests */
	spinlock_t        gpu_cycle_counter_requests_lock;
	/** A bit mask identifying the shader cores that the power policy would like to be on.
	 * The current state of the cores may be different, but there should be transitions in progress that will
	 * eventually achieve this state (assuming that the policy doesn't change its mind in the mean time.
	 */
	u64                     desired_shader_state;
	/** bit mask indicating which shader cores are currently in a power-on transition */
	u64                     powering_on_shader_state;
	/** A bit mask identifying the tiler cores that the power policy would like to be on.
	 * @see kbase_pm_device_data:desired_shader_state */
	u64                     desired_tiler_state;
	/** bit mask indicating which tiler core are currently in a power-on transition */
	u64                     powering_on_tiler_state;

	/** bit mask indicating which l2-caches are currently in a power-on transition */
	u64                     powering_on_l2_state;
	/** bit mask indicating which l3-caches are currently in a power-on transition */
	u64                     powering_on_l3_state;

	/** Lock protecting the power state of the device.
	 *
	 * This lock must be held when accessing the shader_available_bitmap, tiler_available_bitmap, shader_inuse_bitmap
	 * and tiler_inuse_bitmap fields of kbase_device. It is also held when the hardware power registers are being
	 * written to, to ensure that two threads do not conflict over the power transitions that the hardware should
	 * make.
	 */
	spinlock_t        power_change_lock;

	/** This flag is set iff the GPU is powered as requested by the desired_xxx_state variables */
	atomic_t              gpu_in_desired_state;

	/** Set to true when the GPU is powered and register accesses are possible, false otherwise */
	mali_bool               gpu_powered;
	/** Spinlock that must be held when writing gpu_powered */
	spinlock_t        gpu_powered_lock;

	/** Structure to hold metrics for the GPU */
	kbasep_pm_metrics_data  metrics;

	/** Callback when the GPU needs to be turned on. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 *
	 * @return 1 if GPU state was lost, 0 otherwise
	 */
	int (*callback_power_on)(struct kbase_device *kbdev);

	/** Callback when the GPU may be turned off. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_off)(struct kbase_device *kbdev);

	/** Callback for initializing the runtime power management.
	 *
	 * @param kbdev         The kbase device
	 *
	 * @return MALI_ERROR_NONE on success, else error code
	 */
	mali_error (*callback_power_runtime_init)(struct kbase_device *kbdev);

	/** Callback for terminating the runtime power management.
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_runtime_term)(struct kbase_device *kbdev);

	/** Callback when the GPU needs to be turned on. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 *
	 * @return 1 if GPU state was lost, 0 otherwise
	 */
	int (*callback_power_runtime_on)(struct kbase_device *kbdev);

	/** Callback when the GPU may be turned off. See @ref kbase_pm_callback_conf
	 *
	 * @param kbdev         The kbase device
	 */
	void (*callback_power_runtime_off)(struct kbase_device *kbdev);

} kbase_pm_device_data;

/** Get the current policy.
 * Returns the policy that is currently active.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return The current policy
 */
const kbase_pm_policy *kbase_pm_get_policy(struct kbase_device *kbdev);

/** Change the policy to the one specified.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param policy    The policy to change to (valid pointer returned from @ref kbase_pm_list_policies)
 */
void kbase_pm_set_policy(struct kbase_device *kbdev, const kbase_pm_policy *policy);

/** Retrieve a static list of the available policies.
 * @param[out]  policies    An array pointer to take the list of policies. This may be NULL.
 *                          The contents of this array must not be modified.
 *
 * @return The number of policies
 */
int kbase_pm_list_policies(const kbase_pm_policy * const **policies);

/** The current policy is ready to change to the new policy
 *
 * The current policy must ensure that all cores have finished transitioning before calling this function.
 * The new policy is sent an @ref KBASE_PM_EVENT_POLICY_INIT event.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_change_policy(struct kbase_device *kbdev);

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

/** Send an event to the active power policy.
 *
 * The event is queued for sending to the active power policy. The event is merged with the current queue by the @ref
 * kbasep_pm_merge_event function which may decide to drop events.
 *
 * Note that this function may be called in an atomic context on Linux which implies that it must not sleep.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param event     The event that should be queued
 */
void kbase_pm_send_event(struct kbase_device *kbdev, kbase_pm_event event);

/** Turn one or more cores on.
 *
 * This function is called by the active power policy to turn one or more cores on.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param type      The type of core (see the @ref kbase_pm_core_type enumeration)
 * @param cores     A bitmask of cores to turn on
 */
void kbase_pm_invoke_power_up(struct kbase_device *kbdev, kbase_pm_core_type type, u64 cores);

/** Turn one or more cores off.
 *
 * This function is called by the active power policy to turn one or more core off.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param type      The type of core (see the @ref kbase_pm_core_type enumeration)
 * @param cores     A bitmask of cores to turn off
 */
void kbase_pm_invoke_power_down(struct kbase_device *kbdev, kbase_pm_core_type type, u64 cores);

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

/** Return whether the power manager is active
 *
 * This function will return true when there are cores (of any time) that are currently transitioning between power
 * states.
 *
 * It can be used on receipt of the @ref KBASE_PM_EVENT_GPU_STATE_CHANGED message to determine whether the requested power
 * transitions have completely finished or not.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return true when there are cores transitioning between power states, false otherwise
 */
mali_bool kbase_pm_get_pwr_active(struct kbase_device *kbdev);

/** Turn the clock for the device on, and enable device interrupts.
 *
 * This function can be used by a power policy to turn the clock for the GPU on. It should be modified during
 * integration to perform the necessary actions to ensure that the GPU is fully powered and clocked.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_clock_on(struct kbase_device *kbdev);

/** Disable device interrupts, and turn the clock for the device off.
 *
 * This function can be used by a power policy to turn the clock for the GPU off. It should be modified during
 * integration to perform the necessary actions to turn the clock off (if this is possible in the integration).
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_clock_off(struct kbase_device *kbdev);

/** Enable interrupts on the device.
 *
 * Interrupts are also enabled after a call to kbase_pm_clock_on().
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_enable_interrupts(struct kbase_device *kbdev);

/** Disable interrupts on the device.
 *
 * This prevents interrupt delivery to the CPU so no further @ref KBASE_PM_EVENT_GPU_STATE_CHANGED messages will be
 * received until @ref kbase_pm_enable_interrupts or kbase_pm_clock_on() is called.
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
 *
 * @return MALI_ERROR_NONE if the device is supported and successfully reset.
 */
mali_error kbase_pm_init_hw(struct kbase_device *kbdev);

/** Inform the power management system that the power state of the device is transitioning.
 *
 * This function must be called by the active power policy before transitioning the core between an 'off state' and an
 * 'on state'. It resets the wait queues that are waited on by @ref kbase_pm_wait_for_power_up and @ref
 * kbase_pm_wait_for_power_down.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_power_transitioning(struct kbase_device *kbdev);

/** The GPU has been powered up successfully.
 *
 * This function must be called by the active power policy when the GPU has been powered up successfully. It signals
 * to the rest of the system that jobs can start being submitted to the device.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_power_up_done(struct kbase_device *kbdev);

/** The GPU has been reset successfully.
 *
 * This function must be called by the GPU interrupt handler when the RESET_COMPLETED bit is set. It signals to the
 * power management initialization code that the GPU has been successfully reset.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_reset_done(struct kbase_device *kbdev);

/** The GPU has been powered down successfully.
 *
 * This function must be called by the active power policy when the GPU has been powered down successfully. It signals
 * to the rest of the system that a system suspend can now take place.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_power_down_done(struct kbase_device *kbdev);

/** Wait for the power policy to signal power up.
 *
 * This function waits for the power policy to signal power up by calling @ref kbase_pm_power_up_done. After the power
 * policy has signalled this the function will return immediately until the power policy calls @ref
 * kbase_pm_power_transitioning.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_wait_for_power_up(struct kbase_device *kbdev);

/** Wait for the power policy to signal power down.
 *
 * This function waits for the power policy to signal power down by calling @ref kbase_pm_power_down_done. After the
 * power policy has signalled this the function will return immediately until the power policy calls @ref
 * kbase_pm_power_transitioning.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_wait_for_power_down(struct kbase_device *kbdev);

/** Increment the count of active contexts.
 *
 * This function should be called when a context is about to submit a job. It informs the active power policy that the
 * GPU is going to be in use shortly and the policy is expected to start turning on the GPU.
 *
 * This function will block until the GPU is available.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_context_active(struct kbase_device *kbdev);

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
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_check_transitions(struct kbase_device *kbdev);

/** Read the bitmasks of present cores.
 *
 * This information is cached to avoid having to perform register reads whenever the information is required.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbasep_pm_read_present_cores(struct kbase_device *kbdev);

/** Mark one or more cores as being required for jobs to be submitted.
 *
 * This function is called by the job scheduler to mark one or both cores
 * as being required to submit jobs that are ready to run.
 *
 * The cores requested are reference counted and a subsequent call to @ref kbase_pm_register_inuse_cores or
 * @ref kbase_pm_unrequest_cores should be made to dereference the cores as being 'needed'.
 *
 * The current running policy is sent an @ref KBASE_PM_EVENT_CHANGE_GPU_STATE if power up of requested core is
 * required.

 * The policy is expected to make these cores available at some point in the future,
 * but may take an arbitrary length of time to reach this state.
 *
 * @param kbdev         The kbase device structure for the device
 * @param shader_cores  A bitmask of shader cores which are necessary for the job
 * @param tiler_cores   A bitmask of tiler cores which are necessary for the job
 *
 * @return MALI_ERROR_NONE if the cores were successfully requested.
 */
mali_error kbase_pm_request_cores(struct kbase_device *kbdev, u64 shader_cores, u64 tiler_cores);

/** Unmark one or more cores as being required for jobs to be submitted.
 *
 * This function undoes the effect of @ref kbase_pm_request_cores. It should be used when a job is not
 * going to be submitted to the hardware (e.g. the job is cancelled before it is enqueued).
 *
 * The current running policy is sent an @ref KBASE_PM_EVENT_CHANGE_GPU_STATE if power down of requested core
 * is required.
 *
 * The policy may use this as an indication that it can power down cores.
 *
 * @param kbdev         The kbase device structure for the device
 * @param shader_cores  A bitmask of shader cores (as given to @ref kbase_pm_request_cores)
 * @param tiler_cores   A bitmask of tiler cores (as given to @ref kbase_pm_request_cores)
 */
void kbase_pm_unrequest_cores(struct kbase_device *kbdev, u64 shader_cores, u64 tiler_cores);

/** Register a set of cores as in use by a job.
 *
 * This function should be called after @ref kbase_pm_request_cores when the job is about to be submitted to
 * the hardware. It will check that the necessary cores are available and if so update the 'needed' and 'inuse'
 * bitmasks to reflect that the job is now committed to being run.
 *
 * If the necessary cores are not currently available then the function will return MALI_FALSE and have no effect.
 *
 * @param kbdev         The kbase device structure for the device
 * @param shader_cores  A bitmask of shader cores (as given to @ref kbase_pm_request_cores)
 * @param tiler_cores   A bitmask of tiler cores (as given to @ref kbase_pm_request_cores)
 *
 * @return MALI_TRUE if the job can be submitted to the hardware or MALI_FALSE if the job is not ready to run.
 */
mali_bool kbase_pm_register_inuse_cores(struct kbase_device *kbdev, u64 shader_cores, u64 tiler_cores);

/** Release cores after a job has run.
 *
 * This function should be called when a job has finished running on the hardware. A call to @ref
 * kbase_pm_register_inuse_cores must have previously occurred. The reference counts of the specified cores will be
 * decremented which may cause the bitmask of 'inuse' cores to be reduced. The power policy may then turn off any
 * cores which are no longer 'inuse'.
 *
 * @param kbdev         The kbase device structure for the device
 * @param shader_cores  A bitmask of shader cores (as given to @ref kbase_pm_register_inuse_cores)
 * @param tiler_cores   A bitmask of tiler cores (as given to @ref kbase_pm_register_inuse_cores)
 */
void kbase_pm_release_cores(struct kbase_device *kbdev, u64 shader_cores, u64 tiler_cores);

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

/** Request the use of l2 caches for all core groups, power up, wait and prevent the power manager from
 *  powering down the l2 caches.
 *
 *  This tells the power management that the caches should be powered up, and they
 *  should remain powered, irrespective of the usage of tiler and shader cores. This does not
 *  return until the l2 caches are powered up.
 *
 *  The caller must call @ref kbase_pm_release_l2_caches when they are finished to
 *  allow normal power management of the l2 caches to resume.
 *
 *  This should only be used when power management is active.
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */

void kbase_pm_request_l2_caches(struct kbase_device *kbdev);

/** Release the use of l2 caches for all core groups and allow the power manager to
 *  power them down when necessary.
 *
 *  This tells the power management that the caches can be powered down if necessary, with respect
 *  to the usage of tiler and shader cores.
 *
 *  The caller must have called @ref kbase_pm_request_l2_caches prior to a call to this.
 *
 *  This should only be used when power management is active.
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_release_l2_caches(struct kbase_device *kbdev);

#endif /* _KBASE_PM_H_ */
