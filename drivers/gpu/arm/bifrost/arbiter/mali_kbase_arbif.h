/* SPDX-License-Identifier: GPL-2.0 */
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

/**
 * DOC: Mali arbiter interface APIs to share GPU between Virtual Machines
 */

#ifndef _MALI_KBASE_ARBIF_H_
#define _MALI_KBASE_ARBIF_H_

/**
 * enum kbase_arbif_evt - Internal Arbiter event.
 *
 * @KBASE_VM_GPU_INITIALIZED_EVT: KBase has finished initializing
 *                                and can be stopped
 * @KBASE_VM_GPU_STOP_EVT: Stop message received from Arbiter
 * @KBASE_VM_GPU_GRANTED_EVT: Grant message received from Arbiter
 * @KBASE_VM_GPU_LOST_EVT: Lost message received from Arbiter
 * @KBASE_VM_GPU_IDLE_EVENT: KBase has transitioned into an inactive state.
 * @KBASE_VM_REF_EVENT: KBase has transitioned into an active state.
 * @KBASE_VM_OS_SUSPEND_EVENT: KBase is suspending
 * @KBASE_VM_OS_RESUME_EVENT: Kbase is resuming
 */
enum kbase_arbif_evt {
	KBASE_VM_GPU_INITIALIZED_EVT = 1,
	KBASE_VM_GPU_STOP_EVT,
	KBASE_VM_GPU_GRANTED_EVT,
	KBASE_VM_GPU_LOST_EVT,
	KBASE_VM_GPU_IDLE_EVENT,
	KBASE_VM_REF_EVENT,
	KBASE_VM_OS_SUSPEND_EVENT,
	KBASE_VM_OS_RESUME_EVENT,
};

/**
 * kbase_arbif_init() - Initialize the arbiter interface functionality.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Initialize the arbiter interface and also determines
 * if Arbiter functionality is required.
 *
 * Return: 0 if the Arbiter interface was successfully initialized or the
 *           Arbiter was not required.
 */
int kbase_arbif_init(struct kbase_device *kbdev);

/**
 * kbase_arbif_destroy() - Cleanups the arbiter interface functionality.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Cleans up the arbiter interface functionality and resets the reference count
 * of the arbif module used
 */
void kbase_arbif_destroy(struct kbase_device *kbdev);

/**
 * kbase_arbif_get_max_config() - Request max config info
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * call back function from arb interface to arbiter requesting max config info
 */
void kbase_arbif_get_max_config(struct kbase_device *kbdev);

/**
 * kbase_arbif_gpu_request() - Send GPU request message to the arbiter
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Sends a message to Arbiter to request GPU access.
 */
void kbase_arbif_gpu_request(struct kbase_device *kbdev);

/**
 * kbase_arbif_gpu_stopped() - Send GPU stopped message to the arbiter
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @gpu_required: true if GPU access is still required
 *                (Arbiter will automatically send another grant message)
 *
 * Sends a message to Arbiter to notify that the GPU has stopped.
 * @note Once this call has been made, KBase must not attempt to access the GPU
 *       until the #KBASE_VM_GPU_GRANTED_EVT event has been received.
 */
void kbase_arbif_gpu_stopped(struct kbase_device *kbdev, u8 gpu_required);

/**
 * kbase_arbif_gpu_active() - Send a GPU active message to the arbiter
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Sends a message to Arbiter to report that KBase has gone active.
 */
void kbase_arbif_gpu_active(struct kbase_device *kbdev);

/**
 * kbase_arbif_gpu_idle() - Send a GPU idle message to the arbiter
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Sends a message to Arbiter to report that KBase has gone idle.
 */
void kbase_arbif_gpu_idle(struct kbase_device *kbdev);

#endif /* _MALI_KBASE_ARBIF_H_ */
