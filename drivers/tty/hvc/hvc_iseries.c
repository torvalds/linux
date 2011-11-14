/*
 * iSeries vio driver interface to hvc_console.c
 *
 * This code is based heavily on hvc_vio.c and viocons.c
 *
 * Copyright (C) 2006 Stephen Rothwell, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <stdarg.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/console.h>

#include <asm/hvconsole.h>
#include <asm/vio.h>
#include <asm/prom.h>
#include <asm/firmware.h>
#include <asm/iseries/vio.h>
#include <asm/iseries/hv_call.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_lp_event.h>

#include "hvc_console.h"

#define VTTY_PORTS 10

static DEFINE_SPINLOCK(consolelock);
static DEFINE_SPINLOCK(consoleloglock);

static const char hvc_driver_name[] = "hvc_console";

#define IN_BUF_SIZE	200

/*
 * Our port information.
 */
static struct port_info {
	HvLpIndex lp;
	u64 seq;	/* sequence number of last HV send */
	u64 ack;	/* last ack from HV */
	struct hvc_struct *hp;
	int in_start;
	int in_end;
	unsigned char in_buf[IN_BUF_SIZE];
} port_info[VTTY_PORTS] = {
	[ 0 ... VTTY_PORTS - 1 ] = {
		.lp = HvLpIndexInvalid
	}
};

#define viochar_is_console(pi)	((pi) == &port_info[0])

static struct vio_device_id hvc_driver_table[] __devinitdata = {
	{"serial", "IBM,iSeries-vty"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, hvc_driver_table);

static void hvlog(char *fmt, ...)
{
	int i;
	unsigned long flags;
	va_list args;
	static char buf[256];

	spin_lock_irqsave(&consoleloglock, flags);
	va_start(args, fmt);
	i = vscnprintf(buf, sizeof(buf) - 1, fmt, args);
	va_end(args);
	buf[i++] = '\r';
	HvCall_writeLogBuffer(buf, i);
	spin_unlock_irqrestore(&consoleloglock, flags);
}

/*
 * Initialize the common fields in a charLpEvent
 */
static void init_data_event(struct viocharlpevent *viochar, HvLpIndex lp)
{
	struct HvLpEvent *hev = &viochar->event;

	memset(viochar, 0, sizeof(struct viocharlpevent));

	hev->flags = HV_LP_EVENT_VALID | HV_LP_EVENT_DEFERRED_ACK |
		HV_LP_EVENT_INT;
	hev->xType = HvLpEvent_Type_VirtualIo;
	hev->xSubtype = viomajorsubtype_chario | viochardata;
	hev->xSourceLp = HvLpConfig_getLpIndex();
	hev->xTargetLp = lp;
	hev->xSizeMinus1 = sizeof(struct viocharlpevent);
	hev->xSourceInstanceId = viopath_sourceinst(lp);
	hev->xTargetInstanceId = viopath_targetinst(lp);
}

static int get_chars(uint32_t vtermno, char *buf, int count)
{
	struct port_info *pi;
	int n = 0;
	unsigned long flags;

	if (vtermno >= VTTY_PORTS)
		return -EINVAL;
	if (count == 0)
		return 0;

	pi = &port_info[vtermno];
	spin_lock_irqsave(&consolelock, flags);

	if (pi->in_end == 0)
		goto done;

	n = pi->in_end - pi->in_start;
	if (n > count)
		n = count;
	memcpy(buf, &pi->in_buf[pi->in_start], n);
	pi->in_start += n;
	if (pi->in_start == pi->in_end) {
		pi->in_start = 0;
		pi->in_end = 0;
	}
done:
	spin_unlock_irqrestore(&consolelock, flags);
	return n;
}

static int put_chars(uint32_t vtermno, const char *buf, int count)
{
	struct viocharlpevent *viochar;
	struct port_info *pi;
	HvLpEvent_Rc hvrc;
	unsigned long flags;
	int sent = 0;

	if (vtermno >= VTTY_PORTS)
		return -EINVAL;

	pi = &port_info[vtermno];

	spin_lock_irqsave(&consolelock, flags);

	if (viochar_is_console(pi) && !viopath_isactive(pi->lp)) {
		HvCall_writeLogBuffer(buf, count);
		sent = count;
		goto done;
	}

	viochar = vio_get_event_buffer(viomajorsubtype_chario);
	if (viochar == NULL) {
		hvlog("\n\rviocons: Can't get viochar buffer.");
		goto done;
	}

	while ((count > 0) && ((pi->seq - pi->ack) < VIOCHAR_WINDOW)) {
		int len;

		len = (count > VIOCHAR_MAX_DATA) ? VIOCHAR_MAX_DATA : count;

		if (viochar_is_console(pi))
			HvCall_writeLogBuffer(buf, len);

		init_data_event(viochar, pi->lp);

		viochar->len = len;
		viochar->event.xCorrelationToken = pi->seq++;
		viochar->event.xSizeMinus1 =
			offsetof(struct viocharlpevent, data) + len;

		memcpy(viochar->data, buf, len);

		hvrc = HvCallEvent_signalLpEvent(&viochar->event);
		if (hvrc)
			hvlog("\n\rerror sending event! return code %d\n\r",
				(int)hvrc);
		sent += len;
		count -= len;
		buf += len;
	}

	vio_free_event_buffer(viomajorsubtype_chario, viochar);
done:
	spin_unlock_irqrestore(&consolelock, flags);
	return sent;
}

static const struct hv_ops hvc_get_put_ops = {
	.get_chars = get_chars,
	.put_chars = put_chars,
	.notifier_add = notifier_add_irq,
	.notifier_del = notifier_del_irq,
	.notifier_hangup = notifier_hangup_irq,
};

static int __devinit hvc_vio_probe(struct vio_dev *vdev,
			const struct vio_device_id *id)
{
	struct hvc_struct *hp;
	struct port_info *pi;

	/* probed with invalid parameters. */
	if (!vdev || !id)
		return -EPERM;

	if (vdev->unit_address >= VTTY_PORTS)
		return -ENODEV;

	pi = &port_info[vdev->unit_address];

	hp = hvc_alloc(vdev->unit_address, vdev->irq, &hvc_get_put_ops,
			VIOCHAR_MAX_DATA);
	if (IS_ERR(hp))
		return PTR_ERR(hp);
	pi->hp = hp;
	dev_set_drvdata(&vdev->dev, pi);

	return 0;
}

static int __devexit hvc_vio_remove(struct vio_dev *vdev)
{
	struct port_info *pi = dev_get_drvdata(&vdev->dev);
	struct hvc_struct *hp = pi->hp;

	return hvc_remove(hp);
}

static struct vio_driver hvc_vio_driver = {
	.id_table	= hvc_driver_table,
	.probe		= hvc_vio_probe,
	.remove		= __devexit_p(hvc_vio_remove),
	.driver		= {
		.name	= hvc_driver_name,
		.owner	= THIS_MODULE,
	}
};

static void hvc_open_event(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	u8 port = cevent->virtual_device;
	struct port_info *pi;
	int reject = 0;

	if (hvlpevent_is_ack(event)) {
		if (port >= VTTY_PORTS)
			return;

		spin_lock_irqsave(&consolelock, flags);

		pi = &port_info[port];
		if (event->xRc == HvLpEvent_Rc_Good) {
			pi->seq = pi->ack = 0;
			/*
			 * This line allows connections from the primary
			 * partition but once one is connected from the
			 * primary partition nothing short of a reboot
			 * of linux will allow access from the hosting
			 * partition again without a required iSeries fix.
			 */
			pi->lp = event->xTargetLp;
		}

		spin_unlock_irqrestore(&consolelock, flags);
		if (event->xRc != HvLpEvent_Rc_Good)
			printk(KERN_WARNING
			       "hvc: handle_open_event: event->xRc == (%d).\n",
			       event->xRc);

		if (event->xCorrelationToken != 0) {
			atomic_t *aptr= (atomic_t *)event->xCorrelationToken;
			atomic_set(aptr, 1);
		} else
			printk(KERN_WARNING
			       "hvc: weird...got open ack without atomic\n");
		return;
	}

	/* This had better require an ack, otherwise complain */
	if (!hvlpevent_need_ack(event)) {
		printk(KERN_WARNING "hvc: viocharopen without ack bit!\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);

	/* Make sure this is a good virtual tty */
	if (port >= VTTY_PORTS) {
		event->xRc = HvLpEvent_Rc_SubtypeError;
		cevent->subtype_result_code = viorc_openRejected;
		/*
		 * Flag state here since we can't printk while holding
		 * the consolelock spinlock.
		 */
		reject = 1;
	} else {
		pi = &port_info[port];
		if ((pi->lp != HvLpIndexInvalid) &&
				(pi->lp != event->xSourceLp)) {
			/*
			 * If this is tty is already connected to a different
			 * partition, fail.
			 */
			event->xRc = HvLpEvent_Rc_SubtypeError;
			cevent->subtype_result_code = viorc_openRejected;
			reject = 2;
		} else {
			pi->lp = event->xSourceLp;
			event->xRc = HvLpEvent_Rc_Good;
			cevent->subtype_result_code = viorc_good;
			pi->seq = pi->ack = 0;
		}
	}

	spin_unlock_irqrestore(&consolelock, flags);

	if (reject == 1)
		printk(KERN_WARNING "hvc: open rejected: bad virtual tty.\n");
	else if (reject == 2)
		printk(KERN_WARNING "hvc: open rejected: console in exclusive "
				"use by another partition.\n");

	/* Return the acknowledgement */
	HvCallEvent_ackLpEvent(event);
}

/*
 * Handle a close charLpEvent.  This should ONLY be an Interrupt because the
 * virtual console should never actually issue a close event to the hypervisor
 * because the virtual console never goes away.  A close event coming from the
 * hypervisor simply means that there are no client consoles connected to the
 * virtual console.
 */
static void hvc_close_event(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	u8 port = cevent->virtual_device;

	if (!hvlpevent_is_int(event)) {
		printk(KERN_WARNING
			"hvc: got unexpected close acknowledgement\n");
		return;
	}

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING
			"hvc: close message from invalid virtual device.\n");
		return;
	}

	/* For closes, just mark the console partition invalid */
	spin_lock_irqsave(&consolelock, flags);

	if (port_info[port].lp == event->xSourceLp)
		port_info[port].lp = HvLpIndexInvalid;

	spin_unlock_irqrestore(&consolelock, flags);
}

static void hvc_data_event(struct HvLpEvent *event)
{
	unsigned long flags;
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	struct port_info *pi;
	int n;
	u8 port = cevent->virtual_device;

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING "hvc: data on invalid virtual device %d\n",
				port);
		return;
	}
	if (cevent->len == 0)
		return;

	/*
	 * Change 05/01/2003 - Ryan Arnold: If a partition other than
	 * the current exclusive partition tries to send us data
	 * events then just drop them on the floor because we don't
	 * want his stinking data.  He isn't authorized to receive
	 * data because he wasn't the first one to get the console,
	 * therefore he shouldn't be allowed to send data either.
	 * This will work without an iSeries fix.
	 */
	pi = &port_info[port];
	if (pi->lp != event->xSourceLp)
		return;

	spin_lock_irqsave(&consolelock, flags);

	n = IN_BUF_SIZE - pi->in_end;
	if (n > cevent->len)
		n = cevent->len;
	if (n > 0) {
		memcpy(&pi->in_buf[pi->in_end], cevent->data, n);
		pi->in_end += n;
	}
	spin_unlock_irqrestore(&consolelock, flags);
	if (n == 0)
		printk(KERN_WARNING "hvc: input buffer overflow\n");
}

static void hvc_ack_event(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;
	unsigned long flags;
	u8 port = cevent->virtual_device;

	if (port >= VTTY_PORTS) {
		printk(KERN_WARNING "hvc: data on invalid virtual device\n");
		return;
	}

	spin_lock_irqsave(&consolelock, flags);
	port_info[port].ack = event->xCorrelationToken;
	spin_unlock_irqrestore(&consolelock, flags);
}

static void hvc_config_event(struct HvLpEvent *event)
{
	struct viocharlpevent *cevent = (struct viocharlpevent *)event;

	if (cevent->data[0] == 0x01)
		printk(KERN_INFO "hvc: window resized to %d: %d: %d: %d\n",
		       cevent->data[1], cevent->data[2],
		       cevent->data[3], cevent->data[4]);
	else
		printk(KERN_WARNING "hvc: unknown config event\n");
}

static void hvc_handle_event(struct HvLpEvent *event)
{
	int charminor;

	if (event == NULL)
		return;

	charminor = event->xSubtype & VIOMINOR_SUBTYPE_MASK;
	switch (charminor) {
	case viocharopen:
		hvc_open_event(event);
		break;
	case viocharclose:
		hvc_close_event(event);
		break;
	case viochardata:
		hvc_data_event(event);
		break;
	case viocharack:
		hvc_ack_event(event);
		break;
	case viocharconfig:
		hvc_config_event(event);
		break;
	default:
		if (hvlpevent_is_int(event) && hvlpevent_need_ack(event)) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static int __init send_open(HvLpIndex remoteLp, void *sem)
{
	return HvCallEvent_signalLpEventFast(remoteLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_chario | viocharopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(remoteLp),
			viopath_targetinst(remoteLp),
			(u64)(unsigned long)sem, VIOVERSION << 16,
			0, 0, 0, 0);
}

static int __init hvc_vio_init(void)
{
	atomic_t wait_flag;
	int rc;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		return -EIO;

	/* +2 for fudge */
	rc = viopath_open(HvLpConfig_getPrimaryLpIndex(),
			viomajorsubtype_chario, VIOCHAR_WINDOW + 2);
	if (rc)
		printk(KERN_WARNING "hvc: error opening to primary %d\n", rc);

	if (viopath_hostLp == HvLpIndexInvalid)
		vio_set_hostlp();

	/*
	 * And if the primary is not the same as the hosting LP, open to the
	 * hosting lp
	 */
	if ((viopath_hostLp != HvLpIndexInvalid) &&
	    (viopath_hostLp != HvLpConfig_getPrimaryLpIndex())) {
		printk(KERN_INFO "hvc: open path to hosting (%d)\n",
				viopath_hostLp);
		rc = viopath_open(viopath_hostLp, viomajorsubtype_chario,
				VIOCHAR_WINDOW + 2);	/* +2 for fudge */
		if (rc)
			printk(KERN_WARNING
				"error opening to partition %d: %d\n",
				viopath_hostLp, rc);
	}

	if (vio_setHandler(viomajorsubtype_chario, hvc_handle_event) < 0)
		printk(KERN_WARNING
			"hvc: error seting handler for console events!\n");

	/*
	 * First, try to open the console to the hosting lp.
	 * Wait on a semaphore for the response.
	 */
	atomic_set(&wait_flag, 0);
	if ((viopath_isactive(viopath_hostLp)) &&
	    (send_open(viopath_hostLp, &wait_flag) == 0)) {
		printk(KERN_INFO "hvc: hosting partition %d\n", viopath_hostLp);
		while (atomic_read(&wait_flag) == 0)
			mb();
		atomic_set(&wait_flag, 0);
	}

	/*
	 * If we don't have an active console, try the primary
	 */
	if ((!viopath_isactive(port_info[0].lp)) &&
	    (viopath_isactive(HvLpConfig_getPrimaryLpIndex())) &&
	    (send_open(HvLpConfig_getPrimaryLpIndex(), &wait_flag) == 0)) {
		printk(KERN_INFO "hvc: opening console to primary partition\n");
		while (atomic_read(&wait_flag) == 0)
			mb();
	}

	/* Register as a vio device to receive callbacks */
	rc = vio_register_driver(&hvc_vio_driver);

	return rc;
}
module_init(hvc_vio_init); /* after drivers/char/hvc_console.c */

static void __exit hvc_vio_exit(void)
{
	vio_unregister_driver(&hvc_vio_driver);
}
module_exit(hvc_vio_exit);

/* the device tree order defines our numbering */
static int __init hvc_find_vtys(void)
{
	struct device_node *vty;
	int num_found = 0;

	for (vty = of_find_node_by_name(NULL, "vty"); vty != NULL;
			vty = of_find_node_by_name(vty, "vty")) {
		const uint32_t *vtermno;

		/* We have statically defined space for only a certain number
		 * of console adapters.
		 */
		if ((num_found >= MAX_NR_HVC_CONSOLES) ||
				(num_found >= VTTY_PORTS)) {
			of_node_put(vty);
			break;
		}

		vtermno = of_get_property(vty, "reg", NULL);
		if (!vtermno)
			continue;

		if (!of_device_is_compatible(vty, "IBM,iSeries-vty"))
			continue;

		if (num_found == 0)
			add_preferred_console("hvc", 0, NULL);
		hvc_instantiate(*vtermno, num_found, &hvc_get_put_ops);
		++num_found;
	}

	return num_found;
}
console_initcall(hvc_find_vtys);
