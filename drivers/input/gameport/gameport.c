/*
 * Generic gameport layer
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 2005 Dmitry Torokhov
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>	/* HZ */
#include <linux/mutex.h>
#include <linux/freezer.h>

/*#include <asm/io.h>*/

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Generic gameport layer");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(__gameport_register_port);
EXPORT_SYMBOL(gameport_unregister_port);
EXPORT_SYMBOL(__gameport_register_driver);
EXPORT_SYMBOL(gameport_unregister_driver);
EXPORT_SYMBOL(gameport_open);
EXPORT_SYMBOL(gameport_close);
EXPORT_SYMBOL(gameport_set_phys);
EXPORT_SYMBOL(gameport_start_polling);
EXPORT_SYMBOL(gameport_stop_polling);

/*
 * gameport_mutex protects entire gameport subsystem and is taken
 * every time gameport port or driver registrered or unregistered.
 */
static DEFINE_MUTEX(gameport_mutex);

static LIST_HEAD(gameport_list);

static struct bus_type gameport_bus;

static void gameport_add_port(struct gameport *gameport);
static void gameport_attach_driver(struct gameport_driver *drv);
static void gameport_reconnect_port(struct gameport *gameport);
static void gameport_disconnect_port(struct gameport *gameport);

#if defined(__i386__)

#include <asm/i8253.h>

#define DELTA(x,y)      ((y)-(x)+((y)<(x)?1193182/HZ:0))
#define GET_TIME(x)     do { x = get_time_pit(); } while (0)

static unsigned int get_time_pit(void)
{
	unsigned long flags;
	unsigned int count;

	spin_lock_irqsave(&i8253_lock, flags);
	outb_p(0x00, 0x43);
	count = inb_p(0x40);
	count |= inb_p(0x40) << 8;
	spin_unlock_irqrestore(&i8253_lock, flags);

	return count;
}

#endif



/*
 * gameport_measure_speed() measures the gameport i/o speed.
 */

static int gameport_measure_speed(struct gameport *gameport)
{
#if defined(__i386__)

	unsigned int i, t, t1, t2, t3, tx;
	unsigned long flags;

	if (gameport_open(gameport, NULL, GAMEPORT_MODE_RAW))
		return 0;

	tx = 1 << 30;

	for(i = 0; i < 50; i++) {
		local_irq_save(flags);
		GET_TIME(t1);
		for (t = 0; t < 50; t++) gameport_read(gameport);
		GET_TIME(t2);
		GET_TIME(t3);
		local_irq_restore(flags);
		udelay(i * 10);
		if ((t = DELTA(t2,t1) - DELTA(t3,t2)) < tx) tx = t;
	}

	gameport_close(gameport);
	return 59659 / (tx < 1 ? 1 : tx);

#elif defined (__x86_64__)

	unsigned int i, t;
	unsigned long tx, t1, t2, flags;

	if (gameport_open(gameport, NULL, GAMEPORT_MODE_RAW))
		return 0;

	tx = 1 << 30;

	for(i = 0; i < 50; i++) {
		local_irq_save(flags);
		rdtscl(t1);
		for (t = 0; t < 50; t++) gameport_read(gameport);
		rdtscl(t2);
		local_irq_restore(flags);
		udelay(i * 10);
		if (t2 - t1 < tx) tx = t2 - t1;
	}

	gameport_close(gameport);
	return (cpu_data(raw_smp_processor_id()).loops_per_jiffy *
		(unsigned long)HZ / (1000 / 50)) / (tx < 1 ? 1 : tx);

#else

	unsigned int j, t = 0;

	if (gameport_open(gameport, NULL, GAMEPORT_MODE_RAW))
		return 0;

	j = jiffies; while (j == jiffies);
	j = jiffies; while (j == jiffies) { t++; gameport_read(gameport); }

	gameport_close(gameport);
	return t * HZ / 1000;

#endif
}

void gameport_start_polling(struct gameport *gameport)
{
	spin_lock(&gameport->timer_lock);

	if (!gameport->poll_cnt++) {
		BUG_ON(!gameport->poll_handler);
		BUG_ON(!gameport->poll_interval);
		mod_timer(&gameport->poll_timer, jiffies + msecs_to_jiffies(gameport->poll_interval));
	}

	spin_unlock(&gameport->timer_lock);
}

void gameport_stop_polling(struct gameport *gameport)
{
	spin_lock(&gameport->timer_lock);

	if (!--gameport->poll_cnt)
		del_timer(&gameport->poll_timer);

	spin_unlock(&gameport->timer_lock);
}

static void gameport_run_poll_handler(unsigned long d)
{
	struct gameport *gameport = (struct gameport *)d;

	gameport->poll_handler(gameport);
	if (gameport->poll_cnt)
		mod_timer(&gameport->poll_timer, jiffies + msecs_to_jiffies(gameport->poll_interval));
}

/*
 * Basic gameport -> driver core mappings
 */

static int gameport_bind_driver(struct gameport *gameport, struct gameport_driver *drv)
{
	int error;

	gameport->dev.driver = &drv->driver;
	if (drv->connect(gameport, drv)) {
		gameport->dev.driver = NULL;
		return -ENODEV;
	}

	error = device_bind_driver(&gameport->dev);
	if (error) {
		printk(KERN_WARNING
			"gameport: device_bind_driver() failed "
			"for %s (%s) and %s, error: %d\n",
			gameport->phys, gameport->name,
			drv->description, error);
		drv->disconnect(gameport);
		gameport->dev.driver = NULL;
		return error;
	}

	return 0;
}

static void gameport_find_driver(struct gameport *gameport)
{
	int error;

	error = device_attach(&gameport->dev);
	if (error < 0)
		printk(KERN_WARNING
			"gameport: device_attach() failed for %s (%s), error: %d\n",
			gameport->phys, gameport->name, error);
}


/*
 * Gameport event processing.
 */

enum gameport_event_type {
	GAMEPORT_REGISTER_PORT,
	GAMEPORT_ATTACH_DRIVER,
};

struct gameport_event {
	enum gameport_event_type type;
	void *object;
	struct module *owner;
	struct list_head node;
};

static DEFINE_SPINLOCK(gameport_event_lock);	/* protects gameport_event_list */
static LIST_HEAD(gameport_event_list);
static DECLARE_WAIT_QUEUE_HEAD(gameport_wait);
static struct task_struct *gameport_task;

static int gameport_queue_event(void *object, struct module *owner,
				enum gameport_event_type event_type)
{
	unsigned long flags;
	struct gameport_event *event;
	int retval = 0;

	spin_lock_irqsave(&gameport_event_lock, flags);

	/*
	 * Scan event list for the other events for the same gameport port,
	 * starting with the most recent one. If event is the same we
	 * do not need add new one. If event is of different type we
	 * need to add this event and should not look further because
	 * we need to preseve sequence of distinct events.
	 */
	list_for_each_entry_reverse(event, &gameport_event_list, node) {
		if (event->object == object) {
			if (event->type == event_type)
				goto out;
			break;
		}
	}

	event = kmalloc(sizeof(struct gameport_event), GFP_ATOMIC);
	if (!event) {
		printk(KERN_ERR
			"gameport: Not enough memory to queue event %d\n",
			event_type);
		retval = -ENOMEM;
		goto out;
	}

	if (!try_module_get(owner)) {
		printk(KERN_WARNING
			"gameport: Can't get module reference, dropping event %d\n",
			event_type);
		kfree(event);
		retval = -EINVAL;
		goto out;
	}

	event->type = event_type;
	event->object = object;
	event->owner = owner;

	list_add_tail(&event->node, &gameport_event_list);
	wake_up(&gameport_wait);

out:
	spin_unlock_irqrestore(&gameport_event_lock, flags);
	return retval;
}

static void gameport_free_event(struct gameport_event *event)
{
	module_put(event->owner);
	kfree(event);
}

static void gameport_remove_duplicate_events(struct gameport_event *event)
{
	struct list_head *node, *next;
	struct gameport_event *e;
	unsigned long flags;

	spin_lock_irqsave(&gameport_event_lock, flags);

	list_for_each_safe(node, next, &gameport_event_list) {
		e = list_entry(node, struct gameport_event, node);
		if (event->object == e->object) {
			/*
			 * If this event is of different type we should not
			 * look further - we only suppress duplicate events
			 * that were sent back-to-back.
			 */
			if (event->type != e->type)
				break;

			list_del_init(node);
			gameport_free_event(e);
		}
	}

	spin_unlock_irqrestore(&gameport_event_lock, flags);
}

static struct gameport_event *gameport_get_event(void)
{
	struct gameport_event *event;
	struct list_head *node;
	unsigned long flags;

	spin_lock_irqsave(&gameport_event_lock, flags);

	if (list_empty(&gameport_event_list)) {
		spin_unlock_irqrestore(&gameport_event_lock, flags);
		return NULL;
	}

	node = gameport_event_list.next;
	event = list_entry(node, struct gameport_event, node);
	list_del_init(node);

	spin_unlock_irqrestore(&gameport_event_lock, flags);

	return event;
}

static void gameport_handle_event(void)
{
	struct gameport_event *event;

	mutex_lock(&gameport_mutex);

	/*
	 * Note that we handle only one event here to give swsusp
	 * a chance to freeze kgameportd thread. Gameport events
	 * should be pretty rare so we are not concerned about
	 * taking performance hit.
	 */
	if ((event = gameport_get_event())) {

		switch (event->type) {
			case GAMEPORT_REGISTER_PORT:
				gameport_add_port(event->object);
				break;

			case GAMEPORT_ATTACH_DRIVER:
				gameport_attach_driver(event->object);
				break;

			default:
				break;
		}

		gameport_remove_duplicate_events(event);
		gameport_free_event(event);
	}

	mutex_unlock(&gameport_mutex);
}

/*
 * Remove all events that have been submitted for a given object,
 * be it a gameport port or a driver.
 */
static void gameport_remove_pending_events(void *object)
{
	struct list_head *node, *next;
	struct gameport_event *event;
	unsigned long flags;

	spin_lock_irqsave(&gameport_event_lock, flags);

	list_for_each_safe(node, next, &gameport_event_list) {
		event = list_entry(node, struct gameport_event, node);
		if (event->object == object) {
			list_del_init(node);
			gameport_free_event(event);
		}
	}

	spin_unlock_irqrestore(&gameport_event_lock, flags);
}

/*
 * Destroy child gameport port (if any) that has not been fully registered yet.
 *
 * Note that we rely on the fact that port can have only one child and therefore
 * only one child registration request can be pending. Additionally, children
 * are registered by driver's connect() handler so there can't be a grandchild
 * pending registration together with a child.
 */
static struct gameport *gameport_get_pending_child(struct gameport *parent)
{
	struct gameport_event *event;
	struct gameport *gameport, *child = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gameport_event_lock, flags);

	list_for_each_entry(event, &gameport_event_list, node) {
		if (event->type == GAMEPORT_REGISTER_PORT) {
			gameport = event->object;
			if (gameport->parent == parent) {
				child = gameport;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&gameport_event_lock, flags);
	return child;
}

static int gameport_thread(void *nothing)
{
	set_freezable();
	do {
		gameport_handle_event();
		wait_event_freezable(gameport_wait,
			kthread_should_stop() || !list_empty(&gameport_event_list));
	} while (!kthread_should_stop());

	printk(KERN_DEBUG "gameport: kgameportd exiting\n");
	return 0;
}


/*
 * Gameport port operations
 */

static ssize_t gameport_show_description(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gameport *gameport = to_gameport_port(dev);
	return sprintf(buf, "%s\n", gameport->name);
}

static ssize_t gameport_rebind_driver(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct gameport *gameport = to_gameport_port(dev);
	struct device_driver *drv;
	int error;

	error = mutex_lock_interruptible(&gameport_mutex);
	if (error)
		return error;

	if (!strncmp(buf, "none", count)) {
		gameport_disconnect_port(gameport);
	} else if (!strncmp(buf, "reconnect", count)) {
		gameport_reconnect_port(gameport);
	} else if (!strncmp(buf, "rescan", count)) {
		gameport_disconnect_port(gameport);
		gameport_find_driver(gameport);
	} else if ((drv = driver_find(buf, &gameport_bus)) != NULL) {
		gameport_disconnect_port(gameport);
		error = gameport_bind_driver(gameport, to_gameport_driver(drv));
		put_driver(drv);
	} else {
		error = -EINVAL;
	}

	mutex_unlock(&gameport_mutex);

	return error ? error : count;
}

static struct device_attribute gameport_device_attrs[] = {
	__ATTR(description, S_IRUGO, gameport_show_description, NULL),
	__ATTR(drvctl, S_IWUSR, NULL, gameport_rebind_driver),
	__ATTR_NULL
};

static void gameport_release_port(struct device *dev)
{
	struct gameport *gameport = to_gameport_port(dev);

	kfree(gameport);
	module_put(THIS_MODULE);
}

void gameport_set_phys(struct gameport *gameport, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(gameport->phys, sizeof(gameport->phys), fmt, args);
	va_end(args);
}

/*
 * Prepare gameport port for registration.
 */
static void gameport_init_port(struct gameport *gameport)
{
	static atomic_t gameport_no = ATOMIC_INIT(0);

	__module_get(THIS_MODULE);

	mutex_init(&gameport->drv_mutex);
	device_initialize(&gameport->dev);
	dev_set_name(&gameport->dev, "gameport%lu", (unsigned long)atomic_inc_return(&gameport_no) - 1);
	gameport->dev.bus = &gameport_bus;
	gameport->dev.release = gameport_release_port;
	if (gameport->parent)
		gameport->dev.parent = &gameport->parent->dev;

	INIT_LIST_HEAD(&gameport->node);
	spin_lock_init(&gameport->timer_lock);
	init_timer(&gameport->poll_timer);
	gameport->poll_timer.function = gameport_run_poll_handler;
	gameport->poll_timer.data = (unsigned long)gameport;
}

/*
 * Complete gameport port registration.
 * Driver core will attempt to find appropriate driver for the port.
 */
static void gameport_add_port(struct gameport *gameport)
{
	int error;

	if (gameport->parent)
		gameport->parent->child = gameport;

	gameport->speed = gameport_measure_speed(gameport);

	list_add_tail(&gameport->node, &gameport_list);

	if (gameport->io)
		printk(KERN_INFO "gameport: %s is %s, io %#x, speed %dkHz\n",
			gameport->name, gameport->phys, gameport->io, gameport->speed);
	else
		printk(KERN_INFO "gameport: %s is %s, speed %dkHz\n",
			gameport->name, gameport->phys, gameport->speed);

	error = device_add(&gameport->dev);
	if (error)
		printk(KERN_ERR
			"gameport: device_add() failed for %s (%s), error: %d\n",
			gameport->phys, gameport->name, error);
	else
		gameport->registered = 1;
}

/*
 * gameport_destroy_port() completes deregistration process and removes
 * port from the system
 */
static void gameport_destroy_port(struct gameport *gameport)
{
	struct gameport *child;

	child = gameport_get_pending_child(gameport);
	if (child) {
		gameport_remove_pending_events(child);
		put_device(&child->dev);
	}

	if (gameport->parent) {
		gameport->parent->child = NULL;
		gameport->parent = NULL;
	}

	if (gameport->registered) {
		device_del(&gameport->dev);
		gameport->registered = 0;
	}

	list_del_init(&gameport->node);

	gameport_remove_pending_events(gameport);
	put_device(&gameport->dev);
}

/*
 * Reconnect gameport port and all its children (re-initialize attached devices)
 */
static void gameport_reconnect_port(struct gameport *gameport)
{
	do {
		if (!gameport->drv || !gameport->drv->reconnect || gameport->drv->reconnect(gameport)) {
			gameport_disconnect_port(gameport);
			gameport_find_driver(gameport);
			/* Ok, old children are now gone, we are done */
			break;
		}
		gameport = gameport->child;
	} while (gameport);
}

/*
 * gameport_disconnect_port() unbinds a port from its driver. As a side effect
 * all child ports are unbound and destroyed.
 */
static void gameport_disconnect_port(struct gameport *gameport)
{
	struct gameport *s, *parent;

	if (gameport->child) {
		/*
		 * Children ports should be disconnected and destroyed
		 * first, staring with the leaf one, since we don't want
		 * to do recursion
		 */
		for (s = gameport; s->child; s = s->child)
			/* empty */;

		do {
			parent = s->parent;

			device_release_driver(&s->dev);
			gameport_destroy_port(s);
		} while ((s = parent) != gameport);
	}

	/*
	 * Ok, no children left, now disconnect this port
	 */
	device_release_driver(&gameport->dev);
}

/*
 * Submits register request to kgameportd for subsequent execution.
 * Note that port registration is always asynchronous.
 */
void __gameport_register_port(struct gameport *gameport, struct module *owner)
{
	gameport_init_port(gameport);
	gameport_queue_event(gameport, owner, GAMEPORT_REGISTER_PORT);
}

/*
 * Synchronously unregisters gameport port.
 */
void gameport_unregister_port(struct gameport *gameport)
{
	mutex_lock(&gameport_mutex);
	gameport_disconnect_port(gameport);
	gameport_destroy_port(gameport);
	mutex_unlock(&gameport_mutex);
}


/*
 * Gameport driver operations
 */

static ssize_t gameport_driver_show_description(struct device_driver *drv, char *buf)
{
	struct gameport_driver *driver = to_gameport_driver(drv);
	return sprintf(buf, "%s\n", driver->description ? driver->description : "(none)");
}

static struct driver_attribute gameport_driver_attrs[] = {
	__ATTR(description, S_IRUGO, gameport_driver_show_description, NULL),
	__ATTR_NULL
};

static int gameport_driver_probe(struct device *dev)
{
	struct gameport *gameport = to_gameport_port(dev);
	struct gameport_driver *drv = to_gameport_driver(dev->driver);

	drv->connect(gameport, drv);
	return gameport->drv ? 0 : -ENODEV;
}

static int gameport_driver_remove(struct device *dev)
{
	struct gameport *gameport = to_gameport_port(dev);
	struct gameport_driver *drv = to_gameport_driver(dev->driver);

	drv->disconnect(gameport);
	return 0;
}

static void gameport_attach_driver(struct gameport_driver *drv)
{
	int error;

	error = driver_attach(&drv->driver);
	if (error)
		printk(KERN_ERR
			"gameport: driver_attach() failed for %s, error: %d\n",
			drv->driver.name, error);
}

int __gameport_register_driver(struct gameport_driver *drv, struct module *owner,
				const char *mod_name)
{
	int error;

	drv->driver.bus = &gameport_bus;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;

	/*
	 * Temporarily disable automatic binding because probing
	 * takes long time and we are better off doing it in kgameportd
	 */
	drv->ignore = 1;

	error = driver_register(&drv->driver);
	if (error) {
		printk(KERN_ERR
			"gameport: driver_register() failed for %s, error: %d\n",
			drv->driver.name, error);
		return error;
	}

	/*
	 * Reset ignore flag and let kgameportd bind the driver to free ports
	 */
	drv->ignore = 0;
	error = gameport_queue_event(drv, NULL, GAMEPORT_ATTACH_DRIVER);
	if (error) {
		driver_unregister(&drv->driver);
		return error;
	}

	return 0;
}

void gameport_unregister_driver(struct gameport_driver *drv)
{
	struct gameport *gameport;

	mutex_lock(&gameport_mutex);

	drv->ignore = 1;	/* so gameport_find_driver ignores it */
	gameport_remove_pending_events(drv);

start_over:
	list_for_each_entry(gameport, &gameport_list, node) {
		if (gameport->drv == drv) {
			gameport_disconnect_port(gameport);
			gameport_find_driver(gameport);
			/* we could've deleted some ports, restart */
			goto start_over;
		}
	}

	driver_unregister(&drv->driver);

	mutex_unlock(&gameport_mutex);
}

static int gameport_bus_match(struct device *dev, struct device_driver *drv)
{
	struct gameport_driver *gameport_drv = to_gameport_driver(drv);

	return !gameport_drv->ignore;
}

static struct bus_type gameport_bus = {
	.name		= "gameport",
	.dev_attrs	= gameport_device_attrs,
	.drv_attrs	= gameport_driver_attrs,
	.match		= gameport_bus_match,
	.probe		= gameport_driver_probe,
	.remove		= gameport_driver_remove,
};

static void gameport_set_drv(struct gameport *gameport, struct gameport_driver *drv)
{
	mutex_lock(&gameport->drv_mutex);
	gameport->drv = drv;
	mutex_unlock(&gameport->drv_mutex);
}

int gameport_open(struct gameport *gameport, struct gameport_driver *drv, int mode)
{
	if (gameport->open) {
		if (gameport->open(gameport, mode)) {
			return -1;
		}
	} else {
		if (mode != GAMEPORT_MODE_RAW)
			return -1;
	}

	gameport_set_drv(gameport, drv);
	return 0;
}

void gameport_close(struct gameport *gameport)
{
	del_timer_sync(&gameport->poll_timer);
	gameport->poll_handler = NULL;
	gameport->poll_interval = 0;
	gameport_set_drv(gameport, NULL);
	if (gameport->close)
		gameport->close(gameport);
}

static int __init gameport_init(void)
{
	int error;

	error = bus_register(&gameport_bus);
	if (error) {
		printk(KERN_ERR "gameport: failed to register gameport bus, error: %d\n", error);
		return error;
	}

	gameport_task = kthread_run(gameport_thread, NULL, "kgameportd");
	if (IS_ERR(gameport_task)) {
		bus_unregister(&gameport_bus);
		error = PTR_ERR(gameport_task);
		printk(KERN_ERR "gameport: Failed to start kgameportd, error: %d\n", error);
		return error;
	}

	return 0;
}

static void __exit gameport_exit(void)
{
	bus_unregister(&gameport_bus);
	kthread_stop(gameport_task);
}

subsys_initcall(gameport_init);
module_exit(gameport_exit);
