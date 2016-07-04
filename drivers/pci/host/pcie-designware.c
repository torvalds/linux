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
#include <linux/delay.h>

#include "pcie-designware.h"

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_USLEEP_MIN		90000
#define LINK_WAIT_USLEEP_MAX		100000

/* Parameters for the waiting for iATU enabled routine */
#define LINK_WAIT_MAX_IATU_RETRIES	5
#define LINK_WAIT_IATU_MIN		9000
#define LINK_WAIT_IATU_MAX		10000

/* Synopsys-specific PCIe configuration registers */
#define PCIE_PORT_LINK_CONTROL		0x710
#define PORT_LINK_MODE_MASK		(0x3f << 16)
#define PORT_LINK_MODE_1_LANES		(0x1 << 16)
#define PORT_LINK_MODE_2_LANES		(0x3 << 16)
#define PORT_LINK_MODE_4_LANES		(0x7 << 16)
#define PORT_LINK_MODE_8_LANES		(0xf << 16)

#define PCIE_LINK_WIDTH_SPEED_CONTROL	0x80C
#define PORT_LOGIC_SPEED_CHANGE		(0x1 << 17)
#define PORT_LOGIC_LINK_WIDTH_MASK	(0x1f << 8)
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
#define PCIE_ATU_REGION_INDEX2		(0x2 << 0)
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

/*
 * iATU Unroll-specific register definitions
 * From 4.80 core version the address translation will be made by unroll
 */
#define PCIE_ATU_UNR_REGION_CTRL1	0x00
#define PCIE_ATU_UNR_REGION_CTRL2	0x04
#define PCIE_ATU_UNR_LOWER_BASE		0x08
#define PCIE_ATU_UNR_UPPER_BASE		0x0C
#define PCIE_ATU_UNR_LIMIT		0x10
#define PCIE_ATU_UNR_LOWER_TARGET	0x14
#define PCIE_ATU_UNR_UPPER_TARGET	0x18

/* Register address builder */
#define PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(region)  ((0x3 << 20) | (region << 9))

/* PCIe Port Logic registers */
#define PLR_OFFSET			0x700
#define PCIE_PHY_DEBUG_R1		(PLR_OFFSET + 0x2c)
#define PCIE_PHY_DEBUG_R1_LINK_UP	(0x1 << 4)
#define PCIE_PHY_DEBUG_R1_LINK_IN_TRAINING	(0x1 << 29)

static struct pci_ops dw_pcie_ops;

int dw_pcie_cfg_read(void __iomem *addr, int size, u32 *val)
{
	if ((uintptr_t)addr & (size - 1)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (size == 4)
		*val = readl(addr);
	else if (size == 2)
		*val = readw(addr);
	else if (size == 1)
		*val = readb(addr);
	else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

int dw_pcie_cfg_write(void __iomem *addr, int size, u32 val)
{
	if ((uintptr_t)addr & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr);
	else if (size == 1)
		writeb(val, addr);
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static inline u32 dw_pcie_readl_rc(struct pcie_port *pp, u32 reg)
{
	if (pp->ops->readl_rc)
		return pp->ops->readl_rc(pp, pp->dbi_base + reg);

	return readl(pp->dbi_base + reg);
}

static inline void dw_pcie_writel_rc(struct pcie_port *pp, u32 val, u32 reg)
{
	if (pp->ops->writel_rc)
		pp->ops->writel_rc(pp, val, pp->dbi_base + reg);
	else
		writel(val, pp->dbi_base + reg);
}

static inline u32 dw_pcie_readl_unroll(struct pcie_port *pp, u32 index, u32 reg)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	if (pp->ops->readl_rc)
		return pp->ops->readl_rc(pp, pp->dbi_base + offset + reg);

	return readl(pp->dbi_base + offset + reg);
}

static inline void dw_pcie_writel_unroll(struct pcie_port *pp, u32 index,
					 u32 val, u32 reg)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	if (pp->ops->writel_rc)
		pp->ops->writel_rc(pp, val, pp->dbi_base + offset + reg);
	else
		writel(val, pp->dbi_base + offset + reg);
}

static int dw_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
			       u32 *val)
{
	if (pp->ops->rd_own_conf)
		return pp->ops->rd_own_conf(pp, where, size, val);

	return dw_pcie_cfg_read(pp->dbi_base + where, size, val);
}

static int dw_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
			       u32 val)
{
	if (pp->ops->wr_own_conf)
		return pp->ops->wr_own_conf(pp, where, size, val);

	return dw_pcie_cfg_write(pp->dbi_base + where, size, val);
}

static void dw_pcie_prog_outbound_atu(struct pcie_port *pp, int index,
		int type, u64 cpu_addr, u64 pci_addr, u32 size)
{
	u32 retries, val;

	if (pp->iatu_unroll_enabled) {
		dw_pcie_writel_unroll(pp, index,
			lower_32_bits(cpu_addr), PCIE_ATU_UNR_LOWER_BASE);
		dw_pcie_writel_unroll(pp, index,
			upper_32_bits(cpu_addr), PCIE_ATU_UNR_UPPER_BASE);
		dw_pcie_writel_unroll(pp, index,
			lower_32_bits(cpu_addr + size - 1), PCIE_ATU_UNR_LIMIT);
		dw_pcie_writel_unroll(pp, index,
			lower_32_bits(pci_addr), PCIE_ATU_UNR_LOWER_TARGET);
		dw_pcie_writel_unroll(pp, index,
			upper_32_bits(pci_addr), PCIE_ATU_UNR_UPPER_TARGET);
		dw_pcie_writel_unroll(pp, index,
			type, PCIE_ATU_UNR_REGION_CTRL1);
		dw_pcie_writel_unroll(pp, index,
			PCIE_ATU_ENABLE, PCIE_ATU_UNR_REGION_CTRL2);
	} else {
		dw_pcie_writel_rc(pp, PCIE_ATU_REGION_OUTBOUND | index,
						PCIE_ATU_VIEWPORT);
		dw_pcie_writel_rc(pp, lower_32_bits(cpu_addr),
						PCIE_ATU_LOWER_BASE);
		dw_pcie_writel_rc(pp, upper_32_bits(cpu_addr),
						PCIE_ATU_UPPER_BASE);
		dw_pcie_writel_rc(pp, lower_32_bits(cpu_addr + size - 1),
						PCIE_ATU_LIMIT);
		dw_pcie_writel_rc(pp, lower_32_bits(pci_addr),
						PCIE_ATU_LOWER_TARGET);
		dw_pcie_writel_rc(pp, upper_32_bits(pci_addr),
						PCIE_ATU_UPPER_TARGET);
		dw_pcie_writel_rc(pp, type, PCIE_ATU_CR1);
		dw_pcie_writel_rc(pp, PCIE_ATU_ENABLE, PCIE_ATU_CR2);
	}

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		if (pp->iatu_unroll_enabled)
			val = dw_pcie_readl_unroll(pp, index,
						   PCIE_ATU_UNR_REGION_CTRL2);
		else
			val = dw_pcie_readl_rc(pp, PCIE_ATU_CR2);

		if (val == PCIE_ATU_ENABLE)
			return;

		usleep_range(LINK_WAIT_IATU_MIN, LINK_WAIT_IATU_MAX);
	}
	dev_err(pp->dev, "iATU is not being enabled\n");
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
	u64 msi_target;

	pp->msi_data = __get_free_pages(GFP_KERNEL, 0);
	msi_target = virt_to_phys((void *)pp->msi_data);

	/* program the msi_data */
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_LO, 4,
			    (u32)(msi_target & 0xffffffff));
	dw_pcie_wr_own_conf(pp, PCIE_MSI_ADDR_HI, 4,
			    (u32)(msi_target >> 32 & 0xffffffff));
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
	struct pcie_port *pp = (struct pcie_port *) msi_desc_to_pci_sysdata(desc);

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
	desc->nvec_used = no_irqs;
	desc->msi_attrib.multiple = order_base_2(no_irqs);

	return irq;

no_valid_irq:
	*pos = pos0;
	return -ENOSPC;
}

static void dw_msi_setup_msg(struct pcie_port *pp, unsigned int irq, u32 pos)
{
	struct msi_msg msg;
	u64 msi_target;

	if (pp->ops->get_msi_addr)
		msi_target = pp->ops->get_msi_addr(pp);
	else
		msi_target = virt_to_phys((void *)pp->msi_data);

	msg.address_lo = (u32)(msi_target & 0xffffffff);
	msg.address_hi = (u32)(msi_target >> 32 & 0xffffffff);

	if (pp->ops->get_msi_data)
		msg.data = pp->ops->get_msi_data(pp, pos);
	else
		msg.data = pos;

	pci_write_msi_msg(irq, &msg);
}

static int dw_msi_setup_irq(struct msi_controller *chip, struct pci_dev *pdev,
			struct msi_desc *desc)
{
	int irq, pos;
	struct pcie_port *pp = pdev->bus->sysdata;

	if (desc->msi_attrib.is_msix)
		return -EINVAL;

	irq = assign_irq(1, desc, &pos);
	if (irq < 0)
		return irq;

	dw_msi_setup_msg(pp, irq, pos);

	return 0;
}

static int dw_msi_setup_irqs(struct msi_controller *chip, struct pci_dev *pdev,
			     int nvec, int type)
{
#ifdef CONFIG_PCI_MSI
	int irq, pos;
	struct msi_desc *desc;
	struct pcie_port *pp = pdev->bus->sysdata;

	/* MSI-X interrupts are not supported */
	if (type == PCI_CAP_ID_MSIX)
		return -EINVAL;

	WARN_ON(!list_is_singular(&pdev->dev.msi_list));
	desc = list_entry(pdev->dev.msi_list.next, struct msi_desc, list);

	irq = assign_irq(nvec, desc, &pos);
	if (irq < 0)
		return irq;

	dw_msi_setup_msg(pp, irq, pos);

	return 0;
#else
	return -EINVAL;
#endif
}

static void dw_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);
	struct msi_desc *msi = irq_data_get_msi_desc(data);
	struct pcie_port *pp = (struct pcie_port *) msi_desc_to_pci_sysdata(msi);

	clear_irq_range(pp, irq, 1, data->hwirq);
}

static struct msi_controller dw_pcie_msi_chip = {
	.setup_irq = dw_msi_setup_irq,
	.setup_irqs = dw_msi_setup_irqs,
	.teardown_irq = dw_msi_teardown_irq,
};

int dw_pcie_wait_for_link(struct pcie_port *pp)
{
	int retries;

	/* check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (dw_pcie_link_up(pp)) {
			dev_info(pp->dev, "link up\n");
			return 0;
		}
		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	dev_err(pp->dev, "phy link never came up\n");

	return -ETIMEDOUT;
}

int dw_pcie_link_up(struct pcie_port *pp)
{
	u32 val;

	if (pp->ops->link_up)
		return pp->ops->link_up(pp);

	val = readl(pp->dbi_base + PCIE_PHY_DEBUG_R1);
	return ((val & PCIE_PHY_DEBUG_R1_LINK_UP) &&
		(!(val & PCIE_PHY_DEBUG_R1_LINK_IN_TRAINING)));
}

static int dw_pcie_msi_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dw_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = dw_pcie_msi_map,
};

static u8 dw_pcie_iatu_unroll_enabled(struct pcie_port *pp)
{
	u32 val;

	val = dw_pcie_readl_rc(pp, PCIE_ATU_VIEWPORT);
	if (val == 0xffffffff)
		return 1;

	return 0;
}

int dw_pcie_host_init(struct pcie_port *pp)
{
	struct device_node *np = pp->dev->of_node;
	struct platform_device *pdev = to_platform_device(pp->dev);
	struct pci_bus *bus, *child;
	struct resource *cfg_res;
	int i, ret;
	LIST_HEAD(res);
	struct resource_entry *win;

	cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (cfg_res) {
		pp->cfg0_size = resource_size(cfg_res)/2;
		pp->cfg1_size = resource_size(cfg_res)/2;
		pp->cfg0_base = cfg_res->start;
		pp->cfg1_base = cfg_res->start + pp->cfg0_size;
	} else if (!pp->va_cfg0_base) {
		dev_err(pp->dev, "missing *config* reg space\n");
	}

	ret = of_pci_get_host_bridge_resources(np, 0, 0xff, &res, &pp->io_base);
	if (ret)
		return ret;

	ret = devm_request_pci_bus_resources(&pdev->dev, &res);
	if (ret)
		goto error;

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &res) {
		switch (resource_type(win->res)) {
		case IORESOURCE_IO:
			pp->io = win->res;
			pp->io->name = "I/O";
			pp->io_size = resource_size(pp->io);
			pp->io_bus_addr = pp->io->start - win->offset;
			ret = pci_remap_iospace(pp->io, pp->io_base);
			if (ret)
				dev_warn(pp->dev, "error %d: failed to map resource %pR\n",
					 ret, pp->io);
			break;
		case IORESOURCE_MEM:
			pp->mem = win->res;
			pp->mem->name = "MEM";
			pp->mem_size = resource_size(pp->mem);
			pp->mem_bus_addr = pp->mem->start - win->offset;
			break;
		case 0:
			pp->cfg = win->res;
			pp->cfg0_size = resource_size(pp->cfg)/2;
			pp->cfg1_size = resource_size(pp->cfg)/2;
			pp->cfg0_base = pp->cfg->start;
			pp->cfg1_base = pp->cfg->start + pp->cfg0_size;
			break;
		case IORESOURCE_BUS:
			pp->busn = win->res;
			break;
		}
	}

	if (!pp->dbi_base) {
		pp->dbi_base = devm_ioremap(pp->dev, pp->cfg->start,
					resource_size(pp->cfg));
		if (!pp->dbi_base) {
			dev_err(pp->dev, "error with ioremap\n");
			ret = -ENOMEM;
			goto error;
		}
	}

	pp->mem_base = pp->mem->start;

	if (!pp->va_cfg0_base) {
		pp->va_cfg0_base = devm_ioremap(pp->dev, pp->cfg0_base,
						pp->cfg0_size);
		if (!pp->va_cfg0_base) {
			dev_err(pp->dev, "error with ioremap in function\n");
			ret = -ENOMEM;
			goto error;
		}
	}

	if (!pp->va_cfg1_base) {
		pp->va_cfg1_base = devm_ioremap(pp->dev, pp->cfg1_base,
						pp->cfg1_size);
		if (!pp->va_cfg1_base) {
			dev_err(pp->dev, "error with ioremap\n");
			ret = -ENOMEM;
			goto error;
		}
	}

	ret = of_property_read_u32(np, "num-lanes", &pp->lanes);
	if (ret)
		pp->lanes = 0;

	ret = of_property_read_u32(np, "num-viewport", &pp->num_viewport);
	if (ret)
		pp->num_viewport = 2;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		if (!pp->ops->msi_host_init) {
			pp->irq_domain = irq_domain_add_linear(pp->dev->of_node,
						MAX_MSI_IRQS, &msi_domain_ops,
						&dw_pcie_msi_chip);
			if (!pp->irq_domain) {
				dev_err(pp->dev, "irq domain init failed\n");
				ret = -ENXIO;
				goto error;
			}

			for (i = 0; i < MAX_MSI_IRQS; i++)
				irq_create_mapping(pp->irq_domain, i);
		} else {
			ret = pp->ops->msi_host_init(pp, &dw_pcie_msi_chip);
			if (ret < 0)
				goto error;
		}
	}

	pp->iatu_unroll_enabled = dw_pcie_iatu_unroll_enabled(pp);

	if (pp->ops->host_init)
		pp->ops->host_init(pp);

	pp->root_bus_nr = pp->busn->start;
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		bus = pci_scan_root_bus_msi(pp->dev, pp->root_bus_nr,
					    &dw_pcie_ops, pp, &res,
					    &dw_pcie_msi_chip);
		dw_pcie_msi_chip.dev = pp->dev;
	} else
		bus = pci_scan_root_bus(pp->dev, pp->root_bus_nr, &dw_pcie_ops,
					pp, &res);
	if (!bus) {
		ret = -ENOMEM;
		goto error;
	}

	if (pp->ops->scan_bus)
		pp->ops->scan_bus(pp);

#ifdef CONFIG_ARM
	/* support old dtbs that incorrectly describe IRQs */
	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);
#endif

	pci_bus_size_bridges(bus);
	pci_bus_assign_resources(bus);

	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(bus);
	return 0;

error:
	pci_free_resource_list(&res);
	return ret;
}

static int dw_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 *val)
{
	int ret, type;
	u32 busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;

	if (pp->ops->rd_other_conf)
		return pp->ops->rd_other_conf(pp, bus, devfn, where, size, val);

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->parent->number == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX1,
				  type, cpu_addr,
				  busdev, cfg_size);
	ret = dw_pcie_cfg_read(va_cfg_base + where, size, val);
	if (pp->num_viewport <= 2)
		dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX1,
					  PCIE_ATU_TYPE_IO, pp->io_base,
					  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 val)
{
	int ret, type;
	u32 busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;

	if (pp->ops->wr_other_conf)
		return pp->ops->wr_other_conf(pp, bus, devfn, where, size, val);

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->parent->number == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX1,
				  type, cpu_addr,
				  busdev, cfg_size);
	ret = dw_pcie_cfg_write(va_cfg_base + where, size, val);
	if (pp->num_viewport <= 2)
		dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX1,
					  PCIE_ATU_TYPE_IO, pp->io_base,
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
	struct pcie_port *pp = bus->sysdata;

	if (dw_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number == pp->root_bus_nr)
		return dw_pcie_rd_own_conf(pp, where, size, val);

	return dw_pcie_rd_other_conf(pp, bus, devfn, where, size, val);
}

static int dw_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	struct pcie_port *pp = bus->sysdata;

	if (dw_pcie_valid_config(pp, bus, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == pp->root_bus_nr)
		return dw_pcie_wr_own_conf(pp, where, size, val);

	return dw_pcie_wr_other_conf(pp, bus, devfn, where, size, val);
}

static struct pci_ops dw_pcie_ops = {
	.read = dw_pcie_rd_conf,
	.write = dw_pcie_wr_conf,
};

void dw_pcie_setup_rc(struct pcie_port *pp)
{
	u32 val;

	/* set the number of lanes */
	val = dw_pcie_readl_rc(pp, PCIE_PORT_LINK_CONTROL);
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
	default:
		dev_err(pp->dev, "num-lanes %u: invalid value\n", pp->lanes);
		return;
	}
	dw_pcie_writel_rc(pp, val, PCIE_PORT_LINK_CONTROL);

	/* set link width speed control register */
	val = dw_pcie_readl_rc(pp, PCIE_LINK_WIDTH_SPEED_CONTROL);
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
	val = dw_pcie_readl_rc(pp, PCI_INTERRUPT_LINE);
	val &= 0xffff00ff;
	val |= 0x00000100;
	dw_pcie_writel_rc(pp, val, PCI_INTERRUPT_LINE);

	/* setup bus numbers */
	val = dw_pcie_readl_rc(pp, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00010100;
	dw_pcie_writel_rc(pp, val, PCI_PRIMARY_BUS);

	/* setup command register */
	val = dw_pcie_readl_rc(pp, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie_writel_rc(pp, val, PCI_COMMAND);

	/*
	 * If the platform provides ->rd_other_conf, it means the platform
	 * uses its own address translation component rather than ATU, so
	 * we should not program the ATU here.
	 */
	if (!pp->ops->rd_other_conf) {
		dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX0,
					  PCIE_ATU_TYPE_MEM, pp->mem_base,
					  pp->mem_bus_addr, pp->mem_size);
		if (pp->num_viewport > 2)
			dw_pcie_prog_outbound_atu(pp, PCIE_ATU_REGION_INDEX2,
						  PCIE_ATU_TYPE_IO, pp->io_base,
						  pp->io_bus_addr, pp->io_size);
	}

	dw_pcie_wr_own_conf(pp, PCI_BASE_ADDRESS_0, 4, 0);

	/* program correct class for RC */
	dw_pcie_wr_own_conf(pp, PCI_CLASS_DEVICE, 2, PCI_CLASS_BRIDGE_PCI);

	dw_pcie_rd_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, &val);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_wr_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);
}

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Designware PCIe host controller driver");
MODULE_LICENSE("GPL v2");
