// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
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
 * Base kernel job manager APIs
 */

#include <mali_kbase.h>
#include <mali_kbase_config.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_linux_trace.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_reset_gpu.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_kinstr_jm.h>
#include <mali_kbase_hwaccess_instr.h>
#include <mali_kbase_hwcnt_context.h>
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_jm_internal.h>
#include <mali_kbase_regs_history_debugfs.h>

static void kbasep_try_reset_gpu_early_locked(struct kbase_device *kbdev);
static u64 kbasep_apply_limited_core_mask(const struct kbase_device *kbdev,
				const u64 affinity, const u64 limited_core_mask);

static u64 kbase_job_write_affinity(struct kbase_device *kbdev,
				base_jd_core_req core_req,
				int js, const u64 limited_core_mask)
{
	u64 affinity;
	bool skip_affinity_check = false;

	if ((core_req & (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T)) ==
			BASE_JD_REQ_T) {
		/* Tiler-only atom, affinity value can be programed as 0 */
		affinity = 0;
		skip_affinity_check = true;
	} else if ((core_req & (BASE_JD_REQ_COHERENT_GROUP |
			BASE_JD_REQ_SPECIFIC_COHERENT_GROUP))) {
		unsigned int num_core_groups = kbdev->gpu_props.num_core_groups;
		struct mali_base_gpu_coherent_group_info *coherency_info =
			&kbdev->gpu_props.props.coherency_info;

		affinity = kbdev->pm.backend.shaders_avail &
				kbdev->pm.debug_core_mask[js];

		/* JS2 on a dual core group system targets core group 1. All
		 * other cases target core group 0.
		 */
		if (js == 2 && num_core_groups > 1)
			affinity &= coherency_info->group[1].core_mask;
		else if (num_core_groups > 1)
			affinity &= coherency_info->group[0].core_mask;
		else
			affinity &= kbdev->gpu_props.curr_config.shader_present;
	} else {
		/* Use all cores */
		affinity = kbdev->pm.backend.shaders_avail &
				kbdev->pm.debug_core_mask[js];
	}

	if (core_req & BASE_JD_REQ_LIMITED_CORE_MASK) {
		/* Limiting affinity due to BASE_JD_REQ_LIMITED_CORE_MASK by applying the limited core mask. */
		affinity = kbasep_apply_limited_core_mask(kbdev, affinity, limited_core_mask);
	}

	if (unlikely(!affinity && !skip_affinity_check)) {
#ifdef CONFIG_MALI_BIFROST_DEBUG
		u64 shaders_ready =
			kbase_pm_get_ready_cores(kbdev, KBASE_PM_CORE_SHADER);

		WARN_ON(!(shaders_ready & kbdev->pm.backend.shaders_avail));
#endif

		affinity = kbdev->pm.backend.shaders_avail;

		if (core_req & BASE_JD_REQ_LIMITED_CORE_MASK) {
			/* Limiting affinity again to make sure it only enables shader cores with backed TLS memory. */
			affinity = kbasep_apply_limited_core_mask(kbdev, affinity, limited_core_mask);

#ifdef CONFIG_MALI_BIFROST_DEBUG
			/* affinity should never be 0 */
			WARN_ON(!affinity);
#endif
		}
	}

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_AFFINITY_NEXT_LO),
					affinity & 0xFFFFFFFF);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_AFFINITY_NEXT_HI),
					affinity >> 32);

	return affinity;
}

/**
 * select_job_chain() - Select which job chain to submit to the GPU
 * @katom: Pointer to the atom about to be submitted to the GPU
 *
 * Selects one of the fragment job chains attached to the special atom at the
 * end of a renderpass, or returns the address of the single job chain attached
 * to any other type of atom.
 *
 * Which job chain is selected depends upon whether the tiling phase of the
 * renderpass completed normally or was soft-stopped because it used too
 * much memory. It also depends upon whether one of the fragment job chains
 * has already been run as part of the same renderpass.
 *
 * Return: GPU virtual address of the selected job chain
 */
static u64 select_job_chain(struct kbase_jd_atom *katom)
{
	struct kbase_context *const kctx = katom->kctx;
	u64 jc = katom->jc;
	struct kbase_jd_renderpass *rp;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (!(katom->core_req & BASE_JD_REQ_END_RENDERPASS))
		return jc;

	compiletime_assert((1ull << (sizeof(katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[katom->renderpass_id];
	/* We can read a subset of renderpass state without holding
	 * higher-level locks (but not end_katom, for example).
	 * If the end-of-renderpass atom is running with as-yet indeterminate
	 * OOM state then assume that the start atom was not soft-stopped.
	 */
	switch (rp->state) {
	case KBASE_JD_RP_OOM:
		/* Tiling ran out of memory.
		 * Start of incremental rendering, used once.
		 */
		jc = katom->jc_fragment.norm_read_forced_write;
		break;
	case KBASE_JD_RP_START:
	case KBASE_JD_RP_PEND_OOM:
		/* Tiling completed successfully first time.
		 * Single-iteration rendering, used once.
		 */
		jc = katom->jc_fragment.norm_read_norm_write;
		break;
	case KBASE_JD_RP_RETRY_OOM:
		/* Tiling ran out of memory again.
		 * Continuation of incremental rendering, used as
		 * many times as required.
		 */
		jc = katom->jc_fragment.forced_read_forced_write;
		break;
	case KBASE_JD_RP_RETRY:
	case KBASE_JD_RP_RETRY_PEND_OOM:
		/* Tiling completed successfully this time.
		 * End of incremental rendering, used once.
		 */
		jc = katom->jc_fragment.forced_read_norm_write;
		break;
	default:
		WARN_ON(1);
		break;
	}

	dev_dbg(kctx->kbdev->dev,
		"Selected job chain 0x%llx for end atom %pK in state %d\n",
		jc, (void *)katom, (int)rp->state);

	katom->jc = jc;
	return jc;
}

void kbase_job_hw_submit(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom,
				int js)
{
	struct kbase_context *kctx;
	u32 cfg;
	u64 const jc_head = select_job_chain(katom);
	u64 affinity;
	struct slot_rb *ptr_slot_rb = &kbdev->hwaccess.backend.slot_rb[js];

	lockdep_assert_held(&kbdev->hwaccess_lock);
	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(katom);

	kctx = katom->kctx;

	/* Command register must be available */
	KBASE_DEBUG_ASSERT(kbasep_jm_is_js_free(kbdev, js, kctx));

	dev_dbg(kctx->kbdev->dev, "Write JS_HEAD_NEXT 0x%llx for atom %pK\n",
		jc_head, (void *)katom);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_HEAD_NEXT_LO),
						jc_head & 0xFFFFFFFF);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_HEAD_NEXT_HI),
						jc_head >> 32);

	affinity = kbase_job_write_affinity(kbdev, katom->core_req, js,
						kctx->limited_core_mask);

	/* start MMU, medium priority, cache clean/flush on end, clean/flush on
	 * start
	 */
	cfg = kctx->as_nr;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_FLUSH_REDUCTION) &&
			!(kbdev->serialize_jobs & KBASE_SERIALIZE_RESET))
		cfg |= JS_CONFIG_ENABLE_FLUSH_REDUCTION;

	if (0 != (katom->core_req & BASE_JD_REQ_SKIP_CACHE_START)) {
		/* Force a cache maintenance operation if the newly submitted
		 * katom to the slot is from a different kctx. For a JM GPU
		 * that has the feature BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER,
		 * applies a FLUSH_INV_SHADER_OTHER. Otherwise, do a
		 * FLUSH_CLEAN_INVALIDATE.
		 */
		u64 tagged_kctx = ptr_slot_rb->last_kctx_tagged;

		if (tagged_kctx != SLOT_RB_NULL_TAG_VAL && tagged_kctx != SLOT_RB_TAG_KCTX(kctx)) {
			if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_FLUSH_INV_SHADER_OTHER))
				cfg |= JS_CONFIG_START_FLUSH_INV_SHADER_OTHER;
			else
				cfg |= JS_CONFIG_START_FLUSH_CLEAN_INVALIDATE;
		} else
			cfg |= JS_CONFIG_START_FLUSH_NO_ACTION;
	} else
		cfg |= JS_CONFIG_START_FLUSH_CLEAN_INVALIDATE;

	if (0 != (katom->core_req & BASE_JD_REQ_SKIP_CACHE_END) &&
			!(kbdev->serialize_jobs & KBASE_SERIALIZE_RESET))
		cfg |= JS_CONFIG_END_FLUSH_NO_ACTION;
	else if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_CLEAN_ONLY_SAFE))
		cfg |= JS_CONFIG_END_FLUSH_CLEAN;
	else
		cfg |= JS_CONFIG_END_FLUSH_CLEAN_INVALIDATE;

	cfg |= JS_CONFIG_THREAD_PRI(8);

	if ((katom->atom_flags & KBASE_KATOM_FLAG_PROTECTED) ||
	    (katom->core_req & BASE_JD_REQ_END_RENDERPASS))
		cfg |= JS_CONFIG_DISABLE_DESCRIPTOR_WR_BK;

	if (!ptr_slot_rb->job_chain_flag) {
		cfg |= JS_CONFIG_JOB_CHAIN_FLAG;
		katom->atom_flags |= KBASE_KATOM_FLAGS_JOBCHAIN;
		ptr_slot_rb->job_chain_flag = true;
	} else {
		katom->atom_flags &= ~KBASE_KATOM_FLAGS_JOBCHAIN;
		ptr_slot_rb->job_chain_flag = false;
	}

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_CONFIG_NEXT), cfg);

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_FLUSH_REDUCTION))
		kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_FLUSH_ID_NEXT),
				katom->flush_id);

	/* Write an approximate start timestamp.
	 * It's approximate because there might be a job in the HEAD register.
	 */
	katom->start_timestamp = ktime_get_raw();

	/* GO ! */
	dev_dbg(kbdev->dev, "JS: Submitting atom %pK from ctx %pK to js[%d] with head=0x%llx",
				katom, kctx, js, jc_head);

	KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, JM_SUBMIT, kctx, katom, jc_head, js,
							(u32)affinity);

	KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT(kbdev, kctx,
		js, kbase_jd_atom_id(kctx, katom), TL_JS_EVENT_START);

	KBASE_TLSTREAM_TL_ATTRIB_ATOM_CONFIG(kbdev, katom, jc_head,
			affinity, cfg);
	KBASE_TLSTREAM_TL_RET_CTX_LPU(
		kbdev,
		kctx,
		&kbdev->gpu_props.props.raw_props.js_features[
			katom->slot_nr]);
	KBASE_TLSTREAM_TL_RET_ATOM_AS(kbdev, katom, &kbdev->as[kctx->as_nr]);
	KBASE_TLSTREAM_TL_RET_ATOM_LPU(
			kbdev,
			katom,
			&kbdev->gpu_props.props.raw_props.js_features[js],
			"ctx_nr,atom_nr");
	kbase_kinstr_jm_atom_hw_submit(katom);

	/* Update the slot's last katom submission kctx */
	ptr_slot_rb->last_kctx_tagged = SLOT_RB_TAG_KCTX(kctx);

#if IS_ENABLED(CONFIG_GPU_TRACEPOINTS)
	if (!kbase_backend_nr_atoms_submitted(kbdev, js)) {
		/* If this is the only job on the slot, trace it as starting */
		char js_string[16];

		trace_gpu_sched_switch(
				kbasep_make_job_slot_string(js, js_string,
						sizeof(js_string)),
				ktime_to_ns(katom->start_timestamp),
				(u32)katom->kctx->id, 0, katom->work_id);
	}
#endif

	trace_sysgraph_gpu(SGR_SUBMIT, kctx->id,
			kbase_jd_atom_id(kctx, katom), js);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_COMMAND_NEXT),
						JS_COMMAND_START);
}

/**
 * kbasep_job_slot_update_head_start_timestamp - Update timestamp
 * @kbdev: kbase device
 * @js: job slot
 * @end_timestamp: timestamp
 *
 * Update the start_timestamp of the job currently in the HEAD, based on the
 * fact that we got an IRQ for the previous set of completed jobs.
 *
 * The estimate also takes into account the time the job was submitted, to
 * work out the best estimate (which might still result in an over-estimate to
 * the calculated time spent)
 */
static void kbasep_job_slot_update_head_start_timestamp(
						struct kbase_device *kbdev,
						int js,
						ktime_t end_timestamp)
{
	ktime_t timestamp_diff;
	struct kbase_jd_atom *katom;

	/* Checking the HEAD position for the job slot */
	katom = kbase_gpu_inspect(kbdev, js, 0);
	if (katom != NULL) {
		timestamp_diff = ktime_sub(end_timestamp,
				katom->start_timestamp);
		if (ktime_to_ns(timestamp_diff) >= 0) {
			/* Only update the timestamp if it's a better estimate
			 * than what's currently stored. This is because our
			 * estimate that accounts for the throttle time may be
			 * too much of an overestimate
			 */
			katom->start_timestamp = end_timestamp;
		}
	}
}

/**
 * kbasep_trace_tl_event_lpu_softstop - Call event_lpu_softstop timeline
 * tracepoint
 * @kbdev: kbase device
 * @js: job slot
 *
 * Make a tracepoint call to the instrumentation module informing that
 * softstop happened on given lpu (job slot).
 */
static void kbasep_trace_tl_event_lpu_softstop(struct kbase_device *kbdev,
					int js)
{
	KBASE_TLSTREAM_TL_EVENT_LPU_SOFTSTOP(
		kbdev,
		&kbdev->gpu_props.props.raw_props.js_features[js]);
}

void kbase_job_done(struct kbase_device *kbdev, u32 done)
{
	int i;
	u32 count = 0;
	ktime_t end_timestamp;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	KBASE_DEBUG_ASSERT(kbdev);

	KBASE_KTRACE_ADD_JM(kbdev, JM_IRQ, NULL, NULL, 0, done);

	end_timestamp = ktime_get_raw();

	while (done) {
		u32 failed = done >> 16;

		/* treat failed slots as finished slots */
		u32 finished = (done & 0xFFFF) | failed;

		/* Note: This is inherently unfair, as we always check for lower
		 * numbered interrupts before the higher numbered ones.
		 */
		i = ffs(finished) - 1;
		KBASE_DEBUG_ASSERT(i >= 0);

		do {
			int nr_done;
			u32 active;
			u32 completion_code = BASE_JD_EVENT_DONE;/* assume OK */
			u64 job_tail = 0;

			if (failed & (1u << i)) {
				/* read out the job slot status code if the job
				 * slot reported failure
				 */
				completion_code = kbase_reg_read(kbdev,
					JOB_SLOT_REG(i, JS_STATUS));

				if (completion_code == BASE_JD_EVENT_STOPPED) {
					u64 job_head;

					KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT(
						kbdev, NULL,
						i, 0, TL_JS_EVENT_SOFT_STOP);

					kbasep_trace_tl_event_lpu_softstop(
						kbdev, i);

					/* Soft-stopped job - read the value of
					 * JS<n>_TAIL so that the job chain can
					 * be resumed
					 */
					job_tail = (u64)kbase_reg_read(kbdev,
						JOB_SLOT_REG(i, JS_TAIL_LO)) |
						((u64)kbase_reg_read(kbdev,
						JOB_SLOT_REG(i, JS_TAIL_HI))
						 << 32);
					job_head = (u64)kbase_reg_read(kbdev,
						JOB_SLOT_REG(i, JS_HEAD_LO)) |
						((u64)kbase_reg_read(kbdev,
						JOB_SLOT_REG(i, JS_HEAD_HI))
						 << 32);
					/* For a soft-stopped job chain js_tail should
					 * same as the js_head, but if not then the
					 * job chain was incorrectly marked as
					 * soft-stopped. In such case we should not
					 * be resuming the job chain from js_tail and
					 * report the completion_code as UNKNOWN.
					 */
					if (job_tail != job_head)
						completion_code = BASE_JD_EVENT_UNKNOWN;

				} else if (completion_code ==
						BASE_JD_EVENT_NOT_STARTED) {
					/* PRLAM-10673 can cause a TERMINATED
					 * job to come back as NOT_STARTED,
					 * but the error interrupt helps us
					 * detect it
					 */
					completion_code =
						BASE_JD_EVENT_TERMINATED;
				}

				kbase_gpu_irq_evict(kbdev, i, completion_code);

				/* Some jobs that encounter a BUS FAULT may
				 * result in corrupted state causing future
				 * jobs to hang. Reset GPU before allowing
				 * any other jobs on the slot to continue.
				 */
				if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TTRX_3076)) {
					if (completion_code == BASE_JD_EVENT_JOB_BUS_FAULT) {
						if (kbase_prepare_to_reset_gpu_locked(
							    kbdev,
							    RESET_FLAGS_NONE))
							kbase_reset_gpu_locked(kbdev);
					}
				}
			}

			kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR),
					done & ((1 << i) | (1 << (i + 16))));
			active = kbase_reg_read(kbdev,
					JOB_CONTROL_REG(JOB_IRQ_JS_STATE));

			if (((active >> i) & 1) == 0 &&
					(((done >> (i + 16)) & 1) == 0)) {
				/* There is a potential race we must work
				 * around:
				 *
				 *  1. A job slot has a job in both current and
				 *     next registers
				 *  2. The job in current completes
				 *     successfully, the IRQ handler reads
				 *     RAWSTAT and calls this function with the
				 *     relevant bit set in "done"
				 *  3. The job in the next registers becomes the
				 *     current job on the GPU
				 *  4. Sometime before the JOB_IRQ_CLEAR line
				 *     above the job on the GPU _fails_
				 *  5. The IRQ_CLEAR clears the done bit but not
				 *     the failed bit. This atomically sets
				 *     JOB_IRQ_JS_STATE. However since both jobs
				 *     have now completed the relevant bits for
				 *     the slot are set to 0.
				 *
				 * If we now did nothing then we'd incorrectly
				 * assume that _both_ jobs had completed
				 * successfully (since we haven't yet observed
				 * the fail bit being set in RAWSTAT).
				 *
				 * So at this point if there are no active jobs
				 * left we check to see if RAWSTAT has a failure
				 * bit set for the job slot. If it does we know
				 * that there has been a new failure that we
				 * didn't previously know about, so we make sure
				 * that we record this in active (but we wait
				 * for the next loop to deal with it).
				 *
				 * If we were handling a job failure (i.e. done
				 * has the relevant high bit set) then we know
				 * that the value read back from
				 * JOB_IRQ_JS_STATE is the correct number of
				 * remaining jobs because the failed job will
				 * have prevented any futher jobs from starting
				 * execution.
				 */
				u32 rawstat = kbase_reg_read(kbdev,
					JOB_CONTROL_REG(JOB_IRQ_RAWSTAT));

				if ((rawstat >> (i + 16)) & 1) {
					/* There is a failed job that we've
					 * missed - add it back to active
					 */
					active |= (1u << i);
				}
			}

			dev_dbg(kbdev->dev, "Job ended with status 0x%08X\n",
							completion_code);

			nr_done = kbase_backend_nr_atoms_submitted(kbdev, i);
			nr_done -= (active >> i) & 1;
			nr_done -= (active >> (i + 16)) & 1;

			if (nr_done <= 0) {
				dev_warn(kbdev->dev, "Spurious interrupt on slot %d",
									i);

				goto spurious;
			}

			count += nr_done;

			while (nr_done) {
				if (nr_done == 1) {
					kbase_gpu_complete_hw(kbdev, i,
								completion_code,
								job_tail,
								&end_timestamp);
					kbase_jm_try_kick_all(kbdev);
				} else {
					/* More than one job has completed.
					 * Since this is not the last job being
					 * reported this time it must have
					 * passed. This is because the hardware
					 * will not allow further jobs in a job
					 * slot to complete until the failed job
					 * is cleared from the IRQ status.
					 */
					kbase_gpu_complete_hw(kbdev, i,
							BASE_JD_EVENT_DONE,
							0,
							&end_timestamp);
				}
				nr_done--;
			}
 spurious:
			done = kbase_reg_read(kbdev,
					JOB_CONTROL_REG(JOB_IRQ_RAWSTAT));

			failed = done >> 16;
			finished = (done & 0xFFFF) | failed;
			if (done)
				end_timestamp = ktime_get_raw();
		} while (finished & (1 << i));

		kbasep_job_slot_update_head_start_timestamp(kbdev, i,
								end_timestamp);
	}

	if (atomic_read(&kbdev->hwaccess.backend.reset_gpu) ==
						KBASE_RESET_GPU_COMMITTED) {
		/* If we're trying to reset the GPU then we might be able to do
		 * it early (without waiting for a timeout) because some jobs
		 * have completed
		 */
		kbasep_try_reset_gpu_early_locked(kbdev);
	}
	KBASE_KTRACE_ADD_JM(kbdev, JM_IRQ_END, NULL, NULL, 0, count);
}

void kbasep_job_slot_soft_or_hard_stop_do_action(struct kbase_device *kbdev,
					int js,
					u32 action,
					base_jd_core_req core_reqs,
					struct kbase_jd_atom *target_katom)
{
#if KBASE_KTRACE_ENABLE
	u32 status_reg_before;
	u64 job_in_head_before;
	u32 status_reg_after;

	KBASE_DEBUG_ASSERT(!(action & (~JS_COMMAND_MASK)));

	/* Check the head pointer */
	job_in_head_before = ((u64) kbase_reg_read(kbdev,
					JOB_SLOT_REG(js, JS_HEAD_LO)))
			| (((u64) kbase_reg_read(kbdev,
					JOB_SLOT_REG(js, JS_HEAD_HI)))
									<< 32);
	status_reg_before = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JS_STATUS));
#endif

	if (action == JS_COMMAND_SOFT_STOP) {
		if (kbase_jd_katom_is_protected(target_katom)) {
#ifdef CONFIG_MALI_BIFROST_DEBUG
			dev_dbg(kbdev->dev,
					"Attempt made to soft-stop a job that cannot be soft-stopped. core_reqs = 0x%x",
					(unsigned int)core_reqs);
#endif				/* CONFIG_MALI_BIFROST_DEBUG */
			return;
		}

		/* We are about to issue a soft stop, so mark the atom as having
		 * been soft stopped
		 */
		target_katom->atom_flags |= KBASE_KATOM_FLAG_BEEN_SOFT_STOPPED;

		/* Mark the point where we issue the soft-stop command */
		KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTSTOP_ISSUE(kbdev, target_katom);

		action = (target_katom->atom_flags &
			  KBASE_KATOM_FLAGS_JOBCHAIN) ?
				 JS_COMMAND_SOFT_STOP_1 :
				 JS_COMMAND_SOFT_STOP_0;
	} else if (action == JS_COMMAND_HARD_STOP) {
		target_katom->atom_flags |= KBASE_KATOM_FLAG_BEEN_HARD_STOPPED;

		action = (target_katom->atom_flags &
			  KBASE_KATOM_FLAGS_JOBCHAIN) ?
				 JS_COMMAND_HARD_STOP_1 :
				 JS_COMMAND_HARD_STOP_0;
	}

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JS_COMMAND), action);

#if KBASE_KTRACE_ENABLE
	status_reg_after = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JS_STATUS));
	if (status_reg_after == BASE_JD_EVENT_ACTIVE) {
		struct kbase_jd_atom *head;
		struct kbase_context *head_kctx;

		head = kbase_gpu_inspect(kbdev, js, 0);
		head_kctx = head->kctx;

		if (status_reg_before == BASE_JD_EVENT_ACTIVE)
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_CHECK_HEAD, head_kctx, head, job_in_head_before, js);
		else
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js);

		switch (action) {
		case JS_COMMAND_SOFT_STOP:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_SOFTSTOP, head_kctx, head, head->jc, js);
			break;
		case JS_COMMAND_SOFT_STOP_0:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_SOFTSTOP_0, head_kctx, head, head->jc, js);
			break;
		case JS_COMMAND_SOFT_STOP_1:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_SOFTSTOP_1, head_kctx, head, head->jc, js);
			break;
		case JS_COMMAND_HARD_STOP:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_HARDSTOP, head_kctx, head, head->jc, js);
			break;
		case JS_COMMAND_HARD_STOP_0:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_HARDSTOP_0, head_kctx, head, head->jc, js);
			break;
		case JS_COMMAND_HARD_STOP_1:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_HARDSTOP_1, head_kctx, head, head->jc, js);
			break;
		default:
			BUG();
			break;
		}
	} else {
		if (status_reg_before == BASE_JD_EVENT_ACTIVE)
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_CHECK_HEAD, NULL, NULL, job_in_head_before, js);
		else
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js);

		switch (action) {
		case JS_COMMAND_SOFT_STOP:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_SOFTSTOP, NULL, NULL, 0, js);
			break;
		case JS_COMMAND_SOFT_STOP_0:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_SOFTSTOP_0, NULL, NULL, 0, js);
			break;
		case JS_COMMAND_SOFT_STOP_1:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_SOFTSTOP_1, NULL, NULL, 0, js);
			break;
		case JS_COMMAND_HARD_STOP:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_HARDSTOP, NULL, NULL, 0, js);
			break;
		case JS_COMMAND_HARD_STOP_0:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_HARDSTOP_0, NULL, NULL, 0, js);
			break;
		case JS_COMMAND_HARD_STOP_1:
			KBASE_KTRACE_ADD_JM_SLOT(kbdev, JM_HARDSTOP_1, NULL, NULL, 0, js);
			break;
		default:
			BUG();
			break;
		}
	}
#endif
}

void kbase_backend_jm_kill_running_jobs_from_kctx(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		kbase_job_slot_hardstop(kctx, i, NULL);
}

void kbase_job_slot_ctx_priority_check_locked(struct kbase_context *kctx,
				struct kbase_jd_atom *target_katom)
{
	struct kbase_device *kbdev;
	int target_js = target_katom->slot_nr;
	int i;
	bool stop_sent = false;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = 0; i < kbase_backend_nr_atoms_on_slot(kbdev, target_js); i++) {
		struct kbase_jd_atom *slot_katom;

		slot_katom = kbase_gpu_inspect(kbdev, target_js, i);
		if (!slot_katom)
			continue;

		if (kbase_js_atom_runs_before(kbdev, target_katom, slot_katom,
					      KBASE_ATOM_ORDERING_FLAG_SEQNR)) {
			if (!stop_sent)
				KBASE_TLSTREAM_TL_ATTRIB_ATOM_PRIORITIZED(
						kbdev,
						target_katom);

			kbase_job_slot_softstop(kbdev, target_js, slot_katom);
			stop_sent = true;
		}
	}
}

static int softstop_start_rp_nolock(
	struct kbase_context *kctx, struct kbase_va_region *reg)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_jd_atom *katom;
	struct kbase_jd_renderpass *rp;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	katom = kbase_gpu_inspect(kbdev, 1, 0);

	if (!katom) {
		dev_dbg(kctx->kbdev->dev, "No atom on job slot\n");
		return -ESRCH;
	}

	if (!(katom->core_req & BASE_JD_REQ_START_RENDERPASS)) {
		dev_dbg(kctx->kbdev->dev,
			"Atom %pK on job slot is not start RP\n", (void *)katom);
		return -EPERM;
	}

	compiletime_assert((1ull << (sizeof(katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[katom->renderpass_id];
	if (WARN_ON(rp->state != KBASE_JD_RP_START &&
		rp->state != KBASE_JD_RP_RETRY))
		return -EINVAL;

	dev_dbg(kctx->kbdev->dev, "OOM in state %d with region %pK\n",
		(int)rp->state, (void *)reg);

	if (WARN_ON(katom != rp->start_katom))
		return -EINVAL;

	dev_dbg(kctx->kbdev->dev, "Adding region %pK to list %pK\n",
		(void *)reg, (void *)&rp->oom_reg_list);
	list_move_tail(&reg->link, &rp->oom_reg_list);
	dev_dbg(kctx->kbdev->dev, "Added region to list\n");

	rp->state = (rp->state == KBASE_JD_RP_START ?
		KBASE_JD_RP_PEND_OOM : KBASE_JD_RP_RETRY_PEND_OOM);

	kbase_job_slot_softstop(kbdev, 1, katom);

	return 0;
}

int kbase_job_slot_softstop_start_rp(struct kbase_context *const kctx,
		struct kbase_va_region *const reg)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	err = softstop_start_rp_nolock(kctx, reg);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return err;
}

void kbase_jm_wait_for_zero_jobs(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned long timeout = msecs_to_jiffies(ZAP_TIMEOUT);

	timeout = wait_event_timeout(kctx->jctx.zero_jobs_wait,
			kctx->jctx.job_nr == 0, timeout);

	if (timeout != 0)
		timeout = wait_event_timeout(
			kctx->jctx.sched_info.ctx.is_scheduled_wait,
			!kbase_ctx_flag(kctx, KCTX_SCHEDULED),
			timeout);

	/* Neither wait timed out; all done! */
	if (timeout != 0)
		goto exit;

	if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)) {
		dev_err(kbdev->dev,
			"Issuing GPU soft-reset because jobs failed to be killed (within %d ms) as part of context termination (e.g. process exit)\n",
			ZAP_TIMEOUT);
		kbase_reset_gpu(kbdev);
	}

	/* Wait for the reset to complete */
	kbase_reset_gpu_wait(kbdev);
exit:
	dev_dbg(kbdev->dev, "Zap: Finished Context %pK", kctx);

	/* Ensure that the signallers of the waitqs have finished */
	mutex_lock(&kctx->jctx.lock);
	mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kctx->jctx.lock);
}

u32 kbase_backend_get_current_flush_id(struct kbase_device *kbdev)
{
	u32 flush_id = 0;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_FLUSH_REDUCTION)) {
		mutex_lock(&kbdev->pm.lock);
		if (kbdev->pm.backend.gpu_powered)
			flush_id = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(LATEST_FLUSH));
		mutex_unlock(&kbdev->pm.lock);
	}

	return flush_id;
}

int kbase_job_slot_init(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
	return 0;
}
KBASE_EXPORT_TEST_API(kbase_job_slot_init);

void kbase_job_slot_halt(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_job_slot_term(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_job_slot_term);


/**
 * kbase_job_slot_softstop_swflags - Soft-stop a job with flags
 * @kbdev:         The kbase device
 * @js:            The job slot to soft-stop
 * @target_katom:  The job that should be soft-stopped (or NULL for any job)
 * @sw_flags:      Flags to pass in about the soft-stop
 *
 * Context:
 *   The job slot lock must be held when calling this function.
 *   The job slot must not already be in the process of being soft-stopped.
 *
 * Soft-stop the specified job slot, with extra information about the stop
 *
 * Where possible any job in the next register is evicted before the soft-stop.
 */
void kbase_job_slot_softstop_swflags(struct kbase_device *kbdev, int js,
			struct kbase_jd_atom *target_katom, u32 sw_flags)
{
	dev_dbg(kbdev->dev, "Soft-stop atom %pK with flags 0x%x (s:%d)\n",
		target_katom, sw_flags, js);

	KBASE_DEBUG_ASSERT(!(sw_flags & JS_COMMAND_MASK));
	kbase_backend_soft_hard_stop_slot(kbdev, NULL, js, target_katom,
			JS_COMMAND_SOFT_STOP | sw_flags);
}

void kbase_job_slot_softstop(struct kbase_device *kbdev, int js,
				struct kbase_jd_atom *target_katom)
{
	kbase_job_slot_softstop_swflags(kbdev, js, target_katom, 0u);
}

void kbase_job_slot_hardstop(struct kbase_context *kctx, int js,
				struct kbase_jd_atom *target_katom)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool stopped;

	stopped = kbase_backend_soft_hard_stop_slot(kbdev, kctx, js,
							target_katom,
							JS_COMMAND_HARD_STOP);
	CSTD_UNUSED(stopped);
}

void kbase_job_check_enter_disjoint(struct kbase_device *kbdev, u32 action,
		base_jd_core_req core_reqs, struct kbase_jd_atom *target_katom)
{
	u32 hw_action = action & JS_COMMAND_MASK;

	/* For soft-stop, don't enter if soft-stop not allowed, or isn't
	 * causing disjoint.
	 */
	if (hw_action == JS_COMMAND_SOFT_STOP &&
			(kbase_jd_katom_is_protected(target_katom) ||
			(0 == (action & JS_COMMAND_SW_CAUSES_DISJOINT))))
		return;

	/* Nothing to do if already logged disjoint state on this atom */
	if (target_katom->atom_flags & KBASE_KATOM_FLAG_IN_DISJOINT)
		return;

	target_katom->atom_flags |= KBASE_KATOM_FLAG_IN_DISJOINT;
	kbase_disjoint_state_up(kbdev);
}

void kbase_job_check_leave_disjoint(struct kbase_device *kbdev,
		struct kbase_jd_atom *target_katom)
{
	if (target_katom->atom_flags & KBASE_KATOM_FLAG_IN_DISJOINT) {
		target_katom->atom_flags &= ~KBASE_KATOM_FLAG_IN_DISJOINT;
		kbase_disjoint_state_down(kbdev);
	}
}

int kbase_reset_gpu_prevent_and_wait(struct kbase_device *kbdev)
{
	WARN(true, "%s Not implemented for JM GPUs", __func__);
	return -EINVAL;
}

int kbase_reset_gpu_try_prevent(struct kbase_device *kbdev)
{
	WARN(true, "%s Not implemented for JM GPUs", __func__);
	return -EINVAL;
}

void kbase_reset_gpu_allow(struct kbase_device *kbdev)
{
	WARN(true, "%s Not implemented for JM GPUs", __func__);
}

void kbase_reset_gpu_assert_prevented(struct kbase_device *kbdev)
{
	WARN(true, "%s Not implemented for JM GPUs", __func__);
}

void kbase_reset_gpu_assert_failed_or_prevented(struct kbase_device *kbdev)
{
	WARN(true, "%s Not implemented for JM GPUs", __func__);
}

static void kbase_debug_dump_registers(struct kbase_device *kbdev)
{
	int i;

	kbase_io_history_dump(kbdev);

	dev_err(kbdev->dev, "Register state:");
	dev_err(kbdev->dev, "  GPU_IRQ_RAWSTAT=0x%08x GPU_STATUS=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS)));
	dev_err(kbdev->dev, "  JOB_IRQ_RAWSTAT=0x%08x JOB_IRQ_JS_STATE=0x%08x",
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT)),
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_JS_STATE)));
	for (i = 0; i < 3; i++) {
		dev_err(kbdev->dev, "  JS%d_STATUS=0x%08x      JS%d_HEAD_LO=0x%08x",
			i, kbase_reg_read(kbdev, JOB_SLOT_REG(i, JS_STATUS)),
			i, kbase_reg_read(kbdev, JOB_SLOT_REG(i, JS_HEAD_LO)));
	}
	dev_err(kbdev->dev, "  MMU_IRQ_RAWSTAT=0x%08x GPU_FAULTSTATUS=0x%08x",
		kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_RAWSTAT)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS)));
	dev_err(kbdev->dev, "  GPU_IRQ_MASK=0x%08x    JOB_IRQ_MASK=0x%08x     MMU_IRQ_MASK=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK)),
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK)),
		kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK)));
	dev_err(kbdev->dev, "  PWR_OVERRIDE0=0x%08x   PWR_OVERRIDE1=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE0)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE1)));
	dev_err(kbdev->dev, "  SHADER_CONFIG=0x%08x   L2_MMU_CONFIG=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(SHADER_CONFIG)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(L2_MMU_CONFIG)));
	dev_err(kbdev->dev, "  TILER_CONFIG=0x%08x    JM_CONFIG=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(TILER_CONFIG)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(JM_CONFIG)));
}

static void kbasep_reset_timeout_worker(struct work_struct *data)
{
	unsigned long flags;
	struct kbase_device *kbdev;
	ktime_t end_timestamp = ktime_get_raw();
	struct kbasep_js_device_data *js_devdata;
	bool silent = false;
	u32 max_loops = KBASE_CLEAN_CACHE_MAX_LOOPS;

	KBASE_DEBUG_ASSERT(data);

	kbdev = container_of(data, struct kbase_device,
						hwaccess.backend.reset_work);

	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	if (atomic_read(&kbdev->hwaccess.backend.reset_gpu) ==
			KBASE_RESET_GPU_SILENT)
		silent = true;

	KBASE_KTRACE_ADD_JM(kbdev, JM_BEGIN_RESET_WORKER, NULL, NULL, 0u, 0);

	/* Disable GPU hardware counters.
	 * This call will block until counters are disabled.
	 */
	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	/* Make sure the timer has completed - this cannot be done from
	 * interrupt context, so this cannot be done within
	 * kbasep_try_reset_gpu_early.
	 */
	hrtimer_cancel(&kbdev->hwaccess.backend.reset_timer);

	if (kbase_pm_context_active_handle_suspend(kbdev,
				KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		/* This would re-activate the GPU. Since it's already idle,
		 * there's no need to reset it
		 */
		atomic_set(&kbdev->hwaccess.backend.reset_gpu,
						KBASE_RESET_GPU_NOT_PENDING);
		kbase_disjoint_state_down(kbdev);
		wake_up(&kbdev->hwaccess.backend.reset_wait);
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return;
	}

	KBASE_DEBUG_ASSERT(kbdev->irq_reset_flush == false);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	spin_lock(&kbdev->mmu_mask_change);
	kbase_pm_reset_start_locked(kbdev);

	/* We're about to flush out the IRQs and their bottom half's */
	kbdev->irq_reset_flush = true;

	/* Disable IRQ to avoid IRQ handlers to kick in after releasing the
	 * spinlock; this also clears any outstanding interrupts
	 */
	kbase_pm_disable_interrupts_nolock(kbdev);

	spin_unlock(&kbdev->mmu_mask_change);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Ensure that any IRQ handlers have finished
	 * Must be done without any locks IRQ handlers will take
	 */
	kbase_synchronize_irqs(kbdev);

	/* Flush out any in-flight work items */
	kbase_flush_mmu_wqs(kbdev);

	/* The flush has completed so reset the active indicator */
	kbdev->irq_reset_flush = false;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TMIX_8463)) {
		/* Ensure that L2 is not transitioning when we send the reset
		 * command
		 */
		while (--max_loops && kbase_pm_get_trans_cores(kbdev,
				KBASE_PM_CORE_L2))
			;

		WARN(!max_loops, "L2 power transition timed out while trying to reset\n");
	}

	mutex_lock(&kbdev->pm.lock);
	/* We hold the pm lock, so there ought to be a current policy */
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.pm_current_policy);

	/* All slot have been soft-stopped and we've waited
	 * SOFT_STOP_RESET_TIMEOUT for the slots to clear, at this point we
	 * assume that anything that is still left on the GPU is stuck there and
	 * we'll kill it when we reset the GPU
	 */

	if (!silent)
		dev_err(kbdev->dev, "Resetting GPU (allowing up to %d ms)",
								RESET_TIMEOUT);

	/* Output the state of some interesting registers to help in the
	 * debugging of GPU resets
	 */
	if (!silent)
		kbase_debug_dump_registers(kbdev);

	/* Complete any jobs that were still on the GPU */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->protected_mode = false;
	if (!kbdev->pm.backend.protected_entry_transition_override)
		kbase_backend_reset(kbdev, &end_timestamp);
	kbase_pm_metrics_update(kbdev, NULL);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Tell hardware counters a reset is about to occur.
	 * If the instr backend is in an unrecoverable error state (e.g. due to
	 * HW being unresponsive), this will transition the backend out of
	 * it, on the assumption a reset will fix whatever problem there was.
	 */
	kbase_instr_hwcnt_on_before_reset(kbdev);

	/* Reset the GPU */
	kbase_pm_init_hw(kbdev, 0);

	mutex_unlock(&kbdev->pm.lock);

	mutex_lock(&js_devdata->runpool_mutex);

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_ctx_sched_restore_all_as(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_pm_enable_interrupts(kbdev);

	kbase_disjoint_state_down(kbdev);

	mutex_unlock(&js_devdata->runpool_mutex);

	mutex_lock(&kbdev->pm.lock);

	kbase_pm_reset_complete(kbdev);

	/* Find out what cores are required now */
	kbase_pm_update_cores_state(kbdev);

	/* Synchronously request and wait for those cores, because if
	 * instrumentation is enabled it would need them immediately.
	 */
	kbase_pm_wait_for_desired_state(kbdev);

	mutex_unlock(&kbdev->pm.lock);

	atomic_set(&kbdev->hwaccess.backend.reset_gpu,
						KBASE_RESET_GPU_NOT_PENDING);

	wake_up(&kbdev->hwaccess.backend.reset_wait);
	if (!silent)
		dev_err(kbdev->dev, "Reset complete");

	/* Try submitting some jobs to restart processing */
	KBASE_KTRACE_ADD_JM(kbdev, JM_SUBMIT_AFTER_RESET, NULL, NULL, 0u, 0);
	kbase_js_sched_all(kbdev);

	/* Process any pending slot updates */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_backend_slot_update(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	kbase_pm_context_idle(kbdev);

	/* Re-enable GPU hardware counters */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	KBASE_KTRACE_ADD_JM(kbdev, JM_END_RESET_WORKER, NULL, NULL, 0u, 0);
}

static enum hrtimer_restart kbasep_reset_timer_callback(struct hrtimer *timer)
{
	struct kbase_device *kbdev = container_of(timer, struct kbase_device,
						hwaccess.backend.reset_timer);

	KBASE_DEBUG_ASSERT(kbdev);

	/* Reset still pending? */
	if (atomic_cmpxchg(&kbdev->hwaccess.backend.reset_gpu,
			KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) ==
						KBASE_RESET_GPU_COMMITTED)
		queue_work(kbdev->hwaccess.backend.reset_workq,
					&kbdev->hwaccess.backend.reset_work);

	return HRTIMER_NORESTART;
}

/*
 * If all jobs are evicted from the GPU then we can reset the GPU
 * immediately instead of waiting for the timeout to elapse
 */

static void kbasep_try_reset_gpu_early_locked(struct kbase_device *kbdev)
{
	int i;
	int pending_jobs = 0;

	KBASE_DEBUG_ASSERT(kbdev);

	/* Count the number of jobs */
	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		pending_jobs += kbase_backend_nr_atoms_submitted(kbdev, i);

	if (pending_jobs > 0) {
		/* There are still jobs on the GPU - wait */
		return;
	}

	/* To prevent getting incorrect registers when dumping failed job,
	 * skip early reset.
	 */
	if (atomic_read(&kbdev->job_fault_debug) > 0)
		return;

	/* Check that the reset has been committed to (i.e. kbase_reset_gpu has
	 * been called), and that no other thread beat this thread to starting
	 * the reset
	 */
	if (atomic_cmpxchg(&kbdev->hwaccess.backend.reset_gpu,
			KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) !=
						KBASE_RESET_GPU_COMMITTED) {
		/* Reset has already occurred */
		return;
	}

	queue_work(kbdev->hwaccess.backend.reset_workq,
					&kbdev->hwaccess.backend.reset_work);
}

static void kbasep_try_reset_gpu_early(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbasep_try_reset_gpu_early_locked(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

/**
 * kbase_prepare_to_reset_gpu_locked - Prepare for resetting the GPU
 * @kbdev: kbase device
 * @flags: Bitfield indicating impact of reset (see flag defines)
 *
 * This function soft-stops all the slots to ensure that as many jobs as
 * possible are saved.
 *
 * Return: boolean which should be interpreted as follows:
 *   true - Prepared for reset, kbase_reset_gpu_locked should be called.
 *   false - Another thread is performing a reset, kbase_reset_gpu should
 *   not be called.
 */
bool kbase_prepare_to_reset_gpu_locked(struct kbase_device *kbdev,
				       unsigned int flags)
{
	int i;

	KBASE_DEBUG_ASSERT(kbdev);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_is_gpu_lost(kbdev)) {
		/* GPU access has been removed, reset will be done by
		 * Arbiter instead
		 */
		return false;
	}
#endif

	if (flags & RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)
		kbase_instr_hwcnt_on_unrecoverable_error(kbdev);

	if (atomic_cmpxchg(&kbdev->hwaccess.backend.reset_gpu,
						KBASE_RESET_GPU_NOT_PENDING,
						KBASE_RESET_GPU_PREPARED) !=
						KBASE_RESET_GPU_NOT_PENDING) {
		/* Some other thread is already resetting the GPU */
		return false;
	}

	kbase_disjoint_state_up(kbdev);

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		kbase_job_slot_softstop(kbdev, i, NULL);

	return true;
}

bool kbase_prepare_to_reset_gpu(struct kbase_device *kbdev, unsigned int flags)
{
	unsigned long lock_flags;
	bool ret;

	spin_lock_irqsave(&kbdev->hwaccess_lock, lock_flags);
	ret = kbase_prepare_to_reset_gpu_locked(kbdev, flags);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, lock_flags);

	return ret;
}
KBASE_EXPORT_TEST_API(kbase_prepare_to_reset_gpu);

/*
 * This function should be called after kbase_prepare_to_reset_gpu if it
 * returns true. It should never be called without a corresponding call to
 * kbase_prepare_to_reset_gpu.
 *
 * After this function is called (or not called if kbase_prepare_to_reset_gpu
 * returned false), the caller should wait for
 * kbdev->hwaccess.backend.reset_waitq to be signalled to know when the reset
 * has completed.
 */
void kbase_reset_gpu(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software issue for
	 * a race to be occurring here
	 */
	KBASE_DEBUG_ASSERT(atomic_read(&kbdev->hwaccess.backend.reset_gpu) ==
						KBASE_RESET_GPU_PREPARED);
	atomic_set(&kbdev->hwaccess.backend.reset_gpu,
						KBASE_RESET_GPU_COMMITTED);

	dev_err(kbdev->dev, "Preparing to soft-reset GPU: Waiting (upto %d ms) for all jobs to complete soft-stop\n",
			kbdev->reset_timeout_ms);

	hrtimer_start(&kbdev->hwaccess.backend.reset_timer,
			HR_TIMER_DELAY_MSEC(kbdev->reset_timeout_ms),
			HRTIMER_MODE_REL);

	/* Try resetting early */
	kbasep_try_reset_gpu_early(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu);

void kbase_reset_gpu_locked(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software issue for
	 * a race to be occurring here
	 */
	KBASE_DEBUG_ASSERT(atomic_read(&kbdev->hwaccess.backend.reset_gpu) ==
						KBASE_RESET_GPU_PREPARED);
	atomic_set(&kbdev->hwaccess.backend.reset_gpu,
						KBASE_RESET_GPU_COMMITTED);

	dev_err(kbdev->dev, "Preparing to soft-reset GPU: Waiting (upto %d ms) for all jobs to complete soft-stop\n",
			kbdev->reset_timeout_ms);
	hrtimer_start(&kbdev->hwaccess.backend.reset_timer,
			HR_TIMER_DELAY_MSEC(kbdev->reset_timeout_ms),
			HRTIMER_MODE_REL);

	/* Try resetting early */
	kbasep_try_reset_gpu_early_locked(kbdev);
}

int kbase_reset_gpu_silent(struct kbase_device *kbdev)
{
	if (atomic_cmpxchg(&kbdev->hwaccess.backend.reset_gpu,
						KBASE_RESET_GPU_NOT_PENDING,
						KBASE_RESET_GPU_SILENT) !=
						KBASE_RESET_GPU_NOT_PENDING) {
		/* Some other thread is already resetting the GPU */
		return -EAGAIN;
	}

	kbase_disjoint_state_up(kbdev);

	queue_work(kbdev->hwaccess.backend.reset_workq,
			&kbdev->hwaccess.backend.reset_work);

	return 0;
}

bool kbase_reset_gpu_is_active(struct kbase_device *kbdev)
{
	if (atomic_read(&kbdev->hwaccess.backend.reset_gpu) ==
			KBASE_RESET_GPU_NOT_PENDING)
		return false;

	return true;
}

int kbase_reset_gpu_wait(struct kbase_device *kbdev)
{
	wait_event(kbdev->hwaccess.backend.reset_wait,
			atomic_read(&kbdev->hwaccess.backend.reset_gpu)
			== KBASE_RESET_GPU_NOT_PENDING);

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu_wait);

int kbase_reset_gpu_init(struct kbase_device *kbdev)
{
	kbdev->hwaccess.backend.reset_workq = alloc_workqueue(
						"Mali reset workqueue", 0, 1);
	if (kbdev->hwaccess.backend.reset_workq == NULL)
		return -ENOMEM;

	INIT_WORK(&kbdev->hwaccess.backend.reset_work,
						kbasep_reset_timeout_worker);

	hrtimer_init(&kbdev->hwaccess.backend.reset_timer, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	kbdev->hwaccess.backend.reset_timer.function =
						kbasep_reset_timer_callback;

	return 0;
}

void kbase_reset_gpu_term(struct kbase_device *kbdev)
{
	destroy_workqueue(kbdev->hwaccess.backend.reset_workq);
}

static u64 kbasep_apply_limited_core_mask(const struct kbase_device *kbdev,
				const u64 affinity, const u64 limited_core_mask)
{
	const u64 result = affinity & limited_core_mask;

#ifdef CONFIG_MALI_BIFROST_DEBUG
	dev_dbg(kbdev->dev,
				"Limiting affinity due to BASE_JD_REQ_LIMITED_CORE_MASK from 0x%lx to 0x%lx (mask is 0x%lx)\n",
				(unsigned long)affinity,
				(unsigned long)result,
				(unsigned long)limited_core_mask);
#else
	CSTD_UNUSED(kbdev);
#endif

	return result;
}
