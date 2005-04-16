/******************************************************************************
 *  usb_atm.c - Generic USB xDSL driver core
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands, SolNegro, Josep Comas
 *  Copyright (C) 2004, David Woodhouse
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

/*
 *  Written by Johan Verrept, maintained by Duncan Sands (duncan.sands@free.fr)
 *
 *  1.7+:	- See the check-in logs
 *
 *  1.6:	- No longer opens a connection if the firmware is not loaded
 *  		- Added support for the speedtouch 330
 *  		- Removed the limit on the number of devices
 *  		- Module now autoloads on device plugin
 *  		- Merged relevant parts of sarlib
 *  		- Replaced the kernel thread with a tasklet
 *  		- New packet transmission code
 *  		- Changed proc file contents
 *  		- Fixed all known SMP races
 *  		- Many fixes and cleanups
 *  		- Various fixes by Oliver Neukum (oliver@neukum.name)
 *
 *  1.5A:	- Version for inclusion in 2.5 series kernel
 *		- Modifications by Richard Purdie (rpurdie@rpsys.net)
 *		- made compatible with kernel 2.5.6 onwards by changing
 *		udsl_usb_send_data_context->urb to a pointer and adding code
 *		to alloc and free it
 *		- remove_wait_queue() added to udsl_atm_processqueue_thread()
 *
 *  1.5:	- fixed memory leak when atmsar_decode_aal5 returned NULL.
 *		(reported by stephen.robinson@zen.co.uk)
 *
 *  1.4:	- changed the spin_lock() under interrupt to spin_lock_irqsave()
 *		- unlink all active send urbs of a vcc that is being closed.
 *
 *  1.3.1:	- added the version number
 *
 *  1.3:	- Added multiple send urb support
 *		- fixed memory leak and vcc->tx_inuse starvation bug
 *		  when not enough memory left in vcc.
 *
 *  1.2:	- Fixed race condition in udsl_usb_send_data()
 *  1.1:	- Turned off packet debugging
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/firmware.h>

#include "usb_atm.h"

#ifdef VERBOSE_DEBUG
static int udsl_print_packet(const unsigned char *data, int len);
#define PACKETDEBUG(arg...)	udsl_print_packet (arg)
#define vdbg(arg...)		dbg (arg)
#else
#define PACKETDEBUG(arg...)
#define vdbg(arg...)
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Duncan Sands <duncan.sands@free.fr>"
#define DRIVER_VERSION	"1.8"
#define DRIVER_DESC	"Generic USB ATM/DSL I/O, version " DRIVER_VERSION

static unsigned int num_rcv_urbs = UDSL_DEFAULT_RCV_URBS;
static unsigned int num_snd_urbs = UDSL_DEFAULT_SND_URBS;
static unsigned int num_rcv_bufs = UDSL_DEFAULT_RCV_BUFS;
static unsigned int num_snd_bufs = UDSL_DEFAULT_SND_BUFS;
static unsigned int rcv_buf_size = UDSL_DEFAULT_RCV_BUF_SIZE;
static unsigned int snd_buf_size = UDSL_DEFAULT_SND_BUF_SIZE;

module_param(num_rcv_urbs, uint, 0444);
MODULE_PARM_DESC(num_rcv_urbs,
		 "Number of urbs used for reception (range: 0-"
		 __MODULE_STRING(UDSL_MAX_RCV_URBS) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_RCV_URBS) ")");

module_param(num_snd_urbs, uint, 0444);
MODULE_PARM_DESC(num_snd_urbs,
		 "Number of urbs used for transmission (range: 0-"
		 __MODULE_STRING(UDSL_MAX_SND_URBS) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_SND_URBS) ")");

module_param(num_rcv_bufs, uint, 0444);
MODULE_PARM_DESC(num_rcv_bufs,
		 "Number of buffers used for reception (range: 0-"
		 __MODULE_STRING(UDSL_MAX_RCV_BUFS) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_RCV_BUFS) ")");

module_param(num_snd_bufs, uint, 0444);
MODULE_PARM_DESC(num_snd_bufs,
		 "Number of buffers used for transmission (range: 0-"
		 __MODULE_STRING(UDSL_MAX_SND_BUFS) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_SND_BUFS) ")");

module_param(rcv_buf_size, uint, 0444);
MODULE_PARM_DESC(rcv_buf_size,
		 "Size of the buffers used for reception (range: 0-"
		 __MODULE_STRING(UDSL_MAX_RCV_BUF_SIZE) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_RCV_BUF_SIZE) ")");

module_param(snd_buf_size, uint, 0444);
MODULE_PARM_DESC(snd_buf_size,
		 "Size of the buffers used for transmission (range: 0-"
		 __MODULE_STRING(UDSL_MAX_SND_BUF_SIZE) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_SND_BUF_SIZE) ")");

/* ATM */

static void udsl_atm_dev_close(struct atm_dev *dev);
static int udsl_atm_open(struct atm_vcc *vcc);
static void udsl_atm_close(struct atm_vcc *vcc);
static int udsl_atm_ioctl(struct atm_dev *dev, unsigned int cmd, void __user * arg);
static int udsl_atm_send(struct atm_vcc *vcc, struct sk_buff *skb);
static int udsl_atm_proc_read(struct atm_dev *atm_dev, loff_t * pos, char *page);

static struct atmdev_ops udsl_atm_devops = {
	.dev_close	= udsl_atm_dev_close,
	.open		= udsl_atm_open,
	.close		= udsl_atm_close,
	.ioctl		= udsl_atm_ioctl,
	.send		= udsl_atm_send,
	.proc_read	= udsl_atm_proc_read,
	.owner		= THIS_MODULE,
};

/***********
**  misc  **
***********/

static inline void udsl_pop(struct atm_vcc *vcc, struct sk_buff *skb)
{
	if (vcc->pop)
		vcc->pop(vcc, skb);
	else
		dev_kfree_skb(skb);
}

/*************
**  decode  **
*************/

static inline struct udsl_vcc_data *udsl_find_vcc(struct udsl_instance_data *instance,
						  short vpi, int vci)
{
	struct udsl_vcc_data *vcc;

	list_for_each_entry(vcc, &instance->vcc_list, list)
		if ((vcc->vci == vci) && (vcc->vpi == vpi))
			return vcc;
	return NULL;
}

static void udsl_extract_cells(struct udsl_instance_data *instance,
			       unsigned char *source, unsigned int howmany)
{
	struct udsl_vcc_data *cached_vcc = NULL;
	struct atm_vcc *vcc;
	struct sk_buff *sarb;
	struct udsl_vcc_data *vcc_data;
	int cached_vci = 0;
	unsigned int i;
	int pti;
	int vci;
	short cached_vpi = 0;
	short vpi;

	for (i = 0; i < howmany;
	     i++, source += ATM_CELL_SIZE + instance->rcv_padding) {
		vpi = ((source[0] & 0x0f) << 4) | (source[1] >> 4);
		vci = ((source[1] & 0x0f) << 12) | (source[2] << 4) | (source[3] >> 4);
		pti = (source[3] & 0x2) != 0;

		vdbg("udsl_extract_cells: vpi %hd, vci %d, pti %d", vpi, vci, pti);

		if (cached_vcc && (vci == cached_vci) && (vpi == cached_vpi))
			vcc_data = cached_vcc;
		else if ((vcc_data = udsl_find_vcc(instance, vpi, vci))) {
			cached_vcc = vcc_data;
			cached_vpi = vpi;
			cached_vci = vci;
		} else {
			dbg("udsl_extract_cells: unknown vpi/vci (%hd/%d)!", vpi, vci);
			continue;
		}

		vcc = vcc_data->vcc;
		sarb = vcc_data->sarb;

		if (sarb->tail + ATM_CELL_PAYLOAD > sarb->end) {
			dbg("udsl_extract_cells: buffer overrun (sarb->len %u, vcc: 0x%p)!", sarb->len, vcc);
			/* discard cells already received */
			skb_trim(sarb, 0);
		}

		memcpy(sarb->tail, source + ATM_CELL_HEADER, ATM_CELL_PAYLOAD);
		__skb_put(sarb, ATM_CELL_PAYLOAD);

		if (pti) {
			struct sk_buff *skb;
			unsigned int length;
			unsigned int pdu_length;

			length = (source[ATM_CELL_SIZE - 6] << 8) + source[ATM_CELL_SIZE - 5];

			/* guard against overflow */
			if (length > ATM_MAX_AAL5_PDU) {
				dbg("udsl_extract_cells: bogus length %u (vcc: 0x%p)!", length, vcc);
				atomic_inc(&vcc->stats->rx_err);
				goto out;
			}

			pdu_length = UDSL_NUM_CELLS(length) * ATM_CELL_PAYLOAD;

			if (sarb->len < pdu_length) {
				dbg("udsl_extract_cells: bogus pdu_length %u (sarb->len: %u, vcc: 0x%p)!", pdu_length, sarb->len, vcc);
				atomic_inc(&vcc->stats->rx_err);
				goto out;
			}

			if (crc32_be(~0, sarb->tail - pdu_length, pdu_length) != 0xc704dd7b) {
				dbg("udsl_extract_cells: packet failed crc check (vcc: 0x%p)!", vcc);
				atomic_inc(&vcc->stats->rx_err);
				goto out;
			}

			vdbg("udsl_extract_cells: got packet (length: %u, pdu_length: %u, vcc: 0x%p)", length, pdu_length, vcc);

			if (!(skb = dev_alloc_skb(length))) {
				dbg("udsl_extract_cells: no memory for skb (length: %u)!", length);
				atomic_inc(&vcc->stats->rx_drop);
				goto out;
			}

			vdbg("udsl_extract_cells: allocated new sk_buff (skb: 0x%p, skb->truesize: %u)", skb, skb->truesize);

			if (!atm_charge(vcc, skb->truesize)) {
				dbg("udsl_extract_cells: failed atm_charge (skb->truesize: %u)!", skb->truesize);
				dev_kfree_skb(skb);
				goto out;	/* atm_charge increments rx_drop */
			}

			memcpy(skb->data, sarb->tail - pdu_length, length);
			__skb_put(skb, length);

			vdbg("udsl_extract_cells: sending skb 0x%p, skb->len %u, skb->truesize %u", skb, skb->len, skb->truesize);

			PACKETDEBUG(skb->data, skb->len);

			vcc->push(vcc, skb);

			atomic_inc(&vcc->stats->rx);
		out:
			skb_trim(sarb, 0);
		}
	}
}

/*************
**  encode  **
*************/

static inline void udsl_fill_cell_header(unsigned char *target, struct atm_vcc *vcc)
{
	target[0] = vcc->vpi >> 4;
	target[1] = (vcc->vpi << 4) | (vcc->vci >> 12);
	target[2] = vcc->vci >> 4;
	target[3] = vcc->vci << 4;
	target[4] = 0xec;
}

static const unsigned char zeros[ATM_CELL_PAYLOAD];

static void udsl_groom_skb(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_control *ctrl = UDSL_SKB(skb);
	unsigned int zero_padding;
	u32 crc;

	ctrl->atm_data.vcc = vcc;

	ctrl->num_cells = UDSL_NUM_CELLS(skb->len);
	ctrl->num_entire = skb->len / ATM_CELL_PAYLOAD;

	zero_padding = ctrl->num_cells * ATM_CELL_PAYLOAD - skb->len - ATM_AAL5_TRAILER;

	if (ctrl->num_entire + 1 < ctrl->num_cells)
		ctrl->pdu_padding = zero_padding - (ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER);
	else
		ctrl->pdu_padding = zero_padding;

	ctrl->aal5_trailer[0] = 0;	/* UU = 0 */
	ctrl->aal5_trailer[1] = 0;	/* CPI = 0 */
	ctrl->aal5_trailer[2] = skb->len >> 8;
	ctrl->aal5_trailer[3] = skb->len;

	crc = crc32_be(~0, skb->data, skb->len);
	crc = crc32_be(crc, zeros, zero_padding);
	crc = crc32_be(crc, ctrl->aal5_trailer, 4);
	crc = ~crc;

	ctrl->aal5_trailer[4] = crc >> 24;
	ctrl->aal5_trailer[5] = crc >> 16;
	ctrl->aal5_trailer[6] = crc >> 8;
	ctrl->aal5_trailer[7] = crc;
}

static unsigned int udsl_write_cells(struct udsl_instance_data *instance,
				     unsigned int howmany, struct sk_buff *skb,
				     unsigned char **target_p)
{
	struct udsl_control *ctrl = UDSL_SKB(skb);
	unsigned char *target = *target_p;
	unsigned int nc, ne, i;

	vdbg("udsl_write_cells: howmany=%u, skb->len=%d, num_cells=%u, num_entire=%u, pdu_padding=%u", howmany, skb->len, ctrl->num_cells, ctrl->num_entire, ctrl->pdu_padding);

	nc = ctrl->num_cells;
	ne = min(howmany, ctrl->num_entire);

	for (i = 0; i < ne; i++) {
		udsl_fill_cell_header(target, ctrl->atm_data.vcc);
		target += ATM_CELL_HEADER;
		memcpy(target, skb->data, ATM_CELL_PAYLOAD);
		target += ATM_CELL_PAYLOAD;
		if (instance->snd_padding) {
			memset(target, 0, instance->snd_padding);
			target += instance->snd_padding;
		}
		__skb_pull(skb, ATM_CELL_PAYLOAD);
	}

	ctrl->num_entire -= ne;

	if (!(ctrl->num_cells -= ne) || !(howmany -= ne))
		goto out;

	udsl_fill_cell_header(target, ctrl->atm_data.vcc);
	target += ATM_CELL_HEADER;
	memcpy(target, skb->data, skb->len);
	target += skb->len;
	__skb_pull(skb, skb->len);
	memset(target, 0, ctrl->pdu_padding);
	target += ctrl->pdu_padding;

	if (--ctrl->num_cells) {
		if (!--howmany) {
			ctrl->pdu_padding = ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER;
			goto out;
		}

		if (instance->snd_padding) {
			memset(target, 0, instance->snd_padding);
			target += instance->snd_padding;
		}
		udsl_fill_cell_header(target, ctrl->atm_data.vcc);
		target += ATM_CELL_HEADER;
		memset(target, 0, ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER);
		target += ATM_CELL_PAYLOAD - ATM_AAL5_TRAILER;

		--ctrl->num_cells;
		UDSL_ASSERT(!ctrl->num_cells);
	}

	memcpy(target, ctrl->aal5_trailer, ATM_AAL5_TRAILER);
	target += ATM_AAL5_TRAILER;
	/* set pti bit in last cell */
	*(target + 3 - ATM_CELL_SIZE) |= 0x2;
	if (instance->snd_padding) {
		memset(target, 0, instance->snd_padding);
		target += instance->snd_padding;
	}
 out:
	*target_p = target;
	return nc - ctrl->num_cells;
}

/**************
**  receive  **
**************/

static void udsl_complete_receive(struct urb *urb, struct pt_regs *regs)
{
	struct udsl_receive_buffer *buf;
	struct udsl_instance_data *instance;
	struct udsl_receiver *rcv;
	unsigned long flags;

	if (!urb || !(rcv = urb->context)) {
		dbg("udsl_complete_receive: bad urb!");
		return;
	}

	instance = rcv->instance;
	buf = rcv->buffer;

	buf->filled_cells = urb->actual_length / (ATM_CELL_SIZE + instance->rcv_padding);

	vdbg("udsl_complete_receive: urb 0x%p, status %d, actual_length %d, filled_cells %u, rcv 0x%p, buf 0x%p", urb, urb->status, urb->actual_length, buf->filled_cells, rcv, buf);

	UDSL_ASSERT(buf->filled_cells <= rcv_buf_size);

	/* may not be in_interrupt() */
	spin_lock_irqsave(&instance->receive_lock, flags);
	list_add(&rcv->list, &instance->spare_receivers);
	list_add_tail(&buf->list, &instance->filled_receive_buffers);
	if (likely(!urb->status))
		tasklet_schedule(&instance->receive_tasklet);
	spin_unlock_irqrestore(&instance->receive_lock, flags);
}

static void udsl_process_receive(unsigned long data)
{
	struct udsl_receive_buffer *buf;
	struct udsl_instance_data *instance = (struct udsl_instance_data *)data;
	struct udsl_receiver *rcv;
	int err;

 made_progress:
	while (!list_empty(&instance->spare_receive_buffers)) {
		spin_lock_irq(&instance->receive_lock);
		if (list_empty(&instance->spare_receivers)) {
			spin_unlock_irq(&instance->receive_lock);
			break;
		}
		rcv = list_entry(instance->spare_receivers.next,
				 struct udsl_receiver, list);
		list_del(&rcv->list);
		spin_unlock_irq(&instance->receive_lock);

		buf = list_entry(instance->spare_receive_buffers.next,
				 struct udsl_receive_buffer, list);
		list_del(&buf->list);

		rcv->buffer = buf;

		usb_fill_bulk_urb(rcv->urb, instance->usb_dev,
				  usb_rcvbulkpipe(instance->usb_dev, instance->data_endpoint),
				  buf->base,
				  rcv_buf_size * (ATM_CELL_SIZE + instance->rcv_padding),
				  udsl_complete_receive, rcv);

		vdbg("udsl_process_receive: sending urb 0x%p, rcv 0x%p, buf 0x%p",
		     rcv->urb, rcv, buf);

		if ((err = usb_submit_urb(rcv->urb, GFP_ATOMIC)) < 0) {
			dbg("udsl_process_receive: urb submission failed (%d)!", err);
			list_add(&buf->list, &instance->spare_receive_buffers);
			spin_lock_irq(&instance->receive_lock);
			list_add(&rcv->list, &instance->spare_receivers);
			spin_unlock_irq(&instance->receive_lock);
			break;
		}
	}

	spin_lock_irq(&instance->receive_lock);
	if (list_empty(&instance->filled_receive_buffers)) {
		spin_unlock_irq(&instance->receive_lock);
		return;		/* done - no more buffers */
	}
	buf = list_entry(instance->filled_receive_buffers.next,
			 struct udsl_receive_buffer, list);
	list_del(&buf->list);
	spin_unlock_irq(&instance->receive_lock);

	vdbg("udsl_process_receive: processing buf 0x%p", buf);
	udsl_extract_cells(instance, buf->base, buf->filled_cells);
	list_add(&buf->list, &instance->spare_receive_buffers);
	goto made_progress;
}

/***********
**  send  **
***********/

static void udsl_complete_send(struct urb *urb, struct pt_regs *regs)
{
	struct udsl_instance_data *instance;
	struct udsl_sender *snd;
	unsigned long flags;

	if (!urb || !(snd = urb->context) || !(instance = snd->instance)) {
		dbg("udsl_complete_send: bad urb!");
		return;
	}

	vdbg("udsl_complete_send: urb 0x%p, status %d, snd 0x%p, buf 0x%p", urb,
	     urb->status, snd, snd->buffer);

	/* may not be in_interrupt() */
	spin_lock_irqsave(&instance->send_lock, flags);
	list_add(&snd->list, &instance->spare_senders);
	list_add(&snd->buffer->list, &instance->spare_send_buffers);
	tasklet_schedule(&instance->send_tasklet);
	spin_unlock_irqrestore(&instance->send_lock, flags);
}

static void udsl_process_send(unsigned long data)
{
	struct udsl_send_buffer *buf;
	struct udsl_instance_data *instance = (struct udsl_instance_data *)data;
	struct sk_buff *skb;
	struct udsl_sender *snd;
	int err;
	unsigned int num_written;

 made_progress:
	spin_lock_irq(&instance->send_lock);
	while (!list_empty(&instance->spare_senders)) {
		if (!list_empty(&instance->filled_send_buffers)) {
			buf = list_entry(instance->filled_send_buffers.next,
					 struct udsl_send_buffer, list);
			list_del(&buf->list);
		} else if ((buf = instance->current_buffer)) {
			instance->current_buffer = NULL;
		} else		/* all buffers empty */
			break;

		snd = list_entry(instance->spare_senders.next,
				 struct udsl_sender, list);
		list_del(&snd->list);
		spin_unlock_irq(&instance->send_lock);

		snd->buffer = buf;
		usb_fill_bulk_urb(snd->urb, instance->usb_dev,
				  usb_sndbulkpipe(instance->usb_dev, instance->data_endpoint),
				  buf->base,
				  (snd_buf_size - buf->free_cells) * (ATM_CELL_SIZE + instance->snd_padding),
				  udsl_complete_send, snd);

		vdbg("udsl_process_send: submitting urb 0x%p (%d cells), snd 0x%p, buf 0x%p",
		     snd->urb, snd_buf_size - buf->free_cells, snd, buf);

		if ((err = usb_submit_urb(snd->urb, GFP_ATOMIC)) < 0) {
			dbg("udsl_process_send: urb submission failed (%d)!", err);
			spin_lock_irq(&instance->send_lock);
			list_add(&snd->list, &instance->spare_senders);
			spin_unlock_irq(&instance->send_lock);
			list_add(&buf->list, &instance->filled_send_buffers);
			return;	/* bail out */
		}

		spin_lock_irq(&instance->send_lock);
	}			/* while */
	spin_unlock_irq(&instance->send_lock);

	if (!instance->current_skb)
		instance->current_skb = skb_dequeue(&instance->sndqueue);
	if (!instance->current_skb)
		return;		/* done - no more skbs */

	skb = instance->current_skb;

	if (!(buf = instance->current_buffer)) {
		spin_lock_irq(&instance->send_lock);
		if (list_empty(&instance->spare_send_buffers)) {
			instance->current_buffer = NULL;
			spin_unlock_irq(&instance->send_lock);
			return;	/* done - no more buffers */
		}
		buf = list_entry(instance->spare_send_buffers.next,
			       struct udsl_send_buffer, list);
		list_del(&buf->list);
		spin_unlock_irq(&instance->send_lock);

		buf->free_start = buf->base;
		buf->free_cells = snd_buf_size;

		instance->current_buffer = buf;
	}

	num_written = udsl_write_cells(instance, buf->free_cells, skb, &buf->free_start);

	vdbg("udsl_process_send: wrote %u cells from skb 0x%p to buffer 0x%p",
	     num_written, skb, buf);

	if (!(buf->free_cells -= num_written)) {
		list_add_tail(&buf->list, &instance->filled_send_buffers);
		instance->current_buffer = NULL;
	}

	vdbg("udsl_process_send: buffer contains %d cells, %d left",
	     snd_buf_size - buf->free_cells, buf->free_cells);

	if (!UDSL_SKB(skb)->num_cells) {
		struct atm_vcc *vcc = UDSL_SKB(skb)->atm_data.vcc;

		udsl_pop(vcc, skb);
		instance->current_skb = NULL;

		atomic_inc(&vcc->stats->tx);
	}

	goto made_progress;
}

static void udsl_cancel_send(struct udsl_instance_data *instance,
			     struct atm_vcc *vcc)
{
	struct sk_buff *skb, *n;

	dbg("udsl_cancel_send entered");
	spin_lock_irq(&instance->sndqueue.lock);
	for (skb = instance->sndqueue.next, n = skb->next;
	     skb != (struct sk_buff *)&instance->sndqueue;
	     skb = n, n = skb->next)
		if (UDSL_SKB(skb)->atm_data.vcc == vcc) {
			dbg("udsl_cancel_send: popping skb 0x%p", skb);
			__skb_unlink(skb, &instance->sndqueue);
			udsl_pop(vcc, skb);
		}
	spin_unlock_irq(&instance->sndqueue.lock);

	tasklet_disable(&instance->send_tasklet);
	if ((skb = instance->current_skb) && (UDSL_SKB(skb)->atm_data.vcc == vcc)) {
		dbg("udsl_cancel_send: popping current skb (0x%p)", skb);
		instance->current_skb = NULL;
		udsl_pop(vcc, skb);
	}
	tasklet_enable(&instance->send_tasklet);
	dbg("udsl_cancel_send done");
}

static int udsl_atm_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;
	int err;

	vdbg("udsl_atm_send called (skb 0x%p, len %u)", skb, skb->len);

	if (!instance) {
		dbg("udsl_atm_send: NULL data!");
		err = -ENODEV;
		goto fail;
	}

	if (vcc->qos.aal != ATM_AAL5) {
		dbg("udsl_atm_send: unsupported ATM type %d!", vcc->qos.aal);
		err = -EINVAL;
		goto fail;
	}

	if (skb->len > ATM_MAX_AAL5_PDU) {
		dbg("udsl_atm_send: packet too long (%d vs %d)!", skb->len,
		    ATM_MAX_AAL5_PDU);
		err = -EINVAL;
		goto fail;
	}

	PACKETDEBUG(skb->data, skb->len);

	udsl_groom_skb(vcc, skb);
	skb_queue_tail(&instance->sndqueue, skb);
	tasklet_schedule(&instance->send_tasklet);

	return 0;

 fail:
	udsl_pop(vcc, skb);
	return err;
}

/********************
**  bean counting  **
********************/

static void udsl_destroy_instance(struct kref *kref)
{
	struct udsl_instance_data *instance =
	    container_of(kref, struct udsl_instance_data, refcount);

	tasklet_kill(&instance->receive_tasklet);
	tasklet_kill(&instance->send_tasklet);
	usb_put_dev(instance->usb_dev);
	kfree(instance);
}

void udsl_get_instance(struct udsl_instance_data *instance)
{
	kref_get(&instance->refcount);
}

void udsl_put_instance(struct udsl_instance_data *instance)
{
	kref_put(&instance->refcount, udsl_destroy_instance);
}

/**********
**  ATM  **
**********/

static void udsl_atm_dev_close(struct atm_dev *dev)
{
	struct udsl_instance_data *instance = dev->dev_data;

	dev->dev_data = NULL;
	udsl_put_instance(instance);
}

static int udsl_atm_proc_read(struct atm_dev *atm_dev, loff_t * pos, char *page)
{
	struct udsl_instance_data *instance = atm_dev->dev_data;
	int left = *pos;

	if (!instance) {
		dbg("udsl_atm_proc_read: NULL instance!");
		return -ENODEV;
	}

	if (!left--)
		return sprintf(page, "%s\n", instance->description);

	if (!left--)
		return sprintf(page, "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			       atm_dev->esi[0], atm_dev->esi[1],
			       atm_dev->esi[2], atm_dev->esi[3],
			       atm_dev->esi[4], atm_dev->esi[5]);

	if (!left--)
		return sprintf(page,
			       "AAL5: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
			       atomic_read(&atm_dev->stats.aal5.tx),
			       atomic_read(&atm_dev->stats.aal5.tx_err),
			       atomic_read(&atm_dev->stats.aal5.rx),
			       atomic_read(&atm_dev->stats.aal5.rx_err),
			       atomic_read(&atm_dev->stats.aal5.rx_drop));

	if (!left--) {
		switch (atm_dev->signal) {
		case ATM_PHY_SIG_FOUND:
			sprintf(page, "Line up");
			break;
		case ATM_PHY_SIG_LOST:
			sprintf(page, "Line down");
			break;
		default:
			sprintf(page, "Line state unknown");
			break;
		}

		if (instance->usb_dev->state == USB_STATE_NOTATTACHED)
			strcat(page, ", disconnected\n");
		else {
			if (instance->status == UDSL_LOADED_FIRMWARE)
				strcat(page, ", firmware loaded\n");
			else if (instance->status == UDSL_LOADING_FIRMWARE)
				strcat(page, ", firmware loading\n");
			else
				strcat(page, ", no firmware\n");
		}

		return strlen(page);
	}

	return 0;
}

static int udsl_atm_open(struct atm_vcc *vcc)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;
	struct udsl_vcc_data *new;
	unsigned int max_pdu;
	int vci = vcc->vci;
	short vpi = vcc->vpi;
	int err;

	dbg("udsl_atm_open: vpi %hd, vci %d", vpi, vci);

	if (!instance) {
		dbg("udsl_atm_open: NULL data!");
		return -ENODEV;
	}

	/* only support AAL5 */
	if ((vcc->qos.aal != ATM_AAL5) || (vcc->qos.rxtp.max_sdu < 0)
	    || (vcc->qos.rxtp.max_sdu > ATM_MAX_AAL5_PDU)) {
		dbg("udsl_atm_open: unsupported ATM type %d!", vcc->qos.aal);
		return -EINVAL;
	}

	if (instance->firmware_wait &&
	    (err = instance->firmware_wait(instance)) < 0) {
		dbg("udsl_atm_open: firmware not loaded (%d)!", err);
		return err;
	}

	down(&instance->serialize);	/* vs self, udsl_atm_close */

	if (udsl_find_vcc(instance, vpi, vci)) {
		dbg("udsl_atm_open: %hd/%d already in use!", vpi, vci);
		up(&instance->serialize);
		return -EADDRINUSE;
	}

	if (!(new = kmalloc(sizeof(struct udsl_vcc_data), GFP_KERNEL))) {
		dbg("udsl_atm_open: no memory for vcc_data!");
		up(&instance->serialize);
		return -ENOMEM;
	}

	memset(new, 0, sizeof(struct udsl_vcc_data));
	new->vcc = vcc;
	new->vpi = vpi;
	new->vci = vci;

	/* udsl_extract_cells requires at least one cell */
	max_pdu = max(1, UDSL_NUM_CELLS(vcc->qos.rxtp.max_sdu)) * ATM_CELL_PAYLOAD;
	if (!(new->sarb = alloc_skb(max_pdu, GFP_KERNEL))) {
		dbg("udsl_atm_open: no memory for SAR buffer!");
		kfree(new);
		up(&instance->serialize);
		return -ENOMEM;
	}

	vcc->dev_data = new;

	tasklet_disable(&instance->receive_tasklet);
	list_add(&new->list, &instance->vcc_list);
	tasklet_enable(&instance->receive_tasklet);

	set_bit(ATM_VF_ADDR, &vcc->flags);
	set_bit(ATM_VF_PARTIAL, &vcc->flags);
	set_bit(ATM_VF_READY, &vcc->flags);

	up(&instance->serialize);

	tasklet_schedule(&instance->receive_tasklet);

	dbg("udsl_atm_open: allocated vcc data 0x%p (max_pdu: %u)", new, max_pdu);

	return 0;
}

static void udsl_atm_close(struct atm_vcc *vcc)
{
	struct udsl_instance_data *instance = vcc->dev->dev_data;
	struct udsl_vcc_data *vcc_data = vcc->dev_data;

	dbg("udsl_atm_close called");

	if (!instance || !vcc_data) {
		dbg("udsl_atm_close: NULL data!");
		return;
	}

	dbg("udsl_atm_close: deallocating vcc 0x%p with vpi %d vci %d",
	    vcc_data, vcc_data->vpi, vcc_data->vci);

	udsl_cancel_send(instance, vcc);

	down(&instance->serialize);	/* vs self, udsl_atm_open */

	tasklet_disable(&instance->receive_tasklet);
	list_del(&vcc_data->list);
	tasklet_enable(&instance->receive_tasklet);

	kfree_skb(vcc_data->sarb);
	vcc_data->sarb = NULL;

	kfree(vcc_data);
	vcc->dev_data = NULL;

	vcc->vpi = ATM_VPI_UNSPEC;
	vcc->vci = ATM_VCI_UNSPEC;
	clear_bit(ATM_VF_READY, &vcc->flags);
	clear_bit(ATM_VF_PARTIAL, &vcc->flags);
	clear_bit(ATM_VF_ADDR, &vcc->flags);

	up(&instance->serialize);

	dbg("udsl_atm_close successful");
}

static int udsl_atm_ioctl(struct atm_dev *dev, unsigned int cmd,
			  void __user * arg)
{
	switch (cmd) {
	case ATM_QUERYLOOP:
		return put_user(ATM_LM_NONE, (int __user *)arg) ? -EFAULT : 0;
	default:
		return -ENOIOCTLCMD;
	}
}

/**********
**  USB  **
**********/

int udsl_instance_setup(struct usb_device *dev,
			struct udsl_instance_data *instance)
{
	char *buf;
	int i, length;

	kref_init(&instance->refcount);	/* one for USB */
	udsl_get_instance(instance);	/* one for ATM */

	init_MUTEX(&instance->serialize);

	instance->usb_dev = dev;

	INIT_LIST_HEAD(&instance->vcc_list);

	instance->status = UDSL_NO_FIRMWARE;
	init_waitqueue_head(&instance->firmware_waiters);

	spin_lock_init(&instance->receive_lock);
	INIT_LIST_HEAD(&instance->spare_receivers);
	INIT_LIST_HEAD(&instance->filled_receive_buffers);

	tasklet_init(&instance->receive_tasklet, udsl_process_receive, (unsigned long)instance);
	INIT_LIST_HEAD(&instance->spare_receive_buffers);

	skb_queue_head_init(&instance->sndqueue);

	spin_lock_init(&instance->send_lock);
	INIT_LIST_HEAD(&instance->spare_senders);
	INIT_LIST_HEAD(&instance->spare_send_buffers);

	tasklet_init(&instance->send_tasklet, udsl_process_send,
		     (unsigned long)instance);
	INIT_LIST_HEAD(&instance->filled_send_buffers);

	/* receive init */
	for (i = 0; i < num_rcv_urbs; i++) {
		struct udsl_receiver *rcv = &(instance->receivers[i]);

		if (!(rcv->urb = usb_alloc_urb(0, GFP_KERNEL))) {
			dbg("udsl_usb_probe: no memory for receive urb %d!", i);
			goto fail;
		}

		rcv->instance = instance;

		list_add(&rcv->list, &instance->spare_receivers);
	}

	for (i = 0; i < num_rcv_bufs; i++) {
		struct udsl_receive_buffer *buf =
		    &(instance->receive_buffers[i]);

		buf->base = kmalloc(rcv_buf_size * (ATM_CELL_SIZE + instance->rcv_padding),
				    GFP_KERNEL);
		if (!buf->base) {
			dbg("udsl_usb_probe: no memory for receive buffer %d!", i);
			goto fail;
		}

		list_add(&buf->list, &instance->spare_receive_buffers);
	}

	/* send init */
	for (i = 0; i < num_snd_urbs; i++) {
		struct udsl_sender *snd = &(instance->senders[i]);

		if (!(snd->urb = usb_alloc_urb(0, GFP_KERNEL))) {
			dbg("udsl_usb_probe: no memory for send urb %d!", i);
			goto fail;
		}

		snd->instance = instance;

		list_add(&snd->list, &instance->spare_senders);
	}

	for (i = 0; i < num_snd_bufs; i++) {
		struct udsl_send_buffer *buf = &(instance->send_buffers[i]);

		buf->base = kmalloc(snd_buf_size * (ATM_CELL_SIZE + instance->snd_padding),
				    GFP_KERNEL);
		if (!buf->base) {
			dbg("udsl_usb_probe: no memory for send buffer %d!", i);
			goto fail;
		}

		list_add(&buf->list, &instance->spare_send_buffers);
	}

	/* ATM init */
	instance->atm_dev = atm_dev_register(instance->driver_name,
					     &udsl_atm_devops, -1, NULL);
	if (!instance->atm_dev) {
		dbg("udsl_usb_probe: failed to register ATM device!");
		goto fail;
	}

	instance->atm_dev->ci_range.vpi_bits = ATM_CI_MAX;
	instance->atm_dev->ci_range.vci_bits = ATM_CI_MAX;
	instance->atm_dev->signal = ATM_PHY_SIG_UNKNOWN;

	/* temp init ATM device, set to 128kbit */
	instance->atm_dev->link_rate = 128 * 1000 / 424;

	/* device description */
	buf = instance->description;
	length = sizeof(instance->description);

	if ((i = usb_string(dev, dev->descriptor.iProduct, buf, length)) < 0)
		goto finish;

	buf += i;
	length -= i;

	i = scnprintf(buf, length, " (");
	buf += i;
	length -= i;

	if (length <= 0 || (i = usb_make_path(dev, buf, length)) < 0)
		goto finish;

	buf += i;
	length -= i;

	snprintf(buf, length, ")");

 finish:
	/* ready for ATM callbacks */
	wmb();
	instance->atm_dev->dev_data = instance;

	usb_get_dev(dev);

	return 0;

 fail:
	for (i = 0; i < num_snd_bufs; i++)
		kfree(instance->send_buffers[i].base);

	for (i = 0; i < num_snd_urbs; i++)
		usb_free_urb(instance->senders[i].urb);

	for (i = 0; i < num_rcv_bufs; i++)
		kfree(instance->receive_buffers[i].base);

	for (i = 0; i < num_rcv_urbs; i++)
		usb_free_urb(instance->receivers[i].urb);

	return -ENOMEM;
}

void udsl_instance_disconnect(struct udsl_instance_data *instance)
{
	int i;

	dbg("udsl_instance_disconnect entered");

	if (!instance) {
		dbg("udsl_instance_disconnect: NULL instance!");
		return;
	}

	/* receive finalize */
	tasklet_disable(&instance->receive_tasklet);

	for (i = 0; i < num_rcv_urbs; i++)
		usb_kill_urb(instance->receivers[i].urb);

	/* no need to take the spinlock */
	INIT_LIST_HEAD(&instance->filled_receive_buffers);
	INIT_LIST_HEAD(&instance->spare_receive_buffers);

	tasklet_enable(&instance->receive_tasklet);

	for (i = 0; i < num_rcv_urbs; i++)
		usb_free_urb(instance->receivers[i].urb);

	for (i = 0; i < num_rcv_bufs; i++)
		kfree(instance->receive_buffers[i].base);

	/* send finalize */
	tasklet_disable(&instance->send_tasklet);

	for (i = 0; i < num_snd_urbs; i++)
		usb_kill_urb(instance->senders[i].urb);

	/* no need to take the spinlock */
	INIT_LIST_HEAD(&instance->spare_senders);
	INIT_LIST_HEAD(&instance->spare_send_buffers);
	instance->current_buffer = NULL;

	tasklet_enable(&instance->send_tasklet);

	for (i = 0; i < num_snd_urbs; i++)
		usb_free_urb(instance->senders[i].urb);

	for (i = 0; i < num_snd_bufs; i++)
		kfree(instance->send_buffers[i].base);

	/* ATM finalize */
	shutdown_atm_dev(instance->atm_dev);
}

EXPORT_SYMBOL_GPL(udsl_get_instance);
EXPORT_SYMBOL_GPL(udsl_put_instance);
EXPORT_SYMBOL_GPL(udsl_instance_setup);
EXPORT_SYMBOL_GPL(udsl_instance_disconnect);

/***********
**  init  **
***********/

static int __init udsl_usb_init(void)
{
	dbg("udsl_usb_init: driver version " DRIVER_VERSION);

	if (sizeof(struct udsl_control) > sizeof(((struct sk_buff *) 0)->cb)) {
		printk(KERN_ERR __FILE__ ": unusable with this kernel!\n");
		return -EIO;
	}

	if ((num_rcv_urbs > UDSL_MAX_RCV_URBS)
	    || (num_snd_urbs > UDSL_MAX_SND_URBS)
	    || (num_rcv_bufs > UDSL_MAX_RCV_BUFS)
	    || (num_snd_bufs > UDSL_MAX_SND_BUFS)
	    || (rcv_buf_size > UDSL_MAX_RCV_BUF_SIZE)
	    || (snd_buf_size > UDSL_MAX_SND_BUF_SIZE))
		return -EINVAL;

	return 0;
}

static void __exit udsl_usb_exit(void)
{
}

module_init(udsl_usb_init);
module_exit(udsl_usb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

/************
**  debug  **
************/

#ifdef VERBOSE_DEBUG
static int udsl_print_packet(const unsigned char *data, int len)
{
	unsigned char buffer[256];
	int i = 0, j = 0;

	for (i = 0; i < len;) {
		buffer[0] = '\0';
		sprintf(buffer, "%.3d :", i);
		for (j = 0; (j < 16) && (i < len); j++, i++) {
			sprintf(buffer, "%s %2.2x", buffer, data[i]);
		}
		dbg("%s", buffer);
	}
	return i;
}
#endif
