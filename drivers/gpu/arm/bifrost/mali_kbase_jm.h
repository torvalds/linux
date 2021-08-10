/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2013-2014, 2016, 2019-2021 ARM Limited. All rights reserved.
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
 * Job manager common APIs
 */

#ifndef _KBASE_JM_H_
#define _KBASE_JM_H_

#if !MALI_USE_CSF
/**
 * kbase_jm_kick() - Indicate that there are jobs ready to run.
 * @kbdev:	Device pointer
 * @js_mask:	Mask of the job slots that can be pulled from.
 *
 * Caller must hold the hwaccess_lock and schedule_sem semaphore
 *
 * Return: Mask of the job slots that can still be submitted to.
 */
u32 kbase_jm_kick(struct kbase_device *kbdev, u32 js_mask);

/**
 * kbase_jm_kick_all() - Indicate that there are jobs ready to run on all job
 *			 slots.
 * @kbdev:	Device pointer
 *
 * Caller must hold the hwaccess_lock and schedule_sem semaphore
 *
 * Return: Mask of the job slots that can still be submitted to.
 */
static inline u32 kbase_jm_kick_all(struct kbase_device *kbdev)
{
	return kbase_jm_kick(kbdev, (1 << kbdev->gpu_props.num_job_slots) - 1);
}

/**
 * kbase_jm_try_kick - Attempt to call kbase_jm_kick
 * @kbdev:   Device pointer
 * @js_mask: Mask of the job slots that can be pulled from
 * Context: Caller must hold hwaccess_lock
 *
 * If schedule_sem can be immediately obtained then this function will call
 * kbase_jm_kick() otherwise it will do nothing.
 */
void kbase_jm_try_kick(struct kbase_device *kbdev, u32 js_mask);

/**
 * kbase_jm_try_kick_all() - Attempt to call kbase_jm_kick_all
 * @kbdev:  Device pointer
 * Context: Caller must hold hwaccess_lock
 *
 * If schedule_sem can be immediately obtained then this function will call
 * kbase_jm_kick_all() otherwise it will do nothing.
 */
void kbase_jm_try_kick_all(struct kbase_device *kbdev);
#endif /* !MALI_USE_CSF */

#if !MALI_USE_CSF
/**
 * kbase_jm_idle_ctx() - Mark a context as idle.
 * @kbdev:	Device pointer
 * @kctx:	Context to mark as idle
 *
 * No more atoms will be pulled from this context until it is marked as active
 * by kbase_js_use_ctx().
 *
 * The context should have no atoms currently pulled from it
 * (kctx->atoms_pulled == 0).
 *
 * Caller must hold the hwaccess_lock
 */
void kbase_jm_idle_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * kbase_jm_return_atom_to_js() - Return an atom to the job scheduler that has
 *				  been soft-stopped or will fail due to a
 *				  dependency
 * @kbdev:	Device pointer
 * @katom:	Atom that has been stopped or will be failed
 *
 * Return: Atom that has now been unblocked and can now be run, or NULL if none
 */
struct kbase_jd_atom *kbase_jm_return_atom_to_js(struct kbase_device *kbdev,
			struct kbase_jd_atom *katom);

/**
 * kbase_jm_complete() - Complete an atom
 * @kbdev:		Device pointer
 * @katom:		Atom that has completed
 * @end_timestamp:	Timestamp of atom completion
 *
 * Return: Atom that has now been unblocked and can now be run, or NULL if none
 */
struct kbase_jd_atom *kbase_jm_complete(struct kbase_device *kbdev,
		struct kbase_jd_atom *katom, ktime_t *end_timestamp);
#endif /* !MALI_USE_CSF */

#endif /* _KBASE_JM_H_ */
