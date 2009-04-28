/*
 * USB hub driver.
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Gregory P. Smith
 * (C) Copyright 2001 Brad Hards (bhards@bigpond.net.au)
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/freezer.h>

#include <asm/uaccess.h>
#include <asm/byteorder.h>

#include "usb.h"
#include "hcd.h"
#include "hub.h"

/* if we are in debug mode, always announce new devices */
#ifdef DEBUG
#ifndef CONFIG_USB_ANNOUNCE_NEW_DEVICES
#define CONFIG_USB_ANNOUNCE_NEW_DEVICES
#endif
#endif

struct usb_hub {
	struct device		*intfdev;	/* the "interface" device */
	struct usb_device	*hdev;
	struct kref		kref;
	struct urb		*urb;		/* for interrupt polling pipe */

	/* buffer for urb ... with extra space in case of babble */
	char			(*buffer)[8];
	dma_addr_t		buffer_dma;	/* DMA address for buffer */
	union {
		struct usb_hub_status	hub;
		struct usb_port_status	port;
	}			*status;	/* buffer for status reports */
	struct mutex		status_mutex;	/* for the status buffer */

	int			error;		/* last reported error */
	int			nerrors;	/* track consecutive errors */

	struct list_head	event_list;	/* hubs w/data or errs ready */
	unsigned long		event_bits[1];	/* status change bitmask */
	unsigned long		change_bits[1];	/* ports with logical connect
							status change */
	unsigned long		busy_bits[1];	/* ports being reset or
							resumed */
#if USB_MAXCHILDREN > 31 /* 8*sizeof(unsigned long) - 1 */
#error event_bits[] is too short!
#endif

	struct usb_hub_descriptor *descriptor;	/* class descriptor */
	struct usb_tt		tt;		/* Transaction Translator */

	unsigned		mA_per_port;	/* current for each child */

	unsigned		limited_power:1;
	unsigned		quiescing:1;
	unsigned		disconnected:1;

	unsigned		has_indicators:1;
	u8			indicator[USB_MAXCHILDREN];
	struct delayed_work	leds;
	struct delayed_work	init_work;
};


/* Protect struct usb_device->state and ->children members
 * Note: Both are also protected by ->dev.sem, except that ->state can
 * change to USB_STATE_NOTATTACHED even when the semaphore isn't held. */
static DEFINE_SPINLOCK(device_state_lock);

/* khubd's worklist and its lock */
static DEFINE_SPINLOCK(hub_event_lock);
static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */

/* Wakes up khubd */
static DECLARE_WAIT_QUEUE_HEAD(khubd_wait);

static struct task_struct *khubd_task;

/* cycle leds on hubs that aren't blinking for attention */
static int blinkenlights = 0;
module_param (blinkenlights, bool, S_IRUGO);
MODULE_PARM_DESC (blinkenlights, "true to cycle leds on hubs");

/*
 * Device SATA8000 FW1.0 from DATAST0R Technology Corp requires about
 * 10 seconds to send reply for the initial 64-byte descriptor request.
 */
/* define initial 64-byte descriptor request timeout in milliseconds */
static int initial_descriptor_timeout = USB_CTRL_GET_TIMEOUT;
module_param(initial_descriptor_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(initial_descriptor_timeout,
		"initial 64-byte descriptor request timeout in milliseconds "
		"(default 5000 - 5.0 seconds)");

/*
 * As of 2.6.10 we introduce a new USB device initialization scheme which
 * closely resembles the way Windows works.  Hopefully it will be compatible
 * with a wider range of devices than the old scheme.  However some previously
 * working devices may start giving rise to "device not accepting address"
 * errors; if that happens the user can try the old scheme by adjusting the
 * following module parameters.
 *
 * For maximum flexibility there are two boolean parameters to control the
 * hub driver's behavior.  On the first initialization attempt, if the
 * "old_scheme_first" parameter is set then the old scheme will be used,
 * otherwise the new scheme is used.  If that fails and "use_both_schemes"
 * is set, then the driver will make another attempt, using the other scheme.
 */
static int old_scheme_first = 0;
module_param(old_scheme_first, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(old_scheme_first,
		 "start with the old device initialization scheme");

static int use_both_schemes = 1;
module_param(use_both_schemes, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(use_both_schemes,
		"try the other device initialization scheme if the "
		"first one fails");

/* Mutual exclusion for EHCI CF initialization.  This interferes with
 * port reset on some companion controllers.
 */
DECLARE_RWSEM(ehci_cf_port_reset_rwsem);
EXPORT_SYMBOL_GPL(ehci_cf_port_reset_rwsem);

#define HUB_DEBOUNCE_TIMEOUT	1500
#define HUB_DEBOUNCE_STEP	  25
#define HUB_DEBOUNCE_STABLE	 100


static int usb_reset_and_verify_device(struct usb_device *udev);

static inline char *portspeed(int portstatus)
{
	if (portstatus & (1 << USB_PORT_FEAT_HIGHSPEED))
    		return "480 Mb/s";
	else if (portstatus & (1 << USB_PORT_FEAT_LOWSPEED))
		return "1.5 Mb/s";
	else if (portstatus & (1 << USB_PORT_FEAT_SUPERSPEED))
		return "5.0 Gb/s";
	else
		return "12 Mb/s";
}

/* Note that hdev or one of its children must be locked! */
static inline struct usb_hub *hdev_to_hub(struct usb_device *hdev)
{
	return usb_get_intfdata(hdev->actconfig->interface[0]);
}

/* USB 2.0 spec Section 11.24.4.5 */
static int get_hub_descriptor(struct usb_device *hdev, void *data, int size)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
			USB_DT_HUB << 8, 0, data, size,
			USB_CTRL_GET_TIMEOUT);
		if (ret >= (USB_DT_HUB_NONVAR_SIZE + 2))
			return ret;
	}
	return -EINVAL;
}

/*
 * USB 2.0 spec Section 11.24.2.1
 */
static int clear_hub_feature(struct usb_device *hdev, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature, 0, NULL, 0, 1000);
}

/*
 * USB 2.0 spec Section 11.24.2.2
 */
static int clear_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, 1000);
}

/*
 * USB 2.0 spec Section 11.24.2.13
 */
static int set_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, 1000);
}

/*
 * USB 2.0 spec Section 11.24.2.7.1.10 and table 11-7
 * for info about using port indicators
 */
static void set_port_led(
	struct usb_hub *hub,
	int port1,
	int selector
)
{
	int status = set_port_feature(hub->hdev, (selector << 8) | port1,
			USB_PORT_FEAT_INDICATOR);
	if (status < 0)
		dev_dbg (hub->intfdev,
			"port %d indicator %s status %d\n",
			port1,
			({ char *s; switch (selector) {
			case HUB_LED_AMBER: s = "amber"; break;
			case HUB_LED_GREEN: s = "green"; break;
			case HUB_LED_OFF: s = "off"; break;
			case HUB_LED_AUTO: s = "auto"; break;
			default: s = "??"; break;
			}; s; }),
			status);
}

#define	LED_CYCLE_PERIOD	((2*HZ)/3)

static void led_work (struct work_struct *work)
{
	struct usb_hub		*hub =
		container_of(work, struct usb_hub, leds.work);
	struct usb_device	*hdev = hub->hdev;
	unsigned		i;
	unsigned		changed = 0;
	int			cursor = -1;

	if (hdev->state != USB_STATE_CONFIGURED || hub->quiescing)
		return;

	for (i = 0; i < hub->descriptor->bNbrPorts; i++) {
		unsigned	selector, mode;

		/* 30%-50% duty cycle */

		switch (hub->indicator[i]) {
		/* cycle marker */
		case INDICATOR_CYCLE:
			cursor = i;
			selector = HUB_LED_AUTO;
			mode = INDICATOR_AUTO;
			break;
		/* blinking green = sw attention */
		case INDICATOR_GREEN_BLINK:
			selector = HUB_LED_GREEN;
			mode = INDICATOR_GREEN_BLINK_OFF;
			break;
		case INDICATOR_GREEN_BLINK_OFF:
			selector = HUB_LED_OFF;
			mode = INDICATOR_GREEN_BLINK;
			break;
		/* blinking amber = hw attention */
		case INDICATOR_AMBER_BLINK:
			selector = HUB_LED_AMBER;
			mode = INDICATOR_AMBER_BLINK_OFF;
			break;
		case INDICATOR_AMBER_BLINK_OFF:
			selector = HUB_LED_OFF;
			mode = INDICATOR_AMBER_BLINK;
			break;
		/* blink green/amber = reserved */
		case INDICATOR_ALT_BLINK:
			selector = HUB_LED_GREEN;
			mode = INDICATOR_ALT_BLINK_OFF;
			break;
		case INDICATOR_ALT_BLINK_OFF:
			selector = HUB_LED_AMBER;
			mode = INDICATOR_ALT_BLINK;
			break;
		default:
			continue;
		}
		if (selector != HUB_LED_AUTO)
			changed = 1;
		set_port_led(hub, i + 1, selector);
		hub->indicator[i] = mode;
	}
	if (!changed && blinkenlights) {
		cursor++;
		cursor %= hub->descriptor->bNbrPorts;
		set_port_led(hub, cursor + 1, HUB_LED_GREEN);
		hub->indicator[cursor] = INDICATOR_CYCLE;
		changed++;
	}
	if (changed)
		schedule_delayed_work(&hub->leds, LED_CYCLE_PERIOD);
}

/* use a short timeout for hub/port status fetches */
#define	USB_STS_TIMEOUT		1000
#define	USB_STS_RETRIES		5

/*
 * USB 2.0 spec Section 11.24.2.6
 */
static int get_hub_status(struct usb_device *hdev,
		struct usb_hub_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES && status == -ETIMEDOUT; i++) {
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
			data, sizeof(*data), USB_STS_TIMEOUT);
	}
	return status;
}

/*
 * USB 2.0 spec Section 11.24.2.7
 */
static int get_port_status(struct usb_device *hdev, int port1,
		struct usb_port_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES && status == -ETIMEDOUT; i++) {
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port1,
			data, sizeof(*data), USB_STS_TIMEOUT);
	}
	return status;
}

static int hub_port_status(struct usb_hub *hub, int port1,
		u16 *status, u16 *change)
{
	int ret;

	mutex_lock(&hub->status_mutex);
	ret = get_port_status(hub->hdev, port1, &hub->status->port);
	if (ret < 4) {
		dev_err(hub->intfdev,
			"%s failed (err = %d)\n", __func__, ret);
		if (ret >= 0)
			ret = -EIO;
	} else {
		*status = le16_to_cpu(hub->status->port.wPortStatus);
		*change = le16_to_cpu(hub->status->port.wPortChange);
		ret = 0;
	}
	mutex_unlock(&hub->status_mutex);
	return ret;
}

static void kick_khubd(struct usb_hub *hub)
{
	unsigned long	flags;

	/* Suppress autosuspend until khubd runs */
	to_usb_interface(hub->intfdev)->pm_usage_cnt = 1;

	spin_lock_irqsave(&hub_event_lock, flags);
	if (!hub->disconnected && list_empty(&hub->event_list)) {
		list_add_tail(&hub->event_list, &hub_event_list);
		wake_up(&khubd_wait);
	}
	spin_unlock_irqrestore(&hub_event_lock, flags);
}

void usb_kick_khubd(struct usb_device *hdev)
{
	/* FIXME: What if hdev isn't bound to the hub driver? */
	kick_khubd(hdev_to_hub(hdev));
}


/* completion function, fires on port status changes and various faults */
static void hub_irq(struct urb *urb)
{
	struct usb_hub *hub = urb->context;
	int status = urb->status;
	unsigned i;
	unsigned long bits;

	switch (status) {
	case -ENOENT:		/* synchronous unlink */
	case -ECONNRESET:	/* async unlink */
	case -ESHUTDOWN:	/* hardware going away */
		return;

	default:		/* presumably an error */
		/* Cause a hub reset after 10 consecutive errors */
		dev_dbg (hub->intfdev, "transfer --> %d\n", status);
		if ((++hub->nerrors < 10) || hub->error)
			goto resubmit;
		hub->error = status;
		/* FALL THROUGH */

	/* let khubd handle things */
	case 0:			/* we got data:  port status changed */
		bits = 0;
		for (i = 0; i < urb->actual_length; ++i)
			bits |= ((unsigned long) ((*hub->buffer)[i]))
					<< (i*8);
		hub->event_bits[0] = bits;
		break;
	}

	hub->nerrors = 0;

	/* Something happened, let khubd figure it out */
	kick_khubd(hub);

resubmit:
	if (hub->quiescing)
		return;

	if ((status = usb_submit_urb (hub->urb, GFP_ATOMIC)) != 0
			&& status != -ENODEV && status != -EPERM)
		dev_err (hub->intfdev, "resubmit --> %d\n", status);
}

/* USB 2.0 spec Section 11.24.2.3 */
static inline int
hub_clear_tt_buffer (struct usb_device *hdev, u16 devinfo, u16 tt)
{
	return usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			       HUB_CLEAR_TT_BUFFER, USB_RT_PORT, devinfo,
			       tt, NULL, 0, 1000);
}

/*
 * enumeration blocks khubd for a long time. we use keventd instead, since
 * long blocking there is the exception, not the rule.  accordingly, HCDs
 * talking to TTs must queue control transfers (not just bulk and iso), so
 * both can talk to the same hub concurrently.
 */
static void hub_tt_kevent (struct work_struct *work)
{
	struct usb_hub		*hub =
		container_of(work, struct usb_hub, tt.kevent);
	unsigned long		flags;
	int			limit = 100;

	spin_lock_irqsave (&hub->tt.lock, flags);
	while (--limit && !list_empty (&hub->tt.clear_list)) {
		struct list_head	*next;
		struct usb_tt_clear	*clear;
		struct usb_device	*hdev = hub->hdev;
		int			status;

		next = hub->tt.clear_list.next;
		clear = list_entry (next, struct usb_tt_clear, clear_list);
		list_del (&clear->clear_list);

		/* drop lock so HCD can concurrently report other TT errors */
		spin_unlock_irqrestore (&hub->tt.lock, flags);
		status = hub_clear_tt_buffer (hdev, clear->devinfo, clear->tt);
		spin_lock_irqsave (&hub->tt.lock, flags);

		if (status)
			dev_err (&hdev->dev,
				"clear tt %d (%04x) error %d\n",
				clear->tt, clear->devinfo, status);
		kfree(clear);
	}
	spin_unlock_irqrestore (&hub->tt.lock, flags);
}

/**
 * usb_hub_tt_clear_buffer - clear control/bulk TT state in high speed hub
 * @udev: the device whose split transaction failed
 * @pipe: identifies the endpoint of the failed transaction
 *
 * High speed HCDs use this to tell the hub driver that some split control or
 * bulk transaction failed in a way that requires clearing internal state of
 * a transaction translator.  This is normally detected (and reported) from
 * interrupt context.
 *
 * It may not be possible for that hub to handle additional full (or low)
 * speed transactions until that state is fully cleared out.
 */
void usb_hub_tt_clear_buffer (struct usb_device *udev, int pipe)
{
	struct usb_tt		*tt = udev->tt;
	unsigned long		flags;
	struct usb_tt_clear	*clear;

	/* we've got to cope with an arbitrary number of pending TT clears,
	 * since each TT has "at least two" buffers that can need it (and
	 * there can be many TTs per hub).  even if they're uncommon.
	 */
	if ((clear = kmalloc (sizeof *clear, GFP_ATOMIC)) == NULL) {
		dev_err (&udev->dev, "can't save CLEAR_TT_BUFFER state\n");
		/* FIXME recover somehow ... RESET_TT? */
		return;
	}

	/* info that CLEAR_TT_BUFFER needs */
	clear->tt = tt->multi ? udev->ttport : 1;
	clear->devinfo = usb_pipeendpoint (pipe);
	clear->devinfo |= udev->devnum << 4;
	clear->devinfo |= usb_pipecontrol (pipe)
			? (USB_ENDPOINT_XFER_CONTROL << 11)
			: (USB_ENDPOINT_XFER_BULK << 11);
	if (usb_pipein (pipe))
		clear->devinfo |= 1 << 15;
	
	/* tell keventd to clear state for this TT */
	spin_lock_irqsave (&tt->lock, flags);
	list_add_tail (&clear->clear_list, &tt->clear_list);
	schedule_work (&tt->kevent);
	spin_unlock_irqrestore (&tt->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_hub_tt_clear_buffer);

/* If do_delay is false, return the number of milliseconds the caller
 * needs to delay.
 */
static unsigned hub_power_on(struct usb_hub *hub, bool do_delay)
{
	int port1;
	unsigned pgood_delay = hub->descriptor->bPwrOn2PwrGood * 2;
	unsigned delay;
	u16 wHubCharacteristics =
			le16_to_cpu(hub->descriptor->wHubCharacteristics);

	/* Enable power on each port.  Some hubs have reserved values
	 * of LPSM (> 2) in their descriptors, even though they are
	 * USB 2.0 hubs.  Some hubs do not implement port-power switching
	 * but only emulate it.  In all cases, the ports won't work
	 * unless we send these messages to the hub.
	 */
	if ((wHubCharacteristics & HUB_CHAR_LPSM) < 2)
		dev_dbg(hub->intfdev, "enabling power on all ports\n");
	else
		dev_dbg(hub->intfdev, "trying to enable port power on "
				"non-switchable hub\n");
	for (port1 = 1; port1 <= hub->descriptor->bNbrPorts; port1++)
		set_port_feature(hub->hdev, port1, USB_PORT_FEAT_POWER);

	/* Wait at least 100 msec for power to become stable */
	delay = max(pgood_delay, (unsigned) 100);
	if (do_delay)
		msleep(delay);
	return delay;
}

static int hub_hub_status(struct usb_hub *hub,
		u16 *status, u16 *change)
{
	int ret;

	mutex_lock(&hub->status_mutex);
	ret = get_hub_status(hub->hdev, &hub->status->hub);
	if (ret < 0)
		dev_err (hub->intfdev,
			"%s failed (err = %d)\n", __func__, ret);
	else {
		*status = le16_to_cpu(hub->status->hub.wHubStatus);
		*change = le16_to_cpu(hub->status->hub.wHubChange); 
		ret = 0;
	}
	mutex_unlock(&hub->status_mutex);
	return ret;
}

static int hub_port_disable(struct usb_hub *hub, int port1, int set_state)
{
	struct usb_device *hdev = hub->hdev;
	int ret = 0;

	if (hdev->children[port1-1] && set_state)
		usb_set_device_state(hdev->children[port1-1],
				USB_STATE_NOTATTACHED);
	if (!hub->error)
		ret = clear_port_feature(hdev, port1, USB_PORT_FEAT_ENABLE);
	if (ret)
		dev_err(hub->intfdev, "cannot disable port %d (err = %d)\n",
				port1, ret);
	return ret;
}

/*
 * Disable a port and mark a logical connnect-change event, so that some
 * time later khubd will disconnect() any existing usb_device on the port
 * and will re-enumerate if there actually is a device attached.
 */
static void hub_port_logical_disconnect(struct usb_hub *hub, int port1)
{
	dev_dbg(hub->intfdev, "logical disconnect on port %d\n", port1);
	hub_port_disable(hub, port1, 1);

	/* FIXME let caller ask to power down the port:
	 *  - some devices won't enumerate without a VBUS power cycle
	 *  - SRP saves power that way
	 *  - ... new call, TBD ...
	 * That's easy if this hub can switch power per-port, and
	 * khubd reactivates the port later (timer, SRP, etc).
	 * Powerdown must be optional, because of reset/DFU.
	 */

	set_bit(port1, hub->change_bits);
 	kick_khubd(hub);
}

enum hub_activation_type {
	HUB_INIT, HUB_INIT2, HUB_INIT3,
	HUB_POST_RESET, HUB_RESUME, HUB_RESET_RESUME,
};

static void hub_init_func2(struct work_struct *ws);
static void hub_init_func3(struct work_struct *ws);

static void hub_activate(struct usb_hub *hub, enum hub_activation_type type)
{
	struct usb_device *hdev = hub->hdev;
	int port1;
	int status;
	bool need_debounce_delay = false;
	unsigned delay;

	/* Continue a partial initialization */
	if (type == HUB_INIT2)
		goto init2;
	if (type == HUB_INIT3)
		goto init3;

	/* After a resume, port power should still be on.
	 * For any other type of activation, turn it on.
	 */
	if (type != HUB_RESUME) {

		/* Speed up system boot by using a delayed_work for the
		 * hub's initial power-up delays.  This is pretty awkward
		 * and the implementation looks like a home-brewed sort of
		 * setjmp/longjmp, but it saves at least 100 ms for each
		 * root hub (assuming usbcore is compiled into the kernel
		 * rather than as a module).  It adds up.
		 *
		 * This can't be done for HUB_RESUME or HUB_RESET_RESUME
		 * because for those activation types the ports have to be
		 * operational when we return.  In theory this could be done
		 * for HUB_POST_RESET, but it's easier not to.
		 */
		if (type == HUB_INIT) {
			delay = hub_power_on(hub, false);
			PREPARE_DELAYED_WORK(&hub->init_work, hub_init_func2);
			schedule_delayed_work(&hub->init_work,
					msecs_to_jiffies(delay));

			/* Suppress autosuspend until init is done */
			to_usb_interface(hub->intfdev)->pm_usage_cnt = 1;
			return;		/* Continues at init2: below */
		} else {
			hub_power_on(hub, true);
		}
	}
 init2:

	/* Check each port and set hub->change_bits to let khubd know
	 * which ports need attention.
	 */
	for (port1 = 1; port1 <= hdev->maxchild; ++port1) {
		struct usb_device *udev = hdev->children[port1-1];
		u16 portstatus, portchange;

		portstatus = portchange = 0;
		status = hub_port_status(hub, port1, &portstatus, &portchange);
		if (udev || (portstatus & USB_PORT_STAT_CONNECTION))
			dev_dbg(hub->intfdev,
					"port %d: status %04x change %04x\n",
					port1, portstatus, portchange);

		/* After anything other than HUB_RESUME (i.e., initialization
		 * or any sort of reset), every port should be disabled.
		 * Unconnected ports should likewise be disabled (paranoia),
		 * and so should ports for which we have no usb_device.
		 */
		if ((portstatus & USB_PORT_STAT_ENABLE) && (
				type != HUB_RESUME ||
				!(portstatus & USB_PORT_STAT_CONNECTION) ||
				!udev ||
				udev->state == USB_STATE_NOTATTACHED)) {
			clear_port_feature(hdev, port1, USB_PORT_FEAT_ENABLE);
			portstatus &= ~USB_PORT_STAT_ENABLE;
		}

		/* Clear status-change flags; we'll debounce later */
		if (portchange & USB_PORT_STAT_C_CONNECTION) {
			need_debounce_delay = true;
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_CONNECTION);
		}
		if (portchange & USB_PORT_STAT_C_ENABLE) {
			need_debounce_delay = true;
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_ENABLE);
		}

		if (!udev || udev->state == USB_STATE_NOTATTACHED) {
			/* Tell khubd to disconnect the device or
			 * check for a new connection
			 */
			if (udev || (portstatus & USB_PORT_STAT_CONNECTION))
				set_bit(port1, hub->change_bits);

		} else if (portstatus & USB_PORT_STAT_ENABLE) {
			/* The power session apparently survived the resume.
			 * If there was an overcurrent or suspend change
			 * (i.e., remote wakeup request), have khubd
			 * take care of it.
			 */
			if (portchange)
				set_bit(port1, hub->change_bits);

		} else if (udev->persist_enabled) {
#ifdef CONFIG_PM
			udev->reset_resume = 1;
#endif
			set_bit(port1, hub->change_bits);

		} else {
			/* The power session is gone; tell khubd */
			usb_set_device_state(udev, USB_STATE_NOTATTACHED);
			set_bit(port1, hub->change_bits);
		}
	}

	/* If no port-status-change flags were set, we don't need any
	 * debouncing.  If flags were set we can try to debounce the
	 * ports all at once right now, instead of letting khubd do them
	 * one at a time later on.
	 *
	 * If any port-status changes do occur during this delay, khubd
	 * will see them later and handle them normally.
	 */
	if (need_debounce_delay) {
		delay = HUB_DEBOUNCE_STABLE;

		/* Don't do a long sleep inside a workqueue routine */
		if (type == HUB_INIT2) {
			PREPARE_DELAYED_WORK(&hub->init_work, hub_init_func3);
			schedule_delayed_work(&hub->init_work,
					msecs_to_jiffies(delay));
			return;		/* Continues at init3: below */
		} else {
			msleep(delay);
		}
	}
 init3:
	hub->quiescing = 0;

	status = usb_submit_urb(hub->urb, GFP_NOIO);
	if (status < 0)
		dev_err(hub->intfdev, "activate --> %d\n", status);
	if (hub->has_indicators && blinkenlights)
		schedule_delayed_work(&hub->leds, LED_CYCLE_PERIOD);

	/* Scan all ports that need attention */
	kick_khubd(hub);
}

/* Implement the continuations for the delays above */
static void hub_init_func2(struct work_struct *ws)
{
	struct usb_hub *hub = container_of(ws, struct usb_hub, init_work.work);

	hub_activate(hub, HUB_INIT2);
}

static void hub_init_func3(struct work_struct *ws)
{
	struct usb_hub *hub = container_of(ws, struct usb_hub, init_work.work);

	hub_activate(hub, HUB_INIT3);
}

enum hub_quiescing_type {
	HUB_DISCONNECT, HUB_PRE_RESET, HUB_SUSPEND
};

static void hub_quiesce(struct usb_hub *hub, enum hub_quiescing_type type)
{
	struct usb_device *hdev = hub->hdev;
	int i;

	cancel_delayed_work_sync(&hub->init_work);

	/* khubd and related activity won't re-trigger */
	hub->quiescing = 1;

	if (type != HUB_SUSPEND) {
		/* Disconnect all the children */
		for (i = 0; i < hdev->maxchild; ++i) {
			if (hdev->children[i])
				usb_disconnect(&hdev->children[i]);
		}
	}

	/* Stop khubd and related activity */
	usb_kill_urb(hub->urb);
	if (hub->has_indicators)
		cancel_delayed_work_sync(&hub->leds);
	if (hub->tt.hub)
		cancel_work_sync(&hub->tt.kevent);
}

/* caller has locked the hub device */
static int hub_pre_reset(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata(intf);

	hub_quiesce(hub, HUB_PRE_RESET);
	return 0;
}

/* caller has locked the hub device */
static int hub_post_reset(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata(intf);

	hub_activate(hub, HUB_POST_RESET);
	return 0;
}

static int hub_configure(struct usb_hub *hub,
	struct usb_endpoint_descriptor *endpoint)
{
	struct usb_device *hdev = hub->hdev;
	struct device *hub_dev = hub->intfdev;
	u16 hubstatus, hubchange;
	u16 wHubCharacteristics;
	unsigned int pipe;
	int maxp, ret;
	char *message;

	hub->buffer = usb_buffer_alloc(hdev, sizeof(*hub->buffer), GFP_KERNEL,
			&hub->buffer_dma);
	if (!hub->buffer) {
		message = "can't allocate hub irq buffer";
		ret = -ENOMEM;
		goto fail;
	}

	hub->status = kmalloc(sizeof(*hub->status), GFP_KERNEL);
	if (!hub->status) {
		message = "can't kmalloc hub status buffer";
		ret = -ENOMEM;
		goto fail;
	}
	mutex_init(&hub->status_mutex);

	hub->descriptor = kmalloc(sizeof(*hub->descriptor), GFP_KERNEL);
	if (!hub->descriptor) {
		message = "can't kmalloc hub descriptor";
		ret = -ENOMEM;
		goto fail;
	}

	/* Request the entire hub descriptor.
	 * hub->descriptor can handle USB_MAXCHILDREN ports,
	 * but the hub can/will return fewer bytes here.
	 */
	ret = get_hub_descriptor(hdev, hub->descriptor,
			sizeof(*hub->descriptor));
	if (ret < 0) {
		message = "can't read hub descriptor";
		goto fail;
	} else if (hub->descriptor->bNbrPorts > USB_MAXCHILDREN) {
		message = "hub has too many ports!";
		ret = -ENODEV;
		goto fail;
	}

	hdev->maxchild = hub->descriptor->bNbrPorts;
	dev_info (hub_dev, "%d port%s detected\n", hdev->maxchild,
		(hdev->maxchild == 1) ? "" : "s");

	wHubCharacteristics = le16_to_cpu(hub->descriptor->wHubCharacteristics);

	if (wHubCharacteristics & HUB_CHAR_COMPOUND) {
		int	i;
		char	portstr [USB_MAXCHILDREN + 1];

		for (i = 0; i < hdev->maxchild; i++)
			portstr[i] = hub->descriptor->DeviceRemovable
				    [((i + 1) / 8)] & (1 << ((i + 1) % 8))
				? 'F' : 'R';
		portstr[hdev->maxchild] = 0;
		dev_dbg(hub_dev, "compound device; port removable status: %s\n", portstr);
	} else
		dev_dbg(hub_dev, "standalone hub\n");

	switch (wHubCharacteristics & HUB_CHAR_LPSM) {
		case 0x00:
			dev_dbg(hub_dev, "ganged power switching\n");
			break;
		case 0x01:
			dev_dbg(hub_dev, "individual port power switching\n");
			break;
		case 0x02:
		case 0x03:
			dev_dbg(hub_dev, "no power switching (usb 1.0)\n");
			break;
	}

	switch (wHubCharacteristics & HUB_CHAR_OCPM) {
		case 0x00:
			dev_dbg(hub_dev, "global over-current protection\n");
			break;
		case 0x08:
			dev_dbg(hub_dev, "individual port over-current protection\n");
			break;
		case 0x10:
		case 0x18:
			dev_dbg(hub_dev, "no over-current protection\n");
                        break;
	}

	spin_lock_init (&hub->tt.lock);
	INIT_LIST_HEAD (&hub->tt.clear_list);
	INIT_WORK (&hub->tt.kevent, hub_tt_kevent);
	switch (hdev->descriptor.bDeviceProtocol) {
		case 0:
			break;
		case 1:
			dev_dbg(hub_dev, "Single TT\n");
			hub->tt.hub = hdev;
			break;
		case 2:
			ret = usb_set_interface(hdev, 0, 1);
			if (ret == 0) {
				dev_dbg(hub_dev, "TT per port\n");
				hub->tt.multi = 1;
			} else
				dev_err(hub_dev, "Using single TT (err %d)\n",
					ret);
			hub->tt.hub = hdev;
			break;
		case 3:
			/* USB 3.0 hubs don't have a TT */
			break;
		default:
			dev_dbg(hub_dev, "Unrecognized hub protocol %d\n",
				hdev->descriptor.bDeviceProtocol);
			break;
	}

	/* Note 8 FS bit times == (8 bits / 12000000 bps) ~= 666ns */
	switch (wHubCharacteristics & HUB_CHAR_TTTT) {
		case HUB_TTTT_8_BITS:
			if (hdev->descriptor.bDeviceProtocol != 0) {
				hub->tt.think_time = 666;
				dev_dbg(hub_dev, "TT requires at most %d "
						"FS bit times (%d ns)\n",
					8, hub->tt.think_time);
			}
			break;
		case HUB_TTTT_16_BITS:
			hub->tt.think_time = 666 * 2;
			dev_dbg(hub_dev, "TT requires at most %d "
					"FS bit times (%d ns)\n",
				16, hub->tt.think_time);
			break;
		case HUB_TTTT_24_BITS:
			hub->tt.think_time = 666 * 3;
			dev_dbg(hub_dev, "TT requires at most %d "
					"FS bit times (%d ns)\n",
				24, hub->tt.think_time);
			break;
		case HUB_TTTT_32_BITS:
			hub->tt.think_time = 666 * 4;
			dev_dbg(hub_dev, "TT requires at most %d "
					"FS bit times (%d ns)\n",
				32, hub->tt.think_time);
			break;
	}

	/* probe() zeroes hub->indicator[] */
	if (wHubCharacteristics & HUB_CHAR_PORTIND) {
		hub->has_indicators = 1;
		dev_dbg(hub_dev, "Port indicators are supported\n");
	}

	dev_dbg(hub_dev, "power on to power good time: %dms\n",
		hub->descriptor->bPwrOn2PwrGood * 2);

	/* power budgeting mostly matters with bus-powered hubs,
	 * and battery-powered root hubs (may provide just 8 mA).
	 */
	ret = usb_get_status(hdev, USB_RECIP_DEVICE, 0, &hubstatus);
	if (ret < 2) {
		message = "can't get hub status";
		goto fail;
	}
	le16_to_cpus(&hubstatus);
	if (hdev == hdev->bus->root_hub) {
		if (hdev->bus_mA == 0 || hdev->bus_mA >= 500)
			hub->mA_per_port = 500;
		else {
			hub->mA_per_port = hdev->bus_mA;
			hub->limited_power = 1;
		}
	} else if ((hubstatus & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
		dev_dbg(hub_dev, "hub controller current requirement: %dmA\n",
			hub->descriptor->bHubContrCurrent);
		hub->limited_power = 1;
		if (hdev->maxchild > 0) {
			int remaining = hdev->bus_mA -
					hub->descriptor->bHubContrCurrent;

			if (remaining < hdev->maxchild * 100)
				dev_warn(hub_dev,
					"insufficient power available "
					"to use all downstream ports\n");
			hub->mA_per_port = 100;		/* 7.2.1.1 */
		}
	} else {	/* Self-powered external hub */
		/* FIXME: What about battery-powered external hubs that
		 * provide less current per port? */
		hub->mA_per_port = 500;
	}
	if (hub->mA_per_port < 500)
		dev_dbg(hub_dev, "%umA bus power budget for each child\n",
				hub->mA_per_port);

	ret = hub_hub_status(hub, &hubstatus, &hubchange);
	if (ret < 0) {
		message = "can't get hub status";
		goto fail;
	}

	/* local power status reports aren't always correct */
	if (hdev->actconfig->desc.bmAttributes & USB_CONFIG_ATT_SELFPOWER)
		dev_dbg(hub_dev, "local power source is %s\n",
			(hubstatus & HUB_STATUS_LOCAL_POWER)
			? "lost (inactive)" : "good");

	if ((wHubCharacteristics & HUB_CHAR_OCPM) == 0)
		dev_dbg(hub_dev, "%sover-current condition exists\n",
			(hubstatus & HUB_STATUS_OVERCURRENT) ? "" : "no ");

	/* set up the interrupt endpoint
	 * We use the EP's maxpacket size instead of (PORTS+1+7)/8
	 * bytes as USB2.0[11.12.3] says because some hubs are known
	 * to send more data (and thus cause overflow). For root hubs,
	 * maxpktsize is defined in hcd.c's fake endpoint descriptors
	 * to be big enough for at least USB_MAXCHILDREN ports. */
	pipe = usb_rcvintpipe(hdev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(hdev, pipe, usb_pipeout(pipe));

	if (maxp > sizeof(*hub->buffer))
		maxp = sizeof(*hub->buffer);

	hub->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!hub->urb) {
		message = "couldn't allocate interrupt urb";
		ret = -ENOMEM;
		goto fail;
	}

	usb_fill_int_urb(hub->urb, hdev, pipe, *hub->buffer, maxp, hub_irq,
		hub, endpoint->bInterval);
	hub->urb->transfer_dma = hub->buffer_dma;
	hub->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* maybe cycle the hub leds */
	if (hub->has_indicators && blinkenlights)
		hub->indicator [0] = INDICATOR_CYCLE;

	hub_activate(hub, HUB_INIT);
	return 0;

fail:
	dev_err (hub_dev, "config failed, %s (err %d)\n",
			message, ret);
	/* hub_disconnect() frees urb and descriptor */
	return ret;
}

static void hub_release(struct kref *kref)
{
	struct usb_hub *hub = container_of(kref, struct usb_hub, kref);

	usb_put_intf(to_usb_interface(hub->intfdev));
	kfree(hub);
}

static unsigned highspeed_hubs;

static void hub_disconnect(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata (intf);

	/* Take the hub off the event list and don't let it be added again */
	spin_lock_irq(&hub_event_lock);
	list_del_init(&hub->event_list);
	hub->disconnected = 1;
	spin_unlock_irq(&hub_event_lock);

	/* Disconnect all children and quiesce the hub */
	hub->error = 0;
	hub_quiesce(hub, HUB_DISCONNECT);

	usb_set_intfdata (intf, NULL);

	if (hub->hdev->speed == USB_SPEED_HIGH)
		highspeed_hubs--;

	usb_free_urb(hub->urb);
	kfree(hub->descriptor);
	kfree(hub->status);
	usb_buffer_free(hub->hdev, sizeof(*hub->buffer), hub->buffer,
			hub->buffer_dma);

	kref_put(&hub->kref, hub_release);
}

static int hub_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_host_interface *desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *hdev;
	struct usb_hub *hub;

	desc = intf->cur_altsetting;
	hdev = interface_to_usbdev(intf);

	if (hdev->level == MAX_TOPO_LEVEL) {
		dev_err(&intf->dev,
			"Unsupported bus topology: hub nested too deep\n");
		return -E2BIG;
	}

#ifdef	CONFIG_USB_OTG_BLACKLIST_HUB
	if (hdev->parent) {
		dev_warn(&intf->dev, "ignoring external hub\n");
		return -ENODEV;
	}
#endif

	/* Some hubs have a subclass of 1, which AFAICT according to the */
	/*  specs is not defined, but it works */
	if ((desc->desc.bInterfaceSubClass != 0) &&
	    (desc->desc.bInterfaceSubClass != 1)) {
descriptor_error:
		dev_err (&intf->dev, "bad descriptor, ignoring hub\n");
		return -EIO;
	}

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (desc->desc.bNumEndpoints != 1)
		goto descriptor_error;

	endpoint = &desc->endpoint[0].desc;

	/* If it's not an interrupt in endpoint, we'd better punt! */
	if (!usb_endpoint_is_int_in(endpoint))
		goto descriptor_error;

	/* We found a hub */
	dev_info (&intf->dev, "USB hub found\n");

	hub = kzalloc(sizeof(*hub), GFP_KERNEL);
	if (!hub) {
		dev_dbg (&intf->dev, "couldn't kmalloc hub struct\n");
		return -ENOMEM;
	}

	kref_init(&hub->kref);
	INIT_LIST_HEAD(&hub->event_list);
	hub->intfdev = &intf->dev;
	hub->hdev = hdev;
	INIT_DELAYED_WORK(&hub->leds, led_work);
	INIT_DELAYED_WORK(&hub->init_work, NULL);
	usb_get_intf(intf);

	usb_set_intfdata (intf, hub);
	intf->needs_remote_wakeup = 1;

	if (hdev->speed == USB_SPEED_HIGH)
		highspeed_hubs++;

	if (hub_configure(hub, endpoint) >= 0)
		return 0;

	hub_disconnect (intf);
	return -ENODEV;
}

static int
hub_ioctl(struct usb_interface *intf, unsigned int code, void *user_data)
{
	struct usb_device *hdev = interface_to_usbdev (intf);

	/* assert ifno == 0 (part of hub spec) */
	switch (code) {
	case USBDEVFS_HUB_PORTINFO: {
		struct usbdevfs_hub_portinfo *info = user_data;
		int i;

		spin_lock_irq(&device_state_lock);
		if (hdev->devnum <= 0)
			info->nports = 0;
		else {
			info->nports = hdev->maxchild;
			for (i = 0; i < info->nports; i++) {
				if (hdev->children[i] == NULL)
					info->port[i] = 0;
				else
					info->port[i] =
						hdev->children[i]->devnum;
			}
		}
		spin_unlock_irq(&device_state_lock);

		return info->nports + 1;
		}

	default:
		return -ENOSYS;
	}
}


static void recursively_mark_NOTATTACHED(struct usb_device *udev)
{
	int i;

	for (i = 0; i < udev->maxchild; ++i) {
		if (udev->children[i])
			recursively_mark_NOTATTACHED(udev->children[i]);
	}
	if (udev->state == USB_STATE_SUSPENDED) {
		udev->discon_suspended = 1;
		udev->active_duration -= jiffies;
	}
	udev->state = USB_STATE_NOTATTACHED;
}

/**
 * usb_set_device_state - change a device's current state (usbcore, hcds)
 * @udev: pointer to device whose state should be changed
 * @new_state: new state value to be stored
 *
 * udev->state is _not_ fully protected by the device lock.  Although
 * most transitions are made only while holding the lock, the state can
 * can change to USB_STATE_NOTATTACHED at almost any time.  This
 * is so that devices can be marked as disconnected as soon as possible,
 * without having to wait for any semaphores to be released.  As a result,
 * all changes to any device's state must be protected by the
 * device_state_lock spinlock.
 *
 * Once a device has been added to the device tree, all changes to its state
 * should be made using this routine.  The state should _not_ be set directly.
 *
 * If udev->state is already USB_STATE_NOTATTACHED then no change is made.
 * Otherwise udev->state is set to new_state, and if new_state is
 * USB_STATE_NOTATTACHED then all of udev's descendants' states are also set
 * to USB_STATE_NOTATTACHED.
 */
void usb_set_device_state(struct usb_device *udev,
		enum usb_device_state new_state)
{
	unsigned long flags;

	spin_lock_irqsave(&device_state_lock, flags);
	if (udev->state == USB_STATE_NOTATTACHED)
		;	/* do nothing */
	else if (new_state != USB_STATE_NOTATTACHED) {

		/* root hub wakeup capabilities are managed out-of-band
		 * and may involve silicon errata ... ignore them here.
		 */
		if (udev->parent) {
			if (udev->state == USB_STATE_SUSPENDED
					|| new_state == USB_STATE_SUSPENDED)
				;	/* No change to wakeup settings */
			else if (new_state == USB_STATE_CONFIGURED)
				device_init_wakeup(&udev->dev,
					(udev->actconfig->desc.bmAttributes
					 & USB_CONFIG_ATT_WAKEUP));
			else
				device_init_wakeup(&udev->dev, 0);
		}
		if (udev->state == USB_STATE_SUSPENDED &&
			new_state != USB_STATE_SUSPENDED)
			udev->active_duration -= jiffies;
		else if (new_state == USB_STATE_SUSPENDED &&
				udev->state != USB_STATE_SUSPENDED)
			udev->active_duration += jiffies;
		udev->state = new_state;
	} else
		recursively_mark_NOTATTACHED(udev);
	spin_unlock_irqrestore(&device_state_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_set_device_state);

/*
 * WUSB devices are simple: they have no hubs behind, so the mapping
 * device <-> virtual port number becomes 1:1. Why? to simplify the
 * life of the device connection logic in
 * drivers/usb/wusbcore/devconnect.c. When we do the initial secret
 * handshake we need to assign a temporary address in the unauthorized
 * space. For simplicity we use the first virtual port number found to
 * be free [drivers/usb/wusbcore/devconnect.c:wusbhc_devconnect_ack()]
 * and that becomes it's address [X < 128] or its unauthorized address
 * [X | 0x80].
 *
 * We add 1 as an offset to the one-based USB-stack port number
 * (zero-based wusb virtual port index) for two reasons: (a) dev addr
 * 0 is reserved by USB for default address; (b) Linux's USB stack
 * uses always #1 for the root hub of the controller. So USB stack's
 * port #1, which is wusb virtual-port #0 has address #2.
 */
static void choose_address(struct usb_device *udev)
{
	int		devnum;
	struct usb_bus	*bus = udev->bus;

	/* If khubd ever becomes multithreaded, this will need a lock */
	if (udev->wusb) {
		devnum = udev->portnum + 1;
		BUG_ON(test_bit(devnum, bus->devmap.devicemap));
	} else {
		/* Try to allocate the next devnum beginning at
		 * bus->devnum_next. */
		devnum = find_next_zero_bit(bus->devmap.devicemap, 128,
					    bus->devnum_next);
		if (devnum >= 128)
			devnum = find_next_zero_bit(bus->devmap.devicemap,
						    128, 1);
		bus->devnum_next = ( devnum >= 127 ? 1 : devnum + 1);
	}
	if (devnum < 128) {
		set_bit(devnum, bus->devmap.devicemap);
		udev->devnum = devnum;
	}
}

static void release_address(struct usb_device *udev)
{
	if (udev->devnum > 0) {
		clear_bit(udev->devnum, udev->bus->devmap.devicemap);
		udev->devnum = -1;
	}
}

static void update_address(struct usb_device *udev, int devnum)
{
	/* The address for a WUSB device is managed by wusbcore. */
	if (!udev->wusb)
		udev->devnum = devnum;
}

#ifdef	CONFIG_USB_SUSPEND

static void usb_stop_pm(struct usb_device *udev)
{
	/* Synchronize with the ksuspend thread to prevent any more
	 * autosuspend requests from being submitted, and decrement
	 * the parent's count of unsuspended children.
	 */
	usb_pm_lock(udev);
	if (udev->parent && !udev->discon_suspended)
		usb_autosuspend_device(udev->parent);
	usb_pm_unlock(udev);

	/* Stop any autosuspend or autoresume requests already submitted */
	cancel_delayed_work_sync(&udev->autosuspend);
	cancel_work_sync(&udev->autoresume);
}

#else

static inline void usb_stop_pm(struct usb_device *udev)
{ }

#endif

/**
 * usb_disconnect - disconnect a device (usbcore-internal)
 * @pdev: pointer to device being disconnected
 * Context: !in_interrupt ()
 *
 * Something got disconnected. Get rid of it and all of its children.
 *
 * If *pdev is a normal device then the parent hub must already be locked.
 * If *pdev is a root hub then this routine will acquire the
 * usb_bus_list_lock on behalf of the caller.
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 */
void usb_disconnect(struct usb_device **pdev)
{
	struct usb_device	*udev = *pdev;
	int			i;

	if (!udev) {
		pr_debug ("%s nodev\n", __func__);
		return;
	}

	/* mark the device as inactive, so any further urb submissions for
	 * this device (and any of its children) will fail immediately.
	 * this quiesces everyting except pending urbs.
	 */
	usb_set_device_state(udev, USB_STATE_NOTATTACHED);
	dev_info (&udev->dev, "USB disconnect, address %d\n", udev->devnum);

	usb_lock_device(udev);

	/* Free up all the children before we remove this device */
	for (i = 0; i < USB_MAXCHILDREN; i++) {
		if (udev->children[i])
			usb_disconnect(&udev->children[i]);
	}

	/* deallocate hcd/hardware state ... nuking all pending urbs and
	 * cleaning up all state associated with the current configuration
	 * so that the hardware is now fully quiesced.
	 */
	dev_dbg (&udev->dev, "unregistering device\n");
	usb_disable_device(udev, 0);
	usb_hcd_synchronize_unlinks(udev);

	usb_remove_ep_devs(&udev->ep0);
	usb_unlock_device(udev);

	/* Unregister the device.  The device driver is responsible
	 * for de-configuring the device and invoking the remove-device
	 * notifier chain (used by usbfs and possibly others).
	 */
	device_del(&udev->dev);

	/* Free the device number and delete the parent's children[]
	 * (or root_hub) pointer.
	 */
	release_address(udev);

	/* Avoid races with recursively_mark_NOTATTACHED() */
	spin_lock_irq(&device_state_lock);
	*pdev = NULL;
	spin_unlock_irq(&device_state_lock);

	usb_stop_pm(udev);

	put_device(&udev->dev);
}

#ifdef CONFIG_USB_ANNOUNCE_NEW_DEVICES
static void show_string(struct usb_device *udev, char *id, char *string)
{
	if (!string)
		return;
	dev_printk(KERN_INFO, &udev->dev, "%s: %s\n", id, string);
}

static void announce_device(struct usb_device *udev)
{
	dev_info(&udev->dev, "New USB device found, idVendor=%04x, idProduct=%04x\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct));
	dev_info(&udev->dev,
		"New USB device strings: Mfr=%d, Product=%d, SerialNumber=%d\n",
		udev->descriptor.iManufacturer,
		udev->descriptor.iProduct,
		udev->descriptor.iSerialNumber);
	show_string(udev, "Product", udev->product);
	show_string(udev, "Manufacturer", udev->manufacturer);
	show_string(udev, "SerialNumber", udev->serial);
}
#else
static inline void announce_device(struct usb_device *udev) { }
#endif

#ifdef	CONFIG_USB_OTG
#include "otg_whitelist.h"
#endif

/**
 * usb_configure_device_otg - FIXME (usbcore-internal)
 * @udev: newly addressed device (in ADDRESS state)
 *
 * Do configuration for On-The-Go devices
 */
static int usb_configure_device_otg(struct usb_device *udev)
{
	int err = 0;

#ifdef	CONFIG_USB_OTG
	/*
	 * OTG-aware devices on OTG-capable root hubs may be able to use SRP,
	 * to wake us after we've powered off VBUS; and HNP, switching roles
	 * "host" to "peripheral".  The OTG descriptor helps figure this out.
	 */
	if (!udev->bus->is_b_host
			&& udev->config
			&& udev->parent == udev->bus->root_hub) {
		struct usb_otg_descriptor	*desc = 0;
		struct usb_bus			*bus = udev->bus;

		/* descriptor may appear anywhere in config */
		if (__usb_get_extra_descriptor (udev->rawdescriptors[0],
					le16_to_cpu(udev->config[0].desc.wTotalLength),
					USB_DT_OTG, (void **) &desc) == 0) {
			if (desc->bmAttributes & USB_OTG_HNP) {
				unsigned		port1 = udev->portnum;

				dev_info(&udev->dev,
					"Dual-Role OTG device on %sHNP port\n",
					(port1 == bus->otg_port)
						? "" : "non-");

				/* enable HNP before suspend, it's simpler */
				if (port1 == bus->otg_port)
					bus->b_hnp_enable = 1;
				err = usb_control_msg(udev,
					usb_sndctrlpipe(udev, 0),
					USB_REQ_SET_FEATURE, 0,
					bus->b_hnp_enable
						? USB_DEVICE_B_HNP_ENABLE
						: USB_DEVICE_A_ALT_HNP_SUPPORT,
					0, NULL, 0, USB_CTRL_SET_TIMEOUT);
				if (err < 0) {
					/* OTG MESSAGE: report errors here,
					 * customize to match your product.
					 */
					dev_info(&udev->dev,
						"can't set HNP mode: %d\n",
						err);
					bus->b_hnp_enable = 0;
				}
			}
		}
	}

	if (!is_targeted(udev)) {

		/* Maybe it can talk to us, though we can't talk to it.
		 * (Includes HNP test device.)
		 */
		if (udev->bus->b_hnp_enable || udev->bus->is_b_host) {
			err = usb_port_suspend(udev, PMSG_SUSPEND);
			if (err < 0)
				dev_dbg(&udev->dev, "HNP fail, %d\n", err);
		}
		err = -ENOTSUPP;
		goto fail;
	}
fail:
#endif
	return err;
}


/**
 * usb_configure_device - Detect and probe device intfs/otg (usbcore-internal)
 * @udev: newly addressed device (in ADDRESS state)
 *
 * This is only called by usb_new_device() and usb_authorize_device()
 * and FIXME -- all comments that apply to them apply here wrt to
 * environment.
 *
 * If the device is WUSB and not authorized, we don't attempt to read
 * the string descriptors, as they will be errored out by the device
 * until it has been authorized.
 */
static int usb_configure_device(struct usb_device *udev)
{
	int err;

	if (udev->config == NULL) {
		err = usb_get_configuration(udev);
		if (err < 0) {
			dev_err(&udev->dev, "can't read configurations, error %d\n",
				err);
			goto fail;
		}
	}
	if (udev->wusb == 1 && udev->authorized == 0) {
		udev->product = kstrdup("n/a (unauthorized)", GFP_KERNEL);
		udev->manufacturer = kstrdup("n/a (unauthorized)", GFP_KERNEL);
		udev->serial = kstrdup("n/a (unauthorized)", GFP_KERNEL);
	}
	else {
		/* read the standard strings and cache them if present */
		udev->product = usb_cache_string(udev, udev->descriptor.iProduct);
		udev->manufacturer = usb_cache_string(udev,
						      udev->descriptor.iManufacturer);
		udev->serial = usb_cache_string(udev, udev->descriptor.iSerialNumber);
	}
	err = usb_configure_device_otg(udev);
fail:
	return err;
}


/**
 * usb_new_device - perform initial device setup (usbcore-internal)
 * @udev: newly addressed device (in ADDRESS state)
 *
 * This is called with devices which have been enumerated, but not yet
 * configured.  The device descriptor is available, but not descriptors
 * for any device configuration.  The caller must have locked either
 * the parent hub (if udev is a normal device) or else the
 * usb_bus_list_lock (if udev is a root hub).  The parent's pointer to
 * udev has already been installed, but udev is not yet visible through
 * sysfs or other filesystem code.
 *
 * It will return if the device is configured properly or not.  Zero if
 * the interface was registered with the driver core; else a negative
 * errno value.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Only the hub driver or root-hub registrar should ever call this.
 */
int usb_new_device(struct usb_device *udev)
{
	int err;

	/* Increment the parent's count of unsuspended children */
	if (udev->parent)
		usb_autoresume_device(udev->parent);

	usb_detect_quirks(udev);		/* Determine quirks */
	err = usb_configure_device(udev);	/* detect & probe dev/intfs */
	if (err < 0)
		goto fail;
	/* export the usbdev device-node for libusb */
	udev->dev.devt = MKDEV(USB_DEVICE_MAJOR,
			(((udev->bus->busnum-1) * 128) + (udev->devnum-1)));

	/* Tell the world! */
	announce_device(udev);

	/* Register the device.  The device driver is responsible
	 * for configuring the device and invoking the add-device
	 * notifier chain (used by usbfs and possibly others).
	 */
	err = device_add(&udev->dev);
	if (err) {
		dev_err(&udev->dev, "can't device_add, error %d\n", err);
		goto fail;
	}

	(void) usb_create_ep_devs(&udev->dev, &udev->ep0, udev);
	return err;

fail:
	usb_set_device_state(udev, USB_STATE_NOTATTACHED);
	usb_stop_pm(udev);
	return err;
}


/**
 * usb_deauthorize_device - deauthorize a device (usbcore-internal)
 * @usb_dev: USB device
 *
 * Move the USB device to a very basic state where interfaces are disabled
 * and the device is in fact unconfigured and unusable.
 *
 * We share a lock (that we have) with device_del(), so we need to
 * defer its call.
 */
int usb_deauthorize_device(struct usb_device *usb_dev)
{
	unsigned cnt;
	usb_lock_device(usb_dev);
	if (usb_dev->authorized == 0)
		goto out_unauthorized;
	usb_dev->authorized = 0;
	usb_set_configuration(usb_dev, -1);
	usb_dev->product = kstrdup("n/a (unauthorized)", GFP_KERNEL);
	usb_dev->manufacturer = kstrdup("n/a (unauthorized)", GFP_KERNEL);
	usb_dev->serial = kstrdup("n/a (unauthorized)", GFP_KERNEL);
	kfree(usb_dev->config);
	usb_dev->config = NULL;
	for (cnt = 0; cnt < usb_dev->descriptor.bNumConfigurations; cnt++)
		kfree(usb_dev->rawdescriptors[cnt]);
	usb_dev->descriptor.bNumConfigurations = 0;
	kfree(usb_dev->rawdescriptors);
out_unauthorized:
	usb_unlock_device(usb_dev);
	return 0;
}


int usb_authorize_device(struct usb_device *usb_dev)
{
	int result = 0, c;
	usb_lock_device(usb_dev);
	if (usb_dev->authorized == 1)
		goto out_authorized;
	kfree(usb_dev->product);
	usb_dev->product = NULL;
	kfree(usb_dev->manufacturer);
	usb_dev->manufacturer = NULL;
	kfree(usb_dev->serial);
	usb_dev->serial = NULL;
	result = usb_autoresume_device(usb_dev);
	if (result < 0) {
		dev_err(&usb_dev->dev,
			"can't autoresume for authorization: %d\n", result);
		goto error_autoresume;
	}
	result = usb_get_device_descriptor(usb_dev, sizeof(usb_dev->descriptor));
	if (result < 0) {
		dev_err(&usb_dev->dev, "can't re-read device descriptor for "
			"authorization: %d\n", result);
		goto error_device_descriptor;
	}
	usb_dev->authorized = 1;
	result = usb_configure_device(usb_dev);
	if (result < 0)
		goto error_configure;
	/* Choose and set the configuration.  This registers the interfaces
	 * with the driver core and lets interface drivers bind to them.
	 */
	c = usb_choose_configuration(usb_dev);
	if (c >= 0) {
		result = usb_set_configuration(usb_dev, c);
		if (result) {
			dev_err(&usb_dev->dev,
				"can't set config #%d, error %d\n", c, result);
			/* This need not be fatal.  The user can try to
			 * set other configurations. */
		}
	}
	dev_info(&usb_dev->dev, "authorized to connect\n");
error_configure:
error_device_descriptor:
error_autoresume:
out_authorized:
	usb_unlock_device(usb_dev);	// complements locktree
	return result;
}


/* Returns 1 if @hub is a WUSB root hub, 0 otherwise */
static unsigned hub_is_wusb(struct usb_hub *hub)
{
	struct usb_hcd *hcd;
	if (hub->hdev->parent != NULL)  /* not a root hub? */
		return 0;
	hcd = container_of(hub->hdev->bus, struct usb_hcd, self);
	return hcd->wireless;
}


#define PORT_RESET_TRIES	5
#define SET_ADDRESS_TRIES	2
#define GET_DESCRIPTOR_TRIES	2
#define SET_CONFIG_TRIES	(2 * (use_both_schemes + 1))
#define USE_NEW_SCHEME(i)	((i) / 2 == old_scheme_first)

#define HUB_ROOT_RESET_TIME	50	/* times are in msec */
#define HUB_SHORT_RESET_TIME	10
#define HUB_LONG_RESET_TIME	200
#define HUB_RESET_TIMEOUT	500

static int hub_port_wait_reset(struct usb_hub *hub, int port1,
				struct usb_device *udev, unsigned int delay)
{
	int delay_time, ret;
	u16 portstatus;
	u16 portchange;

	for (delay_time = 0;
			delay_time < HUB_RESET_TIMEOUT;
			delay_time += delay) {
		/* wait to give the device a chance to reset */
		msleep(delay);

		/* read and decode port status */
		ret = hub_port_status(hub, port1, &portstatus, &portchange);
		if (ret < 0)
			return ret;

		/* Device went away? */
		if (!(portstatus & USB_PORT_STAT_CONNECTION))
			return -ENOTCONN;

		/* bomb out completely if the connection bounced */
		if ((portchange & USB_PORT_STAT_C_CONNECTION))
			return -ENOTCONN;

		/* if we`ve finished resetting, then break out of the loop */
		if (!(portstatus & USB_PORT_STAT_RESET) &&
		    (portstatus & USB_PORT_STAT_ENABLE)) {
			if (hub_is_wusb(hub))
				udev->speed = USB_SPEED_VARIABLE;
			else if (portstatus & USB_PORT_STAT_HIGH_SPEED)
				udev->speed = USB_SPEED_HIGH;
			else if (portstatus & USB_PORT_STAT_LOW_SPEED)
				udev->speed = USB_SPEED_LOW;
			else
				udev->speed = USB_SPEED_FULL;
			return 0;
		}

		/* switch to the long delay after two short delay failures */
		if (delay_time >= 2 * HUB_SHORT_RESET_TIME)
			delay = HUB_LONG_RESET_TIME;

		dev_dbg (hub->intfdev,
			"port %d not reset yet, waiting %dms\n",
			port1, delay);
	}

	return -EBUSY;
}

static int hub_port_reset(struct usb_hub *hub, int port1,
				struct usb_device *udev, unsigned int delay)
{
	int i, status;

	/* Block EHCI CF initialization during the port reset.
	 * Some companion controllers don't like it when they mix.
	 */
	down_read(&ehci_cf_port_reset_rwsem);

	/* Reset the port */
	for (i = 0; i < PORT_RESET_TRIES; i++) {
		status = set_port_feature(hub->hdev,
				port1, USB_PORT_FEAT_RESET);
		if (status)
			dev_err(hub->intfdev,
					"cannot reset port %d (err = %d)\n",
					port1, status);
		else {
			status = hub_port_wait_reset(hub, port1, udev, delay);
			if (status && status != -ENOTCONN)
				dev_dbg(hub->intfdev,
						"port_wait_reset: err = %d\n",
						status);
		}

		/* return on disconnect or reset */
		switch (status) {
		case 0:
			/* TRSTRCY = 10 ms; plus some extra */
			msleep(10 + 40);
			update_address(udev, 0);
			/* FALL THROUGH */
		case -ENOTCONN:
		case -ENODEV:
			clear_port_feature(hub->hdev,
				port1, USB_PORT_FEAT_C_RESET);
			/* FIXME need disconnect() for NOTATTACHED device */
			usb_set_device_state(udev, status
					? USB_STATE_NOTATTACHED
					: USB_STATE_DEFAULT);
			goto done;
		}

		dev_dbg (hub->intfdev,
			"port %d not enabled, trying reset again...\n",
			port1);
		delay = HUB_LONG_RESET_TIME;
	}

	dev_err (hub->intfdev,
		"Cannot enable port %i.  Maybe the USB cable is bad?\n",
		port1);

 done:
	up_read(&ehci_cf_port_reset_rwsem);
	return status;
}

#ifdef	CONFIG_PM

#define MASK_BITS	(USB_PORT_STAT_POWER | USB_PORT_STAT_CONNECTION | \
				USB_PORT_STAT_SUSPEND)
#define WANT_BITS	(USB_PORT_STAT_POWER | USB_PORT_STAT_CONNECTION)

/* Determine whether the device on a port is ready for a normal resume,
 * is ready for a reset-resume, or should be disconnected.
 */
static int check_port_resume_type(struct usb_device *udev,
		struct usb_hub *hub, int port1,
		int status, unsigned portchange, unsigned portstatus)
{
	/* Is the device still present? */
	if (status || (portstatus & MASK_BITS) != WANT_BITS) {
		if (status >= 0)
			status = -ENODEV;
	}

	/* Can't do a normal resume if the port isn't enabled,
	 * so try a reset-resume instead.
	 */
	else if (!(portstatus & USB_PORT_STAT_ENABLE) && !udev->reset_resume) {
		if (udev->persist_enabled)
			udev->reset_resume = 1;
		else
			status = -ENODEV;
	}

	if (status) {
		dev_dbg(hub->intfdev,
				"port %d status %04x.%04x after resume, %d\n",
				port1, portchange, portstatus, status);
	} else if (udev->reset_resume) {

		/* Late port handoff can set status-change bits */
		if (portchange & USB_PORT_STAT_C_CONNECTION)
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_CONNECTION);
		if (portchange & USB_PORT_STAT_C_ENABLE)
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_ENABLE);
	}

	return status;
}

#ifdef	CONFIG_USB_SUSPEND

/*
 * usb_port_suspend - suspend a usb device's upstream port
 * @udev: device that's no longer in active use, not a root hub
 * Context: must be able to sleep; device not locked; pm locks held
 *
 * Suspends a USB device that isn't in active use, conserving power.
 * Devices may wake out of a suspend, if anything important happens,
 * using the remote wakeup mechanism.  They may also be taken out of
 * suspend by the host, using usb_port_resume().  It's also routine
 * to disconnect devices while they are suspended.
 *
 * This only affects the USB hardware for a device; its interfaces
 * (and, for hubs, child devices) must already have been suspended.
 *
 * Selective port suspend reduces power; most suspended devices draw
 * less than 500 uA.  It's also used in OTG, along with remote wakeup.
 * All devices below the suspended port are also suspended.
 *
 * Devices leave suspend state when the host wakes them up.  Some devices
 * also support "remote wakeup", where the device can activate the USB
 * tree above them to deliver data, such as a keypress or packet.  In
 * some cases, this wakes the USB host.
 *
 * Suspending OTG devices may trigger HNP, if that's been enabled
 * between a pair of dual-role devices.  That will change roles, such
 * as from A-Host to A-Peripheral or from B-Host back to B-Peripheral.
 *
 * Devices on USB hub ports have only one "suspend" state, corresponding
 * to ACPI D2, "may cause the device to lose some context".
 * State transitions include:
 *
 *   - suspend, resume ... when the VBUS power link stays live
 *   - suspend, disconnect ... VBUS lost
 *
 * Once VBUS drop breaks the circuit, the port it's using has to go through
 * normal re-enumeration procedures, starting with enabling VBUS power.
 * Other than re-initializing the hub (plug/unplug, except for root hubs),
 * Linux (2.6) currently has NO mechanisms to initiate that:  no khubd
 * timer, no SRP, no requests through sysfs.
 *
 * If CONFIG_USB_SUSPEND isn't enabled, devices only really suspend when
 * the root hub for their bus goes into global suspend ... so we don't
 * (falsely) update the device power state to say it suspended.
 *
 * Returns 0 on success, else negative errno.
 */
int usb_port_suspend(struct usb_device *udev, pm_message_t msg)
{
	struct usb_hub	*hub = hdev_to_hub(udev->parent);
	int		port1 = udev->portnum;
	int		status;

	// dev_dbg(hub->intfdev, "suspend port %d\n", port1);

	/* enable remote wakeup when appropriate; this lets the device
	 * wake up the upstream hub (including maybe the root hub).
	 *
	 * NOTE:  OTG devices may issue remote wakeup (or SRP) even when
	 * we don't explicitly enable it here.
	 */
	if (udev->do_remote_wakeup) {
		status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				USB_REQ_SET_FEATURE, USB_RECIP_DEVICE,
				USB_DEVICE_REMOTE_WAKEUP, 0,
				NULL, 0,
				USB_CTRL_SET_TIMEOUT);
		if (status)
			dev_dbg(&udev->dev, "won't remote wakeup, status %d\n",
					status);
	}

	/* see 7.1.7.6 */
	status = set_port_feature(hub->hdev, port1, USB_PORT_FEAT_SUSPEND);
	if (status) {
		dev_dbg(hub->intfdev, "can't suspend port %d, status %d\n",
				port1, status);
		/* paranoia:  "should not happen" */
		(void) usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				USB_REQ_CLEAR_FEATURE, USB_RECIP_DEVICE,
				USB_DEVICE_REMOTE_WAKEUP, 0,
				NULL, 0,
				USB_CTRL_SET_TIMEOUT);
	} else {
		/* device has up to 10 msec to fully suspend */
		dev_dbg(&udev->dev, "usb %ssuspend\n",
				(msg.event & PM_EVENT_AUTO ? "auto-" : ""));
		usb_set_device_state(udev, USB_STATE_SUSPENDED);
		msleep(10);
	}
	return status;
}

/*
 * If the USB "suspend" state is in use (rather than "global suspend"),
 * many devices will be individually taken out of suspend state using
 * special "resume" signaling.  This routine kicks in shortly after
 * hardware resume signaling is finished, either because of selective
 * resume (by host) or remote wakeup (by device) ... now see what changed
 * in the tree that's rooted at this device.
 *
 * If @udev->reset_resume is set then the device is reset before the
 * status check is done.
 */
static int finish_port_resume(struct usb_device *udev)
{
	int	status = 0;
	u16	devstatus;

	/* caller owns the udev device lock */
	dev_dbg(&udev->dev, "%s\n",
		udev->reset_resume ? "finish reset-resume" : "finish resume");

	/* usb ch9 identifies four variants of SUSPENDED, based on what
	 * state the device resumes to.  Linux currently won't see the
	 * first two on the host side; they'd be inside hub_port_init()
	 * during many timeouts, but khubd can't suspend until later.
	 */
	usb_set_device_state(udev, udev->actconfig
			? USB_STATE_CONFIGURED
			: USB_STATE_ADDRESS);

	/* 10.5.4.5 says not to reset a suspended port if the attached
	 * device is enabled for remote wakeup.  Hence the reset
	 * operation is carried out here, after the port has been
	 * resumed.
	 */
	if (udev->reset_resume)
 retry_reset_resume:
		status = usb_reset_and_verify_device(udev);

 	/* 10.5.4.5 says be sure devices in the tree are still there.
 	 * For now let's assume the device didn't go crazy on resume,
	 * and device drivers will know about any resume quirks.
	 */
	if (status == 0) {
		devstatus = 0;
		status = usb_get_status(udev, USB_RECIP_DEVICE, 0, &devstatus);
		if (status >= 0)
			status = (status > 0 ? 0 : -ENODEV);

		/* If a normal resume failed, try doing a reset-resume */
		if (status && !udev->reset_resume && udev->persist_enabled) {
			dev_dbg(&udev->dev, "retry with reset-resume\n");
			udev->reset_resume = 1;
			goto retry_reset_resume;
		}
	}

	if (status) {
		dev_dbg(&udev->dev, "gone after usb resume? status %d\n",
				status);
	} else if (udev->actconfig) {
		le16_to_cpus(&devstatus);
		if (devstatus & (1 << USB_DEVICE_REMOTE_WAKEUP)) {
			status = usb_control_msg(udev,
					usb_sndctrlpipe(udev, 0),
					USB_REQ_CLEAR_FEATURE,
						USB_RECIP_DEVICE,
					USB_DEVICE_REMOTE_WAKEUP, 0,
					NULL, 0,
					USB_CTRL_SET_TIMEOUT);
			if (status)
				dev_dbg(&udev->dev,
					"disable remote wakeup, status %d\n",
					status);
		}
		status = 0;
	}
	return status;
}

/*
 * usb_port_resume - re-activate a suspended usb device's upstream port
 * @udev: device to re-activate, not a root hub
 * Context: must be able to sleep; device not locked; pm locks held
 *
 * This will re-activate the suspended device, increasing power usage
 * while letting drivers communicate again with its endpoints.
 * USB resume explicitly guarantees that the power session between
 * the host and the device is the same as it was when the device
 * suspended.
 *
 * If @udev->reset_resume is set then this routine won't check that the
 * port is still enabled.  Furthermore, finish_port_resume() above will
 * reset @udev.  The end result is that a broken power session can be
 * recovered and @udev will appear to persist across a loss of VBUS power.
 *
 * For example, if a host controller doesn't maintain VBUS suspend current
 * during a system sleep or is reset when the system wakes up, all the USB
 * power sessions below it will be broken.  This is especially troublesome
 * for mass-storage devices containing mounted filesystems, since the
 * device will appear to have disconnected and all the memory mappings
 * to it will be lost.  Using the USB_PERSIST facility, the device can be
 * made to appear as if it had not disconnected.
 *
 * This facility can be dangerous.  Although usb_reset_and_verify_device() makes
 * every effort to insure that the same device is present after the
 * reset as before, it cannot provide a 100% guarantee.  Furthermore it's
 * quite possible for a device to remain unaltered but its media to be
 * changed.  If the user replaces a flash memory card while the system is
 * asleep, he will have only himself to blame when the filesystem on the
 * new card is corrupted and the system crashes.
 *
 * Returns 0 on success, else negative errno.
 */
int usb_port_resume(struct usb_device *udev, pm_message_t msg)
{
	struct usb_hub	*hub = hdev_to_hub(udev->parent);
	int		port1 = udev->portnum;
	int		status;
	u16		portchange, portstatus;

	/* Skip the initial Clear-Suspend step for a remote wakeup */
	status = hub_port_status(hub, port1, &portstatus, &portchange);
	if (status == 0 && !(portstatus & USB_PORT_STAT_SUSPEND))
		goto SuspendCleared;

	// dev_dbg(hub->intfdev, "resume port %d\n", port1);

	set_bit(port1, hub->busy_bits);

	/* see 7.1.7.7; affects power usage, but not budgeting */
	status = clear_port_feature(hub->hdev,
			port1, USB_PORT_FEAT_SUSPEND);
	if (status) {
		dev_dbg(hub->intfdev, "can't resume port %d, status %d\n",
				port1, status);
	} else {
		/* drive resume for at least 20 msec */
		dev_dbg(&udev->dev, "usb %sresume\n",
				(msg.event & PM_EVENT_AUTO ? "auto-" : ""));
		msleep(25);

		/* Virtual root hubs can trigger on GET_PORT_STATUS to
		 * stop resume signaling.  Then finish the resume
		 * sequence.
		 */
		status = hub_port_status(hub, port1, &portstatus, &portchange);

		/* TRSMRCY = 10 msec */
		msleep(10);
	}

 SuspendCleared:
	if (status == 0) {
		if (portchange & USB_PORT_STAT_C_SUSPEND)
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_SUSPEND);
	}

	clear_bit(port1, hub->busy_bits);

	status = check_port_resume_type(udev,
			hub, port1, status, portchange, portstatus);
	if (status == 0)
		status = finish_port_resume(udev);
	if (status < 0) {
		dev_dbg(&udev->dev, "can't resume, status %d\n", status);
		hub_port_logical_disconnect(hub, port1);
	}
	return status;
}

/* caller has locked udev */
static int remote_wakeup(struct usb_device *udev)
{
	int	status = 0;

	if (udev->state == USB_STATE_SUSPENDED) {
		dev_dbg(&udev->dev, "usb %sresume\n", "wakeup-");
		usb_mark_last_busy(udev);
		status = usb_external_resume_device(udev, PMSG_REMOTE_RESUME);
	}
	return status;
}

#else	/* CONFIG_USB_SUSPEND */

/* When CONFIG_USB_SUSPEND isn't set, we never suspend or resume any ports. */

int usb_port_suspend(struct usb_device *udev, pm_message_t msg)
{
	return 0;
}

/* However we may need to do a reset-resume */

int usb_port_resume(struct usb_device *udev, pm_message_t msg)
{
	struct usb_hub	*hub = hdev_to_hub(udev->parent);
	int		port1 = udev->portnum;
	int		status;
	u16		portchange, portstatus;

	status = hub_port_status(hub, port1, &portstatus, &portchange);
	status = check_port_resume_type(udev,
			hub, port1, status, portchange, portstatus);

	if (status) {
		dev_dbg(&udev->dev, "can't resume, status %d\n", status);
		hub_port_logical_disconnect(hub, port1);
	} else if (udev->reset_resume) {
		dev_dbg(&udev->dev, "reset-resume\n");
		status = usb_reset_and_verify_device(udev);
	}
	return status;
}

static inline int remote_wakeup(struct usb_device *udev)
{
	return 0;
}

#endif

static int hub_suspend(struct usb_interface *intf, pm_message_t msg)
{
	struct usb_hub		*hub = usb_get_intfdata (intf);
	struct usb_device	*hdev = hub->hdev;
	unsigned		port1;

	/* fail if children aren't already suspended */
	for (port1 = 1; port1 <= hdev->maxchild; port1++) {
		struct usb_device	*udev;

		udev = hdev->children [port1-1];
		if (udev && udev->can_submit) {
			if (!(msg.event & PM_EVENT_AUTO))
				dev_dbg(&intf->dev, "port %d nyet suspended\n",
						port1);
			return -EBUSY;
		}
	}

	dev_dbg(&intf->dev, "%s\n", __func__);

	/* stop khubd and related activity */
	hub_quiesce(hub, HUB_SUSPEND);
	return 0;
}

static int hub_resume(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata(intf);

	dev_dbg(&intf->dev, "%s\n", __func__);
	hub_activate(hub, HUB_RESUME);
	return 0;
}

static int hub_reset_resume(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata(intf);

	dev_dbg(&intf->dev, "%s\n", __func__);
	hub_activate(hub, HUB_RESET_RESUME);
	return 0;
}

/**
 * usb_root_hub_lost_power - called by HCD if the root hub lost Vbus power
 * @rhdev: struct usb_device for the root hub
 *
 * The USB host controller driver calls this function when its root hub
 * is resumed and Vbus power has been interrupted or the controller
 * has been reset.  The routine marks @rhdev as having lost power.
 * When the hub driver is resumed it will take notice and carry out
 * power-session recovery for all the "USB-PERSIST"-enabled child devices;
 * the others will be disconnected.
 */
void usb_root_hub_lost_power(struct usb_device *rhdev)
{
	dev_warn(&rhdev->dev, "root hub lost power or was reset\n");
	rhdev->reset_resume = 1;
}
EXPORT_SYMBOL_GPL(usb_root_hub_lost_power);

#else	/* CONFIG_PM */

static inline int remote_wakeup(struct usb_device *udev)
{
	return 0;
}

#define hub_suspend		NULL
#define hub_resume		NULL
#define hub_reset_resume	NULL
#endif


/* USB 2.0 spec, 7.1.7.3 / fig 7-29:
 *
 * Between connect detection and reset signaling there must be a delay
 * of 100ms at least for debounce and power-settling.  The corresponding
 * timer shall restart whenever the downstream port detects a disconnect.
 * 
 * Apparently there are some bluetooth and irda-dongles and a number of
 * low-speed devices for which this debounce period may last over a second.
 * Not covered by the spec - but easy to deal with.
 *
 * This implementation uses a 1500ms total debounce timeout; if the
 * connection isn't stable by then it returns -ETIMEDOUT.  It checks
 * every 25ms for transient disconnects.  When the port status has been
 * unchanged for 100ms it returns the port status.
 */
static int hub_port_debounce(struct usb_hub *hub, int port1)
{
	int ret;
	int total_time, stable_time = 0;
	u16 portchange, portstatus;
	unsigned connection = 0xffff;

	for (total_time = 0; ; total_time += HUB_DEBOUNCE_STEP) {
		ret = hub_port_status(hub, port1, &portstatus, &portchange);
		if (ret < 0)
			return ret;

		if (!(portchange & USB_PORT_STAT_C_CONNECTION) &&
		     (portstatus & USB_PORT_STAT_CONNECTION) == connection) {
			stable_time += HUB_DEBOUNCE_STEP;
			if (stable_time >= HUB_DEBOUNCE_STABLE)
				break;
		} else {
			stable_time = 0;
			connection = portstatus & USB_PORT_STAT_CONNECTION;
		}

		if (portchange & USB_PORT_STAT_C_CONNECTION) {
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_CONNECTION);
		}

		if (total_time >= HUB_DEBOUNCE_TIMEOUT)
			break;
		msleep(HUB_DEBOUNCE_STEP);
	}

	dev_dbg (hub->intfdev,
		"debounce: port %d: total %dms stable %dms status 0x%x\n",
		port1, total_time, stable_time, portstatus);

	if (stable_time < HUB_DEBOUNCE_STABLE)
		return -ETIMEDOUT;
	return portstatus;
}

void usb_ep0_reinit(struct usb_device *udev)
{
	usb_disable_endpoint(udev, 0 + USB_DIR_IN, true);
	usb_disable_endpoint(udev, 0 + USB_DIR_OUT, true);
	usb_enable_endpoint(udev, &udev->ep0, true);
}
EXPORT_SYMBOL_GPL(usb_ep0_reinit);

#define usb_sndaddr0pipe()	(PIPE_CONTROL << 30)
#define usb_rcvaddr0pipe()	((PIPE_CONTROL << 30) | USB_DIR_IN)

static int hub_set_address(struct usb_device *udev, int devnum)
{
	int retval;

	if (devnum <= 1)
		return -EINVAL;
	if (udev->state == USB_STATE_ADDRESS)
		return 0;
	if (udev->state != USB_STATE_DEFAULT)
		return -EINVAL;
	retval = usb_control_msg(udev, usb_sndaddr0pipe(),
		USB_REQ_SET_ADDRESS, 0, devnum, 0,
		NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (retval == 0) {
		/* Device now using proper address. */
		update_address(udev, devnum);
		usb_set_device_state(udev, USB_STATE_ADDRESS);
		usb_ep0_reinit(udev);
	}
	return retval;
}

/* Reset device, (re)assign address, get device descriptor.
 * Device connection must be stable, no more debouncing needed.
 * Returns device in USB_STATE_ADDRESS, except on error.
 *
 * If this is called for an already-existing device (as part of
 * usb_reset_and_verify_device), the caller must own the device lock.  For a
 * newly detected device that is not accessible through any global
 * pointers, it's not necessary to lock the device.
 */
static int
hub_port_init (struct usb_hub *hub, struct usb_device *udev, int port1,
		int retry_counter)
{
	static DEFINE_MUTEX(usb_address0_mutex);

	struct usb_device	*hdev = hub->hdev;
	struct usb_hcd		*hcd = bus_to_hcd(hdev->bus);
	int			i, j, retval;
	unsigned		delay = HUB_SHORT_RESET_TIME;
	enum usb_device_speed	oldspeed = udev->speed;
	char 			*speed, *type;
	int			devnum = udev->devnum;

	/* root hub ports have a slightly longer reset period
	 * (from USB 2.0 spec, section 7.1.7.5)
	 */
	if (!hdev->parent) {
		delay = HUB_ROOT_RESET_TIME;
		if (port1 == hdev->bus->otg_port)
			hdev->bus->b_hnp_enable = 0;
	}

	/* Some low speed devices have problems with the quick delay, so */
	/*  be a bit pessimistic with those devices. RHbug #23670 */
	if (oldspeed == USB_SPEED_LOW)
		delay = HUB_LONG_RESET_TIME;

	mutex_lock(&usb_address0_mutex);

	if ((hcd->driver->flags & HCD_USB3) && udev->config) {
		/* FIXME this will need special handling by the xHCI driver. */
		dev_dbg(&udev->dev,
				"xHCI reset of configured device "
				"not supported yet.\n");
		retval = -EINVAL;
		goto fail;
	} else if (!udev->config && oldspeed == USB_SPEED_SUPER) {
		/* Don't reset USB 3.0 devices during an initial setup */
		usb_set_device_state(udev, USB_STATE_DEFAULT);
	} else {
		/* Reset the device; full speed may morph to high speed */
		/* FIXME a USB 2.0 device may morph into SuperSpeed on reset. */
		retval = hub_port_reset(hub, port1, udev, delay);
		if (retval < 0)		/* error or disconnect */
			goto fail;
		/* success, speed is known */
	}
	retval = -ENODEV;

	if (oldspeed != USB_SPEED_UNKNOWN && oldspeed != udev->speed) {
		dev_dbg(&udev->dev, "device reset changed speed!\n");
		goto fail;
	}
	oldspeed = udev->speed;

	/* USB 2.0 section 5.5.3 talks about ep0 maxpacket ...
	 * it's fixed size except for full speed devices.
	 * For Wireless USB devices, ep0 max packet is always 512 (tho
	 * reported as 0xff in the device descriptor). WUSB1.0[4.8.1].
	 */
	switch (udev->speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_VARIABLE:	/* fixed at 512 */
		udev->ep0.desc.wMaxPacketSize = cpu_to_le16(512);
		break;
	case USB_SPEED_HIGH:		/* fixed at 64 */
		udev->ep0.desc.wMaxPacketSize = cpu_to_le16(64);
		break;
	case USB_SPEED_FULL:		/* 8, 16, 32, or 64 */
		/* to determine the ep0 maxpacket size, try to read
		 * the device descriptor to get bMaxPacketSize0 and
		 * then correct our initial guess.
		 */
		udev->ep0.desc.wMaxPacketSize = cpu_to_le16(64);
		break;
	case USB_SPEED_LOW:		/* fixed at 8 */
		udev->ep0.desc.wMaxPacketSize = cpu_to_le16(8);
		break;
	default:
		goto fail;
	}
 
	type = "";
	switch (udev->speed) {
	case USB_SPEED_LOW:	speed = "low";	break;
	case USB_SPEED_FULL:	speed = "full";	break;
	case USB_SPEED_HIGH:	speed = "high";	break;
	case USB_SPEED_SUPER:
				speed = "super";
				break;
	case USB_SPEED_VARIABLE:
				speed = "variable";
				type = "Wireless ";
				break;
	default: 		speed = "?";	break;
	}
	dev_info (&udev->dev,
		  "%s %s speed %sUSB device using %s and address %d\n",
		  (udev->config) ? "reset" : "new", speed, type,
		  udev->bus->controller->driver->name, devnum);

	/* Set up TT records, if needed  */
	if (hdev->tt) {
		udev->tt = hdev->tt;
		udev->ttport = hdev->ttport;
	} else if (udev->speed != USB_SPEED_HIGH
			&& hdev->speed == USB_SPEED_HIGH) {
		udev->tt = &hub->tt;
		udev->ttport = port1;
	}
 
	/* Why interleave GET_DESCRIPTOR and SET_ADDRESS this way?
	 * Because device hardware and firmware is sometimes buggy in
	 * this area, and this is how Linux has done it for ages.
	 * Change it cautiously.
	 *
	 * NOTE:  If USE_NEW_SCHEME() is true we will start by issuing
	 * a 64-byte GET_DESCRIPTOR request.  This is what Windows does,
	 * so it may help with some non-standards-compliant devices.
	 * Otherwise we start with SET_ADDRESS and then try to read the
	 * first 8 bytes of the device descriptor to get the ep0 maxpacket
	 * value.
	 */
	for (i = 0; i < GET_DESCRIPTOR_TRIES; (++i, msleep(100))) {
		if (USE_NEW_SCHEME(retry_counter)) {
			struct usb_device_descriptor *buf;
			int r = 0;

#define GET_DESCRIPTOR_BUFSIZE	64
			buf = kmalloc(GET_DESCRIPTOR_BUFSIZE, GFP_NOIO);
			if (!buf) {
				retval = -ENOMEM;
				continue;
			}

			/* Retry on all errors; some devices are flakey.
			 * 255 is for WUSB devices, we actually need to use
			 * 512 (WUSB1.0[4.8.1]).
			 */
			for (j = 0; j < 3; ++j) {
				buf->bMaxPacketSize0 = 0;
				r = usb_control_msg(udev, usb_rcvaddr0pipe(),
					USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
					USB_DT_DEVICE << 8, 0,
					buf, GET_DESCRIPTOR_BUFSIZE,
					initial_descriptor_timeout);
				switch (buf->bMaxPacketSize0) {
				case 8: case 16: case 32: case 64: case 255:
					if (buf->bDescriptorType ==
							USB_DT_DEVICE) {
						r = 0;
						break;
					}
					/* FALL THROUGH */
				default:
					if (r == 0)
						r = -EPROTO;
					break;
				}
				if (r == 0)
					break;
			}
			udev->descriptor.bMaxPacketSize0 =
					buf->bMaxPacketSize0;
			kfree(buf);

			retval = hub_port_reset(hub, port1, udev, delay);
			if (retval < 0)		/* error or disconnect */
				goto fail;
			if (oldspeed != udev->speed) {
				dev_dbg(&udev->dev,
					"device reset changed speed!\n");
				retval = -ENODEV;
				goto fail;
			}
			if (r) {
				dev_err(&udev->dev,
					"device descriptor read/64, error %d\n",
					r);
				retval = -EMSGSIZE;
				continue;
			}
#undef GET_DESCRIPTOR_BUFSIZE
		}

 		/*
 		 * If device is WUSB, we already assigned an
 		 * unauthorized address in the Connect Ack sequence;
 		 * authorization will assign the final address.
 		 */
 		if (udev->wusb == 0) {
			for (j = 0; j < SET_ADDRESS_TRIES; ++j) {
				retval = hub_set_address(udev, devnum);
				if (retval >= 0)
					break;
				msleep(200);
			}
			if (retval < 0) {
				dev_err(&udev->dev,
					"device not accepting address %d, error %d\n",
					devnum, retval);
				goto fail;
			}

			/* cope with hardware quirkiness:
			 *  - let SET_ADDRESS settle, some device hardware wants it
			 *  - read ep0 maxpacket even for high and low speed,
			 */
			msleep(10);
			if (USE_NEW_SCHEME(retry_counter))
				break;
  		}

		retval = usb_get_device_descriptor(udev, 8);
		if (retval < 8) {
			dev_err(&udev->dev,
					"device descriptor read/8, error %d\n",
					retval);
			if (retval >= 0)
				retval = -EMSGSIZE;
		} else {
			retval = 0;
			break;
		}
	}
	if (retval)
		goto fail;

	if (udev->descriptor.bMaxPacketSize0 == 0xff ||
			udev->speed == USB_SPEED_SUPER)
		i = 512;
	else
		i = udev->descriptor.bMaxPacketSize0;
	if (le16_to_cpu(udev->ep0.desc.wMaxPacketSize) != i) {
		if (udev->speed != USB_SPEED_FULL ||
				!(i == 8 || i == 16 || i == 32 || i == 64)) {
			dev_err(&udev->dev, "ep0 maxpacket = %d\n", i);
			retval = -EMSGSIZE;
			goto fail;
		}
		dev_dbg(&udev->dev, "ep0 maxpacket = %d\n", i);
		udev->ep0.desc.wMaxPacketSize = cpu_to_le16(i);
		usb_ep0_reinit(udev);
	}
  
	retval = usb_get_device_descriptor(udev, USB_DT_DEVICE_SIZE);
	if (retval < (signed)sizeof(udev->descriptor)) {
		dev_err(&udev->dev, "device descriptor read/all, error %d\n",
			retval);
		if (retval >= 0)
			retval = -ENOMSG;
		goto fail;
	}

	retval = 0;

fail:
	if (retval) {
		hub_port_disable(hub, port1, 0);
		update_address(udev, devnum);	/* for disconnect processing */
	}
	mutex_unlock(&usb_address0_mutex);
	return retval;
}

static void
check_highspeed (struct usb_hub *hub, struct usb_device *udev, int port1)
{
	struct usb_qualifier_descriptor	*qual;
	int				status;

	qual = kmalloc (sizeof *qual, GFP_KERNEL);
	if (qual == NULL)
		return;

	status = usb_get_descriptor (udev, USB_DT_DEVICE_QUALIFIER, 0,
			qual, sizeof *qual);
	if (status == sizeof *qual) {
		dev_info(&udev->dev, "not running at top speed; "
			"connect to a high speed hub\n");
		/* hub LEDs are probably harder to miss than syslog */
		if (hub->has_indicators) {
			hub->indicator[port1-1] = INDICATOR_GREEN_BLINK;
			schedule_delayed_work (&hub->leds, 0);
		}
	}
	kfree(qual);
}

static unsigned
hub_power_remaining (struct usb_hub *hub)
{
	struct usb_device *hdev = hub->hdev;
	int remaining;
	int port1;

	if (!hub->limited_power)
		return 0;

	remaining = hdev->bus_mA - hub->descriptor->bHubContrCurrent;
	for (port1 = 1; port1 <= hdev->maxchild; ++port1) {
		struct usb_device	*udev = hdev->children[port1 - 1];
		int			delta;

		if (!udev)
			continue;

		/* Unconfigured devices may not use more than 100mA,
		 * or 8mA for OTG ports */
		if (udev->actconfig)
			delta = udev->actconfig->desc.bMaxPower * 2;
		else if (port1 != udev->bus->otg_port || hdev->parent)
			delta = 100;
		else
			delta = 8;
		if (delta > hub->mA_per_port)
			dev_warn(&udev->dev,
				 "%dmA is over %umA budget for port %d!\n",
				 delta, hub->mA_per_port, port1);
		remaining -= delta;
	}
	if (remaining < 0) {
		dev_warn(hub->intfdev, "%dmA over power budget!\n",
			- remaining);
		remaining = 0;
	}
	return remaining;
}

/* Handle physical or logical connection change events.
 * This routine is called when:
 * 	a port connection-change occurs;
 *	a port enable-change occurs (often caused by EMI);
 *	usb_reset_and_verify_device() encounters changed descriptors (as from
 *		a firmware download)
 * caller already locked the hub
 */
static void hub_port_connect_change(struct usb_hub *hub, int port1,
					u16 portstatus, u16 portchange)
{
	struct usb_device *hdev = hub->hdev;
	struct device *hub_dev = hub->intfdev;
	struct usb_hcd *hcd = bus_to_hcd(hdev->bus);
	unsigned wHubCharacteristics =
			le16_to_cpu(hub->descriptor->wHubCharacteristics);
	struct usb_device *udev;
	int status, i;

	dev_dbg (hub_dev,
		"port %d, status %04x, change %04x, %s\n",
		port1, portstatus, portchange, portspeed (portstatus));

	if (hub->has_indicators) {
		set_port_led(hub, port1, HUB_LED_AUTO);
		hub->indicator[port1-1] = INDICATOR_AUTO;
	}

#ifdef	CONFIG_USB_OTG
	/* during HNP, don't repeat the debounce */
	if (hdev->bus->is_b_host)
		portchange &= ~(USB_PORT_STAT_C_CONNECTION |
				USB_PORT_STAT_C_ENABLE);
#endif

	/* Try to resuscitate an existing device */
	udev = hdev->children[port1-1];
	if ((portstatus & USB_PORT_STAT_CONNECTION) && udev &&
			udev->state != USB_STATE_NOTATTACHED) {
		usb_lock_device(udev);
		if (portstatus & USB_PORT_STAT_ENABLE) {
			status = 0;		/* Nothing to do */

#ifdef CONFIG_USB_SUSPEND
		} else if (udev->state == USB_STATE_SUSPENDED &&
				udev->persist_enabled) {
			/* For a suspended device, treat this as a
			 * remote wakeup event.
			 */
			if (udev->do_remote_wakeup)
				status = remote_wakeup(udev);

			/* Otherwise leave it be; devices can't tell the
			 * difference between suspended and disabled.
			 */
			else
				status = 0;
#endif

		} else {
			status = -ENODEV;	/* Don't resuscitate */
		}
		usb_unlock_device(udev);

		if (status == 0) {
			clear_bit(port1, hub->change_bits);
			return;
		}
	}

	/* Disconnect any existing devices under this port */
	if (udev)
		usb_disconnect(&hdev->children[port1-1]);
	clear_bit(port1, hub->change_bits);

	if (portchange & (USB_PORT_STAT_C_CONNECTION |
				USB_PORT_STAT_C_ENABLE)) {
		status = hub_port_debounce(hub, port1);
		if (status < 0) {
			if (printk_ratelimit())
				dev_err(hub_dev, "connect-debounce failed, "
						"port %d disabled\n", port1);
			portstatus &= ~USB_PORT_STAT_CONNECTION;
		} else {
			portstatus = status;
		}
	}

	/* Return now if debouncing failed or nothing is connected */
	if (!(portstatus & USB_PORT_STAT_CONNECTION)) {

		/* maybe switch power back on (e.g. root hub was reset) */
		if ((wHubCharacteristics & HUB_CHAR_LPSM) < 2
				&& !(portstatus & (1 << USB_PORT_FEAT_POWER)))
			set_port_feature(hdev, port1, USB_PORT_FEAT_POWER);

		if (portstatus & USB_PORT_STAT_ENABLE)
  			goto done;
		return;
	}

	for (i = 0; i < SET_CONFIG_TRIES; i++) {

		/* reallocate for each attempt, since references
		 * to the previous one can escape in various ways
		 */
		udev = usb_alloc_dev(hdev, hdev->bus, port1);
		if (!udev) {
			dev_err (hub_dev,
				"couldn't allocate port %d usb_device\n",
				port1);
			goto done;
		}

		usb_set_device_state(udev, USB_STATE_POWERED);
 		udev->bus_mA = hub->mA_per_port;
		udev->level = hdev->level + 1;
		udev->wusb = hub_is_wusb(hub);

		/* set the address */
		choose_address(udev);
		if (udev->devnum <= 0) {
			status = -ENOTCONN;	/* Don't retry */
			goto loop;
		}

		/*
		 * USB 3.0 devices are reset automatically before the connect
		 * port status change appears, and the root hub port status
		 * shows the correct speed.  We also get port change
		 * notifications for USB 3.0 devices from the USB 3.0 portion of
		 * an external USB 3.0 hub, but this isn't handled correctly yet
		 * FIXME.
		 */

		if (!(hcd->driver->flags & HCD_USB3))
			udev->speed = USB_SPEED_UNKNOWN;
		else if ((hdev->parent == NULL) &&
				(portstatus & (1 << USB_PORT_FEAT_SUPERSPEED)))
			udev->speed = USB_SPEED_SUPER;
		else
			udev->speed = USB_SPEED_UNKNOWN;

		/* reset (non-USB 3.0 devices) and get descriptor */
		status = hub_port_init(hub, udev, port1, i);
		if (status < 0)
			goto loop;

		/* consecutive bus-powered hubs aren't reliable; they can
		 * violate the voltage drop budget.  if the new child has
		 * a "powered" LED, users should notice we didn't enable it
		 * (without reading syslog), even without per-port LEDs
		 * on the parent.
		 */
		if (udev->descriptor.bDeviceClass == USB_CLASS_HUB
				&& udev->bus_mA <= 100) {
			u16	devstat;

			status = usb_get_status(udev, USB_RECIP_DEVICE, 0,
					&devstat);
			if (status < 2) {
				dev_dbg(&udev->dev, "get status %d ?\n", status);
				goto loop_disable;
			}
			le16_to_cpus(&devstat);
			if ((devstat & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
				dev_err(&udev->dev,
					"can't connect bus-powered hub "
					"to this port\n");
				if (hub->has_indicators) {
					hub->indicator[port1-1] =
						INDICATOR_AMBER_BLINK;
					schedule_delayed_work (&hub->leds, 0);
				}
				status = -ENOTCONN;	/* Don't retry */
				goto loop_disable;
			}
		}
 
		/* check for devices running slower than they could */
		if (le16_to_cpu(udev->descriptor.bcdUSB) >= 0x0200
				&& udev->speed == USB_SPEED_FULL
				&& highspeed_hubs != 0)
			check_highspeed (hub, udev, port1);

		/* Store the parent's children[] pointer.  At this point
		 * udev becomes globally accessible, although presumably
		 * no one will look at it until hdev is unlocked.
		 */
		status = 0;

		/* We mustn't add new devices if the parent hub has
		 * been disconnected; we would race with the
		 * recursively_mark_NOTATTACHED() routine.
		 */
		spin_lock_irq(&device_state_lock);
		if (hdev->state == USB_STATE_NOTATTACHED)
			status = -ENOTCONN;
		else
			hdev->children[port1-1] = udev;
		spin_unlock_irq(&device_state_lock);

		/* Run it through the hoops (find a driver, etc) */
		if (!status) {
			status = usb_new_device(udev);
			if (status) {
				spin_lock_irq(&device_state_lock);
				hdev->children[port1-1] = NULL;
				spin_unlock_irq(&device_state_lock);
			}
		}

		if (status)
			goto loop_disable;

		status = hub_power_remaining(hub);
		if (status)
			dev_dbg(hub_dev, "%dmA power budget left\n", status);

		return;

loop_disable:
		hub_port_disable(hub, port1, 1);
loop:
		usb_ep0_reinit(udev);
		release_address(udev);
		usb_put_dev(udev);
		if ((status == -ENOTCONN) || (status == -ENOTSUPP))
			break;
	}
	if (hub->hdev->parent ||
			!hcd->driver->port_handed_over ||
			!(hcd->driver->port_handed_over)(hcd, port1))
		dev_err(hub_dev, "unable to enumerate USB device on port %d\n",
				port1);
 
done:
	hub_port_disable(hub, port1, 1);
	if (hcd->driver->relinquish_port && !hub->hdev->parent)
		hcd->driver->relinquish_port(hcd, port1);
}

static void hub_events(void)
{
	struct list_head *tmp;
	struct usb_device *hdev;
	struct usb_interface *intf;
	struct usb_hub *hub;
	struct device *hub_dev;
	u16 hubstatus;
	u16 hubchange;
	u16 portstatus;
	u16 portchange;
	int i, ret;
	int connect_change;

	/*
	 *  We restart the list every time to avoid a deadlock with
	 * deleting hubs downstream from this one. This should be
	 * safe since we delete the hub from the event list.
	 * Not the most efficient, but avoids deadlocks.
	 */
	while (1) {

		/* Grab the first entry at the beginning of the list */
		spin_lock_irq(&hub_event_lock);
		if (list_empty(&hub_event_list)) {
			spin_unlock_irq(&hub_event_lock);
			break;
		}

		tmp = hub_event_list.next;
		list_del_init(tmp);

		hub = list_entry(tmp, struct usb_hub, event_list);
		kref_get(&hub->kref);
		spin_unlock_irq(&hub_event_lock);

		hdev = hub->hdev;
		hub_dev = hub->intfdev;
		intf = to_usb_interface(hub_dev);
		dev_dbg(hub_dev, "state %d ports %d chg %04x evt %04x\n",
				hdev->state, hub->descriptor
					? hub->descriptor->bNbrPorts
					: 0,
				/* NOTE: expects max 15 ports... */
				(u16) hub->change_bits[0],
				(u16) hub->event_bits[0]);

		/* Lock the device, then check to see if we were
		 * disconnected while waiting for the lock to succeed. */
		usb_lock_device(hdev);
		if (unlikely(hub->disconnected))
			goto loop;

		/* If the hub has died, clean up after it */
		if (hdev->state == USB_STATE_NOTATTACHED) {
			hub->error = -ENODEV;
			hub_quiesce(hub, HUB_DISCONNECT);
			goto loop;
		}

		/* Autoresume */
		ret = usb_autopm_get_interface(intf);
		if (ret) {
			dev_dbg(hub_dev, "Can't autoresume: %d\n", ret);
			goto loop;
		}

		/* If this is an inactive hub, do nothing */
		if (hub->quiescing)
			goto loop_autopm;

		if (hub->error) {
			dev_dbg (hub_dev, "resetting for error %d\n",
				hub->error);

			ret = usb_reset_device(hdev);
			if (ret) {
				dev_dbg (hub_dev,
					"error resetting hub: %d\n", ret);
				goto loop_autopm;
			}

			hub->nerrors = 0;
			hub->error = 0;
		}

		/* deal with port status changes */
		for (i = 1; i <= hub->descriptor->bNbrPorts; i++) {
			if (test_bit(i, hub->busy_bits))
				continue;
			connect_change = test_bit(i, hub->change_bits);
			if (!test_and_clear_bit(i, hub->event_bits) &&
					!connect_change)
				continue;

			ret = hub_port_status(hub, i,
					&portstatus, &portchange);
			if (ret < 0)
				continue;

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_CONNECTION);
				connect_change = 1;
			}

			if (portchange & USB_PORT_STAT_C_ENABLE) {
				if (!connect_change)
					dev_dbg (hub_dev,
						"port %d enable change, "
						"status %08x\n",
						i, portstatus);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_ENABLE);

				/*
				 * EM interference sometimes causes badly
				 * shielded USB devices to be shutdown by
				 * the hub, this hack enables them again.
				 * Works at least with mouse driver. 
				 */
				if (!(portstatus & USB_PORT_STAT_ENABLE)
				    && !connect_change
				    && hdev->children[i-1]) {
					dev_err (hub_dev,
					    "port %i "
					    "disabled by hub (EMI?), "
					    "re-enabling...\n",
						i);
					connect_change = 1;
				}
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND) {
				struct usb_device *udev;

				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_SUSPEND);
				udev = hdev->children[i-1];
				if (udev) {
					usb_lock_device(udev);
					ret = remote_wakeup(hdev->
							children[i-1]);
					usb_unlock_device(udev);
					if (ret < 0)
						connect_change = 1;
				} else {
					ret = -ENODEV;
					hub_port_disable(hub, i, 1);
				}
				dev_dbg (hub_dev,
					"resume on port %d, status %d\n",
					i, ret);
			}
			
			if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
				dev_err (hub_dev,
					"over-current change on port %d\n",
					i);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_OVER_CURRENT);
				hub_power_on(hub, true);
			}

			if (portchange & USB_PORT_STAT_C_RESET) {
				dev_dbg (hub_dev,
					"reset change on port %d\n",
					i);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_RESET);
			}

			if (connect_change)
				hub_port_connect_change(hub, i,
						portstatus, portchange);
		} /* end for i */

		/* deal with hub status changes */
		if (test_and_clear_bit(0, hub->event_bits) == 0)
			;	/* do nothing */
		else if (hub_hub_status(hub, &hubstatus, &hubchange) < 0)
			dev_err (hub_dev, "get_hub_status failed\n");
		else {
			if (hubchange & HUB_CHANGE_LOCAL_POWER) {
				dev_dbg (hub_dev, "power change\n");
				clear_hub_feature(hdev, C_HUB_LOCAL_POWER);
				if (hubstatus & HUB_STATUS_LOCAL_POWER)
					/* FIXME: Is this always true? */
					hub->limited_power = 1;
				else
					hub->limited_power = 0;
			}
			if (hubchange & HUB_CHANGE_OVERCURRENT) {
				dev_dbg (hub_dev, "overcurrent change\n");
				msleep(500);	/* Cool down */
				clear_hub_feature(hdev, C_HUB_OVER_CURRENT);
                        	hub_power_on(hub, true);
			}
		}

loop_autopm:
		/* Allow autosuspend if we're not going to run again */
		if (list_empty(&hub->event_list))
			usb_autopm_enable(intf);
loop:
		usb_unlock_device(hdev);
		kref_put(&hub->kref, hub_release);

        } /* end while (1) */
}

static int hub_thread(void *__unused)
{
	/* khubd needs to be freezable to avoid intefering with USB-PERSIST
	 * port handover.  Otherwise it might see that a full-speed device
	 * was gone before the EHCI controller had handed its port over to
	 * the companion full-speed controller.
	 */
	set_freezable();

	do {
		hub_events();
		wait_event_freezable(khubd_wait,
				!list_empty(&hub_event_list) ||
				kthread_should_stop());
	} while (!kthread_should_stop() || !list_empty(&hub_event_list));

	pr_debug("%s: khubd exiting\n", usbcore_name);
	return 0;
}

static struct usb_device_id hub_id_table [] = {
    { .match_flags = USB_DEVICE_ID_MATCH_DEV_CLASS,
      .bDeviceClass = USB_CLASS_HUB},
    { .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
      .bInterfaceClass = USB_CLASS_HUB},
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, hub_id_table);

static struct usb_driver hub_driver = {
	.name =		"hub",
	.probe =	hub_probe,
	.disconnect =	hub_disconnect,
	.suspend =	hub_suspend,
	.resume =	hub_resume,
	.reset_resume =	hub_reset_resume,
	.pre_reset =	hub_pre_reset,
	.post_reset =	hub_post_reset,
	.ioctl =	hub_ioctl,
	.id_table =	hub_id_table,
	.supports_autosuspend =	1,
};

int usb_hub_init(void)
{
	if (usb_register(&hub_driver) < 0) {
		printk(KERN_ERR "%s: can't register hub driver\n",
			usbcore_name);
		return -1;
	}

	khubd_task = kthread_run(hub_thread, NULL, "khubd");
	if (!IS_ERR(khubd_task))
		return 0;

	/* Fall through if kernel_thread failed */
	usb_deregister(&hub_driver);
	printk(KERN_ERR "%s: can't start khubd\n", usbcore_name);

	return -1;
}

void usb_hub_cleanup(void)
{
	kthread_stop(khubd_task);

	/*
	 * Hub resources are freed for us by usb_deregister. It calls
	 * usb_driver_purge on every device which in turn calls that
	 * devices disconnect function if it is using this driver.
	 * The hub_disconnect function takes care of releasing the
	 * individual hub resources. -greg
	 */
	usb_deregister(&hub_driver);
} /* usb_hub_cleanup() */

static int descriptors_changed(struct usb_device *udev,
		struct usb_device_descriptor *old_device_descriptor)
{
	int		changed = 0;
	unsigned	index;
	unsigned	serial_len = 0;
	unsigned	len;
	unsigned	old_length;
	int		length;
	char		*buf;

	if (memcmp(&udev->descriptor, old_device_descriptor,
			sizeof(*old_device_descriptor)) != 0)
		return 1;

	/* Since the idVendor, idProduct, and bcdDevice values in the
	 * device descriptor haven't changed, we will assume the
	 * Manufacturer and Product strings haven't changed either.
	 * But the SerialNumber string could be different (e.g., a
	 * different flash card of the same brand).
	 */
	if (udev->serial)
		serial_len = strlen(udev->serial) + 1;

	len = serial_len;
	for (index = 0; index < udev->descriptor.bNumConfigurations; index++) {
		old_length = le16_to_cpu(udev->config[index].desc.wTotalLength);
		len = max(len, old_length);
	}

	buf = kmalloc(len, GFP_NOIO);
	if (buf == NULL) {
		dev_err(&udev->dev, "no mem to re-read configs after reset\n");
		/* assume the worst */
		return 1;
	}
	for (index = 0; index < udev->descriptor.bNumConfigurations; index++) {
		old_length = le16_to_cpu(udev->config[index].desc.wTotalLength);
		length = usb_get_descriptor(udev, USB_DT_CONFIG, index, buf,
				old_length);
		if (length != old_length) {
			dev_dbg(&udev->dev, "config index %d, error %d\n",
					index, length);
			changed = 1;
			break;
		}
		if (memcmp (buf, udev->rawdescriptors[index], old_length)
				!= 0) {
			dev_dbg(&udev->dev, "config index %d changed (#%d)\n",
				index,
				((struct usb_config_descriptor *) buf)->
					bConfigurationValue);
			changed = 1;
			break;
		}
	}

	if (!changed && serial_len) {
		length = usb_string(udev, udev->descriptor.iSerialNumber,
				buf, serial_len);
		if (length + 1 != serial_len) {
			dev_dbg(&udev->dev, "serial string error %d\n",
					length);
			changed = 1;
		} else if (memcmp(buf, udev->serial, length) != 0) {
			dev_dbg(&udev->dev, "serial string changed\n");
			changed = 1;
		}
	}

	kfree(buf);
	return changed;
}

/**
 * usb_reset_and_verify_device - perform a USB port reset to reinitialize a device
 * @udev: device to reset (not in SUSPENDED or NOTATTACHED state)
 *
 * WARNING - don't use this routine to reset a composite device
 * (one with multiple interfaces owned by separate drivers)!
 * Use usb_reset_device() instead.
 *
 * Do a port reset, reassign the device's address, and establish its
 * former operating configuration.  If the reset fails, or the device's
 * descriptors change from their values before the reset, or the original
 * configuration and altsettings cannot be restored, a flag will be set
 * telling khubd to pretend the device has been disconnected and then
 * re-connected.  All drivers will be unbound, and the device will be
 * re-enumerated and probed all over again.
 *
 * Returns 0 if the reset succeeded, -ENODEV if the device has been
 * flagged for logical disconnection, or some other negative error code
 * if the reset wasn't even attempted.
 *
 * The caller must own the device lock.  For example, it's safe to use
 * this from a driver probe() routine after downloading new firmware.
 * For calls that might not occur during probe(), drivers should lock
 * the device using usb_lock_device_for_reset().
 *
 * Locking exception: This routine may also be called from within an
 * autoresume handler.  Such usage won't conflict with other tasks
 * holding the device lock because these tasks should always call
 * usb_autopm_resume_device(), thereby preventing any unwanted autoresume.
 */
static int usb_reset_and_verify_device(struct usb_device *udev)
{
	struct usb_device		*parent_hdev = udev->parent;
	struct usb_hub			*parent_hub;
	struct usb_device_descriptor	descriptor = udev->descriptor;
	int 				i, ret = 0;
	int				port1 = udev->portnum;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED) {
		dev_dbg(&udev->dev, "device reset not allowed in state %d\n",
				udev->state);
		return -EINVAL;
	}

	if (!parent_hdev) {
		/* this requires hcd-specific logic; see OHCI hc_restart() */
		dev_dbg(&udev->dev, "%s for root hub!\n", __func__);
		return -EISDIR;
	}
	parent_hub = hdev_to_hub(parent_hdev);

	set_bit(port1, parent_hub->busy_bits);
	for (i = 0; i < SET_CONFIG_TRIES; ++i) {

		/* ep0 maxpacket size may change; let the HCD know about it.
		 * Other endpoints will be handled by re-enumeration. */
		usb_ep0_reinit(udev);
		ret = hub_port_init(parent_hub, udev, port1, i);
		if (ret >= 0 || ret == -ENOTCONN || ret == -ENODEV)
			break;
	}
	clear_bit(port1, parent_hub->busy_bits);

	if (ret < 0)
		goto re_enumerate;
 
	/* Device might have changed firmware (DFU or similar) */
	if (descriptors_changed(udev, &descriptor)) {
		dev_info(&udev->dev, "device firmware changed\n");
		udev->descriptor = descriptor;	/* for disconnect() calls */
		goto re_enumerate;
  	}

	/* Restore the device's previous configuration */
	if (!udev->actconfig)
		goto done;
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			udev->actconfig->desc.bConfigurationValue, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&udev->dev,
			"can't restore configuration #%d (error=%d)\n",
			udev->actconfig->desc.bConfigurationValue, ret);
		goto re_enumerate;
  	}
	usb_set_device_state(udev, USB_STATE_CONFIGURED);

	/* Put interfaces back into the same altsettings as before.
	 * Don't bother to send the Set-Interface request for interfaces
	 * that were already in altsetting 0; besides being unnecessary,
	 * many devices can't handle it.  Instead just reset the host-side
	 * endpoint state.
	 */
	for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = udev->actconfig->interface[i];
		struct usb_interface_descriptor *desc;

		desc = &intf->cur_altsetting->desc;
		if (desc->bAlternateSetting == 0) {
			usb_disable_interface(udev, intf, true);
			usb_enable_interface(udev, intf, true);
			ret = 0;
		} else {
			ret = usb_set_interface(udev, desc->bInterfaceNumber,
					desc->bAlternateSetting);
		}
		if (ret < 0) {
			dev_err(&udev->dev, "failed to restore interface %d "
				"altsetting %d (error=%d)\n",
				desc->bInterfaceNumber,
				desc->bAlternateSetting,
				ret);
			goto re_enumerate;
		}
	}

done:
	return 0;
 
re_enumerate:
	hub_port_logical_disconnect(parent_hub, port1);
	return -ENODEV;
}

/**
 * usb_reset_device - warn interface drivers and perform a USB port reset
 * @udev: device to reset (not in SUSPENDED or NOTATTACHED state)
 *
 * Warns all drivers bound to registered interfaces (using their pre_reset
 * method), performs the port reset, and then lets the drivers know that
 * the reset is over (using their post_reset method).
 *
 * Return value is the same as for usb_reset_and_verify_device().
 *
 * The caller must own the device lock.  For example, it's safe to use
 * this from a driver probe() routine after downloading new firmware.
 * For calls that might not occur during probe(), drivers should lock
 * the device using usb_lock_device_for_reset().
 *
 * If an interface is currently being probed or disconnected, we assume
 * its driver knows how to handle resets.  For all other interfaces,
 * if the driver doesn't have pre_reset and post_reset methods then
 * we attempt to unbind it and rebind afterward.
 */
int usb_reset_device(struct usb_device *udev)
{
	int ret;
	int i;
	struct usb_host_config *config = udev->actconfig;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED) {
		dev_dbg(&udev->dev, "device reset not allowed in state %d\n",
				udev->state);
		return -EINVAL;
	}

	/* Prevent autosuspend during the reset */
	usb_autoresume_device(udev);

	if (config) {
		for (i = 0; i < config->desc.bNumInterfaces; ++i) {
			struct usb_interface *cintf = config->interface[i];
			struct usb_driver *drv;
			int unbind = 0;

			if (cintf->dev.driver) {
				drv = to_usb_driver(cintf->dev.driver);
				if (drv->pre_reset && drv->post_reset)
					unbind = (drv->pre_reset)(cintf);
				else if (cintf->condition ==
						USB_INTERFACE_BOUND)
					unbind = 1;
				if (unbind)
					usb_forced_unbind_intf(cintf);
			}
		}
	}

	ret = usb_reset_and_verify_device(udev);

	if (config) {
		for (i = config->desc.bNumInterfaces - 1; i >= 0; --i) {
			struct usb_interface *cintf = config->interface[i];
			struct usb_driver *drv;
			int rebind = cintf->needs_binding;

			if (!rebind && cintf->dev.driver) {
				drv = to_usb_driver(cintf->dev.driver);
				if (drv->post_reset)
					rebind = (drv->post_reset)(cintf);
				else if (cintf->condition ==
						USB_INTERFACE_BOUND)
					rebind = 1;
			}
			if (ret == 0 && rebind)
				usb_rebind_intf(cintf);
		}
	}

	usb_autosuspend_device(udev);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_reset_device);


/**
 * usb_queue_reset_device - Reset a USB device from an atomic context
 * @iface: USB interface belonging to the device to reset
 *
 * This function can be used to reset a USB device from an atomic
 * context, where usb_reset_device() won't work (as it blocks).
 *
 * Doing a reset via this method is functionally equivalent to calling
 * usb_reset_device(), except for the fact that it is delayed to a
 * workqueue. This means that any drivers bound to other interfaces
 * might be unbound, as well as users from usbfs in user space.
 *
 * Corner cases:
 *
 * - Scheduling two resets at the same time from two different drivers
 *   attached to two different interfaces of the same device is
 *   possible; depending on how the driver attached to each interface
 *   handles ->pre_reset(), the second reset might happen or not.
 *
 * - If a driver is unbound and it had a pending reset, the reset will
 *   be cancelled.
 *
 * - This function can be called during .probe() or .disconnect()
 *   times. On return from .disconnect(), any pending resets will be
 *   cancelled.
 *
 * There is no no need to lock/unlock the @reset_ws as schedule_work()
 * does its own.
 *
 * NOTE: We don't do any reference count tracking because it is not
 *     needed. The lifecycle of the work_struct is tied to the
 *     usb_interface. Before destroying the interface we cancel the
 *     work_struct, so the fact that work_struct is queued and or
 *     running means the interface (and thus, the device) exist and
 *     are referenced.
 */
void usb_queue_reset_device(struct usb_interface *iface)
{
	schedule_work(&iface->reset_ws);
}
EXPORT_SYMBOL_GPL(usb_queue_reset_device);
