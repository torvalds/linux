/*
 *  drivers/s390/cio/device.c
 *  bus driver for ccw devices
 *
 *    Copyright IBM Corp. 2002,2008
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define KMSG_COMPONENT "cio"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

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
#include <asm/isc.h>

#include "chp.h"
#include "cio.h"
#include "cio_debug.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "io_sch.h"
#include "blacklist.h"

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
static void io_subchannel_shutdown(struct subchannel *);
static int io_subchannel_sch_event(struct subchannel *, int);
static int io_subchannel_chp_event(struct subchannel *, struct chp_link *,
				   int);
static void recovery_func(unsigned long data);
struct workqueue_struct *ccw_device_work;
wait_queue_head_t ccw_device_init_wq;
atomic_t ccw_device_init_count;

static struct css_device_id io_subchannel_ids[] = {
	{ .match_flags = 0x1, .type = SUBCHANNEL_TYPE_IO, },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(css, io_subchannel_ids);

static int io_subchannel_prepare(struct subchannel *sch)
{
	struct ccw_device *cdev;
	/*
	 * Don't allow suspend while a ccw device registration
	 * is still outstanding.
	 */
	cdev = sch_get_cdev(sch);
	if (cdev && !device_is_registered(&cdev->dev))
		return -EAGAIN;
	return 0;
}

static void io_subchannel_settle(void)
{
	wait_event(ccw_device_init_wq,
		   atomic_read(&ccw_device_init_count) == 0);
	flush_workqueue(ccw_device_work);
}

static struct css_driver io_subchannel_driver = {
	.owner = THIS_MODULE,
	.subchannel_type = io_subchannel_ids,
	.name = "io_subchannel",
	.irq = io_subchannel_irq,
	.sch_event = io_subchannel_sch_event,
	.chp_event = io_subchannel_chp_event,
	.probe = io_subchannel_probe,
	.remove = io_subchannel_remove,
	.shutdown = io_subchannel_shutdown,
	.prepare = io_subchannel_prepare,
	.settle = io_subchannel_settle,
};

int __init io_subchannel_init(void)
{
	int ret;

	init_waitqueue_head(&ccw_device_init_wq);
	atomic_set(&ccw_device_init_count, 0);
	setup_timer(&recovery_timer, recovery_func, 0);

	ccw_device_work = create_singlethread_workqueue("cio");
	if (!ccw_device_work)
		return -ENOMEM;
	slow_path_wq = create_singlethread_workqueue("kslowcrw");
	if (!slow_path_wq) {
		ret = -ENOMEM;
		goto out_err;
	}
	if ((ret = bus_register (&ccw_bus_type)))
		goto out_err;

	ret = css_driver_register(&io_subchannel_driver);
	if (ret)
		goto out_err;

	return 0;
out_err:
	if (ccw_device_work)
		destroy_workqueue(ccw_device_work);
	if (slow_path_wq)
		destroy_workqueue(slow_path_wq);
	return ret;
}


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
	if (device_is_registered(&cdev->dev)) {
		/* Undo device_add(). */
		device_del(&cdev->dev);
	}
	if (cdev->private->flags.initialized) {
		cdev->private->flags.initialized = 0;
		/* Release reference from device_initialize(). */
		put_device(&cdev->dev);
	}
}

static void io_subchannel_quiesce(struct subchannel *);

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
	struct subchannel *sch;
	int ret, state;

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
	sch = to_subchannel(cdev->dev.parent);
	/* Wait until a final state or DISCONNECTED is reached */
	while (!dev_fsm_final_state(cdev) &&
	       cdev->private->state != DEV_STATE_DISCONNECTED) {
		spin_unlock_irq(cdev->ccwlock);
		wait_event(cdev->private->wait_q, (dev_fsm_final_state(cdev) ||
			   cdev->private->state == DEV_STATE_DISCONNECTED));
		spin_lock_irq(cdev->ccwlock);
	}
	do {
		ret = ccw_device_offline(cdev);
		if (!ret)
			break;
		CIO_MSG_EVENT(0, "ccw_device_offline returned %d, device "
			      "0.%x.%04x\n", ret, cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
		if (ret != -EBUSY)
			goto error;
		state = cdev->private->state;
		spin_unlock_irq(cdev->ccwlock);
		io_subchannel_quiesce(sch);
		spin_lock_irq(cdev->ccwlock);
		cdev->private->state = state;
	} while (ret == -EBUSY);
	spin_unlock_irq(cdev->ccwlock);
	wait_event(cdev->private->wait_q, (dev_fsm_final_state(cdev) ||
		   cdev->private->state == DEV_STATE_DISCONNECTED));
	/* Inform the user if set offline failed. */
	if (cdev->private->state == DEV_STATE_BOXED) {
		pr_warning("%s: The device entered boxed state while "
			   "being set offline\n", dev_name(&cdev->dev));
	} else if (cdev->private->state == DEV_STATE_NOT_OPER) {
		pr_warning("%s: The device stopped operating while "
			   "being set offline\n", dev_name(&cdev->dev));
	}
	/* Give up reference from ccw_device_set_online(). */
	put_device(&cdev->dev);
	return 0;

error:
	cdev->private->state = DEV_STATE_OFFLINE;
	dev_fsm_event(cdev, DEV_EVENT_NOTOPER);
	spin_unlock_irq(cdev->ccwlock);
	/* Give up reference from ccw_device_set_online(). */
	put_device(&cdev->dev);
	return -ENODEV;
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
	int ret2;

	if (!cdev)
		return -ENODEV;
	if (cdev->online || !cdev->drv)
		return -EINVAL;
	/* Hold on to an extra reference while device is online. */
	if (!get_device(&cdev->dev))
		return -ENODEV;

	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_online(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else {
		CIO_MSG_EVENT(0, "ccw_device_online returned %d, "
			      "device 0.%x.%04x\n",
			      ret, cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno);
		/* Give up online reference since onlining failed. */
		put_device(&cdev->dev);
		return ret;
	}
	spin_lock_irq(cdev->ccwlock);
	/* Check if online processing was successful */
	if ((cdev->private->state != DEV_STATE_ONLINE) &&
	    (cdev->private->state != DEV_STATE_W4SENSE)) {
		spin_unlock_irq(cdev->ccwlock);
		/* Inform the user that set online failed. */
		if (cdev->private->state == DEV_STATE_BOXED) {
			pr_warning("%s: Setting the device online failed "
				   "because it is boxed\n",
				   dev_name(&cdev->dev));
		} else if (cdev->private->state == DEV_STATE_NOT_OPER) {
			pr_warning("%s: Setting the device online failed "
				   "because it is not operational\n",
				   dev_name(&cdev->dev));
		}
		/* Give up online reference since onlining failed. */
		put_device(&cdev->dev);
		return -ENODEV;
	}
	spin_unlock_irq(cdev->ccwlock);
	if (cdev->drv->set_online)
		ret = cdev->drv->set_online(cdev);
	if (ret)
		goto rollback;
	cdev->online = 1;
	return 0;

rollback:
	spin_lock_irq(cdev->ccwlock);
	/* Wait until a final state or DISCONNECTED is reached */
	while (!dev_fsm_final_state(cdev) &&
	       cdev->private->state != DEV_STATE_DISCONNECTED) {
		spin_unlock_irq(cdev->ccwlock);
		wait_event(cdev->private->wait_q, (dev_fsm_final_state(cdev) ||
			   cdev->private->state == DEV_STATE_DISCONNECTED));
		spin_lock_irq(cdev->ccwlock);
	}
	ret2 = ccw_device_offline(cdev);
	if (ret2)
		goto error;
	spin_unlock_irq(cdev->ccwlock);
	wait_event(cdev->private->wait_q, (dev_fsm_final_state(cdev) ||
		   cdev->private->state == DEV_STATE_DISCONNECTED));
	/* Give up online reference since onlining failed. */
	put_device(&cdev->dev);
	return ret;

error:
	CIO_MSG_EVENT(0, "rollback ccw_device_offline returned %d, "
		      "device 0.%x.%04x\n",
		      ret2, cdev->private->dev_id.ssid,
		      cdev->private->dev_id.devno);
	cdev->private->state = DEV_STATE_OFFLINE;
	spin_unlock_irq(cdev->ccwlock);
	/* Give up online reference since onlining failed. */
	put_device(&cdev->dev);
	return ret;
}

static int online_store_handle_offline(struct ccw_device *cdev)
{
	if (cdev->private->state == DEV_STATE_DISCONNECTED) {
		spin_lock_irq(cdev->ccwlock);
		ccw_device_sched_todo(cdev, CDEV_TODO_UNREG_EVAL);
		spin_unlock_irq(cdev->ccwlock);
	} else if (cdev->online && cdev->drv && cdev->drv->set_offline)
		return ccw_device_set_offline(cdev);
	return 0;
}

static int online_store_recog_and_online(struct ccw_device *cdev)
{
	/* Do device recognition, if needed. */
	if (cdev->private->state == DEV_STATE_BOXED) {
		spin_lock_irq(cdev->ccwlock);
		ccw_device_recognition(cdev);
		spin_unlock_irq(cdev->ccwlock);
		wait_event(cdev->private->wait_q,
			   cdev->private->flags.recog_done);
		if (cdev->private->state != DEV_STATE_OFFLINE)
			/* recognition failed */
			return -EAGAIN;
	}
	if (cdev->drv && cdev->drv->set_online)
		ccw_device_set_online(cdev);
	return 0;
}

static int online_store_handle_online(struct ccw_device *cdev, int force)
{
	int ret;

	ret = online_store_recog_and_online(cdev);
	if (ret && !force)
		return ret;
	if (force && cdev->private->state == DEV_STATE_BOXED) {
		ret = ccw_device_stlck(cdev);
		if (ret)
			return ret;
		if (cdev->id.cu_type == 0)
			cdev->private->state = DEV_STATE_NOT_OPER;
		ret = online_store_recog_and_online(cdev);
		if (ret)
			return ret;
	}
	return 0;
}

static ssize_t online_store (struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int force, ret;
	unsigned long i;

	if (!dev_fsm_final_state(cdev) &&
	    cdev->private->state != DEV_STATE_DISCONNECTED)
		return -EAGAIN;
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
		ret = online_store_handle_offline(cdev);
		break;
	case 1:
		ret = online_store_handle_online(cdev, force);
		break;
	default:
		ret = -EINVAL;
	}
out:
	if (cdev->drv)
		module_put(cdev->drv->owner);
	atomic_set(&cdev->private->onoff, 0);
	return (ret < 0) ? ret : count;
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

static struct attribute *io_subchannel_attrs[] = {
	&dev_attr_chpids.attr,
	&dev_attr_pimpampom.attr,
	NULL,
};

static struct attribute_group io_subchannel_attr_group = {
	.attrs = io_subchannel_attrs,
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

static const struct attribute_group *ccwdev_attr_groups[] = {
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
	ret = dev_set_name(&cdev->dev, "0.%x.%04x", cdev->private->dev_id.ssid,
			   cdev->private->dev_id.devno);
	if (ret)
		return ret;
	return device_add(dev);
}

static int match_dev_id(struct device *dev, void *data)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_dev_id *dev_id = data;

	return ccw_dev_id_is_equal(&cdev->private->dev_id, dev_id);
}

static struct ccw_device *get_ccwdev_by_dev_id(struct ccw_dev_id *dev_id)
{
	struct device *dev;

	dev = bus_find_device(&ccw_bus_type, NULL, dev_id, match_dev_id);

	return dev ? to_ccwdev(dev) : NULL;
}

static void ccw_device_do_unbind_bind(struct ccw_device *cdev)
{
	int ret;

	if (device_is_registered(&cdev->dev)) {
		device_release_driver(&cdev->dev);
		ret = device_attach(&cdev->dev);
		WARN_ON(ret == -ENODEV);
	}
}

static void
ccw_device_release(struct device *dev)
{
	struct ccw_device *cdev;

	cdev = to_ccwdev(dev);
	/* Release reference of parent subchannel. */
	put_device(cdev->dev.parent);
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

static void ccw_device_todo(struct work_struct *work);

static int io_subchannel_initialize_dev(struct subchannel *sch,
					struct ccw_device *cdev)
{
	cdev->private->cdev = cdev;
	atomic_set(&cdev->private->onoff, 0);
	cdev->dev.parent = &sch->dev;
	cdev->dev.release = ccw_device_release;
	INIT_WORK(&cdev->private->todo_work, ccw_device_todo);
	cdev->dev.groups = ccwdev_attr_groups;
	/* Do first half of device_register. */
	device_initialize(&cdev->dev);
	if (!get_device(&sch->dev)) {
		/* Release reference from device_initialize(). */
		put_device(&cdev->dev);
		return -ENODEV;
	}
	cdev->private->flags.initialized = 1;
	return 0;
}

static struct ccw_device * io_subchannel_create_ccwdev(struct subchannel *sch)
{
	struct ccw_device *cdev;
	int ret;

	cdev = io_subchannel_allocate_dev(sch);
	if (!IS_ERR(cdev)) {
		ret = io_subchannel_initialize_dev(sch, cdev);
		if (ret)
			cdev = ERR_PTR(ret);
	}
	return cdev;
}

static void io_subchannel_recog(struct ccw_device *, struct subchannel *);

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
	/* Start recognition for the new ccw device. */
	io_subchannel_recog(cdev, sch);
}

/*
 * Register recognized device.
 */
static void io_subchannel_register(struct ccw_device *cdev)
{
	struct subchannel *sch;
	int ret;
	unsigned long flags;

	sch = to_subchannel(cdev->dev.parent);
	/*
	 * Check if subchannel is still registered. It may have become
	 * unregistered if a machine check hit us after finishing
	 * device recognition but before the register work could be
	 * queued.
	 */
	if (!device_is_registered(&sch->dev))
		goto out_err;
	css_update_ssd_info(sch);
	/*
	 * io_subchannel_register() will also be called after device
	 * recognition has been done for a boxed device (which will already
	 * be registered). We need to reprobe since we may now have sense id
	 * information.
	 */
	if (device_is_registered(&cdev->dev)) {
		if (!cdev->drv) {
			ret = device_reprobe(&cdev->dev);
			if (ret)
				/* We can't do much here. */
				CIO_MSG_EVENT(0, "device_reprobe() returned"
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
	dev_set_uevent_suppress(&sch->dev, 0);
	kobject_uevent(&sch->dev.kobj, KOBJ_ADD);
	/* make it known to the system */
	ret = ccw_device_register(cdev);
	if (ret) {
		CIO_MSG_EVENT(0, "Could not register ccw dev 0.%x.%04x: %d\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, ret);
		spin_lock_irqsave(sch->lock, flags);
		sch_set_cdev(sch, NULL);
		spin_unlock_irqrestore(sch->lock, flags);
		/* Release initial device reference. */
		put_device(&cdev->dev);
		goto out_err;
	}
out:
	cdev->private->flags.recog_done = 1;
	wake_up(&cdev->private->wait_q);
out_err:
	if (atomic_dec_and_test(&ccw_device_init_count))
		wake_up(&ccw_device_init_wq);
}

static void ccw_device_call_sch_unregister(struct ccw_device *cdev)
{
	struct subchannel *sch;

	/* Get subchannel reference for local processing. */
	if (!get_device(cdev->dev.parent))
		return;
	sch = to_subchannel(cdev->dev.parent);
	css_sch_device_unregister(sch);
	/* Release subchannel reference for local processing. */
	put_device(&sch->dev);
}

/*
 * subchannel recognition done. Called from the state machine.
 */
void
io_subchannel_recog_done(struct ccw_device *cdev)
{
	if (css_init_done == 0) {
		cdev->private->flags.recog_done = 1;
		return;
	}
	switch (cdev->private->state) {
	case DEV_STATE_BOXED:
		/* Device did not respond in time. */
	case DEV_STATE_NOT_OPER:
		cdev->private->flags.recog_done = 1;
		/* Remove device found not operational. */
		ccw_device_sched_todo(cdev, CDEV_TODO_UNREG);
		if (atomic_dec_and_test(&ccw_device_init_count))
			wake_up(&ccw_device_init_wq);
		break;
	case DEV_STATE_OFFLINE:
		/* 
		 * We can't register the device in interrupt context so
		 * we schedule a work item.
		 */
		ccw_device_sched_todo(cdev, CDEV_TODO_REGISTER);
		break;
	}
}

static void io_subchannel_recog(struct ccw_device *cdev, struct subchannel *sch)
{
	struct ccw_device_private *priv;

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

	/* Increase counter of devices currently in recognition. */
	atomic_inc(&ccw_device_init_count);

	/* Start async. device sensing. */
	spin_lock_irq(sch->lock);
	sch_set_cdev(sch, cdev);
	ccw_device_recognition(cdev);
	spin_unlock_irq(sch->lock);
}

static int ccw_device_move_to_sch(struct ccw_device *cdev,
				  struct subchannel *sch)
{
	struct subchannel *old_sch;
	int rc, old_enabled = 0;

	old_sch = to_subchannel(cdev->dev.parent);
	/* Obtain child reference for new parent. */
	if (!get_device(&sch->dev))
		return -ENODEV;

	if (!sch_is_pseudo_sch(old_sch)) {
		spin_lock_irq(old_sch->lock);
		old_enabled = old_sch->schib.pmcw.ena;
		rc = 0;
		if (old_enabled)
			rc = cio_disable_subchannel(old_sch);
		spin_unlock_irq(old_sch->lock);
		if (rc == -EBUSY) {
			/* Release child reference for new parent. */
			put_device(&sch->dev);
			return rc;
		}
	}

	mutex_lock(&sch->reg_mutex);
	rc = device_move(&cdev->dev, &sch->dev, DPM_ORDER_PARENT_BEFORE_DEV);
	mutex_unlock(&sch->reg_mutex);
	if (rc) {
		CIO_MSG_EVENT(0, "device_move(0.%x.%04x,0.%x.%04x)=%d\n",
			      cdev->private->dev_id.ssid,
			      cdev->private->dev_id.devno, sch->schid.ssid,
			      sch->schib.pmcw.dev, rc);
		if (old_enabled) {
			/* Try to reenable the old subchannel. */
			spin_lock_irq(old_sch->lock);
			cio_enable_subchannel(old_sch, (u32)(addr_t)old_sch);
			spin_unlock_irq(old_sch->lock);
		}
		/* Release child reference for new parent. */
		put_device(&sch->dev);
		return rc;
	}
	/* Clean up old subchannel. */
	if (!sch_is_pseudo_sch(old_sch)) {
		spin_lock_irq(old_sch->lock);
		sch_set_cdev(old_sch, NULL);
		spin_unlock_irq(old_sch->lock);
		css_schedule_eval(old_sch->schid);
	}
	/* Release child reference for old parent. */
	put_device(&old_sch->dev);
	/* Initialize new subchannel. */
	spin_lock_irq(sch->lock);
	cdev->private->schid = sch->schid;
	cdev->ccwlock = sch->lock;
	if (!sch_is_pseudo_sch(sch))
		sch_set_cdev(sch, cdev);
	spin_unlock_irq(sch->lock);
	if (!sch_is_pseudo_sch(sch))
		css_update_ssd_info(sch);
	return 0;
}

static int ccw_device_move_to_orph(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	struct channel_subsystem *css = to_css(sch->dev.parent);

	return ccw_device_move_to_sch(cdev, css->pseudo_subchannel);
}

static void io_subchannel_irq(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);

	CIO_TRACE_EVENT(6, "IRQ");
	CIO_TRACE_EVENT(6, dev_name(&sch->dev));
	if (cdev)
		dev_fsm_event(cdev, DEV_EVENT_INTERRUPT);
}

void io_subchannel_init_config(struct subchannel *sch)
{
	memset(&sch->config, 0, sizeof(sch->config));
	sch->config.csense = 1;
}

static void io_subchannel_init_fields(struct subchannel *sch)
{
	if (cio_is_console(sch->schid))
		sch->opm = 0xff;
	else
		sch->opm = chp_get_sch_opm(sch);
	sch->lpm = sch->schib.pmcw.pam & sch->opm;
	sch->isc = cio_is_console(sch->schid) ? CONSOLE_ISC : IO_SCH_ISC;

	CIO_MSG_EVENT(6, "Detected device %04x on subchannel 0.%x.%04X"
		      " - PIM = %02X, PAM = %02X, POM = %02X\n",
		      sch->schib.pmcw.dev, sch->schid.ssid,
		      sch->schid.sch_no, sch->schib.pmcw.pim,
		      sch->schib.pmcw.pam, sch->schib.pmcw.pom);

	io_subchannel_init_config(sch);
}

/*
 * Note: We always return 0 so that we bind to the device even on error.
 * This is needed so that our remove function is called on unregister.
 */
static int io_subchannel_probe(struct subchannel *sch)
{
	struct ccw_device *cdev;
	int rc;

	if (cio_is_console(sch->schid)) {
		rc = sysfs_create_group(&sch->dev.kobj,
					&io_subchannel_attr_group);
		if (rc)
			CIO_MSG_EVENT(0, "Failed to create io subchannel "
				      "attributes for subchannel "
				      "0.%x.%04x (rc=%d)\n",
				      sch->schid.ssid, sch->schid.sch_no, rc);
		/*
		 * The console subchannel already has an associated ccw_device.
		 * Throw the delayed uevent for the subchannel, register
		 * the ccw_device and exit.
		 */
		dev_set_uevent_suppress(&sch->dev, 0);
		kobject_uevent(&sch->dev.kobj, KOBJ_ADD);
		cdev = sch_get_cdev(sch);
		cdev->dev.groups = ccwdev_attr_groups;
		device_initialize(&cdev->dev);
		cdev->private->flags.initialized = 1;
		ccw_device_register(cdev);
		/*
		 * Check if the device is already online. If it is
		 * the reference count needs to be corrected since we
		 * didn't obtain a reference in ccw_device_set_online.
		 */
		if (cdev->private->state != DEV_STATE_NOT_OPER &&
		    cdev->private->state != DEV_STATE_OFFLINE &&
		    cdev->private->state != DEV_STATE_BOXED)
			get_device(&cdev->dev);
		return 0;
	}
	io_subchannel_init_fields(sch);
	rc = cio_commit_config(sch);
	if (rc)
		goto out_schedule;
	rc = sysfs_create_group(&sch->dev.kobj,
				&io_subchannel_attr_group);
	if (rc)
		goto out_schedule;
	/* Allocate I/O subchannel private data. */
	sch->private = kzalloc(sizeof(struct io_subchannel_private),
			       GFP_KERNEL | GFP_DMA);
	if (!sch->private)
		goto out_schedule;
	css_schedule_eval(sch->schid);
	return 0;

out_schedule:
	spin_lock_irq(sch->lock);
	css_sched_sch_todo(sch, SCH_TODO_UNREG);
	spin_unlock_irq(sch->lock);
	return 0;
}

static int
io_subchannel_remove (struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (!cdev)
		goto out_free;
	io_subchannel_quiesce(sch);
	/* Set ccw device to not operational and drop reference. */
	spin_lock_irq(cdev->ccwlock);
	sch_set_cdev(sch, NULL);
	cdev->private->state = DEV_STATE_NOT_OPER;
	spin_unlock_irq(cdev->ccwlock);
	ccw_device_unregister(cdev);
out_free:
	kfree(sch->private);
	sysfs_remove_group(&sch->dev.kobj, &io_subchannel_attr_group);
	return 0;
}

static void io_subchannel_verify(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (cdev)
		dev_fsm_event(cdev, DEV_EVENT_VERIFY);
}

static void io_subchannel_terminate_path(struct subchannel *sch, u8 mask)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (!cdev)
		return;
	if (cio_update_schib(sch))
		goto err;
	/* Check for I/O on path. */
	if (scsw_actl(&sch->schib.scsw) == 0 || sch->schib.pmcw.lpum != mask)
		goto out;
	if (cdev->private->state == DEV_STATE_ONLINE) {
		ccw_device_kill_io(cdev);
		goto out;
	}
	if (cio_clear(sch))
		goto err;
out:
	/* Trigger path verification. */
	dev_fsm_event(cdev, DEV_EVENT_VERIFY);
	return;

err:
	dev_fsm_event(cdev, DEV_EVENT_NOTOPER);
}

static int io_subchannel_chp_event(struct subchannel *sch,
				   struct chp_link *link, int event)
{
	int mask;

	mask = chp_ssd_get_mask(&sch->ssd_info, link);
	if (!mask)
		return 0;
	switch (event) {
	case CHP_VARY_OFF:
		sch->opm &= ~mask;
		sch->lpm &= ~mask;
		io_subchannel_terminate_path(sch, mask);
		break;
	case CHP_VARY_ON:
		sch->opm |= mask;
		sch->lpm |= mask;
		io_subchannel_verify(sch);
		break;
	case CHP_OFFLINE:
		if (cio_update_schib(sch))
			return -ENODEV;
		io_subchannel_terminate_path(sch, mask);
		break;
	case CHP_ONLINE:
		if (cio_update_schib(sch))
			return -ENODEV;
		sch->lpm |= mask & sch->opm;
		io_subchannel_verify(sch);
		break;
	}
	return 0;
}

static void io_subchannel_quiesce(struct subchannel *sch)
{
	struct ccw_device *cdev;
	int ret;

	spin_lock_irq(sch->lock);
	cdev = sch_get_cdev(sch);
	if (cio_is_console(sch->schid))
		goto out_unlock;
	if (!sch->schib.pmcw.ena)
		goto out_unlock;
	ret = cio_disable_subchannel(sch);
	if (ret != -EBUSY)
		goto out_unlock;
	if (cdev->handler)
		cdev->handler(cdev, cdev->private->intparm, ERR_PTR(-EIO));
	while (ret == -EBUSY) {
		cdev->private->state = DEV_STATE_QUIESCE;
		ret = ccw_device_cancel_halt_clear(cdev);
		if (ret == -EBUSY) {
			ccw_device_set_timeout(cdev, HZ/10);
			spin_unlock_irq(sch->lock);
			wait_event(cdev->private->wait_q,
				   cdev->private->state != DEV_STATE_QUIESCE);
			spin_lock_irq(sch->lock);
		}
		ret = cio_disable_subchannel(sch);
	}
out_unlock:
	spin_unlock_irq(sch->lock);
}

static void io_subchannel_shutdown(struct subchannel *sch)
{
	io_subchannel_quiesce(sch);
}

static int device_is_disconnected(struct ccw_device *cdev)
{
	if (!cdev)
		return 0;
	return (cdev->private->state == DEV_STATE_DISCONNECTED ||
		cdev->private->state == DEV_STATE_DISCONNECTED_SENSE_ID);
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
		CIO_MSG_EVENT(4, "recovery: end\n");
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

static void ccw_device_schedule_recovery(void)
{
	unsigned long flags;

	CIO_MSG_EVENT(4, "recovery: schedule\n");
	spin_lock_irqsave(&recovery_lock, flags);
	if (!timer_pending(&recovery_timer) || (recovery_phase != 0)) {
		recovery_phase = 0;
		mod_timer(&recovery_timer, jiffies + recovery_delay[0] * HZ);
	}
	spin_unlock_irqrestore(&recovery_lock, flags);
}

static int purge_fn(struct device *dev, void *data)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_dev_id *id = &cdev->private->dev_id;

	spin_lock_irq(cdev->ccwlock);
	if (is_blacklisted(id->ssid, id->devno) &&
	    (cdev->private->state == DEV_STATE_OFFLINE)) {
		CIO_MSG_EVENT(3, "ccw: purging 0.%x.%04x\n", id->ssid,
			      id->devno);
		ccw_device_sched_todo(cdev, CDEV_TODO_UNREG);
	}
	spin_unlock_irq(cdev->ccwlock);
	/* Abort loop in case of pending signal. */
	if (signal_pending(current))
		return -EINTR;

	return 0;
}

/**
 * ccw_purge_blacklisted - purge unused, blacklisted devices
 *
 * Unregister all ccw devices that are offline and on the blacklist.
 */
int ccw_purge_blacklisted(void)
{
	CIO_MSG_EVENT(2, "ccw: purging blacklisted devices\n");
	bus_for_each_dev(&ccw_bus_type, NULL, NULL, purge_fn);
	return 0;
}

void ccw_device_set_disconnected(struct ccw_device *cdev)
{
	if (!cdev)
		return;
	ccw_device_set_timeout(cdev, 0);
	cdev->private->flags.fake_irb = 0;
	cdev->private->state = DEV_STATE_DISCONNECTED;
	if (cdev->online)
		ccw_device_schedule_recovery();
}

void ccw_device_set_notoper(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);

	CIO_TRACE_EVENT(2, "notoper");
	CIO_TRACE_EVENT(2, dev_name(&sch->dev));
	ccw_device_set_timeout(cdev, 0);
	cio_disable_subchannel(sch);
	cdev->private->state = DEV_STATE_NOT_OPER;
}

enum io_sch_action {
	IO_SCH_UNREG,
	IO_SCH_ORPH_UNREG,
	IO_SCH_ATTACH,
	IO_SCH_UNREG_ATTACH,
	IO_SCH_ORPH_ATTACH,
	IO_SCH_REPROBE,
	IO_SCH_VERIFY,
	IO_SCH_DISC,
	IO_SCH_NOP,
};

static enum io_sch_action sch_get_action(struct subchannel *sch)
{
	struct ccw_device *cdev;

	cdev = sch_get_cdev(sch);
	if (cio_update_schib(sch)) {
		/* Not operational. */
		if (!cdev)
			return IO_SCH_UNREG;
		if (!ccw_device_notify(cdev, CIO_GONE))
			return IO_SCH_UNREG;
		return IO_SCH_ORPH_UNREG;
	}
	/* Operational. */
	if (!cdev)
		return IO_SCH_ATTACH;
	if (sch->schib.pmcw.dev != cdev->private->dev_id.devno) {
		if (!ccw_device_notify(cdev, CIO_GONE))
			return IO_SCH_UNREG_ATTACH;
		return IO_SCH_ORPH_ATTACH;
	}
	if ((sch->schib.pmcw.pam & sch->opm) == 0) {
		if (!ccw_device_notify(cdev, CIO_NO_PATH))
			return IO_SCH_UNREG;
		return IO_SCH_DISC;
	}
	if (device_is_disconnected(cdev))
		return IO_SCH_REPROBE;
	if (cdev->online)
		return IO_SCH_VERIFY;
	return IO_SCH_NOP;
}

/**
 * io_subchannel_sch_event - process subchannel event
 * @sch: subchannel
 * @process: non-zero if function is called in process context
 *
 * An unspecified event occurred for this subchannel. Adjust data according
 * to the current operational state of the subchannel and device. Return
 * zero when the event has been handled sufficiently or -EAGAIN when this
 * function should be called again in process context.
 */
static int io_subchannel_sch_event(struct subchannel *sch, int process)
{
	unsigned long flags;
	struct ccw_device *cdev;
	struct ccw_dev_id dev_id;
	enum io_sch_action action;
	int rc = -EAGAIN;

	spin_lock_irqsave(sch->lock, flags);
	if (!device_is_registered(&sch->dev))
		goto out_unlock;
	if (work_pending(&sch->todo_work))
		goto out_unlock;
	cdev = sch_get_cdev(sch);
	if (cdev && work_pending(&cdev->private->todo_work))
		goto out_unlock;
	action = sch_get_action(sch);
	CIO_MSG_EVENT(2, "event: sch 0.%x.%04x, process=%d, action=%d\n",
		      sch->schid.ssid, sch->schid.sch_no, process,
		      action);
	/* Perform immediate actions while holding the lock. */
	switch (action) {
	case IO_SCH_REPROBE:
		/* Trigger device recognition. */
		ccw_device_trigger_reprobe(cdev);
		rc = 0;
		goto out_unlock;
	case IO_SCH_VERIFY:
		/* Trigger path verification. */
		io_subchannel_verify(sch);
		rc = 0;
		goto out_unlock;
	case IO_SCH_DISC:
		ccw_device_set_disconnected(cdev);
		rc = 0;
		goto out_unlock;
	case IO_SCH_ORPH_UNREG:
	case IO_SCH_ORPH_ATTACH:
		ccw_device_set_disconnected(cdev);
		break;
	case IO_SCH_UNREG_ATTACH:
	case IO_SCH_UNREG:
		if (cdev)
			ccw_device_set_notoper(cdev);
		break;
	case IO_SCH_NOP:
		rc = 0;
		goto out_unlock;
	default:
		break;
	}
	spin_unlock_irqrestore(sch->lock, flags);
	/* All other actions require process context. */
	if (!process)
		goto out;
	/* Handle attached ccw device. */
	switch (action) {
	case IO_SCH_ORPH_UNREG:
	case IO_SCH_ORPH_ATTACH:
		/* Move ccw device to orphanage. */
		rc = ccw_device_move_to_orph(cdev);
		if (rc)
			goto out;
		break;
	case IO_SCH_UNREG_ATTACH:
		/* Unregister ccw device. */
		ccw_device_unregister(cdev);
		break;
	default:
		break;
	}
	/* Handle subchannel. */
	switch (action) {
	case IO_SCH_ORPH_UNREG:
	case IO_SCH_UNREG:
		css_sch_device_unregister(sch);
		break;
	case IO_SCH_ORPH_ATTACH:
	case IO_SCH_UNREG_ATTACH:
	case IO_SCH_ATTACH:
		dev_id.ssid = sch->schid.ssid;
		dev_id.devno = sch->schib.pmcw.dev;
		cdev = get_ccwdev_by_dev_id(&dev_id);
		if (!cdev) {
			sch_create_and_recog_new_device(sch);
			break;
		}
		rc = ccw_device_move_to_sch(cdev, sch);
		if (rc) {
			/* Release reference from get_ccwdev_by_dev_id() */
			put_device(&cdev->dev);
			goto out;
		}
		spin_lock_irqsave(sch->lock, flags);
		ccw_device_trigger_reprobe(cdev);
		spin_unlock_irqrestore(sch->lock, flags);
		/* Release reference from get_ccwdev_by_dev_id() */
		put_device(&cdev->dev);
		break;
	default:
		break;
	}
	return 0;

out_unlock:
	spin_unlock_irqrestore(sch->lock, flags);
out:
	return rc;
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

static int ccw_device_console_enable(struct ccw_device *cdev,
				     struct subchannel *sch)
{
	int rc;

	/* Attach subchannel private data. */
	sch->private = cio_get_console_priv();
	memset(sch->private, 0, sizeof(struct io_subchannel_private));
	io_subchannel_init_fields(sch);
	rc = cio_commit_config(sch);
	if (rc)
		return rc;
	sch->driver = &io_subchannel_driver;
	/* Initialize the ccw_device structure. */
	cdev->dev.parent= &sch->dev;
	sch_set_cdev(sch, cdev);
	io_subchannel_recog(cdev, sch);
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
	return rc;
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

static int ccw_device_pm_restore(struct device *dev);

int ccw_device_force_console(void)
{
	if (!console_cdev_in_use)
		return -ENODEV;
	return ccw_device_pm_restore(&console_cdev.dev);
}
EXPORT_SYMBOL_GPL(ccw_device_force_console);
#endif

/*
 * get ccw_device matching the busid, but only if owned by cdrv
 */
static int
__ccwdev_check_busid(struct device *dev, void *id)
{
	char *bus_id;

	bus_id = id;

	return (strcmp(bus_id, dev_name(dev)) == 0);
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
			CIO_MSG_EVENT(0, "ccw_device_offline returned %d, "
				      "device 0.%x.%04x\n",
				      ret, cdev->private->dev_id.ssid,
				      cdev->private->dev_id.devno);
		/* Give up reference obtained in ccw_device_set_online(). */
		put_device(&cdev->dev);
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

static int ccw_device_pm_prepare(struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);

	if (work_pending(&cdev->private->todo_work))
		return -EAGAIN;
	/* Fail while device is being set online/offline. */
	if (atomic_read(&cdev->private->onoff))
		return -EAGAIN;

	if (cdev->online && cdev->drv && cdev->drv->prepare)
		return cdev->drv->prepare(cdev);

	return 0;
}

static void ccw_device_pm_complete(struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);

	if (cdev->online && cdev->drv && cdev->drv->complete)
		cdev->drv->complete(cdev);
}

static int ccw_device_pm_freeze(struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	int ret, cm_enabled;

	/* Fail suspend while device is in transistional state. */
	if (!dev_fsm_final_state(cdev))
		return -EAGAIN;
	if (!cdev->online)
		return 0;
	if (cdev->drv && cdev->drv->freeze) {
		ret = cdev->drv->freeze(cdev);
		if (ret)
			return ret;
	}

	spin_lock_irq(sch->lock);
	cm_enabled = cdev->private->cmb != NULL;
	spin_unlock_irq(sch->lock);
	if (cm_enabled) {
		/* Don't have the css write on memory. */
		ret = ccw_set_cmf(cdev, 0);
		if (ret)
			return ret;
	}
	/* From here on, disallow device driver I/O. */
	spin_lock_irq(sch->lock);
	ret = cio_disable_subchannel(sch);
	spin_unlock_irq(sch->lock);

	return ret;
}

static int ccw_device_pm_thaw(struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	int ret, cm_enabled;

	if (!cdev->online)
		return 0;

	spin_lock_irq(sch->lock);
	/* Allow device driver I/O again. */
	ret = cio_enable_subchannel(sch, (u32)(addr_t)sch);
	cm_enabled = cdev->private->cmb != NULL;
	spin_unlock_irq(sch->lock);
	if (ret)
		return ret;

	if (cm_enabled) {
		ret = ccw_set_cmf(cdev, 1);
		if (ret)
			return ret;
	}

	if (cdev->drv && cdev->drv->thaw)
		ret = cdev->drv->thaw(cdev);

	return ret;
}

static void __ccw_device_pm_restore(struct ccw_device *cdev)
{
	struct subchannel *sch = to_subchannel(cdev->dev.parent);

	if (cio_is_console(sch->schid))
		goto out;
	/*
	 * While we were sleeping, devices may have gone or become
	 * available again. Kick re-detection.
	 */
	spin_lock_irq(sch->lock);
	cdev->private->flags.resuming = 1;
	ccw_device_recognition(cdev);
	spin_unlock_irq(sch->lock);
	wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev) ||
		   cdev->private->state == DEV_STATE_DISCONNECTED);
out:
	cdev->private->flags.resuming = 0;
}

static int resume_handle_boxed(struct ccw_device *cdev)
{
	cdev->private->state = DEV_STATE_BOXED;
	if (ccw_device_notify(cdev, CIO_BOXED))
		return 0;
	ccw_device_sched_todo(cdev, CDEV_TODO_UNREG);
	return -ENODEV;
}

static int resume_handle_disc(struct ccw_device *cdev)
{
	cdev->private->state = DEV_STATE_DISCONNECTED;
	if (ccw_device_notify(cdev, CIO_GONE))
		return 0;
	ccw_device_sched_todo(cdev, CDEV_TODO_UNREG);
	return -ENODEV;
}

static int ccw_device_pm_restore(struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct subchannel *sch = to_subchannel(cdev->dev.parent);
	int ret = 0, cm_enabled;

	__ccw_device_pm_restore(cdev);
	spin_lock_irq(sch->lock);
	if (cio_is_console(sch->schid)) {
		cio_enable_subchannel(sch, (u32)(addr_t)sch);
		spin_unlock_irq(sch->lock);
		goto out_restore;
	}
	cdev->private->flags.donotify = 0;
	/* check recognition results */
	switch (cdev->private->state) {
	case DEV_STATE_OFFLINE:
		break;
	case DEV_STATE_BOXED:
		ret = resume_handle_boxed(cdev);
		spin_unlock_irq(sch->lock);
		if (ret)
			goto out;
		goto out_restore;
	case DEV_STATE_DISCONNECTED:
		goto out_disc_unlock;
	default:
		goto out_unreg_unlock;
	}
	/* check if the device id has changed */
	if (sch->schib.pmcw.dev != cdev->private->dev_id.devno) {
		CIO_MSG_EVENT(0, "resume: sch 0.%x.%04x: failed (devno "
			      "changed from %04x to %04x)\n",
			      sch->schid.ssid, sch->schid.sch_no,
			      cdev->private->dev_id.devno,
			      sch->schib.pmcw.dev);
		goto out_unreg_unlock;
	}
	/* check if the device type has changed */
	if (!ccw_device_test_sense_data(cdev)) {
		ccw_device_update_sense_data(cdev);
		ccw_device_sched_todo(cdev, CDEV_TODO_REBIND);
		ret = -ENODEV;
		goto out_unlock;
	}
	if (!cdev->online) {
		ret = 0;
		goto out_unlock;
	}
	ret = ccw_device_online(cdev);
	if (ret)
		goto out_disc_unlock;

	cm_enabled = cdev->private->cmb != NULL;
	spin_unlock_irq(sch->lock);

	wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	if (cdev->private->state != DEV_STATE_ONLINE) {
		spin_lock_irq(sch->lock);
		goto out_disc_unlock;
	}
	if (cm_enabled) {
		ret = ccw_set_cmf(cdev, 1);
		if (ret) {
			CIO_MSG_EVENT(2, "resume: cdev 0.%x.%04x: cmf failed "
				      "(rc=%d)\n", cdev->private->dev_id.ssid,
				      cdev->private->dev_id.devno, ret);
			ret = 0;
		}
	}

out_restore:
	if (cdev->online && cdev->drv && cdev->drv->restore)
		ret = cdev->drv->restore(cdev);
out:
	return ret;

out_disc_unlock:
	ret = resume_handle_disc(cdev);
	spin_unlock_irq(sch->lock);
	if (ret)
		return ret;
	goto out_restore;

out_unreg_unlock:
	ccw_device_sched_todo(cdev, CDEV_TODO_UNREG_EVAL);
	ret = -ENODEV;
out_unlock:
	spin_unlock_irq(sch->lock);
	return ret;
}

static const struct dev_pm_ops ccw_pm_ops = {
	.prepare = ccw_device_pm_prepare,
	.complete = ccw_device_pm_complete,
	.freeze = ccw_device_pm_freeze,
	.thaw = ccw_device_pm_thaw,
	.restore = ccw_device_pm_restore,
};

struct bus_type ccw_bus_type = {
	.name   = "ccw",
	.match  = ccw_bus_match,
	.uevent = ccw_uevent,
	.probe  = ccw_device_probe,
	.remove = ccw_device_remove,
	.shutdown = ccw_device_shutdown,
	.pm = &ccw_pm_ops,
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

static void ccw_device_todo(struct work_struct *work)
{
	struct ccw_device_private *priv;
	struct ccw_device *cdev;
	struct subchannel *sch;
	enum cdev_todo todo;

	priv = container_of(work, struct ccw_device_private, todo_work);
	cdev = priv->cdev;
	sch = to_subchannel(cdev->dev.parent);
	/* Find out todo. */
	spin_lock_irq(cdev->ccwlock);
	todo = priv->todo;
	priv->todo = CDEV_TODO_NOTHING;
	CIO_MSG_EVENT(4, "cdev_todo: cdev=0.%x.%04x todo=%d\n",
		      priv->dev_id.ssid, priv->dev_id.devno, todo);
	spin_unlock_irq(cdev->ccwlock);
	/* Perform todo. */
	switch (todo) {
	case CDEV_TODO_ENABLE_CMF:
		cmf_reenable(cdev);
		break;
	case CDEV_TODO_REBIND:
		ccw_device_do_unbind_bind(cdev);
		break;
	case CDEV_TODO_REGISTER:
		io_subchannel_register(cdev);
		break;
	case CDEV_TODO_UNREG_EVAL:
		if (!sch_is_pseudo_sch(sch))
			css_schedule_eval(sch->schid);
		/* fall-through */
	case CDEV_TODO_UNREG:
		if (sch_is_pseudo_sch(sch))
			ccw_device_unregister(cdev);
		else
			ccw_device_call_sch_unregister(cdev);
		break;
	default:
		break;
	}
	/* Release workqueue ref. */
	put_device(&cdev->dev);
}

/**
 * ccw_device_sched_todo - schedule ccw device operation
 * @cdev: ccw device
 * @todo: todo
 *
 * Schedule the operation identified by @todo to be performed on the slow path
 * workqueue. Do nothing if another operation with higher priority is already
 * scheduled. Needs to be called with ccwdev lock held.
 */
void ccw_device_sched_todo(struct ccw_device *cdev, enum cdev_todo todo)
{
	CIO_MSG_EVENT(4, "cdev_todo: sched cdev=0.%x.%04x todo=%d\n",
		      cdev->private->dev_id.ssid, cdev->private->dev_id.devno,
		      todo);
	if (cdev->private->todo >= todo)
		return;
	cdev->private->todo = todo;
	/* Get workqueue ref. */
	if (!get_device(&cdev->dev))
		return;
	if (!queue_work(slow_path_wq, &cdev->private->todo_work)) {
		/* Already queued, release workqueue ref. */
		put_device(&cdev->dev);
	}
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccw_device_set_online);
EXPORT_SYMBOL(ccw_device_set_offline);
EXPORT_SYMBOL(ccw_driver_register);
EXPORT_SYMBOL(ccw_driver_unregister);
EXPORT_SYMBOL(get_ccwdev_by_busid);
EXPORT_SYMBOL(ccw_bus_type);
EXPORT_SYMBOL(ccw_device_work);
EXPORT_SYMBOL_GPL(ccw_device_get_subchannel_id);
