// SPDX-License-Identifier: GPL-2.0-or-later
/*  Xenbus code for blkif backend
    Copyright (C) 2005 Rusty Russell <rusty@rustcorp.com.au>
    Copyright (C) 2005 XenSource Ltd


*/

#define pr_fmt(fmt) "xen-blkback: " fmt

#include <stdarg.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include "common.h"

/* On the XenBus the max length of 'ring-ref%u'. */
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
static void backend_changed(struct xenbus_watch *, const char *,
			    const char *);
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

	snprintf(buf, TASK_COMM_LEN, "%d.%s", blkif->domid, devname);
	kfree(devpath);

	return 0;
}

static void xen_update_blkif_status(struct xen_blkif *blkif)
{
	int err;
	char name[TASK_COMM_LEN];
	struct xen_blkif_ring *ring;
	int i;

	/* Not ready to connect? */
	if (!blkif->rings || !blkif->rings[0].irq || !blkif->vbd.bdev)
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

	for (i = 0; i < blkif->nr_rings; i++) {
		ring = &blkif->rings[i];
		ring->xenblkd = kthread_run(xen_blkif_schedule, ring, "%s-%d", name, i);
		if (IS_ERR(ring->xenblkd)) {
			err = PTR_ERR(ring->xenblkd);
			ring->xenblkd = NULL;
			xenbus_dev_fatal(blkif->be->dev, err,
					"start %s-%d xenblkd", name, i);
			goto out;
		}
	}
	return;

out:
	while (--i >= 0) {
		ring = &blkif->rings[i];
		kthread_stop(ring->xenblkd);
	}
	return;
}

static int xen_blkif_alloc_rings(struct xen_blkif *blkif)
{
	unsigned int r;

	blkif->rings = kcalloc(blkif->nr_rings, sizeof(struct xen_blkif_ring),
			       GFP_KERNEL);
	if (!blkif->rings)
		return -ENOMEM;

	for (r = 0; r < blkif->nr_rings; r++) {
		struct xen_blkif_ring *ring = &blkif->rings[r];

		spin_lock_init(&ring->blk_ring_lock);
		init_waitqueue_head(&ring->wq);
		INIT_LIST_HEAD(&ring->pending_free);
		INIT_LIST_HEAD(&ring->persistent_purge_list);
		INIT_WORK(&ring->persistent_purge_work, xen_blkbk_unmap_purged_grants);
		gnttab_page_cache_init(&ring->free_pages);

		spin_lock_init(&ring->pending_free_lock);
		init_waitqueue_head(&ring->pending_free_wq);
		init_waitqueue_head(&ring->shutdown_wq);
		ring->blkif = blkif;
		ring->st_print = jiffies;
		ring->active = true;
	}

	return 0;
}

static struct xen_blkif *xen_blkif_alloc(domid_t domid)
{
	struct xen_blkif *blkif;

	BUILD_BUG_ON(MAX_INDIRECT_PAGES > BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST);

	blkif = kmem_cache_zalloc(xen_blkif_cachep, GFP_KERNEL);
	if (!blkif)
		return ERR_PTR(-ENOMEM);

	blkif->domid = domid;
	atomic_set(&blkif->refcnt, 1);
	init_completion(&blkif->drain_complete);

	/*
	 * Because freeing back to the cache may be deferred, it is not
	 * safe to unload the module (and hence destroy the cache) until
	 * this has completed. To prevent premature unloading, take an
	 * extra module reference here and release only when the object
	 * has been freed back to the cache.
	 */
	__module_get(THIS_MODULE);
	INIT_WORK(&blkif->free_work, xen_blkif_deferred_free);

	return blkif;
}

static int xen_blkif_map(struct xen_blkif_ring *ring, grant_ref_t *gref,
			 unsigned int nr_grefs, unsigned int evtchn)
{
	int err;
	struct xen_blkif *blkif = ring->blkif;
	const struct blkif_common_sring *sring_common;
	RING_IDX rsp_prod, req_prod;
	unsigned int size;

	/* Already connected through? */
	if (ring->irq)
		return 0;

	err = xenbus_map_ring_valloc(blkif->be->dev, gref, nr_grefs,
				     &ring->blk_ring);
	if (err < 0)
		return err;

	sring_common = (struct blkif_common_sring *)ring->blk_ring;
	rsp_prod = READ_ONCE(sring_common->rsp_prod);
	req_prod = READ_ONCE(sring_common->req_prod);

	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
	{
		struct blkif_sring *sring_native =
			(struct blkif_sring *)ring->blk_ring;

		BACK_RING_ATTACH(&ring->blk_rings.native, sring_native,
				 rsp_prod, XEN_PAGE_SIZE * nr_grefs);
		size = __RING_SIZE(sring_native, XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	case BLKIF_PROTOCOL_X86_32:
	{
		struct blkif_x86_32_sring *sring_x86_32 =
			(struct blkif_x86_32_sring *)ring->blk_ring;

		BACK_RING_ATTACH(&ring->blk_rings.x86_32, sring_x86_32,
				 rsp_prod, XEN_PAGE_SIZE * nr_grefs);
		size = __RING_SIZE(sring_x86_32, XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	case BLKIF_PROTOCOL_X86_64:
	{
		struct blkif_x86_64_sring *sring_x86_64 =
			(struct blkif_x86_64_sring *)ring->blk_ring;

		BACK_RING_ATTACH(&ring->blk_rings.x86_64, sring_x86_64,
				 rsp_prod, XEN_PAGE_SIZE * nr_grefs);
		size = __RING_SIZE(sring_x86_64, XEN_PAGE_SIZE * nr_grefs);
		break;
	}
	default:
		BUG();
	}

	err = -EIO;
	if (req_prod - rsp_prod > size)
		goto fail;

	err = bind_interdomain_evtchn_to_irqhandler_lateeoi(blkif->be->dev,
			evtchn, xen_blkif_be_int, 0, "blkif-backend", ring);
	if (err < 0)
		goto fail;
	ring->irq = err;

	return 0;

fail:
	xenbus_unmap_ring_vfree(blkif->be->dev, ring->blk_ring);
	ring->blk_rings.common.sring = NULL;
	return err;
}

static int xen_blkif_disconnect(struct xen_blkif *blkif)
{
	struct pending_req *req, *n;
	unsigned int j, r;
	bool busy = false;

	for (r = 0; r < blkif->nr_rings; r++) {
		struct xen_blkif_ring *ring = &blkif->rings[r];
		unsigned int i = 0;

		if (!ring->active)
			continue;

		if (ring->xenblkd) {
			kthread_stop(ring->xenblkd);
			ring->xenblkd = NULL;
			wake_up(&ring->shutdown_wq);
		}

		/* The above kthread_stop() guarantees that at this point we
		 * don't have any discard_io or other_io requests. So, checking
		 * for inflight IO is enough.
		 */
		if (atomic_read(&ring->inflight) > 0) {
			busy = true;
			continue;
		}

		if (ring->irq) {
			unbind_from_irqhandler(ring->irq, ring);
			ring->irq = 0;
		}

		if (ring->blk_rings.common.sring) {
			xenbus_unmap_ring_vfree(blkif->be->dev, ring->blk_ring);
			ring->blk_rings.common.sring = NULL;
		}

		/* Remove all persistent grants and the cache of ballooned pages. */
		xen_blkbk_free_caches(ring);

		/* Check that there is no request in use */
		list_for_each_entry_safe(req, n, &ring->pending_free, free_list) {
			list_del(&req->free_list);

			for (j = 0; j < MAX_INDIRECT_SEGMENTS; j++)
				kfree(req->segments[j]);

			for (j = 0; j < MAX_INDIRECT_PAGES; j++)
				kfree(req->indirect_pages[j]);

			kfree(req);
			i++;
		}

		BUG_ON(atomic_read(&ring->persistent_gnt_in_use) != 0);
		BUG_ON(!list_empty(&ring->persistent_purge_list));
		BUG_ON(!RB_EMPTY_ROOT(&ring->persistent_gnts));
		BUG_ON(ring->free_pages.num_pages != 0);
		BUG_ON(ring->persistent_gnt_c != 0);
		WARN_ON(i != (XEN_BLKIF_REQS_PER_PAGE * blkif->nr_ring_pages));
		ring->active = false;
	}
	if (busy)
		return -EBUSY;

	blkif->nr_ring_pages = 0;
	/*
	 * blkif->rings was allocated in connect_ring, so we should free it in
	 * here.
	 */
	kfree(blkif->rings);
	blkif->rings = NULL;
	blkif->nr_rings = 0;

	return 0;
}

static void xen_blkif_free(struct xen_blkif *blkif)
{
	WARN_ON(xen_blkif_disconnect(blkif));
	xen_vbd_free(&blkif->vbd);
	kfree(blkif->be->mode);
	kfree(blkif->be);

	/* Make sure everything is drained before shutting down */
	kmem_cache_free(xen_blkif_cachep, blkif);
	module_put(THIS_MODULE);
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

void xen_blkif_interface_fini(void)
{
	kmem_cache_destroy(xen_blkif_cachep);
	xen_blkif_cachep = NULL;
}

/*
 *  sysfs interface for VBD I/O requests
 */

#define VBD_SHOW_ALLRING(name, format)					\
	static ssize_t show_##name(struct device *_dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
	{								\
		struct xenbus_device *dev = to_xenbus_device(_dev);	\
		struct backend_info *be = dev_get_drvdata(&dev->dev);	\
		struct xen_blkif *blkif = be->blkif;			\
		unsigned int i;						\
		unsigned long long result = 0;				\
									\
		if (!blkif->rings)				\
			goto out;					\
									\
		for (i = 0; i < blkif->nr_rings; i++) {		\
			struct xen_blkif_ring *ring = &blkif->rings[i];	\
									\
			result += ring->st_##name;			\
		}							\
									\
out:									\
		return sprintf(buf, format, result);			\
	}								\
	static DEVICE_ATTR(name, 0444, show_##name, NULL)

VBD_SHOW_ALLRING(oo_req,  "%llu\n");
VBD_SHOW_ALLRING(rd_req,  "%llu\n");
VBD_SHOW_ALLRING(wr_req,  "%llu\n");
VBD_SHOW_ALLRING(f_req,  "%llu\n");
VBD_SHOW_ALLRING(ds_req,  "%llu\n");
VBD_SHOW_ALLRING(rd_sect, "%llu\n");
VBD_SHOW_ALLRING(wr_sect, "%llu\n");

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

static const struct attribute_group xen_vbdstat_group = {
	.name = "statistics",
	.attrs = xen_vbdstat_attrs,
};

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
	static DEVICE_ATTR(name, 0444, show_##name, NULL)

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

/* Enable the persistent grants feature. */
static bool feature_persistent = true;
module_param(feature_persistent, bool, 0644);
MODULE_PARM_DESC(feature_persistent,
		"Enables the persistent grants feature");

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
	if (q && test_bit(QUEUE_FLAG_WC, &q->queue_flags))
		vbd->flush_support = true;

	if (q && blk_queue_secure_erase(q))
		vbd->discard_secure = true;

	vbd->feature_gnt_persistent = feature_persistent;

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

		/* Put the reference we set in xen_blkif_alloc(). */
		xen_blkif_put(be->blkif);
	}

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
	int state = 0;
	struct block_device *bdev = be->blkif->vbd.bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	if (!xenbus_read_unsigned(dev->nodename, "discard-enable", 1))
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

	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "feature-max-indirect-segments", "%u",
			    MAX_INDIRECT_SEGMENTS);
	if (err)
		dev_warn(&dev->dev,
			 "writing %s/feature-max-indirect-segments (%d)",
			 dev->nodename, err);

	/* Multi-queue: advertise how many queues are supported by us.*/
	err = xenbus_printf(XBT_NIL, dev->nodename,
			    "multi-queue-max-queues", "%u", xenblk_max_queues);
	if (err)
		pr_warn("Error writing multi-queue-max-queues\n");

	/* setup back pointer */
	be->blkif->be = be;

	err = xenbus_watch_pathfmt(dev, &be->backend_watch, NULL,
				   backend_changed,
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
			    const char *path, const char *token)
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
	if (err) {
		kfree(be->mode);
		be->mode = NULL;
		return;
	}

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
		if (err) {
			/*
			 * Clean up so that memory resources can be used by
			 * other devices. connect_ring reported already error.
			 */
			xen_blkif_disconnect(be->blkif);
			break;
		}
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
		fallthrough;
		/* if not online */
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

/* Once a memory pressure is detected, squeeze free page pools for a while. */
static unsigned int buffer_squeeze_duration_ms = 10;
module_param_named(buffer_squeeze_duration_ms,
		buffer_squeeze_duration_ms, int, 0644);
MODULE_PARM_DESC(buffer_squeeze_duration_ms,
"Duration in ms to squeeze pages buffer when a memory pressure is detected");

/*
 * Callback received when the memory pressure is detected.
 */
static void reclaim_memory(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	if (!be)
		return;
	be->blkif->buffer_squeeze_end = jiffies +
		msecs_to_jiffies(buffer_squeeze_duration_ms);
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

	err = xenbus_printf(xbt, dev->nodename, "feature-persistent", "%u",
			be->blkif->vbd.feature_gnt_persistent);
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/feature-persistent",
				 dev->nodename);
		goto abort;
	}

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

/*
 * Each ring may have multi pages, depends on "ring-page-order".
 */
static int read_per_ring_refs(struct xen_blkif_ring *ring, const char *dir)
{
	unsigned int ring_ref[XENBUS_MAX_RING_GRANTS];
	struct pending_req *req, *n;
	int err, i, j;
	struct xen_blkif *blkif = ring->blkif;
	struct xenbus_device *dev = blkif->be->dev;
	unsigned int nr_grefs, evtchn;

	err = xenbus_scanf(XBT_NIL, dir, "event-channel", "%u",
			  &evtchn);
	if (err != 1) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err, "reading %s/event-channel", dir);
		return err;
	}

	nr_grefs = blkif->nr_ring_pages;

	if (unlikely(!nr_grefs)) {
		WARN_ON(true);
		return -EINVAL;
	}

	for (i = 0; i < nr_grefs; i++) {
		char ring_ref_name[RINGREF_NAME_LEN];

		snprintf(ring_ref_name, RINGREF_NAME_LEN, "ring-ref%u", i);
		err = xenbus_scanf(XBT_NIL, dir, ring_ref_name,
				   "%u", &ring_ref[i]);

		if (err != 1) {
			if (nr_grefs == 1)
				break;

			err = -EINVAL;
			xenbus_dev_fatal(dev, err, "reading %s/%s",
					 dir, ring_ref_name);
			return err;
		}
	}

	if (err != 1) {
		WARN_ON(nr_grefs != 1);

		err = xenbus_scanf(XBT_NIL, dir, "ring-ref", "%u",
				   &ring_ref[0]);
		if (err != 1) {
			err = -EINVAL;
			xenbus_dev_fatal(dev, err, "reading %s/ring-ref", dir);
			return err;
		}
	}

	err = -ENOMEM;
	for (i = 0; i < nr_grefs * XEN_BLKIF_REQS_PER_PAGE; i++) {
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req)
			goto fail;
		list_add_tail(&req->free_list, &ring->pending_free);
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
	err = xen_blkif_map(ring, ring_ref, nr_grefs, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref port %u", evtchn);
		goto fail;
	}

	return 0;

fail:
	list_for_each_entry_safe(req, n, &ring->pending_free, free_list) {
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
	return err;
}

static int connect_ring(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	struct xen_blkif *blkif = be->blkif;
	char protocol[64] = "";
	int err, i;
	char *xspath;
	size_t xspathsize;
	const size_t xenstore_path_ext_size = 11; /* sufficient for "/queue-NNN" */
	unsigned int requested_num_queues = 0;
	unsigned int ring_page_order;

	pr_debug("%s %s\n", __func__, dev->otherend);

	blkif->blk_protocol = BLKIF_PROTOCOL_DEFAULT;
	err = xenbus_scanf(XBT_NIL, dev->otherend, "protocol",
			   "%63s", protocol);
	if (err <= 0)
		strcpy(protocol, "unspecified, assuming default");
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_NATIVE))
		blkif->blk_protocol = BLKIF_PROTOCOL_NATIVE;
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_X86_32))
		blkif->blk_protocol = BLKIF_PROTOCOL_X86_32;
	else if (0 == strcmp(protocol, XEN_IO_PROTO_ABI_X86_64))
		blkif->blk_protocol = BLKIF_PROTOCOL_X86_64;
	else {
		xenbus_dev_fatal(dev, err, "unknown fe protocol %s", protocol);
		return -ENOSYS;
	}
	if (blkif->vbd.feature_gnt_persistent)
		blkif->vbd.feature_gnt_persistent =
			xenbus_read_unsigned(dev->otherend,
					"feature-persistent", 0);

	blkif->vbd.overflow_max_grants = 0;

	/*
	 * Read the number of hardware queues from frontend.
	 */
	requested_num_queues = xenbus_read_unsigned(dev->otherend,
						    "multi-queue-num-queues",
						    1);
	if (requested_num_queues > xenblk_max_queues
	    || requested_num_queues == 0) {
		/* Buggy or malicious guest. */
		xenbus_dev_fatal(dev, err,
				"guest requested %u queues, exceeding the maximum of %u.",
				requested_num_queues, xenblk_max_queues);
		return -ENOSYS;
	}
	blkif->nr_rings = requested_num_queues;
	if (xen_blkif_alloc_rings(blkif))
		return -ENOMEM;

	pr_info("%s: using %d queues, protocol %d (%s) %s\n", dev->nodename,
		 blkif->nr_rings, blkif->blk_protocol, protocol,
		 blkif->vbd.feature_gnt_persistent ? "persistent grants" : "");

	ring_page_order = xenbus_read_unsigned(dev->otherend,
					       "ring-page-order", 0);

	if (ring_page_order > xen_blkif_max_ring_order) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err,
				 "requested ring page order %d exceed max:%d",
				 ring_page_order,
				 xen_blkif_max_ring_order);
		return err;
	}

	blkif->nr_ring_pages = 1 << ring_page_order;

	if (blkif->nr_rings == 1)
		return read_per_ring_refs(&blkif->rings[0], dev->otherend);
	else {
		xspathsize = strlen(dev->otherend) + xenstore_path_ext_size;
		xspath = kmalloc(xspathsize, GFP_KERNEL);
		if (!xspath) {
			xenbus_dev_fatal(dev, -ENOMEM, "reading ring references");
			return -ENOMEM;
		}

		for (i = 0; i < blkif->nr_rings; i++) {
			memset(xspath, 0, xspathsize);
			snprintf(xspath, xspathsize, "%s/queue-%u", dev->otherend, i);
			err = read_per_ring_refs(&blkif->rings[i], xspath);
			if (err) {
				kfree(xspath);
				return err;
			}
		}
		kfree(xspath);
	}
	return 0;
}

static const struct xenbus_device_id xen_blkbk_ids[] = {
	{ "vbd" },
	{ "" }
};

static struct xenbus_driver xen_blkbk_driver = {
	.ids  = xen_blkbk_ids,
	.probe = xen_blkbk_probe,
	.remove = xen_blkbk_remove,
	.otherend_changed = frontend_changed,
	.allow_rebind = true,
	.reclaim_memory = reclaim_memory,
};

int xen_blkif_xenbus_init(void)
{
	return xenbus_register_backend(&xen_blkbk_driver);
}

void xen_blkif_xenbus_fini(void)
{
	xenbus_unregister_driver(&xen_blkbk_driver);
}
