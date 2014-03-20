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
 * @file mali_kbase_pm_ca.h
 * Base kernel core availability APIs
 */

#ifndef _KBASE_PM_CA_H_
#define _KBASE_PM_CA_H_

typedef enum kbase_pm_ca_policy_id {
	KBASE_PM_CA_POLICY_ID_FIXED = 1,
	KBASE_PM_CA_POLICY_ID_RANDOM
} kbase_pm_ca_policy_id;

typedef u32 kbase_pm_ca_policy_flags;

/** Core availability policy structure.
 *
 * Each core availability policy exposes a (static) instance of this structure which contains function pointers to the
 * policy's methods.
 */
typedef struct kbase_pm_ca_policy {
	/** The name of this policy */
	char *name;

	/** Function called when the policy is selected
	 *
	 * This should initialize the kbdev->pm.ca_policy_data structure. It should not attempt
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

	/** Function called to get the current shader core availability mask
	 *
	 * When a change in core availability is occuring, the policy must set kbdev->pm.ca_in_transition
	 * to MALI_TRUE. This is to indicate that reporting changes in power state cannot be optimized out,
	 * even if kbdev->pm.desired_shader_state remains unchanged. This must be done by any functions
	 * internal to the Core Availability Policy that change the return value of
	 * kbase_pm_ca_policy::get_core_mask.
	 *
	 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
	 *
	 * @return     The current core availability mask */
	u64 (*get_core_mask) (struct kbase_device *kbdev);

	/** Function called to update the current core status
	 *
	 * If none of the cores in core group 0 are ready or transitioning, then the policy must
	 * ensure that the next call to get_core_mask does not return 0 for all cores in core group
	 * 0. It is an error to disable core group 0 through the core availability policy.
	 *
	 * When a change in core availability has finished, the policy must set kbdev->pm.ca_in_transition
	 * to MALI_FALSE. This is to indicate that changes in power state can once again be optimized out
	 * when kbdev->pm.desired_shader_state is unchanged.
	 *
	 * @param kbdev                   The kbase device structure for the device (must be a valid pointer)
	 * @param cores_ready             The mask of cores currently powered and ready to run jobs
	 * @param cores_transitioning     The mask of cores currently transitioning power state */
	void (*update_core_status) (struct kbase_device *kbdev, u64 cores_ready, u64 cores_transitioning);

	/** Field indicating flags for this policy */
	kbase_pm_ca_policy_flags flags;

	/** Field indicating an ID for this policy. This is not necessarily the
	 * same as its index in the list returned by kbase_pm_list_policies().
	 * It is used purely for debugging. */
	kbase_pm_ca_policy_id id;
} kbase_pm_ca_policy;

/** Initialize core availability framework
 *
 * Must be called before calling any other core availability function
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return MALI_ERROR_NONE if the core availability framework was successfully initialized.
 */
mali_error kbase_pm_ca_init(struct kbase_device *kbdev);

/** Terminate core availability framework
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_ca_term(struct kbase_device *kbdev);

/** Return mask of currently available shaders cores
 * Calls into the core availability policy
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return          The bit mask of available cores
 */
u64 kbase_pm_ca_get_core_mask(struct kbase_device *kbdev);

/** Update core availability policy with current core power status
 * Calls into the core availability policy
 *
 * @param kbdev                The kbase device structure for the device (must be a valid pointer)
 * @param cores_ready          The bit mask of cores ready for job submission
 * @param cores_transitioning  The bit mask of cores that are transitioning power state
 */
void kbase_pm_ca_update_core_status(struct kbase_device *kbdev, u64 cores_ready, u64 cores_transitioning);

/** Enable override for instrumentation
 *
 * This overrides the output of the core availability policy, ensuring that all cores are available
 *
 * @param kbdev                The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_ca_instr_enable(struct kbase_device *kbdev);

/** Disable override for instrumentation
 *
 * This disables any previously enabled override, and resumes normal policy functionality
 *
 * @param kbdev                The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_ca_instr_disable(struct kbase_device *kbdev);

/** Get the current policy.
 * Returns the policy that is currently active.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return The current policy
 */
const kbase_pm_ca_policy *kbase_pm_ca_get_policy(struct kbase_device *kbdev);

/** Change the policy to the one specified.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 * @param policy    The policy to change to (valid pointer returned from @ref kbase_pm_ca_list_policies)
 */
void kbase_pm_ca_set_policy(struct kbase_device *kbdev, const kbase_pm_ca_policy *policy);

/** Retrieve a static list of the available policies.
 * @param[out]  policies    An array pointer to take the list of policies. This may be NULL.
 *                          The contents of this array must not be modified.
 *
 * @return The number of policies
 */
int kbase_pm_ca_list_policies(const kbase_pm_ca_policy * const **policies);

#endif				/* _KBASE_PM_CA_H_ */
