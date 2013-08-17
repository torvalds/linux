/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_jm.c
 * Base kernel job manager APIs
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/common/mali_kbase_gator.h>
#include <kbase/src/common/mali_kbase_js_affinity.h>
#include <kbase/src/common/mali_kbase_8401_workaround.h>
#include <kbase/src/common/mali_kbase_hw.h>

#include "mali_kbase_jm.h"

#define beenthere(f, a...)  OSK_PRINT_INFO(OSK_BASE_JM, "%s:" f, __func__, ##a)

static void kbasep_try_reset_gpu_early(kbase_device *kbdev);

static void kbase_job_hw_submit(kbase_device *kbdev, kbase_jd_atom *katom, int js)
{
	kbase_context *kctx;
	u32 cfg;
	u64 jc_head = katom->jc;

	OSK_ASSERT(kbdev);
	OSK_ASSERT(katom);

	kctx = katom->kctx;

	/* Command register must be available */
	OSK_ASSERT(kbasep_jm_is_js_free(kbdev, js, kctx));
	/* Affinity is not violating */
	kbase_js_debug_log_current_affinities( kbdev );
	OSK_ASSERT(!kbase_js_affinity_would_violate(kbdev, js, katom->affinity));

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), jc_head & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), jc_head >> 32, kctx);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_LO), katom->affinity & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_HI), katom->affinity >> 32, kctx);

	/* start MMU, medium priority, cache clean/flush on end, clean/flush on start */
	cfg = kctx->as_nr | JSn_CONFIG_END_FLUSH_CLEAN_INVALIDATE | JSn_CONFIG_START_MMU
		| JSn_CONFIG_START_FLUSH_CLEAN_INVALIDATE | JSn_CONFIG_THREAD_PRI(8);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_CONFIG_NEXT), cfg, kctx);

	/* Write an approximate start timestamp.
	 * It's approximate because there might be a job in the HEAD register. In
	 * such cases, we'll try to make a better approximation in the IRQ handler
	 * (up to the KBASE_JS_IRQ_THROTTLE_TIME_US). */
	katom->start_timestamp = ktime_get();

	/* GO ! */
	OSK_PRINT_INFO(OSK_BASE_JM, "JS: Submitting atom %p from ctx %p to js[%d] with head=0x%llx, affinity=0x%llx",
	               katom, kctx, js, jc_head, katom->affinity );

	KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_SUBMIT, kctx, katom, jc_head, js, (u32)katom->affinity );
	
#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_job_slots_event(GATOR_MAKE_EVENT(GATOR_JOB_SLOT_START, js), kctx);
#endif /* CONFIG_MALI_GATOR_SUPPORT */
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_START, katom->kctx);
}

void kbase_job_submit_nolock(kbase_device *kbdev, kbase_jd_atom *katom, int js)
{
	kbase_jm_slot *jm_slots;

	OSK_ASSERT(kbdev);

	jm_slots = kbdev->jm_slots;

	/*
	 * We can have:
	 * - one job already done (pending interrupt),
	 * - one running,
	 * - one ready to be run.
	 * Hence a maximum of 3 inflight jobs. We have a 4 job
	 * queue, which I hope will be enough...
	 */
	kbasep_jm_enqueue_submit_slot( &jm_slots[js], katom );
	kbase_job_hw_submit(kbdev, katom, js);
}

void kbase_job_done_slot(kbase_device *kbdev, int s, u32 completion_code, u64 job_tail, ktime_t *end_timestamp)
{
	kbase_jm_slot *slot;
	kbase_jd_atom *katom;
	mali_addr64    jc_head;
	kbase_context *kctx;

	OSK_ASSERT(kbdev);

	if (completion_code != BASE_JD_EVENT_DONE)
		printk(KERN_ERR "t6xx: GPU fault 0x%02lx from job slot %d\n", (unsigned long)completion_code, s);

	/* IMPORTANT: this function must only contain work necessary to complete a
	 * job from a Real IRQ (and not 'fake' completion, e.g. from
	 * Soft-stop). For general work that must happen no matter how the job was
	 * removed from the hardware, place it in kbase_jd_done() */

	slot = &kbdev->jm_slots[s];
	katom = kbasep_jm_dequeue_submit_slot( slot );

	/* If the katom completed is because it's a dummy job for HW workarounds, then take no further action */
	if(kbasep_jm_is_dummy_workaround_job(kbdev, katom))
	{
		KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_JOB_DONE, NULL, NULL, 0, s, completion_code );
		return;
	}

	jc_head = katom->jc;
	kctx = katom->kctx;

	KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_JOB_DONE, kctx, katom, jc_head, s, completion_code );

	if ( completion_code != BASE_JD_EVENT_DONE && completion_code != BASE_JD_EVENT_STOPPED )
	{

#if KBASE_TRACE_DUMP_ON_JOB_SLOT_ERROR != 0
		KBASE_TRACE_DUMP( kbdev );
#endif
	}
	if (job_tail != 0)
	{
		mali_bool was_updated = (job_tail != jc_head);
		/* Some of the job has been executed, so we update the job chain address to where we should resume from */
		katom->jc = job_tail;
		if ( was_updated )
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_UPDATE_HEAD, kctx, katom, job_tail, s );
		}
	}

	/* Only update the event code for jobs that weren't cancelled */
	if ( katom->event_code != BASE_JD_EVENT_JOB_CANCELLED )
	{
		katom->event_code = (base_jd_event_code)completion_code;
	}
	kbase_device_trace_register_access(kctx, REG_WRITE , JOB_CONTROL_REG(JOB_IRQ_CLEAR), 1 << s);

	/* Complete the job, with start_new_jobs = MALI_TRUE
	 *
	 * Also defer remaining work onto the workqueue:
	 * - Re-queue Soft-stopped jobs
	 * - For any other jobs, queue the job back into the dependency system
	 * - Schedule out the parent context if necessary, and schedule a new one in.
	 */
	kbase_jd_done( katom, s, end_timestamp, MALI_TRUE );
}

/**
 * Update the start_timestamp of the job currently in the HEAD, based on the
 * fact that we got an IRQ for the previous set of completed jobs.
 *
 * The estimate also takes into account the KBASE_JS_IRQ_THROTTLE_TIME_US and
 * the time the job was submitted, to work out the best estimate (which might
 * still result in an over-estimate to the calculated time spent)
 */
STATIC void kbasep_job_slot_update_head_start_timestamp( kbase_device *kbdev, kbase_jm_slot *slot, ktime_t end_timestamp )
{
	OSK_ASSERT(slot);

	if ( kbasep_jm_nr_jobs_submitted( slot ) > 0 )
	{
		kbase_jd_atom *katom;
		ktime_t new_timestamp;
		ktime_t timestamp_diff;
		katom = kbasep_jm_peek_idx_submit_slot( slot, 0 ); /* The atom in the HEAD */

		OSK_ASSERT( katom != NULL );

		if ( kbasep_jm_is_dummy_workaround_job( kbdev, katom ) != MALI_FALSE )
		{
			/* Don't access the members of HW workaround 'dummy' jobs */
			return;
		}

		/* Account for any IRQ Throttle time - makes an overestimate of the time spent by the job */
		new_timestamp = ktime_sub_ns( end_timestamp, KBASE_JS_IRQ_THROTTLE_TIME_US * 1000 );
		timestamp_diff = ktime_sub( new_timestamp, katom->start_timestamp );
		if ( ktime_to_ns( timestamp_diff ) >= 0 )
		{
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

	OSK_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	KBASE_TRACE_ADD( kbdev, JM_IRQ, NULL, NULL, 0, done );

	memset( &kbdev->slot_submit_count_irq[0], 0, sizeof(kbdev->slot_submit_count_irq) );

	/* write irq throttle register, this will prevent irqs from occurring until
	 * the given number of gpu clock cycles have passed */
	{
		int irq_throttle_cycles = atomic_read( &kbdev->irq_throttle_cycles );
		kbase_reg_write( kbdev, JOB_CONTROL_REG( JOB_IRQ_THROTTLE ), irq_throttle_cycles, NULL );
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
		i = osk_find_first_set_bit(finished);
		OSK_ASSERT(i >= 0);

		slot = &kbdev->jm_slots[i];

		do {
			int nr_done;
			u32 active;
			u32 completion_code = BASE_JD_EVENT_DONE; /* assume OK */
			u64 job_tail = 0;

			if (failed & (1u << i))
			{
				/* read out the job slot status code if the job slot reported failure */
				completion_code = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_STATUS), NULL);

				switch(completion_code)
				{
					case BASE_JD_EVENT_STOPPED:
#ifdef CONFIG_MALI_GATOR_SUPPORT
						kbase_trace_mali_job_slots_event(GATOR_MAKE_EVENT(GATOR_JOB_SLOT_SOFT_STOPPED, i), NULL);
#endif /* CONFIG_MALI_GATOR_SUPPORT */
						/* Soft-stopped job - read the value of JS<n>_TAIL so that the job chain can be resumed */
						job_tail = (u64)kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_TAIL_LO), NULL) |
						           ((u64)kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_TAIL_HI), NULL) << 32);
						break;
					default:
						OSK_PRINT_WARN(OSK_BASE_JD, "error detected from slot %d, job status 0x%08x (%s)",
						               i, completion_code, kbase_exception_name(completion_code));
				}

				if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_6787))
				{
					/* Limit the number of loops to avoid a hang if the interrupt is missed */
					u32 max_loops = KBASE_CLEAN_CACHE_MAX_LOOPS;

					/* cache flush when jobs complete with non-done codes */
					/* use GPU_COMMAND completion solution */
					/* clean & invalidate the caches */
					kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), 8, NULL);

					/* wait for cache flush to complete before continuing */
					while(--max_loops &&
					      (kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL) & CLEAN_CACHES_COMPLETED) == 0)
					{
					}

					/* clear the CLEAN_CACHES_COMPLETED irq*/
					kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), CLEAN_CACHES_COMPLETED, NULL);
				}
			}

			kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), done & ((1 << i) | (1 << (i + 16))), NULL);
			active = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_JS_STATE), NULL);

			if (((active >> i) & 1) == 0 && (((done >> (i+16)) & 1) == 0))
			{
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

				if ((rawstat >> (i+16)) & 1)
				{
					/* There is a failed job that we've missed - add it back to active */
					active |= (1u << i);
				}
			}

			OSK_PRINT_INFO(OSK_BASE_JM, "Job ended with status 0x%08X\n", completion_code);

			nr_done = kbasep_jm_nr_jobs_submitted( slot );
			nr_done -= (active >> i) & 1;
			nr_done -= (active >> (i + 16)) & 1;

			if (nr_done <= 0)
			{
				OSK_PRINT_WARN(OSK_BASE_JM,
				               "Spurious interrupt on slot %d",
				               i);
				goto spurious;
			}

			count += nr_done;

			while (nr_done) {
				if (nr_done == 1)
				{
					kbase_job_done_slot(kbdev, i, completion_code, job_tail, &end_timestamp);
				}
				else
				{
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
			
			failed = done >> 16;
			finished = (done & 0xFFFF) | failed;
		} while (finished & (1 << i));

		kbasep_job_slot_update_head_start_timestamp( kbdev, slot, end_timestamp );
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	if (atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_COMMITTED)
	{
		/* If we're trying to reset the GPU then we might be able to do it early
		 * (without waiting for a timeout) because some jobs have completed
		 */
		kbasep_try_reset_gpu_early(kbdev);
	}

	KBASE_TRACE_ADD( kbdev, JM_IRQ_END, NULL, NULL, 0, count );
}
KBASE_EXPORT_TEST_API(kbase_job_done)


static mali_bool kbasep_soft_stop_allowed(kbase_device *kbdev, u16 core_reqs)
{
	mali_bool soft_stops_allowed = MALI_TRUE;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
	{
		if ((core_reqs & BASE_JD_REQ_T) != 0)
		{
			soft_stops_allowed = MALI_FALSE;
		}
	}
	return soft_stops_allowed;
}

static mali_bool kbasep_hard_stop_allowed(kbase_device *kbdev, u16 core_reqs)
{
	mali_bool hard_stops_allowed = MALI_TRUE;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8394))
	{
		if ((core_reqs & BASE_JD_REQ_T) != 0)
		{
			hard_stops_allowed = MALI_FALSE;
		}
	}
	return hard_stops_allowed;
}

static void kbasep_job_slot_soft_or_hard_stop_do_action(kbase_device *kbdev, int js, u32 action,
                                                        u16 core_reqs, kbase_context *kctx )
{
#if KBASE_TRACE_ENABLE
	u32 status_reg_before;
	u64 job_in_head_before;
	u32 status_reg_after;

	/* Check the head pointer */
	job_in_head_before = ((u64)kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_LO), NULL))
		| (((u64)kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_HI), NULL)) << 32);
	status_reg_before = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_STATUS), NULL );
#endif

	if (action == JSn_COMMAND_SOFT_STOP)
	{
		mali_bool soft_stop_allowed = kbasep_soft_stop_allowed( kbdev, core_reqs );
		if (!soft_stop_allowed)
		{
#ifdef CONFIG_MALI_DEBUG
			OSK_PRINT(OSK_BASE_JM, "Attempt made to soft-stop a job that cannot be soft-stopped. core_reqs = 0x%X", (unsigned int) core_reqs);
#endif /* CONFIG_MALI_DEBUG */
			return;
		}
	}

	if (action == JSn_COMMAND_HARD_STOP)
	{
		mali_bool hard_stop_allowed = kbasep_hard_stop_allowed( kbdev, core_reqs );
		if (!hard_stop_allowed)
		{
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
			OSK_PRINT_WARN(OSK_BASE_JM, "Attempt made to hard-stop a job that cannot be hard-stopped. core_reqs = 0x%X", (unsigned int) core_reqs);
			return;
		}
	}

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316) && action == JSn_COMMAND_SOFT_STOP)
	{
		int i;
		kbase_jm_slot *slot;
		slot = &kbdev->jm_slots[js];

		for (i = 0; i < kbasep_jm_nr_jobs_submitted(slot); i++)
		{
			kbase_jd_atom *katom;
			kbase_as * as;
	
			katom = kbasep_jm_peek_idx_submit_slot(slot, i);
	
			OSK_ASSERT(katom);

			if ( kbasep_jm_is_dummy_workaround_job( kbdev, katom ) != MALI_FALSE )
			{
				/* Don't access the members of HW workaround 'dummy' jobs
				 *
				 * This assumes that such jobs can't cause HW_ISSUE_8316, and could only be blocked
				 * by other jobs causing HW_ISSUE_8316 (which will get poked/or eventually get killed) */
				continue;
			}

			if ( !katom->poking )
			{
				OSK_ASSERT(katom->kctx);
				OSK_ASSERT(katom->kctx->as_nr != KBASEP_AS_NR_INVALID);

				katom->poking = 1;
				as = &kbdev->as[katom->kctx->as_nr];
				kbase_as_poking_timer_retain(as);
			}
		}
	}

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND), action, kctx);

#if KBASE_TRACE_ENABLE
	status_reg_after = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_STATUS), NULL );
	if (status_reg_after == BASE_JD_EVENT_ACTIVE)
	{
		kbase_jm_slot *slot;
		kbase_jd_atom *head;
		kbase_context *head_kctx;

		slot = &kbdev->jm_slots[js];
		head = kbasep_jm_peek_idx_submit_slot( slot, slot->submitted_nr-1 );
		head_kctx = head->kctx;

		/* We don't need to check kbasep_jm_is_dummy_workaround_job( head ) here:
		 * - Members are not indirected through
		 * - The members will all be zero anyway
		 */
		if ( status_reg_before == BASE_JD_EVENT_ACTIVE )
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_CHECK_HEAD, head_kctx, head, job_in_head_before, js );
		}
		else
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js );
		}
		if (action == JSn_COMMAND_SOFT_STOP)
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_SOFTSTOP, head_kctx, head, head->jc, js );
		}
		else
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_HARDSTOP, head_kctx, head, head->jc, js );
		}
	}
	else
	{
		if ( status_reg_before == BASE_JD_EVENT_ACTIVE )
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_CHECK_HEAD, NULL, NULL, job_in_head_before, js );
		}
		else
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js );
		}

		if (action == JSn_COMMAND_SOFT_STOP)
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_SOFTSTOP, NULL, NULL, 0, js );
		}
		else
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_HARDSTOP, NULL, NULL, 0, js );
		}
	}
#endif
}

/* Helper macros used by kbasep_job_slot_soft_or_hard_stop */
#define JM_SLOT_MAX_JOB_SUBMIT_REGS    2
#define JM_JOB_IS_CURRENT_JOB_INDEX(n) (1 == n) /* Index of the last job to process */
#define JM_JOB_IS_NEXT_JOB_INDEX(n)    (2 == n) /* Index of the prior to last job to process */

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
static void kbasep_job_slot_soft_or_hard_stop(kbase_device *kbdev, kbase_context *kctx, int js,
                                              kbase_jd_atom *target_katom, u32 action)
{
	kbase_jd_atom *katom;
	u8 i;
	u8 jobs_submitted;
	kbase_jm_slot *slot;
	u16 core_reqs;
	kbasep_js_device_data *js_devdata;


	OSK_ASSERT(action == JSn_COMMAND_HARD_STOP || action == JSn_COMMAND_SOFT_STOP);
	OSK_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	slot = &kbdev->jm_slots[js];
	OSK_ASSERT(slot);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	jobs_submitted = kbasep_jm_nr_jobs_submitted( slot );
	KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_SLOT_SOFT_OR_HARD_STOP, kctx, NULL, 0u, js, jobs_submitted );

	if (jobs_submitted > JM_SLOT_MAX_JOB_SUBMIT_REGS)
	{
		i = jobs_submitted - JM_SLOT_MAX_JOB_SUBMIT_REGS;
	}
	else
	{
		i = 0;
	}

	/* Loop through all jobs that have been submitted to the slot and haven't completed */
	for(;i < jobs_submitted;i++)
	{
		katom = kbasep_jm_peek_idx_submit_slot( slot, i );

		if (kctx && katom->kctx != kctx)
		{
			continue;
		}
		if (target_katom && katom != target_katom)
		{
			continue;
		}
		if ( kbasep_jm_is_dummy_workaround_job( kbdev, katom ) )
		{
			continue;
		}

		core_reqs = katom->core_req;

		if (JM_JOB_IS_CURRENT_JOB_INDEX(jobs_submitted - i))
		{
			/* The last job in the slot, check if there is a job in the next register */
			if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), NULL) == 0)
			{
				kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, action, core_reqs, katom->kctx);
			}
			else
			{
				/* The job is in the next registers */
				beenthere("clearing job from next registers on slot %d", js);
				kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_NOP, NULL);

				/* Check to see if we did remove a job from the next registers */
				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), NULL) != 0 ||
				    kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), NULL) != 0)
				{
					/* The job was successfully cleared from the next registers, requeue it */
					kbase_jd_atom *dequeued_katom = kbasep_jm_dequeue_tail_submit_slot( slot );
					OSK_ASSERT(dequeued_katom == katom);
					jobs_submitted --;

					/* Set the next registers to NULL */
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), 0, NULL);
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), 0, NULL);

					KBASE_TRACE_ADD_SLOT( kbdev, JM_SLOT_EVICT, dequeued_katom->kctx, dequeued_katom, dequeued_katom->jc, js );

					dequeued_katom->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
					/* Complete the job, indicate it took no time, but require start_new_jobs == MALI_FALSE
					 * to prevent this slot being resubmitted to until we've dropped the lock */
					kbase_jd_done(dequeued_katom, js, NULL, MALI_FALSE);
				}
				else
				{
					/* The job transitioned into the current registers before we managed to evict it,
					 * in this case we fall back to soft/hard-stopping the job */
					beenthere("missed job in next register, soft/hard-stopping slot %d", js);
					kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, action, core_reqs, katom->kctx);
				}
			}
		}
		else if (JM_JOB_IS_NEXT_JOB_INDEX(jobs_submitted-i))
		{
			/* There's a job after this one, check to see if that job is in the next registers */
			if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), NULL) != 0)
			{
				kbase_jd_atom *check_next_atom;
				/* It is - we should remove that job and soft/hard-stop the slot */

				/* Only proceed when the next jobs isn't a HW workaround 'dummy' job
				 *
				 * This can't be an ASSERT due to MMU fault code:
				 * - This first hard-stops the job that caused the fault
				 *  - Under HW Issue 8401, this inserts a dummy workaround job into NEXT
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
				check_next_atom = kbasep_jm_peek_idx_submit_slot( slot, i+1 );
				if ( kbasep_jm_is_dummy_workaround_job( kbdev, check_next_atom ) != MALI_FALSE )
				{
					continue;
				}

				beenthere("clearing job from next registers on slot %d", js);
				kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_NOP, NULL);

				/* Check to see if we did remove a job from the next registers */
				if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), NULL) != 0 ||
				    kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), NULL) != 0)
				{
					/* We did remove a job from the next registers, requeue it */
					kbase_jd_atom *dequeued_katom = kbasep_jm_dequeue_tail_submit_slot( slot );
					OSK_ASSERT(dequeued_katom != NULL);
					jobs_submitted --;

					/* Set the next registers to NULL */
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), 0, NULL);
					kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), 0, NULL);

					KBASE_TRACE_ADD_SLOT( kbdev, JM_SLOT_EVICT, dequeued_katom->kctx, dequeued_katom, dequeued_katom->jc, js );

					dequeued_katom->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
					/* Complete the job, indicate it took no time, but require start_new_jobs == MALI_FALSE
					 * to prevent this slot being resubmitted to until we've dropped the lock */
					kbase_jd_done(dequeued_katom, js, NULL, MALI_FALSE);
				}
				else
				{
					/* We missed the job, that means the job we're interested in left the hardware before
					 * we managed to do anything, so we can proceed to the next job */
					continue;
				}

				/* Next is now free, so we can soft/hard-stop the slot */
				beenthere("soft/hard-stopped slot %d (there was a job in next which was successfully cleared)\n", js);
				kbasep_job_slot_soft_or_hard_stop_do_action(kbdev, js, action, core_reqs, katom->kctx);
			}
			/* If there was no job in the next registers, then the job we were
			 * interested in has finished, so we need not take any action
			 */
		}
	}
}

void kbase_job_kill_jobs_from_context(kbase_context *kctx)
{
	unsigned long flags;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	int i;

	OSK_ASSERT( kctx != NULL );
	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev != NULL );
	js_devdata = &kbdev->js_data;

	/* Cancel any remaining running jobs for this kctx  */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
	{
		kbase_job_slot_hardstop(kctx, i, NULL);
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
}

void kbase_job_zap_context(kbase_context *kctx)
{
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	int i;
	mali_bool evict_success;

	OSK_ASSERT( kctx != NULL );
	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev != NULL );
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

	mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	js_kctx_info->ctx.is_dying = MALI_TRUE;

	OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Try Evict Ctx %p", kctx );
	mutex_lock( &js_devdata->queue_mutex );
	evict_success = kbasep_js_policy_try_evict_ctx( &js_devdata->policy, kctx );
	mutex_unlock( &js_devdata->queue_mutex );

	mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

	/* locks must be dropped by this point, to prevent deadlock on flush */
	OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Flush Workqueue Ctx %p", kctx );
	KBASE_TRACE_ADD( kbdev, JM_FLUSH_WORKQS, kctx, NULL, 0u, 0u );
	kbase_jd_flush_workqueues( kctx );
	KBASE_TRACE_ADD( kbdev, JM_FLUSH_WORKQS_DONE, kctx, NULL, 0u, 0u );

	/*
	 * At this point we know that:
	 * - If eviction succeeded, it was in the policy queue, but now no longer is
	 * - If eviction failed, then it wasn't in the policy queue. It is one of the following:
	 *  - a. it didn't have any jobs, and so is not in the Policy Queue or the
	 * Run Pool (no work required)
	 *  - b. it was in the process of a scheduling transaction - but this can only
	 * happen as a result of the work-queue. Two options:
	 *   - i. it is now scheduled by the time of the flush - case d.
	 *   - ii. it is evicted from the Run Pool due to having to roll-back a transaction
	 *  - c. it is about to be scheduled out.
	 *   - In this case, we've marked it as dying, so the schedule-out code
	 * marks all jobs for killing, evicts it from the Run Pool, and does *not*
	 * place it back on the Policy Queue. The workqueue flush ensures this has
	 * completed
	 *  - d. it is scheduled, and may or may not be running jobs
	 *  - e. it was scheduled, but didn't get scheduled out during flushing of
	 * the workqueues. By the time we obtain the jsctx_mutex again, it may've
	 * been scheduled out
	 *
	 */

	mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	if ( evict_success != MALI_FALSE || js_kctx_info->ctx.is_scheduled == MALI_FALSE )
	{
		/* The following events require us to kill off remaining jobs and
		 * update PM book-keeping:
		 * - we evicted it correctly (it must have jobs to be in the Policy Queue)
		 *
		 * These events need no action:
		 * - Case a: it didn't have any jobs, and was never in the Queue
		 * - Case b-ii: scheduling transaction was partially rolled-back (this
		 * already cancels the jobs and pm-idles the ctx)
		 * - Case c: scheduled out and killing of all jobs completed on the work-queue (it's not in the Run Pool)
		 * - Case e: it was scheduled out after the workqueue was flushed, but
		 * before we re-obtained the jsctx_mutex. The jobs have already been
		 * cancelled (but the cancel may not have completed yet) and the PM has
		 * already been idled
		 */

		KBASE_TRACE_ADD( kbdev, JM_ZAP_NON_SCHEDULED, kctx, NULL, 0u, js_kctx_info->ctx.is_scheduled );

		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p evict_success=%d, scheduled=%d", kctx, evict_success, js_kctx_info->ctx.is_scheduled );

		if ( evict_success != MALI_FALSE )
		{
			/* Only cancel jobs and pm-idle when we evicted from the policy queue.
			 *
			 * Having is_dying set ensures that this kills, and doesn't requeue
			 *
			 * In addition, is_dying set ensure that this calls kbase_pm_context_idle().
			 * This is safe because the context is guaranteed to not be in the
			 * runpool, by virtue of it being evicted from the policy queue  */
			kbasep_js_runpool_requeue_or_kill_ctx( kbdev, kctx );
		}
		mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
	}
	else
	{
		unsigned long flags;
		mali_bool was_retained;
		/* Didn't evict, but it is scheduled - it's in the Run Pool:
		 * Cases d and b(i) */
		KBASE_TRACE_ADD( kbdev, JM_ZAP_SCHEDULED, kctx, NULL, 0u, js_kctx_info->ctx.is_scheduled );
		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p is in RunPool", kctx );

		/* Disable the ctx from submitting any more jobs */
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		kbasep_js_clear_submit_allowed( js_devdata, kctx );

		/* Retain and (later) release the context whilst it is is now disallowed from submitting
		 * jobs - ensures that someone somewhere will be removing the context later on */
		was_retained = kbasep_js_runpool_retain_ctx_nolock( kbdev, kctx );

		/* Since it's scheduled and we have the jsctx_mutex, it must be retained successfully */
		OSK_ASSERT( was_retained != MALI_FALSE );

		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p Kill Any Running jobs", kctx );
		/* Cancel any remaining running jobs for this kctx - if any. Submit is disallowed
		 * which takes effect immediately, so no more new jobs will appear after we do this.  */
		for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
		{
			kbase_job_slot_hardstop(kctx, i, NULL);
		}
		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
		mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p Release (may or may not schedule out immediately)", kctx );
		kbasep_js_runpool_release_ctx( kbdev, kctx );
	}
	KBASE_TRACE_ADD( kbdev, JM_ZAP_DONE, kctx, NULL, 0u, 0u );

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
	OSK_ASSERT(kbdev);

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
	{
		kbasep_jm_init_submit_slot( &kbdev->jm_slots[i] );
	}
	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_job_slot_init)

void kbase_job_slot_halt(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_job_slot_term(kbase_device *kbdev)
{
	OSK_ASSERT(kbdev);
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
 * @param kctx          The kbase context that contains the job(s) that should be hard-stopped
 * @param js            The job slot to hard-stop
 * @param target_katom  The job that should be hard-stopped (or NULL for all jobs from the context)
 */
void kbase_job_slot_hardstop(kbase_context *kctx, int js, kbase_jd_atom *target_katom)
{
	kbase_device *kbdev = kctx->kbdev;
	kbasep_job_slot_soft_or_hard_stop(kbdev, kctx, js, target_katom, JSn_COMMAND_HARD_STOP);

	if (kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_8401) ||
	    kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_9510))
	{
		/* The workaround for HW issue 8401 has an issue, so instead of hard-stopping
		 * just reset the GPU. This will ensure that the jobs leave the GPU.
		 */
		if (kbase_prepare_to_reset_gpu_locked(kbdev))
		{
			OSK_PRINT_WARN(OSK_BASE_JD, "NOTE: GPU will now be reset as a workaround for a hardware issue");
			kbase_reset_gpu_locked(kbdev);
		}
	}
}

void kbasep_reset_timeout_worker(struct work_struct *data)
{
	unsigned long flags;
	kbase_device *kbdev;
	int i;
	ktime_t end_timestamp = ktime_get();
	kbasep_js_device_data *js_devdata;
	kbase_uk_hwcnt_setup hwcnt_setup = {{0}};
	kbase_instr_state bckp_state;

	OSK_ASSERT(data);

	kbdev = container_of(data, kbase_device, reset_work);

	OSK_ASSERT(kbdev);
	KBASE_TRACE_ADD( kbdev, JM_BEGIN_RESET_WORKER, NULL, NULL, 0u, 0 );

	kbase_pm_context_active(kbdev);

	js_devdata = &kbdev->js_data;

	/* All slot have been soft-stopped and we've waited SOFT_STOP_RESET_TIMEOUT for the slots to clear, at this point
	 * we assume that anything that is still left on the GPU is stuck there and we'll kill it when we reset the GPU */

	OSK_PRINT_ERROR(OSK_BASE_JD, "Resetting GPU");

	/* Make sure the timer has completed - this cannot be done from interrupt context,
	 * so this cannot be done within kbasep_try_reset_gpu_early. */
	hrtimer_cancel(&kbdev->reset_timer);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING)
	{	/*the same interrupt handler preempted itself*/
		/* GPU is being reset*/
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}
	/* Save the HW counters setup */
	if (kbdev->hwcnt.kctx != NULL)
	{
		kbase_context *kctx = kbdev->hwcnt.kctx;
		hwcnt_setup.dump_buffer = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO), kctx) & 0xffffffff;
		hwcnt_setup.dump_buffer |= (mali_addr64)kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI), kctx) << 32;
		hwcnt_setup.jm_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN), kctx);
		hwcnt_setup.shader_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN), kctx);
		hwcnt_setup.tiler_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), kctx);
		hwcnt_setup.l3_cache_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), kctx);
		hwcnt_setup.mmu_l2_bm = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN), kctx);
	}
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
	kbase_pm_power_transitioning(kbdev);
	kbase_pm_init_hw(kbdev);
	/* IRQs were re-enabled by kbase_pm_init_hw */

	kbase_pm_power_transitioning(kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	/* Restore the HW counters setup */
	if (kbdev->hwcnt.kctx != NULL)
	{
		kbase_context *kctx = kbdev->hwcnt.kctx;
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),     hwcnt_setup.dump_buffer & 0xFFFFFFFF, kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),     hwcnt_setup.dump_buffer >> 32,        kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN),       hwcnt_setup.jm_bm,                    kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN),   hwcnt_setup.shader_bm,                kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), hwcnt_setup.l3_cache_bm,              kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN),   hwcnt_setup.mmu_l2_bm,                kctx);

		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
		{
			/* Issue 8186 requires TILER_EN to be disabled before updating PRFCNT_CONFIG. We then restore the register contents */
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0, kctx);
		}

		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN),    hwcnt_setup.tiler_bm,                 kctx);
	}
	kbdev->hwcnt.triggered = 1;
	wake_up(&kbdev->hwcnt.wait);
	kbdev->hwcnt.state = bckp_state;
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Re-init the power policy. Note that this does not re-enable interrupts,
	 * because the call to kbase_pm_clock_on() will do nothing (due to
	 * pm.gpu_powered == MALI_TRUE by this point) */
	kbase_pm_send_event(kbdev, KBASE_PM_EVENT_POLICY_INIT);

	/* Wait for the policy to power up the GPU */
	kbase_pm_wait_for_power_up(kbdev);

	/* Complete any jobs that were still on the GPU */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
	{
		int nr_done;
		kbase_jm_slot *slot = &kbdev->jm_slots[i];

		nr_done = kbasep_jm_nr_jobs_submitted( slot );
		while (nr_done) {
			OSK_PRINT_ERROR(OSK_BASE_JD, "Job stuck in slot %d on the GPU was cancelled", i);
			kbase_job_done_slot(kbdev, i, BASE_JD_EVENT_JOB_CANCELLED, 0, &end_timestamp);
			nr_done--;
		}
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	mutex_lock( &js_devdata->runpool_mutex );

	/* Reprogram the GPU's MMU */
	for(i = 0; i < BASE_MAX_NR_AS; i++)
	{
		if (js_devdata->runpool_irq.per_as_data[i].kctx) {
			kbase_as *as = &kbdev->as[i];
			mutex_lock(&as->transaction_mutex);
			kbase_mmu_update(js_devdata->runpool_irq.per_as_data[i].kctx);
			mutex_unlock(&as->transaction_mutex);
		}
	}

	atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING);
	wake_up(&kbdev->reset_wait);
	OSK_PRINT_ERROR(OSK_BASE_JD, "Reset complete");

	/* Try submitting some jobs to restart processing */
	if (js_devdata->nr_user_contexts_running > 0)
	{
		KBASE_TRACE_ADD( kbdev, JM_SUBMIT_AFTER_RESET, NULL, NULL, 0u, 0 );

		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		kbasep_js_try_run_next_job_nolock(kbdev);
		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	}
	mutex_unlock( &js_devdata->runpool_mutex );

	kbase_pm_context_idle(kbdev);
	KBASE_TRACE_ADD( kbdev, JM_END_RESET_WORKER, NULL, NULL, 0u, 0 );
}

enum hrtimer_restart kbasep_reset_timer_callback(struct hrtimer * timer)
{
	kbase_device *kbdev = container_of(timer, kbase_device, reset_timer);

	OSK_ASSERT(kbdev);

	/* Reset still pending? */
	if (atomic_cmpxchg(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) ==
                       KBASE_RESET_GPU_COMMITTED)
	{
		queue_work(kbdev->reset_workq, &kbdev->reset_work);
	}

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

	OSK_ASSERT(kbdev);

	/* Count the number of jobs */
	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
	{
		kbase_jm_slot *slot = &kbdev->jm_slots[i];
		pending_jobs += kbasep_jm_nr_jobs_submitted(slot);
	}

	if (pending_jobs > 0)
	{
		/* There are still jobs on the GPU - wait */
		return;
	}

	/* Check that the reset has been committed to (i.e. kbase_reset_gpu has been called), and that no other
	 * thread beat this thread to starting the reset */
	if (atomic_cmpxchg(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) !=
	        KBASE_RESET_GPU_COMMITTED)
	{
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

	OSK_ASSERT(kbdev);

	if (atomic_cmpxchg(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING, KBASE_RESET_GPU_PREPARED) !=
	        KBASE_RESET_GPU_NOT_PENDING)
	{
		/* Some other thread is already resetting the GPU */
		return MALI_FALSE;
	}

	OSK_PRINT_ERROR(OSK_BASE_JD, "Preparing to soft-reset GPU: Soft-stopping all jobs");

	for (i = 0; i < kbdev->gpu_props.num_job_slots; i++)
	{
		kbase_job_slot_softstop(kbdev, i, NULL);
	}

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

	OSK_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software issue for a race to be occuring here */
	OSK_ASSERT(atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_PREPARED);
	atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED);

	timeout_ms = kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS);
	hrtimer_start(&kbdev->reset_timer, HR_TIMER_DELAY_MSEC(timeout_ms), HRTIMER_MODE_REL);

	/* Try resetting early */
	kbasep_try_reset_gpu_early(kbdev);
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu)

void kbase_reset_gpu_locked(kbase_device *kbdev)
{
	u32 timeout_ms;

	OSK_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software issue for a race to be occuring here */
	OSK_ASSERT(atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_PREPARED);
	atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED);

	timeout_ms = kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS);
	hrtimer_start(&kbdev->reset_timer, HR_TIMER_DELAY_MSEC(timeout_ms), HRTIMER_MODE_REL);

	/* Try resetting early */
	kbasep_try_reset_gpu_early_locked(kbdev);
}
