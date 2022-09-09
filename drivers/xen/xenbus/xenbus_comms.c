/******************************************************************************
 * xenbus_comms.c
 *
 * Low level code to talks to Xen Store: ringbuffer and event channel.
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

#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <xen/xenbus.h>
#include <asm/xen/hypervisor.h>
#include <xen/events.h>
#include <xen/page.h>
#include "xenbus.h"

/* A list of replies. Currently only one will ever be outstanding. */
LIST_HEAD(xs_reply_list);

/* A list of write requests. */
LIST_HEAD(xb_write_list);
DECLARE_WAIT_QUEUE_HEAD(xb_waitq);
DEFINE_MUTEX(xb_write_mutex);

/* Protect xenbus reader thread against save/restore. */
DEFINE_MUTEX(xs_response_mutex);

static int xenbus_irq;
static struct task_struct *xenbus_task;

static irqreturn_t wake_waiting(int irq, void *unused)
{
	wake_up(&xb_waitq);
	return IRQ_HANDLED;
}

static int check_indexes(XENSTORE_RING_IDX cons, XENSTORE_RING_IDX prod)
{
	return ((prod - cons) <= XENSTORE_RING_SIZE);
}

static void *get_output_chunk(XENSTORE_RING_IDX cons,
			      XENSTORE_RING_IDX prod,
			      char *buf, uint32_t *len)
{
	*len = XENSTORE_RING_SIZE - MASK_XENSTORE_IDX(prod);
	if ((XENSTORE_RING_SIZE - (prod - cons)) < *len)
		*len = XENSTORE_RING_SIZE - (prod - cons);
	return buf + MASK_XENSTORE_IDX(prod);
}

static const void *get_input_chunk(XENSTORE_RING_IDX cons,
				   XENSTORE_RING_IDX prod,
				   const char *buf, uint32_t *len)
{
	*len = XENSTORE_RING_SIZE - MASK_XENSTORE_IDX(cons);
	if ((prod - cons) < *len)
		*len = prod - cons;
	return buf + MASK_XENSTORE_IDX(cons);
}

static int xb_data_to_write(void)
{
	struct xenstore_domain_interface *intf = xen_store_interface;

	return (intf->req_prod - intf->req_cons) != XENSTORE_RING_SIZE &&
		!list_empty(&xb_write_list);
}

/**
 * xb_write - low level write
 * @data: buffer to send
 * @len: length of buffer
 *
 * Returns number of bytes written or -err.
 */
static int xb_write(const void *data, unsigned int len)
{
	struct xenstore_domain_interface *intf = xen_store_interface;
	XENSTORE_RING_IDX cons, prod;
	unsigned int bytes = 0;

	while (len != 0) {
		void *dst;
		unsigned int avail;

		/* Read indexes, then verify. */
		cons = intf->req_cons;
		prod = intf->req_prod;
		if (!check_indexes(cons, prod)) {
			intf->req_cons = intf->req_prod = 0;
			return -EIO;
		}
		if (!xb_data_to_write())
			return bytes;

		/* Must write data /after/ reading the consumer index. */
		virt_mb();

		dst = get_output_chunk(cons, prod, intf->req, &avail);
		if (avail == 0)
			continue;
		if (avail > len)
			avail = len;

		memcpy(dst, data, avail);
		data += avail;
		len -= avail;
		bytes += avail;

		/* Other side must not see new producer until data is there. */
		virt_wmb();
		intf->req_prod += avail;

		/* Implies mb(): other side will see the updated producer. */
		if (prod <= intf->req_cons)
			notify_remote_via_evtchn(xen_store_evtchn);
	}

	return bytes;
}

static int xb_data_to_read(void)
{
	struct xenstore_domain_interface *intf = xen_store_interface;
	return (intf->rsp_cons != intf->rsp_prod);
}

static int xb_read(void *data, unsigned int len)
{
	struct xenstore_domain_interface *intf = xen_store_interface;
	XENSTORE_RING_IDX cons, prod;
	unsigned int bytes = 0;

	while (len != 0) {
		unsigned int avail;
		const char *src;

		/* Read indexes, then verify. */
		cons = intf->rsp_cons;
		prod = intf->rsp_prod;
		if (cons == prod)
			return bytes;

		if (!check_indexes(cons, prod)) {
			intf->rsp_cons = intf->rsp_prod = 0;
			return -EIO;
		}

		src = get_input_chunk(cons, prod, intf->rsp, &avail);
		if (avail == 0)
			continue;
		if (avail > len)
			avail = len;

		/* Must read data /after/ reading the producer index. */
		virt_rmb();

		memcpy(data, src, avail);
		data += avail;
		len -= avail;
		bytes += avail;

		/* Other side must not see free space until we've copied out */
		virt_mb();
		intf->rsp_cons += avail;

		/* Implies mb(): other side will see the updated consumer. */
		if (intf->rsp_prod - cons >= XENSTORE_RING_SIZE)
			notify_remote_via_evtchn(xen_store_evtchn);
	}

	return bytes;
}

static int process_msg(void)
{
	static struct {
		struct xsd_sockmsg msg;
		char *body;
		union {
			void *alloc;
			struct xs_watch_event *watch;
		};
		bool in_msg;
		bool in_hdr;
		unsigned int read;
	} state;
	struct xb_req_data *req;
	int err;
	unsigned int len;

	if (!state.in_msg) {
		state.in_msg = true;
		state.in_hdr = true;
		state.read = 0;

		/*
		 * We must disallow save/restore while reading a message.
		 * A partial read across s/r leaves us out of sync with
		 * xenstored.
		 * xs_response_mutex is locked as long as we are processing one
		 * message. state.in_msg will be true as long as we are holding
		 * the lock here.
		 */
		mutex_lock(&xs_response_mutex);

		if (!xb_data_to_read()) {
			/* We raced with save/restore: pending data 'gone'. */
			mutex_unlock(&xs_response_mutex);
			state.in_msg = false;
			return 0;
		}
	}

	if (state.in_hdr) {
		if (state.read != sizeof(state.msg)) {
			err = xb_read((void *)&state.msg + state.read,
				      sizeof(state.msg) - state.read);
			if (err < 0)
				goto out;
			state.read += err;
			if (state.read != sizeof(state.msg))
				return 0;
			if (state.msg.len > XENSTORE_PAYLOAD_MAX) {
				err = -EINVAL;
				goto out;
			}
		}

		len = state.msg.len + 1;
		if (state.msg.type == XS_WATCH_EVENT)
			len += sizeof(*state.watch);

		state.alloc = kmalloc(len, GFP_NOIO | __GFP_HIGH);
		if (!state.alloc)
			return -ENOMEM;

		if (state.msg.type == XS_WATCH_EVENT)
			state.body = state.watch->body;
		else
			state.body = state.alloc;
		state.in_hdr = false;
		state.read = 0;
	}

	err = xb_read(state.body + state.read, state.msg.len - state.read);
	if (err < 0)
		goto out;

	state.read += err;
	if (state.read != state.msg.len)
		return 0;

	state.body[state.msg.len] = '\0';

	if (state.msg.type == XS_WATCH_EVENT) {
		state.watch->len = state.msg.len;
		err = xs_watch_msg(state.watch);
	} else {
		err = -ENOENT;
		mutex_lock(&xb_write_mutex);
		list_for_each_entry(req, &xs_reply_list, list) {
			if (req->msg.req_id == state.msg.req_id) {
				list_del(&req->list);
				err = 0;
				break;
			}
		}
		mutex_unlock(&xb_write_mutex);
		if (err)
			goto out;

		if (req->state == xb_req_state_wait_reply) {
			req->msg.req_id = req->caller_req_id;
			req->msg.type = state.msg.type;
			req->msg.len = state.msg.len;
			req->body = state.body;
			/* write body, then update state */
			virt_wmb();
			req->state = xb_req_state_got_reply;
			req->cb(req);
		} else
			kfree(req);
	}

	mutex_unlock(&xs_response_mutex);

	state.in_msg = false;
	state.alloc = NULL;
	return err;

 out:
	mutex_unlock(&xs_response_mutex);
	state.in_msg = false;
	kfree(state.alloc);
	state.alloc = NULL;
	return err;
}

static int process_writes(void)
{
	static struct {
		struct xb_req_data *req;
		int idx;
		unsigned int written;
	} state;
	void *base;
	unsigned int len;
	int err = 0;

	if (!xb_data_to_write())
		return 0;

	mutex_lock(&xb_write_mutex);

	if (!state.req) {
		state.req = list_first_entry(&xb_write_list,
					     struct xb_req_data, list);
		state.idx = -1;
		state.written = 0;
	}

	if (state.req->state == xb_req_state_aborted)
		goto out_err;

	while (state.idx < state.req->num_vecs) {
		if (state.idx < 0) {
			base = &state.req->msg;
			len = sizeof(state.req->msg);
		} else {
			base = state.req->vec[state.idx].iov_base;
			len = state.req->vec[state.idx].iov_len;
		}
		err = xb_write(base + state.written, len - state.written);
		if (err < 0)
			goto out_err;
		state.written += err;
		if (state.written != len)
			goto out;

		state.idx++;
		state.written = 0;
	}

	list_del(&state.req->list);
	state.req->state = xb_req_state_wait_reply;
	list_add_tail(&state.req->list, &xs_reply_list);
	state.req = NULL;

 out:
	mutex_unlock(&xb_write_mutex);

	return 0;

 out_err:
	state.req->msg.type = XS_ERROR;
	state.req->err = err;
	list_del(&state.req->list);
	if (state.req->state == xb_req_state_aborted)
		kfree(state.req);
	else {
		/* write err, then update state */
		virt_wmb();
		state.req->state = xb_req_state_got_reply;
		wake_up(&state.req->wq);
	}

	mutex_unlock(&xb_write_mutex);

	state.req = NULL;

	return err;
}

static int xb_thread_work(void)
{
	return xb_data_to_read() || xb_data_to_write();
}

static int xenbus_thread(void *unused)
{
	int err;

	while (!kthread_should_stop()) {
		if (wait_event_interruptible(xb_waitq, xb_thread_work()))
			continue;

		err = process_msg();
		if (err == -ENOMEM)
			schedule();
		else if (err)
			pr_warn_ratelimited("error %d while reading message\n",
					    err);

		err = process_writes();
		if (err)
			pr_warn_ratelimited("error %d while writing message\n",
					    err);
	}

	xenbus_task = NULL;
	return 0;
}

/**
 * xb_init_comms - Set up interrupt handler off store event channel.
 */
int xb_init_comms(void)
{
	struct xenstore_domain_interface *intf = xen_store_interface;

	if (intf->req_prod != intf->req_cons)
		pr_err("request ring is not quiescent (%08x:%08x)!\n",
		       intf->req_cons, intf->req_prod);

	if (intf->rsp_prod != intf->rsp_cons) {
		pr_warn("response ring is not quiescent (%08x:%08x): fixing up\n",
			intf->rsp_cons, intf->rsp_prod);
		/* breaks kdump */
		if (!reset_devices)
			intf->rsp_cons = intf->rsp_prod;
	}

	if (xenbus_irq) {
		/* Already have an irq; assume we're resuming */
		rebind_evtchn_irq(xen_store_evtchn, xenbus_irq);
	} else {
		int err;

		err = bind_evtchn_to_irqhandler(xen_store_evtchn, wake_waiting,
						0, "xenbus", &xb_waitq);
		if (err < 0) {
			pr_err("request irq failed %i\n", err);
			return err;
		}

		xenbus_irq = err;

		if (!xenbus_task) {
			xenbus_task = kthread_run(xenbus_thread, NULL,
						  "xenbus");
			if (IS_ERR(xenbus_task))
				return PTR_ERR(xenbus_task);
		}
	}

	return 0;
}

void xb_deinit_comms(void)
{
	unbind_from_irqhandler(xenbus_irq, &xb_waitq);
	xenbus_irq = 0;
}
