/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Based on elements of hwmon and input subsystems.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include "iio.h"
#include "trigger_consumer.h"

#define IIO_ID_PREFIX "device"
#define IIO_ID_FORMAT IIO_ID_PREFIX "%d"

/* IDR to assign each registered device a unique id*/
static DEFINE_IDA(iio_ida);
/* IDR to allocate character device minor numbers */
static DEFINE_IDA(iio_chrdev_ida);
/* Lock used to protect both of the above */
static DEFINE_SPINLOCK(iio_ida_lock);

dev_t iio_devt;
EXPORT_SYMBOL(iio_devt);

#define IIO_DEV_MAX 256
struct bus_type iio_bus_type = {
	.name = "iio",
};
EXPORT_SYMBOL(iio_bus_type);

void __iio_change_event(struct iio_detected_event_list *ev,
			int ev_code,
			s64 timestamp)
{
	ev->ev.id = ev_code;
	ev->ev.timestamp = timestamp;
}
EXPORT_SYMBOL(__iio_change_event);

/* Used both in the interrupt line put events and the ring buffer ones */

/* Note that in it's current form someone has to be listening before events
 * are queued. Hence a client MUST open the chrdev before the ring buffer is
 * switched on.
 */
int __iio_push_event(struct iio_event_interface *ev_int,
		     int ev_code,
		     s64 timestamp,
		     struct iio_shared_ev_pointer *
		     shared_pointer_p)
{
	struct iio_detected_event_list *ev;
	int ret = 0;

	/* Does anyone care? */
	mutex_lock(&ev_int->event_list_lock);
	if (test_bit(IIO_BUSY_BIT_POS, &ev_int->handler.flags)) {
		if (ev_int->current_events == ev_int->max_events) {
			mutex_unlock(&ev_int->event_list_lock);
			return 0;
		}
		ev = kmalloc(sizeof(*ev), GFP_KERNEL);
		if (ev == NULL) {
			ret = -ENOMEM;
			mutex_unlock(&ev_int->event_list_lock);
			goto error_ret;
		}
		ev->ev.id = ev_code;
		ev->ev.timestamp = timestamp;
		ev->shared_pointer = shared_pointer_p;
		if (ev->shared_pointer)
			shared_pointer_p->ev_p = ev;

		list_add_tail(&ev->list, &ev_int->det_events.list);
		ev_int->current_events++;
		mutex_unlock(&ev_int->event_list_lock);
		wake_up_interruptible(&ev_int->wait);
	} else
		mutex_unlock(&ev_int->event_list_lock);

error_ret:
	return ret;
}
EXPORT_SYMBOL(__iio_push_event);

int iio_push_event(struct iio_dev *dev_info,
		   int ev_line,
		   int ev_code,
		   s64 timestamp)
{
	return __iio_push_event(&dev_info->event_interfaces[ev_line],
				ev_code, timestamp, NULL);
}
EXPORT_SYMBOL(iio_push_event);

/* Generic interrupt line interrupt handler */
static irqreturn_t iio_interrupt_handler(int irq, void *_int_info)
{
	struct iio_interrupt *int_info = _int_info;
	struct iio_dev *dev_info = int_info->dev_info;
	struct iio_event_handler_list *p;
	s64 time_ns;
	unsigned long flags;

	spin_lock_irqsave(&int_info->ev_list_lock, flags);
	if (list_empty(&int_info->ev_list)) {
		spin_unlock_irqrestore(&int_info->ev_list_lock, flags);
		return IRQ_NONE;
	}

	time_ns = iio_get_time_ns();
	list_for_each_entry(p, &int_info->ev_list, list) {
		disable_irq_nosync(irq);
		p->handler(dev_info, 1, time_ns, !(p->refcount > 1));
	}
	spin_unlock_irqrestore(&int_info->ev_list_lock, flags);

	return IRQ_HANDLED;
}

static struct iio_interrupt *iio_allocate_interrupt(void)
{
	struct iio_interrupt *i = kmalloc(sizeof *i, GFP_KERNEL);
	if (i) {
		spin_lock_init(&i->ev_list_lock);
		INIT_LIST_HEAD(&i->ev_list);
	}
	return i;
}

/* Confirming the validity of supplied irq is left to drivers.*/
int iio_register_interrupt_line(unsigned int irq,
				struct iio_dev *dev_info,
				int line_number,
				unsigned long type,
				const char *name)
{
	int ret;

	dev_info->interrupts[line_number] = iio_allocate_interrupt();
	if (dev_info->interrupts[line_number] == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	dev_info->interrupts[line_number]->line_number = line_number;
	dev_info->interrupts[line_number]->irq = irq;
	dev_info->interrupts[line_number]->dev_info = dev_info;

	/* Possibly only request on demand?
	 * Can see this may complicate the handling of interrupts.
	 * However, with this approach we might end up handling lots of
	 * events no-one cares about.*/
	ret = request_irq(irq,
			  &iio_interrupt_handler,
			  type,
			  name,
			  dev_info->interrupts[line_number]);

error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_register_interrupt_line);

/* This turns up an awful lot */
ssize_t iio_read_const_attr(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%s\n", to_iio_const_attr(attr)->string);
}
EXPORT_SYMBOL(iio_read_const_attr);

/* Before this runs the interrupt generator must have been disabled */
void iio_unregister_interrupt_line(struct iio_dev *dev_info, int line_number)
{
	/* make sure the interrupt handlers are all done */
	flush_scheduled_work();
	free_irq(dev_info->interrupts[line_number]->irq,
		 dev_info->interrupts[line_number]);
	kfree(dev_info->interrupts[line_number]);
}
EXPORT_SYMBOL(iio_unregister_interrupt_line);

/* Reference counted add and remove */
void iio_add_event_to_list(struct iio_event_handler_list *el,
			  struct list_head *head)
{
	unsigned long flags;
	struct iio_interrupt *inter = to_iio_interrupt(head);

	/* take mutex to protect this element */
	mutex_lock(&el->exist_lock);
	if (el->refcount == 0) {
		/* Take the event list spin lock */
		spin_lock_irqsave(&inter->ev_list_lock, flags);
		list_add(&el->list, head);
		spin_unlock_irqrestore(&inter->ev_list_lock, flags);
	}
	el->refcount++;
	mutex_unlock(&el->exist_lock);
}
EXPORT_SYMBOL(iio_add_event_to_list);

void iio_remove_event_from_list(struct iio_event_handler_list *el,
			       struct list_head *head)
{
	unsigned long flags;
	struct iio_interrupt *inter = to_iio_interrupt(head);

	mutex_lock(&el->exist_lock);
	el->refcount--;
	if (el->refcount == 0) {
		/* Take the event list spin lock */
		spin_lock_irqsave(&inter->ev_list_lock, flags);
		list_del_init(&el->list);
		spin_unlock_irqrestore(&inter->ev_list_lock, flags);
	}
	mutex_unlock(&el->exist_lock);
}
EXPORT_SYMBOL(iio_remove_event_from_list);

static ssize_t iio_event_chrdev_read(struct file *filep,
				     char __user *buf,
				     size_t count,
				     loff_t *f_ps)
{
	struct iio_event_interface *ev_int = filep->private_data;
	struct iio_detected_event_list *el;
	int ret;
	size_t len;

	mutex_lock(&ev_int->event_list_lock);
	if (list_empty(&ev_int->det_events.list)) {
		if (filep->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto error_mutex_unlock;
		}
		mutex_unlock(&ev_int->event_list_lock);
		/* Blocking on device; waiting for something to be there */
		ret = wait_event_interruptible(ev_int->wait,
					       !list_empty(&ev_int
							   ->det_events.list));
		if (ret)
			goto error_ret;
		/* Single access device so noone else can get the data */
		mutex_lock(&ev_int->event_list_lock);
	}

	el = list_first_entry(&ev_int->det_events.list,
			      struct iio_detected_event_list,
			      list);
	len = sizeof el->ev;
	if (copy_to_user(buf, &(el->ev), len)) {
		ret = -EFAULT;
		goto error_mutex_unlock;
	}
	list_del(&el->list);
	ev_int->current_events--;
	mutex_unlock(&ev_int->event_list_lock);
	/*
	 * Possible concurency issue if an update of this event is on its way
	 * through. May lead to new event being removed whilst the reported
	 * event was the unescalated event. In typical use case this is not a
	 * problem as userspace will say read half the buffer due to a 50%
	 * full event which would make the correct 100% full incorrect anyway.
	 */
	if (el->shared_pointer) {
		spin_lock(&el->shared_pointer->lock);
		(el->shared_pointer->ev_p) = NULL;
		spin_unlock(&el->shared_pointer->lock);
	}
	kfree(el);

	return len;

error_mutex_unlock:
	mutex_unlock(&ev_int->event_list_lock);
error_ret:

	return ret;
}

static int iio_event_chrdev_release(struct inode *inode, struct file *filep)
{
	struct iio_handler *hand = iio_cdev_to_handler(inode->i_cdev);
	struct iio_event_interface *ev_int = hand->private;
	struct iio_detected_event_list *el, *t;

	mutex_lock(&ev_int->event_list_lock);
	clear_bit(IIO_BUSY_BIT_POS, &ev_int->handler.flags);
	/*
	 * In order to maintain a clean state for reopening,
	 * clear out any awaiting events. The mask will prevent
	 * any new __iio_push_event calls running.
	 */
	list_for_each_entry_safe(el, t, &ev_int->det_events.list, list) {
		list_del(&el->list);
		kfree(el);
	}
	mutex_unlock(&ev_int->event_list_lock);

	return 0;
}

static int iio_event_chrdev_open(struct inode *inode, struct file *filep)
{
	struct iio_handler *hand = iio_cdev_to_handler(inode->i_cdev);
	struct iio_event_interface *ev_int = hand->private;

	mutex_lock(&ev_int->event_list_lock);
	if (test_and_set_bit(IIO_BUSY_BIT_POS, &hand->flags)) {
		fops_put(filep->f_op);
		mutex_unlock(&ev_int->event_list_lock);
		return -EBUSY;
	}
	filep->private_data = hand->private;
	mutex_unlock(&ev_int->event_list_lock);

	return 0;
}

static const struct file_operations iio_event_chrdev_fileops = {
	.read =  iio_event_chrdev_read,
	.release = iio_event_chrdev_release,
	.open = iio_event_chrdev_open,
	.owner = THIS_MODULE,
};

static void iio_event_dev_release(struct device *dev)
{
	struct iio_event_interface *ev_int
		= container_of(dev, struct iio_event_interface, dev);
	cdev_del(&ev_int->handler.chrdev);
	iio_device_free_chrdev_minor(MINOR(dev->devt));
};

static struct device_type iio_event_type = {
	.release = iio_event_dev_release,
};

int iio_device_get_chrdev_minor(void)
{
	int ret, val;

ida_again:
	if (unlikely(ida_pre_get(&iio_chrdev_ida, GFP_KERNEL) == 0))
		return -ENOMEM;
	spin_lock(&iio_ida_lock);
	ret = ida_get_new(&iio_chrdev_ida, &val);
	spin_unlock(&iio_ida_lock);
	if (unlikely(ret == -EAGAIN))
		goto ida_again;
	else if (unlikely(ret))
		return ret;
	if (val > IIO_DEV_MAX)
		return -ENOMEM;
	return val;
}

void iio_device_free_chrdev_minor(int val)
{
	spin_lock(&iio_ida_lock);
	ida_remove(&iio_chrdev_ida, val);
	spin_unlock(&iio_ida_lock);
}

int iio_setup_ev_int(struct iio_event_interface *ev_int,
		     const char *name,
		     struct module *owner,
		     struct device *dev)
{
	int ret, minor;

	ev_int->dev.bus = &iio_bus_type;
	ev_int->dev.parent = dev;
	ev_int->dev.type = &iio_event_type;
	device_initialize(&ev_int->dev);

	minor = iio_device_get_chrdev_minor();
	if (minor < 0) {
		ret = minor;
		goto error_device_put;
	}
	ev_int->dev.devt = MKDEV(MAJOR(iio_devt), minor);
	dev_set_name(&ev_int->dev, "%s", name);

	ret = device_add(&ev_int->dev);
	if (ret)
		goto error_free_minor;

	cdev_init(&ev_int->handler.chrdev, &iio_event_chrdev_fileops);
	ev_int->handler.chrdev.owner = owner;

	mutex_init(&ev_int->event_list_lock);
	/* discussion point - make this variable? */
	ev_int->max_events = 10;
	ev_int->current_events = 0;
	INIT_LIST_HEAD(&ev_int->det_events.list);
	init_waitqueue_head(&ev_int->wait);
	ev_int->handler.private = ev_int;
	ev_int->handler.flags = 0;

	ret = cdev_add(&ev_int->handler.chrdev, ev_int->dev.devt, 1);
	if (ret)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	device_unregister(&ev_int->dev);
error_free_minor:
	iio_device_free_chrdev_minor(minor);
error_device_put:
	put_device(&ev_int->dev);

	return ret;
}

void iio_free_ev_int(struct iio_event_interface *ev_int)
{
	device_unregister(&ev_int->dev);
	put_device(&ev_int->dev);
}

static int __init iio_dev_init(void)
{
	int err;

	err = alloc_chrdev_region(&iio_devt, 0, IIO_DEV_MAX, "iio");
	if (err < 0)
		printk(KERN_ERR "%s: failed to allocate char dev region\n",
		       __FILE__);

	return err;
}

static void __exit iio_dev_exit(void)
{
	if (iio_devt)
		unregister_chrdev_region(iio_devt, IIO_DEV_MAX);
}

static int __init iio_init(void)
{
	int ret;

	/* Register sysfs bus */
	ret  = bus_register(&iio_bus_type);
	if (ret < 0) {
		printk(KERN_ERR
		       "%s could not register bus type\n",
			__FILE__);
		goto error_nothing;
	}

	ret = iio_dev_init();
	if (ret < 0)
		goto error_unregister_bus_type;

	return 0;

error_unregister_bus_type:
	bus_unregister(&iio_bus_type);
error_nothing:
	return ret;
}

static void __exit iio_exit(void)
{
	iio_dev_exit();
	bus_unregister(&iio_bus_type);
}

static int iio_device_register_sysfs(struct iio_dev *dev_info)
{
	int ret = 0;

	ret = sysfs_create_group(&dev_info->dev.kobj, dev_info->attrs);
	if (ret) {
		dev_err(dev_info->dev.parent,
			"Failed to register sysfs hooks\n");
		goto error_ret;
	}

error_ret:
	return ret;
}

static void iio_device_unregister_sysfs(struct iio_dev *dev_info)
{
	sysfs_remove_group(&dev_info->dev.kobj, dev_info->attrs);
}

/* Return a negative errno on failure */
int iio_get_new_ida_val(struct ida *this_ida)
{
	int ret;
	int val;

ida_again:
	if (unlikely(ida_pre_get(this_ida, GFP_KERNEL) == 0))
		return -ENOMEM;

	spin_lock(&iio_ida_lock);
	ret = ida_get_new(this_ida, &val);
	spin_unlock(&iio_ida_lock);
	if (unlikely(ret == -EAGAIN))
		goto ida_again;
	else if (unlikely(ret))
		return ret;

	return val;
}
EXPORT_SYMBOL(iio_get_new_ida_val);

void iio_free_ida_val(struct ida *this_ida, int id)
{
	spin_lock(&iio_ida_lock);
	ida_remove(this_ida, id);
	spin_unlock(&iio_ida_lock);
}
EXPORT_SYMBOL(iio_free_ida_val);

static int iio_device_register_id(struct iio_dev *dev_info,
				  struct ida *this_ida)
{
	dev_info->id = iio_get_new_ida_val(&iio_ida);
	if (dev_info->id < 0)
		return dev_info->id;
	return 0;
}

static void iio_device_unregister_id(struct iio_dev *dev_info)
{
	iio_free_ida_val(&iio_ida, dev_info->id);
}

static inline int __iio_add_event_config_attrs(struct iio_dev *dev_info, int i)
{
	int ret;
	/*p for adding, q for removing */
	struct attribute **attrp, **attrq;

	if (dev_info->event_conf_attrs && dev_info->event_conf_attrs[i].attrs) {
		attrp = dev_info->event_conf_attrs[i].attrs;
		while (*attrp) {
			ret =  sysfs_add_file_to_group(&dev_info->dev.kobj,
						       *attrp,
						       dev_info
						       ->event_attrs[i].name);
			if (ret)
				goto error_ret;
			attrp++;
		}
	}
	return 0;

error_ret:
	attrq = dev_info->event_conf_attrs[i].attrs;
	while (attrq != attrp) {
			sysfs_remove_file_from_group(&dev_info->dev.kobj,
					     *attrq,
					     dev_info->event_attrs[i].name);
		attrq++;
	}

	return ret;
}

static inline int __iio_remove_event_config_attrs(struct iio_dev *dev_info,
						  int i)
{
	struct attribute **attrq;

	if (dev_info->event_conf_attrs
		&& dev_info->event_conf_attrs[i].attrs) {
		attrq = dev_info->event_conf_attrs[i].attrs;
		while (*attrq) {
			sysfs_remove_file_from_group(&dev_info->dev.kobj,
						     *attrq,
						     dev_info
						     ->event_attrs[i].name);
			attrq++;
		}
	}

	return 0;
}

static int iio_device_register_eventset(struct iio_dev *dev_info)
{
	int ret = 0, i, j;

	if (dev_info->num_interrupt_lines == 0)
		return 0;

	dev_info->event_interfaces =
		kzalloc(sizeof(struct iio_event_interface)
			*dev_info->num_interrupt_lines,
			GFP_KERNEL);
	if (dev_info->event_interfaces == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	dev_info->interrupts = kzalloc(sizeof(struct iio_interrupt *)
				       *dev_info->num_interrupt_lines,
				       GFP_KERNEL);
	if (dev_info->interrupts == NULL) {
		ret = -ENOMEM;
		goto error_free_event_interfaces;
	}

	for (i = 0; i < dev_info->num_interrupt_lines; i++) {
		dev_info->event_interfaces[i].owner = dev_info->driver_module;

		snprintf(dev_info->event_interfaces[i]._name, 20,
			 "%s:event%d",
			 dev_name(&dev_info->dev),
			 i);

		ret = iio_setup_ev_int(&dev_info->event_interfaces[i],
				       (const char *)(dev_info
						      ->event_interfaces[i]
						      ._name),
				       dev_info->driver_module,
				       &dev_info->dev);
		if (ret) {
			dev_err(&dev_info->dev,
				"Could not get chrdev interface\n");
			goto error_free_setup_ev_ints;
		}

		dev_set_drvdata(&dev_info->event_interfaces[i].dev,
				(void *)dev_info);
		ret = sysfs_create_group(&dev_info
					->event_interfaces[i]
					.dev.kobj,
					&dev_info->event_attrs[i]);

		if (ret) {
			dev_err(&dev_info->dev,
				"Failed to register sysfs for event attrs");
			goto error_remove_sysfs_interfaces;
		}
	}

	for (i = 0; i < dev_info->num_interrupt_lines; i++) {
		ret = __iio_add_event_config_attrs(dev_info, i);
		if (ret)
			goto error_unregister_config_attrs;
	}

	return 0;

error_unregister_config_attrs:
	for (j = 0; j < i; j++)
		__iio_remove_event_config_attrs(dev_info, i);
	i = dev_info->num_interrupt_lines - 1;
error_remove_sysfs_interfaces:
	for (j = 0; j < i; j++)
		sysfs_remove_group(&dev_info
				   ->event_interfaces[j].dev.kobj,
				   &dev_info->event_attrs[j]);
error_free_setup_ev_ints:
	for (j = 0; j < i; j++)
		iio_free_ev_int(&dev_info->event_interfaces[j]);
	kfree(dev_info->interrupts);
error_free_event_interfaces:
	kfree(dev_info->event_interfaces);
error_ret:

	return ret;
}

static void iio_device_unregister_eventset(struct iio_dev *dev_info)
{
	int i;

	if (dev_info->num_interrupt_lines == 0)
		return;
	for (i = 0; i < dev_info->num_interrupt_lines; i++)
		sysfs_remove_group(&dev_info
				   ->event_interfaces[i].dev.kobj,
				   &dev_info->event_attrs[i]);

	for (i = 0; i < dev_info->num_interrupt_lines; i++)
		iio_free_ev_int(&dev_info->event_interfaces[i]);
	kfree(dev_info->interrupts);
	kfree(dev_info->event_interfaces);
}

static void iio_dev_release(struct device *device)
{
	struct iio_dev *dev = to_iio_dev(device);

	iio_put();
	kfree(dev);
}

static struct device_type iio_dev_type = {
	.name = "iio_device",
	.release = iio_dev_release,
};

struct iio_dev *iio_allocate_device(void)
{
	struct iio_dev *dev = kzalloc(sizeof *dev, GFP_KERNEL);

	if (dev) {
		dev->dev.type = &iio_dev_type;
		dev->dev.bus = &iio_bus_type;
		device_initialize(&dev->dev);
		dev_set_drvdata(&dev->dev, (void *)dev);
		mutex_init(&dev->mlock);
		iio_get();
	}

	return dev;
}
EXPORT_SYMBOL(iio_allocate_device);

void iio_free_device(struct iio_dev *dev)
{
	if (dev)
		iio_put_device(dev);
}
EXPORT_SYMBOL(iio_free_device);

int iio_device_register(struct iio_dev *dev_info)
{
	int ret;

	ret = iio_device_register_id(dev_info, &iio_ida);
	if (ret) {
		dev_err(&dev_info->dev, "Failed to get id\n");
		goto error_ret;
	}
	dev_set_name(&dev_info->dev, "device%d", dev_info->id);

	ret = device_add(&dev_info->dev);
	if (ret)
		goto error_free_ida;
	ret = iio_device_register_sysfs(dev_info);
	if (ret) {
		dev_err(dev_info->dev.parent,
			"Failed to register sysfs interfaces\n");
		goto error_del_device;
	}
	ret = iio_device_register_eventset(dev_info);
	if (ret) {
		dev_err(dev_info->dev.parent,
			"Failed to register event set\n");
		goto error_free_sysfs;
	}
	if (dev_info->modes & INDIO_RING_TRIGGERED)
		iio_device_register_trigger_consumer(dev_info);

	return 0;

error_free_sysfs:
	iio_device_unregister_sysfs(dev_info);
error_del_device:
	device_del(&dev_info->dev);
error_free_ida:
	iio_device_unregister_id(dev_info);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_device_register);

void iio_device_unregister(struct iio_dev *dev_info)
{
	if (dev_info->modes & INDIO_RING_TRIGGERED)
		iio_device_unregister_trigger_consumer(dev_info);
	iio_device_unregister_eventset(dev_info);
	iio_device_unregister_sysfs(dev_info);
	iio_device_unregister_id(dev_info);
	device_unregister(&dev_info->dev);
}
EXPORT_SYMBOL(iio_device_unregister);

void iio_put(void)
{
	module_put(THIS_MODULE);
}

void iio_get(void)
{
	__module_get(THIS_MODULE);
}

subsys_initcall(iio_init);
module_exit(iio_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Industrial I/O core");
MODULE_LICENSE("GPL");
