/*
 * drivers/mfd/mfd-core.c
 *
 * core MFD support
 * Copyright (c) 2006 Ian Molton
 * Copyright (c) 2007,2008 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/mfd/core.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

int mfd_shared_cell_enable(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	int err = 0;

	/* only call enable hook if the cell wasn't previously enabled */
	if (atomic_inc_return(cell->usage_count) == 1)
		err = cell->enable(pdev);

	/* if the enable hook failed, decrement counter to allow retries */
	if (err)
		atomic_dec(cell->usage_count);

	return err;
}
EXPORT_SYMBOL(mfd_shared_cell_enable);

int mfd_shared_cell_disable(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	int err = 0;

	/* only disable if no other clients are using it */
	if (atomic_dec_return(cell->usage_count) == 0)
		err = cell->disable(pdev);

	/* if the disable hook failed, increment to allow retries */
	if (err)
		atomic_inc(cell->usage_count);

	/* sanity check; did someone call disable too many times? */
	WARN_ON(atomic_read(cell->usage_count) < 0);

	return err;
}
EXPORT_SYMBOL(mfd_shared_cell_disable);

static int mfd_add_device(struct device *parent, int id,
			  const struct mfd_cell *cell,
			  struct resource *mem_base,
			  int irq_base)
{
	struct resource *res;
	struct platform_device *pdev;
	int ret = -ENOMEM;
	int r;

	pdev = platform_device_alloc(cell->name, id + cell->id);
	if (!pdev)
		goto fail_alloc;

	res = kzalloc(sizeof(*res) * cell->num_resources, GFP_KERNEL);
	if (!res)
		goto fail_device;

	pdev->dev.parent = parent;

	ret = platform_device_add_data(pdev, cell, sizeof(*cell));
	if (ret)
		goto fail_res;

	for (r = 0; r < cell->num_resources; r++) {
		res[r].name = cell->resources[r].name;
		res[r].flags = cell->resources[r].flags;

		/* Find out base to use */
		if ((cell->resources[r].flags & IORESOURCE_MEM) && mem_base) {
			res[r].parent = mem_base;
			res[r].start = mem_base->start +
				cell->resources[r].start;
			res[r].end = mem_base->start +
				cell->resources[r].end;
		} else if (cell->resources[r].flags & IORESOURCE_IRQ) {
			res[r].start = irq_base +
				cell->resources[r].start;
			res[r].end   = irq_base +
				cell->resources[r].end;
		} else {
			res[r].parent = cell->resources[r].parent;
			res[r].start = cell->resources[r].start;
			res[r].end   = cell->resources[r].end;
		}

		if (!cell->ignore_resource_conflicts) {
			ret = acpi_check_resource_conflict(res);
			if (ret)
				goto fail_res;
		}
	}

	ret = platform_device_add_resources(pdev, res, cell->num_resources);
	if (ret)
		goto fail_res;

	ret = platform_device_add(pdev);
	if (ret)
		goto fail_res;

	if (cell->pm_runtime_no_callbacks)
		pm_runtime_no_callbacks(&pdev->dev);

	kfree(res);

	return 0;

/*	platform_device_del(pdev); */
fail_res:
	kfree(res);
fail_device:
	platform_device_put(pdev);
fail_alloc:
	return ret;
}

int mfd_add_devices(struct device *parent, int id,
		    struct mfd_cell *cells, int n_devs,
		    struct resource *mem_base,
		    int irq_base)
{
	int i;
	int ret = 0;
	atomic_t *cnts;

	/* initialize reference counting for all cells */
	cnts = kcalloc(sizeof(*cnts), n_devs, GFP_KERNEL);
	if (!cnts)
		return -ENOMEM;

	for (i = 0; i < n_devs; i++) {
		atomic_set(&cnts[i], 0);
		cells[i].usage_count = &cnts[i];
		ret = mfd_add_device(parent, id, cells + i, mem_base, irq_base);
		if (ret)
			break;
	}

	if (ret)
		mfd_remove_devices(parent);

	return ret;
}
EXPORT_SYMBOL(mfd_add_devices);

static int mfd_remove_devices_fn(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	atomic_t **usage_count = c;

	/* find the base address of usage_count pointers (for freeing) */
	if (!*usage_count || (cell->usage_count < *usage_count))
		*usage_count = cell->usage_count;

	platform_device_unregister(pdev);
	return 0;
}

void mfd_remove_devices(struct device *parent)
{
	atomic_t *cnts = NULL;

	device_for_each_child(parent, &cnts, mfd_remove_devices_fn);
	kfree(cnts);
}
EXPORT_SYMBOL(mfd_remove_devices);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ian Molton, Dmitry Baryshkov");
