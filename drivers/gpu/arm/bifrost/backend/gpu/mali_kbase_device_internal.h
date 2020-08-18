/*
 *
 * (C) COPYRIGHT 2014,2019-2020 ARM Limited. All rights reserved.
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
 * Backend-specific HW access device APIs
 */

#ifndef _KBASE_DEVICE_INTERNAL_H_
#define _KBASE_DEVICE_INTERNAL_H_

/**
 * kbase_reg_write - write to GPU register
 * @kbdev:  Kbase device pointer
 * @offset: Offset of register
 * @value:  Value to write
 *
 * Caller must ensure the GPU is powered (@kbdev->pm.gpu_powered != false).
 */
void kbase_reg_write(struct kbase_device *kbdev, u32 offset, u32 value);

/**
 * kbase_reg_read - read from GPU register
 * @kbdev:  Kbase device pointer
 * @offset: Offset of register
 *
 * Caller must ensure the GPU is powered (@kbdev->pm.gpu_powered != false).
 *
 * Return: Value in desired register
 */
u32 kbase_reg_read(struct kbase_device *kbdev, u32 offset);

/**
 * kbase_is_gpu_lost() - Has the GPU been lost.
 * @kbdev:    Kbase device pointer
 *
 * This function will return true if the GPU has been lost.
 * When this happens register reads will be zero. A zero GPU_ID is
 * invalid so this is used to detect GPU_LOST
 *
 * Return: True if GPU LOST
 */
bool kbase_is_gpu_lost(struct kbase_device *kbdev);

/**
 * kbase_gpu_start_cache_clean - Start a cache clean
 * @kbdev: Kbase device
 *
 * Issue a cache clean and invalidate command to hardware. This function will
 * take hwaccess_lock.
 */
void kbase_gpu_start_cache_clean(struct kbase_device *kbdev);

/**
 * kbase_gpu_start_cache_clean_nolock - Start a cache clean
 * @kbdev: Kbase device
 *
 * Issue a cache clean and invalidate command to hardware. hwaccess_lock
 * must be held by the caller.
 */
void kbase_gpu_start_cache_clean_nolock(struct kbase_device *kbdev);

/**
 * kbase_gpu_wait_cache_clean - Wait for cache cleaning to finish
 * @kbdev: Kbase device
 *
 * This function will take hwaccess_lock, and may sleep.
 */
void kbase_gpu_wait_cache_clean(struct kbase_device *kbdev);

/**
 * kbase_gpu_wait_cache_clean_timeout - Wait for certain time for cache
 *                                      cleaning to finish
 * @kbdev: Kbase device
 * @wait_timeout_ms: Time, in milli seconds, to wait for cache clean to complete.
 *
 * This function will take hwaccess_lock, and may sleep. This is supposed to be
 * called from paths (like GPU reset) where an indefinite wait for the completion
 * of cache clean operation can cause deadlock, as the operation may never
 * complete.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_gpu_wait_cache_clean_timeout(struct kbase_device *kbdev,
		unsigned int wait_timeout_ms);

/**
 * kbase_gpu_cache_clean_wait_complete - Called after the cache cleaning is
 *                                       finished. Would also be called after
 *                                       the GPU reset.
 * @kbdev: Kbase device
 *
 * Caller must hold the hwaccess_lock.
 */
void kbase_gpu_cache_clean_wait_complete(struct kbase_device *kbdev);

/**
 * kbase_gpu_interrupt - GPU interrupt handler
 * @kbdev: Kbase device pointer
 * @val:   The value of the GPU IRQ status register which triggered the call
 *
 * This function is called from the interrupt handler when a GPU irq is to be
 * handled.
 */
void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val);

#endif /* _KBASE_DEVICE_INTERNAL_H_ */
