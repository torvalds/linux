#define pr_fmt(fmt)	"OF: PCI: " fmt

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_pci.h>
#include <linux/slab.h>

static inline int __of_pci_pci_compare(struct device_node *node,
				       unsigned int data)
{
	int devfn;

	devfn = of_pci_get_devfn(node);
	if (devfn < 0)
		return 0;

	return devfn == data;
}

struct device_node *of_pci_find_child_device(struct device_node *parent,
					     unsigned int devfn)
{
	struct device_node *node, *node2;

	for_each_child_of_node(parent, node) {
		if (__of_pci_pci_compare(node, devfn))
			return node;
		/*
		 * Some OFs create a parent node "multifunc-device" as
		 * a fake root for all functions of a multi-function
		 * device we go down them as well.
		 */
		if (!strcmp(node->name, "multifunc-device")) {
			for_each_child_of_node(node, node2) {
				if (__of_pci_pci_compare(node2, devfn)) {
					of_node_put(node);
					return node2;
				}
			}
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(of_pci_find_child_device);

/**
 * of_pci_get_devfn() - Get device and function numbers for a device node
 * @np: device node
 *
 * Parses a standard 5-cell PCI resource and returns an 8-bit value that can
 * be passed to the PCI_SLOT() and PCI_FUNC() macros to extract the device
 * and function numbers respectively. On error a negative error code is
 * returned.
 */
int of_pci_get_devfn(struct device_node *np)
{
	unsigned int size;
	const __be32 *reg;

	reg = of_get_property(np, "reg", &size);

	if (!reg || size < 5 * sizeof(__be32))
		return -EINVAL;

	return (be32_to_cpup(reg) >> 8) & 0xff;
}
EXPORT_SYMBOL_GPL(of_pci_get_devfn);

/**
 * of_pci_parse_bus_range() - parse the bus-range property of a PCI device
 * @node: device node
 * @res: address to a struct resource to return the bus-range
 *
 * Returns 0 on success or a negative error-code on failure.
 */
int of_pci_parse_bus_range(struct device_node *node, struct resource *res)
{
	const __be32 *values;
	int len;

	values = of_get_property(node, "bus-range", &len);
	if (!values || len < sizeof(*values) * 2)
		return -EINVAL;

	res->name = node->name;
	res->start = be32_to_cpup(values++);
	res->end = be32_to_cpup(values);
	res->flags = IORESOURCE_BUS;

	return 0;
}
EXPORT_SYMBOL_GPL(of_pci_parse_bus_range);

/**
 * This function will try to obtain the host bridge domain number by
 * finding a property called "linux,pci-domain" of the given device node.
 *
 * @node: device tree node with the domain information
 *
 * Returns the associated domain number from DT in the range [0-0xffff], or
 * a negative value if the required property is not found.
 */
int of_get_pci_domain_nr(struct device_node *node)
{
	const __be32 *value;
	int len;
	u16 domain;

	value = of_get_property(node, "linux,pci-domain", &len);
	if (!value || len < sizeof(*value))
		return -EINVAL;

	domain = (u16)be32_to_cpup(value);

	return domain;
}
EXPORT_SYMBOL_GPL(of_get_pci_domain_nr);

/**
 * This function will try to find the limitation of link speed by finding
 * a property called "max-link-speed" of the given device node.
 *
 * @node: device tree node with the max link speed information
 *
 * Returns the associated max link speed from DT, or a negative value if the
 * required property is not found or is invalid.
 */
int of_pci_get_max_link_speed(struct device_node *node)
{
	u32 max_link_speed;

	if (of_property_read_u32(node, "max-link-speed", &max_link_speed) ||
	    max_link_speed > 4)
		return -EINVAL;

	return max_link_speed;
}
EXPORT_SYMBOL_GPL(of_pci_get_max_link_speed);

/**
 * of_pci_check_probe_only - Setup probe only mode if linux,pci-probe-only
 *                           is present and valid
 */
void of_pci_check_probe_only(void)
{
	u32 val;
	int ret;

	ret = of_property_read_u32(of_chosen, "linux,pci-probe-only", &val);
	if (ret) {
		if (ret == -ENODATA || ret == -EOVERFLOW)
			pr_warn("linux,pci-probe-only without valid value, ignoring\n");
		return;
	}

	if (val)
		pci_add_flags(PCI_PROBE_ONLY);
	else
		pci_clear_flags(PCI_PROBE_ONLY);

	pr_info("PROBE_ONLY %sabled\n", val ? "en" : "dis");
}
EXPORT_SYMBOL_GPL(of_pci_check_probe_only);

#if defined(CONFIG_OF_ADDRESS)
/**
 * of_pci_get_host_bridge_resources - Parse PCI host bridge resources from DT
 * @dev: device node of the host bridge having the range property
 * @busno: bus number associated with the bridge root bus
 * @bus_max: maximum number of buses for this bridge
 * @resources: list where the range of resources will be added after DT parsing
 * @io_base: pointer to a variable that will contain on return the physical
 * address for the start of the I/O range. Can be NULL if the caller doesn't
 * expect IO ranges to be present in the device tree.
 *
 * It is the caller's job to free the @resources list.
 *
 * This function will parse the "ranges" property of a PCI host bridge device
 * node and setup the resource mapping based on its content. It is expected
 * that the property conforms with the Power ePAPR document.
 *
 * It returns zero if the range parsing has been successful or a standard error
 * value if it failed.
 */
int of_pci_get_host_bridge_resources(struct device_node *dev,
			unsigned char busno, unsigned char bus_max,
			struct list_head *resources, resource_size_t *io_base)
{
	struct resource_entry *window;
	struct resource *res;
	struct resource *bus_range;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	char range_type[4];
	int err;

	if (io_base)
		*io_base = (resource_size_t)OF_BAD_ADDR;

	bus_range = kzalloc(sizeof(*bus_range), GFP_KERNEL);
	if (!bus_range)
		return -ENOMEM;

	pr_info("host bridge %pOF ranges:\n", dev);

	err = of_pci_parse_bus_range(dev, bus_range);
	if (err) {
		bus_range->start = busno;
		bus_range->end = bus_max;
		bus_range->flags = IORESOURCE_BUS;
		pr_info("  No bus range found for %pOF, using %pR\n",
			dev, bus_range);
	} else {
		if (bus_range->end > bus_range->start + bus_max)
			bus_range->end = bus_range->start + bus_max;
	}
	pci_add_resource(resources, bus_range);

	/* Check for ranges property */
	err = of_pci_range_parser_init(&parser, dev);
	if (err)
		goto parse_failed;

	pr_debug("Parsing ranges property...\n");
	for_each_of_pci_range(&parser, &range) {
		/* Read next ranges element */
		if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_IO)
			snprintf(range_type, 4, " IO");
		else if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_MEM)
			snprintf(range_type, 4, "MEM");
		else
			snprintf(range_type, 4, "err");
		pr_info("  %s %#010llx..%#010llx -> %#010llx\n", range_type,
			range.cpu_addr, range.cpu_addr + range.size - 1,
			range.pci_addr);

		/*
		 * If we failed translation or got a zero-sized region
		 * then skip this range
		 */
		if (range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		res = kzalloc(sizeof(struct resource), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto parse_failed;
		}

		err = of_pci_range_to_resource(&range, dev, res);
		if (err) {
			kfree(res);
			continue;
		}

		if (resource_type(res) == IORESOURCE_IO) {
			if (!io_base) {
				pr_err("I/O range found for %pOF. Please provide an io_base pointer to save CPU base address\n",
					dev);
				err = -EINVAL;
				goto conversion_failed;
			}
			if (*io_base != (resource_size_t)OF_BAD_ADDR)
				pr_warn("More than one I/O resource converted for %pOF. CPU base address for old range lost!\n",
					dev);
			*io_base = range.cpu_addr;
		}

		pci_add_resource_offset(resources, res,	res->start - range.pci_addr);
	}

	return 0;

conversion_failed:
	kfree(res);
parse_failed:
	resource_list_for_each_entry(window, resources)
		kfree(window->res);
	pci_free_resource_list(resources);
	return err;
}
EXPORT_SYMBOL_GPL(of_pci_get_host_bridge_resources);
#endif /* CONFIG_OF_ADDRESS */

/**
 * of_pci_map_rid - Translate a requester ID through a downstream mapping.
 * @np: root complex device node.
 * @rid: PCI requester ID to map.
 * @map_name: property name of the map to use.
 * @map_mask_name: optional property name of the mask to use.
 * @target: optional pointer to a target device node.
 * @id_out: optional pointer to receive the translated ID.
 *
 * Given a PCI requester ID, look up the appropriate implementation-defined
 * platform ID and/or the target device which receives transactions on that
 * ID, as per the "iommu-map" and "msi-map" bindings. Either of @target or
 * @id_out may be NULL if only the other is required. If @target points to
 * a non-NULL device node pointer, only entries targeting that node will be
 * matched; if it points to a NULL value, it will receive the device node of
 * the first matching target phandle, with a reference held.
 *
 * Return: 0 on success or a standard error code on failure.
 */
int of_pci_map_rid(struct device_node *np, u32 rid,
		   const char *map_name, const char *map_mask_name,
		   struct device_node **target, u32 *id_out)
{
	u32 map_mask, masked_rid;
	int map_len;
	const __be32 *map = NULL;

	if (!np || !map_name || (!target && !id_out))
		return -EINVAL;

	map = of_get_property(np, map_name, &map_len);
	if (!map) {
		if (target)
			return -ENODEV;
		/* Otherwise, no map implies no translation */
		*id_out = rid;
		return 0;
	}

	if (!map_len || map_len % (4 * sizeof(*map))) {
		pr_err("%pOF: Error: Bad %s length: %d\n", np,
			map_name, map_len);
		return -EINVAL;
	}

	/* The default is to select all bits. */
	map_mask = 0xffffffff;

	/*
	 * Can be overridden by "{iommu,msi}-map-mask" property.
	 * If of_property_read_u32() fails, the default is used.
	 */
	if (map_mask_name)
		of_property_read_u32(np, map_mask_name, &map_mask);

	masked_rid = map_mask & rid;
	for ( ; map_len > 0; map_len -= 4 * sizeof(*map), map += 4) {
		struct device_node *phandle_node;
		u32 rid_base = be32_to_cpup(map + 0);
		u32 phandle = be32_to_cpup(map + 1);
		u32 out_base = be32_to_cpup(map + 2);
		u32 rid_len = be32_to_cpup(map + 3);

		if (rid_base & ~map_mask) {
			pr_err("%pOF: Invalid %s translation - %s-mask (0x%x) ignores rid-base (0x%x)\n",
				np, map_name, map_name,
				map_mask, rid_base);
			return -EFAULT;
		}

		if (masked_rid < rid_base || masked_rid >= rid_base + rid_len)
			continue;

		phandle_node = of_find_node_by_phandle(phandle);
		if (!phandle_node)
			return -ENODEV;

		if (target) {
			if (*target)
				of_node_put(phandle_node);
			else
				*target = phandle_node;

			if (*target != phandle_node)
				continue;
		}

		if (id_out)
			*id_out = masked_rid - rid_base + out_base;

		pr_debug("%pOF: %s, using mask %08x, rid-base: %08x, out-base: %08x, length: %08x, rid: %08x -> %08x\n",
			np, map_name, map_mask, rid_base, out_base,
			rid_len, rid, *id_out);
		return 0;
	}

	pr_err("%pOF: Invalid %s translation - no match for rid 0x%x on %pOF\n",
		np, map_name, rid, target && *target ? *target : NULL);
	return -EFAULT;
}
