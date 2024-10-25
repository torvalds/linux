// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>

#include "mdss_sync.h"

#define MDSS_SYNC_NAME_SIZE		64
#define MDSS_SYNC_DRIVER_NAME	"mdss"

/**
 * struct mdss_fence - sync fence context
 * @base: base sync fence object
 * @name: name of this sync fence
 * @fence_list: linked list of outstanding sync fence
 */
struct mdss_fence {
	struct dma_fence base;
	char name[MDSS_SYNC_NAME_SIZE];
	struct list_head fence_list;
};

#if defined(CONFIG_SYNC_FILE)
/*
 * to_mdss_fence - get mdss fence from fence base object
 * @fence: Pointer to fence base object
 */
static struct mdss_fence *to_mdss_fence(struct dma_fence *fence)
{
	return container_of(fence, struct mdss_fence, base);
}

/*
 * to_mdss_timeline - get mdss timeline from fence base object
 * @fence: Pointer to fence base object
 */
static struct mdss_timeline *to_mdss_timeline(struct dma_fence *fence)
{
	return container_of(fence->lock, struct mdss_timeline, lock);
}

/*
 * mdss_free_timeline - Free the given timeline object
 * @kref: Pointer to timeline kref object.
 */
static void mdss_free_timeline(struct kref *kref)
{
	struct mdss_timeline *tl =
		container_of(kref, struct mdss_timeline, kref);

	kfree(tl);
}

/*
 * mdss_put_timeline - Put the given timeline object
 * @tl: Pointer to timeline object.
 */
static void mdss_put_timeline(struct mdss_timeline *tl)
{
	if (!tl) {
		pr_err("invalid parameters\n");
		return;
	}

	kref_put(&tl->kref, mdss_free_timeline);
}

/*
 * mdss_get_timeline - Get the given timeline object
 * @tl: Pointer to timeline object.
 */
static void mdss_get_timeline(struct mdss_timeline *tl)
{
	if (!tl) {
		pr_err("invalid parameters\n");
		return;
	}

	kref_get(&tl->kref);
}

static const char *mdss_fence_get_driver_name(struct dma_fence *fence)
{
	return MDSS_SYNC_DRIVER_NAME;
}

static const char *mdss_fence_get_timeline_name(struct dma_fence *fence)
{
	struct mdss_timeline *tl = to_mdss_timeline(fence);

	return tl->name;
}

static bool mdss_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static bool mdss_fence_signaled(struct dma_fence *fence)
{
	struct mdss_timeline *tl = to_mdss_timeline(fence);
	bool status;

	status = ((s32) (tl->value - fence->seqno)) >= 0;
	pr_debug("status:%d fence seq:%d and timeline:%s:%d next %d\n",
			status, fence->seqno, tl->name,
			tl->value, tl->next_value);
	return status;
}

static void mdss_fence_release(struct dma_fence *fence)
{
	struct mdss_fence *f = to_mdss_fence(fence);
	struct mdss_timeline *tl = to_mdss_timeline(fence);

	pr_debug("%s for fence %s\n", __func__, f->name);

	if (!fence || (fence->ops->get_driver_name !=
			&mdss_fence_get_driver_name)) {
		pr_debug("invalid parameters\n");
		return;
	}

	spin_lock(&tl->list_lock);
	if (!list_empty(&f->fence_list))
		list_del(&f->fence_list);
	spin_unlock(&tl->list_lock);
	mdss_put_timeline(to_mdss_timeline(fence));
	kfree_rcu(f, base.rcu);
}

static void mdss_fence_value_str(struct dma_fence *fence, char *str, int size)
{
	snprintf(str, size, "%u", fence->seqno);
}

static void mdss_fence_timeline_value_str(struct dma_fence *fence, char *str,
		int size)
{
	struct mdss_timeline *tl = to_mdss_timeline(fence);

	snprintf(str, size, "%u", tl->value);
}

static struct dma_fence_ops mdss_fence_ops = {
	.get_driver_name = mdss_fence_get_driver_name,
	.get_timeline_name = mdss_fence_get_timeline_name,
	.enable_signaling = mdss_fence_enable_signaling,
	.signaled = mdss_fence_signaled,
	.wait = dma_fence_default_wait,
	.release = mdss_fence_release,
	.fence_value_str = mdss_fence_value_str,
	.timeline_value_str = mdss_fence_timeline_value_str,
};

/*
 * mdss_create_timeline - Create timeline object with the given name
 * @name: Pointer to name character string.
 */
struct mdss_timeline *mdss_create_timeline(const char *name)
{
	struct mdss_timeline *tl;

	if (!name) {
		pr_err("invalid parameters\n");
		return NULL;
	}

	tl = kzalloc(sizeof(struct mdss_timeline), GFP_KERNEL);
	if (!tl)
		return NULL;

	kref_init(&tl->kref);
	snprintf(tl->name, sizeof(tl->name), "%s", name);
	spin_lock_init(&tl->lock);
	spin_lock_init(&tl->list_lock);
	tl->context = dma_fence_context_alloc(1);
	INIT_LIST_HEAD(&tl->fence_list_head);

	return tl;
}

/*
 * mdss_destroy_timeline - Destroy the given timeline object
 * @tl: Pointer to timeline object.
 */
void mdss_destroy_timeline(struct mdss_timeline *tl)
{
	mdss_put_timeline(tl);
}

/*
 * mdss_inc_timeline_locked - Increment timeline by given amount
 * @tl: Pointer to timeline object.
 * @increment: the amount to increase the timeline by.
 */
static int mdss_inc_timeline_locked(struct mdss_timeline *tl,
		int increment)
{
	struct mdss_fence *f, *next;
	s32 val;
	bool is_signaled = false;
	struct list_head local_list_head;
	unsigned long flags;

	INIT_LIST_HEAD(&local_list_head);

	spin_lock(&tl->list_lock);
	if (list_empty(&tl->fence_list_head)) {
		pr_debug("fence list is empty\n");
		tl->value += 1;
		spin_unlock(&tl->list_lock);
		return 0;
	}

	list_for_each_entry_safe(f, next, &tl->fence_list_head, fence_list)
		list_move(&f->fence_list, &local_list_head);
	spin_unlock(&tl->list_lock);

	spin_lock_irqsave(&tl->lock, flags);
	val = tl->next_value - tl->value;
	if (val >= increment)
		tl->value += increment;
	spin_unlock_irqrestore(&tl->lock, flags);

	list_for_each_entry_safe(f, next, &local_list_head, fence_list) {
		spin_lock_irqsave(&tl->lock, flags);
		is_signaled = dma_fence_is_signaled_locked(&f->base);
		spin_unlock_irqrestore(&tl->lock, flags);
		if (is_signaled) {
			pr_debug("%s signaled\n", f->name);
			list_del_init(&f->fence_list);
			dma_fence_put(&f->base);
		} else {
			spin_lock(&tl->list_lock);
			list_move(&f->fence_list, &tl->fence_list_head);
			spin_unlock(&tl->list_lock);
		}
	}

	return 0;
}

/*
 * mdss_resync_timeline - Resync timeline to last committed value
 * @tl: Pointer to timeline object.
 */
void mdss_resync_timeline(struct mdss_timeline *tl)
{
	s32 val;

	if (!tl) {
		pr_err("invalid parameters\n");
		return;
	}

	val = tl->next_value - tl->value;
	if (val > 0) {
		pr_warn("flush %s:%d TL(Nxt %d , Crnt %d)\n", tl->name, val,
			tl->next_value, tl->value);
		mdss_inc_timeline_locked(tl, val);
	}
}

/*
 * mdss_get_sync_fence - Create fence object from the given timeline
 * @tl: Pointer to timeline object
 * @timestamp: Pointer to timestamp of the returned fence. Null if not required.
 * Return: pointer fence created on give time line.
 */
struct mdss_fence *mdss_get_sync_fence(
		struct mdss_timeline *tl, const char *fence_name,
		u32 *timestamp, int value)
{
	struct mdss_fence *f;
	unsigned long flags;

	if (!tl) {
		pr_err("invalid parameters\n");
		return NULL;
	}

	f = kzalloc(sizeof(struct mdss_fence), GFP_KERNEL);
	if (!f)
		return NULL;

	INIT_LIST_HEAD(&f->fence_list);
	spin_lock_irqsave(&tl->lock, flags);
	tl->next_value = value;
	dma_fence_init(&f->base, &mdss_fence_ops, &tl->lock, tl->context,
			value);
	mdss_get_timeline(tl);
	spin_unlock_irqrestore(&tl->lock, flags);

	spin_lock(&tl->list_lock);
	list_add_tail(&f->fence_list, &tl->fence_list_head);
	spin_unlock(&tl->list_lock);
	snprintf(f->name, sizeof(f->name), "%s_%u", fence_name, value);

	if (timestamp)
		*timestamp = value;

	pr_debug("fence created at val=%u tl->name= %s tl->value = %d tl->next_value =%d\n",
			value, tl->name, tl->value, tl->next_value);

	return (struct mdss_fence *) &f->base;
}

/*
 * mdss_inc_timeline - Increment timeline by given amount
 * @tl: Pointer to timeline object.
 * @increment: the amount to increase the timeline by.
 */
int mdss_inc_timeline(struct mdss_timeline *tl, int increment)
{
	int rc;

	if (!tl) {
		pr_err("invalid parameters\n");
		return -EINVAL;
	}

	rc = mdss_inc_timeline_locked(tl, increment);
	return rc;
}

/*
 * mdss_get_timeline_commit_ts - Return commit tick of given timeline
 * @tl: Pointer to timeline object.
 */
u32 mdss_get_timeline_commit_ts(struct mdss_timeline *tl)
{
	if (!tl) {
		pr_err("invalid parameters\n");
		return 0;
	}

	return tl->next_value;
}

/*
 * mdss_get_timeline_retire_ts - Return retire tick of given timeline
 * @tl: Pointer to timeline object.
 */
u32 mdss_get_timeline_retire_ts(struct mdss_timeline *tl)
{
	if (!tl) {
		pr_err("invalid parameters\n");
		return 0;
	}

	return tl->value;
}

/*
 * mdss_put_sync_fence - Destroy given fence object
 * @fence: Pointer to fence object.
 */
void mdss_put_sync_fence(struct mdss_fence *fence)
{
	if (!fence) {
		pr_err("invalid parameters\n");
		return;
	}

	dma_fence_put((struct dma_fence *) fence);
}

/*
 * mdss_wait_sync_fence - Wait until fence signal or timeout
 * @fence: Pointer to fence object.
 * @timeout: maximum wait time, in msec, for fence to signal.
 */
int mdss_wait_sync_fence(struct mdss_fence *fence,
		long timeout)
{
	int rc;

	if (!fence) {
		pr_err("invalid parameters\n");
		return -EINVAL;
	}

	rc = dma_fence_wait_timeout((struct dma_fence *) fence, false,
			msecs_to_jiffies(timeout));
	if (rc > 0) {
		pr_debug("fence signaled\n");
		rc = 0;
	} else if (rc == 0) {
		struct dma_fence *input_fence = (struct dma_fence *) fence;
		char timeline_str[MDSS_SYNC_NAME_SIZE];

		if (input_fence->ops->timeline_value_str)
			input_fence->ops->timeline_value_str(input_fence,
					timeline_str, MDSS_SYNC_NAME_SIZE);
		pr_err(
			"drv:%s timeline:%s seqno:%d timeline:%s status:0x%x\n",
			input_fence->ops->get_driver_name(input_fence),
			input_fence->ops->get_timeline_name(input_fence),
			input_fence->seqno, timeline_str,
			input_fence->ops->signaled ?
			input_fence->ops->signaled(input_fence) : 0xffffffff);
		rc = -ETIMEDOUT;
	}

	return rc;
}

/*
 * mdss_get_fd_sync_fence - Get fence object of given file descriptor
 * @fd: File description of fence object.
 */
struct mdss_fence *mdss_get_fd_sync_fence(int fd)
{
	struct dma_fence *fence = NULL;

	fence = sync_file_get_fence(fd);
	return to_mdss_fence(fence);
}

/*
 * mdss_get_sync_fence_fd - Get file descriptor of given fence object
 * @fence: Pointer to fence object.
 * Return: File descriptor on success, or error code on error
 */
int mdss_get_sync_fence_fd(struct mdss_fence *fence)
{
	int fd;
	struct sync_file *sync_file;

	if (!fence) {
		pr_err("invalid parameters\n");
		return -EINVAL;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		pr_err("fail to get unused fd\n");
		return fd;
	}

	sync_file = sync_file_create((struct dma_fence *) fence);
	if (!sync_file) {
		put_unused_fd(fd);
		pr_err("failed to create sync file\n");
		return -ENOMEM;
	}

	fd_install(fd, sync_file->file);

	return fd;
}

/*
 * mdss_put_sync_fence - Destroy given fence object
 * @fence: Pointer to fence object.
 * Return: fence name
 */
const char *mdss_get_sync_fence_name(struct mdss_fence *fence)
{
	struct dma_fence *input_fence = NULL;

	if (!fence) {
		pr_err("invalid parameters\n");
		return NULL;
	}

	input_fence = (struct dma_fence *) &fence->base;

	if (input_fence->ops->get_driver_name != &mdss_fence_get_driver_name)
		return input_fence->ops->get_driver_name(input_fence);

	return fence->name;
}
#endif
