// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
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
		if (priv->seqnum != seqnum)
			continue;

		urb = priv->urb;
		status = urb->status;

		usbip_dbg_vhci_rx("find urb %p vurb %p seqnum %u\n",
				urb, priv, seqnum);

		switch (status) {
		case -ENOENT:
			/* fall through */
		case -ECONNRESET:
			dev_info(&urb->dev->dev,
				 "urb %p was unlinked %ssynchronuously.\n", urb,
				 status == -ENOENT ? "" : "a");
			break;
		case -EINPROGRESS:
			/* no info output */
			break;
		default:
			dev_info(&urb->dev->dev,
				 "urb %p may be in a error, status %d\n", urb,
				 status);
		}

		list_del(&priv->list);
		kfree(priv);
		urb->hcpriv = NULL;

		break;
	}

	return urb;
}

static void vhci_recv_ret_submit(struct vhci_device *vdev,
				 struct usbip_header *pdu)
{
	struct vhci_hcd *vhci_hcd = vdev_to_vhci_hcd(vdev);
	struct vhci *vhci = vhci_hcd->vhci;
	struct usbip_device *ud = &vdev->ud;
	struct urb *urb;
	unsigned long flags;

	spin_lock_irqsave(&vdev->priv_lock, flags);
	urb = pickup_urb_and_free_priv(vdev, pdu->base.seqnum);
	spin_unlock_irqrestore(&vdev->priv_lock, flags);

	if (!urb) {
		pr_err("cannot find a urb of seqnum %u\n", pdu->base.seqnum);
		pr_info("max seqnum %d\n",
			atomic_read(&vhci_hcd->seqnum));
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

	spin_lock_irqsave(&vhci->lock, flags);
	usb_hcd_unlink_urb_from_ep(vhci_hcd_to_hcd(vhci_hcd), urb);
	spin_unlock_irqrestore(&vhci->lock, flags);

	usb_hcd_giveback_urb(vhci_hcd_to_hcd(vhci_hcd), urb, urb->status);

	usbip_dbg_vhci_rx("Leave\n");
}

static struct vhci_unlink *dequeue_pending_unlink(struct vhci_device *vdev,
						  struct usbip_header *pdu)
{
	struct vhci_unlink *unlink, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&vdev->priv_lock, flags);

	list_for_each_entry_safe(unlink, tmp, &vdev->unlink_rx, list) {
		pr_info("unlink->seqnum %lu\n", unlink->seqnum);
		if (unlink->seqnum == pdu->base.seqnum) {
			usbip_dbg_vhci_rx("found pending unlink, %lu\n",
					  unlink->seqnum);
			list_del(&unlink->list);

			spin_unlock_irqrestore(&vdev->priv_lock, flags);
			return unlink;
		}
	}

	spin_unlock_irqrestore(&vdev->priv_lock, flags);

	return NULL;
}

static void vhci_recv_ret_unlink(struct vhci_device *vdev,
				 struct usbip_header *pdu)
{
	struct vhci_hcd *vhci_hcd = vdev_to_vhci_hcd(vdev);
	struct vhci *vhci = vhci_hcd->vhci;
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

	spin_lock_irqsave(&vdev->priv_lock, flags);
	urb = pickup_urb_and_free_priv(vdev, unlink->unlink_seqnum);
	spin_unlock_irqrestore(&vdev->priv_lock, flags);

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

		spin_lock_irqsave(&vhci->lock, flags);
		usb_hcd_unlink_urb_from_ep(vhci_hcd_to_hcd(vhci_hcd), urb);
		spin_unlock_irqrestore(&vhci->lock, flags);

		usb_hcd_giveback_urb(vhci_hcd_to_hcd(vhci_hcd), urb, urb->status);
	}

	kfree(unlink);
}

static int vhci_priv_tx_empty(struct vhci_device *vdev)
{
	int empty = 0;
	unsigned long flags;

	spin_lock_irqsave(&vdev->priv_lock, flags);
	empty = list_empty(&vdev->priv_rx);
	spin_unlock_irqrestore(&vdev->priv_lock, flags);

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

	/* receive a pdu header */
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
