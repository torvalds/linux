// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <asm/errno.h>
#include "of_private.h"

/**
 * of_match_device - Tell if a struct device matches an of_device_id list
 * @ids: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an platform_device present in the
 * system is in its list of supported devices.
 */
const struct of_device_id *of_match_device(const struct of_device_id *matches,
					   const struct device *dev)
{
	if ((!matches) || (!dev->of_node))
		return NULL;
	return of_match_node(matches, dev->of_node);
}
EXPORT_SYMBOL(of_match_device);

struct platform_device *of_dev_get(struct platform_device *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->dev);
	if (tmp)
		return to_platform_device(tmp);
	else
		return NULL;
}
EXPORT_SYMBOL(of_dev_get);

void of_dev_put(struct platform_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL(of_dev_put);

int of_device_add(struct platform_device *ofdev)
{
	BUG_ON(ofdev->dev.of_node == NULL);

	/* name and id have to be set so that the platform bus doesn't get
	 * confused on matching */
	ofdev->name = dev_name(&ofdev->dev);
	ofdev->id = PLATFORM_DEVID_NONE;

	/*
	 * If this device has not binding numa node in devicetree, that is
	 * of_node_to_nid returns NUMA_NO_NODE. device_add will assume that this
	 * device is on the same node as the parent.
	 */
	set_dev_node(&ofdev->dev, of_node_to_nid(ofdev->dev.of_node));

	return device_add(&ofdev->dev);
}

/**
 * of_dma_configure - Setup DMA configuration
 * @dev:	Device to apply DMA configuration
 * @np:		Pointer to OF node having DMA configuration
 *
 * Try to get devices's DMA configuration from DT and update it
 * accordingly.
 *
 * If platform code needs to use its own special DMA configuration, it
 * can use a platform bus notifier and handle BUS_NOTIFY_ADD_DEVICE events
 * to fix up DMA configuration.
 */
int of_dma_configure(struct device *dev, struct device_node *np)
{
	u64 dma_addr, paddr, size = 0;
	int ret;
	bool coherent;
	unsigned long offset;
	const struct iommu_ops *iommu;
	u64 mask;

	ret = of_dma_get_range(np, &dma_addr, &paddr, &size);
	if (ret < 0) {
		/*
		 * For legacy reasons, we have to assume some devices need
		 * DMA configuration regardless of whether "dma-ranges" is
		 * correctly specified or not.
		 */
		if (!dev->bus->force_dma)
			return ret == -ENODEV ? 0 : ret;

		dma_addr = offset = 0;
	} else {
		offset = PFN_DOWN(paddr - dma_addr);

		/*
		 * Add a work around to treat the size as mask + 1 in case
		 * it is defined in DT as a mask.
		 */
		if (size & 1) {
			dev_warn(dev, "Invalid size 0x%llx for dma-range\n",
				 size);
			size = size + 1;
		}

		if (!size) {
			dev_err(dev, "Adjusted size 0x%llx invalid\n", size);
			return -EINVAL;
		}
		dev_dbg(dev, "dma_pfn_offset(%#08lx)\n", offset);
	}

	/*
	 * Set default coherent_dma_mask to 32 bit.  Drivers are expected to
	 * setup the correct supported mask.
	 */
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);
	/*
	 * Set it to coherent_dma_mask by default if the architecture
	 * code has not set it.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;

	if (!size)
		size = max(dev->coherent_dma_mask, dev->coherent_dma_mask + 1);

	dev->dma_pfn_offset = offset;

	/*
	 * Limit coherent and dma mask based on size and default mask
	 * set by the driver.
	 */
	mask = DMA_BIT_MASK(ilog2(dma_addr + size - 1) + 1);
	dev->coherent_dma_mask &= mask;
	*dev->dma_mask &= mask;

	coherent = of_dma_is_coherent(np);
	dev_dbg(dev, "device is%sdma coherent\n",
		coherent ? " " : " not ");

	iommu = of_iommu_configure(dev, np);
	if (IS_ERR(iommu) && PTR_ERR(iommu) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	dev_dbg(dev, "device is%sbehind an iommu\n",
		iommu ? " " : " not ");

	arch_setup_dma_ops(dev, dma_addr, size, iommu, coherent);

	return 0;
}
EXPORT_SYMBOL_GPL(of_dma_configure);

/**
 * of_dma_deconfigure - Clean up DMA configuration
 * @dev:	Device for which to clean up DMA configuration
 *
 * Clean up all configuration performed by of_dma_configure_ops() and free all
 * resources that have been allocated.
 */
void of_dma_deconfigure(struct device *dev)
{
	arch_teardown_dma_ops(dev);
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

const void *of_device_get_match_data(const struct device *dev)
{
	const struct of_device_id *match;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match)
		return NULL;

	return match->data;
}
EXPORT_SYMBOL(of_device_get_match_data);

static ssize_t of_device_get_modalias(struct device *dev, char *str, ssize_t len)
{
	const char *compat;
	char *c;
	struct property *p;
	ssize_t csize;
	ssize_t tsize;

	if ((!dev) || (!dev->of_node))
		return -ENODEV;

	/* Name & Type */
	csize = snprintf(str, len, "of:N%sT%s", dev->of_node->name,
			 dev->of_node->type);
	tsize = csize;
	len -= csize;
	if (str)
		str += csize;

	of_property_for_each_string(dev->of_node, "compatible", p, compat) {
		csize = strlen(compat) + 1;
		tsize += csize;
		if (csize > len)
			continue;

		csize = snprintf(str, len, "C%s", compat);
		for (c = str; c; ) {
			c = strchr(c, ' ');
			if (c)
				*c++ = '_';
		}
		len -= csize;
		str += csize;
	}

	return tsize;
}

int of_device_request_module(struct device *dev)
{
	char *str;
	ssize_t size;
	int ret;

	size = of_device_get_modalias(dev, NULL, 0);
	if (size < 0)
		return size;

	str = kmalloc(size + 1, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	of_device_get_modalias(dev, str, size);
	str[size] = '\0';
	ret = request_module(str);
	kfree(str);

	return ret;
}
EXPORT_SYMBOL_GPL(of_device_request_module);

/**
 * of_device_modalias - Fill buffer with newline terminated modalias string
 */
ssize_t of_device_modalias(struct device *dev, char *str, ssize_t len)
{
	ssize_t sl = of_device_get_modalias(dev, str, len - 2);
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
 */
void of_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const char *compat;
	struct alias_prop *app;
	struct property *p;
	int seen = 0;

	if ((!dev) || (!dev->of_node))
		return;

	add_uevent_var(env, "OF_NAME=%s", dev->of_node->name);
	add_uevent_var(env, "OF_FULLNAME=%pOF", dev->of_node);
	if (dev->of_node->type && strcmp("<NULL>", dev->of_node->type) != 0)
		add_uevent_var(env, "OF_TYPE=%s", dev->of_node->type);

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

int of_device_uevent_modalias(struct device *dev, struct kobj_uevent_env *env)
{
	int sl;

	if ((!dev) || (!dev->of_node))
		return -ENODEV;

	/* Devicetree modalias is tricky, we add it in 2 steps */
	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	sl = of_device_get_modalias(dev, &env->buf[env->buflen-1],
				    sizeof(env->buf) - env->buflen);
	if (sl >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;
	env->buflen += sl;

	return 0;
}
EXPORT_SYMBOL_GPL(of_device_uevent_modalias);
