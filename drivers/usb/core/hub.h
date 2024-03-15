/* SPDX-License-Identifier: GPL-2.0 */
/*
 * usb hub driver head file
 *
 * Copyright (C) 1999 Linus Torvalds
 * Copyright (C) 1999 Johannes Erdfelt
 * Copyright (C) 1999 Gregory P. Smith
 * Copyright (C) 2001 Brad Hards (bhards@bigpond.net.au)
 * Copyright (C) 2012 Intel Corp (tianyu.lan@intel.com)
 *
 *  move struct usb_hub to this file.
 */

#include <linux/usb.h>
#include <linux/usb/ch11.h>
#include <linux/usb/hcd.h>
#include "usb.h"

struct usb_hub {
	struct device		*intfdev;	/* the "interface" device */
	struct usb_device	*hdev;
	struct kref		kref;
	struct urb		*urb;		/* for interrupt polling pipe */

	/* buffer for urb ... with extra space in case of babble */
	u8			(*buffer)[8];
	union {
		struct usb_hub_status	hub;
		struct usb_port_status	port;
	}			*status;	/* buffer for status reports */
	struct mutex		status_mutex;	/* for the status buffer */

	int			error;		/* last reported error */
	int			nerrors;	/* track consecutive errors */

	unsigned long		event_bits[1];	/* status change bitmask */
	unsigned long		change_bits[1];	/* ports with logical connect
							status change */
	unsigned long		removed_bits[1]; /* ports with a "removed"
							device present */
	unsigned long		wakeup_bits[1];	/* ports that have signaled
							remote wakeup */
	unsigned long		power_bits[1]; /* ports that are powered */
	unsigned long		child_usage_bits[1]; /* ports powered on for
							children */
	unsigned long		warm_reset_bits[1]; /* ports requesting warm
							reset recovery */
#if USB_MAXCHILDREN > 31 /* 8*sizeof(unsigned long) - 1 */
#error event_bits[] is too short!
#endif

	struct usb_hub_descriptor *descriptor;	/* class descriptor */
	struct usb_tt		tt;		/* Transaction Translator */

	unsigned		mA_per_port;	/* current for each child */
#ifdef	CONFIG_PM
	unsigned		wakeup_enabled_descendants;
#endif

	unsigned		limited_power:1;
	unsigned		quiescing:1;
	unsigned		disconnected:1;
	unsigned		in_reset:1;
	unsigned		quirk_disable_autosuspend:1;

	unsigned		quirk_check_port_auto_suspend:1;

	unsigned		has_indicators:1;
	u8			indicator[USB_MAXCHILDREN];
	struct delayed_work	leds;
	struct delayed_work	init_work;
	struct work_struct      events;
	spinlock_t		irq_urb_lock;
	struct timer_list	irq_urb_retry;
	struct usb_port		**ports;
	struct list_head        onboard_hub_devs;
};

/**
 * struct usb port - kernel's representation of a usb port
 * @child: usb device attached to the port
 * @dev: generic device interface
 * @port_owner: port's owner
 * @peer: related usb2 and usb3 ports (share the same connector)
 * @req: default pm qos request for hubs without port power control
 * @connect_type: port's connect type
 * @location: opaque representation of platform connector location
 * @status_lock: synchronize port_event() vs usb_port_{suspend|resume}
 * @portnum: port index num based one
 * @is_superspeed cache super-speed status
 * @usb3_lpm_u1_permit: whether USB3 U1 LPM is permitted.
 * @usb3_lpm_u2_permit: whether USB3 U2 LPM is permitted.
 */
struct usb_port {
	struct usb_device *child;
	struct device dev;
	struct usb_dev_state *port_owner;
	struct usb_port *peer;
	struct dev_pm_qos_request *req;
	enum usb_port_connect_type connect_type;
	usb_port_location_t location;
	struct mutex status_lock;
	u32 over_current_count;
	u8 portnum;
	u32 quirks;
	unsigned int is_superspeed:1;
	unsigned int usb3_lpm_u1_permit:1;
	unsigned int usb3_lpm_u2_permit:1;
};

#define to_usb_port(_dev) \
	container_of(_dev, struct usb_port, dev)

extern int usb_hub_create_port_device(struct usb_hub *hub,
		int port1);
extern void usb_hub_remove_port_device(struct usb_hub *hub,
		int port1);
extern int usb_hub_set_port_power(struct usb_device *hdev, struct usb_hub *hub,
		int port1, bool set);
extern struct usb_hub *usb_hub_to_struct_hub(struct usb_device *hdev);
extern void hub_get(struct usb_hub *hub);
extern void hub_put(struct usb_hub *hub);
extern int hub_port_debounce(struct usb_hub *hub, int port1,
		bool must_be_connected);
extern int usb_clear_port_feature(struct usb_device *hdev,
		int port1, int feature);
extern int usb_hub_port_status(struct usb_hub *hub, int port1,
		u16 *status, u16 *change);
extern int usb_port_is_power_on(struct usb_hub *hub, unsigned int portstatus);

static inline bool hub_is_port_power_switchable(struct usb_hub *hub)
{
	__le16 hcs;

	if (!hub)
		return false;
	hcs = hub->descriptor->wHubCharacteristics;
	return (le16_to_cpu(hcs) & HUB_CHAR_LPSM) < HUB_CHAR_NO_LPSM;
}

static inline int hub_is_superspeed(struct usb_device *hdev)
{
	return hdev->descriptor.bDeviceProtocol == USB_HUB_PR_SS;
}

static inline int hub_is_superspeedplus(struct usb_device *hdev)
{
	return (hdev->descriptor.bDeviceProtocol == USB_HUB_PR_SS &&
		le16_to_cpu(hdev->descriptor.bcdUSB) >= 0x0310 &&
		hdev->bos && hdev->bos->ssp_cap);
}

static inline unsigned hub_power_on_good_delay(struct usb_hub *hub)
{
	unsigned delay = hub->descriptor->bPwrOn2PwrGood * 2;

	if (!hub->hdev->parent)	/* root hub */
		return delay;
	else /* Wait at least 100 msec for power to become stable */
		return max(delay, 100U);
}

static inline int hub_port_debounce_be_connected(struct usb_hub *hub,
		int port1)
{
	return hub_port_debounce(hub, port1, true);
}

static inline int hub_port_debounce_be_stable(struct usb_hub *hub,
		int port1)
{
	return hub_port_debounce(hub, port1, false);
}
