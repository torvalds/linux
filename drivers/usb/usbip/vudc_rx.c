/*
 * Copyright (C) 2015 Karol Kosik <karo9@interia.eu>
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
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

#include <net/sock.h>
#include <linux/list.h>
#include <linux/kthread.h>

#include "usbip_common.h"
#include "vudc.h"

static int alloc_urb_from_cmd(struct urb **urbp,
			      struct usbip_header *pdu, u8 type)
{
	struct urb *urb;

	if (type == USB_ENDPOINT_XFER_ISOC)
		urb = usb_alloc_urb(pdu->u.cmd_submit.number_of_packets,
					  GFP_KERNEL);
	else
		urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!urb)
		goto err;

	usbip_pack_pdu(pdu, urb, USBIP_CMD_SUBMIT, 0);

	if (urb->transfer_buffer_length > 0) {
		urb->transfer_buffer = kzalloc(urb->transfer_buffer_length,
			GFP_KERNEL);
		if (!urb->transfer_buffer)
			goto free_urb;
	}

	urb->setup_packet = kmemdup(&pdu->u.cmd_submit.setup, 8,
			    GFP_KERNEL);
	if (!urb->setup_packet)
		goto free_buffer;

	/*
	 * FIXME - we only setup pipe enough for usbip functions
	 * to behave nicely
	 */
	urb->pipe |= pdu->base.direction == USBIP_DIR_IN ?
			USB_DIR_IN : USB_DIR_OUT;

	*urbp = urb;
	return 0;

free_buffer:
	kfree(urb->transfer_buffer);
	urb->transfer_buffer = NULL;
free_urb:
	usb_free_urb(urb);
err:
	return -ENOMEM;
}

static int v_recv_cmd_unlink(struct vudc *udc,
				struct usbip_header *pdu)
{
	unsigned long flags;
	struct urbp *urb_p;

	spin_lock_irqsave(&udc->lock, flags);
	list_for_each_entry(urb_p, &udc->urb_queue, urb_entry) {
		if (urb_p->seqnum != pdu->u.cmd_unlink.seqnum)
			continue;
		urb_p->urb->unlinked = -ECONNRESET;
		urb_p->seqnum = pdu->base.seqnum;
		v_kick_timer(udc, jiffies);
		spin_unlock_irqrestore(&udc->lock, flags);
		return 0;
	}
	/* Not found, completed / not queued */
	spin_lock(&udc->lock_tx);
	v_enqueue_ret_unlink(udc, pdu->base.seqnum, 0);
	wake_up(&udc->tx_waitq);
	spin_unlock(&udc->lock_tx);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int v_recv_cmd_submit(struct vudc *udc,
				 struct usbip_header *pdu)
{
	int ret = 0;
	struct urbp *urb_p;
	u8 address;
	unsigned long flags;

	urb_p = alloc_urbp();
	if (!urb_p) {
		usbip_event_add(&udc->ud, VUDC_EVENT_ERROR_MALLOC);
		return -ENOMEM;
	}

	/* base.ep is pipeendpoint(pipe) */
	address = pdu->base.ep;
	if (pdu->base.direction == USBIP_DIR_IN)
		address |= USB_DIR_IN;

	spin_lock_irq(&udc->lock);
	urb_p->ep = vudc_find_endpoint(udc, address);
	if (!urb_p->ep) {
		/* we don't know the type, there may be isoc data! */
		dev_err(&udc->pdev->dev, "request to nonexistent endpoint");
		spin_unlock_irq(&udc->lock);
		usbip_event_add(&udc->ud, VUDC_EVENT_ERROR_TCP);
		ret = -EPIPE;
		goto free_urbp;
	}
	urb_p->type = urb_p->ep->type;
	spin_unlock_irq(&udc->lock);

	urb_p->new = 1;
	urb_p->seqnum = pdu->base.seqnum;

	if (urb_p->ep->type == USB_ENDPOINT_XFER_ISOC) {
		/* validate packet size and number of packets */
		unsigned int maxp, packets, bytes;

		maxp = usb_endpoint_maxp(urb_p->ep->desc);
		maxp *= usb_endpoint_maxp_mult(urb_p->ep->desc);
		bytes = pdu->u.cmd_submit.transfer_buffer_length;
		packets = DIV_ROUND_UP(bytes, maxp);

		if (pdu->u.cmd_submit.number_of_packets < 0 ||
		    pdu->u.cmd_submit.number_of_packets > packets) {
			dev_err(&udc->gadget.dev,
				"CMD_SUBMIT: isoc invalid num packets %d\n",
				pdu->u.cmd_submit.number_of_packets);
			ret = -EMSGSIZE;
			goto free_urbp;
		}
	}

	ret = alloc_urb_from_cmd(&urb_p->urb, pdu, urb_p->ep->type);
	if (ret) {
		usbip_event_add(&udc->ud, VUDC_EVENT_ERROR_MALLOC);
		ret = -ENOMEM;
		goto free_urbp;
	}

	urb_p->urb->status = -EINPROGRESS;

	/* FIXME: more pipe setup to please usbip_common */
	urb_p->urb->pipe &= ~(3 << 30);
	switch (urb_p->ep->type) {
	case USB_ENDPOINT_XFER_BULK:
		urb_p->urb->pipe |= (PIPE_BULK << 30);
		break;
	case USB_ENDPOINT_XFER_INT:
		urb_p->urb->pipe |= (PIPE_INTERRUPT << 30);
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		urb_p->urb->pipe |= (PIPE_CONTROL << 30);
		break;
	case USB_ENDPOINT_XFER_ISOC:
		urb_p->urb->pipe |= (PIPE_ISOCHRONOUS << 30);
		break;
	}
	ret = usbip_recv_xbuff(&udc->ud, urb_p->urb);
	if (ret < 0)
		goto free_urbp;

	ret = usbip_recv_iso(&udc->ud, urb_p->urb);
	if (ret < 0)
		goto free_urbp;

	spin_lock_irqsave(&udc->lock, flags);
	v_kick_timer(udc, jiffies);
	list_add_tail(&urb_p->urb_entry, &udc->urb_queue);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;

free_urbp:
	free_urbp_and_urb(urb_p);
	return ret;
}

static int v_rx_pdu(struct usbip_device *ud)
{
	int ret;
	struct usbip_header pdu;
	struct vudc *udc = container_of(ud, struct vudc, ud);

	memset(&pdu, 0, sizeof(pdu));
	ret = usbip_recv(ud->tcp_socket, &pdu, sizeof(pdu));
	if (ret != sizeof(pdu)) {
		usbip_event_add(ud, VUDC_EVENT_ERROR_TCP);
		if (ret >= 0)
			return -EPIPE;
		return ret;
	}
	usbip_header_correct_endian(&pdu, 0);

	spin_lock_irq(&ud->lock);
	ret = (ud->status == SDEV_ST_USED);
	spin_unlock_irq(&ud->lock);
	if (!ret) {
		usbip_event_add(ud, VUDC_EVENT_ERROR_TCP);
		return -EBUSY;
	}

	switch (pdu.base.command) {
	case USBIP_CMD_UNLINK:
		ret = v_recv_cmd_unlink(udc, &pdu);
		break;
	case USBIP_CMD_SUBMIT:
		ret = v_recv_cmd_submit(udc, &pdu);
		break;
	default:
		ret = -EPIPE;
		pr_err("rx: unknown command");
		break;
	}
	return ret;
}

int v_rx_loop(void *data)
{
	struct usbip_device *ud = data;
	int ret = 0;

	while (!kthread_should_stop()) {
		if (usbip_event_happened(ud))
			break;
		ret = v_rx_pdu(ud);
		if (ret < 0) {
			pr_warn("v_rx exit with error %d", ret);
			break;
		}
	}
	return ret;
}
