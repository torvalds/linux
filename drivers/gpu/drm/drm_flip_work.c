/*
 * Copyright (C) 2013 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/slab.h>

#include <drm/drm_flip_work.h>
#include <drm/drm_print.h>
#include <drm/drm_util.h>

/**
 * drm_flip_work_allocate_task - allocate a flip-work task
 * @data: data associated to the task
 * @flags: allocator flags
 *
 * Allocate a drm_flip_task object and attach private data to it.
 */
struct drm_flip_task *drm_flip_work_allocate_task(void *data, gfp_t flags)
{
	struct drm_flip_task *task;

	task = kzalloc(sizeof(*task), flags);
	if (task)
		task->data = data;

	return task;
}
EXPORT_SYMBOL(drm_flip_work_allocate_task);

/**
 * drm_flip_work_queue_task - queue a specific task
 * @work: the flip-work
 * @task: the task to handle
 *
 * Queues task, that will later be run (passed back to drm_flip_func_t
 * func) on a work queue after drm_flip_work_commit() is called.
 */
void drm_flip_work_queue_task(struct drm_flip_work *work,
			      struct drm_flip_task *task)
{
	unsigned long flags;

	spin_lock_irqsave(&work->lock, flags);
	list_add_tail(&task->node, &work->queued);
	spin_unlock_irqrestore(&work->lock, flags);
}
EXPORT_SYMBOL(drm_flip_work_queue_task);

/**
 * drm_flip_work_queue - queue work
 * @work: the flip-work
 * @val: the value to queue
 *
 * Queues work, that will later be run (passed back to drm_flip_func_t
 * func) on a work queue after drm_flip_work_commit() is called.
 */
void drm_flip_work_queue(struct drm_flip_work *work, void *val)
{
	struct drm_flip_task *task;

	task = drm_flip_work_allocate_task(val,
				drm_can_sleep() ? GFP_KERNEL : GFP_ATOMIC);
	if (task) {
		drm_flip_work_queue_task(work, task);
	} else {
		DRM_ERROR("%s could not allocate task!\n", work->name);
		work->func(work, val);
	}
}
EXPORT_SYMBOL(drm_flip_work_queue);

/**
 * drm_flip_work_commit - commit queued work
 * @work: the flip-work
 * @wq: the work-queue to run the queued work on
 *
 * Trigger work previously queued by drm_flip_work_queue() to run
 * on a workqueue.  The typical usage would be to queue work (via
 * drm_flip_work_queue()) at any point (from vblank irq and/or
 * prior), and then from vblank irq commit the queued work.
 */
void drm_flip_work_commit(struct drm_flip_work *work,
		struct workqueue_struct *wq)
{
	unsigned long flags;

	spin_lock_irqsave(&work->lock, flags);
	list_splice_tail(&work->queued, &work->commited);
	INIT_LIST_HEAD(&work->queued);
	spin_unlock_irqrestore(&work->lock, flags);
	queue_work(wq, &work->worker);
}
EXPORT_SYMBOL(drm_flip_work_commit);

static void flip_worker(struct work_struct *w)
{
	struct drm_flip_work *work = container_of(w, struct drm_flip_work, worker);
	struct list_head tasks;
	unsigned long flags;

	while (1) {
		struct drm_flip_task *task, *tmp;

		INIT_LIST_HEAD(&tasks);
		spin_lock_irqsave(&work->lock, flags);
		list_splice_tail(&work->commited, &tasks);
		INIT_LIST_HEAD(&work->commited);
		spin_unlock_irqrestore(&work->lock, flags);

		if (list_empty(&tasks))
			break;

		list_for_each_entry_safe(task, tmp, &tasks, node) {
			work->func(work, task->data);
			kfree(task);
		}
	}
}

/**
 * drm_flip_work_init - initialize flip-work
 * @work: the flip-work to initialize
 * @name: debug name
 * @func: the callback work function
 *
 * Initializes/allocates resources for the flip-work
 */
void drm_flip_work_init(struct drm_flip_work *work,
		const char *name, drm_flip_func_t func)
{
	work->name = name;
	INIT_LIST_HEAD(&work->queued);
	INIT_LIST_HEAD(&work->commited);
	spin_lock_init(&work->lock);
	work->func = func;

	INIT_WORK(&work->worker, flip_worker);
}
EXPORT_SYMBOL(drm_flip_work_init);

/**
 * drm_flip_work_cleanup - cleans up flip-work
 * @work: the flip-work to cleanup
 *
 * Destroy resources allocated for the flip-work
 */
void drm_flip_work_cleanup(struct drm_flip_work *work)
{
	WARN_ON(!list_empty(&work->queued) || !list_empty(&work->commited));
}
EXPORT_SYMBOL(drm_flip_work_cleanup);
