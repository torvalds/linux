/*
 * Synopsys Designware PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "pcie-designware.h"

/* Synopsis specific PCIE configuration registers */
#define PCIE_PORT_LINK_CONTROL		0x710
#define PORT_LINK_MODE_MASK		(0x3f << 16)
#define PORT_LINK_MODE_1_LANES		(0x1 << 16)
#define PORT_LINK_MODE_2_LANES		(0x3 << 16)
#define PORT_LINK_MODE_4_LANES		(0x7 << 16)
#define PORT_LINK_MODE_8_LANES		(0xf << 16)

#define PCIE_LINK_WIDTH_SPEED_CONTROL	0x80C
#define PORT_LOGIC_SPEED_CHANGE		(0x1 << 17)
#define PORT_LOGIC_LINK_WIDTH_MASK	(0x1ff << 8)
#define PORT_LOGIC_LINK_WIDTH_1_LANES	(0x1 << 8)
#define PORT_LOGIC_LINK_WIDTH_2_LANES	(0x2 << 8)
#define PORT_LOGIC_LINK_WIDTH_4_LANES	(0x4 << 8)
#define PORT_LOGIC_LINK_WIDTH_8_LANES	(0x8 << 8)

#define PCIE_MSI_ADDR_LO		0x820
#define PCIE_MSI_ADDR_HI		0x824
#define PCIE_MSI_INTR0_ENABLE		0x828
#define PCIE_MSI_INTR0_MASK		0x82C
#define PCIE_MSI_INTR0_STATUS		0x830

#define PCIE_ATU_VIEWPORT		0x900
#define PCIE_ATU_REGION_INBOUND		(0x1 << 31)
#define PCIE_ATU_REGION_OUTBOUND	(0x0 << 31)
#define PCIE_ATU_REGION_INDEX1		(0x1 << 0)
#define PCIE_ATU_REGION_INDEX0		(0x0 << 0)
#define PCIE_ATU_CR1			0x904
#define PCIE_ATU_TYPE_MEM		(0x0 << 0)
#define PCIE_ATU_TYPE_IO		(0x2 << 0)
#define PCIE_ATU_TYPE_CFG0		(0x4 << 0)
#define PCIE_ATU_TYPE_CFG1		(0x5 << 0)
#define PCIE_ATU_CR2			0x908
#define PCIE_ATU_ENABLE			(0x1 << 31)
#define PCIE_ATU_BAR_MODE_ENABLE	(0x1 << 30)
#define PCIE_ATU_LOWER_BASE		0x90C
#define PCIE_ATU_UPPER_BASE		0x910
#define PCIE_ATU_LIMIT			0x914
#define PCIE_ATU_LOWER_TARGET		0x918
#define PCIE_ATU_BUS(x)			(((x) & 0xff) << 24)
#define PCIE_ATU_DEV(x)			(((x) & 0x1f) << 19)
#define PCIE_ATU_FUNC(x)		(((x) & 0x7) << 16)
#define PCIE_ATU_UPPER_TARGET		0x91C

static struct hw_pci dw_pci;

static unsigned long global_io_offset;

static inline struct pcie_port *sys_to_pcie(struct pci_sys_data *sys)
{
	BUG_ON(!sys->private_data);

	return sys->private_data;
}

int dw_pcie_cfg_read(void __iomem *addr, int where, int size, u32 *val)
{
	*val = readl(addr);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;
	else if (size != 4)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

int dw_pcie_cfg_write(void __iomem *addr, int where, int size, u32 val)
{
	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr + (where & 2));
	else if (size == 1)
		writeb(val, addr + (where & 3));
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static inline void dw_pcie_readl_rc(struct pcie_port *pp, u32 reg, u32 *val)
{
	if (pp->ops->readl_rc)
		pp->ops->readl_rc(pp, pp->dbi_base + reg, val);
	else
		*val = readl(pp->dbi_base + reg);
}

static inline void dw_pcie_writel_rc(struct pcie_port *pp, u32 val, u32 reg)
{
	if (pp->ops->writel_rc)
		pp->ops->writel_rc(pp, val, pp->dbi_base + reg);
	else
		writel(val, pp->dbi_base + reg);
}

static int dw_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
			       u32 *val)
{
	int ret;

	if (pp->ops->rd_own_conf)
		ret = pp->ops->rd_own_conf(pp, where, size, val);
	else
		ret = dw_pcie_cfg_read(pp->dbi_base + (where & ~0x3), where,
				size, val);

	return ret;
}

static int dw_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
			       u32 val)
{
	int ret;

	if (pp->ops->wr_own_conf)
		ret = pp->ops->wr_own_conf(pp, where, size, val);
	else
		ret = dw_pcie_cfg_write(pp->dbi_base + (where & ~0x3), where,
				size, val);

	return ret;
}

static void dw_pcie_prog_outbound_atu(struct pcie_port *pp, int index,
		int type, u64 cpu_addr, u64 pci_addr, u32 size)
{
	dw_pcie_writel_rc(pp, PCIE_ATU_REGION_OUTBOUND | index,
			  PCIE_ATU_VIEWPORT);
	dw_pcie_writel_rc(pp, lower_32_bits(cpu_addr), PCIE_ATU_LOWER_BASE);
	dw_pcie_writel_rc(pp, upper_32_bits(cpu_addr), PCIE_ATU_UPPER_BASE);
	dw_pcie_writel_rc(pp, lower_32_bits(cpu_addr + size - 1),
			  PCIE_ATU_LIMIT);
	dw_pcie_writel_rc(pp, lower_32_bits(pci_addr), PCIE_ATU_LOWER_TARGET);
	dw_pcie_writel_rc(pp, upper_32_bits(pci_addr), PCIE_ATU_UPPER_TARGET);
	dw_pcie_writel_rc(pp, type, PCIE_ATU_CR1);
	dw_pcie_writel_rc(pp, PCIE_ATU_ENABLE, PCIE_ATU_CR2);
}

static struct irq_chip dw_msi_irq_chip = {
	.name = "PCI-MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

/* MSI int handler */
irqreturn_t dw_handle_msi_irq(struct pcie_port *pp)
{
	unsigned long val;
	int i, pos, irq;
	irqreturn_t ret = IRQ_NONE;

	for (i = 0; i < MAX_MSI_CTRLS; i++) {
		dw_pcie_rd_own_conf(pp, PCIE_MSI_INTR0_STATUS + i * 12, 4,
				(u32 *)&val);
		if (val) {
			ret = IRQ_HANDLED;
			pos = 0;
			while ((pos = find_next_bit(&val, 32, pos)) != 32) {
				irq = irq_find_mapping(pp->irq_domain,
						i * 32 + pos);
				dw_pcie_wr_own_conf(pp,
						PCIE_MSI_INTR0_STATUS + i * 12,
						4, 1 << pos);
				generic_handle_irq(irq);
				pos++;
			}
		}
	}

	return ret;
}

void dw_pcie_msi_init(struct pcie_port *pp)
{
	pp->msi_data = __get_free_pages(GFP_KERNEL, 0);

	/* program the msi_data */
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_LO, 4,
			virt_to_phys((void *)pp->msi_data));
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_HI, 4, 0);
}

static void dw_pcie_msi_clear_irq(struct pcie_port *pp, int irq)
{
	unsigned int res, bit, val;

	res = (irq / 32) * 12;
	bit = irq % 32;
	dw_pcie_rd_own_conf(pp, PCIE_MSI_INTR0_ENABLE + res, 4, &val);
	val &= ~(1 << bit);
	dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_ENABLE + res, 4, val);
}

static void clear_irq_range(struct pcie_port *pp, unsigned int irq_base,
			    unsigned int nvec, unsigned int pos)
{
	unsigned int i;

	for (i = 0; i < nvec; i++) {
		irq_set_msi_desc_off(irq_base, i, NULL);
		/* Disable corresponding interrupt on MSI controller */
		if (pp->ops->msi_clear_irq)
			pp->ops->msi_clear_irq(pp, pos + i);
		else
			dw_pcie_msi_clear_irq(pp, pos + i);
	}

	bitmap_release_region(pp->msi_irq_in_use, pos, order_base_2(nvec));
}

static void dw_pcie_msi_set_irq(struct pcie_port *pp, int irq)
{
	unsigned int res, bit, val;

	res = (irq / 32) * 12;
	bit = irq % 32;
	dw_pcie_rd_own_conf(pp, PCIE_MSI_INTR0_ENABLE + res, 4, &val);
	val |= 1 << bit;
	dw_pcie_wr_own_conf(pp, PCIE_MSI_INTR0_ENABLE + res, 4, val);
}

static int assign_irq(int no_irqs, struct msi_desc *desc, int *pos)
{
	int irq, pos0, i;
	struct pcie_port *pp = sys_to_pcie(desc->dev->bus->sysdata);

	pos0 = bitmap_find_free_region(pp->msi_irq_in_use, MAX_MSI_IRQS,
				       order_base_2(no_irqs));
	if (pos0 < 0)
		goto no_valid_irq;

	irq = irq_find_mapping(pp->irq_domain, pos0);
	if (!irq)
		goto no_valid_irq;

	/*
	 * irq_create_mapping (called from dw_pcie_host_init) pre-allocates
	 * descs so there is no need to allocate descs here. We can therefore
	 * assume that if irq_find_mapping above returns non-zero, then the
	 * descs are also successfully allocated.
	 */

	for (i = 0; i < no_irqs; i++) {
		if (irq_set_msi_desc_off(irq, i, desc) != 0) {
			clear_irq_range(pp, irq, i, pos0);
			goto no_valid_irq;
		}
		/*Enable corresponding interrupt in MSI interrupt controller */
		if (pp->ops->msi_set_irq)
			pp->ops->msi_set_irq(pp, pos0 + i);
		else
			dw_pcie_msi_set_irq(pp, pos0 + i);
	}

	*pos = pos0;
	return irq;

no_valid_irq:
	*pos = pos0;
	return -ENOSPC;
}

static int dw_msi_setup_irq(struct msi_controller *chip, struct pci_dev *pdev,
			struct msi_desc *desc)
{
	int irq, pos;
	struct msi_msg msg;
	struct pcie_port *pp = sys_to_pcie(pdev->bus->sysdata);

	if (desc->msi_attrib.is_msix)
		return -EINVAL;

	irq = assign_irq(1, desc, &pos);
	if (irq < 0)
		return irq;

	if (pp->ops->get_msi_addr)
		msg.address_lo = pp->ops->get_msi_addr(pp);
	else
		msg.address_lo = virt_to_phys((void *)pp->msi_data);
	msg.address_hi = 0x0;

	if (pp->ops->get_msi_data)
		msg.data = pp->ops->get_msi_data(pp, pos);
	else
		msg.data = pos;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static void dw_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);
	struct msi_desc *msi = irq_data_get_msi_desc(data);
	struct pcie_port *pp = sys_to_pcie(msi->dev->bus->sysdata);

	clear_irq_range(pp, irq, 1, data->hwirq);
}

static struct msi_controller dw_pcie_msi_chip = {
	.setup_irq = dw_msi_setup_irq,
	.teardown_irq = dw_msi_teardown_irq,
};

int dw_pcie_link_up(struct pcie_port *pp)
{
	if (pp->ops->link_up)
		return pp->ops->link_up(pp);
	else
		return 0;
}

static int dw_pcie_msi_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dw_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = dw_pcie_msi_map,
};

int dw_pcie_host_init(struct pcie_port *pp)
{
	struct device_node *np = pp->dev->of_node;
	struct platform_device *pdev = to_platform_device(pp->dev);
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	struct resource *cfg_res;
	u32 val, na, ns;
	const __be32 *addrp;
	int i, index, ret;

	/* Find the address cell size and the number of cells in order to get
	 * the untranslated address.
	 */
	of_property_read_u32(np, "#address-cells", &na);
	ns = of_n_size_cells(np);

	cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (cfg_res) {
		pp->cfg0_size = resource_size(cfg_res)/2;
		pp->cfg1_size = resource_size(cfg_res)/2;
		pp->cfg0_base = cfg_res->start;
		pp->cfg1_base = cfg_res->start + pp->cfg0_size;

		/* Find the untranslated configuration space address */
		index = of_property_match_string(np, "reg-names", "config");
		addrp = of_get_address(np, index, NULL, NULL);
		pp->cfg0_mod_base = of_read_number(addrp, ns);
		pp->cfg1_mod_base = pp->cfg0_mod_base + pp->cfg0_size;
	} else {
		dev_err(pp->dev, "missing *config* reg space\n");
	}

	if (of_pci_range_parser_init(&parser, np)) {
		dev_err(pp->dev, "missing ranges property\n");
		return -EINVAL;
	}

	/* Get the I/O and memory ranges from DT */
	for_each_of_pci_range(&parser, &range) {
		unsigned long restype = range.flags & IORESOURCE_TYPE_BITS;

		if (restype == IORESOURCE_IO) {
			of_pci_range_to_resource(&range, np, &pp->io);
			pp->io.name = "I/O";
			pp->io.start = max_t(resource_size_t,
					     PCIBIOS_MIN_IO,
					     range.pci_addr + global_io_offset);
			pp->io.end = min_t(resource_size_t,
					   IO_SPACE_LIMIT,
					   range.pci_addr + range.size
					   + global_io_offset - 1);
			pp->io_size = resource_size(&pp->io);
			pp->io_bus_addr = range.pci_addr;
			pp->io_base = range.cpu_addr;

			/* Find the untranslated IO space address */
			pp->io_mod_base = of_read_number(parser.range -
							 parser.np + na, ns);
		}
		if (restype == IORESOURCE_MEM) {
			of_pci_range_to_resource(&range, np, &pp->mem);
			pp->mem.name = "MEM";
			pp->mem_size = resource_size(&pp->mem);
			pp->mem_bus_addr = range.pci_addr;

			/* Find the untranslated MEM space address */
			pp->mem_mod_base = of_read_number(parser.range -
							  parser.np + na, ns);
		}
		if (restype == 0) {
			of_pci_range_to_resource(&range, np, &pp->cfg);
			pp->cfg0_size = resource_size(&pp->cfg)/2;
			pp->cfg1_size = resource_size(&pp->cfg)/2;
			pp->cfg0_base = pp->cfg.start;
			pp->cfg1_base = pp->cfg.start + pp->cfg0_size;

			/* Find the untranslated configuration space address */
			pp->cfg0_mod_base = of_read_number(parser.range -
							   parser.np + na, ns);
			pp->cfg1_mod_base = pp->cfg0_mod_base +
					    pp->cfg0_size;
		}
	}

	ret = of_pci_parse_bus_range(np, &pp->busn);
	if (ret < 0) {
		pp->busn.name = np->name;
		pp->busn.start = 0;
		pp->busn.end = 0xff;
		pp->busn.flags = IORESOURCE_BUS;
		dev_dbg(pp->dev, "failed to parse bus-range property: %d, using default %pR\n",
			ret, &pp->busn);
	}

	if (!pp->dbi_base) {
		pp->dbi_base = devm_ioremap(pp->dev, pp->cfg.start,
					resource_size(&pp->cfg));
		if (!pp->dbi_base) {
			dev_err(pp->dev, "error with ioremap\n");
			return -ENOMEM;
		}
	}

	pp->mem_base = pp->mem.start;

	if (!pp->va_cfg0_base) {
		pp->va_cfg0_base = devm_ioremap(pp->dev, pp->cfg0_base,
						pp->cfg0_size);
		if (!pp->va_cfg0_base) {
			dev_err(pp->dev, "error with ioremap in function\n");
			return -ENOMEM;
		}
	}

	if (!pp->va_cfg1_base) {
		pp->va_cfg1_base = devm_ioremap(pp->dev, pp->cfg1_base,
						pp->cfg1_size);
		if (!pp->va_cfg1_base) {
			dev_err(pp->dev, "error with ioremap\n");
			return -ENOMEM;
		}
	}

	if (of_property_read_u32(np, "num-lanes", &pp->lanes)) {
		dev_err(pp->dev, "Failed to parse the number of lanes\n");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		if (!pp->ops->msi_host_init) {
			pp->irq_domain = irq_domain_add_linear(pp->dev->of_node,
						MAX_MSI_IRQS, &msi_domain_ops,
						&dw_pcie_msi_chip);
			if (!pp->irq_domain) {
				dev_err(pp->dev, "irq domain init failed\n");
				return -ENXIO;
			}

			for (i = 0; i < MAX_MSI_IRQS; i++)
				irq_create_mapping(pp->irq_domain, i);
		} else {
			ret = pp->ops->msi_host_init(pp, &dw_pcie_msi_chip);
			if (ret < 0)
				return ret;
		}
	}

	if (pp->ops->host_init)
		pp->ops->host_init(pp);

	if (!pp->ops->rd_other_conf)
		dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX1,
					  PCIE_ATU_TYPE_MEM, pp->mem_mod_base,
					  pp->mem_bus_addr, pp->mem_size);

	dw_pcie_wr_own_conf(pp, PCI_BASE_ADDRESS_0, 4, 0);

	/* program correct class for RC */
	dw_pcie_wr_own_conf(pp, PCI_CLASS_DEVICE, 2, PCI_CLASS_BRIDGE_PCI);

	dw_pcie_rd_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, &val);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_wr_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);

#ifdef CONFIG_PCI_MSI
	dw_pcie_msi_chip.dev = pp->dev;
	dw_pci.msi_ctrl = &dw_pcie_msi_chip;
#endif

	dw_pci.nr_controllers = 1;
	dw_pci.private_data = (void **)&pp;

	pci_common_init_dev(pp->dev, &dw_pci);

	return 0;
}

static int dw_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 *val)
{
	int ret, type;
	u32 address, busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;

	if (bus->parent->number == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_mod_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_mod_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX0,
				  type, cpu_addr,
				  busdev, cfg_size);
	ret = dw_pcie_cfg_read(va_cfg_base + address, where, size, val);
	dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX0,
				  PCIE_ATU_TYPE_IO, pp->io_mod_base,
				  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 val)
{
	int ret, type;
	u32 address, busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;

	if (bus->parent->number == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_mod_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_mod_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX0,
				  type, cpu_addr,
				  busdev, cfg_size);
	ret = dw_pcie_cfg_write(va_cfg_base + address, where, size, val);
	dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX0,
				  PCIE_ATU_TYPE_IO, pp->io_mod_base,
				  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_valid_config(struct pcie_port *pp,
				struct pci_bus *bus, int dev)
{
	/* If there is no link, then there is no device */
	if (bus->number != pp->root_bus_nr) {
		if (!dw_pcie_link_up(pp))
			return 0;
	}

	/* access only one slot on each root port */
	if (bus->number == pp->root_bus_nr && dev > 0)
		return 0;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's (Virtual Bridge's) DS side.
	 */
	if (bus->primary == pp->root_bus_nr && dev > 0)
		return 0;

	return 1;
}

static int dw_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	struct pcie_port *pp = sys_to_pcie(bus->sysdata);
	int ret;

	if (dw_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number != pp->root_bus_nr)
		if (pp->ops->rd_other_conf)
			ret = pp->ops->rd_other_conf(pp, bus, devfn,
						where, size, val);
		else
			ret = dw_pcie_rd_other_conf(pp, bus, devfn,
						where, size, val);
	else
		ret = dw_pcie_rd_own_conf(pp, where, size, val);

	return ret;
}

static int dw_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	struct pcie_port *pp = sys_to_pcie(bus->sysdata);
	int ret;

	if (dw_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number != pp->root_bus_nr)
		if (pp->ops->wr_other_conf)
			ret = pp->ops->wr_other_conf(pp, bus, devfn,
						where, size, val);
		else
			ret = dw_pcie_wr_other_conf(pp, bus, devfn,
						where, size, val);
	else
		ret = dw_pcie_wr_own_conf(pp, where, size, val);

	return ret;
}

static struct pci_ops dw_pcie_ops = {
	.read = dw_pcie_rd_conf,
	.write = dw_pcie_wr_conf,
};

static int dw_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct pcie_port *pp;

	pp = sys_to_pcie(sys);

	if (global_io_offset < SZ_1M && pp->io_size > 0) {
		sys->io_offset = global_io_offset - pp->io_bus_addr;
		pci_ioremap_io(global_io_offset, pp->io_base);
		global_io_offset += SZ_64K;
		pci_add_resource_offset(&sys->resources, &pp->io,
					sys->io_offset);
	}

	sys->mem_offset = pp->mem.start - pp->mem_bus_addr;
	pci_add_resource_offset(&sys->resources, &pp->mem, sys->mem_offset);
	pci_add_resource(&sys->resources, &pp->busn);

	return 1;
}

static struct pci_bus *dw_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_bus *bus;
	struct pcie_port *pp = sys_to_pcie(sys);

	pp->root_bus_nr = sys->busnr;
	bus = pci_scan_root_bus(pp->dev, sys->busnr,
				  &dw_pcie_ops, sys, &sys->resources);
	if (!bus)
		return NULL;

	if (bus && pp->ops->scan_bus)
		pp->ops->scan_bus(pp);

	return bus;
}

static int dw_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pcie_port *pp = sys_to_pcie(dev->bus->sysdata);
	int irq;

	irq = of_irq_parse_and_map_pci(dev, slot, pin);
	if (!irq)
		irq = pp->irq;

	return irq;
}

static struct hw_pci dw_pci = {
	.setup		= dw_pcie_setup,
	.scan		= dw_pcie_scan_bus,
	.map_irq	= dw_pcie_map_irq,
};

void dw_pcie_setup_rc(struct pcie_port *pp)
{
	u32 val;
	u32 membase;
	u32 memlimit;

	/* set the number of lanes */
	dw_pcie_readl_rc(pp, PCIE_PORT_LINK_CONTROL, &val);
	val &= ~PORT_LINK_MODE_MASK;
	switch (pp->lanes) {
	case 1:
		val |= PORT_LINK_MODE_1_LANES;
		break;
	case 2:
		val |= PORT_LINK_MODE_2_LANES;
		break;
	case 4:
		val |= PORT_LINK_MODE_4_LANES;
		break;
	case 8:
		val |= PORT_LINK_MODE_8_LANES;
		break;
	}
	dw_pcie_writel_rc(pp, val, PCIE_PORT_LINK_CONTROL);

	/* set link width speed control register */
	dw_pcie_readl_rc(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, &val);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	switch (pp->lanes) {
	case 1:
		val |= PORT_LOGIC_LINK_WIDTH_1_LANES;
		break;
	case 2:
		val |= PORT_LOGIC_LINK_WIDTH_2_LANES;
		break;
	case 4:
		val |= PORT_LOGIC_LINK_WIDTH_4_LANES;
		break;
	case 8:
		val |= PORT_LOGIC_LINK_WIDTH_8_LANES;
		break;
	}
	dw_pcie_writel_rc(pp, val, PCIE_LINK_WIDTH_SPEED_CONTROL);

	/* setup RC BARs */
	dw_pcie_writel_rc(pp, 0x00000004, PCI_BASE_ADDRESS_0);
	dw_pcie_writel_rc(pp, 0x00000000, PCI_BASE_ADDRESS_1);

	/* setup interrupt pins */
	dw_pcie_readl_rc(pp, PCI_INTERRUPT_LINE, &val);
	val &= 0xffff00ff;
	val |= 0x00000100;
	dw_pcie_writel_rc(pp, val, PCI_INTERRUPT_LINE);

	/* setup bus numbers */
	dw_pcie_readl_rc(pp, PCI_PRIMARY_BUS, &val);
	val &= 0xff000000;
	val |= 0x00010100;
	dw_pcie_writel_rc(pp, val, PCI_PRIMARY_BUS);

	/* setup memory base, memory limit */
	membase = ((u32)pp->mem_base & 0xfff00000) >> 16;
	memlimit = (pp->mem_size + (u32)pp->mem_base) & 0xfff00000;
	val = memlimit | membase;
	dw_pcie_writel_rc(pp, val, PCI_MEMORY_BASE);

	/* setup command register */
	dw_pcie_readl_rc(pp, PCI_COMMAND, &val);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie_writel_rc(pp, val, PCI_COMMAND);
}

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Designware PCIe host controller driver");
MODULE_LICENSE("GPL v2");
