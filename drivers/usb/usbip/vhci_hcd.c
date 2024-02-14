// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Nobuo Iwata
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

int vhci_num_controllers = VHCI_NR_HCS;
struct vhci *vhcis;

static const char * const bit_desc[] = {
	"CONNECTION",		/*0*/
	"ENABLE",		/*1*/
	"SUSPEND",		/*2*/
	"OVER_CURRENT",		/*3*/
	"RESET",		/*4*/
	"L1",			/*5*/
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
	"C_L1",			/*21*/
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

static const char * const bit_desc_ss[] = {
	"CONNECTION",		/*0*/
	"ENABLE",		/*1*/
	"SUSPEND",		/*2*/
	"OVER_CURRENT",		/*3*/
	"RESET",		/*4*/
	"L1",			/*5*/
	"R6",			/*6*/
	"R7",			/*7*/
	"R8",			/*8*/
	"POWER",		/*9*/
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
	"C_BH_RESET",		/*21*/
	"C_LINK_STATE",		/*22*/
	"C_CONFIG_ERROR",	/*23*/
	"R24",			/*24*/
	"R25",			/*25*/
	"R26",			/*26*/
	"R27",			/*27*/
	"R28",			/*28*/
	"R29",			/*29*/
	"R30",			/*30*/
	"R31",			/*31*/
};

static void dump_port_status_diff(u32 prev_status, u32 new_status, bool usb3)
{
	int i = 0;
	u32 bit = 1;
	const char * const *desc = bit_desc;

	if (usb3)
		desc = bit_desc_ss;

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

		if (prev || new) {
			pr_debug(" %c%s\n", change, desc[i]);

			if (bit == 1) /* USB_PORT_STAT_CONNECTION */
				pr_debug(" %c%s\n", change, "USB_PORT_STAT_SPEED_5GBPS");
		}
		bit <<= 1;
		i++;
	}
	pr_debug("\n");
}

void rh_port_connect(struct vhci_device *vdev, enum usb_device_speed speed)
{
	struct vhci_hcd	*vhci_hcd = vdev_to_vhci_hcd(vdev);
	struct vhci *vhci = vhci_hcd->vhci;
	int		rhport = vdev->rhport;
	u32		status;
	unsigned long	flags;

	usbip_dbg_vhci_rh("rh_port_connect %d\n", rhport);

	spin_lock_irqsave(&vhci->lock, flags);

	status = vhci_hcd->port_status[rhport];

	status |= USB_PORT_STAT_CONNECTION | (1 << USB_PORT_FEAT_C_CONNECTION);

	switch (speed) {
	case USB_SPEED_HIGH:
		status |= USB_PORT_STAT_HIGH_SPEED;
		break;
	case USB_SPEED_LOW:
		status |= USB_PORT_STAT_LOW_SPEED;
		break;
	default:
		break;
	}

	vhci_hcd->port_status[rhport] = status;

	spin_unlock_irqrestore(&vhci->lock, flags);

	usb_hcd_poll_rh_status(vhci_hcd_to_hcd(vhci_hcd));
}

static void rh_port_disconnect(struct vhci_device *vdev)
{
	struct vhci_hcd	*vhci_hcd = vdev_to_vhci_hcd(vdev);
	struct vhci *vhci = vhci_hcd->vhci;
	int		rhport = vdev->rhport;
	u32		status;
	unsigned long	flags;

	usbip_dbg_vhci_rh("rh_port_disconnect %d\n", rhport);

	spin_lock_irqsave(&vhci->lock, flags);

	status = vhci_hcd->port_status[rhport];

	status &= ~USB_PORT_STAT_CONNECTION;
	status |= (1 << USB_PORT_FEAT_C_CONNECTION);

	vhci_hcd->port_status[rhport] = status;

	spin_unlock_irqrestore(&vhci->lock, flags);
	usb_hcd_poll_rh_status(vhci_hcd_to_hcd(vhci_hcd));
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
	struct vhci_hcd	*vhci_hcd = hcd_to_vhci_hcd(hcd);
	struct vhci *vhci = vhci_hcd->vhci;
	int		retval = DIV_ROUND_UP(VHCI_HC_PORTS + 1, 8);
	int		rhport;
	int		changed = 0;
	unsigned long	flags;

	memset(buf, 0, retval);

	spin_lock_irqsave(&vhci->lock, flags);
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		usbip_dbg_vhci_rh("hw accessible flag not on?\n");
		goto done;
	}

	/* check pseudo status register for each port */
	for (rhport = 0; rhport < VHCI_HC_PORTS; rhport++) {
		if ((vhci_hcd->port_status[rhport] & PORT_C_MASK)) {
			/* The status of a port has been changed, */
			usbip_dbg_vhci_rh("port %d status changed\n", rhport);

			buf[(rhport + 1) / 8] |= 1 << (rhport + 1) % 8;
			changed = 1;
		}
	}

	if ((hcd->state == HC_STATE_SUSPENDED) && (changed == 1))
		usb_hcd_resume_root_hub(hcd);

done:
	spin_unlock_irqrestore(&vhci->lock, flags);
	return changed ? retval : 0;
}

/* usb 3.0 root hub device descriptor */
static struct {
	struct usb_bos_descriptor bos;
	struct usb_ss_cap_descriptor ss_cap;
} __packed usb3_bos_desc = {

	.bos = {
		.bLength		= USB_DT_BOS_SIZE,
		.bDescriptorType	= USB_DT_BOS,
		.wTotalLength		= cpu_to_le16(sizeof(usb3_bos_desc)),
		.bNumDeviceCaps		= 1,
	},
	.ss_cap = {
		.bLength		= USB_DT_USB_SS_CAP_SIZE,
		.bDescriptorType	= USB_DT_DEVICE_CAPABILITY,
		.bDevCapabilityType	= USB_SS_CAP_TYPE,
		.wSpeedSupported	= cpu_to_le16(USB_5GBPS_OPERATION),
		.bFunctionalitySupport	= ilog2(USB_5GBPS_OPERATION),
	},
};

static inline void
ss_hub_descriptor(struct usb_hub_descriptor *desc)
{
	memset(desc, 0, sizeof *desc);
	desc->bDescriptorType = USB_DT_SS_HUB;
	desc->bDescLength = 12;
	desc->wHubCharacteristics = cpu_to_le16(
		HUB_CHAR_INDV_PORT_LPSM | HUB_CHAR_COMMON_OCPM);
	desc->bNbrPorts = VHCI_HC_PORTS;
	desc->u.ss.bHubHdrDecLat = 0x04; /* Worst case: 0.4 micro sec*/
	desc->u.ss.DeviceRemovable = 0xffff;
}

static inline void hub_descriptor(struct usb_hub_descriptor *desc)
{
	int width;

	memset(desc, 0, sizeof(*desc));
	desc->bDescriptorType = USB_DT_HUB;
	desc->wHubCharacteristics = cpu_to_le16(
		HUB_CHAR_INDV_PORT_LPSM | HUB_CHAR_COMMON_OCPM);

	desc->bNbrPorts = VHCI_HC_PORTS;
	BUILD_BUG_ON(VHCI_HC_PORTS > USB_MAXCHILDREN);
	width = desc->bNbrPorts / 8 + 1;
	desc->bDescLength = USB_DT_HUB_NONVAR_SIZE + 2 * width;
	memset(&desc->u.hs.DeviceRemovable[0], 0, width);
	memset(&desc->u.hs.DeviceRemovable[width], 0xff, width);
}

static int vhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			    u16 wIndex, char *buf, u16 wLength)
{
	struct vhci_hcd	*vhci_hcd;
	struct vhci	*vhci;
	int             retval = 0;
	int		rhport = -1;
	unsigned long	flags;
	bool invalid_rhport = false;

	u32 prev_port_status[VHCI_HC_PORTS];

	if (!HCD_HW_ACCESSIBLE(hcd))
		return -ETIMEDOUT;

	/*
	 * NOTE:
	 * wIndex (bits 0-7) shows the port number and begins from 1?
	 */
	wIndex = ((__u8)(wIndex & 0x00ff));
	usbip_dbg_vhci_rh("typeReq %x wValue %x wIndex %x\n", typeReq, wValue,
			  wIndex);

	/*
	 * wIndex can be 0 for some request types (typeReq). rhport is
	 * in valid range when wIndex >= 1 and < VHCI_HC_PORTS.
	 *
	 * Reference port_status[] only with valid rhport when
	 * invalid_rhport is false.
	 */
	if (wIndex < 1 || wIndex > VHCI_HC_PORTS) {
		invalid_rhport = true;
		if (wIndex > VHCI_HC_PORTS)
			pr_err("invalid port number %d\n", wIndex);
	} else
		rhport = wIndex - 1;

	vhci_hcd = hcd_to_vhci_hcd(hcd);
	vhci = vhci_hcd->vhci;

	spin_lock_irqsave(&vhci->lock, flags);

	/* store old status and compare now and old later */
	if (usbip_dbg_flag_vhci_rh) {
		if (!invalid_rhport)
			memcpy(prev_port_status, vhci_hcd->port_status,
				sizeof(prev_port_status));
	}

	switch (typeReq) {
	case ClearHubFeature:
		usbip_dbg_vhci_rh(" ClearHubFeature\n");
		break;
	case ClearPortFeature:
		if (invalid_rhport) {
			pr_err("invalid port number %d\n", wIndex);
			goto error;
		}
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (hcd->speed == HCD_USB3) {
				pr_err(" ClearPortFeature: USB_PORT_FEAT_SUSPEND req not "
				       "supported for USB 3.0 roothub\n");
				goto error;
			}
			usbip_dbg_vhci_rh(
				" ClearPortFeature: USB_PORT_FEAT_SUSPEND\n");
			if (vhci_hcd->port_status[rhport] & USB_PORT_STAT_SUSPEND) {
				/* 20msec signaling */
				vhci_hcd->resuming = 1;
				vhci_hcd->re_timeout = jiffies + msecs_to_jiffies(20);
			}
			break;
		case USB_PORT_FEAT_POWER:
			usbip_dbg_vhci_rh(
				" ClearPortFeature: USB_PORT_FEAT_POWER\n");
			if (hcd->speed == HCD_USB3)
				vhci_hcd->port_status[rhport] &= ~USB_SS_PORT_STAT_POWER;
			else
				vhci_hcd->port_status[rhport] &= ~USB_PORT_STAT_POWER;
			break;
		default:
			usbip_dbg_vhci_rh(" ClearPortFeature: default %x\n",
					  wValue);
			if (wValue >= 32)
				goto error;
			vhci_hcd->port_status[rhport] &= ~(1 << wValue);
			break;
		}
		break;
	case GetHubDescriptor:
		usbip_dbg_vhci_rh(" GetHubDescriptor\n");
		if (hcd->speed == HCD_USB3 &&
				(wLength < USB_DT_SS_HUB_SIZE ||
				 wValue != (USB_DT_SS_HUB << 8))) {
			pr_err("Wrong hub descriptor type for USB 3.0 roothub.\n");
			goto error;
		}
		if (hcd->speed == HCD_USB3)
			ss_hub_descriptor((struct usb_hub_descriptor *) buf);
		else
			hub_descriptor((struct usb_hub_descriptor *) buf);
		break;
	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
		if (hcd->speed != HCD_USB3)
			goto error;

		if ((wValue >> 8) != USB_DT_BOS)
			goto error;

		memcpy(buf, &usb3_bos_desc, sizeof(usb3_bos_desc));
		retval = sizeof(usb3_bos_desc);
		break;
	case GetHubStatus:
		usbip_dbg_vhci_rh(" GetHubStatus\n");
		*(__le32 *) buf = cpu_to_le32(0);
		break;
	case GetPortStatus:
		usbip_dbg_vhci_rh(" GetPortStatus port %x\n", wIndex);
		if (invalid_rhport) {
			pr_err("invalid port number %d\n", wIndex);
			retval = -EPIPE;
			goto error;
		}

		/* we do not care about resume. */

		/* whoever resets or resumes must GetPortStatus to
		 * complete it!!
		 */
		if (vhci_hcd->resuming && time_after(jiffies, vhci_hcd->re_timeout)) {
			vhci_hcd->port_status[rhport] |= (1 << USB_PORT_FEAT_C_SUSPEND);
			vhci_hcd->port_status[rhport] &= ~(1 << USB_PORT_FEAT_SUSPEND);
			vhci_hcd->resuming = 0;
			vhci_hcd->re_timeout = 0;
		}

		if ((vhci_hcd->port_status[rhport] & (1 << USB_PORT_FEAT_RESET)) !=
		    0 && time_after(jiffies, vhci_hcd->re_timeout)) {
			vhci_hcd->port_status[rhport] |= (1 << USB_PORT_FEAT_C_RESET);
			vhci_hcd->port_status[rhport] &= ~(1 << USB_PORT_FEAT_RESET);
			vhci_hcd->re_timeout = 0;

			/*
			 * A few drivers do usb reset during probe when
			 * the device could be in VDEV_ST_USED state
			 */
			if (vhci_hcd->vdev[rhport].ud.status ==
				VDEV_ST_NOTASSIGNED ||
			    vhci_hcd->vdev[rhport].ud.status ==
				VDEV_ST_USED) {
				usbip_dbg_vhci_rh(
					" enable rhport %d (status %u)\n",
					rhport,
					vhci_hcd->vdev[rhport].ud.status);
				vhci_hcd->port_status[rhport] |=
					USB_PORT_STAT_ENABLE;
			}

			if (hcd->speed < HCD_USB3) {
				switch (vhci_hcd->vdev[rhport].speed) {
				case USB_SPEED_HIGH:
					vhci_hcd->port_status[rhport] |=
					      USB_PORT_STAT_HIGH_SPEED;
					break;
				case USB_SPEED_LOW:
					vhci_hcd->port_status[rhport] |=
						USB_PORT_STAT_LOW_SPEED;
					break;
				default:
					pr_err("vhci_device speed not set\n");
					break;
				}
			}
		}
		((__le16 *) buf)[0] = cpu_to_le16(vhci_hcd->port_status[rhport]);
		((__le16 *) buf)[1] =
			cpu_to_le16(vhci_hcd->port_status[rhport] >> 16);

		usbip_dbg_vhci_rh(" GetPortStatus bye %x %x\n", ((u16 *)buf)[0],
				  ((u16 *)buf)[1]);
		break;
	case SetHubFeature:
		usbip_dbg_vhci_rh(" SetHubFeature\n");
		retval = -EPIPE;
		break;
	case SetPortFeature:
		switch (wValue) {
		case USB_PORT_FEAT_LINK_STATE:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_LINK_STATE\n");
			if (hcd->speed != HCD_USB3) {
				pr_err("USB_PORT_FEAT_LINK_STATE req not "
				       "supported for USB 2.0 roothub\n");
				goto error;
			}
			/*
			 * Since this is dummy we don't have an actual link so
			 * there is nothing to do for the SET_LINK_STATE cmd
			 */
			break;
		case USB_PORT_FEAT_U1_TIMEOUT:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_U1_TIMEOUT\n");
			fallthrough;
		case USB_PORT_FEAT_U2_TIMEOUT:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_U2_TIMEOUT\n");
			/* TODO: add suspend/resume support! */
			if (hcd->speed != HCD_USB3) {
				pr_err("USB_PORT_FEAT_U1/2_TIMEOUT req not "
				       "supported for USB 2.0 roothub\n");
				goto error;
			}
			break;
		case USB_PORT_FEAT_SUSPEND:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_SUSPEND\n");
			/* Applicable only for USB2.0 hub */
			if (hcd->speed == HCD_USB3) {
				pr_err("USB_PORT_FEAT_SUSPEND req not "
				       "supported for USB 3.0 roothub\n");
				goto error;
			}

			if (invalid_rhport) {
				pr_err("invalid port number %d\n", wIndex);
				goto error;
			}

			vhci_hcd->port_status[rhport] |= USB_PORT_STAT_SUSPEND;
			break;
		case USB_PORT_FEAT_POWER:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_POWER\n");
			if (invalid_rhport) {
				pr_err("invalid port number %d\n", wIndex);
				goto error;
			}
			if (hcd->speed == HCD_USB3)
				vhci_hcd->port_status[rhport] |= USB_SS_PORT_STAT_POWER;
			else
				vhci_hcd->port_status[rhport] |= USB_PORT_STAT_POWER;
			break;
		case USB_PORT_FEAT_BH_PORT_RESET:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_BH_PORT_RESET\n");
			if (invalid_rhport) {
				pr_err("invalid port number %d\n", wIndex);
				goto error;
			}
			/* Applicable only for USB3.0 hub */
			if (hcd->speed != HCD_USB3) {
				pr_err("USB_PORT_FEAT_BH_PORT_RESET req not "
				       "supported for USB 2.0 roothub\n");
				goto error;
			}
			fallthrough;
		case USB_PORT_FEAT_RESET:
			usbip_dbg_vhci_rh(
				" SetPortFeature: USB_PORT_FEAT_RESET\n");
			if (invalid_rhport) {
				pr_err("invalid port number %d\n", wIndex);
				goto error;
			}
			/* if it's already enabled, disable */
			if (hcd->speed == HCD_USB3) {
				vhci_hcd->port_status[rhport] = 0;
				vhci_hcd->port_status[rhport] =
					(USB_SS_PORT_STAT_POWER |
					 USB_PORT_STAT_CONNECTION |
					 USB_PORT_STAT_RESET);
			} else if (vhci_hcd->port_status[rhport] & USB_PORT_STAT_ENABLE) {
				vhci_hcd->port_status[rhport] &= ~(USB_PORT_STAT_ENABLE
					| USB_PORT_STAT_LOW_SPEED
					| USB_PORT_STAT_HIGH_SPEED);
			}

			/* 50msec reset signaling */
			vhci_hcd->re_timeout = jiffies + msecs_to_jiffies(50);
			fallthrough;
		default:
			usbip_dbg_vhci_rh(" SetPortFeature: default %d\n",
					  wValue);
			if (invalid_rhport) {
				pr_err("invalid port number %d\n", wIndex);
				goto error;
			}
			if (wValue >= 32)
				goto error;
			if (hcd->speed == HCD_USB3) {
				if ((vhci_hcd->port_status[rhport] &
				     USB_SS_PORT_STAT_POWER) != 0) {
					vhci_hcd->port_status[rhport] |= (1 << wValue);
				}
			} else
				if ((vhci_hcd->port_status[rhport] &
				     USB_PORT_STAT_POWER) != 0) {
					vhci_hcd->port_status[rhport] |= (1 << wValue);
				}
		}
		break;
	case GetPortErrorCount:
		usbip_dbg_vhci_rh(" GetPortErrorCount\n");
		if (hcd->speed != HCD_USB3) {
			pr_err("GetPortErrorCount req not "
			       "supported for USB 2.0 roothub\n");
			goto error;
		}
		/* We'll always return 0 since this is a dummy hub */
		*(__le32 *) buf = cpu_to_le32(0);
		break;
	case SetHubDepth:
		usbip_dbg_vhci_rh(" SetHubDepth\n");
		if (hcd->speed != HCD_USB3) {
			pr_err("SetHubDepth req not supported for "
			       "USB 2.0 roothub\n");
			goto error;
		}
		break;
	default:
		pr_err("default hub control req: %04x v%04x i%04x l%d\n",
			typeReq, wValue, wIndex, wLength);
error:
		/* "protocol stall" on error */
		retval = -EPIPE;
	}

	if (usbip_dbg_flag_vhci_rh) {
		pr_debug("port %d\n", rhport);
		/* Only dump valid port status */
		if (!invalid_rhport) {
			dump_port_status_diff(prev_port_status[rhport],
					      vhci_hcd->port_status[rhport],
					      hcd->speed == HCD_USB3);
		}
	}
	usbip_dbg_vhci_rh(" bye\n");

	spin_unlock_irqrestore(&vhci->lock, flags);

	if (!invalid_rhport &&
	    (vhci_hcd->port_status[rhport] & PORT_C_MASK) != 0) {
		usb_hcd_poll_rh_status(hcd);
	}

	return retval;
}

static void vhci_tx_urb(struct urb *urb, struct vhci_device *vdev)
{
	struct vhci_priv *priv;
	struct vhci_hcd *vhci_hcd = vdev_to_vhci_hcd(vdev);
	unsigned long flags;

	priv = kzalloc(sizeof(struct vhci_priv), GFP_ATOMIC);
	if (!priv) {
		usbip_event_add(&vdev->ud, VDEV_EVENT_ERROR_MALLOC);
		return;
	}

	spin_lock_irqsave(&vdev->priv_lock, flags);

	priv->seqnum = atomic_inc_return(&vhci_hcd->seqnum);
	if (priv->seqnum == 0xffff)
		dev_info(&urb->dev->dev, "seqnum max\n");

	priv->vdev = vdev;
	priv->urb = urb;

	urb->hcpriv = (void *) priv;

	list_add_tail(&priv->list, &vdev->priv_tx);

	wake_up(&vdev->waitq_tx);
	spin_unlock_irqrestore(&vdev->priv_lock, flags);
}

static int vhci_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	struct vhci_hcd *vhci_hcd = hcd_to_vhci_hcd(hcd);
	struct vhci *vhci = vhci_hcd->vhci;
	struct device *dev = &urb->dev->dev;
	u8 portnum = urb->dev->portnum;
	int ret = 0;
	struct vhci_device *vdev;
	unsigned long flags;

	if (portnum > VHCI_HC_PORTS) {
		pr_err("invalid port number %d\n", portnum);
		return -ENODEV;
	}
	vdev = &vhci_hcd->vdev[portnum-1];

	if (!urb->transfer_buffer && !urb->num_sgs &&
	     urb->transfer_buffer_length) {
		dev_dbg(dev, "Null URB transfer buffer\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&vhci->lock, flags);

	if (urb->status != -EINPROGRESS) {
		dev_err(dev, "URB already unlinked!, status %d\n", urb->status);
		spin_unlock_irqrestore(&vhci->lock, flags);
		return urb->status;
	}

	/* refuse enqueue for dead connection */
	spin_lock(&vdev->ud.lock);
	if (vdev->ud.status == VDEV_ST_NULL ||
	    vdev->ud.status == VDEV_ST_ERROR) {
		dev_err(dev, "enqueue for inactive port %d\n", vdev->rhport);
		spin_unlock(&vdev->ud.lock);
		spin_unlock_irqrestore(&vhci->lock, flags);
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
				usbip_dbg_vhci_hc(
					"Not yet?:Get_Descriptor to device 0 (get max pipe size)\n");

			usb_put_dev(vdev->udev);
			vdev->udev = usb_get_dev(urb->dev);
			goto out;

		default:
			/* NOT REACHED */
			dev_err(dev,
				"invalid request to devnum 0 bRequest %u, wValue %u\n",
				ctrlreq->bRequest,
				ctrlreq->wValue);
			ret =  -EINVAL;
			goto no_need_xmit;
		}

	}

out:
	vhci_tx_urb(urb, vdev);
	spin_unlock_irqrestore(&vhci->lock, flags);

	return 0;

no_need_xmit:
	usb_hcd_unlink_urb_from_ep(hcd, urb);
no_need_unlink:
	spin_unlock_irqrestore(&vhci->lock, flags);
	if (!ret) {
		/* usb_hcd_giveback_urb() should be called with
		 * irqs disabled
		 */
		local_irq_disable();
		usb_hcd_giveback_urb(hcd, urb, urb->status);
		local_irq_enable();
	}
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
	struct vhci_hcd *vhci_hcd = hcd_to_vhci_hcd(hcd);
	struct vhci *vhci = vhci_hcd->vhci;
	struct vhci_priv *priv;
	struct vhci_device *vdev;
	unsigned long flags;

	spin_lock_irqsave(&vhci->lock, flags);

	priv = urb->hcpriv;
	if (!priv) {
		/* URB was never linked! or will be soon given back by
		 * vhci_rx. */
		spin_unlock_irqrestore(&vhci->lock, flags);
		return -EIDRM;
	}

	{
		int ret = 0;

		ret = usb_hcd_check_unlink_urb(hcd, urb, status);
		if (ret) {
			spin_unlock_irqrestore(&vhci->lock, flags);
			return ret;
		}
	}

	 /* send unlink request here? */
	vdev = priv->vdev;

	if (!vdev->ud.tcp_socket) {
		/* tcp connection is closed */
		spin_lock(&vdev->priv_lock);

		list_del(&priv->list);
		kfree(priv);
		urb->hcpriv = NULL;

		spin_unlock(&vdev->priv_lock);

		/*
		 * If tcp connection is alive, we have sent CMD_UNLINK.
		 * vhci_rx will receive RET_UNLINK and give back the URB.
		 * Otherwise, we give back it here.
		 */
		usb_hcd_unlink_urb_from_ep(hcd, urb);

		spin_unlock_irqrestore(&vhci->lock, flags);
		usb_hcd_giveback_urb(hcd, urb, urb->status);
		spin_lock_irqsave(&vhci->lock, flags);

	} else {
		/* tcp connection is alive */
		struct vhci_unlink *unlink;

		spin_lock(&vdev->priv_lock);

		/* setup CMD_UNLINK pdu */
		unlink = kzalloc(sizeof(struct vhci_unlink), GFP_ATOMIC);
		if (!unlink) {
			spin_unlock(&vdev->priv_lock);
			spin_unlock_irqrestore(&vhci->lock, flags);
			usbip_event_add(&vdev->ud, VDEV_EVENT_ERROR_MALLOC);
			return -ENOMEM;
		}

		unlink->seqnum = atomic_inc_return(&vhci_hcd->seqnum);
		if (unlink->seqnum == 0xffff)
			pr_info("seqnum max\n");

		unlink->unlink_seqnum = priv->seqnum;

		/* send cmd_unlink and try to cancel the pending URB in the
		 * peer */
		list_add_tail(&unlink->list, &vdev->unlink_tx);
		wake_up(&vdev->waitq_tx);

		spin_unlock(&vdev->priv_lock);
	}

	spin_unlock_irqrestore(&vhci->lock, flags);

	usbip_dbg_vhci_hc("leave\n");
	return 0;
}

static void vhci_cleanup_unlink_list(struct vhci_device *vdev,
		struct list_head *unlink_list)
{
	struct vhci_hcd *vhci_hcd = vdev_to_vhci_hcd(vdev);
	struct usb_hcd *hcd = vhci_hcd_to_hcd(vhci_hcd);
	struct vhci *vhci = vhci_hcd->vhci;
	struct vhci_unlink *unlink, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&vhci->lock, flags);
	spin_lock(&vdev->priv_lock);

	list_for_each_entry_safe(unlink, tmp, unlink_list, list) {
		struct urb *urb;

		urb = pickup_urb_and_free_priv(vdev, unlink->unlink_seqnum);
		if (!urb) {
			list_del(&unlink->list);
			kfree(unlink);
			continue;
		}

		urb->status = -ENODEV;

		usb_hcd_unlink_urb_from_ep(hcd, urb);

		list_del(&unlink->list);

		spin_unlock(&vdev->priv_lock);
		spin_unlock_irqrestore(&vhci->lock, flags);

		usb_hcd_giveback_urb(hcd, urb, urb->status);

		spin_lock_irqsave(&vhci->lock, flags);
		spin_lock(&vdev->priv_lock);

		kfree(unlink);
	}

	spin_unlock(&vdev->priv_lock);
	spin_unlock_irqrestore(&vhci->lock, flags);
}

static void vhci_device_unlink_cleanup(struct vhci_device *vdev)
{
	/* give back URB of unsent unlink request */
	vhci_cleanup_unlink_list(vdev, &vdev->unlink_tx);

	/* give back URB of unanswered unlink request */
	vhci_cleanup_unlink_list(vdev, &vdev->unlink_rx);
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
		pr_debug("shutdown tcp_socket %d\n", ud->sockfd);
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
		sockfd_put(vdev->ud.tcp_socket);
		vdev->ud.tcp_socket = NULL;
		vdev->ud.sockfd = -1;
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
	rh_port_disconnect(vdev);

	pr_info("disconnect device\n");
}

static void vhci_device_reset(struct usbip_device *ud)
{
	struct vhci_device *vdev = container_of(ud, struct vhci_device, ud);
	unsigned long flags;

	spin_lock_irqsave(&ud->lock, flags);

	vdev->speed  = 0;
	vdev->devid  = 0;

	usb_put_dev(vdev->udev);
	vdev->udev = NULL;

	if (ud->tcp_socket) {
		sockfd_put(ud->tcp_socket);
		ud->tcp_socket = NULL;
		ud->sockfd = -1;
	}
	ud->status = VDEV_ST_NULL;

	spin_unlock_irqrestore(&ud->lock, flags);
}

static void vhci_device_unusable(struct usbip_device *ud)
{
	unsigned long flags;

	spin_lock_irqsave(&ud->lock, flags);
	ud->status = VDEV_ST_ERROR;
	spin_unlock_irqrestore(&ud->lock, flags);
}

static void vhci_device_init(struct vhci_device *vdev)
{
	memset(vdev, 0, sizeof(struct vhci_device));

	vdev->ud.side   = USBIP_VHCI;
	vdev->ud.status = VDEV_ST_NULL;
	spin_lock_init(&vdev->ud.lock);
	mutex_init(&vdev->ud.sysfs_lock);

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

static int hcd_name_to_id(const char *name)
{
	char *c;
	long val;
	int ret;

	c = strchr(name, '.');
	if (c == NULL)
		return 0;

	ret = kstrtol(c+1, 10, &val);
	if (ret < 0)
		return ret;

	return val;
}

static int vhci_setup(struct usb_hcd *hcd)
{
	struct vhci *vhci = *((void **)dev_get_platdata(hcd->self.controller));

	if (usb_hcd_is_primary_hcd(hcd)) {
		vhci->vhci_hcd_hs = hcd_to_vhci_hcd(hcd);
		vhci->vhci_hcd_hs->vhci = vhci;
		/*
		 * Mark the first roothub as being USB 2.0.
		 * The USB 3.0 roothub will be registered later by
		 * vhci_hcd_probe()
		 */
		hcd->speed = HCD_USB2;
		hcd->self.root_hub->speed = USB_SPEED_HIGH;
	} else {
		vhci->vhci_hcd_ss = hcd_to_vhci_hcd(hcd);
		vhci->vhci_hcd_ss->vhci = vhci;
		hcd->speed = HCD_USB3;
		hcd->self.root_hub->speed = USB_SPEED_SUPER;
	}

	/*
	 * Support SG.
	 * sg_tablesize is an arbitrary value to alleviate memory pressure
	 * on the host.
	 */
	hcd->self.sg_tablesize = 32;
	hcd->self.no_sg_constraint = 1;

	return 0;
}

static int vhci_start(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci_hcd = hcd_to_vhci_hcd(hcd);
	int id, rhport;
	int err;

	usbip_dbg_vhci_hc("enter vhci_start\n");

	if (usb_hcd_is_primary_hcd(hcd))
		spin_lock_init(&vhci_hcd->vhci->lock);

	/* initialize private data of usb_hcd */

	for (rhport = 0; rhport < VHCI_HC_PORTS; rhport++) {
		struct vhci_device *vdev = &vhci_hcd->vdev[rhport];

		vhci_device_init(vdev);
		vdev->rhport = rhport;
	}

	atomic_set(&vhci_hcd->seqnum, 0);

	hcd->power_budget = 0; /* no limit */
	hcd->uses_new_polling = 1;

#ifdef CONFIG_USB_OTG
	hcd->self.otg_port = 1;
#endif

	id = hcd_name_to_id(hcd_name(hcd));
	if (id < 0) {
		pr_err("invalid vhci name %s\n", hcd_name(hcd));
		return -EINVAL;
	}

	/* vhci_hcd is now ready to be controlled through sysfs */
	if (id == 0 && usb_hcd_is_primary_hcd(hcd)) {
		err = vhci_init_attr_group();
		if (err) {
			dev_err(hcd_dev(hcd), "init attr group failed, err = %d\n", err);
			return err;
		}
		err = sysfs_create_group(&hcd_dev(hcd)->kobj, &vhci_attr_group);
		if (err) {
			dev_err(hcd_dev(hcd), "create sysfs files failed, err = %d\n", err);
			vhci_finish_attr_group();
			return err;
		}
		pr_info("created sysfs %s\n", hcd_name(hcd));
	}

	return 0;
}

static void vhci_stop(struct usb_hcd *hcd)
{
	struct vhci_hcd *vhci_hcd = hcd_to_vhci_hcd(hcd);
	int id, rhport;

	usbip_dbg_vhci_hc("stop VHCI controller\n");

	/* 1. remove the userland interface of vhci_hcd */
	id = hcd_name_to_id(hcd_name(hcd));
	if (id == 0 && usb_hcd_is_primary_hcd(hcd)) {
		sysfs_remove_group(&hcd_dev(hcd)->kobj, &vhci_attr_group);
		vhci_finish_attr_group();
	}

	/* 2. shutdown all the ports of vhci_hcd */
	for (rhport = 0; rhport < VHCI_HC_PORTS; rhport++) {
		struct vhci_device *vdev = &vhci_hcd->vdev[rhport];

		usbip_event_add(&vdev->ud, VDEV_EVENT_REMOVED);
		usbip_stop_eh(&vdev->ud);
	}
}

static int vhci_get_frame_number(struct usb_hcd *hcd)
{
	dev_err_ratelimited(&hcd->self.root_hub->dev, "Not yet implemented\n");
	return 0;
}

#ifdef CONFIG_PM

/* FIXME: suspend/resume */
static int vhci_bus_suspend(struct usb_hcd *hcd)
{
	struct vhci *vhci = *((void **)dev_get_platdata(hcd->self.controller));
	unsigned long flags;

	dev_dbg(&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock_irqsave(&vhci->lock, flags);
	hcd->state = HC_STATE_SUSPENDED;
	spin_unlock_irqrestore(&vhci->lock, flags);

	return 0;
}

static int vhci_bus_resume(struct usb_hcd *hcd)
{
	struct vhci *vhci = *((void **)dev_get_platdata(hcd->self.controller));
	int rc = 0;
	unsigned long flags;

	dev_dbg(&hcd->self.root_hub->dev, "%s\n", __func__);

	spin_lock_irqsave(&vhci->lock, flags);
	if (!HCD_HW_ACCESSIBLE(hcd))
		rc = -ESHUTDOWN;
	else
		hcd->state = HC_STATE_RUNNING;
	spin_unlock_irqrestore(&vhci->lock, flags);

	return rc;
}

#else

#define vhci_bus_suspend      NULL
#define vhci_bus_resume       NULL
#endif

/* Change a group of bulk endpoints to support multiple stream IDs */
static int vhci_alloc_streams(struct usb_hcd *hcd, struct usb_device *udev,
	struct usb_host_endpoint **eps, unsigned int num_eps,
	unsigned int num_streams, gfp_t mem_flags)
{
	dev_dbg(&hcd->self.root_hub->dev, "vhci_alloc_streams not implemented\n");
	return 0;
}

/* Reverts a group of bulk endpoints back to not using stream IDs. */
static int vhci_free_streams(struct usb_hcd *hcd, struct usb_device *udev,
	struct usb_host_endpoint **eps, unsigned int num_eps,
	gfp_t mem_flags)
{
	dev_dbg(&hcd->self.root_hub->dev, "vhci_free_streams not implemented\n");
	return 0;
}

static const struct hc_driver vhci_hc_driver = {
	.description	= driver_name,
	.product_desc	= driver_desc,
	.hcd_priv_size	= sizeof(struct vhci_hcd),

	.flags		= HCD_USB3 | HCD_SHARED,

	.reset		= vhci_setup,
	.start		= vhci_start,
	.stop		= vhci_stop,

	.urb_enqueue	= vhci_urb_enqueue,
	.urb_dequeue	= vhci_urb_dequeue,

	.get_frame_number = vhci_get_frame_number,

	.hub_status_data = vhci_hub_status,
	.hub_control    = vhci_hub_control,
	.bus_suspend	= vhci_bus_suspend,
	.bus_resume	= vhci_bus_resume,

	.alloc_streams	= vhci_alloc_streams,
	.free_streams	= vhci_free_streams,
};

static int vhci_hcd_probe(struct platform_device *pdev)
{
	struct vhci             *vhci = *((void **)dev_get_platdata(&pdev->dev));
	struct usb_hcd		*hcd_hs;
	struct usb_hcd		*hcd_ss;
	int			ret;

	usbip_dbg_vhci_hc("name %s id %d\n", pdev->name, pdev->id);

	/*
	 * Allocate and initialize hcd.
	 * Our private data is also allocated automatically.
	 */
	hcd_hs = usb_create_hcd(&vhci_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd_hs) {
		pr_err("create primary hcd failed\n");
		return -ENOMEM;
	}
	hcd_hs->has_tt = 1;

	/*
	 * Finish generic HCD structure initialization and register.
	 * Call the driver's reset() and start() routines.
	 */
	ret = usb_add_hcd(hcd_hs, 0, 0);
	if (ret != 0) {
		pr_err("usb_add_hcd hs failed %d\n", ret);
		goto put_usb2_hcd;
	}

	hcd_ss = usb_create_shared_hcd(&vhci_hc_driver, &pdev->dev,
				       dev_name(&pdev->dev), hcd_hs);
	if (!hcd_ss) {
		ret = -ENOMEM;
		pr_err("create shared hcd failed\n");
		goto remove_usb2_hcd;
	}

	ret = usb_add_hcd(hcd_ss, 0, 0);
	if (ret) {
		pr_err("usb_add_hcd ss failed %d\n", ret);
		goto put_usb3_hcd;
	}

	usbip_dbg_vhci_hc("bye\n");
	return 0;

put_usb3_hcd:
	usb_put_hcd(hcd_ss);
remove_usb2_hcd:
	usb_remove_hcd(hcd_hs);
put_usb2_hcd:
	usb_put_hcd(hcd_hs);
	vhci->vhci_hcd_hs = NULL;
	vhci->vhci_hcd_ss = NULL;
	return ret;
}

static void vhci_hcd_remove(struct platform_device *pdev)
{
	struct vhci *vhci = *((void **)dev_get_platdata(&pdev->dev));

	/*
	 * Disconnects the root hub,
	 * then reverses the effects of usb_add_hcd(),
	 * invoking the HCD's stop() methods.
	 */
	usb_remove_hcd(vhci_hcd_to_hcd(vhci->vhci_hcd_ss));
	usb_put_hcd(vhci_hcd_to_hcd(vhci->vhci_hcd_ss));

	usb_remove_hcd(vhci_hcd_to_hcd(vhci->vhci_hcd_hs));
	usb_put_hcd(vhci_hcd_to_hcd(vhci->vhci_hcd_hs));

	vhci->vhci_hcd_hs = NULL;
	vhci->vhci_hcd_ss = NULL;
}

#ifdef CONFIG_PM

/* what should happen for USB/IP under suspend/resume? */
static int vhci_hcd_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd;
	struct vhci *vhci;
	int rhport;
	int connected = 0;
	int ret = 0;
	unsigned long flags;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	hcd = platform_get_drvdata(pdev);
	if (!hcd)
		return 0;

	vhci = *((void **)dev_get_platdata(hcd->self.controller));

	spin_lock_irqsave(&vhci->lock, flags);

	for (rhport = 0; rhport < VHCI_HC_PORTS; rhport++) {
		if (vhci->vhci_hcd_hs->port_status[rhport] &
		    USB_PORT_STAT_CONNECTION)
			connected += 1;

		if (vhci->vhci_hcd_ss->port_status[rhport] &
		    USB_PORT_STAT_CONNECTION)
			connected += 1;
	}

	spin_unlock_irqrestore(&vhci->lock, flags);

	if (connected > 0) {
		dev_info(&pdev->dev,
			 "We have %d active connection%s. Do not suspend.\n",
			 connected, (connected == 1 ? "" : "s"));
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
	if (!hcd)
		return 0;
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
	.remove_new = vhci_hcd_remove,
	.suspend = vhci_hcd_suspend,
	.resume	= vhci_hcd_resume,
	.driver	= {
		.name = driver_name,
	},
};

static void del_platform_devices(void)
{
	int i;

	for (i = 0; i < vhci_num_controllers; i++) {
		platform_device_unregister(vhcis[i].pdev);
		vhcis[i].pdev = NULL;
	}
	sysfs_remove_link(&platform_bus.kobj, driver_name);
}

static int __init vhci_hcd_init(void)
{
	int i, ret;

	if (usb_disabled())
		return -ENODEV;

	if (vhci_num_controllers < 1)
		vhci_num_controllers = 1;

	vhcis = kcalloc(vhci_num_controllers, sizeof(struct vhci), GFP_KERNEL);
	if (vhcis == NULL)
		return -ENOMEM;

	ret = platform_driver_register(&vhci_driver);
	if (ret)
		goto err_driver_register;

	for (i = 0; i < vhci_num_controllers; i++) {
		void *vhci = &vhcis[i];
		struct platform_device_info pdevinfo = {
			.name = driver_name,
			.id = i,
			.data = &vhci,
			.size_data = sizeof(void *),
		};

		vhcis[i].pdev = platform_device_register_full(&pdevinfo);
		ret = PTR_ERR_OR_ZERO(vhcis[i].pdev);
		if (ret < 0) {
			while (i--)
				platform_device_unregister(vhcis[i].pdev);
			goto err_add_hcd;
		}
	}

	return 0;

err_add_hcd:
	platform_driver_unregister(&vhci_driver);
err_driver_register:
	kfree(vhcis);
	return ret;
}

static void __exit vhci_hcd_exit(void)
{
	del_platform_devices();
	platform_driver_unregister(&vhci_driver);
	kfree(vhcis);
}

module_init(vhci_hcd_init);
module_exit(vhci_hcd_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
