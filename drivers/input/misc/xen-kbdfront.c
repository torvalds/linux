/*
 * Xen para-virtual input device
 *
 * Copyright (C) 2005 Anthony Liguori <aliguori@us.ibm.com>
 * Copyright (C) 2006-2008 Red Hat, Inc., Markus Armbruster <armbru@redhat.com>
 *
 *  Based on linux/drivers/input/mouse/sermouse.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>

#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/grant_table.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/fbif.h>
#include <xen/interface/io/kbdif.h>
#include <xen/xenbus.h>
#include <xen/platform_pci.h>

struct xenkbd_info {
	struct input_dev *kbd;
	struct input_dev *ptr;
	struct xenkbd_page *page;
	int gref;
	int irq;
	struct xenbus_device *xbdev;
	char phys[32];
};

static int xenkbd_remove(struct xenbus_device *);
static int xenkbd_connect_backend(struct xenbus_device *, struct xenkbd_info *);
static void xenkbd_disconnect_backend(struct xenkbd_info *);

/*
 * Note: if you need to send out events, see xenfb_do_update() for how
 * to do that.
 */

static irqreturn_t input_handler(int rq, void *dev_id)
{
	struct xenkbd_info *info = dev_id;
	struct xenkbd_page *page = info->page;
	__u32 cons, prod;

	prod = page->in_prod;
	if (prod == page->in_cons)
		return IRQ_HANDLED;
	rmb();			/* ensure we see ring contents up to prod */
	for (cons = page->in_cons; cons != prod; cons++) {
		union xenkbd_in_event *event;
		struct input_dev *dev;
		event = &XENKBD_IN_RING_REF(page, cons);

		dev = info->ptr;
		switch (event->type) {
		case XENKBD_TYPE_MOTION:
			input_report_rel(dev, REL_X, event->motion.rel_x);
			input_report_rel(dev, REL_Y, event->motion.rel_y);
			if (event->motion.rel_z)
				input_report_rel(dev, REL_WHEEL,
						 -event->motion.rel_z);
			break;
		case XENKBD_TYPE_KEY:
			dev = NULL;
			if (test_bit(event->key.keycode, info->kbd->keybit))
				dev = info->kbd;
			if (test_bit(event->key.keycode, info->ptr->keybit))
				dev = info->ptr;
			if (dev)
				input_report_key(dev, event->key.keycode,
						 event->key.pressed);
			else
				pr_warning("unhandled keycode 0x%x\n",
					   event->key.keycode);
			break;
		case XENKBD_TYPE_POS:
			input_report_abs(dev, ABS_X, event->pos.abs_x);
			input_report_abs(dev, ABS_Y, event->pos.abs_y);
			if (event->pos.rel_z)
				input_report_rel(dev, REL_WHEEL,
						 -event->pos.rel_z);
			break;
		}
		if (dev)
			input_sync(dev);
	}
	mb();			/* ensure we got ring contents */
	page->in_cons = cons;
	notify_remote_via_irq(info->irq);

	return IRQ_HANDLED;
}

static int xenkbd_probe(struct xenbus_device *dev,
				  const struct xenbus_device_id *id)
{
	int ret, i, abs;
	struct xenkbd_info *info;
	struct input_dev *kbd, *ptr;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
		return -ENOMEM;
	}
	dev_set_drvdata(&dev->dev, info);
	info->xbdev = dev;
	info->irq = -1;
	info->gref = -1;
	snprintf(info->phys, sizeof(info->phys), "xenbus/%s", dev->nodename);

	info->page = (void *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!info->page)
		goto error_nomem;

	if (xenbus_scanf(XBT_NIL, dev->otherend, "feature-abs-pointer", "%d", &abs) < 0)
		abs = 0;
	if (abs)
		xenbus_printf(XBT_NIL, dev->nodename, "request-abs-pointer", "1");

	/* keyboard */
	kbd = input_allocate_device();
	if (!kbd)
		goto error_nomem;
	kbd->name = "Xen Virtual Keyboard";
	kbd->phys = info->phys;
	kbd->id.bustype = BUS_PCI;
	kbd->id.vendor = 0x5853;
	kbd->id.product = 0xffff;

	__set_bit(EV_KEY, kbd->evbit);
	for (i = KEY_ESC; i < KEY_UNKNOWN; i++)
		__set_bit(i, kbd->keybit);
	for (i = KEY_OK; i < KEY_MAX; i++)
		__set_bit(i, kbd->keybit);

	ret = input_register_device(kbd);
	if (ret) {
		input_free_device(kbd);
		xenbus_dev_fatal(dev, ret, "input_register_device(kbd)");
		goto error;
	}
	info->kbd = kbd;

	/* pointing device */
	ptr = input_allocate_device();
	if (!ptr)
		goto error_nomem;
	ptr->name = "Xen Virtual Pointer";
	ptr->phys = info->phys;
	ptr->id.bustype = BUS_PCI;
	ptr->id.vendor = 0x5853;
	ptr->id.product = 0xfffe;

	if (abs) {
		__set_bit(EV_ABS, ptr->evbit);
		input_set_abs_params(ptr, ABS_X, 0, XENFB_WIDTH, 0, 0);
		input_set_abs_params(ptr, ABS_Y, 0, XENFB_HEIGHT, 0, 0);
	} else {
		input_set_capability(ptr, EV_REL, REL_X);
		input_set_capability(ptr, EV_REL, REL_Y);
	}
	input_set_capability(ptr, EV_REL, REL_WHEEL);

	__set_bit(EV_KEY, ptr->evbit);
	for (i = BTN_LEFT; i <= BTN_TASK; i++)
		__set_bit(i, ptr->keybit);

	ret = input_register_device(ptr);
	if (ret) {
		input_free_device(ptr);
		xenbus_dev_fatal(dev, ret, "input_register_device(ptr)");
		goto error;
	}
	info->ptr = ptr;

	ret = xenkbd_connect_backend(dev, info);
	if (ret < 0)
		goto error;

	return 0;

 error_nomem:
	ret = -ENOMEM;
	xenbus_dev_fatal(dev, ret, "allocating device memory");
 error:
	xenkbd_remove(dev);
	return ret;
}

static int xenkbd_resume(struct xenbus_device *dev)
{
	struct xenkbd_info *info = dev_get_drvdata(&dev->dev);

	xenkbd_disconnect_backend(info);
	memset(info->page, 0, PAGE_SIZE);
	return xenkbd_connect_backend(dev, info);
}

static int xenkbd_remove(struct xenbus_device *dev)
{
	struct xenkbd_info *info = dev_get_drvdata(&dev->dev);

	xenkbd_disconnect_backend(info);
	if (info->kbd)
		input_unregister_device(info->kbd);
	if (info->ptr)
		input_unregister_device(info->ptr);
	free_page((unsigned long)info->page);
	kfree(info);
	return 0;
}

static int xenkbd_connect_backend(struct xenbus_device *dev,
				  struct xenkbd_info *info)
{
	int ret, evtchn;
	struct xenbus_transaction xbt;

	ret = gnttab_grant_foreign_access(dev->otherend_id,
	                                  virt_to_mfn(info->page), 0);
	if (ret < 0)
		return ret;
	info->gref = ret;

	ret = xenbus_alloc_evtchn(dev, &evtchn);
	if (ret)
		goto error_grant;
	ret = bind_evtchn_to_irqhandler(evtchn, input_handler,
					0, dev->devicetype, info);
	if (ret < 0) {
		xenbus_dev_fatal(dev, ret, "bind_evtchn_to_irqhandler");
		goto error_evtchan;
	}
	info->irq = ret;

 again:
	ret = xenbus_transaction_start(&xbt);
	if (ret) {
		xenbus_dev_fatal(dev, ret, "starting transaction");
		goto error_irqh;
	}
	ret = xenbus_printf(xbt, dev->nodename, "page-ref", "%lu",
			    virt_to_mfn(info->page));
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "page-gref", "%u", info->gref);
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "event-channel", "%u",
			    evtchn);
	if (ret)
		goto error_xenbus;
	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, ret, "completing transaction");
		goto error_irqh;
	}

	xenbus_switch_state(dev, XenbusStateInitialised);
	return 0;

 error_xenbus:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, ret, "writing xenstore");
 error_irqh:
	unbind_from_irqhandler(info->irq, info);
	info->irq = -1;
 error_evtchan:
	xenbus_free_evtchn(dev, evtchn);
 error_grant:
	gnttab_end_foreign_access_ref(info->gref, 0);
	info->gref = -1;
	return ret;
}

static void xenkbd_disconnect_backend(struct xenkbd_info *info)
{
	if (info->irq >= 0)
		unbind_from_irqhandler(info->irq, info);
	info->irq = -1;
	if (info->gref >= 0)
		gnttab_end_foreign_access_ref(info->gref, 0);
	info->gref = -1;
}

static void xenkbd_backend_changed(struct xenbus_device *dev,
				   enum xenbus_state backend_state)
{
	struct xenkbd_info *info = dev_get_drvdata(&dev->dev);
	int ret, val;

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
		break;

	case XenbusStateInitWait:
InitWait:
		ret = xenbus_scanf(XBT_NIL, info->xbdev->otherend,
				   "feature-abs-pointer", "%d", &val);
		if (ret < 0)
			val = 0;
		if (val) {
			ret = xenbus_printf(XBT_NIL, info->xbdev->nodename,
					    "request-abs-pointer", "1");
			if (ret)
				pr_warning("xenkbd: can't request abs-pointer");
		}

		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateConnected:
		/*
		 * Work around xenbus race condition: If backend goes
		 * through InitWait to Connected fast enough, we can
		 * get Connected twice here.
		 */
		if (dev->state != XenbusStateConnected)
			goto InitWait; /* no InitWait seen yet, fudge it */

		/* Set input abs params to match backend screen res */
		if (xenbus_scanf(XBT_NIL, info->xbdev->otherend,
				 "width", "%d", &val) > 0)
			input_set_abs_params(info->ptr, ABS_X, 0, val, 0, 0);

		if (xenbus_scanf(XBT_NIL, info->xbdev->otherend,
				 "height", "%d", &val) > 0)
			input_set_abs_params(info->ptr, ABS_Y, 0, val, 0, 0);

		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		/* Missed the backend's CLOSING state -- fallthrough */
	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
		break;
	}
}

static const struct xenbus_device_id xenkbd_ids[] = {
	{ "vkbd" },
	{ "" }
};

static struct xenbus_driver xenkbd_driver = {
	.ids = xenkbd_ids,
	.probe = xenkbd_probe,
	.remove = xenkbd_remove,
	.resume = xenkbd_resume,
	.otherend_changed = xenkbd_backend_changed,
};

static int __init xenkbd_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	/* Nothing to do if running in dom0. */
	if (xen_initial_domain())
		return -ENODEV;

	if (!xen_has_pv_devices())
		return -ENODEV;

	return xenbus_register_frontend(&xenkbd_driver);
}

static void __exit xenkbd_cleanup(void)
{
	xenbus_unregister_driver(&xenkbd_driver);
}

module_init(xenkbd_init);
module_exit(xenkbd_cleanup);

MODULE_DESCRIPTION("Xen virtual keyboard/pointer device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:vkbd");
