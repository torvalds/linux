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

#include <linux/platform_device.h>
#include "../../usb/core/hcd.h"


struct vhci_device {
	struct usb_device *udev;

	/*
	 * devid specifies a remote usb device uniquely instead
	 * of combination of busnum and devnum.
	 */
	__u32 devid;

	/* speed of a remote device */
	enum usb_device_speed speed;

	/*  vhci root-hub port to which this device is attached  */
	__u32 rhport;

	struct usbip_device ud;


	/* lock for the below link lists */
	spinlock_t priv_lock;

	/* vhci_priv is linked to one of them. */
	struct list_head priv_tx;
	struct list_head priv_rx;

	/* vhci_unlink is linked to one of them */
	struct list_head unlink_tx;
	struct list_head unlink_rx;

	/* vhci_tx thread sleeps for this queue */
	wait_queue_head_t waitq_tx;
};


/* urb->hcpriv, use container_of() */
struct vhci_priv {
	unsigned long seqnum;
	struct list_head list;

	struct vhci_device *vdev;
	struct urb *urb;
};


struct vhci_unlink {
	/* seqnum of this request */
	unsigned long seqnum;

	struct list_head list;

	/* seqnum of the unlink target */
	unsigned long unlink_seqnum;
};

/*
 * The number of ports is less than 16 ?
 * USB_MAXCHILDREN is statically defined to 16 in usb.h.  Its maximum value
 * would be 31 because the event_bits[1] of struct usb_hub is defined as
 * unsigned long in hub.h
 */
#define VHCI_NPORTS 8

/* for usb_bus.hcpriv */
struct vhci_hcd {
	spinlock_t	lock;

	u32	port_status[VHCI_NPORTS];

	unsigned	resuming:1;
	unsigned long	re_timeout;

	atomic_t seqnum;

	/*
	 * NOTE:
	 * wIndex shows the port number and begins from 1.
	 * But, the index of this array begins from 0.
	 */
	struct vhci_device vdev[VHCI_NPORTS];
};


extern struct vhci_hcd *the_controller;
extern struct attribute_group dev_attr_group;


/*-------------------------------------------------------------------------*/
/* prototype declaration */

/* vhci_hcd.c */
void rh_port_connect(int rhport, enum usb_device_speed speed);
void rh_port_disconnect(int rhport);
void vhci_rx_loop(struct usbip_task *ut);
void vhci_tx_loop(struct usbip_task *ut);

struct urb *pickup_urb_and_free_priv(struct vhci_device *vdev,
					    __u32 seqnum);

#define hardware		(&the_controller->pdev.dev)

static inline struct vhci_device *port_to_vdev(__u32 port)
{
	return &the_controller->vdev[port];
}

static inline struct vhci_hcd *hcd_to_vhci(struct usb_hcd *hcd)
{
	return (struct vhci_hcd *) (hcd->hcd_priv);
}

static inline struct usb_hcd *vhci_to_hcd(struct vhci_hcd *vhci)
{
	return container_of((void *) vhci, struct usb_hcd, hcd_priv);
}

static inline struct device *vhci_dev(struct vhci_hcd *vhci)
{
	return vhci_to_hcd(vhci)->self.controller;
}
