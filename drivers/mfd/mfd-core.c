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
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

static struct device_type mfd_dev_type = {
	.name	= "mfd_device",
};

int mfd_cell_enable(struct platform_device *pdev)
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
EXPORT_SYMBOL(mfd_cell_enable);

int mfd_cell_disable(struct platform_device *pdev)
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
EXPORT_SYMBOL(mfd_cell_disable);

static int mfd_platform_add_cell(struct platform_device *pdev,
				 const struct mfd_cell *cell,
				 atomic_t *usage_count)
{
	if (!cell)
		return 0;

	pdev->mfd_cell = kmemdup(cell, sizeof(*cell), GFP_KERNEL);
	if (!pdev->mfd_cell)
		return -ENOMEM;

	pdev->mfd_cell->usage_count = usage_count;
	return 0;
}

#if IS_ENABLED(CONFIG_ACPI)
static void mfd_acpi_add_device(const struct mfd_cell *cell,
				struct platform_device *pdev)
{
	struct acpi_device *parent_adev;
	struct acpi_device *adev;

	parent_adev = ACPI_COMPANION(pdev->dev.parent);
	if (!parent_adev)
		return;

	/*
	 * MFD child device gets its ACPI handle either from the ACPI
	 * device directly under the parent that matches the acpi_pnpid or
	 * it will use the parent handle if is no acpi_pnpid is given.
	 */
	adev = parent_adev;
	if (cell->acpi_pnpid) {
		struct acpi_device_id ids[2] = {};
		struct acpi_device *child_adev;

		strlcpy(ids[0].id, cell->acpi_pnpid, sizeof(ids[0].id));
		list_for_each_entry(child_adev, &parent_adev->children, node)
			if (acpi_match_device_ids(child_adev, ids)) {
				adev = child_adev;
				break;
			}
	}

	ACPI_COMPANION_SET(&pdev->dev, adev);
}
#else
static inline void mfd_acpi_add_device(const struct mfd_cell *cell,
				       struct platform_device *pdev)
{
}
#endif

static int mfd_add_device(struct device *parent, int id,
			  const struct mfd_cell *cell, atomic_t *usage_count,
			  struct resource *mem_base,
			  int irq_base, struct irq_domain *domain)
{
	struct resource *res;
	struct platform_device *pdev;
	struct device_node *np = NULL;
	int ret = -ENOMEM;
	int platform_id;
	int r;

	if (id == PLATFORM_DEVID_AUTO)
		platform_id = id;
	else
		platform_id = id + cell->id;

	pdev = platform_device_alloc(cell->name, platform_id);
	if (!pdev)
		goto fail_alloc;

	res = kzalloc(sizeof(*res) * cell->num_resources, GFP_KERNEL);
	if (!res)
		goto fail_device;

	pdev->dev.parent = parent;
	pdev->dev.type = &mfd_dev_type;
	pdev->dev.dma_mask = parent->dma_mask;
	pdev->dev.dma_parms = parent->dma_parms;
	pdev->dev.coherent_dma_mask = parent->coherent_dma_mask;

	ret = regulator_bulk_register_supply_alias(
			&pdev->dev, cell->parent_supplies,
			parent, cell->parent_supplies,
			cell->num_parent_supplies);
	if (ret < 0)
		goto fail_res;

	if (parent->of_node && cell->of_compatible) {
		for_each_child_of_node(parent->of_node, np) {
			if (of_device_is_compatible(np, cell->of_compatible)) {
				pdev->dev.of_node = np;
				break;
			}
		}
	}

	mfd_acpi_add_device(cell, pdev);

	if (cell->pdata_size) {
		ret = platform_device_add_data(pdev,
					cell->platform_data, cell->pdata_size);
		if (ret)
			goto fail_alias;
	}

	ret = mfd_platform_add_cell(pdev, cell, usage_count);
	if (ret)
		goto fail_alias;

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
			if (domain) {
				/* Unable to create mappings for IRQ ranges. */
				WARN_ON(cell->resources[r].start !=
					cell->resources[r].end);
				res[r].start = res[r].end = irq_create_mapping(
					domain, cell->resources[r].start);
			} else {
				res[r].start = irq_base +
					cell->resources[r].start;
				res[r].end   = irq_base +
					cell->resources[r].end;
			}
		} else {
			res[r].parent = cell->resources[r].parent;
			res[r].start = cell->resources[r].start;
			res[r].end   = cell->resources[r].end;
		}

		if (!cell->ignore_resource_conflicts) {
			if (has_acpi_companion(&pdev->dev)) {
				ret = acpi_check_resource_conflict(&res[r]);
				if (ret)
					goto fail_alias;
			}
		}
	}

	ret = platform_device_add_resources(pdev, res, cell->num_resources);
	if (ret)
		goto fail_alias;

	ret = platform_device_add(pdev);
	if (ret)
		goto fail_alias;

	if (cell->pm_runtime_no_callbacks)
		pm_runtime_no_callbacks(&pdev->dev);

	kfree(res);

	return 0;

fail_alias:
	regulator_bulk_unregister_supply_alias(&pdev->dev,
					       cell->parent_supplies,
					       cell->num_parent_supplies);
fail_res:
	kfree(res);
fail_device:
	platform_device_put(pdev);
fail_alloc:
	return ret;
}

int mfd_add_devices(struct device *parent, int id,
		    const struct mfd_cell *cells, int n_devs,
		    struct resource *mem_base,
		    int irq_base, struct irq_domain *domain)
{
	int i;
	int ret;
	atomic_t *cnts;

	/* initialize reference counting for all cells */
	cnts = kcalloc(n_devs, sizeof(*cnts), GFP_KERNEL);
	if (!cnts)
		return -ENOMEM;

	for (i = 0; i < n_devs; i++) {
		atomic_set(&cnts[i], 0);
		ret = mfd_add_device(parent, id, cells + i, cnts + i, mem_base,
				     irq_base, domain);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	if (i)
		mfd_remove_devices(parent);
	else
		kfree(cnts);
	return ret;
}
EXPORT_SYMBOL(mfd_add_devices);

static int mfd_remove_devices_fn(struct device *dev, void *c)
{
	struct platform_device *pdev;
	const struct mfd_cell *cell;
	atomic_t **usage_count = c;

	if (dev->type != &mfd_dev_type)
		return 0;

	pdev = to_platform_device(dev);
	cell = mfd_get_cell(pdev);

	regulator_bulk_unregister_supply_alias(dev, cell->parent_supplies,
					       cell->num_parent_supplies);

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

int mfd_clone_cell(const char *cell, const char **clones, size_t n_clones)
{
	struct mfd_cell cell_entry;
	struct device *dev;
	struct platform_device *pdev;
	int i;

	/* fetch the parent cell's device (should already be registered!) */
	dev = bus_find_device_by_name(&platform_bus_type, NULL, cell);
	if (!dev) {
		printk(KERN_ERR "failed to find device for cell %s\n", cell);
		return -ENODEV;
	}
	pdev = to_platform_device(dev);
	memcpy(&cell_entry, mfd_get_cell(pdev), sizeof(cell_entry));

	WARN_ON(!cell_entry.enable);

	for (i = 0; i < n_clones; i++) {
		cell_entry.name = clones[i];
		/* don't give up if a single call fails; just report error */
		if (mfd_add_device(pdev->dev.parent, -1, &cell_entry,
				   cell_entry.usage_count, NULL, 0, NULL))
			dev_err(dev, "failed to create platform device '%s'\n",
					clones[i]);
	}

	return 0;
}
EXPORT_SYMBOL(mfd_clone_cell);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ian Molton, Dmitry Baryshkov");
