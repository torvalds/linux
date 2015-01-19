/*
 * card_sysfs.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  card sysfs/driver model support.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/cardreader/sdio.h>
#include <linux/cardreader/card_block.h>

#define dev_to_memory_card(d)	container_of(d, struct memory_card, dev)
#define to_card_driver(d)	container_of(d, struct card_driver, drv)
#define cls_dev_to_card_host(d)	container_of(d, struct card_host, class_dev)

extern struct completion card_devdel_comp;
extern struct completion card_devadd_comp;
void card_release_card(struct device *dev)
{
	struct memory_card *card = dev_to_memory_card(dev);

	kfree(card);
}

/*
 * This currently matches any card driver to any card card - drivers
 * themselves make the decision whether to drive this card in their
 * probe method.  However, we force "bad" cards to fail.
 */
static int card_bus_match(struct device *dev, struct device_driver *drv)
{
	struct memory_card *card = dev_to_memory_card(dev);
	return !(card->state & CARD_STATE_BAD);
}

static int card_bus_suspend(struct device *dev, pm_message_t state)
{
	struct card_driver *drv = to_card_driver(dev->driver);
	struct memory_card *card = dev_to_memory_card(dev);
	int ret = 0;

	if (dev->driver && drv->suspend)
		ret = drv->suspend(card, state);
	return ret;
}

static int card_bus_resume(struct device *dev)
{
	struct card_driver *drv = to_card_driver(dev->driver);
	struct memory_card *card = dev_to_memory_card(dev);
	int ret = 0;

	if (dev->driver && drv->resume)
		ret = drv->resume(card);
	return ret;
}

static int card_bus_probe(struct device *dev)
{
	struct card_driver *drv = to_card_driver(dev->driver);
	struct memory_card *card = dev_to_memory_card(dev);

	if (card->card_type == CARD_SDIO || 
                  card->card_type == CARD_INAND_LP)
		return 0;
	return drv->probe(card);
}

static int card_bus_remove(struct device *dev)
{
	struct card_driver *drv = to_card_driver(dev->driver);
	struct memory_card *card = dev_to_memory_card(dev);

	drv->remove(card);

	return 0;
}

static struct bus_type card_bus_type = {
	.name = "memorycard",
	.match = card_bus_match,
	.probe = card_bus_probe,
	.remove = card_bus_remove,
	.suspend = card_bus_suspend,
	.resume = card_bus_resume,
};

/**
 *	card_register_driver - register a media driver
 *	@drv: card media driver
 */
int card_register_driver(struct card_driver *drv)
{
	drv->drv.bus = &card_bus_type;
	return driver_register(&drv->drv);
}

EXPORT_SYMBOL(card_register_driver);

/**
 *	card_unregister_driver - unregister a media driver
 *	@drv: card media driver
 */
void card_unregister_driver(struct card_driver *drv)
{
	drv->drv.bus = &card_bus_type;
	driver_unregister(&drv->drv);
}

EXPORT_SYMBOL(card_unregister_driver);

/*
 * Internal function.  Initialise a card card structure.
 */
void card_init_card(struct memory_card *card, struct card_host *host)
{
	memset(card, 0, sizeof(struct memory_card));
	card->host = host;
	device_initialize(&card->dev);
	card->dev.parent = &card->host->class_dev;
	card->dev.bus = &card_bus_type;
	card->dev.release = card_release_card;
}

EXPORT_SYMBOL(card_init_card);

#include <linux/delay.h>
/*
 * Internal function.  Register a new card card with the driver model.
 */
int card_register_card(struct memory_card *card)
{
	int ret = 0;
	
	dev_set_name(&card->dev, "%s:%s", card_hostname(card->host), card->name);

	/*return device_add(&card->dev);*/
	ret = device_add(&card->dev);

	complete(&card_devadd_comp);
	return ret;
}

EXPORT_SYMBOL(card_register_card);

/*
 * Internal function.  Unregister a new card card with the
 * driver model, and (eventually) free it.
 */
void card_remove_card(struct memory_card *card)
{	
	if (card->state & CARD_STATE_PRESENT){
		init_completion(&card_devdel_comp);
		device_del(&card->dev);
	          put_device(&card->dev);
		wait_for_completion(&card_devdel_comp);
                   return;
	}

	put_device(&card->dev);
}

EXPORT_SYMBOL(card_remove_card);

static void card_host_classdev_release(struct device *dev)
{
	struct card_host *host = cls_dev_to_card_host(dev);
	kfree(host);
}

static struct class card_host_class = {
	.name = "card_host",
	.dev_release = card_host_classdev_release,
};

static DEFINE_IDR(card_host_idr);
static DEFINE_SPINLOCK(card_host_lock);

/*
 * Internal function. Allocate a new card host.
 */
struct card_host *card_alloc_host_sysfs(int extra, struct device *dev)
{
	struct card_host *host;

	host = kmalloc(sizeof(struct card_host) + extra, GFP_KERNEL);
	if (host) {
		memset(host, 0, sizeof(struct card_host) + extra);

		host->parent = dev;
		host->class_dev.parent = dev;
		host->class_dev.class = &card_host_class;
		device_initialize(&host->class_dev);
	}

	return host;
}

EXPORT_SYMBOL(card_alloc_host_sysfs);

/*
 * Internal function. Register a new card host with the card class.
 */
int card_add_host_sysfs(struct card_host *host)
{
	int err;

	if (!idr_pre_get(&card_host_idr, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&card_host_lock);
	err = idr_get_new(&card_host_idr, host, &host->index);
	spin_unlock(&card_host_lock);
	if (err)
		return err;

	dev_set_name(&host->class_dev, "memorycard%d", host->index);
	//snprintf(host->class_dev.bus_id, BUS_ID_SIZE, "memorycard%d", host->index);

	return device_add(&host->class_dev);
}

/*
 * Internal function. Unregister a card host with the card class.
 */
void card_remove_host_sysfs(struct card_host *host)
{
	device_del(&host->class_dev);

	spin_lock(&card_host_lock);
	idr_remove(&card_host_idr, host->index);
	spin_unlock(&card_host_lock);
}

/*
 * Internal function. Free a card host.
 */
void card_free_host_sysfs(struct card_host *host)
{
	put_device(&host->class_dev);
}

static struct workqueue_struct *workqueue;

/*
 * Internal function. Schedule work in the card work queue.
 */
int card_schedule_work(struct work_struct *work)
{
	return queue_work(workqueue, work);
}

/*
 * Internal function. Schedule delayed work in the card work queue.
 */
int card_schedule_delayed_work(struct delayed_work *work, unsigned long delay)
{
	return queue_delayed_work(workqueue, work, delay);
}

/*
 * Internal function. Flush all scheduled work from the card work queue.
 */
void card_flush_scheduled_work(void)
{
	flush_workqueue(workqueue);
}

/**
 *	card_align_data_size - pads a transfer size to a more optimal value
 *	@card: the Memory card associated with the data transfer
 *	@sz: original transfer size
 *
 *	Pads the original data size with a number of extra bytes in
 *	order to avoid controller bugs and/or performance hits
 *	(e.g. some controllers revert to PIO for certain sizes).
 *
 *	Returns the improved size, which might be unmodified.
 *
 *	Note that this function is only relevant when issuing a
 *	single scatter gather entry.
 */
unsigned int card_align_data_size(struct memory_card *card, unsigned int sz)
{
	/*
	 * FIXME: We don't have a system for the controller to tell
	 * the core about its problems yet, so for now we just 32-bit
	 * align the size.
	 */
	sz = ((sz + 3) / 4) * 4;

	return sz;
}
EXPORT_SYMBOL(card_align_data_size);

int card_register_host_class(void)
{
	return class_register(&card_host_class);
}

void card_unregister_host_class(void)
{
	class_unregister(&card_host_class);
}

int card_register_bus(void)
{
	return bus_register(&card_bus_type);
}

void card_unregister_bus(void)
{
	return bus_unregister(&card_bus_type);
}

static int __init card_init(void)
{
	int ret = 0;

	workqueue = create_singlethread_workqueue("kcardd");
	if (!workqueue)
		return -ENOMEM;

	ret = card_register_bus();
		if (ret)
		goto destroy_workqueue;

	ret = card_register_host_class();
	if (ret)
		goto unregister_bus;

#ifdef CONFIG_SDIO
	ret = sdio_register_bus();
	if (ret)
		goto unregister_host_class;
#endif

	return 0;

unregister_host_class:
	card_unregister_host_class();
unregister_bus:
	card_unregister_bus();
destroy_workqueue:
	destroy_workqueue(workqueue);

	return ret;
}

static void __exit card_exit(void)
{
#ifdef CONFIG_SDIO
	sdio_unregister_bus();
#endif
	card_unregister_host_class();
	card_unregister_bus();
	destroy_workqueue(workqueue);
}

module_init(card_init);
module_exit(card_exit);
