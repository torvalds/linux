// SPDX-License-Identifier: GPL-2.0
/*
* Host Controller Driver for the Elan Digital Systems U132 adapter
*
* Copyright(C) 2006 Elan Digital Systems Limited
* http://www.elandigitalsystems.com
*
* Author and Maintainer - Tony Olech - Elan Digital Systems
* tony.olech@elandigitalsystems.com
*
* This driver was written by Tony Olech(tony.olech@elandigitalsystems.com)
* based on various USB host drivers in the 2.6.15 linux kernel
* with constant reference to the 3rd Edition of Linux Device Drivers
* published by O'Reilly
*
* The U132 adapter is a USB to CardBus adapter specifically designed
* for PC cards that contain an OHCI host controller. Typical PC cards
* are the Orange Mobile 3G Option GlobeTrotter Fusion card.
*
* The U132 adapter will *NOT *work with PC cards that do not contain
* an OHCI controller. A simple way to test whether a PC card has an
* OHCI controller as an interface is to insert the PC card directly
* into a laptop(or desktop) with a CardBus slot and if "lspci" shows
* a new USB controller and "lsusb -v" shows a new OHCI Host Controller
* then there is a good chance that the U132 adapter will support the
* PC card.(you also need the specific client driver for the PC card)
*
* Please inform the Author and Maintainer about any PC cards that
* contain OHCI Host Controller and work when directly connected to
* an embedded CardBus slot but do not work when they are connected
* via an ELAN U132 adapter.
*
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pci_ids.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

	/* FIXME ohci.h is ONLY for internal use by the OHCI driver.
	 * If you're going to try stuff like this, you need to split
	 * out shareable stuff (register declarations?) into its own
	 * file, maybe name <linux/usb/ohci.h>
	 */

#include "ohci.h"
#define OHCI_CONTROL_INIT OHCI_CTRL_CBSR
#define OHCI_INTR_INIT (OHCI_INTR_MIE | OHCI_INTR_UE | OHCI_INTR_RD | \
	OHCI_INTR_WDH)
MODULE_AUTHOR("Tony Olech - Elan Digital Systems Limited");
MODULE_DESCRIPTION("U132 USB Host Controller Driver");
MODULE_LICENSE("GPL");
#define INT_MODULE_PARM(n, v) static int n = v;module_param(n, int, 0444)
INT_MODULE_PARM(testing, 0);
/* Some boards misreport power switching/overcurrent*/
static bool distrust_firmware = true;
module_param(distrust_firmware, bool, 0);
MODULE_PARM_DESC(distrust_firmware, "true to distrust firmware power/overcurren"
	"t setup");
static DECLARE_WAIT_QUEUE_HEAD(u132_hcd_wait);
/*
* u132_module_lock exists to protect access to global variables
*
*/
static struct mutex u132_module_lock;
static int u132_exiting;
static int u132_instances;
static struct list_head u132_static_list;
/*
* end of the global variables protected by u132_module_lock
*/
static struct workqueue_struct *workqueue;
#define MAX_U132_PORTS 7
#define MAX_U132_ADDRS 128
#define MAX_U132_UDEVS 4
#define MAX_U132_ENDPS 100
#define MAX_U132_RINGS 4
static const char *cc_to_text[16] = {
	"No Error ",
	"CRC Error ",
	"Bit Stuff ",
	"Data Togg ",
	"Stall ",
	"DevNotResp ",
	"PIDCheck ",
	"UnExpPID ",
	"DataOver ",
	"DataUnder ",
	"(for hw) ",
	"(for hw) ",
	"BufferOver ",
	"BuffUnder ",
	"(for HCD) ",
	"(for HCD) "
};
struct u132_port {
	struct u132 *u132;
	int reset;
	int enable;
	int power;
	int Status;
};
struct u132_addr {
	u8 address;
};
struct u132_udev {
	struct kref kref;
	struct usb_device *usb_device;
	u8 enumeration;
	u8 udev_number;
	u8 usb_addr;
	u8 portnumber;
	u8 endp_number_in[16];
	u8 endp_number_out[16];
};
#define ENDP_QUEUE_SHIFT 3
#define ENDP_QUEUE_SIZE (1<<ENDP_QUEUE_SHIFT)
#define ENDP_QUEUE_MASK (ENDP_QUEUE_SIZE-1)
struct u132_urbq {
	struct list_head urb_more;
	struct urb *urb;
};
struct u132_spin {
	spinlock_t slock;
};
struct u132_endp {
	struct kref kref;
	u8 udev_number;
	u8 endp_number;
	u8 usb_addr;
	u8 usb_endp;
	struct u132 *u132;
	struct list_head endp_ring;
	struct u132_ring *ring;
	unsigned toggle_bits:2;
	unsigned active:1;
	unsigned delayed:1;
	unsigned input:1;
	unsigned output:1;
	unsigned pipetype:2;
	unsigned dequeueing:1;
	unsigned edset_flush:1;
	unsigned spare_bits:14;
	unsigned long jiffies;
	struct usb_host_endpoint *hep;
	struct u132_spin queue_lock;
	u16 queue_size;
	u16 queue_last;
	u16 queue_next;
	struct urb *urb_list[ENDP_QUEUE_SIZE];
	struct list_head urb_more;
	struct delayed_work scheduler;
};
struct u132_ring {
	unsigned in_use:1;
	unsigned length:7;
	u8 number;
	struct u132 *u132;
	struct u132_endp *curr_endp;
	struct delayed_work scheduler;
};
struct u132 {
	struct kref kref;
	struct list_head u132_list;
	struct mutex sw_lock;
	struct mutex scheduler_lock;
	struct u132_platform_data *board;
	struct platform_device *platform_dev;
	struct u132_ring ring[MAX_U132_RINGS];
	int sequence_num;
	int going;
	int power;
	int reset;
	int num_ports;
	u32 hc_control;
	u32 hc_fminterval;
	u32 hc_roothub_status;
	u32 hc_roothub_a;
	u32 hc_roothub_portstatus[MAX_ROOT_PORTS];
	int flags;
	unsigned long next_statechange;
	struct delayed_work monitor;
	int num_endpoints;
	struct u132_addr addr[MAX_U132_ADDRS];
	struct u132_udev udev[MAX_U132_UDEVS];
	struct u132_port port[MAX_U132_PORTS];
	struct u132_endp *endp[MAX_U132_ENDPS];
};

/*
* these cannot be inlines because we need the structure offset!!
* Does anyone have a better way?????
*/
#define ftdi_read_pcimem(pdev, member, data) usb_ftdi_elan_read_pcimem(pdev, \
	offsetof(struct ohci_regs, member), 0, data);
#define ftdi_write_pcimem(pdev, member, data) usb_ftdi_elan_write_pcimem(pdev, \
	offsetof(struct ohci_regs, member), 0, data);
#define u132_read_pcimem(u132, member, data) \
	usb_ftdi_elan_read_pcimem(u132->platform_dev, offsetof(struct \
	ohci_regs, member), 0, data);
#define u132_write_pcimem(u132, member, data) \
	usb_ftdi_elan_write_pcimem(u132->platform_dev, offsetof(struct \
	ohci_regs, member), 0, data);
static inline struct u132 *udev_to_u132(struct u132_udev *udev)
{
	u8 udev_number = udev->udev_number;
	return container_of(udev, struct u132, udev[udev_number]);
}

static inline struct u132 *hcd_to_u132(struct usb_hcd *hcd)
{
	return (struct u132 *)(hcd->hcd_priv);
}

static inline struct usb_hcd *u132_to_hcd(struct u132 *u132)
{
	return container_of((void *)u132, struct usb_hcd, hcd_priv);
}

static inline void u132_disable(struct u132 *u132)
{
	u132_to_hcd(u132)->state = HC_STATE_HALT;
}


#define kref_to_u132(d) container_of(d, struct u132, kref)
#define kref_to_u132_endp(d) container_of(d, struct u132_endp, kref)
#define kref_to_u132_udev(d) container_of(d, struct u132_udev, kref)
#include "../misc/usb_u132.h"
static const char hcd_name[] = "u132_hcd";
#define PORT_C_MASK ((USB_PORT_STAT_C_CONNECTION | USB_PORT_STAT_C_ENABLE | \
	USB_PORT_STAT_C_SUSPEND | USB_PORT_STAT_C_OVERCURRENT | \
	USB_PORT_STAT_C_RESET) << 16)
static void u132_hcd_delete(struct kref *kref)
{
	struct u132 *u132 = kref_to_u132(kref);
	struct platform_device *pdev = u132->platform_dev;
	struct usb_hcd *hcd = u132_to_hcd(u132);
	u132->going += 1;
	mutex_lock(&u132_module_lock);
	list_del_init(&u132->u132_list);
	u132_instances -= 1;
	mutex_unlock(&u132_module_lock);
	dev_warn(&u132->platform_dev->dev, "FREEING the hcd=%p and thus the u13"
		"2=%p going=%d pdev=%p\n", hcd, u132, u132->going, pdev);
	usb_put_hcd(hcd);
}

static inline void u132_u132_put_kref(struct u132 *u132)
{
	kref_put(&u132->kref, u132_hcd_delete);
}

static inline void u132_u132_init_kref(struct u132 *u132)
{
	kref_init(&u132->kref);
}

static void u132_udev_delete(struct kref *kref)
{
	struct u132_udev *udev = kref_to_u132_udev(kref);
	udev->udev_number = 0;
	udev->usb_device = NULL;
	udev->usb_addr = 0;
	udev->enumeration = 0;
}

static inline void u132_udev_put_kref(struct u132 *u132, struct u132_udev *udev)
{
	kref_put(&udev->kref, u132_udev_delete);
}

static inline void u132_udev_get_kref(struct u132 *u132, struct u132_udev *udev)
{
	kref_get(&udev->kref);
}

static inline void u132_udev_init_kref(struct u132 *u132,
	struct u132_udev *udev)
{
	kref_init(&udev->kref);
}

static inline void u132_ring_put_kref(struct u132 *u132, struct u132_ring *ring)
{
	kref_put(&u132->kref, u132_hcd_delete);
}

static void u132_ring_requeue_work(struct u132 *u132, struct u132_ring *ring,
	unsigned int delta)
{
	if (delta > 0) {
		if (queue_delayed_work(workqueue, &ring->scheduler, delta))
			return;
	} else if (queue_delayed_work(workqueue, &ring->scheduler, 0))
		return;
	kref_put(&u132->kref, u132_hcd_delete);
}

static void u132_ring_queue_work(struct u132 *u132, struct u132_ring *ring,
	unsigned int delta)
{
	kref_get(&u132->kref);
	u132_ring_requeue_work(u132, ring, delta);
}

static void u132_ring_cancel_work(struct u132 *u132, struct u132_ring *ring)
{
	if (cancel_delayed_work(&ring->scheduler))
		kref_put(&u132->kref, u132_hcd_delete);
}

static void u132_endp_delete(struct kref *kref)
{
	struct u132_endp *endp = kref_to_u132_endp(kref);
	struct u132 *u132 = endp->u132;
	u8 usb_addr = endp->usb_addr;
	u8 usb_endp = endp->usb_endp;
	u8 address = u132->addr[usb_addr].address;
	struct u132_udev *udev = &u132->udev[address];
	u8 endp_number = endp->endp_number;
	struct usb_host_endpoint *hep = endp->hep;
	struct u132_ring *ring = endp->ring;
	struct list_head *head = &endp->endp_ring;
	ring->length -= 1;
	if (endp == ring->curr_endp) {
		if (list_empty(head)) {
			ring->curr_endp = NULL;
			list_del(head);
		} else {
			struct u132_endp *next_endp = list_entry(head->next,
				struct u132_endp, endp_ring);
			ring->curr_endp = next_endp;
			list_del(head);
		}
	} else
		list_del(head);
	if (endp->input) {
		udev->endp_number_in[usb_endp] = 0;
		u132_udev_put_kref(u132, udev);
	}
	if (endp->output) {
		udev->endp_number_out[usb_endp] = 0;
		u132_udev_put_kref(u132, udev);
	}
	u132->endp[endp_number - 1] = NULL;
	hep->hcpriv = NULL;
	kfree(endp);
	u132_u132_put_kref(u132);
}

static inline void u132_endp_put_kref(struct u132 *u132, struct u132_endp *endp)
{
	kref_put(&endp->kref, u132_endp_delete);
}

static inline void u132_endp_get_kref(struct u132 *u132, struct u132_endp *endp)
{
	kref_get(&endp->kref);
}

static inline void u132_endp_init_kref(struct u132 *u132,
	struct u132_endp *endp)
{
	kref_init(&endp->kref);
	kref_get(&u132->kref);
}

static void u132_endp_queue_work(struct u132 *u132, struct u132_endp *endp,
	unsigned int delta)
{
	if (queue_delayed_work(workqueue, &endp->scheduler, delta))
		kref_get(&endp->kref);
}

static void u132_endp_cancel_work(struct u132 *u132, struct u132_endp *endp)
{
	if (cancel_delayed_work(&endp->scheduler))
		kref_put(&endp->kref, u132_endp_delete);
}

static inline void u132_monitor_put_kref(struct u132 *u132)
{
	kref_put(&u132->kref, u132_hcd_delete);
}

static void u132_monitor_queue_work(struct u132 *u132, unsigned int delta)
{
	if (queue_delayed_work(workqueue, &u132->monitor, delta))
		kref_get(&u132->kref);
}

static void u132_monitor_requeue_work(struct u132 *u132, unsigned int delta)
{
	if (!queue_delayed_work(workqueue, &u132->monitor, delta))
		kref_put(&u132->kref, u132_hcd_delete);
}

static void u132_monitor_cancel_work(struct u132 *u132)
{
	if (cancel_delayed_work(&u132->monitor))
		kref_put(&u132->kref, u132_hcd_delete);
}

static int read_roothub_info(struct u132 *u132)
{
	u32 revision;
	int retval;
	retval = u132_read_pcimem(u132, revision, &revision);
	if (retval) {
		dev_err(&u132->platform_dev->dev, "error %d accessing device co"
			"ntrol\n", retval);
		return retval;
	} else if ((revision & 0xFF) == 0x10) {
	} else if ((revision & 0xFF) == 0x11) {
	} else {
		dev_err(&u132->platform_dev->dev, "device revision is not valid"
			" %08X\n", revision);
		return -ENODEV;
	}
	retval = u132_read_pcimem(u132, control, &u132->hc_control);
	if (retval) {
		dev_err(&u132->platform_dev->dev, "error %d accessing device co"
			"ntrol\n", retval);
		return retval;
	}
	retval = u132_read_pcimem(u132, roothub.status,
		&u132->hc_roothub_status);
	if (retval) {
		dev_err(&u132->platform_dev->dev, "error %d accessing device re"
			"g roothub.status\n", retval);
		return retval;
	}
	retval = u132_read_pcimem(u132, roothub.a, &u132->hc_roothub_a);
	if (retval) {
		dev_err(&u132->platform_dev->dev, "error %d accessing device re"
			"g roothub.a\n", retval);
		return retval;
	}
	{
		int I = u132->num_ports;
		int i = 0;
		while (I-- > 0) {
			retval = u132_read_pcimem(u132, roothub.portstatus[i],
				&u132->hc_roothub_portstatus[i]);
			if (retval) {
				dev_err(&u132->platform_dev->dev, "error %d acc"
					"essing device roothub.portstatus[%d]\n"
					, retval, i);
				return retval;
			} else
				i += 1;
		}
	}
	return 0;
}

static void u132_hcd_monitor_work(struct work_struct *work)
{
	struct u132 *u132 = container_of(work, struct u132, monitor.work);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		u132_monitor_put_kref(u132);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		u132_monitor_put_kref(u132);
		return;
	} else {
		int retval;
		mutex_lock(&u132->sw_lock);
		retval = read_roothub_info(u132);
		if (retval) {
			struct usb_hcd *hcd = u132_to_hcd(u132);
			u132_disable(u132);
			u132->going = 1;
			mutex_unlock(&u132->sw_lock);
			usb_hc_died(hcd);
			ftdi_elan_gone_away(u132->platform_dev);
			u132_monitor_put_kref(u132);
			return;
		} else {
			u132_monitor_requeue_work(u132, 500);
			mutex_unlock(&u132->sw_lock);
			return;
		}
	}
}

static void u132_hcd_giveback_urb(struct u132 *u132, struct u132_endp *endp,
	struct urb *urb, int status)
{
	struct u132_ring *ring;
	unsigned long irqs;
	struct usb_hcd *hcd = u132_to_hcd(u132);
	urb->error_count = 0;
	spin_lock_irqsave(&endp->queue_lock.slock, irqs);
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	endp->queue_next += 1;
	if (ENDP_QUEUE_SIZE > --endp->queue_size) {
		endp->active = 0;
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
	} else {
		struct list_head *next = endp->urb_more.next;
		struct u132_urbq *urbq = list_entry(next, struct u132_urbq,
			urb_more);
		list_del(next);
		endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] =
			urbq->urb;
		endp->active = 0;
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		kfree(urbq);
	}
	mutex_lock(&u132->scheduler_lock);
	ring = endp->ring;
	ring->in_use = 0;
	u132_ring_cancel_work(u132, ring);
	u132_ring_queue_work(u132, ring, 0);
	mutex_unlock(&u132->scheduler_lock);
	u132_endp_put_kref(u132, endp);
	usb_hcd_giveback_urb(hcd, urb, status);
}

static void u132_hcd_forget_urb(struct u132 *u132, struct u132_endp *endp,
	struct urb *urb, int status)
{
	u132_endp_put_kref(u132, endp);
}

static void u132_hcd_abandon_urb(struct u132 *u132, struct u132_endp *endp,
	struct urb *urb, int status)
{
	unsigned long irqs;
	struct usb_hcd *hcd = u132_to_hcd(u132);
	urb->error_count = 0;
	spin_lock_irqsave(&endp->queue_lock.slock, irqs);
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	endp->queue_next += 1;
	if (ENDP_QUEUE_SIZE > --endp->queue_size) {
		endp->active = 0;
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
	} else {
		struct list_head *next = endp->urb_more.next;
		struct u132_urbq *urbq = list_entry(next, struct u132_urbq,
			urb_more);
		list_del(next);
		endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] =
			urbq->urb;
		endp->active = 0;
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		kfree(urbq);
	}
	usb_hcd_giveback_urb(hcd, urb, status);
}

static inline int edset_input(struct u132 *u132, struct u132_ring *ring,
	struct u132_endp *endp, struct urb *urb, u8 address, u8 toggle_bits,
	void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
	int toggle_bits, int error_count, int condition_code, int repeat_number,
	 int halted, int skipped, int actual, int non_null))
{
	return usb_ftdi_elan_edset_input(u132->platform_dev, ring->number, endp,
		 urb, address, endp->usb_endp, toggle_bits, callback);
}

static inline int edset_setup(struct u132 *u132, struct u132_ring *ring,
	struct u132_endp *endp, struct urb *urb, u8 address, u8 toggle_bits,
	void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
	int toggle_bits, int error_count, int condition_code, int repeat_number,
	 int halted, int skipped, int actual, int non_null))
{
	return usb_ftdi_elan_edset_setup(u132->platform_dev, ring->number, endp,
		 urb, address, endp->usb_endp, toggle_bits, callback);
}

static inline int edset_single(struct u132 *u132, struct u132_ring *ring,
	struct u132_endp *endp, struct urb *urb, u8 address, u8 toggle_bits,
	void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
	int toggle_bits, int error_count, int condition_code, int repeat_number,
	 int halted, int skipped, int actual, int non_null))
{
	return usb_ftdi_elan_edset_single(u132->platform_dev, ring->number,
		endp, urb, address, endp->usb_endp, toggle_bits, callback);
}

static inline int edset_output(struct u132 *u132, struct u132_ring *ring,
	struct u132_endp *endp, struct urb *urb, u8 address, u8 toggle_bits,
	void (*callback) (void *endp, struct urb *urb, u8 *buf, int len,
	int toggle_bits, int error_count, int condition_code, int repeat_number,
	 int halted, int skipped, int actual, int non_null))
{
	return usb_ftdi_elan_edset_output(u132->platform_dev, ring->number,
		endp, urb, address, endp->usb_endp, toggle_bits, callback);
}


/*
* must not LOCK sw_lock
*
*/
static void u132_hcd_interrupt_recv(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	struct u132_udev *udev = &u132->udev[address];
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		struct u132_ring *ring = endp->ring;
		u8 *u = urb->transfer_buffer + urb->actual_length;
		u8 *b = buf;
		int L = len;

		while (L-- > 0)
			*u++ = *b++;

		urb->actual_length += len;
		if ((condition_code == TD_CC_NOERROR) &&
			(urb->transfer_buffer_length > urb->actual_length)) {
			endp->toggle_bits = toggle_bits;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0,
				1 & toggle_bits);
			if (urb->actual_length > 0) {
				int retval;
				mutex_unlock(&u132->scheduler_lock);
				retval = edset_single(u132, ring, endp, urb,
					address, endp->toggle_bits,
					u132_hcd_interrupt_recv);
				if (retval != 0)
					u132_hcd_giveback_urb(u132, endp, urb,
						retval);
			} else {
				ring->in_use = 0;
				endp->active = 0;
				endp->jiffies = jiffies +
					msecs_to_jiffies(urb->interval);
				u132_ring_cancel_work(u132, ring);
				u132_ring_queue_work(u132, ring, 0);
				mutex_unlock(&u132->scheduler_lock);
				u132_endp_put_kref(u132, endp);
			}
			return;
		} else if ((condition_code == TD_DATAUNDERRUN) &&
			((urb->transfer_flags & URB_SHORT_NOT_OK) == 0)) {
			endp->toggle_bits = toggle_bits;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0,
				1 & toggle_bits);
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb, 0);
			return;
		} else {
			if (condition_code == TD_CC_NOERROR) {
				endp->toggle_bits = toggle_bits;
				usb_settoggle(udev->usb_device, endp->usb_endp,
					0, 1 & toggle_bits);
			} else if (condition_code == TD_CC_STALL) {
				endp->toggle_bits = 0x2;
				usb_settoggle(udev->usb_device, endp->usb_endp,
					0, 0);
			} else {
				endp->toggle_bits = 0x2;
				usb_settoggle(udev->usb_device, endp->usb_endp,
					0, 0);
				dev_err(&u132->platform_dev->dev, "urb=%p givin"
					"g back INTERRUPT %s\n", urb,
					cc_to_text[condition_code]);
			}
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb,
				cc_to_error[condition_code]);
			return;
		}
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_bulk_output_sent(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		struct u132_ring *ring = endp->ring;
		urb->actual_length += len;
		endp->toggle_bits = toggle_bits;
		if (urb->transfer_buffer_length > urb->actual_length) {
			int retval;
			mutex_unlock(&u132->scheduler_lock);
			retval = edset_output(u132, ring, endp, urb, address,
				endp->toggle_bits, u132_hcd_bulk_output_sent);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		} else {
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb, 0);
			return;
		}
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_bulk_input_recv(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	struct u132_udev *udev = &u132->udev[address];
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		struct u132_ring *ring = endp->ring;
		u8 *u = urb->transfer_buffer + urb->actual_length;
		u8 *b = buf;
		int L = len;

		while (L-- > 0)
			*u++ = *b++;

		urb->actual_length += len;
		if ((condition_code == TD_CC_NOERROR) &&
			(urb->transfer_buffer_length > urb->actual_length)) {
			int retval;
			endp->toggle_bits = toggle_bits;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0,
				1 & toggle_bits);
			mutex_unlock(&u132->scheduler_lock);
			retval = usb_ftdi_elan_edset_input(u132->platform_dev,
				ring->number, endp, urb, address,
				endp->usb_endp, endp->toggle_bits,
				u132_hcd_bulk_input_recv);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		} else if (condition_code == TD_CC_NOERROR) {
			endp->toggle_bits = toggle_bits;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0,
				1 & toggle_bits);
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb,
				cc_to_error[condition_code]);
			return;
		} else if ((condition_code == TD_DATAUNDERRUN) &&
			((urb->transfer_flags & URB_SHORT_NOT_OK) == 0)) {
			endp->toggle_bits = toggle_bits;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0,
				1 & toggle_bits);
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb, 0);
			return;
		} else if (condition_code == TD_DATAUNDERRUN) {
			endp->toggle_bits = toggle_bits;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0,
				1 & toggle_bits);
			dev_warn(&u132->platform_dev->dev, "urb=%p(SHORT NOT OK"
				") giving back BULK IN %s\n", urb,
				cc_to_text[condition_code]);
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb, 0);
			return;
		} else if (condition_code == TD_CC_STALL) {
			endp->toggle_bits = 0x2;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0, 0);
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb,
				cc_to_error[condition_code]);
			return;
		} else {
			endp->toggle_bits = 0x2;
			usb_settoggle(udev->usb_device, endp->usb_endp, 0, 0);
			dev_err(&u132->platform_dev->dev, "urb=%p giving back B"
				"ULK IN code=%d %s\n", urb, condition_code,
				cc_to_text[condition_code]);
			mutex_unlock(&u132->scheduler_lock);
			u132_hcd_giveback_urb(u132, endp, urb,
				cc_to_error[condition_code]);
			return;
		}
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_configure_empty_sent(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_configure_input_recv(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		struct u132_ring *ring = endp->ring;
		u8 *u = urb->transfer_buffer;
		u8 *b = buf;
		int L = len;

		while (L-- > 0)
			*u++ = *b++;

		urb->actual_length = len;
		if ((condition_code == TD_CC_NOERROR) || ((condition_code ==
			TD_DATAUNDERRUN) && ((urb->transfer_flags &
			URB_SHORT_NOT_OK) == 0))) {
			int retval;
			mutex_unlock(&u132->scheduler_lock);
			retval = usb_ftdi_elan_edset_empty(u132->platform_dev,
				ring->number, endp, urb, address,
				endp->usb_endp, 0x3,
				u132_hcd_configure_empty_sent);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		} else if (condition_code == TD_CC_STALL) {
			mutex_unlock(&u132->scheduler_lock);
			dev_warn(&u132->platform_dev->dev, "giving back SETUP I"
				"NPUT STALL urb %p\n", urb);
			u132_hcd_giveback_urb(u132, endp, urb,
				cc_to_error[condition_code]);
			return;
		} else {
			mutex_unlock(&u132->scheduler_lock);
			dev_err(&u132->platform_dev->dev, "giving back SETUP IN"
				"PUT %s urb %p\n", cc_to_text[condition_code],
				urb);
			u132_hcd_giveback_urb(u132, endp, urb,
				cc_to_error[condition_code]);
			return;
		}
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_configure_empty_recv(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_configure_setup_sent(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		if (usb_pipein(urb->pipe)) {
			int retval;
			struct u132_ring *ring = endp->ring;
			mutex_unlock(&u132->scheduler_lock);
			retval = usb_ftdi_elan_edset_input(u132->platform_dev,
				ring->number, endp, urb, address,
				endp->usb_endp, 0,
				u132_hcd_configure_input_recv);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		} else {
			int retval;
			struct u132_ring *ring = endp->ring;
			mutex_unlock(&u132->scheduler_lock);
			retval = usb_ftdi_elan_edset_input(u132->platform_dev,
				ring->number, endp, urb, address,
				endp->usb_endp, 0,
				u132_hcd_configure_empty_recv);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		}
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_enumeration_empty_recv(void *data, struct urb *urb,
	u8 *buf, int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	struct u132_udev *udev = &u132->udev[address];
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		u132->addr[0].address = 0;
		endp->usb_addr = udev->usb_addr;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_enumeration_address_sent(void *data, struct urb *urb,
	u8 *buf, int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		int retval;
		struct u132_ring *ring = endp->ring;
		mutex_unlock(&u132->scheduler_lock);
		retval = usb_ftdi_elan_edset_input(u132->platform_dev,
			ring->number, endp, urb, 0, endp->usb_endp, 0,
			u132_hcd_enumeration_empty_recv);
		if (retval != 0)
			u132_hcd_giveback_urb(u132, endp, urb, retval);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_initial_empty_sent(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_initial_input_recv(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		int retval;
		struct u132_ring *ring = endp->ring;
		u8 *u = urb->transfer_buffer;
		u8 *b = buf;
		int L = len;

		while (L-- > 0)
			*u++ = *b++;

		urb->actual_length = len;
		mutex_unlock(&u132->scheduler_lock);
		retval = usb_ftdi_elan_edset_empty(u132->platform_dev,
			ring->number, endp, urb, address, endp->usb_endp, 0x3,
			u132_hcd_initial_empty_sent);
		if (retval != 0)
			u132_hcd_giveback_urb(u132, endp, urb, retval);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

static void u132_hcd_initial_setup_sent(void *data, struct urb *urb, u8 *buf,
	int len, int toggle_bits, int error_count, int condition_code,
	int repeat_number, int halted, int skipped, int actual, int non_null)
{
	struct u132_endp *endp = data;
	struct u132 *u132 = endp->u132;
	u8 address = u132->addr[endp->usb_addr].address;
	mutex_lock(&u132->scheduler_lock);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_forget_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (endp->dequeueing) {
		endp->dequeueing = 0;
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -EINTR);
		return;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, -ENODEV);
		return;
	} else if (!urb->unlinked) {
		int retval;
		struct u132_ring *ring = endp->ring;
		mutex_unlock(&u132->scheduler_lock);
		retval = usb_ftdi_elan_edset_input(u132->platform_dev,
			ring->number, endp, urb, address, endp->usb_endp, 0,
			u132_hcd_initial_input_recv);
		if (retval != 0)
			u132_hcd_giveback_urb(u132, endp, urb, retval);
		return;
	} else {
		dev_err(&u132->platform_dev->dev, "CALLBACK called urb=%p "
				"unlinked=%d\n", urb, urb->unlinked);
		mutex_unlock(&u132->scheduler_lock);
		u132_hcd_giveback_urb(u132, endp, urb, 0);
		return;
	}
}

/*
* this work function is only executed from the work queue
*
*/
static void u132_hcd_ring_work_scheduler(struct work_struct *work)
{
	struct u132_ring *ring =
		container_of(work, struct u132_ring, scheduler.work);
	struct u132 *u132 = ring->u132;
	mutex_lock(&u132->scheduler_lock);
	if (ring->in_use) {
		mutex_unlock(&u132->scheduler_lock);
		u132_ring_put_kref(u132, ring);
		return;
	} else if (ring->curr_endp) {
		struct u132_endp *endp, *last_endp = ring->curr_endp;
		unsigned long wakeup = 0;
		list_for_each_entry(endp, &last_endp->endp_ring, endp_ring) {
			if (endp->queue_next == endp->queue_last) {
			} else if ((endp->delayed == 0)
				|| time_after_eq(jiffies, endp->jiffies)) {
				ring->curr_endp = endp;
				u132_endp_cancel_work(u132, last_endp);
				u132_endp_queue_work(u132, last_endp, 0);
				mutex_unlock(&u132->scheduler_lock);
				u132_ring_put_kref(u132, ring);
				return;
			} else {
				unsigned long delta = endp->jiffies - jiffies;
				if (delta > wakeup)
					wakeup = delta;
			}
		}
		if (last_endp->queue_next == last_endp->queue_last) {
		} else if ((last_endp->delayed == 0) || time_after_eq(jiffies,
			last_endp->jiffies)) {
			u132_endp_cancel_work(u132, last_endp);
			u132_endp_queue_work(u132, last_endp, 0);
			mutex_unlock(&u132->scheduler_lock);
			u132_ring_put_kref(u132, ring);
			return;
		} else {
			unsigned long delta = last_endp->jiffies - jiffies;
			if (delta > wakeup)
				wakeup = delta;
		}
		if (wakeup > 0) {
			u132_ring_requeue_work(u132, ring, wakeup);
			mutex_unlock(&u132->scheduler_lock);
			return;
		} else {
			mutex_unlock(&u132->scheduler_lock);
			u132_ring_put_kref(u132, ring);
			return;
		}
	} else {
		mutex_unlock(&u132->scheduler_lock);
		u132_ring_put_kref(u132, ring);
		return;
	}
}

static void u132_hcd_endp_work_scheduler(struct work_struct *work)
{
	struct u132_ring *ring;
	struct u132_endp *endp =
		container_of(work, struct u132_endp, scheduler.work);
	struct u132 *u132 = endp->u132;
	mutex_lock(&u132->scheduler_lock);
	ring = endp->ring;
	if (endp->edset_flush) {
		endp->edset_flush = 0;
		if (endp->dequeueing)
			usb_ftdi_elan_edset_flush(u132->platform_dev,
				ring->number, endp);
		mutex_unlock(&u132->scheduler_lock);
		u132_endp_put_kref(u132, endp);
		return;
	} else if (endp->active) {
		mutex_unlock(&u132->scheduler_lock);
		u132_endp_put_kref(u132, endp);
		return;
	} else if (ring->in_use) {
		mutex_unlock(&u132->scheduler_lock);
		u132_endp_put_kref(u132, endp);
		return;
	} else if (endp->queue_next == endp->queue_last) {
		mutex_unlock(&u132->scheduler_lock);
		u132_endp_put_kref(u132, endp);
		return;
	} else if (endp->pipetype == PIPE_INTERRUPT) {
		u8 address = u132->addr[endp->usb_addr].address;
		if (ring->in_use) {
			mutex_unlock(&u132->scheduler_lock);
			u132_endp_put_kref(u132, endp);
			return;
		} else {
			int retval;
			struct urb *urb = endp->urb_list[ENDP_QUEUE_MASK &
				endp->queue_next];
			endp->active = 1;
			ring->curr_endp = endp;
			ring->in_use = 1;
			mutex_unlock(&u132->scheduler_lock);
			retval = edset_single(u132, ring, endp, urb, address,
				endp->toggle_bits, u132_hcd_interrupt_recv);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		}
	} else if (endp->pipetype == PIPE_CONTROL) {
		u8 address = u132->addr[endp->usb_addr].address;
		if (ring->in_use) {
			mutex_unlock(&u132->scheduler_lock);
			u132_endp_put_kref(u132, endp);
			return;
		} else if (address == 0) {
			int retval;
			struct urb *urb = endp->urb_list[ENDP_QUEUE_MASK &
				endp->queue_next];
			endp->active = 1;
			ring->curr_endp = endp;
			ring->in_use = 1;
			mutex_unlock(&u132->scheduler_lock);
			retval = edset_setup(u132, ring, endp, urb, address,
				0x2, u132_hcd_initial_setup_sent);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		} else if (endp->usb_addr == 0) {
			int retval;
			struct urb *urb = endp->urb_list[ENDP_QUEUE_MASK &
				endp->queue_next];
			endp->active = 1;
			ring->curr_endp = endp;
			ring->in_use = 1;
			mutex_unlock(&u132->scheduler_lock);
			retval = edset_setup(u132, ring, endp, urb, 0, 0x2,
				u132_hcd_enumeration_address_sent);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		} else {
			int retval;
			struct urb *urb = endp->urb_list[ENDP_QUEUE_MASK &
				endp->queue_next];
			address = u132->addr[endp->usb_addr].address;
			endp->active = 1;
			ring->curr_endp = endp;
			ring->in_use = 1;
			mutex_unlock(&u132->scheduler_lock);
			retval = edset_setup(u132, ring, endp, urb, address,
				0x2, u132_hcd_configure_setup_sent);
			if (retval != 0)
				u132_hcd_giveback_urb(u132, endp, urb, retval);
			return;
		}
	} else {
		if (endp->input) {
			u8 address = u132->addr[endp->usb_addr].address;
			if (ring->in_use) {
				mutex_unlock(&u132->scheduler_lock);
				u132_endp_put_kref(u132, endp);
				return;
			} else {
				int retval;
				struct urb *urb = endp->urb_list[
					ENDP_QUEUE_MASK & endp->queue_next];
				endp->active = 1;
				ring->curr_endp = endp;
				ring->in_use = 1;
				mutex_unlock(&u132->scheduler_lock);
				retval = edset_input(u132, ring, endp, urb,
					address, endp->toggle_bits,
					u132_hcd_bulk_input_recv);
				if (retval == 0) {
				} else
					u132_hcd_giveback_urb(u132, endp, urb,
						retval);
				return;
			}
		} else {	/* output pipe */
			u8 address = u132->addr[endp->usb_addr].address;
			if (ring->in_use) {
				mutex_unlock(&u132->scheduler_lock);
				u132_endp_put_kref(u132, endp);
				return;
			} else {
				int retval;
				struct urb *urb = endp->urb_list[
					ENDP_QUEUE_MASK & endp->queue_next];
				endp->active = 1;
				ring->curr_endp = endp;
				ring->in_use = 1;
				mutex_unlock(&u132->scheduler_lock);
				retval = edset_output(u132, ring, endp, urb,
					address, endp->toggle_bits,
					u132_hcd_bulk_output_sent);
				if (retval == 0) {
				} else
					u132_hcd_giveback_urb(u132, endp, urb,
						retval);
				return;
			}
		}
	}
}
#ifdef CONFIG_PM

static void port_power(struct u132 *u132, int pn, int is_on)
{
	u132->port[pn].power = is_on;
}

#endif

static void u132_power(struct u132 *u132, int is_on)
{
	struct usb_hcd *hcd = u132_to_hcd(u132)
		;	/* hub is inactive unless the port is powered */
	if (is_on) {
		if (u132->power)
			return;
		u132->power = 1;
	} else {
		u132->power = 0;
		hcd->state = HC_STATE_HALT;
	}
}

static int u132_periodic_reinit(struct u132 *u132)
{
	int retval;
	u32 fi = u132->hc_fminterval & 0x03fff;
	u32 fit;
	u32 fminterval;
	retval = u132_read_pcimem(u132, fminterval, &fminterval);
	if (retval)
		return retval;
	fit = fminterval & FIT;
	retval = u132_write_pcimem(u132, fminterval,
		(fit ^ FIT) | u132->hc_fminterval);
	if (retval)
		return retval;
	return u132_write_pcimem(u132, periodicstart,
	       ((9 * fi) / 10) & 0x3fff);
}

static char *hcfs2string(int state)
{
	switch (state) {
	case OHCI_USB_RESET:
		return "reset";
	case OHCI_USB_RESUME:
		return "resume";
	case OHCI_USB_OPER:
		return "operational";
	case OHCI_USB_SUSPEND:
		return "suspend";
	}
	return "?";
}

static int u132_init(struct u132 *u132)
{
	int retval;
	u32 control;
	u132_disable(u132);
	u132->next_statechange = jiffies;
	retval = u132_write_pcimem(u132, intrdisable, OHCI_INTR_MIE);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, control, &control);
	if (retval)
		return retval;
	if (u132->num_ports == 0) {
		u32 rh_a = -1;
		retval = u132_read_pcimem(u132, roothub.a, &rh_a);
		if (retval)
			return retval;
		u132->num_ports = rh_a & RH_A_NDP;
		retval = read_roothub_info(u132);
		if (retval)
			return retval;
	}
	if (u132->num_ports > MAX_U132_PORTS)
		return -EINVAL;

	return 0;
}


/* Start an OHCI controller, set the BUS operational
* resets USB and controller
* enable interrupts
*/
static int u132_run(struct u132 *u132)
{
	int retval;
	u32 control;
	u32 status;
	u32 fminterval;
	u32 periodicstart;
	u32 cmdstatus;
	u32 roothub_a;
	int mask = OHCI_INTR_INIT;
	int first = u132->hc_fminterval == 0;
	int sleep_time = 0;
	int reset_timeout = 30;	/* ... allow extra time */
	u132_disable(u132);
	if (first) {
		u32 temp;
		retval = u132_read_pcimem(u132, fminterval, &temp);
		if (retval)
			return retval;
		u132->hc_fminterval = temp & 0x3fff;
		u132->hc_fminterval |= FSMP(u132->hc_fminterval) << 16;
	}
	retval = u132_read_pcimem(u132, control, &u132->hc_control);
	if (retval)
		return retval;
	dev_info(&u132->platform_dev->dev, "resetting from state '%s', control "
		"= %08X\n", hcfs2string(u132->hc_control & OHCI_CTRL_HCFS),
		u132->hc_control);
	switch (u132->hc_control & OHCI_CTRL_HCFS) {
	case OHCI_USB_OPER:
		sleep_time = 0;
		break;
	case OHCI_USB_SUSPEND:
	case OHCI_USB_RESUME:
		u132->hc_control &= OHCI_CTRL_RWC;
		u132->hc_control |= OHCI_USB_RESUME;
		sleep_time = 10;
		break;
	default:
		u132->hc_control &= OHCI_CTRL_RWC;
		u132->hc_control |= OHCI_USB_RESET;
		sleep_time = 50;
		break;
	}
	retval = u132_write_pcimem(u132, control, u132->hc_control);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, control, &control);
	if (retval)
		return retval;
	msleep(sleep_time);
	retval = u132_read_pcimem(u132, roothub.a, &roothub_a);
	if (retval)
		return retval;
	if (!(roothub_a & RH_A_NPS)) {
		int temp;	/* power down each port */
		for (temp = 0; temp < u132->num_ports; temp++) {
			retval = u132_write_pcimem(u132,
				roothub.portstatus[temp], RH_PS_LSDA);
			if (retval)
				return retval;
		}
	}
	retval = u132_read_pcimem(u132, control, &control);
	if (retval)
		return retval;
retry:
	retval = u132_read_pcimem(u132, cmdstatus, &status);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, cmdstatus, OHCI_HCR);
	if (retval)
		return retval;
extra:	{
		retval = u132_read_pcimem(u132, cmdstatus, &status);
		if (retval)
			return retval;
		if (0 != (status & OHCI_HCR)) {
			if (--reset_timeout == 0) {
				dev_err(&u132->platform_dev->dev, "USB HC reset"
					" timed out!\n");
				return -ENODEV;
			} else {
				msleep(5);
				goto extra;
			}
		}
	}
	if (u132->flags & OHCI_QUIRK_INITRESET) {
		retval = u132_write_pcimem(u132, control, u132->hc_control);
		if (retval)
			return retval;
		retval = u132_read_pcimem(u132, control, &control);
		if (retval)
			return retval;
	}
	retval = u132_write_pcimem(u132, ed_controlhead, 0x00000000);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, ed_bulkhead, 0x11000000);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, hcca, 0x00000000);
	if (retval)
		return retval;
	retval = u132_periodic_reinit(u132);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, fminterval, &fminterval);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, periodicstart, &periodicstart);
	if (retval)
		return retval;
	if (0 == (fminterval & 0x3fff0000) || 0 == periodicstart) {
		if (!(u132->flags & OHCI_QUIRK_INITRESET)) {
			u132->flags |= OHCI_QUIRK_INITRESET;
			goto retry;
		} else
			dev_err(&u132->platform_dev->dev, "init err(%08x %04x)"
				"\n", fminterval, periodicstart);
	}			/* start controller operations */
	u132->hc_control &= OHCI_CTRL_RWC;
	u132->hc_control |= OHCI_CONTROL_INIT | OHCI_CTRL_BLE | OHCI_USB_OPER;
	retval = u132_write_pcimem(u132, control, u132->hc_control);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, cmdstatus, OHCI_BLF);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, cmdstatus, &cmdstatus);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, control, &control);
	if (retval)
		return retval;
	u132_to_hcd(u132)->state = HC_STATE_RUNNING;
	retval = u132_write_pcimem(u132, roothub.status, RH_HS_DRWE);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, intrstatus, mask);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, intrdisable,
		OHCI_INTR_MIE | OHCI_INTR_OC | OHCI_INTR_RHSC | OHCI_INTR_FNO |
		OHCI_INTR_UE | OHCI_INTR_RD | OHCI_INTR_SF | OHCI_INTR_WDH |
		OHCI_INTR_SO);
	if (retval)
		return retval;	/* handle root hub init quirks ... */
	retval = u132_read_pcimem(u132, roothub.a, &roothub_a);
	if (retval)
		return retval;
	roothub_a &= ~(RH_A_PSM | RH_A_OCPM);
	if (u132->flags & OHCI_QUIRK_SUPERIO) {
		roothub_a |= RH_A_NOCP;
		roothub_a &= ~(RH_A_POTPGT | RH_A_NPS);
		retval = u132_write_pcimem(u132, roothub.a, roothub_a);
		if (retval)
			return retval;
	} else if ((u132->flags & OHCI_QUIRK_AMD756) || distrust_firmware) {
		roothub_a |= RH_A_NPS;
		retval = u132_write_pcimem(u132, roothub.a, roothub_a);
		if (retval)
			return retval;
	}
	retval = u132_write_pcimem(u132, roothub.status, RH_HS_LPSC);
	if (retval)
		return retval;
	retval = u132_write_pcimem(u132, roothub.b,
		(roothub_a & RH_A_NPS) ? 0 : RH_B_PPCM);
	if (retval)
		return retval;
	retval = u132_read_pcimem(u132, control, &control);
	if (retval)
		return retval;
	mdelay((roothub_a >> 23) & 0x1fe);
	u132_to_hcd(u132)->state = HC_STATE_RUNNING;
	return 0;
}

static void u132_hcd_stop(struct usb_hcd *hcd)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "u132 device %p(hcd=%p) has b"
			"een removed %d\n", u132, hcd, u132->going);
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device hcd=%p is being remov"
			"ed\n", hcd);
	} else {
		mutex_lock(&u132->sw_lock);
		msleep(100);
		u132_power(u132, 0);
		mutex_unlock(&u132->sw_lock);
	}
}

static int u132_hcd_start(struct usb_hcd *hcd)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else if (hcd->self.controller) {
		int retval;
		struct platform_device *pdev =
			to_platform_device(hcd->self.controller);
		u16 vendor = ((struct u132_platform_data *)
			dev_get_platdata(&pdev->dev))->vendor;
		u16 device = ((struct u132_platform_data *)
			dev_get_platdata(&pdev->dev))->device;
		mutex_lock(&u132->sw_lock);
		msleep(10);
		if (vendor == PCI_VENDOR_ID_AMD && device == 0x740c) {
			u132->flags = OHCI_QUIRK_AMD756;
		} else if (vendor == PCI_VENDOR_ID_OPTI && device == 0xc861) {
			dev_err(&u132->platform_dev->dev, "WARNING: OPTi workar"
				"ounds unavailable\n");
		} else if (vendor == PCI_VENDOR_ID_COMPAQ && device == 0xa0f8)
			u132->flags |= OHCI_QUIRK_ZFMICRO;
		retval = u132_run(u132);
		if (retval) {
			u132_disable(u132);
			u132->going = 1;
		}
		msleep(100);
		mutex_unlock(&u132->sw_lock);
		return retval;
	} else {
		dev_err(&u132->platform_dev->dev, "platform_device missing\n");
		return -ENODEV;
	}
}

static int u132_hcd_reset(struct usb_hcd *hcd)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else {
		int retval;
		mutex_lock(&u132->sw_lock);
		retval = u132_init(u132);
		if (retval) {
			u132_disable(u132);
			u132->going = 1;
		}
		mutex_unlock(&u132->sw_lock);
		return retval;
	}
}

static int create_endpoint_and_queue_int(struct u132 *u132,
	struct u132_udev *udev, struct urb *urb,
	struct usb_device *usb_dev, u8 usb_addr, u8 usb_endp, u8 address,
	gfp_t mem_flags)
{
	struct u132_ring *ring;
	unsigned long irqs;
	int rc;
	u8 endp_number;
	struct u132_endp *endp = kmalloc(sizeof(struct u132_endp), mem_flags);

	if (!endp)
		return -ENOMEM;

	spin_lock_init(&endp->queue_lock.slock);
	spin_lock_irqsave(&endp->queue_lock.slock, irqs);
	rc = usb_hcd_link_urb_to_ep(u132_to_hcd(u132), urb);
	if (rc) {
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		kfree(endp);
		return rc;
	}

	endp_number = ++u132->num_endpoints;
	urb->ep->hcpriv = u132->endp[endp_number - 1] = endp;
	INIT_DELAYED_WORK(&endp->scheduler, u132_hcd_endp_work_scheduler);
	INIT_LIST_HEAD(&endp->urb_more);
	ring = endp->ring = &u132->ring[0];
	if (ring->curr_endp) {
		list_add_tail(&endp->endp_ring, &ring->curr_endp->endp_ring);
	} else {
		INIT_LIST_HEAD(&endp->endp_ring);
		ring->curr_endp = endp;
	}
	ring->length += 1;
	endp->dequeueing = 0;
	endp->edset_flush = 0;
	endp->active = 0;
	endp->delayed = 0;
	endp->endp_number = endp_number;
	endp->u132 = u132;
	endp->hep = urb->ep;
	endp->pipetype = usb_pipetype(urb->pipe);
	u132_endp_init_kref(u132, endp);
	if (usb_pipein(urb->pipe)) {
		endp->toggle_bits = 0x2;
		usb_settoggle(udev->usb_device, usb_endp, 0, 0);
		endp->input = 1;
		endp->output = 0;
		udev->endp_number_in[usb_endp] = endp_number;
		u132_udev_get_kref(u132, udev);
	} else {
		endp->toggle_bits = 0x2;
		usb_settoggle(udev->usb_device, usb_endp, 1, 0);
		endp->input = 0;
		endp->output = 1;
		udev->endp_number_out[usb_endp] = endp_number;
		u132_udev_get_kref(u132, udev);
	}
	urb->hcpriv = u132;
	endp->delayed = 1;
	endp->jiffies = jiffies + msecs_to_jiffies(urb->interval);
	endp->udev_number = address;
	endp->usb_addr = usb_addr;
	endp->usb_endp = usb_endp;
	endp->queue_size = 1;
	endp->queue_last = 0;
	endp->queue_next = 0;
	endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] = urb;
	spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
	u132_endp_queue_work(u132, endp, msecs_to_jiffies(urb->interval));
	return 0;
}

static int queue_int_on_old_endpoint(struct u132 *u132,
	struct u132_udev *udev, struct urb *urb,
	struct usb_device *usb_dev, struct u132_endp *endp, u8 usb_addr,
	u8 usb_endp, u8 address)
{
	urb->hcpriv = u132;
	endp->delayed = 1;
	endp->jiffies = jiffies + msecs_to_jiffies(urb->interval);
	if (endp->queue_size++ < ENDP_QUEUE_SIZE) {
		endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] = urb;
	} else {
		struct u132_urbq *urbq = kmalloc(sizeof(struct u132_urbq),
			GFP_ATOMIC);
		if (urbq == NULL) {
			endp->queue_size -= 1;
			return -ENOMEM;
		} else {
			list_add_tail(&urbq->urb_more, &endp->urb_more);
			urbq->urb = urb;
		}
	}
	return 0;
}

static int create_endpoint_and_queue_bulk(struct u132 *u132,
	struct u132_udev *udev, struct urb *urb,
	struct usb_device *usb_dev, u8 usb_addr, u8 usb_endp, u8 address,
	gfp_t mem_flags)
{
	int ring_number;
	struct u132_ring *ring;
	unsigned long irqs;
	int rc;
	u8 endp_number;
	struct u132_endp *endp = kmalloc(sizeof(struct u132_endp), mem_flags);

	if (!endp)
		return -ENOMEM;

	spin_lock_init(&endp->queue_lock.slock);
	spin_lock_irqsave(&endp->queue_lock.slock, irqs);
	rc = usb_hcd_link_urb_to_ep(u132_to_hcd(u132), urb);
	if (rc) {
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		kfree(endp);
		return rc;
	}

	endp_number = ++u132->num_endpoints;
	urb->ep->hcpriv = u132->endp[endp_number - 1] = endp;
	INIT_DELAYED_WORK(&endp->scheduler, u132_hcd_endp_work_scheduler);
	INIT_LIST_HEAD(&endp->urb_more);
	endp->dequeueing = 0;
	endp->edset_flush = 0;
	endp->active = 0;
	endp->delayed = 0;
	endp->endp_number = endp_number;
	endp->u132 = u132;
	endp->hep = urb->ep;
	endp->pipetype = usb_pipetype(urb->pipe);
	u132_endp_init_kref(u132, endp);
	if (usb_pipein(urb->pipe)) {
		endp->toggle_bits = 0x2;
		usb_settoggle(udev->usb_device, usb_endp, 0, 0);
		ring_number = 3;
		endp->input = 1;
		endp->output = 0;
		udev->endp_number_in[usb_endp] = endp_number;
		u132_udev_get_kref(u132, udev);
	} else {
		endp->toggle_bits = 0x2;
		usb_settoggle(udev->usb_device, usb_endp, 1, 0);
		ring_number = 2;
		endp->input = 0;
		endp->output = 1;
		udev->endp_number_out[usb_endp] = endp_number;
		u132_udev_get_kref(u132, udev);
	}
	ring = endp->ring = &u132->ring[ring_number - 1];
	if (ring->curr_endp) {
		list_add_tail(&endp->endp_ring, &ring->curr_endp->endp_ring);
	} else {
		INIT_LIST_HEAD(&endp->endp_ring);
		ring->curr_endp = endp;
	}
	ring->length += 1;
	urb->hcpriv = u132;
	endp->udev_number = address;
	endp->usb_addr = usb_addr;
	endp->usb_endp = usb_endp;
	endp->queue_size = 1;
	endp->queue_last = 0;
	endp->queue_next = 0;
	endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] = urb;
	spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
	u132_endp_queue_work(u132, endp, 0);
	return 0;
}

static int queue_bulk_on_old_endpoint(struct u132 *u132, struct u132_udev *udev,
	struct urb *urb,
	struct usb_device *usb_dev, struct u132_endp *endp, u8 usb_addr,
	u8 usb_endp, u8 address)
{
	urb->hcpriv = u132;
	if (endp->queue_size++ < ENDP_QUEUE_SIZE) {
		endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] = urb;
	} else {
		struct u132_urbq *urbq = kmalloc(sizeof(struct u132_urbq),
			GFP_ATOMIC);
		if (urbq == NULL) {
			endp->queue_size -= 1;
			return -ENOMEM;
		} else {
			list_add_tail(&urbq->urb_more, &endp->urb_more);
			urbq->urb = urb;
		}
	}
	return 0;
}

static int create_endpoint_and_queue_control(struct u132 *u132,
	struct urb *urb,
	struct usb_device *usb_dev, u8 usb_addr, u8 usb_endp,
	gfp_t mem_flags)
{
	struct u132_ring *ring;
	unsigned long irqs;
	int rc;
	u8 endp_number;
	struct u132_endp *endp = kmalloc(sizeof(struct u132_endp), mem_flags);

	if (!endp)
		return -ENOMEM;

	spin_lock_init(&endp->queue_lock.slock);
	spin_lock_irqsave(&endp->queue_lock.slock, irqs);
	rc = usb_hcd_link_urb_to_ep(u132_to_hcd(u132), urb);
	if (rc) {
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		kfree(endp);
		return rc;
	}

	endp_number = ++u132->num_endpoints;
	urb->ep->hcpriv = u132->endp[endp_number - 1] = endp;
	INIT_DELAYED_WORK(&endp->scheduler, u132_hcd_endp_work_scheduler);
	INIT_LIST_HEAD(&endp->urb_more);
	ring = endp->ring = &u132->ring[0];
	if (ring->curr_endp) {
		list_add_tail(&endp->endp_ring, &ring->curr_endp->endp_ring);
	} else {
		INIT_LIST_HEAD(&endp->endp_ring);
		ring->curr_endp = endp;
	}
	ring->length += 1;
	endp->dequeueing = 0;
	endp->edset_flush = 0;
	endp->active = 0;
	endp->delayed = 0;
	endp->endp_number = endp_number;
	endp->u132 = u132;
	endp->hep = urb->ep;
	u132_endp_init_kref(u132, endp);
	u132_endp_get_kref(u132, endp);
	if (usb_addr == 0) {
		u8 address = u132->addr[usb_addr].address;
		struct u132_udev *udev = &u132->udev[address];
		endp->udev_number = address;
		endp->usb_addr = usb_addr;
		endp->usb_endp = usb_endp;
		endp->input = 1;
		endp->output = 1;
		endp->pipetype = usb_pipetype(urb->pipe);
		u132_udev_init_kref(u132, udev);
		u132_udev_get_kref(u132, udev);
		udev->endp_number_in[usb_endp] = endp_number;
		udev->endp_number_out[usb_endp] = endp_number;
		urb->hcpriv = u132;
		endp->queue_size = 1;
		endp->queue_last = 0;
		endp->queue_next = 0;
		endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] = urb;
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		u132_endp_queue_work(u132, endp, 0);
		return 0;
	} else {		/*(usb_addr > 0) */
		u8 address = u132->addr[usb_addr].address;
		struct u132_udev *udev = &u132->udev[address];
		endp->udev_number = address;
		endp->usb_addr = usb_addr;
		endp->usb_endp = usb_endp;
		endp->input = 1;
		endp->output = 1;
		endp->pipetype = usb_pipetype(urb->pipe);
		u132_udev_get_kref(u132, udev);
		udev->enumeration = 2;
		udev->endp_number_in[usb_endp] = endp_number;
		udev->endp_number_out[usb_endp] = endp_number;
		urb->hcpriv = u132;
		endp->queue_size = 1;
		endp->queue_last = 0;
		endp->queue_next = 0;
		endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] = urb;
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		u132_endp_queue_work(u132, endp, 0);
		return 0;
	}
}

static int queue_control_on_old_endpoint(struct u132 *u132,
	struct urb *urb,
	struct usb_device *usb_dev, struct u132_endp *endp, u8 usb_addr,
	u8 usb_endp)
{
	if (usb_addr == 0) {
		if (usb_pipein(urb->pipe)) {
			urb->hcpriv = u132;
			if (endp->queue_size++ < ENDP_QUEUE_SIZE) {
				endp->urb_list[ENDP_QUEUE_MASK &
					endp->queue_last++] = urb;
			} else {
				struct u132_urbq *urbq =
					kmalloc(sizeof(struct u132_urbq),
					GFP_ATOMIC);
				if (urbq == NULL) {
					endp->queue_size -= 1;
					return -ENOMEM;
				} else {
					list_add_tail(&urbq->urb_more,
						&endp->urb_more);
					urbq->urb = urb;
				}
			}
			return 0;
		} else {	/* usb_pipeout(urb->pipe) */
			struct u132_addr *addr = &u132->addr[usb_dev->devnum];
			int I = MAX_U132_UDEVS;
			int i = 0;
			while (--I > 0) {
				struct u132_udev *udev = &u132->udev[++i];
				if (udev->usb_device) {
					continue;
				} else {
					udev->enumeration = 1;
					u132->addr[0].address = i;
					endp->udev_number = i;
					udev->udev_number = i;
					udev->usb_addr = usb_dev->devnum;
					u132_udev_init_kref(u132, udev);
					udev->endp_number_in[usb_endp] =
						endp->endp_number;
					u132_udev_get_kref(u132, udev);
					udev->endp_number_out[usb_endp] =
						endp->endp_number;
					udev->usb_device = usb_dev;
					((u8 *) (urb->setup_packet))[2] =
						addr->address = i;
					u132_udev_get_kref(u132, udev);
					break;
				}
			}
			if (I == 0) {
				dev_err(&u132->platform_dev->dev, "run out of d"
					"evice space\n");
				return -EINVAL;
			}
			urb->hcpriv = u132;
			if (endp->queue_size++ < ENDP_QUEUE_SIZE) {
				endp->urb_list[ENDP_QUEUE_MASK &
					endp->queue_last++] = urb;
			} else {
				struct u132_urbq *urbq =
					kmalloc(sizeof(struct u132_urbq),
					GFP_ATOMIC);
				if (urbq == NULL) {
					endp->queue_size -= 1;
					return -ENOMEM;
				} else {
					list_add_tail(&urbq->urb_more,
						&endp->urb_more);
					urbq->urb = urb;
				}
			}
			return 0;
		}
	} else {		/*(usb_addr > 0) */
		u8 address = u132->addr[usb_addr].address;
		struct u132_udev *udev = &u132->udev[address];
		urb->hcpriv = u132;
		if (udev->enumeration != 2)
			udev->enumeration = 2;
		if (endp->queue_size++ < ENDP_QUEUE_SIZE) {
			endp->urb_list[ENDP_QUEUE_MASK & endp->queue_last++] =
				urb;
		} else {
			struct u132_urbq *urbq =
				kmalloc(sizeof(struct u132_urbq), GFP_ATOMIC);
			if (urbq == NULL) {
				endp->queue_size -= 1;
				return -ENOMEM;
			} else {
				list_add_tail(&urbq->urb_more, &endp->urb_more);
				urbq->urb = urb;
			}
		}
		return 0;
	}
}

static int u132_urb_enqueue(struct usb_hcd *hcd, struct urb *urb,
		gfp_t mem_flags)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (irqs_disabled()) {
		if (gfpflags_allow_blocking(mem_flags)) {
			printk(KERN_ERR "invalid context for function that might sleep\n");
			return -EINVAL;
		}
	}
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed "
				"urb=%p\n", urb);
		return -ESHUTDOWN;
	} else {
		u8 usb_addr = usb_pipedevice(urb->pipe);
		u8 usb_endp = usb_pipeendpoint(urb->pipe);
		struct usb_device *usb_dev = urb->dev;
		if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
			u8 address = u132->addr[usb_addr].address;
			struct u132_udev *udev = &u132->udev[address];
			struct u132_endp *endp = urb->ep->hcpriv;
			urb->actual_length = 0;
			if (endp) {
				unsigned long irqs;
				int retval;
				spin_lock_irqsave(&endp->queue_lock.slock,
					irqs);
				retval = usb_hcd_link_urb_to_ep(hcd, urb);
				if (retval == 0) {
					retval = queue_int_on_old_endpoint(
							u132, udev, urb,
							usb_dev, endp,
							usb_addr, usb_endp,
							address);
					if (retval)
						usb_hcd_unlink_urb_from_ep(
	hcd, urb);
				}
				spin_unlock_irqrestore(&endp->queue_lock.slock,
					irqs);
				if (retval) {
					return retval;
				} else {
					u132_endp_queue_work(u132, endp,
						msecs_to_jiffies(urb->interval))
						;
					return 0;
				}
			} else if (u132->num_endpoints == MAX_U132_ENDPS) {
				return -EINVAL;
			} else {	/*(endp == NULL) */
				return create_endpoint_and_queue_int(u132, udev,
						urb, usb_dev, usb_addr,
						usb_endp, address, mem_flags);
			}
		} else if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
			dev_err(&u132->platform_dev->dev, "the hardware does no"
				"t support PIPE_ISOCHRONOUS\n");
			return -EINVAL;
		} else if (usb_pipetype(urb->pipe) == PIPE_BULK) {
			u8 address = u132->addr[usb_addr].address;
			struct u132_udev *udev = &u132->udev[address];
			struct u132_endp *endp = urb->ep->hcpriv;
			urb->actual_length = 0;
			if (endp) {
				unsigned long irqs;
				int retval;
				spin_lock_irqsave(&endp->queue_lock.slock,
					irqs);
				retval = usb_hcd_link_urb_to_ep(hcd, urb);
				if (retval == 0) {
					retval = queue_bulk_on_old_endpoint(
							u132, udev, urb,
							usb_dev, endp,
							usb_addr, usb_endp,
							address);
					if (retval)
						usb_hcd_unlink_urb_from_ep(
	hcd, urb);
				}
				spin_unlock_irqrestore(&endp->queue_lock.slock,
					irqs);
				if (retval) {
					return retval;
				} else {
					u132_endp_queue_work(u132, endp, 0);
					return 0;
				}
			} else if (u132->num_endpoints == MAX_U132_ENDPS) {
				return -EINVAL;
			} else
				return create_endpoint_and_queue_bulk(u132,
					udev, urb, usb_dev, usb_addr,
					usb_endp, address, mem_flags);
		} else {
			struct u132_endp *endp = urb->ep->hcpriv;
			u16 urb_size = 8;
			u8 *b = urb->setup_packet;
			int i = 0;
			char data[30 * 3 + 4];
			char *d = data;
			int m = (sizeof(data) - 1) / 3;
			int l = 0;
			data[0] = 0;
			while (urb_size-- > 0) {
				if (i > m) {
				} else if (i++ < m) {
					int w = sprintf(d, " %02X", *b++);
					d += w;
					l += w;
				} else
					d += sprintf(d, " ..");
			}
			if (endp) {
				unsigned long irqs;
				int retval;
				spin_lock_irqsave(&endp->queue_lock.slock,
					irqs);
				retval = usb_hcd_link_urb_to_ep(hcd, urb);
				if (retval == 0) {
					retval = queue_control_on_old_endpoint(
							u132, urb, usb_dev,
							endp, usb_addr,
							usb_endp);
					if (retval)
						usb_hcd_unlink_urb_from_ep(
								hcd, urb);
				}
				spin_unlock_irqrestore(&endp->queue_lock.slock,
					irqs);
				if (retval) {
					return retval;
				} else {
					u132_endp_queue_work(u132, endp, 0);
					return 0;
				}
			} else if (u132->num_endpoints == MAX_U132_ENDPS) {
				return -EINVAL;
			} else
				return create_endpoint_and_queue_control(u132,
					urb, usb_dev, usb_addr, usb_endp,
					mem_flags);
		}
	}
}

static int dequeue_from_overflow_chain(struct u132 *u132,
	struct u132_endp *endp, struct urb *urb)
{
	struct u132_urbq *urbq;

	list_for_each_entry(urbq, &endp->urb_more, urb_more) {
		if (urbq->urb == urb) {
			struct usb_hcd *hcd = u132_to_hcd(u132);
			list_del(&urbq->urb_more);
			endp->queue_size -= 1;
			urb->error_count = 0;
			usb_hcd_giveback_urb(hcd, urb, 0);
			return 0;
		} else
			continue;
	}
	dev_err(&u132->platform_dev->dev, "urb=%p not found in endp[%d]=%p ring"
		"[%d] %c%c usb_endp=%d usb_addr=%d size=%d next=%04X last=%04X"
		"\n", urb, endp->endp_number, endp, endp->ring->number,
		endp->input ? 'I' : ' ', endp->output ? 'O' : ' ',
		endp->usb_endp, endp->usb_addr, endp->queue_size,
		endp->queue_next, endp->queue_last);
	return -EINVAL;
}

static int u132_endp_urb_dequeue(struct u132 *u132, struct u132_endp *endp,
		struct urb *urb, int status)
{
	unsigned long irqs;
	int rc;

	spin_lock_irqsave(&endp->queue_lock.slock, irqs);
	rc = usb_hcd_check_unlink_urb(u132_to_hcd(u132), urb, status);
	if (rc) {
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		return rc;
	}
	if (endp->queue_size == 0) {
		dev_err(&u132->platform_dev->dev, "urb=%p not found in endp[%d]"
			"=%p ring[%d] %c%c usb_endp=%d usb_addr=%d\n", urb,
			endp->endp_number, endp, endp->ring->number,
			endp->input ? 'I' : ' ', endp->output ? 'O' : ' ',
			endp->usb_endp, endp->usb_addr);
		spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
		return -EINVAL;
	}
	if (urb == endp->urb_list[ENDP_QUEUE_MASK & endp->queue_next]) {
		if (endp->active) {
			endp->dequeueing = 1;
			endp->edset_flush = 1;
			u132_endp_queue_work(u132, endp, 0);
			spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
			return 0;
		} else {
			spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
			u132_hcd_abandon_urb(u132, endp, urb, status);
			return 0;
		}
	} else {
		u16 queue_list = 0;
		u16 queue_size = endp->queue_size;
		u16 queue_scan = endp->queue_next;
		struct urb **urb_slot = NULL;
		while (++queue_list < ENDP_QUEUE_SIZE && --queue_size > 0) {
			if (urb == endp->urb_list[ENDP_QUEUE_MASK &
				++queue_scan]) {
				urb_slot = &endp->urb_list[ENDP_QUEUE_MASK &
					queue_scan];
				break;
			} else
				continue;
		}
		while (++queue_list < ENDP_QUEUE_SIZE && --queue_size > 0) {
			*urb_slot = endp->urb_list[ENDP_QUEUE_MASK &
				++queue_scan];
			urb_slot = &endp->urb_list[ENDP_QUEUE_MASK &
				queue_scan];
		}
		if (urb_slot) {
			struct usb_hcd *hcd = u132_to_hcd(u132);

			usb_hcd_unlink_urb_from_ep(hcd, urb);
			endp->queue_size -= 1;
			if (list_empty(&endp->urb_more)) {
				spin_unlock_irqrestore(&endp->queue_lock.slock,
					irqs);
			} else {
				struct list_head *next = endp->urb_more.next;
				struct u132_urbq *urbq = list_entry(next,
					struct u132_urbq, urb_more);
				list_del(next);
				*urb_slot = urbq->urb;
				spin_unlock_irqrestore(&endp->queue_lock.slock,
					irqs);
				kfree(urbq);
			} urb->error_count = 0;
			usb_hcd_giveback_urb(hcd, urb, status);
			return 0;
		} else if (list_empty(&endp->urb_more)) {
			dev_err(&u132->platform_dev->dev, "urb=%p not found in "
				"endp[%d]=%p ring[%d] %c%c usb_endp=%d usb_addr"
				"=%d size=%d next=%04X last=%04X\n", urb,
				endp->endp_number, endp, endp->ring->number,
				endp->input ? 'I' : ' ',
				endp->output ? 'O' : ' ', endp->usb_endp,
				endp->usb_addr, endp->queue_size,
				endp->queue_next, endp->queue_last);
			spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
			return -EINVAL;
		} else {
			int retval;

			usb_hcd_unlink_urb_from_ep(u132_to_hcd(u132), urb);
			retval = dequeue_from_overflow_chain(u132, endp,
				urb);
			spin_unlock_irqrestore(&endp->queue_lock.slock, irqs);
			return retval;
		}
	}
}

static int u132_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 2) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else {
		u8 usb_addr = usb_pipedevice(urb->pipe);
		u8 usb_endp = usb_pipeendpoint(urb->pipe);
		u8 address = u132->addr[usb_addr].address;
		struct u132_udev *udev = &u132->udev[address];
		if (usb_pipein(urb->pipe)) {
			u8 endp_number = udev->endp_number_in[usb_endp];
			struct u132_endp *endp = u132->endp[endp_number - 1];
			return u132_endp_urb_dequeue(u132, endp, urb, status);
		} else {
			u8 endp_number = udev->endp_number_out[usb_endp];
			struct u132_endp *endp = u132->endp[endp_number - 1];
			return u132_endp_urb_dequeue(u132, endp, urb, status);
		}
	}
}

static void u132_endpoint_disable(struct usb_hcd *hcd,
	struct usb_host_endpoint *hep)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 2) {
		dev_err(&u132->platform_dev->dev, "u132 device %p(hcd=%p hep=%p"
			") has been removed %d\n", u132, hcd, hep,
			u132->going);
	} else {
		struct u132_endp *endp = hep->hcpriv;
		if (endp)
			u132_endp_put_kref(u132, endp);
	}
}

static int u132_get_frame(struct usb_hcd *hcd)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else {
		int frame = 0;
		dev_err(&u132->platform_dev->dev, "TODO: u132_get_frame\n");
		mdelay(100);
		return frame;
	}
}

static int u132_roothub_descriptor(struct u132 *u132,
	struct usb_hub_descriptor *desc)
{
	int retval;
	u16 temp;
	u32 rh_a = -1;
	u32 rh_b = -1;
	retval = u132_read_pcimem(u132, roothub.a, &rh_a);
	if (retval)
		return retval;
	desc->bDescriptorType = USB_DT_HUB;
	desc->bPwrOn2PwrGood = (rh_a & RH_A_POTPGT) >> 24;
	desc->bHubContrCurrent = 0;
	desc->bNbrPorts = u132->num_ports;
	temp = 1 + (u132->num_ports / 8);
	desc->bDescLength = 7 + 2 * temp;
	temp = HUB_CHAR_COMMON_LPSM | HUB_CHAR_COMMON_OCPM;
	if (rh_a & RH_A_NPS)
		temp |= HUB_CHAR_NO_LPSM;
	if (rh_a & RH_A_PSM)
		temp |= HUB_CHAR_INDV_PORT_LPSM;
	if (rh_a & RH_A_NOCP)
		temp |= HUB_CHAR_NO_OCPM;
	else if (rh_a & RH_A_OCPM)
		temp |= HUB_CHAR_INDV_PORT_OCPM;
	desc->wHubCharacteristics = cpu_to_le16(temp);
	retval = u132_read_pcimem(u132, roothub.b, &rh_b);
	if (retval)
		return retval;
	memset(desc->u.hs.DeviceRemovable, 0xff,
			sizeof(desc->u.hs.DeviceRemovable));
	desc->u.hs.DeviceRemovable[0] = rh_b & RH_B_DR;
	if (u132->num_ports > 7) {
		desc->u.hs.DeviceRemovable[1] = (rh_b & RH_B_DR) >> 8;
		desc->u.hs.DeviceRemovable[2] = 0xff;
	} else
		desc->u.hs.DeviceRemovable[1] = 0xff;
	return 0;
}

static int u132_roothub_status(struct u132 *u132, __le32 *desc)
{
	u32 rh_status = -1;
	int ret_status = u132_read_pcimem(u132, roothub.status, &rh_status);
	*desc = cpu_to_le32(rh_status);
	return ret_status;
}

static int u132_roothub_portstatus(struct u132 *u132, __le32 *desc, u16 wIndex)
{
	if (wIndex == 0 || wIndex > u132->num_ports) {
		return -EINVAL;
	} else {
		int port = wIndex - 1;
		u32 rh_portstatus = -1;
		int ret_portstatus = u132_read_pcimem(u132,
			roothub.portstatus[port], &rh_portstatus);
		*desc = cpu_to_le32(rh_portstatus);
		if (*(u16 *) (desc + 2)) {
			dev_info(&u132->platform_dev->dev, "Port %d Status Chan"
				"ge = %08X\n", port, *desc);
		}
		return ret_portstatus;
	}
}


/* this timer value might be vendor-specific ... */
#define PORT_RESET_HW_MSEC 10
#define PORT_RESET_MSEC 10
/* wrap-aware logic morphed from <linux/jiffies.h> */
#define tick_before(t1, t2) ((s16)(((s16)(t1))-((s16)(t2))) < 0)
static int u132_roothub_portreset(struct u132 *u132, int port_index)
{
	int retval;
	u32 fmnumber;
	u16 now;
	u16 reset_done;
	retval = u132_read_pcimem(u132, fmnumber, &fmnumber);
	if (retval)
		return retval;
	now = fmnumber;
	reset_done = now + PORT_RESET_MSEC;
	do {
		u32 portstat;
		do {
			retval = u132_read_pcimem(u132,
				roothub.portstatus[port_index], &portstat);
			if (retval)
				return retval;
			if (RH_PS_PRS & portstat)
				continue;
			else
				break;
		} while (tick_before(now, reset_done));
		if (RH_PS_PRS & portstat)
			return -ENODEV;
		if (RH_PS_CCS & portstat) {
			if (RH_PS_PRSC & portstat) {
				retval = u132_write_pcimem(u132,
					roothub.portstatus[port_index],
					RH_PS_PRSC);
				if (retval)
					return retval;
			}
		} else
			break;	/* start the next reset,
				sleep till it's probably done */
		retval = u132_write_pcimem(u132, roothub.portstatus[port_index],
			 RH_PS_PRS);
		if (retval)
			return retval;
		msleep(PORT_RESET_HW_MSEC);
		retval = u132_read_pcimem(u132, fmnumber, &fmnumber);
		if (retval)
			return retval;
		now = fmnumber;
	} while (tick_before(now, reset_done));
	return 0;
}

static int u132_roothub_setportfeature(struct u132 *u132, u16 wValue,
	u16 wIndex)
{
	if (wIndex == 0 || wIndex > u132->num_ports) {
		return -EINVAL;
	} else {
		int port_index = wIndex - 1;
		struct u132_port *port = &u132->port[port_index];
		port->Status &= ~(1 << wValue);
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			return u132_write_pcimem(u132,
			       roothub.portstatus[port_index], RH_PS_PSS);
		case USB_PORT_FEAT_POWER:
			return u132_write_pcimem(u132,
			       roothub.portstatus[port_index], RH_PS_PPS);
		case USB_PORT_FEAT_RESET:
			return u132_roothub_portreset(u132, port_index);
		default:
			return -EPIPE;
		}
	}
}

static int u132_roothub_clearportfeature(struct u132 *u132, u16 wValue,
	u16 wIndex)
{
	if (wIndex == 0 || wIndex > u132->num_ports) {
		return -EINVAL;
	} else {
		int port_index = wIndex - 1;
		u32 temp;
		struct u132_port *port = &u132->port[port_index];
		port->Status &= ~(1 << wValue);
		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			temp = RH_PS_CCS;
			break;
		case USB_PORT_FEAT_C_ENABLE:
			temp = RH_PS_PESC;
			break;
		case USB_PORT_FEAT_SUSPEND:
			temp = RH_PS_POCI;
			if ((u132->hc_control & OHCI_CTRL_HCFS)
				!= OHCI_USB_OPER) {
				dev_err(&u132->platform_dev->dev, "TODO resume_"
					"root_hub\n");
			}
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			temp = RH_PS_PSSC;
			break;
		case USB_PORT_FEAT_POWER:
			temp = RH_PS_LSDA;
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			temp = RH_PS_CSC;
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			temp = RH_PS_OCIC;
			break;
		case USB_PORT_FEAT_C_RESET:
			temp = RH_PS_PRSC;
			break;
		default:
			return -EPIPE;
		}
		return u132_write_pcimem(u132, roothub.portstatus[port_index],
		       temp);
	}
}


/* the virtual root hub timer IRQ checks for hub status*/
static int u132_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device hcd=%p has been remov"
			"ed %d\n", hcd, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device hcd=%p is being remov"
			"ed\n", hcd);
		return -ESHUTDOWN;
	} else {
		int i, changed = 0, length = 1;
		if (u132->flags & OHCI_QUIRK_AMD756) {
			if ((u132->hc_roothub_a & RH_A_NDP) > MAX_ROOT_PORTS) {
				dev_err(&u132->platform_dev->dev, "bogus NDP, r"
					"ereads as NDP=%d\n",
					u132->hc_roothub_a & RH_A_NDP);
				goto done;
			}
		}
		if (u132->hc_roothub_status & (RH_HS_LPSC | RH_HS_OCIC))
			buf[0] = changed = 1;
		else
			buf[0] = 0;
		if (u132->num_ports > 7) {
			buf[1] = 0;
			length++;
		}
		for (i = 0; i < u132->num_ports; i++) {
			if (u132->hc_roothub_portstatus[i] & (RH_PS_CSC |
				RH_PS_PESC | RH_PS_PSSC | RH_PS_OCIC |
				RH_PS_PRSC)) {
				changed = 1;
				if (i < 7)
					buf[0] |= 1 << (i + 1);
				else
					buf[1] |= 1 << (i - 7);
				continue;
			}
			if (!(u132->hc_roothub_portstatus[i] & RH_PS_CCS))
				continue;

			if ((u132->hc_roothub_portstatus[i] & RH_PS_PSS))
				continue;
		}
done:
		return changed ? length : 0;
	}
}

static int u132_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
	u16 wIndex, char *buf, u16 wLength)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else {
		int retval = 0;
		mutex_lock(&u132->sw_lock);
		switch (typeReq) {
		case ClearHubFeature:
			switch (wValue) {
			case C_HUB_OVER_CURRENT:
			case C_HUB_LOCAL_POWER:
				break;
			default:
				goto stall;
			}
			break;
		case SetHubFeature:
			switch (wValue) {
			case C_HUB_OVER_CURRENT:
			case C_HUB_LOCAL_POWER:
				break;
			default:
				goto stall;
			}
			break;
		case ClearPortFeature:{
				retval = u132_roothub_clearportfeature(u132,
					wValue, wIndex);
				if (retval)
					goto error;
				break;
			}
		case GetHubDescriptor:{
				retval = u132_roothub_descriptor(u132,
					(struct usb_hub_descriptor *)buf);
				if (retval)
					goto error;
				break;
			}
		case GetHubStatus:{
				retval = u132_roothub_status(u132,
					(__le32 *) buf);
				if (retval)
					goto error;
				break;
			}
		case GetPortStatus:{
				retval = u132_roothub_portstatus(u132,
					(__le32 *) buf, wIndex);
				if (retval)
					goto error;
				break;
			}
		case SetPortFeature:{
				retval = u132_roothub_setportfeature(u132,
					wValue, wIndex);
				if (retval)
					goto error;
				break;
			}
		default:
			goto stall;
		error:
			u132_disable(u132);
			u132->going = 1;
			break;
		stall:
			retval = -EPIPE;
			break;
		}
		mutex_unlock(&u132->sw_lock);
		return retval;
	}
}

static int u132_start_port_reset(struct usb_hcd *hcd, unsigned port_num)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else
		return 0;
}


#ifdef CONFIG_PM
static int u132_bus_suspend(struct usb_hcd *hcd)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else
		return 0;
}

static int u132_bus_resume(struct usb_hcd *hcd)
{
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else
		return 0;
}

#else
#define u132_bus_suspend NULL
#define u132_bus_resume NULL
#endif
static const struct hc_driver u132_hc_driver = {
	.description = hcd_name,
	.hcd_priv_size = sizeof(struct u132),
	.irq = NULL,
	.flags = HCD_USB11 | HCD_MEMORY,
	.reset = u132_hcd_reset,
	.start = u132_hcd_start,
	.stop = u132_hcd_stop,
	.urb_enqueue = u132_urb_enqueue,
	.urb_dequeue = u132_urb_dequeue,
	.endpoint_disable = u132_endpoint_disable,
	.get_frame_number = u132_get_frame,
	.hub_status_data = u132_hub_status_data,
	.hub_control = u132_hub_control,
	.bus_suspend = u132_bus_suspend,
	.bus_resume = u132_bus_resume,
	.start_port_reset = u132_start_port_reset,
};

/*
* This function may be called by the USB core whilst the "usb_all_devices_rwsem"
* is held for writing, thus this module must not call usb_remove_hcd()
* synchronously - but instead should immediately stop activity to the
* device and asynchronously call usb_remove_hcd()
*/
static int u132_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	if (hcd) {
		struct u132 *u132 = hcd_to_u132(hcd);
		if (u132->going++ > 1) {
			dev_err(&u132->platform_dev->dev, "already being remove"
				"d\n");
			return -ENODEV;
		} else {
			int rings = MAX_U132_RINGS;
			int endps = MAX_U132_ENDPS;
			dev_err(&u132->platform_dev->dev, "removing device u132"
				".%d\n", u132->sequence_num);
			msleep(100);
			mutex_lock(&u132->sw_lock);
			u132_monitor_cancel_work(u132);
			while (rings-- > 0) {
				struct u132_ring *ring = &u132->ring[rings];
				u132_ring_cancel_work(u132, ring);
			} while (endps-- > 0) {
				struct u132_endp *endp = u132->endp[endps];
				if (endp)
					u132_endp_cancel_work(u132, endp);
			}
			u132->going += 1;
			printk(KERN_INFO "removing device u132.%d\n",
				u132->sequence_num);
			mutex_unlock(&u132->sw_lock);
			usb_remove_hcd(hcd);
			u132_u132_put_kref(u132);
			return 0;
		}
	} else
		return 0;
}

static void u132_initialise(struct u132 *u132, struct platform_device *pdev)
{
	int rings = MAX_U132_RINGS;
	int ports = MAX_U132_PORTS;
	int addrs = MAX_U132_ADDRS;
	int udevs = MAX_U132_UDEVS;
	int endps = MAX_U132_ENDPS;
	u132->board = dev_get_platdata(&pdev->dev);
	u132->platform_dev = pdev;
	u132->power = 0;
	u132->reset = 0;
	mutex_init(&u132->sw_lock);
	mutex_init(&u132->scheduler_lock);
	while (rings-- > 0) {
		struct u132_ring *ring = &u132->ring[rings];
		ring->u132 = u132;
		ring->number = rings + 1;
		ring->length = 0;
		ring->curr_endp = NULL;
		INIT_DELAYED_WORK(&ring->scheduler,
				  u132_hcd_ring_work_scheduler);
	}
	mutex_lock(&u132->sw_lock);
	INIT_DELAYED_WORK(&u132->monitor, u132_hcd_monitor_work);
	while (ports-- > 0) {
		struct u132_port *port = &u132->port[ports];
		port->u132 = u132;
		port->reset = 0;
		port->enable = 0;
		port->power = 0;
		port->Status = 0;
	}
	while (addrs-- > 0) {
		struct u132_addr *addr = &u132->addr[addrs];
		addr->address = 0;
	}
	while (udevs-- > 0) {
		struct u132_udev *udev = &u132->udev[udevs];
		int i = ARRAY_SIZE(udev->endp_number_in);
		int o = ARRAY_SIZE(udev->endp_number_out);
		udev->usb_device = NULL;
		udev->udev_number = 0;
		udev->usb_addr = 0;
		udev->portnumber = 0;
		while (i-- > 0)
			udev->endp_number_in[i] = 0;

		while (o-- > 0)
			udev->endp_number_out[o] = 0;

	}
	while (endps-- > 0)
		u132->endp[endps] = NULL;

	mutex_unlock(&u132->sw_lock);
}

static int u132_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	int retval;
	u32 control;
	u32 rh_a = -1;

	msleep(100);
	if (u132_exiting > 0)
		return -ENODEV;

	retval = ftdi_write_pcimem(pdev, intrdisable, OHCI_INTR_MIE);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(pdev, control, &control);
	if (retval)
		return retval;
	retval = ftdi_read_pcimem(pdev, roothub.a, &rh_a);
	if (retval)
		return retval;
	if (pdev->dev.dma_mask)
		return -EINVAL;

	hcd = usb_create_hcd(&u132_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		printk(KERN_ERR "failed to create the usb hcd struct for U132\n"
			);
		ftdi_elan_gone_away(pdev);
		return -ENOMEM;
	} else {
		struct u132 *u132 = hcd_to_u132(hcd);
		retval = 0;
		hcd->rsrc_start = 0;
		mutex_lock(&u132_module_lock);
		list_add_tail(&u132->u132_list, &u132_static_list);
		u132->sequence_num = ++u132_instances;
		mutex_unlock(&u132_module_lock);
		u132_u132_init_kref(u132);
		u132_initialise(u132, pdev);
		hcd->product_desc = "ELAN U132 Host Controller";
		retval = usb_add_hcd(hcd, 0, 0);
		if (retval != 0) {
			dev_err(&u132->platform_dev->dev, "init error %d\n",
				retval);
			u132_u132_put_kref(u132);
			return retval;
		} else {
			device_wakeup_enable(hcd->self.controller);
			u132_monitor_queue_work(u132, 100);
			return 0;
		}
	}
}


#ifdef CONFIG_PM
/*
 * for this device there's no useful distinction between the controller
 * and its root hub.
 */
static int u132_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else {
		int retval = 0, ports;

		switch (state.event) {
		case PM_EVENT_FREEZE:
			retval = u132_bus_suspend(hcd);
			break;
		case PM_EVENT_SUSPEND:
		case PM_EVENT_HIBERNATE:
			ports = MAX_U132_PORTS;
			while (ports-- > 0) {
				port_power(u132, ports, 0);
			}
			break;
		}
		return retval;
	}
}

static int u132_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct u132 *u132 = hcd_to_u132(hcd);
	if (u132->going > 1) {
		dev_err(&u132->platform_dev->dev, "device has been removed %d\n"
			, u132->going);
		return -ENODEV;
	} else if (u132->going > 0) {
		dev_err(&u132->platform_dev->dev, "device is being removed\n");
		return -ESHUTDOWN;
	} else {
		int retval = 0;
		if (!u132->port[0].power) {
			int ports = MAX_U132_PORTS;
			while (ports-- > 0) {
				port_power(u132, ports, 1);
			}
			retval = 0;
		} else {
			retval = u132_bus_resume(hcd);
		}
		return retval;
	}
}

#else
#define u132_suspend NULL
#define u132_resume NULL
#endif
/*
* this driver is loaded explicitly by ftdi_u132
*
* the platform_driver struct is static because it is per type of module
*/
static struct platform_driver u132_platform_driver = {
	.probe = u132_probe,
	.remove = u132_remove,
	.suspend = u132_suspend,
	.resume = u132_resume,
	.driver = {
		   .name = hcd_name,
		   },
};
static int __init u132_hcd_init(void)
{
	int retval;
	INIT_LIST_HEAD(&u132_static_list);
	u132_instances = 0;
	u132_exiting = 0;
	mutex_init(&u132_module_lock);
	if (usb_disabled())
		return -ENODEV;
	printk(KERN_INFO "driver %s\n", hcd_name);
	workqueue = create_singlethread_workqueue("u132");
	retval = platform_driver_register(&u132_platform_driver);
	return retval;
}


module_init(u132_hcd_init);
static void __exit u132_hcd_exit(void)
{
	struct u132 *u132;
	struct u132 *temp;
	mutex_lock(&u132_module_lock);
	u132_exiting += 1;
	mutex_unlock(&u132_module_lock);
	list_for_each_entry_safe(u132, temp, &u132_static_list, u132_list) {
		platform_device_unregister(u132->platform_dev);
	}
	platform_driver_unregister(&u132_platform_driver);
	printk(KERN_INFO "u132-hcd driver deregistered\n");
	wait_event(u132_hcd_wait, u132_instances == 0);
	flush_workqueue(workqueue);
	destroy_workqueue(workqueue);
}


module_exit(u132_hcd_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:u132_hcd");
