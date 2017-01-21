/**
 * APM X-Gene PCIe Driver
 *
 * Copyright (c) 2014 Applied Micro Circuits Corporation.
 *
 * Author: Tanmay Inamdar <tinamdar@apm.com>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define PCIECORE_CTLANDSTATUS		0x50
#define PIM1_1L				0x80
#define IBAR2				0x98
#define IR2MSK				0x9c
#define PIM2_1L				0xa0
#define IBAR3L				0xb4
#define IR3MSKL				0xbc
#define PIM3_1L				0xc4
#define OMR1BARL			0x100
#define OMR2BARL			0x118
#define OMR3BARL			0x130
#define CFGBARL				0x154
#define CFGBARH				0x158
#define CFGCTL				0x15c
#define RTDID				0x160
#define BRIDGE_CFG_0			0x2000
#define BRIDGE_CFG_4			0x2010
#define BRIDGE_STATUS_0			0x2600

#define LINK_UP_MASK			0x00000100
#define AXI_EP_CFG_ACCESS		0x10000
#define EN_COHERENCY			0xF0000000
#define EN_REG				0x00000001
#define OB_LO_IO			0x00000002
#define XGENE_PCIE_VENDORID		0x10E8
#define XGENE_PCIE_DEVICEID		0xE004
#define SZ_1T				(SZ_1G*1024ULL)
#define PIPE_PHY_RATE_RD(src)		((0xc000 & (u32)(src)) >> 0xe)

#define ROOT_CAP_AND_CTRL		0x5C

/* PCIe IP version */
#define XGENE_PCIE_IP_VER_UNKN		0
#define XGENE_PCIE_IP_VER_1		1
#define XGENE_PCIE_IP_VER_2		2

#if defined(CONFIG_PCI_XGENE) || (defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS))
struct xgene_pcie_port {
	struct device_node	*node;
	struct device		*dev;
	struct clk		*clk;
	void __iomem		*csr_base;
	void __iomem		*cfg_base;
	unsigned long		cfg_addr;
	bool			link_up;
	u32			version;
};

static u32 xgene_pcie_readl(struct xgene_pcie_port *port, u32 reg)
{
	return readl(port->csr_base + reg);
}

static void xgene_pcie_writel(struct xgene_pcie_port *port, u32 reg, u32 val)
{
	writel(val, port->csr_base + reg);
}

static inline u32 pcie_bar_low_val(u32 addr, u32 flags)
{
	return (addr & PCI_BASE_ADDRESS_MEM_MASK) | flags;
}

static inline struct xgene_pcie_port *pcie_bus_to_port(struct pci_bus *bus)
{
	struct pci_config_window *cfg;

	if (acpi_disabled)
		return (struct xgene_pcie_port *)(bus->sysdata);

	cfg = bus->sysdata;
	return (struct xgene_pcie_port *)(cfg->priv);
}

/*
 * When the address bit [17:16] is 2'b01, the Configuration access will be
 * treated as Type 1 and it will be forwarded to external PCIe device.
 */
static void __iomem *xgene_pcie_get_cfg_base(struct pci_bus *bus)
{
	struct xgene_pcie_port *port = pcie_bus_to_port(bus);

	if (bus->number >= (bus->primary + 1))
		return port->cfg_base + AXI_EP_CFG_ACCESS;

	return port->cfg_base;
}

/*
 * For Configuration request, RTDID register is used as Bus Number,
 * Device Number and Function number of the header fields.
 */
static void xgene_pcie_set_rtdid_reg(struct pci_bus *bus, uint devfn)
{
	struct xgene_pcie_port *port = pcie_bus_to_port(bus);
	unsigned int b, d, f;
	u32 rtdid_val = 0;

	b = bus->number;
	d = PCI_SLOT(devfn);
	f = PCI_FUNC(devfn);

	if (!pci_is_root_bus(bus))
		rtdid_val = (b << 8) | (d << 3) | f;

	xgene_pcie_writel(port, RTDID, rtdid_val);
	/* read the register back to ensure flush */
	xgene_pcie_readl(port, RTDID);
}

/*
 * X-Gene PCIe port uses BAR0-BAR1 of RC's configuration space as
 * the translation from PCI bus to native BUS.  Entire DDR region
 * is mapped into PCIe space using these registers, so it can be
 * reached by DMA from EP devices.  The BAR0/1 of bridge should be
 * hidden during enumeration to avoid the sizing and resource allocation
 * by PCIe core.
 */
static bool xgene_pcie_hide_rc_bars(struct pci_bus *bus, int offset)
{
	if (pci_is_root_bus(bus) && ((offset == PCI_BASE_ADDRESS_0) ||
				     (offset == PCI_BASE_ADDRESS_1)))
		return true;

	return false;
}

static void __iomem *xgene_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
			      int offset)
{
	if ((pci_is_root_bus(bus) && devfn != 0) ||
	    xgene_pcie_hide_rc_bars(bus, offset))
		return NULL;

	xgene_pcie_set_rtdid_reg(bus, devfn);
	return xgene_pcie_get_cfg_base(bus) + offset;
}

static int xgene_pcie_config_read32(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 *val)
{
	struct xgene_pcie_port *port = pcie_bus_to_port(bus);

	if (pci_generic_config_read32(bus, devfn, where & ~0x3, 4, val) !=
	    PCIBIOS_SUCCESSFUL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * The v1 controller has a bug in its Configuration Request
	 * Retry Status (CRS) logic: when CRS is enabled and we read the
	 * Vendor and Device ID of a non-existent device, the controller
	 * fabricates return data of 0xFFFF0001 ("device exists but is not
	 * ready") instead of 0xFFFFFFFF ("device does not exist").  This
	 * causes the PCI core to retry the read until it times out.
	 * Avoid this by not claiming to support CRS.
	 */
	if (pci_is_root_bus(bus) && (port->version == XGENE_PCIE_IP_VER_1) &&
	    ((where & ~0x3) == ROOT_CAP_AND_CTRL))
		*val &= ~(PCI_EXP_RTCAP_CRSVIS << 16);

	if (size <= 2)
		*val = (*val >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);

	return PCIBIOS_SUCCESSFUL;
}
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
static int xgene_get_csr_resource(struct acpi_device *adev,
				  struct resource *res)
{
	struct device *dev = &adev->dev;
	struct resource_entry *entry;
	struct list_head list;
	unsigned long flags;
	int ret;

	INIT_LIST_HEAD(&list);
	flags = IORESOURCE_MEM;
	ret = acpi_dev_get_resources(adev, &list,
				     acpi_dev_filter_resource_type_cb,
				     (void *) flags);
	if (ret < 0) {
		dev_err(dev, "failed to parse _CRS method, error code %d\n",
			ret);
		return ret;
	}

	if (ret == 0) {
		dev_err(dev, "no IO and memory resources present in _CRS\n");
		return -EINVAL;
	}

	entry = list_first_entry(&list, struct resource_entry, node);
	*res = *entry->res;
	acpi_dev_free_resource_list(&list);
	return 0;
}

static int xgene_pcie_ecam_init(struct pci_config_window *cfg, u32 ipversion)
{
	struct device *dev = cfg->parent;
	struct acpi_device *adev = to_acpi_device(dev);
	struct xgene_pcie_port *port;
	struct resource csr;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	ret = xgene_get_csr_resource(adev, &csr);
	if (ret) {
		dev_err(dev, "can't get CSR resource\n");
		return ret;
	}
	port->csr_base = devm_ioremap_resource(dev, &csr);
	if (IS_ERR(port->csr_base))
		return PTR_ERR(port->csr_base);

	port->cfg_base = cfg->win;
	port->version = ipversion;

	cfg->priv = port;
	return 0;
}

static int xgene_v1_pcie_ecam_init(struct pci_config_window *cfg)
{
	return xgene_pcie_ecam_init(cfg, XGENE_PCIE_IP_VER_1);
}

struct pci_ecam_ops xgene_v1_pcie_ecam_ops = {
	.bus_shift      = 16,
	.init           = xgene_v1_pcie_ecam_init,
	.pci_ops        = {
		.map_bus        = xgene_pcie_map_bus,
		.read           = xgene_pcie_config_read32,
		.write          = pci_generic_config_write,
	}
};

static int xgene_v2_pcie_ecam_init(struct pci_config_window *cfg)
{
	return xgene_pcie_ecam_init(cfg, XGENE_PCIE_IP_VER_2);
}

struct pci_ecam_ops xgene_v2_pcie_ecam_ops = {
	.bus_shift      = 16,
	.init           = xgene_v2_pcie_ecam_init,
	.pci_ops        = {
		.map_bus        = xgene_pcie_map_bus,
		.read           = xgene_pcie_config_read32,
		.write          = pci_generic_config_write,
	}
};
#endif

#if defined(CONFIG_PCI_XGENE)
static u64 xgene_pcie_set_ib_mask(struct xgene_pcie_port *port, u32 addr,
				  u32 flags, u64 size)
{
	u64 mask = (~(size - 1) & PCI_BASE_ADDRESS_MEM_MASK) | flags;
	u32 val32 = 0;
	u32 val;

	val32 = xgene_pcie_readl(port, addr);
	val = (val32 & 0x0000ffff) | (lower_32_bits(mask) << 16);
	xgene_pcie_writel(port, addr, val);

	val32 = xgene_pcie_readl(port, addr + 0x04);
	val = (val32 & 0xffff0000) | (lower_32_bits(mask) >> 16);
	xgene_pcie_writel(port, addr + 0x04, val);

	val32 = xgene_pcie_readl(port, addr + 0x04);
	val = (val32 & 0x0000ffff) | (upper_32_bits(mask) << 16);
	xgene_pcie_writel(port, addr + 0x04, val);

	val32 = xgene_pcie_readl(port, addr + 0x08);
	val = (val32 & 0xffff0000) | (upper_32_bits(mask) >> 16);
	xgene_pcie_writel(port, addr + 0x08, val);

	return mask;
}

static void xgene_pcie_linkup(struct xgene_pcie_port *port,
				   u32 *lanes, u32 *speed)
{
	u32 val32;

	port->link_up = false;
	val32 = xgene_pcie_readl(port, PCIECORE_CTLANDSTATUS);
	if (val32 & LINK_UP_MASK) {
		port->link_up = true;
		*speed = PIPE_PHY_RATE_RD(val32);
		val32 = xgene_pcie_readl(port, BRIDGE_STATUS_0);
		*lanes = val32 >> 26;
	}
}

static int xgene_pcie_init_port(struct xgene_pcie_port *port)
{
	struct device *dev = port->dev;
	int rc;

	port->clk = clk_get(dev, NULL);
	if (IS_ERR(port->clk)) {
		dev_err(dev, "clock not available\n");
		return -ENODEV;
	}

	rc = clk_prepare_enable(port->clk);
	if (rc) {
		dev_err(dev, "clock enable failed\n");
		return rc;
	}

	return 0;
}

static int xgene_pcie_map_reg(struct xgene_pcie_port *port,
			      struct platform_device *pdev)
{
	struct device *dev = port->dev;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csr");
	port->csr_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(port->csr_base))
		return PTR_ERR(port->csr_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	port->cfg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(port->cfg_base))
		return PTR_ERR(port->cfg_base);
	port->cfg_addr = res->start;

	return 0;
}

static void xgene_pcie_setup_ob_reg(struct xgene_pcie_port *port,
				    struct resource *res, u32 offset,
				    u64 cpu_addr, u64 pci_addr)
{
	struct device *dev = port->dev;
	resource_size_t size = resource_size(res);
	u64 restype = resource_type(res);
	u64 mask = 0;
	u32 min_size;
	u32 flag = EN_REG;

	if (restype == IORESOURCE_MEM) {
		min_size = SZ_128M;
	} else {
		min_size = 128;
		flag |= OB_LO_IO;
	}

	if (size >= min_size)
		mask = ~(size - 1) | flag;
	else
		dev_warn(dev, "res size 0x%llx less than minimum 0x%x\n",
			 (u64)size, min_size);

	xgene_pcie_writel(port, offset, lower_32_bits(cpu_addr));
	xgene_pcie_writel(port, offset + 0x04, upper_32_bits(cpu_addr));
	xgene_pcie_writel(port, offset + 0x08, lower_32_bits(mask));
	xgene_pcie_writel(port, offset + 0x0c, upper_32_bits(mask));
	xgene_pcie_writel(port, offset + 0x10, lower_32_bits(pci_addr));
	xgene_pcie_writel(port, offset + 0x14, upper_32_bits(pci_addr));
}

static void xgene_pcie_setup_cfg_reg(struct xgene_pcie_port *port)
{
	u64 addr = port->cfg_addr;

	xgene_pcie_writel(port, CFGBARL, lower_32_bits(addr));
	xgene_pcie_writel(port, CFGBARH, upper_32_bits(addr));
	xgene_pcie_writel(port, CFGCTL, EN_REG);
}

static int xgene_pcie_map_ranges(struct xgene_pcie_port *port,
				 struct list_head *res,
				 resource_size_t io_base)
{
	struct resource_entry *window;
	struct device *dev = port->dev;
	int ret;

	resource_list_for_each_entry(window, res) {
		struct resource *res = window->res;
		u64 restype = resource_type(res);

		dev_dbg(dev, "%pR\n", res);

		switch (restype) {
		case IORESOURCE_IO:
			xgene_pcie_setup_ob_reg(port, res, OMR3BARL, io_base,
						res->start - window->offset);
			ret = pci_remap_iospace(res, io_base);
			if (ret < 0)
				return ret;
			break;
		case IORESOURCE_MEM:
			if (res->flags & IORESOURCE_PREFETCH)
				xgene_pcie_setup_ob_reg(port, res, OMR2BARL,
							res->start,
							res->start -
							window->offset);
			else
				xgene_pcie_setup_ob_reg(port, res, OMR1BARL,
							res->start,
							res->start -
							window->offset);
			break;
		case IORESOURCE_BUS:
			break;
		default:
			dev_err(dev, "invalid resource %pR\n", res);
			return -EINVAL;
		}
	}
	xgene_pcie_setup_cfg_reg(port);
	return 0;
}

static void xgene_pcie_setup_pims(struct xgene_pcie_port *port, u32 pim_reg,
				  u64 pim, u64 size)
{
	xgene_pcie_writel(port, pim_reg, lower_32_bits(pim));
	xgene_pcie_writel(port, pim_reg + 0x04,
			  upper_32_bits(pim) | EN_COHERENCY);
	xgene_pcie_writel(port, pim_reg + 0x10, lower_32_bits(size));
	xgene_pcie_writel(port, pim_reg + 0x14, upper_32_bits(size));
}

/*
 * X-Gene PCIe support maximum 3 inbound memory regions
 * This function helps to select a region based on size of region
 */
static int xgene_pcie_select_ib_reg(u8 *ib_reg_mask, u64 size)
{
	if ((size > 4) && (size < SZ_16M) && !(*ib_reg_mask & (1 << 1))) {
		*ib_reg_mask |= (1 << 1);
		return 1;
	}

	if ((size > SZ_1K) && (size < SZ_1T) && !(*ib_reg_mask & (1 << 0))) {
		*ib_reg_mask |= (1 << 0);
		return 0;
	}

	if ((size > SZ_1M) && (size < SZ_1T) && !(*ib_reg_mask & (1 << 2))) {
		*ib_reg_mask |= (1 << 2);
		return 2;
	}

	return -EINVAL;
}

static void xgene_pcie_setup_ib_reg(struct xgene_pcie_port *port,
				    struct of_pci_range *range, u8 *ib_reg_mask)
{
	void __iomem *cfg_base = port->cfg_base;
	struct device *dev = port->dev;
	void *bar_addr;
	u32 pim_reg;
	u64 cpu_addr = range->cpu_addr;
	u64 pci_addr = range->pci_addr;
	u64 size = range->size;
	u64 mask = ~(size - 1) | EN_REG;
	u32 flags = PCI_BASE_ADDRESS_MEM_TYPE_64;
	u32 bar_low;
	int region;

	region = xgene_pcie_select_ib_reg(ib_reg_mask, range->size);
	if (region < 0) {
		dev_warn(dev, "invalid pcie dma-range config\n");
		return;
	}

	if (range->flags & IORESOURCE_PREFETCH)
		flags |= PCI_BASE_ADDRESS_MEM_PREFETCH;

	bar_low = pcie_bar_low_val((u32)cpu_addr, flags);
	switch (region) {
	case 0:
		xgene_pcie_set_ib_mask(port, BRIDGE_CFG_4, flags, size);
		bar_addr = cfg_base + PCI_BASE_ADDRESS_0;
		writel(bar_low, bar_addr);
		writel(upper_32_bits(cpu_addr), bar_addr + 0x4);
		pim_reg = PIM1_1L;
		break;
	case 1:
		xgene_pcie_writel(port, IBAR2, bar_low);
		xgene_pcie_writel(port, IR2MSK, lower_32_bits(mask));
		pim_reg = PIM2_1L;
		break;
	case 2:
		xgene_pcie_writel(port, IBAR3L, bar_low);
		xgene_pcie_writel(port, IBAR3L + 0x4, upper_32_bits(cpu_addr));
		xgene_pcie_writel(port, IR3MSKL, lower_32_bits(mask));
		xgene_pcie_writel(port, IR3MSKL + 0x4, upper_32_bits(mask));
		pim_reg = PIM3_1L;
		break;
	}

	xgene_pcie_setup_pims(port, pim_reg, pci_addr, ~(size - 1));
}

static int pci_dma_range_parser_init(struct of_pci_range_parser *parser,
				     struct device_node *node)
{
	const int na = 3, ns = 2;
	int rlen;

	parser->node = node;
	parser->pna = of_n_addr_cells(node);
	parser->np = parser->pna + na + ns;

	parser->range = of_get_property(node, "dma-ranges", &rlen);
	if (!parser->range)
		return -ENOENT;
	parser->end = parser->range + rlen / sizeof(__be32);

	return 0;
}

static int xgene_pcie_parse_map_dma_ranges(struct xgene_pcie_port *port)
{
	struct device_node *np = port->node;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	struct device *dev = port->dev;
	u8 ib_reg_mask = 0;

	if (pci_dma_range_parser_init(&parser, np)) {
		dev_err(dev, "missing dma-ranges property\n");
		return -EINVAL;
	}

	/* Get the dma-ranges from DT */
	for_each_of_pci_range(&parser, &range) {
		u64 end = range.cpu_addr + range.size - 1;

		dev_dbg(dev, "0x%08x 0x%016llx..0x%016llx -> 0x%016llx\n",
			range.flags, range.cpu_addr, end, range.pci_addr);
		xgene_pcie_setup_ib_reg(port, &range, &ib_reg_mask);
	}
	return 0;
}

/* clear BAR configuration which was done by firmware */
static void xgene_pcie_clear_config(struct xgene_pcie_port *port)
{
	int i;

	for (i = PIM1_1L; i <= CFGCTL; i += 4)
		xgene_pcie_writel(port, i, 0);
}

static int xgene_pcie_setup(struct xgene_pcie_port *port,
			    struct list_head *res,
			    resource_size_t io_base)
{
	struct device *dev = port->dev;
	u32 val, lanes = 0, speed = 0;
	int ret;

	xgene_pcie_clear_config(port);

	/* setup the vendor and device IDs correctly */
	val = (XGENE_PCIE_DEVICEID << 16) | XGENE_PCIE_VENDORID;
	xgene_pcie_writel(port, BRIDGE_CFG_0, val);

	ret = xgene_pcie_map_ranges(port, res, io_base);
	if (ret)
		return ret;

	ret = xgene_pcie_parse_map_dma_ranges(port);
	if (ret)
		return ret;

	xgene_pcie_linkup(port, &lanes, &speed);
	if (!port->link_up)
		dev_info(dev, "(rc) link down\n");
	else
		dev_info(dev, "(rc) x%d gen-%d link up\n", lanes, speed + 1);
	return 0;
}

static struct pci_ops xgene_pcie_ops = {
	.map_bus = xgene_pcie_map_bus,
	.read = xgene_pcie_config_read32,
	.write = pci_generic_config_write32,
};

static int xgene_pcie_probe_bridge(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	struct xgene_pcie_port *port;
	resource_size_t iobase = 0;
	struct pci_bus *bus;
	int ret;
	LIST_HEAD(res);

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->node = of_node_get(dn);
	port->dev = dev;

	port->version = XGENE_PCIE_IP_VER_UNKN;
	if (of_device_is_compatible(port->node, "apm,xgene-pcie"))
		port->version = XGENE_PCIE_IP_VER_1;

	ret = xgene_pcie_map_reg(port, pdev);
	if (ret)
		return ret;

	ret = xgene_pcie_init_port(port);
	if (ret)
		return ret;

	ret = of_pci_get_host_bridge_resources(dn, 0, 0xff, &res, &iobase);
	if (ret)
		return ret;

	ret = devm_request_pci_bus_resources(dev, &res);
	if (ret)
		goto error;

	ret = xgene_pcie_setup(port, &res, iobase);
	if (ret)
		goto error;

	bus = pci_create_root_bus(dev, 0, &xgene_pcie_ops, port, &res);
	if (!bus) {
		ret = -ENOMEM;
		goto error;
	}

	pci_scan_child_bus(bus);
	pci_assign_unassigned_bus_resources(bus);
	pci_bus_add_devices(bus);
	return 0;

error:
	pci_free_resource_list(&res);
	return ret;
}

static const struct of_device_id xgene_pcie_match_table[] = {
	{.compatible = "apm,xgene-pcie",},
	{},
};

static struct platform_driver xgene_pcie_driver = {
	.driver = {
		   .name = "xgene-pcie",
		   .of_match_table = of_match_ptr(xgene_pcie_match_table),
	},
	.probe = xgene_pcie_probe_bridge,
};
builtin_platform_driver(xgene_pcie_driver);
#endif
