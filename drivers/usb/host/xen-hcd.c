// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xen-hcd.c
 *
 * Xen USB Virtual Host Controller driver
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/list.h>
#include <linux/usb/hcd.h>
#include <linux/io.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/events.h>
#include <xen/page.h>

#include <xen/interface/io/usbif.h>

/* Private per-URB data */
struct urb_priv {
	struct list_head list;
	struct urb *urb;
	int req_id;		/* RING_REQUEST id for submitting */
	int unlink_req_id;	/* RING_REQUEST id for unlinking */
	int status;
	bool unlinked;		/* dequeued marker */
};

/* virtual roothub port status */
struct rhport_status {
	__u32 status;
	bool resuming;		/* in resuming */
	bool c_connection;	/* connection changed */
	unsigned long timeout;
};

/* status of attached device */
struct vdevice_status {
	int devnum;
	enum usb_device_state status;
	enum usb_device_speed speed;
};

/* RING request shadow */
struct usb_shadow {
	struct xenusb_urb_request req;
	struct urb *urb;
	bool in_flight;
};

struct xenhcd_info {
	/* Virtual Host Controller has 4 urb queues */
	struct list_head pending_submit_list;
	struct list_head pending_unlink_list;
	struct list_head in_progress_list;
	struct list_head giveback_waiting_list;

	spinlock_t lock;

	/* timer that kick pending and giveback waiting urbs */
	struct timer_list watchdog;
	unsigned long actions;

	/* virtual root hub */
	int rh_numports;
	struct rhport_status ports[XENUSB_MAX_PORTNR];
	struct vdevice_status devices[XENUSB_MAX_PORTNR];

	/* Xen related staff */
	struct xenbus_device *xbdev;
	int urb_ring_ref;
	int conn_ring_ref;
	struct xenusb_urb_front_ring urb_ring;
	struct xenusb_conn_front_ring conn_ring;

	unsigned int evtchn;
	unsigned int irq;
	struct usb_shadow shadow[XENUSB_URB_RING_SIZE];
	unsigned int shadow_free;

	bool error;
};

#define GRANT_INVALID_REF 0

#define XENHCD_RING_JIFFIES (HZ/200)
#define XENHCD_SCAN_JIFFIES 1

enum xenhcd_timer_action {
	TIMER_RING_WATCHDOG,
	TIMER_SCAN_PENDING_URBS,
};

static struct kmem_cache *xenhcd_urbp_cachep;

static inline struct xenhcd_info *xenhcd_hcd_to_info(struct usb_hcd *hcd)
{
	return (struct xenhcd_info *)hcd->hcd_priv;
}

static inline struct usb_hcd *xenhcd_info_to_hcd(struct xenhcd_info *info)
{
	return container_of((void *)info, struct usb_hcd, hcd_priv);
}

static void xenhcd_set_error(struct xenhcd_info *info, const char *msg)
{
	info->error = true;

	pr_alert("xen-hcd: protocol error: %s!\n", msg);
}

static inline void xenhcd_timer_action_done(struct xenhcd_info *info,
					    enum xenhcd_timer_action action)
{
	clear_bit(action, &info->actions);
}

static void xenhcd_timer_action(struct xenhcd_info *info,
				enum xenhcd_timer_action action)
{
	if (timer_pending(&info->watchdog) &&
	    test_bit(TIMER_SCAN_PENDING_URBS, &info->actions))
		return;

	if (!test_and_set_bit(action, &info->actions)) {
		unsigned long t;

		switch (action) {
		case TIMER_RING_WATCHDOG:
			t = XENHCD_RING_JIFFIES;
			break;
		default:
			t = XENHCD_SCAN_JIFFIES;
			break;
		}
		mod_timer(&info->watchdog, t + jiffies);
	}
}

/*
 * set virtual port connection status
 */
static void xenhcd_set_connect_state(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if (info->ports[port].status & USB_PORT_STAT_POWER) {
		switch (info->devices[port].speed) {
		case XENUSB_SPEED_NONE:
			info->ports[port].status &=
				~(USB_PORT_STAT_CONNECTION |
				  USB_PORT_STAT_ENABLE |
				  USB_PORT_STAT_LOW_SPEED |
				  USB_PORT_STAT_HIGH_SPEED |
				  USB_PORT_STAT_SUSPEND);
			break;
		case XENUSB_SPEED_LOW:
			info->ports[port].status |= USB_PORT_STAT_CONNECTION;
			info->ports[port].status |= USB_PORT_STAT_LOW_SPEED;
			break;
		case XENUSB_SPEED_FULL:
			info->ports[port].status |= USB_PORT_STAT_CONNECTION;
			break;
		case XENUSB_SPEED_HIGH:
			info->ports[port].status |= USB_PORT_STAT_CONNECTION;
			info->ports[port].status |= USB_PORT_STAT_HIGH_SPEED;
			break;
		default: /* error */
			return;
		}
		info->ports[port].status |= (USB_PORT_STAT_C_CONNECTION << 16);
	}
}

/*
 * set virtual device connection status
 */
static int xenhcd_rhport_connect(struct xenhcd_info *info, __u8 portnum,
				 __u8 speed)
{
	int port;

	if (portnum < 1 || portnum > info->rh_numports)
		return -EINVAL; /* invalid port number */

	port = portnum - 1;
	if (info->devices[port].speed != speed) {
		switch (speed) {
		case XENUSB_SPEED_NONE: /* disconnect */
			info->devices[port].status = USB_STATE_NOTATTACHED;
			break;
		case XENUSB_SPEED_LOW:
		case XENUSB_SPEED_FULL:
		case XENUSB_SPEED_HIGH:
			info->devices[port].status = USB_STATE_ATTACHED;
			break;
		default: /* error */
			return -EINVAL;
		}
		info->devices[port].speed = speed;
		info->ports[port].c_connection = true;

		xenhcd_set_connect_state(info, portnum);
	}

	return 0;
}

/*
 * SetPortFeature(PORT_SUSPENDED)
 */
static void xenhcd_rhport_suspend(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	info->ports[port].status |= USB_PORT_STAT_SUSPEND;
	info->devices[port].status = USB_STATE_SUSPENDED;
}

/*
 * ClearPortFeature(PORT_SUSPENDED)
 */
static void xenhcd_rhport_resume(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if (info->ports[port].status & USB_PORT_STAT_SUSPEND) {
		info->ports[port].resuming = true;
		info->ports[port].timeout = jiffies + msecs_to_jiffies(20);
	}
}

/*
 * SetPortFeature(PORT_POWER)
 */
static void xenhcd_rhport_power_on(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if ((info->ports[port].status & USB_PORT_STAT_POWER) == 0) {
		info->ports[port].status |= USB_PORT_STAT_POWER;
		if (info->devices[port].status != USB_STATE_NOTATTACHED)
			info->devices[port].status = USB_STATE_POWERED;
		if (info->ports[port].c_connection)
			xenhcd_set_connect_state(info, portnum);
	}
}

/*
 * ClearPortFeature(PORT_POWER)
 * SetConfiguration(non-zero)
 * Power_Source_Off
 * Over-current
 */
static void xenhcd_rhport_power_off(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if (info->ports[port].status & USB_PORT_STAT_POWER) {
		info->ports[port].status = 0;
		if (info->devices[port].status != USB_STATE_NOTATTACHED)
			info->devices[port].status = USB_STATE_ATTACHED;
	}
}

/*
 * ClearPortFeature(PORT_ENABLE)
 */
static void xenhcd_rhport_disable(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	info->ports[port].status &= ~USB_PORT_STAT_ENABLE;
	info->ports[port].status &= ~USB_PORT_STAT_SUSPEND;
	info->ports[port].resuming = false;
	if (info->devices[port].status != USB_STATE_NOTATTACHED)
		info->devices[port].status = USB_STATE_POWERED;
}

/*
 * SetPortFeature(PORT_RESET)
 */
static void xenhcd_rhport_reset(struct xenhcd_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	info->ports[port].status &= ~(USB_PORT_STAT_ENABLE |
				      USB_PORT_STAT_LOW_SPEED |
				      USB_PORT_STAT_HIGH_SPEED);
	info->ports[port].status |= USB_PORT_STAT_RESET;

	if (info->devices[port].status != USB_STATE_NOTATTACHED)
		info->devices[port].status = USB_STATE_ATTACHED;

	/* 10msec reset signaling */
	info->ports[port].timeout = jiffies + msecs_to_jiffies(10);
}

#ifdef CONFIG_PM
static int xenhcd_bus_suspend(struct usb_hcd *hcd)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);
	int ret = 0;
	int i, ports;

	ports = info->rh_numports;

	spin_lock_irq(&info->lock);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		ret = -ESHUTDOWN;
	} else {
		/* suspend any active ports*/
		for (i = 1; i <= ports; i++)
			xenhcd_rhport_suspend(info, i);
	}
	spin_unlock_irq(&info->lock);

	del_timer_sync(&info->watchdog);

	return ret;
}

static int xenhcd_bus_resume(struct usb_hcd *hcd)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);
	int ret = 0;
	int i, ports;

	ports = info->rh_numports;

	spin_lock_irq(&info->lock);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		ret = -ESHUTDOWN;
	} else {
		/* resume any suspended ports*/
		for (i = 1; i <= ports; i++)
			xenhcd_rhport_resume(info, i);
	}
	spin_unlock_irq(&info->lock);

	return ret;
}
#endif

static void xenhcd_hub_descriptor(struct xenhcd_info *info,
				  struct usb_hub_descriptor *desc)
{
	__u16 temp;
	int ports = info->rh_numports;

	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = 10; /* EHCI says 20ms max */
	desc->bHubContrCurrent = 0;
	desc->bNbrPorts = ports;

	/* size of DeviceRemovable and PortPwrCtrlMask fields */
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* bitmaps for DeviceRemovable and PortPwrCtrlMask */
	memset(&desc->u.hs.DeviceRemovable[0], 0, temp);
	memset(&desc->u.hs.DeviceRemovable[temp], 0xff, temp);

	/* per-port over current reporting and no power switching */
	temp = 0x000a;
	desc->wHubCharacteristics = cpu_to_le16(temp);
}

/* port status change mask for hub_status_data */
#define PORT_C_MASK	((USB_PORT_STAT_C_CONNECTION |		\
			  USB_PORT_STAT_C_ENABLE |		\
			  USB_PORT_STAT_C_SUSPEND |		\
			  USB_PORT_STAT_C_OVERCURRENT |		\
			  USB_PORT_STAT_C_RESET) << 16)

/*
 * See USB 2.0 Spec, 11.12.4 Hub and Port Status Change Bitmap.
 * If port status changed, writes the bitmap to buf and return
 * that length(number of bytes).
 * If Nothing changed, return 0.
 */
static int xenhcd_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);
	int ports;
	int i;
	unsigned long flags;
	int ret;
	int changed = 0;

	/* initialize the status to no-changes */
	ports = info->rh_numports;
	ret = 1 + (ports / 8);
	memset(buf, 0, ret);

	spin_lock_irqsave(&info->lock, flags);

	for (i = 0; i < ports; i++) {
		/* check status for each port */
		if (info->ports[i].status & PORT_C_MASK) {
			buf[(i + 1) / 8] |= 1 << (i + 1) % 8;
			changed = 1;
		}
	}

	if ((hcd->state == HC_STATE_SUSPENDED) && (changed == 1))
		usb_hcd_resume_root_hub(hcd);

	spin_unlock_irqrestore(&info->lock, flags);

	return changed ? ret : 0;
}

static int xenhcd_hub_control(struct usb_hcd *hcd, __u16 typeReq, __u16 wValue,
			      __u16 wIndex, char *buf, __u16 wLength)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);
	int ports = info->rh_numports;
	unsigned long flags;
	int ret = 0;
	int i;
	int changed = 0;

	spin_lock_irqsave(&info->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		/* ignore this request */
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			xenhcd_rhport_resume(info, wIndex);
			break;
		case USB_PORT_FEAT_POWER:
			xenhcd_rhport_power_off(info, wIndex);
			break;
		case USB_PORT_FEAT_ENABLE:
			xenhcd_rhport_disable(info, wIndex);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			info->ports[wIndex - 1].c_connection = false;
			fallthrough;
		default:
			info->ports[wIndex - 1].status &= ~(1 << wValue);
			break;
		}
		break;
	case GetHubDescriptor:
		xenhcd_hub_descriptor(info, (struct usb_hub_descriptor *)buf);
		break;
	case GetHubStatus:
		/* always local power supply good and no over-current exists. */
		*(__le32 *)buf = cpu_to_le32(0);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;

		wIndex--;

		/* resume completion */
		if (info->ports[wIndex].resuming &&
		    time_after_eq(jiffies, info->ports[wIndex].timeout)) {
			info->ports[wIndex].status |=
				USB_PORT_STAT_C_SUSPEND << 16;
			info->ports[wIndex].status &= ~USB_PORT_STAT_SUSPEND;
		}

		/* reset completion */
		if ((info->ports[wIndex].status & USB_PORT_STAT_RESET) != 0 &&
		    time_after_eq(jiffies, info->ports[wIndex].timeout)) {
			info->ports[wIndex].status |=
				USB_PORT_STAT_C_RESET << 16;
			info->ports[wIndex].status &= ~USB_PORT_STAT_RESET;

			if (info->devices[wIndex].status !=
			    USB_STATE_NOTATTACHED) {
				info->ports[wIndex].status |=
					USB_PORT_STAT_ENABLE;
				info->devices[wIndex].status =
					USB_STATE_DEFAULT;
			}

			switch (info->devices[wIndex].speed) {
			case XENUSB_SPEED_LOW:
				info->ports[wIndex].status |=
					USB_PORT_STAT_LOW_SPEED;
				break;
			case XENUSB_SPEED_HIGH:
				info->ports[wIndex].status |=
					USB_PORT_STAT_HIGH_SPEED;
				break;
			default:
				break;
			}
		}

		*(__le32 *)buf = cpu_to_le32(info->ports[wIndex].status);
		break;
	case SetPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;

		switch (wValue) {
		case USB_PORT_FEAT_POWER:
			xenhcd_rhport_power_on(info, wIndex);
			break;
		case USB_PORT_FEAT_RESET:
			xenhcd_rhport_reset(info, wIndex);
			break;
		case USB_PORT_FEAT_SUSPEND:
			xenhcd_rhport_suspend(info, wIndex);
			break;
		default:
			if (info->ports[wIndex-1].status & USB_PORT_STAT_POWER)
				info->ports[wIndex-1].status |= (1 << wValue);
		}
		break;

	case SetHubFeature:
		/* not supported */
	default:
error:
		ret = -EPIPE;
	}
	spin_unlock_irqrestore(&info->lock, flags);

	/* check status for each port */
	for (i = 0; i < ports; i++) {
		if (info->ports[i].status & PORT_C_MASK)
			changed = 1;
	}
	if (changed)
		usb_hcd_poll_rh_status(hcd);

	return ret;
}

static void xenhcd_free_urb_priv(struct urb_priv *urbp)
{
	urbp->urb->hcpriv = NULL;
	kmem_cache_free(xenhcd_urbp_cachep, urbp);
}

static inline unsigned int xenhcd_get_id_from_freelist(struct xenhcd_info *info)
{
	unsigned int free;

	free = info->shadow_free;
	info->shadow_free = info->shadow[free].req.id;
	info->shadow[free].req.id = 0x0fff; /* debug */
	return free;
}

static inline void xenhcd_add_id_to_freelist(struct xenhcd_info *info,
					     unsigned int id)
{
	info->shadow[id].req.id	= info->shadow_free;
	info->shadow[id].urb = NULL;
	info->shadow_free = id;
}

static inline int xenhcd_count_pages(void *addr, int length)
{
	unsigned long vaddr = (unsigned long)addr;

	return PFN_UP(vaddr + length) - PFN_DOWN(vaddr);
}

static void xenhcd_gnttab_map(struct xenhcd_info *info, void *addr, int length,
			      grant_ref_t *gref_head,
			      struct xenusb_request_segment *seg,
			      int nr_pages, int flags)
{
	grant_ref_t ref;
	unsigned int offset;
	unsigned int len = length;
	unsigned int bytes;
	int i;

	for (i = 0; i < nr_pages; i++) {
		offset = offset_in_page(addr);

		bytes = PAGE_SIZE - offset;
		if (bytes > len)
			bytes = len;

		ref = gnttab_claim_grant_reference(gref_head);
		gnttab_grant_foreign_access_ref(ref, info->xbdev->otherend_id,
						virt_to_gfn(addr), flags);
		seg[i].gref = ref;
		seg[i].offset = (__u16)offset;
		seg[i].length = (__u16)bytes;

		addr += bytes;
		len -= bytes;
	}
}

static __u32 xenhcd_pipe_urb_to_xenusb(__u32 urb_pipe, __u8 port)
{
	static __u32 pipe;

	pipe = usb_pipedevice(urb_pipe) << XENUSB_PIPE_DEV_SHIFT;
	pipe |= usb_pipeendpoint(urb_pipe) << XENUSB_PIPE_EP_SHIFT;
	if (usb_pipein(urb_pipe))
		pipe |= XENUSB_PIPE_DIR;
	switch (usb_pipetype(urb_pipe)) {
	case PIPE_ISOCHRONOUS:
		pipe |= XENUSB_PIPE_TYPE_ISOC << XENUSB_PIPE_TYPE_SHIFT;
		break;
	case PIPE_INTERRUPT:
		pipe |= XENUSB_PIPE_TYPE_INT << XENUSB_PIPE_TYPE_SHIFT;
		break;
	case PIPE_CONTROL:
		pipe |= XENUSB_PIPE_TYPE_CTRL << XENUSB_PIPE_TYPE_SHIFT;
		break;
	case PIPE_BULK:
		pipe |= XENUSB_PIPE_TYPE_BULK << XENUSB_PIPE_TYPE_SHIFT;
		break;
	}
	pipe = xenusb_setportnum_pipe(pipe, port);

	return pipe;
}

static int xenhcd_map_urb_for_request(struct xenhcd_info *info, struct urb *urb,
				      struct xenusb_urb_request *req)
{
	grant_ref_t gref_head;
	int nr_buff_pages = 0;
	int nr_isodesc_pages = 0;
	int nr_grants = 0;

	if (urb->transfer_buffer_length) {
		nr_buff_pages = xenhcd_count_pages(urb->transfer_buffer,
						urb->transfer_buffer_length);

		if (usb_pipeisoc(urb->pipe))
			nr_isodesc_pages = xenhcd_count_pages(
				&urb->iso_frame_desc[0],
				sizeof(struct usb_iso_packet_descriptor) *
				urb->number_of_packets);

		nr_grants = nr_buff_pages + nr_isodesc_pages;
		if (nr_grants > XENUSB_MAX_SEGMENTS_PER_REQUEST) {
			pr_err("xenhcd: error: %d grants\n", nr_grants);
			return -E2BIG;
		}

		if (gnttab_alloc_grant_references(nr_grants, &gref_head)) {
			pr_err("xenhcd: gnttab_alloc_grant_references() error\n");
			return -ENOMEM;
		}

		xenhcd_gnttab_map(info, urb->transfer_buffer,
				  urb->transfer_buffer_length, &gref_head,
				  &req->seg[0], nr_buff_pages,
				  usb_pipein(urb->pipe) ? 0 : GTF_readonly);
	}

	req->pipe = xenhcd_pipe_urb_to_xenusb(urb->pipe, urb->dev->portnum);
	req->transfer_flags = 0;
	if (urb->transfer_flags & URB_SHORT_NOT_OK)
		req->transfer_flags |= XENUSB_SHORT_NOT_OK;
	req->buffer_length = urb->transfer_buffer_length;
	req->nr_buffer_segs = nr_buff_pages;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		req->u.isoc.interval = urb->interval;
		req->u.isoc.start_frame = urb->start_frame;
		req->u.isoc.number_of_packets = urb->number_of_packets;
		req->u.isoc.nr_frame_desc_segs = nr_isodesc_pages;

		xenhcd_gnttab_map(info, &urb->iso_frame_desc[0],
				  sizeof(struct usb_iso_packet_descriptor) *
				  urb->number_of_packets,
				  &gref_head, &req->seg[nr_buff_pages],
				  nr_isodesc_pages, 0);
		break;
	case PIPE_INTERRUPT:
		req->u.intr.interval = urb->interval;
		break;
	case PIPE_CONTROL:
		if (urb->setup_packet)
			memcpy(req->u.ctrl, urb->setup_packet, 8);
		break;
	case PIPE_BULK:
		break;
	default:
		break;
	}

	if (nr_grants)
		gnttab_free_grant_references(gref_head);

	return 0;
}

static void xenhcd_gnttab_done(struct xenhcd_info *info, unsigned int id)
{
	struct usb_shadow *shadow = info->shadow + id;
	int nr_segs = 0;
	int i;

	if (!shadow->in_flight) {
		xenhcd_set_error(info, "Illegal request id");
		return;
	}
	shadow->in_flight = false;

	nr_segs = shadow->req.nr_buffer_segs;

	if (xenusb_pipeisoc(shadow->req.pipe))
		nr_segs += shadow->req.u.isoc.nr_frame_desc_segs;

	for (i = 0; i < nr_segs; i++) {
		if (!gnttab_try_end_foreign_access(shadow->req.seg[i].gref))
			xenhcd_set_error(info, "backend didn't release grant");
	}

	shadow->req.nr_buffer_segs = 0;
	shadow->req.u.isoc.nr_frame_desc_segs = 0;
}

static int xenhcd_translate_status(int status)
{
	switch (status) {
	case XENUSB_STATUS_OK:
		return 0;
	case XENUSB_STATUS_NODEV:
		return -ENODEV;
	case XENUSB_STATUS_INVAL:
		return -EINVAL;
	case XENUSB_STATUS_STALL:
		return -EPIPE;
	case XENUSB_STATUS_IOERROR:
		return -EPROTO;
	case XENUSB_STATUS_BABBLE:
		return -EOVERFLOW;
	default:
		return -ESHUTDOWN;
	}
}

static void xenhcd_giveback_urb(struct xenhcd_info *info, struct urb *urb,
				int status)
{
	struct urb_priv *urbp = (struct urb_priv *)urb->hcpriv;
	int priv_status = urbp->status;

	list_del_init(&urbp->list);
	xenhcd_free_urb_priv(urbp);

	if (urb->status == -EINPROGRESS)
		urb->status = xenhcd_translate_status(status);

	spin_unlock(&info->lock);
	usb_hcd_giveback_urb(xenhcd_info_to_hcd(info), urb,
			     priv_status <= 0 ? priv_status : urb->status);
	spin_lock(&info->lock);
}

static int xenhcd_do_request(struct xenhcd_info *info, struct urb_priv *urbp)
{
	struct xenusb_urb_request *req;
	struct urb *urb = urbp->urb;
	unsigned int id;
	int notify;
	int ret;

	id = xenhcd_get_id_from_freelist(info);
	req = &info->shadow[id].req;
	req->id = id;

	if (unlikely(urbp->unlinked)) {
		req->u.unlink.unlink_id = urbp->req_id;
		req->pipe = xenusb_setunlink_pipe(xenhcd_pipe_urb_to_xenusb(
						 urb->pipe, urb->dev->portnum));
		urbp->unlink_req_id = id;
	} else {
		ret = xenhcd_map_urb_for_request(info, urb, req);
		if (ret) {
			xenhcd_add_id_to_freelist(info, id);
			return ret;
		}
		urbp->req_id = id;
	}

	req = RING_GET_REQUEST(&info->urb_ring, info->urb_ring.req_prod_pvt);
	*req = info->shadow[id].req;

	info->urb_ring.req_prod_pvt++;
	info->shadow[id].urb = urb;
	info->shadow[id].in_flight = true;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->urb_ring, notify);
	if (notify)
		notify_remote_via_irq(info->irq);

	return 0;
}

static void xenhcd_kick_pending_urbs(struct xenhcd_info *info)
{
	struct urb_priv *urbp;

	while (!list_empty(&info->pending_submit_list)) {
		if (RING_FULL(&info->urb_ring)) {
			xenhcd_timer_action(info, TIMER_RING_WATCHDOG);
			return;
		}

		urbp = list_entry(info->pending_submit_list.next,
				  struct urb_priv, list);
		if (!xenhcd_do_request(info, urbp))
			list_move_tail(&urbp->list, &info->in_progress_list);
		else
			xenhcd_giveback_urb(info, urbp->urb, -ESHUTDOWN);
	}
	xenhcd_timer_action_done(info, TIMER_SCAN_PENDING_URBS);
}

/*
 * caller must lock info->lock
 */
static void xenhcd_cancel_all_enqueued_urbs(struct xenhcd_info *info)
{
	struct urb_priv *urbp, *tmp;
	int req_id;

	list_for_each_entry_safe(urbp, tmp, &info->in_progress_list, list) {
		req_id = urbp->req_id;
		if (!urbp->unlinked) {
			xenhcd_gnttab_done(info, req_id);
			if (info->error)
				return;
			if (urbp->urb->status == -EINPROGRESS)
				/* not dequeued */
				xenhcd_giveback_urb(info, urbp->urb,
						    -ESHUTDOWN);
			else	/* dequeued */
				xenhcd_giveback_urb(info, urbp->urb,
						    urbp->urb->status);
		}
		info->shadow[req_id].urb = NULL;
	}

	list_for_each_entry_safe(urbp, tmp, &info->pending_submit_list, list)
		xenhcd_giveback_urb(info, urbp->urb, -ESHUTDOWN);
}

/*
 * caller must lock info->lock
 */
static void xenhcd_giveback_unlinked_urbs(struct xenhcd_info *info)
{
	struct urb_priv *urbp, *tmp;

	list_for_each_entry_safe(urbp, tmp, &info->giveback_waiting_list, list)
		xenhcd_giveback_urb(info, urbp->urb, urbp->urb->status);
}

static int xenhcd_submit_urb(struct xenhcd_info *info, struct urb_priv *urbp)
{
	int ret;

	if (RING_FULL(&info->urb_ring)) {
		list_add_tail(&urbp->list, &info->pending_submit_list);
		xenhcd_timer_action(info, TIMER_RING_WATCHDOG);
		return 0;
	}

	if (!list_empty(&info->pending_submit_list)) {
		list_add_tail(&urbp->list, &info->pending_submit_list);
		xenhcd_timer_action(info, TIMER_SCAN_PENDING_URBS);
		return 0;
	}

	ret = xenhcd_do_request(info, urbp);
	if (ret == 0)
		list_add_tail(&urbp->list, &info->in_progress_list);

	return ret;
}

static int xenhcd_unlink_urb(struct xenhcd_info *info, struct urb_priv *urbp)
{
	int ret;

	/* already unlinked? */
	if (urbp->unlinked)
		return -EBUSY;

	urbp->unlinked = true;

	/* the urb is still in pending_submit queue */
	if (urbp->req_id == ~0) {
		list_move_tail(&urbp->list, &info->giveback_waiting_list);
		xenhcd_timer_action(info, TIMER_SCAN_PENDING_URBS);
		return 0;
	}

	/* send unlink request to backend */
	if (RING_FULL(&info->urb_ring)) {
		list_move_tail(&urbp->list, &info->pending_unlink_list);
		xenhcd_timer_action(info, TIMER_RING_WATCHDOG);
		return 0;
	}

	if (!list_empty(&info->pending_unlink_list)) {
		list_move_tail(&urbp->list, &info->pending_unlink_list);
		xenhcd_timer_action(info, TIMER_SCAN_PENDING_URBS);
		return 0;
	}

	ret = xenhcd_do_request(info, urbp);
	if (ret == 0)
		list_move_tail(&urbp->list, &info->in_progress_list);

	return ret;
}

static void xenhcd_res_to_urb(struct xenhcd_info *info,
			      struct xenusb_urb_response *res, struct urb *urb)
{
	if (unlikely(!urb))
		return;

	if (res->actual_length > urb->transfer_buffer_length)
		urb->actual_length = urb->transfer_buffer_length;
	else if (res->actual_length < 0)
		urb->actual_length = 0;
	else
		urb->actual_length = res->actual_length;
	urb->error_count = res->error_count;
	urb->start_frame = res->start_frame;
	xenhcd_giveback_urb(info, urb, res->status);
}

static int xenhcd_urb_request_done(struct xenhcd_info *info,
				   unsigned int *eoiflag)
{
	struct xenusb_urb_response res;
	RING_IDX i, rp;
	__u16 id;
	int more_to_do = 0;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);

	rp = info->urb_ring.sring->rsp_prod;
	if (RING_RESPONSE_PROD_OVERFLOW(&info->urb_ring, rp)) {
		xenhcd_set_error(info, "Illegal index on urb-ring");
		goto err;
	}
	rmb(); /* ensure we see queued responses up to "rp" */

	for (i = info->urb_ring.rsp_cons; i != rp; i++) {
		RING_COPY_RESPONSE(&info->urb_ring, i, &res);
		id = res.id;
		if (id >= XENUSB_URB_RING_SIZE) {
			xenhcd_set_error(info, "Illegal data on urb-ring");
			goto err;
		}

		if (likely(xenusb_pipesubmit(info->shadow[id].req.pipe))) {
			xenhcd_gnttab_done(info, id);
			if (info->error)
				goto err;
			xenhcd_res_to_urb(info, &res, info->shadow[id].urb);
		}

		xenhcd_add_id_to_freelist(info, id);

		*eoiflag = 0;
	}
	info->urb_ring.rsp_cons = i;

	if (i != info->urb_ring.req_prod_pvt)
		RING_FINAL_CHECK_FOR_RESPONSES(&info->urb_ring, more_to_do);
	else
		info->urb_ring.sring->rsp_event = i + 1;

	spin_unlock_irqrestore(&info->lock, flags);

	return more_to_do;

 err:
	spin_unlock_irqrestore(&info->lock, flags);
	return 0;
}

static int xenhcd_conn_notify(struct xenhcd_info *info, unsigned int *eoiflag)
{
	struct xenusb_conn_response res;
	struct xenusb_conn_request *req;
	RING_IDX rc, rp;
	__u16 id;
	__u8 portnum, speed;
	int more_to_do = 0;
	int notify;
	int port_changed = 0;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);

	rc = info->conn_ring.rsp_cons;
	rp = info->conn_ring.sring->rsp_prod;
	if (RING_RESPONSE_PROD_OVERFLOW(&info->conn_ring, rp)) {
		xenhcd_set_error(info, "Illegal index on conn-ring");
		spin_unlock_irqrestore(&info->lock, flags);
		return 0;
	}
	rmb(); /* ensure we see queued responses up to "rp" */

	while (rc != rp) {
		RING_COPY_RESPONSE(&info->conn_ring, rc, &res);
		id = res.id;
		portnum = res.portnum;
		speed = res.speed;
		info->conn_ring.rsp_cons = ++rc;

		if (xenhcd_rhport_connect(info, portnum, speed)) {
			xenhcd_set_error(info, "Illegal data on conn-ring");
			spin_unlock_irqrestore(&info->lock, flags);
			return 0;
		}

		if (info->ports[portnum - 1].c_connection)
			port_changed = 1;

		barrier();

		req = RING_GET_REQUEST(&info->conn_ring,
				       info->conn_ring.req_prod_pvt);
		req->id = id;
		info->conn_ring.req_prod_pvt++;

		*eoiflag = 0;
	}

	if (rc != info->conn_ring.req_prod_pvt)
		RING_FINAL_CHECK_FOR_RESPONSES(&info->conn_ring, more_to_do);
	else
		info->conn_ring.sring->rsp_event = rc + 1;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->conn_ring, notify);
	if (notify)
		notify_remote_via_irq(info->irq);

	spin_unlock_irqrestore(&info->lock, flags);

	if (port_changed)
		usb_hcd_poll_rh_status(xenhcd_info_to_hcd(info));

	return more_to_do;
}

static irqreturn_t xenhcd_int(int irq, void *dev_id)
{
	struct xenhcd_info *info = (struct xenhcd_info *)dev_id;
	unsigned int eoiflag = XEN_EOI_FLAG_SPURIOUS;

	if (unlikely(info->error)) {
		xen_irq_lateeoi(irq, XEN_EOI_FLAG_SPURIOUS);
		return IRQ_HANDLED;
	}

	while (xenhcd_urb_request_done(info, &eoiflag) |
	       xenhcd_conn_notify(info, &eoiflag))
		/* Yield point for this unbounded loop. */
		cond_resched();

	xen_irq_lateeoi(irq, eoiflag);
	return IRQ_HANDLED;
}

static void xenhcd_destroy_rings(struct xenhcd_info *info)
{
	if (info->irq)
		unbind_from_irqhandler(info->irq, info);
	info->irq = 0;

	if (info->urb_ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->urb_ring_ref,
					  (unsigned long)info->urb_ring.sring);
		info->urb_ring_ref = GRANT_INVALID_REF;
	}
	info->urb_ring.sring = NULL;

	if (info->conn_ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->conn_ring_ref,
					  (unsigned long)info->conn_ring.sring);
		info->conn_ring_ref = GRANT_INVALID_REF;
	}
	info->conn_ring.sring = NULL;
}

static int xenhcd_setup_rings(struct xenbus_device *dev,
			      struct xenhcd_info *info)
{
	struct xenusb_urb_sring *urb_sring;
	struct xenusb_conn_sring *conn_sring;
	grant_ref_t gref;
	int err;

	info->urb_ring_ref = GRANT_INVALID_REF;
	info->conn_ring_ref = GRANT_INVALID_REF;

	urb_sring = (struct xenusb_urb_sring *)get_zeroed_page(
							GFP_NOIO | __GFP_HIGH);
	if (!urb_sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating urb ring");
		return -ENOMEM;
	}
	SHARED_RING_INIT(urb_sring);
	FRONT_RING_INIT(&info->urb_ring, urb_sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, urb_sring, 1, &gref);
	if (err < 0) {
		free_page((unsigned long)urb_sring);
		info->urb_ring.sring = NULL;
		goto fail;
	}
	info->urb_ring_ref = gref;

	conn_sring = (struct xenusb_conn_sring *)get_zeroed_page(
							GFP_NOIO | __GFP_HIGH);
	if (!conn_sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating conn ring");
		err = -ENOMEM;
		goto fail;
	}
	SHARED_RING_INIT(conn_sring);
	FRONT_RING_INIT(&info->conn_ring, conn_sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, conn_sring, 1, &gref);
	if (err < 0) {
		free_page((unsigned long)conn_sring);
		info->conn_ring.sring = NULL;
		goto fail;
	}
	info->conn_ring_ref = gref;

	err = xenbus_alloc_evtchn(dev, &info->evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "xenbus_alloc_evtchn");
		goto fail;
	}

	err = bind_evtchn_to_irq_lateeoi(info->evtchn);
	if (err <= 0) {
		xenbus_dev_fatal(dev, err, "bind_evtchn_to_irq_lateeoi");
		goto fail;
	}

	info->irq = err;

	err = request_threaded_irq(info->irq, NULL, xenhcd_int,
				   IRQF_ONESHOT, "xenhcd", info);
	if (err) {
		xenbus_dev_fatal(dev, err, "request_threaded_irq");
		goto free_irq;
	}

	return 0;

free_irq:
	unbind_from_irqhandler(info->irq, info);
fail:
	xenhcd_destroy_rings(info);
	return err;
}

static int xenhcd_talk_to_backend(struct xenbus_device *dev,
				  struct xenhcd_info *info)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;

	err = xenhcd_setup_rings(dev, info);
	if (err)
		return err;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_ring;
	}

	err = xenbus_printf(xbt, dev->nodename, "urb-ring-ref", "%u",
			    info->urb_ring_ref);
	if (err) {
		message = "writing urb-ring-ref";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "conn-ring-ref", "%u",
			    info->conn_ring_ref);
	if (err) {
		message = "writing conn-ring-ref";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "event-channel", "%u",
			    info->evtchn);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_ring;
	}

	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);

destroy_ring:
	xenhcd_destroy_rings(info);

	return err;
}

static int xenhcd_connect(struct xenbus_device *dev)
{
	struct xenhcd_info *info = dev_get_drvdata(&dev->dev);
	struct xenusb_conn_request *req;
	int idx, err;
	int notify;
	char name[TASK_COMM_LEN];
	struct usb_hcd *hcd;

	hcd = xenhcd_info_to_hcd(info);
	snprintf(name, TASK_COMM_LEN, "xenhcd.%d", hcd->self.busnum);

	err = xenhcd_talk_to_backend(dev, info);
	if (err)
		return err;

	/* prepare ring for hotplug notification */
	for (idx = 0; idx < XENUSB_CONN_RING_SIZE; idx++) {
		req = RING_GET_REQUEST(&info->conn_ring, idx);
		req->id = idx;
	}
	info->conn_ring.req_prod_pvt = idx;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->conn_ring, notify);
	if (notify)
		notify_remote_via_irq(info->irq);

	return 0;
}

static void xenhcd_disconnect(struct xenbus_device *dev)
{
	struct xenhcd_info *info = dev_get_drvdata(&dev->dev);
	struct usb_hcd *hcd = xenhcd_info_to_hcd(info);

	usb_remove_hcd(hcd);
	xenbus_frontend_closed(dev);
}

static void xenhcd_watchdog(struct timer_list *timer)
{
	struct xenhcd_info *info = from_timer(info, timer, watchdog);
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	if (likely(HC_IS_RUNNING(xenhcd_info_to_hcd(info)->state))) {
		xenhcd_timer_action_done(info, TIMER_RING_WATCHDOG);
		xenhcd_giveback_unlinked_urbs(info);
		xenhcd_kick_pending_urbs(info);
	}
	spin_unlock_irqrestore(&info->lock, flags);
}

/*
 * one-time HC init
 */
static int xenhcd_setup(struct usb_hcd *hcd)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);

	spin_lock_init(&info->lock);
	INIT_LIST_HEAD(&info->pending_submit_list);
	INIT_LIST_HEAD(&info->pending_unlink_list);
	INIT_LIST_HEAD(&info->in_progress_list);
	INIT_LIST_HEAD(&info->giveback_waiting_list);
	timer_setup(&info->watchdog, xenhcd_watchdog, 0);

	hcd->has_tt = (hcd->driver->flags & HCD_MASK) != HCD_USB11;

	return 0;
}

/*
 * start HC running
 */
static int xenhcd_run(struct usb_hcd *hcd)
{
	hcd->uses_new_polling = 1;
	clear_bit(HCD_FLAG_POLL_RH, &hcd->flags);
	hcd->state = HC_STATE_RUNNING;
	return 0;
}

/*
 * stop running HC
 */
static void xenhcd_stop(struct usb_hcd *hcd)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);

	del_timer_sync(&info->watchdog);
	spin_lock_irq(&info->lock);
	/* cancel all urbs */
	hcd->state = HC_STATE_HALT;
	xenhcd_cancel_all_enqueued_urbs(info);
	xenhcd_giveback_unlinked_urbs(info);
	spin_unlock_irq(&info->lock);
}

/*
 * called as .urb_enqueue()
 * non-error returns are promise to giveback the urb later
 */
static int xenhcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
			      gfp_t mem_flags)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);
	struct urb_priv *urbp;
	unsigned long flags;
	int ret;

	if (unlikely(info->error))
		return -ESHUTDOWN;

	urbp = kmem_cache_zalloc(xenhcd_urbp_cachep, mem_flags);
	if (!urbp)
		return -ENOMEM;

	spin_lock_irqsave(&info->lock, flags);

	urbp->urb = urb;
	urb->hcpriv = urbp;
	urbp->req_id = ~0;
	urbp->unlink_req_id = ~0;
	INIT_LIST_HEAD(&urbp->list);
	urbp->status = 1;
	urb->unlinked = false;

	ret = xenhcd_submit_urb(info, urbp);

	if (ret)
		xenhcd_free_urb_priv(urbp);

	spin_unlock_irqrestore(&info->lock, flags);

	return ret;
}

/*
 * called as .urb_dequeue()
 */
static int xenhcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct xenhcd_info *info = xenhcd_hcd_to_info(hcd);
	struct urb_priv *urbp;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&info->lock, flags);

	urbp = urb->hcpriv;
	if (urbp) {
		urbp->status = status;
		ret = xenhcd_unlink_urb(info, urbp);
	}

	spin_unlock_irqrestore(&info->lock, flags);

	return ret;
}

/*
 * called from usb_get_current_frame_number(),
 * but, almost all drivers not use such function.
 */
static int xenhcd_get_frame(struct usb_hcd *hcd)
{
	/* it means error, but probably no problem :-) */
	return 0;
}

static struct hc_driver xenhcd_usb20_hc_driver = {
	.description = "xen-hcd",
	.product_desc = "Xen USB2.0 Virtual Host Controller",
	.hcd_priv_size = sizeof(struct xenhcd_info),
	.flags = HCD_USB2,

	/* basic HC lifecycle operations */
	.reset = xenhcd_setup,
	.start = xenhcd_run,
	.stop = xenhcd_stop,

	/* managing urb I/O */
	.urb_enqueue = xenhcd_urb_enqueue,
	.urb_dequeue = xenhcd_urb_dequeue,
	.get_frame_number = xenhcd_get_frame,

	/* root hub operations */
	.hub_status_data = xenhcd_hub_status_data,
	.hub_control = xenhcd_hub_control,
#ifdef CONFIG_PM
	.bus_suspend = xenhcd_bus_suspend,
	.bus_resume = xenhcd_bus_resume,
#endif
};

static struct hc_driver xenhcd_usb11_hc_driver = {
	.description = "xen-hcd",
	.product_desc = "Xen USB1.1 Virtual Host Controller",
	.hcd_priv_size = sizeof(struct xenhcd_info),
	.flags = HCD_USB11,

	/* basic HC lifecycle operations */
	.reset = xenhcd_setup,
	.start = xenhcd_run,
	.stop = xenhcd_stop,

	/* managing urb I/O */
	.urb_enqueue = xenhcd_urb_enqueue,
	.urb_dequeue = xenhcd_urb_dequeue,
	.get_frame_number = xenhcd_get_frame,

	/* root hub operations */
	.hub_status_data = xenhcd_hub_status_data,
	.hub_control = xenhcd_hub_control,
#ifdef CONFIG_PM
	.bus_suspend = xenhcd_bus_suspend,
	.bus_resume = xenhcd_bus_resume,
#endif
};

static struct usb_hcd *xenhcd_create_hcd(struct xenbus_device *dev)
{
	int i;
	int err = 0;
	int num_ports;
	int usb_ver;
	struct usb_hcd *hcd = NULL;
	struct xenhcd_info *info;

	err = xenbus_scanf(XBT_NIL, dev->otherend, "num-ports", "%d",
			   &num_ports);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading num-ports");
		return ERR_PTR(-EINVAL);
	}
	if (num_ports < 1 || num_ports > XENUSB_MAX_PORTNR) {
		xenbus_dev_fatal(dev, err, "invalid num-ports");
		return ERR_PTR(-EINVAL);
	}

	err = xenbus_scanf(XBT_NIL, dev->otherend, "usb-ver", "%d", &usb_ver);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading usb-ver");
		return ERR_PTR(-EINVAL);
	}
	switch (usb_ver) {
	case XENUSB_VER_USB11:
		hcd = usb_create_hcd(&xenhcd_usb11_hc_driver, &dev->dev,
				     dev_name(&dev->dev));
		break;
	case XENUSB_VER_USB20:
		hcd = usb_create_hcd(&xenhcd_usb20_hc_driver, &dev->dev,
				     dev_name(&dev->dev));
		break;
	default:
		xenbus_dev_fatal(dev, err, "invalid usb-ver");
		return ERR_PTR(-EINVAL);
	}
	if (!hcd) {
		xenbus_dev_fatal(dev, err,
				 "fail to allocate USB host controller");
		return ERR_PTR(-ENOMEM);
	}

	info = xenhcd_hcd_to_info(hcd);
	info->xbdev = dev;
	info->rh_numports = num_ports;

	for (i = 0; i < XENUSB_URB_RING_SIZE; i++) {
		info->shadow[i].req.id = i + 1;
		info->shadow[i].urb = NULL;
		info->shadow[i].in_flight = false;
	}
	info->shadow[XENUSB_URB_RING_SIZE - 1].req.id = 0x0fff;

	return hcd;
}

static void xenhcd_backend_changed(struct xenbus_device *dev,
				   enum xenbus_state backend_state)
{
	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
		break;

	case XenbusStateInitWait:
	case XenbusStateInitialised:
	case XenbusStateConnected:
		if (dev->state != XenbusStateInitialising)
			break;
		if (!xenhcd_connect(dev))
			xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		fallthrough;	/* Missed the backend's Closing state. */
	case XenbusStateClosing:
		xenhcd_disconnect(dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 backend_state);
		break;
	}
}

static int xenhcd_remove(struct xenbus_device *dev)
{
	struct xenhcd_info *info = dev_get_drvdata(&dev->dev);
	struct usb_hcd *hcd = xenhcd_info_to_hcd(info);

	xenhcd_destroy_rings(info);
	usb_put_hcd(hcd);

	return 0;
}

static int xenhcd_probe(struct xenbus_device *dev,
			const struct xenbus_device_id *id)
{
	int err;
	struct usb_hcd *hcd;
	struct xenhcd_info *info;

	if (usb_disabled())
		return -ENODEV;

	hcd = xenhcd_create_hcd(dev);
	if (IS_ERR(hcd)) {
		err = PTR_ERR(hcd);
		xenbus_dev_fatal(dev, err,
				 "fail to create usb host controller");
		return err;
	}

	info = xenhcd_hcd_to_info(hcd);
	dev_set_drvdata(&dev->dev, info);

	err = usb_add_hcd(hcd, 0, 0);
	if (err) {
		xenbus_dev_fatal(dev, err, "fail to add USB host controller");
		usb_put_hcd(hcd);
		dev_set_drvdata(&dev->dev, NULL);
	}

	return err;
}

static const struct xenbus_device_id xenhcd_ids[] = {
	{ "vusb" },
	{ "" },
};

static struct xenbus_driver xenhcd_driver = {
	.ids			= xenhcd_ids,
	.probe			= xenhcd_probe,
	.otherend_changed	= xenhcd_backend_changed,
	.remove			= xenhcd_remove,
};

static int __init xenhcd_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	xenhcd_urbp_cachep = kmem_cache_create("xenhcd_urb_priv",
					sizeof(struct urb_priv), 0, 0, NULL);
	if (!xenhcd_urbp_cachep) {
		pr_err("xenhcd failed to create kmem cache\n");
		return -ENOMEM;
	}

	return xenbus_register_frontend(&xenhcd_driver);
}
module_init(xenhcd_init);

static void __exit xenhcd_exit(void)
{
	kmem_cache_destroy(xenhcd_urbp_cachep);
	xenbus_unregister_driver(&xenhcd_driver);
}
module_exit(xenhcd_exit);

MODULE_ALIAS("xen:vusb");
MODULE_AUTHOR("Juergen Gross <jgross@suse.com>");
MODULE_DESCRIPTION("Xen USB Virtual Host Controller driver (xen-hcd)");
MODULE_LICENSE("Dual BSD/GPL");
