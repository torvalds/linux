/*
 * The USB Monitor, inspired by Dave Harding's USBMon.
 *
 * mon_main.c: Main file, module initiation and exit, registrations, etc.
 *
 * Copyright (C) 2005 Pete Zaitcev (zaitcev@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/notifier.h>
#include <linux/mutex.h>

#include "usb_mon.h"
#include "../core/hcd.h"

static void mon_stop(struct mon_bus *mbus);
static void mon_dissolve(struct mon_bus *mbus, struct usb_bus *ubus);
static void mon_bus_drop(struct kref *r);
static void mon_bus_init(struct usb_bus *ubus);

DEFINE_MUTEX(mon_lock);

struct mon_bus mon_bus0;		/* Pseudo bus meaning "all buses" */
static LIST_HEAD(mon_buses);		/* All buses we know: struct mon_bus */

/*
 * Link a reader into the bus.
 *
 * This must be called with mon_lock taken because of mbus->ref.
 */
void mon_reader_add(struct mon_bus *mbus, struct mon_reader *r)
{
	unsigned long flags;
	struct list_head *p;

	spin_lock_irqsave(&mbus->lock, flags);
	if (mbus->nreaders == 0) {
		if (mbus == &mon_bus0) {
			list_for_each (p, &mon_buses) {
				struct mon_bus *m1;
				m1 = list_entry(p, struct mon_bus, bus_link);
				m1->u_bus->monitored = 1;
			}
		} else {
			mbus->u_bus->monitored = 1;
		}
	}
	mbus->nreaders++;
	list_add_tail(&r->r_link, &mbus->r_list);
	spin_unlock_irqrestore(&mbus->lock, flags);

	kref_get(&mbus->ref);
}

/*
 * Unlink reader from the bus.
 *
 * This is called with mon_lock taken, so we can decrement mbus->ref.
 */
void mon_reader_del(struct mon_bus *mbus, struct mon_reader *r)
{
	unsigned long flags;

	spin_lock_irqsave(&mbus->lock, flags);
	list_del(&r->r_link);
	--mbus->nreaders;
	if (mbus->nreaders == 0)
		mon_stop(mbus);
	spin_unlock_irqrestore(&mbus->lock, flags);

	kref_put(&mbus->ref, mon_bus_drop);
}

/*
 */
static void mon_bus_submit(struct mon_bus *mbus, struct urb *urb)
{
	unsigned long flags;
	struct list_head *pos;
	struct mon_reader *r;

	spin_lock_irqsave(&mbus->lock, flags);
	mbus->cnt_events++;
	list_for_each (pos, &mbus->r_list) {
		r = list_entry(pos, struct mon_reader, r_link);
		r->rnf_submit(r->r_data, urb);
	}
	spin_unlock_irqrestore(&mbus->lock, flags);
	return;
}

static void mon_submit(struct usb_bus *ubus, struct urb *urb)
{
	struct mon_bus *mbus;

	if ((mbus = ubus->mon_bus) != NULL)
		mon_bus_submit(mbus, urb);
	mon_bus_submit(&mon_bus0, urb);
}

/*
 */
static void mon_bus_submit_error(struct mon_bus *mbus, struct urb *urb, int error)
{
	unsigned long flags;
	struct list_head *pos;
	struct mon_reader *r;

	spin_lock_irqsave(&mbus->lock, flags);
	mbus->cnt_events++;
	list_for_each (pos, &mbus->r_list) {
		r = list_entry(pos, struct mon_reader, r_link);
		r->rnf_error(r->r_data, urb, error);
	}
	spin_unlock_irqrestore(&mbus->lock, flags);
	return;
}

static void mon_submit_error(struct usb_bus *ubus, struct urb *urb, int error)
{
	struct mon_bus *mbus;

	if ((mbus = ubus->mon_bus) != NULL)
		mon_bus_submit_error(mbus, urb, error);
	mon_bus_submit_error(&mon_bus0, urb, error);
}

/*
 */
static void mon_bus_complete(struct mon_bus *mbus, struct urb *urb,
		int status)
{
	unsigned long flags;
	struct list_head *pos;
	struct mon_reader *r;

	spin_lock_irqsave(&mbus->lock, flags);
	mbus->cnt_events++;
	list_for_each (pos, &mbus->r_list) {
		r = list_entry(pos, struct mon_reader, r_link);
		r->rnf_complete(r->r_data, urb, status);
	}
	spin_unlock_irqrestore(&mbus->lock, flags);
}

static void mon_complete(struct usb_bus *ubus, struct urb *urb, int status)
{
	struct mon_bus *mbus;

	if ((mbus = ubus->mon_bus) != NULL)
		mon_bus_complete(mbus, urb, status);
	mon_bus_complete(&mon_bus0, urb, status);
}

/* int (*unlink_urb) (struct urb *urb, int status); */

/*
 * Stop monitoring.
 */
static void mon_stop(struct mon_bus *mbus)
{
	struct usb_bus *ubus;
	struct list_head *p;

	if (mbus == &mon_bus0) {
		list_for_each (p, &mon_buses) {
			mbus = list_entry(p, struct mon_bus, bus_link);
			/*
			 * We do not change nreaders here, so rely on mon_lock.
			 */
			if (mbus->nreaders == 0 && (ubus = mbus->u_bus) != NULL)
				ubus->monitored = 0;
		}
	} else {
		/*
		 * A stop can be called for a dissolved mon_bus in case of
		 * a reader staying across an rmmod foo_hcd, so test ->u_bus.
		 */
		if (mon_bus0.nreaders == 0 && (ubus = mbus->u_bus) != NULL) {
			ubus->monitored = 0;
			mb();
		}
	}
}

/*
 * Add a USB bus (usually by a modprobe foo-hcd)
 *
 * This does not return an error code because the core cannot care less
 * if monitoring is not established.
 */
static void mon_bus_add(struct usb_bus *ubus)
{
	mon_bus_init(ubus);
	mutex_lock(&mon_lock);
	if (mon_bus0.nreaders != 0)
		ubus->monitored = 1;
	mutex_unlock(&mon_lock);
}

/*
 * Remove a USB bus (either from rmmod foo-hcd or from a hot-remove event).
 */
static void mon_bus_remove(struct usb_bus *ubus)
{
	struct mon_bus *mbus = ubus->mon_bus;

	mutex_lock(&mon_lock);
	list_del(&mbus->bus_link);
	if (mbus->text_inited)
		mon_text_del(mbus);
	if (mbus->bin_inited)
		mon_bin_del(mbus);

	mon_dissolve(mbus, ubus);
	kref_put(&mbus->ref, mon_bus_drop);
	mutex_unlock(&mon_lock);
}

static int mon_notify(struct notifier_block *self, unsigned long action,
		      void *dev)
{
	switch (action) {
	case USB_BUS_ADD:
		mon_bus_add(dev);
		break;
	case USB_BUS_REMOVE:
		mon_bus_remove(dev);
	}
	return NOTIFY_OK;
}

static struct notifier_block mon_nb = {
	.notifier_call = 	mon_notify,
};

/*
 * Ops
 */
static struct usb_mon_operations mon_ops_0 = {
	.urb_submit =	mon_submit,
	.urb_submit_error = mon_submit_error,
	.urb_complete =	mon_complete,
};

/*
 * Tear usb_bus and mon_bus apart.
 */
static void mon_dissolve(struct mon_bus *mbus, struct usb_bus *ubus)
{

	if (ubus->monitored) {
		ubus->monitored = 0;
		mb();
	}

	ubus->mon_bus = NULL;
	mbus->u_bus = NULL;
	mb();

	/* We want synchronize_irq() here, but that needs an argument. */
}

/*
 */
static void mon_bus_drop(struct kref *r)
{
	struct mon_bus *mbus = container_of(r, struct mon_bus, ref);
	kfree(mbus);
}

/*
 * Initialize a bus for us:
 *  - allocate mon_bus
 *  - refcount USB bus struct
 *  - link
 */
static void mon_bus_init(struct usb_bus *ubus)
{
	struct mon_bus *mbus;

	if ((mbus = kzalloc(sizeof(struct mon_bus), GFP_KERNEL)) == NULL)
		goto err_alloc;
	kref_init(&mbus->ref);
	spin_lock_init(&mbus->lock);
	INIT_LIST_HEAD(&mbus->r_list);

	/*
	 * We don't need to take a reference to ubus, because we receive
	 * a notification if the bus is about to be removed.
	 */
	mbus->u_bus = ubus;
	ubus->mon_bus = mbus;

	mbus->text_inited = mon_text_add(mbus, ubus);
	mbus->bin_inited = mon_bin_add(mbus, ubus);

	mutex_lock(&mon_lock);
	list_add_tail(&mbus->bus_link, &mon_buses);
	mutex_unlock(&mon_lock);
	return;

err_alloc:
	return;
}

static void mon_bus0_init(void)
{
	struct mon_bus *mbus = &mon_bus0;

	kref_init(&mbus->ref);
	spin_lock_init(&mbus->lock);
	INIT_LIST_HEAD(&mbus->r_list);

	mbus->text_inited = mon_text_add(mbus, NULL);
	mbus->bin_inited = mon_bin_add(mbus, NULL);
}

/*
 * Search a USB bus by number. Notice that USB bus numbers start from one,
 * which we may later use to identify "all" with zero.
 *
 * This function must be called with mon_lock held.
 *
 * This is obviously inefficient and may be revised in the future.
 */
struct mon_bus *mon_bus_lookup(unsigned int num)
{
	struct list_head *p;
	struct mon_bus *mbus;

	if (num == 0) {
		return &mon_bus0;
	}
	list_for_each (p, &mon_buses) {
		mbus = list_entry(p, struct mon_bus, bus_link);
		if (mbus->u_bus->busnum == num) {
			return mbus;
		}
	}
	return NULL;
}

static int __init mon_init(void)
{
	struct usb_bus *ubus;
	int rc;

	if ((rc = mon_text_init()) != 0)
		goto err_text;
	if ((rc = mon_bin_init()) != 0)
		goto err_bin;

	mon_bus0_init();

	if (usb_mon_register(&mon_ops_0) != 0) {
		printk(KERN_NOTICE TAG ": unable to register with the core\n");
		rc = -ENODEV;
		goto err_reg;
	}
	// MOD_INC_USE_COUNT(which_module?);

	usb_register_notify(&mon_nb);

	mutex_lock(&usb_bus_list_lock);
	list_for_each_entry (ubus, &usb_bus_list, bus_list) {
		mon_bus_init(ubus);
	}
	mutex_unlock(&usb_bus_list_lock);
	return 0;

err_reg:
	mon_bin_exit();
err_bin:
	mon_text_exit();
err_text:
	return rc;
}

static void __exit mon_exit(void)
{
	struct mon_bus *mbus;
	struct list_head *p;

	usb_unregister_notify(&mon_nb);
	usb_mon_deregister();

	mutex_lock(&mon_lock);

	while (!list_empty(&mon_buses)) {
		p = mon_buses.next;
		mbus = list_entry(p, struct mon_bus, bus_link);
		list_del(p);

		if (mbus->text_inited)
			mon_text_del(mbus);
		if (mbus->bin_inited)
			mon_bin_del(mbus);

		/*
		 * This never happens, because the open/close paths in
		 * file level maintain module use counters and so rmmod fails
		 * before reaching here. However, better be safe...
		 */
		if (mbus->nreaders) {
			printk(KERN_ERR TAG
			    ": Outstanding opens (%d) on usb%d, leaking...\n",
			    mbus->nreaders, mbus->u_bus->busnum);
			atomic_set(&mbus->ref.refcount, 2);	/* Force leak */
		}

		mon_dissolve(mbus, mbus->u_bus);
		kref_put(&mbus->ref, mon_bus_drop);
	}

	mbus = &mon_bus0;
	if (mbus->text_inited)
		mon_text_del(mbus);
	if (mbus->bin_inited)
		mon_bin_del(mbus);

	mutex_unlock(&mon_lock);

	mon_text_exit();
	mon_bin_exit();
}

module_init(mon_init);
module_exit(mon_exit);

MODULE_LICENSE("GPL");
