// SPDX-License-Identifier: GPL-2.0
//
// Bus implementation for the NuBus subsystem.
//
// Copyright (C) 2017 Finn Thain

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/nubus.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#define to_nubus_board(d)       container_of(d, struct nubus_board, dev)
#define to_nubus_driver(d)      container_of(d, struct nubus_driver, driver)

static int nubus_device_probe(struct device *dev)
{
	struct nubus_driver *ndrv = to_nubus_driver(dev->driver);
	int err = -ENODEV;

	if (ndrv->probe)
		err = ndrv->probe(to_nubus_board(dev));
	return err;
}

static void nubus_device_remove(struct device *dev)
{
	struct nubus_driver *ndrv = to_nubus_driver(dev->driver);

	if (ndrv->remove)
		ndrv->remove(to_nubus_board(dev));
}

static const struct bus_type nubus_bus_type = {
	.name		= "nubus",
	.probe		= nubus_device_probe,
	.remove		= nubus_device_remove,
};

int nubus_driver_register(struct nubus_driver *ndrv)
{
	ndrv->driver.bus = &nubus_bus_type;
	return driver_register(&ndrv->driver);
}
EXPORT_SYMBOL(nubus_driver_register);

void nubus_driver_unregister(struct nubus_driver *ndrv)
{
	driver_unregister(&ndrv->driver);
}
EXPORT_SYMBOL(nubus_driver_unregister);

static struct device nubus_parent = {
	.init_name	= "nubus",
};

static int __init nubus_bus_register(void)
{
	return bus_register(&nubus_bus_type);
}
postcore_initcall(nubus_bus_register);

int __init nubus_parent_device_register(void)
{
	return device_register(&nubus_parent);
}

static void nubus_device_release(struct device *dev)
{
	struct nubus_board *board = to_nubus_board(dev);
	struct nubus_rsrc *fres, *tmp;

	list_for_each_entry_safe(fres, tmp, &nubus_func_rsrcs, list)
		if (fres->board == board) {
			list_del(&fres->list);
			kfree(fres);
		}
	kfree(board);
}

int nubus_device_register(struct nubus_board *board)
{
	board->dev.parent = &nubus_parent;
	board->dev.release = nubus_device_release;
	board->dev.bus = &nubus_bus_type;
	dev_set_name(&board->dev, "slot.%X", board->slot);
	board->dev.dma_mask = &board->dev.coherent_dma_mask;
	dma_set_mask(&board->dev, DMA_BIT_MASK(32));
	return device_register(&board->dev);
}

static int nubus_print_device_name_fn(struct device *dev, void *data)
{
	struct nubus_board *board = to_nubus_board(dev);
	struct seq_file *m = data;

	seq_printf(m, "Slot %X: %s\n", board->slot, board->name);
	return 0;
}

int nubus_proc_show(struct seq_file *m, void *data)
{
	return bus_for_each_dev(&nubus_bus_type, NULL, m,
				nubus_print_device_name_fn);
}
