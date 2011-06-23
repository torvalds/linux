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

static void setup_cmd_submit_pdu(struct usbip_header *pdup,  struct urb *urb)
{
	struct vhci_priv *priv = ((struct vhci_priv *)urb->hcpriv);
	struct vhci_device *vdev = priv->vdev;

	usbip_dbg_vhci_tx("URB, local devnum %u, remote devid %u\n",
			  usb_pipedevice(urb->pipe), vdev->devid);

	pdup->base.command   = USBIP_CMD_SUBMIT;
	pdup->base.seqnum    = priv->seqnum;
	pdup->base.devid     = vdev->devid;
	pdup->base.direction = usb_pipein(urb->pipe) ?
		USBIP_DIR_IN : USBIP_DIR_OUT;
	pdup->base.ep	     = usb_pipeendpoint(urb->pipe);

	usbip_pack_pdu(pdup, urb, USBIP_CMD_SUBMIT, 1);

	if (urb->setup_packet)
		memcpy(pdup->u.cmd_submit.setup, urb->setup_packet, 8);
}

static struct vhci_priv *dequeue_from_priv_tx(struct vhci_device *vdev)
{
	unsigned long flags;
	struct vhci_priv *priv, *tmp;

	spin_lock_irqsave(&vdev->priv_lock, flags);

	list_for_each_entry_safe(priv, tmp, &vdev->priv_tx, list) {
		list_move_tail(&priv->list, &vdev->priv_rx);
		spin_unlock_irqrestore(&vdev->priv_lock, flags);
		return priv;
	}

	spin_unlock_irqrestore(&vdev->priv_lock, flags);

	return NULL;
}

static int vhci_send_cmd_submit(struct vhci_device *vdev)
{
	struct vhci_priv *priv = NULL;

	struct msghdr msg;
	struct kvec iov[3];
	size_t txsize;

	size_t total_size = 0;

	while ((priv = dequeue_from_priv_tx(vdev)) != NULL) {
		int ret;
		struct urb *urb = priv->urb;
		struct usbip_header pdu_header;
		void *iso_buffer = NULL;

		txsize = 0;
		memset(&pdu_header, 0, sizeof(pdu_header));
		memset(&msg, 0, sizeof(msg));
		memset(&iov, 0, sizeof(iov));

		usbip_dbg_vhci_tx("setup txdata urb %p\n", urb);

		/* 1. setup usbip_header */
		setup_cmd_submit_pdu(&pdu_header, urb);
		usbip_header_correct_endian(&pdu_header, 1);

		iov[0].iov_base = &pdu_header;
		iov[0].iov_len  = sizeof(pdu_header);
		txsize += sizeof(pdu_header);

		/* 2. setup transfer buffer */
		if (!usb_pipein(urb->pipe) && urb->transfer_buffer_length > 0) {
			iov[1].iov_base = urb->transfer_buffer;
			iov[1].iov_len  = urb->transfer_buffer_length;
			txsize += urb->transfer_buffer_length;
		}

		/* 3. setup iso_packet_descriptor */
		if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
			ssize_t len = 0;

			iso_buffer = usbip_alloc_iso_desc_pdu(urb, &len);
			if (!iso_buffer) {
				usbip_event_add(&vdev->ud,
						SDEV_EVENT_ERROR_MALLOC);
				return -1;
			}

			iov[2].iov_base = iso_buffer;
			iov[2].iov_len  = len;
			txsize += len;
		}

		ret = kernel_sendmsg(vdev->ud.tcp_socket, &msg, iov, 3, txsize);
		if (ret != txsize) {
			pr_err("sendmsg failed!, ret=%d for %zd\n", ret,
			       txsize);
			kfree(iso_buffer);
			usbip_event_add(&vdev->ud, VDEV_EVENT_ERROR_TCP);
			return -1;
		}

		kfree(iso_buffer);
		usbip_dbg_vhci_tx("send txdata\n");

		total_size += txsize;
	}

	return total_size;
}

static struct vhci_unlink *dequeue_from_unlink_tx(struct vhci_device *vdev)
{
	unsigned long flags;
	struct vhci_unlink *unlink, *tmp;

	spin_lock_irqsave(&vdev->priv_lock, flags);

	list_for_each_entry_safe(unlink, tmp, &vdev->unlink_tx, list) {
		list_move_tail(&unlink->list, &vdev->unlink_rx);
		spin_unlock_irqrestore(&vdev->priv_lock, flags);
		return unlink;
	}

	spin_unlock_irqrestore(&vdev->priv_lock, flags);

	return NULL;
}

static int vhci_send_cmd_unlink(struct vhci_device *vdev)
{
	struct vhci_unlink *unlink = NULL;

	struct msghdr msg;
	struct kvec iov[3];
	size_t txsize;

	size_t total_size = 0;

	while ((unlink = dequeue_from_unlink_tx(vdev)) != NULL) {
		int ret;
		struct usbip_header pdu_header;

		txsize = 0;
		memset(&pdu_header, 0, sizeof(pdu_header));
		memset(&msg, 0, sizeof(msg));
		memset(&iov, 0, sizeof(iov));

		usbip_dbg_vhci_tx("setup cmd unlink, %lu\n", unlink->seqnum);

		/* 1. setup usbip_header */
		pdu_header.base.command = USBIP_CMD_UNLINK;
		pdu_header.base.seqnum  = unlink->seqnum;
		pdu_header.base.devid	= vdev->devid;
		pdu_header.base.ep	= 0;
		pdu_header.u.cmd_unlink.seqnum = unlink->unlink_seqnum;

		usbip_header_correct_endian(&pdu_header, 1);

		iov[0].iov_base = &pdu_header;
		iov[0].iov_len  = sizeof(pdu_header);
		txsize += sizeof(pdu_header);

		ret = kernel_sendmsg(vdev->ud.tcp_socket, &msg, iov, 1, txsize);
		if (ret != txsize) {
			pr_err("sendmsg failed!, ret=%d for %zd\n", ret,
			       txsize);
			usbip_event_add(&vdev->ud, VDEV_EVENT_ERROR_TCP);
			return -1;
		}

		usbip_dbg_vhci_tx("send txdata\n");

		total_size += txsize;
	}

	return total_size;
}

int vhci_tx_loop(void *data)
{
	struct usbip_device *ud = data;
	struct vhci_device *vdev = container_of(ud, struct vhci_device, ud);

	while (!kthread_should_stop()) {
		if (vhci_send_cmd_submit(vdev) < 0)
			break;

		if (vhci_send_cmd_unlink(vdev) < 0)
			break;

		wait_event_interruptible(vdev->waitq_tx,
					 (!list_empty(&vdev->priv_tx) ||
					  !list_empty(&vdev->unlink_tx) ||
					  kthread_should_stop()));

		usbip_dbg_vhci_tx("pending urbs ?, now wake up\n");
	}

	return 0;
}
