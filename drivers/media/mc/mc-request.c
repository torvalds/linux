// SPDX-License-Identifier: GPL-2.0
/*
 * Media device request objects
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 * Copyright (C) 2018 Intel Corporation
 * Copyright (C) 2018 Google, Inc.
 *
 * Author: Hans Verkuil <hans.verkuil@cisco.com>
 * Author: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/refcount.h>

#include <media/media-device.h>
#include <media/media-request.h>

static const char * const request_state[] = {
	[MEDIA_REQUEST_STATE_IDLE]	 = "idle",
	[MEDIA_REQUEST_STATE_VALIDATING] = "validating",
	[MEDIA_REQUEST_STATE_QUEUED]	 = "queued",
	[MEDIA_REQUEST_STATE_COMPLETE]	 = "complete",
	[MEDIA_REQUEST_STATE_CLEANING]	 = "cleaning",
	[MEDIA_REQUEST_STATE_UPDATING]	 = "updating",
};

static const char *
media_request_state_str(enum media_request_state state)
{
	BUILD_BUG_ON(ARRAY_SIZE(request_state) != NR_OF_MEDIA_REQUEST_STATE);

	if (WARN_ON(state >= ARRAY_SIZE(request_state)))
		return "invalid";
	return request_state[state];
}

static void media_request_clean(struct media_request *req)
{
	struct media_request_object *obj, *obj_safe;

	/* Just a sanity check. No other code path is allowed to change this. */
	WARN_ON(req->state != MEDIA_REQUEST_STATE_CLEANING);
	WARN_ON(req->updating_count);
	WARN_ON(req->access_count);

	list_for_each_entry_safe(obj, obj_safe, &req->objects, list) {
		media_request_object_unbind(obj);
		media_request_object_put(obj);
	}

	req->updating_count = 0;
	req->access_count = 0;
	WARN_ON(req->num_incomplete_objects);
	req->num_incomplete_objects = 0;
	wake_up_interruptible_all(&req->poll_wait);
}

static void media_request_release(struct kref *kref)
{
	struct media_request *req =
		container_of(kref, struct media_request, kref);
	struct media_device *mdev = req->mdev;

	dev_dbg(mdev->dev, "request: release %s\n", req->debug_str);

	/* No other users, no need for a spinlock */
	req->state = MEDIA_REQUEST_STATE_CLEANING;

	media_request_clean(req);

	if (mdev->ops->req_free)
		mdev->ops->req_free(req);
	else
		kfree(req);
}

void media_request_put(struct media_request *req)
{
	kref_put(&req->kref, media_request_release);
}
EXPORT_SYMBOL_GPL(media_request_put);

static int media_request_close(struct inode *inode, struct file *filp)
{
	struct media_request *req = filp->private_data;

	media_request_put(req);
	return 0;
}

static __poll_t media_request_poll(struct file *filp,
				   struct poll_table_struct *wait)
{
	struct media_request *req = filp->private_data;
	unsigned long flags;
	__poll_t ret = 0;

	if (!(poll_requested_events(wait) & EPOLLPRI))
		return 0;

	poll_wait(filp, &req->poll_wait, wait);
	spin_lock_irqsave(&req->lock, flags);
	if (req->state == MEDIA_REQUEST_STATE_COMPLETE) {
		ret = EPOLLPRI;
		goto unlock;
	}
	if (req->state != MEDIA_REQUEST_STATE_QUEUED) {
		ret = EPOLLERR;
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&req->lock, flags);
	return ret;
}

static long media_request_ioctl_queue(struct media_request *req)
{
	struct media_device *mdev = req->mdev;
	enum media_request_state state;
	unsigned long flags;
	int ret;

	dev_dbg(mdev->dev, "request: queue %s\n", req->debug_str);

	/*
	 * Ensure the request that is validated will be the one that gets queued
	 * next by serialising the queueing process. This mutex is also used
	 * to serialize with canceling a vb2 queue and with setting values such
	 * as controls in a request.
	 */
	mutex_lock(&mdev->req_queue_mutex);

	media_request_get(req);

	spin_lock_irqsave(&req->lock, flags);
	if (req->state == MEDIA_REQUEST_STATE_IDLE)
		req->state = MEDIA_REQUEST_STATE_VALIDATING;
	state = req->state;
	spin_unlock_irqrestore(&req->lock, flags);
	if (state != MEDIA_REQUEST_STATE_VALIDATING) {
		dev_dbg(mdev->dev,
			"request: unable to queue %s, request in state %s\n",
			req->debug_str, media_request_state_str(state));
		media_request_put(req);
		mutex_unlock(&mdev->req_queue_mutex);
		return -EBUSY;
	}

	ret = mdev->ops->req_validate(req);

	/*
	 * If the req_validate was successful, then we mark the state as QUEUED
	 * and call req_queue. The reason we set the state first is that this
	 * allows req_queue to unbind or complete the queued objects in case
	 * they are immediately 'consumed'. State changes from QUEUED to another
	 * state can only happen if either the driver changes the state or if
	 * the user cancels the vb2 queue. The driver can only change the state
	 * after each object is queued through the req_queue op (and note that
	 * that op cannot fail), so setting the state to QUEUED up front is
	 * safe.
	 *
	 * The other reason for changing the state is if the vb2 queue is
	 * canceled, and that uses the req_queue_mutex which is still locked
	 * while req_queue is called, so that's safe as well.
	 */
	spin_lock_irqsave(&req->lock, flags);
	req->state = ret ? MEDIA_REQUEST_STATE_IDLE
			 : MEDIA_REQUEST_STATE_QUEUED;
	spin_unlock_irqrestore(&req->lock, flags);

	if (!ret)
		mdev->ops->req_queue(req);

	mutex_unlock(&mdev->req_queue_mutex);

	if (ret) {
		dev_dbg(mdev->dev, "request: can't queue %s (%d)\n",
			req->debug_str, ret);
		media_request_put(req);
	}

	return ret;
}

static long media_request_ioctl_reinit(struct media_request *req)
{
	struct media_device *mdev = req->mdev;
	unsigned long flags;

	spin_lock_irqsave(&req->lock, flags);
	if (req->state != MEDIA_REQUEST_STATE_IDLE &&
	    req->state != MEDIA_REQUEST_STATE_COMPLETE) {
		dev_dbg(mdev->dev,
			"request: %s not in idle or complete state, cannot reinit\n",
			req->debug_str);
		spin_unlock_irqrestore(&req->lock, flags);
		return -EBUSY;
	}
	if (req->access_count) {
		dev_dbg(mdev->dev,
			"request: %s is being accessed, cannot reinit\n",
			req->debug_str);
		spin_unlock_irqrestore(&req->lock, flags);
		return -EBUSY;
	}
	req->state = MEDIA_REQUEST_STATE_CLEANING;
	spin_unlock_irqrestore(&req->lock, flags);

	media_request_clean(req);

	spin_lock_irqsave(&req->lock, flags);
	req->state = MEDIA_REQUEST_STATE_IDLE;
	spin_unlock_irqrestore(&req->lock, flags);

	return 0;
}

static long media_request_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct media_request *req = filp->private_data;

	switch (cmd) {
	case MEDIA_REQUEST_IOC_QUEUE:
		return media_request_ioctl_queue(req);
	case MEDIA_REQUEST_IOC_REINIT:
		return media_request_ioctl_reinit(req);
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations request_fops = {
	.owner = THIS_MODULE,
	.poll = media_request_poll,
	.unlocked_ioctl = media_request_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = media_request_ioctl,
#endif /* CONFIG_COMPAT */
	.release = media_request_close,
};

struct media_request *
media_request_get_by_fd(struct media_device *mdev, int request_fd)
{
	struct fd f;
	struct media_request *req;

	if (!mdev || !mdev->ops ||
	    !mdev->ops->req_validate || !mdev->ops->req_queue)
		return ERR_PTR(-EBADR);

	f = fdget(request_fd);
	if (!f.file)
		goto err_no_req_fd;

	if (f.file->f_op != &request_fops)
		goto err_fput;
	req = f.file->private_data;
	if (req->mdev != mdev)
		goto err_fput;

	/*
	 * Note: as long as someone has an open filehandle of the request,
	 * the request can never be released. The fdget() above ensures that
	 * even if userspace closes the request filehandle, the release()
	 * fop won't be called, so the media_request_get() always succeeds
	 * and there is no race condition where the request was released
	 * before media_request_get() is called.
	 */
	media_request_get(req);
	fdput(f);

	return req;

err_fput:
	fdput(f);

err_no_req_fd:
	dev_dbg(mdev->dev, "cannot find request_fd %d\n", request_fd);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(media_request_get_by_fd);

int media_request_alloc(struct media_device *mdev, int *alloc_fd)
{
	struct media_request *req;
	struct file *filp;
	int fd;
	int ret;

	/* Either both are NULL or both are non-NULL */
	if (WARN_ON(!mdev->ops->req_alloc ^ !mdev->ops->req_free))
		return -ENOMEM;

	if (mdev->ops->req_alloc)
		req = mdev->ops->req_alloc(mdev);
	else
		req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto err_free_req;
	}

	filp = anon_inode_getfile("request", &request_fops, NULL, O_CLOEXEC);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto err_put_fd;
	}

	filp->private_data = req;
	req->mdev = mdev;
	req->state = MEDIA_REQUEST_STATE_IDLE;
	req->num_incomplete_objects = 0;
	kref_init(&req->kref);
	INIT_LIST_HEAD(&req->objects);
	spin_lock_init(&req->lock);
	init_waitqueue_head(&req->poll_wait);
	req->updating_count = 0;
	req->access_count = 0;

	*alloc_fd = fd;

	snprintf(req->debug_str, sizeof(req->debug_str), "%u:%d",
		 atomic_inc_return(&mdev->request_id), fd);
	dev_dbg(mdev->dev, "request: allocated %s\n", req->debug_str);

	fd_install(fd, filp);

	return 0;

err_put_fd:
	put_unused_fd(fd);

err_free_req:
	if (mdev->ops->req_free)
		mdev->ops->req_free(req);
	else
		kfree(req);

	return ret;
}

static void media_request_object_release(struct kref *kref)
{
	struct media_request_object *obj =
		container_of(kref, struct media_request_object, kref);
	struct media_request *req = obj->req;

	if (WARN_ON(req))
		media_request_object_unbind(obj);
	obj->ops->release(obj);
}

struct media_request_object *
media_request_object_find(struct media_request *req,
			  const struct media_request_object_ops *ops,
			  void *priv)
{
	struct media_request_object *obj;
	struct media_request_object *found = NULL;
	unsigned long flags;

	if (WARN_ON(!ops || !priv))
		return NULL;

	spin_lock_irqsave(&req->lock, flags);
	list_for_each_entry(obj, &req->objects, list) {
		if (obj->ops == ops && obj->priv == priv) {
			media_request_object_get(obj);
			found = obj;
			break;
		}
	}
	spin_unlock_irqrestore(&req->lock, flags);
	return found;
}
EXPORT_SYMBOL_GPL(media_request_object_find);

void media_request_object_put(struct media_request_object *obj)
{
	kref_put(&obj->kref, media_request_object_release);
}
EXPORT_SYMBOL_GPL(media_request_object_put);

void media_request_object_init(struct media_request_object *obj)
{
	obj->ops = NULL;
	obj->req = NULL;
	obj->priv = NULL;
	obj->completed = false;
	INIT_LIST_HEAD(&obj->list);
	kref_init(&obj->kref);
}
EXPORT_SYMBOL_GPL(media_request_object_init);

int media_request_object_bind(struct media_request *req,
			      const struct media_request_object_ops *ops,
			      void *priv, bool is_buffer,
			      struct media_request_object *obj)
{
	unsigned long flags;
	int ret = -EBUSY;

	if (WARN_ON(!ops->release))
		return -EBADR;

	spin_lock_irqsave(&req->lock, flags);

	if (WARN_ON(req->state != MEDIA_REQUEST_STATE_UPDATING))
		goto unlock;

	obj->req = req;
	obj->ops = ops;
	obj->priv = priv;

	if (is_buffer)
		list_add_tail(&obj->list, &req->objects);
	else
		list_add(&obj->list, &req->objects);
	req->num_incomplete_objects++;
	ret = 0;

unlock:
	spin_unlock_irqrestore(&req->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(media_request_object_bind);

void media_request_object_unbind(struct media_request_object *obj)
{
	struct media_request *req = obj->req;
	unsigned long flags;
	bool completed = false;

	if (WARN_ON(!req))
		return;

	spin_lock_irqsave(&req->lock, flags);
	list_del(&obj->list);
	obj->req = NULL;

	if (req->state == MEDIA_REQUEST_STATE_COMPLETE)
		goto unlock;

	if (WARN_ON(req->state == MEDIA_REQUEST_STATE_VALIDATING))
		goto unlock;

	if (req->state == MEDIA_REQUEST_STATE_CLEANING) {
		if (!obj->completed)
			req->num_incomplete_objects--;
		goto unlock;
	}

	if (WARN_ON(!req->num_incomplete_objects))
		goto unlock;

	req->num_incomplete_objects--;
	if (req->state == MEDIA_REQUEST_STATE_QUEUED &&
	    !req->num_incomplete_objects) {
		req->state = MEDIA_REQUEST_STATE_COMPLETE;
		completed = true;
		wake_up_interruptible_all(&req->poll_wait);
	}

unlock:
	spin_unlock_irqrestore(&req->lock, flags);
	if (obj->ops->unbind)
		obj->ops->unbind(obj);
	if (completed)
		media_request_put(req);
}
EXPORT_SYMBOL_GPL(media_request_object_unbind);

void media_request_object_complete(struct media_request_object *obj)
{
	struct media_request *req = obj->req;
	unsigned long flags;
	bool completed = false;

	spin_lock_irqsave(&req->lock, flags);
	if (obj->completed)
		goto unlock;
	obj->completed = true;
	if (WARN_ON(!req->num_incomplete_objects) ||
	    WARN_ON(req->state != MEDIA_REQUEST_STATE_QUEUED))
		goto unlock;

	if (!--req->num_incomplete_objects) {
		req->state = MEDIA_REQUEST_STATE_COMPLETE;
		wake_up_interruptible_all(&req->poll_wait);
		completed = true;
	}
unlock:
	spin_unlock_irqrestore(&req->lock, flags);
	if (completed)
		media_request_put(req);
}
EXPORT_SYMBOL_GPL(media_request_object_complete);
