// SPDX-License-Identifier: GPL-2.0+
/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    and		 Arnd Bergmann, IBM Corp.
 *    Merged from powerpc/kernel/of_platform.c and
 *    sparc{,64}/kernel/of_device.c by Stephen Rothwell
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/erryes.h>
#include <linux/module.h>
#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_iommu.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

const struct of_device_id of_default_bus_match_table[] = {
	{ .compatible = "simple-bus", },
	{ .compatible = "simple-mfd", },
	{ .compatible = "isa", },
#ifdef CONFIG_ARM_AMBA
	{ .compatible = "arm,amba-bus", },
#endif /* CONFIG_ARM_AMBA */
	{} /* Empty terminated list */
};

static const struct of_device_id of_skipped_yesde_table[] = {
	{ .compatible = "operating-points-v2", },
	{} /* Empty terminated list */
};

/**
 * of_find_device_by_yesde - Find the platform_device associated with a yesde
 * @np: Pointer to device tree yesde
 *
 * Takes a reference to the embedded struct device which needs to be dropped
 * after use.
 *
 * Returns platform_device pointer, or NULL if yest found
 */
struct platform_device *of_find_device_by_yesde(struct device_yesde *np)
{
	struct device *dev;

	dev = bus_find_device_by_of_yesde(&platform_bus_type, np);
	return dev ? to_platform_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_device_by_yesde);

#ifdef CONFIG_OF_ADDRESS
/*
 * The following routines scan a subtree and registers a device for
 * each applicable yesde.
 *
 * Note: sparc doesn't use these routines because it has a different
 * mechanism for creating devices from device tree yesdes.
 */

/**
 * of_device_make_bus_id - Use the device yesde data to assign a unique name
 * @dev: pointer to device structure that is linked to a device tree yesde
 *
 * This routine will first try using the translated bus address to
 * derive a unique name. If it canyest, then it will prepend names from
 * parent yesdes until a unique name can be derived.
 */
static void of_device_make_bus_id(struct device *dev)
{
	struct device_yesde *yesde = dev->of_yesde;
	const __be32 *reg;
	u64 addr;

	/* Construct the name, using parent yesdes if necessary to ensure uniqueness */
	while (yesde->parent) {
		/*
		 * If the address can be translated, then that is as much
		 * uniqueness as we need. Make it the first component and return
		 */
		reg = of_get_property(yesde, "reg", NULL);
		if (reg && (addr = of_translate_address(yesde, reg)) != OF_BAD_ADDR) {
			dev_set_name(dev, dev_name(dev) ? "%llx.%pOFn:%s" : "%llx.%pOFn",
				     addr, yesde, dev_name(dev));
			return;
		}

		/* format arguments only used if dev_name() resolves to NULL */
		dev_set_name(dev, dev_name(dev) ? "%s:%s" : "%s",
			     kbasename(yesde->full_name), dev_name(dev));
		yesde = yesde->parent;
	}
}

/**
 * of_device_alloc - Allocate and initialize an of_device
 * @np: device yesde to assign to device
 * @bus_id: Name to assign to the device.  May be null to use default name.
 * @parent: Parent device.
 */
struct platform_device *of_device_alloc(struct device_yesde *np,
				  const char *bus_id,
				  struct device *parent)
{
	struct platform_device *dev;
	int rc, i, num_reg = 0, num_irq;
	struct resource *res, temp_res;

	dev = platform_device_alloc("", PLATFORM_DEVID_NONE);
	if (!dev)
		return NULL;

	/* count the io and irq resources */
	while (of_address_to_resource(np, num_reg, &temp_res) == 0)
		num_reg++;
	num_irq = of_irq_count(np);

	/* Populate the resource table */
	if (num_irq || num_reg) {
		res = kcalloc(num_irq + num_reg, sizeof(*res), GFP_KERNEL);
		if (!res) {
			platform_device_put(dev);
			return NULL;
		}

		dev->num_resources = num_reg + num_irq;
		dev->resource = res;
		for (i = 0; i < num_reg; i++, res++) {
			rc = of_address_to_resource(np, i, res);
			WARN_ON(rc);
		}
		if (of_irq_to_resource_table(np, res, num_irq) != num_irq)
			pr_debug("yest all legacy IRQ resources mapped for %pOFn\n",
				 np);
	}

	dev->dev.of_yesde = of_yesde_get(np);
	dev->dev.fwyesde = &np->fwyesde;
	dev->dev.parent = parent ? : &platform_bus;

	if (bus_id)
		dev_set_name(&dev->dev, "%s", bus_id);
	else
		of_device_make_bus_id(&dev->dev);

	return dev;
}
EXPORT_SYMBOL(of_device_alloc);

/**
 * of_platform_device_create_pdata - Alloc, initialize and register an of_device
 * @np: pointer to yesde to create device for
 * @bus_id: name to assign device
 * @platform_data: pointer to populate platform_data pointer with
 * @parent: Linux device model parent device.
 *
 * Returns pointer to created platform device, or NULL if a device was yest
 * registered.  Unavailable devices will yest get registered.
 */
static struct platform_device *of_platform_device_create_pdata(
					struct device_yesde *np,
					const char *bus_id,
					void *platform_data,
					struct device *parent)
{
	struct platform_device *dev;

	if (!of_device_is_available(np) ||
	    of_yesde_test_and_set_flag(np, OF_POPULATED))
		return NULL;

	dev = of_device_alloc(np, bus_id, parent);
	if (!dev)
		goto err_clear_flag;

	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	dev->dev.bus = &platform_bus_type;
	dev->dev.platform_data = platform_data;
	of_msi_configure(&dev->dev, dev->dev.of_yesde);

	if (of_device_add(dev) != 0) {
		platform_device_put(dev);
		goto err_clear_flag;
	}

	return dev;

err_clear_flag:
	of_yesde_clear_flag(np, OF_POPULATED);
	return NULL;
}

/**
 * of_platform_device_create - Alloc, initialize and register an of_device
 * @np: pointer to yesde to create device for
 * @bus_id: name to assign device
 * @parent: Linux device model parent device.
 *
 * Returns pointer to created platform device, or NULL if a device was yest
 * registered.  Unavailable devices will yest get registered.
 */
struct platform_device *of_platform_device_create(struct device_yesde *np,
					    const char *bus_id,
					    struct device *parent)
{
	return of_platform_device_create_pdata(np, bus_id, NULL, parent);
}
EXPORT_SYMBOL(of_platform_device_create);

#ifdef CONFIG_ARM_AMBA
static struct amba_device *of_amba_device_create(struct device_yesde *yesde,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	struct amba_device *dev;
	const void *prop;
	int i, ret;

	pr_debug("Creating amba device %pOF\n", yesde);

	if (!of_device_is_available(yesde) ||
	    of_yesde_test_and_set_flag(yesde, OF_POPULATED))
		return NULL;

	dev = amba_device_alloc(NULL, 0, 0);
	if (!dev)
		goto err_clear_flag;

	/* AMBA devices only support a single DMA mask */
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dev.dma_mask = &dev->dev.coherent_dma_mask;

	/* setup generic device info */
	dev->dev.of_yesde = of_yesde_get(yesde);
	dev->dev.fwyesde = &yesde->fwyesde;
	dev->dev.parent = parent ? : &platform_bus;
	dev->dev.platform_data = platform_data;
	if (bus_id)
		dev_set_name(&dev->dev, "%s", bus_id);
	else
		of_device_make_bus_id(&dev->dev);

	/* Allow the HW Peripheral ID to be overridden */
	prop = of_get_property(yesde, "arm,primecell-periphid", NULL);
	if (prop)
		dev->periphid = of_read_ulong(prop, 1);

	/* Decode the IRQs and address ranges */
	for (i = 0; i < AMBA_NR_IRQS; i++)
		dev->irq[i] = irq_of_parse_and_map(yesde, i);

	ret = of_address_to_resource(yesde, 0, &dev->res);
	if (ret) {
		pr_err("amba: of_address_to_resource() failed (%d) for %pOF\n",
		       ret, yesde);
		goto err_free;
	}

	ret = amba_device_add(dev, &iomem_resource);
	if (ret) {
		pr_err("amba_device_add() failed (%d) for %pOF\n",
		       ret, yesde);
		goto err_free;
	}

	return dev;

err_free:
	amba_device_put(dev);
err_clear_flag:
	of_yesde_clear_flag(yesde, OF_POPULATED);
	return NULL;
}
#else /* CONFIG_ARM_AMBA */
static struct amba_device *of_amba_device_create(struct device_yesde *yesde,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	return NULL;
}
#endif /* CONFIG_ARM_AMBA */

/**
 * of_devname_lookup() - Given a device yesde, lookup the preferred Linux name
 */
static const struct of_dev_auxdata *of_dev_lookup(const struct of_dev_auxdata *lookup,
				 struct device_yesde *np)
{
	const struct of_dev_auxdata *auxdata;
	struct resource res;
	int compatible = 0;

	if (!lookup)
		return NULL;

	auxdata = lookup;
	for (; auxdata->compatible; auxdata++) {
		if (!of_device_is_compatible(np, auxdata->compatible))
			continue;
		compatible++;
		if (!of_address_to_resource(np, 0, &res))
			if (res.start != auxdata->phys_addr)
				continue;
		pr_debug("%pOF: devname=%s\n", np, auxdata->name);
		return auxdata;
	}

	if (!compatible)
		return NULL;

	/* Try compatible match if yes phys_addr and name are specified */
	auxdata = lookup;
	for (; auxdata->compatible; auxdata++) {
		if (!of_device_is_compatible(np, auxdata->compatible))
			continue;
		if (!auxdata->phys_addr && !auxdata->name) {
			pr_debug("%pOF: compatible match\n", np);
			return auxdata;
		}
	}

	return NULL;
}

/**
 * of_platform_bus_create() - Create a device for a yesde and its children.
 * @bus: device yesde of the bus to instantiate
 * @matches: match table for bus yesdes
 * @lookup: auxdata table for matching id and platform_data with device yesdes
 * @parent: parent for new device, or NULL for top level.
 * @strict: require compatible property
 *
 * Creates a platform_device for the provided device_yesde, and optionally
 * recursively create devices for all the child yesdes.
 */
static int of_platform_bus_create(struct device_yesde *bus,
				  const struct of_device_id *matches,
				  const struct of_dev_auxdata *lookup,
				  struct device *parent, bool strict)
{
	const struct of_dev_auxdata *auxdata;
	struct device_yesde *child;
	struct platform_device *dev;
	const char *bus_id = NULL;
	void *platform_data = NULL;
	int rc = 0;

	/* Make sure it has a compatible property */
	if (strict && (!of_get_property(bus, "compatible", NULL))) {
		pr_debug("%s() - skipping %pOF, yes compatible prop\n",
			 __func__, bus);
		return 0;
	}

	/* Skip yesdes for which we don't want to create devices */
	if (unlikely(of_match_yesde(of_skipped_yesde_table, bus))) {
		pr_debug("%s() - skipping %pOF yesde\n", __func__, bus);
		return 0;
	}

	if (of_yesde_check_flag(bus, OF_POPULATED_BUS)) {
		pr_debug("%s() - skipping %pOF, already populated\n",
			__func__, bus);
		return 0;
	}

	auxdata = of_dev_lookup(lookup, bus);
	if (auxdata) {
		bus_id = auxdata->name;
		platform_data = auxdata->platform_data;
	}

	if (of_device_is_compatible(bus, "arm,primecell")) {
		/*
		 * Don't return an error here to keep compatibility with older
		 * device tree files.
		 */
		of_amba_device_create(bus, bus_id, platform_data, parent);
		return 0;
	}

	dev = of_platform_device_create_pdata(bus, bus_id, platform_data, parent);
	if (!dev || !of_match_yesde(matches, bus))
		return 0;

	for_each_child_of_yesde(bus, child) {
		pr_debug("   create child: %pOF\n", child);
		rc = of_platform_bus_create(child, matches, lookup, &dev->dev, strict);
		if (rc) {
			of_yesde_put(child);
			break;
		}
	}
	of_yesde_set_flag(bus, OF_POPULATED_BUS);
	return rc;
}

/**
 * of_platform_bus_probe() - Probe the device-tree for platform buses
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table for bus yesdes
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Note that children of the provided root are yest instantiated as devices
 * unless the specified root itself matches the bus list and is yest NULL.
 */
int of_platform_bus_probe(struct device_yesde *root,
			  const struct of_device_id *matches,
			  struct device *parent)
{
	struct device_yesde *child;
	int rc = 0;

	root = root ? of_yesde_get(root) : of_find_yesde_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("%s()\n", __func__);
	pr_debug(" starting at: %pOF\n", root);

	/* Do a self check of bus type, if there's a match, create children */
	if (of_match_yesde(matches, root)) {
		rc = of_platform_bus_create(root, matches, NULL, parent, false);
	} else for_each_child_of_yesde(root, child) {
		if (!of_match_yesde(matches, child))
			continue;
		rc = of_platform_bus_create(child, matches, NULL, parent, false);
		if (rc) {
			of_yesde_put(child);
			break;
		}
	}

	of_yesde_put(root);
	return rc;
}
EXPORT_SYMBOL(of_platform_bus_probe);

/**
 * of_platform_populate() - Populate platform_devices from device tree data
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table, NULL to use the default
 * @lookup: auxdata table for matching id and platform_data with device yesdes
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Similar to of_platform_bus_probe(), this function walks the device tree
 * and creates devices from yesdes.  It differs in that it follows the modern
 * convention of requiring all device yesdes to have a 'compatible' property,
 * and it is suitable for creating devices which are children of the root
 * yesde (of_platform_bus_probe will only create children of the root which
 * are selected by the @matches argument).
 *
 * New board support should be using this function instead of
 * of_platform_bus_probe().
 *
 * Returns 0 on success, < 0 on failure.
 */
int of_platform_populate(struct device_yesde *root,
			const struct of_device_id *matches,
			const struct of_dev_auxdata *lookup,
			struct device *parent)
{
	struct device_yesde *child;
	int rc = 0;

	root = root ? of_yesde_get(root) : of_find_yesde_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("%s()\n", __func__);
	pr_debug(" starting at: %pOF\n", root);

	device_links_supplier_sync_state_pause();
	for_each_child_of_yesde(root, child) {
		rc = of_platform_bus_create(child, matches, lookup, parent, true);
		if (rc) {
			of_yesde_put(child);
			break;
		}
	}
	device_links_supplier_sync_state_resume();

	of_yesde_set_flag(root, OF_POPULATED_BUS);

	of_yesde_put(root);
	return rc;
}
EXPORT_SYMBOL_GPL(of_platform_populate);

int of_platform_default_populate(struct device_yesde *root,
				 const struct of_dev_auxdata *lookup,
				 struct device *parent)
{
	return of_platform_populate(root, of_default_bus_match_table, lookup,
				    parent);
}
EXPORT_SYMBOL_GPL(of_platform_default_populate);

#ifndef CONFIG_PPC
static const struct of_device_id reserved_mem_matches[] = {
	{ .compatible = "qcom,rmtfs-mem" },
	{ .compatible = "qcom,cmd-db" },
	{ .compatible = "ramoops" },
	{}
};

static int __init of_platform_default_populate_init(void)
{
	struct device_yesde *yesde;

	device_links_supplier_sync_state_pause();

	if (!of_have_populated_dt())
		return -ENODEV;

	/*
	 * Handle certain compatibles explicitly, since we don't want to create
	 * platform_devices for every yesde in /reserved-memory with a
	 * "compatible",
	 */
	for_each_matching_yesde(yesde, reserved_mem_matches)
		of_platform_device_create(yesde, NULL, NULL);

	yesde = of_find_yesde_by_path("/firmware");
	if (yesde) {
		of_platform_populate(yesde, NULL, NULL, NULL);
		of_yesde_put(yesde);
	}

	/* Populate everything else. */
	of_platform_default_populate(NULL, NULL, NULL);

	return 0;
}
arch_initcall_sync(of_platform_default_populate_init);

static int __init of_platform_sync_state_init(void)
{
	device_links_supplier_sync_state_resume();
	return 0;
}
late_initcall_sync(of_platform_sync_state_init);
#endif

int of_platform_device_destroy(struct device *dev, void *data)
{
	/* Do yest touch devices yest populated from the device tree */
	if (!dev->of_yesde || !of_yesde_check_flag(dev->of_yesde, OF_POPULATED))
		return 0;

	/* Recurse for any yesdes that were treated as busses */
	if (of_yesde_check_flag(dev->of_yesde, OF_POPULATED_BUS))
		device_for_each_child(dev, NULL, of_platform_device_destroy);

	of_yesde_clear_flag(dev->of_yesde, OF_POPULATED);
	of_yesde_clear_flag(dev->of_yesde, OF_POPULATED_BUS);

	if (dev->bus == &platform_bus_type)
		platform_device_unregister(to_platform_device(dev));
#ifdef CONFIG_ARM_AMBA
	else if (dev->bus == &amba_bustype)
		amba_device_unregister(to_amba_device(dev));
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(of_platform_device_destroy);

/**
 * of_platform_depopulate() - Remove devices populated from device tree
 * @parent: device which children will be removed
 *
 * Complementary to of_platform_populate(), this function removes children
 * of the given device (and, recurrently, their children) that have been
 * created from their respective device tree yesdes (and only those,
 * leaving others - eg. manually created - unharmed).
 */
void of_platform_depopulate(struct device *parent)
{
	if (parent->of_yesde && of_yesde_check_flag(parent->of_yesde, OF_POPULATED_BUS)) {
		device_for_each_child(parent, NULL, of_platform_device_destroy);
		of_yesde_clear_flag(parent->of_yesde, OF_POPULATED_BUS);
	}
}
EXPORT_SYMBOL_GPL(of_platform_depopulate);

static void devm_of_platform_populate_release(struct device *dev, void *res)
{
	of_platform_depopulate(*(struct device **)res);
}

/**
 * devm_of_platform_populate() - Populate platform_devices from device tree data
 * @dev: device that requested to populate from device tree data
 *
 * Similar to of_platform_populate(), but will automatically call
 * of_platform_depopulate() when the device is unbound from the bus.
 *
 * Returns 0 on success, < 0 on failure.
 */
int devm_of_platform_populate(struct device *dev)
{
	struct device **ptr;
	int ret;

	if (!dev)
		return -EINVAL;

	ptr = devres_alloc(devm_of_platform_populate_release,
			   sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = of_platform_populate(dev->of_yesde, NULL, NULL, dev);
	if (ret) {
		devres_free(ptr);
	} else {
		*ptr = dev;
		devres_add(dev, ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_of_platform_populate);

static int devm_of_platform_match(struct device *dev, void *res, void *data)
{
	struct device **ptr = res;

	if (!ptr) {
		WARN_ON(!ptr);
		return 0;
	}

	return *ptr == data;
}

/**
 * devm_of_platform_depopulate() - Remove devices populated from device tree
 * @dev: device that requested to depopulate from device tree data
 *
 * Complementary to devm_of_platform_populate(), this function removes children
 * of the given device (and, recurrently, their children) that have been
 * created from their respective device tree yesdes (and only those,
 * leaving others - eg. manually created - unharmed).
 */
void devm_of_platform_depopulate(struct device *dev)
{
	int ret;

	ret = devres_release(dev, devm_of_platform_populate_release,
			     devm_of_platform_match, dev);

	WARN_ON(ret);
}
EXPORT_SYMBOL_GPL(devm_of_platform_depopulate);

#ifdef CONFIG_OF_DYNAMIC
static int of_platform_yestify(struct yestifier_block *nb,
				unsigned long action, void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct platform_device *pdev_parent, *pdev;
	bool children_left;

	switch (of_reconfig_get_state_change(action, rd)) {
	case OF_RECONFIG_CHANGE_ADD:
		/* verify that the parent is a bus */
		if (!of_yesde_check_flag(rd->dn->parent, OF_POPULATED_BUS))
			return NOTIFY_OK;	/* yest for us */

		/* already populated? (driver using of_populate manually) */
		if (of_yesde_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* pdev_parent may be NULL when yes bus platform device */
		pdev_parent = of_find_device_by_yesde(rd->dn->parent);
		pdev = of_platform_device_create(rd->dn, NULL,
				pdev_parent ? &pdev_parent->dev : NULL);
		of_dev_put(pdev_parent);

		if (pdev == NULL) {
			pr_err("%s: failed to create for '%pOF'\n",
					__func__, rd->dn);
			/* of_platform_device_create tosses the error code */
			return yestifier_from_erryes(-EINVAL);
		}
		break;

	case OF_RECONFIG_CHANGE_REMOVE:

		/* already depopulated? */
		if (!of_yesde_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* find our device by yesde */
		pdev = of_find_device_by_yesde(rd->dn);
		if (pdev == NULL)
			return NOTIFY_OK;	/* yes? yest meant for us */

		/* unregister takes one ref away */
		of_platform_device_destroy(&pdev->dev, &children_left);

		/* and put the reference of the find */
		of_dev_put(pdev);
		break;
	}

	return NOTIFY_OK;
}

static struct yestifier_block platform_of_yestifier = {
	.yestifier_call = of_platform_yestify,
};

void of_platform_register_reconfig_yestifier(void)
{
	WARN_ON(of_reconfig_yestifier_register(&platform_of_yestifier));
}
#endif /* CONFIG_OF_DYNAMIC */

#endif /* CONFIG_OF_ADDRESS */
