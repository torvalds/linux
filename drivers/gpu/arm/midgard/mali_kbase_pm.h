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





/**
 * @file mali_kbase_pm.h
 * Power management API definitions
 */

#ifndef _KBASE_PM_H_
#define _KBASE_PM_H_

#include "mali_kbase_hwaccess_pm.h"

#define PM_ENABLE_IRQS       0x01
#define PM_HW_ISSUES_DETECT  0x02


/** Initialize the power management framework.
 *
 * Must be called before any other power management function
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @return 0 if the power management framework was successfully initialized.
 */
int kbase_pm_init(struct kbase_device *kbdev);

/** Power up GPU after all modules have been initialized and interrupt handlers installed.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 *
 * @param flags     Flags to pass on to kbase_pm_init_hw
 *
 * @return 0 if powerup was successful.
 */
int kbase_pm_powerup(struct kbase_device *kbdev, unsigned int flags);

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
enum kbase_pm_suspend_handler {
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
};

/** Suspend 'safe' variant of kbase_pm_context_active()
 *
 * If a suspend is in progress, this allows for various different ways of
 * handling the suspend. Refer to @ref enum kbase_pm_suspend_handler for details.
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
int kbase_pm_context_active_handle_suspend(struct kbase_device *kbdev, enum kbase_pm_suspend_handler suspend_handler);

/** Decrement the reference count of active contexts.
 *
 * This function should be called when a context becomes idle. After this call the GPU may be turned off by the power
 * policy so the calling code should ensure that it does not access the GPU's registers.
 *
 * @param kbdev     The kbase device structure for the device (must be a valid pointer)
 */
void kbase_pm_context_idle(struct kbase_device *kbdev);

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

/**
 * kbase_pm_vsync_callback - vsync callback
 *
 * @buffer_updated: 1 if a new frame was displayed, 0 otherwise
 * @data: Pointer to the kbase device as returned by kbase_find_device()
 *
 * Callback function used to notify the power management code that a vsync has
 * occurred on the display.
 */
void kbase_pm_vsync_callback(int buffer_updated, void *data);

#endif				/* _KBASE_PM_H_ */
