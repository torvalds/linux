/*
 *  linux/arch/arm/plat-meson/lm.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *  Copyright (C) 2010 Amlogic,Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <plat/lm.h>

static int lm_match(struct device *dev, struct device_driver *drv)
{
    return 1;
}

static int lm_bus_probe(struct device *dev)
{
    struct lm_device *lmdev = to_lm_device(dev);
    struct lm_driver *lmdrv = to_lm_driver(dev->driver);

    return lmdrv->probe(lmdev);
}

static int lm_bus_remove(struct device *dev)
{
    struct lm_device *lmdev = to_lm_device(dev);
    struct lm_driver *lmdrv = to_lm_driver(dev->driver);

    if (lmdrv->remove) {
        lmdrv->remove(lmdev);
    }
    return 0;
}

static struct bus_type lm_bustype = {
    .name       = "logicmodule",
    .match      = lm_match,
    .probe      = lm_bus_probe,
    .remove     = lm_bus_remove,
    //  .suspend    = lm_bus_suspend,
    //  .resume     = lm_bus_resume,
};

static int __init lm_init(void)
{
    int res;

    res = bus_register(&lm_bustype);
    if (res) {
        printk("register lm_bustype error!\n");
    }

    //lm_device_register(&ld);
    return res;
}

postcore_initcall(lm_init);
//module_init(lm_init);

int lm_driver_register(struct lm_driver *drv)
{
    drv->drv.bus = &lm_bustype;
    return driver_register(&drv->drv);
}

void lm_driver_unregister(struct lm_driver *drv)
{
    driver_unregister(&drv->drv);
}

static void lm_device_release(struct device *dev)
{
    struct lm_device *d = to_lm_device(dev);

    kfree(d);
}

int lm_device_register(struct lm_device *dev)
{
    int ret;

    dev->dev.release = lm_device_release;
    dev->dev.bus = &lm_bustype;

    printk( "register lm device %s\n", dev_name(&dev->dev));

//    dev_set_name(&dev->dev, "lm%d", dev->id);
    //dev->resource.name = dev_name(&dev->dev);

 //   ret = request_resource(&iomem_resource, dev->resource);
    //  if (ret == 0) {
    ret = device_register(&dev->dev);
    if(ret){
	printk("lm device %d register failed!\n",dev->id);
    }
    //      if (ret)
    //          release_resource(&dev->resource);
    //  }
    return ret;
}

EXPORT_SYMBOL(lm_driver_register);
EXPORT_SYMBOL(lm_driver_unregister);
