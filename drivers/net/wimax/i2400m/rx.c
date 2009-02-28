/*
 * Intel Wireless WiMAX Connection 2400m
 * Handle incoming traffic and deliver it to the control or data planes
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Yanir Lubetkin <yanirx.lubetkin@intel.com>
 *  - Initial implementation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *  - Use skb_clone(), break up processing in chunks
 *  - Split transport/device specific
 *  - Make buffer size dynamic to exert less memory pressure
 *
 *
 * This handles the RX path.
 *
 * We receive an RX message from the bus-specific driver, which
 * contains one or more payloads that have potentially different
 * destinataries (data or control paths).
 *
 * So we just take that payload from the transport specific code in
 * the form of an skb, break it up in chunks (a cloned skb each in the
 * case of network packets) and pass it to netdev or to the
 * command/ack handler (and from there to the WiMAX stack).
 *
 * PROTOCOL FORMAT
 *
 * The format of the buffer is:
 *
 * HEADER                      (struct i2400m_msg_hdr)
 * PAYLOAD DESCRIPTOR 0        (struct i2400m_pld)
 * PAYLOAD DESCRIPTOR 1
 * ...
 * PAYLOAD DESCRIPTOR N
 * PAYLOAD 0                   (raw bytes)
 * PAYLOAD 1
 * ...
 * PAYLOAD N
 *
 * See tx.c for a deeper description on alignment requirements and
 * other fun facts of it.
 *
 * DATA PACKETS
 *
 * In firmwares <= v1.3, data packets have no header for RX, but they
 * do for TX (currently unused).
 *
 * In firmware >= 1.4, RX packets have an extended header (16
 * bytes). This header conveys information for management of host
 * reordering of packets (the device offloads storage of the packets
 * for reordering to the host).
 *
 * Currently this information is not used as the current code doesn't
 * enable host reordering.
 *
 * The header is used as dummy space to emulate an ethernet header and
 * thus be able to act as an ethernet device without having to reallocate.
 *
 * ROADMAP
 *
 * i2400m_rx
 *   i2400m_rx_msg_hdr_check
 *   i2400m_rx_pl_descr_check
 *   i2400m_rx_payload
 *     i2400m_net_rx
 *     i2400m_rx_edata
 *       i2400m_net_erx
 *     i2400m_rx_ctl
 *       i2400m_msg_size_check
 *       i2400m_report_hook_work    [in a workqueue]
 *         i2400m_report_hook
 *       wimax_msg_to_user
 *       i2400m_rx_ctl_ack
 *         wimax_msg_to_user_alloc
 *     i2400m_rx_trace
 *       i2400m_msg_size_check
 *       wimax_msg
 */
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include "i2400m.h"


#define D_SUBMODULE rx
#include "debug-levels.h"

struct i2400m_report_hook_args {
	struct sk_buff *skb_rx;
	const struct i2400m_l3l4_hdr *l3l4_hdr;
	size_t size;
};


/*
 * Execute i2400m_report_hook in a workqueue
 *
 * Unpacks arguments from the deferred call, executes it and then
 * drops the references.
 *
 * Obvious NOTE: References are needed because we are a separate
 *     thread; otherwise the buffer changes under us because it is
 *     released by the original caller.
 */
static
void i2400m_report_hook_work(struct work_struct *ws)
{
	struct i2400m_work *iw =
		container_of(ws, struct i2400m_work, ws);
	struct i2400m_report_hook_args *args = (void *) iw->pl;
	i2400m_report_hook(iw->i2400m, args->l3l4_hdr, args->size);
	kfree_skb(args->skb_rx);
	i2400m_put(iw->i2400m);
	kfree(iw);
}


/*
 * Process an ack to a command
 *
 * @i2400m: device descriptor
 * @payload: pointer to message
 * @size: size of the message
 *
 * Pass the acknodledgment (in an skb) to the thread that is waiting
 * for it in i2400m->msg_completion.
 *
 * We need to coordinate properly with the thread waiting for the
 * ack. Check if it is waiting or if it is gone. We loose the spinlock
 * to avoid allocating on atomic contexts (yeah, could use GFP_ATOMIC,
 * but this is not so speed critical).
 */
static
void i2400m_rx_ctl_ack(struct i2400m *i2400m,
		       const void *payload, size_t size)
{
	struct device *dev = i2400m_dev(i2400m);
	struct wimax_dev *wimax_dev = &i2400m->wimax_dev;
	unsigned long flags;
	struct sk_buff *ack_skb;

	/* Anyone waiting for an answer? */
	spin_lock_irqsave(&i2400m->rx_lock, flags);
	if (i2400m->ack_skb != ERR_PTR(-EINPROGRESS)) {
		dev_err(dev, "Huh? reply to command with no waiters\n");
		goto error_no_waiter;
	}
	spin_unlock_irqrestore(&i2400m->rx_lock, flags);

	ack_skb = wimax_msg_alloc(wimax_dev, NULL, payload, size, GFP_KERNEL);

	/* Check waiter didn't time out waiting for the answer... */
	spin_lock_irqsave(&i2400m->rx_lock, flags);
	if (i2400m->ack_skb != ERR_PTR(-EINPROGRESS)) {
		d_printf(1, dev, "Huh? waiter for command reply cancelled\n");
		goto error_waiter_cancelled;
	}
	if (ack_skb == NULL) {
		dev_err(dev, "CMD/GET/SET ack: cannot allocate SKB\n");
		i2400m->ack_skb = ERR_PTR(-ENOMEM);
	} else
		i2400m->ack_skb = ack_skb;
	spin_unlock_irqrestore(&i2400m->rx_lock, flags);
	complete(&i2400m->msg_completion);
	return;

error_waiter_cancelled:
	kfree_skb(ack_skb);
error_no_waiter:
	spin_unlock_irqrestore(&i2400m->rx_lock, flags);
	return;
}


/*
 * Receive and process a control payload
 *
 * @i2400m: device descriptor
 * @skb_rx: skb that contains the payload (for reference counting)
 * @payload: pointer to message
 * @size: size of the message
 *
 * There are two types of control RX messages: reports (asynchronous,
 * like your every day interrupts) and 'acks' (reponses to a command,
 * get or set request).
 *
 * If it is a report, we run hooks on it (to extract information for
 * things we need to do in the driver) and then pass it over to the
 * WiMAX stack to send it to user space.
 *
 * NOTE: report processing is done in a workqueue specific to the
 *     generic driver, to avoid deadlocks in the system.
 *
 * If it is not a report, it is an ack to a previously executed
 * command, set or get, so wake up whoever is waiting for it from
 * i2400m_msg_to_dev(). i2400m_rx_ctl_ack() takes care of that.
 *
 * Note that the sizes we pass to other functions from here are the
 * sizes of the _l3l4_hdr + payload, not full buffer sizes, as we have
 * verified in _msg_size_check() that they are congruent.
 *
 * For reports: We can't clone the original skb where the data is
 * because we need to send this up via netlink; netlink has to add
 * headers and we can't overwrite what's preceeding the payload...as
 * it is another message. So we just dup them.
 */
static
void i2400m_rx_ctl(struct i2400m *i2400m, struct sk_buff *skb_rx,
		   const void *payload, size_t size)
{
	int result;
	struct device *dev = i2400m_dev(i2400m);
	const struct i2400m_l3l4_hdr *l3l4_hdr = payload;
	unsigned msg_type;

	result = i2400m_msg_size_check(i2400m, l3l4_hdr, size);
	if (result < 0) {
		dev_err(dev, "HW BUG? device sent a bad message: %d\n",
			result);
		goto error_check;
	}
	msg_type = le16_to_cpu(l3l4_hdr->type);
	d_printf(1, dev, "%s 0x%04x: %zu bytes\n",
		 msg_type & I2400M_MT_REPORT_MASK ? "REPORT" : "CMD/SET/GET",
		 msg_type, size);
	d_dump(2, dev, l3l4_hdr, size);
	if (msg_type & I2400M_MT_REPORT_MASK) {
		/* These hooks have to be ran serialized; as well, the
		 * handling might force the execution of commands, and
		 * that might cause reentrancy issues with
		 * bus-specific subdrivers and workqueues. So we run
		 * it in a separate workqueue. */
		struct i2400m_report_hook_args args = {
			.skb_rx = skb_rx,
			.l3l4_hdr = l3l4_hdr,
			.size = size
		};
		if (unlikely(i2400m->ready == 0))	/* only send if up */
			return;
		skb_get(skb_rx);
		i2400m_queue_work(i2400m, i2400m_report_hook_work,
				  GFP_KERNEL, &args, sizeof(args));
		result = wimax_msg(&i2400m->wimax_dev, NULL, l3l4_hdr, size,
				   GFP_KERNEL);
		if (result < 0)
			dev_err(dev, "error sending report to userspace: %d\n",
				result);
	} else		/* an ack to a CMD, GET or SET */
		i2400m_rx_ctl_ack(i2400m, payload, size);
error_check:
	return;
}


/*
 * Receive and send up a trace
 *
 * @i2400m: device descriptor
 * @skb_rx: skb that contains the trace (for reference counting)
 * @payload: pointer to trace message inside the skb
 * @size: size of the message
 *
 * THe i2400m might produce trace information (diagnostics) and we
 * send them through a different kernel-to-user pipe (to avoid
 * clogging it).
 *
 * As in i2400m_rx_ctl(), we can't clone the original skb where the
 * data is because we need to send this up via netlink; netlink has to
 * add headers and we can't overwrite what's preceeding the
 * payload...as it is another message. So we just dup them.
 */
static
void i2400m_rx_trace(struct i2400m *i2400m,
		     const void *payload, size_t size)
{
	int result;
	struct device *dev = i2400m_dev(i2400m);
	struct wimax_dev *wimax_dev = &i2400m->wimax_dev;
	const struct i2400m_l3l4_hdr *l3l4_hdr = payload;
	unsigned msg_type;

	result = i2400m_msg_size_check(i2400m, l3l4_hdr, size);
	if (result < 0) {
		dev_err(dev, "HW BUG? device sent a bad trace message: %d\n",
			result);
		goto error_check;
	}
	msg_type = le16_to_cpu(l3l4_hdr->type);
	d_printf(1, dev, "Trace %s 0x%04x: %zu bytes\n",
		 msg_type & I2400M_MT_REPORT_MASK ? "REPORT" : "CMD/SET/GET",
		 msg_type, size);
	d_dump(2, dev, l3l4_hdr, size);
	if (unlikely(i2400m->ready == 0))	/* only send if up */
		return;
	result = wimax_msg(wimax_dev, "trace", l3l4_hdr, size, GFP_KERNEL);
	if (result < 0)
		dev_err(dev, "error sending trace to userspace: %d\n",
			result);
error_check:
	return;
}

/*
 * Receive and send up an extended data packet
 *
 * @i2400m: device descriptor
 * @skb_rx: skb that contains the extended data packet
 * @single_last: 1 if the payload is the only one or the last one of
 *     the skb.
 * @payload: pointer to the packet's data inside the skb
 * @size: size of the payload
 *
 * Starting in v1.4 of the i2400m's firmware, the device can send data
 * packets to the host in an extended format that; this incudes a 16
 * byte header (struct i2400m_pl_edata_hdr). Using this header's space
 * we can fake ethernet headers for ethernet device emulation without
 * having to copy packets around.
 *
 * This function handles said path.
 */
static
void i2400m_rx_edata(struct i2400m *i2400m, struct sk_buff *skb_rx,
		     unsigned single_last, const void *payload, size_t size)
{
	struct device *dev = i2400m_dev(i2400m);
	const struct i2400m_pl_edata_hdr *hdr = payload;
	struct net_device *net_dev = i2400m->wimax_dev.net_dev;
	struct sk_buff *skb;
	enum i2400m_cs cs;
	unsigned reorder_needed;

	d_fnstart(4, dev, "(i2400m %p skb_rx %p single %u payload %p "
		  "size %zu)\n", i2400m, skb_rx, single_last, payload, size);
	if (size < sizeof(*hdr)) {
		dev_err(dev, "ERX: HW BUG? message with short header (%zu "
			"vs %zu bytes expected)\n", size, sizeof(*hdr));
		goto error;
	}
	reorder_needed = le32_to_cpu(hdr->reorder & I2400M_REORDER_NEEDED);
	cs = hdr->cs;
	if (reorder_needed) {
		dev_err(dev, "ERX: HW BUG? reorder needed, it was disabled\n");
		goto error;
	}
	/* ok, so now decide if we want to clone or reuse the skb,
	 * pull and trim it so the beginning is the space for the eth
	 * header and pass it to i2400m_net_erx() for the stack */
	if (single_last) {
		skb = skb_get(skb_rx);
		d_printf(3, dev, "ERX: reusing single payload skb %p\n", skb);
	} else {
		skb = skb_clone(skb_rx, GFP_KERNEL);
		d_printf(3, dev, "ERX: cloning %p\n", skb);
		if (skb == NULL) {
			dev_err(dev, "ERX: no memory to clone skb\n");
			net_dev->stats.rx_dropped++;
			goto error_skb_clone;
		}
	}
	/* now we have to pull and trim so that the skb points to the
	 * beginning of the IP packet; the netdev part will add the
	 * ethernet header as needed. */
	BUILD_BUG_ON(ETH_HLEN > sizeof(*hdr));
	skb_pull(skb, payload + sizeof(*hdr) - (void *) skb->data);
	skb_trim(skb, (void *) skb_end_pointer(skb) - payload + sizeof(*hdr));
	i2400m_net_erx(i2400m, skb, cs);
error_skb_clone:
error:
	d_fnend(4, dev, "(i2400m %p skb_rx %p single %u payload %p "
		"size %zu) = void\n", i2400m, skb_rx, single_last, payload, size);
	return;
}




/*
 * Act on a received payload
 *
 * @i2400m: device instance
 * @skb_rx: skb where the transaction was received
 * @single_last: 1 this is the only payload or the last one (so the
 *     skb can be reused instead of cloned).
 * @pld: payload descriptor
 * @payload: payload data
 *
 * Upon reception of a payload, look at its guts in the payload
 * descriptor and decide what to do with it. If it is a single payload
 * skb or if the last skb is a data packet, the skb will be referenced
 * and modified (so it doesn't have to be cloned).
 */
static
void i2400m_rx_payload(struct i2400m *i2400m, struct sk_buff *skb_rx,
		       unsigned single_last, const struct i2400m_pld *pld,
		       const void *payload)
{
	struct device *dev = i2400m_dev(i2400m);
	size_t pl_size = i2400m_pld_size(pld);
	enum i2400m_pt pl_type = i2400m_pld_type(pld);

	d_printf(7, dev, "RX: received payload type %u, %zu bytes\n",
		 pl_type, pl_size);
	d_dump(8, dev, payload, pl_size);

	switch (pl_type) {
	case I2400M_PT_DATA:
		d_printf(3, dev, "RX: data payload %zu bytes\n", pl_size);
		i2400m_net_rx(i2400m, skb_rx, single_last, payload, pl_size);
		break;
	case I2400M_PT_CTRL:
		i2400m_rx_ctl(i2400m, skb_rx, payload, pl_size);
		break;
	case I2400M_PT_TRACE:
		i2400m_rx_trace(i2400m, payload, pl_size);
		break;
	case I2400M_PT_EDATA:
		d_printf(3, dev, "ERX: data payload %zu bytes\n", pl_size);
		i2400m_rx_edata(i2400m, skb_rx, single_last, payload, pl_size);
		break;
	default:	/* Anything else shouldn't come to the host */
		if (printk_ratelimit())
			dev_err(dev, "RX: HW BUG? unexpected payload type %u\n",
				pl_type);
	}
}


/*
 * Check a received transaction's message header
 *
 * @i2400m: device descriptor
 * @msg_hdr: message header
 * @buf_size: size of the received buffer
 *
 * Check that the declarations done by a RX buffer message header are
 * sane and consistent with the amount of data that was received.
 */
static
int i2400m_rx_msg_hdr_check(struct i2400m *i2400m,
			    const struct i2400m_msg_hdr *msg_hdr,
			    size_t buf_size)
{
	int result = -EIO;
	struct device *dev = i2400m_dev(i2400m);
	if (buf_size < sizeof(*msg_hdr)) {
		dev_err(dev, "RX: HW BUG? message with short header (%zu "
			"vs %zu bytes expected)\n", buf_size, sizeof(*msg_hdr));
		goto error;
	}
	if (msg_hdr->barker != cpu_to_le32(I2400M_D2H_MSG_BARKER)) {
		dev_err(dev, "RX: HW BUG? message received with unknown "
			"barker 0x%08x (buf_size %zu bytes)\n",
			le32_to_cpu(msg_hdr->barker), buf_size);
		goto error;
	}
	if (msg_hdr->num_pls == 0) {
		dev_err(dev, "RX: HW BUG? zero payload packets in message\n");
		goto error;
	}
	if (le16_to_cpu(msg_hdr->num_pls) > I2400M_MAX_PLS_IN_MSG) {
		dev_err(dev, "RX: HW BUG? message contains more payload "
			"than maximum; ignoring.\n");
		goto error;
	}
	result = 0;
error:
	return result;
}


/*
 * Check a payload descriptor against the received data
 *
 * @i2400m: device descriptor
 * @pld: payload descriptor
 * @pl_itr: offset (in bytes) in the received buffer the payload is
 *          located
 * @buf_size: size of the received buffer
 *
 * Given a payload descriptor (part of a RX buffer), check it is sane
 * and that the data it declares fits in the buffer.
 */
static
int i2400m_rx_pl_descr_check(struct i2400m *i2400m,
			      const struct i2400m_pld *pld,
			      size_t pl_itr, size_t buf_size)
{
	int result = -EIO;
	struct device *dev = i2400m_dev(i2400m);
	size_t pl_size = i2400m_pld_size(pld);
	enum i2400m_pt pl_type = i2400m_pld_type(pld);

	if (pl_size > i2400m->bus_pl_size_max) {
		dev_err(dev, "RX: HW BUG? payload @%zu: size %zu is "
			"bigger than maximum %zu; ignoring message\n",
			pl_itr, pl_size, i2400m->bus_pl_size_max);
		goto error;
	}
	if (pl_itr + pl_size > buf_size) {	/* enough? */
		dev_err(dev, "RX: HW BUG? payload @%zu: size %zu "
			"goes beyond the received buffer "
			"size (%zu bytes); ignoring message\n",
			pl_itr, pl_size, buf_size);
		goto error;
	}
	if (pl_type >= I2400M_PT_ILLEGAL) {
		dev_err(dev, "RX: HW BUG? illegal payload type %u; "
			"ignoring message\n", pl_type);
		goto error;
	}
	result = 0;
error:
	return result;
}


/**
 * i2400m_rx - Receive a buffer of data from the device
 *
 * @i2400m: device descriptor
 * @skb: skbuff where the data has been received
 *
 * Parse in a buffer of data that contains an RX message sent from the
 * device. See the file header for the format. Run all checks on the
 * buffer header, then run over each payload's descriptors, verify
 * their consistency and act on each payload's contents.  If
 * everything is succesful, update the device's statistics.
 *
 * Note: You need to set the skb to contain only the length of the
 * received buffer; for that, use skb_trim(skb, RECEIVED_SIZE).
 *
 * Returns:
 *
 * 0 if ok, < 0 errno on error
 *
 * If ok, this function owns now the skb and the caller DOESN'T have
 * to run kfree_skb() on it. However, on error, the caller still owns
 * the skb and it is responsible for releasing it.
 */
int i2400m_rx(struct i2400m *i2400m, struct sk_buff *skb)
{
	int i, result;
	struct device *dev = i2400m_dev(i2400m);
	const struct i2400m_msg_hdr *msg_hdr;
	size_t pl_itr, pl_size, skb_len;
	unsigned long flags;
	unsigned num_pls, single_last;

	skb_len = skb->len;
	d_fnstart(4, dev, "(i2400m %p skb %p [size %zu])\n",
		  i2400m, skb, skb_len);
	result = -EIO;
	msg_hdr = (void *) skb->data;
	result = i2400m_rx_msg_hdr_check(i2400m, msg_hdr, skb->len);
	if (result < 0)
		goto error_msg_hdr_check;
	result = -EIO;
	num_pls = le16_to_cpu(msg_hdr->num_pls);
	pl_itr = sizeof(*msg_hdr) +	/* Check payload descriptor(s) */
		num_pls * sizeof(msg_hdr->pld[0]);
	pl_itr = ALIGN(pl_itr, I2400M_PL_PAD);
	if (pl_itr > skb->len) {	/* got all the payload descriptors? */
		dev_err(dev, "RX: HW BUG? message too short (%u bytes) for "
			"%u payload descriptors (%zu each, total %zu)\n",
			skb->len, num_pls, sizeof(msg_hdr->pld[0]), pl_itr);
		goto error_pl_descr_short;
	}
	/* Walk each payload payload--check we really got it */
	for (i = 0; i < num_pls; i++) {
		/* work around old gcc warnings */
		pl_size = i2400m_pld_size(&msg_hdr->pld[i]);
		result = i2400m_rx_pl_descr_check(i2400m, &msg_hdr->pld[i],
						  pl_itr, skb->len);
		if (result < 0)
			goto error_pl_descr_check;
		single_last = num_pls == 1 || i == num_pls - 1;
		i2400m_rx_payload(i2400m, skb, single_last, &msg_hdr->pld[i],
				  skb->data + pl_itr);
		pl_itr += ALIGN(pl_size, I2400M_PL_PAD);
		cond_resched();		/* Don't monopolize */
	}
	kfree_skb(skb);
	/* Update device statistics */
	spin_lock_irqsave(&i2400m->rx_lock, flags);
	i2400m->rx_pl_num += i;
	if (i > i2400m->rx_pl_max)
		i2400m->rx_pl_max = i;
	if (i < i2400m->rx_pl_min)
		i2400m->rx_pl_min = i;
	i2400m->rx_num++;
	i2400m->rx_size_acc += skb->len;
	if (skb->len < i2400m->rx_size_min)
		i2400m->rx_size_min = skb->len;
	if (skb->len > i2400m->rx_size_max)
		i2400m->rx_size_max = skb->len;
	spin_unlock_irqrestore(&i2400m->rx_lock, flags);
error_pl_descr_check:
error_pl_descr_short:
error_msg_hdr_check:
	d_fnend(4, dev, "(i2400m %p skb %p [size %zu]) = %d\n",
		i2400m, skb, skb_len, result);
	return result;
}
EXPORT_SYMBOL_GPL(i2400m_rx);
