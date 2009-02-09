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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdarg.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include "common.h"

#undef DPRINTK
#define DPRINTK(fmt, args...)				\
	pr_debug("blkback/xenbus (%s:%d) " fmt ".\n",	\
		 __FUNCTION__, __LINE__, ##args)

struct backend_info
{
	struct xenbus_device *dev;
	blkif_t *blkif;
	struct xenbus_watch backend_watch;
	unsigned major;
	unsigned minor;
	char *mode;
};

static void connect(struct backend_info *);
static int connect_ring(struct backend_info *);
static void backend_changed(struct xenbus_watch *, const char **,
			    unsigned int);

static int blkback_name(blkif_t *blkif, char *buf)
{
	char *devpath, *devname;
	struct xenbus_device *dev = blkif->be->dev;

	devpath = xenbus_read(XBT_NIL, dev->nodename, "dev", NULL);
	if (IS_ERR(devpath))
		return PTR_ERR(devpath);

	if ((devname = strstr(devpath, "/dev/")) != NULL)
		devname += strlen("/dev/");
	else
		devname  = devpath;

	snprintf(buf, TASK_COMM_LEN, "blkback.%d.%s", blkif->domid, devname);
	kfree(devpath);

	return 0;
}

static void update_blkif_status(blkif_t *blkif)
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

	blkif->xenblkd = kthread_run(blkif_schedule, blkif, name);
	if (IS_ERR(blkif->xenblkd)) {
		err = PTR_ERR(blkif->xenblkd);
		blkif->xenblkd = NULL;
		xenbus_dev_error(blkif->be->dev, err, "start xenblkd");
	}
}


/****************************************************************
 *  sysfs interface for VBD I/O requests
 */

#define VBD_SHOW(name, format, args...)					\
	static ssize_t show_##name(struct device *_dev,			\
				   struct device_attribute *attr,	\
				   char *buf)				\
	{								\
		struct xenbus_device *dev = to_xenbus_device(_dev);	\
		struct backend_info *be = dev->dev.driver_data;		\
									\
		return sprintf(buf, format, ##args);			\
	}								\
	static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

VBD_SHOW(oo_req,  "%d\n", be->blkif->st_oo_req);
VBD_SHOW(rd_req,  "%d\n", be->blkif->st_rd_req);
VBD_SHOW(wr_req,  "%d\n", be->blkif->st_wr_req);
VBD_SHOW(br_req,  "%d\n", be->blkif->st_br_req);
VBD_SHOW(rd_sect, "%d\n", be->blkif->st_rd_sect);
VBD_SHOW(wr_sect, "%d\n", be->blkif->st_wr_sect);

static struct attribute *vbdstat_attrs[] = {
	&dev_attr_oo_req.attr,
	&dev_attr_rd_req.attr,
	&dev_attr_wr_req.attr,
	&dev_attr_br_req.attr,
	&dev_attr_rd_sect.attr,
	&dev_attr_wr_sect.attr,
	NULL
};

static struct attribute_group vbdstat_group = {
	.name = "statistics",
	.attrs = vbdstat_attrs,
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

	error = sysfs_create_group(&dev->dev.kobj, &vbdstat_group);
	if (error)
		goto fail3;

	return 0;

fail3:	sysfs_remove_group(&dev->dev.kobj, &vbdstat_group);
fail2:	device_remove_file(&dev->dev, &dev_attr_mode);
fail1:	device_remove_file(&dev->dev, &dev_attr_physical_device);
	return error;
}

void xenvbd_sysfs_delif(struct xenbus_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &vbdstat_group);
	device_remove_file(&dev->dev, &dev_attr_mode);
	device_remove_file(&dev->dev, &dev_attr_physical_device);
}

static int blkback_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev->dev.driver_data;

	DPRINTK("");

	if (be->major || be->minor)
		xenvbd_sysfs_delif(dev);

	if (be->backend_watch.node) {
		unregister_xenbus_watch(&be->backend_watch);
		kfree(be->backend_watch.node);
		be->backend_watch.node = NULL;
	}

	if (be->blkif) {
		blkif_disconnect(be->blkif);
		vbd_free(&be->blkif->vbd);
		blkif_free(be->blkif);
		be->blkif = NULL;
	}

	kfree(be);
	dev->dev.driver_data = NULL;
	return 0;
}

int blkback_barrier(struct xenbus_transaction xbt,
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

/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures, and watch the store waiting for the hotplug scripts to tell us
 * the device's physical major and minor numbers.  Switch to InitWait.
 */
static int blkback_probe(struct xenbus_device *dev,
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
	dev->dev.driver_data = be;

	be->blkif = blkif_alloc(dev->otherend_id);
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
	blkback_remove(dev);
	return err;
}


/**
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
		/* Since this watch will fire once immediately after it is
		   registered, we expect this.  Ignore it, and wait for the
		   hotplug scripts. */
		return;
	}
	if (err != 2) {
		xenbus_dev_fatal(dev, err, "reading physical-device");
		return;
	}

	if ((be->major || be->minor) &&
	    ((be->major != major) || (be->minor != minor))) {
		printk(KERN_WARNING
		       "blkback: changing physical device (from %x:%x to "
		       "%x:%x) not supported.\n", be->major, be->minor,
		       major, minor);
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
		long handle = simple_strtoul(p, NULL, 0);

		be->major = major;
		be->minor = minor;

		err = vbd_create(be->blkif, handle, major, minor,
				 (NULL == strchr(be->mode, 'w')), cdrom);
		if (err) {
			be->major = be->minor = 0;
			xenbus_dev_fatal(dev, err, "creating vbd structure");
			return;
		}

		err = xenvbd_sysfs_addif(dev);
		if (err) {
			vbd_free(&be->blkif->vbd);
			be->major = be->minor = 0;
			xenbus_dev_fatal(dev, err, "creating sysfs entries");
			return;
		}

		/* We're potentially connected now */
		update_blkif_status(be->blkif);
	}
}


/**
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *dev,
			     enum xenbus_state frontend_state)
{
	struct backend_info *be = dev->dev.driver_data;
	int err;

	DPRINTK("%s", xenbus_strstate(frontend_state));

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (dev->state == XenbusStateClosed) {
			printk(KERN_INFO "%s: %s: prepare for reconnect\n",
			       __FUNCTION__, dev->nodename);
			xenbus_switch_state(dev, XenbusStateInitWait);
		}
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		/* Ensure we connect even when two watches fire in
		   close successsion and we miss the intermediate value
		   of frontend_state. */
		if (dev->state == XenbusStateConnected)
			break;

		err = connect_ring(be);
		if (err)
			break;
		update_blkif_status(be->blkif);
		break;

	case XenbusStateClosing:
		blkif_disconnect(be->blkif);
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xenbus_switch_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}


/* ** Connection ** */


/**
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

	err = blkback_barrier(xbt, be, 1);
	if (err)
		goto abort;

	err = xenbus_printf(xbt, dev->nodename, "sectors", "%llu",
			    vbd_size(&be->blkif->vbd));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/sectors",
				 dev->nodename);
		goto abort;
	}

	/* FIXME: use a typename instead */
	err = xenbus_printf(xbt, dev->nodename, "info", "%u",
			    vbd_info(&be->blkif->vbd));
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/info",
				 dev->nodename);
		goto abort;
	}
	err = xenbus_printf(xbt, dev->nodename, "sector-size", "%lu",
			    vbd_secsize(&be->blkif->vbd));
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
		xenbus_dev_fatal(dev, err, "switching to Connected state",
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

	err = xenbus_gather(XBT_NIL, dev->otherend, "ring-ref", "%lu", &ring_ref,
			    "event-channel", "%u", &evtchn, NULL);
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
	printk(KERN_INFO
	       "blkback: ring-ref %ld, event-channel %d, protocol %d (%s)\n",
	       ring_ref, evtchn, be->blkif->blk_protocol, protocol);

	/* Map the shared frame, irq etc. */
	err = blkif_map(be->blkif, ring_ref, evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err, "mapping ring-ref %lu port %u",
				 ring_ref, evtchn);
		return err;
	}

	return 0;
}


/* ** Driver Registration ** */


static const struct xenbus_device_id blkback_ids[] = {
	{ "vbd" },
	{ "" }
};


static struct xenbus_driver blkback = {
	.name = "vbd",
	.owner = THIS_MODULE,
	.ids = blkback_ids,
	.probe = blkback_probe,
	.remove = blkback_remove,
	.otherend_changed = frontend_changed
};


void blkif_xenbus_init(void)
{
	/* XXX must_check */
	(void)xenbus_register_backend(&blkback);
}
