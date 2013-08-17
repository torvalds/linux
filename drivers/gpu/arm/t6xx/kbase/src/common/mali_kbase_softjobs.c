/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



#include <kbase/src/common/mali_kbase.h>

#ifdef CONFIG_SYNC
#include <linux/sync.h>
#include <linux/syscalls.h>
#include "../linux/mali_kbase_sync.h"
#endif

/**
 * @file mali_kbase_softjobs.c
 *
 * This file implements the logic behind software only jobs that are
 * executed within the driver rather than being handed over to the GPU.
 */

static base_jd_event_code kbase_dump_cpu_gpu_time(kbase_jd_atom *katom)
{
	kbase_va_region *reg;
	osk_phy_addr addr;
	u64 pfn;
	u32 offset;
	char *page;
	struct timespec ts;
	base_dump_cpu_gpu_counters data;
	u64 system_time;
	u64 cycle_counter;
	mali_addr64 jc = katom->jc;
	kbase_context *kctx = katom->kctx;

	u32 hi1, hi2;

	memset(&data, 0, sizeof(data));

	kbase_pm_context_active(kctx->kbdev);

	/* Read hi, lo, hi to ensure that overflow from lo to hi is handled correctly */
	do {
		hi1 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_HI), NULL);
		cycle_counter = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_LO), NULL);
		hi2 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_HI), NULL);
		cycle_counter |= (((u64)hi1) << 32);
	} while (hi1 != hi2);

	/* Read hi, lo, hi to ensure that overflow from lo to hi is handled correctly */
	do {
		hi1 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(TIMESTAMP_HI), NULL);
		system_time = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(TIMESTAMP_LO), NULL);
		hi2 = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(TIMESTAMP_HI), NULL);
		system_time |= (((u64)hi1) << 32);
	} while (hi1 != hi2);

	/* Record the CPU's idea of current time */
	getnstimeofday(&ts);

	kbase_pm_context_idle(kctx->kbdev);

	data.sec = ts.tv_sec;
	data.usec = ts.tv_nsec / 1000;
	data.system_time = system_time;
	data.cycle_counter = cycle_counter;

	pfn = jc >> 12;
	offset = jc & 0xFFF;

	if (offset > 0x1000-sizeof(data))
	{
		/* Wouldn't fit in the page */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	reg = kbase_region_tracker_find_region_enclosing_address(kctx, jc);
	if (!reg)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}
	
	if (! (reg->flags & KBASE_REG_GPU_WR) )
	{
		/* Region is not writable by GPU so we won't write to it either */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	if (!reg->phy_pages)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	addr = reg->phy_pages[pfn - reg->start_pfn];
	if (!addr)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	page = osk_kmap(addr);
	if (!page)
	{
		return BASE_JD_EVENT_JOB_CANCELLED;
	}
	memcpy(page+offset, &data, sizeof(data));
	osk_sync_to_cpu(addr+offset, page+offset, sizeof(data));
	osk_kunmap(addr, page);

	return BASE_JD_EVENT_DONE;
}

#ifdef CONFIG_SYNC

/* Complete an atom that has returned '1' from kbase_process_soft_job (i.e. has waited)
 *
 * @param katom     The atom to complete
 */
static void complete_soft_job(kbase_jd_atom *katom)
{
	int err;
	kbase_context *kctx = katom->kctx;

	kbasep_list_trace_add(15, kctx->kbdev, katom, &kctx->waiting_soft_jobs, KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_WAITING_SOFT_JOBS);
	mutex_lock(&kctx->jctx.lock);
	OSK_DLIST_REMOVE(&kctx->waiting_soft_jobs, katom, dep_item[0], err);
	if (err) {
		kbasep_list_trace_dump(kctx->kbdev);
		BUG();
	}
	kbase_finish_soft_job(katom);
	if (jd_done_nolock(katom))
	{
		kbasep_js_try_schedule_head_ctx( kctx->kbdev );
	}
	mutex_unlock(&kctx->jctx.lock);
}


static base_jd_event_code kbase_fence_trigger(kbase_jd_atom *katom, int result)
{
	struct sync_pt *pt;
	struct sync_timeline *timeline;

	if (!list_is_singular(&katom->fence->pt_list_head))
	{
		/* Not exactly one item in the list - so it didn't (directly) come from us */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	pt = list_first_entry(&katom->fence->pt_list_head, struct sync_pt, pt_list);
	timeline = pt->parent;

	if (!kbase_sync_timeline_is_ours(timeline))
	{
		/* Fence has a sync_pt which isn't ours! */
		return BASE_JD_EVENT_JOB_CANCELLED;
	}

	kbase_sync_signal_pt(pt, result);

	sync_timeline_signal(timeline);

	return (result < 0) ? BASE_JD_EVENT_JOB_CANCELLED : BASE_JD_EVENT_DONE;
}

static void kbase_fence_wait_worker(struct work_struct *data)
{
	kbase_jd_atom *katom;
	kbase_context *kctx;

	katom = container_of(data, kbase_jd_atom, work);
	kctx = katom->kctx;

	complete_soft_job(katom);
}

static void kbase_fence_wait_callback(struct sync_fence *fence, struct sync_fence_waiter *waiter)
{
	kbase_jd_atom *katom = container_of(waiter, kbase_jd_atom, sync_waiter);
	kbase_context *kctx;

	OSK_ASSERT(NULL != katom);

	kctx = katom->kctx;

	OSK_ASSERT(NULL != kctx);

	/* Propagate the fence status to the atom.
	 * If negative then cancel this atom and its dependencies.
	 */
	if (fence->status < 0)
	{
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
	}

	/* To prevent a potential deadlock we schedule the work onto the job_done_wq workqueue
	 *
	 * The issue is that we may signal the timeline while holding kctx->jctx.lock and
	 * the callbacks are run synchronously from sync_timeline_signal. So we simply defer the work.
	 */

	OSK_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, kbase_fence_wait_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

static int kbase_fence_wait(kbase_jd_atom *katom)
{
	int ret;

	OSK_ASSERT(NULL != katom);
	OSK_ASSERT(NULL != katom->kctx);

	sync_fence_waiter_init(&katom->sync_waiter, kbase_fence_wait_callback);

	ret = sync_fence_wait_async(katom->fence, &katom->sync_waiter);

	if (ret == 1)
	{
		/* Already signalled */
		return 0;
	}
	else if (ret < 0)
	{
		goto cancel_atom;
	}
	return 1;

cancel_atom:
	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
	/* We should cause the dependant jobs in the bag to be failed,
	 * to do this we schedule the work queue to complete this job */
	OSK_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, kbase_fence_wait_worker);
	queue_work(katom->kctx->jctx.job_done_wq, &katom->work);
	return 1;
}

static void kbase_fence_cancel_wait(kbase_jd_atom *katom)
{
	if (sync_fence_cancel_async(katom->fence, &katom->sync_waiter) != 0)
	{
		/* The wait wasn't cancelled - leave the cleanup for kbase_fence_wait_callback */
		return;
	}

	/* Wait was cancelled - zap the atoms */
	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	kbase_finish_soft_job(katom);

	if (jd_done_nolock(katom))
	{
		kbasep_js_try_schedule_head_ctx( katom->kctx->kbdev );
	}
}

#endif

int kbase_process_soft_job(kbase_jd_atom *katom )
{
	switch(katom->core_req)
	{
		case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
			katom->event_code = kbase_dump_cpu_gpu_time(katom);
			break;
#ifdef CONFIG_SYNC
		case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
			OSK_ASSERT(katom->fence != NULL);
			katom->event_code = kbase_fence_trigger(katom, katom->event_code == BASE_JD_EVENT_DONE ? 0 : -EFAULT);
			/* Release the reference as we don't need it any more */
			sync_fence_put(katom->fence);
			katom->fence = NULL;
			break;
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			return kbase_fence_wait(katom);
#endif /* CONFIG_SYNC */
	}

	/* Atom is complete */
	return 0;
}

void kbase_cancel_soft_job(kbase_jd_atom *katom)
{
	switch(katom->core_req)
	{
#ifdef CONFIG_SYNC
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			kbase_fence_cancel_wait(katom);
			break;
#endif
		default:
			/* This soft-job doesn't support cancellation! */
			OSK_ASSERT(0);
	}
}

mali_error kbase_prepare_soft_job(kbase_jd_atom *katom )
{
	switch(katom->core_req) {
		case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
			/* Nothing to do */
			break;
#ifdef CONFIG_SYNC
		case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
			{
				base_fence fence;
				int fd;
				if (MALI_ERROR_NONE != ukk_copy_from_user(sizeof(fence), &fence, (__user void*)(uintptr_t)katom->jc))
				{
					return MALI_ERROR_FUNCTION_FAILED;
				}
				fd = kbase_stream_create_fence(fence.basep.stream_fd);
				if (fd < 0)
				{
					return MALI_ERROR_FUNCTION_FAILED;
				}
				katom->fence = sync_fence_fdget(fd);

				if (katom->fence == NULL)
				{
					/* The only way the fence can be NULL is if userspace closed it for us.
					 * So we don't need to clear it up */
					return MALI_ERROR_FUNCTION_FAILED;
				}
				fence.basep.fd = fd;
				if (MALI_ERROR_NONE != ukk_copy_to_user(sizeof(fence), (__user void*)(uintptr_t)katom->jc, &fence))
				{
					katom->fence = NULL;
					sys_close(fd);
					return MALI_ERROR_FUNCTION_FAILED;
				}
			}
			break;
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			{
				base_fence fence;
				if (MALI_ERROR_NONE != ukk_copy_from_user(sizeof(fence), &fence, (__user void*)(uintptr_t)katom->jc))
				{
					return MALI_ERROR_FUNCTION_FAILED;
				}

				/* Get a reference to the fence object */
				katom->fence = sync_fence_fdget(fence.basep.fd);
				if (katom->fence == NULL)
				{
					return MALI_ERROR_FUNCTION_FAILED;
				}
			}
			break;
#endif /* CONFIG_SYNC */
		default:
			/* Unsupported soft-job */
			return MALI_ERROR_FUNCTION_FAILED;
	}
	return MALI_ERROR_NONE;
}

void kbase_finish_soft_job(kbase_jd_atom *katom )
{
	switch(katom->core_req) {
		case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
			/* Nothing to do */
			break;
#ifdef CONFIG_SYNC
		case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
			if (katom->fence) {
				/* The fence has not yet been signalled, so we do it now */
				kbase_fence_trigger(katom, katom->event_code == BASE_JD_EVENT_DONE ? 0 : -EFAULT);
				sync_fence_put(katom->fence);
				katom->fence = NULL;
			}
			break;
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			/* Release the reference to the fence object */
			sync_fence_put(katom->fence);
			katom->fence = NULL;
			break;
#endif /* CONFIG_SYNC */
	}
}
