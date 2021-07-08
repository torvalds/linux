/*
 *
 * (C) COPYRIGHT 2014-2017 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_jm.h>
#include <mali_kbase_js.h>
#include <mali_kbase_tlstream.h>
#include <mali_kbase_10969_workaround.h>
#include <backend/gpu/mali_kbase_cache_policy_backend.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <backend/gpu/mali_kbase_js_affinity.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

/* Return whether the specified ringbuffer is empty. HW access lock must be
 * held */
#define SLOT_RB_EMPTY(rb)   (rb->write_idx == rb->read_idx)
/* Return number of atoms currently in the specified ringbuffer. HW access lock
 * must be held */
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

	kbase_js_debug_log_current_affinities(kbdev);

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

struct kbase_jd_atom *kbase_backend_inspect_head(struct kbase_device *kbdev,
					int js)
{
	return kbase_gpu_inspect(kbdev, js, 0);
}

struct kbase_jd_atom *kbase_backend_inspect_tail(struct kbase_device *kbdev,
					int js)
{
	struct slot_rb *rb = &kbdev->hwaccess.backend.slot_rb[js];

	if (SLOT_RB_EMPTY(rb))
		return NULL;

	return rb->entries[(rb->write_idx - 1) & SLOT_RB_MASK].katom;
}

/**
 * kbase_gpu_atoms_submitted - Inspect whether a slot has any atoms currently
 * on the GPU
 * @kbdev:  Device pointer
 * @js:     Job slot to inspect
 *
 * Return: true if there are atoms on the GPU for slot js,
 *         false otherwise
 */
static bool kbase_gpu_atoms_submitted(struct kbase_device *kbdev, int js)
{
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < SLOT_RB_SIZE; i++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, i);

		if (!katom)
			return false;
		if (katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED ||
				katom->gpu_rb_state == KBASE_ATOM_GPU_RB_READY)
			return true;
	}

	return false;
}

/**
 * kbase_gpu_atoms_submitted_any() - Inspect whether there are any atoms
 * currently on the GPU
 * @kbdev:  Device pointer
 *
 * Return: true if there are any atoms on the GPU, false otherwise
 */
static bool kbase_gpu_atoms_submitted_any(struct kbase_device *kbdev)
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
	if (atomic_read(&kbdev->hwaccess.backend.reset_gpu) !=
						KBASE_RESET_GPU_NOT_PENDING) {
		/* The GPU is being reset - so prevent submission */
		return 0;
	}

	return SLOT_RB_SIZE - kbase_backend_nr_atoms_on_slot(kbdev, js);
}


static void kbasep_js_job_check_deref_cores(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom);

static bool kbasep_js_job_check_ref_cores(struct kbase_device *kbdev,
						int js,
						struct kbase_jd_atom *katom)
{
	/* The most recently checked affinity. Having this at this scope allows
	 * us to guarantee that we've checked the affinity in this function
	 * call.
	 */
	u64 recently_chosen_affinity = 0;
	bool chosen_affinity = false;
	bool retry;

	do {
		retry = false;

		/* NOTE: The following uses a number of FALLTHROUGHs to optimize
		 * the calls to this function. Ending of the function is
		 * indicated by BREAK OUT */
		switch (katom->coreref_state) {
			/* State when job is first attempted to be run */
		case KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED:
			KBASE_DEBUG_ASSERT(katom->affinity == 0);

			/* Compute affinity */
			if (false == kbase_js_choose_affinity(
					&recently_chosen_affinity, kbdev, katom,
									js)) {
				/* No cores are currently available */
				/* *** BREAK OUT: No state transition *** */
				break;
			}

			chosen_affinity = true;

			/* Request the cores */
			kbase_pm_request_cores(kbdev,
					katom->core_req & BASE_JD_REQ_T,
						recently_chosen_affinity);

			katom->affinity = recently_chosen_affinity;

			/* Proceed to next state */
			katom->coreref_state =
			KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES;

			/* ***FALLTHROUGH: TRANSITION TO HIGHER STATE*** */
			/* fallthrough */
		case KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES:
			{
				enum kbase_pm_cores_ready cores_ready;

				KBASE_DEBUG_ASSERT(katom->affinity != 0 ||
					(katom->core_req & BASE_JD_REQ_T));

				cores_ready = kbase_pm_register_inuse_cores(
						kbdev,
						katom->core_req & BASE_JD_REQ_T,
						katom->affinity);
				if (cores_ready == KBASE_NEW_AFFINITY) {
					/* Affinity no longer valid - return to
					 * previous state */
					kbasep_js_job_check_deref_cores(kbdev,
									katom);
					KBASE_TRACE_ADD_SLOT_INFO(kbdev,
					JS_CORE_REF_REGISTER_INUSE_FAILED,
							katom->kctx, katom,
							katom->jc, js,
							(u32) katom->affinity);
					/* *** BREAK OUT: Return to previous
					 * state, retry *** */
					retry = true;
					break;
				}
				if (cores_ready == KBASE_CORES_NOT_READY) {
					/* Stay in this state and return, to
					 * retry at this state later */
					KBASE_TRACE_ADD_SLOT_INFO(kbdev,
					JS_CORE_REF_REGISTER_INUSE_FAILED,
							katom->kctx, katom,
							katom->jc, js,
							(u32) katom->affinity);
					/* *** BREAK OUT: No state transition
					 * *** */
					break;
				}
				/* Proceed to next state */
				katom->coreref_state =
				KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY;
			}

			/* ***FALLTHROUGH: TRANSITION TO HIGHER STATE*** */
			/* fallthrough */
		case KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY:
			KBASE_DEBUG_ASSERT(katom->affinity != 0 ||
					(katom->core_req & BASE_JD_REQ_T));

			/* Optimize out choosing the affinity twice in the same
			 * function call */
			if (chosen_affinity == false) {
				/* See if the affinity changed since a previous
				 * call. */
				if (false == kbase_js_choose_affinity(
						&recently_chosen_affinity,
							kbdev, katom, js)) {
					/* No cores are currently available */
					kbasep_js_job_check_deref_cores(kbdev,
									katom);
					KBASE_TRACE_ADD_SLOT_INFO(kbdev,
					JS_CORE_REF_REQUEST_ON_RECHECK_FAILED,
						katom->kctx, katom,
						katom->jc, js,
						(u32) recently_chosen_affinity);
					/* *** BREAK OUT: Transition to lower
					 * state *** */
					break;
				}
				chosen_affinity = true;
			}

			/* Now see if this requires a different set of cores */
			if (recently_chosen_affinity != katom->affinity) {
				enum kbase_pm_cores_ready cores_ready;

				kbase_pm_request_cores(kbdev,
						katom->core_req & BASE_JD_REQ_T,
						recently_chosen_affinity);

				/* Register new cores whilst we still hold the
				 * old ones, to minimize power transitions */
				cores_ready =
					kbase_pm_register_inuse_cores(kbdev,
						katom->core_req & BASE_JD_REQ_T,
						recently_chosen_affinity);
				kbasep_js_job_check_deref_cores(kbdev, katom);

				/* Fixup the state that was reduced by
				 * deref_cores: */
				katom->coreref_state =
				KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY;
				katom->affinity = recently_chosen_affinity;
				if (cores_ready == KBASE_NEW_AFFINITY) {
					/* Affinity no longer valid - return to
					 * previous state */
					katom->coreref_state =
					KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES;

					kbasep_js_job_check_deref_cores(kbdev,
									katom);

					KBASE_TRACE_ADD_SLOT_INFO(kbdev,
					JS_CORE_REF_REGISTER_INUSE_FAILED,
							katom->kctx, katom,
							katom->jc, js,
							(u32) katom->affinity);
					/* *** BREAK OUT: Return to previous
					 * state, retry *** */
					retry = true;
					break;
				}
				/* Now might be waiting for powerup again, with
				 * a new affinity */
				if (cores_ready == KBASE_CORES_NOT_READY) {
					/* Return to previous state */
					katom->coreref_state =
					KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES;
					KBASE_TRACE_ADD_SLOT_INFO(kbdev,
					JS_CORE_REF_REGISTER_ON_RECHECK_FAILED,
							katom->kctx, katom,
							katom->jc, js,
							(u32) katom->affinity);
					/* *** BREAK OUT: Transition to lower
					 * state *** */
					break;
				}
			}
			/* Proceed to next state */
			katom->coreref_state =
			KBASE_ATOM_COREREF_STATE_CHECK_AFFINITY_VIOLATIONS;

			/* ***FALLTHROUGH: TRANSITION TO HIGHER STATE*** */
			/* fallthrough */
		case KBASE_ATOM_COREREF_STATE_CHECK_AFFINITY_VIOLATIONS:
			KBASE_DEBUG_ASSERT(katom->affinity != 0 ||
					(katom->core_req & BASE_JD_REQ_T));
			KBASE_DEBUG_ASSERT(katom->affinity ==
						recently_chosen_affinity);

			/* Note: this is where the caller must've taken the
			 * hwaccess_lock */

			/* Check for affinity violations - if there are any,
			 * then we just ask the caller to requeue and try again
			 * later */
			if (kbase_js_affinity_would_violate(kbdev, js,
					katom->affinity) != false) {
				/* Return to previous state */
				katom->coreref_state =
				KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY;
				/* *** BREAK OUT: Transition to lower state ***
				 */
				KBASE_TRACE_ADD_SLOT_INFO(kbdev,
					JS_CORE_REF_AFFINITY_WOULD_VIOLATE,
					katom->kctx, katom, katom->jc, js,
					(u32) katom->affinity);
				break;
			}

			/* No affinity violations would result, so the cores are
			 * ready */
			katom->coreref_state = KBASE_ATOM_COREREF_STATE_READY;
			/* *** BREAK OUT: Cores Ready *** */
			break;

		default:
			KBASE_DEBUG_ASSERT_MSG(false,
					"Unhandled kbase_atom_coreref_state %d",
							katom->coreref_state);
			break;
		}
	} while (retry != false);

	return (katom->coreref_state == KBASE_ATOM_COREREF_STATE_READY);
}

static void kbasep_js_job_check_deref_cores(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	switch (katom->coreref_state) {
	case KBASE_ATOM_COREREF_STATE_READY:
		/* State where atom was submitted to the HW - just proceed to
		 * power-down */
		KBASE_DEBUG_ASSERT(katom->affinity != 0 ||
					(katom->core_req & BASE_JD_REQ_T));

		/* fallthrough */

	case KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY:
		/* State where cores were registered */
		KBASE_DEBUG_ASSERT(katom->affinity != 0 ||
					(katom->core_req & BASE_JD_REQ_T));
		kbase_pm_release_cores(kbdev, katom->core_req & BASE_JD_REQ_T,
							katom->affinity);

		break;

	case KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES:
		/* State where cores were requested, but not registered */
		KBASE_DEBUG_ASSERT(katom->affinity != 0 ||
					(katom->core_req & BASE_JD_REQ_T));
		kbase_pm_unrequest_cores(kbdev, katom->core_req & BASE_JD_REQ_T,
							katom->affinity);
		break;

	case KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED:
		/* Initial state - nothing required */
		KBASE_DEBUG_ASSERT(katom->affinity == 0);
		break;

	default:
		KBASE_DEBUG_ASSERT_MSG(false,
						"Unhandled coreref_state: %d",
							katom->coreref_state);
		break;
	}

	katom->affinity = 0;
	katom->coreref_state = KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
}

static void kbasep_js_job_check_deref_cores_nokatom(struct kbase_device *kbdev,
		base_jd_core_req core_req, u64 affinity,
		enum kbase_atom_coreref_state coreref_state)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	switch (coreref_state) {
	case KBASE_ATOM_COREREF_STATE_READY:
		/* State where atom was submitted to the HW - just proceed to
		 * power-down */
		KBASE_DEBUG_ASSERT(affinity != 0 ||
					(core_req & BASE_JD_REQ_T));

		/* fallthrough */

	case KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY:
		/* State where cores were registered */
		KBASE_DEBUG_ASSERT(affinity != 0 ||
					(core_req & BASE_JD_REQ_T));
		kbase_pm_release_cores(kbdev, core_req & BASE_JD_REQ_T,
							affinity);

		break;

	case KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES:
		/* State where cores were requested, but not registered */
		KBASE_DEBUG_ASSERT(affinity != 0 ||
					(core_req & BASE_JD_REQ_T));
		kbase_pm_unrequest_cores(kbdev, core_req & BASE_JD_REQ_T,
							affinity);
		break;

	case KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED:
		/* Initial state - nothing required */
		KBASE_DEBUG_ASSERT(affinity == 0);
		break;

	default:
		KBASE_DEBUG_ASSERT_MSG(false,
						"Unhandled coreref_state: %d",
							coreref_state);
		break;
	}
}

static void kbase_gpu_release_atom(struct kbase_device *kbdev,
					struct kbase_jd_atom *katom,
					ktime_t *end_timestamp)
{
	struct kbase_context *kctx = katom->kctx;

	switch (katom->gpu_rb_state) {
	case KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB:
		/* Should be impossible */
		WARN(1, "Attempting to release atom not in ringbuffer\n");
		break;

	case KBASE_ATOM_GPU_RB_SUBMITTED:
		/* Inform power management at start/finish of atom so it can
		 * update its GPU utilisation metrics. Mark atom as not
		 * submitted beforehand. */
		katom->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;
		kbase_pm_metrics_update(kbdev, end_timestamp);

		if (katom->core_req & BASE_JD_REQ_PERMON)
			kbase_pm_release_gpu_cycle_counter_nolock(kbdev);
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */

		KBASE_TLSTREAM_TL_NRET_ATOM_LPU(katom,
			&kbdev->gpu_props.props.raw_props.js_features
				[katom->slot_nr]);
		KBASE_TLSTREAM_TL_NRET_ATOM_AS(katom, &kbdev->as[kctx->as_nr]);
		KBASE_TLSTREAM_TL_NRET_CTX_LPU(kctx,
			&kbdev->gpu_props.props.raw_props.js_features
				[katom->slot_nr]);
		/* fallthrough */
	case KBASE_ATOM_GPU_RB_READY:
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_GPU_RB_WAITING_AFFINITY:
		kbase_js_affinity_release_slot_cores(kbdev, katom->slot_nr,
							katom->affinity);
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_GPU_RB_WAITING_FOR_CORE_AVAILABLE:
		break;

	case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_TRANSITION:
		if (katom->protected_state.enter !=
				KBASE_ATOM_ENTER_PROTECTED_CHECK ||
				katom->protected_state.exit !=
				KBASE_ATOM_EXIT_PROTECTED_CHECK)
			kbdev->protected_mode_transition = false;

		if (kbase_jd_katom_is_protected(katom) &&
				(katom->protected_state.enter ==
				KBASE_ATOM_ENTER_PROTECTED_IDLE_L2)) {
			kbase_vinstr_resume(kbdev->vinstr_ctx);

			/* Go back to configured model for IPA */
			kbase_ipa_model_use_configured_locked(kbdev);
		}


		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV:
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_GPU_RB_WAITING_BLOCKED:
		/* ***FALLTHROUGH: TRANSITION TO LOWER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_GPU_RB_RETURN_TO_JS:
		break;
	}

	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_WAITING_BLOCKED;
	katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;
}

static void kbase_gpu_mark_atom_for_return(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	kbase_gpu_release_atom(kbdev, katom, NULL);
	katom->gpu_rb_state = KBASE_ATOM_GPU_RB_RETURN_TO_JS;
}

static inline bool kbase_gpu_rmu_workaround(struct kbase_device *kbdev, int js)
{
	struct kbase_backend_data *backend = &kbdev->hwaccess.backend;
	bool slot_busy[3];

	if (!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987))
		return true;
	slot_busy[0] = kbase_gpu_nr_atoms_on_slot_min(kbdev, 0,
					KBASE_ATOM_GPU_RB_WAITING_AFFINITY);
	slot_busy[1] = kbase_gpu_nr_atoms_on_slot_min(kbdev, 1,
					KBASE_ATOM_GPU_RB_WAITING_AFFINITY);
	slot_busy[2] = kbase_gpu_nr_atoms_on_slot_min(kbdev, 2,
					KBASE_ATOM_GPU_RB_WAITING_AFFINITY);

	if ((js == 2 && !(slot_busy[0] || slot_busy[1])) ||
		(js != 2 && !slot_busy[2]))
		return true;

	/* Don't submit slot 2 atom while GPU has jobs on slots 0/1 */
	if (js == 2 && (kbase_gpu_atoms_submitted(kbdev, 0) ||
			kbase_gpu_atoms_submitted(kbdev, 1) ||
			backend->rmu_workaround_flag))
		return false;

	/* Don't submit slot 0/1 atom while GPU has jobs on slot 2 */
	if (js != 2 && (kbase_gpu_atoms_submitted(kbdev, 2) ||
			!backend->rmu_workaround_flag))
		return false;

	backend->rmu_workaround_flag = !backend->rmu_workaround_flag;

	return true;
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

static int kbase_gpu_protected_mode_enter(struct kbase_device *kbdev)
{
	int err = -EINVAL;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ONCE(!kbdev->protected_ops,
			"Cannot enter protected mode: protected callbacks not specified.\n");

	/*
	 * When entering into protected mode, we must ensure that the
	 * GPU is not operating in coherent mode as well. This is to
	 * ensure that no protected memory can be leaked.
	 */
	if (kbdev->system_coherency == COHERENCY_ACE)
		kbase_cache_set_coherency_mode(kbdev, COHERENCY_ACE_LITE);

	if (kbdev->protected_ops) {
		/* Switch GPU to protected mode */
		err = kbdev->protected_ops->protected_mode_enable(
				kbdev->protected_dev);

		if (err)
			dev_warn(kbdev->dev, "Failed to enable protected mode: %d\n",
					err);
		else
			kbdev->protected_mode = true;
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
	kbase_reset_gpu_silent(kbdev);

	return 0;
}

static int kbase_jm_enter_protected_mode(struct kbase_device *kbdev,
		struct kbase_jd_atom **katom, int idx, int js)
{
	int err = 0;

	switch (katom[idx]->protected_state.enter) {
	case KBASE_ATOM_ENTER_PROTECTED_CHECK:
		KBASE_TLSTREAM_AUX_PROTECTED_ENTER_START(kbdev);
		/* The checks in KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV
		 * should ensure that we are not already transitiong, and that
		 * there are no atoms currently on the GPU. */
		WARN_ON(kbdev->protected_mode_transition);
		WARN_ON(kbase_gpu_atoms_submitted_any(kbdev));

		kbdev->protected_mode_transition = true;
		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_VINSTR;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_VINSTR:
		if (kbase_vinstr_try_suspend(kbdev->vinstr_ctx) < 0) {
			/*
			 * We can't switch now because
			 * the vinstr core state switch
			 * is not done yet.
			 */
			return -EAGAIN;
		}

		/* Use generic model for IPA in protected mode */
		kbase_ipa_model_use_fallback_locked(kbdev);

		/* Once reaching this point GPU must be
		 * switched to protected mode or vinstr
		 * re-enabled. */

		/*
		 * Not in correct mode, begin protected mode switch.
		 * Entering protected mode requires us to power down the L2,
		 * and drop out of fully coherent mode.
		 */
		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_IDLE_L2;

		kbase_pm_update_cores_state_nolock(kbdev);

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_IDLE_L2:
		/* Avoid unnecessary waiting on non-ACE platforms. */
		if (kbdev->current_gpu_coherency_mode == COHERENCY_ACE) {
			if (kbase_pm_get_ready_cores(kbdev, KBASE_PM_CORE_L2) ||
				kbase_pm_get_trans_cores(kbdev, KBASE_PM_CORE_L2)) {
				/*
				* The L2 is still powered, wait for all the users to
				* finish with it before doing the actual reset.
				*/
				return -EAGAIN;
			}
		}

		katom[idx]->protected_state.enter =
			KBASE_ATOM_ENTER_PROTECTED_FINISHED;

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_ENTER_PROTECTED_FINISHED:

		/* No jobs running, so we can switch GPU mode right now. */
		err = kbase_gpu_protected_mode_enter(kbdev);

		/*
		 * Regardless of result, we are no longer transitioning
		 * the GPU.
		 */
		kbdev->protected_mode_transition = false;
		KBASE_TLSTREAM_AUX_PROTECTED_ENTER_END(kbdev);
		if (err) {
			/*
			 * Failed to switch into protected mode, resume
			 * vinstr core and fail atom.
			 */
			kbase_vinstr_resume(kbdev->vinstr_ctx);
			katom[idx]->event_code = BASE_JD_EVENT_JOB_INVALID;
			kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);
			/* Only return if head atom or previous atom
			 * already removed - as atoms must be returned
			 * in order. */
			if (idx == 0 || katom[0]->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
				kbase_gpu_dequeue_atom(kbdev, js, NULL);
				kbase_jm_return_atom_to_js(kbdev, katom[idx]);
			}

			/* Go back to configured model for IPA */
			kbase_ipa_model_use_configured_locked(kbdev);

			return -EINVAL;
		}

		/* Protected mode sanity checks. */
		KBASE_DEBUG_ASSERT_MSG(
			kbase_jd_katom_is_protected(katom[idx]) ==
			kbase_gpu_in_protected_mode(kbdev),
			"Protected mode of atom (%d) doesn't match protected mode of GPU (%d)",
			kbase_jd_katom_is_protected(katom[idx]),
			kbase_gpu_in_protected_mode(kbdev));
		katom[idx]->gpu_rb_state =
			KBASE_ATOM_GPU_RB_READY;
	}

	return 0;
}

static int kbase_jm_exit_protected_mode(struct kbase_device *kbdev,
		struct kbase_jd_atom **katom, int idx, int js)
{
	int err = 0;


	switch (katom[idx]->protected_state.exit) {
	case KBASE_ATOM_EXIT_PROTECTED_CHECK:
		KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_START(kbdev);
		/* The checks in KBASE_ATOM_GPU_RB_WAITING_PROTECTED_MODE_PREV
		 * should ensure that we are not already transitiong, and that
		 * there are no atoms currently on the GPU. */
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
		kbase_pm_update_cores_state_nolock(kbdev);

		/* ***TRANSITION TO HIGHER STATE*** */
		/* fallthrough */
	case KBASE_ATOM_EXIT_PROTECTED_IDLE_L2:
		if (kbase_pm_get_ready_cores(kbdev, KBASE_PM_CORE_L2) ||
				kbase_pm_get_trans_cores(kbdev, KBASE_PM_CORE_L2)) {
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

		if (err) {
			kbdev->protected_mode_transition = false;

			/* Failed to exit protected mode, fail atom */
			katom[idx]->event_code = BASE_JD_EVENT_JOB_INVALID;
			kbase_gpu_mark_atom_for_return(kbdev, katom[idx]);
			/* Only return if head atom or previous atom
			 * already removed - as atoms must be returned
			 * in order */
			if (idx == 0 || katom[0]->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
				kbase_gpu_dequeue_atom(kbdev, js, NULL);
				kbase_jm_return_atom_to_js(kbdev, katom[idx]);
			}

			kbase_vinstr_resume(kbdev->vinstr_ctx);

			/* Use generic model for IPA in protected mode */
			kbase_ipa_model_use_fallback_locked(kbdev);

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
				if (katom[idx]->atom_flags &
						KBASE_KATOM_FLAG_X_DEP_BLOCKED)
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
					   completed, not unpulled. */
					katom[idx]->event_code =
						BASE_JD_EVENT_DONE;
					/* Only return if head atom or previous
					 * atom already removed - as atoms must
					 * be returned in order. */
					if (idx == 0 ||	katom[0]->gpu_rb_state ==
							KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
						kbase_gpu_dequeue_atom(kbdev, js, NULL);
						kbase_jm_return_atom_to_js(kbdev, katom[idx]);
					}
					break;
				}

				cores_ready =
					kbasep_js_job_check_ref_cores(kbdev, js,
								katom[idx]);

				if (katom[idx]->event_code ==
						BASE_JD_EVENT_PM_EVENT) {
					katom[idx]->gpu_rb_state =
						KBASE_ATOM_GPU_RB_RETURN_TO_JS;
					break;
				}

				if (!cores_ready)
					break;

				kbase_js_affinity_retain_slot_cores(kbdev, js,
							katom[idx]->affinity);
				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_WAITING_AFFINITY;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_WAITING_AFFINITY:
				if (!kbase_gpu_rmu_workaround(kbdev, js))
					break;

				katom[idx]->gpu_rb_state =
					KBASE_ATOM_GPU_RB_READY;

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_READY:

				if (idx == 1) {
					/* Only submit if head atom or previous
					 * atom already submitted */
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
				 * submit atom if any other slots are in use */
				if ((kbdev->serialize_jobs &
						KBASE_SERIALIZE_INTER_SLOT) &&
						other_slots_busy(kbdev, js))
					break;

				if ((kbdev->serialize_jobs &
						KBASE_SERIALIZE_RESET) &&
						kbase_reset_gpu_active(kbdev))
					break;

				/* Check if this job needs the cycle counter
				 * enabled before submission */
				if (katom[idx]->core_req & BASE_JD_REQ_PERMON)
					kbase_pm_request_gpu_cycle_counter_l2_is_on(
									kbdev);

				kbase_job_hw_submit(kbdev, katom[idx], js);
				katom[idx]->gpu_rb_state =
						KBASE_ATOM_GPU_RB_SUBMITTED;

				/* Inform power management at start/finish of
				 * atom so it can update its GPU utilisation
				 * metrics. */
				kbase_pm_metrics_update(kbdev,
						&katom[idx]->start_timestamp);

				/* ***TRANSITION TO HIGHER STATE*** */
				/* fallthrough */
			case KBASE_ATOM_GPU_RB_SUBMITTED:
				/* Atom submitted to HW, nothing else to do */
				break;

			case KBASE_ATOM_GPU_RB_RETURN_TO_JS:
				/* Only return if head atom or previous atom
				 * already removed - as atoms must be returned
				 * in order */
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

	/* Warn if PRLAM-8987 affinity restrictions are violated */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987))
		WARN_ON((kbase_gpu_atoms_submitted(kbdev, 0) ||
			kbase_gpu_atoms_submitted(kbdev, 1)) &&
			kbase_gpu_atoms_submitted(kbdev, 2));
}


void kbase_backend_run_atom(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);
	kbase_gpu_enqueue_atom(kbdev, katom);
	kbase_backend_slot_update(kbdev);
}

#define HAS_DEP(katom) (katom->pre_dep || katom->atom_flags & \
	(KBASE_KATOM_FLAG_X_DEP_BLOCKED | KBASE_KATOM_FLAG_FAIL_BLOCKER))

bool kbase_gpu_irq_evict(struct kbase_device *kbdev, int js)
{
	struct kbase_jd_atom *katom;
	struct kbase_jd_atom *next_katom;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom = kbase_gpu_inspect(kbdev, js, 0);
	next_katom = kbase_gpu_inspect(kbdev, js, 1);

	if (next_katom && katom->kctx == next_katom->kctx &&
		next_katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED &&
		HAS_DEP(next_katom) &&
		(kbase_reg_read(kbdev, JOB_SLOT_REG(js, JS_HEAD_NEXT_LO), NULL)
									!= 0 ||
		kbase_reg_read(kbdev, JOB_SLOT_REG(js, JS_HEAD_NEXT_HI), NULL)
									!= 0)) {
		kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_COMMAND_NEXT),
				JS_COMMAND_NOP, NULL);
		next_katom->gpu_rb_state = KBASE_ATOM_GPU_RB_READY;

		KBASE_TLSTREAM_TL_NRET_ATOM_LPU(katom,
				&kbdev->gpu_props.props.raw_props.js_features
					[katom->slot_nr]);
		KBASE_TLSTREAM_TL_NRET_ATOM_AS(katom, &kbdev->as
					[katom->kctx->as_nr]);
		KBASE_TLSTREAM_TL_NRET_CTX_LPU(katom->kctx,
				&kbdev->gpu_props.props.raw_props.js_features
					[katom->slot_nr]);

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

	if ((kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6787) || (katom->core_req &
					BASE_JD_REQ_SKIP_CACHE_END)) &&
			completion_code != BASE_JD_EVENT_DONE &&
			!(completion_code & BASE_JD_SW_EVENT)) {
		/* When a job chain fails, on a T60x or when
		 * BASE_JD_REQ_SKIP_CACHE_END is set, the GPU cache is not
		 * flushed. To prevent future evictions causing possible memory
		 * corruption we need to flush the cache manually before any
		 * affected memory gets reused. */
		katom->need_cache_flush_cores_retained = katom->affinity;
		kbase_pm_request_cores(kbdev, false, katom->affinity);
	} else if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10676)) {
		if (kbdev->gpu_props.num_core_groups > 1 &&
			!(katom->affinity &
			kbdev->gpu_props.props.coherency_info.group[0].core_mask
									) &&
			(katom->affinity &
			kbdev->gpu_props.props.coherency_info.group[1].core_mask
									)) {
			dev_info(kbdev->dev, "JD: Flushing cache due to PRLAM-10676\n");
			katom->need_cache_flush_cores_retained =
								katom->affinity;
			kbase_pm_request_cores(kbdev, false,
							katom->affinity);
		}
	}

	katom = kbase_gpu_dequeue_atom(kbdev, js, end_timestamp);
	kbase_timeline_job_slot_done(kbdev, katom->kctx, katom, js, 0);

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
			kbase_gpu_dequeue_atom(kbdev, js, end_timestamp);
			kbase_jm_return_atom_to_js(kbdev, next_katom);
		}
	} else if (completion_code != BASE_JD_EVENT_DONE) {
		struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
		int i;

#if KBASE_TRACE_DUMP_ON_JOB_SLOT_ERROR != 0
		KBASE_TRACE_DUMP(kbdev);
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
				 * dequeued when atom at idx0 completes */
				katom_idx1->event_code = BASE_JD_EVENT_STOPPED;
				kbase_gpu_mark_atom_for_return(kbdev,
								katom_idx1);
			}
		}
	}

	KBASE_TRACE_ADD_SLOT_INFO(kbdev, JM_JOB_DONE, kctx, katom, katom->jc,
					js, completion_code);

	if (job_tail != 0 && job_tail != katom->jc) {
		bool was_updated = (job_tail != katom->jc);

		/* Some of the job has been executed, so we update the job chain
		 * address to where we should resume from */
		katom->jc = job_tail;
		if (was_updated)
			KBASE_TRACE_ADD_SLOT(kbdev, JM_UPDATE_HEAD, katom->kctx,
						katom, job_tail, js);
	}

	/* Only update the event code for jobs that weren't cancelled */
	if (katom->event_code != BASE_JD_EVENT_JOB_CANCELLED)
		katom->event_code = (base_jd_event_code)completion_code;

	kbase_device_trace_register_access(kctx, REG_WRITE,
						JOB_CONTROL_REG(JOB_IRQ_CLEAR),
						1 << js);

	/* Complete the job, and start new ones
	 *
	 * Also defer remaining work onto the workqueue:
	 * - Re-queue Soft-stopped jobs
	 * - For any other jobs, queue the job back into the dependency system
	 * - Schedule out the parent context if necessary, and schedule a new
	 *   one in.
	 */
#ifdef CONFIG_GPU_TRACEPOINTS
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
		/* Cross-slot dependency has now become runnable. Try to submit
		 * it. */

		/* Check if there are lower priority jobs to soft stop */
		kbase_job_slot_ctx_priority_check_locked(kctx, katom);

		kbase_jm_try_kick(kbdev, 1 << katom->slot_nr);
	}

	/* Job completion may have unblocked other atoms. Try to update all job
	 * slots */
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
					KBASE_ATOM_EXIT_PROTECTED_RESET_WAIT)
			{
				KBASE_TLSTREAM_AUX_PROTECTED_LEAVE_END(kbdev);

				kbase_vinstr_resume(kbdev->vinstr_ctx);

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
			if (katom->gpu_rb_state < KBASE_ATOM_GPU_RB_SUBMITTED)
				keep_in_jm_rb = true;

			kbase_gpu_release_atom(kbdev, katom, NULL);

			/*
			 * If the atom wasn't on HW when the reset was issued
			 * then leave it in the RB and next time we're kicked
			 * it will be processed again from the starting state.
			 */
			if (keep_in_jm_rb) {
				kbasep_js_job_check_deref_cores(kbdev, katom);
				katom->coreref_state = KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
				katom->affinity = 0;
				katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;
				/* As the atom was not removed, increment the
				 * index so that we read the correct atom in the
				 * next iteration. */
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

	kbdev->protected_mode_transition = false;
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

static void kbase_job_evicted(struct kbase_jd_atom *katom)
{
	kbase_timeline_job_slot_done(katom->kctx->kbdev, katom->kctx, katom,
			katom->slot_nr, KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT);
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
		 * returned out of order */
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
			if (katom_idx1 && katom_idx1->gpu_rb_state ==
						KBASE_ATOM_GPU_RB_SUBMITTED) {
				/* katom_idx0 and katom_idx1 are on GPU */

				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_COMMAND_NEXT), NULL) == 0) {
					/* idx0 has already completed - stop
					 * idx1 if needed*/
					if (katom_idx1_valid) {
						kbase_gpu_stop_atom(kbdev, js,
								katom_idx1,
								action);
						ret = true;
					}
				} else {
					/* idx1 is in NEXT registers - attempt
					 * to remove */
					kbase_reg_write(kbdev,
							JOB_SLOT_REG(js,
							JS_COMMAND_NEXT),
							JS_COMMAND_NOP, NULL);

					if (kbase_reg_read(kbdev,
							JOB_SLOT_REG(js,
							JS_HEAD_NEXT_LO), NULL)
									!= 0 ||
						kbase_reg_read(kbdev,
							JOB_SLOT_REG(js,
							JS_HEAD_NEXT_HI), NULL)
									!= 0) {
						/* idx1 removed successfully,
						 * will be handled in IRQ */
						kbase_job_evicted(katom_idx1);
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
						 * stop idx1 if needed */
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
				 * atom */
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
						JS_COMMAND_NEXT), NULL) == 0) {
				/* idx0 has already completed - stop idx1 */
				kbase_gpu_stop_atom(kbdev, js, katom_idx1,
									action);
				ret = true;
			} else {
				/* idx1 is in NEXT registers - attempt to
				 * remove */
				kbase_reg_write(kbdev, JOB_SLOT_REG(js,
							JS_COMMAND_NEXT),
							JS_COMMAND_NOP, NULL);

				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_HEAD_NEXT_LO), NULL) != 0 ||
				    kbase_reg_read(kbdev, JOB_SLOT_REG(js,
						JS_HEAD_NEXT_HI), NULL) != 0) {
					/* idx1 removed successfully, will be
					 * handled in IRQ once idx0 completes */
					kbase_job_evicted(katom_idx1);
					kbase_gpu_remove_atom(kbdev, katom_idx1,
									action,
									false);
				} else {
					/* idx0 has already completed - stop
					 * idx1 */
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

void kbase_gpu_cacheclean(struct kbase_device *kbdev)
{
	/* Limit the number of loops to avoid a hang if the interrupt is missed
	 */
	u32 max_loops = KBASE_CLEAN_CACHE_MAX_LOOPS;

	mutex_lock(&kbdev->cacheclean_lock);

	/* use GPU_COMMAND completion solution */
	/* clean & invalidate the caches */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, NULL, 0u, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_CLEAN_INV_CACHES, NULL);

	/* wait for cache flush to complete before continuing */
	while (--max_loops &&
		(kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL) &
						CLEAN_CACHES_COMPLETED) == 0)
		;

	/* clear the CLEAN_CACHES_COMPLETED irq */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, NULL, 0u,
							CLEAN_CACHES_COMPLETED);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR),
						CLEAN_CACHES_COMPLETED, NULL);
	KBASE_DEBUG_ASSERT_MSG(kbdev->hwcnt.backend.state !=
						KBASE_INSTR_STATE_CLEANING,
	    "Instrumentation code was cleaning caches, but Job Management code cleared their IRQ - Instrumentation code will now hang.");

	mutex_unlock(&kbdev->cacheclean_lock);
}

void kbase_backend_cacheclean(struct kbase_device *kbdev,
		struct kbase_jd_atom *katom)
{
	if (katom->need_cache_flush_cores_retained) {
		unsigned long flags;

		kbase_gpu_cacheclean(kbdev);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_pm_unrequest_cores(kbdev, false,
					katom->need_cache_flush_cores_retained);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		katom->need_cache_flush_cores_retained = 0;
	}
}

void kbase_backend_complete_wq(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	/*
	 * If cache flush required due to HW workaround then perform the flush
	 * now
	 */
	kbase_backend_cacheclean(kbdev, katom);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10969)            &&
	    (katom->core_req & BASE_JD_REQ_FS)                        &&
	    katom->event_code == BASE_JD_EVENT_TILE_RANGE_FAULT       &&
	    (katom->atom_flags & KBASE_KATOM_FLAG_BEEN_SOFT_STOPPPED) &&
	    !(katom->atom_flags & KBASE_KATOM_FLAGS_RERUN)) {
		dev_dbg(kbdev->dev, "Soft-stopped fragment shader job got a TILE_RANGE_FAULT. Possible HW issue, trying SW workaround\n");
		if (kbasep_10969_workaround_clamp_coordinates(katom)) {
			/* The job had a TILE_RANGE_FAULT after was soft-stopped
			 * Due to an HW issue we try to execute the job again.
			 */
			dev_dbg(kbdev->dev,
				"Clamping has been executed, try to rerun the job\n"
			);
			katom->event_code = BASE_JD_EVENT_STOPPED;
			katom->atom_flags |= KBASE_KATOM_FLAGS_RERUN;
		}
	}

	/* Clear the coreref_state now - while check_deref_cores() may not have
	 * been called yet, the caller will have taken a copy of this field. If
	 * this is not done, then if the atom is re-scheduled (following a soft
	 * stop) then the core reference would not be retaken. */
	katom->coreref_state = KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
	katom->affinity = 0;
}

void kbase_backend_complete_wq_post_sched(struct kbase_device *kbdev,
		base_jd_core_req core_req, u64 affinity,
		enum kbase_atom_coreref_state coreref_state)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbasep_js_job_check_deref_cores_nokatom(kbdev, core_req, affinity,
			coreref_state);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

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
	struct kbasep_js_device_data *js_devdata;
	unsigned long flags;
	int js;

	js_devdata = &kbdev->js_data;

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
				"  js%d idx%d : katom=%p gpu_rb_state=%d\n",
				js, idx, katom, katom->gpu_rb_state);
			else
				dev_info(kbdev->dev, "  js%d idx%d : empty\n",
								js, idx);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}



