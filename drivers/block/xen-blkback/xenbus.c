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

#include <stdarg.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include "common.h"

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

struct xenbus_device *xen_blkbk_xenbus(struct backend_info *be)
{
	return be->dev;
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

	snprintf(buf, TASK_COMM_LEN, "blkback.%d.%s", blkif->domid, devname);
	kfree(devpath);

	return 0;
}

static void xen_update_blkif_status(struct xen_blkif *blkif)
{
	int err;
	char name[TASK_COMM_LEN];

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

	blkif->xenblkd = kthread_run(xen_blkif_schedule, blkif, name);
	if (IS_ERR(blkif->xenblkd)) {
		err = PTR_ERR(blkif->xenblkd);
		blkif->xenblkd = NULL;
		xenbus_dev_error(blkif->be->dev, err, "start xenblkd");
	}
}

static struct xen_blkif *xen_blkif_alloc(domid_t domid)
{
	struct xen_blkif *blkif;

	blkif = kmem_cache_alloc(xen_blkif_cachep, GFP_KERNEL);
	if (!blkif)
		return ERR_PTR(-ENOMEM);

	memset(blkif, 0, sizeof(*blkif));
	blkif->domid = domid;
	spin_lock_init(&blkif->blk_ring_lock);
	atomic_set(&blkif->refcnt, 1);
	init_waitqueue_head(&blkif->wq);
	init_completion(&blkif->drain_complete);
	atomic_set(&blkif->drain, 0);
	blkif->st_print = jiffies;
	init_waitqueue_head(&blkif->waiting_to_free);

	return blkif;
}

static int xen_blkif_map(struct xen_blkif *blkif, unsigned long shared_page,
			 unsigned int evtchn)
{
	int err;

	/* Already connected through? */
	if (blkif->irq)
		return 0;

	err = xenbus_map_ring_valloc(blkif->be->dev, shared_page, &blkif->blk_ring);
	if (err < 0)
		return err;

	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
	{
		struct blkif_sring *sring;
		sring = (struct blkif_sring *)blkif->blk_ring;
		BACK_RING_INIT(&blkif->blk_rings.native, sring, PAGE_SIZE);
		break;
	}
	case BLKIF_PROTOCOL_X86_32:
	{
		struct blkif_x86_32_sring *sring_x86_32;
		sring_x86_32 = (struct blkif_x86_32_sring *)blkif->blk_ring;
		BACK_RING_INIT(&blkif->blk_rings.x86_32, sring_x86_32, PAGE_SIZE);
		break;
	}
	case BLKIF_PROTOCOL_X86_64:
	{
		struct blkif_x86_64_sring *sring_x86_64;
		sring_x86_64 = (struct blkif_x86_64_sring *)blkif->blk_ring;
		BACK_RING_INIT(&blkif->blk_rings.x86_64, sring_x86_64, PAGE_SIZE);
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

static void xen_blkif_disconnect(struct xen_blkif *blkif)
{
	if (blkif->xenblkd) {
		kthread_stop(blkif->xenblkd);
		blkif->xenblkd = NULL;
	}

	atomic_dec(&blkif->refcnt);
	wait_event(blkif->waiting_to_free, atomic_read(&blkif->refcnt) == 0);
	atomic_inc(&blkif->refcnt);

	if (blkif->irq) {
		unbind_from_irqhandler(blkif->irq, blkif);
		blkif->irq = 0;
	}

	if (blkif->blk_rings.common.sring) {
		xenbus_unmap_ring_vfree(blkif->be->dev, blkif->blk_ring);
		blkif->blk_rings.common.sring = NULL;
	}
}

void xen_blkif_free(struct xen_blkif *blkif)
{
	if (!atomic_dec_and_test(&blkif->refcnt))
		BUG();
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

VBD_SHOW(oo_req,  "%d\n", be->blkif->st_oo_req);
VBD_SHOW(rd_req,  "%d\n", be->blkif->st_rd_req);
VBD_SHOW(wr_req,  "%d\n", be->blkif->st_wr_req);
VBD_SHOW(f_req,  "%d\n", be->blkif->st_f_req);
VBD_SHOW(ds_req,  "%d\n", be->blkif->st_ds_req);
VBD_SHOW(rd_sect, "%d\n", be->blkif->st_rd_sect);
VBD_SHOW(wr_sect, "%d\n", be->blkif->st_wr_sect);

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

int xenvbd_sysfs_addif(struct xenbus_device *dev)
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

void xenvbd_sysfs_delif(struct xenbus_device *dev)
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
		DPRINTK("xen_vbd_create: device %08x could not be opened.\n",
			vbd->pdevice);
		return -ENOENT;
	}

	vbd->bdev = bdev;
	if (vbd->bdev->bd_disk == NULL) {
		DPRINTK("xen_vbd_create: device %08x doesn't exist.\n",
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

	DPRINTK("Successful creation of handle=%04x (dom=%u)\n",
		handle, blkif->domid);
	return 0;
}
static int xen_blkbk_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	DPRINTK("");

	if (be->major || be->minor)
		xenvbd_sysfs_delif(dev);

	if (be->backend_watch.node) {
		unregister_xenbus_watch(&be->backend_watch);
		kfree(be->backend_watch.node);
		be->backend_watch.node = NULL;
	}

	if (be->blkif) {
		xen_blkif_disconnect(be->blkif);
		xen_vbd_free(&be->blkif->vbd);
		xen_blkif_free(be->blkif);
		be->blkif = NULL;
	}

	kfree(be);
	dev_set_drvdata(&dev->dev, NULL);
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
		xenbus_dev_fatal(dev, err, "writing feature-flush-cache");

	return err;
}

int xen_blkbk_discard(struct xenbus_transaction xbt, struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	struct xen_blkif *blkif = be->blkif;
	char *type;
	int err;
	int state = 0;

	type = xenbus_read(XBT_NIL, dev->nodename, "type", NULL);
	if (!IS_ERR(type)) {
		if (strncmp(type, "file", 4) == 0) {
			state = 1;
			blkif->blk_backend_type = BLKIF_BACKEND_FILE;
		}
		if (strncmp(type, "phy", 3) == 0) {
			struct block_device *bdev = be->blkif->vbd.bdev;
			struct request_queue *q = bdev_get_queue(bdev);
			if (blk_queue_discard(q)) {
				err = xenbus_printf(xbt, dev->nodename,
					"discard-granularity", "%u",
					q->limits.discard_granularity);
				if (err) {
					xenbus_dev_fatal(dev, err,
						"writing discard-granularity");
					goto kfree;
				}
				err = xenbus_printf(xbt, dev->nodename,
					"discard-alignment", "%u",
					q->limits.discard_alignment);
				if (err) {
					xenbus_dev_fatal(dev, err,
						"writing discard-alignment");
					goto kfree;
				}
				state = 1;
				blkif->blk_backend_type = BLKIF_BACKEND_PHY;
			}
			/* Optional. */
			err = xenbus_printf(xbt, dev->nodename,
				"discard-secure", "%d",
				blkif->vbd.discard_secure);
			if (err) {
				xenbus_dev_fatal(dev, err,
					"writting discard-secure");
				goto kfree;
			}
		}
	} else {
		err = PTR_ERR(type);
		xenbus_dev_fatal(dev, err, "reading type");
		goto out;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-discard",
			    "%d", state);
	if (err)
		xenbus_dev_fatal(dev, err, "writing feature-discard");
kfree:
	kfree(type);
out:
	return err;
}
int xen_blkbk_barrier(struct xenbus_transaction xbt,
		      struct backend_info *be, int state)
{
	struct xenbus_device *dev = be->dev;
	int err;

	err = xenbus_printf(xbt, dev->nodename, "feature-barrier",
			    "%d", state);
	if (err)
		xenbus_dev_fatal(dev, err, "writing feature-barrier");

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

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;

fail:
	DPRINTK("failed");
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
	char *device_type;

	DPRINTK("");

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

	if ((be->major || be->minor) &&
	    ((be->major != major) || (be->minor != minor))) {
		pr_warn(DRV_PFX "changing physical device (from %x:%x to %x:%x) not supported.\n",
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

	if (be->major == 0 && be->minor == 0) {
		/* Front end dir is a number, which is used as the handle. */

		char *p = strrchr(dev->otherend, '/') + 1;
		long handle;
		err = strict_strtoul(p, 0, &handle);
		if (err)
			return;

		be->major = major;
		be->minor = minor;

		err = xen_vbd_create(be->blkif, handle, major, minor,
				 (NULL == strchr(be->mode, 'w')), cdrom);
		if (err) {
			be->major = 0;
			be->minor = 0;
			xenbus_dev_fatal(dev, err, "creating vbd structure");
			return;
		}

		err = xenvbd_sysfs_addif(dev);
		if (err) {
			xen_vbd_free(&be->blkif->vbd);
			be->major = 0;
			be->minor = 0;
			xenbus_dev_fatal(dev, err, "creating sysfs entries");
			return;
		}

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

	DPRINTK("%s", xenbus_strstate(frontend_state));

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			pr_info(DRV_PFX "%s: prepare for reconnect\n",
				dev->nodename);
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
		xen_blkif_disconnect(be->blkif);

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

	DPRINTK("%s", dev->otherend);

	/* Supply the information about the device the frontend needs */
again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		return;
	}

	err = xen_blkbk_flush_diskcache(xbt, be, be->blkif->vbd.flush_support);
	if (err)
		goto abort;

	err = xen_blkbk_discard(xbt, be);

	/* If we can't advertise it is OK. */
	err = xen_blkbk_barrier(xbt, be, be->blkif->vbd.flush_support);

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
	unsigned long ring_ref;
	unsigned int evtchn;
	char protocol[64] = "";
	int err;

	DPRINTK("%s", dev->otherend);

	err = xenbus_gather(XBT_NIL, dev->otherend, "ring-ref", "%lu",
			    &ring_ref, "event-channel", "%u", &evtchn, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				 "reading %s/ring-ref and event-channel",
				 dev->otherend);
		return err;
	}

	be->blkif->blk_protocol = BLKIF_PROTOCOL_NATIVE;
	err = xenbus_gather(XBT_NIL, dev->otherend, "protocol",
			    "%63s", protocol, NULL);
	if (err)
		strcpy(protocol, "unspecified, assuming native");
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
	pr_info(DRV_PFX "ring-ref %ld, event-channel %d, protocol %d (%s)\n",
		ring_ref, evtchn, be->blkif->blk_protocol, protocol);

	/* Map the shared frame, irq etc. */
	err = xen_blkif_map(be->blkif, ring_ref, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref %lu port %u",
				 ring_ref, evtchn);
		return err;
	}

	return 0;
}


/* ** Driver Registration ** */


static const struct xenbus_device_id xen_blkbk_ids[] = {
	{ "vbd" },
	{ "" }
};


static DEFINE_XENBUS_DRIVER(xen_blkbk, ,
	.probe = xen_blkbk_probe,
	.remove = xen_blkbk_remove,
	.otherend_changed = frontend_changed
);


int xen_blkif_xenbus_init(void)
{
	return xenbus_register_backend(&xen_blkbk_driver);
}
