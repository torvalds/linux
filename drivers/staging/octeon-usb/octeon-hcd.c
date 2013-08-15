/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Cavium Networks
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/usb.h>

#include <linux/time.h>
#include <linux/delay.h>

#include <asm/octeon/cvmx.h>
#include "cvmx-usb.h"
#include <asm/octeon/cvmx-iob-defs.h>

#include <linux/usb/hcd.h>

#include <linux/err.h>

struct octeon_hcd {
	spinlock_t lock;
	cvmx_usb_state_t usb;
	struct tasklet_struct dequeue_tasklet;
	struct list_head dequeue_list;
};

/* convert between an HCD pointer and the corresponding struct octeon_hcd */
static inline struct octeon_hcd *hcd_to_octeon(struct usb_hcd *hcd)
{
	return (struct octeon_hcd *)(hcd->hcd_priv);
}

static inline struct usb_hcd *octeon_to_hcd(struct octeon_hcd *p)
{
	return container_of((void *)p, struct usb_hcd, hcd_priv);
}

static inline struct octeon_hcd *cvmx_usb_to_octeon(cvmx_usb_state_t *p)
{
	return container_of(p, struct octeon_hcd, usb);
}

static irqreturn_t octeon_usb_irq(struct usb_hcd *hcd)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	cvmx_usb_poll(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;
}

static void octeon_usb_port_callback(cvmx_usb_state_t *usb,
				     cvmx_usb_callback_t reason,
				     cvmx_usb_complete_t status,
				     int pipe_handle,
				     int submit_handle,
				     int bytes_transferred,
				     void *user_data)
{
	struct octeon_hcd *priv = cvmx_usb_to_octeon(usb);

	spin_unlock(&priv->lock);
	usb_hcd_poll_rh_status(octeon_to_hcd(priv));
	spin_lock(&priv->lock);
}

static int octeon_usb_start(struct usb_hcd *hcd)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	hcd->state = HC_STATE_RUNNING;
	spin_lock_irqsave(&priv->lock, flags);
	cvmx_usb_register_callback(&priv->usb, CVMX_USB_CALLBACK_PORT_CHANGED,
				   octeon_usb_port_callback, NULL);
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static void octeon_usb_stop(struct usb_hcd *hcd)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	cvmx_usb_register_callback(&priv->usb, CVMX_USB_CALLBACK_PORT_CHANGED,
				   NULL, NULL);
	spin_unlock_irqrestore(&priv->lock, flags);
	hcd->state = HC_STATE_HALT;
}

static int octeon_usb_get_frame_number(struct usb_hcd *hcd)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);

	return cvmx_usb_get_frame_number(&priv->usb);
}

static void octeon_usb_urb_complete_callback(cvmx_usb_state_t *usb,
					     cvmx_usb_callback_t reason,
					     cvmx_usb_complete_t status,
					     int pipe_handle,
					     int submit_handle,
					     int bytes_transferred,
					     void *user_data)
{
	struct octeon_hcd *priv = cvmx_usb_to_octeon(usb);
	struct usb_hcd *hcd = octeon_to_hcd(priv);
	struct device *dev = hcd->self.controller;
	struct urb *urb = user_data;

	urb->actual_length = bytes_transferred;
	urb->hcpriv = NULL;

	if (!list_empty(&urb->urb_list)) {
		/*
		 * It is on the dequeue_list, but we are going to call
		 * usb_hcd_giveback_urb(), so we must clear it from
		 * the list.  We got to it before the
		 * octeon_usb_urb_dequeue_work() tasklet did.
		 */
		list_del(&urb->urb_list);
		/* No longer on the dequeue_list. */
		INIT_LIST_HEAD(&urb->urb_list);
	}

	/* For Isochronous transactions we need to update the URB packet status
	   list from data in our private copy */
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;
		/*
		 * The pointer to the private list is stored in the setup_packet
		 * field.
		 */
		cvmx_usb_iso_packet_t *iso_packet = (cvmx_usb_iso_packet_t *) urb->setup_packet;
		/* Recalculate the transfer size by adding up each packet */
		urb->actual_length = 0;
		for (i = 0; i < urb->number_of_packets; i++) {
			if (iso_packet[i].status == CVMX_USB_COMPLETE_SUCCESS) {
				urb->iso_frame_desc[i].status = 0;
				urb->iso_frame_desc[i].actual_length = iso_packet[i].length;
				urb->actual_length += urb->iso_frame_desc[i].actual_length;
			} else {
				dev_dbg(dev, "ISOCHRONOUS packet=%d of %d status=%d pipe=%d submit=%d size=%d\n",
					i, urb->number_of_packets,
					iso_packet[i].status, pipe_handle,
					submit_handle, iso_packet[i].length);
				urb->iso_frame_desc[i].status = -EREMOTEIO;
			}
		}
		/* Free the private list now that we don't need it anymore */
		kfree(iso_packet);
		urb->setup_packet = NULL;
	}

	switch (status) {
	case CVMX_USB_COMPLETE_SUCCESS:
		urb->status = 0;
		break;
	case CVMX_USB_COMPLETE_CANCEL:
		if (urb->status == 0)
			urb->status = -ENOENT;
		break;
	case CVMX_USB_COMPLETE_STALL:
		dev_dbg(dev, "status=stall pipe=%d submit=%d size=%d\n",
			pipe_handle, submit_handle, bytes_transferred);
		urb->status = -EPIPE;
		break;
	case CVMX_USB_COMPLETE_BABBLEERR:
		dev_dbg(dev, "status=babble pipe=%d submit=%d size=%d\n",
			pipe_handle, submit_handle, bytes_transferred);
		urb->status = -EPIPE;
		break;
	case CVMX_USB_COMPLETE_SHORT:
		dev_dbg(dev, "status=short pipe=%d submit=%d size=%d\n",
			pipe_handle, submit_handle, bytes_transferred);
		urb->status = -EREMOTEIO;
		break;
	case CVMX_USB_COMPLETE_ERROR:
	case CVMX_USB_COMPLETE_XACTERR:
	case CVMX_USB_COMPLETE_DATATGLERR:
	case CVMX_USB_COMPLETE_FRAMEERR:
		dev_dbg(dev, "status=%d pipe=%d submit=%d size=%d\n",
			status, pipe_handle, submit_handle, bytes_transferred);
		urb->status = -EPROTO;
		break;
	}
	spin_unlock(&priv->lock);
	usb_hcd_giveback_urb(octeon_to_hcd(priv), urb, urb->status);
	spin_lock(&priv->lock);
}

static int octeon_usb_urb_enqueue(struct usb_hcd *hcd,
				  struct urb *urb,
				  gfp_t mem_flags)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	struct device *dev = hcd->self.controller;
	int submit_handle = -1;
	int pipe_handle;
	unsigned long flags;
	cvmx_usb_iso_packet_t *iso_packet;
	struct usb_host_endpoint *ep = urb->ep;

	urb->status = 0;
	INIT_LIST_HEAD(&urb->urb_list);	/* not enqueued on dequeue_list */
	spin_lock_irqsave(&priv->lock, flags);

	if (!ep->hcpriv) {
		cvmx_usb_transfer_t transfer_type;
		cvmx_usb_speed_t speed;
		int split_device = 0;
		int split_port = 0;
		switch (usb_pipetype(urb->pipe)) {
		case PIPE_ISOCHRONOUS:
			transfer_type = CVMX_USB_TRANSFER_ISOCHRONOUS;
			break;
		case PIPE_INTERRUPT:
			transfer_type = CVMX_USB_TRANSFER_INTERRUPT;
			break;
		case PIPE_CONTROL:
			transfer_type = CVMX_USB_TRANSFER_CONTROL;
			break;
		default:
			transfer_type = CVMX_USB_TRANSFER_BULK;
			break;
		}
		switch (urb->dev->speed) {
		case USB_SPEED_LOW:
			speed = CVMX_USB_SPEED_LOW;
			break;
		case USB_SPEED_FULL:
			speed = CVMX_USB_SPEED_FULL;
			break;
		default:
			speed = CVMX_USB_SPEED_HIGH;
			break;
		}
		/*
		 * For slow devices on high speed ports we need to find the hub
		 * that does the speed translation so we know where to send the
		 * split transactions.
		 */
		if (speed != CVMX_USB_SPEED_HIGH) {
			/*
			 * Start at this device and work our way up the usb
			 * tree.
			 */
			struct usb_device *dev = urb->dev;
			while (dev->parent) {
				/*
				 * If our parent is high speed then he'll
				 * receive the splits.
				 */
				if (dev->parent->speed == USB_SPEED_HIGH) {
					split_device = dev->parent->devnum;
					split_port = dev->portnum;
					break;
				}
				/*
				 * Move up the tree one level. If we make it all
				 * the way up the tree, then the port must not
				 * be in high speed mode and we don't need a
				 * split.
				 */
				dev = dev->parent;
			}
		}
		pipe_handle = cvmx_usb_open_pipe(&priv->usb,
						 0,
						 usb_pipedevice(urb->pipe),
						 usb_pipeendpoint(urb->pipe),
						 speed,
						 le16_to_cpu(ep->desc.wMaxPacketSize) & 0x7ff,
						 transfer_type,
						 usb_pipein(urb->pipe) ? CVMX_USB_DIRECTION_IN : CVMX_USB_DIRECTION_OUT,
						 urb->interval,
						 (le16_to_cpu(ep->desc.wMaxPacketSize) >> 11) & 0x3,
						 split_device,
						 split_port);
		if (pipe_handle < 0) {
			spin_unlock_irqrestore(&priv->lock, flags);
			dev_dbg(dev, "Failed to create pipe\n");
			return -ENOMEM;
		}
		ep->hcpriv = (void *)(0x10000L + pipe_handle);
	} else {
		pipe_handle = 0xffff & (long)ep->hcpriv;
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		dev_dbg(dev, "Submit isochronous to %d.%d\n",
			usb_pipedevice(urb->pipe), usb_pipeendpoint(urb->pipe));
		/*
		 * Allocate a structure to use for our private list of
		 * isochronous packets.
		 */
		iso_packet = kmalloc(urb->number_of_packets * sizeof(cvmx_usb_iso_packet_t), GFP_ATOMIC);
		if (iso_packet) {
			int i;
			/* Fill the list with the data from the URB */
			for (i = 0; i < urb->number_of_packets; i++) {
				iso_packet[i].offset = urb->iso_frame_desc[i].offset;
				iso_packet[i].length = urb->iso_frame_desc[i].length;
				iso_packet[i].status = CVMX_USB_COMPLETE_ERROR;
			}
			/*
			 * Store a pointer to the list in the URB setup_packet
			 * field. We know this currently isn't being used and
			 * this saves us a bunch of logic.
			 */
			urb->setup_packet = (char *)iso_packet;
			submit_handle = cvmx_usb_submit_isochronous(&priv->usb, pipe_handle,
							urb->start_frame,
							0 /* flags */ ,
							urb->number_of_packets,
							iso_packet,
							urb->transfer_dma,
							urb->transfer_buffer_length,
							octeon_usb_urb_complete_callback,
							urb);
			/*
			 * If submit failed we need to free our private packet
			 * list.
			 */
			if (submit_handle < 0) {
				urb->setup_packet = NULL;
				kfree(iso_packet);
			}
		}
		break;
	case PIPE_INTERRUPT:
		dev_dbg(dev, "Submit interrupt to %d.%d\n",
			usb_pipedevice(urb->pipe), usb_pipeendpoint(urb->pipe));
		submit_handle = cvmx_usb_submit_interrupt(&priv->usb, pipe_handle,
					      urb->transfer_dma,
					      urb->transfer_buffer_length,
					      octeon_usb_urb_complete_callback,
					      urb);
		break;
	case PIPE_CONTROL:
		dev_dbg(dev, "Submit control to %d.%d\n",
			usb_pipedevice(urb->pipe), usb_pipeendpoint(urb->pipe));
		submit_handle = cvmx_usb_submit_control(&priv->usb, pipe_handle,
					    urb->setup_dma,
					    urb->transfer_dma,
					    urb->transfer_buffer_length,
					    octeon_usb_urb_complete_callback,
					    urb);
		break;
	case PIPE_BULK:
		dev_dbg(dev, "Submit bulk to %d.%d\n",
			usb_pipedevice(urb->pipe), usb_pipeendpoint(urb->pipe));
		submit_handle = cvmx_usb_submit_bulk(&priv->usb, pipe_handle,
					 urb->transfer_dma,
					 urb->transfer_buffer_length,
					 octeon_usb_urb_complete_callback,
					 urb);
		break;
	}
	if (submit_handle < 0) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dev_dbg(dev, "Failed to submit\n");
		return -ENOMEM;
	}
	urb->hcpriv = (void *)(long)(((submit_handle & 0xffff) << 16) | pipe_handle);
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static void octeon_usb_urb_dequeue_work(unsigned long arg)
{
	unsigned long flags;
	struct octeon_hcd *priv = (struct octeon_hcd *)arg;

	spin_lock_irqsave(&priv->lock, flags);

	while (!list_empty(&priv->dequeue_list)) {
		int pipe_handle;
		int submit_handle;
		struct urb *urb = container_of(priv->dequeue_list.next, struct urb, urb_list);
		list_del(&urb->urb_list);
		/* not enqueued on dequeue_list */
		INIT_LIST_HEAD(&urb->urb_list);
		pipe_handle = 0xffff & (long)urb->hcpriv;
		submit_handle = ((long)urb->hcpriv) >> 16;
		cvmx_usb_cancel(&priv->usb, pipe_handle, submit_handle);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int octeon_usb_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	if (!urb->dev)
		return -EINVAL;

	spin_lock_irqsave(&priv->lock, flags);

	urb->status = status;
	list_add_tail(&urb->urb_list, &priv->dequeue_list);

	spin_unlock_irqrestore(&priv->lock, flags);

	tasklet_schedule(&priv->dequeue_tasklet);

	return 0;
}

static void octeon_usb_endpoint_disable(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	struct device *dev = hcd->self.controller;

	if (ep->hcpriv) {
		struct octeon_hcd *priv = hcd_to_octeon(hcd);
		int pipe_handle = 0xffff & (long)ep->hcpriv;
		unsigned long flags;
		spin_lock_irqsave(&priv->lock, flags);
		cvmx_usb_cancel_all(&priv->usb, pipe_handle);
		if (cvmx_usb_close_pipe(&priv->usb, pipe_handle))
			dev_dbg(dev, "Closing pipe %d failed\n", pipe_handle);
		spin_unlock_irqrestore(&priv->lock, flags);
		ep->hcpriv = NULL;
	}
}

static int octeon_usb_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	cvmx_usb_port_status_t port_status;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	port_status = cvmx_usb_get_status(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);
	buf[0] = 0;
	buf[0] = port_status.connect_change << 1;

	return (buf[0] != 0);
}

static int octeon_usb_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue, u16 wIndex, char *buf, u16 wLength)
{
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	struct device *dev = hcd->self.controller;
	cvmx_usb_port_status_t usb_port_status;
	int port_status;
	struct usb_hub_descriptor *desc;
	unsigned long flags;

	switch (typeReq) {
	case ClearHubFeature:
		dev_dbg(dev, "ClearHubFeature\n");
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* Nothing required here */
			break;
		default:
			return -EINVAL;
		}
		break;
	case ClearPortFeature:
		dev_dbg(dev, "ClearPortFeature\n");
		if (wIndex != 1) {
			dev_dbg(dev, " INVALID\n");
			return -EINVAL;
		}

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			dev_dbg(dev, " ENABLE\n");
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_disable(&priv->usb);
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(dev, " SUSPEND\n");
			/* Not supported on Octeon */
			break;
		case USB_PORT_FEAT_POWER:
			dev_dbg(dev, " POWER\n");
			/* Not supported on Octeon */
			break;
		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(dev, " INDICATOR\n");
			/* Port inidicator not supported */
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			dev_dbg(dev, " C_CONNECTION\n");
			/* Clears drivers internal connect status change flag */
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_set_status(&priv->usb, cvmx_usb_get_status(&priv->usb));
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_C_RESET:
			dev_dbg(dev, " C_RESET\n");
			/*
			 * Clears the driver's internal Port Reset Change flag.
			 */
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_set_status(&priv->usb, cvmx_usb_get_status(&priv->usb));
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			dev_dbg(dev, " C_ENABLE\n");
			/*
			 * Clears the driver's internal Port Enable/Disable
			 * Change flag.
			 */
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_set_status(&priv->usb, cvmx_usb_get_status(&priv->usb));
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			dev_dbg(dev, " C_SUSPEND\n");
			/*
			 * Clears the driver's internal Port Suspend Change
			 * flag, which is set when resume signaling on the host
			 * port is complete.
			 */
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(dev, " C_OVER_CURRENT\n");
			/* Clears the driver's overcurrent Change flag */
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_set_status(&priv->usb, cvmx_usb_get_status(&priv->usb));
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		default:
			dev_dbg(dev, " UNKNOWN\n");
			return -EINVAL;
		}
		break;
	case GetHubDescriptor:
		dev_dbg(dev, "GetHubDescriptor\n");
		desc = (struct usb_hub_descriptor *)buf;
		desc->bDescLength = 9;
		desc->bDescriptorType = 0x29;
		desc->bNbrPorts = 1;
		desc->wHubCharacteristics = 0x08;
		desc->bPwrOn2PwrGood = 1;
		desc->bHubContrCurrent = 0;
		desc->u.hs.DeviceRemovable[0] = 0;
		desc->u.hs.DeviceRemovable[1] = 0xff;
		break;
	case GetHubStatus:
		dev_dbg(dev, "GetHubStatus\n");
		*(__le32 *) buf = 0;
		break;
	case GetPortStatus:
		dev_dbg(dev, "GetPortStatus\n");
		if (wIndex != 1) {
			dev_dbg(dev, " INVALID\n");
			return -EINVAL;
		}

		spin_lock_irqsave(&priv->lock, flags);
		usb_port_status = cvmx_usb_get_status(&priv->usb);
		spin_unlock_irqrestore(&priv->lock, flags);
		port_status = 0;

		if (usb_port_status.connect_change) {
			port_status |= (1 << USB_PORT_FEAT_C_CONNECTION);
			dev_dbg(dev, " C_CONNECTION\n");
		}

		if (usb_port_status.port_enabled) {
			port_status |= (1 << USB_PORT_FEAT_C_ENABLE);
			dev_dbg(dev, " C_ENABLE\n");
		}

		if (usb_port_status.connected) {
			port_status |= (1 << USB_PORT_FEAT_CONNECTION);
			dev_dbg(dev, " CONNECTION\n");
		}

		if (usb_port_status.port_enabled) {
			port_status |= (1 << USB_PORT_FEAT_ENABLE);
			dev_dbg(dev, " ENABLE\n");
		}

		if (usb_port_status.port_over_current) {
			port_status |= (1 << USB_PORT_FEAT_OVER_CURRENT);
			dev_dbg(dev, " OVER_CURRENT\n");
		}

		if (usb_port_status.port_powered) {
			port_status |= (1 << USB_PORT_FEAT_POWER);
			dev_dbg(dev, " POWER\n");
		}

		if (usb_port_status.port_speed == CVMX_USB_SPEED_HIGH) {
			port_status |= USB_PORT_STAT_HIGH_SPEED;
			dev_dbg(dev, " HIGHSPEED\n");
		} else if (usb_port_status.port_speed == CVMX_USB_SPEED_LOW) {
			port_status |= (1 << USB_PORT_FEAT_LOWSPEED);
			dev_dbg(dev, " LOWSPEED\n");
		}

		*((__le32 *) buf) = cpu_to_le32(port_status);
		break;
	case SetHubFeature:
		dev_dbg(dev, "SetHubFeature\n");
		/* No HUB features supported */
		break;
	case SetPortFeature:
		dev_dbg(dev, "SetPortFeature\n");
		if (wIndex != 1) {
			dev_dbg(dev, " INVALID\n");
			return -EINVAL;
		}

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(dev, " SUSPEND\n");
			return -EINVAL;
		case USB_PORT_FEAT_POWER:
			dev_dbg(dev, " POWER\n");
			return -EINVAL;
		case USB_PORT_FEAT_RESET:
			dev_dbg(dev, " RESET\n");
			spin_lock_irqsave(&priv->lock, flags);
			cvmx_usb_disable(&priv->usb);
			if (cvmx_usb_enable(&priv->usb))
				dev_dbg(dev, "Failed to enable the port\n");
			spin_unlock_irqrestore(&priv->lock, flags);
			return 0;
		case USB_PORT_FEAT_INDICATOR:
			dev_dbg(dev, " INDICATOR\n");
			/* Not supported */
			break;
		default:
			dev_dbg(dev, " UNKNOWN\n");
			return -EINVAL;
		}
		break;
	default:
		dev_dbg(dev, "Unknown root hub request\n");
		return -EINVAL;
	}
	return 0;
}


static const struct hc_driver octeon_hc_driver = {
	.description		= "Octeon USB",
	.product_desc		= "Octeon Host Controller",
	.hcd_priv_size		= sizeof(struct octeon_hcd),
	.irq			= octeon_usb_irq,
	.flags			= HCD_MEMORY | HCD_USB2,
	.start			= octeon_usb_start,
	.stop			= octeon_usb_stop,
	.urb_enqueue		= octeon_usb_urb_enqueue,
	.urb_dequeue		= octeon_usb_urb_dequeue,
	.endpoint_disable	= octeon_usb_endpoint_disable,
	.get_frame_number	= octeon_usb_get_frame_number,
	.hub_status_data	= octeon_usb_hub_status_data,
	.hub_control		= octeon_usb_hub_control,
};


static int octeon_usb_driver_probe(struct device *dev)
{
	int status;
	int usb_num = to_platform_device(dev)->id;
	int irq = platform_get_irq(to_platform_device(dev), 0);
	struct octeon_hcd *priv;
	struct usb_hcd *hcd;
	unsigned long flags;

	/*
	 * Set the DMA mask to 64bits so we get buffers already translated for
	 * DMA.
	 */
	dev->coherent_dma_mask = ~0;
	dev->dma_mask = &dev->coherent_dma_mask;

	hcd = usb_create_hcd(&octeon_hc_driver, dev, dev_name(dev));
	if (!hcd) {
		dev_dbg(dev, "Failed to allocate memory for HCD\n");
		return -1;
	}
	hcd->uses_new_polling = 1;
	priv = (struct octeon_hcd *)hcd->hcd_priv;

	spin_lock_init(&priv->lock);

	tasklet_init(&priv->dequeue_tasklet, octeon_usb_urb_dequeue_work, (unsigned long)priv);
	INIT_LIST_HEAD(&priv->dequeue_list);

	status = cvmx_usb_initialize(&priv->usb, usb_num, CVMX_USB_INITIALIZE_FLAGS_CLOCK_AUTO);
	if (status) {
		dev_dbg(dev, "USB initialization failed with %d\n", status);
		kfree(hcd);
		return -1;
	}

	/* This delay is needed for CN3010, but I don't know why... */
	mdelay(10);

	spin_lock_irqsave(&priv->lock, flags);
	cvmx_usb_poll(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);

	status = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (status) {
		dev_dbg(dev, "USB add HCD failed with %d\n", status);
		kfree(hcd);
		return -1;
	}

	dev_dbg(dev, "Registered HCD for port %d on irq %d\n", usb_num, irq);

	return 0;
}

static int octeon_usb_driver_remove(struct device *dev)
{
	int status;
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct octeon_hcd *priv = hcd_to_octeon(hcd);
	unsigned long flags;

	usb_remove_hcd(hcd);
	tasklet_kill(&priv->dequeue_tasklet);
	spin_lock_irqsave(&priv->lock, flags);
	status = cvmx_usb_shutdown(&priv->usb);
	spin_unlock_irqrestore(&priv->lock, flags);
	if (status)
		dev_dbg(dev, "USB shutdown failed with %d\n", status);

	kfree(hcd);

	return 0;
}

static struct device_driver octeon_usb_driver = {
	.name	= "OcteonUSB",
	.bus	= &platform_bus_type,
	.probe	= octeon_usb_driver_probe,
	.remove	= octeon_usb_driver_remove,
};


#define MAX_USB_PORTS   10
static struct platform_device *pdev_glob[MAX_USB_PORTS];
static int octeon_usb_registered;
static int __init octeon_usb_module_init(void)
{
	int num_devices = cvmx_usb_get_num_ports();
	int device;

	if (usb_disabled() || num_devices == 0)
		return -ENODEV;

	if (driver_register(&octeon_usb_driver))
		return -ENOMEM;

	octeon_usb_registered = 1;

	/*
	 * Only cn52XX and cn56XX have DWC_OTG USB hardware and the
	 * IOB priority registers.  Under heavy network load USB
	 * hardware can be starved by the IOB causing a crash.  Give
	 * it a priority boost if it has been waiting more than 400
	 * cycles to avoid this situation.
	 *
	 * Testing indicates that a cnt_val of 8192 is not sufficient,
	 * but no failures are seen with 4096.  We choose a value of
	 * 400 to give a safety factor of 10.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)) {
		union cvmx_iob_n2c_l2c_pri_cnt pri_cnt;

		pri_cnt.u64 = 0;
		pri_cnt.s.cnt_enb = 1;
		pri_cnt.s.cnt_val = 400;
		cvmx_write_csr(CVMX_IOB_N2C_L2C_PRI_CNT, pri_cnt.u64);
	}

	for (device = 0; device < num_devices; device++) {
		struct resource irq_resource;
		struct platform_device *pdev;
		memset(&irq_resource, 0, sizeof(irq_resource));
		irq_resource.start = (device == 0) ? OCTEON_IRQ_USB0 : OCTEON_IRQ_USB1;
		irq_resource.end = irq_resource.start;
		irq_resource.flags = IORESOURCE_IRQ;
		pdev = platform_device_register_simple((char *)octeon_usb_driver.  name, device, &irq_resource, 1);
		if (IS_ERR(pdev)) {
			driver_unregister(&octeon_usb_driver);
			octeon_usb_registered = 0;
			return PTR_ERR(pdev);
		}
		if (device < MAX_USB_PORTS)
			pdev_glob[device] = pdev;

	}
	return 0;
}

static void __exit octeon_usb_module_cleanup(void)
{
	int i;

	for (i = 0; i < MAX_USB_PORTS; i++)
		if (pdev_glob[i]) {
			platform_device_unregister(pdev_glob[i]);
			pdev_glob[i] = NULL;
		}
	if (octeon_usb_registered)
		driver_unregister(&octeon_usb_driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cavium Networks <support@caviumnetworks.com>");
MODULE_DESCRIPTION("Cavium Networks Octeon USB Host driver.");
module_init(octeon_usb_module_init);
module_exit(octeon_usb_module_cleanup);
