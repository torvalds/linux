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

#include <linux/init.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "usbip_common.h"
#include "vhci.h"

#define DRIVER_AUTHOR "Takahiro Hirofuchi"
#define DRIVER_DESC "USB/IP 'Virtual' Host Controller (VHCI) Driver"

/*
 * TODO
 *	- update root hub emulation
 *	- move the emulation code to userland ?
 *		porting to other operating systems
 *		minimize kernel code
 *	- add suspend/resume code
 *	- clean up everything
 */

/* See usb gadget dummy hcd */

static int vhci_hub_status(struct usb_hcd *hcd, char *buff);
static int vhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			    u16 wIndex, char *buff, u16 wLength);
static int vhci_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
			    gfp_t mem_flags);
static int vhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status);
static int vhci_start(struct usb_hcd *vhci_hcd);
static void vhci_stop(struct usb_hcd *hcd);
static int vhci_get_frame_number(struct usb_hcd *hcd);

static const char driver_name[] = "vhci_hcd";
static const char driver_desc[] = "USB/IP Virtual Host Controller";

struct vhci_hcd *the_controller;

static const char * const bit_desc[] = {
	"CONNECTION",		/*0*/
	"ENABLE",		/*1*/
	"SUSPEND",		/*2*/
	"OVER_CURRENT",		/*3*/
	"RESET",		/*4*/
	"R5",			/*5*/
	"R6",			/*6*/
	"R7",			/*7*/
	"POWER",		/*8*/
	"LOWSPEED",		/*9*/
	"HIGHSPEED",		/*10*/
	"PORT_TEST",		/*11*/
	"INDICATOR",		/*12*/
	"R13",			/*13*/
	"R14",			/*14*/
	"R15",			/*15*/
	"C_CONNECTION",		/*16*/
	"C_ENABLE",		/*17*/
	"C_SUSPEND",		/*18*/
	"C_OVER_CURRENT",	/*19*/
	"C_RESET",		/*20*/
	"R21",			/*21*/
	"R22",			/*22*/
	"R23",			/*23*/
	"R24",			/*24*/
	"R25",			/*25*/
	"R26",			/*26*/
	"R27",			/*27*/
	"R28",			/*28*/
	"R29",			/*29*/
	"R30",			/*30*/
	"R31",			/*31*/
};

static void dump_port_status_diff(u32 prev_status, u32 new_status)
{
	int i = 0;
	u32 bit = 1;

	pr_debug("status prev -> new: %08x -> %08x\n", prev_status, new_status);
	while (bit) {
		u32 prev = prev_status & bit;
		u32 new = new_status & bit;
		char change;

		if (!prev && new)
			change = '+';
		else if (prev && !new)
			change = '-';
		else
			change = ' ';

		if (prev || new)
			pr_debug(" %c%s\n", change, bit_desc[i]);
		bit <<= 1;
		i++;
	}
	pr_debug("\n");
}

void rh_port_connect(int rhport, enum usb_device_speed speed)
{
	usbip_dbg_vhci_rh("rh_port_connect %d\n", rhport);

	spin_lock(&the_controller->lock);

	the_controller->port_status[rhport] |= USB_PORT_STAT_CONNECTION
		| (1 << USB_PORT_FEAT_C_CONNECTION);

	switch (speed) {
	case USB_SPEED_HIGH:
		the_controller->port_status[rhport] |= USB_PORT_STAT_HIGH_SPEED;
		break;
	case USB_SPEED_LOW:
		the_controller->port_status[rhport] |= USB_PORT_STAT_LOW_SPEED;
		break;
	default:
		break;
	}

	spin_unlock(&the_controller->lock);

	usb_hcd_poll_rh_status(vhci_to_hcd(the_controller));
}

static void rh_port_disconnect(int rhport)
{
	usbip_dbg_vhci_rh("rh_port_disconnect %d\n", rhport);

	spin_lock(&the_controller->lock);

	the_controller->port_status[rhport] &= ~USB_PORT_STAT_CONNECTION;
	the_controller->port_status[rhport] |=
					(1 << USB_PORT_FEAT_C_CONNECTION);

	spin_unlock(&the_controller->lock);
	usb_hcd_poll_rh_status(vhci_to_hcd(the_controller));
}

#define PORT_C_MASK				\
	((USB_PORT_STAT_C_CONNECTION		\
	  | USB_PORT_STAT_C_ENABLE		\
	  | USB_PORT_STAT_C_SUSPEND		\
	  | USB_PORT_STAT_C_OVERCURRENT		\
	  | USB_PORT_STAT_C_RESET) << 16)

/*
 * Returns 0 if the status hasn't changed, or the number of bytes in buf.
 * Ports are 0-indexed from the HCD point of view,
 * and 1-indexed from the USB core pointer of view.
 *
 * @buf: a bitmap to show which port status has been changed.
 *  bit  0: reserved
 *  bit  1: the status of port 0 has been changed.
 *  bit  2: the status of port 1 has been changed.
 *  ...
 */
static int vhci_hub_status(struct usb_hcd *hcd, char *buf)
{
	struct vhci_hcd	*vhci;
	int		retval;
	int		rhport;
	int		changed = 0;

	retval = DIV_ROUND_UP(VHCI_NPORTS + 1, 8);
	memset(buf, 0, retval);

	vhci = hcd_to_vhci(hcd);

	spin_lock(&vhci->lock);
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		usbip_dbg_vhci_rh("hw accessible flag not on?\n");
		goto done;
	}

	/* check pseudo status register for each port */
	for (rhport = 0; rhport < VHCI_NPORTS; rhport++) {
		if ((vhci->port_status[rhport] & PORT_C_MASK)) {
			/* The status of a port has been changed, */
			usbip_dbg_vhci_rh("port %d status changed\n", rhport);

			buf[(rhport + 1) / 8] |= 1 << (rhport + 1) % 8;
			changed = 1;
		}
	}

	pr_info("changed %d\n", changed);

	if ((hcd->state == HC_STATE_SUSPENDED) && (changed == 1))
		usb_hcd_resume_root_hub(hcd);

done:
	spin_unlock(&vhci->lock);
	return changed ? retval : 0;
}

static inline void hub_descriptor(struct usb_hub_descriptor *desc)
{
	memset(desc, 0, sizeof(*desc));
	desc->bDescriptorType = 0x29;
	desc->bDescLength = 9;
	desc->wHubCharacteristics = (__constant_cpu_to_le16(0x0001));
	desc->bNbrPorts = VHCI_NPORTS;
	desc->u.hs.DeviceRemovable[0] = 0xff;
	desc->u.hs.DeviceRemovable[1] = 0xff;
}

static int vhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			    u16 wIndex, char *buf, u16 wLength)
{
	struct vhci_hcd	*dum;
	int             retval = 0;
	int		rhport;

	u32 prev_port_status[VHCI_NPORTS];

	if (!HCD_HW_ACCESSIBLE(hcd))
		return -ETIMEDOUT;

	/*
	 * NOTE:
	 * wIndex shows the port number and begins from 1.
	 */
	usbip_dbg_vhci_rh("typeReq %x wValue %x wIndex %x\n", typeReq, wValue,
			  wIndex);
	if (wIndex > VHCI_NPORTS)
		pr_err("invalid port number %d\n", wIndex);
	rhport = ((__u8)(wIndex & 0x00ff)) - 1;

	dum = hcd_to_vhci(hcd);

	spin_lock(&dum->lock);

	/* store old status and compare now and old later */
	if (usbip_dbg_flag_vhci_rh) {
		memcpy(prev_port_status, dum->port_status,
			sizeof(prev_port_status));
	}

	switch (typeReq) {
	case ClearHubFeature:
		usbip_dbg_vhci_rh(" ClearHubFeature\n");
		break;
	case ClearPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (dum->port_status[rhport] & USB_PORT_STAT_SUSPEND) {
				/* 20msec signaling */
				dum->resuming = 1;
				dum->re_timeout =
					jiffies + msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_POWER:
			usbip_dbg_vhci_rh(" ClearPortFeature: "
					  "USB_PORT_FEAT_POWER\n");
			dum->port_status[rhport] = 0;
			dum->resuming = 0;
			break;
		case USB_PORT_FEAT_C_RESET:
			usbip_dbg_vhci_rh(" ClearPortFeature: "
					  "USB_PORT_FEAT_C_RESET\n");
			switch (dum->vdev[rhport].speed) {
			case USB_SPEED_HIGH:
				dum->port_status[rhport] |=
					USB_PORT_STAT_HIGH_SPEED;
				break;
			case USB_SPEED_LOW:
				dum->port_status[rhport] |=
					USB_PORT_STAT_LOW_SPEED;
				break;
			default:
				break;
			}
		default:
			usbip_dbg_vhci_rh(" ClearPortFeature: default %x\n",
					  wValue);
			dum->port_status[rhport] &= ~(1 << wValue);
			break;
		}
		break;
	case GetHubDescriptor:
		usbip_dbg_vhci_rh(" GetHubDescriptor\n");
		hub_descriptor((struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		usbip_dbg_vhci_rh(" GetHubStatus\n");
		*(__le32 *) buf = __constant_cpu_to_le32(0);
		break;
	case GetPortStatus:
		usbip_dbg_vhci_rh(" GetPortStatus port %x\n", wIndex);
		if (wIndex > VHCI_NPORTS || wIndex < 1) {
			pr_err("invalid port number %d\n", wIndex);
			retval = -EPIPE;
		}

		/* we do not care about resume. */

		/* whoever resets or resumes must GetPortStatus to
		 * complete it!!
		 */
		if (dum->resuming && time_after(jiffies, dum->re_timeout)) {
			dum->port_status[rhport] |=
				(1 << USB_PORT_FEAT_C_SUSPEND);
			dum->port_status[rhport] &=
				~(1 << USB_PORT_FEAT_SUSPEND);
			dum->resuming = 0;
			dum->re_timeout = 0;
		}

		if ((dum->port_status[rhport] & (1 << USB_PORT_FEAT_RESET)) !=
		    0 && time_after(jiffies, dum->re_timeout)) {
			dum->port_status[rhport] |=
				(1 << USB_PORT_FEAT_C_RESET);
			dum->port_status[rhport] &=
				~(1 << USB_PORT_FEAT_RESET);
			dum->re_timeout = 0;

			if (dum->vdev[rhport].ud.status ==
			    VDEV_ST_NOTASSIGNED) {
				usbip_dbg_vhci_rh(" enable rhport %d "
						  "(status %u)\n",
						  rhport,
						  dum->vdev[rhport].ud.status);
				dum->port_status[rhport] |=
					USB_PORT_STAT_ENABLE;
			}
		}
		((__le16 *) buf)[0] = cpu_to_le16(dum->port_status[rhport]);
		((__le16 *) buf)[1] = cpu_to_le16(dum->port_status[rhport] >> 16);

		usbip_dbg_vhci_rh(" GetPortStatus bye %x %x\n", ((u16 *)buf)[0],
				  ((u16 *)buf)[1]);
		break;
	case SetHubFeature:
		usbip_dbg_vhci_rh(" SetHubFeature\n");
		retval = -EPIPE;
		break;
	case SetPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			usbip_dbg_vhci_rh(" SetPortFeature: "
					  "USB_PORT_FEAT_SUSPEND\n");
			break;
		case USB_PORT_FEAT_RESET:
			usbip_dbg_vhci_rh(" SetPortFeature: "
					  "USB_PORT_FEAT_RESET\n");
			/* if it's already running, disconnect first */
			if (dum->port_status[rhport] & USB_PORT_STAT_ENABLE) {
				dum->port_status[rhport] &=
					~(USB_PORT_STAT_ENABLE |
					  USB_PORT_STAT_LOW_SPEED |
					  USB_PORT_STAT_HIGH_SPEED);
				/* FIXME test that code path! */
			}
			/* 50msec reset signaling */
			dum->re_timeout = jiffies + msecs_to_jiffies(50);

			/* FALLTHROUGH */
		default:
			usbip_dbg_vhci_rh(" SetPortFeature: default %d\n",
					  wValue);
			dum->port_status[rhport] |= (1 << wValue);
			break;
		}
		break;

	default:
		pr_err("default: no such request\n");

		/* "protocol stall" on error */
		retval = -EPIPE;
	}

	if (usbip_dbg_flag_vhci_rh) {
		pr_debug("port %d\n", rhport);
		/* Only dump valid port status */
		if (rhport >= 0) {
			dump_port_status_diff(prev_port_status[rhport],
					      dum->port_status[rhport]);
		}
	}
	usbip_dbg_vhci_rh(" bye\n");

	spin_unlock(&dum->lock);

	return retval;
}

static struct vhci_device *get_vdev(struct usb_device *udev)
{
	int i;

	if (!udev)
		return NULL;

	for (i = 0; i < VHCI_NPORTS; i++)
		if (the_controller->vdev[i].udev == udev)
			return port_to_vdev(i);

	return NULL;
}

static void vhci_tx_urb(struct urb *urb)
{
	struct vhci_device *vdev = get_vdev(urb->dev);
	struct vhci_priv *priv;

	if (!vdev) {
		pr_err("could not get virtual device");
		return;
	}

	priv = kzalloc(sizeof(struct vhci_priv), GFP_ATOMIC);
	if (!priv) {
		usbip_event_add(&vdev->ud, VDEV_EVENT_ERROR_MALLOC);
		return;
	}

	spin_lock(&vdev->priv_lock);

	priv->seqnum = atomic_inc_return(&the_controller->seqnum);
	if (priv->seqnum == 0xffff)
		dev_info(&urb->dev->dev, "seqnum max\n");

	priv->vdev = vdev;
	priv->urb = urb;

	urb->hcpriv = (void *) priv;

	list_add_tail(&priv->list, &vdev->priv_tx);

	wake_up(&vdev->waitq_tx);
	spin_unlock(&vdev->priv_lock);
}

static int vhci_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
			    gfp_t mem_flags)
{
	struct device *dev = &urb->dev->dev;
	int ret = 0;
	struct vhci_device *vdev;

	usbip_dbg_vhci_hc("enter, usb_hcd %p urb %p mem_flags %d\n",
			  hcd, urb, mem_flags);

	/* patch to usb_sg_init() is in 2.5.60 */
	BUG_ON(!urb->transfer_buffer && urb->transfer_buffer_length);

	spin_lock(&the_controller->lock);

	if (urb->status != -EINPROGRESS) {
		dev_err(dev, "URB already unlinked!, status %d\n", urb->status);
		spin_unlock(&the_controller->lock);
		return urb->status;
	}

	vdev = port_to_vdev(urb->dev->portnum-1);

	/* refuse enqueue for dead connection */
	spin_lock(&vdev->ud.lock);
	if (vdev->ud.status == VDEV_ST_NULL ||
	    vdev->ud.status == VDEV_ST_ERROR) {
		dev_err(dev, "enqueue for inactive port %d\n", vdev->rhport);
		spin_unlock(&vdev->ud.lock);
		spin_unlock(&the_controller->lock);
		return -ENODEV;
	}
	spin_unlock(&vdev->ud.lock);

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret)
		goto no_need_unlink;

	/*
	 * The enumeration process is as follows;
	 *
	 *  1. Get_Descriptor request to DevAddrs(0) EndPoint(0)
	 *     to get max packet length of default pipe
	 *
	 *  2. Set_Address request to DevAddr(0) EndPoint(0)
	 *
	 */
	if (usb_pipedevice(urb->pipe) == 0) {
		__u8 type = usb_pipetype(urb->pipe);
		struct usb_ctrlrequest *ctrlreq =
			(struct usb_ctrlrequest *) urb->setup_packet;

		if (type != PIPE_CONTROL || !ctrlreq) {
			dev_err(dev, "invalid request to devnum 0\n");
			ret = -EINVAL;
			goto no_need_xmit;
		}

		switch (ctrlreq->bRequest) {
		case USB_REQ_SET_ADDRESS:
			/* set_address may come when a device is reset */
			dev_info(dev, "SetAddress Request (%d) to port %d\n",
				 ctrlreq->wValue, vdev->rhport);

			if (vdev->udev)
				usb_put_dev(vdev->udev);
			vdev->udev = usb_get_dev(urb->dev);

			spin_lock(&vdev->ud.lock);
			vdev->ud.status = VDEV_ST_USED;
			spin_unlock(&vdev->ud.lock);

			if (urb->status == -EINPROGRESS) {
				/* This request is successfully completed. */
				/* If not -EINPROGRESS, possibly unlinked. */
				urb->status = 0;
			}

			goto no_need_xmit;

		case USB_REQ_GET_DESCRIPTOR:
			if (ctrlreq->wValue == cpu_to_le16(USB_DT_DEVICE << 8))
				usbip_dbg_vhci_hc("Not yet?: "
						  "Get_Descriptor to device 0 "
						  "(get max pipe size)\n");

			if (vdev->udev)
				usb_put_dev(vdev->udev);
			vdev->udev = usb_get_dev(urb->dev);
			goto out;

		default:
			/* NOT REACHED */
			dev_err(dev, "invalid request to devnum 0 bRequest %u, "
				"wValue %u\n", ctrlreq->bRequest,
				ctrlreq->wValue);
			ret =  -EINVAL;
			goto no_need_xmit;
		}

	}

out:
	vhci_tx_urb(urb);
	spin_unlock(&the_controller->lock);

	return 0;

no_need_xmit:
	usb_hcd_unlink_urb_from_ep(hcd, urb);
no_need_unlink:
	spin_unlock(&the_controller->lock);
	usb_hcd_giveback_urb(vhci_to_hcd(the_controller), urb, urb->status);
	return ret;
}

/*
 * vhci_rx gives back the urb after receiving the reply of the urb.  If an
 * unlink pdu is sent or not, vhci_rx receives a normal return pdu and gives
 * back its urb. For the driver unlinking the urb, the content of the urb is
 * not important, but the calling to its completion handler is important; the
 * completion of unlinking is notified by the completion handler.
 *
 *
 * CLIENT SIDE
 *
 * - When vhci_hcd receives RET_SUBMIT,
 *
 *	- case 1a). the urb of the pdu is not unlinking.
 *		- normal case
 *		=> just give back the urb
 *
 *	- case 1b). the urb of the pdu is unlinking.
 *		- usbip.ko will return a reply of the unlinking request.
 *		=> give back the urb now and go to case 2b).
 *
 * - When vhci_hcd receives RET_UNLINK,
 *
 *	- case 2a). a submit request is still pending in vhci_hcd.
 *		- urb was really pending in usbip.ko and urb_unlink_urb() was
 *		  completed there.
 *		=> free a pending submit request
 *		=> notify unlink completeness by giving back the urb
 *
 *	- case 2b). a submit request is *not* pending in vhci_hcd.
 *		- urb was already given back to the core driver.
 *		=> do not give back the urb
 *
 *
 * SERVER SIDE
 *
 * - When usbip receives CMD_UNLINK,
 *
 *	- case 3a). the urb of the unlink request is now in submission.
 *		=> do usb_unlink_urb().
 *		=> after the unlink is completed, send RET_UNLINK.
 *
 *	- case 3b). the urb of the unlink request is not in submission.
 *		- may be already completed or never be received
 *		=> send RET_UNLINK
 *
 */
static int vhci_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct vhci_priv *priv;
	struct vhci_device *vdev;

	pr_info("dequeue a urb %p\n", urb);

	spin_lock(&the_controller->lock);

	priv = urb->hcpriv;
	if (!priv) {
		/* URB was never linked! or will be soon given back by
		 * vhci_rx. */
		spin_unlock(&the_controller->lock);
		return 0;
	}

	{
		int ret = 0;
		ret = usb_hcd_check_unlink_urb(hcd, urb, status);
		if (ret) {
			spin_unlock(&the_controller->lock);
			return ret;
		}
	}

	 /* send unlink request here? */
	vdev = priv->vdev;

	if (!vdev->ud.tcp_socket) {
		/* tcp connection is closed */
		spin_lock(&vdev->priv_lock);

		pr_info("device %p seems to be disconnected\n", vdev);
		list_del(&priv->list);
		kfree(priv);
		urb->hcpriv = NULL;

		spin_unlock(&vdev->priv_lock);

		/*
		 * If tcp connection is alive, we have sent CMD_UNLINK.
		 * vhci_rx will receive RET_UNLINK and give back the URB.
		 * Otherwise, we give back it here.
		 */
		pr_info("gives back urb %p\n", urb);

		usb_hcd_unlink_urb_from_ep(hcd, urb);

		spin_unlock(&the_controller->lock);
		usb_hcd_giveback_urb(vhci_to_hcd(the_controller), urb,
				     urb->status);
		spin_lock(&the_controller->lock);

	} else {
		/* tcp connection is alive */
		struct vhci_unlink *unlink;

		spin_lock(&vdev->priv_lock);

		/* setup CMD_UNLINK pdu */
		unlink = kzalloc(sizeof(struct vhci_unlink), GFP_ATOMIC);
		if (!unlink) {
			spin_unlock(&vdev->priv_lock);
			spin_unlock(&the_controller->lock);
			usbip_event_add(&vdev->ud, VDEV_EVENT_ERROR_MALLOC);
			return -ENOMEM;
		}

		unlink->seqnum = atomic_inc_return(&the_controller->seqnum);
		if (unlink->seqnum == 0xffff)
			pr_info("seqnum max\n");

		unlink->unlink_seqnum = priv->seqnum;

		pr_info("device %p seems to be still connected\n", vdev);

		/* send cmd_unlink and try to cancel the pending URB in the
		 * peer */
		list_add_tail(&unlink->list, &vdev->unlink_tx);
		wake_up(&vdev->waitq_tx);

		spin_unlock(&vdev->priv_lock);
	}

	spin_unlock(&the_controller->lock);

	usbip_dbg_vhci_hc("leave\n");
	return 0;
}

static void vhci_device_unlink_cleanup(struct vhci_device *vdev)
{
	struct vhci_unlink *unlink, *tmp;

	spin_lock(&the_controller->lock);
	spin_lock(&vdev->priv_lock);

	list_for_each_entry_safe(unlink, tmp, &vdev->unlink_tx, list) {
		pr_info("unlink cleanup tx %lu\n", unlink->unlink_seqnum);
		list_del(&unlink->list);
		kfree(unlink);
	}

	while (!list_empty(&vdev->unlink_rx)) {
		struct urb *urb;

		unlink = list_first_entry(&vdev->unlink_rx, struct vhci_unlink,
			list);

		/* give back URB of unanswered unlink request */
		pr_info("unlink cleanup rx %lu\n", unlink->unlink_seqnum);

		urb = pickup_urb_and_free_priv(vdev, unlink->unlink_seqnum);
		if (!urb) {
			pr_info("the urb (seqnum %lu) was already given back\n",
				unlink->unlink_seqnum);
			list_del(&unlink->list);
			kfree(unlink);
			continue;
		}

		urb->status = -ENODEV;

		usb_hcd_unlink_urb_from_ep(vhci_to_hcd(the_controller), urb);

		list_del(&unlink->list);

		spin_unlock(&vdev->priv_lock);
		spin_unlock(&the_controller->lock);

		usb_hcd_giveback_urb(vhci_to_hcd(the_controller), urb,
				     urb->status);

		spin_lock(&the_controller->lock);
		spin_lock(&vdev->priv_lock);

		kfree(unlink);
	}

	spin_unlock(&vdev->priv_lock);
	spin_unlock(&the_controller->lock);
}

/*
 * The important thing is that only one context begins cleanup.
 * This is why error handling and cleanup become simple.
 * We do not want to consider race condition as possible.
 */
static void vhci_shutdown_connection(struct usbip_device *ud)
{
	struct vhci_device *vdev = container_of(ud, struct vhci_device, ud);

	/* need this? see stub_dev.c */
	if (ud->tcp_socket) {
		pr_debug("shutdown tcp_socket %p\n", ud->tcp_socket);
		kernel_sock_shutdown(ud->tcp_socket, SHUT_RDWR);
	}

	/* kill threads related to this sdev */
	if (vdev->ud.tcp_rx) {
		kthread_stop_put(vdev->ud.tcp_rx);
		vdev->ud.tcp_rx = NULL;
	}
	if (vdev->ud.tcp_tx) {
		kthread_stop_put(vdev->ud.tcp_tx);
		vdev->ud.tcp_tx = NULL;
	}
	pr_info("stop threads\n");

	/* active connection is closed */
	if (vdev->ud.tcp_socket) {
		fput(vdev->ud.tcp_socket->file);
		vdev->ud.tcp_socket = NULL;
	}
	pr_info("release socket\n");

	vhci_device_unlink_cleanup(vdev);

	/*
	 * rh_port_disconnect() is a trigger of ...
	 *   usb_disable_device():
	 *	disable all the endpoints for a USB device.
	 *   usb_disable_endpoint():
	 *	disable endpoints. pending urbs are unlinked(dequeued).
	 *
	 * NOTE: After calling rh_port_disconnect(), the USB device drivers of a
	 * detached device should release used urbs in a cleanup function (i.e.
	 * xxx_disconnect()). Therefore, vhci_hcd does not need to release
	 * pushed urbs and their private data in this function.
	 *
	 * NOTE: vhci_dequeue() must be considered carefully. When shutting down
	 * a connection, vhci_shutdown_connection() expects vhci_dequeue()
	 * gives back pushed urbs and frees their private data by request of
	 * the cleanup function of a USB driver. When unlinking a urb with an
	 * active connection, vhci_dequeue() does not give back the urb which
	 * is actually given back by vhci_rx after receiving its return pdu.
	 *
	 */
	rh_port_disconnect(vdev->rhport);

	pr_info("disconnect device\n");
}


static void vhci_device_reset(struct usbip_device *ud)
{
	struct vhci_device *vdev = container_of(ud, struct vhci_device, ud);

	spin_lock(&ud->lock);

	vdev->speed  = 0;
	vdev->devid  = 0;

	if (vdev->udev)
		usb_put_dev(vdev->udev);
	vdev->udev = NULL;

	if (ud->tcp_socket) {
		fput(ud->tcp_socket->file);
		ud->tcp_socket = NULL;
	}
	ud->status = VDEV_ST_NULL;

	spin_unlock(&ud->lock);
}

static void vhci_device_unusable(struct usbip_device *ud)
{
	spin_lock(&ud->lock);
	ud->status = VDEV_ST_ERROR;
	spin_unlock(&ud->lock);
}

static void vhci_device_init(struct vhci_device *vdev)
{
	memset(vdev, 0, sizeof(*vdev));

	vdev->ud.side   = USBIP_VHCI;
	vdev->ud.status = VDEV_ST_NULL;
	spin_lock_init(&vdev->ud.lock);

	INIT_LIST_HEAD(&vdev->priv_rx);
	INIT_LIST_HEAD(&vdev->priv_tx);
	INIT_LIST_HEAD(&vdev->unlink_tx);
	INIT_LIST_HEAD(&vdev->unlink_rx);
	spin_lock_init(&vdev->priv_lock);

	init_waitqueue_head(&vdev->waitq_tx);

	vdev->ud.eh_ops.shutdown = vhci_shutdown_connection;
	vdev->ud.eh_ops.reset = vhci_device_reset;
	vdev->ud.eh_ops.unusable = vhci_device_unusable;

	usbip_start_eh(&vdev->ud);
}

static int vhci_start(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	int rhport;
	int err = 0;

	usbip_dbg_vhci_hc("enter vhci_start\n");

	/* initialize private data of usb_hcd */

	for (rhport = 0; rhport < VHCI_NPORTS; rhport++) {
		struct vhci_device *vdev = &vhci->vdev[rhport];
		vhci_device_init(vdev);
		vdev->rhport = rhport;
	}

	atomic_set(&vhci->seqnum, 0);
	spin_lock_init(&vhci->lock);

	hcd->power_budget = 0; /* no limit */
	hcd->uses_new_polling = 1;

	/* vhci_hcd is now ready to be controlled through sysfs */
	err = sysfs_create_group(&vhci_dev(vhci)->kobj, &dev_attr_group);
	if (err) {
		pr_err("create sysfs files\n");
		return err;
	}

	return 0;
}

static void vhci_stop(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	int rhport = 0;

	usbip_dbg_vhci_hc("stop VHCI controller\n");

	/* 1. remove the userland interface of vhci_hcd */
	sysfs_remove_group(&vhci_dev(vhci)->kobj, &dev_attr_group);

	/* 2. shutdown all the ports of vhci_hcd */
	for (rhport = 0; rhport < VHCI_NPORTS; rhport++) {
		struct vhci_device *vdev = &vhci->vdev[rhport];

		usbip_event_add(&vdev->ud, VDEV_EVENT_REMOVED);
		usbip_stop_eh(&vdev->ud);
	}
}

static int vhci_get_frame_number(struct usb_hcd *hcd)
{
	pr_err("Not yet implemented\n");
	return 0;
}

#ifdef CONFIG_PM

/* FIXME: suspend/resume */
static int vhci_bus_suspend(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);

	dev_dbg(&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock(&vhci->lock);
	hcd->state = HC_STATE_SUSPENDED;
	spin_unlock(&vhci->lock);

	return 0;
}

static int vhci_bus_resume(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci = hcd_to_vhci(hcd);
	int rc = 0;

	dev_dbg(&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock(&vhci->lock);
	if (!HCD_HW_ACCESSIBLE(hcd))
		rc = -ESHUTDOWN;
	else
		hcd->state = HC_STATE_RUNNING;
	spin_unlock(&vhci->lock);

	return rc;
}

#else

#define vhci_bus_suspend      NULL
#define vhci_bus_resume       NULL
#endif

static struct hc_driver vhci_hc_driver = {
	.description	= driver_name,
	.product_desc	= driver_desc,
	.hcd_priv_size	= sizeof(struct vhci_hcd),

	.flags		= HCD_USB2,

	.start		= vhci_start,
	.stop		= vhci_stop,

	.urb_enqueue	= vhci_urb_enqueue,
	.urb_dequeue	= vhci_urb_dequeue,

	.get_frame_number = vhci_get_frame_number,

	.hub_status_data = vhci_hub_status,
	.hub_control    = vhci_hub_control,
	.bus_suspend	= vhci_bus_suspend,
	.bus_resume	= vhci_bus_resume,
};

static int vhci_hcd_probe(struct platform_device *pdev)
{
	struct usb_hcd		*hcd;
	int			ret;

	usbip_dbg_vhci_hc("name %s id %d\n", pdev->name, pdev->id);

	/*
	 * Allocate and initialize hcd.
	 * Our private data is also allocated automatically.
	 */
	hcd = usb_create_hcd(&vhci_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		pr_err("create hcd failed\n");
		return -ENOMEM;
	}
	hcd->has_tt = 1;

	/* this is private data for vhci_hcd */
	the_controller = hcd_to_vhci(hcd);

	/*
	 * Finish generic HCD structure initialization and register.
	 * Call the driver's reset() and start() routines.
	 */
	ret = usb_add_hcd(hcd, 0, 0);
	if (ret != 0) {
		pr_err("usb_add_hcd failed %d\n", ret);
		usb_put_hcd(hcd);
		the_controller = NULL;
		return ret;
	}

	usbip_dbg_vhci_hc("bye\n");
	return 0;
}

static int vhci_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd	*hcd;

	hcd = platform_get_drvdata(pdev);
	if (!hcd)
		return 0;

	/*
	 * Disconnects the root hub,
	 * then reverses the effects of usb_add_hcd(),
	 * invoking the HCD's stop() methods.
	 */
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	the_controller = NULL;

	return 0;
}

#ifdef CONFIG_PM

/* what should happen for USB/IP under suspend/resume? */
static int vhci_hcd_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd;
	int rhport = 0;
	int connected = 0;
	int ret = 0;

	hcd = platform_get_drvdata(pdev);

	spin_lock(&the_controller->lock);

	for (rhport = 0; rhport < VHCI_NPORTS; rhport++)
		if (the_controller->port_status[rhport] &
		    USB_PORT_STAT_CONNECTION)
			connected += 1;

	spin_unlock(&the_controller->lock);

	if (connected > 0) {
		dev_info(&pdev->dev, "We have %d active connection%s. Do not "
			 "suspend.\n", connected, (connected == 1 ? "" : "s"));
		ret =  -EBUSY;
	} else {
		dev_info(&pdev->dev, "suspend vhci_hcd");
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	}

	return ret;
}

static int vhci_hcd_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	hcd = platform_get_drvdata(pdev);
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_hcd_poll_rh_status(hcd);

	return 0;
}

#else

#define vhci_hcd_suspend	NULL
#define vhci_hcd_resume		NULL

#endif

static struct platform_driver vhci_driver = {
	.probe	= vhci_hcd_probe,
	.remove	= vhci_hcd_remove,
	.suspend = vhci_hcd_suspend,
	.resume	= vhci_hcd_resume,
	.driver	= {
		.name = driver_name,
		.owner = THIS_MODULE,
	},
};

/*
 * The VHCI 'device' is 'virtual'; not a real plug&play hardware.
 * We need to add this virtual device as a platform device arbitrarily:
 *	1. platform_device_register()
 */
static void the_pdev_release(struct device *dev)
{
	return;
}

static struct platform_device the_pdev = {
	/* should be the same name as driver_name */
	.name = driver_name,
	.id = -1,
	.dev = {
		.release = the_pdev_release,
	},
};

static int __init vhci_hcd_init(void)
{
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = platform_driver_register(&vhci_driver);
	if (ret)
		goto err_driver_register;

	ret = platform_device_register(&the_pdev);
	if (ret)
		goto err_platform_device_register;

	pr_info(DRIVER_DESC " v" USBIP_VERSION "\n");
	return ret;

err_platform_device_register:
	platform_driver_unregister(&vhci_driver);
err_driver_register:
	return ret;
}

static void __exit vhci_hcd_exit(void)
{
	platform_device_unregister(&the_pdev);
	platform_driver_unregister(&vhci_driver);
}

module_init(vhci_hcd_init);
module_exit(vhci_hcd_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(USBIP_VERSION);
