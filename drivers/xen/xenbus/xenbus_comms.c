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

#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <xen/xenbus.h>
#include <asm/xen/hypervisor.h>
#include <xen/events.h>
#include <xen/page.h>
#include "xenbus_comms.h"

static int xenbus_irq;

static DECLARE_WORK(probe_work, xenbus_probe);

static DECLARE_WAIT_QUEUE_HEAD(xb_waitq);

static irqreturn_t wake_waiting(int irq, void *unused)
{
	if (unlikely(xenstored_ready == 0)) {
		xenstored_ready = 1;
		schedule_work(&probe_work);
	}

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

/**
 * xb_write - low level write
 * @data: buffer to send
 * @len: length of buffer
 *
 * Returns 0 on success, error otherwise.
 */
int xb_write(const void *data, unsigned len)
{
	struct xenstore_domain_interface *intf = xen_store_interface;
	XENSTORE_RING_IDX cons, prod;
	int rc;

	while (len != 0) {
		void *dst;
		unsigned int avail;

		rc = wait_event_interruptible(
			xb_waitq,
			(intf->req_prod - intf->req_cons) !=
			XENSTORE_RING_SIZE);
		if (rc < 0)
			return rc;

		/* Read indexes, then verify. */
		cons = intf->req_cons;
		prod = intf->req_prod;
		if (!check_indexes(cons, prod)) {
			intf->req_cons = intf->req_prod = 0;
			return -EIO;
		}

		dst = get_output_chunk(cons, prod, intf->req, &avail);
		if (avail == 0)
			continue;
		if (avail > len)
			avail = len;

		/* Must write data /after/ reading the consumer index. */
		mb();

		memcpy(dst, data, avail);
		data += avail;
		len -= avail;

		/* Other side must not see new producer until data is there. */
		wmb();
		intf->req_prod += avail;

		/* Implies mb(): other side will see the updated producer. */
		notify_remote_via_evtchn(xen_store_evtchn);
	}

	return 0;
}

int xb_data_to_read(void)
{
	struct xenstore_domain_interface *intf = xen_store_interface;
	return (intf->rsp_cons != intf->rsp_prod);
}

int xb_wait_for_data_to_read(void)
{
	return wait_event_interruptible(xb_waitq, xb_data_to_read());
}

int xb_read(void *data, unsigned len)
{
	struct xenstore_domain_interface *intf = xen_store_interface;
	XENSTORE_RING_IDX cons, prod;
	int rc;

	while (len != 0) {
		unsigned int avail;
		const char *src;

		rc = xb_wait_for_data_to_read();
		if (rc < 0)
			return rc;

		/* Read indexes, then verify. */
		cons = intf->rsp_cons;
		prod = intf->rsp_prod;
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
		rmb();

		memcpy(data, src, avail);
		data += avail;
		len -= avail;

		/* Other side must not see free space until we've copied out */
		mb();
		intf->rsp_cons += avail;

		pr_debug("Finished read of %i bytes (%i to go)\n", avail, len);

		/* Implies mb(): other side will see the updated consumer. */
		notify_remote_via_evtchn(xen_store_evtchn);
	}

	return 0;
}

/**
 * xb_init_comms - Set up interrupt handler off store event channel.
 */
int xb_init_comms(void)
{
	struct xenstore_domain_interface *intf = xen_store_interface;

	if (intf->req_prod != intf->req_cons)
		printk(KERN_ERR "XENBUS request ring is not quiescent "
		       "(%08x:%08x)!\n", intf->req_cons, intf->req_prod);

	if (intf->rsp_prod != intf->rsp_cons) {
		printk(KERN_WARNING "XENBUS response ring is not quiescent "
		       "(%08x:%08x): fixing up\n",
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
		if (err <= 0) {
			printk(KERN_ERR "XENBUS request irq failed %i\n", err);
			return err;
		}

		xenbus_irq = err;
	}

	return 0;
}

void xb_deinit_comms(void)
{
	unbind_from_irqhandler(xenbus_irq, &xb_waitq);
	xenbus_irq = 0;
}
