/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
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

#ifndef _KBASE_RESET_GPU_H_
#define _KBASE_RESET_GPU_H_

/**
 * kbase_prepare_to_reset_gpu_locked - Prepare for resetting the GPU.
 * @kbdev: Device pointer
 *
 * Caller is expected to hold the kbdev->hwaccess_lock.
 *
 * Return: a boolean which should be interpreted as follows:
 * - true  - Prepared for reset, kbase_reset_gpu should be called.
 * - false - Another thread is performing a reset, kbase_reset_gpu should
 *           not be called.
 */
bool kbase_prepare_to_reset_gpu_locked(struct kbase_device *kbdev);

/**
 * kbase_prepare_to_reset_gpu - Prepare for resetting the GPU.
 * @kbdev: Device pointer
 *
 * Return: a boolean which should be interpreted as follows:
 * - true  - Prepared for reset, kbase_reset_gpu should be called.
 * - false - Another thread is performing a reset, kbase_reset_gpu should
 *           not be called.
 */
bool kbase_prepare_to_reset_gpu(struct kbase_device *kbdev);

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
 * Return: True if the GPU is in the process of being reset (or if the reset of
 * GPU failed, not applicable to Job Manager GPUs).
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

/**
 * kbase_reset_gpu_register_complete_cb - Register the callback function to be
 *                                        invoked on completion of GPU reset.
 *
 * @kbdev: Device pointer
 * @complete_callback: Pointer to the callback function
 */
void kbase_reset_gpu_register_complete_cb(struct kbase_device *kbdev,
			int (*complete_callback)(struct kbase_device *kbdev));

#endif
