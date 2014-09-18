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

#include "drmP.h"
#include "drm_flip_work.h"

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
	if (kfifo_put(&work->fifo, val)) {
		atomic_inc(&work->pending);
	} else {
		DRM_ERROR("%s fifo full!\n", work->name);
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
	uint32_t pending = atomic_read(&work->pending);
	atomic_add(pending, &work->count);
	atomic_sub(pending, &work->pending);
	queue_work(wq, &work->worker);
}
EXPORT_SYMBOL(drm_flip_work_commit);

static void flip_worker(struct work_struct *w)
{
	struct drm_flip_work *work = container_of(w, struct drm_flip_work, worker);
	uint32_t count = atomic_read(&work->count);
	void *val = NULL;

	atomic_sub(count, &work->count);

	while(count--)
		if (!WARN_ON(!kfifo_get(&work->fifo, &val)))
			work->func(work, val);
}

/**
 * drm_flip_work_init - initialize flip-work
 * @work: the flip-work to initialize
 * @size: the max queue depth
 * @name: debug name
 * @func: the callback work function
 *
 * Initializes/allocates resources for the flip-work
 *
 * RETURNS:
 * Zero on success, error code on failure.
 */
int drm_flip_work_init(struct drm_flip_work *work, int size,
		const char *name, drm_flip_func_t func)
{
	int ret;

	work->name = name;
	atomic_set(&work->count, 0);
	atomic_set(&work->pending, 0);
	work->func = func;

	ret = kfifo_alloc(&work->fifo, size, GFP_KERNEL);
	if (ret) {
		DRM_ERROR("could not allocate %s fifo\n", name);
		return ret;
	}

	INIT_WORK(&work->worker, flip_worker);

	return 0;
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
	WARN_ON(!kfifo_is_empty(&work->fifo));
	kfifo_free(&work->fifo);
}
EXPORT_SYMBOL(drm_flip_work_cleanup);
