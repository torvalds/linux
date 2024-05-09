// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>

#include "hgsl.h"

#define HGSL_HSYNC_FINI_RETRY_COUNT 50
#define HGSL_HSYNC_FINI_RETRY_TIME_SLICE 10
#define HGSL_TIMELINE_INFINITE_TIMEOUT (~(0ULL))

static const struct dma_fence_ops hgsl_hsync_fence_ops;
static const struct dma_fence_ops hgsl_isync_fence_ops;


int hgsl_hsync_fence_create_fd(struct hgsl_context *context,
				uint32_t ts)
{
	int fence_fd;
	struct hgsl_hsync_fence *fence;

	fence_fd = get_unused_fd_flags(0);
	if (fence_fd < 0)
		return fence_fd;

	fence = hgsl_hsync_fence_create(context, ts);
	if (fence == NULL) {
		put_unused_fd(fence_fd);
		return -ENOMEM;
	}

	fd_install(fence_fd, fence->sync_file->file);

	return fence_fd;
}

struct hgsl_hsync_fence *hgsl_hsync_fence_create(
					struct hgsl_context *context,
					uint32_t ts)
{
	unsigned long flags;
	struct hgsl_hsync_timeline *timeline = context->timeline;
	struct hgsl_hsync_fence *fence;

	if (timeline == NULL)
		return NULL;

	if (!kref_get_unless_zero(&timeline->kref))
		return NULL;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL) {
		hgsl_hsync_timeline_put(timeline);
		return NULL;
	}

	fence->ts = ts;

	dma_fence_init(&fence->fence, &hgsl_hsync_fence_ops,
			&timeline->lock, timeline->fence_context, ts);

	fence->sync_file = sync_file_create(&fence->fence);
	dma_fence_put(&fence->fence);
	if (fence->sync_file == NULL) {
		hgsl_hsync_timeline_put(timeline);
		return NULL;
	}

	fence->timeline = timeline;
	spin_lock_irqsave(&timeline->lock, flags);
	list_add_tail(&fence->child_list, &timeline->fence_list);
	spin_unlock_irqrestore(&timeline->lock, flags);

	return fence;
}

void hgsl_hsync_timeline_signal(struct hgsl_hsync_timeline *timeline,
						unsigned int ts)
{
	struct hgsl_hsync_fence *cur, *next;
	unsigned long flags;

	if (!kref_get_unless_zero(&timeline->kref))
		return;

	if (hgsl_ts32_ge(timeline->last_ts, ts)) {
		hgsl_hsync_timeline_put(timeline);
		return;
	}

	spin_lock_irqsave(&timeline->lock, flags);
	timeline->last_ts = ts;
	list_for_each_entry_safe(cur, next, &timeline->fence_list,
					child_list) {
		if (dma_fence_is_signaled_locked(&cur->fence))
			list_del_init(&cur->child_list);
	}
	spin_unlock_irqrestore(&timeline->lock, flags);

	hgsl_hsync_timeline_put(timeline);
}

int hgsl_hsync_timeline_create(struct hgsl_context *context)
{
	struct hgsl_hsync_timeline *timeline;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		return -ENOMEM;

	snprintf(timeline->name, HGSL_TIMELINE_NAME_LEN,
		"timeline_%s_%d",
		current->comm, current->pid);

	kref_init(&timeline->kref);
	timeline->fence_context = dma_fence_context_alloc(1);
	INIT_LIST_HEAD(&timeline->fence_list);
	spin_lock_init(&timeline->lock);
	timeline->context = context;

	context->timeline = timeline;

	return 0;
}

static void hgsl_hsync_timeline_destroy(struct kref *kref)
{
	struct hgsl_hsync_timeline *timeline =
		container_of(kref, struct hgsl_hsync_timeline, kref);

	kfree(timeline);
}

void hgsl_hsync_timeline_put(struct hgsl_hsync_timeline *timeline)
{
	if (timeline)
		kref_put(&timeline->kref, hgsl_hsync_timeline_destroy);
}

void hgsl_hsync_timeline_fini(struct hgsl_context *context)
{
	struct hgsl_hsync_timeline *timeline = context->timeline;
	struct hgsl_hsync_fence *fence;
	int retry_count = HGSL_HSYNC_FINI_RETRY_COUNT;
	unsigned int max_ts = 0;
	unsigned long flags;

	if (!kref_get_unless_zero(&timeline->kref))
		return;

	spin_lock_irqsave(&timeline->lock, flags);
	while ((retry_count >= 0) && (!list_empty(&timeline->fence_list))) {
		spin_unlock_irqrestore(&timeline->lock, flags);
		msleep(HGSL_HSYNC_FINI_RETRY_TIME_SLICE);
		retry_count--;
		spin_lock_irqsave(&timeline->lock, flags);
	}

	list_for_each_entry(fence, &timeline->fence_list, child_list)
		if (max_ts < fence->ts)
			max_ts = fence->ts;
	spin_unlock_irqrestore(&timeline->lock, flags);

	hgsl_hsync_timeline_signal(timeline, max_ts);
	context->last_ts = max_ts;

	hgsl_hsync_timeline_put(timeline);
}

static const char *hgsl_hsync_get_driver_name(struct dma_fence *base)
{
	return "hgsl-timeline";
}

static const char *hgsl_hsync_get_timeline_name(struct dma_fence *base)
{
	struct hgsl_hsync_fence *fence =
			container_of(base, struct hgsl_hsync_fence, fence);
	struct hgsl_hsync_timeline *timeline = fence->timeline;

	return (timeline == NULL) ? "null" : timeline->name;
}

static bool hgsl_hsync_enable_signaling(struct dma_fence *base)
{
	return true;
}

static bool hgsl_hsync_has_signaled(struct dma_fence *base)
{
	struct hgsl_hsync_fence *fence =
			container_of(base, struct hgsl_hsync_fence, fence);
	struct hgsl_hsync_timeline *timeline = fence->timeline;

	return hgsl_ts32_ge(timeline->last_ts, fence->ts);
}

static void hgsl_hsync_fence_release(struct dma_fence *base)
{
	struct hgsl_hsync_fence *fence =
			container_of(base, struct hgsl_hsync_fence, fence);
	struct hgsl_hsync_timeline *timeline = fence->timeline;

	if (timeline) {
		spin_lock(&timeline->lock);
		list_del_init(&fence->child_list);
		spin_unlock(&timeline->lock);
		hgsl_hsync_timeline_put(timeline);
	}
	kfree(fence);
}

static void hgsl_hsync_fence_value_str(struct dma_fence *base,
				      char *str, int size)
{
	struct hgsl_hsync_fence *fence =
			container_of(base, struct hgsl_hsync_fence, fence);

	snprintf(str, size, "%u", fence->ts);
}

static void hgsl_hsync_timeline_value_str(struct dma_fence *base,
				  char *str, int size)
{
	struct hgsl_hsync_fence *fence =
			container_of(base, struct hgsl_hsync_fence, fence);
	struct hgsl_hsync_timeline *timeline = fence->timeline;

	if (!kref_get_unless_zero(&timeline->kref))
		return;

	snprintf(str, size, "Last retired TS:%u", timeline->last_ts);

	hgsl_hsync_timeline_put(timeline);
}

static const struct dma_fence_ops hgsl_hsync_fence_ops = {
	.get_driver_name = hgsl_hsync_get_driver_name,
	.get_timeline_name = hgsl_hsync_get_timeline_name,
	.enable_signaling = hgsl_hsync_enable_signaling,
	.signaled = hgsl_hsync_has_signaled,
	.wait = dma_fence_default_wait,
	.release = hgsl_hsync_fence_release,

	.fence_value_str = hgsl_hsync_fence_value_str,
	.timeline_value_str = hgsl_hsync_timeline_value_str,
};

static void hgsl_isync_timeline_release(struct kref *kref)
{
	struct hgsl_isync_timeline *timeline = container_of(kref,
					struct hgsl_isync_timeline,
					kref);

	kfree(timeline);
}

static struct hgsl_isync_timeline *
hgsl_isync_timeline_get(struct hgsl_priv *priv, int id, bool check_owner)
{
	int ret = 0;
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_isync_timeline *timeline = NULL;

	spin_lock(&hgsl->isync_timeline_lock);
	timeline = idr_find(&hgsl->isync_timeline_idr, id);
	if (timeline) {
		if (check_owner && (timeline->priv != priv)) {
			timeline = NULL;
		} else {
			ret = kref_get_unless_zero(&timeline->kref);
			if (!ret)
				timeline = NULL;
		}
	}
	spin_unlock(&hgsl->isync_timeline_lock);

	return timeline;
}

static void hgsl_isync_timeline_put(struct hgsl_isync_timeline *timeline)
{
	if (timeline)
		kref_put(&timeline->kref, hgsl_isync_timeline_release);
}

int hgsl_isync_timeline_create(struct hgsl_priv *priv,
				    uint32_t *timeline_id,
				    uint32_t flags,
				    uint64_t initial_ts)
{
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_isync_timeline *timeline;
	int ret = -EINVAL;
	uint32_t idr;

	if (timeline_id == NULL)
		return -EINVAL;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (timeline == NULL)
		return -ENOMEM;

	kref_init(&timeline->kref);
	timeline->context = dma_fence_context_alloc(1);
	INIT_LIST_HEAD(&timeline->fence_list);
	spin_lock_init(&timeline->lock);
	timeline->priv = priv;
	snprintf((char *) timeline->name, sizeof(timeline->name),
					"isync-timeline-%d", *timeline_id);
	timeline->flags = flags;
	timeline->last_ts = initial_ts;
	timeline->is64bits = ((flags & HGSL_ISYNC_64BITS_TIMELINE) != 0);

	idr_preload(GFP_KERNEL);
	spin_lock(&hgsl->isync_timeline_lock);
	idr = idr_alloc(&hgsl->isync_timeline_idr, timeline, 1, 0, GFP_NOWAIT);
	if (idr > 0) {
		timeline->id = idr;
		*timeline_id = idr;
		ret = 0;
	}
	spin_unlock(&hgsl->isync_timeline_lock);
	idr_preload_end();

	/* allocate IDR failed */
	if (ret != 0)
		kfree(timeline);

	return ret;
}

int hgsl_isync_fence_create(struct hgsl_priv *priv, uint32_t timeline_id,
				uint32_t ts, bool ts_is_valid, int *fence_fd)
{
	unsigned long flags;
	struct hgsl_isync_timeline *timeline = NULL;
	struct hgsl_isync_fence *fence = NULL;
	struct sync_file *sync_file = NULL;
	int ret = 0;

	if (fence_fd == NULL)
		return -EINVAL;

	timeline = hgsl_isync_timeline_get(priv, timeline_id, true);
	if (timeline == NULL) {
		ret = -EINVAL;
		goto out;
	}

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* set a minimal ts if user don't set it */
	if (!ts_is_valid)
		ts = 1;

	fence->ts = ts;

	dma_fence_init(&fence->fence, &hgsl_isync_fence_ops,
						&timeline->lock,
						timeline->context,
						ts);

	sync_file = sync_file_create(&fence->fence);
	if (sync_file == NULL) {
		ret = -ENOMEM;
		goto out_fence;
	}

	*fence_fd = get_unused_fd_flags(0);
	if (*fence_fd < 0) {
		ret = -EBADF;
		goto out_fence;
	}

	fd_install(*fence_fd, sync_file->file);

	fence->timeline = timeline;
	spin_lock_irqsave(&timeline->lock, flags);
	list_add_tail(&fence->child_list, &timeline->fence_list);
	spin_unlock_irqrestore(&timeline->lock, flags);

out_fence:
	dma_fence_put(&fence->fence);
out:
	if (ret) {
		if (sync_file)
			fput(sync_file->file);

		if (timeline)
			hgsl_isync_timeline_put(timeline);
	}

	return ret;
}

static int hgsl_isync_timeline_destruct(struct hgsl_priv *priv,
				struct hgsl_isync_timeline *timeline)
{
	unsigned long flags;
	struct hgsl_isync_fence *cur, *next;
	LIST_HEAD(flist);

	if (timeline == NULL)
		return -EINVAL;

	spin_lock_irqsave(&timeline->lock, flags);
	list_for_each_entry_safe(cur, next, &timeline->fence_list,
				 child_list) {
		if (dma_fence_get_rcu(&cur->fence)) {
			list_del_init(&cur->child_list);
			list_add(&cur->free_list, &flist);
		}
	}
	spin_unlock_irqrestore(&timeline->lock, flags);

	list_for_each_entry_safe(cur, next, &flist, free_list) {
		list_del(&cur->free_list);
		dma_fence_signal(&cur->fence);
		dma_fence_put(&cur->fence);
	}

	hgsl_isync_timeline_put(timeline);

	return 0;
}

int hgsl_isync_timeline_destroy(struct hgsl_priv *priv, uint32_t id)
{
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_isync_timeline *timeline;

	spin_lock(&hgsl->isync_timeline_lock);
	timeline = idr_find(&hgsl->isync_timeline_idr, id);
	if (timeline) {
		if (timeline->priv == priv) {
			idr_remove(&hgsl->isync_timeline_idr, timeline->id);
			timeline->id = 0;
		} else {
			timeline = NULL;
		}
	}
	spin_unlock(&hgsl->isync_timeline_lock);

	if (timeline == NULL)
		return -EINVAL;

	return hgsl_isync_timeline_destruct(priv, timeline);
}

void hgsl_isync_fini(struct hgsl_priv *priv)
{
	LIST_HEAD(flist);
	struct qcom_hgsl *hgsl = priv->dev;
	struct hgsl_isync_timeline *cur, *t;
	uint32_t idr;

	spin_lock(&hgsl->isync_timeline_lock);
	idr_for_each_entry(&hgsl->isync_timeline_idr,
					cur, idr) {
		if (cur->priv == priv) {
			idr_remove(&hgsl->isync_timeline_idr, idr);
			list_add(&cur->free_list, &flist);
		}
	}
	spin_unlock(&hgsl->isync_timeline_lock);

	list_for_each_entry_safe(cur, t, &flist, free_list) {
		list_del(&cur->free_list);
		hgsl_isync_timeline_destruct(priv, cur);
	}

}

static int _isync_timeline_signal(
				struct hgsl_isync_timeline *timeline,
				struct dma_fence *fence)
{
	unsigned long flags;
	int ret = -EINVAL;
	struct hgsl_isync_fence *cur, *next;
	bool found = false;

	spin_lock_irqsave(&timeline->lock, flags);
	list_for_each_entry_safe(cur, next, &timeline->fence_list,
						child_list) {
		if (fence == &cur->fence) {
			list_del_init(&cur->child_list);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&timeline->lock, flags);

	if (found) {
		dma_fence_signal(fence);
		ret = 0;
	}

	return ret;
}

int hgsl_isync_fence_signal(struct hgsl_priv *priv, uint32_t timeline_id,
							int fence_fd)
{
	struct hgsl_isync_timeline *timeline;
	struct dma_fence *fence = NULL;
	int ret = -EINVAL;

	timeline = hgsl_isync_timeline_get(priv, timeline_id, true);
	if (timeline == NULL) {
		ret = -EINVAL;
		goto out;
	}

	fence = sync_file_get_fence(fence_fd);
	if (fence == NULL) {
		ret = -EBADF;
		goto out;
	}

	ret = _isync_timeline_signal(timeline, fence);
out:
	if (fence)
		dma_fence_put(fence);

	if (timeline)
		hgsl_isync_timeline_put(timeline);
	return ret;
}

int hgsl_isync_forward(struct hgsl_priv *priv, uint32_t timeline_id,
							uint64_t ts, bool check_owner)
{
	unsigned long flags;
	struct hgsl_isync_timeline *timeline;
	struct hgsl_isync_fence *cur, *next;
	struct dma_fence *base;
	LIST_HEAD(flist);

	timeline = hgsl_isync_timeline_get(priv, timeline_id, check_owner);
	if (timeline == NULL)
		return -EINVAL;

	if (hgsl_ts_ge(timeline->last_ts, ts, timeline->is64bits))
		goto out;

	spin_lock_irqsave(&timeline->lock, flags);
	timeline->last_ts = ts;
	list_for_each_entry_safe(cur, next, &timeline->fence_list,
				 child_list) {
		if (hgsl_ts_ge(ts, cur->ts, timeline->is64bits)) {
			base = dma_fence_get_rcu(&cur->fence);
			list_del_init(&cur->child_list);

			/* It *shouldn't* happen. If it does, it's
			 * the last thing you'll see
			 */
			if (base == NULL)
				pr_warn(" Invalid fence:%p.\n", cur);
			else
				list_add(&cur->free_list, &flist);
		}
	}
	spin_unlock_irqrestore(&timeline->lock, flags);

	list_for_each_entry_safe(cur, next, &flist, free_list) {
		list_del(&cur->free_list);
		dma_fence_signal(&cur->fence);
		dma_fence_put(&cur->fence);
	}

out:
	if (timeline)
		hgsl_isync_timeline_put(timeline);
	return 0;
}

int hgsl_isync_query(struct hgsl_priv *priv, uint32_t timeline_id,
							uint64_t *ts)
{
	struct hgsl_isync_timeline *timeline;

	timeline = hgsl_isync_timeline_get(priv, timeline_id, false);
	if (timeline == NULL)
		return -EINVAL;

	*ts = timeline->last_ts;

	hgsl_isync_timeline_put(timeline);
	return 0;
}
static struct dma_fence *hgsl_timelines_to_fence_array(struct hgsl_priv *priv,
		u64 timelines, u32 count, u64 usize, bool any)
{
	void __user *uptr = u64_to_user_ptr(timelines);
	struct dma_fence_array *array;
	struct dma_fence **fences;
	struct hgsl_isync_fence *fence = NULL;
	int i, ret = 0;

	if (!count || count > INT_MAX)
		return ERR_PTR(-EINVAL);

	fences = kcalloc(count, sizeof(*fences),
		GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);

	if (!fences)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		struct hgsl_timeline_val val;
		struct hgsl_isync_timeline *timeline;

		if (copy_struct_from_user(&val, sizeof(val), uptr, usize)) {
			ret = -EFAULT;
			goto err;
		}

		if (val.padding) {
			ret = -EINVAL;
			goto err;
		}

		timeline = hgsl_isync_timeline_get(priv, val.timeline_id, false);
		if (!timeline) {
			ret = -ENOENT;
			goto err;
		}

		fence = kzalloc(sizeof(*fence), GFP_KERNEL);
		if (fence == NULL) {
			hgsl_isync_timeline_put(timeline);
			ret = -ENOMEM;
			goto err;
		}

		fence->timeline = timeline;
		fence->ts = val.timepoint;

		dma_fence_init(&fence->fence, &hgsl_isync_fence_ops,
							&timeline->lock,
							timeline->context,
							fence->ts);

		spin_lock(&timeline->lock);
		list_add_tail(&fence->child_list, &timeline->fence_list);
		spin_unlock(&timeline->lock);

		fences[i] = &fence->fence;

		uptr += usize;
	}

	/* No need for a fence array for only one fence */
	if (count == 1) {
		struct dma_fence *fence = fences[0];

		kfree(fences);
		return fence;
	}

	array = dma_fence_array_create(count, fences,
		dma_fence_context_alloc(1), 0, any);

	if (array)
		return &array->base;

	ret = -ENOMEM;
err:
	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(fences[i]))
			dma_fence_put(fences[i]);
	}

	kfree(fences);
	return ERR_PTR(ret);
}

int hgsl_isync_wait_multiple(struct hgsl_priv *priv, struct hgsl_timeline_wait *param)
{
	struct dma_fence *fence;
	unsigned long timeout;
	signed long ret;

	if (param->flags != HGSL_TIMELINE_WAIT_ANY &&
		param->flags != HGSL_TIMELINE_WAIT_ALL)
		return -EINVAL;

	if (param->padding)
		return -EINVAL;

	fence = hgsl_timelines_to_fence_array(priv, param->timelines,
		param->count, param->timelines_size,
		(param->flags == HGSL_TIMELINE_WAIT_ANY));

	if (IS_ERR(fence))
		return PTR_ERR(fence);

	if (param->timeout_nanosec == HGSL_TIMELINE_INFINITE_TIMEOUT)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else {
		struct timespec64 timespec;

		timespec.tv_sec = param->timeout_nanosec / NSEC_PER_SEC;
		timespec.tv_nsec = param->timeout_nanosec % NSEC_PER_SEC;
		timeout = timespec64_to_jiffies(&timespec);
	}

	if (!timeout)
		ret = dma_fence_is_signaled(fence) ? 0 : -EBUSY;
	else {
		ret = dma_fence_wait_timeout(fence, true, timeout);

		if (!ret)
			ret = -ETIMEDOUT;
		else if (ret > 0)
			ret = 0;
		else if (ret == -ERESTARTSYS)
			ret = -EINTR;
	}

	dma_fence_put(fence);

	return ret;
}


static const char *hgsl_isync_get_driver_name(struct dma_fence *base)
{
	return "hgsl";
}

static const char *hgsl_isync_get_timeline_name(struct dma_fence *base)
{
	struct hgsl_isync_fence *fence =
				container_of(base,
					     struct hgsl_isync_fence,
					     fence);

	struct hgsl_isync_timeline *timeline = fence->timeline;

	return (timeline == NULL) ? "null":timeline->name;
}

static bool hgsl_isync_has_signaled(struct dma_fence *base)
{
	struct hgsl_isync_fence *fence = NULL;
	struct hgsl_isync_timeline *timeline = NULL;

	if (base) {
		fence = container_of(base, struct hgsl_isync_fence, fence);
		timeline = fence->timeline;
		if (timeline)
			return hgsl_ts_ge(timeline->last_ts, fence->ts, timeline->is64bits);
	}

	return false;
}

static bool hgsl_isync_enable_signaling(struct dma_fence *base)
{
	return !hgsl_isync_has_signaled(base);
}

static void hgsl_isync_fence_release(struct dma_fence *base)
{
	unsigned long flags;
	struct hgsl_isync_fence *fence = container_of(base,
				    struct hgsl_isync_fence,
				    fence);
	struct hgsl_isync_timeline *timeline = fence->timeline;

	if (timeline) {
		spin_lock_irqsave(&timeline->lock, flags);
		list_del_init(&fence->child_list);
		spin_unlock_irqrestore(&timeline->lock, flags);

		dma_fence_signal(base);
		hgsl_isync_timeline_put(fence->timeline);
	}

	kfree(fence);
}

static void hgsl_isync_fence_value_str(struct dma_fence *base,
				      char *str, int size)
{
	snprintf(str, size, "%llu", base->context);
}

static const struct dma_fence_ops hgsl_isync_fence_ops = {
	.get_driver_name = hgsl_isync_get_driver_name,
	.get_timeline_name = hgsl_isync_get_timeline_name,
	.enable_signaling = hgsl_isync_enable_signaling,
	.signaled = hgsl_isync_has_signaled,
	.wait = dma_fence_default_wait,
	.release = hgsl_isync_fence_release,

	.fence_value_str = hgsl_isync_fence_value_str,
};
