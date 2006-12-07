/*
 *  linux/drivers/mmc/mmc_sysfs.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC sysfs/driver model support.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/workqueue.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "mmc.h"

#define dev_to_mmc_card(d)	container_of(d, struct mmc_card, dev)
#define to_mmc_driver(d)	container_of(d, struct mmc_driver, drv)
#define cls_dev_to_mmc_host(d)	container_of(d, struct mmc_host, class_dev)

#define MMC_ATTR(name, fmt, args...)					\
static ssize_t mmc_##name##_show (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	struct mmc_card *card = dev_to_mmc_card(dev);			\
	return sprintf(buf, fmt, args);					\
}

MMC_ATTR(cid, "%08x%08x%08x%08x\n", card->raw_cid[0], card->raw_cid[1],
	card->raw_cid[2], card->raw_cid[3]);
MMC_ATTR(csd, "%08x%08x%08x%08x\n", card->raw_csd[0], card->raw_csd[1],
	card->raw_csd[2], card->raw_csd[3]);
MMC_ATTR(scr, "%08x%08x\n", card->raw_scr[0], card->raw_scr[1]);
MMC_ATTR(date, "%02d/%04d\n", card->cid.month, card->cid.year);
MMC_ATTR(fwrev, "0x%x\n", card->cid.fwrev);
MMC_ATTR(hwrev, "0x%x\n", card->cid.hwrev);
MMC_ATTR(manfid, "0x%06x\n", card->cid.manfid);
MMC_ATTR(name, "%s\n", card->cid.prod_name);
MMC_ATTR(oemid, "0x%04x\n", card->cid.oemid);
MMC_ATTR(serial, "0x%08x\n", card->cid.serial);

#define MMC_ATTR_RO(name) __ATTR(name, S_IRUGO, mmc_##name##_show, NULL)

static struct device_attribute mmc_dev_attrs[] = {
	MMC_ATTR_RO(cid),
	MMC_ATTR_RO(csd),
	MMC_ATTR_RO(date),
	MMC_ATTR_RO(fwrev),
	MMC_ATTR_RO(hwrev),
	MMC_ATTR_RO(manfid),
	MMC_ATTR_RO(name),
	MMC_ATTR_RO(oemid),
	MMC_ATTR_RO(serial),
	__ATTR_NULL
};

static struct device_attribute mmc_dev_attr_scr = MMC_ATTR_RO(scr);


static void mmc_release_card(struct device *dev)
{
	struct mmc_card *card = dev_to_mmc_card(dev);

	kfree(card);
}

/*
 * This currently matches any MMC driver to any MMC card - drivers
 * themselves make the decision whether to drive this card in their
 * probe method.  However, we force "bad" cards to fail.
 */
static int mmc_bus_match(struct device *dev, struct device_driver *drv)
{
	struct mmc_card *card = dev_to_mmc_card(dev);
	return !mmc_card_bad(card);
}

static int
mmc_bus_uevent(struct device *dev, char **envp, int num_envp, char *buf,
		int buf_size)
{
	struct mmc_card *card = dev_to_mmc_card(dev);
	char ccc[13];
	int i = 0;

#define add_env(fmt,val)						\
	({								\
		int len, ret = -ENOMEM;					\
		if (i < num_envp) {					\
			envp[i++] = buf;				\
			len = snprintf(buf, buf_size, fmt, val) + 1;	\
			buf_size -= len;				\
			buf += len;					\
			if (buf_size >= 0)				\
				ret = 0;				\
		}							\
		ret;							\
	})

	for (i = 0; i < 12; i++)
		ccc[i] = card->csd.cmdclass & (1 << i) ? '1' : '0';
	ccc[12] = '\0';

	i = 0;
	add_env("MMC_CCC=%s", ccc);
	add_env("MMC_MANFID=%06x", card->cid.manfid);
	add_env("MMC_NAME=%s", mmc_card_name(card));
	add_env("MMC_OEMID=%04x", card->cid.oemid);

	return 0;
}

static int mmc_bus_suspend(struct device *dev, pm_message_t state)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	int ret = 0;

	if (dev->driver && drv->suspend)
		ret = drv->suspend(card, state);
	return ret;
}

static int mmc_bus_resume(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);
	int ret = 0;

	if (dev->driver && drv->resume)
		ret = drv->resume(card);
	return ret;
}

static int mmc_bus_probe(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);

	return drv->probe(card);
}

static int mmc_bus_remove(struct device *dev)
{
	struct mmc_driver *drv = to_mmc_driver(dev->driver);
	struct mmc_card *card = dev_to_mmc_card(dev);

	drv->remove(card);

	return 0;
}

static struct bus_type mmc_bus_type = {
	.name		= "mmc",
	.dev_attrs	= mmc_dev_attrs,
	.match		= mmc_bus_match,
	.uevent		= mmc_bus_uevent,
	.probe		= mmc_bus_probe,
	.remove		= mmc_bus_remove,
	.suspend	= mmc_bus_suspend,
	.resume		= mmc_bus_resume,
};

/**
 *	mmc_register_driver - register a media driver
 *	@drv: MMC media driver
 */
int mmc_register_driver(struct mmc_driver *drv)
{
	drv->drv.bus = &mmc_bus_type;
	return driver_register(&drv->drv);
}

EXPORT_SYMBOL(mmc_register_driver);

/**
 *	mmc_unregister_driver - unregister a media driver
 *	@drv: MMC media driver
 */
void mmc_unregister_driver(struct mmc_driver *drv)
{
	drv->drv.bus = &mmc_bus_type;
	driver_unregister(&drv->drv);
}

EXPORT_SYMBOL(mmc_unregister_driver);


/*
 * Internal function.  Initialise a MMC card structure.
 */
void mmc_init_card(struct mmc_card *card, struct mmc_host *host)
{
	memset(card, 0, sizeof(struct mmc_card));
	card->host = host;
	device_initialize(&card->dev);
	card->dev.parent = mmc_dev(host);
	card->dev.bus = &mmc_bus_type;
	card->dev.release = mmc_release_card;
}

/*
 * Internal function.  Register a new MMC card with the driver model.
 */
int mmc_register_card(struct mmc_card *card)
{
	int ret;

	snprintf(card->dev.bus_id, sizeof(card->dev.bus_id),
		 "%s:%04x", mmc_hostname(card->host), card->rca);

	ret = device_add(&card->dev);
	if (ret == 0) {
		if (mmc_card_sd(card)) {
			ret = device_create_file(&card->dev, &mmc_dev_attr_scr);
			if (ret)
				device_del(&card->dev);
		}
	}
	return ret;
}

/*
 * Internal function.  Unregister a new MMC card with the
 * driver model, and (eventually) free it.
 */
void mmc_remove_card(struct mmc_card *card)
{
	if (mmc_card_present(card)) {
		if (mmc_card_sd(card))
			device_remove_file(&card->dev, &mmc_dev_attr_scr);

		device_del(&card->dev);
	}

	put_device(&card->dev);
}


static void mmc_host_classdev_release(struct device *dev)
{
	struct mmc_host *host = cls_dev_to_mmc_host(dev);
	kfree(host);
}

static struct class mmc_host_class = {
	.name		= "mmc_host",
	.dev_release	= mmc_host_classdev_release,
};

static DEFINE_IDR(mmc_host_idr);
static DEFINE_SPINLOCK(mmc_host_lock);

/*
 * Internal function. Allocate a new MMC host.
 */
struct mmc_host *mmc_alloc_host_sysfs(int extra, struct device *dev)
{
	struct mmc_host *host;

	host = kmalloc(sizeof(struct mmc_host) + extra, GFP_KERNEL);
	if (host) {
		memset(host, 0, sizeof(struct mmc_host) + extra);

		host->parent = dev;
		host->class_dev.parent = dev;
		host->class_dev.class = &mmc_host_class;
		device_initialize(&host->class_dev);
	}

	return host;
}

/*
 * Internal function. Register a new MMC host with the MMC class.
 */
int mmc_add_host_sysfs(struct mmc_host *host)
{
	int err;

	if (!idr_pre_get(&mmc_host_idr, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&mmc_host_lock);
	err = idr_get_new(&mmc_host_idr, host, &host->index);
	spin_unlock(&mmc_host_lock);
	if (err)
		return err;

	snprintf(host->class_dev.bus_id, BUS_ID_SIZE,
		 "mmc%d", host->index);

	return device_add(&host->class_dev);
}

/*
 * Internal function. Unregister a MMC host with the MMC class.
 */
void mmc_remove_host_sysfs(struct mmc_host *host)
{
	device_del(&host->class_dev);

	spin_lock(&mmc_host_lock);
	idr_remove(&mmc_host_idr, host->index);
	spin_unlock(&mmc_host_lock);
}

/*
 * Internal function. Free a MMC host.
 */
void mmc_free_host_sysfs(struct mmc_host *host)
{
	put_device(&host->class_dev);
}

static struct workqueue_struct *workqueue;

/*
 * Internal function. Schedule delayed work in the MMC work queue.
 */
int mmc_schedule_delayed_work(struct delayed_work *work, unsigned long delay)
{
	return queue_delayed_work(workqueue, work, delay);
}

/*
 * Internal function. Flush all scheduled work from the MMC work queue.
 */
void mmc_flush_scheduled_work(void)
{
	flush_workqueue(workqueue);
}

static int __init mmc_init(void)
{
	int ret;

	workqueue = create_singlethread_workqueue("kmmcd");
	if (!workqueue)
		return -ENOMEM;

	ret = bus_register(&mmc_bus_type);
	if (ret == 0) {
		ret = class_register(&mmc_host_class);
		if (ret)
			bus_unregister(&mmc_bus_type);
	}
	return ret;
}

static void __exit mmc_exit(void)
{
	class_unregister(&mmc_host_class);
	bus_unregister(&mmc_bus_type);
	destroy_workqueue(workqueue);
}

module_init(mmc_init);
module_exit(mmc_exit);
