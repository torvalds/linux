/*
 *  The Serio abstraction module
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 *  Copyright (c) 2004 Dmitry Torokhov
 *  Copyright (c) 2003 Daniele Bellucci
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/freezer.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Serio abstraction core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(serio_interrupt);
EXPORT_SYMBOL(__serio_register_port);
EXPORT_SYMBOL(serio_unregister_port);
EXPORT_SYMBOL(serio_unregister_child_port);
EXPORT_SYMBOL(__serio_register_driver);
EXPORT_SYMBOL(serio_unregister_driver);
EXPORT_SYMBOL(serio_open);
EXPORT_SYMBOL(serio_close);
EXPORT_SYMBOL(serio_rescan);
EXPORT_SYMBOL(serio_reconnect);

/*
 * serio_mutex protects entire serio subsystem and is taken every time
 * serio port or driver registrered or unregistered.
 */
static DEFINE_MUTEX(serio_mutex);

static LIST_HEAD(serio_list);

static struct bus_type serio_bus;

static void serio_add_port(struct serio *serio);
static int serio_reconnect_port(struct serio *serio);
static void serio_disconnect_port(struct serio *serio);
static void serio_reconnect_chain(struct serio *serio);
static void serio_attach_driver(struct serio_driver *drv);

static int serio_connect_driver(struct serio *serio, struct serio_driver *drv)
{
	int retval;

	mutex_lock(&serio->drv_mutex);
	retval = drv->connect(serio, drv);
	mutex_unlock(&serio->drv_mutex);

	return retval;
}

static int serio_reconnect_driver(struct serio *serio)
{
	int retval = -1;

	mutex_lock(&serio->drv_mutex);
	if (serio->drv && serio->drv->reconnect)
		retval = serio->drv->reconnect(serio);
	mutex_unlock(&serio->drv_mutex);

	return retval;
}

static void serio_disconnect_driver(struct serio *serio)
{
	mutex_lock(&serio->drv_mutex);
	if (serio->drv)
		serio->drv->disconnect(serio);
	mutex_unlock(&serio->drv_mutex);
}

static int serio_match_port(const struct serio_device_id *ids, struct serio *serio)
{
	while (ids->type || ids->proto) {
		if ((ids->type == SERIO_ANY || ids->type == serio->id.type) &&
		    (ids->proto == SERIO_ANY || ids->proto == serio->id.proto) &&
		    (ids->extra == SERIO_ANY || ids->extra == serio->id.extra) &&
		    (ids->id == SERIO_ANY || ids->id == serio->id.id))
			return 1;
		ids++;
	}
	return 0;
}

/*
 * Basic serio -> driver core mappings
 */

static int serio_bind_driver(struct serio *serio, struct serio_driver *drv)
{
	int error;

	if (serio_match_port(drv->id_table, serio)) {

		serio->dev.driver = &drv->driver;
		if (serio_connect_driver(serio, drv)) {
			serio->dev.driver = NULL;
			return -ENODEV;
		}

		error = device_bind_driver(&serio->dev);
		if (error) {
			printk(KERN_WARNING
				"serio: device_bind_driver() failed "
				"for %s (%s) and %s, error: %d\n",
				serio->phys, serio->name,
				drv->description, error);
			serio_disconnect_driver(serio);
			serio->dev.driver = NULL;
			return error;
		}
	}
	return 0;
}

static void serio_find_driver(struct serio *serio)
{
	int error;

	error = device_attach(&serio->dev);
	if (error < 0)
		printk(KERN_WARNING
			"serio: device_attach() failed for %s (%s), error: %d\n",
			serio->phys, serio->name, error);
}


/*
 * Serio event processing.
 */

enum serio_event_type {
	SERIO_RESCAN_PORT,
	SERIO_RECONNECT_PORT,
	SERIO_RECONNECT_CHAIN,
	SERIO_REGISTER_PORT,
	SERIO_ATTACH_DRIVER,
};

struct serio_event {
	enum serio_event_type type;
	void *object;
	struct module *owner;
	struct list_head node;
};

static DEFINE_SPINLOCK(serio_event_lock);	/* protects serio_event_list */
static LIST_HEAD(serio_event_list);
static DECLARE_WAIT_QUEUE_HEAD(serio_wait);
static struct task_struct *serio_task;

static int serio_queue_event(void *object, struct module *owner,
			     enum serio_event_type event_type)
{
	unsigned long flags;
	struct serio_event *event;
	int retval = 0;

	spin_lock_irqsave(&serio_event_lock, flags);

	/*
	 * Scan event list for the other events for the same serio port,
	 * starting with the most recent one. If event is the same we
	 * do not need add new one. If event is of different type we
	 * need to add this event and should not look further because
	 * we need to preseve sequence of distinct events.
	 */
	list_for_each_entry_reverse(event, &serio_event_list, node) {
		if (event->object == object) {
			if (event->type == event_type)
				goto out;
			break;
		}
	}

	event = kmalloc(sizeof(struct serio_event), GFP_ATOMIC);
	if (!event) {
		printk(KERN_ERR
			"serio: Not enough memory to queue event %d\n",
			event_type);
		retval = -ENOMEM;
		goto out;
	}

	if (!try_module_get(owner)) {
		printk(KERN_WARNING
			"serio: Can't get module reference, dropping event %d\n",
			event_type);
		kfree(event);
		retval = -EINVAL;
		goto out;
	}

	event->type = event_type;
	event->object = object;
	event->owner = owner;

	list_add_tail(&event->node, &serio_event_list);
	wake_up(&serio_wait);

out:
	spin_unlock_irqrestore(&serio_event_lock, flags);
	return retval;
}

static void serio_free_event(struct serio_event *event)
{
	module_put(event->owner);
	kfree(event);
}

static void serio_remove_duplicate_events(struct serio_event *event)
{
	struct list_head *node, *next;
	struct serio_event *e;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_safe(node, next, &serio_event_list) {
		e = list_entry(node, struct serio_event, node);
		if (event->object == e->object) {
			/*
			 * If this event is of different type we should not
			 * look further - we only suppress duplicate events
			 * that were sent back-to-back.
			 */
			if (event->type != e->type)
				break;

			list_del_init(node);
			serio_free_event(e);
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
}


static struct serio_event *serio_get_event(void)
{
	struct serio_event *event;
	struct list_head *node;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	if (list_empty(&serio_event_list)) {
		spin_unlock_irqrestore(&serio_event_lock, flags);
		return NULL;
	}

	node = serio_event_list.next;
	event = list_entry(node, struct serio_event, node);
	list_del_init(node);

	spin_unlock_irqrestore(&serio_event_lock, flags);

	return event;
}

static void serio_handle_event(void)
{
	struct serio_event *event;

	mutex_lock(&serio_mutex);

	/*
	 * Note that we handle only one event here to give swsusp
	 * a chance to freeze kseriod thread. Serio events should
	 * be pretty rare so we are not concerned about taking
	 * performance hit.
	 */
	if ((event = serio_get_event())) {

		switch (event->type) {
			case SERIO_REGISTER_PORT:
				serio_add_port(event->object);
				break;

			case SERIO_RECONNECT_PORT:
				serio_reconnect_port(event->object);
				break;

			case SERIO_RESCAN_PORT:
				serio_disconnect_port(event->object);
				serio_find_driver(event->object);
				break;

			case SERIO_RECONNECT_CHAIN:
				serio_reconnect_chain(event->object);
				break;

			case SERIO_ATTACH_DRIVER:
				serio_attach_driver(event->object);
				break;

			default:
				break;
		}

		serio_remove_duplicate_events(event);
		serio_free_event(event);
	}

	mutex_unlock(&serio_mutex);
}

/*
 * Remove all events that have been submitted for a given
 * object, be it serio port or driver.
 */
static void serio_remove_pending_events(void *object)
{
	struct list_head *node, *next;
	struct serio_event *event;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_safe(node, next, &serio_event_list) {
		event = list_entry(node, struct serio_event, node);
		if (event->object == object) {
			list_del_init(node);
			serio_free_event(event);
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
}

/*
 * Destroy child serio port (if any) that has not been fully registered yet.
 *
 * Note that we rely on the fact that port can have only one child and therefore
 * only one child registration request can be pending. Additionally, children
 * are registered by driver's connect() handler so there can't be a grandchild
 * pending registration together with a child.
 */
static struct serio *serio_get_pending_child(struct serio *parent)
{
	struct serio_event *event;
	struct serio *serio, *child = NULL;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_entry(event, &serio_event_list, node) {
		if (event->type == SERIO_REGISTER_PORT) {
			serio = event->object;
			if (serio->parent == parent) {
				child = serio;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
	return child;
}

static int serio_thread(void *nothing)
{
	set_freezable();
	do {
		serio_handle_event();
		wait_event_freezable(serio_wait,
			kthread_should_stop() || !list_empty(&serio_event_list));
	} while (!kthread_should_stop());

	printk(KERN_DEBUG "serio: kseriod exiting\n");
	return 0;
}


/*
 * Serio port operations
 */

static ssize_t serio_show_description(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%s\n", serio->name);
}

static ssize_t serio_show_modalias(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);

	return sprintf(buf, "serio:ty%02Xpr%02Xid%02Xex%02X\n",
			serio->id.type, serio->id.proto, serio->id.id, serio->id.extra);
}

static ssize_t serio_show_id_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.type);
}

static ssize_t serio_show_id_proto(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.proto);
}

static ssize_t serio_show_id_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.id);
}

static ssize_t serio_show_id_extra(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.extra);
}

static DEVICE_ATTR(type, S_IRUGO, serio_show_id_type, NULL);
static DEVICE_ATTR(proto, S_IRUGO, serio_show_id_proto, NULL);
static DEVICE_ATTR(id, S_IRUGO, serio_show_id_id, NULL);
static DEVICE_ATTR(extra, S_IRUGO, serio_show_id_extra, NULL);

static struct attribute *serio_device_id_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_proto.attr,
	&dev_attr_id.attr,
	&dev_attr_extra.attr,
	NULL
};

static struct attribute_group serio_id_attr_group = {
	.name	= "id",
	.attrs	= serio_device_id_attrs,
};

static ssize_t serio_rebind_driver(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct serio *serio = to_serio_port(dev);
	struct device_driver *drv;
	int error;

	error = mutex_lock_interruptible(&serio_mutex);
	if (error)
		return error;

	if (!strncmp(buf, "none", count)) {
		serio_disconnect_port(serio);
	} else if (!strncmp(buf, "reconnect", count)) {
		serio_reconnect_chain(serio);
	} else if (!strncmp(buf, "rescan", count)) {
		serio_disconnect_port(serio);
		serio_find_driver(serio);
	} else if ((drv = driver_find(buf, &serio_bus)) != NULL) {
		serio_disconnect_port(serio);
		error = serio_bind_driver(serio, to_serio_driver(drv));
		put_driver(drv);
	} else {
		error = -EINVAL;
	}

	mutex_unlock(&serio_mutex);

	return error ? error : count;
}

static ssize_t serio_show_bind_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%s\n", serio->manual_bind ? "manual" : "auto");
}

static ssize_t serio_set_bind_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct serio *serio = to_serio_port(dev);
	int retval;

	retval = count;
	if (!strncmp(buf, "manual", count)) {
		serio->manual_bind = 1;
	} else if (!strncmp(buf, "auto", count)) {
		serio->manual_bind = 0;
	} else {
		retval = -EINVAL;
	}

	return retval;
}

static struct device_attribute serio_device_attrs[] = {
	__ATTR(description, S_IRUGO, serio_show_description, NULL),
	__ATTR(modalias, S_IRUGO, serio_show_modalias, NULL),
	__ATTR(drvctl, S_IWUSR, NULL, serio_rebind_driver),
	__ATTR(bind_mode, S_IWUSR | S_IRUGO, serio_show_bind_mode, serio_set_bind_mode),
	__ATTR_NULL
};


static void serio_release_port(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);

	kfree(serio);
	module_put(THIS_MODULE);
}

/*
 * Prepare serio port for registration.
 */
static void serio_init_port(struct serio *serio)
{
	static atomic_t serio_no = ATOMIC_INIT(0);

	__module_get(THIS_MODULE);

	INIT_LIST_HEAD(&serio->node);
	spin_lock_init(&serio->lock);
	mutex_init(&serio->drv_mutex);
	device_initialize(&serio->dev);
	dev_set_name(&serio->dev, "serio%ld",
			(long)atomic_inc_return(&serio_no) - 1);
	serio->dev.bus = &serio_bus;
	serio->dev.release = serio_release_port;
	if (serio->parent) {
		serio->dev.parent = &serio->parent->dev;
		serio->depth = serio->parent->depth + 1;
	} else
		serio->depth = 0;
	lockdep_set_subclass(&serio->lock, serio->depth);
}

/*
 * Complete serio port registration.
 * Driver core will attempt to find appropriate driver for the port.
 */
static void serio_add_port(struct serio *serio)
{
	int error;

	if (serio->parent) {
		serio_pause_rx(serio->parent);
		serio->parent->child = serio;
		serio_continue_rx(serio->parent);
	}

	list_add_tail(&serio->node, &serio_list);
	if (serio->start)
		serio->start(serio);
	error = device_add(&serio->dev);
	if (error)
		printk(KERN_ERR
			"serio: device_add() failed for %s (%s), error: %d\n",
			serio->phys, serio->name, error);
	else {
		serio->registered = 1;
		error = sysfs_create_group(&serio->dev.kobj, &serio_id_attr_group);
		if (error)
			printk(KERN_ERR
				"serio: sysfs_create_group() failed for %s (%s), error: %d\n",
				serio->phys, serio->name, error);
	}
}

/*
 * serio_destroy_port() completes deregistration process and removes
 * port from the system
 */
static void serio_destroy_port(struct serio *serio)
{
	struct serio *child;

	child = serio_get_pending_child(serio);
	if (child) {
		serio_remove_pending_events(child);
		put_device(&child->dev);
	}

	if (serio->stop)
		serio->stop(serio);

	if (serio->parent) {
		serio_pause_rx(serio->parent);
		serio->parent->child = NULL;
		serio_continue_rx(serio->parent);
		serio->parent = NULL;
	}

	if (serio->registered) {
		sysfs_remove_group(&serio->dev.kobj, &serio_id_attr_group);
		device_del(&serio->dev);
		serio->registered = 0;
	}

	list_del_init(&serio->node);
	serio_remove_pending_events(serio);
	put_device(&serio->dev);
}

/*
 * Reconnect serio port (re-initialize attached device).
 * If reconnect fails (old device is no longer attached or
 * there was no device to begin with) we do full rescan in
 * hope of finding a driver for the port.
 */
static int serio_reconnect_port(struct serio *serio)
{
	int error = serio_reconnect_driver(serio);

	if (error) {
		serio_disconnect_port(serio);
		serio_find_driver(serio);
	}

	return error;
}

/*
 * Reconnect serio port and all its children (re-initialize attached devices)
 */
static void serio_reconnect_chain(struct serio *serio)
{
	do {
		if (serio_reconnect_port(serio)) {
			/* Ok, old children are now gone, we are done */
			break;
		}
		serio = serio->child;
	} while (serio);
}

/*
 * serio_disconnect_port() unbinds a port from its driver. As a side effect
 * all child ports are unbound and destroyed.
 */
static void serio_disconnect_port(struct serio *serio)
{
	struct serio *s, *parent;

	if (serio->child) {
		/*
		 * Children ports should be disconnected and destroyed
		 * first, staring with the leaf one, since we don't want
		 * to do recursion
		 */
		for (s = serio; s->child; s = s->child)
			/* empty */;

		do {
			parent = s->parent;

			device_release_driver(&s->dev);
			serio_destroy_port(s);
		} while ((s = parent) != serio);
	}

	/*
	 * Ok, no children left, now disconnect this port
	 */
	device_release_driver(&serio->dev);
}

void serio_rescan(struct serio *serio)
{
	serio_queue_event(serio, NULL, SERIO_RESCAN_PORT);
}

void serio_reconnect(struct serio *serio)
{
	serio_queue_event(serio, NULL, SERIO_RECONNECT_CHAIN);
}

/*
 * Submits register request to kseriod for subsequent execution.
 * Note that port registration is always asynchronous.
 */
void __serio_register_port(struct serio *serio, struct module *owner)
{
	serio_init_port(serio);
	serio_queue_event(serio, owner, SERIO_REGISTER_PORT);
}

/*
 * Synchronously unregisters serio port.
 */
void serio_unregister_port(struct serio *serio)
{
	mutex_lock(&serio_mutex);
	serio_disconnect_port(serio);
	serio_destroy_port(serio);
	mutex_unlock(&serio_mutex);
}

/*
 * Safely unregisters child port if one is present.
 */
void serio_unregister_child_port(struct serio *serio)
{
	mutex_lock(&serio_mutex);
	if (serio->child) {
		serio_disconnect_port(serio->child);
		serio_destroy_port(serio->child);
	}
	mutex_unlock(&serio_mutex);
}


/*
 * Serio driver operations
 */

static ssize_t serio_driver_show_description(struct device_driver *drv, char *buf)
{
	struct serio_driver *driver = to_serio_driver(drv);
	return sprintf(buf, "%s\n", driver->description ? driver->description : "(none)");
}

static ssize_t serio_driver_show_bind_mode(struct device_driver *drv, char *buf)
{
	struct serio_driver *serio_drv = to_serio_driver(drv);
	return sprintf(buf, "%s\n", serio_drv->manual_bind ? "manual" : "auto");
}

static ssize_t serio_driver_set_bind_mode(struct device_driver *drv, const char *buf, size_t count)
{
	struct serio_driver *serio_drv = to_serio_driver(drv);
	int retval;

	retval = count;
	if (!strncmp(buf, "manual", count)) {
		serio_drv->manual_bind = 1;
	} else if (!strncmp(buf, "auto", count)) {
		serio_drv->manual_bind = 0;
	} else {
		retval = -EINVAL;
	}

	return retval;
}


static struct driver_attribute serio_driver_attrs[] = {
	__ATTR(description, S_IRUGO, serio_driver_show_description, NULL),
	__ATTR(bind_mode, S_IWUSR | S_IRUGO,
		serio_driver_show_bind_mode, serio_driver_set_bind_mode),
	__ATTR_NULL
};

static int serio_driver_probe(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);
	struct serio_driver *drv = to_serio_driver(dev->driver);

	return serio_connect_driver(serio, drv);
}

static int serio_driver_remove(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);

	serio_disconnect_driver(serio);
	return 0;
}

static void serio_cleanup(struct serio *serio)
{
	mutex_lock(&serio->drv_mutex);
	if (serio->drv && serio->drv->cleanup)
		serio->drv->cleanup(serio);
	mutex_unlock(&serio->drv_mutex);
}

static void serio_shutdown(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);

	serio_cleanup(serio);
}

static void serio_attach_driver(struct serio_driver *drv)
{
	int error;

	error = driver_attach(&drv->driver);
	if (error)
		printk(KERN_WARNING
			"serio: driver_attach() failed for %s with error %d\n",
			drv->driver.name, error);
}

int __serio_register_driver(struct serio_driver *drv, struct module *owner, const char *mod_name)
{
	int manual_bind = drv->manual_bind;
	int error;

	drv->driver.bus = &serio_bus;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;

	/*
	 * Temporarily disable automatic binding because probing
	 * takes long time and we are better off doing it in kseriod
	 */
	drv->manual_bind = 1;

	error = driver_register(&drv->driver);
	if (error) {
		printk(KERN_ERR
			"serio: driver_register() failed for %s, error: %d\n",
			drv->driver.name, error);
		return error;
	}

	/*
	 * Restore original bind mode and let kseriod bind the
	 * driver to free ports
	 */
	if (!manual_bind) {
		drv->manual_bind = 0;
		error = serio_queue_event(drv, NULL, SERIO_ATTACH_DRIVER);
		if (error) {
			driver_unregister(&drv->driver);
			return error;
		}
	}

	return 0;
}

void serio_unregister_driver(struct serio_driver *drv)
{
	struct serio *serio;

	mutex_lock(&serio_mutex);

	drv->manual_bind = 1;	/* so serio_find_driver ignores it */
	serio_remove_pending_events(drv);

start_over:
	list_for_each_entry(serio, &serio_list, node) {
		if (serio->drv == drv) {
			serio_disconnect_port(serio);
			serio_find_driver(serio);
			/* we could've deleted some ports, restart */
			goto start_over;
		}
	}

	driver_unregister(&drv->driver);
	mutex_unlock(&serio_mutex);
}

static void serio_set_drv(struct serio *serio, struct serio_driver *drv)
{
	serio_pause_rx(serio);
	serio->drv = drv;
	serio_continue_rx(serio);
}

static int serio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct serio *serio = to_serio_port(dev);
	struct serio_driver *serio_drv = to_serio_driver(drv);

	if (serio->manual_bind || serio_drv->manual_bind)
		return 0;

	return serio_match_port(serio_drv->id_table, serio);
}

#ifdef CONFIG_HOTPLUG

#define SERIO_ADD_UEVENT_VAR(fmt, val...)				\
	do {								\
		int err = add_uevent_var(env, fmt, val);		\
		if (err)						\
			return err;					\
	} while (0)

static int serio_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct serio *serio;

	if (!dev)
		return -ENODEV;

	serio = to_serio_port(dev);

	SERIO_ADD_UEVENT_VAR("SERIO_TYPE=%02x", serio->id.type);
	SERIO_ADD_UEVENT_VAR("SERIO_PROTO=%02x", serio->id.proto);
	SERIO_ADD_UEVENT_VAR("SERIO_ID=%02x", serio->id.id);
	SERIO_ADD_UEVENT_VAR("SERIO_EXTRA=%02x", serio->id.extra);
	SERIO_ADD_UEVENT_VAR("MODALIAS=serio:ty%02Xpr%02Xid%02Xex%02X",
				serio->id.type, serio->id.proto, serio->id.id, serio->id.extra);

	return 0;
}
#undef SERIO_ADD_UEVENT_VAR

#else

static int serio_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return -ENODEV;
}

#endif /* CONFIG_HOTPLUG */

#ifdef CONFIG_PM
static int serio_suspend(struct device *dev, pm_message_t state)
{
	if (dev->power.power_state.event != state.event) {
		if (state.event == PM_EVENT_SUSPEND)
			serio_cleanup(to_serio_port(dev));

		dev->power.power_state = state;
	}

	return 0;
}

static int serio_resume(struct device *dev)
{
	/*
	 * Driver reconnect can take a while, so better let kseriod
	 * deal with it.
	 */
	if (dev->power.power_state.event != PM_EVENT_ON) {
		dev->power.power_state = PMSG_ON;
		serio_queue_event(to_serio_port(dev), NULL,
				  SERIO_RECONNECT_PORT);
	}

	return 0;
}
#endif /* CONFIG_PM */

/* called from serio_driver->connect/disconnect methods under serio_mutex */
int serio_open(struct serio *serio, struct serio_driver *drv)
{
	serio_set_drv(serio, drv);

	if (serio->open && serio->open(serio)) {
		serio_set_drv(serio, NULL);
		return -1;
	}
	return 0;
}

/* called from serio_driver->connect/disconnect methods under serio_mutex */
void serio_close(struct serio *serio)
{
	if (serio->close)
		serio->close(serio);

	serio_set_drv(serio, NULL);
}

irqreturn_t serio_interrupt(struct serio *serio,
		unsigned char data, unsigned int dfl)
{
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;

	spin_lock_irqsave(&serio->lock, flags);

        if (likely(serio->drv)) {
                ret = serio->drv->interrupt(serio, data, dfl);
	} else if (!dfl && serio->registered) {
		serio_rescan(serio);
		ret = IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&serio->lock, flags);

	return ret;
}

static struct bus_type serio_bus = {
	.name		= "serio",
	.dev_attrs	= serio_device_attrs,
	.drv_attrs	= serio_driver_attrs,
	.match		= serio_bus_match,
	.uevent		= serio_uevent,
	.probe		= serio_driver_probe,
	.remove		= serio_driver_remove,
	.shutdown	= serio_shutdown,
#ifdef CONFIG_PM
	.suspend	= serio_suspend,
	.resume		= serio_resume,
#endif
};

static int __init serio_init(void)
{
	int error;

	error = bus_register(&serio_bus);
	if (error) {
		printk(KERN_ERR "serio: failed to register serio bus, error: %d\n", error);
		return error;
	}

	serio_task = kthread_run(serio_thread, NULL, "kseriod");
	if (IS_ERR(serio_task)) {
		bus_unregister(&serio_bus);
		error = PTR_ERR(serio_task);
		printk(KERN_ERR "serio: Failed to start kseriod, error: %d\n", error);
		return error;
	}

	return 0;
}

static void __exit serio_exit(void)
{
	bus_unregister(&serio_bus);
	kthread_stop(serio_task);
}

subsys_initcall(serio_init);
module_exit(serio_exit);
