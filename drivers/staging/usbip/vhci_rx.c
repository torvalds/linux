/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/kthread.h>
#include <linux/slab.h>

#include "usbip_common.h"
#include "vhci.h"

/* get URB from transmitted urb queue. caller must hold vdev->priv_lock */
struct urb *pickup_urb_and_free_priv(struct vhci_device *vdev, __u32 seqnum)
{
	struct vhci_priv *priv, *tmp;
	struct urb *urb = NULL;
	int status;

	list_for_each_entry_safe(priv, tmp, &vdev->priv_rx, list) {
		if (priv->seqnum == seqnum) {
			urb = priv->urb;
			status = urb->status;

			usbip_dbg_vhci_rx("find urb %p vurb %p seqnum %u\n",
					  urb, priv, seqnum);

			/* TODO: fix logic here to improve indent situtation */
			if (status != -EINPROGRESS) {
				if (status == -ENOENT ||
				    status == -ECONNRESET)
					dev_info(&urb->dev->dev,
						 "urb %p was unlinked "
						 "%ssynchronuously.\n", urb,
						 status == -ENOENT ? "" : "a");
				else
					dev_info(&urb->dev->dev,
						 "urb %p may be in a error, "
						 "status %d\n", urb, status);
			}

			list_del(&priv->list);
			kfree(priv);
			urb->hcpriv = NULL;

			break;
		}
	}

	return urb;
}

static void vhci_recv_ret_submit(struct vhci_device *vdev,
				 struct usbip_header *pdu)
{
	struct usbip_device *ud = &vdev->ud;
	struct urb *urb;
	unsigned long flags;

	spin_lock(&vdev->priv_lock);
	urb = pickup_urb_and_free_priv(vdev, pdu->base.seqnum);
	spin_unlock(&vdev->priv_lock);

	if (!urb) {
		pr_err("cannot find a urb of seqnum %u\n", pdu->base.seqnum);
		pr_info("max seqnum %d\n",
			atomic_read(&the_controller->seqnum));
		usbip_event_add(ud, VDEV_EVENT_ERROR_TCP);
		return;
	}

	/* unpack the pdu to a urb */
	usbip_pack_pdu(pdu, urb, USBIP_RET_SUBMIT, 0);

	/* recv transfer buffer */
	if (usbip_recv_xbuff(ud, urb) < 0)
		return;

	/* recv iso_packet_descriptor */
	if (usbip_recv_iso(ud, urb) < 0)
		return;

	/* restore the padding in iso packets */
	usbip_pad_iso(ud, urb);

	if (usbip_dbg_flag_vhci_rx)
		usbip_dump_urb(urb);

	usbip_dbg_vhci_rx("now giveback urb %p\n", urb);

	spin_lock_irqsave(&the_controller->lock, flags);
	usb_hcd_unlink_urb_from_ep(vhci_to_hcd(the_controller), urb);
	spin_unlock_irqrestore(&the_controller->lock, flags);

	usb_hcd_giveback_urb(vhci_to_hcd(the_controller), urb, urb->status);

	usbip_dbg_vhci_rx("Leave\n");

	return;
}

static struct vhci_unlink *dequeue_pending_unlink(struct vhci_device *vdev,
						  struct usbip_header *pdu)
{
	struct vhci_unlink *unlink, *tmp;

	spin_lock(&vdev->priv_lock);

	list_for_each_entry_safe(unlink, tmp, &vdev->unlink_rx, list) {
		pr_info("unlink->seqnum %lu\n", unlink->seqnum);
		if (unlink->seqnum == pdu->base.seqnum) {
			usbip_dbg_vhci_rx("found pending unlink, %lu\n",
					  unlink->seqnum);
			list_del(&unlink->list);

			spin_unlock(&vdev->priv_lock);
			return unlink;
		}
	}

	spin_unlock(&vdev->priv_lock);

	return NULL;
}

static void vhci_recv_ret_unlink(struct vhci_device *vdev,
				 struct usbip_header *pdu)
{
	struct vhci_unlink *unlink;
	struct urb *urb;
	unsigned long flags;

	usbip_dump_header(pdu);

	unlink = dequeue_pending_unlink(vdev, pdu);
	if (!unlink) {
		pr_info("cannot find the pending unlink %u\n",
			pdu->base.seqnum);
		return;
	}

	spin_lock(&vdev->priv_lock);
	urb = pickup_urb_and_free_priv(vdev, unlink->unlink_seqnum);
	spin_unlock(&vdev->priv_lock);

	if (!urb) {
		/*
		 * I get the result of a unlink request. But, it seems that I
		 * already received the result of its submit result and gave
		 * back the URB.
		 */
		pr_info("the urb (seqnum %d) was already given back\n",
			pdu->base.seqnum);
	} else {
		usbip_dbg_vhci_rx("now giveback urb %p\n", urb);

		/* If unlink is successful, status is -ECONNRESET */
		urb->status = pdu->u.ret_unlink.status;
		pr_info("urb->status %d\n", urb->status);

		spin_lock_irqsave(&the_controller->lock, flags);
		usb_hcd_unlink_urb_from_ep(vhci_to_hcd(the_controller), urb);
		spin_unlock_irqrestore(&the_controller->lock, flags);

		usb_hcd_giveback_urb(vhci_to_hcd(the_controller), urb,
				     urb->status);
	}

	kfree(unlink);
}

static int vhci_priv_tx_empty(struct vhci_device *vdev)
{
	int empty = 0;

	spin_lock(&vdev->priv_lock);
	empty = list_empty(&vdev->priv_rx);
	spin_unlock(&vdev->priv_lock);

	return empty;
}

/* recv a pdu */
static void vhci_rx_pdu(struct usbip_device *ud)
{
	int ret;
	struct usbip_header pdu;
	struct vhci_device *vdev = container_of(ud, struct vhci_device, ud);

	usbip_dbg_vhci_rx("Enter\n");

	memset(&pdu, 0, sizeof(pdu));

	/* 1. receive a pdu header */
	ret = usbip_recv(ud->tcp_socket, &pdu, sizeof(pdu));
	if (ret < 0) {
		if (ret == -ECONNRESET)
			pr_info("connection reset by peer\n");
		else if (ret == -EAGAIN) {
			/* ignore if connection was idle */
			if (vhci_priv_tx_empty(vdev))
				return;
			pr_info("connection timed out with pending urbs\n");
		} else if (ret != -ERESTARTSYS)
			pr_info("xmit failed %d\n", ret);

		usbip_event_add(ud, VDEV_EVENT_ERROR_TCP);
		return;
	}
	if (ret == 0) {
		pr_info("connection closed");
		usbip_event_add(ud, VDEV_EVENT_DOWN);
		return;
	}
	if (ret != sizeof(pdu)) {
		pr_err("received pdu size is %d, should be %d\n", ret,
		       (unsigned int)sizeof(pdu));
		usbip_event_add(ud, VDEV_EVENT_ERROR_TCP);
		return;
	}

	usbip_header_correct_endian(&pdu, 0);

	if (usbip_dbg_flag_vhci_rx)
		usbip_dump_header(&pdu);

	switch (pdu.base.command) {
	case USBIP_RET_SUBMIT:
		vhci_recv_ret_submit(vdev, &pdu);
		break;
	case USBIP_RET_UNLINK:
		vhci_recv_ret_unlink(vdev, &pdu);
		break;
	default:
		/* NOT REACHED */
		pr_err("unknown pdu %u\n", pdu.base.command);
		usbip_dump_header(&pdu);
		usbip_event_add(ud, VDEV_EVENT_ERROR_TCP);
		break;
	}
}

int vhci_rx_loop(void *data)
{
	struct usbip_device *ud = data;

	while (!kthread_should_stop()) {
		if (usbip_event_happened(ud))
			break;

		vhci_rx_pdu(ud);
	}

	return 0;
}
