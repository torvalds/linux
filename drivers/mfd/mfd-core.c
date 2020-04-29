// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/mfd/mfd-core.c
 *
 * core MFD support
 * Copyright (c) 2006 Ian Molton
 * Copyright (c) 2007,2008 Dmitry Baryshkov
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/property.h>
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

	if (!cell->enable) {
		dev_dbg(&pdev->dev, "No .enable() call-back registered\n");
		return 0;
	}

	return cell->enable(pdev);
}
EXPORT_SYMBOL(mfd_cell_enable);

int mfd_cell_disable(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);

	if (!cell->disable) {
		dev_dbg(&pdev->dev, "No .disable() call-back registered\n");
		return 0;
	}

	return cell->disable(pdev);
}
EXPORT_SYMBOL(mfd_cell_disable);

#if IS_ENABLED(CONFIG_ACPI)
static void mfd_acpi_add_device(const struct mfd_cell *cell,
				struct platform_device *pdev)
{
	const struct mfd_cell_acpi_match *match = cell->acpi_match;
	struct acpi_device *parent, *child;
	struct acpi_device *adev;

	parent = ACPI_COMPANION(pdev->dev.parent);
	if (!parent)
		return;

	/*
	 * MFD child device gets its ACPI handle either from the ACPI device
	 * directly under the parent that matches the either _HID or _CID, or
	 * _ADR or it will use the parent handle if is no ID is given.
	 *
	 * Note that use of _ADR is a grey area in the ACPI specification,
	 * though Intel Galileo Gen2 is using it to distinguish the children
	 * devices.
	 */
	adev = parent;
	if (match) {
		if (match->pnpid) {
			struct acpi_device_id ids[2] = {};

			strlcpy(ids[0].id, match->pnpid, sizeof(ids[0].id));
			list_for_each_entry(child, &parent->children, node) {
				if (!acpi_match_device_ids(child, ids)) {
					adev = child;
					break;
				}
			}
		} else {
			unsigned long long adr;
			acpi_status status;

			list_for_each_entry(child, &parent->children, node) {
				status = acpi_evaluate_integer(child->handle,
							       "_ADR", NULL,
							       &adr);
				if (ACPI_SUCCESS(status) && match->adr == adr) {
					adev = child;
					break;
				}
			}
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
			  const struct mfd_cell *cell,
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

	pdev->mfd_cell = kmemdup(cell, sizeof(*cell), GFP_KERNEL);
	if (!pdev->mfd_cell)
		goto fail_device;

	res = kcalloc(cell->num_resources, sizeof(*res), GFP_KERNEL);
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
				if (!of_device_is_available(np)) {
					/* Ignore disabled devices error free */
					ret = 0;
					goto fail_alias;
				}
				pdev->dev.of_node = np;
				pdev->dev.fwnode = &np->fwnode;
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

	if (cell->properties) {
		ret = platform_device_add_properties(pdev, cell->properties);
		if (ret)
			goto fail_alias;
	}

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

/**
 * mfd_add_devices - register child devices
 *
 * @parent:	Pointer to parent device.
 * @id:		Can be PLATFORM_DEVID_AUTO to let the Platform API take care
 *		of device numbering, or will be added to a device's cell_id.
 * @cells:	Array of (struct mfd_cell)s describing child devices.
 * @n_devs:	Number of child devices to register.
 * @mem_base:	Parent register range resource for child devices.
 * @irq_base:	Base of the range of virtual interrupt numbers allocated for
 *		this MFD device. Unused if @domain is specified.
 * @domain:	Interrupt domain to create mappings for hardware interrupts.
 */
int mfd_add_devices(struct device *parent, int id,
		    const struct mfd_cell *cells, int n_devs,
		    struct resource *mem_base,
		    int irq_base, struct irq_domain *domain)
{
	int i;
	int ret;

	for (i = 0; i < n_devs; i++) {
		ret = mfd_add_device(parent, id, cells + i, mem_base,
				     irq_base, domain);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	if (i)
		mfd_remove_devices(parent);

	return ret;
}
EXPORT_SYMBOL(mfd_add_devices);

static int mfd_remove_devices_fn(struct device *dev, void *data)
{
	struct platform_device *pdev;
	const struct mfd_cell *cell;

	if (dev->type != &mfd_dev_type)
		return 0;

	pdev = to_platform_device(dev);
	cell = mfd_get_cell(pdev);

	regulator_bulk_unregister_supply_alias(dev, cell->parent_supplies,
					       cell->num_parent_supplies);

	platform_device_unregister(pdev);
	return 0;
}

void mfd_remove_devices(struct device *parent)
{
	device_for_each_child_reverse(parent, NULL, mfd_remove_devices_fn);
}
EXPORT_SYMBOL(mfd_remove_devices);

static void devm_mfd_dev_release(struct device *dev, void *res)
{
	mfd_remove_devices(dev);
}

/**
 * devm_mfd_add_devices - Resource managed version of mfd_add_devices()
 *
 * Returns 0 on success or an appropriate negative error number on failure.
 * All child-devices of the MFD will automatically be removed when it gets
 * unbinded.
 */
int devm_mfd_add_devices(struct device *dev, int id,
			 const struct mfd_cell *cells, int n_devs,
			 struct resource *mem_base,
			 int irq_base, struct irq_domain *domain)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_mfd_dev_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = mfd_add_devices(dev, id, cells, n_devs, mem_base,
			      irq_base, domain);
	if (ret < 0) {
		devres_free(ptr);
		return ret;
	}

	*ptr = dev;
	devres_add(dev, ptr);

	return ret;
}
EXPORT_SYMBOL(devm_mfd_add_devices);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ian Molton, Dmitry Baryshkov");
