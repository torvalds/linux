/*
 * MediaTek PCIe host controller driver.
 *
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *	   Honghui Zhang <honghui.zhang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

/* PCIe shared registers */
#define PCIE_SYS_CFG		0x00
#define PCIE_INT_ENABLE		0x0c
#define PCIE_CFG_ADDR		0x20
#define PCIE_CFG_DATA		0x24

/* PCIe per port registers */
#define PCIE_BAR0_SETUP		0x10
#define PCIE_CLASS		0x34
#define PCIE_LINK_STATUS	0x50

#define PCIE_PORT_INT_EN(x)	BIT(20 + (x))
#define PCIE_PORT_PERST(x)	BIT(1 + (x))
#define PCIE_PORT_LINKUP	BIT(0)
#define PCIE_BAR_MAP_MAX	GENMASK(31, 16)

#define PCIE_BAR_ENABLE		BIT(0)
#define PCIE_REVISION_ID	BIT(0)
#define PCIE_CLASS_CODE		(0x60400 << 8)
#define PCIE_CONF_REG(regn)	(((regn) & GENMASK(7, 2)) | \
				((((regn) >> 8) & GENMASK(3, 0)) << 24))
#define PCIE_CONF_FUN(fun)	(((fun) << 8) & GENMASK(10, 8))
#define PCIE_CONF_DEV(dev)	(((dev) << 11) & GENMASK(15, 11))
#define PCIE_CONF_BUS(bus)	(((bus) << 16) & GENMASK(23, 16))
#define PCIE_CONF_ADDR(regn, fun, dev, bus) \
	(PCIE_CONF_REG(regn) | PCIE_CONF_FUN(fun) | \
	 PCIE_CONF_DEV(dev) | PCIE_CONF_BUS(bus))

/* MediaTek specific configuration registers */
#define PCIE_FTS_NUM		0x70c
#define PCIE_FTS_NUM_MASK	GENMASK(15, 8)
#define PCIE_FTS_NUM_L0(x)	((x) & 0xff << 8)

#define PCIE_FC_CREDIT		0x73c
#define PCIE_FC_CREDIT_MASK	(GENMASK(31, 31) | GENMASK(28, 16))
#define PCIE_FC_CREDIT_VAL(x)	((x) << 16)

/* PCIe V2 share registers */
#define PCIE_SYS_CFG_V2		0x0
#define PCIE_CSR_LTSSM_EN(x)	BIT(0 + (x) * 8)
#define PCIE_CSR_ASPM_L1_EN(x)	BIT(1 + (x) * 8)

/* PCIe V2 per-port registers */
#define PCIE_MSI_VECTOR		0x0c0
#define PCIE_INT_MASK		0x420
#define INTX_MASK		GENMASK(19, 16)
#define INTX_SHIFT		16
#define PCIE_INT_STATUS		0x424
#define MSI_STATUS		BIT(23)
#define PCIE_IMSI_STATUS	0x42c
#define PCIE_IMSI_ADDR		0x430
#define MSI_MASK		BIT(23)
#define MTK_MSI_IRQS_NUM	32

#define PCIE_AHB_TRANS_BASE0_L	0x438
#define PCIE_AHB_TRANS_BASE0_H	0x43c
#define AHB2PCIE_SIZE(x)	((x) & GENMASK(4, 0))
#define PCIE_AXI_WINDOW0	0x448
#define WIN_ENABLE		BIT(7)

/* PCIe V2 configuration transaction header */
#define PCIE_CFG_HEADER0	0x460
#define PCIE_CFG_HEADER1	0x464
#define PCIE_CFG_HEADER2	0x468
#define PCIE_CFG_WDATA		0x470
#define PCIE_APP_TLP_REQ	0x488
#define PCIE_CFG_RDATA		0x48c
#define APP_CFG_REQ		BIT(0)
#define APP_CPL_STATUS		GENMASK(7, 5)

#define CFG_WRRD_TYPE_0		4
#define CFG_WR_FMT		2
#define CFG_RD_FMT		0

#define CFG_DW0_LENGTH(length)	((length) & GENMASK(9, 0))
#define CFG_DW0_TYPE(type)	(((type) << 24) & GENMASK(28, 24))
#define CFG_DW0_FMT(fmt)	(((fmt) << 29) & GENMASK(31, 29))
#define CFG_DW2_REGN(regn)	((regn) & GENMASK(11, 2))
#define CFG_DW2_FUN(fun)	(((fun) << 16) & GENMASK(18, 16))
#define CFG_DW2_DEV(dev)	(((dev) << 19) & GENMASK(23, 19))
#define CFG_DW2_BUS(bus)	(((bus) << 24) & GENMASK(31, 24))
#define CFG_HEADER_DW0(type, fmt) \
	(CFG_DW0_LENGTH(1) | CFG_DW0_TYPE(type) | CFG_DW0_FMT(fmt))
#define CFG_HEADER_DW1(where, size) \
	(GENMASK(((size) - 1), 0) << ((where) & 0x3))
#define CFG_HEADER_DW2(regn, fun, dev, bus) \
	(CFG_DW2_REGN(regn) | CFG_DW2_FUN(fun) | \
	CFG_DW2_DEV(dev) | CFG_DW2_BUS(bus))

#define PCIE_RST_CTRL		0x510
#define PCIE_PHY_RSTB		BIT(0)
#define PCIE_PIPE_SRSTB		BIT(1)
#define PCIE_MAC_SRSTB		BIT(2)
#define PCIE_CRSTB		BIT(3)
#define PCIE_PERSTB		BIT(8)
#define PCIE_LINKDOWN_RST_EN	GENMASK(15, 13)
#define PCIE_LINK_STATUS_V2	0x804
#define PCIE_PORT_LINKUP_V2	BIT(10)

struct mtk_pcie_port;

/**
 * struct mtk_pcie_soc - differentiate between host generations
 * @has_msi: whether this host supports MSI interrupts or not
 * @ops: pointer to configuration access functions
 * @startup: pointer to controller setting functions
 * @setup_irq: pointer to initialize IRQ functions
 */
struct mtk_pcie_soc {
	bool has_msi;
	struct pci_ops *ops;
	int (*startup)(struct mtk_pcie_port *port);
	int (*setup_irq)(struct mtk_pcie_port *port, struct device_node *node);
};

/**
 * struct mtk_pcie_port - PCIe port information
 * @base: IO mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @reset: pointer to port reset control
 * @sys_ck: pointer to transaction/data link layer clock
 * @ahb_ck: pointer to AHB slave interface operating clock for CSR access
 *          and RC initiated MMIO access
 * @axi_ck: pointer to application layer MMIO channel operating clock
 * @aux_ck: pointer to pe2_mac_bridge and pe2_mac_core operating clock
 *          when pcie_mac_ck/pcie_pipe_ck is turned off
 * @obff_ck: pointer to OBFF functional block operating clock
 * @pipe_ck: pointer to LTSSM and PHY/MAC layer operating clock
 * @phy: pointer to PHY control block
 * @lane: lane count
 * @slot: port slot
 * @irq_domain: legacy INTx IRQ domain
 * @msi_domain: MSI IRQ domain
 * @msi_irq_in_use: bit map for assigned MSI IRQ
 */
struct mtk_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct mtk_pcie *pcie;
	struct reset_control *reset;
	struct clk *sys_ck;
	struct clk *ahb_ck;
	struct clk *axi_ck;
	struct clk *aux_ck;
	struct clk *obff_ck;
	struct clk *pipe_ck;
	struct phy *phy;
	u32 lane;
	u32 slot;
	struct irq_domain *irq_domain;
	struct irq_domain *msi_domain;
	DECLARE_BITMAP(msi_irq_in_use, MTK_MSI_IRQS_NUM);
};

/**
 * struct mtk_pcie - PCIe host information
 * @dev: pointer to PCIe device
 * @base: IO mapped register base
 * @free_ck: free-run reference clock
 * @io: IO resource
 * @pio: PIO resource
 * @mem: non-prefetchable memory resource
 * @busn: bus range
 * @offset: IO / Memory offset
 * @ports: pointer to PCIe port information
 * @soc: pointer to SoC-dependent operations
 */
struct mtk_pcie {
	struct device *dev;
	void __iomem *base;
	struct clk *free_ck;

	struct resource io;
	struct resource pio;
	struct resource mem;
	struct resource busn;
	struct {
		resource_size_t mem;
		resource_size_t io;
	} offset;
	struct list_head ports;
	const struct mtk_pcie_soc *soc;
};

static void mtk_pcie_subsys_powerdown(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;

	clk_disable_unprepare(pcie->free_ck);

	if (dev->pm_domain) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}
}

static void mtk_pcie_port_free(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;

	devm_iounmap(dev, port->base);
	list_del(&port->list);
	devm_kfree(dev, port);
}

static void mtk_pcie_put_resources(struct mtk_pcie *pcie)
{
	struct mtk_pcie_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		phy_power_off(port->phy);
		phy_exit(port->phy);
		clk_disable_unprepare(port->pipe_ck);
		clk_disable_unprepare(port->obff_ck);
		clk_disable_unprepare(port->axi_ck);
		clk_disable_unprepare(port->aux_ck);
		clk_disable_unprepare(port->ahb_ck);
		clk_disable_unprepare(port->sys_ck);
		mtk_pcie_port_free(port);
	}

	mtk_pcie_subsys_powerdown(pcie);
}

static int mtk_pcie_check_cfg_cpld(struct mtk_pcie_port *port)
{
	u32 val;
	int err;

	err = readl_poll_timeout_atomic(port->base + PCIE_APP_TLP_REQ, val,
					!(val & APP_CFG_REQ), 10,
					100 * USEC_PER_MSEC);
	if (err)
		return PCIBIOS_SET_FAILED;

	if (readl(port->base + PCIE_APP_TLP_REQ) & APP_CPL_STATUS)
		return PCIBIOS_SET_FAILED;

	return PCIBIOS_SUCCESSFUL;
}

static int mtk_pcie_hw_rd_cfg(struct mtk_pcie_port *port, u32 bus, u32 devfn,
			      int where, int size, u32 *val)
{
	u32 tmp;

	/* Write PCIe configuration transaction header for Cfgrd */
	writel(CFG_HEADER_DW0(CFG_WRRD_TYPE_0, CFG_RD_FMT),
	       port->base + PCIE_CFG_HEADER0);
	writel(CFG_HEADER_DW1(where, size), port->base + PCIE_CFG_HEADER1);
	writel(CFG_HEADER_DW2(where, PCI_FUNC(devfn), PCI_SLOT(devfn), bus),
	       port->base + PCIE_CFG_HEADER2);

	/* Trigger h/w to transmit Cfgrd TLP */
	tmp = readl(port->base + PCIE_APP_TLP_REQ);
	tmp |= APP_CFG_REQ;
	writel(tmp, port->base + PCIE_APP_TLP_REQ);

	/* Check completion status */
	if (mtk_pcie_check_cfg_cpld(port))
		return PCIBIOS_SET_FAILED;

	/* Read cpld payload of Cfgrd */
	*val = readl(port->base + PCIE_CFG_RDATA);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

static int mtk_pcie_hw_wr_cfg(struct mtk_pcie_port *port, u32 bus, u32 devfn,
			      int where, int size, u32 val)
{
	/* Write PCIe configuration transaction header for Cfgwr */
	writel(CFG_HEADER_DW0(CFG_WRRD_TYPE_0, CFG_WR_FMT),
	       port->base + PCIE_CFG_HEADER0);
	writel(CFG_HEADER_DW1(where, size), port->base + PCIE_CFG_HEADER1);
	writel(CFG_HEADER_DW2(where, PCI_FUNC(devfn), PCI_SLOT(devfn), bus),
	       port->base + PCIE_CFG_HEADER2);

	/* Write Cfgwr data */
	val = val << 8 * (where & 3);
	writel(val, port->base + PCIE_CFG_WDATA);

	/* Trigger h/w to transmit Cfgwr TLP */
	val = readl(port->base + PCIE_APP_TLP_REQ);
	val |= APP_CFG_REQ;
	writel(val, port->base + PCIE_APP_TLP_REQ);

	/* Check completion status */
	return mtk_pcie_check_cfg_cpld(port);
}

static struct mtk_pcie_port *mtk_pcie_find_port(struct pci_bus *bus,
						unsigned int devfn)
{
	struct mtk_pcie *pcie = bus->sysdata;
	struct mtk_pcie_port *port;

	list_for_each_entry(port, &pcie->ports, list)
		if (port->slot == PCI_SLOT(devfn))
			return port;

	return NULL;
}

static int mtk_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	struct mtk_pcie_port *port;
	u32 bn = bus->number;
	int ret;

	port = mtk_pcie_find_port(bus, devfn);
	if (!port) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	ret = mtk_pcie_hw_rd_cfg(port, bn, devfn, where, size, val);
	if (ret)
		*val = ~0;

	return ret;
}

static int mtk_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct mtk_pcie_port *port;
	u32 bn = bus->number;

	port = mtk_pcie_find_port(bus, devfn);
	if (!port)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return mtk_pcie_hw_wr_cfg(port, bn, devfn, where, size, val);
}

static struct pci_ops mtk_pcie_ops_v2 = {
	.read  = mtk_pcie_config_read,
	.write = mtk_pcie_config_write,
};

static int mtk_pcie_startup_port_v2(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct resource *mem = &pcie->mem;
	u32 val;
	size_t size;
	int err;

	/* MT7622 platforms need to enable LTSSM and ASPM from PCIe subsys */
	if (pcie->base) {
		val = readl(pcie->base + PCIE_SYS_CFG_V2);
		val |= PCIE_CSR_LTSSM_EN(port->slot) |
		       PCIE_CSR_ASPM_L1_EN(port->slot);
		writel(val, pcie->base + PCIE_SYS_CFG_V2);
	}

	/* Assert all reset signals */
	writel(0, port->base + PCIE_RST_CTRL);

	/*
	 * Enable PCIe link down reset, if link status changed from link up to
	 * link down, this will reset MAC control registers and configuration
	 * space.
	 */
	writel(PCIE_LINKDOWN_RST_EN, port->base + PCIE_RST_CTRL);

	/* De-assert PHY, PE, PIPE, MAC and configuration reset	*/
	val = readl(port->base + PCIE_RST_CTRL);
	val |= PCIE_PHY_RSTB | PCIE_PERSTB | PCIE_PIPE_SRSTB |
	       PCIE_MAC_SRSTB | PCIE_CRSTB;
	writel(val, port->base + PCIE_RST_CTRL);

	/* 100ms timeout value should be enough for Gen1/2 training */
	err = readl_poll_timeout(port->base + PCIE_LINK_STATUS_V2, val,
				 !!(val & PCIE_PORT_LINKUP_V2), 20,
				 100 * USEC_PER_MSEC);
	if (err)
		return -ETIMEDOUT;

	/* Set INTx mask */
	val = readl(port->base + PCIE_INT_MASK);
	val &= ~INTX_MASK;
	writel(val, port->base + PCIE_INT_MASK);

	/* Set AHB to PCIe translation windows */
	size = mem->end - mem->start;
	val = lower_32_bits(mem->start) | AHB2PCIE_SIZE(fls(size));
	writel(val, port->base + PCIE_AHB_TRANS_BASE0_L);

	val = upper_32_bits(mem->start);
	writel(val, port->base + PCIE_AHB_TRANS_BASE0_H);

	/* Set PCIe to AXI translation memory space.*/
	val = fls(0xffffffff) | WIN_ENABLE;
	writel(val, port->base + PCIE_AXI_WINDOW0);

	return 0;
}

static int mtk_pcie_msi_alloc(struct mtk_pcie_port *port)
{
	int msi;

	msi = find_first_zero_bit(port->msi_irq_in_use, MTK_MSI_IRQS_NUM);
	if (msi < MTK_MSI_IRQS_NUM)
		set_bit(msi, port->msi_irq_in_use);
	else
		return -ENOSPC;

	return msi;
}

static void mtk_pcie_msi_free(struct mtk_pcie_port *port, unsigned long hwirq)
{
	clear_bit(hwirq, port->msi_irq_in_use);
}

static int mtk_pcie_msi_setup_irq(struct msi_controller *chip,
				  struct pci_dev *pdev, struct msi_desc *desc)
{
	struct mtk_pcie_port *port;
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;
	phys_addr_t msg_addr;

	port = mtk_pcie_find_port(pdev->bus, pdev->devfn);
	if (!port)
		return -EINVAL;

	hwirq = mtk_pcie_msi_alloc(port);
	if (hwirq < 0)
		return hwirq;

	irq = irq_create_mapping(port->msi_domain, hwirq);
	if (!irq) {
		mtk_pcie_msi_free(port, hwirq);
		return -EINVAL;
	}

	chip->dev = &pdev->dev;

	irq_set_msi_desc(irq, desc);

	/* MT2712/MT7622 only support 32-bit MSI addresses */
	msg_addr = virt_to_phys(port->base + PCIE_MSI_VECTOR);
	msg.address_hi = 0;
	msg.address_lo = lower_32_bits(msg_addr);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static void mtk_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct pci_dev *pdev = to_pci_dev(chip->dev);
	struct irq_data *d = irq_get_irq_data(irq);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct mtk_pcie_port *port;

	port = mtk_pcie_find_port(pdev->bus, pdev->devfn);
	if (!port)
		return;

	irq_dispose_mapping(irq);
	mtk_pcie_msi_free(port, hwirq);
}

static struct msi_controller mtk_pcie_msi_chip = {
	.setup_irq = mtk_pcie_msi_setup_irq,
	.teardown_irq = mtk_msi_teardown_irq,
};

static struct irq_chip mtk_msi_irq_chip = {
	.name = "MTK PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static int mtk_pcie_msi_map(struct irq_domain *domain, unsigned int irq,
			    irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &mtk_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = mtk_pcie_msi_map,
};

static void mtk_pcie_enable_msi(struct mtk_pcie_port *port)
{
	u32 val;
	phys_addr_t msg_addr;

	msg_addr = virt_to_phys(port->base + PCIE_MSI_VECTOR);
	val = lower_32_bits(msg_addr);
	writel(val, port->base + PCIE_IMSI_ADDR);

	val = readl(port->base + PCIE_INT_MASK);
	val &= ~MSI_MASK;
	writel(val, port->base + PCIE_INT_MASK);
}

static int mtk_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = mtk_pcie_intx_map,
};

static int mtk_pcie_init_irq_domain(struct mtk_pcie_port *port,
				    struct device_node *node)
{
	struct device *dev = port->pcie->dev;
	struct device_node *pcie_intc_node;

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "no PCIe Intc node found\n");
		return -ENODEV;
	}

	port->irq_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						 &intx_domain_ops, port);
	if (!port->irq_domain) {
		dev_err(dev, "failed to get INTx IRQ domain\n");
		return -ENODEV;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		port->msi_domain = irq_domain_add_linear(node, MTK_MSI_IRQS_NUM,
							 &msi_domain_ops,
							 &mtk_pcie_msi_chip);
		if (!port->msi_domain) {
			dev_err(dev, "failed to create MSI IRQ domain\n");
			return -ENODEV;
		}
		mtk_pcie_enable_msi(port);
	}

	return 0;
}

static irqreturn_t mtk_pcie_intr_handler(int irq, void *data)
{
	struct mtk_pcie_port *port = (struct mtk_pcie_port *)data;
	unsigned long status;
	u32 virq;
	u32 bit = INTX_SHIFT;

	while ((status = readl(port->base + PCIE_INT_STATUS)) & INTX_MASK) {
		for_each_set_bit_from(bit, &status, PCI_NUM_INTX + INTX_SHIFT) {
			/* Clear the INTx */
			writel(1 << bit, port->base + PCIE_INT_STATUS);
			virq = irq_find_mapping(port->irq_domain,
						bit - INTX_SHIFT);
			generic_handle_irq(virq);
		}
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		while ((status = readl(port->base + PCIE_INT_STATUS)) & MSI_STATUS) {
			unsigned long imsi_status;

			while ((imsi_status = readl(port->base + PCIE_IMSI_STATUS))) {
				for_each_set_bit(bit, &imsi_status, MTK_MSI_IRQS_NUM) {
					/* Clear the MSI */
					writel(1 << bit, port->base + PCIE_IMSI_STATUS);
					virq = irq_find_mapping(port->msi_domain, bit);
					generic_handle_irq(virq);
				}
			}
			/* Clear MSI interrupt status */
			writel(MSI_STATUS, port->base + PCIE_INT_STATUS);
		}
	}

	return IRQ_HANDLED;
}

static int mtk_pcie_setup_irq(struct mtk_pcie_port *port,
			      struct device_node *node)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int err, irq;

	irq = platform_get_irq(pdev, port->slot);
	err = devm_request_irq(dev, irq, mtk_pcie_intr_handler,
			       IRQF_SHARED, "mtk-pcie", port);
	if (err) {
		dev_err(dev, "unable to request IRQ %d\n", irq);
		return err;
	}

	err = mtk_pcie_init_irq_domain(port, node);
	if (err) {
		dev_err(dev, "failed to init PCIe IRQ domain\n");
		return err;
	}

	return 0;
}

static void __iomem *mtk_pcie_map_bus(struct pci_bus *bus,
				      unsigned int devfn, int where)
{
	struct mtk_pcie *pcie = bus->sysdata;

	writel(PCIE_CONF_ADDR(where, PCI_FUNC(devfn), PCI_SLOT(devfn),
			      bus->number), pcie->base + PCIE_CFG_ADDR);

	return pcie->base + PCIE_CFG_DATA + (where & 3);
}

static struct pci_ops mtk_pcie_ops = {
	.map_bus = mtk_pcie_map_bus,
	.read  = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int mtk_pcie_startup_port(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	u32 func = PCI_FUNC(port->slot << 3);
	u32 slot = PCI_SLOT(port->slot << 3);
	u32 val;
	int err;

	/* assert port PERST_N */
	val = readl(pcie->base + PCIE_SYS_CFG);
	val |= PCIE_PORT_PERST(port->slot);
	writel(val, pcie->base + PCIE_SYS_CFG);

	/* de-assert port PERST_N */
	val = readl(pcie->base + PCIE_SYS_CFG);
	val &= ~PCIE_PORT_PERST(port->slot);
	writel(val, pcie->base + PCIE_SYS_CFG);

	/* 100ms timeout value should be enough for Gen1/2 training */
	err = readl_poll_timeout(port->base + PCIE_LINK_STATUS, val,
				 !!(val & PCIE_PORT_LINKUP), 20,
				 100 * USEC_PER_MSEC);
	if (err)
		return -ETIMEDOUT;

	/* enable interrupt */
	val = readl(pcie->base + PCIE_INT_ENABLE);
	val |= PCIE_PORT_INT_EN(port->slot);
	writel(val, pcie->base + PCIE_INT_ENABLE);

	/* map to all DDR region. We need to set it before cfg operation. */
	writel(PCIE_BAR_MAP_MAX | PCIE_BAR_ENABLE,
	       port->base + PCIE_BAR0_SETUP);

	/* configure class code and revision ID */
	writel(PCIE_CLASS_CODE | PCIE_REVISION_ID, port->base + PCIE_CLASS);

	/* configure FC credit */
	writel(PCIE_CONF_ADDR(PCIE_FC_CREDIT, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	val = readl(pcie->base + PCIE_CFG_DATA);
	val &= ~PCIE_FC_CREDIT_MASK;
	val |= PCIE_FC_CREDIT_VAL(0x806c);
	writel(PCIE_CONF_ADDR(PCIE_FC_CREDIT, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	writel(val, pcie->base + PCIE_CFG_DATA);

	/* configure RC FTS number to 250 when it leaves L0s */
	writel(PCIE_CONF_ADDR(PCIE_FTS_NUM, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	val = readl(pcie->base + PCIE_CFG_DATA);
	val &= ~PCIE_FTS_NUM_MASK;
	val |= PCIE_FTS_NUM_L0(0x50);
	writel(PCIE_CONF_ADDR(PCIE_FTS_NUM, func, slot, 0),
	       pcie->base + PCIE_CFG_ADDR);
	writel(val, pcie->base + PCIE_CFG_DATA);

	return 0;
}

static void mtk_pcie_enable_port(struct mtk_pcie_port *port)
{
	struct mtk_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	int err;

	err = clk_prepare_enable(port->sys_ck);
	if (err) {
		dev_err(dev, "failed to enable sys_ck%d clock\n", port->slot);
		goto err_sys_clk;
	}

	err = clk_prepare_enable(port->ahb_ck);
	if (err) {
		dev_err(dev, "failed to enable ahb_ck%d\n", port->slot);
		goto err_ahb_clk;
	}

	err = clk_prepare_enable(port->aux_ck);
	if (err) {
		dev_err(dev, "failed to enable aux_ck%d\n", port->slot);
		goto err_aux_clk;
	}

	err = clk_prepare_enable(port->axi_ck);
	if (err) {
		dev_err(dev, "failed to enable axi_ck%d\n", port->slot);
		goto err_axi_clk;
	}

	err = clk_prepare_enable(port->obff_ck);
	if (err) {
		dev_err(dev, "failed to enable obff_ck%d\n", port->slot);
		goto err_obff_clk;
	}

	err = clk_prepare_enable(port->pipe_ck);
	if (err) {
		dev_err(dev, "failed to enable pipe_ck%d\n", port->slot);
		goto err_pipe_clk;
	}

	reset_control_assert(port->reset);
	reset_control_deassert(port->reset);

	err = phy_init(port->phy);
	if (err) {
		dev_err(dev, "failed to initialize port%d phy\n", port->slot);
		goto err_phy_init;
	}

	err = phy_power_on(port->phy);
	if (err) {
		dev_err(dev, "failed to power on port%d phy\n", port->slot);
		goto err_phy_on;
	}

	if (!pcie->soc->startup(port))
		return;

	dev_info(dev, "Port%d link down\n", port->slot);

	phy_power_off(port->phy);
err_phy_on:
	phy_exit(port->phy);
err_phy_init:
	clk_disable_unprepare(port->pipe_ck);
err_pipe_clk:
	clk_disable_unprepare(port->obff_ck);
err_obff_clk:
	clk_disable_unprepare(port->axi_ck);
err_axi_clk:
	clk_disable_unprepare(port->aux_ck);
err_aux_clk:
	clk_disable_unprepare(port->ahb_ck);
err_ahb_clk:
	clk_disable_unprepare(port->sys_ck);
err_sys_clk:
	mtk_pcie_port_free(port);
}

static int mtk_pcie_parse_port(struct mtk_pcie *pcie,
			       struct device_node *node,
			       int slot)
{
	struct mtk_pcie_port *port;
	struct resource *regs;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	char name[10];
	int err;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	err = of_property_read_u32(node, "num-lanes", &port->lane);
	if (err) {
		dev_err(dev, "missing num-lanes property\n");
		return err;
	}

	snprintf(name, sizeof(name), "port%d", slot);
	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	port->base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(port->base)) {
		dev_err(dev, "failed to map port%d base\n", slot);
		return PTR_ERR(port->base);
	}

	snprintf(name, sizeof(name), "sys_ck%d", slot);
	port->sys_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->sys_ck)) {
		dev_err(dev, "failed to get sys_ck%d clock\n", slot);
		return PTR_ERR(port->sys_ck);
	}

	/* sys_ck might be divided into the following parts in some chips */
	snprintf(name, sizeof(name), "ahb_ck%d", slot);
	port->ahb_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->ahb_ck)) {
		if (PTR_ERR(port->ahb_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->ahb_ck = NULL;
	}

	snprintf(name, sizeof(name), "axi_ck%d", slot);
	port->axi_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->axi_ck)) {
		if (PTR_ERR(port->axi_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->axi_ck = NULL;
	}

	snprintf(name, sizeof(name), "aux_ck%d", slot);
	port->aux_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->aux_ck)) {
		if (PTR_ERR(port->aux_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->aux_ck = NULL;
	}

	snprintf(name, sizeof(name), "obff_ck%d", slot);
	port->obff_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->obff_ck)) {
		if (PTR_ERR(port->obff_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->obff_ck = NULL;
	}

	snprintf(name, sizeof(name), "pipe_ck%d", slot);
	port->pipe_ck = devm_clk_get(dev, name);
	if (IS_ERR(port->pipe_ck)) {
		if (PTR_ERR(port->pipe_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		port->pipe_ck = NULL;
	}

	snprintf(name, sizeof(name), "pcie-rst%d", slot);
	port->reset = devm_reset_control_get_optional_exclusive(dev, name);
	if (PTR_ERR(port->reset) == -EPROBE_DEFER)
		return PTR_ERR(port->reset);

	/* some platforms may use default PHY setting */
	snprintf(name, sizeof(name), "pcie-phy%d", slot);
	port->phy = devm_phy_optional_get(dev, name);
	if (IS_ERR(port->phy))
		return PTR_ERR(port->phy);

	port->slot = slot;
	port->pcie = pcie;

	if (pcie->soc->setup_irq) {
		err = pcie->soc->setup_irq(port, node);
		if (err)
			return err;
	}

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	return 0;
}

static int mtk_pcie_subsys_powerup(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *regs;
	int err;

	/* get shared registers, which are optional */
	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "subsys");
	if (regs) {
		pcie->base = devm_ioremap_resource(dev, regs);
		if (IS_ERR(pcie->base)) {
			dev_err(dev, "failed to map shared register\n");
			return PTR_ERR(pcie->base);
		}
	}

	pcie->free_ck = devm_clk_get(dev, "free_ck");
	if (IS_ERR(pcie->free_ck)) {
		if (PTR_ERR(pcie->free_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		pcie->free_ck = NULL;
	}

	if (dev->pm_domain) {
		pm_runtime_enable(dev);
		pm_runtime_get_sync(dev);
	}

	/* enable top level clock */
	err = clk_prepare_enable(pcie->free_ck);
	if (err) {
		dev_err(dev, "failed to enable free_ck\n");
		goto err_free_ck;
	}

	return 0;

err_free_ck:
	if (dev->pm_domain) {
		pm_runtime_put_sync(dev);
		pm_runtime_disable(dev);
	}

	return err;
}

static int mtk_pcie_setup(struct mtk_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node, *child;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct resource res;
	struct mtk_pcie_port *port, *tmp;
	int err;

	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	for_each_of_pci_range(&parser, &range) {
		err = of_pci_range_to_resource(&range, node, &res);
		if (err < 0)
			return err;

		switch (res.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			pcie->offset.io = res.start - range.pci_addr;

			memcpy(&pcie->pio, &res, sizeof(res));
			pcie->pio.name = node->full_name;

			pcie->io.start = range.cpu_addr;
			pcie->io.end = range.cpu_addr + range.size - 1;
			pcie->io.flags = IORESOURCE_MEM;
			pcie->io.name = "I/O";

			memcpy(&res, &pcie->io, sizeof(res));
			break;

		case IORESOURCE_MEM:
			pcie->offset.mem = res.start - range.pci_addr;

			memcpy(&pcie->mem, &res, sizeof(res));
			pcie->mem.name = "non-prefetchable";
			break;
		}
	}

	err = of_pci_parse_bus_range(node, &pcie->busn);
	if (err < 0) {
		dev_err(dev, "failed to parse bus ranges property: %d\n", err);
		pcie->busn.name = node->name;
		pcie->busn.start = 0;
		pcie->busn.end = 0xff;
		pcie->busn.flags = IORESOURCE_BUS;
	}

	for_each_available_child_of_node(node, child) {
		int slot;

		err = of_pci_get_devfn(child);
		if (err < 0) {
			dev_err(dev, "failed to parse devfn: %d\n", err);
			return err;
		}

		slot = PCI_SLOT(err);

		err = mtk_pcie_parse_port(pcie, child, slot);
		if (err)
			return err;
	}

	err = mtk_pcie_subsys_powerup(pcie);
	if (err)
		return err;

	/* enable each port, and then check link status */
	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		mtk_pcie_enable_port(port);

	/* power down PCIe subsys if slots are all empty (link down) */
	if (list_empty(&pcie->ports))
		mtk_pcie_subsys_powerdown(pcie);

	return 0;
}

static int mtk_pcie_request_resources(struct mtk_pcie *pcie)
{
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct list_head *windows = &host->windows;
	struct device *dev = pcie->dev;
	int err;

	pci_add_resource_offset(windows, &pcie->pio, pcie->offset.io);
	pci_add_resource_offset(windows, &pcie->mem, pcie->offset.mem);
	pci_add_resource(windows, &pcie->busn);

	err = devm_request_pci_bus_resources(dev, windows);
	if (err < 0)
		return err;

	pci_remap_iospace(&pcie->pio, pcie->io.start);

	return 0;
}

static int mtk_pcie_register_host(struct pci_host_bridge *host)
{
	struct mtk_pcie *pcie = pci_host_bridge_priv(host);
	struct pci_bus *child;
	int err;

	host->busnr = pcie->busn.start;
	host->dev.parent = pcie->dev;
	host->ops = pcie->soc->ops;
	host->map_irq = of_irq_parse_and_map_pci;
	host->swizzle_irq = pci_common_swizzle;
	host->sysdata = pcie;
	if (IS_ENABLED(CONFIG_PCI_MSI) && pcie->soc->has_msi)
		host->msi = &mtk_pcie_msi_chip;

	err = pci_scan_root_bus_bridge(host);
	if (err < 0)
		return err;

	pci_bus_size_bridges(host->bus);
	pci_bus_assign_resources(host->bus);

	list_for_each_entry(child, &host->bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(host->bus);

	return 0;
}

static int mtk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_pcie *pcie;
	struct pci_host_bridge *host;
	int err;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);

	pcie->dev = dev;
	pcie->soc = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	err = mtk_pcie_setup(pcie);
	if (err)
		return err;

	err = mtk_pcie_request_resources(pcie);
	if (err)
		goto put_resources;

	err = mtk_pcie_register_host(host);
	if (err)
		goto put_resources;

	return 0;

put_resources:
	if (!list_empty(&pcie->ports))
		mtk_pcie_put_resources(pcie);

	return err;
}

static const struct mtk_pcie_soc mtk_pcie_soc_v1 = {
	.ops = &mtk_pcie_ops,
	.startup = mtk_pcie_startup_port,
};

static const struct mtk_pcie_soc mtk_pcie_soc_v2 = {
	.has_msi = true,
	.ops = &mtk_pcie_ops_v2,
	.startup = mtk_pcie_startup_port_v2,
	.setup_irq = mtk_pcie_setup_irq,
};

static const struct of_device_id mtk_pcie_ids[] = {
	{ .compatible = "mediatek,mt2701-pcie", .data = &mtk_pcie_soc_v1 },
	{ .compatible = "mediatek,mt7623-pcie", .data = &mtk_pcie_soc_v1 },
	{ .compatible = "mediatek,mt2712-pcie", .data = &mtk_pcie_soc_v2 },
	{ .compatible = "mediatek,mt7622-pcie", .data = &mtk_pcie_soc_v2 },
	{},
};

static struct platform_driver mtk_pcie_driver = {
	.probe = mtk_pcie_probe,
	.driver = {
		.name = "mtk-pcie",
		.of_match_table = mtk_pcie_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(mtk_pcie_driver);
