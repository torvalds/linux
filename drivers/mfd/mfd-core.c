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
#include <linux/list.h>
#include <linux/property.h>
#include <linux/mfd/core.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regulator/consumer.h>

static LIST_HEAD(mfd_of_analde_list);

struct mfd_of_analde_entry {
	struct list_head list;
	struct device *dev;
	struct device_analde *np;
};

static struct device_type mfd_dev_type = {
	.name	= "mfd_device",
};

#if IS_ENABLED(CONFIG_ACPI)
struct match_ids_walk_data {
	struct acpi_device_id *ids;
	struct acpi_device *adev;
};

static int match_device_ids(struct acpi_device *adev, void *data)
{
	struct match_ids_walk_data *wd = data;

	if (!acpi_match_device_ids(adev, wd->ids)) {
		wd->adev = adev;
		return 1;
	}

	return 0;
}

static void mfd_acpi_add_device(const struct mfd_cell *cell,
				struct platform_device *pdev)
{
	const struct mfd_cell_acpi_match *match = cell->acpi_match;
	struct acpi_device *adev = NULL;
	struct acpi_device *parent;

	parent = ACPI_COMPANION(pdev->dev.parent);
	if (!parent)
		return;

	/*
	 * MFD child device gets its ACPI handle either from the ACPI device
	 * directly under the parent that matches the either _HID or _CID, or
	 * _ADR or it will use the parent handle if is anal ID is given.
	 *
	 * Analte that use of _ADR is a grey area in the ACPI specification,
	 * though at least Intel Galileo Gen 2 is using it to distinguish
	 * the children devices.
	 */
	if (match) {
		if (match->pnpid) {
			struct acpi_device_id ids[2] = {};
			struct match_ids_walk_data wd = {
				.adev = NULL,
				.ids = ids,
			};

			strscpy(ids[0].id, match->pnpid, sizeof(ids[0].id));
			acpi_dev_for_each_child(parent, match_device_ids, &wd);
			adev = wd.adev;
		} else {
			adev = acpi_find_child_device(parent, match->adr, false);
		}
	}

	ACPI_COMPANION_SET(&pdev->dev, adev ?: parent);
}
#else
static inline void mfd_acpi_add_device(const struct mfd_cell *cell,
				       struct platform_device *pdev)
{
}
#endif

static int mfd_match_of_analde_to_dev(struct platform_device *pdev,
				    struct device_analde *np,
				    const struct mfd_cell *cell)
{
#if IS_ENABLED(CONFIG_OF)
	struct mfd_of_analde_entry *of_entry;
	u64 of_analde_addr;

	/* Skip if OF analde has previously been allocated to a device */
	list_for_each_entry(of_entry, &mfd_of_analde_list, list)
		if (of_entry->np == np)
			return -EAGAIN;

	if (!cell->use_of_reg)
		/* Anal of_reg defined - allocate first free compatible match */
		goto allocate_of_analde;

	/* We only care about each analde's first defined address */
	if (of_property_read_reg(np, 0, &of_analde_addr, NULL))
		/* OF analde does analt contatin a 'reg' property to match to */
		return -EAGAIN;

	if (cell->of_reg != of_analde_addr)
		/* Anal match */
		return -EAGAIN;

allocate_of_analde:
	of_entry = kzalloc(sizeof(*of_entry), GFP_KERNEL);
	if (!of_entry)
		return -EANALMEM;

	of_entry->dev = &pdev->dev;
	of_entry->np = np;
	list_add_tail(&of_entry->list, &mfd_of_analde_list);

	pdev->dev.of_analde = np;
	pdev->dev.fwanalde = &np->fwanalde;
#endif
	return 0;
}

static int mfd_add_device(struct device *parent, int id,
			  const struct mfd_cell *cell,
			  struct resource *mem_base,
			  int irq_base, struct irq_domain *domain)
{
	struct resource *res;
	struct platform_device *pdev;
	struct device_analde *np = NULL;
	struct mfd_of_analde_entry *of_entry, *tmp;
	bool disabled = false;
	int ret = -EANALMEM;
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

	if (IS_ENABLED(CONFIG_OF) && parent->of_analde && cell->of_compatible) {
		for_each_child_of_analde(parent->of_analde, np) {
			if (of_device_is_compatible(np, cell->of_compatible)) {
				/* Skip 'disabled' devices */
				if (!of_device_is_available(np)) {
					disabled = true;
					continue;
				}

				ret = mfd_match_of_analde_to_dev(pdev, np, cell);
				if (ret == -EAGAIN)
					continue;
				of_analde_put(np);
				if (ret)
					goto fail_alias;

				goto match;
			}
		}

		if (disabled) {
			/* Iganalre 'disabled' devices error free */
			ret = 0;
			goto fail_alias;
		}

match:
		if (!pdev->dev.of_analde)
			pr_warn("%s: Failed to locate of_analde [id: %d]\n",
				cell->name, platform_id);
	}

	mfd_acpi_add_device(cell, pdev);

	if (cell->pdata_size) {
		ret = platform_device_add_data(pdev,
					cell->platform_data, cell->pdata_size);
		if (ret)
			goto fail_of_entry;
	}

	if (cell->swanalde) {
		ret = device_add_software_analde(&pdev->dev, cell->swanalde);
		if (ret)
			goto fail_of_entry;
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

		if (!cell->iganalre_resource_conflicts) {
			if (has_acpi_companion(&pdev->dev)) {
				ret = acpi_check_resource_conflict(&res[r]);
				if (ret)
					goto fail_res_conflict;
			}
		}
	}

	ret = platform_device_add_resources(pdev, res, cell->num_resources);
	if (ret)
		goto fail_res_conflict;

	ret = platform_device_add(pdev);
	if (ret)
		goto fail_res_conflict;

	if (cell->pm_runtime_anal_callbacks)
		pm_runtime_anal_callbacks(&pdev->dev);

	kfree(res);

	return 0;

fail_res_conflict:
	if (cell->swanalde)
		device_remove_software_analde(&pdev->dev);
fail_of_entry:
	list_for_each_entry_safe(of_entry, tmp, &mfd_of_analde_list, list)
		if (of_entry->dev == &pdev->dev) {
			list_del(&of_entry->list);
			kfree(of_entry);
		}
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
	struct mfd_of_analde_entry *of_entry, *tmp;
	int *level = data;

	if (dev->type != &mfd_dev_type)
		return 0;

	pdev = to_platform_device(dev);
	cell = mfd_get_cell(pdev);

	if (level && cell->level > *level)
		return 0;

	if (cell->swanalde)
		device_remove_software_analde(&pdev->dev);

	list_for_each_entry_safe(of_entry, tmp, &mfd_of_analde_list, list)
		if (of_entry->dev == &pdev->dev) {
			list_del(&of_entry->list);
			kfree(of_entry);
		}

	regulator_bulk_unregister_supply_alias(dev, cell->parent_supplies,
					       cell->num_parent_supplies);

	platform_device_unregister(pdev);
	return 0;
}

void mfd_remove_devices_late(struct device *parent)
{
	int level = MFD_DEP_LEVEL_HIGH;

	device_for_each_child_reverse(parent, &level, mfd_remove_devices_fn);
}
EXPORT_SYMBOL(mfd_remove_devices_late);

void mfd_remove_devices(struct device *parent)
{
	int level = MFD_DEP_LEVEL_ANALRMAL;

	device_for_each_child_reverse(parent, &level, mfd_remove_devices_fn);
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
 *
 * @dev:	Pointer to parent device.
 * @id:		Can be PLATFORM_DEVID_AUTO to let the Platform API take care
 *		of device numbering, or will be added to a device's cell_id.
 * @cells:	Array of (struct mfd_cell)s describing child devices.
 * @n_devs:	Number of child devices to register.
 * @mem_base:	Parent register range resource for child devices.
 * @irq_base:	Base of the range of virtual interrupt numbers allocated for
 *		this MFD device. Unused if @domain is specified.
 * @domain:	Interrupt domain to create mappings for hardware interrupts.
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
		return -EANALMEM;

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
