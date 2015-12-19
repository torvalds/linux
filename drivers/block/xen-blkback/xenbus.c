/*  Xenbus code for blkif backend
    Copyright (C) 2005 Rusty Russell <rusty@rustcorp.com.au>
    Copyright (C) 2005 XenSource Ltd

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*/

#define pr_fmt(fmt) "xen-blkback: " fmt

#include <stdarg.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include "common.h"

/* Enlarge the array size in order to fully show blkback name. */
#define BLKBACK_NAME_LEN (20)
#define RINGREF_NAME_LEN (20)

struct backend_info {
	struct xenbus_device	*dev;
	struct xen_blkif	*blkif;
	struct xenbus_watch	backend_watch;
	unsigned		major;
	unsigned		minor;
	char			*mode;
};

static struct kmem_cache *xen_blkif_cachep;
static void connect(struct backend_info *);
static int connect_ring(struct backend_info *);
static void backend_changed(struct xenbus_watch *, const char **,
			    unsigned int);
static void xen_blkif_free(struct xen_blkif *blkif);
static void xen_vbd_free(struct xen_vbd *vbd);

struct xenbus_device *xen_blkbk_xenbus(struct backend_info *be)
{
	return be->dev;
}

/*
 * The last request could free the device from softirq context and
 * xen_blkif_free() can sleep.
 */
static void xen_blkif_deferred_free(struct work_struct *work)
{
	struct xen_blkif *blkif;

	blkif = container_of(work, struct xen_blkif, free_work);
	xen_blkif_free(blkif);
}

static int blkback_name(struct xen_blkif *blkif, char *buf)
{
	char *devpath, *devname;
	struct xenbus_device *dev = blkif->be->dev;

	devpath = xenbus_read(XBT_NIL, dev->nodename, "dev", NULL);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);

	devname = strstr(devpath, "/dev/");
	if (devname != NULL)
		devname += strlen("/dev/");
	else
		devname  = devpath;

	snprintf(buf, BLKBACK_NAME_LEN, "blkback.%d.%s", blkif->domid, devname);
	kfree(devpath);

	return 0;
}

static void xen_update_blkif_status(struct xen_blkif *blkif)
{
	int err;
	char name[BLKBACK_NAME_LEN];

	/* Not ready to connect? */
	if (!blkif->irq || !blkif->vbd.bdev)
		return;

	/* Already connected? */
	if (blkif->be->dev->state == XenbusStateConnected)
		return;

	/* Attempt to connect: exit if we fail to. */
	connect(blkif->be);
	if (blkif->be->dev->state != XenbusStateConnected)
		return;

	err = blkback_name(blkif, name);
	if (err) {
		xenbus_dev_error(blkif->be->dev, err, "get blkback dev name");
		return;
	}

	err = filemap_write_and_wait(blkif->vbd.bdev->bd_inode->i_mapping);
	if (err) {
		xenbus_dev_error(blkif->be->dev, err, "block flush");
		return;
	}
	invalidate_inode_pages2(blkif->vbd.bdev->bd_inode->i_mapping);

	blkif->xenblkd = kthread_run(xen_blkif_schedule, blkif, "%s", name);
	if (IS_ERR(blkif->xenblkd)) {
		err = PTR_ERR(blkif->xenblkd);
		blkif->xenblkd = NULL;
		xenbus_dev_error(blkif->be->dev, err, "start xenblkd");
		return;
	}
}

static struct xen_blkif *xen_blkif_alloc(domid_t domid)
{
	struct xen_blkif *blkif;

	BUILD_BUG_ON(MAX_INDIRECT_PAGES > BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST);

	blkif = kmem_cache_zalloc(xen_blkif_cachep, GFP_KERNEL);
	if (!blkif)
		return ERR_PTR(-ENOMEM);

	blkif->domid = domid;
	spin_lock_init(&blkif->blk_ring_lock);
	atomic_set(&blkif->refcnt, 1);
	init_waitqueue_head(&blkif->wq);
	init_completion(&blkif->drain_complete);
	atomic_set(&blkif->drain, 0);
	blkif->st_print = jiffies;
	blkif->persistent_gnts.rb_node = NULL;
	spin_lock_init(&blkif->free_pages_lock);
	INIT_LIST_HEAD(&blkif->free_pages);
	INIT_LIST_HEAD(&blkif->persistent_purge_list);
	blkif->free_pages_num = 0;
	atomic_set(&blkif->persistent_gnt_in_use, 0);
	atomic_set(&blkif->inflight, 0);
	INIT_WORK(&blkif->persistent_purge_work, xen_blkbk_unmap_purged_grants);

	INIT_LIST_HEAD(&blkif->pending_free);
	INIT_WORK(&blkif->free_work, xen_blkif_deferred_free);
	spin_lock_init(&blkif->pending_free_lock);
	init_waitqueue_head(&blkif->pending_free_wq);
	init_waitqueue_head(&blkif->shutdown_wq);

	return blkif;
}

static int xen_blkif_map(struct xen_blkif *blkif, grant_ref_t *gref,
			 unsigned int nr_grefs, unsigned int evtchn)
{
	int err;

	/* Already connected through? */
	if (blkif->irq)
		return 0;

	err = xenbus_map_ring_valloc(blkif->be->dev, gref, nr_grefs,
				     &blkif->blk_ring);
	if (err < 0)
		return err;

	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
	{
		struct blkif_sring *sring;
		sring = (struct blkif_sring *)blkif->blk_ring;
		BACK_RING_INIT(&blkif->blk_rings.native, sring,
			       XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	case BLKIF_PROTOCOL_X86_32:
	{
		struct blkif_x86_32_sring *sring_x86_32;
		sring_x86_32 = (struct blkif_x86_32_sring *)blkif->blk_ring;
		BACK_RING_INIT(&blkif->blk_rings.x86_32, sring_x86_32,
			       XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	case BLKIF_PROTOCOL_X86_64:
	{
		struct blkif_x86_64_sring *sring_x86_64;
		sring_x86_64 = (struct blkif_x86_64_sring *)blkif->blk_ring;
		BACK_RING_INIT(&blkif->blk_rings.x86_64, sring_x86_64,
			       XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	default:
		BUG();
	}

	err = bind_interdomain_evtchn_to_irqhandler(blkif->domid, evtchn,
						    xen_blkif_be_int, 0,
						    "blkif-backend", blkif);
	if (err < 0) {
		xenbus_unmap_ring_vfree(blkif->be->dev, blkif->blk_ring);
		blkif->blk_rings.common.sring = NULL;
		return err;
	}
	blkif->irq = err;

	return 0;
}

static int xen_blkif_disconnect(struct xen_blkif *blkif)
{
	struct pending_req *req, *n;
	int i = 0, j;

	if (blkif->xenblkd) {
		kthread_stop(blkif->xenblkd);
		wake_up(&blkif->shutdown_wq);
		blkif->xenblkd = NULL;
	}

	/* The above kthread_stop() guarantees that at this point we
	 * don't have any discard_io or other_io requests. So, checking
	 * for inflight IO is enough.
	 */
	if (atomic_read(&blkif->inflight) > 0)
		return -EBUSY;

	if (blkif->irq) {
		unbind_from_irqhandler(blkif->irq, blkif);
		blkif->irq = 0;
	}

	if (blkif->blk_rings.common.sring) {
		xenbus_unmap_ring_vfree(blkif->be->dev, blkif->blk_ring);
		blkif->blk_rings.common.sring = NULL;
	}

	/* Remove all persistent grants and the cache of ballooned pages. */
	xen_blkbk_free_caches(blkif);

	/* Check that there is no request in use */
	list_for_each_entry_safe(req, n, &blkif->pending_free, free_list) {
		list_del(&req->free_list);

		for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++)
			kfree(req->segments[j]);

		for (j = 0; j < MAX_INDIRECT_PAGES; j++)
			kfree(req->indirect_pages[j]);

		kfree(req);
		i++;
	}

	WARN_ON(i != (XEN_BLKIF_REQS_PER_PAGE * blkif->nr_ring_pages));
	blkif->nr_ring_pages = 0;

	return 0;
}

static void xen_blkif_free(struct xen_blkif *blkif)
{

	xen_blkif_disconnect(blkif);
	xen_vbd_free(&blkif->vbd);

	/* Make sure everything is drained before shutting down */
	BUG_ON(blkif->persistent_gnt_c != 0);
	BUG_ON(atomic_read(&blkif->persistent_gnt_in_use) != 0);
	BUG_ON(blkif->free_pages_num != 0);
	BUG_ON(!list_empty(&blkif->persistent_purge_list));
	BUG_ON(!list_empty(&blkif->free_pages));
	BUG_ON(!RB_EMPTY_ROOT(&blkif->persistent_gnts));

	kmem_cache_free(xen_blkif_cachep, blkif);
}

int __init xen_blkif_interface_init(void)
{
	xen_blkif_cachep = kmem_cache_create("blkif_cache",
					     sizeof(struct xen_blkif),
					     0, 0, NULL);
	if (!xen_blkif_cachep)
		return -ENOMEM;

	return 0;
}

/*
 *  sysfs interface for VBD I/O requests
 */

#define VBD_SHOW(name, format, args...)					\
	static ssize_t show_##name(struct device *_dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
	{								\
		struct xenbus_device *dev = to_xenbus_device(_dev);	\
		struct backend_info *be = dev_get_drvdata(&dev->dev);	\
									\
		return sprintf(buf, format, ##args);			\
	}								\
	static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

VBD_SHOW(oo_req,  "%llu\n", be->blkif->st_oo_req);
VBD_SHOW(rd_req,  "%llu\n", be->blkif->st_rd_req);
VBD_SHOW(wr_req,  "%llu\n", be->blkif->st_wr_req);
VBD_SHOW(f_req,  "%llu\n", be->blkif->st_f_req);
VBD_SHOW(ds_req,  "%llu\n", be->blkif->st_ds_req);
VBD_SHOW(rd_sect, "%llu\n", be->blkif->st_rd_sect);
VBD_SHOW(wr_sect, "%llu\n", be->blkif->st_wr_sect);

static struct attribute *xen_vbdstat_attrs[] = {
	&dev_attr_oo_req.attr,
	&dev_attr_rd_req.attr,
	&dev_attr_wr_req.attr,
	&dev_attr_f_req.attr,
	&dev_attr_ds_req.attr,
	&dev_attr_rd_sect.attr,
	&dev_attr_wr_sect.attr,
	NULL
};

static struct attribute_group xen_vbdstat_group = {
	.name = "statistics",
	.attrs = xen_vbdstat_attrs,
};

VBD_SHOW(physical_device, "%x:%x\n", be->major, be->minor);
VBD_SHOW(mode, "%s\n", be->mode);

static int xenvbd_sysfs_addif(struct xenbus_device *dev)
{
	int error;

	error = device_create_file(&dev->dev, &dev_attr_physical_device);
	if (error)
		goto fail1;

	error = device_create_file(&dev->dev, &dev_attr_mode);
	if (error)
		goto fail2;

	error = sysfs_create_group(&dev->dev.kobj, &xen_vbdstat_group);
	if (error)
		goto fail3;

	return 0;

fail3:	sysfs_remove_group(&dev->dev.kobj, &xen_vbdstat_group);
fail2:	device_remove_file(&dev->dev, &dev_attr_mode);
fail1:	device_remove_file(&dev->dev, &dev_attr_physical_device);
	return error;
}

static void xenvbd_sysfs_delif(struct xenbus_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &xen_vbdstat_group);
	device_remove_file(&dev->dev, &dev_attr_mode);
	device_remove_file(&dev->dev, &dev_attr_physical_device);
}


static void xen_vbd_free(struct xen_vbd *vbd)
{
	if (vbd->bdev)
		blkdev_put(vbd->bdev, vbd->readonly ? FMODE_READ : FMODE_WRITE);
	vbd->bdev = NULL;
}

static int xen_vbd_create(struct xen_blkif *blkif, blkif_vdev_t handle,
			  unsigned major, unsigned minor, int readonly,
			  int cdrom)
{
	struct xen_vbd *vbd;
	struct block_device *bdev;
	struct request_queue *q;

	vbd = &blkif->vbd;
	vbd->handle   = handle;
	vbd->readonly = readonly;
	vbd->type     = 0;

	vbd->pdevice  = MKDEV(major, minor);

	bdev = blkdev_get_by_dev(vbd->pdevice, vbd->readonly ?
				 FMODE_READ : FMODE_WRITE, NULL);

	if (IS_ERR(bdev)) {
		pr_warn("xen_vbd_create: device %08x could not be opened\n",
			vbd->pdevice);
		return -ENOENT;
	}

	vbd->bdev = bdev;
	if (vbd->bdev->bd_disk == NULL) {
		pr_warn("xen_vbd_create: device %08x doesn't exist\n",
			vbd->pdevice);
		xen_vbd_free(vbd);
		return -ENOENT;
	}
	vbd->size = vbd_sz(vbd);

	if (vbd->bdev->bd_disk->flags & GENHD_FL_CD || cdrom)
		vbd->type |= VDISK_CDROM;
	if (vbd->bdev->bd_disk->flags & GENHD_FL_REMOVABLE)
		vbd->type |= VDISK_REMOVABLE;

	q = bdev_get_queue(bdev);
	if (q && q->flush_flags)
		vbd->flush_support = true;

	if (q && blk_queue_secdiscard(q))
		vbd->discard_secure = true;

	pr_debug("Successful creation of handle=%04x (dom=%u)\n",
		handle, blkif->domid);
	return 0;
}
static int xen_blkbk_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	pr_debug("%s %p %d\n", __func__, dev, dev->otherend_id);

	if (be->major || be->minor)
		xenvbd_sysfs_delif(dev);

	if (be->backend_watch.node) {
		unregister_xenbus_watch(&be->backend_watch);
		kfree(be->backend_watch.node);
		be->backend_watch.node = NULL;
	}

	dev_set_drvdata(&dev->dev, NULL);

	if (be->blkif) {
		xen_blkif_disconnect(be->blkif);
		xen_blkif_put(be->blkif);
	}

	kfree(be->mode);
	kfree(be);
	return 0;
}

int xen_blkbk_flush_diskcache(struct xenbus_transaction xbt,
			      struct backend_info *be, int state)
{
	struct xenbus_device *dev = be->dev;
	int err;

	err = xenbus_printf(xbt, dev->nodename, "feature-flush-cache",
			    "%d", state);
	if (err)
		dev_warn(&dev->dev, "writing feature-flush-cache (%d)", err);

	return err;
}

static void xen_blkbk_discard(struct xenbus_transaction xbt, struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	struct xen_blkif *blkif = be->blkif;
	int err;
	int state = 0, discard_enable;
	struct block_device *bdev = be->blkif->vbd.bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	err = xenbus_scanf(XBT_NIL, dev->nodename, "discard-enable", "%d",
			   &discard_enable);
	if (err == 1 && !discard_enable)
		return;

	if (blk_queue_discard(q)) {
		err = xenbus_printf(xbt, dev->nodename,
			"discard-granularity", "%u",
			q->limits.discard_granularity);
		if (err) {
			dev_warn(&dev->dev, "writing discard-granularity (%d)", err);
			return;
		}
		err = xenbus_printf(xbt, dev->nodename,
			"discard-alignment", "%u",
			q->limits.discard_alignment);
		if (err) {
			dev_warn(&dev->dev, "writing discard-alignment (%d)", err);
			return;
		}
		state = 1;
		/* Optional. */
		err = xenbus_printf(xbt, dev->nodename,
				    "discard-secure", "%d",
				    blkif->vbd.discard_secure);
		if (err) {
			dev_warn(&dev->dev, "writing discard-secure (%d)", err);
			return;
		}
	}
	err = xenbus_printf(xbt, dev->nodename, "feature-discard",
			    "%d", state);
	if (err)
		dev_warn(&dev->dev, "writing feature-discard (%d)", err);
}
int xen_blkbk_barrier(struct xenbus_transaction xbt,
		      struct backend_info *be, int state)
{
	struct xenbus_device *dev = be->dev;
	int err;

	err = xenbus_printf(xbt, dev->nodename, "feature-barrier",
			    "%d", state);
	if (err)
		dev_warn(&dev->dev, "writing feature-barrier (%d)", err);

	return err;
}

/*
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures, and watch the store waiting for the hotplug scripts to tell us
 * the device's physical major and minor numbers.  Switch to InitWait.
 */
static int xen_blkbk_probe(struct xenbus_device *dev,
			   const struct xenbus_device_id *id)
{
	int err;
	struct backend_info *be = kzalloc(sizeof(struct backend_info),
					  GFP_KERNEL);

	/* match the pr_debug in xen_blkbk_remove */
	pr_debug("%s %p %d\n", __func__, dev, dev->otherend_id);

	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM,
				 "allocating backend structure");
		return -ENOMEM;
	}
	be->dev = dev;
	dev_set_drvdata(&dev->dev, be);

	be->blkif = xen_blkif_alloc(dev->otherend_id);
	if (IS_ERR(be->blkif)) {
		err = PTR_ERR(be->blkif);
		be->blkif = NULL;
		xenbus_dev_fatal(dev, err, "creating block interface");
		goto fail;
	}

	/* setup back pointer */
	be->blkif->be = be;

	err = xenbus_watch_pathfmt(dev, &be->backend_watch, backend_changed,
				   "%s/%s", dev->nodename, "physical-device");
	if (err)
		goto fail;

	err = xenbus_printf(XBT_NIL, dev->nodename, "max-ring-page-order", "%u",
			    xen_blkif_max_ring_order);
	if (err)
		pr_warn("%s write out 'max-ring-page-order' failed\n", __func__);

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;

fail:
	pr_warn("%s failed\n", __func__);
	xen_blkbk_remove(dev);
	return err;
}


/*
 * Callback received when the hotplug scripts have placed the physical-device
 * node.  Read it and the mode node, and create a vbd.  If the frontend is
 * ready, connect.
 */
static void backend_changed(struct xenbus_watch *watch,
			    const char **vec, unsigned int len)
{
	int err;
	unsigned major;
	unsigned minor;
	struct backend_info *be
		= container_of(watch, struct backend_info, backend_watch);
	struct xenbus_device *dev = be->dev;
	int cdrom = 0;
	unsigned long handle;
	char *device_type;

	pr_debug("%s %p %d\n", __func__, dev, dev->otherend_id);

	err = xenbus_scanf(XBT_NIL, dev->nodename, "physical-device", "%x:%x",
			   &major, &minor);
	if (XENBUS_EXIST_ERR(err)) {
		/*
		 * Since this watch will fire once immediately after it is
		 * registered, we expect this.  Ignore it, and wait for the
		 * hotplug scripts.
		 */
		return;
	}
	if (err != 2) {
		xenbus_dev_fatal(dev, err, "reading physical-device");
		return;
	}

	if (be->major | be->minor) {
		if (be->major != major || be->minor != minor)
			pr_warn("changing physical device (from %x:%x to %x:%x) not supported.\n",
				be->major, be->minor, major, minor);
		return;
	}

	be->mode = xenbus_read(XBT_NIL, dev->nodename, "mode", NULL);
	if (IS_ERR(be->mode)) {
		err = PTR_ERR(be->mode);
		be->mode = NULL;
		xenbus_dev_fatal(dev, err, "reading mode");
		return;
	}

	device_type = xenbus_read(XBT_NIL, dev->otherend, "device-type", NULL);
	if (!IS_ERR(device_type)) {
		cdrom = strcmp(device_type, "cdrom") == 0;
		kfree(device_type);
	}

	/* Front end dir is a number, which is used as the handle. */
	err = kstrtoul(strrchr(dev->otherend, '/') + 1, 0, &handle);
	if (err)
		return;

	be->major = major;
	be->minor = minor;

	err = xen_vbd_create(be->blkif, handle, major, minor,
			     !strchr(be->mode, 'w'), cdrom);

	if (err)
		xenbus_dev_fatal(dev, err, "creating vbd structure");
	else {
		err = xenvbd_sysfs_addif(dev);
		if (err) {
			xen_vbd_free(&be->blkif->vbd);
			xenbus_dev_fatal(dev, err, "creating sysfs entries");
		}
	}

	if (err) {
		kfree(be->mode);
		be->mode = NULL;
		be->major = 0;
		be->minor = 0;
	} else {
		/* We're potentially connected now */
		xen_update_blkif_status(be->blkif);
	}
}


/*
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *dev,
			     enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);
	int err;

	pr_debug("%s %p %s\n", __func__, dev, xenbus_strstate(frontend_state));

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			pr_info("%s: prepare for reconnect\n", dev->nodename);
			xenbus_switch_state(dev, XenbusStateInitWait);
		}
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		/*
		 * Ensure we connect even when two watches fire in
		 * close succession and we miss the intermediate value
		 * of frontend_state.
		 */
		if (dev->state == XenbusStateConnected)
			break;

		/*
		 * Enforce precondition before potential leak point.
		 * xen_blkif_disconnect() is idempotent.
		 */
		err = xen_blkif_disconnect(be->blkif);
		if (err) {
			xenbus_dev_fatal(dev, err, "pending I/O");
			break;
		}

		err = connect_ring(be);
		if (err)
			break;
		xen_update_blkif_status(be->blkif);
		break;

	case XenbusStateClosing:
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xen_blkif_disconnect(be->blkif);
		xenbus_switch_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		/* implies xen_blkif_disconnect() via xen_blkbk_remove() */
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}


/* ** Connection ** */


/*
 * Write the physical details regarding the block device to the store, and
 * switch to Connected state.
 */
static void connect(struct backend_info *be)
{
	struct xenbus_transaction xbt;
	int err;
	struct xenbus_device *dev = be->dev;

	pr_debug("%s %s\n", __func__, dev->otherend);

	/* Supply the information about the device the frontend needs */
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		return;
	}

	/* If we can't advertise it is OK. */
	xen_blkbk_flush_diskcache(xbt, be, be->blkif->vbd.flush_support);

	xen_blkbk_discard(xbt, be);

	xen_blkbk_barrier(xbt, be, be->blkif->vbd.flush_support);

	err = xenbus_printf(xbt, dev->nodename, "feature-persistent", "%u", 1);
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/feature-persistent",
				 dev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, dev->nodename, "feature-max-indirect-segments", "%u",
			    MAX_INDIRECT_SEGMENTS);
	if (err)
		dev_warn(&dev->dev, "writing %s/feature-max-indirect-segments (%d)",
			 dev->nodename, err);

	err = xenbus_printf(xbt, dev->nodename, "sectors", "%llu",
			    (unsigned long long)vbd_sz(&be->blkif->vbd));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/sectors",
				 dev->nodename);
		goto abort;
	}

	/* FIXME: use a typename instead */
	err = xenbus_printf(xbt, dev->nodename, "info", "%u",
			    be->blkif->vbd.type |
			    (be->blkif->vbd.readonly ? VDISK_READONLY : 0));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/info",
				 dev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, dev->nodename, "sector-size", "%lu",
			    (unsigned long)
			    bdev_logical_block_size(be->blkif->vbd.bdev));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/sector-size",
				 dev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, dev->nodename, "physical-sector-size", "%u",
			    bdev_physical_block_size(be->blkif->vbd.bdev));
	if (err)
		xenbus_dev_error(dev, err, "writing %s/physical-sector-size",
				 dev->nodename);

	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		xenbus_dev_fatal(dev, err, "ending transaction");

	err = xenbus_switch_state(dev, XenbusStateConnected);
	if (err)
		xenbus_dev_fatal(dev, err, "%s: switching to Connected state",
				 dev->nodename);

	return;
 abort:
	xenbus_transaction_end(xbt, 1);
}


static int connect_ring(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	unsigned int ring_ref[XENBUS_MAX_RING_GRANTS];
	unsigned int evtchn, nr_grefs, ring_page_order;
	unsigned int pers_grants;
	char protocol[64] = "";
	struct pending_req *req, *n;
	int err, i, j;

	pr_debug("%s %s\n", __func__, dev->otherend);

	err = xenbus_scanf(XBT_NIL, dev->otherend, "event-channel", "%u",
			  &evtchn);
	if (err != 1) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err, "reading %s/event-channel",
				 dev->otherend);
		return err;
	}
	pr_info("event-channel %u\n", evtchn);

	err = xenbus_scanf(XBT_NIL, dev->otherend, "ring-page-order", "%u",
			  &ring_page_order);
	if (err != 1) {
		err = xenbus_scanf(XBT_NIL, dev->otherend, "ring-ref",
				  "%u", &ring_ref[0]);
		if (err != 1) {
			err = -EINVAL;
			xenbus_dev_fatal(dev, err, "reading %s/ring-ref",
					 dev->otherend);
			return err;
		}
		nr_grefs = 1;
		pr_info("%s:using single page: ring-ref %d\n", dev->otherend,
			ring_ref[0]);
	} else {
		unsigned int i;

		if (ring_page_order > xen_blkif_max_ring_order) {
			err = -EINVAL;
			xenbus_dev_fatal(dev, err, "%s/request %d ring page order exceed max:%d",
					 dev->otherend, ring_page_order,
					 xen_blkif_max_ring_order);
			return err;
		}

		nr_grefs = 1 << ring_page_order;
		for (i = 0; i < nr_grefs; i++) {
			char ring_ref_name[RINGREF_NAME_LEN];

			snprintf(ring_ref_name, RINGREF_NAME_LEN, "ring-ref%u", i);
			err = xenbus_scanf(XBT_NIL, dev->otherend, ring_ref_name,
					   "%u", &ring_ref[i]);
			if (err != 1) {
				err = -EINVAL;
				xenbus_dev_fatal(dev, err, "reading %s/%s",
						 dev->otherend, ring_ref_name);
				return err;
			}
			pr_info("ring-ref%u: %u\n", i, ring_ref[i]);
		}
	}

	be->blkif->blk_protocol = BLKIF_PROTOCOL_DEFAULT;
	err = xenbus_gather(XBT_NIL, dev->otherend, "protocol",
			    "%63s", protocol, NULL);
	if (err)
		strcpy(protocol, "unspecified, assuming default");
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_NATIVE))
		be->blkif->blk_protocol = BLKIF_PROTOCOL_NATIVE;
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_X86_32))
		be->blkif->blk_protocol = BLKIF_PROTOCOL_X86_32;
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_X86_64))
		be->blkif->blk_protocol = BLKIF_PROTOCOL_X86_64;
	else {
		xenbus_dev_fatal(dev, err, "unknown fe protocol %s", protocol);
		return -1;
	}
	err = xenbus_gather(XBT_NIL, dev->otherend,
			    "feature-persistent", "%u",
			    &pers_grants, NULL);
	if (err)
		pers_grants = 0;

	be->blkif->vbd.feature_gnt_persistent = pers_grants;
	be->blkif->vbd.overflow_max_grants = 0;
	be->blkif->nr_ring_pages = nr_grefs;

	pr_info("ring-pages:%d, event-channel %d, protocol %d (%s) %s\n",
		nr_grefs, evtchn, be->blkif->blk_protocol, protocol,
		pers_grants ? "persistent grants" : "");

	for (i = 0; i < nr_grefs * XEN_BLKIF_REQS_PER_PAGE; i++) {
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req)
			goto fail;
		list_add_tail(&req->free_list, &be->blkif->pending_free);
		for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++) {
			req->segments[j] = kzalloc(sizeof(*req->segments[0]), GFP_KERNEL);
			if (!req->segments[j])
				goto fail;
		}
		for (j = 0; j < MAX_INDIRECT_PAGES; j++) {
			req->indirect_pages[j] = kzalloc(sizeof(*req->indirect_pages[0]),
							 GFP_KERNEL);
			if (!req->indirect_pages[j])
				goto fail;
		}
	}

	/* Map the shared frame, irq etc. */
	err = xen_blkif_map(be->blkif, ring_ref, nr_grefs, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref port %u", evtchn);
		return err;
	}

	return 0;

fail:
	list_for_each_entry_safe(req, n, &be->blkif->pending_free, free_list) {
		list_del(&req->free_list);
		for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++) {
			if (!req->segments[j])
				break;
			kfree(req->segments[j]);
		}
		for (j = 0; j < MAX_INDIRECT_PAGES; j++) {
			if (!req->indirect_pages[j])
				break;
			kfree(req->indirect_pages[j]);
		}
		kfree(req);
	}
	return -ENOMEM;
}

static const struct xenbus_device_id xen_blkbk_ids[] = {
	{ "vbd" },
	{ "" }
};

static struct xenbus_driver xen_blkbk_driver = {
	.ids  = xen_blkbk_ids,
	.probe = xen_blkbk_probe,
	.remove = xen_blkbk_remove,
	.otherend_changed = frontend_changed
};

int xen_blkif_xenbus_init(void)
{
	return xenbus_register_backend(&xen_blkbk_driver);
}
