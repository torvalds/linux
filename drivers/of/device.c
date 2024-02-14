// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-direct.h> /* for bus_dma_region */
#include <linux/dma-map-ops.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <asm/errno.h>
#include "of_private.h"

/**
 * of_match_device - Tell if a struct device matches an of_device_id list
 * @matches: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an platform_device present in the
 * system is in its list of supported devices.
 */
const struct of_device_id *of_match_device(const struct of_device_id *matches,
					   const struct device *dev)
{
	if (!matches || !dev->of_node || dev->of_node_reused)
		return NULL;
	return of_match_node(matches, dev->of_node);
}
EXPORT_SYMBOL(of_match_device);

static void
of_dma_set_restricted_buffer(struct device *dev, struct device_node *np)
{
	struct device_node *node, *of_node = dev->of_node;
	int count, i;

	if (!IS_ENABLED(CONFIG_DMA_RESTRICTED_POOL))
		return;

	count = of_property_count_elems_of_size(of_node, "memory-region",
						sizeof(u32));
	/*
	 * If dev->of_node doesn't exist or doesn't contain memory-region, try
	 * the OF node having DMA configuration.
	 */
	if (count <= 0) {
		of_node = np;
		count = of_property_count_elems_of_size(
			of_node, "memory-region", sizeof(u32));
	}

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(of_node, "memory-region", i);
		/*
		 * There might be multiple memory regions, but only one
		 * restricted-dma-pool region is allowed.
		 */
		if (of_device_is_compatible(node, "restricted-dma-pool") &&
		    of_device_is_available(node)) {
			of_node_put(node);
			break;
		}
		of_node_put(node);
	}

	/*
	 * Attempt to initialize a restricted-dma-pool region if one was found.
	 * Note that count can hold a negative error code.
	 */
	if (i < count && of_reserved_mem_device_init_by_idx(dev, of_node, i))
		dev_warn(dev, "failed to initialise \"restricted-dma-pool\" memory node\n");
}

/**
 * of_dma_configure_id - Setup DMA configuration
 * @dev:	Device to apply DMA configuration
 * @np:		Pointer to OF node having DMA configuration
 * @force_dma:  Whether device is to be set up by of_dma_configure() even if
 *		DMA capability is not explicitly described by firmware.
 * @id:		Optional const pointer value input id
 *
 * Try to get devices's DMA configuration from DT and update it
 * accordingly.
 *
 * If platform code needs to use its own special DMA configuration, it
 * can use a platform bus notifier and handle BUS_NOTIFY_ADD_DEVICE events
 * to fix up DMA configuration.
 */
int of_dma_configure_id(struct device *dev, struct device_node *np,
			bool force_dma, const u32 *id)
{
	const struct bus_dma_region *map = NULL;
	struct device_node *bus_np;
	u64 dma_start = 0;
	u64 mask, end, size = 0;
	bool coherent;
	int iommu_ret;
	int ret;

	if (np == dev->of_node)
		bus_np = __of_get_dma_parent(np);
	else
		bus_np = of_node_get(np);

	ret = of_dma_get_range(bus_np, &map);
	of_node_put(bus_np);
	if (ret < 0) {
		/*
		 * For legacy reasons, we have to assume some devices need
		 * DMA configuration regardless of whether "dma-ranges" is
		 * correctly specified or not.
		 */
		if (!force_dma)
			return ret == -ENODEV ? 0 : ret;
	} else {
		const struct bus_dma_region *r = map;
		u64 dma_end = 0;

		/* Determine the overall bounds of all DMA regions */
		for (dma_start = ~0; r->size; r++) {
			/* Take lower and upper limits */
			if (r->dma_start < dma_start)
				dma_start = r->dma_start;
			if (r->dma_start + r->size > dma_end)
				dma_end = r->dma_start + r->size;
		}
		size = dma_end - dma_start;

		/*
		 * Add a work around to treat the size as mask + 1 in case
		 * it is defined in DT as a mask.
		 */
		if (size & 1) {
			dev_warn(dev, "Invalid size 0x%llx for dma-range(s)\n",
				 size);
			size = size + 1;
		}

		if (!size) {
			dev_err(dev, "Adjusted size 0x%llx invalid\n", size);
			kfree(map);
			return -EINVAL;
		}
	}

	/*
	 * If @dev is expected to be DMA-capable then the bus code that created
	 * it should have initialised its dma_mask pointer by this point. For
	 * now, we'll continue the legacy behaviour of coercing it to the
	 * coherent mask if not, but we'll no longer do so quietly.
	 */
	if (!dev->dma_mask) {
		dev_warn(dev, "DMA mask not set\n");
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	if (!size && dev->coherent_dma_mask)
		size = max(dev->coherent_dma_mask, dev->coherent_dma_mask + 1);
	else if (!size)
		size = 1ULL << 32;

	/*
	 * Limit coherent and dma mask based on size and default mask
	 * set by the driver.
	 */
	end = dma_start + size - 1;
	mask = DMA_BIT_MASK(ilog2(end) + 1);
	dev->coherent_dma_mask &= mask;
	*dev->dma_mask &= mask;
	/* ...but only set bus limit and range map if we found valid dma-ranges earlier */
	if (!ret) {
		dev->bus_dma_limit = end;
		dev->dma_range_map = map;
	}

	coherent = of_dma_is_coherent(np);
	dev_dbg(dev, "device is%sdma coherent\n",
		coherent ? " " : " not ");

	iommu_ret = of_iommu_configure(dev, np, id);
	if (iommu_ret == -EPROBE_DEFER) {
		/* Don't touch range map if it wasn't set from a valid dma-ranges */
		if (!ret)
			dev->dma_range_map = NULL;
		kfree(map);
		return -EPROBE_DEFER;
	} else if (iommu_ret == -ENODEV) {
		dev_dbg(dev, "device is not behind an iommu\n");
	} else if (iommu_ret) {
		dev_err(dev, "iommu configuration for device failed with %pe\n",
			ERR_PTR(iommu_ret));

		/*
		 * Historically this routine doesn't fail driver probing
		 * due to errors in of_iommu_configure()
		 */
	} else
		dev_dbg(dev, "device is behind an iommu\n");

	arch_setup_dma_ops(dev, dma_start, size, coherent);

	if (iommu_ret)
		of_dma_set_restricted_buffer(dev, np);

	return 0;
}
EXPORT_SYMBOL_GPL(of_dma_configure_id);

const void *of_device_get_match_data(const struct device *dev)
{
	const struct of_device_id *match;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match)
		return NULL;

	return match->data;
}
EXPORT_SYMBOL(of_device_get_match_data);

/**
 * of_device_modalias - Fill buffer with newline terminated modalias string
 * @dev:	Calling device
 * @str:	Modalias string
 * @len:	Size of @str
 */
ssize_t of_device_modalias(struct device *dev, char *str, ssize_t len)
{
	ssize_t sl;

	if (!dev || !dev->of_node || dev->of_node_reused)
		return -ENODEV;

	sl = of_modalias(dev->of_node, str, len - 2);
	if (sl < 0)
		return sl;
	if (sl > len - 2)
		return -ENOMEM;

	str[sl++] = '\n';
	str[sl] = 0;
	return sl;
}
EXPORT_SYMBOL_GPL(of_device_modalias);

/**
 * of_device_uevent - Display OF related uevent information
 * @dev:	Device to display the uevent information for
 * @env:	Kernel object's userspace event reference to fill up
 */
void of_device_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const char *compat, *type;
	struct alias_prop *app;
	struct property *p;
	int seen = 0;

	if ((!dev) || (!dev->of_node))
		return;

	add_uevent_var(env, "OF_NAME=%pOFn", dev->of_node);
	add_uevent_var(env, "OF_FULLNAME=%pOF", dev->of_node);
	type = of_node_get_device_type(dev->of_node);
	if (type)
		add_uevent_var(env, "OF_TYPE=%s", type);

	/* Since the compatible field can contain pretty much anything
	 * it's not really legal to split it out with commas. We split it
	 * up using a number of environment variables instead. */
	of_property_for_each_string(dev->of_node, "compatible", p, compat) {
		add_uevent_var(env, "OF_COMPATIBLE_%d=%s", seen, compat);
		seen++;
	}
	add_uevent_var(env, "OF_COMPATIBLE_N=%d", seen);

	seen = 0;
	mutex_lock(&of_mutex);
	list_for_each_entry(app, &aliases_lookup, link) {
		if (dev->of_node == app->np) {
			add_uevent_var(env, "OF_ALIAS_%d=%s", seen,
				       app->alias);
			seen++;
		}
	}
	mutex_unlock(&of_mutex);
}
EXPORT_SYMBOL_GPL(of_device_uevent);

int of_device_uevent_modalias(const struct device *dev, struct kobj_uevent_env *env)
{
	int sl;

	if ((!dev) || (!dev->of_node) || dev->of_node_reused)
		return -ENODEV;

	/* Devicetree modalias is tricky, we add it in 2 steps */
	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	sl = of_modalias(dev->of_node, &env->buf[env->buflen-1],
			 sizeof(env->buf) - env->buflen);
	if (sl < 0)
		return sl;
	if (sl >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;
	env->buflen += sl;

	return 0;
}
EXPORT_SYMBOL_GPL(of_device_uevent_modalias);

/**
 * of_device_make_bus_id - Use the device node data to assign a unique name
 * @dev: pointer to device structure that is linked to a device tree node
 *
 * This routine will first try using the translated bus address to
 * derive a unique name. If it cannot, then it will prepend names from
 * parent nodes until a unique name can be derived.
 */
void of_device_make_bus_id(struct device *dev)
{
	struct device_node *node = dev->of_node;
	const __be32 *reg;
	u64 addr;
	u32 mask;

	/* Construct the name, using parent nodes if necessary to ensure uniqueness */
	while (node->parent) {
		/*
		 * If the address can be translated, then that is as much
		 * uniqueness as we need. Make it the first component and return
		 */
		reg = of_get_property(node, "reg", NULL);
		if (reg && (addr = of_translate_address(node, reg)) != OF_BAD_ADDR) {
			if (!of_property_read_u32(node, "mask", &mask))
				dev_set_name(dev, dev_name(dev) ? "%llx.%x.%pOFn:%s" : "%llx.%x.%pOFn",
					     addr, ffs(mask) - 1, node, dev_name(dev));

			else
				dev_set_name(dev, dev_name(dev) ? "%llx.%pOFn:%s" : "%llx.%pOFn",
					     addr, node, dev_name(dev));
			return;
		}

		/* format arguments only used if dev_name() resolves to NULL */
		dev_set_name(dev, dev_name(dev) ? "%s:%s" : "%s",
			     kbasename(node->full_name), dev_name(dev));
		node = node->parent;
	}
}
EXPORT_SYMBOL_GPL(of_device_make_bus_id);
