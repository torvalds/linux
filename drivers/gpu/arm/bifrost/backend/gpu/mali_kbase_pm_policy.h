/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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
 * Power policy API definitions
 */

#ifndef _KBASE_PM_POLICY_H_
#define _KBASE_PM_POLICY_H_

/**
 * kbase_pm_policy_init - Initialize power policy framework
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Must be called before calling any other policy function
 *
 * Return: 0 if the power policy framework was successfully
 *         initialized, -errno otherwise.
 */
int kbase_pm_policy_init(struct kbase_device *kbdev);

/**
 * kbase_pm_policy_term - Terminate power policy framework
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_policy_term(struct kbase_device *kbdev);

/**
 * kbase_pm_update_active - Update the active power state of the GPU
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Calls into the current power policy
 */
void kbase_pm_update_active(struct kbase_device *kbdev);

/**
 * kbase_pm_update_cores - Update the desired core state of the GPU
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Calls into the current power policy
 */
void kbase_pm_update_cores(struct kbase_device *kbdev);


enum kbase_pm_cores_ready {
	KBASE_CORES_NOT_READY = 0,
	KBASE_NEW_AFFINITY = 1,
	KBASE_CORES_READY = 2
};


/**
 * kbase_pm_request_cores_sync - Synchronous variant of kbase_pm_request_cores()
 *
 * @kbdev:          The kbase device structure for the device
 * @tiler_required: true if the tiler is required, false otherwise
 * @shader_cores:   A bitmask of shader cores which are necessary for the job
 *
 * When this function returns, the @shader_cores will be in the READY state.
 *
 * This is safe variant of kbase_pm_check_transitions_sync(): it handles the
 * work of ensuring the requested cores will remain powered until a matching
 * call to kbase_pm_unrequest_cores()/kbase_pm_release_cores() (as appropriate)
 * is made.
 */
void kbase_pm_request_cores_sync(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores);

/**
 * kbase_pm_request_cores - Mark one or more cores as being required
 *                          for jobs to be submitted
 *
 * @kbdev:          The kbase device structure for the device
 * @tiler_required: true if the tiler is required, false otherwise
 * @shader_cores:   A bitmask of shader cores which are necessary for the job
 *
 * This function is called by the job scheduler to mark one or more cores as
 * being required to submit jobs that are ready to run.
 *
 * The cores requested are reference counted and a subsequent call to
 * kbase_pm_register_inuse_cores() or kbase_pm_unrequest_cores() should be
 * made to dereference the cores as being 'needed'.
 *
 * The active power policy will meet or exceed the requirements of the
 * requested cores in the system. Any core transitions needed will be begun
 * immediately, but they might not complete/the cores might not be available
 * until a Power Management IRQ.
 *
 * Return: 0 if the cores were successfully requested, or -errno otherwise.
 */
void kbase_pm_request_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores);

/**
 * kbase_pm_unrequest_cores - Unmark one or more cores as being required for
 *                            jobs to be submitted.
 *
 * @kbdev:          The kbase device structure for the device
 * @tiler_required: true if the tiler is required, false otherwise
 * @shader_cores:   A bitmask of shader cores (as given to
 *                  kbase_pm_request_cores() )
 *
 * This function undoes the effect of kbase_pm_request_cores(). It should be
 * used when a job is not going to be submitted to the hardware (e.g. the job is
 * cancelled before it is enqueued).
 *
 * The active power policy will meet or exceed the requirements of the
 * requested cores in the system. Any core transitions needed will be begun
 * immediately, but they might not complete until a Power Management IRQ.
 *
 * The policy may use this as an indication that it can power down cores.
 */
void kbase_pm_unrequest_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores);

/**
 * kbase_pm_register_inuse_cores - Register a set of cores as in use by a job
 *
 * @kbdev:          The kbase device structure for the device
 * @tiler_required: true if the tiler is required, false otherwise
 * @shader_cores:   A bitmask of shader cores (as given to
 *                  kbase_pm_request_cores() )
 *
 * This function should be called after kbase_pm_request_cores() when the job
 * is about to be submitted to the hardware. It will check that the necessary
 * cores are available and if so update the 'needed' and 'inuse' bitmasks to
 * reflect that the job is now committed to being run.
 *
 * If the necessary cores are not currently available then the function will
 * return %KBASE_CORES_NOT_READY and have no effect.
 *
 * Return: %KBASE_CORES_NOT_READY if the cores are not immediately ready,
 *
 *         %KBASE_NEW_AFFINITY if the affinity requested is not allowed,
 *
 *         %KBASE_CORES_READY if the cores requested are already available
 */
enum kbase_pm_cores_ready kbase_pm_register_inuse_cores(
						struct kbase_device *kbdev,
						bool tiler_required,
						u64 shader_cores);

/**
 * kbase_pm_release_cores - Release cores after a job has run
 *
 * @kbdev:          The kbase device structure for the device
 * @tiler_required: true if the tiler is required, false otherwise
 * @shader_cores:   A bitmask of shader cores (as given to
 *                  kbase_pm_register_inuse_cores() )
 *
 * This function should be called when a job has finished running on the
 * hardware. A call to kbase_pm_register_inuse_cores() must have previously
 * occurred. The reference counts of the specified cores will be decremented
 * which may cause the bitmask of 'inuse' cores to be reduced. The power policy
 * may then turn off any cores which are no longer 'inuse'.
 */
void kbase_pm_release_cores(struct kbase_device *kbdev,
				bool tiler_required, u64 shader_cores);

/**
 * kbase_pm_request_l2_caches - Request l2 caches
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Request the use of l2 caches for all core groups, power up, wait and prevent
 * the power manager from powering down the l2 caches.
 *
 * This tells the power management that the caches should be powered up, and
 * they should remain powered, irrespective of the usage of shader cores. This
 * does not return until the l2 caches are powered up.
 *
 * The caller must call kbase_pm_release_l2_caches() when they are finished
 * to allow normal power management of the l2 caches to resume.
 *
 * This should only be used when power management is active.
 */
void kbase_pm_request_l2_caches(struct kbase_device *kbdev);

/**
 * kbase_pm_request_l2_caches_l2_is_on - Request l2 caches but don't power on
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Increment the count of l2 users but do not attempt to power on the l2
 *
 * It is the callers responsibility to ensure that the l2 is already powered up
 * and to eventually call kbase_pm_release_l2_caches()
 */
void kbase_pm_request_l2_caches_l2_is_on(struct kbase_device *kbdev);

/**
 * kbase_pm_request_l2_caches - Release l2 caches
 *
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Release the use of l2 caches for all core groups and allow the power manager
 * to power them down when necessary.
 *
 * This tells the power management that the caches can be powered down if
 * necessary, with respect to the usage of shader cores.
 *
 * The caller must have called kbase_pm_request_l2_caches() prior to a call
 * to this.
 *
 * This should only be used when power management is active.
 */
void kbase_pm_release_l2_caches(struct kbase_device *kbdev);

#endif /* _KBASE_PM_POLICY_H_ */
