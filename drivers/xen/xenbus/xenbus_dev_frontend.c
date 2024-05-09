/*
 * Driver giving user-space access to the kernel's xenbus connection
 * to xenstore.
 *
 * Copyright (c) 2005, Christian Limpach
 * Copyright (c) 2005, Rusty Russell, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Changes:
 * 2008-10-07  Alex Zeffertt    Replaced /proc/xen/xenbus with xenfs filesystem
 *                              and /proc/xen compatibility mount point.
 *                              Turned xenfs into a loadable module.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uio.h>
#include <linux/notifier.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>

#include <xen/xenbus.h>
#include <xen/xen.h>
#include <asm/xen/hypervisor.h>

#include "xenbus.h"

unsigned int xb_dev_generation_id;

/*
 * An element of a list of outstanding transactions, for which we're
 * still waiting a reply.
 */
struct xenbus_transaction_holder {
	struct list_head list;
	struct xenbus_transaction handle;
	unsigned int generation_id;
};

/*
 * A buffer of data on the queue.
 */
struct read_buffer {
	struct list_head list;
	unsigned int cons;
	unsigned int len;
	char msg[] __counted_by(len);
};

struct xenbus_file_priv {
	/*
	 * msgbuffer_mutex is held while partial requests are built up
	 * and complete requests are acted on.  It therefore protects
	 * the "transactions" and "watches" lists, and the partial
	 * request length and buffer.
	 *
	 * reply_mutex protects the reply being built up to return to
	 * usermode.  It nests inside msgbuffer_mutex but may be held
	 * alone during a watch callback.
	 */
	struct mutex msgbuffer_mutex;

	/* In-progress transactions */
	struct list_head transactions;

	/* Active watches. */
	struct list_head watches;

	/* Partial request. */
	unsigned int len;
	union {
		struct xsd_sockmsg msg;
		char buffer[XENSTORE_PAYLOAD_MAX];
	} u;

	/* Response queue. */
	struct mutex reply_mutex;
	struct list_head read_buffers;
	wait_queue_head_t read_waitq;

	struct kref kref;

	struct work_struct wq;
};

/* Read out any raw xenbus messages queued up. */
static ssize_t xenbus_file_read(struct file *filp,
			       char __user *ubuf,
			       size_t len, loff_t *ppos)
{
	struct xenbus_file_priv *u = filp->private_data;
	struct read_buffer *rb;
	ssize_t i;
	int ret;

	mutex_lock(&u->reply_mutex);
again:
	while (list_empty(&u->read_buffers)) {
		mutex_unlock(&u->reply_mutex);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(u->read_waitq,
					       !list_empty(&u->read_buffers));
		if (ret)
			return ret;
		mutex_lock(&u->reply_mutex);
	}

	rb = list_entry(u->read_buffers.next, struct read_buffer, list);
	i = 0;
	while (i < len) {
		size_t sz = min_t(size_t, len - i, rb->len - rb->cons);

		ret = copy_to_user(ubuf + i, &rb->msg[rb->cons], sz);

		i += sz - ret;
		rb->cons += sz - ret;

		if (ret != 0) {
			if (i == 0)
				i = -EFAULT;
			goto out;
		}

		/* Clear out buffer if it has been consumed */
		if (rb->cons == rb->len) {
			list_del(&rb->list);
			kfree(rb);
			if (list_empty(&u->read_buffers))
				break;
			rb = list_entry(u->read_buffers.next,
					struct read_buffer, list);
		}
	}
	if (i == 0)
		goto again;

out:
	mutex_unlock(&u->reply_mutex);
	return i;
}

/*
 * Add a buffer to the queue.  Caller must hold the appropriate lock
 * if the queue is not local.  (Commonly the caller will build up
 * multiple queued buffers on a temporary local list, and then add it
 * to the appropriate list under lock once all the buffers have een
 * successfully allocated.)
 */
static int queue_reply(struct list_head *queue, const void *data, size_t len)
{
	struct read_buffer *rb;

	if (len == 0)
		return 0;
	if (len > XENSTORE_PAYLOAD_MAX)
		return -EINVAL;

	rb = kmalloc(struct_size(rb, msg, len), GFP_KERNEL);
	if (rb == NULL)
		return -ENOMEM;

	rb->cons = 0;
	rb->len = len;

	memcpy(rb->msg, data, len);

	list_add_tail(&rb->list, queue);
	return 0;
}

/*
 * Free all the read_buffer s on a list.
 * Caller must have sole reference to list.
 */
static void queue_cleanup(struct list_head *list)
{
	struct read_buffer *rb;

	while (!list_empty(list)) {
		rb = list_entry(list->next, struct read_buffer, list);
		list_del(list->next);
		kfree(rb);
	}
}

struct watch_adapter {
	struct list_head list;
	struct xenbus_watch watch;
	struct xenbus_file_priv *dev_data;
	char *token;
};

static void free_watch_adapter(struct watch_adapter *watch)
{
	kfree(watch->watch.node);
	kfree(watch->token);
	kfree(watch);
}

static struct watch_adapter *alloc_watch_adapter(const char *path,
						 const char *token)
{
	struct watch_adapter *watch;

	watch = kzalloc(sizeof(*watch), GFP_KERNEL);
	if (watch == NULL)
		goto out_fail;

	watch->watch.node = kstrdup(path, GFP_KERNEL);
	if (watch->watch.node == NULL)
		goto out_free;

	watch->token = kstrdup(token, GFP_KERNEL);
	if (watch->token == NULL)
		goto out_free;

	return watch;

out_free:
	free_watch_adapter(watch);

out_fail:
	return NULL;
}

static void watch_fired(struct xenbus_watch *watch,
			const char *path,
			const char *token)
{
	struct watch_adapter *adap;
	struct xsd_sockmsg hdr;
	const char *token_caller;
	int path_len, tok_len, body_len;
	int ret;
	LIST_HEAD(staging_q);

	adap = container_of(watch, struct watch_adapter, watch);

	token_caller = adap->token;

	path_len = strlen(path) + 1;
	tok_len = strlen(token_caller) + 1;
	body_len = path_len + tok_len;

	hdr.type = XS_WATCH_EVENT;
	hdr.len = body_len;

	mutex_lock(&adap->dev_data->reply_mutex);

	ret = queue_reply(&staging_q, &hdr, sizeof(hdr));
	if (!ret)
		ret = queue_reply(&staging_q, path, path_len);
	if (!ret)
		ret = queue_reply(&staging_q, token_caller, tok_len);

	if (!ret) {
		/* success: pass reply list onto watcher */
		list_splice_tail(&staging_q, &adap->dev_data->read_buffers);
		wake_up(&adap->dev_data->read_waitq);
	} else
		queue_cleanup(&staging_q);

	mutex_unlock(&adap->dev_data->reply_mutex);
}

static void xenbus_worker(struct work_struct *wq)
{
	struct xenbus_file_priv *u;
	struct xenbus_transaction_holder *trans, *tmp;
	struct watch_adapter *watch, *tmp_watch;
	struct read_buffer *rb, *tmp_rb;

	u = container_of(wq, struct xenbus_file_priv, wq);

	/*
	 * No need for locking here because there are no other users,
	 * by definition.
	 */

	list_for_each_entry_safe(trans, tmp, &u->transactions, list) {
		xenbus_transaction_end(trans->handle, 1);
		list_del(&trans->list);
		kfree(trans);
	}

	list_for_each_entry_safe(watch, tmp_watch, &u->watches, list) {
		unregister_xenbus_watch(&watch->watch);
		list_del(&watch->list);
		free_watch_adapter(watch);
	}

	list_for_each_entry_safe(rb, tmp_rb, &u->read_buffers, list) {
		list_del(&rb->list);
		kfree(rb);
	}
	kfree(u);
}

static void xenbus_file_free(struct kref *kref)
{
	struct xenbus_file_priv *u;

	/*
	 * We might be called in xenbus_thread().
	 * Use workqueue to avoid deadlock.
	 */
	u = container_of(kref, struct xenbus_file_priv, kref);
	schedule_work(&u->wq);
}

static struct xenbus_transaction_holder *xenbus_get_transaction(
	struct xenbus_file_priv *u, uint32_t tx_id)
{
	struct xenbus_transaction_holder *trans;

	list_for_each_entry(trans, &u->transactions, list)
		if (trans->handle.id == tx_id)
			return trans;

	return NULL;
}

void xenbus_dev_queue_reply(struct xb_req_data *req)
{
	struct xenbus_file_priv *u = req->par;
	struct xenbus_transaction_holder *trans = NULL;
	int rc;
	LIST_HEAD(staging_q);

	xs_request_exit(req);

	mutex_lock(&u->msgbuffer_mutex);

	if (req->type == XS_TRANSACTION_START) {
		trans = xenbus_get_transaction(u, 0);
		if (WARN_ON(!trans))
			goto out;
		if (req->msg.type == XS_ERROR) {
			list_del(&trans->list);
			kfree(trans);
		} else {
			rc = kstrtou32(req->body, 10, &trans->handle.id);
			if (WARN_ON(rc))
				goto out;
		}
	} else if (req->type == XS_TRANSACTION_END) {
		trans = xenbus_get_transaction(u, req->msg.tx_id);
		if (WARN_ON(!trans))
			goto out;
		list_del(&trans->list);
		kfree(trans);
	}

	mutex_unlock(&u->msgbuffer_mutex);

	mutex_lock(&u->reply_mutex);
	rc = queue_reply(&staging_q, &req->msg, sizeof(req->msg));
	if (!rc)
		rc = queue_reply(&staging_q, req->body, req->msg.len);
	if (!rc) {
		list_splice_tail(&staging_q, &u->read_buffers);
		wake_up(&u->read_waitq);
	} else {
		queue_cleanup(&staging_q);
	}
	mutex_unlock(&u->reply_mutex);

	kfree(req->body);
	kfree(req);

	kref_put(&u->kref, xenbus_file_free);

	return;

 out:
	mutex_unlock(&u->msgbuffer_mutex);
}

static int xenbus_command_reply(struct xenbus_file_priv *u,
				unsigned int msg_type, const char *reply)
{
	struct {
		struct xsd_sockmsg hdr;
		char body[16];
	} msg;
	int rc;

	msg.hdr = u->u.msg;
	msg.hdr.type = msg_type;
	msg.hdr.len = strlen(reply) + 1;
	if (msg.hdr.len > sizeof(msg.body))
		return -E2BIG;
	memcpy(&msg.body, reply, msg.hdr.len);

	mutex_lock(&u->reply_mutex);
	rc = queue_reply(&u->read_buffers, &msg, sizeof(msg.hdr) + msg.hdr.len);
	wake_up(&u->read_waitq);
	mutex_unlock(&u->reply_mutex);

	if (!rc)
		kref_put(&u->kref, xenbus_file_free);

	return rc;
}

static int xenbus_write_transaction(unsigned msg_type,
				    struct xenbus_file_priv *u)
{
	int rc;
	struct xenbus_transaction_holder *trans = NULL;
	struct {
		struct xsd_sockmsg hdr;
		char body[];
	} *msg = (void *)u->u.buffer;

	if (msg_type == XS_TRANSACTION_START) {
		trans = kzalloc(sizeof(*trans), GFP_KERNEL);
		if (!trans) {
			rc = -ENOMEM;
			goto out;
		}
		trans->generation_id = xb_dev_generation_id;
		list_add(&trans->list, &u->transactions);
	} else if (msg->hdr.tx_id != 0 &&
		   !xenbus_get_transaction(u, msg->hdr.tx_id))
		return xenbus_command_reply(u, XS_ERROR, "ENOENT");
	else if (msg_type == XS_TRANSACTION_END &&
		 !(msg->hdr.len == 2 &&
		   (!strcmp(msg->body, "T") || !strcmp(msg->body, "F"))))
		return xenbus_command_reply(u, XS_ERROR, "EINVAL");
	else if (msg_type == XS_TRANSACTION_END) {
		trans = xenbus_get_transaction(u, msg->hdr.tx_id);
		if (trans && trans->generation_id != xb_dev_generation_id) {
			list_del(&trans->list);
			kfree(trans);
			if (!strcmp(msg->body, "T"))
				return xenbus_command_reply(u, XS_ERROR,
							    "EAGAIN");
			else
				return xenbus_command_reply(u,
							    XS_TRANSACTION_END,
							    "OK");
		}
	}

	rc = xenbus_dev_request_and_reply(&msg->hdr, u);
	if (rc && trans) {
		list_del(&trans->list);
		kfree(trans);
	}

out:
	return rc;
}

static int xenbus_write_watch(unsigned msg_type, struct xenbus_file_priv *u)
{
	struct watch_adapter *watch;
	char *path, *token;
	int err, rc;

	path = u->u.buffer + sizeof(u->u.msg);
	token = memchr(path, 0, u->u.msg.len);
	if (token == NULL) {
		rc = xenbus_command_reply(u, XS_ERROR, "EINVAL");
		goto out;
	}
	token++;
	if (memchr(token, 0, u->u.msg.len - (token - path)) == NULL) {
		rc = xenbus_command_reply(u, XS_ERROR, "EINVAL");
		goto out;
	}

	if (msg_type == XS_WATCH) {
		watch = alloc_watch_adapter(path, token);
		if (watch == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		watch->watch.callback = watch_fired;
		watch->dev_data = u;

		err = register_xenbus_watch(&watch->watch);
		if (err) {
			free_watch_adapter(watch);
			rc = err;
			goto out;
		}
		list_add(&watch->list, &u->watches);
	} else {
		list_for_each_entry(watch, &u->watches, list) {
			if (!strcmp(watch->token, token) &&
			    !strcmp(watch->watch.node, path)) {
				unregister_xenbus_watch(&watch->watch);
				list_del(&watch->list);
				free_watch_adapter(watch);
				break;
			}
		}
	}

	/* Success.  Synthesize a reply to say all is OK. */
	rc = xenbus_command_reply(u, msg_type, "OK");

out:
	return rc;
}

static ssize_t xenbus_file_write(struct file *filp,
				const char __user *ubuf,
				size_t len, loff_t *ppos)
{
	struct xenbus_file_priv *u = filp->private_data;
	uint32_t msg_type;
	int rc = len;
	int ret;

	/*
	 * We're expecting usermode to be writing properly formed
	 * xenbus messages.  If they write an incomplete message we
	 * buffer it up.  Once it is complete, we act on it.
	 */

	/*
	 * Make sure concurrent writers can't stomp all over each
	 * other's messages and make a mess of our partial message
	 * buffer.  We don't make any attemppt to stop multiple
	 * writers from making a mess of each other's incomplete
	 * messages; we're just trying to guarantee our own internal
	 * consistency and make sure that single writes are handled
	 * atomically.
	 */
	mutex_lock(&u->msgbuffer_mutex);

	/* Get this out of the way early to avoid confusion */
	if (len == 0)
		goto out;

	/* Can't write a xenbus message larger we can buffer */
	if (len > sizeof(u->u.buffer) - u->len) {
		/* On error, dump existing buffer */
		u->len = 0;
		rc = -EINVAL;
		goto out;
	}

	ret = copy_from_user(u->u.buffer + u->len, ubuf, len);

	if (ret != 0) {
		rc = -EFAULT;
		goto out;
	}

	/* Deal with a partial copy. */
	len -= ret;
	rc = len;

	u->len += len;

	/* Return if we haven't got a full message yet */
	if (u->len < sizeof(u->u.msg))
		goto out;	/* not even the header yet */

	/* If we're expecting a message that's larger than we can
	   possibly send, dump what we have and return an error. */
	if ((sizeof(u->u.msg) + u->u.msg.len) > sizeof(u->u.buffer)) {
		rc = -E2BIG;
		u->len = 0;
		goto out;
	}

	if (u->len < (sizeof(u->u.msg) + u->u.msg.len))
		goto out;	/* incomplete data portion */

	/*
	 * OK, now we have a complete message.  Do something with it.
	 */

	kref_get(&u->kref);

	msg_type = u->u.msg.type;

	switch (msg_type) {
	case XS_WATCH:
	case XS_UNWATCH:
		/* (Un)Ask for some path to be watched for changes */
		ret = xenbus_write_watch(msg_type, u);
		break;

	default:
		/* Send out a transaction */
		ret = xenbus_write_transaction(msg_type, u);
		break;
	}
	if (ret != 0) {
		rc = ret;
		kref_put(&u->kref, xenbus_file_free);
	}

	/* Buffered message consumed */
	u->len = 0;

 out:
	mutex_unlock(&u->msgbuffer_mutex);
	return rc;
}

static int xenbus_file_open(struct inode *inode, struct file *filp)
{
	struct xenbus_file_priv *u;

	if (xen_store_evtchn == 0)
		return -ENOENT;

	stream_open(inode, filp);

	u = kzalloc(sizeof(*u), GFP_KERNEL);
	if (u == NULL)
		return -ENOMEM;

	kref_init(&u->kref);

	INIT_LIST_HEAD(&u->transactions);
	INIT_LIST_HEAD(&u->watches);
	INIT_LIST_HEAD(&u->read_buffers);
	init_waitqueue_head(&u->read_waitq);
	INIT_WORK(&u->wq, xenbus_worker);

	mutex_init(&u->reply_mutex);
	mutex_init(&u->msgbuffer_mutex);

	filp->private_data = u;

	return 0;
}

static int xenbus_file_release(struct inode *inode, struct file *filp)
{
	struct xenbus_file_priv *u = filp->private_data;

	kref_put(&u->kref, xenbus_file_free);

	return 0;
}

static __poll_t xenbus_file_poll(struct file *file, poll_table *wait)
{
	struct xenbus_file_priv *u = file->private_data;

	poll_wait(file, &u->read_waitq, wait);
	if (!list_empty(&u->read_buffers))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

const struct file_operations xen_xenbus_fops = {
	.read = xenbus_file_read,
	.write = xenbus_file_write,
	.open = xenbus_file_open,
	.release = xenbus_file_release,
	.poll = xenbus_file_poll,
	.llseek = no_llseek,
};
EXPORT_SYMBOL_GPL(xen_xenbus_fops);

static struct miscdevice xenbus_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xen/xenbus",
	.fops = &xen_xenbus_fops,
};

static int __init xenbus_init(void)
{
	int err;

	if (!xen_domain())
		return -ENODEV;

	err = misc_register(&xenbus_dev);
	if (err)
		pr_err("Could not register xenbus frontend device\n");
	return err;
}
device_initcall(xenbus_init);
