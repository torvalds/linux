// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/pci.h>
#include "pci.h"

/*
 * On the state of PCI's devres implementation:
 *
 * The older devres API for PCI has two significant problems:
 *
 * 1. It is very strongly tied to the statically allocated mapping table in
 *    struct pcim_iomap_devres below. This is mostly solved in the sense of the
 *    pcim_ functions in this file providing things like ranged mapping by
 *    bypassing this table, whereas the functions that were present in the old
 *    API still enter the mapping addresses into the table for users of the old
 *    API.
 *
 * 2. The region-request-functions in pci.c do become managed IF the device has
 *    been enabled with pcim_enable_device() instead of pci_enable_device().
 *    This resulted in the API becoming inconsistent: Some functions have an
 *    obviously managed counter-part (e.g., pci_iomap() <-> pcim_iomap()),
 *    whereas some don't and are never managed, while others don't and are
 *    _sometimes_ managed (e.g. pci_request_region()).
 *
 *    Consequently, in the new API, region requests performed by the pcim_
 *    functions are automatically cleaned up through the devres callback
 *    pcim_addr_resource_release().
 *
 *    Users of pcim_enable_device() + pci_*region*() are redirected in
 *    pci.c to the managed functions here in this file. This isn't exactly
 *    perfect, but the only alternative way would be to port ALL drivers
 *    using said combination to pcim_ functions.
 *
 * TODO:
 * Remove the legacy table entirely once all calls to pcim_iomap_table() in
 * the kernel have been removed.
 */

/*
 * Legacy struct storing addresses to whole mapped BARs.
 */
struct pcim_iomap_devres {
	void __iomem *table[PCI_NUM_RESOURCES];
};

/* Used to restore the old INTx state on driver detach. */
struct pcim_intx_devres {
	int orig_intx;
};

enum pcim_addr_devres_type {
	/* Default initializer. */
	PCIM_ADDR_DEVRES_TYPE_INVALID,

	/* A requested region spanning an entire BAR. */
	PCIM_ADDR_DEVRES_TYPE_REGION,

	/*
	 * A requested region spanning an entire BAR, and a mapping for
	 * the entire BAR.
	 */
	PCIM_ADDR_DEVRES_TYPE_REGION_MAPPING,

	/*
	 * A mapping within a BAR, either spanning the whole BAR or just a
	 * range.  Without a requested region.
	 */
	PCIM_ADDR_DEVRES_TYPE_MAPPING,
};

/*
 * This struct envelops IO or MEM addresses, i.e., mappings and region
 * requests, because those are very frequently requested and released
 * together.
 */
struct pcim_addr_devres {
	enum pcim_addr_devres_type type;
	void __iomem *baseaddr;
	unsigned long offset;
	unsigned long len;
	int bar;
};

static inline void pcim_addr_devres_clear(struct pcim_addr_devres *res)
{
	memset(res, 0, sizeof(*res));
	res->bar = -1;
}

/*
 * The following functions, __pcim_*_region*, exist as counterparts to the
 * versions from pci.c - which, unfortunately, can be in "hybrid mode", i.e.,
 * sometimes managed, sometimes not.
 *
 * To separate the APIs cleanly, we define our own, simplified versions here.
 */

/**
 * __pcim_request_region_range - Request a ranged region
 * @pdev: PCI device the region belongs to
 * @bar: BAR the range is within
 * @offset: offset from the BAR's start address
 * @maxlen: length in bytes, beginning at @offset
 * @name: name of the driver requesting the resource
 * @req_flags: flags for the request, e.g., for kernel-exclusive requests
 *
 * Returns: 0 on success, a negative error code on failure.
 *
 * Request a range within a device's PCI BAR.  Sanity check the input.
 */
static int __pcim_request_region_range(struct pci_dev *pdev, int bar,
				       unsigned long offset,
				       unsigned long maxlen,
				       const char *name, int req_flags)
{
	resource_size_t start = pci_resource_start(pdev, bar);
	resource_size_t len = pci_resource_len(pdev, bar);
	unsigned long dev_flags = pci_resource_flags(pdev, bar);

	if (start == 0 || len == 0) /* Unused BAR. */
		return 0;
	if (len <= offset)
		return -EINVAL;

	start += offset;
	len -= offset;

	if (len > maxlen && maxlen != 0)
		len = maxlen;

	if (dev_flags & IORESOURCE_IO) {
		if (!request_region(start, len, name))
			return -EBUSY;
	} else if (dev_flags & IORESOURCE_MEM) {
		if (!__request_mem_region(start, len, name, req_flags))
			return -EBUSY;
	} else {
		/* That's not a device we can request anything on. */
		return -ENODEV;
	}

	return 0;
}

static void __pcim_release_region_range(struct pci_dev *pdev, int bar,
					unsigned long offset,
					unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(pdev, bar);
	resource_size_t len = pci_resource_len(pdev, bar);
	unsigned long flags = pci_resource_flags(pdev, bar);

	if (len <= offset || start == 0)
		return;

	if (len == 0 || maxlen == 0) /* This an unused BAR. Do nothing. */
		return;

	start += offset;
	len -= offset;

	if (len > maxlen)
		len = maxlen;

	if (flags & IORESOURCE_IO)
		release_region(start, len);
	else if (flags & IORESOURCE_MEM)
		release_mem_region(start, len);
}

static int __pcim_request_region(struct pci_dev *pdev, int bar,
				 const char *name, int flags)
{
	unsigned long offset = 0;
	unsigned long len = pci_resource_len(pdev, bar);

	return __pcim_request_region_range(pdev, bar, offset, len, name, flags);
}

static void __pcim_release_region(struct pci_dev *pdev, int bar)
{
	unsigned long offset = 0;
	unsigned long len = pci_resource_len(pdev, bar);

	__pcim_release_region_range(pdev, bar, offset, len);
}

static void pcim_addr_resource_release(struct device *dev, void *resource_raw)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcim_addr_devres *res = resource_raw;

	switch (res->type) {
	case PCIM_ADDR_DEVRES_TYPE_REGION:
		__pcim_release_region(pdev, res->bar);
		break;
	case PCIM_ADDR_DEVRES_TYPE_REGION_MAPPING:
		pci_iounmap(pdev, res->baseaddr);
		__pcim_release_region(pdev, res->bar);
		break;
	case PCIM_ADDR_DEVRES_TYPE_MAPPING:
		pci_iounmap(pdev, res->baseaddr);
		break;
	default:
		break;
	}
}

static struct pcim_addr_devres *pcim_addr_devres_alloc(struct pci_dev *pdev)
{
	struct pcim_addr_devres *res;

	res = devres_alloc_node(pcim_addr_resource_release, sizeof(*res),
				GFP_KERNEL, dev_to_node(&pdev->dev));
	if (res)
		pcim_addr_devres_clear(res);
	return res;
}

/* Just for consistency and readability. */
static inline void pcim_addr_devres_free(struct pcim_addr_devres *res)
{
	devres_free(res);
}

/*
 * Used by devres to identify a pcim_addr_devres.
 */
static int pcim_addr_resources_match(struct device *dev,
				     void *a_raw, void *b_raw)
{
	struct pcim_addr_devres *a, *b;

	a = a_raw;
	b = b_raw;

	if (a->type != b->type)
		return 0;

	switch (a->type) {
	case PCIM_ADDR_DEVRES_TYPE_REGION:
	case PCIM_ADDR_DEVRES_TYPE_REGION_MAPPING:
		return a->bar == b->bar;
	case PCIM_ADDR_DEVRES_TYPE_MAPPING:
		return a->baseaddr == b->baseaddr;
	default:
		return 0;
	}
}

static void devm_pci_unmap_iospace(struct device *dev, void *ptr)
{
	struct resource **res = ptr;

	pci_unmap_iospace(*res);
}

/**
 * devm_pci_remap_iospace - Managed pci_remap_iospace()
 * @dev: Generic device to remap IO address for
 * @res: Resource describing the I/O space
 * @phys_addr: physical address of range to be mapped
 *
 * Managed pci_remap_iospace().  Map is automatically unmapped on driver
 * detach.
 */
int devm_pci_remap_iospace(struct device *dev, const struct resource *res,
			   phys_addr_t phys_addr)
{
	const struct resource **ptr;
	int error;

	ptr = devres_alloc(devm_pci_unmap_iospace, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	error = pci_remap_iospace(res, phys_addr);
	if (error) {
		devres_free(ptr);
	} else	{
		*ptr = res;
		devres_add(dev, ptr);
	}

	return error;
}
EXPORT_SYMBOL(devm_pci_remap_iospace);

/**
 * devm_pci_remap_cfgspace - Managed pci_remap_cfgspace()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed pci_remap_cfgspace().  Map is automatically unmapped on driver
 * detach.
 */
void __iomem *devm_pci_remap_cfgspace(struct device *dev,
				      resource_size_t offset,
				      resource_size_t size)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = pci_remap_cfgspace(offset, size);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_pci_remap_cfgspace);

/**
 * devm_pci_remap_cfg_resource - check, request region and ioremap cfg resource
 * @dev: generic device to handle the resource for
 * @res: configuration space resource to be handled
 *
 * Checks that a resource is a valid memory region, requests the memory
 * region and ioremaps with pci_remap_cfgspace() API that ensures the
 * proper PCI configuration space memory attributes are guaranteed.
 *
 * All operations are managed and will be undone on driver detach.
 *
 * Returns a pointer to the remapped memory or an IOMEM_ERR_PTR() encoded error
 * code on failure. Usage example::
 *
 *	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *	base = devm_pci_remap_cfg_resource(&pdev->dev, res);
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 */
void __iomem *devm_pci_remap_cfg_resource(struct device *dev,
					  struct resource *res)
{
	resource_size_t size;
	const char *name;
	void __iomem *dest_ptr;

	BUG_ON(!dev);

	if (!res || resource_type(res) != IORESOURCE_MEM) {
		dev_err(dev, "invalid resource\n");
		return IOMEM_ERR_PTR(-EINVAL);
	}

	size = resource_size(res);

	if (res->name)
		name = devm_kasprintf(dev, GFP_KERNEL, "%s %s", dev_name(dev),
				      res->name);
	else
		name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!name)
		return IOMEM_ERR_PTR(-ENOMEM);

	if (!devm_request_mem_region(dev, res->start, size, name)) {
		dev_err(dev, "can't request region for resource %pR\n", res);
		return IOMEM_ERR_PTR(-EBUSY);
	}

	dest_ptr = devm_pci_remap_cfgspace(dev, res->start, size);
	if (!dest_ptr) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		devm_release_mem_region(dev, res->start, size);
		dest_ptr = IOMEM_ERR_PTR(-ENOMEM);
	}

	return dest_ptr;
}
EXPORT_SYMBOL(devm_pci_remap_cfg_resource);

static void __pcim_clear_mwi(void *pdev_raw)
{
	struct pci_dev *pdev = pdev_raw;

	pci_clear_mwi(pdev);
}

/**
 * pcim_set_mwi - a device-managed pci_set_mwi()
 * @pdev: the PCI device for which MWI is enabled
 *
 * Managed pci_set_mwi().
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int pcim_set_mwi(struct pci_dev *pdev)
{
	int ret;

	ret = devm_add_action(&pdev->dev, __pcim_clear_mwi, pdev);
	if (ret != 0)
		return ret;

	ret = pci_set_mwi(pdev);
	if (ret != 0)
		devm_remove_action(&pdev->dev, __pcim_clear_mwi, pdev);

	return ret;
}
EXPORT_SYMBOL(pcim_set_mwi);

static inline bool mask_contains_bar(int mask, int bar)
{
	return mask & BIT(bar);
}

static void pcim_intx_restore(struct device *dev, void *data)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcim_intx_devres *res = data;

	pci_intx(pdev, res->orig_intx);
}

static void save_orig_intx(struct pci_dev *pdev, struct pcim_intx_devres *res)
{
	u16 pci_command;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
	res->orig_intx = !(pci_command & PCI_COMMAND_INTX_DISABLE);
}

/**
 * pcim_intx - managed pci_intx()
 * @pdev: the PCI device to operate on
 * @enable: boolean: whether to enable or disable PCI INTx
 *
 * Returns: 0 on success, -ENOMEM on error.
 *
 * Enable/disable PCI INTx for device @pdev.
 * Restore the original state on driver detach.
 */
int pcim_intx(struct pci_dev *pdev, int enable)
{
	struct pcim_intx_devres *res;
	struct device *dev = &pdev->dev;

	/*
	 * pcim_intx() must only restore the INTx value that existed before the
	 * driver was loaded, i.e., before it called pcim_intx() for the
	 * first time.
	 */
	res = devres_find(dev, pcim_intx_restore, NULL, NULL);
	if (!res) {
		res = devres_alloc(pcim_intx_restore, sizeof(*res), GFP_KERNEL);
		if (!res)
			return -ENOMEM;

		save_orig_intx(pdev, res);
		devres_add(dev, res);
	}

	pci_intx(pdev, enable);

	return 0;
}
EXPORT_SYMBOL_GPL(pcim_intx);

static void pcim_disable_device(void *pdev_raw)
{
	struct pci_dev *pdev = pdev_raw;

	if (!pdev->pinned)
		pci_disable_device(pdev);

	pdev->is_managed = false;
}

/**
 * pcim_enable_device - Managed pci_enable_device()
 * @pdev: PCI device to be initialized
 *
 * Returns: 0 on success, negative error code on failure.
 *
 * Managed pci_enable_device(). Device will automatically be disabled on
 * driver detach.
 */
int pcim_enable_device(struct pci_dev *pdev)
{
	int ret;

	ret = devm_add_action(&pdev->dev, pcim_disable_device, pdev);
	if (ret != 0)
		return ret;

	/*
	 * We prefer removing the action in case of an error over
	 * devm_add_action_or_reset() because the latter could theoretically be
	 * disturbed by users having pinned the device too soon.
	 */
	ret = pci_enable_device(pdev);
	if (ret != 0) {
		devm_remove_action(&pdev->dev, pcim_disable_device, pdev);
		return ret;
	}

	pdev->is_managed = true;

	return ret;
}
EXPORT_SYMBOL(pcim_enable_device);

/**
 * pcim_pin_device - Pin managed PCI device
 * @pdev: PCI device to pin
 *
 * Pin managed PCI device @pdev. Pinned device won't be disabled on driver
 * detach. @pdev must have been enabled with pcim_enable_device().
 */
void pcim_pin_device(struct pci_dev *pdev)
{
	pdev->pinned = true;
}
EXPORT_SYMBOL(pcim_pin_device);

static void pcim_iomap_release(struct device *gendev, void *res)
{
	/*
	 * Do nothing. This is legacy code.
	 *
	 * Cleanup of the mappings is now done directly through the callbacks
	 * registered when creating them.
	 */
}

/**
 * pcim_iomap_table - access iomap allocation table (DEPRECATED)
 * @pdev: PCI device to access iomap table for
 *
 * Returns:
 * Const pointer to array of __iomem pointers on success, NULL on failure.
 *
 * Access iomap allocation table for @dev.  If iomap table doesn't
 * exist and @pdev is managed, it will be allocated.  All iomaps
 * recorded in the iomap table are automatically unmapped on driver
 * detach.
 *
 * This function might sleep when the table is first allocated but can
 * be safely called without context and guaranteed to succeed once
 * allocated.
 *
 * This function is DEPRECATED. Do not use it in new code. Instead, obtain a
 * mapping's address directly from one of the pcim_* mapping functions. For
 * example:
 * void __iomem \*mappy = pcim_iomap(pdev, bar, length);
 */
void __iomem * const *pcim_iomap_table(struct pci_dev *pdev)
{
	struct pcim_iomap_devres *dr, *new_dr;

	dr = devres_find(&pdev->dev, pcim_iomap_release, NULL, NULL);
	if (dr)
		return dr->table;

	new_dr = devres_alloc_node(pcim_iomap_release, sizeof(*new_dr), GFP_KERNEL,
				   dev_to_node(&pdev->dev));
	if (!new_dr)
		return NULL;
	dr = devres_get(&pdev->dev, new_dr, NULL, NULL);
	return dr->table;
}
EXPORT_SYMBOL(pcim_iomap_table);

/*
 * Fill the legacy mapping-table, so that drivers using the old API can
 * still get a BAR's mapping address through pcim_iomap_table().
 */
static int pcim_add_mapping_to_legacy_table(struct pci_dev *pdev,
					    void __iomem *mapping, int bar)
{
	void __iomem **legacy_iomap_table;

	if (!pci_bar_index_is_valid(bar))
		return -EINVAL;

	legacy_iomap_table = (void __iomem **)pcim_iomap_table(pdev);
	if (!legacy_iomap_table)
		return -ENOMEM;

	/* The legacy mechanism doesn't allow for duplicate mappings. */
	WARN_ON(legacy_iomap_table[bar]);

	legacy_iomap_table[bar] = mapping;

	return 0;
}

/*
 * Remove a mapping. The table only contains whole-BAR mappings, so this will
 * never interfere with ranged mappings.
 */
static void pcim_remove_mapping_from_legacy_table(struct pci_dev *pdev,
						  void __iomem *addr)
{
	int bar;
	void __iomem **legacy_iomap_table;

	legacy_iomap_table = (void __iomem **)pcim_iomap_table(pdev);
	if (!legacy_iomap_table)
		return;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (legacy_iomap_table[bar] == addr) {
			legacy_iomap_table[bar] = NULL;
			return;
		}
	}
}

/*
 * The same as pcim_remove_mapping_from_legacy_table(), but identifies the
 * mapping by its BAR index.
 */
static void pcim_remove_bar_from_legacy_table(struct pci_dev *pdev, int bar)
{
	void __iomem **legacy_iomap_table;

	if (!pci_bar_index_is_valid(bar))
		return;

	legacy_iomap_table = (void __iomem **)pcim_iomap_table(pdev);
	if (!legacy_iomap_table)
		return;

	legacy_iomap_table[bar] = NULL;
}

/**
 * pcim_iomap - Managed pcim_iomap()
 * @pdev: PCI device to iomap for
 * @bar: BAR to iomap
 * @maxlen: Maximum length of iomap
 *
 * Returns: __iomem pointer on success, NULL on failure.
 *
 * Managed pci_iomap(). Map is automatically unmapped on driver detach. If
 * desired, unmap manually only with pcim_iounmap().
 *
 * This SHOULD only be used once per BAR.
 *
 * NOTE:
 * Contrary to the other pcim_* functions, this function does not return an
 * IOMEM_ERR_PTR() on failure, but a simple NULL. This is done for backwards
 * compatibility.
 */
void __iomem *pcim_iomap(struct pci_dev *pdev, int bar, unsigned long maxlen)
{
	void __iomem *mapping;
	struct pcim_addr_devres *res;

	if (!pci_bar_index_is_valid(bar))
		return NULL;

	res = pcim_addr_devres_alloc(pdev);
	if (!res)
		return NULL;
	res->type = PCIM_ADDR_DEVRES_TYPE_MAPPING;

	mapping = pci_iomap(pdev, bar, maxlen);
	if (!mapping)
		goto err_iomap;
	res->baseaddr = mapping;

	if (pcim_add_mapping_to_legacy_table(pdev, mapping, bar) != 0)
		goto err_table;

	devres_add(&pdev->dev, res);
	return mapping;

err_table:
	pci_iounmap(pdev, mapping);
err_iomap:
	pcim_addr_devres_free(res);
	return NULL;
}
EXPORT_SYMBOL(pcim_iomap);

/**
 * pcim_iounmap - Managed pci_iounmap()
 * @pdev: PCI device to iounmap for
 * @addr: Address to unmap
 *
 * Managed pci_iounmap(). @addr must have been mapped using a pcim_* mapping
 * function.
 */
void pcim_iounmap(struct pci_dev *pdev, void __iomem *addr)
{
	struct pcim_addr_devres res_searched;

	pcim_addr_devres_clear(&res_searched);
	res_searched.type = PCIM_ADDR_DEVRES_TYPE_MAPPING;
	res_searched.baseaddr = addr;

	if (devres_release(&pdev->dev, pcim_addr_resource_release,
			pcim_addr_resources_match, &res_searched) != 0) {
		/* Doesn't exist. User passed nonsense. */
		return;
	}

	pcim_remove_mapping_from_legacy_table(pdev, addr);
}
EXPORT_SYMBOL(pcim_iounmap);

/**
 * pcim_iomap_region - Request and iomap a PCI BAR
 * @pdev: PCI device to map IO resources for
 * @bar: Index of a BAR to map
 * @name: Name of the driver requesting the resource
 *
 * Returns: __iomem pointer on success, an IOMEM_ERR_PTR on failure.
 *
 * Mapping and region will get automatically released on driver detach. If
 * desired, release manually only with pcim_iounmap_region().
 */
void __iomem *pcim_iomap_region(struct pci_dev *pdev, int bar,
				       const char *name)
{
	int ret;
	struct pcim_addr_devres *res;

	if (!pci_bar_index_is_valid(bar))
		return IOMEM_ERR_PTR(-EINVAL);

	res = pcim_addr_devres_alloc(pdev);
	if (!res)
		return IOMEM_ERR_PTR(-ENOMEM);

	res->type = PCIM_ADDR_DEVRES_TYPE_REGION_MAPPING;
	res->bar = bar;

	ret = __pcim_request_region(pdev, bar, name, 0);
	if (ret != 0)
		goto err_region;

	res->baseaddr = pci_iomap(pdev, bar, 0);
	if (!res->baseaddr) {
		ret = -EINVAL;
		goto err_iomap;
	}

	devres_add(&pdev->dev, res);
	return res->baseaddr;

err_iomap:
	__pcim_release_region(pdev, bar);
err_region:
	pcim_addr_devres_free(res);

	return IOMEM_ERR_PTR(ret);
}
EXPORT_SYMBOL(pcim_iomap_region);

/**
 * pcim_iounmap_region - Unmap and release a PCI BAR
 * @pdev: PCI device to operate on
 * @bar: Index of BAR to unmap and release
 *
 * Unmap a BAR and release its region manually. Only pass BARs that were
 * previously mapped by pcim_iomap_region().
 */
void pcim_iounmap_region(struct pci_dev *pdev, int bar)
{
	struct pcim_addr_devres res_searched;

	pcim_addr_devres_clear(&res_searched);
	res_searched.type = PCIM_ADDR_DEVRES_TYPE_REGION_MAPPING;
	res_searched.bar = bar;

	devres_release(&pdev->dev, pcim_addr_resource_release,
			pcim_addr_resources_match, &res_searched);
}
EXPORT_SYMBOL(pcim_iounmap_region);

/**
 * pcim_iomap_regions - Request and iomap PCI BARs (DEPRECATED)
 * @pdev: PCI device to map IO resources for
 * @mask: Mask of BARs to request and iomap
 * @name: Name of the driver requesting the resources
 *
 * Returns: 0 on success, negative error code on failure.
 *
 * Request and iomap regions specified by @mask.
 *
 * This function is DEPRECATED. Do not use it in new code.
 * Use pcim_iomap_region() instead.
 */
int pcim_iomap_regions(struct pci_dev *pdev, int mask, const char *name)
{
	int ret;
	int bar;
	void __iomem *mapping;

	for (bar = 0; bar < DEVICE_COUNT_RESOURCE; bar++) {
		if (!mask_contains_bar(mask, bar))
			continue;

		mapping = pcim_iomap_region(pdev, bar, name);
		if (IS_ERR(mapping)) {
			ret = PTR_ERR(mapping);
			goto err;
		}
		ret = pcim_add_mapping_to_legacy_table(pdev, mapping, bar);
		if (ret != 0)
			goto err;
	}

	return 0;

err:
	while (--bar >= 0) {
		pcim_iounmap_region(pdev, bar);
		pcim_remove_bar_from_legacy_table(pdev, bar);
	}

	return ret;
}
EXPORT_SYMBOL(pcim_iomap_regions);

static int _pcim_request_region(struct pci_dev *pdev, int bar, const char *name,
				int request_flags)
{
	int ret;
	struct pcim_addr_devres *res;

	if (!pci_bar_index_is_valid(bar))
		return -EINVAL;

	res = pcim_addr_devres_alloc(pdev);
	if (!res)
		return -ENOMEM;
	res->type = PCIM_ADDR_DEVRES_TYPE_REGION;
	res->bar = bar;

	ret = __pcim_request_region(pdev, bar, name, request_flags);
	if (ret != 0) {
		pcim_addr_devres_free(res);
		return ret;
	}

	devres_add(&pdev->dev, res);
	return 0;
}

/**
 * pcim_request_region - Request a PCI BAR
 * @pdev: PCI device to request region for
 * @bar: Index of BAR to request
 * @name: Name of the driver requesting the resource
 *
 * Returns: 0 on success, a negative error code on failure.
 *
 * Request region specified by @bar.
 *
 * The region will automatically be released on driver detach. If desired,
 * release manually only with pcim_release_region().
 */
int pcim_request_region(struct pci_dev *pdev, int bar, const char *name)
{
	return _pcim_request_region(pdev, bar, name, 0);
}
EXPORT_SYMBOL(pcim_request_region);

/**
 * pcim_request_region_exclusive - Request a PCI BAR exclusively
 * @pdev: PCI device to request region for
 * @bar: Index of BAR to request
 * @name: Name of the driver requesting the resource
 *
 * Returns: 0 on success, a negative error code on failure.
 *
 * Request region specified by @bar exclusively.
 *
 * The region will automatically be released on driver detach. If desired,
 * release manually only with pcim_release_region().
 */
int pcim_request_region_exclusive(struct pci_dev *pdev, int bar, const char *name)
{
	return _pcim_request_region(pdev, bar, name, IORESOURCE_EXCLUSIVE);
}

/**
 * pcim_release_region - Release a PCI BAR
 * @pdev: PCI device to operate on
 * @bar: Index of BAR to release
 *
 * Release a region manually that was previously requested by
 * pcim_request_region().
 */
void pcim_release_region(struct pci_dev *pdev, int bar)
{
	struct pcim_addr_devres res_searched;

	pcim_addr_devres_clear(&res_searched);
	res_searched.type = PCIM_ADDR_DEVRES_TYPE_REGION;
	res_searched.bar = bar;

	devres_release(&pdev->dev, pcim_addr_resource_release,
			pcim_addr_resources_match, &res_searched);
}


/**
 * pcim_release_all_regions - Release all regions of a PCI-device
 * @pdev: the PCI device
 *
 * Release all regions previously requested through pcim_request_region()
 * or pcim_request_all_regions().
 *
 * Can be called from any context, i.e., not necessarily as a counterpart to
 * pcim_request_all_regions().
 */
static void pcim_release_all_regions(struct pci_dev *pdev)
{
	int bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		pcim_release_region(pdev, bar);
}

/**
 * pcim_request_all_regions - Request all regions
 * @pdev: PCI device to map IO resources for
 * @name: name of the driver requesting the resources
 *
 * Returns: 0 on success, negative error code on failure.
 *
 * Requested regions will automatically be released at driver detach. If
 * desired, release individual regions with pcim_release_region() or all of
 * them at once with pcim_release_all_regions().
 */
int pcim_request_all_regions(struct pci_dev *pdev, const char *name)
{
	int ret;
	int bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		ret = pcim_request_region(pdev, bar, name);
		if (ret != 0)
			goto err;
	}

	return 0;

err:
	pcim_release_all_regions(pdev);

	return ret;
}
EXPORT_SYMBOL(pcim_request_all_regions);

/**
 * pcim_iounmap_regions - Unmap and release PCI BARs (DEPRECATED)
 * @pdev: PCI device to map IO resources for
 * @mask: Mask of BARs to unmap and release
 *
 * Unmap and release regions specified by @mask.
 *
 * This function is DEPRECATED. Do not use it in new code.
 * Use pcim_iounmap_region() instead.
 */
void pcim_iounmap_regions(struct pci_dev *pdev, int mask)
{
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (!mask_contains_bar(mask, i))
			continue;

		pcim_iounmap_region(pdev, i);
		pcim_remove_bar_from_legacy_table(pdev, i);
	}
}
EXPORT_SYMBOL(pcim_iounmap_regions);

/**
 * pcim_iomap_range - Create a ranged __iomap mapping within a PCI BAR
 * @pdev: PCI device to map IO resources for
 * @bar: Index of the BAR
 * @offset: Offset from the begin of the BAR
 * @len: Length in bytes for the mapping
 *
 * Returns: __iomem pointer on success, an IOMEM_ERR_PTR on failure.
 *
 * Creates a new IO-Mapping within the specified @bar, ranging from @offset to
 * @offset + @len.
 *
 * The mapping will automatically get unmapped on driver detach. If desired,
 * release manually only with pcim_iounmap().
 */
void __iomem *pcim_iomap_range(struct pci_dev *pdev, int bar,
		unsigned long offset, unsigned long len)
{
	void __iomem *mapping;
	struct pcim_addr_devres *res;

	if (!pci_bar_index_is_valid(bar))
		return IOMEM_ERR_PTR(-EINVAL);

	res = pcim_addr_devres_alloc(pdev);
	if (!res)
		return IOMEM_ERR_PTR(-ENOMEM);

	mapping = pci_iomap_range(pdev, bar, offset, len);
	if (!mapping) {
		pcim_addr_devres_free(res);
		return IOMEM_ERR_PTR(-EINVAL);
	}

	res->type = PCIM_ADDR_DEVRES_TYPE_MAPPING;
	res->baseaddr = mapping;

	/*
	 * Ranged mappings don't get added to the legacy-table, since the table
	 * only ever keeps track of whole BARs.
	 */

	devres_add(&pdev->dev, res);
	return mapping;
}
EXPORT_SYMBOL(pcim_iomap_range);
