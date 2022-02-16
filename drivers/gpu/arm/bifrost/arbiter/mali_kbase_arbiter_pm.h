/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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
 * DOC: Mali arbiter power manager state machine and APIs
 */

#ifndef _MALI_KBASE_ARBITER_PM_H_
#define _MALI_KBASE_ARBITER_PM_H_

#include "mali_kbase_arbif.h"

/**
 * enum kbase_vm_state - Current PM Arbitration state.
 *
 * @KBASE_VM_STATE_INITIALIZING: Special state before arbiter is initialized.
 * @KBASE_VM_STATE_INITIALIZING_WITH_GPU: Initialization after GPU
 *                                        has been granted.
 * @KBASE_VM_STATE_SUSPENDED: KBase is suspended by OS and GPU is not assigned.
 * @KBASE_VM_STATE_STOPPED: GPU is not assigned to KBase and is not required.
 * @KBASE_VM_STATE_STOPPED_GPU_REQUESTED: GPU is not assigned to KBase
 *                                        but a request has been made.
 * @KBASE_VM_STATE_STARTING: GPU is assigned and KBase is getting ready to run.
 * @KBASE_VM_STATE_IDLE: GPU is assigned but KBase has no work to do
 * @KBASE_VM_STATE_ACTIVE: GPU is assigned and KBase is busy using it
 * @KBASE_VM_STATE_SUSPEND_PENDING: OS is going into suspend mode.
 * @KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT: OS is going into suspend mode but GPU
 *                                         has already been requested.
 *                                         In this situation we must wait for
 *                                         the Arbiter to send a GRANTED message
 *                                         and respond immediately with
 *                                         a STOPPED message before entering
 *                                         the suspend mode.
 * @KBASE_VM_STATE_STOPPING_IDLE: Arbiter has sent a stopped message and there
 *                                is currently no work to do on the GPU.
 * @KBASE_VM_STATE_STOPPING_ACTIVE: Arbiter has sent a stopped message when
 *                                  KBase has work to do.
 */
enum kbase_vm_state {
	KBASE_VM_STATE_INITIALIZING,
	KBASE_VM_STATE_INITIALIZING_WITH_GPU,
	KBASE_VM_STATE_SUSPENDED,
	KBASE_VM_STATE_STOPPED,
	KBASE_VM_STATE_STOPPED_GPU_REQUESTED,
	KBASE_VM_STATE_STARTING,
	KBASE_VM_STATE_IDLE,
	KBASE_VM_STATE_ACTIVE,
	KBASE_VM_STATE_SUSPEND_PENDING,
	KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT,
	KBASE_VM_STATE_STOPPING_IDLE,
	KBASE_VM_STATE_STOPPING_ACTIVE
};

/**
 * kbase_arbiter_pm_early_init() - Initialize arbiter for VM Paravirtualized use
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Initialize the arbiter and other required resources during the runtime
 * and request the GPU for the VM for the first time.
 *
 * Return: 0 if successful, otherwise a standard Linux error code
 */
int kbase_arbiter_pm_early_init(struct kbase_device *kbdev);

/**
 * kbase_arbiter_pm_early_term() - Shutdown arbiter and free resources.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Clean up all the resources
 */
void kbase_arbiter_pm_early_term(struct kbase_device *kbdev);

/**
 * kbase_arbiter_pm_release_interrupts() - Release the GPU interrupts
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Releases interrupts and set the interrupt flag to false
 */
void kbase_arbiter_pm_release_interrupts(struct kbase_device *kbdev);

/**
 * kbase_arbiter_pm_install_interrupts() - Install the GPU interrupts
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Install interrupts and set the interrupt_install flag to true.
 *
 * Return: 0 if success, or a Linux error code
 */
int kbase_arbiter_pm_install_interrupts(struct kbase_device *kbdev);

/**
 * kbase_arbiter_pm_vm_event() - Dispatch VM event to the state machine
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @event: The event to dispatch
 *
 * The state machine function. Receives events and transitions states
 * according the event received and the current state
 */
void kbase_arbiter_pm_vm_event(struct kbase_device *kbdev,
	enum kbase_arbif_evt event);

/**
 * kbase_arbiter_pm_ctx_active_handle_suspend() - Handle suspend operation for
 *                                                arbitration mode
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @suspend_handler: The handler code for how to handle a suspend
 *                   that might occur
 *
 * This function handles a suspend event from the driver,
 * communicating with the arbiter and waiting synchronously for the GPU
 * to be granted again depending on the VM state.
 *
 * Return: 0 if success, 1 if failure due to system suspending/suspended
 */
int kbase_arbiter_pm_ctx_active_handle_suspend(struct kbase_device *kbdev,
	enum kbase_pm_suspend_handler suspend_handler);


/**
 * kbase_arbiter_pm_vm_stopped() - Handle stop event for the VM
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * This function handles a stop event for the VM.
 * It will update the VM state and forward the stop event to the driver.
 */
void kbase_arbiter_pm_vm_stopped(struct kbase_device *kbdev);

/**
 * kbase_arbiter_set_max_config() - Set the max config data in kbase device.
 * @kbdev: The kbase device structure for the device (must be a valid pointer).
 * @max_l2_slices: The maximum number of L2 slices.
 * @max_core_mask: The largest core mask.
 *
 * This function handles a stop event for the VM.
 * It will update the VM state and forward the stop event to the driver.
 */
void kbase_arbiter_set_max_config(struct kbase_device *kbdev,
				  uint32_t max_l2_slices,
				  uint32_t max_core_mask);

/**
 * kbase_arbiter_pm_gpu_assigned() - Determine if this VM has access to the GPU
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: 0 if the VM does not have access, 1 if it does, and a negative number
 * if an error occurred
 */
int kbase_arbiter_pm_gpu_assigned(struct kbase_device *kbdev);

extern struct kbase_clk_rate_trace_op_conf arb_clk_rate_trace_ops;

/**
 * struct kbase_arbiter_freq - Holding the GPU clock frequency data retrieved
 * from arbiter
 * @arb_freq:      GPU clock frequency value
 * @arb_freq_lock: Mutex protecting access to arbfreq value
 * @nb:            Notifier block to receive rate change callbacks
 * @freq_updated:  Flag to indicate whether a frequency changed has just been
 *                 communicated to avoid "GPU_GRANTED when not expected" warning
 */
struct kbase_arbiter_freq {
	uint32_t arb_freq;
	struct mutex arb_freq_lock;
	struct notifier_block *nb;
	bool freq_updated;
};

/**
 * kbase_arbiter_pm_update_gpu_freq() - Update GPU frequency
 * @arb_freq: Pointer to GPU clock frequency data
 * @freq:     The new frequency
 *
 * Updates the GPU frequency and triggers any notifications
 */
void kbase_arbiter_pm_update_gpu_freq(struct kbase_arbiter_freq *arb_freq,
	uint32_t freq);

#endif /*_MALI_KBASE_ARBITER_PM_H_ */
