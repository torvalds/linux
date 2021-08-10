// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
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
 * Register-based HW access backend specific APIs
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_jm.h>
#include <mali_kbase_js.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_kbase_hwcnt_context.h>
#include <mali_kbase_reset_gpu.h>
#include <mali_kbase_kinstr_jm.h>
#include <backend/gpu/mali_kbase_cache_policy_backend.h>
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

/* Return whether the specified ringbuffer is empty. HW access lock must be
 * held
 */
#define SLOT_RB_EMPTY(rb)   (rb->write_idx == rb->read_idx)
/* Return number of atoms currently in the specified ringbuffer. HW access lock
 * must be held
 */
#define SLOT_RB_ENTRIES(rb) (int)(s8)(rb->write_idx - rb->read_idx)

static void kbase_gpu_release_atom(struct kbase_device *kbdev,
					struct kbase_jd_atom *katom,
					ktime_t *end_timestamp);

/**
 * kbase_gpu_enqueue_atom - Enqueue an atom in the HW access ringbuffer
 * @kbdev: Device pointer
 * @katom: Atom to enqueue
 *
 * Context: Caller must hold the HW access lock
 */
static void kbase_gpu_enqueue_atom(struct kbase_device *kbdev,
					struct kbase_jd_atom *katom)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[katom->slot_nr];

	WARN_ON(SLOT_RB_ENTRIES(rb) >= SLOT_RB_SIZE);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	rb->entries[rb->write_idx & SLOT_RB_MASK].katom = katom;
	rb->write_idx++;

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_WAITING_BLOCKED;
}

/**
 * kbase_gpu_dequeue_atom - Remove an atom from the HW access ringbuffer, once
 * it has been completed
 * @kbdev:         Device pointer
 * @js:            Job slot to remove atom from
 * @end_timestamp: Pointer to timestamp of atom completion. May be NULL, in
 *                 which case current time will be used.
 *
 * Context: Caller must hold the HW access lock
 *
 * Return: Atom removed from ringbuffer
 */
static struct kbase_jd_atom *kbase_gpu_dequeue_atom(struct kbase_device *kbdev,
						int js,
						ktime_t *end_timestamp)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];
	struct kbase_jd_atom *katom;

	if (SLOT_RB_EMPTY(rb)) {
		WARN(1, "GPU ringbuffer unexpectedly empty\n");
		return NULL;
	}

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom = rb->entries[rb->read_idx & SLOT_RB_MASK].katom;

	kbase_gpu_release_atom(kbdev, katom, end_timestamp);

	rb->read_idx++;

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB;

	return katom;
}

struct kbase_jd_atom *kbase_gpu_inspect(struct kbase_device *kbdev, int js,
					int idx)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if ((SLOT_RB_ENTRIES(rb) - 1) < idx)
		return NULL; /* idx out of range */

	return rb->entries[(rb->read_idx + idx) & SLOT_RB_MASK].katom;
}

struct kbase_jd_atom *kbase_backend_inspect_tail(struct kbase_device *kbdev,
					int js)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];

	if (SLOT_RB_EMPTY(rb))
		return NULL;

	return rb->entries[(rb->write_idx - 1) & SLOT_RB_MASK].katom;
}

bool kbase_gpu_atoms_submitted_any(struct kbase_device *kbdev)
{
	int js;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		for (i = 0; i < SLOT_RB_SIZE; i++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

			if (katom && katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
				return true;
		}
	}
	return false;
}

int kbase_backend_nr_atoms_submitted(struct kbase_device *kbdev, int js)
{
	int nr = 0;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

		if (katom && (katom->gpu_rb_state ==
						KBASE_ATOM_GPU_RB_SUBMITTED))
			nr++;
	}

	return nr;
}

int kbase_backend_nr_atoms_on_slot(struct kbase_device *kbdev, int js)
{
	int nr = 0;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		if (kbase_gpu_inspect(kbdev, js, i))
			nr++;
	}

	return nr;
}

static int kbase_gpu_nr_atoms_on_slot_min(struct kbase_device *kbdev, int js,
				enum kbase_atom_gpu_rb_state min_rb_state)
{
	int nr = 0;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

		if (katom && (katom->gpu_rb_state >= min_rb_state))
			nr++;
	}

	return nr;
}

/**
 * check_secure_atom - Check if the given atom is in the given secure state and
 *                     has a ringbuffer state of at least
 *                     KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION
 * @katom:  Atom pointer
 * @secure: Desired secure state
 *
 * Return: true if atom is in the given state, false otherwise
 */
static bool check_secure_atom(struct kbase_jd_atom *katom, bool secure)
{
	if (katom->gpu_rb_state >=
			KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION &&
			((kbase_jd_katom_is_protected(katom) && secure) ||
			(!kbase_jd_katom_is_protected(katom) && !secure)))
		return true;

	return false;
}

/**
 * kbase_gpu_check_secure_atoms - Check if there are any atoms in the given
 *                                secure state in the ringbuffers of at least
 *                                state
 *                                KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE
 * @kbdev:  Device pointer
 * @secure: Desired secure state
 *
 * Return: true if any atoms are in the given state, false otherwise
 */
static bool kbase_gpu_check_secure_atoms(struct kbase_device *kbdev,
		bool secure)
{
	int js, i;

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		for (i = 0; i < SLOT_RB_SIZE; i++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev,
					js, i);

			if (katom) {
				if (check_secure_atom(katom, secure))
					return true;
			}
		}
	}

	return false;
}

int kbase_backend_slot_free(struct kbase_device *kbdev, int js)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (atomic_read(&kbdev->hwaccess.backend.reset_gpu) !=
						KBASE_RESET_GPU_NOT_PENDING) {
		/* The GPU is being reset - so prevent submission */
		return 0;
	}

	return SLOT_RB_SIZE - kbase_backend_nr_atoms_on_slot(kbdev, js);
}


static void kbase_gpu_release_atom(struct kbase_device *kbdev,
					struct kbase_jd_atom *katom,
					ktime_t *end_timestamp)
{
	struct kbase_context *kctx = katom->kctx;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	switch (katom->gpu_rb_state) {
	case KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB:
		/* Should be impossible */
		WARN(1, "Attempting to release atom not in ringbuffer\n");
		break;

	case KBASE_ATOM_GPU_RB_SUBMITTED:
		kbase_kinstr_jm_atom_hw_release(katom);
		/* Inform power management at start/finish of atom so it can
		 * update its GPU utilisation metrics. Mark atom as not
		 * submitted beforehand.
		 */
		katom->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;
		kbase_pm_metrics_update(kbdev, end_timestamp);

		/* Inform platform at start/finish of atom */
		kbasep_platform_event_atom_complete(katom);

		if (katom->core_req & BASE_JD_REQ_PERMON)
			kbase_pm_release_gpu_cycle_counter_nolock(kbdev);

		KBASE_TLSTREAM_TL_NRET_ATOM_LPU(kbdev, katom,
			&kbdev->gpu_props.props.raw_props.js_features
				[katom->slot_nr]);
		KBASE_TLSTREAM_TL_NRET_ATOM_AS(kbdev, katom, &kbdev->as[kctx->as_nr]);
		KBASE_TLSTREAM_TL_NRET_CTX_LPU(kbdev, kctx,
			&kbdev->gpu_props.props.raw_props.js_features
				[katom->slot_nr]);

		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */

	case KBASE_ATOM_GPU_RB_READY:
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */

	case KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE:
		break;

	case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION:
		if (kbase_jd_katom_is_protected(katom) &&
				(katom->protected_state.enter !=
				KBASE_ATOM_ENTER_PROTECTED_CHECK) &&
				(katom->protected_state.enter !=
				KBASE_ATOM_ENTER_PROTECTED_HWCNT)) {
			kbase_pm_protected_override_disable(kbdev);
			kbase_pm_update_cores_state_nolock(kbdev);
		}
		if (kbase_jd_katom_is_protected(katom) &&
				(katom->protected_state.enter ==
				KBASE_ATOM_ENTER_PROTECTED_IDLE_L2))
			kbase_pm_protected_entry_override_disable(kbdev);
		if (!kbase_jd_katom_is_protected(katom) &&
				(katom->protected_state.exit !=
				KBASE_ATOM_EXIT_PROTECTED_CHECK) &&
				(katom->protected_state.exit !=
				KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT)) {
			kbase_pm_protected_override_disable(kbdev);
			kbase_pm_update_cores_state_nolock(kbdev);
		}

		if (katom->protected_state.enter !=
				KBASE_ATOM_ENTER_PROTECTED_CHECK ||
				katom->protected_state.exit !=
				KBASE_ATOM_EXIT_PROTECTED_CHECK)
			kbdev->protected_mode_transition = false;
		/* If the atom has suspended hwcnt but has not yet entered
		 * protected mode, then resume hwcnt now. If the GPU is now in
		 * protected mode then hwcnt will be resumed by GPU reset so
		 * don't resume it here.
		 */
		if (kbase_jd_katom_is_protected(katom) &&
				((katom->protected_state.enter ==
				KBASE_ATOM_ENTER_PROTECTED_IDLE_L2) ||
				 (katom->protected_state.enter ==
				KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY))) {
			WARN_ON(!kbdev->protected_mode_hwcnt_disabled);
			kbdev->protected_mode_hwcnt_desired = true;
			if (kbdev->protected_mode_hwcnt_disabled) {
				kbase_hwcnt_context_enable(
					kbdev->hwcnt_gpu_ctx);
				kbdev->protected_mode_hwcnt_disabled = false;
			}
		}

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234)) {
			if (katom->atom_flags &
					KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT) {
				kbase_pm_protected_l2_override(kbdev, false);
				katom->atom_flags &=
					~KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT;
			}
		}

		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */

	case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV:
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */

	case KBASE_ATOM_GPU_RB_WAITING_BLOCKED:
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */

	case KBASE_ATOM_GPU_RB_RETURN_TO_JS:
		break;
	}

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_WAITING_BLOCKED;
	katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;
}

static void kbase_gpu_mark_atom_for_return(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_gpu_release_atom(kbdev, katom, NULL);
	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_RETURN_TO_JS;
}

/**
 * other_slots_busy - Determine if any job slots other than @js are currently
 *                    running atoms
 * @kbdev: Device pointer
 * @js:    Job slot
 *
 * Return: true if any slots other than @js are busy, false otherwise
 */
static inline bool other_slots_busy(struct kbase_device *kbdev, int js)
{
	int slot;

	for (slot = 0; slot < kbdev->gpu_props.num_job_slots; slot++) {
		if (slot == js)
			continue;

		if (kbase_gpu_nr_atoms_on_slot_min(kbdev, slot,
				KBASE_ATOM_GPU_RB_SUBMITTED))
			return true;
	}

	return false;
}

static inline bool kbase_gpu_in_protected_mode(struct kbase_device *kbdev)
{
	return kbdev->protected_mode;
}

static void kbase_gpu_disable_coherent(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	/*
	 * When entering into protected mode, we must ensure that the
	 * GPU is not operating in coherent mode as well. This is to
	 * ensure that no protected memory can be leaked.
	 */
	if (kbdev->system_coherency == COHERENCY_ACE)
		kbase_cache_set_coherency_mode(kbdev, COHERENCY_ACE_LITE);
}

static int kbase_gpu_protected_mode_enter(struct kbase_device *kbdev)
{
	int err = -EINVAL;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ONCE(!kbdev->protected_ops,
			"Cannot enter protected mode: protected callbacks not specified.\n");

	if (kbdev->protected_ops) {
		/* Switch GPU to protected mode */
		err = kbdev->protected_ops->protected_mode_enable(
				kbdev->protected_dev);

		if (err) {
			dev_warn(kbdev->dev, "Failed to enable protected mode: %d\n",
					err);
		} else {
			kbdev->protected_mode = true;
			kbase_ipa_protection_mode_switch_event(kbdev);
		}
	}

	return err;
}

static int kbase_gpu_protected_mode_reset(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ONCE(!kbdev->protected_ops,
			"Cannot exit protected mode: protected callbacks not specified.\n");

	if (!kbdev->protected_ops)
		return -EINVAL;

	/* The protected mode disable callback will be called as part of reset
	 */
	return kbase_reset_gpu_silent(kbdev);
}

static int kbase_jm_protected_entry(struct kbase_device *kbdev,
				struct kbase_jd_atom **katom, int idx, int js)
{
	int err = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	err = kbase_gpu_protected_mode_enter(kbdev);

	/*
	 * Regardless of result before this call, we are no longer
	 * transitioning the GPU.
	 */

	kbdev->protected_mode_transition = false;
	kbase_pm_protected_override_disable(kbdev);
	kbase_pm_update_cores_state_nolock(kbdev);

	KBASE_TLSTREAM_AUX_PROTECTED_ENTER_END(kbdev, kbdev);
	if (err) {
		/*
		 * Failed to switch into protected mode, resume
		 * GPU hwcnt and fail atom.
		 */
		WARN_ON(!kbdev->protected_mode_hwcnt_disabled);
		kbdev->protected_mode_hwcnt_desired = true;
		if (kbdev->protected_mode_hwcnt_disabled) {
			kbase_hwcnt_context_enable(
				kbdev->hwcnt_gpu_ctx);
			kbdev->protected_mode_hwcnt_disabled = false;
		}

		katom[idx]->event_code = BASE_JD_EVENT_JOB_INVALID;
		kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);
		/*
		 * Only return if head atom or previous atom
		 * already removed - as atoms must be returned
		 * in order.
		 */
		if (idx == 0 || katom[0]->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
			kbase_gpu_dequeue_atom(kbdev, js, NULL);
			kbase_jm_return_atom_to_js(kbdev, katom[idx]);
		}

		return -EINVAL;
	}

	/*
	 * Protected mode sanity checks.
	 */
	KBASE_DEBUG_ASSERT_MSG(
			kbase_jd_katom_is_protected(katom[idx]) ==
			kbase_gpu_in_protected_mode(kbdev),
			"Protected mode of atom (%d) doesn't match protected mode of GPU (%d)",
			kbase_jd_katom_is_protected(katom[idx]),
			kbase_gpu_in_protected_mode(kbdev));
	katom[idx]->gpu_rb_state =
			KBASE_ATOM_GPU_RB_READY;

	return err;
}

static int kbase_jm_enter_protected_mode(struct kbase_device *kbdev,
		struct kbase_jd_atom **katom, int idx, int js)
{
	int err = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	switch (katom[idx]->protected_state.enter) {
	case KBASE_ATOM_ENTER_PROTECTED_CHECK:
		KBASE_TLSTREAM_AUX_PROTECTED_ENTER_START(kbdev, kbdev);
		/* The checks in KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV
		 * should ensure that we are not already transitiong, and that
		 * there are no atoms currently on the GPU.
		 */
		WARN_ON(kbdev->protected_mode_transition);
		WARN_ON(kbase_gpu_atoms_submitted_any(kbdev));
		/* If hwcnt is disabled, it means we didn't clean up correctly
		 * during last exit from protected mode.
		 */
		WARN_ON(kbdev->protected_mode_hwcnt_disabled);

		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_HWCNT;

		kbdev->protected_mode_transition = true;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_HWCNT:
		/* See if we can get away with disabling hwcnt atomically */
		kbdev->protected_mode_hwcnt_desired = false;
		if (!kbdev->protected_mode_hwcnt_disabled) {
			if (kbase_hwcnt_context_disable_atomic(
				kbdev->hwcnt_gpu_ctx))
				kbdev->protected_mode_hwcnt_disabled = true;
		}

		/* We couldn't disable atomically, so kick off a worker */
		if (!kbdev->protected_mode_hwcnt_disabled) {
			kbase_hwcnt_context_queue_work(
				kbdev->hwcnt_gpu_ctx,
				&kbdev->protected_mode_hwcnt_disable_work);
			return -EAGAIN;
		}

		/* Once reaching this point GPU must be switched to protected
		 * mode or hwcnt re-enabled.
		 */

		if (kbase_pm_protected_entry_override_enable(kbdev))
			return -EAGAIN;

		/*
		 * Not in correct mode, begin protected mode switch.
		 * Entering protected mode requires us to power down the L2,
		 * and drop out of fully coherent mode.
		 */
		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_IDLE_L2;

		kbase_pm_protected_override_enable(kbdev);
		/*
		 * Only if the GPU reset hasn't been initiated, there is a need
		 * to invoke the state machine to explicitly power down the
		 * shader cores and L2.
		 */
		if (!kbdev->pm.backend.protected_entry_transition_override)
			kbase_pm_update_cores_state_nolock(kbdev);

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_IDLE_L2:
		/* Avoid unnecessary waiting on non-ACE platforms. */
		if (kbdev->system_coherency == COHERENCY_ACE) {
			if (kbdev->pm.backend.l2_always_on) {
				/*
				 * If the GPU reset hasn't completed, then L2
				 * could still be powered up.
				 */
				if (kbase_reset_gpu_is_active(kbdev))
					return -EAGAIN;
			}

			if (kbase_pm_get_ready_cores(kbdev,
						KBASE_PM_CORE_L2) ||
				kbase_pm_get_trans_cores(kbdev,
						KBASE_PM_CORE_L2) ||
				kbase_is_gpu_removed(kbdev)) {
				/*
				 * The L2 is still powered, wait for all
				 * the users to finish with it before doing
				 * the actual reset.
				 */
				return -EAGAIN;
			}
		}

		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_SET_COHERENCY:
		/*
		 * When entering into protected mode, we must ensure that the
		 * GPU is not operating in coherent mode as well. This is to
		 * ensure that no protected memory can be leaked.
		 */
		kbase_gpu_disable_coherent(kbdev);

		kbase_pm_protected_entry_override_disable(kbdev);

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234)) {
			/*
			 * Power on L2 caches; this will also result in the
			 * correct value written to coherency enable register.
			 */
			kbase_pm_protected_l2_override(kbdev, true);

			/*
			 * Set the flag on the atom that additional
			 * L2 references are taken.
			 */
			katom[idx]->atom_flags |=
					KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT;
		}

		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_FINISHED;

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234))
			return -EAGAIN;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_FINISHED:
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TGOX_R1_1234)) {
			/*
			 * Check that L2 caches are powered and, if so,
			 * enter protected mode.
			 */
			if (kbdev->pm.backend.l2_state == KBASE_L2_ON) {
				/*
				 * Remove additional L2 reference and reset
				 * the atom flag which denotes it.
				 */
				if (katom[idx]->atom_flags &
					KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT) {
					kbase_pm_protected_l2_override(kbdev,
							false);
					katom[idx]->atom_flags &=
						~KBASE_KATOM_FLAG_HOLDING_L2_REF_PROT;
				}

				err = kbase_jm_protected_entry(kbdev, katom, idx, js);

				if (err)
					return err;
			} else {
				/*
				 * still waiting for L2 caches to power up
				 */
				return -EAGAIN;
			}
		} else {
			err = kbase_jm_protected_entry(kbdev, katom, idx, js);

			if (err)
				return err;
		}
	}

	return 0;
}

static int kbase_jm_exit_protected_mode(struct kbase_device *kbdev,
		struct kbase_jd_atom **katom, int idx, int js)
{
	int err = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	switch (katom[idx]->protected_state.exit) {
	case KBASE_ATOM_EXIT_PROTECTED_CHECK:
		KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_START(kbdev, kbdev);
		/* The checks in KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV
		 * should ensure that we are not already transitiong, and that
		 * there are no atoms currently on the GPU.
		 */
		WARN_ON(kbdev->protected_mode_transition);
		WARN_ON(kbase_gpu_atoms_submitted_any(kbdev));

		/*
		 * Exiting protected mode requires a reset, but first the L2
		 * needs to be powered down to ensure it's not active when the
		 * reset is issued.
		 */
		katom[idx]->protected_state.exit =
				KBASE_ATOM_EXIT_PROTECTED_IDLE_L2;

		kbdev->protected_mode_transition = true;
		kbase_pm_protected_override_enable(kbdev);
		kbase_pm_update_cores_state_nolock(kbdev);

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_EXIT_PROTECTED_IDLE_L2:
		if (kbdev->pm.backend.l2_state != KBASE_L2_OFF) {
			/*
			 * The L2 is still powered, wait for all the users to
			 * finish with it before doing the actual reset.
			 */
			return -EAGAIN;
		}
		katom[idx]->protected_state.exit =
				KBASE_ATOM_EXIT_PROTECTED_RESET;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_EXIT_PROTECTED_RESET:
		/* Issue the reset to the GPU */
		err = kbase_gpu_protected_mode_reset(kbdev);

		if (err == -EAGAIN)
			return -EAGAIN;

		if (err) {
			kbdev->protected_mode_transition = false;
			kbase_pm_protected_override_disable(kbdev);

			/* Failed to exit protected mode, fail atom */
			katom[idx]->event_code = BASE_JD_EVENT_JOB_INVALID;
			kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);
			/* Only return if head atom or previous atom
			 * already removed - as atoms must be returned in order
			 */
			if (idx == 0 || katom[0]->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
				kbase_gpu_dequeue_atom(kbdev, js, NULL);
				kbase_jm_return_atom_to_js(kbdev, katom[idx]);
			}

			/* If we're exiting from protected mode, hwcnt must have
			 * been disabled during entry.
			 */
			WARN_ON(!kbdev->protected_mode_hwcnt_disabled);
			kbdev->protected_mode_hwcnt_desired = true;
			if (kbdev->protected_mode_hwcnt_disabled) {
				kbase_hwcnt_context_enable(
					kbdev->hwcnt_gpu_ctx);
				kbdev->protected_mode_hwcnt_disabled = false;
			}

			return -EINVAL;
		}

		katom[idx]->protected_state.exit =
				KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT:
		/* A GPU reset is issued when exiting protected mode. Once the
		 * reset is done all atoms' state will also be reset. For this
		 * reason, if the atom is still in this state we can safely
		 * say that the reset has not completed i.e., we have not
		 * finished exiting protected mode yet.
		 */
		return -EAGAIN;
	}

	return 0;
}

void kbase_backend_slot_update(struct kbase_device *kbdev)
{
	int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_reset_gpu_is_active(kbdev) ||
			kbase_is_gpu_removed(kbdev))
#else
	if (kbase_reset_gpu_is_active(kbdev))
#endif
		return;

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		struct kbase_jd_atom *katom[2];
		int idx;

		katom[0] = kbase_gpu_inspect(kbdev, js, 0);
		katom[1] = kbase_gpu_inspect(kbdev, js, 1);
		WARN_ON(katom[1] && !katom[0]);

		for (idx = 0; idx < SLOT_RB_SIZE; idx++) {
			bool cores_ready;
			int ret;

			if (!katom[idx])
				continue;

			switch (katom[idx]->gpu_rb_state) {
			case KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB:
				/* Should be impossible */
				WARN(1, "Attempting to update atom not in ringbuffer\n");
				break;

			case KBASE_ATOM_GPU_RB_WAITING_BLOCKED:
				if (kbase_js_atom_blocked_on_x_dep(katom[idx]))
					break;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV:
				if (kbase_gpu_check_secure_atoms(kbdev,
						!kbase_jd_katom_is_protected(
						katom[idx])))
					break;

				if ((idx == 1) && (kbase_jd_katom_is_protected(
								katom[0]) !=
						kbase_jd_katom_is_protected(
								katom[1])))
					break;

				if (kbdev->protected_mode_transition)
					break;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION:

				/*
				 * Exiting protected mode must be done before
				 * the references on the cores are taken as
				 * a power down the L2 is required which
				 * can't happen after the references for this
				 * atom are taken.
				 */

				if (!kbase_gpu_in_protected_mode(kbdev) &&
					kbase_jd_katom_is_protected(katom[idx])) {
					/* Atom needs to transition into protected mode. */
					ret = kbase_jm_enter_protected_mode(kbdev,
							katom, idx, js);
					if (ret)
						break;
				} else if (kbase_gpu_in_protected_mode(kbdev) &&
					!kbase_jd_katom_is_protected(katom[idx])) {
					/* Atom needs to transition out of protected mode. */
					ret = kbase_jm_exit_protected_mode(kbdev,
							katom, idx, js);
					if (ret)
						break;
				}
				katom[idx]->protected_state.exit =
						KBASE_ATOM_EXIT_PROTECTED_CHECK;

				/* Atom needs no protected mode transition. */

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE:
				if (katom[idx]->will_fail_event_code) {
					kbase_gpu_mark_atom_for_return(kbdev,
							katom[idx]);
					/* Set EVENT_DONE so this atom will be
					 * completed, not unpulled.
					 */
					katom[idx]->event_code =
						BASE_JD_EVENT_DONE;
					/* Only return if head atom or previous
					 * atom already removed - as atoms must
					 * be returned in order.
					 */
					if (idx == 0 ||	katom[0]->gpu_rb_state ==
							KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
						kbase_gpu_dequeue_atom(kbdev, js, NULL);
						kbase_jm_return_atom_to_js(kbdev, katom[idx]);
					}
					break;
				}

				cores_ready = kbase_pm_cores_requested(kbdev,
						true);

				if (katom[idx]->event_code ==
						BASE_JD_EVENT_PM_EVENT) {
					katom[idx]->gpu_rb_state =
						KBASE_ATOM_GPU_RB_RETURN_TO_JS;
					break;
				}

				if (!cores_ready)
					break;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_READY;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_READY:

				if (idx == 1) {
					/* Only submit if head atom or previous
					 * atom already submitted
					 */
					if ((katom[0]->gpu_rb_state !=
						KBASE_ATOM_GPU_RB_SUBMITTED &&
						katom[0]->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB))
						break;

					/* If intra-slot serialization in use
					 * then don't submit atom to NEXT slot
					 */
					if (kbdev->serialize_jobs &
						KBASE_SERIALIZE_INTRA_SLOT)
						break;
				}

				/* If inter-slot serialization in use then don't
				 * submit atom if any other slots are in use
				 */
				if ((kbdev->serialize_jobs &
						KBASE_SERIALIZE_INTER_SLOT) &&
						other_slots_busy(kbdev, js))
					break;

#ifdef CONFIG_MALI_GEM5_BUILD
				if (!kbasep_jm_is_js_free(kbdev, js,
						katom[idx]->kctx))
					break;
#endif
				/* Check if this job needs the cycle counter
				 * enabled before submission
				 */
				if (katom[idx]->core_req & BASE_JD_REQ_PERMON)
					kbase_pm_request_gpu_cycle_counter_l2_is_on(
									kbdev);

				kbase_job_hw_submit(kbdev, katom[idx], js);
				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_SUBMITTED;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_SUBMITTED:

				/* Inform power management at start/finish of
				 * atom so it can update its GPU utilisation
				 * metrics.
				 */
				kbase_pm_metrics_update(kbdev,
						&katom[idx]->start_timestamp);

				/* Inform platform at start/finish of atom */
				kbasep_platform_event_atom_submit(katom[idx]);

				break;

			case KBASE_ATOM_GPU_RB_RETURN_TO_JS:
				/* Only return if head atom or previous atom
				 * already removed - as atoms must be returned
				 * in order
				 */
				if (idx == 0 || katom[0]->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
					kbase_gpu_dequeue_atom(kbdev, js, NULL);
					kbase_jm_return_atom_to_js(kbdev,
								katom[idx]);
				}
				break;
			}
		}
	}
}


void kbase_backend_run_atom(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	dev_dbg(kbdev->dev, "Backend running atom %pK\n", (void *)katom);

	kbase_gpu_enqueue_atom(kbdev, katom);
	kbase_backend_slot_update(kbdev);
}

#define HAS_DEP(katom) (katom->pre_dep || katom->atom_flags & \
	(KBASE_KATOM_FLAG_X_DEP_BLOCKED | KBASE_KATOM_FLAG_FAIL_BLOCKER))

bool kbase_gpu_irq_evict(struct kbase_device *kbdev, int js,
				u32 completion_code)
{
	struct kbase_jd_atom *katom;
	struct kbase_jd_atom *next_katom;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom = kbase_gpu_inspect(kbdev, js, 0);
	next_katom = kbase_gpu_inspect(kbdev, js, 1);

	if (next_katom && katom->kctx == next_katom->kctx &&
		next_katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED &&
		(HAS_DEP(next_katom) || next_katom->sched_priority ==
				katom->sched_priority) &&
		(kbase_reg_read(kbdev, JOB_SLOT_REG(js, JS_HEAD_NEXT_LO))
									!= 0 ||
		kbase_reg_read(kbdev, JOB_SLOT_REG(js, JS_HEAD_NEXT_HI))
									!= 0)) {
		kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_COMMAND_NEXT),
				JS_COMMAND_NOP);
		next_katom->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;

		if (completion_code == BASE_JD_EVENT_STOPPED) {
			KBASE_TLSTREAM_TL_NRET_ATOM_LPU(kbdev, next_katom,
				&kbdev->gpu_props.props.raw_props.js_features
					[next_katom->slot_nr]);
			KBASE_TLSTREAM_TL_NRET_ATOM_AS(kbdev, next_katom, &kbdev->as
					[next_katom->kctx->as_nr]);
			KBASE_TLSTREAM_TL_NRET_CTX_LPU(kbdev, next_katom->kctx,
				&kbdev->gpu_props.props.raw_props.js_features
					[next_katom->slot_nr]);
		}

		if (next_katom->core_req & BASE_JD_REQ_PERMON)
			kbase_pm_release_gpu_cycle_counter_nolock(kbdev);

		return true;
	}

	return false;
}

void kbase_gpu_complete_hw(struct kbase_device *kbdev, int js,
				u32 completion_code,
				u64 job_tail,
				ktime_t *end_timestamp)
{
	struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, 0);
	struct kbase_context *kctx = katom->kctx;

	dev_dbg(kbdev->dev,
		"Atom %pK completed on hw with code 0x%x and job_tail 0x%llx (s:%d)\n",
		(void *)katom, completion_code, job_tail, js);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/*
	 * When a hard-stop is followed close after a soft-stop, the completion
	 * code may be set to STOPPED, even though the job is terminated
	 */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TMIX_8438)) {
		if (completion_code == BASE_JD_EVENT_STOPPED &&
				(katom->atom_flags &
				KBASE_KATOM_FLAG_BEEN_HARD_STOPPED)) {
			completion_code = BASE_JD_EVENT_TERMINATED;
		}
	}

	if ((katom->core_req & BASE_JD_REQ_SKIP_CACHE_END) &&
			completion_code != BASE_JD_EVENT_DONE &&
			!(completion_code & BASE_JD_SW_EVENT)) {
		/* When a job chain fails, on a T60x or when
		 * BASE_JD_REQ_SKIP_CACHE_END is set, the GPU cache is not
		 * flushed. To prevent future evictions causing possible memory
		 * corruption we need to flush the cache manually before any
		 * affected memory gets reused.
		 */
		katom->need_cache_flush_cores_retained = true;
	}

	katom = kbase_gpu_dequeue_atom(kbdev, js, end_timestamp);

	if (completion_code == BASE_JD_EVENT_STOPPED) {
		struct kbase_jd_atom *next_katom = kbase_gpu_inspect(kbdev, js,
									0);

		/*
		 * Dequeue next atom from ringbuffers on same slot if required.
		 * This atom will already have been removed from the NEXT
		 * registers by kbase_gpu_soft_hard_stop_slot(), to ensure that
		 * the atoms on this slot are returned in the correct order.
		 */
		if (next_katom && katom->kctx == next_katom->kctx &&
				next_katom->sched_priority ==
				katom->sched_priority) {
			WARN_ON(next_katom->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_SUBMITTED);
			kbase_gpu_dequeue_atom(kbdev, js, end_timestamp);
			kbase_jm_return_atom_to_js(kbdev, next_katom);
		}
	} else if (completion_code != BASE_JD_EVENT_DONE) {
		struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
		int i;

		if (!kbase_ctx_flag(katom->kctx, KCTX_DYING))
			dev_warn(kbdev->dev, "error detected from slot %d, job status 0x%08x (%s)",
					js, completion_code,
					kbase_gpu_exception_name(
					completion_code));

#if KBASE_KTRACE_DUMP_ON_JOB_SLOT_ERROR != 0
		KBASE_KTRACE_DUMP(kbdev);
#endif
		kbasep_js_clear_submit_allowed(js_devdata, katom->kctx);

		/*
		 * Remove all atoms on the same context from ringbuffers. This
		 * will not remove atoms that are already on the GPU, as these
		 * are guaranteed not to have fail dependencies on the failed
		 * atom.
		 */
		for (i = 0; i < kbdev->gpu_props.num_job_slots; i++) {
			struct kbase_jd_atom *katom_idx0 =
						kbase_gpu_inspect(kbdev, i, 0);
			struct kbase_jd_atom *katom_idx1 =
						kbase_gpu_inspect(kbdev, i, 1);

			if (katom_idx0 && katom_idx0->kctx == katom->kctx &&
					HAS_DEP(katom_idx0) &&
					katom_idx0->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_SUBMITTED) {
				/* Dequeue katom_idx0 from ringbuffer */
				kbase_gpu_dequeue_atom(kbdev, i, end_timestamp);

				if (katom_idx1 &&
						katom_idx1->kctx == katom->kctx
						&& HAS_DEP(katom_idx1) &&
						katom_idx0->gpu_rb_state !=
						KBASE_ATOM_GPU_RB_SUBMITTED) {
					/* Dequeue katom_idx1 from ringbuffer */
					kbase_gpu_dequeue_atom(kbdev, i,
							end_timestamp);

					katom_idx1->event_code =
							BASE_JD_EVENT_STOPPED;
					kbase_jm_return_atom_to_js(kbdev,
								katom_idx1);
				}
				katom_idx0->event_code = BASE_JD_EVENT_STOPPED;
				kbase_jm_return_atom_to_js(kbdev, katom_idx0);

			} else if (katom_idx1 &&
					katom_idx1->kctx == katom->kctx &&
					HAS_DEP(katom_idx1) &&
					katom_idx1->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_SUBMITTED) {
				/* Can not dequeue this atom yet - will be
				 * dequeued when atom at idx0 completes
				 */
				katom_idx1->event_code = BASE_JD_EVENT_STOPPED;
				kbase_gpu_mark_atom_for_return(kbdev,
								katom_idx1);
			}
		}
	}

	KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, JM_JOB_DONE, kctx, katom, katom->jc, js, completion_code);

	if (job_tail != 0 && job_tail != katom->jc) {
		/* Some of the job has been executed */
		dev_dbg(kbdev->dev,
			"Update job chain address of atom %pK to resume from 0x%llx\n",
			(void *)katom, job_tail);

		katom->jc = job_tail;
		KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_UPDATE_HEAD, katom->kctx,
					katom, job_tail, js);
	}

	/* Only update the event code for jobs that weren't cancelled */
	if (katom->event_code != BASE_JD_EVENT_JOB_CANCELLED)
		katom->event_code = (enum base_jd_event_code)completion_code;

	/* Complete the job, and start new ones
	 *
	 * Also defer remaining work onto the workqueue:
	 * - Re-queue Soft-stopped jobs
	 * - For any other jobs, queue the job back into the dependency system
	 * - Schedule out the parent context if necessary, and schedule a new
	 *   one in.
	 */
#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
	{
		/* The atom in the HEAD */
		struct kbase_jd_atom *next_katom = kbase_gpu_inspect(kbdev, js,
									0);

		if (next_katom && next_katom->gpu_rb_state ==
						KBASE_ATOM_GPU_RB_SUBMITTED) {
			char js_string[16];

			trace_gpu_sched_switch(kbasep_make_job_slot_string(js,
							js_string,
							sizeof(js_string)),
						ktime_to_ns(*end_timestamp),
						(u32)next_katom->kctx->id, 0,
						next_katom->work_id);
			kbdev->hwaccess.backend.slot_rb[js].last_context =
							next_katom->kctx;
		} else {
			char js_string[16];

			trace_gpu_sched_switch(kbasep_make_job_slot_string(js,
							js_string,
							sizeof(js_string)),
						ktime_to_ns(ktime_get()), 0, 0,
						0);
			kbdev->hwaccess.backend.slot_rb[js].last_context = 0;
		}
	}
#endif

	if (kbdev->serialize_jobs & KBASE_SERIALIZE_RESET)
		kbase_reset_gpu_silent(kbdev);

	if (completion_code == BASE_JD_EVENT_STOPPED)
		katom = kbase_jm_return_atom_to_js(kbdev, katom);
	else
		katom = kbase_jm_complete(kbdev, katom, end_timestamp);

	if (katom) {
		dev_dbg(kbdev->dev,
			"Cross-slot dependency %pK has become runnable.\n",
			(void *)katom);

		/* Check if there are lower priority jobs to soft stop */
		kbase_job_slot_ctx_priority_check_locked(kctx, katom);

		kbase_jm_try_kick(kbdev, 1 << katom->slot_nr);
	}

	/* For partial shader core off L2 cache flush */
	kbase_pm_update_state(kbdev);

	/* Job completion may have unblocked other atoms. Try to update all job
	 * slots
	 */
	kbase_backend_slot_update(kbdev);
}

void kbase_backend_reset(struct kbase_device *kbdev, ktime_t *end_timestamp)
{
	int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Reset should always take the GPU out of protected mode */
	WARN_ON(kbase_gpu_in_protected_mode(kbdev));

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		int atom_idx = 0;
		int idx;

		for (idx = 0; idx < SLOT_RB_SIZE; idx++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev,
					js, atom_idx);
			bool keep_in_jm_rb = false;

			if (!katom)
				break;
			if (katom->protected_state.exit ==
			    KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT) {
				/* protected mode sanity checks */
				KBASE_DEBUG_ASSERT_MSG(
					kbase_jd_katom_is_protected(katom) == kbase_gpu_in_protected_mode(kbdev),
					"Protected mode of atom (%d) doesn't match protected mode of GPU (%d)",
					kbase_jd_katom_is_protected(katom), kbase_gpu_in_protected_mode(kbdev));
				KBASE_DEBUG_ASSERT_MSG(
					(kbase_jd_katom_is_protected(katom) && js == 0) ||
					!kbase_jd_katom_is_protected(katom),
					"Protected atom on JS%d not supported", js);
			}
			if ((katom->gpu_rb_state < KBASE_ATOM_GPU_RB_SUBMITTED) &&
			    !kbase_ctx_flag(katom->kctx, KCTX_DYING))
				keep_in_jm_rb = true;

			kbase_gpu_release_atom(kbdev, katom, NULL);

			/*
			 * If the atom wasn't on HW when the reset was issued
			 * then leave it in the RB and next time we're kicked
			 * it will be processed again from the starting state.
			 */
			if (keep_in_jm_rb) {
				katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;
				/* As the atom was not removed, increment the
				 * index so that we read the correct atom in the
				 * next iteration.
				 */
				atom_idx++;
				continue;
			}

			/*
			 * The atom was on the HW when the reset was issued
			 * all we can do is fail the atom.
			 */
			kbase_gpu_dequeue_atom(kbdev, js, NULL);
			katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
			kbase_jm_complete(kbdev, katom, end_timestamp);
		}
	}

	/* Re-enable GPU hardware counters if we're resetting from protected
	 * mode.
	 */
	kbdev->protected_mode_hwcnt_desired = true;
	if (kbdev->protected_mode_hwcnt_disabled) {
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
		kbdev->protected_mode_hwcnt_disabled = false;

		KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_END(kbdev, kbdev);
	}

	kbdev->protected_mode_transition = false;
	kbase_pm_protected_override_disable(kbdev);
}

static inline void kbase_gpu_stop_atom(struct kbase_device *kbdev,
					int js,
					struct kbase_jd_atom *katom,
					u32 action)
{
	u32 hw_action = action & JS_COMMAND_MASK;

	kbase_job_check_enter_disjoint(kbdev, action, katom->core_req, katom);
	kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, hw_action,
							katom->core_req, katom);
	katom->kctx->blocked_js[js][katom->sched_priority] = true;
}

static inline void kbase_gpu_remove_atom(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom,
						u32 action,
						bool disjoint)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
	kbase_gpu_mark_atom_for_return(kbdev, katom);
	katom->kctx->blocked_js[katom->slot_nr][katom->sched_priority] = true;

	if (disjoint)
		kbase_job_check_enter_disjoint(kbdev, action, katom->core_req,
									katom);
}

static int should_stop_x_dep_slot(struct kbase_jd_atom *katom)
{
	if (katom->x_post_dep) {
		struct kbase_jd_atom *dep_atom = katom->x_post_dep;

		if (dep_atom->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB &&
			dep_atom->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_RETURN_TO_JS)
			return dep_atom->slot_nr;
	}
	return -1;
}

bool kbase_backend_soft_hard_stop_slot(struct kbase_device *kbdev,
					struct kbase_context *kctx,
					int js,
					struct kbase_jd_atom *katom,
					u32 action)
{
	struct kbase_jd_atom *katom_idx0;
	struct kbase_jd_atom *katom_idx1;

	bool katom_idx0_valid, katom_idx1_valid;

	bool ret = false;

	int stop_x_dep_idx0 = -1, stop_x_dep_idx1 = -1;
	int prio_idx0 = 0, prio_idx1 = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom_idx0 = kbase_gpu_inspect(kbdev, js, 0);
	katom_idx1 = kbase_gpu_inspect(kbdev, js, 1);

	if (katom_idx0)
		prio_idx0 = katom_idx0->sched_priority;
	if (katom_idx1)
		prio_idx1 = katom_idx1->sched_priority;

	if (katom) {
		katom_idx0_valid = (katom_idx0 == katom);
		/* If idx0 is to be removed and idx1 is on the same context,
		 * then idx1 must also be removed otherwise the atoms might be
		 * returned out of order
		 */
		if (katom_idx1)
			katom_idx1_valid = (katom_idx1 == katom) ||
						(katom_idx0_valid &&
							(katom_idx0->kctx ==
							katom_idx1->kctx));
		else
			katom_idx1_valid = false;
	} else {
		katom_idx0_valid = (katom_idx0 &&
				(!kctx || katom_idx0->kctx == kctx));
		katom_idx1_valid = (katom_idx1 &&
				(!kctx || katom_idx1->kctx == kctx) &&
				prio_idx0 == prio_idx1);
	}

	if (katom_idx0_valid)
		stop_x_dep_idx0 = should_stop_x_dep_slot(katom_idx0);
	if (katom_idx1_valid)
		stop_x_dep_idx1 = should_stop_x_dep_slot(katom_idx1);

	if (katom_idx0_valid) {
		if (katom_idx0->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {
			/* Simple case - just dequeue and return */
			kbase_gpu_dequeue_atom(kbdev, js, NULL);
			if (katom_idx1_valid) {
				kbase_gpu_dequeue_atom(kbdev, js, NULL);
				katom_idx1->event_code =
						BASE_JD_EVENT_REMOVED_FROM_NEXT;
				kbase_jm_return_atom_to_js(kbdev, katom_idx1);
				katom_idx1->kctx->blocked_js[js][prio_idx1] =
						true;
			}

			katom_idx0->event_code =
						BASE_JD_EVENT_REMOVED_FROM_NEXT;
			kbase_jm_return_atom_to_js(kbdev, katom_idx0);
			katom_idx0->kctx->blocked_js[js][prio_idx0] = true;
		} else {
			/* katom_idx0 is on GPU */
			if (katom_idx1_valid && katom_idx1->gpu_rb_state ==
						KBASE_ATOM_GPU_RB_SUBMITTED) {
				/* katom_idx0 and katom_idx1 are on GPU */

				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_COMMAND_NEXT)) == 0) {
					/* idx0 has already completed - stop
					 * idx1 if needed
					 */
					if (katom_idx1_valid) {
						kbase_gpu_stop_atom(kbdev, js,
								katom_idx1,
								action);
						ret = true;
					}
				} else {
					/* idx1 is in NEXT registers - attempt
					 * to remove
					 */
					kbase_reg_write(kbdev,
							JOB_SLOT_REG(js,
							JS_COMMAND_NEXT),
							JS_COMMAND_NOP);

					if (kbase_reg_read(kbdev,
							JOB_SLOT_REG(js,
							JS_HEAD_NEXT_LO))
									!= 0 ||
						kbase_reg_read(kbdev,
							JOB_SLOT_REG(js,
							JS_HEAD_NEXT_HI))
									!= 0) {
						/* idx1 removed successfully,
						 * will be handled in IRQ
						 */
						kbase_gpu_remove_atom(kbdev,
								katom_idx1,
								action, true);
						stop_x_dep_idx1 =
					should_stop_x_dep_slot(katom_idx1);

						/* stop idx0 if still on GPU */
						kbase_gpu_stop_atom(kbdev, js,
								katom_idx0,
								action);
						ret = true;
					} else if (katom_idx1_valid) {
						/* idx0 has already completed,
						 * stop idx1 if needed
						 */
						kbase_gpu_stop_atom(kbdev, js,
								katom_idx1,
								action);
						ret = true;
					}
				}
			} else if (katom_idx1_valid) {
				/* idx1 not on GPU but must be dequeued*/

				/* idx1 will be handled in IRQ */
				kbase_gpu_remove_atom(kbdev, katom_idx1, action,
									false);
				/* stop idx0 */
				/* This will be repeated for anything removed
				 * from the next registers, since their normal
				 * flow was also interrupted, and this function
				 * might not enter disjoint state e.g. if we
				 * don't actually do a hard stop on the head
				 * atom
				 */
				kbase_gpu_stop_atom(kbdev, js, katom_idx0,
									action);
				ret = true;
			} else {
				/* no atom in idx1 */
				/* just stop idx0 */
				kbase_gpu_stop_atom(kbdev, js, katom_idx0,
									action);
				ret = true;
			}
		}
	} else if (katom_idx1_valid) {
		if (katom_idx1->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED) {
			/* Mark for return */
			/* idx1 will be returned once idx0 completes */
			kbase_gpu_remove_atom(kbdev, katom_idx1, action,
									false);
		} else {
			/* idx1 is on GPU */
			if (kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_COMMAND_NEXT)) == 0) {
				/* idx0 has already completed - stop idx1 */
				kbase_gpu_stop_atom(kbdev, js, katom_idx1,
									action);
				ret = true;
			} else {
				/* idx1 is in NEXT registers - attempt to
				 * remove
				 */
				kbase_reg_write(kbdev, JOB_SLOT_REG(js,
							JS_COMMAND_NEXT),
							JS_COMMAND_NOP);

				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_HEAD_NEXT_LO)) != 0 ||
				    kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_HEAD_NEXT_HI)) != 0) {
					/* idx1 removed successfully, will be
					 * handled in IRQ once idx0 completes
					 */
					kbase_gpu_remove_atom(kbdev, katom_idx1,
									action,
									false);
				} else {
					/* idx0 has already completed - stop
					 * idx1
					 */
					kbase_gpu_stop_atom(kbdev, js,
								katom_idx1,
								action);
					ret = true;
				}
			}
		}
	}


	if (stop_x_dep_idx0 != -1)
		kbase_backend_soft_hard_stop_slot(kbdev, kctx, stop_x_dep_idx0,
								NULL, action);

	if (stop_x_dep_idx1 != -1)
		kbase_backend_soft_hard_stop_slot(kbdev, kctx, stop_x_dep_idx1,
								NULL, action);

	return ret;
}

void kbase_backend_cache_clean(struct kbase_device *kbdev,
		struct kbase_jd_atom *katom)
{
	if (katom->need_cache_flush_cores_retained) {
		kbase_gpu_start_cache_clean(kbdev);
		kbase_gpu_wait_cache_clean(kbdev);

		katom->need_cache_flush_cores_retained = false;
	}
}

void kbase_backend_complete_wq(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	/*
	 * If cache flush required due to HW workaround then perform the flush
	 * now
	 */
	kbase_backend_cache_clean(kbdev, katom);
}

void kbase_backend_complete_wq_post_sched(struct kbase_device *kbdev,
		base_jd_core_req core_req)
{
	if (!kbdev->pm.active_count) {
		mutex_lock(&kbdev->js_data.runpool_mutex);
		mutex_lock(&kbdev->pm.lock);
		kbase_pm_update_active(kbdev);
		mutex_unlock(&kbdev->pm.lock);
		mutex_unlock(&kbdev->js_data.runpool_mutex);
	}
}

void kbase_gpu_dump_slots(struct kbase_device *kbdev)
{
	unsigned long flags;
	int js;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	dev_info(kbdev->dev, "kbase_gpu_dump_slots:\n");

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		int idx;

		for (idx = 0; idx < SLOT_RB_SIZE; idx++) {
			struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev,
									js,
									idx);

			if (katom)
				dev_info(kbdev->dev,
				"  js%d idx%d : katom=%pK gpu_rb_state=%d\n",
				js, idx, katom, katom->gpu_rb_state);
			else
				dev_info(kbdev->dev, "  js%d idx%d : empty\n",
								js, idx);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}
