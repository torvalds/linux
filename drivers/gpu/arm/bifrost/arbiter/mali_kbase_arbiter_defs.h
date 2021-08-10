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

/**
 * @file
 * Mali structures define to support arbitration feature
 */

#ifndef _MALI_KBASE_ARBITER_DEFS_H_
#define _MALI_KBASE_ARBITER_DEFS_H_

#include "mali_kbase_arbiter_pm.h"

/**
 * struct kbase_arbiter_vm_state - Struct representing the state and containing the
 *                      data of pm work
 * @kbdev:           Pointer to kbase device structure (must be a valid pointer)
 * @vm_state_lock:   The lock protecting the VM state when arbiter is used.
 *                   This lock must also be held whenever the VM state is being
 *                   transitioned
 * @vm_state_wait:   Wait queue set when GPU is granted
 * @vm_state:        Current state of VM
 * @vm_arb_wq:       Work queue for resuming or stopping work on the GPU for use
 *                   with the Arbiter
 * @vm_suspend_work: Work item for vm_arb_wq to stop current work on GPU
 * @vm_resume_work:  Work item for vm_arb_wq to resume current work on GPU
 * @vm_arb_starting: Work queue resume in progress
 * @vm_arb_stopping: Work queue suspend in progress
 * @interrupts_installed: Flag set when interrupts are installed
 * @vm_request_timer: Timer to monitor GPU request
 */
struct kbase_arbiter_vm_state {
	struct kbase_device *kbdev;
	struct mutex vm_state_lock;
	wait_queue_head_t vm_state_wait;
	enum kbase_vm_state vm_state;
	struct workqueue_struct *vm_arb_wq;
	struct work_struct vm_suspend_work;
	struct work_struct vm_resume_work;
	bool vm_arb_starting;
	bool vm_arb_stopping;
	bool interrupts_installed;
	struct hrtimer vm_request_timer;
};

/**
 * struct kbase_arbiter_device - Representing an instance of arbiter device,
 *                               allocated from the probe method of Mali driver
 * @arb_if:                 Pointer to the arbiter interface device
 * @arb_dev:                Pointer to the arbiter device
 * @arb_freq:               GPU clock frequency retrieved from arbiter.
 */
struct kbase_arbiter_device {
	struct arbiter_if_dev *arb_if;
	struct device *arb_dev;
	struct kbase_arbiter_freq arb_freq;
};

#endif /* _MALI_KBASE_ARBITER_DEFS_H_ */
