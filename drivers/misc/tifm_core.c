/*
 *  tifm_core.c - TI FlashMedia driver
 *
 *  Copyright (C) 2006 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/tifm.h>
#include <linux/init.h>
#include <linux/idr.h>

#define DRIVER_NAME "tifm_core"
#define DRIVER_VERSION "0.7"

static DEFINE_IDR(tifm_adapter_idr);
static DEFINE_SPINLOCK(tifm_adapter_lock);

static tifm_media_id *tifm_device_match(tifm_media_id *ids,
			struct tifm_dev *dev)
{
	while (*ids) {
		if (dev->media_id == *ids)
			return ids;
		ids++;
	}
	return NULL;
}

static int tifm_match(struct device *dev, struct device_driver *drv)
{
	struct tifm_dev *fm_dev = container_of(dev, struct tifm_dev, dev);
	struct tifm_driver *fm_drv;

	fm_drv = container_of(drv, struct tifm_driver, driver);
	if (!fm_drv->id_table)
		return -EINVAL;
	if (tifm_device_match(fm_drv->id_table, fm_dev))
		return 1;
	return -ENODEV;
}

static int tifm_uevent(struct device *dev, char **envp, int num_envp,
		       char *buffer, int buffer_size)
{
	struct tifm_dev *fm_dev;
	int i = 0;
	int length = 0;
	const char *card_type_name[] = {"INV", "SM", "MS", "SD"};

	if (!dev || !(fm_dev = container_of(dev, struct tifm_dev, dev)))
		return -ENODEV;
	if (add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &length,
			"TIFM_CARD_TYPE=%s", card_type_name[fm_dev->media_id]))
		return -ENOMEM;

	return 0;
}

#ifdef CONFIG_PM

static int tifm_device_suspend(struct device *dev, pm_message_t state)
{
	struct tifm_dev *fm_dev = container_of(dev, struct tifm_dev, dev);
	struct tifm_driver *drv = fm_dev->drv;

	if (drv && drv->suspend)
		return drv->suspend(fm_dev, state);
	return 0;
}

static int tifm_device_resume(struct device *dev)
{
	struct tifm_dev *fm_dev = container_of(dev, struct tifm_dev, dev);
	struct tifm_driver *drv = fm_dev->drv;

	if (drv && drv->resume)
		return drv->resume(fm_dev);
	return 0;
}

#else

#define tifm_device_suspend NULL
#define tifm_device_resume NULL

#endif /* CONFIG_PM */

static struct bus_type tifm_bus_type = {
	.name    = "tifm",
	.match   = tifm_match,
	.uevent  = tifm_uevent,
	.suspend = tifm_device_suspend,
	.resume  = tifm_device_resume
};

static void tifm_free(struct class_device *cdev)
{
	struct tifm_adapter *fm = container_of(cdev, struct tifm_adapter, cdev);

	kfree(fm->sockets);
	kfree(fm);
}

static struct class tifm_adapter_class = {
	.name    = "tifm_adapter",
	.release = tifm_free
};

struct tifm_adapter *tifm_alloc_adapter(void)
{
	struct tifm_adapter *fm;

	fm = kzalloc(sizeof(struct tifm_adapter), GFP_KERNEL);
	if (fm) {
		fm->cdev.class = &tifm_adapter_class;
		spin_lock_init(&fm->lock);
		class_device_initialize(&fm->cdev);
	}
	return fm;
}
EXPORT_SYMBOL(tifm_alloc_adapter);

void tifm_free_adapter(struct tifm_adapter *fm)
{
	class_device_put(&fm->cdev);
}
EXPORT_SYMBOL(tifm_free_adapter);

int tifm_add_adapter(struct tifm_adapter *fm,
		     int (*mediathreadfn)(void *data))
{
	int rc;

	if (!idr_pre_get(&tifm_adapter_idr, GFP_KERNEL))
		return -ENOMEM;

	spin_lock(&tifm_adapter_lock);
	rc = idr_get_new(&tifm_adapter_idr, fm, &fm->id);
	spin_unlock(&tifm_adapter_lock);
	if (!rc) {
		snprintf(fm->cdev.class_id, BUS_ID_SIZE, "tifm%u", fm->id);
		fm->media_switcher = kthread_create(mediathreadfn,
						    fm, "tifm/%u", fm->id);

		if (!IS_ERR(fm->media_switcher))
			return class_device_add(&fm->cdev);

		spin_lock(&tifm_adapter_lock);
		idr_remove(&tifm_adapter_idr, fm->id);
		spin_unlock(&tifm_adapter_lock);
		rc = -ENOMEM;
	}
	return rc;
}
EXPORT_SYMBOL(tifm_add_adapter);

void tifm_remove_adapter(struct tifm_adapter *fm)
{
	class_device_del(&fm->cdev);

	spin_lock(&tifm_adapter_lock);
	idr_remove(&tifm_adapter_idr, fm->id);
	spin_unlock(&tifm_adapter_lock);
}
EXPORT_SYMBOL(tifm_remove_adapter);

void tifm_free_device(struct device *dev)
{
	struct tifm_dev *fm_dev = container_of(dev, struct tifm_dev, dev);
	kfree(fm_dev);
}
EXPORT_SYMBOL(tifm_free_device);

static void tifm_dummy_signal_irq(struct tifm_dev *sock,
				  unsigned int sock_irq_status)
{
	return;
}

struct tifm_dev *tifm_alloc_device(struct tifm_adapter *fm)
{
	struct tifm_dev *dev = kzalloc(sizeof(struct tifm_dev), GFP_KERNEL);

	if (dev) {
		spin_lock_init(&dev->lock);

		dev->dev.parent = fm->dev;
		dev->dev.bus = &tifm_bus_type;
		dev->dev.release = tifm_free_device;
		dev->signal_irq = tifm_dummy_signal_irq;
	}
	return dev;
}
EXPORT_SYMBOL(tifm_alloc_device);

void tifm_eject(struct tifm_dev *sock)
{
	struct tifm_adapter *fm = dev_get_drvdata(sock->dev.parent);
	fm->eject(fm, sock);
}
EXPORT_SYMBOL(tifm_eject);

int tifm_map_sg(struct tifm_dev *sock, struct scatterlist *sg, int nents,
		int direction)
{
	return pci_map_sg(to_pci_dev(sock->dev.parent), sg, nents, direction);
}
EXPORT_SYMBOL(tifm_map_sg);

void tifm_unmap_sg(struct tifm_dev *sock, struct scatterlist *sg, int nents,
		   int direction)
{
	pci_unmap_sg(to_pci_dev(sock->dev.parent), sg, nents, direction);
}
EXPORT_SYMBOL(tifm_unmap_sg);

static int tifm_device_probe(struct device *dev)
{
	struct tifm_driver *drv;
	struct tifm_dev *fm_dev;
	int rc = 0;
	const tifm_media_id *id;

	drv = container_of(dev->driver, struct tifm_driver, driver);
	fm_dev = container_of(dev, struct tifm_dev, dev);
	get_device(dev);
	if (!fm_dev->drv && drv->probe && drv->id_table) {
		rc = -ENODEV;
		id = tifm_device_match(drv->id_table, fm_dev);
		if (id)
			rc = drv->probe(fm_dev);
		if (rc >= 0) {
			rc = 0;
			fm_dev->drv = drv;
		}
	}
	if (rc)
		put_device(dev);
	return rc;
}

static int tifm_device_remove(struct device *dev)
{
	struct tifm_dev *fm_dev = container_of(dev, struct tifm_dev, dev);
	struct tifm_driver *drv = fm_dev->drv;

	if (drv) {
		fm_dev->signal_irq = tifm_dummy_signal_irq;
		if (drv->remove)
			drv->remove(fm_dev);
		fm_dev->drv = NULL;
	}

	put_device(dev);
	return 0;
}

int tifm_register_driver(struct tifm_driver *drv)
{
	drv->driver.bus = &tifm_bus_type;
	drv->driver.probe = tifm_device_probe;
	drv->driver.remove = tifm_device_remove;
	drv->driver.suspend = tifm_device_suspend;
	drv->driver.resume = tifm_device_resume;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(tifm_register_driver);

void tifm_unregister_driver(struct tifm_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(tifm_unregister_driver);

static int __init tifm_init(void)
{
	int rc = bus_register(&tifm_bus_type);

	if (!rc) {
		rc = class_register(&tifm_adapter_class);
		if (rc)
			bus_unregister(&tifm_bus_type);
	}

	return rc;
}

static void __exit tifm_exit(void)
{
	class_unregister(&tifm_adapter_class);
	bus_unregister(&tifm_bus_type);
}

subsys_initcall(tifm_init);
module_exit(tifm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Dubov");
MODULE_DESCRIPTION("TI FlashMedia core driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
