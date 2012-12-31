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

#if BASE_HW_ISSUE_8401
#include <kbase/src/common/mali_kbase_8401_workaround.h>
#endif

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

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), jc_head & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), jc_head >> 32, kctx);

	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_LO), katom->affinity & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_AFFINITY_NEXT_HI), katom->affinity >> 32, kctx);

	/* start MMU, medium priority, cache clean/flush on end, clean/flush on start */
	cfg = kctx->as_nr | (3 << 12) | (1 << 10) | (8 << 16) | (3 << 8);
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_CONFIG_NEXT), cfg, kctx);

	/* Write an approximate start timestamp.
	 * It's approximate because there might be a job in the HEAD register. In
	 * such cases, we'll try to make a better approximation in the IRQ handler
	 * (up to the KBASE_JS_IRQ_THROTTLE_TIME_US). */
	katom->start_timestamp = kbasep_js_get_js_ticks();

	/* GO ! */
	OSK_PRINT_INFO(OSK_BASE_JM, "JS: Submitting atom %p from ctx %p to js[%d] with head=0x%llx, affinity=0x%llx",
	               katom, kctx, jc_head, katom->affinity );

	KBASE_TRACE_ADD_SLOT( kbdev, JM_SUBMIT, kctx, katom->user_atom, jc_head, js );

#if MALI_GATOR_SUPPORT
	kbase_trace_mali_timeline_event(GATOR_MAKE_EVENT(GATOR_TIMELINE_START, js));
#endif
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_START, katom->kctx);
}

void kbase_job_submit_nolock(kbase_device *kbdev, kbase_jd_atom *katom, int js)
{
	kbase_jm_slot *jm_slots;

	OSK_ASSERT(kbdev);

	jm_slots = kbdev->jm_slots;

#if BASE_HW_ISSUE_5713
	OSK_ASSERT(!jm_slots[js].submission_blocked_for_soft_stop);
#endif

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

void kbase_job_done_slot(kbase_device *kbdev, int s, u32 completion_code, u64 job_tail, kbasep_js_tick *end_timestamp)
{
	kbase_jm_slot *slot;
	kbase_jd_atom *katom;
	mali_addr64    jc_head;
	kbase_context *kctx;

	OSK_ASSERT(kbdev);

	slot = &kbdev->jm_slots[s];
	katom = kbasep_jm_dequeue_submit_slot( slot );

	/* If the katom completed is because it's a dummy job for HW workarounds, then take no further action */
	if(kbasep_jm_is_dummy_workaround_job(katom))
	{
		KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_JOB_DONE, NULL, NULL, 0, s, completion_code );
		return;
	}

#if BASE_HW_ISSUE_8316
	if (katom->poking)
	{
		OSK_ASSERT(katom->kctx->as_nr != KBASEP_AS_NR_INVALID);
		kbase_as_poking_timer_release(&kbdev->as[katom->kctx->as_nr]);
		katom->poking = 0;
	}
#endif /* BASE_HW_ISSUE_8316 */

	jc_head = katom->jc;
	kctx = katom->kctx;

	KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_JOB_DONE, kctx, katom->user_atom, jc_head, s, completion_code );

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
			KBASE_TRACE_ADD_SLOT( kbdev, JM_UPDATE_HEAD, kctx, katom->user_atom, job_tail, s );
		}
	}

#if BASE_HW_ISSUE_5713
	if (kbasep_jm_nr_jobs_submitted( slot ) == 0)
	{
		slot->submission_blocked_for_soft_stop = MALI_FALSE;
	}
#endif

	/* Only update the event code for jobs that weren't cancelled */
	if ( katom->event.event_code != BASE_JD_EVENT_JOB_CANCELLED )
	{
		katom->event.event_code = completion_code;
	}

#if MALI_GATOR_SUPPORT
	kbase_trace_mali_timeline_event(GATOR_MAKE_EVENT(GATOR_TIMELINE_STOP, s));
#endif
	kbasep_js_job_done_slot_irq( kbdev, s, katom, end_timestamp );

	kbase_device_trace_register_access(kctx, REG_WRITE , JOB_CONTROL_REG(JOB_IRQ_CLEAR), 1 << s);

	/* Defer the remaining work onto the workqueue:
	 * - Re-queue Soft-stopped jobs
	 * - For any other jobs, queue the job back into the dependency system
	 * - Schedule out the parent context if necessary, and schedule a new one in.
	 * - Try submitting jobs from outside of IRQ context if we failed here.
	 */
	kbase_jd_done(katom);
}

/**
 * Update the start_timestamp of the job currently in the HEAD, based on the
 * fact that we got an IRQ for the previous set of completed jobs.
 *
 * The estimate also takes into account the KBASE_JS_IRQ_THROTTLE_TIME_US and
 * the time the job was submitted, to work out the best estimate (which might
 * still result in an over-estimate to the calculated time spent)
 */
STATIC void kbasep_job_slot_update_head_start_timestamp( kbase_jm_slot *slot, kbasep_js_tick end_timestamp )
{
	OSK_ASSERT(slot);

	if ( kbasep_jm_nr_jobs_submitted( slot ) > 0 )
	{
		kbase_jd_atom *katom;
		kbasep_js_tick new_timestamp;
		katom = kbasep_jm_peek_idx_submit_slot( slot, 0 ); /* The atom in the HEAD */

		OSK_ASSERT( katom != NULL );

		if ( kbasep_jm_is_dummy_workaround_job( katom ) != MALI_FALSE )
		{
			/* Don't access the members of HW workaround 'dummy' jobs */
			return;
		}

		/* Account for any IRQ Throttle time - makes an overestimate of the time spent by the job */
		new_timestamp = end_timestamp - kbasep_js_convert_js_us_to_ticks(KBASE_JS_IRQ_THROTTLE_TIME_US);
		if ( kbasep_js_ticks_after( new_timestamp, katom->start_timestamp ) )
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
	int i;
	u32 count = 0;
	kbasep_js_tick end_timestamp = kbasep_js_get_js_ticks();

	OSK_ASSERT(kbdev);

	KBASE_TRACE_ADD( kbdev, JM_IRQ, NULL, NULL, 0, done );

	OSK_MEMSET( &kbdev->slot_submit_count_irq[0], 0, sizeof(kbdev->slot_submit_count_irq) );

	/* write irq throttle register, this will prevent irqs from occurring until
	 * the given number of gpu clock cycles have passed */
	{
		u32 irq_throttle_cycles = osk_atomic_get( &kbdev->irq_throttle_cycles );
		kbase_reg_write( kbdev, JOB_CONTROL_REG( JOB_IRQ_THROTTLE ), irq_throttle_cycles, NULL );
	}

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

		slot = kbase_job_slot_lock(kbdev, i);

		do {
			int nr_done;
			u32 active;
			u32 completion_code = BASE_JD_EVENT_DONE; /* assume OK */
			u64 job_tail = 0;

#if BASE_HW_ISSUE_5713
			if (failed & (1u << i) || slot->submission_blocked_for_soft_stop)
#else
			if (failed & (1u << i))
#endif
			{
				/* read out the job slot status code if the job slot reported failure */
				completion_code = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_STATUS), NULL);

				switch(completion_code)
				{
					case BASE_JD_EVENT_NOT_STARTED:
						/* NOT_STARTED means that the job slot is idle - so the previous job must have completed */
						completion_code = BASE_JD_EVENT_DONE;
						break;
					case BASE_JD_EVENT_STOPPED:
						/* Soft-stopped job - read the value of JS<n>_TAIL so that the job chain can be resumed */
						job_tail = (u64)kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_TAIL_LO), NULL) |
						           ((u64)kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_TAIL_HI), NULL) << 32);
						break;
#if BASE_HW_ISSUE_5713
					case BASE_JD_EVENT_ACTIVE:
						if (slot->submission_blocked_for_soft_stop)
						{
							/* We're still waiting for the job to soft-stop, but a previous job has completed */
							completion_code = BASE_JD_EVENT_DONE;
							break;
						}
#endif
					default:
						OSK_PRINT_WARN(OSK_BASE_JD, "error detected from slot %d, job status 0x%08x (%s)",
						               i, completion_code, kbase_exception_name(completion_code));
				}
#if BASE_HW_ISSUE_6787
				/* cache flush when jobs complete with non-done codes */
#if BASE_HW_ISSUE_6315
				{
					kbase_context *kctx;
					u32 saved_job;
					u32 saved_affinity_lo = 0;
					u32 saved_affinity_hi = 0;
					u32 saved_config = 0;
					u32 saved_head_lo = 0;
					u32 saved_head_hi = 0;
					u32 *null_job;
					u32 cfg;
					int as_number;
					kbase_jd_atom katom;

					/* use null job completion solution */
					/* get slot context */
					cfg = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_CONFIG), NULL);
					as_number = cfg & 0xf;
					kctx = kbdev->js_data.runpool_irq.per_as_data[as_number].kctx;

					/* save the next job (if there was one) */
					saved_job = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_COMMAND_NEXT), NULL);
					if(saved_job)
					{
						saved_affinity_lo = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_AFFINITY_NEXT_LO), NULL);
						saved_affinity_hi = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_AFFINITY_NEXT_HI), NULL);
						saved_config = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_CONFIG_NEXT), NULL);
						saved_head_lo = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_HEAD_NEXT_LO), NULL);
						saved_head_hi = kbase_reg_read(kbdev, JOB_SLOT_REG(i, JSn_HEAD_NEXT_HI), NULL);
						/* clear the next job */
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_COMMAND_NEXT), JSn_COMMAND_NOP, NULL);
					}

					/* create a null_job */
					null_job = (u32 *) kctx->nulljob_va;
					memset(null_job, 0,  8*sizeof(u32));
					/* word 4 = flags, set the job_type to 1 (null_job) */
					null_job[4] = (1 << 1);

					/* create the katom struct that job_hw_submit expects to see */
					katom.user_atom = NULL;
					katom.kctx = kctx;
					/* nulljob uses reserved memory */
					katom.jc = 4096;

					/* write job to hw registers */
					kbase_job_hw_submit(kbdev, &katom, i);

					/* start the job */
					kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), done & ((1 << i) | (1 << (i + 16))), NULL);

					/* wait for the job to complete */
					do
					{
						done = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT), NULL);

						failed = done >> 16;
						finished = (done & 0xFFFF) | failed;
					} while ((finished & (1 << i)) == 0);
				
					/* restore previous job (if there was one) */
					if(saved_job)
					{
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_AFFINITY_NEXT_LO), saved_affinity_lo, NULL);
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_AFFINITY_NEXT_HI), saved_affinity_hi, NULL);
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_CONFIG_NEXT), saved_config, NULL);
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_HEAD_NEXT_LO), saved_head_lo, NULL);
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_HEAD_NEXT_HI), saved_head_hi, NULL);
						kbase_reg_write(kbdev, JOB_SLOT_REG(i, JSn_COMMAND_NEXT), saved_job, NULL);
					}
				}
#else /* BASE_HW_ISSUE_6315 */
				/* use GPU_COMMAND completion solution */
				/* clean & invalidate the caches */
				kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), 8, NULL);

				/* wait for cache flush to complete before continuing */
				while((kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT), NULL) & CLEAN_CACHES_COMPLETED) == 0);
#endif /* BASE_HW_ISSUE_6315 */
#endif /* BASE_HW_ISSUE_6787 */
			}

			kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), done & ((1 << i) | (1 << (i + 16))), NULL);
			active = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_JS_STATE), NULL);

			if (((active >> i) & 1) == 0)
			{
				/* Work around a hardware issue. If a job fails after we last read RAWSTAT but
				 * before our call to IRQ_CLEAR then it will not be present in JS_STATE */
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

		kbasep_job_slot_update_head_start_timestamp( slot, end_timestamp );

		kbase_job_slot_unlock(kbdev, i);
	}

	if (osk_atomic_get(&kbdev->reset_gpu) == KBASE_RESET_GPU_COMMITTED)
	{
		/* If we're trying to reset the GPU then we might be able to do it early
		 * (without waiting for a timeout) because some jobs have completed
		 */
		kbasep_try_reset_gpu_early(kbdev);
	}

	KBASE_TRACE_ADD( kbdev, JM_IRQ_END, NULL, NULL, 0, count );
}
KBASE_EXPORT_TEST_API(kbase_job_done)


static mali_bool kbasep_soft_stop_allowed(u16 core_reqs)
{
	mali_bool soft_stops_allowed = MALI_TRUE;

#if BASE_HW_ISSUE_8408
	if ( (core_reqs & BASE_JD_REQ_T) != 0 )
	{
		soft_stops_allowed = MALI_FALSE;
	}
#else
	CSTD_UNUSED(core_reqs);
#endif

	return soft_stops_allowed;
}

static mali_bool kbasep_hard_stop_allowed(u16 core_reqs)
{
	mali_bool hard_stops_allowed = MALI_TRUE;

#if BASE_HW_ISSUE_8394
	if ( (core_reqs & BASE_JD_REQ_T) != 0 )
	{
		hard_stops_allowed = MALI_FALSE;
	}
#else
	CSTD_UNUSED(core_reqs);
#endif

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
		mali_bool soft_stop_allowed = kbasep_soft_stop_allowed( core_reqs );
		if (!soft_stop_allowed)
		{
			OSK_PRINT_WARN(OSK_BASE_JM, "Attempt made to soft-stop a job that cannot be soft-stopped. core_reqs = 0x%X", (unsigned int) core_reqs);
			return;
		}
	}

	if (action == JSn_COMMAND_HARD_STOP)
	{
		mali_bool hard_stop_allowed = kbasep_hard_stop_allowed( core_reqs );
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

#if BASE_HW_ISSUE_8316
	if (action == JSn_COMMAND_SOFT_STOP)
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

			if ( kbasep_jm_is_dummy_workaround_job( katom ) != MALI_FALSE )
			{
				/* Don't access the members of HW workaround 'dummy' jobs
				 *
				 * This assumes that such jobs can't cause HW_ISSUE_8316, and could only be blocked
				 * by other jobs causing HW_ISSUE_8316 (which will get poked/or eventually get killed) */
				continue;
			}

			OSK_ASSERT(katom->kctx);
			OSK_ASSERT(katom->kctx->as_nr != KBASEP_AS_NR_INVALID);
	
			katom->poking = 1;
			as = &kbdev->as[katom->kctx->as_nr];
			kbase_as_poking_timer_retain(as);
		}
	}
#endif /* BASE_HW_ISSUE_8316 */

#if BASE_HW_ISSUE_5713
	if (action == JSn_COMMAND_SOFT_STOP)
	{
		/* PRLAM-5713 means that we can't reliably identify a job that has been soft-stopped in the interrupt handler.
		 * To work around this we ensure that the slot is not submitted to again until we've dealt with the job that 
		 * is soft-stopped.
		 */
		kbase_jm_slot *slot;
		slot = &kbdev->jm_slots[js];
		slot->submission_blocked_for_soft_stop = MALI_TRUE;
	}
#endif
	kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND), action, kctx);

#if KBASE_TRACE_ENABLE
	status_reg_after = kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_STATUS), NULL );
	if (status_reg_after == 0x8)
	{
		kbase_jm_slot *slot;
		kbase_jd_atom *head;
		kbase_context *kctx;

		slot = &kbdev->jm_slots[js];
		head = kbasep_jm_peek_idx_submit_slot( slot, slot->submitted_nr-1 );
		kctx = head->kctx;

		/* We don't need to check kbasep_jm_is_dummy_workaround_job( head ) here:
		 * - Members are not indirected through
		 * - The members will all be zero anyway
		 */
		if ( status_reg_before == 0x8 )
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_CHECK_HEAD, kctx, head->user_atom, job_in_head_before, js );
		}
		else
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_CHECK_HEAD, NULL, NULL, 0, js );
		}
		if (action == JSn_COMMAND_SOFT_STOP)
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_SOFTSTOP, kctx, head->user_atom, head->jc, js );
		}
		else
		{
			KBASE_TRACE_ADD_SLOT( kbdev, JM_HARDSTOP, kctx, head->user_atom, head->jc, js );
		}
	}
	else
	{
		if ( status_reg_before == 0x8 )
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

#if BASE_HW_ISSUE_8401
	/* Only issue a compute dummy job on slots that support them */
	if((action == JSn_COMMAND_HARD_STOP) & (js != 0))
	{
		kbasep_8401_submit_dummy_job(kbdev, js);
	}
#endif
}

/** Soft or hard-stop a slot
 *
 * This function safely ensures that the correct job is either hard or soft-stopped.
 * It deals with evicting jobs from the next registers where appropriate.
 *
 * This does not attempt to stop or evict jobs that are 'dummy' jobs for HW workarounds.
 *
 * @param kbdex         The kbase device
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


	OSK_ASSERT(action == JSn_COMMAND_HARD_STOP || action == JSn_COMMAND_SOFT_STOP);
	OSK_ASSERT(kbdev);

	slot = &kbdev->jm_slots[js];
	OSK_ASSERT(slot);

	jobs_submitted = kbasep_jm_nr_jobs_submitted( slot );
	KBASE_TRACE_ADD_SLOT_INFO( kbdev, JM_ZAP_CONTEXT_SLOT, kctx, NULL, 0u, js, jobs_submitted );

	if (jobs_submitted > 2)
	{
		i = jobs_submitted - 2;
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
		if ( kbasep_jm_is_dummy_workaround_job( katom ) )
		{
			continue;
		}

		core_reqs = katom->core_req;

		if (i+1 == jobs_submitted)
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

					dequeued_katom->event.event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
					kbase_jd_done(dequeued_katom);
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
		else if (i+2 == jobs_submitted)
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
				if ( kbasep_jm_is_dummy_workaround_job( check_next_atom ) != MALI_FALSE )
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

					dequeued_katom->event.event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;
					kbase_jd_done(dequeued_katom);
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
	kbase_device *kbdev;
	int i;

	OSK_ASSERT( kctx != NULL );
	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev != NULL );

	/* Cancel any remaining running jobs for this kctx  */
	for (i = 0; i < kbdev->nr_job_slots; i++)
	{
		kbase_job_slot_lock(kbdev, i);
		kbase_job_slot_hardstop(kctx, i, NULL);
		kbase_job_slot_unlock(kbdev, i);
	}
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

	osk_mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	js_kctx_info->ctx.is_dying = MALI_TRUE;

	OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Try Evict Ctx %p", kctx );
	osk_mutex_lock( &js_devdata->queue_mutex );
	evict_success = kbasep_js_policy_try_evict_ctx( &js_devdata->policy, kctx );
	osk_mutex_unlock( &js_devdata->queue_mutex );

	osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

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
	 *
	 * Also Note: No-one can now clear the not_scheduled_waitq, because the
	 * context is guarenteed to not be in the policy queue, and can never
	 * return to it either (because is_dying is set). The waitq may already by
	 * clear (due to it being scheduled), but the code below ensures that it
	 * will eventually get set (be descheduled).
	 */

	osk_mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
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

		/* Only cancel jobs and pm-idle when we evicted from the policy queue*/
		if ( evict_success != MALI_FALSE )
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p kill remaining jobs and idle ctx", kctx );
			OSK_ASSERT( js_kctx_info->ctx.nr_jobs > 0 );
			/* Notify PM that a context has gone idle */
			kbase_pm_context_idle(kbdev);

			/* Kill all the jobs present (call kbase_jd_cancel on all jobs) */
			kbasep_js_policy_kill_all_ctx_jobs( &js_devdata->policy, kctx );
		}
		osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
	}
	else
	{
		mali_bool was_retained;
		/* Didn't evict, but it is scheduled - it's in the Run Pool:
		 * Cases d and b(i) */
		KBASE_TRACE_ADD( kbdev, JM_ZAP_SCHEDULED, kctx, NULL, 0u, js_kctx_info->ctx.is_scheduled );
		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p is in RunPool", kctx );

		/* Disable the ctx from submitting any more jobs */
		osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
		kbasep_js_clear_submit_allowed( js_devdata, kctx );
		osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

		/* Retain and release the context whilst it is is now disallowed from submitting
		 * jobs - ensures that someone somewhere will be removing the context later on */
		was_retained = kbasep_js_runpool_retain_ctx( kbdev, kctx );

		/* Since it's scheduled and we have the jsctx_mutex, it must be retained successfully */
		OSK_ASSERT( was_retained != MALI_FALSE );

		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p Kill Any Running jobs", kctx );
		/* Cancel any remaining running jobs for this kctx - if any. Submit is disallowed
		 * which takes effect from the dropping of the runpool_irq lock above, so no more new
		 * jobs will appear after we do this.  */
		for (i = 0; i < kbdev->nr_job_slots; i++)
		{
			kbase_job_slot_lock(kbdev, i);
			kbase_job_slot_hardstop(kctx, i, NULL);
			kbase_job_slot_unlock(kbdev, i);
		}
		osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

		OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Ctx %p Release (may or may not schedule out immediately)", kctx );
		kbasep_js_runpool_release_ctx( kbdev, kctx );
	}
	KBASE_TRACE_ADD( kbdev, JM_ZAP_DONE, kctx, NULL, 0u, 0u );

	/* After this, you must wait on both the kbase_jd_context::zero_jobs_waitq
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
	osk_error osk_err;

	OSK_ASSERT(kbdev);

#if BASE_HW_ISSUE_7347 == 0
	for (i = 0; i < kbdev->nr_job_slots; i++)
	{
		osk_err = osk_spinlock_irq_init(&kbdev->jm_slots[i].lock, OSK_LOCK_ORDER_JSLOT);
		if (OSK_ERR_NONE != osk_err)
		{
			int j;
			for (j = 0; j < i; j++)
			{
				osk_spinlock_irq_term(&kbdev->jm_slots[j].lock);
			}
			return MALI_ERROR_FUNCTION_FAILED;
		}
		kbasep_jm_init_submit_slot( &kbdev->jm_slots[i] );
	}
#else
	osk_err = osk_spinlock_irq_init(&kbdev->jm_slot_lock, OSK_LOCK_ORDER_JSLOT);
	if (OSK_ERR_NONE != osk_err)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}
	for(i = 0; i < kbdev->nr_job_slots; i++)
	{
		kbasep_jm_init_submit_slot( &kbdev->jm_slots[i] );
	}
#endif
	return MALI_ERROR_NONE;
}
KBASE_EXPORT_TEST_API(kbase_job_slot_init)

void kbase_job_slot_halt(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_job_slot_term(kbase_device *kbdev)
{
#if BASE_HW_ISSUE_7347 == 0
	int i;

	OSK_ASSERT(kbdev);

	for (i = 0; i < kbdev->nr_job_slots; i++)
	{
		osk_spinlock_irq_term(&kbdev->jm_slots[i].lock);
	}
#else
	osk_spinlock_irq_term(&kbdev->jm_slot_lock);
#endif
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
	kbasep_job_slot_soft_or_hard_stop(kctx->kbdev, kctx, js, target_katom, JSn_COMMAND_HARD_STOP);
}

void kbasep_reset_timeout_worker(osk_workq_work *data)
{
	kbase_device *kbdev;
	int i;
	kbasep_js_tick end_timestamp = kbasep_js_get_js_ticks();
	kbasep_js_device_data *js_devdata;
	int c;

	OSK_ASSERT(data);

	kbdev = CONTAINER_OF(data, kbase_device, reset_work);

	OSK_ASSERT(kbdev);

	js_devdata = &kbdev->js_data;

	/* All slot have been soft-stopped and we've waited SOFT_STOP_RESET_TIMEOUT for the slots to clear, at this point
	 * we assume that anything that is still left on the GPU is stuck there and we'll kill it when we reset the GPU */

	OSK_PRINT_ERROR(OSK_BASE_JD, "Resetting GPU");

	/* Make sure the timer has completed - this cannot be done from interrupt context,
	 * so this cannot be done within kbasep_try_reset_gpu_early. */
	osk_timer_stop(&kbdev->reset_timer);

	/* Reset the GPU */
	kbase_pm_power_transitioning(kbdev);
	kbase_pm_init_hw(kbdev);

	kbase_pm_power_transitioning(kbdev);

	/* Pretend there is a context active - this is to satisify the precondition of KBASE_PM_EVENT_POLICY_INIT.
	 * Note that we cannot use kbase_pm_context_active here because that waits for the power up which must happen
	 * after the KBASE_PM_EVENT_POLICY_INIT message is sent
	 */
	osk_spinlock_irq_lock(&kbdev->pm.active_count_lock);
	c = ++kbdev->pm.active_count;
	osk_spinlock_irq_unlock(&kbdev->pm.active_count_lock);

	if (c == 1)
	{
		kbasep_pm_record_gpu_active(kbdev);
	}

	/* Re-init the power policy */
	kbase_pm_send_event(kbdev, KBASE_PM_EVENT_POLICY_INIT);

	/* Wait for the policy to power up the GPU */
	kbase_pm_wait_for_power_up(kbdev);

	/* Idle the fake context */
	kbase_pm_context_idle(kbdev);

	/* Complete any jobs that were still on the GPU */
	for (i = 0; i < kbdev->nr_job_slots; i++)
	{
		int nr_done;
		kbase_jm_slot *slot = kbase_job_slot_lock(kbdev, i);

		nr_done = kbasep_jm_nr_jobs_submitted( slot );
		while (nr_done) {
			OSK_PRINT_ERROR(OSK_BASE_JD, "Job stuck in slot %d on the GPU was cancelled", i);
			kbase_job_done_slot(kbdev, i, BASE_JD_EVENT_JOB_CANCELLED, 0, &end_timestamp);
			nr_done--;
		}
#if BASE_HW_ISSUE_5713
		/* We've reset the GPU so the slot is no longer soft-stopped */
		slot->submission_blocked_for_soft_stop = MALI_FALSE;
#endif

		kbase_job_slot_unlock(kbdev, i);
	}

	osk_mutex_lock( &js_devdata->runpool_mutex );

	osk_atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING);
	osk_waitq_set(&kbdev->reset_waitq);
	OSK_PRINT_ERROR(OSK_BASE_JD, "Reset complete");

	/* Reprogram the GPU's MMU */
	for(i = 0; i < BASE_MAX_NR_AS; i++)
	{
		if (js_devdata->runpool_irq.per_as_data[i].kctx) {
			kbase_mmu_update(js_devdata->runpool_irq.per_as_data[i].kctx);
		}
	}

	/* Try submitting some jobs to restart processing */
	kbasep_js_try_run_next_job(kbdev);
	osk_mutex_unlock( &js_devdata->runpool_mutex );
}

void kbasep_reset_timer_callback(void *data)
{
	kbase_device *kbdev = (kbase_device*)data;

	OSK_ASSERT(kbdev);

	if (osk_atomic_compare_and_swap(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) !=
	        KBASE_RESET_GPU_COMMITTED)
	{
		/* Reset has been cancelled or has already occurred */
		return;
	}

	osk_workq_submit(&kbdev->reset_workq, &kbdev->reset_work);
}

/*
 * If all jobs are evicted from the GPU then we can reset the GPU
 * immediately instead of waiting for the timeout to elapse
 */
static void kbasep_try_reset_gpu_early(kbase_device *kbdev)
{
	int i;
	int pending_jobs = 0;

	OSK_ASSERT(kbdev);

	/* Count the number of jobs */
	for (i = 0; i < kbdev->nr_job_slots; i++)
	{
		kbase_jm_slot *slot = kbase_job_slot_lock(kbdev, i);
		pending_jobs += kbasep_jm_nr_jobs_submitted(slot);
		kbase_job_slot_unlock(kbdev, i);
	}
	
	if (pending_jobs > 0)
	{
		/* There are still jobs on the GPU - wait */
		return;
	}

	/* Check that the reset has been committed to (i.e. kbase_reset_gpu has been called), and that no other
	 * thread beat this thread to starting the reset */
	if (osk_atomic_compare_and_swap(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED, KBASE_RESET_GPU_HAPPENING) !=
	        KBASE_RESET_GPU_COMMITTED)
	{
		/* Reset has already occurred */
		return;
	}

	osk_workq_submit(&kbdev->reset_workq, &kbdev->reset_work);
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
mali_bool kbase_prepare_to_reset_gpu(kbase_device *kbdev)
{
	int i;

	OSK_ASSERT(kbdev);

	if (osk_atomic_compare_and_swap(&kbdev->reset_gpu, KBASE_RESET_GPU_NOT_PENDING, KBASE_RESET_GPU_PREPARED) !=
	        KBASE_RESET_GPU_NOT_PENDING)
	{
		/* Some other thread is already resetting the GPU */
		return MALI_FALSE;
	}

	osk_waitq_clear(&kbdev->reset_waitq);

	OSK_PRINT_ERROR(OSK_BASE_JD, "Preparing to soft-reset GPU: Soft-stopping all jobs");

	for (i = 0; i < kbdev->nr_job_slots; i++)
	{
		kbase_job_slot_lock(kbdev, i);
		kbase_job_slot_softstop(kbdev, i, NULL);
		kbase_job_slot_unlock(kbdev, i);
	}

	return MALI_TRUE;
}

/*
 * This function should be called after kbase_prepare_to_reset_gpu iff it returns MALI_TRUE.
 * It should never be called without a corresponding call to kbase_prepare_to_reset_gpu.
 *
 * After this function is called (or not called if kbase_prepare_to_reset_gpu returned MALI_FALSE),
 * the caller should wait for kbdev->reset_waitq to be signalled to know when the reset has completed.
 */
void kbase_reset_gpu(kbase_device *kbdev)
{
	osk_error ret;
	u32 timeout_ms;

	OSK_ASSERT(kbdev);

	/* Note this is an assert/atomic_set because it is a software bug for a race to be occuring here */
	OSK_ASSERT(osk_atomic_get(&kbdev->reset_gpu) == KBASE_RESET_GPU_PREPARED);
	osk_atomic_set(&kbdev->reset_gpu, KBASE_RESET_GPU_COMMITTED);

	timeout_ms = kbasep_get_config_value(kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS);
	ret = osk_timer_start(&kbdev->reset_timer, timeout_ms);
	if (ret != OSK_ERR_NONE)
	{
		OSK_PRINT_ERROR(OSK_BASE_JD, "Failed to start timer for soft-resetting GPU");
		/* We can't rescue jobs from the GPU so immediately reset */
		osk_workq_submit(&kbdev->reset_workq, &kbdev->reset_work);
	}

	/* Try resetting early */
	kbasep_try_reset_gpu_early(kbdev);
}

