/*
 *  drivers/s390/cio/device.c
 *  bus driver for ccw devices
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/timer.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/param.h>		/* HZ */
#include <asm/cmb.h>

#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "io_sch.h"

static struct timer_list recovery_timer;
static DEFINE_SPINLOCK(recovery_lock);
static int recovery_phase;
static const unsigned long recovery_delay[] = { 3, 30, 300 };

/******************* bus type handling ***********************/

/* The Linux driver model distinguishes between a bus type and
 * the bus itself. Of course we only have one channel
 * subsystem driver and one channel system per machine, but
 * we still use the abstraction. T.R. says it's a good idea. */
static int
ccw_bus_match (struct device * dev, struct device_driver * drv)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_driver *cdrv = to_ccwdrv(drv);
	const struct ccw_device_id *ids = cdrv->ids, *found;

	if (!ids)
		return 0;

	found = ccw_device_id_match(ids, &cdev->id);
	if (!found)
		return 0;

	cdev->id.driver_info = found->driver_info;

	return 1;
}

/* Store modalias string delimited by prefix/suffix string into buffer with
 * specified size. Return length of resulting string (excluding trailing '\0')
 * even if string doesn't fit buffer (snprintf semantics). */
static int snprint_alias(char *buf, size_t size,
			 struct ccw_device_id *id, const char *suffix)
{
	int len;

	len = snprintf(buf, size, "ccw:t%04Xm%02X", id->cu_type, id->cu_model);
	if (len > size)
		return len;
	buf += len;
	size -= len;

	if (id->dev_type != 0)
		len += snprintf(buf, size, "dt%04Xdm%02X%s", id->dev_type,
				id->dev_model, suffix);
	else
		len += snprintf(buf, size, "dtdm%s", suffix);

	return len;
}

/* Set up environment variables for ccw device uevent. Return 0 on success,
 * non-zero otherwise. */
static int ccw_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);
	int ret;
	char modalias_buf[30];

	/* CU_TYPE= */
	ret = add_uevent_var(env, "CU_TYPE=%04X", id->cu_type);
	if (ret)
		return ret;

	/* CU_MODEL= */
	ret = add_uevent_var(env, "CU_MODEL=%02X", id->cu_model);
	if (ret)
		return ret;

	/* The next two can be zero, that's ok for us */
	/* DEV_TYPE= */
	ret = add_uevent_var(env, "DEV_TYPE=%04X", id->dev_type);
	if (ret)
		return ret;

	/* DEV_MODEL= */
	ret = add_uevent_var(env, "DEV_MODEL=%02X", id->dev_model);
	if (ret)
		return ret;

	/* MODALIAS=  */
	snprint_alias(modalias_buf, sizeof(modalias_buf), id, "");
	ret = add_uevent_var(env, "MODALIAS=%s", modalias_buf);
	return ret;
}

struct bus_type ccw_bus_type;

static void io_subchannel_irq(struct subchannel *);
static int io_subchannel_probe(struct subchannel *);
static int io_subchannel_remove(struct subchannel *);
static int io_subchannel_notify(struct subchannel *, int);
static void io_subchannel_verify(struct subchannel *);
static void io_subchannel_ioterm(struct subchannel *);
static void io_subchannel_shutdown(struct subchannel *);

static struct css_driver io_subchannel_driver = {
	.owner = THIS_MODULE,
	.subchannel_type = SUBCHANNEL_TYPE_IO,
	.name = "io_subchannel",
	.irq = io_subchannel_irq,
	.notify = io_subchannel_notify,
	.verify = io_subchannel_verify,
	.termination = io_subchannel_ioterm,
	.probe = io_subchannel_probe,
	.remove = io_subchannel_remove,
	.shutdown = io_subchannel_shutdown,
};

struct workqueue_struct *ccw_device_work;
struct workqueue_struct *ccw_device_notify_work;
wait_queue_head_t ccw_device_init_wq;
atomic_t ccw_device_init_count;

static void recovery_func(unsigned long data);

static int __init
init_ccw_bus_type (void)
{
	int ret;

	init_waitqueue_head(&ccw_device_init_wq);
	atomic_set(&ccw_device_init_count, 0);
	setup_timer(&recovery_timer, recovery_func, 0);

	ccw_device_work = create_singlethread_workqueue("cio");
	if (!ccw_device_work)
		return -ENOMEM; /* FIXME: better errno ? */
	ccw_device_notify_work = create_singlethread_workqueue("cio_notify");
	if (!ccw_device_notify_work) {
		ret = -ENOMEM; /* FIXME: better errno ? */
		goto out_err;
	}
	slow_path_wq = create_singlethread_workqueue("kslowcrw");
	if (!slow_path_wq) {
		ret = -ENOMEM; /* FIXME: better errno ? */
		goto out_err;
	}
	if ((ret = bus_register (&ccw_bus_type)))
		goto out_err;

	ret = css_driver_register(&io_subchannel_driver);
	if (ret)
		goto out_err;

	wait_event(ccw_device_init_wq,
		   atomic_read(&ccw_device_init_count) == 0);
	flush_workqueue(ccw_device_work);
	return 0;
out_err:
	if (ccw_device_work)
		destroy_workqueue(ccw_device_work);
	if (ccw_device_notify_work)
		destroy_workqueue(ccw_device_notify_work);
	if (slow_path_wq)
		destroy_workqueue(slow_path_wq);
	return ret;
}

static void __exit
cleanup_ccw_bus_type (void)
{
	css_driver_unregister(&io_subchannel_driver);
	bus_unregister(&ccw_bus_type);
	destroy_workqueue(ccw_device_notify_work);
	destroy_workqueue(ccw_device_work);
}

subsys_initcall(init_ccw_bus_type);
module_exit(cleanup_ccw_bus_type);

/************************ device handling **************************/

/*
 * A ccw_device has some interfaces in sysfs in addition to the
 * standard ones.
 * The following entries are designed to export the information which
 * resided in 2.4 in /proc/subchannels. Subchannel and device number
 * are obvious, so they don't have an entry :)
 * TODO: Split chpids and pimpampom up? Where is "in use" in the tree?
 */
static ssize_t
chpids_show (struct device * dev, struct device_attribute *attr, char * buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct chsc_ssd_info *ssd = &sch->ssd_info;
	ssize_t ret = 0;
	int chp;
	int mask;

	for (chp = 0; chp < 8; chp++) {
		mask = 0x80 >> chp;
		if (ssd->path_mask & mask)
			ret += sprintf(buf + ret, "%02x ", ssd->chpid[chp].id);
		else
			ret += sprintf(buf + ret, "00 ");
	}
	ret += sprintf (buf+ret, "\n");
	return min((ssize_t)PAGE_SIZE, ret);
}

static ssize_t
pimpampom_show (struct device * dev, struct device_attribute *attr, char * buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct pmcw *pmcw = &sch->schib.pmcw;

	return sprintf (buf, "%02x %02x %02x\n",
			pmcw->pim, pmcw->pam, pmcw->pom);
}

static ssize_t
devtype_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);

	if (id->dev_type != 0)
		return sprintf(buf, "%04x/%02x\n",
				id->dev_type, id->dev_model);
	else
		return sprintf(buf, "n/a\n");
}

static ssize_t
cutype_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);

	return sprintf(buf, "%04x/%02x\n",
		       id->cu_type, id->cu_model);
}

static ssize_t
modalias_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);
	int len;

	len = snprint_alias(buf, PAGE_SIZE, id, "\n");

	return len > PAGE_SIZE ? PAGE_SIZE : len;
}

static ssize_t
online_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);

	return sprintf(buf, cdev->online ? "1\n" : "0\n");
}

int ccw_device_is_orphan(struct ccw_device *cdev)
{
	return sch_is_pseudo_sch(to_subchannel(cdev->dev.parent));
}

static void ccw_device_unregister(struct ccw_device *cdev)
{
	if (test_and_clear_bit(1, &cdev->private->registered))
		device_del(&cdev->dev);
}

static void ccw_device_remove_orphan_cb(struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);

	ccw_device_unregister(cdev);
	put_device(&cdev->dev);
}

static void ccw_device_remove_sch_cb(struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	css_sch_device_unregister(sch);
	/* Reset intparm to zeroes. */
	sch->schib.pmcw.intparm = 0;
	cio_modify(sch);
	put_device(&sch->dev);
}

static void
ccw_device_remove_disconnected(struct ccw_device *cdev)
{
	unsigned long flags;
	int rc;

	/*
	 * Forced offline in disconnected state means
	 * 'throw away device'.
	 */
	if (ccw_device_is_orphan(cdev)) {
		/*
		 * Deregister ccw device.
		 * Unfortunately, we cannot do this directly from the
		 * attribute method.
		 */
		spin_lock_irqsave(cdev->ccwlock, flags);
		cdev->private->state = DEV_STATE_NOT_OPER;
		spin_unlock_irqrestore(cdev->ccwlock, flags);
		rc = device_schedule_callback(&cdev->dev,
					      ccw_device_remove_orphan_cb);
		if (rc)
			CIO_MSG_EVENT(2, "Couldn't unregister orphan "
				      "0.%x.%04x\n",
				      cdev->private->dev_id.ssid,
				      cdev->private->dev_id.devno);
		return;
	}
	/* Deregister subchannel, which will kill the ccw device. */
	rc = device_schedule_callback(cdev->dev.parent,
				      ccw_device_remove_sch_cb);
	if (rc)
		CIO_MSG_EVENT(2, "Couldn't unregister disconnected device "
			      "0.%x.%04x\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
}

/**
 * ccw_device_set_offline() - disable a ccw device for I/O
 * @cdev: target ccw device
 *
 * This function calls the driver's set_offline() function for @cdev, if
 * given, and then disables @cdev.
 * Returns:
 *   %0 on success and a negative error value on failure.
 * Context:
 *  enabled, ccw device lock not held
 */
int ccw_device_set_offline(struct ccw_device *cdev)
{
	int ret;

	if (!cdev)
		return -ENODEV;
	if (!cdev->online || !cdev->drv)
		return -EINVAL;

	if (cdev->drv->set_offline) {
		ret = cdev->drv->set_offline(cdev);
		if (ret != 0)
			return ret;
	}
	cdev->online = 0;
	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_offline(cdev);
	if (ret == -ENODEV) {
		if (cdev->private->state != DEV_STATE_NOT_OPER) {
			cdev->private->state = DEV_STATE_OFFLINE;
			dev_fsm_event(cdev, DEV_EVENT_NOTOPER);
		}
		spin_unlock_irq(cdev->ccwlock);
		return ret;
	}
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else {
		CIO_MSG_EVENT(2, "ccw_device_offline returned %d, "
			      "device 0.%x.%04x\n",
			      ret, cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
		cdev->online = 1;
	}
 	return ret;
}

/**
 * ccw_device_set_online() - enable a ccw device for I/O
 * @cdev: target ccw device
 *
 * This function first enables @cdev and then calls the driver's set_online()
 * function for @cdev, if given. If set_online() returns an error, @cdev is
 * disabled again.
 * Returns:
 *   %0 on success and a negative error value on failure.
 * Context:
 *  enabled, ccw device lock not held
 */
int ccw_device_set_online(struct ccw_device *cdev)
{
	int ret;

	if (!cdev)
		return -ENODEV;
	if (cdev->online || !cdev->drv)
		return -EINVAL;

	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_online(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else {
		CIO_MSG_EVENT(2, "ccw_device_online returned %d, "
			      "device 0.%x.%04x\n",
			      ret, cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
		return ret;
	}
	if (cdev->private->state != DEV_STATE_ONLINE)
		return -ENODEV;
	if (!cdev->drv->set_online || cdev->drv->set_online(cdev) == 0) {
		cdev->online = 1;
		return 0;
	}
	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_offline(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else
		CIO_MSG_EVENT(2, "ccw_device_offline returned %d, "
			      "device 0.%x.%04x\n",
			      ret, cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
	return (ret == 0) ? -ENODEV : ret;
}

static void online_store_handle_offline(struct ccw_device *cdev)
{
	if (cdev->private->state == DEV_STATE_DISCONNECTED)
		ccw_device_remove_disconnected(cdev);
	else if (cdev->drv && cdev->drv->set_offline)
		ccw_device_set_offline(cdev);
}

static int online_store_recog_and_online(struct ccw_device *cdev)
{
	int ret;

	/* Do device recognition, if needed. */
	if (cdev->id.cu_type == 0) {
		ret = ccw_device_recognition(cdev);
		if (ret) {
			CIO_MSG_EVENT(0, "Couldn't start recognition "
				      "for device 0.%x.%04x (ret=%d)\n",
				      cdev->private->dev_id.ssid,
				      cdev->private->dev_id.devno, ret);
			return ret;
		}
		wait_event(cdev->private->wait_q,
			   cdev->private->flags.recog_done);
	}
	if (cdev->drv && cdev->drv->set_online)
		ccw_device_set_online(cdev);
	return 0;
}
static void online_store_handle_online(struct ccw_device *cdev, int force)
{
	int ret;

	ret = online_store_recog_and_online(cdev);
	if (ret)
		return;
	if (force && cdev->private->state == DEV_STATE_BOXED) {
		ret = ccw_device_stlck(cdev);
		if (ret) {
			dev_warn(&cdev->dev,
				 "ccw_device_stlck returned %d!\n", ret);
			return;
		}
		if (cdev->id.cu_type == 0)
			cdev->private->state = DEV_STATE_NOT_OPER;
		online_store_recog_and_online(cdev);
	}

}

static ssize_t online_store (struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int force, ret;
	unsigned long i;

	if (atomic_cmpxchg(&cdev->private->onoff, 0, 1) != 0)
		return -EAGAIN;

	if (cdev->drv && !try_module_get(cdev->drv->owner)) {
		atomic_set(&cdev->private->onoff, 0);
		return -EINVAL;
	}
	if (!strncmp(buf, "force\n", count)) {
		force = 1;
		i = 1;
		ret = 0;
	} else {
		force = 0;
		ret = strict_strtoul(buf, 16, &i);
	}
	if (ret)
		goto out;
	switch (i) {
	case 0:
		online_store_handle_offline(cdev);
		ret = count;
		break;
	case 1:
		online_store_handle_online(cdev, force);
		ret = count;
		break;
	default:
		ret = -EINVAL;
	}
out:
	if (cdev->drv)
		module_put(cdev->drv->owner);
	atomic_set(&cdev->private->onoff, 0);
	return ret;
}

static ssize_t
available_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct subchannel *sch;

	if (ccw_device_is_orphan(cdev))
		return sprintf(buf, "no device\n");
	switch (cdev->private->state) {
	case DEV_STATE_BOXED:
		return sprintf(buf, "boxed\n");
	case DEV_STATE_DISCONNECTED:
	case DEV_STATE_DISCONNECTED_SENSE_ID:
	case DEV_STATE_NOT_OPER:
		sch = to_subchannel(dev->parent);
		if (!sch->lpm)
			return sprintf(buf, "no path\n");
		else
			return sprintf(buf, "no device\n");
	default:
		/* All other states considered fine. */
		return sprintf(buf, "good\n");
	}
}

static DEVICE_ATTR(chpids, 0444, chpids_show, NULL);
static DEVICE_ATTR(pimpampom, 0444, pimpampom_show, NULL);
static DEVICE_ATTR(devtype, 0444, devtype_show, NULL);
static DEVICE_ATTR(cutype, 0444, cutype_show, NULL);
static DEVICE_ATTR(modalias, 0444, modalias_show, NULL);
static DEVICE_ATTR(online, 0644, online_show, online_store);
static DEVICE_ATTR(availability, 0444, available_show, NULL);

static struct attribute * subch_attrs[] = {
	&dev_attr_chpids.attr,
	&dev_attr_pimpampom.attr,
	NULL,
};

static struct attribute_group subch_attr_group = {
	.attrs = subch_attrs,
};

struct attribute_group *subch_attr_groups[] = {
	&subch_attr_group,
	NULL,
};

static struct attribute * ccwdev_attrs[] = {
	&dev_attr_devtype.attr,
	&dev_attr_cutype.attr,
	&dev_attr_modalias.attr,
	&dev_attr_online.attr,
	&dev_attr_cmb_enable.attr,
	&dev_attr_availability.attr,
	NULL,
};

static struct attribute_group ccwdev_attr_group = {
	.attrs = ccwdev_attrs,
};

static struct attribute_group *ccwdev_attr_groups[] = {
	&ccwdev_attr_group,
	NULL,
};

/* this is a simple abstraction for device_register that sets the
 * correct bus type and adds the bus specific files */
static int ccw_device_register(struct ccw_device *cdev)
{
	struct device *dev = &cdev->dev;
	int ret;

	dev->bus = &ccw_bus_type;

	if ((ret = device_add(dev)))
		return ret;

	set_bit(1, &cdev->private->registered);
	return ret;
}

struct match_data {
	struct ccw_dev_id dev_id;
	struct ccw_device * sibling;
};

static int
match_devno(struct device * dev, void * data)
{
	struct match_data * d = data;
	struct ccw_device * cdev;

	cdev = to_ccwdev(dev);
	if ((cdev->private->state == DEV_STATE_DISCONNECTED) &&
	    !ccw_device_is_orphan(cdev) &&
	    ccw_dev_id_is_equal(&cdev->private->dev_id, &d->dev_id) &&
	    (cdev != d->sibling))
		return 1;
	return 0;
}

static struct ccw_device * get_disc_ccwdev_by_dev_id(struct ccw_dev_id *dev_id,
						     struct ccw_device *sibling)
{
	struct device *dev;
	struct match_data data;

	data.dev_id = *dev_id;
	data.sibling = sibling;
	dev = bus_find_device(&ccw_bus_type, NULL, &data, match_devno);

	return dev ? to_ccwdev(dev) : NULL;
}

static int match_orphan(struct device *dev, void *data)
{
	struct ccw_dev_id *dev_id;
	struct ccw_device *cdev;

	dev_id = data;
	cdev = to_ccwdev(dev);
	return ccw_dev_id_is_equal(&cdev->private->dev_id, dev_id);
}

static struct ccw_device *
get_orphaned_ccwdev_by_dev_id(struct channel_subsystem *css,
			      struct ccw_dev_id *dev_id)
{
	struct device *dev;

	dev = device_find_child(&css->pseudo_subchannel->dev, dev_id,
				match_orphan);

	return dev ? to_ccwdev(dev) : NULL;
}

static void
ccw_device_add_changed(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	if (device_add(&cdev->dev)) {
		put_device(&cdev->dev);
		return;
	}
	set_bit(1, &cdev->private->registered);
}

void ccw_device_do_unreg_rereg(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;
	struct subchannel *sch;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	sch = to_subchannel(cdev->dev.parent);

	ccw_device_unregister(cdev);
	PREPARE_WORK(&cdev->private->kick_work,
		     ccw_device_add_changed);
	queue_work(ccw_device_work, &cdev->private->kick_work);
}

static void
ccw_device_release(struct device *dev)
{
	struct ccw_device *cdev;

	cdev = to_ccwdev(dev);
	kfree(cdev->private);
	kfree(cdev);
}

static struct ccw_device * io_subchannel_allocate_dev(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev  = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (cdev) {
		cdev->private = kzalloc(sizeof(struct ccw_device_private),
					GFP_KERNEL | GFP_DMA);
		if (cdev->private)
			return cdev;
	}
	kfree(cdev);
	return ERR_PTR(-ENOMEM);
}

static int io_subchannel_initialize_dev(struct subchannel *sch,
					struct ccw_device *cdev)
{
	cdev->private->cdev = cdev;
	atomic_set(&cdev->private->onoff, 0);
	cdev->dev.parent = &sch->dev;
	cdev->dev.release = ccw_device_release;
	INIT_WORK(&cdev->private->kick_work, NULL);
	cdev->dev.groups = ccwdev_attr_groups;
	/* Do first half of device_register. */
	device_initialize(&cdev->dev);
	if (!get_device(&sch->dev)) {
		if (cdev->dev.release)
			cdev->dev.release(&cdev->dev);
		return -ENODEV;
	}
	return 0;
}

static struct ccw_device * io_subchannel_create_ccwdev(struct subchannel *sch)
{
	struct ccw_device *cdev;
	int ret;

	cdev = io_subchannel_allocate_dev(sch);
	if (!IS_ERR(cdev)) {
		ret = io_subchannel_initialize_dev(sch, cdev);
		if (ret) {
			kfree(cdev);
			cdev = ERR_PTR(ret);
		}
	}
	return cdev;
}

static int io_subchannel_recog(struct ccw_device *, struct subchannel *);

static void sch_attach_device(struct subchannel *sch,
			      struct ccw_device *cdev)
{
	css_update_ssd_info(sch);
	spin_lock_irq(sch->lock);
	sch_set_cdev(sch, cdev);
	cdev->private->schid = sch->schid;
	cdev->ccwlock = sch->lock;
	device_trigger_reprobe(sch);
	spin_unlock_irq(sch->lock);
}

static void sch_attach_disconnected_device(struct subchannel *sch,
					   struct ccw_device *cdev)
{
	struct subchannel *other_sch;
	int ret;

	other_sch = to_subchannel(get_device(cdev->dev.parent));
	ret = device_move(&cdev->dev, &sch->dev);
	if (ret) {
		CIO_MSG_EVENT(2, "Moving disconnected device 0.%x.%04x failed "
			      "(ret=%d)!\n", cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, ret);
		put_device(&other_sch->dev);
		return;
	}
	sch_set_cdev(other_sch, NULL);
	/* No need to keep a subchannel without ccw device around. */
	css_sch_device_unregister(other_sch);
	put_device(&other_sch->dev);
	sch_attach_device(sch, cdev);
}

static void sch_attach_orphaned_device(struct subchannel *sch,
				       struct ccw_device *cdev)
{
	int ret;

	/* Try to move the ccw device to its new subchannel. */
	ret = device_move(&cdev->dev, &sch->dev);
	if (ret) {
		CIO_MSG_EVENT(0, "Moving device 0.%x.%04x from orphanage "
			      "failed (ret=%d)!\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, ret);
		return;
	}
	sch_attach_device(sch, cdev);
}

static void sch_create_and_recog_new_device(struct subchannel *sch)
{
	struct ccw_device *cdev;

	/* Need to allocate a new ccw device. */
	cdev = io_subchannel_create_ccwdev(sch);
	if (IS_ERR(cdev)) {
		/* OK, we did everything we could... */
		css_sch_device_unregister(sch);
		return;
	}
	spin_lock_irq(sch->lock);
	sch_set_cdev(sch, cdev);
	spin_unlock_irq(sch->lock);
	/* Start recognition for the new ccw device. */
	if (io_subchannel_recog(cdev, sch)) {
		spin_lock_irq(sch->lock);
		sch_set_cdev(sch, NULL);
		spin_unlock_irq(sch->lock);
		if (cdev->dev.release)
			cdev->dev.release(&cdev->dev);
		css_sch_device_unregister(sch);
	}
}


void ccw_device_move_to_orphanage(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;
	struct ccw_device *replacing_cdev;
	struct subchannel *sch;
	int ret;
	struct channel_subsystem *css;
	struct ccw_dev_id dev_id;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	sch = to_subchannel(cdev->dev.parent);
	css = to_css(sch->dev.parent);
	dev_id.devno = sch->schib.pmcw.dev;
	dev_id.ssid = sch->schid.ssid;

	/*
	 * Move the orphaned ccw device to the orphanage so the replacing
	 * ccw device can take its place on the subchannel.
	 */
	ret = device_move(&cdev->dev, &css->pseudo_subchannel->dev);
	if (ret) {
		CIO_MSG_EVENT(0, "Moving device 0.%x.%04x to orphanage failed "
			      "(ret=%d)!\n", cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, ret);
		return;
	}
	cdev->ccwlock = css->pseudo_subchannel->lock;
	/*
	 * Search for the replacing ccw device
	 * - among the disconnected devices
	 * - in the orphanage
	 */
	replacing_cdev = get_disc_ccwdev_by_dev_id(&dev_id, cdev);
	if (replacing_cdev) {
		sch_attach_disconnected_device(sch, replacing_cdev);
		return;
	}
	replacing_cdev = get_orphaned_ccwdev_by_dev_id(css, &dev_id);
	if (replacing_cdev) {
		sch_attach_orphaned_device(sch, replacing_cdev);
		return;
	}
	sch_create_and_recog_new_device(sch);
}

/*
 * Register recognized device.
 */
static void
io_subchannel_register(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;
	struct subchannel *sch;
	int ret;
	unsigned long flags;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	sch = to_subchannel(cdev->dev.parent);
	css_update_ssd_info(sch);
	/*
	 * io_subchannel_register() will also be called after device
	 * recognition has been done for a boxed device (which will already
	 * be registered). We need to reprobe since we may now have sense id
	 * information.
	 */
	if (klist_node_attached(&cdev->dev.knode_parent)) {
		if (!cdev->drv) {
			ret = device_reprobe(&cdev->dev);
			if (ret)
				/* We can't do much here. */
				CIO_MSG_EVENT(2, "device_reprobe() returned"
					      " %d for 0.%x.%04x\n", ret,
					      cdev->private->dev_id.ssid,
					      cdev->private->dev_id.devno);
		}
		goto out;
	}
	/*
	 * Now we know this subchannel will stay, we can throw
	 * our delayed uevent.
	 */
	sch->dev.uevent_suppress = 0;
	kobject_uevent(&sch->dev.kobj, KOBJ_ADD);
	/* make it known to the system */
	ret = ccw_device_register(cdev);
	if (ret) {
		CIO_MSG_EVENT(0, "Could not register ccw dev 0.%x.%04x: %d\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, ret);
		put_device(&cdev->dev);
		spin_lock_irqsave(sch->lock, flags);
		sch_set_cdev(sch, NULL);
		spin_unlock_irqrestore(sch->lock, flags);
		kfree (cdev->private);
		kfree (cdev);
		put_device(&sch->dev);
		if (atomic_dec_and_test(&ccw_device_init_count))
			wake_up(&ccw_device_init_wq);
		return;
	}
	put_device(&cdev->dev);
out:
	cdev->private->flags.recog_done = 1;
	put_device(&sch->dev);
	wake_up(&cdev->private->wait_q);
	if (atomic_dec_and_test(&ccw_device_init_count))
		wake_up(&ccw_device_init_wq);
}

static void ccw_device_call_sch_unregister(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;
	struct subchannel *sch;

	priv = container_of(work, struct ccw_device_private, kick_work);
	cdev = priv->cdev;
	sch = to_subchannel(cdev->dev.parent);
	css_sch_device_unregister(sch);
	/* Reset intparm to zeroes. */
	sch->schib.pmcw.intparm = 0;
	cio_modify(sch);
	put_device(&cdev->dev);
	put_device(&sch->dev);
}

/*
 * subchannel recognition done. Called from the state machine.
 */
void
io_subchannel_recog_done(struct ccw_device *cdev)
{
	struct subchannel *sch;

	if (css_init_done == 0) {
		cdev->private->flags.recog_done = 1;
		return;
	}
	switch (cdev->private->state) {
	case DEV_STATE_NOT_OPER:
		cdev->private->flags.recog_done = 1;
		/* Remove device found not operational. */
		if (!get_device(&cdev->dev))
			break;
		sch = to_subchannel(cdev->dev.parent);
		PREPARE_WORK(&cdev->private->kick_work,
			     ccw_device_call_sch_unregister);
		queue_work(slow_path_wq, &cdev->private->kick_work);
		if (atomic_dec_and_test(&ccw_device_init_count))
			wake_up(&ccw_device_init_wq);
		break;
	case DEV_STATE_BOXED:
		/* Device did not respond in time. */
	case DEV_STATE_OFFLINE:
		/* 
		 * We can't register the device in interrupt context so
		 * we schedule a work item.
		 */
		if (!get_device(&cdev->dev))
			break;
		PREPARE_WORK(&cdev->private->kick_work,
			     io_subchannel_register);
		queue_work(slow_path_wq, &cdev->private->kick_work);
		break;
	}
}

static int
io_subchannel_recog(struct ccw_device *cdev, struct subchannel *sch)
{
	int rc;
	struct ccw_device_private *priv;

	sch_set_cdev(sch, cdev);
	sch->driver = &io_subchannel_driver;
	cdev->ccwlock = sch->lock;

	/* Init private data. */
	priv = cdev->private;
	priv->dev_id.devno = sch->schib.pmcw.dev;
	priv->dev_id.ssid = sch->schid.ssid;
	priv->schid = sch->schid;
	priv->state = DEV_STATE_NOT_OPER;
	INIT_LIST_HEAD(&priv->cmb_list);
	init_waitqueue_head(&priv->wait_q);
	init_timer(&priv->timer);

	/* Set an initial name for the device. */
	snprintf (cdev->dev.bus_id, BUS_ID_SIZE, "0.%x.%04x",
		  sch->schid.ssid, sch->schib.pmcw.dev);

	/* Increase counter of devices currently in recognition. */
	atomic_inc(&ccw_device_init_count);

	/* Start async. device sensing. */
	spin_lock_irq(sch->lock);
	rc = ccw_device_recognition(cdev);
	spin_unlock_irq(sch->lock);
	if (rc) {
		if (atomic_dec_and_test(&ccw_device_init_count))
			wake_up(&ccw_device_init_wq);
	}
	return rc;
}

static void ccw_device_move_to_sch(struct work_struct *work)
{
	struct ccw_device_private *priv;
	int rc;
	struct subchannel *sch;
	struct ccw_device *cdev;
	struct subchannel *former_parent;

	priv = container_of(work, struct ccw_device_private, kick_work);
	sch = priv->sch;
	cdev = priv->cdev;
	former_parent = ccw_device_is_orphan(cdev) ?
		NULL : to_subchannel(get_device(cdev->dev.parent));
	mutex_lock(&sch->reg_mutex);
	/* Try to move the ccw device to its new subchannel. */
	rc = device_move(&cdev->dev, &sch->dev);
	mutex_unlock(&sch->reg_mutex);
	if (rc) {
		CIO_MSG_EVENT(2, "Moving device 0.%x.%04x to subchannel "
			      "0.%x.%04x failed (ret=%d)!\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schid.sch_no, rc);
		css_sch_device_unregister(sch);
		goto out;
	}
	if (former_parent) {
		spin_lock_irq(former_parent->lock);
		sch_set_cdev(former_parent, NULL);
		spin_unlock_irq(former_parent->lock);
		css_sch_device_unregister(former_parent);
		/* Reset intparm to zeroes. */
		former_parent->schib.pmcw.intparm = 0;
		cio_modify(former_parent);
	}
	sch_attach_device(sch, cdev);
out:
	if (former_parent)
		put_device(&former_parent->dev);
	put_device(&cdev->dev);
}

static void io_subchannel_irq(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);

	CIO_TRACE_EVENT(3, "IRQ");
	CIO_TRACE_EVENT(3, sch->dev.bus_id);
	if (cdev)
		dev_fsm_event(cdev, DEV_EVENT_INTERRUPT);
}

static int
io_subchannel_probe (struct subchannel *sch)
{
	struct ccw_device *cdev;
	int rc;
	unsigned long flags;
	struct ccw_dev_id dev_id;

	cdev = sch_get_cdev(sch);
	if (cdev) {
		/*
		 * This subchannel already has an associated ccw_device.
		 * Register it and exit. This happens for all early
		 * device, e.g. the console.
		 */
		cdev->dev.groups = ccwdev_attr_groups;
		device_initialize(&cdev->dev);
		ccw_device_register(cdev);
		/*
		 * Check if the device is already online. If it is
		 * the reference count needs to be corrected
		 * (see ccw_device_online and css_init_done for the
		 * ugly details).
		 */
		if (cdev->private->state != DEV_STATE_NOT_OPER &&
		    cdev->private->state != DEV_STATE_OFFLINE &&
		    cdev->private->state != DEV_STATE_BOXED)
			get_device(&cdev->dev);
		return 0;
	}
	/*
	 * First check if a fitting device may be found amongst the
	 * disconnected devices or in the orphanage.
	 */
	dev_id.devno = sch->schib.pmcw.dev;
	dev_id.ssid = sch->schid.ssid;
	/* Allocate I/O subchannel private data. */
	sch->private = kzalloc(sizeof(struct io_subchannel_private),
			       GFP_KERNEL | GFP_DMA);
	if (!sch->private)
		return -ENOMEM;
	cdev = get_disc_ccwdev_by_dev_id(&dev_id, NULL);
	if (!cdev)
		cdev = get_orphaned_ccwdev_by_dev_id(to_css(sch->dev.parent),
						     &dev_id);
	if (cdev) {
		/*
		 * Schedule moving the device until when we have a registered
		 * subchannel to move to and succeed the probe. We can
		 * unregister later again, when the probe is through.
		 */
		cdev->private->sch = sch;
		PREPARE_WORK(&cdev->private->kick_work,
			     ccw_device_move_to_sch);
		queue_work(slow_path_wq, &cdev->private->kick_work);
		return 0;
	}
	cdev = io_subchannel_create_ccwdev(sch);
	if (IS_ERR(cdev)) {
		kfree(sch->private);
		return PTR_ERR(cdev);
	}
	rc = io_subchannel_recog(cdev, sch);
	if (rc) {
		spin_lock_irqsave(sch->lock, flags);
		sch_set_cdev(sch, NULL);
		spin_unlock_irqrestore(sch->lock, flags);
		if (cdev->dev.release)
			cdev->dev.release(&cdev->dev);
		kfree(sch->private);
	}

	return rc;
}

static int
io_subchannel_remove (struct subchannel *sch)
{
	struct ccw_device *cdev;
	unsigned long flags;

	cdev = sch_get_cdev(sch);
	if (!cdev)
		return 0;
	/* Set ccw device to not operational and drop reference. */
	spin_lock_irqsave(cdev->ccwlock, flags);
	sch_set_cdev(sch, NULL);
	cdev->private->state = DEV_STATE_NOT_OPER;
	spin_unlock_irqrestore(cdev->ccwlock, flags);
	ccw_device_unregister(cdev);
	put_device(&cdev->dev);
	kfree(sch->private);
	return 0;
}

static int io_subchannel_notify(struct subchannel *sch, int event)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (!cdev)
		return 0;
	if (!cdev->drv)
		return 0;
	if (!cdev->online)
		return 0;
	return cdev->drv->notify ? cdev->drv->notify(cdev, event) : 0;
}

static void io_subchannel_verify(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (cdev)
		dev_fsm_event(cdev, DEV_EVENT_VERIFY);
}

static void io_subchannel_ioterm(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (!cdev)
		return;
	/* Internal I/O will be retried by the interrupt handler. */
	if (cdev->private->flags.intretry)
		return;
	cdev->private->state = DEV_STATE_CLEAR_VERIFY;
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      ERR_PTR(-EIO));
}

static void
io_subchannel_shutdown(struct subchannel *sch)
{
	struct ccw_device *cdev;
	int ret;

	cdev = sch_get_cdev(sch);

	if (cio_is_console(sch->schid))
		return;
	if (!sch->schib.pmcw.ena)
		/* Nothing to do. */
		return;
	ret = cio_disable_subchannel(sch);
	if (ret != -EBUSY)
		/* Subchannel is disabled, we're done. */
		return;
	cdev->private->state = DEV_STATE_QUIESCE;
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm,
			      ERR_PTR(-EIO));
	ret = ccw_device_cancel_halt_clear(cdev);
	if (ret == -EBUSY) {
		ccw_device_set_timeout(cdev, HZ/10);
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	}
	cio_disable_subchannel(sch);
}

#ifdef CONFIG_CCW_CONSOLE
static struct ccw_device console_cdev;
static struct ccw_device_private console_private;
static int console_cdev_in_use;

static DEFINE_SPINLOCK(ccw_console_lock);

spinlock_t * cio_get_console_lock(void)
{
	return &ccw_console_lock;
}

static int
ccw_device_console_enable (struct ccw_device *cdev, struct subchannel *sch)
{
	int rc;

	/* Attach subchannel private data. */
	sch->private = cio_get_console_priv();
	memset(sch->private, 0, sizeof(struct io_subchannel_private));
	/* Initialize the ccw_device structure. */
	cdev->dev.parent= &sch->dev;
	rc = io_subchannel_recog(cdev, sch);
	if (rc)
		return rc;

	/* Now wait for the async. recognition to come to an end. */
	spin_lock_irq(cdev->ccwlock);
	while (!dev_fsm_final_state(cdev))
		wait_cons_dev();
	rc = -EIO;
	if (cdev->private->state != DEV_STATE_OFFLINE)
		goto out_unlock;
	ccw_device_online(cdev);
	while (!dev_fsm_final_state(cdev))
		wait_cons_dev();
	if (cdev->private->state != DEV_STATE_ONLINE)
		goto out_unlock;
	rc = 0;
out_unlock:
	spin_unlock_irq(cdev->ccwlock);
	return 0;
}

struct ccw_device *
ccw_device_probe_console(void)
{
	struct subchannel *sch;
	int ret;

	if (xchg(&console_cdev_in_use, 1) != 0)
		return ERR_PTR(-EBUSY);
	sch = cio_probe_console();
	if (IS_ERR(sch)) {
		console_cdev_in_use = 0;
		return (void *) sch;
	}
	memset(&console_cdev, 0, sizeof(struct ccw_device));
	memset(&console_private, 0, sizeof(struct ccw_device_private));
	console_cdev.private = &console_private;
	console_private.cdev = &console_cdev;
	ret = ccw_device_console_enable(&console_cdev, sch);
	if (ret) {
		cio_release_console();
		console_cdev_in_use = 0;
		return ERR_PTR(ret);
	}
	console_cdev.online = 1;
	return &console_cdev;
}
#endif

/*
 * get ccw_device matching the busid, but only if owned by cdrv
 */
static int
__ccwdev_check_busid(struct device *dev, void *id)
{
	char *bus_id;

	bus_id = id;

	return (strncmp(bus_id, dev->bus_id, BUS_ID_SIZE) == 0);
}


/**
 * get_ccwdev_by_busid() - obtain device from a bus id
 * @cdrv: driver the device is owned by
 * @bus_id: bus id of the device to be searched
 *
 * This function searches all devices owned by @cdrv for a device with a bus
 * id matching @bus_id.
 * Returns:
 *  If a match is found, its reference count of the found device is increased
 *  and it is returned; else %NULL is returned.
 */
struct ccw_device *get_ccwdev_by_busid(struct ccw_driver *cdrv,
				       const char *bus_id)
{
	struct device *dev;
	struct device_driver *drv;

	drv = get_driver(&cdrv->driver);
	if (!drv)
		return NULL;

	dev = driver_find_device(drv, NULL, (void *)bus_id,
				 __ccwdev_check_busid);
	put_driver(drv);

	return dev ? to_ccwdev(dev) : NULL;
}

/************************** device driver handling ************************/

/* This is the implementation of the ccw_driver class. The probe, remove
 * and release methods are initially very similar to the device_driver
 * implementations, with the difference that they have ccw_device
 * arguments.
 *
 * A ccw driver also contains the information that is needed for
 * device matching.
 */
static int
ccw_device_probe (struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_driver *cdrv = to_ccwdrv(dev->driver);
	int ret;

	cdev->drv = cdrv; /* to let the driver call _set_online */

	ret = cdrv->probe ? cdrv->probe(cdev) : -ENODEV;

	if (ret) {
		cdev->drv = NULL;
		return ret;
	}

	return 0;
}

static int
ccw_device_remove (struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_driver *cdrv = cdev->drv;
	int ret;

	if (cdrv->remove)
		cdrv->remove(cdev);
	if (cdev->online) {
		cdev->online = 0;
		spin_lock_irq(cdev->ccwlock);
		ret = ccw_device_offline(cdev);
		spin_unlock_irq(cdev->ccwlock);
		if (ret == 0)
			wait_event(cdev->private->wait_q,
				   dev_fsm_final_state(cdev));
		else
			//FIXME: we can't fail!
			CIO_MSG_EVENT(2, "ccw_device_offline returned %d, "
				      "device 0.%x.%04x\n",
				      ret, cdev->private->dev_id.ssid,
				      cdev->private->dev_id.devno);
	}
	ccw_device_set_timeout(cdev, 0);
	cdev->drv = NULL;
	return 0;
}

static void ccw_device_shutdown(struct device *dev)
{
	struct ccw_device *cdev;

	cdev = to_ccwdev(dev);
	if (cdev->drv && cdev->drv->shutdown)
		cdev->drv->shutdown(cdev);
	disable_cmf(cdev);
}

struct bus_type ccw_bus_type = {
	.name   = "ccw",
	.match  = ccw_bus_match,
	.uevent = ccw_uevent,
	.probe  = ccw_device_probe,
	.remove = ccw_device_remove,
	.shutdown = ccw_device_shutdown,
};

/**
 * ccw_driver_register() - register a ccw driver
 * @cdriver: driver to be registered
 *
 * This function is mainly a wrapper around driver_register().
 * Returns:
 *   %0 on success and a negative error value on failure.
 */
int ccw_driver_register(struct ccw_driver *cdriver)
{
	struct device_driver *drv = &cdriver->driver;

	drv->bus = &ccw_bus_type;
	drv->name = cdriver->name;
	drv->owner = cdriver->owner;

	return driver_register(drv);
}

/**
 * ccw_driver_unregister() - deregister a ccw driver
 * @cdriver: driver to be deregistered
 *
 * This function is mainly a wrapper around driver_unregister().
 */
void ccw_driver_unregister(struct ccw_driver *cdriver)
{
	driver_unregister(&cdriver->driver);
}

/* Helper func for qdio. */
struct subchannel_id
ccw_device_get_subchannel_id(struct ccw_device *cdev)
{
	struct subchannel *sch;

	sch = to_subchannel(cdev->dev.parent);
	return sch->schid;
}

static int recovery_check(struct device *dev, void *data)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int *redo = data;

	spin_lock_irq(cdev->ccwlock);
	switch (cdev->private->state) {
	case DEV_STATE_DISCONNECTED:
		CIO_MSG_EVENT(3, "recovery: trigger 0.%x.%04x\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
		dev_fsm_event(cdev, DEV_EVENT_VERIFY);
		*redo = 1;
		break;
	case DEV_STATE_DISCONNECTED_SENSE_ID:
		*redo = 1;
		break;
	}
	spin_unlock_irq(cdev->ccwlock);

	return 0;
}

static void recovery_work_func(struct work_struct *unused)
{
	int redo = 0;

	bus_for_each_dev(&ccw_bus_type, NULL, &redo, recovery_check);
	if (redo) {
		spin_lock_irq(&recovery_lock);
		if (!timer_pending(&recovery_timer)) {
			if (recovery_phase < ARRAY_SIZE(recovery_delay) - 1)
				recovery_phase++;
			mod_timer(&recovery_timer, jiffies +
				  recovery_delay[recovery_phase] * HZ);
		}
		spin_unlock_irq(&recovery_lock);
	} else
		CIO_MSG_EVENT(2, "recovery: end\n");
}

static DECLARE_WORK(recovery_work, recovery_work_func);

static void recovery_func(unsigned long data)
{
	/*
	 * We can't do our recovery in softirq context and it's not
	 * performance critical, so we schedule it.
	 */
	schedule_work(&recovery_work);
}

void ccw_device_schedule_recovery(void)
{
	unsigned long flags;

	CIO_MSG_EVENT(2, "recovery: schedule\n");
	spin_lock_irqsave(&recovery_lock, flags);
	if (!timer_pending(&recovery_timer) || (recovery_phase != 0)) {
		recovery_phase = 0;
		mod_timer(&recovery_timer, jiffies + recovery_delay[0] * HZ);
	}
	spin_unlock_irqrestore(&recovery_lock, flags);
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccw_device_set_online);
EXPORT_SYMBOL(ccw_device_set_offline);
EXPORT_SYMBOL(ccw_driver_register);
EXPORT_SYMBOL(ccw_driver_unregister);
EXPORT_SYMBOL(get_ccwdev_by_busid);
EXPORT_SYMBOL(ccw_bus_type);
EXPORT_SYMBOL(ccw_device_work);
EXPORT_SYMBOL(ccw_device_notify_work);
EXPORT_SYMBOL_GPL(ccw_device_get_subchannel_id);
