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
#include <linux/debugfs.h>
#include <linux/smp_lock.h>

#include "usb_mon.h"
#include "../core/hcd.h"

static void mon_submit(struct usb_bus *ubus, struct urb *urb);
static void mon_complete(struct usb_bus *ubus, struct urb *urb);
static void mon_stop(struct mon_bus *mbus);
static void mon_dissolve(struct mon_bus *mbus, struct usb_bus *ubus);
static void mon_bus_drop(struct kref *r);
static void mon_bus_init(struct dentry *mondir, struct usb_bus *ubus);

DECLARE_MUTEX(mon_lock);

static struct dentry *mon_dir;		/* /dbg/usbmon */
static LIST_HEAD(mon_buses);		/* All buses we know: struct mon_bus */

/*
 * Link a reader into the bus.
 *
 * This must be called with mon_lock taken because of mbus->ref.
 */
void mon_reader_add(struct mon_bus *mbus, struct mon_reader *r)
{
	unsigned long flags;
	struct usb_bus *ubus;

	spin_lock_irqsave(&mbus->lock, flags);
	if (mbus->nreaders == 0) {
		ubus = mbus->u_bus;
		if (ubus->monitored) {
			/*
			 * Something is really broken, refuse to go on and
			 * possibly corrupt ops pointers or worse.
			 */
			printk(KERN_ERR TAG ": bus %d is already monitored\n",
			    ubus->busnum);
			spin_unlock_irqrestore(&mbus->lock, flags);
			return;
		}
		ubus->monitored = 1;
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
static void mon_submit(struct usb_bus *ubus, struct urb *urb)
{
	struct mon_bus *mbus;
	unsigned long flags;
	struct list_head *pos;
	struct mon_reader *r;

	mbus = ubus->mon_bus;
	if (mbus == NULL)
		goto out_unlocked;

	spin_lock_irqsave(&mbus->lock, flags);
	if (mbus->nreaders == 0)
		goto out_locked;

	list_for_each (pos, &mbus->r_list) {
		r = list_entry(pos, struct mon_reader, r_link);
		r->rnf_submit(r->r_data, urb);
	}

	spin_unlock_irqrestore(&mbus->lock, flags);
	return;

out_locked:
	spin_unlock_irqrestore(&mbus->lock, flags);
out_unlocked:
	return;
}

/*
 */
static void mon_submit_error(struct usb_bus *ubus, struct urb *urb, int err)
{
	struct mon_bus *mbus;

	mbus = ubus->mon_bus;
	if (mbus == NULL)
		goto out_unlocked;

	/*
	 * XXX Capture the error code and the 'E' event.
	 */

	return;

out_unlocked:
	return;
}

/*
 */
static void mon_complete(struct usb_bus *ubus, struct urb *urb)
{
	struct mon_bus *mbus;
	unsigned long flags;
	struct list_head *pos;
	struct mon_reader *r;

	mbus = ubus->mon_bus;
	if (mbus == NULL) {
		/*
		 * This should not happen.
		 * At this point we do not even know the bus number...
		 */
		printk(KERN_ERR TAG ": Null mon bus in URB, pipe 0x%x\n",
		    urb->pipe);
		return;
	}

	spin_lock_irqsave(&mbus->lock, flags);
	list_for_each (pos, &mbus->r_list) {
		r = list_entry(pos, struct mon_reader, r_link);
		r->rnf_complete(r->r_data, urb);
	}
	spin_unlock_irqrestore(&mbus->lock, flags);
}

/* int (*unlink_urb) (struct urb *urb, int status); */

/*
 * Stop monitoring.
 * Obviously this must be well locked, so no need to play with mb's.
 */
static void mon_stop(struct mon_bus *mbus)
{
	struct usb_bus *ubus = mbus->u_bus;

	/*
	 * A stop can be called for a dissolved mon_bus in case of
	 * a reader staying across an rmmod foo_hcd.
	 */
	if (ubus != NULL) {
		ubus->monitored = 0;
		mb();
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
	mon_bus_init(mon_dir, ubus);
}

/*
 * Remove a USB bus (either from rmmod foo-hcd or from a hot-remove event).
 */
static void mon_bus_remove(struct usb_bus *ubus)
{
	struct mon_bus *mbus = ubus->mon_bus;

	down(&mon_lock);
	list_del(&mbus->bus_link);
	debugfs_remove(mbus->dent_t);
	debugfs_remove(mbus->dent_s);

	mon_dissolve(mbus, ubus);
	kref_put(&mbus->ref, mon_bus_drop);
	up(&mon_lock);
}

/*
 * Ops
 */
static struct usb_mon_operations mon_ops_0 = {
	.urb_submit =	mon_submit,
	.urb_submit_error = mon_submit_error,
	.urb_complete =	mon_complete,
	.bus_add =	mon_bus_add,
	.bus_remove =	mon_bus_remove,
};

/*
 * Tear usb_bus and mon_bus apart.
 */
static void mon_dissolve(struct mon_bus *mbus, struct usb_bus *ubus)
{

	/*
	 * Never happens, but...
	 */
	if (ubus->monitored) {
		printk(KERN_ERR TAG ": bus %d is dissolved while monitored\n",
		    ubus->busnum);
		ubus->monitored = 0;
		mb();
	}

	ubus->mon_bus = NULL;
	mbus->u_bus = NULL;
	mb();
	// usb_bus_put(ubus);
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
static void mon_bus_init(struct dentry *mondir, struct usb_bus *ubus)
{
	struct dentry *d;
	struct mon_bus *mbus;
	enum { NAMESZ = 10 };
	char name[NAMESZ];
	int rc;

	if ((mbus = kmalloc(sizeof(struct mon_bus), GFP_KERNEL)) == NULL)
		goto err_alloc;
	memset(mbus, 0, sizeof(struct mon_bus));
	kref_init(&mbus->ref);
	spin_lock_init(&mbus->lock);
	INIT_LIST_HEAD(&mbus->r_list);

	/*
	 * This usb_bus_get here is superfluous, because we receive
	 * a notification if usb_bus is about to be removed.
	 */
	// usb_bus_get(ubus);
	mbus->u_bus = ubus;
	ubus->mon_bus = mbus;

	rc = snprintf(name, NAMESZ, "%dt", ubus->busnum);
	if (rc <= 0 || rc >= NAMESZ)
		goto err_print_t;
	d = debugfs_create_file(name, 0600, mondir, mbus, &mon_fops_text);
	if (d == NULL)
		goto err_create_t;
	mbus->dent_t = d;

	rc = snprintf(name, NAMESZ, "%ds", ubus->busnum);
	if (rc <= 0 || rc >= NAMESZ)
		goto err_print_s;
	d = debugfs_create_file(name, 0600, mondir, mbus, &mon_fops_stat);
	if (d == NULL)
		goto err_create_s;
	mbus->dent_s = d;

	down(&mon_lock);
	list_add_tail(&mbus->bus_link, &mon_buses);
	up(&mon_lock);
	return;

err_create_s:
err_print_s:
	debugfs_remove(mbus->dent_t);
err_create_t:
err_print_t:
	kfree(mbus);
err_alloc:
	return;
}

static int __init mon_init(void)
{
	struct usb_bus *ubus;
	struct dentry *mondir;

	mondir = debugfs_create_dir("usbmon", NULL);
	if (IS_ERR(mondir)) {
		printk(KERN_NOTICE TAG ": debugfs is not available\n");
		return -ENODEV;
	}
	if (mondir == NULL) {
		printk(KERN_NOTICE TAG ": unable to create usbmon directory\n");
		return -ENODEV;
	}
	mon_dir = mondir;

	if (usb_mon_register(&mon_ops_0) != 0) {
		printk(KERN_NOTICE TAG ": unable to register with the core\n");
		debugfs_remove(mondir);
		return -ENODEV;
	}
	// MOD_INC_USE_COUNT(which_module?);

	down(&usb_bus_list_lock);
	list_for_each_entry (ubus, &usb_bus_list, bus_list) {
		mon_bus_init(mondir, ubus);
	}
	up(&usb_bus_list_lock);
	return 0;
}

static void __exit mon_exit(void)
{
	struct mon_bus *mbus;
	struct list_head *p;

	usb_mon_deregister();

	down(&mon_lock);
	while (!list_empty(&mon_buses)) {
		p = mon_buses.next;
		mbus = list_entry(p, struct mon_bus, bus_link);
		list_del(p);

		debugfs_remove(mbus->dent_t);
		debugfs_remove(mbus->dent_s);

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
	up(&mon_lock);

	debugfs_remove(mon_dir);
}

module_init(mon_init);
module_exit(mon_exit);

MODULE_LICENSE("GPL");
