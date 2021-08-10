/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2021 ARM Limited. All rights reserved.
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

/*
 * Register-based HW access backend specific definitions
 */

#ifndef _KBASE_HWACCESS_GPU_DEFS_H_
#define _KBASE_HWACCESS_GPU_DEFS_H_

/* SLOT_RB_SIZE must be < 256 */
#define SLOT_RB_SIZE 2
#define SLOT_RB_MASK (SLOT_RB_SIZE - 1)

/**
 * struct rb_entry - Ringbuffer entry
 * @katom:	Atom associated with this entry
 */
struct rb_entry {
	struct kbase_jd_atom *katom;
};

/**
 * struct slot_rb - Slot ringbuffer
 * @entries:		Ringbuffer entries
 * @last_context:	The last context to submit a job on this slot
 * @read_idx:		Current read index of buffer
 * @write_idx:		Current write index of buffer
 * @job_chain_flag:	Flag used to implement jobchain disambiguation
 */
struct slot_rb {
	struct rb_entry entries[SLOT_RB_SIZE];

	struct kbase_context *last_context;

	u8 read_idx;
	u8 write_idx;

	u8 job_chain_flag;
};

/**
 * struct kbase_backend_data - GPU backend specific data for HW access layer
 * @slot_rb:			Slot ringbuffers
 * @scheduling_timer:		The timer tick used for rescheduling jobs
 * @timer_running:		Is the timer running? The runpool_mutex must be
 *				held whilst modifying this.
 * @suspend_timer:              Is the timer suspended? Set when a suspend
 *                              occurs and cleared on resume. The runpool_mutex
 *                              must be held whilst modifying this.
 * @reset_gpu:			Set to a KBASE_RESET_xxx value (see comments)
 * @reset_workq:		Work queue for performing the reset
 * @reset_work:			Work item for performing the reset
 * @reset_wait:			Wait event signalled when the reset is complete
 * @reset_timer:		Timeout for soft-stops before the reset
 * @timeouts_updated:           Have timeout values just been updated?
 *
 * The hwaccess_lock (a spinlock) must be held when accessing this structure
 */
struct kbase_backend_data {
#if !MALI_USE_CSF
	struct slot_rb slot_rb[BASE_JM_MAX_NR_SLOTS];
	struct hrtimer scheduling_timer;

	bool timer_running;
#endif
	bool suspend_timer;

	atomic_t reset_gpu;

/* The GPU reset isn't pending */
#define KBASE_RESET_GPU_NOT_PENDING     0
/* kbase_prepare_to_reset_gpu has been called */
#define KBASE_RESET_GPU_PREPARED        1
/* kbase_reset_gpu has been called - the reset will now definitely happen
 * within the timeout period
 */
#define KBASE_RESET_GPU_COMMITTED       2
/* The GPU reset process is currently occuring (timeout has expired or
 * kbasep_try_reset_gpu_early was called)
 */
#define KBASE_RESET_GPU_HAPPENING       3
/* Reset the GPU silently, used when resetting the GPU as part of normal
 * behavior (e.g. when exiting protected mode).
 */
#define KBASE_RESET_GPU_SILENT          4
	struct workqueue_struct *reset_workq;
	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	struct hrtimer reset_timer;

	bool timeouts_updated;
};

#endif /* _KBASE_HWACCESS_GPU_DEFS_H_ */
