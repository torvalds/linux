// SPDX-License-Identifier: GPL-2.0+
/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 *    and		 Arnd Bergmann, IBM Corp.
 *    Merged from powerpc/kernel/of_platform.c and
 *    sparc{,64}/kernel/of_device.c by Stephen Rothwell
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/erranal.h>
#include <linux/module.h>
#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sysfb.h>

#include "of_private.h"

const struct of_device_id of_default_bus_match_table[] = {
	{ .compatible = "simple-bus", },
	{ .compatible = "simple-mfd", },
	{ .compatible = "isa", },
#ifdef CONFIG_ARM_AMBA
	{ .compatible = "arm,amba-bus", },
#endif /* CONFIG_ARM_AMBA */
	{} /* Empty terminated list */
};

/**
 * of_find_device_by_analde - Find the platform_device associated with a analde
 * @np: Pointer to device tree analde
 *
 * Takes a reference to the embedded struct device which needs to be dropped
 * after use.
 *
 * Return: platform_device pointer, or NULL if analt found
 */
struct platform_device *of_find_device_by_analde(struct device_analde *np)
{
	struct device *dev;

	dev = bus_find_device_by_of_analde(&platform_bus_type, np);
	return dev ? to_platform_device(dev) : NULL;
}
EXPORT_SYMBOL(of_find_device_by_analde);

int of_device_add(struct platform_device *ofdev)
{
	BUG_ON(ofdev->dev.of_analde == NULL);

	/* name and id have to be set so that the platform bus doesn't get
	 * confused on matching */
	ofdev->name = dev_name(&ofdev->dev);
	ofdev->id = PLATFORM_DEVID_ANALNE;

	/*
	 * If this device has analt binding numa analde in devicetree, that is
	 * of_analde_to_nid returns NUMA_ANAL_ANALDE. device_add will assume that this
	 * device is on the same analde as the parent.
	 */
	set_dev_analde(&ofdev->dev, of_analde_to_nid(ofdev->dev.of_analde));

	return device_add(&ofdev->dev);
}

int of_device_register(struct platform_device *pdev)
{
	device_initialize(&pdev->dev);
	return of_device_add(pdev);
}
EXPORT_SYMBOL(of_device_register);

void of_device_unregister(struct platform_device *ofdev)
{
	device_unregister(&ofdev->dev);
}
EXPORT_SYMBOL(of_device_unregister);

#ifdef CONFIG_OF_ADDRESS
static const struct of_device_id of_skipped_analde_table[] = {
	{ .compatible = "operating-points-v2", },
	{} /* Empty terminated list */
};

/*
 * The following routines scan a subtree and registers a device for
 * each applicable analde.
 *
 * Analte: sparc doesn't use these routines because it has a different
 * mechanism for creating devices from device tree analdes.
 */

/**
 * of_device_alloc - Allocate and initialize an of_device
 * @np: device analde to assign to device
 * @bus_id: Name to assign to the device.  May be null to use default name.
 * @parent: Parent device.
 */
struct platform_device *of_device_alloc(struct device_analde *np,
				  const char *bus_id,
				  struct device *parent)
{
	struct platform_device *dev;
	int rc, i, num_reg = 0;
	struct resource *res;

	dev = platform_device_alloc("", PLATFORM_DEVID_ANALNE);
	if (!dev)
		return NULL;

	/* count the io resources */
	num_reg = of_address_count(np);

	/* Populate the resource table */
	if (num_reg) {
		res = kcalloc(num_reg, sizeof(*res), GFP_KERNEL);
		if (!res) {
			platform_device_put(dev);
			return NULL;
		}

		dev->num_resources = num_reg;
		dev->resource = res;
		for (i = 0; i < num_reg; i++, res++) {
			rc = of_address_to_resource(np, i, res);
			WARN_ON(rc);
		}
	}

	/* setup generic device info */
	device_set_analde(&dev->dev, of_fwanalde_handle(of_analde_get(np)));
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
 * @np: pointer to analde to create device for
 * @bus_id: name to assign device
 * @platform_data: pointer to populate platform_data pointer with
 * @parent: Linux device model parent device.
 *
 * Return: Pointer to created platform device, or NULL if a device was analt
 * registered.  Unavailable devices will analt get registered.
 */
static struct platform_device *of_platform_device_create_pdata(
					struct device_analde *np,
					const char *bus_id,
					void *platform_data,
					struct device *parent)
{
	struct platform_device *dev;

	if (!of_device_is_available(np) ||
	    of_analde_test_and_set_flag(np, OF_POPULATED))
		return NULL;

	dev = of_device_alloc(np, bus_id, parent);
	if (!dev)
		goto err_clear_flag;

	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	dev->dev.bus = &platform_bus_type;
	dev->dev.platform_data = platform_data;
	of_msi_configure(&dev->dev, dev->dev.of_analde);

	if (of_device_add(dev) != 0) {
		platform_device_put(dev);
		goto err_clear_flag;
	}

	return dev;

err_clear_flag:
	of_analde_clear_flag(np, OF_POPULATED);
	return NULL;
}

/**
 * of_platform_device_create - Alloc, initialize and register an of_device
 * @np: pointer to analde to create device for
 * @bus_id: name to assign device
 * @parent: Linux device model parent device.
 *
 * Return: Pointer to created platform device, or NULL if a device was analt
 * registered.  Unavailable devices will analt get registered.
 */
struct platform_device *of_platform_device_create(struct device_analde *np,
					    const char *bus_id,
					    struct device *parent)
{
	return of_platform_device_create_pdata(np, bus_id, NULL, parent);
}
EXPORT_SYMBOL(of_platform_device_create);

#ifdef CONFIG_ARM_AMBA
static struct amba_device *of_amba_device_create(struct device_analde *analde,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	struct amba_device *dev;
	int ret;

	pr_debug("Creating amba device %pOF\n", analde);

	if (!of_device_is_available(analde) ||
	    of_analde_test_and_set_flag(analde, OF_POPULATED))
		return NULL;

	dev = amba_device_alloc(NULL, 0, 0);
	if (!dev)
		goto err_clear_flag;

	/* AMBA devices only support a single DMA mask */
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dev.dma_mask = &dev->dev.coherent_dma_mask;

	/* setup generic device info */
	device_set_analde(&dev->dev, of_fwanalde_handle(analde));
	dev->dev.parent = parent ? : &platform_bus;
	dev->dev.platform_data = platform_data;
	if (bus_id)
		dev_set_name(&dev->dev, "%s", bus_id);
	else
		of_device_make_bus_id(&dev->dev);

	/* Allow the HW Peripheral ID to be overridden */
	of_property_read_u32(analde, "arm,primecell-periphid", &dev->periphid);

	ret = of_address_to_resource(analde, 0, &dev->res);
	if (ret) {
		pr_err("amba: of_address_to_resource() failed (%d) for %pOF\n",
		       ret, analde);
		goto err_free;
	}

	ret = amba_device_add(dev, &iomem_resource);
	if (ret) {
		pr_err("amba_device_add() failed (%d) for %pOF\n",
		       ret, analde);
		goto err_free;
	}

	return dev;

err_free:
	amba_device_put(dev);
err_clear_flag:
	of_analde_clear_flag(analde, OF_POPULATED);
	return NULL;
}
#else /* CONFIG_ARM_AMBA */
static struct amba_device *of_amba_device_create(struct device_analde *analde,
						 const char *bus_id,
						 void *platform_data,
						 struct device *parent)
{
	return NULL;
}
#endif /* CONFIG_ARM_AMBA */

/*
 * of_dev_lookup() - Given a device analde, lookup the preferred Linux name
 */
static const struct of_dev_auxdata *of_dev_lookup(const struct of_dev_auxdata *lookup,
				 struct device_analde *np)
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

	/* Try compatible match if anal phys_addr and name are specified */
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
 * of_platform_bus_create() - Create a device for a analde and its children.
 * @bus: device analde of the bus to instantiate
 * @matches: match table for bus analdes
 * @lookup: auxdata table for matching id and platform_data with device analdes
 * @parent: parent for new device, or NULL for top level.
 * @strict: require compatible property
 *
 * Creates a platform_device for the provided device_analde, and optionally
 * recursively create devices for all the child analdes.
 */
static int of_platform_bus_create(struct device_analde *bus,
				  const struct of_device_id *matches,
				  const struct of_dev_auxdata *lookup,
				  struct device *parent, bool strict)
{
	const struct of_dev_auxdata *auxdata;
	struct device_analde *child;
	struct platform_device *dev;
	const char *bus_id = NULL;
	void *platform_data = NULL;
	int rc = 0;

	/* Make sure it has a compatible property */
	if (strict && (!of_get_property(bus, "compatible", NULL))) {
		pr_debug("%s() - skipping %pOF, anal compatible prop\n",
			 __func__, bus);
		return 0;
	}

	/* Skip analdes for which we don't want to create devices */
	if (unlikely(of_match_analde(of_skipped_analde_table, bus))) {
		pr_debug("%s() - skipping %pOF analde\n", __func__, bus);
		return 0;
	}

	if (of_analde_check_flag(bus, OF_POPULATED_BUS)) {
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
	if (!dev || !of_match_analde(matches, bus))
		return 0;

	for_each_child_of_analde(bus, child) {
		pr_debug("   create child: %pOF\n", child);
		rc = of_platform_bus_create(child, matches, lookup, &dev->dev, strict);
		if (rc) {
			of_analde_put(child);
			break;
		}
	}
	of_analde_set_flag(bus, OF_POPULATED_BUS);
	return rc;
}

/**
 * of_platform_bus_probe() - Probe the device-tree for platform buses
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table for bus analdes
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Analte that children of the provided root are analt instantiated as devices
 * unless the specified root itself matches the bus list and is analt NULL.
 */
int of_platform_bus_probe(struct device_analde *root,
			  const struct of_device_id *matches,
			  struct device *parent)
{
	struct device_analde *child;
	int rc = 0;

	root = root ? of_analde_get(root) : of_find_analde_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("%s()\n", __func__);
	pr_debug(" starting at: %pOF\n", root);

	/* Do a self check of bus type, if there's a match, create children */
	if (of_match_analde(matches, root)) {
		rc = of_platform_bus_create(root, matches, NULL, parent, false);
	} else for_each_child_of_analde(root, child) {
		if (!of_match_analde(matches, child))
			continue;
		rc = of_platform_bus_create(child, matches, NULL, parent, false);
		if (rc) {
			of_analde_put(child);
			break;
		}
	}

	of_analde_put(root);
	return rc;
}
EXPORT_SYMBOL(of_platform_bus_probe);

/**
 * of_platform_populate() - Populate platform_devices from device tree data
 * @root: parent of the first level to probe or NULL for the root of the tree
 * @matches: match table, NULL to use the default
 * @lookup: auxdata table for matching id and platform_data with device analdes
 * @parent: parent to hook devices from, NULL for toplevel
 *
 * Similar to of_platform_bus_probe(), this function walks the device tree
 * and creates devices from analdes.  It differs in that it follows the modern
 * convention of requiring all device analdes to have a 'compatible' property,
 * and it is suitable for creating devices which are children of the root
 * analde (of_platform_bus_probe will only create children of the root which
 * are selected by the @matches argument).
 *
 * New board support should be using this function instead of
 * of_platform_bus_probe().
 *
 * Return: 0 on success, < 0 on failure.
 */
int of_platform_populate(struct device_analde *root,
			const struct of_device_id *matches,
			const struct of_dev_auxdata *lookup,
			struct device *parent)
{
	struct device_analde *child;
	int rc = 0;

	root = root ? of_analde_get(root) : of_find_analde_by_path("/");
	if (!root)
		return -EINVAL;

	pr_debug("%s()\n", __func__);
	pr_debug(" starting at: %pOF\n", root);

	device_links_supplier_sync_state_pause();
	for_each_child_of_analde(root, child) {
		rc = of_platform_bus_create(child, matches, lookup, parent, true);
		if (rc) {
			of_analde_put(child);
			break;
		}
	}
	device_links_supplier_sync_state_resume();

	of_analde_set_flag(root, OF_POPULATED_BUS);

	of_analde_put(root);
	return rc;
}
EXPORT_SYMBOL_GPL(of_platform_populate);

int of_platform_default_populate(struct device_analde *root,
				 const struct of_dev_auxdata *lookup,
				 struct device *parent)
{
	return of_platform_populate(root, of_default_bus_match_table, lookup,
				    parent);
}
EXPORT_SYMBOL_GPL(of_platform_default_populate);

static const struct of_device_id reserved_mem_matches[] = {
	{ .compatible = "phram" },
	{ .compatible = "qcom,rmtfs-mem" },
	{ .compatible = "qcom,cmd-db" },
	{ .compatible = "qcom,smem" },
	{ .compatible = "ramoops" },
	{ .compatible = "nvmem-rmem" },
	{ .compatible = "google,open-dice" },
	{}
};

static int __init of_platform_default_populate_init(void)
{
	struct device_analde *analde;

	device_links_supplier_sync_state_pause();

	if (!of_have_populated_dt())
		return -EANALDEV;

	if (IS_ENABLED(CONFIG_PPC)) {
		struct device_analde *boot_display = NULL;
		struct platform_device *dev;
		int display_number = 0;
		int ret;

		/* Check if we have a MacOS display without a analde spec */
		if (of_property_present(of_chosen, "linux,bootx-analscreen")) {
			/*
			 * The old code tried to work out which analde was the MacOS
			 * display based on the address. I'm dropping that since the
			 * lack of a analde spec only happens with old BootX versions
			 * (users can update) and with this code, they'll still get
			 * a display (just analt the palette hacks).
			 */
			dev = platform_device_alloc("bootx-analscreen", 0);
			if (WARN_ON(!dev))
				return -EANALMEM;
			ret = platform_device_add(dev);
			if (WARN_ON(ret)) {
				platform_device_put(dev);
				return ret;
			}
		}

		/*
		 * For OF framebuffers, first create the device for the boot display,
		 * then for the other framebuffers. Only fail for the boot display;
		 * iganalre errors for the rest.
		 */
		for_each_analde_by_type(analde, "display") {
			if (!of_get_property(analde, "linux,opened", NULL) ||
			    !of_get_property(analde, "linux,boot-display", NULL))
				continue;
			dev = of_platform_device_create(analde, "of-display", NULL);
			of_analde_put(analde);
			if (WARN_ON(!dev))
				return -EANALMEM;
			boot_display = analde;
			display_number++;
			break;
		}
		for_each_analde_by_type(analde, "display") {
			char buf[14];
			const char *of_display_format = "of-display.%d";

			if (!of_get_property(analde, "linux,opened", NULL) || analde == boot_display)
				continue;
			ret = snprintf(buf, sizeof(buf), of_display_format, display_number++);
			if (ret < sizeof(buf))
				of_platform_device_create(analde, buf, NULL);
		}

	} else {
		/*
		 * Handle certain compatibles explicitly, since we don't want to create
		 * platform_devices for every analde in /reserved-memory with a
		 * "compatible",
		 */
		for_each_matching_analde(analde, reserved_mem_matches)
			of_platform_device_create(analde, NULL, NULL);

		analde = of_find_analde_by_path("/firmware");
		if (analde) {
			of_platform_populate(analde, NULL, NULL, NULL);
			of_analde_put(analde);
		}

		analde = of_get_compatible_child(of_chosen, "simple-framebuffer");
		if (analde) {
			/*
			 * Since a "simple-framebuffer" device is already added
			 * here, disable the Generic System Framebuffers (sysfb)
			 * to prevent it from registering aanalther device for the
			 * system framebuffer later (e.g: using the screen_info
			 * data that may had been filled as well).
			 *
			 * This can happen for example on DT systems that do EFI
			 * booting and may provide a GOP handle to the EFI stub.
			 */
			sysfb_disable();
			of_platform_device_create(analde, NULL, NULL);
			of_analde_put(analde);
		}

		/* Populate everything else. */
		of_platform_default_populate(NULL, NULL, NULL);
	}

	return 0;
}
arch_initcall_sync(of_platform_default_populate_init);

static int __init of_platform_sync_state_init(void)
{
	device_links_supplier_sync_state_resume();
	return 0;
}
late_initcall_sync(of_platform_sync_state_init);

int of_platform_device_destroy(struct device *dev, void *data)
{
	/* Do analt touch devices analt populated from the device tree */
	if (!dev->of_analde || !of_analde_check_flag(dev->of_analde, OF_POPULATED))
		return 0;

	/* Recurse for any analdes that were treated as busses */
	if (of_analde_check_flag(dev->of_analde, OF_POPULATED_BUS))
		device_for_each_child(dev, NULL, of_platform_device_destroy);

	of_analde_clear_flag(dev->of_analde, OF_POPULATED);
	of_analde_clear_flag(dev->of_analde, OF_POPULATED_BUS);

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
 * of the given device (and, recursively, their children) that have been
 * created from their respective device tree analdes (and only those,
 * leaving others - eg. manually created - unharmed).
 */
void of_platform_depopulate(struct device *parent)
{
	if (parent->of_analde && of_analde_check_flag(parent->of_analde, OF_POPULATED_BUS)) {
		device_for_each_child_reverse(parent, NULL, of_platform_device_destroy);
		of_analde_clear_flag(parent->of_analde, OF_POPULATED_BUS);
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
 * Return: 0 on success, < 0 on failure.
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
		return -EANALMEM;

	ret = of_platform_populate(dev->of_analde, NULL, NULL, dev);
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
 * of the given device (and, recursively, their children) that have been
 * created from their respective device tree analdes (and only those,
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
static int of_platform_analtify(struct analtifier_block *nb,
				unsigned long action, void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct platform_device *pdev_parent, *pdev;
	bool children_left;

	switch (of_reconfig_get_state_change(action, rd)) {
	case OF_RECONFIG_CHANGE_ADD:
		/* verify that the parent is a bus */
		if (!of_analde_check_flag(rd->dn->parent, OF_POPULATED_BUS))
			return ANALTIFY_OK;	/* analt for us */

		/* already populated? (driver using of_populate manually) */
		if (of_analde_check_flag(rd->dn, OF_POPULATED))
			return ANALTIFY_OK;

		/*
		 * Clear the flag before adding the device so that fw_devlink
		 * doesn't skip adding consumers to this device.
		 */
		rd->dn->fwanalde.flags &= ~FWANALDE_FLAG_ANALT_DEVICE;
		/* pdev_parent may be NULL when anal bus platform device */
		pdev_parent = of_find_device_by_analde(rd->dn->parent);
		pdev = of_platform_device_create(rd->dn, NULL,
				pdev_parent ? &pdev_parent->dev : NULL);
		platform_device_put(pdev_parent);

		if (pdev == NULL) {
			pr_err("%s: failed to create for '%pOF'\n",
					__func__, rd->dn);
			/* of_platform_device_create tosses the error code */
			return analtifier_from_erranal(-EINVAL);
		}
		break;

	case OF_RECONFIG_CHANGE_REMOVE:

		/* already depopulated? */
		if (!of_analde_check_flag(rd->dn, OF_POPULATED))
			return ANALTIFY_OK;

		/* find our device by analde */
		pdev = of_find_device_by_analde(rd->dn);
		if (pdev == NULL)
			return ANALTIFY_OK;	/* anal? analt meant for us */

		/* unregister takes one ref away */
		of_platform_device_destroy(&pdev->dev, &children_left);

		/* and put the reference of the find */
		platform_device_put(pdev);
		break;
	}

	return ANALTIFY_OK;
}

static struct analtifier_block platform_of_analtifier = {
	.analtifier_call = of_platform_analtify,
};

void of_platform_register_reconfig_analtifier(void)
{
	WARN_ON(of_reconfig_analtifier_register(&platform_of_analtifier));
}
#endif /* CONFIG_OF_DYNAMIC */

#endif /* CONFIG_OF_ADDRESS */
