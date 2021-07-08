/*
 *
 * (C) COPYRIGHT 2011-2017 ARM Limited. All rights reserved.
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





#include <mali_kbase.h>

#if defined(CONFIG_DMA_SHARED_BUFFER)
#include <linux/dma-buf.h>
#include <asm/cacheflush.h>
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) */
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif
#include <linux/dma-mapping.h>
#include <mali_base_kernel.h>
#include <mali_kbase_hwaccess_time.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_tlstream.h>
#include <linux/version.h>
#include <linux/ktime.h>
#include <linux/pfn.h>
#include <linux/sched.h>

/* Mask to check cache alignment of data structures */
#define KBASE_CACHE_ALIGNMENT_MASK		((1<<L1_CACHE_SHIFT)-1)

/**
 * @file mali_kbase_softjobs.c
 *
 * This file implements the logic behind software only jobs that are
 * executed within the driver rather than being handed over to the GPU.
 */

static void kbasep_add_waiting_soft_job(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	unsigned long lflags;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_add_tail(&katom->queue, &kctx->waiting_soft_jobs);
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

void kbasep_remove_waiting_soft_job(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	unsigned long lflags;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_del(&katom->queue);
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

static void kbasep_add_waiting_with_timeout(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	/* Record the start time of this atom so we could cancel it at
	 * the right time.
	 */
	katom->start_timestamp = ktime_get();

	/* Add the atom to the waiting list before the timer is
	 * (re)started to make sure that it gets processed.
	 */
	kbasep_add_waiting_soft_job(katom);

	/* Schedule timeout of this atom after a period if it is not active */
	if (!timer_pending(&kctx->soft_job_timeout)) {
		int timeout_ms = atomic_read(
				&kctx->kbdev->js_data.soft_job_timeout_ms);
		mod_timer(&kctx->soft_job_timeout,
			  jiffies + msecs_to_jiffies(timeout_ms));
	}
}

static int kbasep_read_soft_event_status(
		struct kbase_context *kctx, u64 evt, unsigned char *status)
{
	unsigned char *mapped_evt;
	struct kbase_vmap_struct map;

	mapped_evt = kbase_vmap(kctx, evt, sizeof(*mapped_evt), &map);
	if (!mapped_evt)
		return -EFAULT;

	*status = *mapped_evt;

	kbase_vunmap(kctx, &map);

	return 0;
}

static int kbasep_write_soft_event_status(
		struct kbase_context *kctx, u64 evt, unsigned char new_status)
{
	unsigned char *mapped_evt;
	struct kbase_vmap_struct map;

	if ((new_status != BASE_JD_SOFT_EVENT_SET) &&
	    (new_status != BASE_JD_SOFT_EVENT_RESET))
		return -EINVAL;

	mapped_evt = kbase_vmap(kctx, evt, sizeof(*mapped_evt), &map);
	if (!mapped_evt)
		return -EFAULT;

	*mapped_evt = new_status;

	kbase_vunmap(kctx, &map);

	return 0;
}

static int kbase_dump_cpu_gpu_time(struct kbase_jd_atom *katom)
{
	struct kbase_vmap_struct map;
	void *user_result;
	struct timespec64 ts;
	struct base_dump_cpu_gpu_counters data;
	u64 system_time;
	u64 cycle_counter;
	u64 jc = katom->jc;
	struct kbase_context *kctx = katom->kctx;
	int pm_active_err;

	memset(&data, 0, sizeof(data));

	/* Take the PM active reference as late as possible - otherwise, it could
	 * delay suspend until we process the atom (which may be at the end of a
	 * long chain of dependencies */
	pm_active_err = kbase_pm_context_active_handle_suspend(kctx->kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE);
	if (pm_active_err) {
		struct kbasep_js_device_data *js_devdata = &kctx->kbdev->js_data;

		/* We're suspended - queue this on the list of suspended jobs
		 * Use dep_item[1], because dep_item[0] was previously in use
		 * for 'waiting_soft_jobs'.
		 */
		mutex_lock(&js_devdata->runpool_mutex);
		list_add_tail(&katom->dep_item[1], &js_devdata->suspended_soft_jobs_list);
		mutex_unlock(&js_devdata->runpool_mutex);

		/* Also adding this to the list of waiting soft job */
		kbasep_add_waiting_soft_job(katom);

		return pm_active_err;
	}

	kbase_backend_get_gpu_time(kctx->kbdev, &cycle_counter, &system_time,
									&ts);

	kbase_pm_context_idle(kctx->kbdev);

	data.sec = ts.tv_sec;
	data.usec = ts.tv_nsec / 1000;
	data.system_time = system_time;
	data.cycle_counter = cycle_counter;

	/* Assume this atom will be cancelled until we know otherwise */
	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	/* GPU_WR access is checked on the range for returning the result to
	 * userspace for the following reasons:
	 * - security, this is currently how imported user bufs are checked.
	 * - userspace ddk guaranteed to assume region was mapped as GPU_WR */
	user_result = kbase_vmap_prot(kctx, jc, sizeof(data), KBASE_REG_GPU_WR, &map);
	if (!user_result)
		return 0;

	memcpy(user_result, &data, sizeof(data));

	kbase_vunmap(kctx, &map);

	/* Atom was fine - mark it as done */
	katom->event_code = BASE_JD_EVENT_DONE;

	return 0;
}

#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
/* Called by the explicit fence mechanism when a fence wait has completed */
void kbase_soft_event_wait_callback(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	mutex_lock(&kctx->jctx.lock);
	kbasep_remove_waiting_soft_job(katom);
	kbase_finish_soft_job(katom);
	if (jd_done_nolock(katom, NULL))
		kbase_js_sched_all(kctx->kbdev);
	mutex_unlock(&kctx->jctx.lock);
}
#endif

static void kbasep_soft_event_complete_job(struct work_struct *work)
{
	struct kbase_jd_atom *katom = container_of(work, struct kbase_jd_atom,
			work);
	struct kbase_context *kctx = katom->kctx;
	int resched;

	mutex_lock(&kctx->jctx.lock);
	resched = jd_done_nolock(katom, NULL);
	mutex_unlock(&kctx->jctx.lock);

	if (resched)
		kbase_js_sched_all(kctx->kbdev);
}

void kbasep_complete_triggered_soft_events(struct kbase_context *kctx, u64 evt)
{
	int cancel_timer = 1;
	struct list_head *entry, *tmp;
	unsigned long lflags;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		struct kbase_jd_atom *katom = list_entry(
				entry, struct kbase_jd_atom, queue);

		switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
		case BASE_JD_REQ_SOFT_EVENT_WAIT:
			if (katom->jc == evt) {
				list_del(&katom->queue);

				katom->event_code = BASE_JD_EVENT_DONE;
				INIT_WORK(&katom->work,
					  kbasep_soft_event_complete_job);
				queue_work(kctx->jctx.job_done_wq,
					   &katom->work);
			} else {
				/* There are still other waiting jobs, we cannot
				 * cancel the timer yet.
				 */
				cancel_timer = 0;
			}
			break;
#ifdef CONFIG_MALI_FENCE_DEBUG
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			/* Keep the timer running if fence debug is enabled and
			 * there are waiting fence jobs.
			 */
			cancel_timer = 0;
			break;
#endif
		}
	}

	if (cancel_timer)
		del_timer(&kctx->soft_job_timeout);
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

#ifdef CONFIG_MALI_FENCE_DEBUG
static void kbase_fence_debug_check_atom(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct device *dev = kctx->kbdev->dev;
	int i;

	for (i = 0; i < 2; i++) {
		struct kbase_jd_atom *dep;

		list_for_each_entry(dep, &katom->dep_head[i], dep_item[i]) {
			if (dep->status == KBASE_JD_ATOM_STATE_UNUSED ||
			    dep->status == KBASE_JD_ATOM_STATE_COMPLETED)
				continue;

			if ((dep->core_req & BASE_JD_REQ_SOFT_JOB_TYPE)
					== BASE_JD_REQ_SOFT_FENCE_TRIGGER) {
				/* Found blocked trigger fence. */
				struct kbase_sync_fence_info info;

				if (!kbase_sync_fence_in_info_get(dep, &info)) {
					dev_warn(dev,
						 "\tVictim trigger atom %d fence [%p] %s: %s\n",
						 kbase_jd_atom_id(kctx, dep),
						 info.fence,
						 info.name,
						 kbase_sync_status_string(info.status));
				 }
			}

			kbase_fence_debug_check_atom(dep);
		}
	}
}

static void kbase_fence_debug_wait_timeout(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct device *dev = katom->kctx->kbdev->dev;
	int timeout_ms = atomic_read(&kctx->kbdev->js_data.soft_job_timeout_ms);
	unsigned long lflags;
	struct kbase_sync_fence_info info;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);

	if (kbase_sync_fence_in_info_get(katom, &info)) {
		/* Fence must have signaled just after timeout. */
		spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
		return;
	}

	dev_warn(dev, "ctx %d_%d: Atom %d still waiting for fence [%p] after %dms\n",
		 kctx->tgid, kctx->id,
		 kbase_jd_atom_id(kctx, katom),
		 info.fence, timeout_ms);
	dev_warn(dev, "\tGuilty fence [%p] %s: %s\n",
		 info.fence, info.name,
		 kbase_sync_status_string(info.status));

	/* Search for blocked trigger atoms */
	kbase_fence_debug_check_atom(katom);

	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);

	kbase_sync_fence_in_dump(katom);
}

struct kbase_fence_debug_work {
	struct kbase_jd_atom *katom;
	struct work_struct work;
};

static void kbase_fence_debug_wait_timeout_worker(struct work_struct *work)
{
	struct kbase_fence_debug_work *w = container_of(work,
			struct kbase_fence_debug_work, work);
	struct kbase_jd_atom *katom = w->katom;
	struct kbase_context *kctx = katom->kctx;

	mutex_lock(&kctx->jctx.lock);
	kbase_fence_debug_wait_timeout(katom);
	mutex_unlock(&kctx->jctx.lock);

	kfree(w);
}

static void kbase_fence_debug_timeout(struct kbase_jd_atom *katom)
{
	struct kbase_fence_debug_work *work;
	struct kbase_context *kctx = katom->kctx;

	/* Enqueue fence debug worker. Use job_done_wq to get
	 * debug print ordered with job completion.
	 */
	work = kzalloc(sizeof(struct kbase_fence_debug_work), GFP_ATOMIC);
	/* Ignore allocation failure. */
	if (work) {
		work->katom = katom;
		INIT_WORK(&work->work, kbase_fence_debug_wait_timeout_worker);
		queue_work(kctx->jctx.job_done_wq, &work->work);
	}
}
#endif /* CONFIG_MALI_FENCE_DEBUG */

void kbasep_soft_job_timeout_worker(struct timer_list *t)
{
	struct kbase_context *kctx = from_timer(kctx, t, soft_job_timeout);
	u32 timeout_ms = (u32)atomic_read(
			&kctx->kbdev->js_data.soft_job_timeout_ms);
	struct timer_list *timer = &kctx->soft_job_timeout;
	ktime_t cur_time = ktime_get();
	bool restarting = false;
	unsigned long lflags;
	struct list_head *entry, *tmp;

	spin_lock_irqsave(&kctx->waiting_soft_jobs_lock, lflags);
	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		struct kbase_jd_atom *katom = list_entry(entry,
				struct kbase_jd_atom, queue);
		s64 elapsed_time = ktime_to_ms(ktime_sub(cur_time,
					katom->start_timestamp));

		if (elapsed_time < (s64)timeout_ms) {
			restarting = true;
			continue;
		}

		switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
		case BASE_JD_REQ_SOFT_EVENT_WAIT:
			/* Take it out of the list to ensure that it
			 * will be cancelled in all cases
			 */
			list_del(&katom->queue);

			katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
			INIT_WORK(&katom->work, kbasep_soft_event_complete_job);
			queue_work(kctx->jctx.job_done_wq, &katom->work);
			break;
#ifdef CONFIG_MALI_FENCE_DEBUG
		case BASE_JD_REQ_SOFT_FENCE_WAIT:
			kbase_fence_debug_timeout(katom);
			break;
#endif
		}
	}

	if (restarting)
		mod_timer(timer, jiffies + msecs_to_jiffies(timeout_ms));
	spin_unlock_irqrestore(&kctx->waiting_soft_jobs_lock, lflags);
}

static int kbasep_soft_event_wait(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	unsigned char status;

	/* The status of this soft-job is stored in jc */
	if (kbasep_read_soft_event_status(kctx, katom->jc, &status)) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		return 0;
	}

	if (status == BASE_JD_SOFT_EVENT_SET)
		return 0; /* Event already set, nothing to do */

	kbasep_add_waiting_with_timeout(katom);

	return 1;
}

static void kbasep_soft_event_update_locked(struct kbase_jd_atom *katom,
				     unsigned char new_status)
{
	/* Complete jobs waiting on the same event */
	struct kbase_context *kctx = katom->kctx;

	if (kbasep_write_soft_event_status(kctx, katom->jc, new_status) != 0) {
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		return;
	}

	if (new_status == BASE_JD_SOFT_EVENT_SET)
		kbasep_complete_triggered_soft_events(kctx, katom->jc);
}

/**
 * kbase_soft_event_update() - Update soft event state
 * @kctx: Pointer to context
 * @event: Event to update
 * @new_status: New status value of event
 *
 * Update the event, and wake up any atoms waiting for the event.
 *
 * Return: 0 on success, a negative error code on failure.
 */
int kbase_soft_event_update(struct kbase_context *kctx,
			     u64 event,
			     unsigned char new_status)
{
	int err = 0;

	mutex_lock(&kctx->jctx.lock);

	if (kbasep_write_soft_event_status(kctx, event, new_status)) {
		err = -ENOENT;
		goto out;
	}

	if (new_status == BASE_JD_SOFT_EVENT_SET)
		kbasep_complete_triggered_soft_events(kctx, event);

out:
	mutex_unlock(&kctx->jctx.lock);

	return err;
}

static void kbasep_soft_event_cancel_job(struct kbase_jd_atom *katom)
{
	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
	if (jd_done_nolock(katom, NULL))
		kbase_js_sched_all(katom->kctx->kbdev);
}

struct kbase_debug_copy_buffer {
	size_t size;
	struct page **pages;
	int nr_pages;
	size_t offset;
	struct kbase_mem_phy_alloc *gpu_alloc;

	struct page **extres_pages;
	int nr_extres_pages;
};

static inline void free_user_buffer(struct kbase_debug_copy_buffer *buffer)
{
	struct page **pages = buffer->extres_pages;
	int nr_pages = buffer->nr_extres_pages;

	if (pages) {
		int i;

		for (i = 0; i < nr_pages; i++) {
			struct page *pg = pages[i];

			if (pg)
				put_page(pg);
		}
		kfree(pages);
	}
}

static void kbase_debug_copy_finish(struct kbase_jd_atom *katom)
{
	struct kbase_debug_copy_buffer *buffers =
			(struct kbase_debug_copy_buffer *)(uintptr_t)katom->jc;
	unsigned int i;
	unsigned int nr = katom->nr_extres;

	if (!buffers)
		return;

	kbase_gpu_vm_lock(katom->kctx);
	for (i = 0; i < nr; i++) {
		int p;
		struct kbase_mem_phy_alloc *gpu_alloc = buffers[i].gpu_alloc;

		if (!buffers[i].pages)
			break;
		for (p = 0; p < buffers[i].nr_pages; p++) {
			struct page *pg = buffers[i].pages[p];

			if (pg)
				put_page(pg);
		}
		kfree(buffers[i].pages);
		if (gpu_alloc) {
			switch (gpu_alloc->type) {
			case KBASE_MEM_TYPE_IMPORTED_USER_BUF:
			{
				free_user_buffer(&buffers[i]);
				break;
			}
			default:
				/* Nothing to be done. */
				break;
			}
			kbase_mem_phy_alloc_put(gpu_alloc);
		}
	}
	kbase_gpu_vm_unlock(katom->kctx);
	kfree(buffers);

	katom->jc = 0;
}

static int kbase_debug_copy_prepare(struct kbase_jd_atom *katom)
{
	struct kbase_debug_copy_buffer *buffers;
	struct base_jd_debug_copy_buffer *user_buffers = NULL;
	unsigned int i;
	unsigned int nr = katom->nr_extres;
	int ret = 0;
	void __user *user_structs = (void __user *)(uintptr_t)katom->jc;

	if (!user_structs)
		return -EINVAL;

	buffers = kcalloc(nr, sizeof(*buffers), GFP_KERNEL);
	if (!buffers) {
		ret = -ENOMEM;
		katom->jc = 0;
		goto out_cleanup;
	}
	katom->jc = (u64)(uintptr_t)buffers;

	user_buffers = kmalloc_array(nr, sizeof(*user_buffers), GFP_KERNEL);

	if (!user_buffers) {
		ret = -ENOMEM;
		goto out_cleanup;
	}

	ret = copy_from_user(user_buffers, user_structs,
			sizeof(*user_buffers)*nr);
	if (ret)
		goto out_cleanup;

	for (i = 0; i < nr; i++) {
		u64 addr = user_buffers[i].address;
		u64 page_addr = addr & PAGE_MASK;
		u64 end_page_addr = addr + user_buffers[i].size - 1;
		u64 last_page_addr = end_page_addr & PAGE_MASK;
		int nr_pages = (last_page_addr-page_addr)/PAGE_SIZE+1;
		int pinned_pages;
		struct kbase_va_region *reg;
		struct base_external_resource user_extres;

		if (!addr)
			continue;

		buffers[i].nr_pages = nr_pages;
		buffers[i].offset = addr & ~PAGE_MASK;
		if (buffers[i].offset >= PAGE_SIZE) {
			ret = -EINVAL;
			goto out_cleanup;
		}
		buffers[i].size = user_buffers[i].size;

		buffers[i].pages = kcalloc(nr_pages, sizeof(struct page *),
				GFP_KERNEL);
		if (!buffers[i].pages) {
			ret = -ENOMEM;
			goto out_cleanup;
		}

		pinned_pages = get_user_pages_fast(page_addr,
					nr_pages,
					1, /* Write */
					buffers[i].pages);
		if (pinned_pages < 0) {
			ret = pinned_pages;
			goto out_cleanup;
		}
		if (pinned_pages != nr_pages) {
			ret = -EINVAL;
			goto out_cleanup;
		}

		user_extres = user_buffers[i].extres;
		if (user_extres.ext_resource == 0ULL) {
			ret = -EINVAL;
			goto out_cleanup;
		}

		kbase_gpu_vm_lock(katom->kctx);
		reg = kbase_region_tracker_find_region_enclosing_address(
				katom->kctx, user_extres.ext_resource &
				~BASE_EXT_RES_ACCESS_EXCLUSIVE);

		if (NULL == reg || NULL == reg->gpu_alloc ||
				(reg->flags & KBASE_REG_FREE)) {
			ret = -EINVAL;
			goto out_unlock;
		}

		buffers[i].gpu_alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);
		buffers[i].nr_extres_pages = reg->nr_pages;

		if (reg->nr_pages*PAGE_SIZE != buffers[i].size)
			dev_warn(katom->kctx->kbdev->dev, "Copy buffer is not of same size as the external resource to copy.\n");

		switch (reg->gpu_alloc->type) {
		case KBASE_MEM_TYPE_IMPORTED_USER_BUF:
		{
			struct kbase_mem_phy_alloc *alloc = reg->gpu_alloc;
			unsigned long nr_pages =
				alloc->imported.user_buf.nr_pages;

			if (alloc->imported.user_buf.mm != current->mm) {
				ret = -EINVAL;
				goto out_unlock;
			}
			buffers[i].extres_pages = kcalloc(nr_pages,
					sizeof(struct page *), GFP_KERNEL);
			if (!buffers[i].extres_pages) {
				ret = -ENOMEM;
				goto out_unlock;
			}

			ret = get_user_pages_fast(
					alloc->imported.user_buf.address,
					nr_pages, 0,
					buffers[i].extres_pages);
			if (ret != nr_pages)
				goto out_unlock;
			ret = 0;
			break;
		}
		case KBASE_MEM_TYPE_IMPORTED_UMP:
		{
			dev_warn(katom->kctx->kbdev->dev,
					"UMP is not supported for debug_copy jobs\n");
			ret = -EINVAL;
			goto out_unlock;
		}
		default:
			/* Nothing to be done. */
			break;
		}
		kbase_gpu_vm_unlock(katom->kctx);
	}
	kfree(user_buffers);

	return ret;

out_unlock:
	kbase_gpu_vm_unlock(katom->kctx);

out_cleanup:
	kfree(buffers);
	kfree(user_buffers);

	/* Frees allocated memory for kbase_debug_copy_job struct, including
	 * members, and sets jc to 0 */
	kbase_debug_copy_finish(katom);
	return ret;
}

static void kbase_mem_copy_from_extres_page(struct kbase_context *kctx,
		void *extres_page, struct page **pages, unsigned int nr_pages,
		unsigned int *target_page_nr, size_t offset, size_t *to_copy)
{
	void *target_page = kmap(pages[*target_page_nr]);
	size_t chunk = PAGE_SIZE-offset;

	lockdep_assert_held(&kctx->reg_lock);

	if (!target_page) {
		*target_page_nr += 1;
		dev_warn(kctx->kbdev->dev, "kmap failed in debug_copy job.");
		return;
	}

	chunk = min(chunk, *to_copy);

	memcpy(target_page + offset, extres_page, chunk);
	*to_copy -= chunk;

	kunmap(pages[*target_page_nr]);

	*target_page_nr += 1;
	if (*target_page_nr >= nr_pages)
		return;

	target_page = kmap(pages[*target_page_nr]);
	if (!target_page) {
		*target_page_nr += 1;
		dev_warn(kctx->kbdev->dev, "kmap failed in debug_copy job.");
		return;
	}

	KBASE_DEBUG_ASSERT(target_page);

	chunk = min(offset, *to_copy);
	memcpy(target_page, extres_page + PAGE_SIZE-offset, chunk);
	*to_copy -= chunk;

	kunmap(pages[*target_page_nr]);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static void *dma_buf_kmap_page(struct kbase_mem_phy_alloc *gpu_alloc,
	unsigned long page_num, struct page **page)
{
	struct sg_table *sgt = gpu_alloc->imported.umm.sgt;
	struct sg_page_iter sg_iter;
	unsigned long page_index = 0;

	if (WARN_ON(gpu_alloc->type != KBASE_MEM_TYPE_IMPORTED_UMM))
		return NULL;

	if (!sgt)
		return NULL;

	if (WARN_ON(page_num >= gpu_alloc->nents))
		return NULL;

	for_each_sg_page(sgt->sgl, &sg_iter, sgt->nents, 0) {
		if (page_index == page_num) {
			*page = sg_page_iter_page(&sg_iter);

			return kmap(*page);
		}
		page_index++;
	}

	return NULL;
}
#endif

static int kbase_mem_copy_from_extres(struct kbase_context *kctx,
		struct kbase_debug_copy_buffer *buf_data)
{
	unsigned int i;
	unsigned int target_page_nr = 0;
	struct page **pages = buf_data->pages;
	u64 offset = buf_data->offset;
	size_t extres_size = buf_data->nr_extres_pages*PAGE_SIZE;
	size_t to_copy = min(extres_size, buf_data->size);
	struct kbase_mem_phy_alloc *gpu_alloc = buf_data->gpu_alloc;
	int ret = 0;

	KBASE_DEBUG_ASSERT(pages != NULL);

	kbase_gpu_vm_lock(kctx);
	if (!gpu_alloc) {
		ret = -EINVAL;
		goto out_unlock;
	}

	switch (gpu_alloc->type) {
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF:
	{
		for (i = 0; i < buf_data->nr_extres_pages; i++) {
			struct page *pg = buf_data->extres_pages[i];
			void *extres_page = kmap(pg);

			if (extres_page)
				kbase_mem_copy_from_extres_page(kctx,
						extres_page, pages,
						buf_data->nr_pages,
						&target_page_nr,
						offset, &to_copy);

			kunmap(pg);
			if (target_page_nr >= buf_data->nr_pages)
				break;
		}
		break;
	}
	break;
#ifdef CONFIG_DMA_SHARED_BUFFER
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		struct dma_buf *dma_buf = gpu_alloc->imported.umm.dma_buf;

		KBASE_DEBUG_ASSERT(dma_buf != NULL);
		KBASE_DEBUG_ASSERT(dma_buf->size ==
				   buf_data->nr_extres_pages * PAGE_SIZE);

		ret = dma_buf_begin_cpu_access(dma_buf,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0) && !defined(CONFIG_CHROMEOS)
				0, buf_data->nr_extres_pages*PAGE_SIZE,
#endif
				DMA_FROM_DEVICE);
		if (ret)
			goto out_unlock;

		for (i = 0; i < buf_data->nr_extres_pages; i++) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
			struct page *pg;
			void *extres_page = dma_buf_kmap_page(gpu_alloc, i, &pg);
#else
			void *extres_page = dma_buf_kmap(dma_buf, i);
#endif

			if (extres_page)
				kbase_mem_copy_from_extres_page(kctx,
						extres_page, pages,
						buf_data->nr_pages,
						&target_page_nr,
						offset, &to_copy);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
			kunmap(pg);
#else
			dma_buf_kunmap(dma_buf, i, extres_page);
#endif
			if (target_page_nr >= buf_data->nr_pages)
				break;
		}
		dma_buf_end_cpu_access(dma_buf,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0) && !defined(CONFIG_CHROMEOS)
				0, buf_data->nr_extres_pages*PAGE_SIZE,
#endif
				DMA_FROM_DEVICE);
		break;
	}
#endif
	default:
		ret = -EINVAL;
	}
out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return ret;

}

static int kbase_debug_copy(struct kbase_jd_atom *katom)
{
	struct kbase_debug_copy_buffer *buffers =
			(struct kbase_debug_copy_buffer *)(uintptr_t)katom->jc;
	unsigned int i;

	for (i = 0; i < katom->nr_extres; i++) {
		int res = kbase_mem_copy_from_extres(katom->kctx, &buffers[i]);

		if (res)
			return res;
	}

	return 0;
}

static int kbase_jit_allocate_prepare(struct kbase_jd_atom *katom)
{
	__user void *data = (__user void *)(uintptr_t) katom->jc;
	struct base_jit_alloc_info *info;
	struct kbase_context *kctx = katom->kctx;
	int ret;

	/* Fail the job if there is no info structure */
	if (!data) {
		ret = -EINVAL;
		goto fail;
	}

	/* Copy the information for safe access and future storage */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto fail;
	}

	if (copy_from_user(info, data, sizeof(*info)) != 0) {
		ret = -EINVAL;
		goto free_info;
	}

	/* If the ID is zero then fail the job */
	if (info->id == 0) {
		ret = -EINVAL;
		goto free_info;
	}

	/* Sanity check that the PA fits within the VA */
	if (info->va_pages < info->commit_pages) {
		ret = -EINVAL;
		goto free_info;
	}

	/* Ensure the GPU address is correctly aligned */
	if ((info->gpu_alloc_addr & 0x7) != 0) {
		ret = -EINVAL;
		goto free_info;
	}

	/* Replace the user pointer with our kernel allocated info structure */
	katom->jc = (u64)(uintptr_t) info;
	katom->jit_blocked = false;

	lockdep_assert_held(&kctx->jctx.lock);
	list_add_tail(&katom->jit_node, &kctx->jit_atoms_head);

	/*
	 * Note:
	 * The provided info->gpu_alloc_addr isn't validated here as
	 * userland can cache allocations which means that even
	 * though the region is valid it doesn't represent the
	 * same thing it used to.
	 *
	 * Complete validation of va_pages, commit_pages and extent
	 * isn't done here as it will be done during the call to
	 * kbase_mem_alloc.
	 */
	return 0;

free_info:
	kfree(info);
fail:
	katom->jc = 0;
	return ret;
}

static u8 kbase_jit_free_get_id(struct kbase_jd_atom *katom)
{
	if (WARN_ON(katom->core_req != BASE_JD_REQ_SOFT_JIT_FREE))
		return 0;

	return (u8) katom->jc;
}

static int kbase_jit_allocate_process(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct base_jit_alloc_info *info;
	struct kbase_va_region *reg;
	struct kbase_vmap_struct mapping;
	u64 *ptr, new_addr;

	if (katom->jit_blocked) {
		list_del(&katom->queue);
		katom->jit_blocked = false;
	}

	info = (struct base_jit_alloc_info *) (uintptr_t) katom->jc;

	/* The JIT ID is still in use so fail the allocation */
	if (kctx->jit_alloc[info->id]) {
		katom->event_code = BASE_JD_EVENT_MEM_GROWTH_FAILED;
		return 0;
	}

	/* Create a JIT allocation */
	reg = kbase_jit_allocate(kctx, info);
	if (!reg) {
		struct kbase_jd_atom *jit_atom;
		bool can_block = false;

		lockdep_assert_held(&kctx->jctx.lock);

		jit_atom = list_first_entry(&kctx->jit_atoms_head,
				struct kbase_jd_atom, jit_node);

		list_for_each_entry(jit_atom, &kctx->jit_atoms_head, jit_node) {
			if (jit_atom == katom)
				break;
			if (jit_atom->core_req == BASE_JD_REQ_SOFT_JIT_FREE) {
				u8 free_id = kbase_jit_free_get_id(jit_atom);

				if (free_id && kctx->jit_alloc[free_id]) {
					/* A JIT free which is active and
					 * submitted before this atom
					 */
					can_block = true;
					break;
				}
			}
		}

		if (!can_block) {
			/* Mark the allocation so we know it's in use even if
			 * the allocation itself fails.
			 */
			kctx->jit_alloc[info->id] =
				(struct kbase_va_region *) -1;

			katom->event_code = BASE_JD_EVENT_MEM_GROWTH_FAILED;
			return 0;
		}

		/* There are pending frees for an active allocation
		 * so we should wait to see whether they free the memory.
		 * Add to the beginning of the list to ensure that the atom is
		 * processed only once in kbase_jit_free_finish
		 */
		list_add(&katom->queue, &kctx->jit_pending_alloc);
		katom->jit_blocked = true;

		return 1;
	}

	/*
	 * Write the address of the JIT allocation to the user provided
	 * GPU allocation.
	 */
	ptr = kbase_vmap(kctx, info->gpu_alloc_addr, sizeof(*ptr),
			&mapping);
	if (!ptr) {
		/*
		 * Leave the allocation "live" as the JIT free jit will be
		 * submitted anyway.
		 */
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return 0;
	}

	new_addr = reg->start_pfn << PAGE_SHIFT;
	*ptr = new_addr;
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_JIT(
			katom, info->gpu_alloc_addr, new_addr);
	kbase_vunmap(kctx, &mapping);

	katom->event_code = BASE_JD_EVENT_DONE;

	/*
	 * Bind it to the user provided ID. Do this last so we can check for
	 * the JIT free racing this JIT alloc job.
	 */
	kctx->jit_alloc[info->id] = reg;

	return 0;
}

static void kbase_jit_allocate_finish(struct kbase_jd_atom *katom)
{
	struct base_jit_alloc_info *info;

	lockdep_assert_held(&katom->kctx->jctx.lock);

	/* Remove atom from jit_atoms_head list */
	list_del(&katom->jit_node);

	if (katom->jit_blocked) {
		list_del(&katom->queue);
		katom->jit_blocked = false;
	}

	info = (struct base_jit_alloc_info *) (uintptr_t) katom->jc;
	/* Free the info structure */
	kfree(info);
}

static int kbase_jit_free_prepare(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	lockdep_assert_held(&kctx->jctx.lock);
	list_add_tail(&katom->jit_node, &kctx->jit_atoms_head);

	return 0;
}

static void kbase_jit_free_process(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	u8 id = kbase_jit_free_get_id(katom);

	/*
	 * If the ID is zero or it is not in use yet then fail the job.
	 */
	if ((id == 0) || (kctx->jit_alloc[id] == NULL)) {
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		return;
	}

	/*
	 * If the ID is valid but the allocation request failed still succeed
	 * this soft job but don't try and free the allocation.
	 */
	if (kctx->jit_alloc[id] != (struct kbase_va_region *) -1)
		kbase_jit_free(kctx, kctx->jit_alloc[id]);

	kctx->jit_alloc[id] = NULL;
}

static void kbasep_jit_free_finish_worker(struct work_struct *work)
{
	struct kbase_jd_atom *katom = container_of(work, struct kbase_jd_atom,
			work);
	struct kbase_context *kctx = katom->kctx;
	int resched;

	mutex_lock(&kctx->jctx.lock);
	kbase_finish_soft_job(katom);
	resched = jd_done_nolock(katom, NULL);
	mutex_unlock(&kctx->jctx.lock);

	if (resched)
		kbase_js_sched_all(kctx->kbdev);
}

static void kbase_jit_free_finish(struct kbase_jd_atom *katom)
{
	struct list_head *i, *tmp;
	struct kbase_context *kctx = katom->kctx;

	lockdep_assert_held(&kctx->jctx.lock);
	/* Remove this atom from the kctx->jit_atoms_head list */
	list_del(&katom->jit_node);

	list_for_each_safe(i, tmp, &kctx->jit_pending_alloc) {
		struct kbase_jd_atom *pending_atom = list_entry(i,
				struct kbase_jd_atom, queue);
		if (kbase_jit_allocate_process(pending_atom) == 0) {
			/* Atom has completed */
			INIT_WORK(&pending_atom->work,
					kbasep_jit_free_finish_worker);
			queue_work(kctx->jctx.job_done_wq, &pending_atom->work);
		}
	}
}

static int kbase_ext_res_prepare(struct kbase_jd_atom *katom)
{
	__user struct base_external_resource_list *user_ext_res;
	struct base_external_resource_list *ext_res;
	u64 count = 0;
	size_t copy_size;
	int ret;

	user_ext_res = (__user struct base_external_resource_list *)
			(uintptr_t) katom->jc;

	/* Fail the job if there is no info structure */
	if (!user_ext_res) {
		ret = -EINVAL;
		goto fail;
	}

	if (copy_from_user(&count, &user_ext_res->count, sizeof(u64)) != 0) {
		ret = -EINVAL;
		goto fail;
	}

	/* Is the number of external resources in range? */
	if (!count || count > BASE_EXT_RES_COUNT_MAX) {
		ret = -EINVAL;
		goto fail;
	}

	/* Copy the information for safe access and future storage */
	copy_size = sizeof(*ext_res);
	copy_size += sizeof(struct base_external_resource) * (count - 1);
	ext_res = kzalloc(copy_size, GFP_KERNEL);
	if (!ext_res) {
		ret = -ENOMEM;
		goto fail;
	}

	if (copy_from_user(ext_res, user_ext_res, copy_size) != 0) {
		ret = -EINVAL;
		goto free_info;
	}

	/*
	 * Overwrite the count with the first value incase it was changed
	 * after the fact.
	 */
	ext_res->count = count;

	/*
	 * Replace the user pointer with our kernel allocated
	 * ext_res structure.
	 */
	katom->jc = (u64)(uintptr_t) ext_res;

	return 0;

free_info:
	kfree(ext_res);
fail:
	return ret;
}

static void kbase_ext_res_process(struct kbase_jd_atom *katom, bool map)
{
	struct base_external_resource_list *ext_res;
	int i;
	bool failed = false;

	ext_res = (struct base_external_resource_list *) (uintptr_t) katom->jc;
	if (!ext_res)
		goto failed_jc;

	kbase_gpu_vm_lock(katom->kctx);

	for (i = 0; i < ext_res->count; i++) {
		u64 gpu_addr;

		gpu_addr = ext_res->ext_res[i].ext_resource &
				~BASE_EXT_RES_ACCESS_EXCLUSIVE;
		if (map) {
			if (!kbase_sticky_resource_acquire(katom->kctx,
					gpu_addr))
				goto failed_loop;
		} else
			if (!kbase_sticky_resource_release(katom->kctx, NULL,
					gpu_addr))
				failed = true;
	}

	/*
	 * In the case of unmap we continue unmapping other resources in the
	 * case of failure but will always report failure if _any_ unmap
	 * request fails.
	 */
	if (failed)
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
	else
		katom->event_code = BASE_JD_EVENT_DONE;

	kbase_gpu_vm_unlock(katom->kctx);

	return;

failed_loop:
	while (--i > 0) {
		u64 gpu_addr;

		gpu_addr = ext_res->ext_res[i].ext_resource &
				~BASE_EXT_RES_ACCESS_EXCLUSIVE;

		kbase_sticky_resource_release(katom->kctx, NULL, gpu_addr);
	}

	katom->event_code = BASE_JD_EVENT_JOB_INVALID;
	kbase_gpu_vm_unlock(katom->kctx);

failed_jc:
	return;
}

static void kbase_ext_res_finish(struct kbase_jd_atom *katom)
{
	struct base_external_resource_list *ext_res;

	ext_res = (struct base_external_resource_list *) (uintptr_t) katom->jc;
	/* Free the info structure */
	kfree(ext_res);
}

int kbase_process_soft_job(struct kbase_jd_atom *katom)
{
	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
		return kbase_dump_cpu_gpu_time(katom);

#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		katom->event_code = kbase_sync_fence_out_trigger(katom,
				katom->event_code == BASE_JD_EVENT_DONE ?
								0 : -EFAULT);
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
	{
		int ret = kbase_sync_fence_in_wait(katom);

		if (ret == 1) {
#ifdef CONFIG_MALI_FENCE_DEBUG
			kbasep_add_waiting_with_timeout(katom);
#else
			kbasep_add_waiting_soft_job(katom);
#endif
		}
		return ret;
	}
#endif

	case BASE_JD_REQ_SOFT_REPLAY:
		return kbase_replay_process(katom);
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
		return kbasep_soft_event_wait(katom);
	case BASE_JD_REQ_SOFT_EVENT_SET:
		kbasep_soft_event_update_locked(katom, BASE_JD_SOFT_EVENT_SET);
		break;
	case BASE_JD_REQ_SOFT_EVENT_RESET:
		kbasep_soft_event_update_locked(katom, BASE_JD_SOFT_EVENT_RESET);
		break;
	case BASE_JD_REQ_SOFT_DEBUG_COPY:
	{
		int res = kbase_debug_copy(katom);

		if (res)
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		break;
	}
	case BASE_JD_REQ_SOFT_JIT_ALLOC:
		return kbase_jit_allocate_process(katom);
	case BASE_JD_REQ_SOFT_JIT_FREE:
		kbase_jit_free_process(katom);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_MAP:
		kbase_ext_res_process(katom, true);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_UNMAP:
		kbase_ext_res_process(katom, false);
		break;
	}

	/* Atom is complete */
	return 0;
}

void kbase_cancel_soft_job(struct kbase_jd_atom *katom)
{
	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		kbase_sync_fence_in_cancel_wait(katom);
		break;
#endif
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
		kbasep_soft_event_cancel_job(katom);
		break;
	default:
		/* This soft-job doesn't support cancellation! */
		KBASE_DEBUG_ASSERT(0);
	}
}

int kbase_prepare_soft_job(struct kbase_jd_atom *katom)
{
	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
		{
			if (0 != (katom->jc & KBASE_CACHE_ALIGNMENT_MASK))
				return -EINVAL;
		}
		break;
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		{
			struct base_fence fence;
			int fd;

			if (0 != copy_from_user(&fence, (__user void *)(uintptr_t) katom->jc, sizeof(fence)))
				return -EINVAL;

			fd = kbase_sync_fence_out_create(katom,
							 fence.basep.stream_fd);
			if (fd < 0)
				return -EINVAL;

			fence.basep.fd = fd;
			if (0 != copy_to_user((__user void *)(uintptr_t) katom->jc, &fence, sizeof(fence))) {
				kbase_sync_fence_out_remove(katom);
				kbase_sync_fence_close_fd(fd);
				fence.basep.fd = -EINVAL;
				return -EINVAL;
			}
		}
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		{
			struct base_fence fence;
			int ret;

			if (0 != copy_from_user(&fence, (__user void *)(uintptr_t) katom->jc, sizeof(fence)))
				return -EINVAL;

			/* Get a reference to the fence object */
			ret = kbase_sync_fence_in_from_fd(katom,
							  fence.basep.fd);
			if (ret < 0)
				return ret;

#ifdef CONFIG_MALI_DMA_FENCE
			/*
			 * Set KCTX_NO_IMPLICIT_FENCE in the context the first
			 * time a soft fence wait job is observed. This will
			 * prevent the implicit dma-buf fence to conflict with
			 * the Android native sync fences.
			 */
			if (!kbase_ctx_flag(katom->kctx, KCTX_NO_IMPLICIT_SYNC))
				kbase_ctx_flag_set(katom->kctx, KCTX_NO_IMPLICIT_SYNC);
#endif /* CONFIG_MALI_DMA_FENCE */
		}
		break;
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */
	case BASE_JD_REQ_SOFT_JIT_ALLOC:
		return kbase_jit_allocate_prepare(katom);
	case BASE_JD_REQ_SOFT_REPLAY:
		break;
	case BASE_JD_REQ_SOFT_JIT_FREE:
		return kbase_jit_free_prepare(katom);
	case BASE_JD_REQ_SOFT_EVENT_WAIT:
	case BASE_JD_REQ_SOFT_EVENT_SET:
	case BASE_JD_REQ_SOFT_EVENT_RESET:
		if (katom->jc == 0)
			return -EINVAL;
		break;
	case BASE_JD_REQ_SOFT_DEBUG_COPY:
		return kbase_debug_copy_prepare(katom);
	case BASE_JD_REQ_SOFT_EXT_RES_MAP:
		return kbase_ext_res_prepare(katom);
	case BASE_JD_REQ_SOFT_EXT_RES_UNMAP:
		return kbase_ext_res_prepare(katom);
	default:
		/* Unsupported soft-job */
		return -EINVAL;
	}
	return 0;
}

void kbase_finish_soft_job(struct kbase_jd_atom *katom)
{
	switch (katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) {
	case BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME:
		/* Nothing to do */
		break;
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	case BASE_JD_REQ_SOFT_FENCE_TRIGGER:
		/* If fence has not yet been signaled, do it now */
		kbase_sync_fence_out_trigger(katom, katom->event_code ==
				BASE_JD_EVENT_DONE ? 0 : -EFAULT);
		break;
	case BASE_JD_REQ_SOFT_FENCE_WAIT:
		/* Release katom's reference to fence object */
		kbase_sync_fence_in_remove(katom);
		break;
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */
	case BASE_JD_REQ_SOFT_DEBUG_COPY:
		kbase_debug_copy_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_JIT_ALLOC:
		kbase_jit_allocate_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_MAP:
		kbase_ext_res_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_EXT_RES_UNMAP:
		kbase_ext_res_finish(katom);
		break;
	case BASE_JD_REQ_SOFT_JIT_FREE:
		kbase_jit_free_finish(katom);
		break;
	}
}

void kbase_resume_suspended_soft_jobs(struct kbase_device *kbdev)
{
	LIST_HEAD(local_suspended_soft_jobs);
	struct kbase_jd_atom *tmp_iter;
	struct kbase_jd_atom *katom_iter;
	struct kbasep_js_device_data *js_devdata;
	bool resched = false;

	KBASE_DEBUG_ASSERT(kbdev);

	js_devdata = &kbdev->js_data;

	/* Move out the entire list */
	mutex_lock(&js_devdata->runpool_mutex);
	list_splice_init(&js_devdata->suspended_soft_jobs_list,
			&local_suspended_soft_jobs);
	mutex_unlock(&js_devdata->runpool_mutex);

	/*
	 * Each atom must be detached from the list and ran separately -
	 * it could be re-added to the old list, but this is unlikely
	 */
	list_for_each_entry_safe(katom_iter, tmp_iter,
			&local_suspended_soft_jobs, dep_item[1]) {
		struct kbase_context *kctx = katom_iter->kctx;

		mutex_lock(&kctx->jctx.lock);

		/* Remove from the global list */
		list_del(&katom_iter->dep_item[1]);
		/* Remove from the context's list of waiting soft jobs */
		kbasep_remove_waiting_soft_job(katom_iter);

		if (kbase_process_soft_job(katom_iter) == 0) {
			kbase_finish_soft_job(katom_iter);
			resched |= jd_done_nolock(katom_iter, NULL);
		} else {
			KBASE_DEBUG_ASSERT((katom_iter->core_req &
					BASE_JD_REQ_SOFT_JOB_TYPE)
					!= BASE_JD_REQ_SOFT_REPLAY);
		}

		mutex_unlock(&kctx->jctx.lock);
	}

	if (resched)
		kbase_js_sched_all(kbdev);
}
