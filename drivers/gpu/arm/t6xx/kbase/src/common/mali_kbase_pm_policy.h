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
 * @file mali_kbase_pm_policy.h
 * Power policy API definitions
 */

#ifndef _KBASE_PM_POLICY_H_
#define _KBASE_PM_POLICY_H_

/** List of policy IDs */
typedef enum kbase_pm_policy_id {
	KBASE_PM_POLICY_ID_DEMAND = 1,
	KBASE_PM_POLICY_ID_ALWAYS_ON,
	KBASE_PM_POLICY_ID_COARSE_DEMAND,
#if MALI_CUSTOMER_RELEASE == 0
	KBASE_PM_POLICY_ID_DEMAND_ALWAYS_POWERED,
	KBASE_PM_POLICY_ID_FAST_START
#endif
} kbase_pm_policy_id;

typedef u32 kbase_pm_policy_flags;

/** Power policy structure.
 *
 * Each power policy exposes a (static) instance of this structure which contains function pointers to the
 * policy's methods.
 */
typedef struct kbase_pm_policy {
	/** The name of this policy */
	char *name;

	/** Function called when the policy is selected
	 *
	 * This should initialize the kbdev->pm.pm_policy_data structure. It should not attempt
	 * to make any changes to hardware state.
	 *
	 * It is undefined what state the cores are in when the function is called.
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 */
	void (*init) (struct kbase_device *kbdev);

	/** Function called when the policy is unselected.
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 */
	void (*term) (struct kbase_device *kbdev);

	/** Function called to get the current shader core mask
	 *
	 * The returned mask should meet or exceed (kbdev->shader_needed_bitmap | kbdev->shader_inuse_bitmap).
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 *
	 * @return     The mask of shader cores to be powered */
	u64 (*get_core_mask) (struct kbase_device *kbdev);

	/** Function called to get the current overall GPU power state
	 *
	 * This function should consider the state of kbdev->pm.active_count. If this count is greater than 0 then
	 * there is at least one active context on the device and the GPU should be powered. If it is equal to 0
	 * then there are no active contexts and the GPU could be powered off if desired.
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 *
	 * @return     MALI_TRUE if the GPU should be powered, MALI_FALSE otherwise */
	mali_bool (*get_core_active) (struct kbase_device *kbdev);

	/** Field indicating flags for this policy */
	kbase_pm_policy_flags flags;

	/** Field indicating an ID for this policy. This is not necessarily the
	 * same as its index in the list returned by kbase_pm_list_policies().
	 * It is used purely for debugging. */
	kbase_pm_policy_id id;
} kbase_pm_policy;

/** Initialize power policy framework
 *
 * Must be called before calling any other policy function
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return MALI_ERROR_NONE if the power policy framework was successfully initialized.
 */
mali_error kbase_pm_policy_init(struct kbase_device *kbdev);

/** Terminate power policy framework
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_policy_term(struct kbase_device *kbdev);

/** Update the active power state of the GPU
 * Calls into the current power policy
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_update_active(struct kbase_device *kbdev);

/** Update the desired core state of the GPU
 * Calls into the current power policy
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_update_cores(struct kbase_device *kbdev);

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


typedef enum kbase_pm_cores_ready {
	KBASE_CORES_NOT_READY = 0,
	KBASE_NEW_AFFINITY = 1,
	KBASE_CORES_READY = 2
} kbase_pm_cores_ready;


/** Synchronous variant of kbase_pm_request_cores()
 *
 * When this function returns, the @a shader_cores will be in the READY state.
 *
 * This is safe variant of kbase_pm_check_transitions_sync(): it handles the
 * work of ensuring the requested cores will remain powered until a matching
 * call to kbase_pm_unrequest_cores()/kbase_pm_release_cores() (as appropriate)
 * is made.
 *
 * @param kbdev           The kbase device structure for the device
 * @param tiler_required  MALI_TRUE if the tiler is required, MALI_FALSE otherwise
 * @param shader_cores    A bitmask of shader cores which are necessary for the job
 */

void kbase_pm_request_cores_sync(struct kbase_device *kbdev, mali_bool tiler_required, u64 shader_cores);

/** Mark one or more cores as being required for jobs to be submitted.
 *
 * This function is called by the job scheduler to mark one or more cores
 * as being required to submit jobs that are ready to run.
 *
 * The cores requested are reference counted and a subsequent call to @ref kbase_pm_register_inuse_cores or
 * @ref kbase_pm_unrequest_cores should be made to dereference the cores as being 'needed'.
 *
 * The active power policy will meet or exceed the requirements of the
 * requested cores in the system. Any core transitions needed will be begun
 * immediately, but they might not complete/the cores might not be available
 * until a Power Management IRQ.
 *
 * @param kbdev           The kbase device structure for the device
 * @param tiler_required  MALI_TRUE if the tiler is required, MALI_FALSE otherwise
 * @param shader_cores    A bitmask of shader cores which are necessary for the job
 *
 * @return MALI_ERROR_NONE if the cores were successfully requested.
 */
void kbase_pm_request_cores(struct kbase_device *kbdev, mali_bool tiler_required, u64 shader_cores);

/** Unmark one or more cores as being required for jobs to be submitted.
 *
 * This function undoes the effect of @ref kbase_pm_request_cores. It should be used when a job is not
 * going to be submitted to the hardware (e.g. the job is cancelled before it is enqueued).
 *
 * The active power policy will meet or exceed the requirements of the
 * requested cores in the system. Any core transitions needed will be begun
 * immediately, but they might not complete until a Power Management IRQ.
 *
 * The policy may use this as an indication that it can power down cores.
 *
 * @param kbdev           The kbase device structure for the device
 * @param tiler_required  MALI_TRUE if the tiler is required, MALI_FALSE otherwise
 * @param shader_cores    A bitmask of shader cores (as given to @ref kbase_pm_request_cores)
 */
void kbase_pm_unrequest_cores(struct kbase_device *kbdev, mali_bool tiler_required, u64 shader_cores);

/** Register a set of cores as in use by a job.
 *
 * This function should be called after @ref kbase_pm_request_cores when the job is about to be submitted to
 * the hardware. It will check that the necessary cores are available and if so update the 'needed' and 'inuse'
 * bitmasks to reflect that the job is now committed to being run.
 *
 * If the necessary cores are not currently available then the function will return MALI_FALSE and have no effect.
 *
 * @param kbdev           The kbase device structure for the device
 * @param tiler_required  MALI_TRUE if the tiler is required, MALI_FALSE otherwise
 * @param shader_cores    A bitmask of shader cores (as given to @ref kbase_pm_request_cores)
 *
 * @return MALI_TRUE if the job can be submitted to the hardware or MALI_FALSE if the job is not ready to run.
 */
mali_bool kbase_pm_register_inuse_cores(struct kbase_device *kbdev, mali_bool tiler_required, u64 shader_cores);

/** Release cores after a job has run.
 *
 * This function should be called when a job has finished running on the hardware. A call to @ref
 * kbase_pm_register_inuse_cores must have previously occurred. The reference counts of the specified cores will be
 * decremented which may cause the bitmask of 'inuse' cores to be reduced. The power policy may then turn off any
 * cores which are no longer 'inuse'.
 *
 * @param kbdev         The kbase device structure for the device
 * @param tiler_required  MALI_TRUE if the tiler is required, MALI_FALSE otherwise
 * @param shader_cores  A bitmask of shader cores (as given to @ref kbase_pm_register_inuse_cores)
 */
void kbase_pm_release_cores(struct kbase_device *kbdev, mali_bool tiler_required, u64 shader_cores);

/** Request the use of l2 caches for all core groups, power up, wait and prevent the power manager from
 *  powering down the l2 caches.
 *
 *  This tells the power management that the caches should be powered up, and they
 *  should remain powered, irrespective of the usage of shader cores. This does not
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
 *  to the usage of shader cores.
 *
 *  The caller must have called @ref kbase_pm_request_l2_caches prior to a call to this.
 *
 *  This should only be used when power management is active.
 *
 * @param kbdev    The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_release_l2_caches(struct kbase_device *kbdev);

#endif				/* _KBASE_PM_POLICY_H_ */
