/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_RESET_GPU_H_
#define _KBASE_RESET_GPU_H_

/**
 * kbase_reset_gpu_prevent_and_wait - Prevent GPU resets from starting whilst
 *                                    the current thread is accessing the GPU,
 *                                    and wait for any in-flight reset to
 *                                    finish.
 * @kbdev: Device pointer
 *
 * This should be used when a potential access to the HW is going to be made
 * from a non-atomic context.
 *
 * It will wait for any in-flight reset to finish before returning. Hence,
 * correct lock ordering must be observed with respect to the calling thread
 * and the reset worker thread.
 *
 * This does not synchronize general access to the HW, and so multiple threads
 * can prevent GPU reset concurrently, whilst not being serialized. This is
 * advantageous as the threads can make this call at points where they do not
 * know for sure yet whether they will indeed access the GPU (for example, to
 * respect lock ordering), without unnecessarily blocking others.
 *
 * Threads must still use other synchronization to ensure they access the HW
 * consistently, at a point where they are certain it needs to be accessed.
 *
 * On success, ensure that when access to the GPU by the caller thread has
 * finished, that it calls kbase_reset_gpu_allow() again to allow resets to
 * happen.
 *
 * This may return a failure in cases such as a previous failure to reset the
 * GPU within a reasonable time. If that happens, the GPU might be
 * non-operational and the caller should not attempt any further access.
 *
 * Note:
 * For atomic context, instead check kbase_reset_gpu_is_active().
 *
 * Return: 0 on success, or negative error code on failure.
 */
int kbase_reset_gpu_prevent_and_wait(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_try_prevent - Attempt to prevent GPU resets from starting
 *                               whilst the current thread is accessing the
 *                               GPU, unless a reset is already in progress.
 * @kbdev: Device pointer
 *
 * Similar to kbase_reset_gpu_prevent_and_wait(), but it does not wait for an
 * existing reset to complete. This can be used on codepaths that the Reset
 * worker waits on, where use of kbase_reset_gpu_prevent_and_wait() would
 * otherwise deadlock.
 *
 * Instead, a reset that is currently happening will cause this function to
 * return an error code indicating that, and further resets will not have been
 * prevented.
 *
 * In such cases, the caller must check for -EAGAIN, and take similar actions
 * as for handling reset in atomic context. That is, they must cancel any
 * actions that depended on reset being prevented, possibly deferring them
 * until after the reset.
 *
 * Otherwise a successful return means that the caller can continue its actions
 * safely in the knowledge that reset is prevented, and the reset worker will
 * correctly wait instead of deadlocking against this thread.
 *
 * On success, ensure that when access to the GPU by the caller thread has
 * finished, that it calls kbase_reset_gpu_allow() again to allow resets to
 * happen.
 *
 * Refer to kbase_reset_gpu_prevent_and_wait() for more information.
 *
 * Return: 0 on success. -EAGAIN if a reset is currently happening. Other
 * negative error codes on failure.
 */
int kbase_reset_gpu_try_prevent(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_allow - Allow GPU resets to happen again after having been
 *                         previously prevented.
 * @kbdev: Device pointer
 *
 * This should be used when a potential access to the HW has finished from a
 * non-atomic context.
 *
 * It must be used from the same thread that originally made a previously call
 * to kbase_reset_gpu_prevent_and_wait(). It must not be deferred to another
 * thread.
 */
void kbase_reset_gpu_allow(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_assert_prevented - Make debugging checks that GPU reset is
 *                                    currently prevented by the current
 *                                    thread.
 * @kbdev: Device pointer
 *
 * Make debugging checks that the current thread has made a call to
 * kbase_reset_gpu_prevent_and_wait(), but has yet to make a subsequent call to
 * kbase_reset_gpu_allow().
 *
 * CONFIG_LOCKDEP is required to prove that reset is indeed
 * prevented. Otherwise only limited debugging checks can be made.
 */
void kbase_reset_gpu_assert_prevented(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_assert_failed_or_prevented - Make debugging checks that
 *                                              either GPU reset previously
 *                                              failed, or is currently
 *                                              prevented.
 *
 * @kbdev: Device pointer
 *
 * As with kbase_reset_gpu_assert_prevented(), but also allow for paths where
 * reset was not prevented due to a failure, yet we still need to execute the
 * cleanup code following.
 *
 * Cleanup code following this call must handle any inconsistent state modified
 * by the failed GPU reset, and must timeout any blocking operations instead of
 * waiting forever.
 */
void kbase_reset_gpu_assert_failed_or_prevented(struct kbase_device *kbdev);

/**
 * Flags for kbase_prepare_to_reset_gpu
 */
#define RESET_FLAGS_NONE ((unsigned int)0)
/* This reset should be treated as an unrecoverable error by HW counter logic */
#define RESET_FLAGS_HWC_UNRECOVERABLE_ERROR ((unsigned int)(1 << 0))

/**
 * kbase_prepare_to_reset_gpu_locked - Prepare for resetting the GPU.
 * @kbdev: Device pointer
 * @flags: Bitfield indicating impact of reset (see flag defines)
 *
 * Caller is expected to hold the kbdev->hwaccess_lock.
 *
 * Return: a boolean which should be interpreted as follows:
 * - true  - Prepared for reset, kbase_reset_gpu should be called.
 * - false - Another thread is performing a reset, kbase_reset_gpu should
 *           not be called.
 */
bool kbase_prepare_to_reset_gpu_locked(struct kbase_device *kbdev,
				       unsigned int flags);

/**
 * kbase_prepare_to_reset_gpu - Prepare for resetting the GPU.
 * @kbdev: Device pointer
 * @flags: Bitfield indicating impact of reset (see flag defines)

 * Return: a boolean which should be interpreted as follows:
 * - true  - Prepared for reset, kbase_reset_gpu should be called.
 * - false - Another thread is performing a reset, kbase_reset_gpu should
 *           not be called.
 */
bool kbase_prepare_to_reset_gpu(struct kbase_device *kbdev, unsigned int flags);

/**
 * kbase_reset_gpu - Reset the GPU
 * @kbdev: Device pointer
 *
 * This function should be called after kbase_prepare_to_reset_gpu if it returns
 * true. It should never be called without a corresponding call to
 * kbase_prepare_to_reset_gpu (only on Job Manager GPUs).
 *
 * After this function is called the caller should call kbase_reset_gpu_wait()
 * to know when the reset has completed.
 */
void kbase_reset_gpu(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_locked - Reset the GPU
 * @kbdev: Device pointer
 *
 * This function should be called after kbase_prepare_to_reset_gpu_locked if it
 * returns true. It should never be called without a corresponding call to
 * kbase_prepare_to_reset_gpu (only on Job Manager GPUs).
 * Caller is expected to hold the kbdev->hwaccess_lock.
 *
 * After this function is called, the caller should call kbase_reset_gpu_wait()
 * to know when the reset has completed.
 */
void kbase_reset_gpu_locked(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_silent - Reset the GPU silently
 * @kbdev: Device pointer
 *
 * Reset the GPU without trying to cancel jobs (applicable to Job Manager GPUs)
 * and don't emit messages into the kernel log while doing the reset.
 *
 * This function should be used in cases where we are doing a controlled reset
 * of the GPU as part of normal processing (e.g. exiting protected mode) where
 * the driver will have ensured the scheduler has been idled and all other
 * users of the GPU (e.g. instrumentation) have been suspended.
 *
 * Return: 0 if the reset was started successfully
 *         -EAGAIN if another reset is currently in progress
 */
int kbase_reset_gpu_silent(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_is_active - Reports if the GPU is being reset
 * @kbdev: Device pointer
 *
 * Any changes made to the HW when this returns true may be lost, overwritten
 * or corrupted.
 *
 * Note that unless appropriate locks are held when using this function, the
 * state could change immediately afterwards.
 *
 * Return: True if the GPU is in the process of being reset.
 */
bool kbase_reset_gpu_is_active(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_wait - Wait for a GPU reset to complete
 * @kbdev: Device pointer
 *
 * This function may wait indefinitely.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_reset_gpu_wait(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_init - Initialize the GPU reset handling mechanism.
 *
 * @kbdev: Device pointer
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_reset_gpu_init(struct kbase_device *kbdev);

/**
 * kbase_reset_gpu_term - Terminate the GPU reset handling mechanism.
 *
 * @kbdev: Device pointer
 */
void kbase_reset_gpu_term(struct kbase_device *kbdev);

#endif
