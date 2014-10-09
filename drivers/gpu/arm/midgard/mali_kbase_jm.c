/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_jm.c
 * Base kernel job manager APIs
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_js_affinity.h>
#include <mali_kbase_hw.h>

#include "mali_kbase_jm.h"

#define beenthere(kctx, f, a...)  dev_dbg(kctx->kbdev->dev, "%s:" f, __func__, ##a)

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
u64 mali_js0_affinity_mask = 0xFFFFFFFFFFFFFFFFULL;
u64 mali_js1_affinity_mask = 0xFFFFFFFFFFFFFFFFULL;
u64 mali_js2_affinity_mask = 0xFFFFFFFFFFFFFFFFULL;
#endif


static void kbasep_try_reset_gpu_early(kbase_device *kbdev);

#ifdef CONFIG_GPU_TRACEPOINTS
static char *kbasep_make_job_slot_string(int js, char *js_string)
{
	sprintf(js_string, "job_slot_%i", js);
	return js_string;
}
#endif

static void kbase_job_hw_submit(kbase_device *kbdev, kbase_jd_atom *katom, int js)
{
	kbase_context *kctx;
	u32 cfg;
	u64 jc_head = katom->jc;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(katom);

	kctx = katom->kctx;

	/* Command register must be available */
	KBASE_DEBUG_ASSERT(kbasep_jm_is_js_free(kbdev, js, kctx));
	/* Affinity is not violating */
	kbase_js_debug_log_current_affinities(kbdev);
	KBASE_DEBUG_ASSERT(!kbase_js_affinity_would_violate(kbdev, js, katom->affinity));

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), jc_head & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), jc_head >> 32, kctx);

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	{
		u64 mask;
		u32 value;

		if( 0 == js )
		{
			mask = mali_js0_affinity_mask;
		}
		else if( 1 == js )
		{
			mask = mali_js1_affinity_mask;
		}
		else
		{
			mask = mali_js2_affinity_mask;
		}

		value = katom->affinity & (mask & 0xFFFFFFFF);

		kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_LO), value, kctx);

		value = (katom->affinity >> 32) & ((mask>>32) & 0xFFFFFFFF);
		kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_HI), value, kctx);
	}
#else
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_LO), katom->affinity & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_HI), katom->affinity >> 32, kctx);
#endif

	/* start MMU, medium priority, cache clean/flush on end, clean/flush on start */
	cfg = kctx->as_nr | JSn_CONFIG_END_FLUSH_CLEAN_INVALIDATE | JSn_CONFIG_START_MMU | JSn_CONFIG_START_FLUSH_CLEAN_INVALIDATE | JSn_CONFIG_THREAD_PRI(8);

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_JOBCHAIN_DISAMBIGUATION)) {
		if (!kbdev->jm_slots[js].job_chain_flag) {
			cfg |= JSn_CONFIG_JOB_CHAIN_FLAG;
			katom->atom_flags |= KBASE_KATOM_FLAGS_JOBCHAIN;
			kbdev->jm_slots[js].job_chain_flag = MALI_TRUE;
		} else {
			katom->atom_flags &= ~KBASE_KATOM_FLAGS_JOBCHAIN;
			kbdev->jm_slots[js].job_chain_flag = MALI_FALSE;
		}
	}

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_CONFIG_NEXT), cfg, kctx);

	/* Write an approximate start timestamp.
	 * It's approximate because there might be a job in the HEAD register. In
	 * such cases, we'll try to make a better approximation in the IRQ handler
	 * (up to the KBASE_JS_IRQ_THROTTLE_TIME_US). */
	katom->start_timestamp = ktime_get();

	/* GO ! */
	dev_dbg(kbdev->dev, "JS: Submitting atom %p from ctx %p to js[%d] with head=0x%llx, affinity=0x%llx", katom, kctx, js, jc_head, katom->affinity);

	KBASE_TRACE_ADD_SLOT_INFO(kbdev, JM_SUBMIT, kctx, katom, jc_head, js, (u32) katom->affinity);

#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_job_slots_event(GATOR_MAKE_EVENT(GATOR_JOB_SLOT_START, js), kctx, kbase_jd_atom_id(kctx, katom)); 
#endif				/* CONFIG_MALI_GATOR_SUPPORT */
#ifdef CONFIG_GPU_TRACEPOINTS
	if (kbasep_jm_nr_jobs_submitted(&kbdev->jm_slots[js]) == 1)
	{
		/* If this is the only job on the slot, trace it as starting */
		char js_string[16];
		trace_gpu_sched_switch(kbasep_make_job_slot_string(js, js_string), ktime_to_ns(katom->start_timestamp), (u32)katom->kctx, 0, katom->work_id);
		kbdev->jm_slots[js].last_context = katom->kctx;
	}
#endif
	kbase_timeline_job_slot_submit(kbdev, kctx, katom, js);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_START, katom->kctx);
}

void kbase_job_submit_nolock(kbase_device *kbdev, kbase_jd_atom *katom, int js)
{
	kbase_jm_slot *jm_slots;
	base_jd_core_req core_req;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(katom);

	jm_slots = kbdev->jm_slots;

	core_req = katom->core_req;
	if (core_req & BASE_JD_REQ_ONLY_COMPUTE) {
		unsigned long flags;
		int device_nr = (core_req & BASE_JD_REQ_SPECIFIC_COHERENT_GROUP) ? katom->device_nr : 0;
		KBASE_DEBUG_ASSERT(device_nr < 2);
		spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
		kbasep_pm_record_job_status(kbdev);
		kbdev->pm.metrics.active_cl_ctx[device_nr]++;
		spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
	} else {
		unsigned long flags;

		spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
		kbasep_pm_record_job_status(kbdev);
		kbdev->pm.metrics.active_gl_ctx++;
		spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
	}

	/*
	 * We can have:
	 * - one job already done (pending interrupt),
	 * - one running,
	 * - one ready to be run.
	 * Hence a maximum of 3 inflight jobs. We have a 4 job
	 * queue, which I hope will be enough...
	 */
	kbasep_jm_enqueue_submit_slot(&jm_slots[js], katom);
	kbase_job_hw_submit(kbdev, katom, js);
}

void kbase_job_done_slot(kbase_device *kbdev, int s, u32 completion_code, u64 job_tail, ktime_t *end_timestamp)
{
	kbase_jm_slot *slot;
	kbase_jd_atom *katom;
	mali_addr64 jc_head;
	kbase_context *kctx;

	KBASE_DEBUG_ASSERT(kbdev);

	if (completion_code != BASE_JD_EVENT_DONE && completion_code != BASE_JD_EVENT_STOPPED)
		dev_err(kbdev->dev, "t6xx: GPU fault 0x%02lx from job slot %d\n", (unsigned long)completion_code, s);

	/* IMPORTANT: this function must only contain work necessary to complete a
	 * job from a Real IRQ (and not 'fake' completion, e.g. from
	 * Soft-stop). For general work that must happen no matter how the job was
	 * removed from the hardware, place it in kbase_jd_done() */

	slot = &kbdev->jm_slots[s];
	katom = kbasep_jm_dequeue_submit_slot(slot);

	/* If the katom completed is because it's a dummy job for HW workarounds, then take no further action */
	if (kbasep_jm_is_dummy_workaround_job(kbdev, katom)) {
		KBASE_TRACE_ADD_SLOT_INFO(kbdev, JM_JOB_DONE, NULL, NULL, 0, s, completion_code);
		return;
	}

	jc_head = katom->jc;
	kctx = katom->kctx;

	KBASE_TRACE_ADD_SLOT_INFO(kbdev, JM_JOB_DONE, kctx, katom, jc_head, s, completion_code);

	if (completion_code != BASE_JD_EVENT_DONE && completion_code != BASE_JD_EVENT_STOPPED) {

#if KBASE_TRACE_DUMP_ON_JOB_SLOT_ERROR != 0
		KBASE_TRACE_DUMP(kbdev);
#endif
	}
	if (job_tail != 0) {
		mali_bool was_updated = (job_tail != jc_head);
		/* Some of the job has been executed, so we update the job chain address to where we should resume from */
		katom->jc = job_tail;
		if (was_updated)
			KBASE_TRACE_ADD_SLOT(kbdev, JM_UPDATE_HEAD, kctx, katom, job_tail, s);
	}

	/* Only update the event code for jobs that weren't cancelled */
	if (katom->event_code != BASE_JD_EVENT_JOB_CANCELLED)
		katom->event_code = (base_jd_event_code) completion_code;

	kbase_device_trace_register_access(kctx, REG_WRITE, JOB_CONTROL_REG(JOB_IRQ_CLEAR), 1 << s);

	/* Complete the job, and start new ones
	 *
	 * Also defer remaining work onto the workqueue:
	 * - Re-queue Soft-stopped jobs
	 * - For any other jobs, queue the job back into the dependency system
	 * - Schedule out the parent context if necessary, and schedule a new one in.
	 */
#ifdef CONFIG_GPU_TRACEPOINTS
	if (kbasep_jm_nr_jobs_submitted(slot) != 0) {
		kbase_jd_atom *katom;
		char js_string[16];
		katom = kbasep_jm_peek_idx_submit_slot(slot, 0);        /* The atom in the HEAD */
		trace_gpu_sched_switch(kbasep_make_job_slot_string(s, js_string), ktime_to_ns(*end_timestamp), (u32)katom->kctx, 0, katom->work_id);
		slot->last_context = katom->kctx;
	} else {
		char js_string[16];
		trace_gpu_sched_switch(kbasep_make_job_slot_string(s, js_string), ktime_to_ns(ktime_get()), 0, 0, 0);
		slot->last_context = 0;
	}
#endif
	kbase_jd_done(katom, s, end_timestamp, KBASE_JS_ATOM_DONE_START_NEW_ATOMS);
}

/**
 * Update the start_timestamp of the job currently in the HEAD, based on the
 * fact that we got an IRQ for the previous set of completed jobs.
 *
 * The estimate also takes into account the KBASE_JS_IRQ_THROTTLE_TIME_US and
 * the time the job was submitted, to work out the best estimate (which might
 * still result in an over-estimate to the calculated time spent)
 */
STATIC void kbasep_job_slot_update_head_start_timestamp(kbase_device *kbdev, kbase_jm_slot *slot, ktime_t end_timestamp)
{
	KBASE_DEBUG_ASSERT(slot);

	if (kbasep_jm_nr_jobs_submitted(slot) > 0) {
		kbase_jd_atom *katom;
		ktime_t new_timestamp;
		ktime_t timestamp_diff;
		katom = kbasep_jm_peek_idx_submit_slot(slot, 0);	/* The atom in the HEAD */

		KBASE_DEBUG_ASSERT(katom != NULL);

		if (kbasep_jm_is_dummy_workaround_job(kbdev, katom) != MALI_FALSE) {
			/* Don't access the members of HW workaround 'dummy' jobs */
			return;
		}

		/* Account for any IRQ Throttle time - makes an overestimate of the time spent by the job */
		new_timestamp = ktime_sub_ns(end_timestamp, KBASE_JS_IRQ_THROTTLE_TIME_US * 1000);
		timestamp_diff = ktime_sub(new_timestamp, katom->start_timestamp);
		if (ktime_to_ns(timestamp_diff) >= 0) {
			/* Only update the timestamp if it's a better estimate than what's currently stored.
			 * This is because our estimate that accounts for the throttle time may be too much
			 * of an overestimate */
			katom->start_timestamp = new_timestamp;
		}
	}
}

void kbase_job_done(kbase_device *kbdev, u32 done)
{
	unsigned long flags;
	int i;
	u32 count = 0;
	ktime_t end_timestamp = ktime_get();
	kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	KBASE_TRACE_ADD(kbdev, JM_IRQ, NULL, NULL, 0, done);

	memset(&kbdev->slot_submit_count_irq[0], 0, sizeof(kbdev->slot_submit_count_irq));

	/* write irq throttle register, this will prevent irqs from occurring until
	 * the given number of gpu clock cycles have passed */
	{
		int irq_throttle_cycles = atomic_read(&kbdev->irq_throttle_cycles);
		kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_THROTTLE), irq_throttle_cycles, NULL);
	}

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	while (done) {
		kbase_jm_slot *slot;
		u32 failed = done >> 16;

		/* treat failed slots as finished slots */
		u32 finished = (done & 0xFFFF) | failed;

		/* Note: This is inherently unfair, as we always check
		 * for lower numbered interrupts before the higher
		 * numbered ones.*/
		i = ffs(finished) - 1;
		KBASE_DEBUG_ASSERT(i >= 0);

		slot = &kbdev->jm_slots[i];

		do {
			int nr_done;
			u32 active;
			u32 completion_code = BASE_JD_EVENT_DONE;	/* assume OK */
			u64 job_tail = 0;

			if (failed & (1u << i)) {
				/* read out the job slot status code if the job slot reported failure */
				completion_code = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_STATUS), NULL);

				switch (completion_code) {
				case BASE_JD_EVENT_STOPPED:
#ifdef CONFIG_MALI_GATOR_SUPPORT
					kbase_trace_mali_job_slots_event(GATOR_MAKE_EVENT(GATOR_JOB_SLOT_SOFT_STOPPED, i), NULL, 0);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */
					/* Soft-stopped job - read the value of JS<n>_TAIL so that the job chain can be resumed */
					job_tail = (u64) kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_TAIL_LO), NULL) | ((u64) kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_TAIL_HI), NULL) << 32);
					break;
				case BASE_JD_EVENT_NOT_STARTED:
					/* PRLAM-10673 can cause a TERMINATED job to come back as NOT_STARTED, but the error interrupt helps us detect it */
					completion_code = BASE_JD_EVENT_TERMINATED;
					/* fall throught */
				default:
					dev_warn(kbdev->dev, "error detected from slot %d, job status 0x%08x (%s)", i, completion_code, kbase_exception_name(completion_code));
				kbdev->kbase_group_error++;
				}
			}

			kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), done & ((1 << i) | (1 << (i + 16))), NULL);
			active = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_JS_STATE), NULL);

			if (((active >> i) & 1) == 0 && (((done >> (i + 16)) & 1) == 0)) {
				/* There is a potential race we must work around:
				 *
				 *  1. A job slot has a job in both current and next registers
				 *  2. The job in current completes successfully, the IRQ handler reads RAWSTAT
				 *     and calls this function with the relevant bit set in "done"
				 *  3. The job in the next registers becomes the current job on the GPU
				 *  4. Sometime before the JOB_IRQ_CLEAR line above the job on the GPU _fails_
				 *  5. The IRQ_CLEAR clears the done bit but not the failed bit. This atomically sets
				 *     JOB_IRQ_JS_STATE. However since both jobs have now completed the relevant bits
				 *     for the slot are set to 0.
				 *
				 * If we now did nothing then we'd incorrectly assume that _both_ jobs had completed
				 * successfully (since we haven't yet observed the fail bit being set in RAWSTAT).
				 *
				 * So at this point if there are no active jobs left we check to see if RAWSTAT has a failure
				 * bit set for the job slot. If it does we know that there has been a new failure that we
				 * didn't previously know about, so we make sure that we record this in active (but we wait
				 * for the next loop to deal with it).
				 *
				 * If we were handling a job failure (i.e. done has the relevant high bit set) then we know that
				 * the value read back from JOB_IRQ_JS_STATE is the correct number of remaining jobs because
				 * the failed job will have prevented any futher jobs from starting execution.
				 */
				u32 rawstat = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT), NULL);

				if ((rawstat >> (i + 16)) & 1) {
					/* There is a failed job that we've missed - add it back to active */
					active |= (1u << i);
				}
			}

			dev_dbg(kbdev->dev, "Job ended with status 0x%08X\n", completion_code);

			nr_done = kbasep_jm_nr_jobs_submitted(slot);
			nr_done -= (active >> i) & 1;
			nr_done -= (active >> (i + 16)) & 1;

			if (nr_done <= 0) {
				dev_warn(kbdev->dev, "Spurious interrupt on slot %d", i);
				goto spurious;
			}

			count += nr_done;

			while (nr_done) {
				if (nr_done == 1) {
					kbase_job_done_slot(kbdev, i, completion_code, job_tail, &end_timestamp);
				} else {
					/* More than one job has completed. Since this is not the last job being reported this time it
					 * must have passed. This is because the hardware will not allow further jobs in a job slot to
					 * complete until the faile job is cleared from the IRQ status.
					 */
					kbase_job_done_slot(kbdev, i, BASE_JD_EVENT_DONE, 0, &end_timestamp);
				}
				nr_done--;
			}

 spurious:
			done = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT), NULL);

			if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10883)) {
				/* Workaround for missing interrupt caused by PRLAM-10883 */
				if (((active >> i) & 1) && (0 == kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_STATUS), NULL))) {
					/* Force job slot to be processed again */
					done |= (1u << i);
				}
			}

			failed = done >> 16;
			finished = (done & 0xFFFF) | failed;
		} while (finished & (1 << i));

		kbasep_job_slot_update_head_start_timestamp(kbdev, slot, end_timestamp);
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	if (atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_COMMITTED) {
		/* If we're trying to reset the GPU then we might be able to do it early
		 * (without waiting for a timeout) because some jobs have completed
		 */
		kbasep_try_reset_gpu_early(kbdev);
	}

	KBASE_TRACE_ADD(kbdev, JM_IRQ_END, NULL, NULL, 0, count);
}
KBASE_EXPORT_TEST_API(kbase_job_done)

static mali_bool kbasep_soft_stop_allowed(kbase_device *kbdev, u16 core_reqs)
{
	mali_bool soft_stops_allowed = MALI_TRUE;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408)) {
		if ((core_reqs & BASE_JD_REQ_T) != 0)
			soft_stops_allowed = MALI_FALSE;
	}
	return soft_stops_allowed;
}

static mali_bool kbasep_hard_stop_allowed(kbase_device *kbdev, u16 core_reqs)
{
	mali_bool hard_stops_allowed = MALI_TRUE;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8394)) {
		if ((core_reqs & BASE_JD_REQ_T) != 0)
			hard_stops_allowed = MALI_FALSE;
	}
	return hard_stops_allowed;
}

static void kbasep_job_slot_soft_or_hard_stop_do_action(kbase_device *kbdev, int js, u32 action, u16 core_reqs, kbase_jd_atom * target_katom )
{
	kbase_context *kctx = target_katom->kctx;
#if KBASE_TRACE_ENABLE
	u32 status_reg_before;
	u64 job_in_head_before;
	u32 status_reg_after;

	/* Check the head pointer */
	job_in_head_before = ((u64) kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_LO), NULL))
	    | (((u64) kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_HI), NULL)) << 32);
	status_reg_before = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_STATUS), NULL);
#endif

	if (action == JSn_COMMAND_SOFT_STOP) {
		mali_bool soft_stop_allowed = kbasep_soft_stop_allowed(kbdev, core_reqs);
		if (!soft_stop_allowed) {
#ifdef CONFIG_MALI_DEBUG
			dev_dbg(kbdev->dev, "Attempt made to soft-stop a job that cannot be soft-stopped. core_reqs = 0x%X", (unsigned int)core_reqs);
#endif				/* CONFIG_MALI_DEBUG */
			return;
		}

		/* We are about to issue a soft stop, so mark the atom as having been soft stopped */
		target_katom->atom_flags |= KBASE_KATOM_FLAG_BEEN_SOFT_STOPPPED;
	}

	if (action == JSn_COMMAND_HARD_STOP) {
		mali_bool hard_stop_allowed = kbasep_hard_stop_allowed(kbdev, core_reqs);
		if (!hard_stop_allowed) {
			/* Jobs can be hard-stopped for the following reasons:
			 *  * CFS decides the job has been running too long (and soft-stop has not occurred).
			 *    In this case the GPU will be reset by CFS if the job remains on the GPU.
			 *
			 *  * The context is destroyed, kbase_jd_zap_context will attempt to hard-stop the job. However
			 *    it also has a watchdog which will cause the GPU to be reset if the job remains on the GPU.
			 *
			 *  * An (unhandled) MMU fault occurred. As long as BASE_HW_ISSUE_8245 is defined then
			 *    the GPU will be reset.
			 *
			 * All three cases result in the GPU being reset if the hard-stop fails,
			 * so it is safe to just return and ignore the hard-stop request.
			 */
			dev_warn(kbdev->dev, "Attempt made to hard-stop a job that cannot be hard-stopped. core_reqs = 0x%X", (unsigned int)core_reqs);
			return;
		}
	}

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316) && action == JSn_COMMAND_SOFT_STOP) {
		int i;
		kbase_jm_slot *slot;
		slot = &kbdev->jm_slots[js];

		for (i = 0; i < kbasep_jm_nr_jobs_submitted(slot); i++) {
			kbase_jd_atom *katom;

			katom = kbasep_jm_peek_idx_submit_slot(slot, i);

			KBASE_DEBUG_ASSERT(katom);

			if (kbasep_jm_is_dummy_workaround_job(kbdev, katom) != MALI_FALSE) {
				/* Don't access the members of HW workaround 'dummy' jobs
				 *
				 * This assumes that such jobs can't cause HW_ISSUE_8316, and could only be blocked
				 * by other jobs causing HW_ISSUE_8316 (which will get poked/or eventually get killed) */
				continue;
			}

			/* For HW_ISSUE_8316, only 'bad' jobs attacking the system can
			 * cause this issue: normally, all memory should be allocated in
			 * multiples of 4 pages, and growable memory should be changed size
			 * in multiples of 4 pages.
			 *
			 * Whilst such 'bad' jobs can be cleared by a GPU reset, the
			 * locking up of a uTLB entry caused by the bad job could also
			 * stall other ASs, meaning that other ASs' jobs don't complete in
			 * the 'grace' period before the reset. We don't want to lose other
			 * ASs' jobs when they would normally complete fine, so we must
			 * 'poke' the MMU regularly to help other ASs complete */
			kbase_as_poking_timer_retain_atom(kbdev, katom->kctx, katom);
		}
	}

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_JOBCHAIN_DISAMBIGUATION)) {
		if (action == JSn_COMMAND_SOFT_STOP)
			action = (target_katom->atom_flags & KBASE_KATOM_FLAGS_JOBCHAIN) ? 
				 JSn_COMMAND_SOFT_STOP_1:
		         JSn_COMMAND_SOFT_STOP_0;
		else
			action = (target_katom->atom_flags & KBASE_KATOM_FLAGS_JOBCHAIN) ? 
				 JSn_COMMAND_HARD_STOP_1:
		         JSn_COMMAND_HARD_STOP_0;
	}

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND), action, kctx);

#if KBASE_TRACE_ENABLE
	status_reg_after = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_STATUS), NULL);
	if (status_reg_after == BASE_JD_EVENT_ACTIVE) {
		kbase_jm_slot *slot;
		kbase_jd_atom *head;
		kbase_context *head_kctx;

		slot = &kbdev->jm_slots[js];
		head = kbasep_jm_peek_idx_submit_slot(slot, slot->submitted_nr - 1);
		head_kctx = head->kctx;

		/* We don't need to check kbasep_jm_is_dummy_workaround_job( head ) here:
		 * - Members are not indirected through
		 * - The members will all be zero anyway
		 */
		if (status_reg_before == BASE_JD_EVENT_ACTIVE)
			KBASE_TRACE_ADD_SLOT(kbdev, JM_CHECK_HEAD, head_kctx, head, job_in_head_before, js);
		else
			KBASE_TRACE_ADD_SLOT(kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js);

		switch(action) {
		case JSn_COMMAND_SOFT_STOP:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_SOFTSTOP, head_kctx, head, head->jc, js);
			break;
		case JSn_COMMAND_SOFT_STOP_0:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_SOFTSTOP_0, head_kctx, head, head->jc, js);
			break;
		case JSn_COMMAND_SOFT_STOP_1:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_SOFTSTOP_1, head_kctx, head, head->jc, js);
			break;
		case JSn_COMMAND_HARD_STOP:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_HARDSTOP, head_kctx, head, head->jc, js);
			break;
		case JSn_COMMAND_HARD_STOP_0:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_HARDSTOP_0, head_kctx, head, head->jc, js);
			break;
		case JSn_COMMAND_HARD_STOP_1:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_HARDSTOP_1, head_kctx, head, head->jc, js);
			break;
		default:
			BUG();
			break;
		}
	} else {
		if (status_reg_before == BASE_JD_EVENT_ACTIVE)
			KBASE_TRACE_ADD_SLOT(kbdev, JM_CHECK_HEAD, NULL, NULL, job_in_head_before, js);
		else
			KBASE_TRACE_ADD_SLOT(kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js);

		switch(action) {
		case JSn_COMMAND_SOFT_STOP:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_SOFTSTOP, NULL, NULL, 0, js);
			break;
		case JSn_COMMAND_SOFT_STOP_0:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_SOFTSTOP_0, NULL, NULL, 0, js);
			break;
		case JSn_COMMAND_SOFT_STOP_1:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_SOFTSTOP_1, NULL, NULL, 0, js);
			break;
		case JSn_COMMAND_HARD_STOP:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_HARDSTOP, NULL, NULL, 0, js);
			break;
		case JSn_COMMAND_HARD_STOP_0:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_HARDSTOP_0, NULL, NULL, 0, js);
			break;
		case JSn_COMMAND_HARD_STOP_1:
			KBASE_TRACE_ADD_SLOT(kbdev, JM_HARDSTOP_1, NULL, NULL, 0, js);
			break;
		default:
			BUG();
			break;
		}
	}
#endif
}

/* Helper macros used by kbasep_job_slot_soft_or_hard_stop */
#define JM_SLOT_MAX_JOB_SUBMIT_REGS    2
#define JM_JOB_IS_CURRENT_JOB_INDEX(n) (1 == n)	/* Index of the last job to process */
#define JM_JOB_IS_NEXT_JOB_INDEX(n)    (2 == n)	/* Index of the prior to last job to process */

/** Soft or hard-stop a slot
 *
 * This function safely ensures that the correct job is either hard or soft-stopped.
 * It deals with evicting jobs from the next registers where appropriate.
 *
 * This does not attempt to stop or evict jobs that are 'dummy' jobs for HW workarounds.
 *
 * @param kbdev         The kbase device
 * @param kctx          The context to soft/hard-stop job(s) from (or NULL is all jobs should be targeted)
 * @param js            The slot that the job(s) are on
 * @param target_katom  The atom that should be targeted (or NULL if all jobs from the context should be targeted)
 * @param action        The action to perform, either JSn_COMMAND_HARD_STOP or JSn_COMMAND_SOFT_STOP
 */
static void kbasep_job_slot_soft_or_hard_stop(kbase_device *kbdev, kbase_context *kctx, int js, kbase_jd_atom *target_katom, u32 action)
{
	kbase_jd_atom *katom;
	u8 i;
	u8 jobs_submitted;
	kbase_jm_slot *slot;
	u16 core_reqs;
	kbasep_js_device_data *js_devdata;
	mali_bool can_safely_stop = kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_JOBCHAIN_DISAMBIGUATION);

	KBASE_DEBUG_ASSERT(action == JSn_COMMAND_HARD_STOP || action == JSn_COMMAND_SOFT_STOP);
	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	slot = &kbdev->jm_slots[js];
	KBASE_DEBUG_ASSERT(slot);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	jobs_submitted = kbasep_jm_nr_jobs_submitted(slot);

	KBASE_TIMELINE_TRY_SOFT_STOP(kctx, js, 1);
	KBASE_TRACE_ADD_SLOT_INFO(kbdev, JM_SLOT_SOFT_OR_HARD_STOP, kctx, NULL, 0u, js, jobs_submitted);

	if (jobs_submitted > JM_SLOT_MAX_JOB_SUBMIT_REGS)
		i = jobs_submitted - JM_SLOT_MAX_JOB_SUBMIT_REGS;
	else
		i = 0;

	/* Loop through all jobs that have been submitted to the slot and haven't completed */
	for (; i < jobs_submitted; i++) {
		katom = kbasep_jm_peek_idx_submit_slot(slot, i);

		if (kctx && katom->kctx != kctx)
			continue;

		if (target_katom && katom != target_katom)
			continue;

		if (kbasep_jm_is_dummy_workaround_job(kbdev, katom))
			continue;

		core_reqs = katom->core_req;
	
		if (JM_JOB_IS_CURRENT_JOB_INDEX(jobs_submitted - i)) {
			/* The last job in the slot, check if there is a job in the next register */
			if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), NULL) == 0)
				kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, action, core_reqs, katom);
			else {
				/* The job is in the next registers */
				beenthere(kctx, "clearing job from next registers on slot %d", js);
				kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_NOP, NULL);
				/* Check to see if we did remove a job from the next registers */
				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), NULL) != 0 || kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), NULL) != 0) {
					/* The job was successfully cleared from the next registers, requeue it */
					kbase_jd_atom *dequeued_katom = kbasep_jm_dequeue_tail_submit_slot(slot);
					KBASE_DEBUG_ASSERT(dequeued_katom == katom);
					jobs_submitted--;

					/* Set the next registers to NULL */
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), 0, NULL);
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), 0, NULL);

					/* As the job is removed from the next registers we undo the associated
					 * update to the job_chain_flag for the job slot. */
					if (can_safely_stop)
						slot->job_chain_flag = !slot->job_chain_flag;

					KBASE_TRACE_ADD_SLOT(kbdev, JM_SLOT_EVICT, dequeued_katom->kctx, dequeued_katom, dequeued_katom->jc, js);

					/* Complete the job, indicate it took no time, but don't submit any more at this point */
					kbase_jd_done(dequeued_katom, js, NULL, KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT);
				} else {
					/* The job transitioned into the current registers before we managed to evict it,
					 * in this case we fall back to soft/hard-stopping the job */
					beenthere(kctx, "missed job in next register, soft/hard-stopping slot %d", js);
					kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, action, core_reqs, katom);
				}
			}
		} else if (JM_JOB_IS_NEXT_JOB_INDEX(jobs_submitted - i)) {
			/* There's a job after this one, check to see if that job is in the next registers.
             * If so, we need to pay attention to not accidently stop that one when issueing
             * the command to stop the one pointed to by the head registers (as the one in the head
             * may finish in the mean time and the one in the next moves to the head). Either the hardware
			 * has support for this using job chain disambiguation or we need to evict the job
			 * from the next registers first to ensure we can safely stop the one pointed to by
			 * the head registers. */
			if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), NULL) != 0) {
				kbase_jd_atom *check_next_atom;
				/* It is - we should remove that job and soft/hard-stop the slot */

				/* Only proceed when the next job isn't a HW workaround 'dummy' job
				 *
				 * This can't be an ASSERT due to MMU fault code:
				 * - This first hard-stops the job that caused the fault
				 * - Under HW Issue 8245, it will then reset the GPU
				 *  - This causes a Soft-stop to occur on all slots
				 * - By the time of the soft-stop, we may (depending on timing) still have:
				 *  - The original job in HEAD, if it's not finished the hard-stop
				 *  - The dummy workaround job in NEXT
				 *
				 * Other cases could be coded in future that cause back-to-back Soft/Hard
				 * stops with dummy workaround jobs in place, e.g. MMU handler code and Job
				 * Scheduler watchdog timer running in parallel.
				 *
				 * Note, the index i+1 is valid to peek from: i == jobs_submitted-2, therefore
				 * i+1 == jobs_submitted-1 */
				check_next_atom = kbasep_jm_peek_idx_submit_slot(slot, i + 1);
				if (kbasep_jm_is_dummy_workaround_job(kbdev, check_next_atom) != MALI_FALSE)
					continue;

				if (!can_safely_stop) {
					beenthere(kctx, "clearing job from next registers on slot %d", js);
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_NOP, NULL);

					/* Check to see if we did remove a job from the next registers */
					if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), NULL) != 0 || kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), NULL) != 0) {
						/* We did remove a job from the next registers, requeue it */
						kbase_jd_atom *dequeued_katom = kbasep_jm_dequeue_tail_submit_slot(slot);
						KBASE_DEBUG_ASSERT(dequeued_katom != NULL);
						jobs_submitted--;

						/* Set the next registers to NULL */
						kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), 0, NULL);
						kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), 0, NULL);

						KBASE_TRACE_ADD_SLOT(kbdev, JM_SLOT_EVICT, dequeued_katom->kctx, dequeued_katom, dequeued_katom->jc, js);

						/* Complete the job, indicate it took no time, but don't submit any more at this point */
						kbase_jd_done(dequeued_katom, js, NULL, KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT);
					} else {
						/* We missed the job, that means the job we're interested in left the hardware before
						 * we managed to do anything, so we can proceed to the next job */
						continue;
					}
				}

				/* Next is now free, so we can soft/hard-stop the slot */
				beenthere(kctx, "soft/hard-stopped slot %d (there was a job in next which was successfully cleared)\n", js);
				kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, action, core_reqs, katom);
			}
			/* If there was no job in the next registers, then the job we were
			 * interested in has finished, so we need not take any action
			 */
		}
	}

	KBASE_TIMELINE_TRY_SOFT_STOP(kctx, js, 0);
}

void kbase_job_kill_jobs_from_context(kbase_context *kctx)
{
	unsigned long flags;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	int i;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	/* Cancel any remaining running jobs for this kctx  */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* Invalidate all jobs in context, to prevent re-submitting */
	for (i = 0; i < BASE_JD_ATOM_COUNT; i++)
		kctx->jctx.atoms[i].event_code = BASE_JD_EVENT_JOB_CANCELLED;

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		kbase_job_slot_hardstop(kctx, i, NULL);

	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
}

void kbase_job_zap_context(kbase_context *kctx)
{
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	int i;
	mali_bool evict_success;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	/*
	 * Critical assumption: No more submission is possible outside of the
	 * workqueue. This is because the OS *must* prevent U/K calls (IOCTLs)
	 * whilst the kbase_context is terminating.
	 */

	/* First, atomically do the following:
	 * - mark the context as dying
	 * - try to evict it from the policy queue */

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	js_kctx_info->ctx.is_dying = MALI_TRUE;

	dev_dbg(kbdev->dev, "Zap: Try Evict Ctx %p", kctx);
	mutex_lock(&js_devdata->queue_mutex);
	evict_success = kbasep_js_policy_try_evict_ctx(&js_devdata->policy, kctx);
	mutex_unlock(&js_devdata->queue_mutex);

	/*
	 * At this point we know:
	 * - If eviction succeeded, it was in the policy queue, but now no longer is
	 *  - We must cancel the jobs here. No Power Manager active reference to
	 * release.
	 *  - This happens asynchronously - kbase_jd_zap_context() will wait for
	 * those jobs to be killed.
	 * - If eviction failed, then it wasn't in the policy queue. It is one of
	 * the following:
	 *  - a. it didn't have any jobs, and so is not in the Policy Queue or the
	 * Run Pool (not scheduled)
	 *   - Hence, no more work required to cancel jobs. No Power Manager active
	 * reference to release.
	 *  - b. it was in the middle of a scheduling transaction (and thus must
	 * have at least 1 job). This can happen from a syscall or a kernel thread.
	 * We still hold the jsctx_mutex, and so the thread must be waiting inside
	 * kbasep_js_try_schedule_head_ctx(), before checking whether the runpool
	 * is full. That thread will continue after we drop the mutex, and will
	 * notice the context is dying. It will rollback the transaction, killing
	 * all jobs at the same time. kbase_jd_zap_context() will wait for those
	 * jobs to be killed.
	 *   - Hence, no more work required to cancel jobs, or to release the Power
	 * Manager active reference.
	 *  - c. it is scheduled, and may or may not be running jobs
	 * - We must cause it to leave the runpool by stopping it from submitting
	 * any more jobs. When it finally does leave,
	 * kbasep_js_runpool_requeue_or_kill_ctx() will kill all remaining jobs
	 * (because it is dying), release the Power Manager active reference, and
	 * will not requeue the context in the policy queue. kbase_jd_zap_context()
	 * will wait for those jobs to be killed.
	 *  - Hence, work required just to make it leave the runpool. Cancelling
	 * jobs and releasing the Power manager active reference will be handled
	 * when it leaves the runpool.
	 */

	if (evict_success != MALI_FALSE || js_kctx_info->ctx.is_scheduled == MALI_FALSE) {
		/* The following events require us to kill off remaining jobs and
		 * update PM book-keeping:
		 * - we evicted it correctly (it must have jobs to be in the Policy Queue)
		 *
		 * These events need no action, but take this path anyway:
		 * - Case a: it didn't have any jobs, and was never in the Queue
		 * - Case b: scheduling transaction will be partially rolled-back (this
		 * already cancels the jobs)
		 */

		KBASE_TRACE_ADD(kbdev, JM_ZAP_NON_SCHEDULED, kctx, NULL, 0u, js_kctx_info->ctx.is_scheduled);

		dev_dbg(kbdev->dev, "Zap: Ctx %p evict_success=%d, scheduled=%d", kctx, evict_success, js_kctx_info->ctx.is_scheduled);

		if (evict_success != MALI_FALSE) {
			/* Only cancel jobs when we evicted from the policy queue. No Power
			 * Manager active reference was held.
			 *
			 * Having is_dying set ensures that this kills, and doesn't requeue */
			kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, MALI_FALSE);
		}
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	} else {
		unsigned long flags;
		mali_bool was_retained;
		/* Case c: didn't evict, but it is scheduled - it's in the Run Pool */
		KBASE_TRACE_ADD(kbdev, JM_ZAP_SCHEDULED, kctx, NULL, 0u, js_kctx_info->ctx.is_scheduled);
		dev_dbg(kbdev->dev, "Zap: Ctx %p is in RunPool", kctx);

		/* Disable the ctx from submitting any more jobs */
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		/* Retain and (later) release the context whilst it is is now disallowed from submitting
		 * jobs - ensures that someone somewhere will be removing the context later on */
		was_retained = kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);

		/* Since it's scheduled and we have the jsctx_mutex, it must be retained successfully */
		KBASE_DEBUG_ASSERT(was_retained != MALI_FALSE);

		dev_dbg(kbdev->dev, "Zap: Ctx %p Kill Any Running jobs", kctx);
		/* Cancel any remaining running jobs for this kctx - if any. Submit is disallowed
		 * which takes effect immediately, so no more new jobs will appear after we do this.  */
		for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
			kbase_job_slot_hardstop(kctx, i, NULL);

		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		dev_dbg(kbdev->dev, "Zap: Ctx %p Release (may or may not schedule out immediately)", kctx);
		kbasep_js_runpool_release_ctx(kbdev, kctx);
	}
	KBASE_TRACE_ADD(kbdev, JM_ZAP_DONE, kctx, NULL, 0u, 0u);

	/* After this, you must wait on both the kbase_jd_context::zero_jobs_wait
	 * and the kbasep_js_kctx_info::ctx::is_scheduled_waitq - to wait for the
	 * jobs to be destroyed, and the context to be de-scheduled (if it was on
	 * the runpool).
	 *
	 * kbase_jd_zap_context() will do this. */
}
KBASE_EXPORT_TEST_API(kbase_job_zap_context)

mali_error kbase_job_slot_init(kbase_device *kbdev)
{
	int i;
	KBASE_DEBUG_ASSERT(kbdev);

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		kbasep_jm_init_submit_slot(&kbdev->jm_slots[i]);

	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_job_slot_init)

void kbase_job_slot_halt(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_job_slot_term(kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_job_slot_term)

/**
 * Soft-stop the specified job slot
 *
 * The job slot lock must be held when calling this function.
 * The job slot must not already be in the process of being soft-stopped.
 *
 * Where possible any job in the next register is evicted before the soft-stop.
 *
 * @param kbdev         The kbase device
 * @param js            The job slot to soft-stop
 * @param target_katom  The job that should be soft-stopped (or NULL for any job)
 */
void kbase_job_slot_softstop(kbase_device *kbdev, int js, kbase_jd_atom *target_katom)
{
	kbasep_job_slot_soft_or_hard_stop(kbdev, NULL, js, target_katom, JSn_COMMAND_SOFT_STOP);
}

/**
 * Hard-stop the specified job slot
 *
 * The job slot lock must be held when calling this function.
 *
 * @param kctx		The kbase context that contains the job(s) that should
 *			be hard-stopped
 * @param js		The job slot to hard-stop
 * @param target_katom	The job that should be hard-stopped (or NULL for all
 *			jobs from the context)
 */
void kbase_job_slot_hardstop(kbase_context *kctx, int js,
				kbase_jd_atom *target_katom)
{
	kbase_device *kbdev = kctx->kbdev;

	kbasep_job_slot_soft_or_hard_stop(kbdev, kctx, js, target_katom,
						JSn_COMMAND_HARD_STOP);
	if (kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_8401) ||
		kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_9510) ||
		(kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_T76X_3542) &&
		(target_katom == NULL || target_katom->core_req & BASE_JD_REQ_FS_AFBC))) {
		/* MIDBASE-2916 if a fragment job with AFBC encoding is
		 * hardstopped, ensure to do a soft reset also in order to
		 * clear the GPU status.
		 * Workaround for HW issue 8401 has an issue,so after
		 * hard-stopping just reset the GPU. This will ensure that the
		 * jobs leave the GPU.*/
		if (kbase_prepare_to_reset_gpu_locked(kbdev)) {
			dev_err(kbdev->dev, "Issueing GPU\
			soft-reset after hard stopping due to hardware issue");
			kbase_reset_gpu_locked(kbdev);
		}
	}
}


void kbase_debug_dump_registers(kbase_device *kbdev)
{
	int i;
	dev_err(kbdev->dev, "Register state:");
	dev_err(kbdev->dev, "  GPU_IRQ_RAWSTAT=0x%08x GPU_STATUS=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS), NULL));
	dev_err(kbdev->dev, "  JOB_IRQ_RAWSTAT=0x%08x JOB_IRQ_JS_STATE=0x%08x JOB_IRQ_THROTTLE=0x%08x",
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT), NULL),
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_JS_STATE), NULL),
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_THROTTLE), NULL));
	for (i = 0; i < 3; i++) {
		dev_err(kbdev->dev, "  JS%d_STATUS=0x%08x      JS%d_HEAD_LO=0x%08x",
			i, kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_STATUS),
					NULL),
			i, kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_HEAD_LO),
					NULL));
	}
	dev_err(kbdev->dev, "  MMU_IRQ_RAWSTAT=0x%08x GPU_FAULTSTATUS=0x%08x",
		kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_RAWSTAT), NULL),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS), NULL));
	dev_err(kbdev->dev, "  GPU_IRQ_MASK=0x%08x    JOB_IRQ_MASK=0x%08x     MMU_IRQ_MASK=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL),
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK), NULL),
		kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK), NULL));
	dev_err(kbdev->dev, "  PWR_OVERRIDE0=0x%08x   PWR_OVERRIDE1=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE0), NULL),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE1), NULL));
	dev_err(kbdev->dev, "  SHADER_CONFIG=0x%08x   L2_MMU_CONFIG=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(SHADER_CONFIG), NULL),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(L2_MMU_CONFIG), NULL));
}

void kbasep_reset_timeout_worker(struct work_struct *data)
{
	unsigned long flags;
	kbase_device *kbdev;
	int i;
	ktime_t end_timestamp = ktime_get();
	kbasep_js_device_data *js_devdata;
	kbase_uk_hwcnt_setup hwcnt_setup = { {0} };
	kbase_instr_state bckp_state;

	KBASE_DEBUG_ASSERT(data);

	kbdev = container_of(data, kbase_device, reset_work);

	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	KBASE_TRACE_ADD(kbdev, JM_BEGIN_RESET_WORKER, NULL, NULL, 0u, 0);

	/* Make sure the timer has completed - this cannot be done from interrupt context,
	 * so this cannot be done within kbasep_try_reset_gpu_early. */
	hrtimer_cancel(&kbdev->reset_timer);

	if (kbase_pm_context_active_handle_suspend(kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		/* This would re-activate the GPU. Since it's already idle, there's no
		 * need to reset it */
		atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING);
		wake_up(&kbdev->reset_wait);
		return;
	}

	mutex_lock(&kbdev->pm.lock);
	/* We hold the pm lock, so there ought to be a current policy */
	KBASE_DEBUG_ASSERT(kbdev->pm.pm_current_policy);

	/* All slot have been soft-stopped and we've waited SOFT_STOP_RESET_TIMEOUT for the slots to clear, at this point
	 * we assume that anything that is still left on the GPU is stuck there and we'll kill it when we reset the GPU */

	dev_err(kbdev->dev, "Resetting GPU (allowing up to %d ms)", RESET_TIMEOUT);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING) {	/*the same interrupt handler preempted itself */
		/* GPU is being reset */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}
	/* Save the HW counters setup */
	if (kbdev->hwcnt.kctx != NULL) {
		kbase_context *kctx = kbdev->hwcnt.kctx;
		hwcnt_setup.dump_buffer = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO), kctx) & 0xffffffff;
		hwcnt_setup.dump_buffer |= (mali_addr64) kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI), kctx) << 32;
		hwcnt_setup.jm_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN), kctx);
		hwcnt_setup.shader_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN), kctx);
		hwcnt_setup.tiler_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), kctx);
		hwcnt_setup.l3_cache_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), kctx);
		hwcnt_setup.mmu_l2_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN), kctx);
	}

	/* Output the state of some interesting registers to help in the
	 * debugging of GPU resets */
	kbase_debug_dump_registers(kbdev);

	bckp_state = kbdev->hwcnt.state;
	kbdev->hwcnt.state = KBASE_INSTR_STATE_RESETTING;
	kbdev->hwcnt.triggered = 0;
	/* Disable IRQ to avoid IRQ handlers to kick in after releaseing the spinlock;
	 * this also clears any outstanding interrupts */
	kbase_pm_disable_interrupts(kbdev);
	/* Ensure that any IRQ handlers have finished */
	kbase_synchronize_irqs(kbdev);
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Reset the GPU */
	kbase_pm_init_hw(kbdev, MALI_TRUE);
	/* IRQs were re-enabled by kbase_pm_init_hw, and GPU is still powered */

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	/* Restore the HW counters setup */
	if (kbdev->hwcnt.kctx != NULL) {
		kbase_context *kctx = kbdev->hwcnt.kctx;
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_OFF, kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),     hwcnt_setup.dump_buffer & 0xFFFFFFFF, kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),     hwcnt_setup.dump_buffer >> 32,        kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN),       hwcnt_setup.jm_bm,                    kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN),   hwcnt_setup.shader_bm,                kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), hwcnt_setup.l3_cache_bm,              kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN),   hwcnt_setup.mmu_l2_bm,                kctx);

		/* Due to PRLAM-8186 we need to disable the Tiler before we enable the HW counter dump. */
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0, kctx);
		else
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), hwcnt_setup.tiler_bm, kctx);

		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);

		/* If HW has PRLAM-8186 we can now re-enable the tiler HW counters dump */
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), hwcnt_setup.tiler_bm, kctx);
	}
	kbdev->hwcnt.state = bckp_state;
	switch(kbdev->hwcnt.state) {
	/* Cases for waking kbasep_cache_clean_worker worker */
	case KBASE_INSTR_STATE_CLEANED:
		/* Cache-clean IRQ occurred, but we reset:
		 * Wakeup incase the waiter saw RESETTING */
	case KBASE_INSTR_STATE_REQUEST_CLEAN:
		/* After a clean was requested, but before the regs were written:
		 * Wakeup incase the waiter saw RESETTING */
		wake_up(&kbdev->hwcnt.cache_clean_wait);
		break;
	case KBASE_INSTR_STATE_CLEANING:
		/* Either:
		 * 1) We've not got the Cache-clean IRQ yet: it was lost, or:
		 * 2) We got it whilst resetting: it was voluntarily lost
		 *
		 * So, move to the next state and wakeup: */
		kbdev->hwcnt.state = KBASE_INSTR_STATE_CLEANED;
		wake_up(&kbdev->hwcnt.cache_clean_wait);
		break;

	/* Cases for waking anyone else */
	case KBASE_INSTR_STATE_DUMPING:
		/* If dumping, abort the dump, because we may've lost the IRQ */
		kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
		kbdev->hwcnt.triggered = 1;
		wake_up(&kbdev->hwcnt.wait);
		break;
	case KBASE_INSTR_STATE_DISABLED:
	case KBASE_INSTR_STATE_IDLE:
	case KBASE_INSTR_STATE_FAULT:
		/* Every other reason: wakeup in that state */
		kbdev->hwcnt.triggered = 1;
		wake_up(&kbdev->hwcnt.wait);
		break;

	/* Unhandled cases */
	case KBASE_INSTR_STATE_RESETTING:
	default:
		BUG();
		break;
	}
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Complete any jobs that were still on the GPU */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++) {
		int nr_done;
		kbase_jm_slot *slot = &kbdev->jm_slots[i];

		nr_done = kbasep_jm_nr_jobs_submitted(slot);
		while (nr_done) {
			dev_err(kbdev->dev, "Job stuck in slot %d on the GPU was cancelled", i);
			kbase_job_done_slot(kbdev, i, BASE_JD_EVENT_JOB_CANCELLED, 0, &end_timestamp);
			nr_done--;
		}
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	mutex_lock(&js_devdata->runpool_mutex);

	/* Reprogram the GPU's MMU */
	for (i = 0; i < BASE_MAX_NR_AS; i++) {
		if (js_devdata->runpool_irq.per_as_data[i].kctx) {
			kbase_as *as = &kbdev->as[i];
			mutex_lock(&as->transaction_mutex);
			kbase_mmu_update(js_devdata->runpool_irq.per_as_data[i].kctx);
			mutex_unlock(&as->transaction_mutex);
		}
	}

	atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING);
	wake_up(&kbdev->reset_wait);
	dev_err(kbdev->dev, "Reset complete");

	/* Find out what cores are required now */
	kbase_pm_update_cores_state(kbdev);

	/* Synchronously request and wait for those cores, because if
	 * instrumentation is enabled it would need them immediately. */
	kbase_pm_check_transitions_sync(kbdev);

	/* Try submitting some jobs to restart processing */
	if (js_devdata->nr_user_contexts_running > 0) {
		KBASE_TRACE_ADD(kbdev, JM_SUBMIT_AFTER_RESET, NULL, NULL, 0u, 0);

		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		kbasep_js_try_run_next_job_nolock(kbdev);
		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	}
	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&kbdev->pm.lock);

	kbase_pm_context_idle(kbdev);
	KBASE_TRACE_ADD(kbdev, JM_END_RESET_WORKER, NULL, NULL, 0u, 0);
}

enum hrtimer_restart kbasep_reset_timer_callback(struct hrtimer *timer)
{
	kbase_device *kbdev = container_of(timer, kbase_device, reset_timer);

	KBASE_DEBUG_ASSERT(kbdev);

	/* Reset still pending? */
	if (atomic_cmpxchg(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) == KBASE_RESET_GPU_COMMITTED)
		queue_work(kbdev->reset_workq, &kbdev->reset_work);

	return HRTIMER_NORESTART;
}

/*
 * If all jobs are evicted from the GPU then we can reset the GPU
 * immediately instead of waiting for the timeout to elapse
 */

static void kbasep_try_reset_gpu_early_locked(kbase_device *kbdev)
{
	int i;
	int pending_jobs = 0;

	KBASE_DEBUG_ASSERT(kbdev);

	/* Count the number of jobs */
	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++) {
		kbase_jm_slot *slot = &kbdev->jm_slots[i];
		pending_jobs += kbasep_jm_nr_jobs_submitted(slot);
	}

	if (pending_jobs > 0) {
		/* There are still jobs on the GPU - wait */
		return;
	}

	/* Check that the reset has been committed to (i.e. kbase_reset_gpu has been called), and that no other
	 * thread beat this thread to starting the reset */
	if (atomic_cmpxchg(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) != KBASE_RESET_GPU_COMMITTED) {
		/* Reset has already occurred */
		return;
	}
	queue_work(kbdev->reset_workq, &kbdev->reset_work);
}

static void kbasep_try_reset_gpu_early(kbase_device *kbdev)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;

	js_devdata = &kbdev->js_data;
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	kbasep_try_reset_gpu_early_locked(kbdev);
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
}

/*
 * Prepare for resetting the GPU.
 * This function just soft-stops all the slots to ensure that as many jobs as possible are saved.
 *
 * The function returns a boolean which should be interpreted as follows:
 * - MALI_TRUE - Prepared for reset, kbase_reset_gpu should be called.
 * - MALI_FALSE - Another thread is performing a reset, kbase_reset_gpu should not be called.
 *
 * @return See description
 */
mali_bool kbase_prepare_to_reset_gpu_locked(kbase_device *kbdev)
{
	int i;

	KBASE_DEBUG_ASSERT(kbdev);

	if (atomic_cmpxchg(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING, KBASE_RESET_GPU_PREPARED) != KBASE_RESET_GPU_NOT_PENDING) {
		/* Some other thread is already resetting the GPU */
		return MALI_FALSE;
	}

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		kbase_job_slot_softstop(kbdev, i, NULL);

	return MALI_TRUE;
}

mali_bool kbase_prepare_to_reset_gpu(kbase_device *kbdev)
{
	unsigned long flags;
	mali_bool ret;
	kbasep_js_device_data *js_devdata;

	js_devdata = &kbdev->js_data;
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	ret = kbase_prepare_to_reset_gpu_locked(kbdev);
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	return ret;
}
KBASE_EXPORT_TEST_API(kbase_prepare_to_reset_gpu)

/*
 * This function should be called after kbase_prepare_to_reset_gpu iff it returns MALI_TRUE.
 * It should never be called without a corresponding call to kbase_prepare_to_reset_gpu.
 *
 * After this function is called (or not called if kbase_prepare_to_reset_gpu returned MALI_FALSE),
 * the caller should wait for kbdev->reset_waitq to be signalled to know when the reset has completed.
 */
void kbase_reset_gpu(kbase_device *kbdev)
{
	u32 timeout_ms;

	KBASE_DEBUG_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software issue for a race to be occuring here */
	KBASE_DEBUG_ASSERT(atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_PREPARED);
	atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED);

	timeout_ms = kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS);
	dev_err(kbdev->dev, "Preparing to soft-reset GPU: Waiting (upto %d ms) for all jobs to complete soft-stop\n", timeout_ms);
	hrtimer_start(&kbdev->reset_timer, HR_TIMER_DELAY_MSEC(timeout_ms), HRTIMER_MODE_REL);

	/* Try resetting early */
	kbasep_try_reset_gpu_early(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu)

void kbase_reset_gpu_locked(kbase_device *kbdev)
{
	u32 timeout_ms;

	KBASE_DEBUG_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software issue for a race to be occuring here */
	KBASE_DEBUG_ASSERT(atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_PREPARED);
	atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED);

	timeout_ms = kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS);
	dev_err(kbdev->dev, "Preparing to soft-reset GPU: Waiting (upto %d ms) for all jobs to complete soft-stop\n", timeout_ms);
	hrtimer_start(&kbdev->reset_timer, HR_TIMER_DELAY_MSEC(timeout_ms), HRTIMER_MODE_REL);

	/* Try resetting early */
	kbasep_try_reset_gpu_early_locked(kbdev);
}
