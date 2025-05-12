/******************************************************************************
 * xenbus_xs.c
 *
 * This is the kernel equivalent of the "xs" library.  We don't need everything
 * and we use xenbus_comms for communication.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/unistd.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/kthread.h>
#include <linux/reboot.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <asm/xen/hypervisor.h>
#include <xen/xenbus.h>
#include <xen/xen.h>
#include "xenbus.h"

/*
 * Framework to protect suspend/resume handling against normal Xenstore
 * message handling:
 * During suspend/resume there must be no open transaction and no pending
 * Xenstore request.
 * New watch events happening in this time can be ignored by firing all watches
 * after resume.
 */

/* Lock protecting enter/exit critical region. */
static DEFINE_SPINLOCK(xs_state_lock);
/* Number of users in critical region (protected by xs_state_lock). */
static unsigned int xs_state_users;
/* Suspend handler waiting or already active (protected by xs_state_lock)? */
static int xs_suspend_active;
/* Unique Xenstore request id (protected by xs_state_lock). */
static uint32_t xs_request_id;

/* Wait queue for all callers waiting for critical region to become usable. */
static DECLARE_WAIT_QUEUE_HEAD(xs_state_enter_wq);
/* Wait queue for suspend handling waiting for critical region being empty. */
static DECLARE_WAIT_QUEUE_HEAD(xs_state_exit_wq);

/* List of registered watches, and a lock to protect it. */
static LIST_HEAD(watches);
static DEFINE_SPINLOCK(watches_lock);

/* List of pending watch callback events, and a lock to protect it. */
static LIST_HEAD(watch_events);
static DEFINE_SPINLOCK(watch_events_lock);

/* Protect watch (de)register against save/restore. */
static DECLARE_RWSEM(xs_watch_rwsem);

/*
 * Details of the xenwatch callback kernel thread. The thread waits on the
 * watch_events_waitq for work to do (queued on watch_events list). When it
 * wakes up it acquires the xenwatch_mutex before reading the list and
 * carrying out work.
 */
static pid_t xenwatch_pid;
static DEFINE_MUTEX(xenwatch_mutex);
static DECLARE_WAIT_QUEUE_HEAD(watch_events_waitq);

static void xs_suspend_enter(void)
{
	spin_lock(&xs_state_lock);
	xs_suspend_active++;
	spin_unlock(&xs_state_lock);
	wait_event(xs_state_exit_wq, xs_state_users == 0);
}

static void xs_suspend_exit(void)
{
	xb_dev_generation_id++;
	spin_lock(&xs_state_lock);
	xs_suspend_active--;
	spin_unlock(&xs_state_lock);
	wake_up_all(&xs_state_enter_wq);
}

void xs_free_req(struct kref *kref)
{
	struct xb_req_data *req = container_of(kref, struct xb_req_data, kref);
	kfree(req);
}

static uint32_t xs_request_enter(struct xb_req_data *req)
{
	uint32_t rq_id;

	req->type = req->msg.type;

	spin_lock(&xs_state_lock);

	while (!xs_state_users && xs_suspend_active) {
		spin_unlock(&xs_state_lock);
		wait_event(xs_state_enter_wq, xs_suspend_active == 0);
		spin_lock(&xs_state_lock);
	}

	if (req->type == XS_TRANSACTION_START && !req->user_req)
		xs_state_users++;
	xs_state_users++;
	rq_id = xs_request_id++;

	spin_unlock(&xs_state_lock);

	return rq_id;
}

void xs_request_exit(struct xb_req_data *req)
{
	spin_lock(&xs_state_lock);
	xs_state_users--;
	if ((req->type == XS_TRANSACTION_START && req->msg.type == XS_ERROR) ||
	    (req->type == XS_TRANSACTION_END && !req->user_req &&
	     !WARN_ON_ONCE(req->msg.type == XS_ERROR &&
			   !strcmp(req->body, "ENOENT"))))
		xs_state_users--;
	spin_unlock(&xs_state_lock);

	if (xs_suspend_active && !xs_state_users)
		wake_up(&xs_state_exit_wq);
}

static int get_error(const char *errorstring)
{
	unsigned int i;

	for (i = 0; strcmp(errorstring, xsd_errors[i].errstring) != 0; i++) {
		if (i == ARRAY_SIZE(xsd_errors) - 1) {
			pr_warn("xen store gave: unknown error %s\n",
				errorstring);
			return EINVAL;
		}
	}
	return xsd_errors[i].errnum;
}

static bool xenbus_ok(void)
{
	switch (xen_store_domain_type) {
	case XS_LOCAL:
		switch (system_state) {
		case SYSTEM_POWER_OFF:
		case SYSTEM_RESTART:
		case SYSTEM_HALT:
			return false;
		default:
			break;
		}
		return true;
	case XS_PV:
	case XS_HVM:
		/* FIXME: Could check that the remote domain is alive,
		 * but it is normally initial domain. */
		return true;
	default:
		break;
	}
	return false;
}

static bool test_reply(struct xb_req_data *req)
{
	if (req->state == xb_req_state_got_reply || !xenbus_ok()) {
		/* read req->state before all other fields */
		virt_rmb();
		return true;
	}

	/* Make sure to reread req->state each time. */
	barrier();

	return false;
}

static void *read_reply(struct xb_req_data *req)
{
	do {
		wait_event(req->wq, test_reply(req));

		if (!xenbus_ok())
			/*
			 * If we are in the process of being shut-down there is
			 * no point of trying to contact XenBus - it is either
			 * killed (xenstored application) or the other domain
			 * has been killed or is unreachable.
			 */
			return ERR_PTR(-EIO);
		if (req->err)
			return ERR_PTR(req->err);

	} while (req->state != xb_req_state_got_reply);

	return req->body;
}

static void xs_send(struct xb_req_data *req, struct xsd_sockmsg *msg)
{
	bool notify;

	req->msg = *msg;
	req->err = 0;
	req->state = xb_req_state_queued;
	init_waitqueue_head(&req->wq);

	/* Save the caller req_id and restore it later in the reply */
	req->caller_req_id = req->msg.req_id;
	req->msg.req_id = xs_request_enter(req);

	/*
	 * Take 2nd ref.  One for this thread, and the second for the
	 * xenbus_thread.
	 */
	kref_get(&req->kref);

	mutex_lock(&xb_write_mutex);
	list_add_tail(&req->list, &xb_write_list);
	notify = list_is_singular(&xb_write_list);
	mutex_unlock(&xb_write_mutex);

	if (notify)
		wake_up(&xb_waitq);
}

static void *xs_wait_for_reply(struct xb_req_data *req, struct xsd_sockmsg *msg)
{
	void *ret;

	ret = read_reply(req);

	xs_request_exit(req);

	msg->type = req->msg.type;
	msg->len = req->msg.len;

	mutex_lock(&xb_write_mutex);
	if (req->state == xb_req_state_queued ||
	    req->state == xb_req_state_wait_reply)
		req->state = xb_req_state_aborted;

	kref_put(&req->kref, xs_free_req);
	mutex_unlock(&xb_write_mutex);

	return ret;
}

static void xs_wake_up(struct xb_req_data *req)
{
	wake_up(&req->wq);
}

int xenbus_dev_request_and_reply(struct xsd_sockmsg *msg, void *par)
{
	struct xb_req_data *req;
	struct kvec *vec;

	req = kmalloc(sizeof(*req) + sizeof(*vec), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	vec = (struct kvec *)(req + 1);
	vec->iov_len = msg->len;
	vec->iov_base = msg + 1;

	req->vec = vec;
	req->num_vecs = 1;
	req->cb = xenbus_dev_queue_reply;
	req->par = par;
	req->user_req = true;
	kref_init(&req->kref);

	xs_send(req, msg);

	return 0;
}
EXPORT_SYMBOL(xenbus_dev_request_and_reply);

/* Send message to xs, get kmalloc'ed reply.  ERR_PTR() on error. */
static void *xs_talkv(struct xenbus_transaction t,
		      enum xsd_sockmsg_type type,
		      const struct kvec *iovec,
		      unsigned int num_vecs,
		      unsigned int *len)
{
	struct xb_req_data *req;
	struct xsd_sockmsg msg;
	void *ret = NULL;
	unsigned int i;
	int err;

	req = kmalloc(sizeof(*req), GFP_NOIO | __GFP_HIGH);
	if (!req)
		return ERR_PTR(-ENOMEM);

	req->vec = iovec;
	req->num_vecs = num_vecs;
	req->cb = xs_wake_up;
	req->user_req = false;
	kref_init(&req->kref);

	msg.req_id = 0;
	msg.tx_id = t.id;
	msg.type = type;
	msg.len = 0;
	for (i = 0; i < num_vecs; i++)
		msg.len += iovec[i].iov_len;

	xs_send(req, &msg);

	ret = xs_wait_for_reply(req, &msg);
	if (len)
		*len = msg.len;

	if (IS_ERR(ret))
		return ret;

	if (msg.type == XS_ERROR) {
		err = get_error(ret);
		kfree(ret);
		return ERR_PTR(-err);
	}

	if (msg.type != type) {
		pr_warn_ratelimited("unexpected type [%d], expected [%d]\n",
				    msg.type, type);
		kfree(ret);
		return ERR_PTR(-EINVAL);
	}
	return ret;
}

/* Simplified version of xs_talkv: single message. */
static void *xs_single(struct xenbus_transaction t,
		       enum xsd_sockmsg_type type,
		       const char *string,
		       unsigned int *len)
{
	struct kvec iovec;

	iovec.iov_base = (void *)string;
	iovec.iov_len = strlen(string) + 1;
	return xs_talkv(t, type, &iovec, 1, len);
}

/* Many commands only need an ack, don't care what it says. */
static int xs_error(char *reply)
{
	if (IS_ERR(reply))
		return PTR_ERR(reply);
	kfree(reply);
	return 0;
}

static unsigned int count_strings(const char *strings, unsigned int len)
{
	unsigned int num;
	const char *p;

	for (p = strings, num = 0; p < strings + len; p += strlen(p) + 1)
		num++;

	return num;
}

/* Return the path to dir with /name appended. Buffer must be kfree()'ed. */
static char *join(const char *dir, const char *name)
{
	char *buffer;

	if (strlen(name) == 0)
		buffer = kasprintf(GFP_NOIO | __GFP_HIGH, "%s", dir);
	else
		buffer = kasprintf(GFP_NOIO | __GFP_HIGH, "%s/%s", dir, name);
	return (!buffer) ? ERR_PTR(-ENOMEM) : buffer;
}

static char **split(char *strings, unsigned int len, unsigned int *num)
{
	char *p, **ret;

	/* Count the strings. */
	*num = count_strings(strings, len);

	/* Transfer to one big alloc for easy freeing. */
	ret = kmalloc(*num * sizeof(char *) + len, GFP_NOIO | __GFP_HIGH);
	if (!ret) {
		kfree(strings);
		return ERR_PTR(-ENOMEM);
	}
	memcpy(&ret[*num], strings, len);
	kfree(strings);

	strings = (char *)&ret[*num];
	for (p = strings, *num = 0; p < strings + len; p += strlen(p) + 1)
		ret[(*num)++] = p;

	return ret;
}

char **xenbus_directory(struct xenbus_transaction t,
			const char *dir, const char *node, unsigned int *num)
{
	char *strings, *path;
	unsigned int len;

	path = join(dir, node);
	if (IS_ERR(path))
		return ERR_CAST(path);

	strings = xs_single(t, XS_DIRECTORY, path, &len);
	kfree(path);
	if (IS_ERR(strings))
		return ERR_CAST(strings);

	return split(strings, len, num);
}
EXPORT_SYMBOL_GPL(xenbus_directory);

/* Check if a path exists. Return 1 if it does. */
int xenbus_exists(struct xenbus_transaction t,
		  const char *dir, const char *node)
{
	char **d;
	int dir_n;

	d = xenbus_directory(t, dir, node, &dir_n);
	if (IS_ERR(d))
		return 0;
	kfree(d);
	return 1;
}
EXPORT_SYMBOL_GPL(xenbus_exists);

/* Get the value of a single file.
 * Returns a kmalloced value: call free() on it after use.
 * len indicates length in bytes.
 */
void *xenbus_read(struct xenbus_transaction t,
		  const char *dir, const char *node, unsigned int *len)
{
	char *path;
	void *ret;

	path = join(dir, node);
	if (IS_ERR(path))
		return ERR_CAST(path);

	ret = xs_single(t, XS_READ, path, len);
	kfree(path);
	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_read);

/* Write the value of a single file.
 * Returns -err on failure.
 */
int xenbus_write(struct xenbus_transaction t,
		 const char *dir, const char *node, const char *string)
{
	const char *path;
	struct kvec iovec[2];
	int ret;

	path = join(dir, node);
	if (IS_ERR(path))
		return PTR_ERR(path);

	iovec[0].iov_base = (void *)path;
	iovec[0].iov_len = strlen(path) + 1;
	iovec[1].iov_base = (void *)string;
	iovec[1].iov_len = strlen(string);

	ret = xs_error(xs_talkv(t, XS_WRITE, iovec, ARRAY_SIZE(iovec), NULL));
	kfree(path);
	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_write);

/* Create a new directory. */
int xenbus_mkdir(struct xenbus_transaction t,
		 const char *dir, const char *node)
{
	char *path;
	int ret;

	path = join(dir, node);
	if (IS_ERR(path))
		return PTR_ERR(path);

	ret = xs_error(xs_single(t, XS_MKDIR, path, NULL));
	kfree(path);
	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_mkdir);

/* Destroy a file or directory (directories must be empty). */
int xenbus_rm(struct xenbus_transaction t, const char *dir, const char *node)
{
	char *path;
	int ret;

	path = join(dir, node);
	if (IS_ERR(path))
		return PTR_ERR(path);

	ret = xs_error(xs_single(t, XS_RM, path, NULL));
	kfree(path);
	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_rm);

/* Start a transaction: changes by others will not be seen during this
 * transaction, and changes will not be visible to others until end.
 */
int xenbus_transaction_start(struct xenbus_transaction *t)
{
	char *id_str;

	id_str = xs_single(XBT_NIL, XS_TRANSACTION_START, "", NULL);
	if (IS_ERR(id_str))
		return PTR_ERR(id_str);

	t->id = simple_strtoul(id_str, NULL, 0);
	kfree(id_str);
	return 0;
}
EXPORT_SYMBOL_GPL(xenbus_transaction_start);

/* End a transaction.
 * If abandon is true, transaction is discarded instead of committed.
 */
int xenbus_transaction_end(struct xenbus_transaction t, int abort)
{
	char abortstr[2];

	if (abort)
		strcpy(abortstr, "F");
	else
		strcpy(abortstr, "T");

	return xs_error(xs_single(t, XS_TRANSACTION_END, abortstr, NULL));
}
EXPORT_SYMBOL_GPL(xenbus_transaction_end);

/* Single read and scanf: returns -errno or num scanned. */
int xenbus_scanf(struct xenbus_transaction t,
		 const char *dir, const char *node, const char *fmt, ...)
{
	va_list ap;
	int ret;
	char *val;

	val = xenbus_read(t, dir, node, NULL);
	if (IS_ERR(val))
		return PTR_ERR(val);

	va_start(ap, fmt);
	ret = vsscanf(val, fmt, ap);
	va_end(ap);
	kfree(val);
	/* Distinctive errno. */
	if (ret == 0)
		return -ERANGE;
	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_scanf);

/* Read an (optional) unsigned value. */
unsigned int xenbus_read_unsigned(const char *dir, const char *node,
				  unsigned int default_val)
{
	unsigned int val;
	int ret;

	ret = xenbus_scanf(XBT_NIL, dir, node, "%u", &val);
	if (ret <= 0)
		val = default_val;

	return val;
}
EXPORT_SYMBOL_GPL(xenbus_read_unsigned);

/* Single printf and write: returns -errno or 0. */
int xenbus_printf(struct xenbus_transaction t,
		  const char *dir, const char *node, const char *fmt, ...)
{
	va_list ap;
	int ret;
	char *buf;

	va_start(ap, fmt);
	buf = kvasprintf(GFP_NOIO | __GFP_HIGH, fmt, ap);
	va_end(ap);

	if (!buf)
		return -ENOMEM;

	ret = xenbus_write(t, dir, node, buf);

	kfree(buf);

	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_printf);

/* Takes tuples of names, scanf-style args, and void **, NULL terminated. */
int xenbus_gather(struct xenbus_transaction t, const char *dir, ...)
{
	va_list ap;
	const char *name;
	int ret = 0;

	va_start(ap, dir);
	while (ret == 0 && (name = va_arg(ap, char *)) != NULL) {
		const char *fmt = va_arg(ap, char *);
		void *result = va_arg(ap, void *);
		char *p;

		p = xenbus_read(t, dir, name, NULL);
		if (IS_ERR(p)) {
			ret = PTR_ERR(p);
			break;
		}
		if (fmt) {
			if (sscanf(p, fmt, result) == 0)
				ret = -EINVAL;
			kfree(p);
		} else
			*(char **)result = p;
	}
	va_end(ap);
	return ret;
}
EXPORT_SYMBOL_GPL(xenbus_gather);

static int xs_watch(const char *path, const char *token)
{
	struct kvec iov[2];

	iov[0].iov_base = (void *)path;
	iov[0].iov_len = strlen(path) + 1;
	iov[1].iov_base = (void *)token;
	iov[1].iov_len = strlen(token) + 1;

	return xs_error(xs_talkv(XBT_NIL, XS_WATCH, iov,
				 ARRAY_SIZE(iov), NULL));
}

static int xs_unwatch(const char *path, const char *token)
{
	struct kvec iov[2];

	iov[0].iov_base = (char *)path;
	iov[0].iov_len = strlen(path) + 1;
	iov[1].iov_base = (char *)token;
	iov[1].iov_len = strlen(token) + 1;

	return xs_error(xs_talkv(XBT_NIL, XS_UNWATCH, iov,
				 ARRAY_SIZE(iov), NULL));
}

static struct xenbus_watch *find_watch(const char *token)
{
	struct xenbus_watch *i, *cmp;

	cmp = (void *)simple_strtoul(token, NULL, 16);

	list_for_each_entry(i, &watches, list)
		if (i == cmp)
			return i;

	return NULL;
}

int xs_watch_msg(struct xs_watch_event *event)
{
	if (count_strings(event->body, event->len) != 2) {
		kfree(event);
		return -EINVAL;
	}
	event->path = (const char *)event->body;
	event->token = (const char *)strchr(event->body, '\0') + 1;

	spin_lock(&watches_lock);
	event->handle = find_watch(event->token);
	if (event->handle != NULL &&
			(!event->handle->will_handle ||
			 event->handle->will_handle(event->handle,
				 event->path, event->token))) {
		spin_lock(&watch_events_lock);
		list_add_tail(&event->list, &watch_events);
		event->handle->nr_pending++;
		wake_up(&watch_events_waitq);
		spin_unlock(&watch_events_lock);
	} else
		kfree(event);
	spin_unlock(&watches_lock);

	return 0;
}

/*
 * Certain older XenBus toolstack cannot handle reading values that are
 * not populated. Some Xen 3.4 installation are incapable of doing this
 * so if we are running on anything older than 4 do not attempt to read
 * control/platform-feature-xs_reset_watches.
 */
static bool xen_strict_xenbus_quirk(void)
{
#ifdef CONFIG_X86
	uint32_t eax, ebx, ecx, edx, base;

	base = xen_cpuid_base();
	cpuid(base + 1, &eax, &ebx, &ecx, &edx);

	if ((eax >> 16) < 4)
		return true;
#endif
	return false;

}
static void xs_reset_watches(void)
{
	int err;

	if (!xen_hvm_domain() || xen_initial_domain())
		return;

	if (xen_strict_xenbus_quirk())
		return;

	if (!xenbus_read_unsigned("control",
				  "platform-feature-xs_reset_watches", 0))
		return;

	err = xs_error(xs_single(XBT_NIL, XS_RESET_WATCHES, "", NULL));
	if (err && err != -EEXIST)
		pr_warn("xs_reset_watches failed: %d\n", err);
}

/* Register callback to watch this node. */
int register_xenbus_watch(struct xenbus_watch *watch)
{
	/* Pointer in ascii is the token. */
	char token[sizeof(watch) * 2 + 1];
	int err;

	sprintf(token, "%lX", (long)watch);

	watch->nr_pending = 0;

	down_read(&xs_watch_rwsem);

	spin_lock(&watches_lock);
	BUG_ON(find_watch(token));
	list_add(&watch->list, &watches);
	spin_unlock(&watches_lock);

	err = xs_watch(watch->node, token);

	if (err) {
		spin_lock(&watches_lock);
		list_del(&watch->list);
		spin_unlock(&watches_lock);
	}

	up_read(&xs_watch_rwsem);

	return err;
}
EXPORT_SYMBOL_GPL(register_xenbus_watch);

void unregister_xenbus_watch(struct xenbus_watch *watch)
{
	struct xs_watch_event *event, *tmp;
	char token[sizeof(watch) * 2 + 1];
	int err;

	sprintf(token, "%lX", (long)watch);

	down_read(&xs_watch_rwsem);

	spin_lock(&watches_lock);
	BUG_ON(!find_watch(token));
	list_del(&watch->list);
	spin_unlock(&watches_lock);

	err = xs_unwatch(watch->node, token);
	if (err)
		pr_warn("Failed to release watch %s: %i\n", watch->node, err);

	up_read(&xs_watch_rwsem);

	/* Make sure there are no callbacks running currently (unless
	   its us) */
	if (current->pid != xenwatch_pid)
		mutex_lock(&xenwatch_mutex);

	/* Cancel pending watch events. */
	spin_lock(&watch_events_lock);
	if (watch->nr_pending) {
		list_for_each_entry_safe(event, tmp, &watch_events, list) {
			if (event->handle != watch)
				continue;
			list_del(&event->list);
			kfree(event);
		}
		watch->nr_pending = 0;
	}
	spin_unlock(&watch_events_lock);

	if (current->pid != xenwatch_pid)
		mutex_unlock(&xenwatch_mutex);
}
EXPORT_SYMBOL_GPL(unregister_xenbus_watch);

void xs_suspend(void)
{
	xs_suspend_enter();

	mutex_lock(&xs_response_mutex);
	down_write(&xs_watch_rwsem);
}

void xs_resume(void)
{
	struct xenbus_watch *watch;
	char token[sizeof(watch) * 2 + 1];

	xb_init_comms();

	mutex_unlock(&xs_response_mutex);

	xs_suspend_exit();

	/* No need for watches_lock: the xs_watch_rwsem is sufficient. */
	list_for_each_entry(watch, &watches, list) {
		sprintf(token, "%lX", (long)watch);
		xs_watch(watch->node, token);
	}

	up_write(&xs_watch_rwsem);
}

void xs_suspend_cancel(void)
{
	up_write(&xs_watch_rwsem);
	mutex_unlock(&xs_response_mutex);

	xs_suspend_exit();
}

static int xenwatch_thread(void *unused)
{
	struct xs_watch_event *event;

	xenwatch_pid = current->pid;

	for (;;) {
		wait_event_interruptible(watch_events_waitq,
					 !list_empty(&watch_events));

		if (kthread_should_stop())
			break;

		mutex_lock(&xenwatch_mutex);

		spin_lock(&watch_events_lock);
		event = list_first_entry_or_null(&watch_events,
				struct xs_watch_event, list);
		if (event) {
			list_del(&event->list);
			event->handle->nr_pending--;
		}
		spin_unlock(&watch_events_lock);

		if (event) {
			event->handle->callback(event->handle, event->path,
						event->token);
			kfree(event);
		}

		mutex_unlock(&xenwatch_mutex);
	}

	return 0;
}

/*
 * Wake up all threads waiting for a xenstore reply. In case of shutdown all
 * pending replies will be marked as "aborted" in order to let the waiters
 * return in spite of xenstore possibly no longer being able to reply. This
 * will avoid blocking shutdown by a thread waiting for xenstore but being
 * necessary for shutdown processing to proceed.
 */
static int xs_reboot_notify(struct notifier_block *nb,
			    unsigned long code, void *unused)
{
	struct xb_req_data *req;

	mutex_lock(&xb_write_mutex);
	list_for_each_entry(req, &xs_reply_list, list)
		wake_up(&req->wq);
	list_for_each_entry(req, &xb_write_list, list)
		wake_up(&req->wq);
	mutex_unlock(&xb_write_mutex);
	return NOTIFY_DONE;
}

static struct notifier_block xs_reboot_nb = {
	.notifier_call = xs_reboot_notify,
};

int xs_init(void)
{
	int err;
	struct task_struct *task;

	register_reboot_notifier(&xs_reboot_nb);

	/* Initialize the shared memory rings to talk to xenstored */
	err = xb_init_comms();
	if (err)
		return err;

	task = kthread_run(xenwatch_thread, NULL, "xenwatch");
	if (IS_ERR(task))
		return PTR_ERR(task);

	/* shutdown watches for kexec boot */
	xs_reset_watches();

	return 0;
}
