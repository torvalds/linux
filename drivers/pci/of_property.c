// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include "pci.h"

#define OF_PCI_ADDRESS_CELLS		3
#define OF_PCI_SIZE_CELLS		2
#define OF_PCI_MAX_INT_PIN		4

struct of_pci_addr_pair {
	u32		phys_addr[OF_PCI_ADDRESS_CELLS];
	u32		size[OF_PCI_SIZE_CELLS];
};

/*
 * Each entry in the ranges table is a tuple containing the child address,
 * the parent address, and the size of the region in the child address space.
 * Thus, for PCI, in each entry parent address is an address on the primary
 * side and the child address is the corresponding address on the secondary
 * side.
 */
struct of_pci_range_entry {
	u32		child_addr[OF_PCI_ADDRESS_CELLS];
	u32		parent_addr[OF_PCI_ADDRESS_CELLS];
	u32		size[OF_PCI_SIZE_CELLS];
};

#define OF_PCI_ADDR_SPACE_IO		0x1
#define OF_PCI_ADDR_SPACE_MEM32		0x2
#define OF_PCI_ADDR_SPACE_MEM64		0x3

#define OF_PCI_ADDR_FIELD_NONRELOC	BIT(31)
#define OF_PCI_ADDR_FIELD_SS		GENMASK(25, 24)
#define OF_PCI_ADDR_FIELD_PREFETCH	BIT(30)
#define OF_PCI_ADDR_FIELD_BUS		GENMASK(23, 16)
#define OF_PCI_ADDR_FIELD_DEV		GENMASK(15, 11)
#define OF_PCI_ADDR_FIELD_FUNC		GENMASK(10, 8)
#define OF_PCI_ADDR_FIELD_REG		GENMASK(7, 0)

enum of_pci_prop_compatible {
	PROP_COMPAT_PCI_VVVV_DDDD,
	PROP_COMPAT_PCICLASS_CCSSPP,
	PROP_COMPAT_PCICLASS_CCSS,
	PROP_COMPAT_NUM,
};

static void of_pci_set_address(struct pci_dev *pdev, u32 *prop, u64 addr,
			       u32 reg_num, u32 flags, bool reloc)
{
	if (pdev) {
		prop[0] = FIELD_PREP(OF_PCI_ADDR_FIELD_BUS, pdev->bus->number) |
			  FIELD_PREP(OF_PCI_ADDR_FIELD_DEV, PCI_SLOT(pdev->devfn)) |
			  FIELD_PREP(OF_PCI_ADDR_FIELD_FUNC, PCI_FUNC(pdev->devfn));
	} else
		prop[0] = 0;

	prop[0] |= flags | reg_num;
	if (!reloc) {
		prop[0] |= OF_PCI_ADDR_FIELD_NONRELOC;
		prop[1] = upper_32_bits(addr);
		prop[2] = lower_32_bits(addr);
	}
}

static int of_pci_get_addr_flags(const struct resource *res, u32 *flags)
{
	u32 ss;

	if (res->flags & IORESOURCE_IO)
		ss = OF_PCI_ADDR_SPACE_IO;
	else if (res->flags & IORESOURCE_MEM_64)
		ss = OF_PCI_ADDR_SPACE_MEM64;
	else if (res->flags & IORESOURCE_MEM)
		ss = OF_PCI_ADDR_SPACE_MEM32;
	else
		return -EINVAL;

	*flags = 0;
	if (res->flags & IORESOURCE_PREFETCH)
		*flags |= OF_PCI_ADDR_FIELD_PREFETCH;

	*flags |= FIELD_PREP(OF_PCI_ADDR_FIELD_SS, ss);

	return 0;
}

static int of_pci_prop_bus_range(struct pci_dev *pdev,
				 struct of_changeset *ocs,
				 struct device_node *np)
{
	u32 bus_range[] = { pdev->subordinate->busn_res.start,
			    pdev->subordinate->busn_res.end };

	return of_changeset_add_prop_u32_array(ocs, np, "bus-range", bus_range,
					       ARRAY_SIZE(bus_range));
}

static int of_pci_prop_ranges(struct pci_dev *pdev, struct of_changeset *ocs,
			      struct device_node *np)
{
	struct of_pci_range_entry *rp;
	struct resource *res;
	int i, j, ret;
	u32 flags, num;
	u64 val64;

	if (pci_is_bridge(pdev)) {
		num = PCI_BRIDGE_RESOURCE_NUM;
		res = &pdev->resource[PCI_BRIDGE_RESOURCES];
	} else {
		num = PCI_STD_NUM_BARS;
		res = &pdev->resource[PCI_STD_RESOURCES];
	}

	rp = kcalloc(num, sizeof(*rp), GFP_KERNEL);
	if (!rp)
		return -ENOMEM;

	for (i = 0, j = 0; j < num; j++) {
		if (!resource_size(&res[j]))
			continue;

		if (of_pci_get_addr_flags(&res[j], &flags))
			continue;

		val64 = pci_bus_address(pdev, &res[j] - pdev->resource);
		of_pci_set_address(pdev, rp[i].parent_addr, val64, 0, flags,
				   false);
		if (pci_is_bridge(pdev)) {
			memcpy(rp[i].child_addr, rp[i].parent_addr,
			       sizeof(rp[i].child_addr));
		} else {
			/*
			 * For endpoint device, the lower 64-bits of child
			 * address is always zero.
			 */
			rp[i].child_addr[0] = j;
		}

		val64 = resource_size(&res[j]);
		rp[i].size[0] = upper_32_bits(val64);
		rp[i].size[1] = lower_32_bits(val64);

		i++;
	}

	ret = of_changeset_add_prop_u32_array(ocs, np, "ranges", (u32 *)rp,
					      i * sizeof(*rp) / sizeof(u32));
	kfree(rp);

	return ret;
}

static int of_pci_prop_reg(struct pci_dev *pdev, struct of_changeset *ocs,
			   struct device_node *np)
{
	struct of_pci_addr_pair reg = { 0 };

	/* configuration space */
	of_pci_set_address(pdev, reg.phys_addr, 0, 0, 0, true);

	return of_changeset_add_prop_u32_array(ocs, np, "reg", (u32 *)&reg,
					       sizeof(reg) / sizeof(u32));
}

static int of_pci_prop_interrupts(struct pci_dev *pdev,
				  struct of_changeset *ocs,
				  struct device_node *np)
{
	int ret;
	u8 pin;

	ret = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
	if (ret != 0)
		return ret;

	if (!pin)
		return 0;

	return of_changeset_add_prop_u32(ocs, np, "interrupts", (u32)pin);
}

static int of_pci_prop_intr_ctrl(struct pci_dev *pdev, struct of_changeset *ocs,
				 struct device_node *np)
{
	int ret;
	u8 pin;

	ret = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
	if (ret != 0)
		return ret;

	if (!pin)
		return 0;

	ret = of_changeset_add_prop_u32(ocs, np, "#interrupt-cells", 1);
	if (ret)
		return ret;

	return of_changeset_add_prop_bool(ocs, np, "interrupt-controller");
}

static int of_pci_prop_intr_map(struct pci_dev *pdev, struct of_changeset *ocs,
				struct device_node *np)
{
	u32 i, addr_sz[OF_PCI_MAX_INT_PIN] = { 0 }, map_sz = 0;
	struct of_phandle_args out_irq[OF_PCI_MAX_INT_PIN];
	__be32 laddr[OF_PCI_ADDRESS_CELLS] = { 0 };
	u32 int_map_mask[] = { 0xffff00, 0, 0, 7 };
	struct device_node *pnode;
	struct pci_dev *child;
	u32 *int_map, *mapp;
	int ret;
	u8 pin;

	pnode = pci_device_to_OF_node(pdev->bus->self);
	if (!pnode)
		pnode = pci_bus_to_OF_node(pdev->bus);

	if (!pnode) {
		pci_err(pdev, "failed to get parent device node");
		return -EINVAL;
	}

	laddr[0] = cpu_to_be32((pdev->bus->number << 16) | (pdev->devfn << 8));
	for (pin = 1; pin <= OF_PCI_MAX_INT_PIN;  pin++) {
		i = pin - 1;
		out_irq[i].np = pnode;
		out_irq[i].args_count = 1;
		out_irq[i].args[0] = pin;
		ret = of_irq_parse_raw(laddr, &out_irq[i]);
		if (ret) {
			out_irq[i].np = NULL;
			pci_dbg(pdev, "parse irq %d failed, ret %d", pin, ret);
			continue;
		}
		of_property_read_u32(out_irq[i].np, "#address-cells",
				     &addr_sz[i]);
	}

	list_for_each_entry(child, &pdev->subordinate->devices, bus_list) {
		for (pin = 1; pin <= OF_PCI_MAX_INT_PIN; pin++) {
			i = pci_swizzle_interrupt_pin(child, pin) - 1;
			if (!out_irq[i].np)
				continue;
			map_sz += 5 + addr_sz[i] + out_irq[i].args_count;
		}
	}

	/*
	 * Parsing interrupt failed for all pins. In this case, it does not
	 * need to generate interrupt-map property.
	 */
	if (!map_sz)
		return 0;

	int_map = kcalloc(map_sz, sizeof(u32), GFP_KERNEL);
	if (!int_map)
		return -ENOMEM;
	mapp = int_map;

	list_for_each_entry(child, &pdev->subordinate->devices, bus_list) {
		for (pin = 1; pin <= OF_PCI_MAX_INT_PIN; pin++) {
			i = pci_swizzle_interrupt_pin(child, pin) - 1;
			if (!out_irq[i].np)
				continue;

			*mapp = (child->bus->number << 16) |
				(child->devfn << 8);
			mapp += OF_PCI_ADDRESS_CELLS;
			*mapp = pin;
			mapp++;
			*mapp = out_irq[i].np->phandle;
			mapp++;
			if (addr_sz[i]) {
				ret = of_property_read_u32_array(out_irq[i].np,
								 "reg", mapp,
								 addr_sz[i]);
				if (ret)
					goto failed;
			}
			mapp += addr_sz[i];
			memcpy(mapp, out_irq[i].args,
			       out_irq[i].args_count * sizeof(u32));
			mapp += out_irq[i].args_count;
		}
	}

	ret = of_changeset_add_prop_u32_array(ocs, np, "interrupt-map", int_map,
					      map_sz);
	if (ret)
		goto failed;

	ret = of_changeset_add_prop_u32(ocs, np, "#interrupt-cells", 1);
	if (ret)
		goto failed;

	ret = of_changeset_add_prop_u32_array(ocs, np, "interrupt-map-mask",
					      int_map_mask,
					      ARRAY_SIZE(int_map_mask));
	if (ret)
		goto failed;

	kfree(int_map);
	return 0;

failed:
	kfree(int_map);
	return ret;
}

static int of_pci_prop_compatible(struct pci_dev *pdev,
				  struct of_changeset *ocs,
				  struct device_node *np)
{
	const char *compat_strs[PROP_COMPAT_NUM] = { 0 };
	int i, ret;

	compat_strs[PROP_COMPAT_PCI_VVVV_DDDD] =
		kasprintf(GFP_KERNEL, "pci%x,%x", pdev->vendor, pdev->device);
	compat_strs[PROP_COMPAT_PCICLASS_CCSSPP] =
		kasprintf(GFP_KERNEL, "pciclass,%06x", pdev->class);
	compat_strs[PROP_COMPAT_PCICLASS_CCSS] =
		kasprintf(GFP_KERNEL, "pciclass,%04x", pdev->class >> 8);

	ret = of_changeset_add_prop_string_array(ocs, np, "compatible",
						 compat_strs, PROP_COMPAT_NUM);
	for (i = 0; i < PROP_COMPAT_NUM; i++)
		kfree(compat_strs[i]);

	return ret;
}

int of_pci_add_properties(struct pci_dev *pdev, struct of_changeset *ocs,
			  struct device_node *np)
{
	int ret;

	/*
	 * The added properties will be released when the
	 * changeset is destroyed.
	 */
	if (pci_is_bridge(pdev)) {
		ret = of_changeset_add_prop_string(ocs, np, "device_type",
						   "pci");
		if (ret)
			return ret;

		ret = of_pci_prop_bus_range(pdev, ocs, np);
		if (ret)
			return ret;

		ret = of_pci_prop_intr_map(pdev, ocs, np);
		if (ret)
			return ret;
	} else {
		ret = of_pci_prop_intr_ctrl(pdev, ocs, np);
		if (ret)
			return ret;
	}

	ret = of_pci_prop_ranges(pdev, ocs, np);
	if (ret)
		return ret;

	ret = of_changeset_add_prop_u32(ocs, np, "#address-cells",
					OF_PCI_ADDRESS_CELLS);
	if (ret)
		return ret;

	ret = of_changeset_add_prop_u32(ocs, np, "#size-cells",
					OF_PCI_SIZE_CELLS);
	if (ret)
		return ret;

	ret = of_pci_prop_reg(pdev, ocs, np);
	if (ret)
		return ret;

	ret = of_pci_prop_compatible(pdev, ocs, np);
	if (ret)
		return ret;

	ret = of_pci_prop_interrupts(pdev, ocs, np);
	if (ret)
		return ret;

	return 0;
}

static bool of_pci_is_range_resource(const struct resource *res, u32 *flags)
{
	if (!(resource_type(res) & IORESOURCE_MEM) &&
	    !(resource_type(res) & IORESOURCE_MEM_64))
		return false;

	if (of_pci_get_addr_flags(res, flags))
		return false;

	return true;
}

static int of_pci_host_bridge_prop_ranges(struct pci_host_bridge *bridge,
					  struct of_changeset *ocs,
					  struct device_node *np)
{
	struct resource_entry *window;
	unsigned int ranges_sz = 0;
	unsigned int n_range = 0;
	struct resource *res;
	int n_addr_cells;
	u32 *ranges;
	u64 val64;
	u32 flags;
	int ret;

	n_addr_cells = of_n_addr_cells(np);
	if (n_addr_cells <= 0 || n_addr_cells > 2)
		return -EINVAL;

	resource_list_for_each_entry(window, &bridge->windows) {
		res = window->res;
		if (!of_pci_is_range_resource(res, &flags))
			continue;
		n_range++;
	}

	if (!n_range)
		return 0;

	ranges = kcalloc(n_range,
			 (OF_PCI_ADDRESS_CELLS + OF_PCI_SIZE_CELLS +
			  n_addr_cells) * sizeof(*ranges),
			 GFP_KERNEL);
	if (!ranges)
		return -ENOMEM;

	resource_list_for_each_entry(window, &bridge->windows) {
		res = window->res;
		if (!of_pci_is_range_resource(res, &flags))
			continue;

		/* PCI bus address */
		val64 = res->start;
		of_pci_set_address(NULL, &ranges[ranges_sz],
				   val64 - window->offset, 0, flags, false);
		ranges_sz += OF_PCI_ADDRESS_CELLS;

		/* Host bus address */
		if (n_addr_cells == 2)
			ranges[ranges_sz++] = upper_32_bits(val64);
		ranges[ranges_sz++] = lower_32_bits(val64);

		/* Size */
		val64 = resource_size(res);
		ranges[ranges_sz] = upper_32_bits(val64);
		ranges[ranges_sz + 1] = lower_32_bits(val64);
		ranges_sz += OF_PCI_SIZE_CELLS;
	}

	ret = of_changeset_add_prop_u32_array(ocs, np, "ranges", ranges,
					      ranges_sz);
	kfree(ranges);
	return ret;
}

int of_pci_add_host_bridge_properties(struct pci_host_bridge *bridge,
				      struct of_changeset *ocs,
				      struct device_node *np)
{
	int ret;

	ret = of_changeset_add_prop_string(ocs, np, "device_type", "pci");
	if (ret)
		return ret;

	ret = of_changeset_add_prop_u32(ocs, np, "#address-cells",
					OF_PCI_ADDRESS_CELLS);
	if (ret)
		return ret;

	ret = of_changeset_add_prop_u32(ocs, np, "#size-cells",
					OF_PCI_SIZE_CELLS);
	if (ret)
		return ret;

	ret = of_pci_host_bridge_prop_ranges(bridge, ocs, np);
	if (ret)
		return ret;

	return 0;
}
