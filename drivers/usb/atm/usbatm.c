/******************************************************************************
 *  usbatm.c - Generic USB xDSL driver core
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands, SolNegro, Josep Comas
 *  Copyright (C) 2004, David Woodhouse, Roman Kagan
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
 *  Written by Johan Verrept, Duncan Sands (duncan.sands@free.fr) and David Woodhouse
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
 *		usbatm_usb_send_data_context->urb to a pointer and adding code
 *		to alloc and free it
 *		- remove_wait_queue() added to usbatm_atm_processqueue_thread()
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
 *  1.2:	- Fixed race condition in usbatm_usb_send_data()
 *  1.1:	- Turned off packet debugging
 *
 */

#include "usbatm.h"

#include <linux/uaccess.h>
#include <linux/crc32.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/ratelimit.h>

#ifdef VERBOSE_DEBUG
static int usbatm_print_packet(struct usbatm_data *instance, const unsigned char *data, int len);
#define PACKETDEBUG(arg...)	usbatm_print_packet(arg)
#define vdbg(arg...)		dev_dbg(arg)
#else
#define PACKETDEBUG(arg...)
#define vdbg(arg...)
#endif

#define DRIVER_AUTHOR	"Johan Verrept, Duncan Sands <duncan.sands@free.fr>"
#define DRIVER_DESC	"Generic USB ATM/DSL I/O"

static const char usbatm_driver_name[] = "usbatm";

#define UDSL_MAX_RCV_URBS		16
#define UDSL_MAX_SND_URBS		16
#define UDSL_MAX_BUF_SIZE		65536
#define UDSL_DEFAULT_RCV_URBS		4
#define UDSL_DEFAULT_SND_URBS		4
#define UDSL_DEFAULT_RCV_BUF_SIZE	3392	/* 64 * ATM_CELL_SIZE */
#define UDSL_DEFAULT_SND_BUF_SIZE	3392	/* 64 * ATM_CELL_SIZE */

#define ATM_CELL_HEADER			(ATM_CELL_SIZE - ATM_CELL_PAYLOAD)

#define THROTTLE_MSECS			100	/* delay to recover processing after urb submission fails */

static unsigned int num_rcv_urbs = UDSL_DEFAULT_RCV_URBS;
static unsigned int num_snd_urbs = UDSL_DEFAULT_SND_URBS;
static unsigned int rcv_buf_bytes = UDSL_DEFAULT_RCV_BUF_SIZE;
static unsigned int snd_buf_bytes = UDSL_DEFAULT_SND_BUF_SIZE;

module_param(num_rcv_urbs, uint, S_IRUGO);
MODULE_PARM_DESC(num_rcv_urbs,
		 "Number of urbs used for reception (range: 0-"
		 __MODULE_STRING(UDSL_MAX_RCV_URBS) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_RCV_URBS) ")");

module_param(num_snd_urbs, uint, S_IRUGO);
MODULE_PARM_DESC(num_snd_urbs,
		 "Number of urbs used for transmission (range: 0-"
		 __MODULE_STRING(UDSL_MAX_SND_URBS) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_SND_URBS) ")");

module_param(rcv_buf_bytes, uint, S_IRUGO);
MODULE_PARM_DESC(rcv_buf_bytes,
		 "Size of the buffers used for reception, in bytes (range: 1-"
		 __MODULE_STRING(UDSL_MAX_BUF_SIZE) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_RCV_BUF_SIZE) ")");

module_param(snd_buf_bytes, uint, S_IRUGO);
MODULE_PARM_DESC(snd_buf_bytes,
		 "Size of the buffers used for transmission, in bytes (range: 1-"
		 __MODULE_STRING(UDSL_MAX_BUF_SIZE) ", default: "
		 __MODULE_STRING(UDSL_DEFAULT_SND_BUF_SIZE) ")");


/* receive */

struct usbatm_vcc_data {
	/* vpi/vci lookup */
	struct list_head list;
	short vpi;
	int vci;
	struct atm_vcc *vcc;

	/* raw cell reassembly */
	struct sk_buff *sarb;
};


/* send */

struct usbatm_control {
	struct atm_skb_data atm;
	u32 len;
	u32 crc;
};

#define UDSL_SKB(x)		((struct usbatm_control *)(x)->cb)


/* ATM */

static void usbatm_atm_dev_close(struct atm_dev *atm_dev);
static int usbatm_atm_open(struct atm_vcc *vcc);
static void usbatm_atm_close(struct atm_vcc *vcc);
static int usbatm_atm_ioctl(struct atm_dev *atm_dev, unsigned int cmd, void __user *arg);
static int usbatm_atm_send(struct atm_vcc *vcc, struct sk_buff *skb);
static int usbatm_atm_proc_read(struct atm_dev *atm_dev, loff_t *pos, char *page);

static struct atmdev_ops usbatm_atm_devops = {
	.dev_close	= usbatm_atm_dev_close,
	.open		= usbatm_atm_open,
	.close		= usbatm_atm_close,
	.ioctl		= usbatm_atm_ioctl,
	.send		= usbatm_atm_send,
	.proc_read	= usbatm_atm_proc_read,
	.owner		= THIS_MODULE,
};


/***********
**  misc  **
***********/

static inline unsigned int usbatm_pdu_length(unsigned int length)
{
	length += ATM_CELL_PAYLOAD - 1 + ATM_AAL5_TRAILER;
	return length - length % ATM_CELL_PAYLOAD;
}

static inline void usbatm_pop(struct atm_vcc *vcc, struct sk_buff *skb)
{
	if (vcc->pop)
		vcc->pop(vcc, skb);
	else
		dev_kfree_skb_any(skb);
}


/***********
**  urbs  **
************/

static struct urb *usbatm_pop_urb(struct usbatm_channel *channel)
{
	struct urb *urb;

	spin_lock_irq(&channel->lock);
	if (list_empty(&channel->list)) {
		spin_unlock_irq(&channel->lock);
		return NULL;
	}

	urb = list_entry(channel->list.next, struct urb, urb_list);
	list_del(&urb->urb_list);
	spin_unlock_irq(&channel->lock);

	return urb;
}

static int usbatm_submit_urb(struct urb *urb)
{
	struct usbatm_channel *channel = urb->context;
	int ret;

	/* vdbg("%s: submitting urb 0x%p, size %u",
	     __func__, urb, urb->transfer_buffer_length); */

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		if (printk_ratelimit())
			atm_warn(channel->usbatm, "%s: urb 0x%p submission failed (%d)!\n",
				__func__, urb, ret);

		/* consider all errors transient and return the buffer back to the queue */
		urb->status = -EAGAIN;
		spin_lock_irq(&channel->lock);

		/* must add to the front when sending; doesn't matter when receiving */
		list_add(&urb->urb_list, &channel->list);

		spin_unlock_irq(&channel->lock);

		/* make sure the channel doesn't stall */
		mod_timer(&channel->delay, jiffies + msecs_to_jiffies(THROTTLE_MSECS));
	}

	return ret;
}

static void usbatm_complete(struct urb *urb)
{
	struct usbatm_channel *channel = urb->context;
	unsigned long flags;
	int status = urb->status;

	/* vdbg("%s: urb 0x%p, status %d, actual_length %d",
	     __func__, urb, status, urb->actual_length); */

	/* usually in_interrupt(), but not always */
	spin_lock_irqsave(&channel->lock, flags);

	/* must add to the back when receiving; doesn't matter when sending */
	list_add_tail(&urb->urb_list, &channel->list);

	spin_unlock_irqrestore(&channel->lock, flags);

	if (unlikely(status) &&
			(!(channel->usbatm->flags & UDSL_IGNORE_EILSEQ) ||
			 status != -EILSEQ)) {
		if (status == -ESHUTDOWN)
			return;

		if (printk_ratelimit())
			atm_warn(channel->usbatm, "%s: urb 0x%p failed (%d)!\n",
				__func__, urb, status);
		/* throttle processing in case of an error */
		mod_timer(&channel->delay, jiffies + msecs_to_jiffies(THROTTLE_MSECS));
	} else
		tasklet_schedule(&channel->tasklet);
}


/*************
**  decode  **
*************/

static inline struct usbatm_vcc_data *usbatm_find_vcc(struct usbatm_data *instance,
						  short vpi, int vci)
{
	struct usbatm_vcc_data *vcc_data;

	list_for_each_entry(vcc_data, &instance->vcc_list, list)
		if ((vcc_data->vci == vci) && (vcc_data->vpi == vpi))
			return vcc_data;
	return NULL;
}

static void usbatm_extract_one_cell(struct usbatm_data *instance, unsigned char *source)
{
	struct atm_vcc *vcc;
	struct sk_buff *sarb;
	short vpi = ((source[0] & 0x0f) << 4)  | (source[1] >> 4);
	int vci = ((source[1] & 0x0f) << 12) | (source[2] << 4) | (source[3] >> 4);
	u8 pti = ((source[3] & 0xe) >> 1);

	if ((vci != instance->cached_vci) || (vpi != instance->cached_vpi)) {
		instance->cached_vpi = vpi;
		instance->cached_vci = vci;

		instance->cached_vcc = usbatm_find_vcc(instance, vpi, vci);

		if (!instance->cached_vcc)
			atm_rldbg(instance, "%s: unknown vpi/vci (%hd/%d)!\n", __func__, vpi, vci);
	}

	if (!instance->cached_vcc)
		return;

	vcc = instance->cached_vcc->vcc;

	/* OAM F5 end-to-end */
	if (pti == ATM_PTI_E2EF5) {
		if (printk_ratelimit())
			atm_warn(instance, "%s: OAM not supported (vpi %d, vci %d)!\n",
				__func__, vpi, vci);
		atomic_inc(&vcc->stats->rx_err);
		return;
	}

	sarb = instance->cached_vcc->sarb;

	if (sarb->tail + ATM_CELL_PAYLOAD > sarb->end) {
		atm_rldbg(instance, "%s: buffer overrun (sarb->len %u, vcc: 0x%p)!\n",
				__func__, sarb->len, vcc);
		/* discard cells already received */
		skb_trim(sarb, 0);
	}

	memcpy(skb_tail_pointer(sarb), source + ATM_CELL_HEADER, ATM_CELL_PAYLOAD);
	__skb_put(sarb, ATM_CELL_PAYLOAD);

	if (pti & 1) {
		struct sk_buff *skb;
		unsigned int length;
		unsigned int pdu_length;

		length = (source[ATM_CELL_SIZE - 6] << 8) + source[ATM_CELL_SIZE - 5];

		/* guard against overflow */
		if (length > ATM_MAX_AAL5_PDU) {
			atm_rldbg(instance, "%s: bogus length %u (vcc: 0x%p)!\n",
				  __func__, length, vcc);
			atomic_inc(&vcc->stats->rx_err);
			goto out;
		}

		pdu_length = usbatm_pdu_length(length);

		if (sarb->len < pdu_length) {
			atm_rldbg(instance, "%s: bogus pdu_length %u (sarb->len: %u, vcc: 0x%p)!\n",
				  __func__, pdu_length, sarb->len, vcc);
			atomic_inc(&vcc->stats->rx_err);
			goto out;
		}

		if (crc32_be(~0, skb_tail_pointer(sarb) - pdu_length, pdu_length) != 0xc704dd7b) {
			atm_rldbg(instance, "%s: packet failed crc check (vcc: 0x%p)!\n",
				  __func__, vcc);
			atomic_inc(&vcc->stats->rx_err);
			goto out;
		}

		vdbg(&instance->usb_intf->dev,
		     "%s: got packet (length: %u, pdu_length: %u, vcc: 0x%p)",
		     __func__, length, pdu_length, vcc);

		skb = dev_alloc_skb(length);
		if (!skb) {
			if (printk_ratelimit())
				atm_err(instance, "%s: no memory for skb (length: %u)!\n",
					__func__, length);
			atomic_inc(&vcc->stats->rx_drop);
			goto out;
		}

		vdbg(&instance->usb_intf->dev,
		     "%s: allocated new sk_buff (skb: 0x%p, skb->truesize: %u)",
		     __func__, skb, skb->truesize);

		if (!atm_charge(vcc, skb->truesize)) {
			atm_rldbg(instance, "%s: failed atm_charge (skb->truesize: %u)!\n",
				  __func__, skb->truesize);
			dev_kfree_skb_any(skb);
			goto out;	/* atm_charge increments rx_drop */
		}

		skb_copy_to_linear_data(skb,
					skb_tail_pointer(sarb) - pdu_length,
					length);
		__skb_put(skb, length);

		vdbg(&instance->usb_intf->dev,
		     "%s: sending skb 0x%p, skb->len %u, skb->truesize %u",
		     __func__, skb, skb->len, skb->truesize);

		PACKETDEBUG(instance, skb->data, skb->len);

		vcc->push(vcc, skb);

		atomic_inc(&vcc->stats->rx);
	out:
		skb_trim(sarb, 0);
	}
}

static void usbatm_extract_cells(struct usbatm_data *instance,
		unsigned char *source, unsigned int avail_data)
{
	unsigned int stride = instance->rx_channel.stride;
	unsigned int buf_usage = instance->buf_usage;

	/* extract cells from incoming data, taking into account that
	 * the length of avail data may not be a multiple of stride */

	if (buf_usage > 0) {
		/* we have a partially received atm cell */
		unsigned char *cell_buf = instance->cell_buf;
		unsigned int space_left = stride - buf_usage;

		if (avail_data >= space_left) {
			/* add new data and process cell */
			memcpy(cell_buf + buf_usage, source, space_left);
			source += space_left;
			avail_data -= space_left;
			usbatm_extract_one_cell(instance, cell_buf);
			instance->buf_usage = 0;
		} else {
			/* not enough data to fill the cell */
			memcpy(cell_buf + buf_usage, source, avail_data);
			instance->buf_usage = buf_usage + avail_data;
			return;
		}
	}

	for (; avail_data >= stride; avail_data -= stride, source += stride)
		usbatm_extract_one_cell(instance, source);

	if (avail_data > 0) {
		/* length was not a multiple of stride -
		 * save remaining data for next call */
		memcpy(instance->cell_buf, source, avail_data);
		instance->buf_usage = avail_data;
	}
}


/*************
**  encode  **
*************/

static unsigned int usbatm_write_cells(struct usbatm_data *instance,
				       struct sk_buff *skb,
				       u8 *target, unsigned int avail_space)
{
	struct usbatm_control *ctrl = UDSL_SKB(skb);
	struct atm_vcc *vcc = ctrl->atm.vcc;
	unsigned int bytes_written;
	unsigned int stride = instance->tx_channel.stride;

	for (bytes_written = 0; bytes_written < avail_space && ctrl->len;
	     bytes_written += stride, target += stride) {
		unsigned int data_len = min_t(unsigned int, skb->len, ATM_CELL_PAYLOAD);
		unsigned int left = ATM_CELL_PAYLOAD - data_len;
		u8 *ptr = target;

		ptr[0] = vcc->vpi >> 4;
		ptr[1] = (vcc->vpi << 4) | (vcc->vci >> 12);
		ptr[2] = vcc->vci >> 4;
		ptr[3] = vcc->vci << 4;
		ptr[4] = 0xec;
		ptr += ATM_CELL_HEADER;

		skb_copy_from_linear_data(skb, ptr, data_len);
		ptr += data_len;
		__skb_pull(skb, data_len);

		if (!left)
			continue;

		memset(ptr, 0, left);

		if (left >= ATM_AAL5_TRAILER) {	/* trailer will go in this cell */
			u8 *trailer = target + ATM_CELL_SIZE - ATM_AAL5_TRAILER;
			/* trailer[0] = 0;		UU = 0 */
			/* trailer[1] = 0;		CPI = 0 */
			trailer[2] = ctrl->len >> 8;
			trailer[3] = ctrl->len;

			ctrl->crc = ~crc32_be(ctrl->crc, ptr, left - 4);

			trailer[4] = ctrl->crc >> 24;
			trailer[5] = ctrl->crc >> 16;
			trailer[6] = ctrl->crc >> 8;
			trailer[7] = ctrl->crc;

			target[3] |= 0x2;	/* adjust PTI */

			ctrl->len = 0;		/* tag this skb finished */
		} else
			ctrl->crc = crc32_be(ctrl->crc, ptr, left);
	}

	return bytes_written;
}


/**************
**  receive  **
**************/

static void usbatm_rx_process(unsigned long data)
{
	struct usbatm_data *instance = (struct usbatm_data *)data;
	struct urb *urb;

	while ((urb = usbatm_pop_urb(&instance->rx_channel))) {
		vdbg(&instance->usb_intf->dev,
		     "%s: processing urb 0x%p", __func__, urb);

		if (usb_pipeisoc(urb->pipe)) {
			unsigned char *merge_start = NULL;
			unsigned int merge_length = 0;
			const unsigned int packet_size = instance->rx_channel.packet_size;
			int i;

			for (i = 0; i < urb->number_of_packets; i++) {
				if (!urb->iso_frame_desc[i].status) {
					unsigned int actual_length = urb->iso_frame_desc[i].actual_length;

					if (!merge_length)
						merge_start = (unsigned char *)urb->transfer_buffer + urb->iso_frame_desc[i].offset;
					merge_length += actual_length;
					if (merge_length && (actual_length < packet_size)) {
						usbatm_extract_cells(instance, merge_start, merge_length);
						merge_length = 0;
					}
				} else {
					atm_rldbg(instance, "%s: status %d in frame %d!\n", __func__, urb->status, i);
					if (merge_length)
						usbatm_extract_cells(instance, merge_start, merge_length);
					merge_length = 0;
					instance->buf_usage = 0;
				}
			}

			if (merge_length)
				usbatm_extract_cells(instance, merge_start, merge_length);
		} else
			if (!urb->status)
				usbatm_extract_cells(instance, urb->transfer_buffer, urb->actual_length);
			else
				instance->buf_usage = 0;

		if (usbatm_submit_urb(urb))
			return;
	}
}


/***********
**  send  **
***********/

static void usbatm_tx_process(unsigned long data)
{
	struct usbatm_data *instance = (struct usbatm_data *)data;
	struct sk_buff *skb = instance->current_skb;
	struct urb *urb = NULL;
	const unsigned int buf_size = instance->tx_channel.buf_size;
	unsigned int bytes_written = 0;
	u8 *buffer = NULL;

	if (!skb)
		skb = skb_dequeue(&instance->sndqueue);

	while (skb) {
		if (!urb) {
			urb = usbatm_pop_urb(&instance->tx_channel);
			if (!urb)
				break;		/* no more senders */
			buffer = urb->transfer_buffer;
			bytes_written = (urb->status == -EAGAIN) ?
				urb->transfer_buffer_length : 0;
		}

		bytes_written += usbatm_write_cells(instance, skb,
						  buffer + bytes_written,
						  buf_size - bytes_written);

		vdbg(&instance->usb_intf->dev,
		     "%s: wrote %u bytes from skb 0x%p to urb 0x%p",
		     __func__, bytes_written, skb, urb);

		if (!UDSL_SKB(skb)->len) {
			struct atm_vcc *vcc = UDSL_SKB(skb)->atm.vcc;

			usbatm_pop(vcc, skb);
			atomic_inc(&vcc->stats->tx);

			skb = skb_dequeue(&instance->sndqueue);
		}

		if (bytes_written == buf_size || (!skb && bytes_written)) {
			urb->transfer_buffer_length = bytes_written;

			if (usbatm_submit_urb(urb))
				break;
			urb = NULL;
		}
	}

	instance->current_skb = skb;
}

static void usbatm_cancel_send(struct usbatm_data *instance,
			       struct atm_vcc *vcc)
{
	struct sk_buff *skb, *n;

	spin_lock_irq(&instance->sndqueue.lock);
	skb_queue_walk_safe(&instance->sndqueue, skb, n) {
		if (UDSL_SKB(skb)->atm.vcc == vcc) {
			atm_dbg(instance, "%s: popping skb 0x%p\n", __func__, skb);
			__skb_unlink(skb, &instance->sndqueue);
			usbatm_pop(vcc, skb);
		}
	}
	spin_unlock_irq(&instance->sndqueue.lock);

	tasklet_disable(&instance->tx_channel.tasklet);
	if ((skb = instance->current_skb) && (UDSL_SKB(skb)->atm.vcc == vcc)) {
		atm_dbg(instance, "%s: popping current skb (0x%p)\n", __func__, skb);
		instance->current_skb = NULL;
		usbatm_pop(vcc, skb);
	}
	tasklet_enable(&instance->tx_channel.tasklet);
}

static int usbatm_atm_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct usbatm_data *instance = vcc->dev->dev_data;
	struct usbatm_control *ctrl = UDSL_SKB(skb);
	int err;

	/* racy disconnection check - fine */
	if (!instance || instance->disconnected) {
#ifdef VERBOSE_DEBUG
		printk_ratelimited(KERN_DEBUG "%s: %s!\n", __func__, instance ? "disconnected" : "NULL instance");
#endif
		err = -ENODEV;
		goto fail;
	}

	if (vcc->qos.aal != ATM_AAL5) {
		atm_rldbg(instance, "%s: unsupported ATM type %d!\n", __func__, vcc->qos.aal);
		err = -EINVAL;
		goto fail;
	}

	if (skb->len > ATM_MAX_AAL5_PDU) {
		atm_rldbg(instance, "%s: packet too long (%d vs %d)!\n",
				__func__, skb->len, ATM_MAX_AAL5_PDU);
		err = -EINVAL;
		goto fail;
	}

	PACKETDEBUG(instance, skb->data, skb->len);

	/* initialize the control block */
	ctrl->atm.vcc = vcc;
	ctrl->len = skb->len;
	ctrl->crc = crc32_be(~0, skb->data, skb->len);

	skb_queue_tail(&instance->sndqueue, skb);
	tasklet_schedule(&instance->tx_channel.tasklet);

	return 0;

 fail:
	usbatm_pop(vcc, skb);
	return err;
}


/********************
**  bean counting  **
********************/

static void usbatm_destroy_instance(struct kref *kref)
{
	struct usbatm_data *instance = container_of(kref, struct usbatm_data, refcount);

	tasklet_kill(&instance->rx_channel.tasklet);
	tasklet_kill(&instance->tx_channel.tasklet);
	usb_put_dev(instance->usb_dev);
	kfree(instance);
}

static void usbatm_get_instance(struct usbatm_data *instance)
{
	kref_get(&instance->refcount);
}

static void usbatm_put_instance(struct usbatm_data *instance)
{
	kref_put(&instance->refcount, usbatm_destroy_instance);
}


/**********
**  ATM  **
**********/

static void usbatm_atm_dev_close(struct atm_dev *atm_dev)
{
	struct usbatm_data *instance = atm_dev->dev_data;

	if (!instance)
		return;

	atm_dev->dev_data = NULL; /* catch bugs */
	usbatm_put_instance(instance);	/* taken in usbatm_atm_init */
}

static int usbatm_atm_proc_read(struct atm_dev *atm_dev, loff_t *pos, char *page)
{
	struct usbatm_data *instance = atm_dev->dev_data;
	int left = *pos;

	if (!instance)
		return -ENODEV;

	if (!left--)
		return sprintf(page, "%s\n", instance->description);

	if (!left--)
		return sprintf(page, "MAC: %pM\n", atm_dev->esi);

	if (!left--)
		return sprintf(page,
			       "AAL5: tx %d ( %d err ), rx %d ( %d err, %d drop )\n",
			       atomic_read(&atm_dev->stats.aal5.tx),
			       atomic_read(&atm_dev->stats.aal5.tx_err),
			       atomic_read(&atm_dev->stats.aal5.rx),
			       atomic_read(&atm_dev->stats.aal5.rx_err),
			       atomic_read(&atm_dev->stats.aal5.rx_drop));

	if (!left--) {
		if (instance->disconnected)
			return sprintf(page, "Disconnected\n");
		else
			switch (atm_dev->signal) {
			case ATM_PHY_SIG_FOUND:
				return sprintf(page, "Line up\n");
			case ATM_PHY_SIG_LOST:
				return sprintf(page, "Line down\n");
			default:
				return sprintf(page, "Line state unknown\n");
			}
	}

	return 0;
}

static int usbatm_atm_open(struct atm_vcc *vcc)
{
	struct usbatm_data *instance = vcc->dev->dev_data;
	struct usbatm_vcc_data *new = NULL;
	int ret;
	int vci = vcc->vci;
	short vpi = vcc->vpi;

	if (!instance)
		return -ENODEV;

	/* only support AAL5 */
	if ((vcc->qos.aal != ATM_AAL5)) {
		atm_warn(instance, "%s: unsupported ATM type %d!\n", __func__, vcc->qos.aal);
		return -EINVAL;
	}

	/* sanity checks */
	if ((vcc->qos.rxtp.max_sdu < 0) || (vcc->qos.rxtp.max_sdu > ATM_MAX_AAL5_PDU)) {
		atm_dbg(instance, "%s: max_sdu %d out of range!\n", __func__, vcc->qos.rxtp.max_sdu);
		return -EINVAL;
	}

	mutex_lock(&instance->serialize);	/* vs self, usbatm_atm_close, usbatm_usb_disconnect */

	if (instance->disconnected) {
		atm_dbg(instance, "%s: disconnected!\n", __func__);
		ret = -ENODEV;
		goto fail;
	}

	if (usbatm_find_vcc(instance, vpi, vci)) {
		atm_dbg(instance, "%s: %hd/%d already in use!\n", __func__, vpi, vci);
		ret = -EADDRINUSE;
		goto fail;
	}

	new = kzalloc(sizeof(struct usbatm_vcc_data), GFP_KERNEL);
	if (!new) {
		ret = -ENOMEM;
		goto fail;
	}

	new->vcc = vcc;
	new->vpi = vpi;
	new->vci = vci;

	new->sarb = alloc_skb(usbatm_pdu_length(vcc->qos.rxtp.max_sdu), GFP_KERNEL);
	if (!new->sarb) {
		atm_err(instance, "%s: no memory for SAR buffer!\n", __func__);
		ret = -ENOMEM;
		goto fail;
	}

	vcc->dev_data = new;

	tasklet_disable(&instance->rx_channel.tasklet);
	instance->cached_vcc = new;
	instance->cached_vpi = vpi;
	instance->cached_vci = vci;
	list_add(&new->list, &instance->vcc_list);
	tasklet_enable(&instance->rx_channel.tasklet);

	set_bit(ATM_VF_ADDR, &vcc->flags);
	set_bit(ATM_VF_PARTIAL, &vcc->flags);
	set_bit(ATM_VF_READY, &vcc->flags);

	mutex_unlock(&instance->serialize);

	atm_dbg(instance, "%s: allocated vcc data 0x%p\n", __func__, new);

	return 0;

fail:
	kfree(new);
	mutex_unlock(&instance->serialize);
	return ret;
}

static void usbatm_atm_close(struct atm_vcc *vcc)
{
	struct usbatm_data *instance = vcc->dev->dev_data;
	struct usbatm_vcc_data *vcc_data = vcc->dev_data;

	if (!instance || !vcc_data)
		return;

	usbatm_cancel_send(instance, vcc);

	mutex_lock(&instance->serialize);	/* vs self, usbatm_atm_open, usbatm_usb_disconnect */

	tasklet_disable(&instance->rx_channel.tasklet);
	if (instance->cached_vcc == vcc_data) {
		instance->cached_vcc = NULL;
		instance->cached_vpi = ATM_VPI_UNSPEC;
		instance->cached_vci = ATM_VCI_UNSPEC;
	}
	list_del(&vcc_data->list);
	tasklet_enable(&instance->rx_channel.tasklet);

	kfree_skb(vcc_data->sarb);
	vcc_data->sarb = NULL;

	kfree(vcc_data);
	vcc->dev_data = NULL;

	vcc->vpi = ATM_VPI_UNSPEC;
	vcc->vci = ATM_VCI_UNSPEC;
	clear_bit(ATM_VF_READY, &vcc->flags);
	clear_bit(ATM_VF_PARTIAL, &vcc->flags);
	clear_bit(ATM_VF_ADDR, &vcc->flags);

	mutex_unlock(&instance->serialize);
}

static int usbatm_atm_ioctl(struct atm_dev *atm_dev, unsigned int cmd,
			  void __user *arg)
{
	struct usbatm_data *instance = atm_dev->dev_data;

	if (!instance || instance->disconnected)
		return -ENODEV;

	switch (cmd) {
	case ATM_QUERYLOOP:
		return put_user(ATM_LM_NONE, (int __user *)arg) ? -EFAULT : 0;
	default:
		return -ENOIOCTLCMD;
	}
}

static int usbatm_atm_init(struct usbatm_data *instance)
{
	struct atm_dev *atm_dev;
	int ret, i;

	/* ATM init.  The ATM initialization scheme suffers from an intrinsic race
	 * condition: callbacks we register can be executed at once, before we have
	 * initialized the struct atm_dev.  To protect against this, all callbacks
	 * abort if atm_dev->dev_data is NULL. */
	atm_dev = atm_dev_register(instance->driver_name,
				   &instance->usb_intf->dev, &usbatm_atm_devops,
				   -1, NULL);
	if (!atm_dev) {
		usb_err(instance, "%s: failed to register ATM device!\n", __func__);
		return -1;
	}

	instance->atm_dev = atm_dev;

	atm_dev->ci_range.vpi_bits = ATM_CI_MAX;
	atm_dev->ci_range.vci_bits = ATM_CI_MAX;
	atm_dev->signal = ATM_PHY_SIG_UNKNOWN;

	/* temp init ATM device, set to 128kbit */
	atm_dev->link_rate = 128 * 1000 / 424;

	if (instance->driver->atm_start && ((ret = instance->driver->atm_start(instance, atm_dev)) < 0)) {
		atm_err(instance, "%s: atm_start failed: %d!\n", __func__, ret);
		goto fail;
	}

	usbatm_get_instance(instance);	/* dropped in usbatm_atm_dev_close */

	/* ready for ATM callbacks */
	mb();
	atm_dev->dev_data = instance;

	/* submit all rx URBs */
	for (i = 0; i < num_rcv_urbs; i++)
		usbatm_submit_urb(instance->urbs[i]);

	return 0;

 fail:
	instance->atm_dev = NULL;
	atm_dev_deregister(atm_dev); /* usbatm_atm_dev_close will eventually be called */
	return ret;
}


/**********
**  USB  **
**********/

static int usbatm_do_heavy_init(void *arg)
{
	struct usbatm_data *instance = arg;
	int ret;

	allow_signal(SIGTERM);
	complete(&instance->thread_started);

	ret = instance->driver->heavy_init(instance, instance->usb_intf);

	if (!ret)
		ret = usbatm_atm_init(instance);

	mutex_lock(&instance->serialize);
	instance->thread = NULL;
	mutex_unlock(&instance->serialize);

	complete_and_exit(&instance->thread_exited, ret);
}

static int usbatm_heavy_init(struct usbatm_data *instance)
{
	struct task_struct *t;

	t = kthread_create(usbatm_do_heavy_init, instance, "%s",
			instance->driver->driver_name);
	if (IS_ERR(t)) {
		usb_err(instance, "%s: failed to create kernel_thread (%ld)!\n",
				__func__, PTR_ERR(t));
		return PTR_ERR(t);
	}

	instance->thread = t;
	wake_up_process(t);
	wait_for_completion(&instance->thread_started);

	return 0;
}

static void usbatm_tasklet_schedule(unsigned long data)
{
	tasklet_schedule((struct tasklet_struct *) data);
}

static void usbatm_init_channel(struct usbatm_channel *channel)
{
	spin_lock_init(&channel->lock);
	INIT_LIST_HEAD(&channel->list);
	channel->delay.function = usbatm_tasklet_schedule;
	channel->delay.data = (unsigned long) &channel->tasklet;
	init_timer(&channel->delay);
}

int usbatm_usb_probe(struct usb_interface *intf, const struct usb_device_id *id,
		     struct usbatm_driver *driver)
{
	struct device *dev = &intf->dev;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usbatm_data *instance;
	char *buf;
	int error = -ENOMEM;
	int i, length;
	unsigned int maxpacket, num_packets;

	/* instance init */
	instance = kzalloc(sizeof(*instance) + sizeof(struct urb *) * (num_rcv_urbs + num_snd_urbs), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;

	/* public fields */

	instance->driver = driver;
	strlcpy(instance->driver_name, driver->driver_name,
		sizeof(instance->driver_name));

	instance->usb_dev = usb_dev;
	instance->usb_intf = intf;

	buf = instance->description;
	length = sizeof(instance->description);

	if ((i = usb_string(usb_dev, usb_dev->descriptor.iProduct, buf, length)) < 0)
		goto bind;

	buf += i;
	length -= i;

	i = scnprintf(buf, length, " (");
	buf += i;
	length -= i;

	if (length <= 0 || (i = usb_make_path(usb_dev, buf, length)) < 0)
		goto bind;

	buf += i;
	length -= i;

	snprintf(buf, length, ")");

 bind:
	if (driver->bind && (error = driver->bind(instance, intf, id)) < 0) {
			dev_err(dev, "%s: bind failed: %d!\n", __func__, error);
			goto fail_free;
	}

	/* private fields */

	kref_init(&instance->refcount);		/* dropped in usbatm_usb_disconnect */
	mutex_init(&instance->serialize);

	instance->thread = NULL;
	init_completion(&instance->thread_started);
	init_completion(&instance->thread_exited);

	INIT_LIST_HEAD(&instance->vcc_list);
	skb_queue_head_init(&instance->sndqueue);

	usbatm_init_channel(&instance->rx_channel);
	usbatm_init_channel(&instance->tx_channel);
	tasklet_init(&instance->rx_channel.tasklet, usbatm_rx_process, (unsigned long)instance);
	tasklet_init(&instance->tx_channel.tasklet, usbatm_tx_process, (unsigned long)instance);
	instance->rx_channel.stride = ATM_CELL_SIZE + driver->rx_padding;
	instance->tx_channel.stride = ATM_CELL_SIZE + driver->tx_padding;
	instance->rx_channel.usbatm = instance->tx_channel.usbatm = instance;

	if ((instance->flags & UDSL_USE_ISOC) && driver->isoc_in)
		instance->rx_channel.endpoint = usb_rcvisocpipe(usb_dev, driver->isoc_in);
	else
		instance->rx_channel.endpoint = usb_rcvbulkpipe(usb_dev, driver->bulk_in);

	instance->tx_channel.endpoint = usb_sndbulkpipe(usb_dev, driver->bulk_out);

	/* tx buffer size must be a positive multiple of the stride */
	instance->tx_channel.buf_size = max(instance->tx_channel.stride,
			snd_buf_bytes - (snd_buf_bytes % instance->tx_channel.stride));

	/* rx buffer size must be a positive multiple of the endpoint maxpacket */
	maxpacket = usb_maxpacket(usb_dev, instance->rx_channel.endpoint, 0);

	if ((maxpacket < 1) || (maxpacket > UDSL_MAX_BUF_SIZE)) {
		dev_err(dev, "%s: invalid endpoint %02x!\n", __func__,
				usb_pipeendpoint(instance->rx_channel.endpoint));
		error = -EINVAL;
		goto fail_unbind;
	}

	num_packets = max(1U, (rcv_buf_bytes + maxpacket / 2) / maxpacket); /* round */

	if (num_packets * maxpacket > UDSL_MAX_BUF_SIZE)
		num_packets--;

	instance->rx_channel.buf_size = num_packets * maxpacket;
	instance->rx_channel.packet_size = maxpacket;

	for (i = 0; i < 2; i++) {
		struct usbatm_channel *channel = i ?
			&instance->tx_channel : &instance->rx_channel;

		dev_dbg(dev, "%s: using %d byte buffer for %s channel 0x%p\n",
			__func__, channel->buf_size, i ? "tx" : "rx", channel);
	}

	/* initialize urbs */

	for (i = 0; i < num_rcv_urbs + num_snd_urbs; i++) {
		u8 *buffer;
		struct usbatm_channel *channel = i < num_rcv_urbs ?
			&instance->rx_channel : &instance->tx_channel;
		struct urb *urb;
		unsigned int iso_packets = usb_pipeisoc(channel->endpoint) ? channel->buf_size / channel->packet_size : 0;

		urb = usb_alloc_urb(iso_packets, GFP_KERNEL);
		if (!urb) {
			error = -ENOMEM;
			goto fail_unbind;
		}

		instance->urbs[i] = urb;

		/* zero the tx padding to avoid leaking information */
		buffer = kzalloc(channel->buf_size, GFP_KERNEL);
		if (!buffer) {
			error = -ENOMEM;
			goto fail_unbind;
		}

		usb_fill_bulk_urb(urb, instance->usb_dev, channel->endpoint,
				  buffer, channel->buf_size, usbatm_complete, channel);
		if (iso_packets) {
			int j;
			urb->interval = 1;
			urb->transfer_flags = URB_ISO_ASAP;
			urb->number_of_packets = iso_packets;
			for (j = 0; j < iso_packets; j++) {
				urb->iso_frame_desc[j].offset = channel->packet_size * j;
				urb->iso_frame_desc[j].length = channel->packet_size;
			}
		}

		/* put all tx URBs on the list of spares */
		if (i >= num_rcv_urbs)
			list_add_tail(&urb->urb_list, &channel->list);

		vdbg(&intf->dev, "%s: alloced buffer 0x%p buf size %u urb 0x%p",
		     __func__, urb->transfer_buffer, urb->transfer_buffer_length, urb);
	}

	instance->cached_vpi = ATM_VPI_UNSPEC;
	instance->cached_vci = ATM_VCI_UNSPEC;
	instance->cell_buf = kmalloc(instance->rx_channel.stride, GFP_KERNEL);

	if (!instance->cell_buf) {
		error = -ENOMEM;
		goto fail_unbind;
	}

	if (!(instance->flags & UDSL_SKIP_HEAVY_INIT) && driver->heavy_init) {
		error = usbatm_heavy_init(instance);
	} else {
		complete(&instance->thread_exited);	/* pretend that heavy_init was run */
		error = usbatm_atm_init(instance);
	}

	if (error < 0)
		goto fail_unbind;

	usb_get_dev(usb_dev);
	usb_set_intfdata(intf, instance);

	return 0;

 fail_unbind:
	if (instance->driver->unbind)
		instance->driver->unbind(instance, intf);
 fail_free:
	kfree(instance->cell_buf);

	for (i = 0; i < num_rcv_urbs + num_snd_urbs; i++) {
		if (instance->urbs[i])
			kfree(instance->urbs[i]->transfer_buffer);
		usb_free_urb(instance->urbs[i]);
	}

	kfree(instance);

	return error;
}
EXPORT_SYMBOL_GPL(usbatm_usb_probe);

void usbatm_usb_disconnect(struct usb_interface *intf)
{
	struct device *dev = &intf->dev;
	struct usbatm_data *instance = usb_get_intfdata(intf);
	struct usbatm_vcc_data *vcc_data;
	int i;

	if (!instance) {
		dev_dbg(dev, "%s: NULL instance!\n", __func__);
		return;
	}

	usb_set_intfdata(intf, NULL);

	mutex_lock(&instance->serialize);
	instance->disconnected = 1;
	if (instance->thread != NULL)
		send_sig(SIGTERM, instance->thread, 1);
	mutex_unlock(&instance->serialize);

	wait_for_completion(&instance->thread_exited);

	mutex_lock(&instance->serialize);
	list_for_each_entry(vcc_data, &instance->vcc_list, list)
		vcc_release_async(vcc_data->vcc, -EPIPE);
	mutex_unlock(&instance->serialize);

	tasklet_disable(&instance->rx_channel.tasklet);
	tasklet_disable(&instance->tx_channel.tasklet);

	for (i = 0; i < num_rcv_urbs + num_snd_urbs; i++)
		usb_kill_urb(instance->urbs[i]);

	del_timer_sync(&instance->rx_channel.delay);
	del_timer_sync(&instance->tx_channel.delay);

	/* turn usbatm_[rt]x_process into something close to a no-op */
	/* no need to take the spinlock */
	INIT_LIST_HEAD(&instance->rx_channel.list);
	INIT_LIST_HEAD(&instance->tx_channel.list);

	tasklet_enable(&instance->rx_channel.tasklet);
	tasklet_enable(&instance->tx_channel.tasklet);

	if (instance->atm_dev && instance->driver->atm_stop)
		instance->driver->atm_stop(instance, instance->atm_dev);

	if (instance->driver->unbind)
		instance->driver->unbind(instance, intf);

	instance->driver_data = NULL;

	for (i = 0; i < num_rcv_urbs + num_snd_urbs; i++) {
		kfree(instance->urbs[i]->transfer_buffer);
		usb_free_urb(instance->urbs[i]);
	}

	kfree(instance->cell_buf);

	/* ATM finalize */
	if (instance->atm_dev) {
		atm_dev_deregister(instance->atm_dev);
		instance->atm_dev = NULL;
	}

	usbatm_put_instance(instance);	/* taken in usbatm_usb_probe */
}
EXPORT_SYMBOL_GPL(usbatm_usb_disconnect);


/***********
**  init  **
***********/

static int __init usbatm_usb_init(void)
{
	if (sizeof(struct usbatm_control) > FIELD_SIZEOF(struct sk_buff, cb)) {
		printk(KERN_ERR "%s unusable with this kernel!\n", usbatm_driver_name);
		return -EIO;
	}

	if ((num_rcv_urbs > UDSL_MAX_RCV_URBS)
	    || (num_snd_urbs > UDSL_MAX_SND_URBS)
	    || (rcv_buf_bytes < 1)
	    || (rcv_buf_bytes > UDSL_MAX_BUF_SIZE)
	    || (snd_buf_bytes < 1)
	    || (snd_buf_bytes > UDSL_MAX_BUF_SIZE))
		return -EINVAL;

	return 0;
}
module_init(usbatm_usb_init);

static void __exit usbatm_usb_exit(void)
{
}
module_exit(usbatm_usb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/************
**  debug  **
************/

#ifdef VERBOSE_DEBUG
static int usbatm_print_packet(struct usbatm_data *instance,
			       const unsigned char *data, int len)
{
	unsigned char buffer[256];
	int i = 0, j = 0;

	for (i = 0; i < len;) {
		buffer[0] = '\0';
		sprintf(buffer, "%.3d :", i);
		for (j = 0; (j < 16) && (i < len); j++, i++)
			sprintf(buffer, "%s %2.2x", buffer, data[i]);
		dev_dbg(&instance->usb_intf->dev, "%s", buffer);
	}
	return i;
}
#endif
