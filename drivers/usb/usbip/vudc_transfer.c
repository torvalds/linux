/*
 * Copyright (C) 2015 Karol Kosik <karo9@interia.eu>
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *
 * Based on dummy_hcd.c, which is:
 * Copyright (C) 2003 David Brownell
 * Copyright (C) 2003-2005 Alan Stern
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/usb.h>
#include <linux/timer.h>
#include <linux/usb/ch9.h>

#include "vudc.h"

#define DEV_REQUEST	(USB_TYPE_STANDARD | USB_RECIP_DEVICE)
#define DEV_INREQUEST	(DEV_REQUEST | USB_DIR_IN)
#define INTF_REQUEST	(USB_TYPE_STANDARD | USB_RECIP_INTERFACE)
#define INTF_INREQUEST	(INTF_REQUEST | USB_DIR_IN)
#define EP_REQUEST	(USB_TYPE_STANDARD | USB_RECIP_ENDPOINT)
#define EP_INREQUEST	(EP_REQUEST | USB_DIR_IN)

static int get_frame_limit(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return 8 /*bytes*/ * 12 /*packets*/;
	case USB_SPEED_FULL:
		return 64 /*bytes*/ * 19 /*packets*/;
	case USB_SPEED_HIGH:
		return 512 /*bytes*/ * 13 /*packets*/ * 8 /*uframes*/;
	case USB_SPEED_SUPER:
		/* Bus speed is 500000 bytes/ms, so use a little less */
		return 490000;
	default:
		/* error */
		return -1;
	}

}

/*
 * handle_control_request() - handles all control transfers
 * @udc: pointer to vudc
 * @urb: the urb request to handle
 * @setup: pointer to the setup data for a USB device control
 *	 request
 * @status: pointer to request handling status
 *
 * Return 0 - if the request was handled
 *	  1 - if the request wasn't handles
 *	  error code on error
 *
 * Adapted from drivers/usb/gadget/udc/dummy_hcd.c
 */
static int handle_control_request(struct vudc *udc, struct urb *urb,
				  struct usb_ctrlrequest *setup,
				  int *status)
{
	struct vep	*ep2;
	int		ret_val = 1;
	unsigned	w_index;
	unsigned	w_value;

	w_index = le16_to_cpu(setup->wIndex);
	w_value = le16_to_cpu(setup->wValue);
	switch (setup->bRequest) {
	case USB_REQ_SET_ADDRESS:
		if (setup->bRequestType != DEV_REQUEST)
			break;
		udc->address = w_value;
		ret_val = 0;
		*status = 0;
		break;
	case USB_REQ_SET_FEATURE:
		if (setup->bRequestType == DEV_REQUEST) {
			ret_val = 0;
			switch (w_value) {
			case USB_DEVICE_REMOTE_WAKEUP:
				break;
			case USB_DEVICE_B_HNP_ENABLE:
				udc->gadget.b_hnp_enable = 1;
				break;
			case USB_DEVICE_A_HNP_SUPPORT:
				udc->gadget.a_hnp_support = 1;
				break;
			case USB_DEVICE_A_ALT_HNP_SUPPORT:
				udc->gadget.a_alt_hnp_support = 1;
				break;
			default:
				ret_val = -EOPNOTSUPP;
			}
			if (ret_val == 0) {
				udc->devstatus |= (1 << w_value);
				*status = 0;
			}
		} else if (setup->bRequestType == EP_REQUEST) {
			/* endpoint halt */
			ep2 = vudc_find_endpoint(udc, w_index);
			if (!ep2 || ep2->ep.name == udc->ep[0].ep.name) {
				ret_val = -EOPNOTSUPP;
				break;
			}
			ep2->halted = 1;
			ret_val = 0;
			*status = 0;
		}
		break;
	case USB_REQ_CLEAR_FEATURE:
		if (setup->bRequestType == DEV_REQUEST) {
			ret_val = 0;
			switch (w_value) {
			case USB_DEVICE_REMOTE_WAKEUP:
				w_value = USB_DEVICE_REMOTE_WAKEUP;
				break;

			case USB_DEVICE_U1_ENABLE:
			case USB_DEVICE_U2_ENABLE:
			case USB_DEVICE_LTM_ENABLE:
				ret_val = -EOPNOTSUPP;
				break;
			default:
				ret_val = -EOPNOTSUPP;
				break;
			}
			if (ret_val == 0) {
				udc->devstatus &= ~(1 << w_value);
				*status = 0;
			}
		} else if (setup->bRequestType == EP_REQUEST) {
			/* endpoint halt */
			ep2 = vudc_find_endpoint(udc, w_index);
			if (!ep2) {
				ret_val = -EOPNOTSUPP;
				break;
			}
			if (!ep2->wedged)
				ep2->halted = 0;
			ret_val = 0;
			*status = 0;
		}
		break;
	case USB_REQ_GET_STATUS:
		if (setup->bRequestType == DEV_INREQUEST
				|| setup->bRequestType == INTF_INREQUEST
				|| setup->bRequestType == EP_INREQUEST) {
			char *buf;
			/*
			 * device: remote wakeup, selfpowered
			 * interface: nothing
			 * endpoint: halt
			 */
			buf = (char *)urb->transfer_buffer;
			if (urb->transfer_buffer_length > 0) {
				if (setup->bRequestType == EP_INREQUEST) {
					ep2 = vudc_find_endpoint(udc, w_index);
					if (!ep2) {
						ret_val = -EOPNOTSUPP;
						break;
					}
					buf[0] = ep2->halted;
				} else if (setup->bRequestType ==
					   DEV_INREQUEST) {
					buf[0] = (u8)udc->devstatus;
				} else
					buf[0] = 0;
			}
			if (urb->transfer_buffer_length > 1)
				buf[1] = 0;
			urb->actual_length = min_t(u32, 2,
				urb->transfer_buffer_length);
			ret_val = 0;
			*status = 0;
		}
		break;
	}
	return ret_val;
}

/* Adapted from dummy_hcd.c ; caller must hold lock */
static int transfer(struct vudc *udc,
		struct urb *urb, struct vep *ep, int limit)
{
	struct vrequest	*req;
	int sent = 0;
top:
	/* if there's no request queued, the device is NAKing; return */
	list_for_each_entry(req, &ep->req_queue, req_entry) {
		unsigned	host_len, dev_len, len;
		void		*ubuf_pos, *rbuf_pos;
		int		is_short, to_host;
		int		rescan = 0;

		/*
		 * 1..N packets of ep->ep.maxpacket each ... the last one
		 * may be short (including zero length).
		 *
		 * writer can send a zlp explicitly (length 0) or implicitly
		 * (length mod maxpacket zero, and 'zero' flag); they always
		 * terminate reads.
		 */
		host_len = urb->transfer_buffer_length - urb->actual_length;
		dev_len = req->req.length - req->req.actual;
		len = min(host_len, dev_len);

		to_host = usb_pipein(urb->pipe);
		if (unlikely(len == 0))
			is_short = 1;
		else {
			/* send multiple of maxpacket first, then remainder */
			if (len >= ep->ep.maxpacket) {
				is_short = 0;
				if (len % ep->ep.maxpacket > 0)
					rescan = 1;
				len -= len % ep->ep.maxpacket;
			} else {
				is_short = 1;
			}

			ubuf_pos = urb->transfer_buffer + urb->actual_length;
			rbuf_pos = req->req.buf + req->req.actual;

			if (urb->pipe & USB_DIR_IN)
				memcpy(ubuf_pos, rbuf_pos, len);
			else
				memcpy(rbuf_pos, ubuf_pos, len);

			urb->actual_length += len;
			req->req.actual += len;
			sent += len;
		}

		/*
		 * short packets terminate, maybe with overflow/underflow.
		 * it's only really an error to write too much.
		 *
		 * partially filling a buffer optionally blocks queue advances
		 * (so completion handlers can clean up the queue) but we don't
		 * need to emulate such data-in-flight.
		 */
		if (is_short) {
			if (host_len == dev_len) {
				req->req.status = 0;
				urb->status = 0;
			} else if (to_host) {
				req->req.status = 0;
				if (dev_len > host_len)
					urb->status = -EOVERFLOW;
				else
					urb->status = 0;
			} else {
				urb->status = 0;
				if (host_len > dev_len)
					req->req.status = -EOVERFLOW;
				else
					req->req.status = 0;
			}

		/* many requests terminate without a short packet */
		/* also check if we need to send zlp */
		} else {
			if (req->req.length == req->req.actual) {
				if (req->req.zero && to_host)
					rescan = 1;
				else
					req->req.status = 0;
			}
			if (urb->transfer_buffer_length == urb->actual_length) {
				if (urb->transfer_flags & URB_ZERO_PACKET &&
				    !to_host)
					rescan = 1;
				else
					urb->status = 0;
			}
		}

		/* device side completion --> continuable */
		if (req->req.status != -EINPROGRESS) {

			list_del_init(&req->req_entry);
			spin_unlock(&udc->lock);
			usb_gadget_giveback_request(&ep->ep, &req->req);
			spin_lock(&udc->lock);

			/* requests might have been unlinked... */
			rescan = 1;
		}

		/* host side completion --> terminate */
		if (urb->status != -EINPROGRESS)
			break;

		/* rescan to continue with any other queued i/o */
		if (rescan)
			goto top;
	}
	return sent;
}

static void v_timer(unsigned long _vudc)
{
	struct vudc *udc = (struct vudc *) _vudc;
	struct transfer_timer *timer = &udc->tr_timer;
	struct urbp *urb_p, *tmp;
	unsigned long flags;
	struct usb_ep *_ep;
	struct vep *ep;
	int ret = 0;
	int total, limit;

	spin_lock_irqsave(&udc->lock, flags);

	total = get_frame_limit(udc->gadget.speed);
	if (total < 0) {	/* unknown speed, or not set yet */
		timer->state = VUDC_TR_IDLE;
		spin_unlock_irqrestore(&udc->lock, flags);
		return;
	}
	/* is it next frame now? */
	if (time_after(jiffies, timer->frame_start + msecs_to_jiffies(1))) {
		timer->frame_limit = total;
		/* FIXME: how to make it accurate? */
		timer->frame_start = jiffies;
	} else {
		total = timer->frame_limit;
	}

	list_for_each_entry(_ep, &udc->gadget.ep_list, ep_list) {
		ep = to_vep(_ep);
		ep->already_seen = 0;
	}

restart:
	list_for_each_entry_safe(urb_p, tmp, &udc->urb_queue, urb_entry) {
		struct urb *urb = urb_p->urb;

		ep = urb_p->ep;
		if (urb->unlinked)
			goto return_urb;
		if (timer->state != VUDC_TR_RUNNING)
			continue;

		if (!ep) {
			urb->status = -EPROTO;
			goto return_urb;
		}

		/* Used up bandwidth? */
		if (total <= 0 && ep->type == USB_ENDPOINT_XFER_BULK)
			continue;

		if (ep->already_seen)
			continue;
		ep->already_seen = 1;
		if (ep == &udc->ep[0] && urb_p->new) {
			ep->setup_stage = 1;
			urb_p->new = 0;
		}
		if (ep->halted && !ep->setup_stage) {
			urb->status = -EPIPE;
			goto return_urb;
		}

		if (ep == &udc->ep[0] && ep->setup_stage) {
			/* TODO - flush any stale requests */
			ep->setup_stage = 0;
			ep->halted = 0;

			ret = handle_control_request(udc, urb,
				(struct usb_ctrlrequest *) urb->setup_packet,
				(&urb->status));
			if (ret > 0) {
				spin_unlock(&udc->lock);
				ret = udc->driver->setup(&udc->gadget,
					(struct usb_ctrlrequest *)
					urb->setup_packet);
				spin_lock(&udc->lock);
			}
			if (ret >= 0) {
				/* no delays (max 64kb data stage) */
				limit = 64 * 1024;
				goto treat_control_like_bulk;
			} else {
				urb->status = -EPIPE;
				urb->actual_length = 0;
				goto return_urb;
			}
		}

		limit = total;
		switch (ep->type) {
		case USB_ENDPOINT_XFER_ISOC:
			/* TODO: support */
			urb->status = -EXDEV;
			break;

		case USB_ENDPOINT_XFER_INT:
			/*
			 * TODO: figure out bandwidth guarantees
			 * for now, give unlimited bandwidth
			 */
			limit += urb->transfer_buffer_length;
			/* fallthrough */
		default:
treat_control_like_bulk:
			total -= transfer(udc, urb, ep, limit);
		}
		if (urb->status == -EINPROGRESS)
			continue;

return_urb:
		if (ep)
			ep->already_seen = ep->setup_stage = 0;

		spin_lock(&udc->lock_tx);
		list_del(&urb_p->urb_entry);
		if (!urb->unlinked) {
			v_enqueue_ret_submit(udc, urb_p);
		} else {
			v_enqueue_ret_unlink(udc, urb_p->seqnum,
					     urb->unlinked);
			free_urbp_and_urb(urb_p);
		}
		wake_up(&udc->tx_waitq);
		spin_unlock(&udc->lock_tx);

		goto restart;
	}

	/* TODO - also wait on empty usb_request queues? */
	if (list_empty(&udc->urb_queue))
		timer->state = VUDC_TR_IDLE;
	else
		mod_timer(&timer->timer,
			  timer->frame_start + msecs_to_jiffies(1));

	spin_unlock_irqrestore(&udc->lock, flags);
}

/* All timer functions are run with udc->lock held */

void v_init_timer(struct vudc *udc)
{
	struct transfer_timer *t = &udc->tr_timer;

	setup_timer(&t->timer, v_timer, (unsigned long) udc);
	t->state = VUDC_TR_STOPPED;
}

void v_start_timer(struct vudc *udc)
{
	struct transfer_timer *t = &udc->tr_timer;

	dev_dbg(&udc->pdev->dev, "timer start");
	switch (t->state) {
	case VUDC_TR_RUNNING:
		return;
	case VUDC_TR_IDLE:
		return v_kick_timer(udc, jiffies);
	case VUDC_TR_STOPPED:
		t->state = VUDC_TR_IDLE;
		t->frame_start = jiffies;
		t->frame_limit = get_frame_limit(udc->gadget.speed);
		return v_kick_timer(udc, jiffies);
	}
}

void v_kick_timer(struct vudc *udc, unsigned long time)
{
	struct transfer_timer *t = &udc->tr_timer;

	dev_dbg(&udc->pdev->dev, "timer kick");
	switch (t->state) {
	case VUDC_TR_RUNNING:
		return;
	case VUDC_TR_IDLE:
		t->state = VUDC_TR_RUNNING;
		/* fallthrough */
	case VUDC_TR_STOPPED:
		/* we may want to kick timer to unqueue urbs */
		mod_timer(&t->timer, time);
	}
}

void v_stop_timer(struct vudc *udc)
{
	struct transfer_timer *t = &udc->tr_timer;

	/* timer itself will take care of stopping */
	dev_dbg(&udc->pdev->dev, "timer stop");
	t->state = VUDC_TR_STOPPED;
}
