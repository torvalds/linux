/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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
 * Register-based HW access backend specific APIs
 */

#ifndef _KBASE_HWACCESS_GPU_H_
#define _KBASE_HWACCESS_GPU_H_

#include <backend/gpu/mali_kbase_pm_internal.h>

/**
 * kbase_gpu_irq_evict - Evict an atom from a NEXT slot
 *
 * @kbdev:         Device pointer
 * @js:            Job slot to evict from
 *
 * Evict the atom in the NEXT slot for the specified job slot. This function is
 * called from the job complete IRQ handler when the previous job has failed.
 *
 * Return: true if job evicted from NEXT registers, false otherwise
 */
bool kbase_gpu_irq_evict(struct kbase_device *kbdev, int js);

/**
 * kbase_gpu_complete_hw - Complete an atom on job slot js
 *
 * @kbdev:           Device pointer
 * @js:              Job slot that has completed
 * @completion_code: Event code from job that has completed
 * @job_tail:        The tail address from the hardware if the job has partially
 *                   completed
 * @end_timestamp:   Time of completion
 */
void kbase_gpu_complete_hw(struct kbase_device *kbdev, int js,
				u32 completion_code,
				u64 job_tail,
				ktime_t *end_timestamp);

/**
 * kbase_gpu_inspect - Inspect the contents of the HW access ringbuffer
 *
 * @kbdev:  Device pointer
 * @js:     Job slot to inspect
 * @idx:    Index into ringbuffer. 0 is the job currently running on
 *          the slot, 1 is the job waiting, all other values are invalid.
 * Return:  The atom at that position in the ringbuffer
 *          or NULL if no atom present
 */
struct kbase_jd_atom *kbase_gpu_inspect(struct kbase_device *kbdev, int js,
					int idx);

/**
 * kbase_gpu_slot_update - Update state based on slot ringbuffers
 *
 * @kbdev:  Device pointer
 *
 * Inspect the jobs in the slot ringbuffers and update state.
 *
 * This will cause jobs to be submitted to hardware if they are unblocked
 */
void kbase_gpu_slot_update(struct kbase_device *kbdev);

/**
 * kbase_gpu_dump_slots - Print the contents of the slot ringbuffers
 *
 * @kbdev:  Device pointer
 */
void kbase_gpu_dump_slots(struct kbase_device *kbdev);

#endif /* _KBASE_HWACCESS_GPU_H_ */
