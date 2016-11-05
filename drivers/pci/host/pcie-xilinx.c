/*
 * PCIe host controller driver for Xilinx AXI PCIe Bridge
 *
 * Copyright (c) 2012 - 2014 Xilinx, Inc.
 *
 * Based on the Tegra PCIe driver
 *
 * Bits taken from Synopsys Designware Host controller driver and
 * ARM PCI Host generic driver.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

/* Register definitions */
#define XILINX_PCIE_REG_BIR		0x00000130
#define XILINX_PCIE_REG_IDR		0x00000138
#define XILINX_PCIE_REG_IMR		0x0000013c
#define XILINX_PCIE_REG_PSCR		0x00000144
#define XILINX_PCIE_REG_RPSC		0x00000148
#define XILINX_PCIE_REG_MSIBASE1	0x0000014c
#define XILINX_PCIE_REG_MSIBASE2	0x00000150
#define XILINX_PCIE_REG_RPEFR		0x00000154
#define XILINX_PCIE_REG_RPIFR1		0x00000158
#define XILINX_PCIE_REG_RPIFR2		0x0000015c

/* Interrupt registers definitions */
#define XILINX_PCIE_INTR_LINK_DOWN	BIT(0)
#define XILINX_PCIE_INTR_ECRC_ERR	BIT(1)
#define XILINX_PCIE_INTR_STR_ERR	BIT(2)
#define XILINX_PCIE_INTR_HOT_RESET	BIT(3)
#define XILINX_PCIE_INTR_CFG_TIMEOUT	BIT(8)
#define XILINX_PCIE_INTR_CORRECTABLE	BIT(9)
#define XILINX_PCIE_INTR_NONFATAL	BIT(10)
#define XILINX_PCIE_INTR_FATAL		BIT(11)
#define XILINX_PCIE_INTR_INTX		BIT(16)
#define XILINX_PCIE_INTR_MSI		BIT(17)
#define XILINX_PCIE_INTR_SLV_UNSUPP	BIT(20)
#define XILINX_PCIE_INTR_SLV_UNEXP	BIT(21)
#define XILINX_PCIE_INTR_SLV_COMPL	BIT(22)
#define XILINX_PCIE_INTR_SLV_ERRP	BIT(23)
#define XILINX_PCIE_INTR_SLV_CMPABT	BIT(24)
#define XILINX_PCIE_INTR_SLV_ILLBUR	BIT(25)
#define XILINX_PCIE_INTR_MST_DECERR	BIT(26)
#define XILINX_PCIE_INTR_MST_SLVERR	BIT(27)
#define XILINX_PCIE_INTR_MST_ERRP	BIT(28)
#define XILINX_PCIE_IMR_ALL_MASK	0x1FF30FED
#define XILINX_PCIE_IDR_ALL_MASK	0xFFFFFFFF

/* Root Port Error FIFO Read Register definitions */
#define XILINX_PCIE_RPEFR_ERR_VALID	BIT(18)
#define XILINX_PCIE_RPEFR_REQ_ID	GENMASK(15, 0)
#define XILINX_PCIE_RPEFR_ALL_MASK	0xFFFFFFFF

/* Root Port Interrupt FIFO Read Register 1 definitions */
#define XILINX_PCIE_RPIFR1_INTR_VALID	BIT(31)
#define XILINX_PCIE_RPIFR1_MSI_INTR	BIT(30)
#define XILINX_PCIE_RPIFR1_INTR_MASK	GENMASK(28, 27)
#define XILINX_PCIE_RPIFR1_ALL_MASK	0xFFFFFFFF
#define XILINX_PCIE_RPIFR1_INTR_SHIFT	27

/* Bridge Info Register definitions */
#define XILINX_PCIE_BIR_ECAM_SZ_MASK	GENMASK(18, 16)
#define XILINX_PCIE_BIR_ECAM_SZ_SHIFT	16

/* Root Port Interrupt FIFO Read Register 2 definitions */
#define XILINX_PCIE_RPIFR2_MSG_DATA	GENMASK(15, 0)

/* Root Port Status/control Register definitions */
#define XILINX_PCIE_REG_RPSC_BEN	BIT(0)

/* Phy Status/Control Register definitions */
#define XILINX_PCIE_REG_PSCR_LNKUP	BIT(11)

/* ECAM definitions */
#define ECAM_BUS_NUM_SHIFT		20
#define ECAM_DEV_NUM_SHIFT		12

/* Number of MSI IRQs */
#define XILINX_NUM_MSI_IRQS		128

/**
 * struct xilinx_pcie_port - PCIe port information
 * @reg_base: IO Mapped Register Base
 * @irq: Interrupt number
 * @msi_pages: MSI pages
 * @root_busno: Root Bus number
 * @dev: Device pointer
 * @msi_domain: MSI IRQ domain pointer
 * @leg_domain: Legacy IRQ domain pointer
 * @resources: Bus Resources
 */
struct xilinx_pcie_port {
	void __iomem *reg_base;
	u32 irq;
	unsigned long msi_pages;
	u8 root_busno;
	struct device *dev;
	struct irq_domain *msi_domain;
	struct irq_domain *leg_domain;
	struct list_head resources;
};

static DECLARE_BITMAP(msi_irq_in_use, XILINX_NUM_MSI_IRQS);

static inline u32 pcie_read(struct xilinx_pcie_port *port, u32 reg)
{
	return readl(port->reg_base + reg);
}

static inline void pcie_write(struct xilinx_pcie_port *port, u32 val, u32 reg)
{
	writel(val, port->reg_base + reg);
}

static inline bool xilinx_pcie_link_is_up(struct xilinx_pcie_port *port)
{
	return (pcie_read(port, XILINX_PCIE_REG_PSCR) &
		XILINX_PCIE_REG_PSCR_LNKUP) ? 1 : 0;
}

/**
 * xilinx_pcie_clear_err_interrupts - Clear Error Interrupts
 * @port: PCIe port information
 */
static void xilinx_pcie_clear_err_interrupts(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	unsigned long val = pcie_read(port, XILINX_PCIE_REG_RPEFR);

	if (val & XILINX_PCIE_RPEFR_ERR_VALID) {
		dev_dbg(dev, "Requester ID %lu\n",
			val & XILINX_PCIE_RPEFR_REQ_ID);
		pcie_write(port, XILINX_PCIE_RPEFR_ALL_MASK,
			   XILINX_PCIE_REG_RPEFR);
	}
}

/**
 * xilinx_pcie_valid_device - Check if a valid device is present on bus
 * @bus: PCI Bus structure
 * @devfn: device/function
 *
 * Return: 'true' on success and 'false' if invalid device is found
 */
static bool xilinx_pcie_valid_device(struct pci_bus *bus, unsigned int devfn)
{
	struct xilinx_pcie_port *port = bus->sysdata;

	/* Check if link is up when trying to access downstream ports */
	if (bus->number != port->root_busno)
		if (!xilinx_pcie_link_is_up(port))
			return false;

	/* Only one device down on each root port */
	if (bus->number == port->root_busno && devfn > 0)
		return false;

	return true;
}

/**
 * xilinx_pcie_map_bus - Get configuration base
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 *
 * Return: Base address of the configuration space needed to be
 *	   accessed.
 */
static void __iomem *xilinx_pcie_map_bus(struct pci_bus *bus,
					 unsigned int devfn, int where)
{
	struct xilinx_pcie_port *port = bus->sysdata;
	int relbus;

	if (!xilinx_pcie_valid_device(bus, devfn))
		return NULL;

	relbus = (bus->number << ECAM_BUS_NUM_SHIFT) |
		 (devfn << ECAM_DEV_NUM_SHIFT);

	return port->reg_base + relbus + where;
}

/* PCIe operations */
static struct pci_ops xilinx_pcie_ops = {
	.map_bus = xilinx_pcie_map_bus,
	.read	= pci_generic_config_read,
	.write	= pci_generic_config_write,
};

/* MSI functions */

/**
 * xilinx_pcie_destroy_msi - Free MSI number
 * @irq: IRQ to be freed
 */
static void xilinx_pcie_destroy_msi(unsigned int irq)
{
	struct msi_desc *msi;
	struct xilinx_pcie_port *port;
	struct irq_data *d = irq_get_irq_data(irq);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	if (!test_bit(hwirq, msi_irq_in_use)) {
		msi = irq_get_msi_desc(irq);
		port = msi_desc_to_pci_sysdata(msi);
		dev_err(port->dev, "Trying to free unused MSI#%d\n", irq);
	} else {
		clear_bit(hwirq, msi_irq_in_use);
	}
}

/**
 * xilinx_pcie_assign_msi - Allocate MSI number
 *
 * Return: A valid IRQ on success and error value on failure.
 */
static int xilinx_pcie_assign_msi(void)
{
	int pos;

	pos = find_first_zero_bit(msi_irq_in_use, XILINX_NUM_MSI_IRQS);
	if (pos < XILINX_NUM_MSI_IRQS)
		set_bit(pos, msi_irq_in_use);
	else
		return -ENOSPC;

	return pos;
}

/**
 * xilinx_msi_teardown_irq - Destroy the MSI
 * @chip: MSI Chip descriptor
 * @irq: MSI IRQ to destroy
 */
static void xilinx_msi_teardown_irq(struct msi_controller *chip,
				    unsigned int irq)
{
	xilinx_pcie_destroy_msi(irq);
	irq_dispose_mapping(irq);
}

/**
 * xilinx_pcie_msi_setup_irq - Setup MSI request
 * @chip: MSI chip pointer
 * @pdev: PCIe device pointer
 * @desc: MSI descriptor pointer
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_msi_setup_irq(struct msi_controller *chip,
				     struct pci_dev *pdev,
				     struct msi_desc *desc)
{
	struct xilinx_pcie_port *port = pdev->bus->sysdata;
	unsigned int irq;
	int hwirq;
	struct msi_msg msg;
	phys_addr_t msg_addr;

	hwirq = xilinx_pcie_assign_msi();
	if (hwirq < 0)
		return hwirq;

	irq = irq_create_mapping(port->msi_domain, hwirq);
	if (!irq)
		return -EINVAL;

	irq_set_msi_desc(irq, desc);

	msg_addr = virt_to_phys((void *)port->msi_pages);

	msg.address_hi = 0;
	msg.address_lo = msg_addr;
	msg.data = irq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

/* MSI Chip Descriptor */
static struct msi_controller xilinx_pcie_msi_chip = {
	.setup_irq = xilinx_pcie_msi_setup_irq,
	.teardown_irq = xilinx_msi_teardown_irq,
};

/* HW Interrupt Chip Descriptor */
static struct irq_chip xilinx_msi_irq_chip = {
	.name = "Xilinx PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

/**
 * xilinx_pcie_msi_map - Set the handler for the MSI and mark IRQ as valid
 * @domain: IRQ domain
 * @irq: Virtual IRQ number
 * @hwirq: HW interrupt number
 *
 * Return: Always returns 0.
 */
static int xilinx_pcie_msi_map(struct irq_domain *domain, unsigned int irq,
			       irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &xilinx_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

/* IRQ Domain operations */
static const struct irq_domain_ops msi_domain_ops = {
	.map = xilinx_pcie_msi_map,
};

/**
 * xilinx_pcie_enable_msi - Enable MSI support
 * @port: PCIe port information
 */
static void xilinx_pcie_enable_msi(struct xilinx_pcie_port *port)
{
	phys_addr_t msg_addr;

	port->msi_pages = __get_free_pages(GFP_KERNEL, 0);
	msg_addr = virt_to_phys((void *)port->msi_pages);
	pcie_write(port, 0x0, XILINX_PCIE_REG_MSIBASE1);
	pcie_write(port, msg_addr, XILINX_PCIE_REG_MSIBASE2);
}

/* INTx Functions */

/**
 * xilinx_pcie_intx_map - Set the handler for the INTx and mark IRQ as valid
 * @domain: IRQ domain
 * @irq: Virtual IRQ number
 * @hwirq: HW interrupt number
 *
 * Return: Always returns 0.
 */
static int xilinx_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

/* INTx IRQ Domain operations */
static const struct irq_domain_ops intx_domain_ops = {
	.map = xilinx_pcie_intx_map,
};

/* PCIe HW Functions */

/**
 * xilinx_pcie_intr_handler - Interrupt Service Handler
 * @irq: IRQ number
 * @data: PCIe port information
 *
 * Return: IRQ_HANDLED on success and IRQ_NONE on failure
 */
static irqreturn_t xilinx_pcie_intr_handler(int irq, void *data)
{
	struct xilinx_pcie_port *port = (struct xilinx_pcie_port *)data;
	struct device *dev = port->dev;
	u32 val, mask, status, msi_data;

	/* Read interrupt decode and mask registers */
	val = pcie_read(port, XILINX_PCIE_REG_IDR);
	mask = pcie_read(port, XILINX_PCIE_REG_IMR);

	status = val & mask;
	if (!status)
		return IRQ_NONE;

	if (status & XILINX_PCIE_INTR_LINK_DOWN)
		dev_warn(dev, "Link Down\n");

	if (status & XILINX_PCIE_INTR_ECRC_ERR)
		dev_warn(dev, "ECRC failed\n");

	if (status & XILINX_PCIE_INTR_STR_ERR)
		dev_warn(dev, "Streaming error\n");

	if (status & XILINX_PCIE_INTR_HOT_RESET)
		dev_info(dev, "Hot reset\n");

	if (status & XILINX_PCIE_INTR_CFG_TIMEOUT)
		dev_warn(dev, "ECAM access timeout\n");

	if (status & XILINX_PCIE_INTR_CORRECTABLE) {
		dev_warn(dev, "Correctable error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_NONFATAL) {
		dev_warn(dev, "Non fatal error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_FATAL) {
		dev_warn(dev, "Fatal error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_INTX) {
		/* INTx interrupt received */
		val = pcie_read(port, XILINX_PCIE_REG_RPIFR1);

		/* Check whether interrupt valid */
		if (!(val & XILINX_PCIE_RPIFR1_INTR_VALID)) {
			dev_warn(dev, "RP Intr FIFO1 read error\n");
			goto error;
		}

		if (!(val & XILINX_PCIE_RPIFR1_MSI_INTR)) {
			/* Clear interrupt FIFO register 1 */
			pcie_write(port, XILINX_PCIE_RPIFR1_ALL_MASK,
				   XILINX_PCIE_REG_RPIFR1);

			/* Handle INTx Interrupt */
			val = ((val & XILINX_PCIE_RPIFR1_INTR_MASK) >>
				XILINX_PCIE_RPIFR1_INTR_SHIFT) + 1;
			generic_handle_irq(irq_find_mapping(port->leg_domain,
							    val));
		}
	}

	if (status & XILINX_PCIE_INTR_MSI) {
		/* MSI Interrupt */
		val = pcie_read(port, XILINX_PCIE_REG_RPIFR1);

		if (!(val & XILINX_PCIE_RPIFR1_INTR_VALID)) {
			dev_warn(dev, "RP Intr FIFO1 read error\n");
			goto error;
		}

		if (val & XILINX_PCIE_RPIFR1_MSI_INTR) {
			msi_data = pcie_read(port, XILINX_PCIE_REG_RPIFR2) &
				   XILINX_PCIE_RPIFR2_MSG_DATA;

			/* Clear interrupt FIFO register 1 */
			pcie_write(port, XILINX_PCIE_RPIFR1_ALL_MASK,
				   XILINX_PCIE_REG_RPIFR1);

			if (IS_ENABLED(CONFIG_PCI_MSI)) {
				/* Handle MSI Interrupt */
				generic_handle_irq(msi_data);
			}
		}
	}

	if (status & XILINX_PCIE_INTR_SLV_UNSUPP)
		dev_warn(dev, "Slave unsupported request\n");

	if (status & XILINX_PCIE_INTR_SLV_UNEXP)
		dev_warn(dev, "Slave unexpected completion\n");

	if (status & XILINX_PCIE_INTR_SLV_COMPL)
		dev_warn(dev, "Slave completion timeout\n");

	if (status & XILINX_PCIE_INTR_SLV_ERRP)
		dev_warn(dev, "Slave Error Poison\n");

	if (status & XILINX_PCIE_INTR_SLV_CMPABT)
		dev_warn(dev, "Slave Completer Abort\n");

	if (status & XILINX_PCIE_INTR_SLV_ILLBUR)
		dev_warn(dev, "Slave Illegal Burst\n");

	if (status & XILINX_PCIE_INTR_MST_DECERR)
		dev_warn(dev, "Master decode error\n");

	if (status & XILINX_PCIE_INTR_MST_SLVERR)
		dev_warn(dev, "Master slave error\n");

	if (status & XILINX_PCIE_INTR_MST_ERRP)
		dev_warn(dev, "Master error poison\n");

error:
	/* Clear the Interrupt Decode register */
	pcie_write(port, status, XILINX_PCIE_REG_IDR);

	return IRQ_HANDLED;
}

/**
 * xilinx_pcie_init_irq_domain - Initialize IRQ domain
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_init_irq_domain(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;

	/* Setup INTx */
	pcie_intc_node = of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	port->leg_domain = irq_domain_add_linear(pcie_intc_node, 4,
						 &intx_domain_ops,
						 port);
	if (!port->leg_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENODEV;
	}

	/* Setup MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		port->msi_domain = irq_domain_add_linear(node,
							 XILINX_NUM_MSI_IRQS,
							 &msi_domain_ops,
							 &xilinx_pcie_msi_chip);
		if (!port->msi_domain) {
			dev_err(dev, "Failed to get a MSI IRQ domain\n");
			return -ENODEV;
		}

		xilinx_pcie_enable_msi(port);
	}

	return 0;
}

/**
 * xilinx_pcie_init_port - Initialize hardware
 * @port: PCIe port information
 */
static void xilinx_pcie_init_port(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;

	if (xilinx_pcie_link_is_up(port))
		dev_info(dev, "PCIe Link is UP\n");
	else
		dev_info(dev, "PCIe Link is DOWN\n");

	/* Disable all interrupts */
	pcie_write(port, ~XILINX_PCIE_IDR_ALL_MASK,
		   XILINX_PCIE_REG_IMR);

	/* Clear pending interrupts */
	pcie_write(port, pcie_read(port, XILINX_PCIE_REG_IDR) &
			 XILINX_PCIE_IMR_ALL_MASK,
		   XILINX_PCIE_REG_IDR);

	/* Enable all interrupts */
	pcie_write(port, XILINX_PCIE_IMR_ALL_MASK, XILINX_PCIE_REG_IMR);

	/* Enable the Bridge enable bit */
	pcie_write(port, pcie_read(port, XILINX_PCIE_REG_RPSC) |
			 XILINX_PCIE_REG_RPSC_BEN,
		   XILINX_PCIE_REG_RPSC);
}

/**
 * xilinx_pcie_parse_dt - Parse Device tree
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_parse_dt(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct resource regs;
	const char *type;
	int err;

	type = of_get_property(node, "device_type", NULL);
	if (!type || strcmp(type, "pci")) {
		dev_err(dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

	err = of_address_to_resource(node, 0, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	port->reg_base = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(port->reg_base))
		return PTR_ERR(port->reg_base);

	port->irq = irq_of_parse_and_map(node, 0);
	err = devm_request_irq(dev, port->irq, xilinx_pcie_intr_handler,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       "xilinx-pcie", port);
	if (err) {
		dev_err(dev, "unable to request irq %d\n", port->irq);
		return err;
	}

	return 0;
}

/**
 * xilinx_pcie_probe - Probe function
 * @pdev: Platform device pointer
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct xilinx_pcie_port *port;
	struct pci_bus *bus;
	int err;
	resource_size_t iobase = 0;
	LIST_HEAD(res);

	if (!dev->of_node)
		return -ENODEV;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->dev = dev;

	err = xilinx_pcie_parse_dt(port);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	xilinx_pcie_init_port(port);

	err = xilinx_pcie_init_irq_domain(port);
	if (err) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return err;
	}

	err = of_pci_get_host_bridge_resources(dev->of_node, 0, 0xff, &res,
					       &iobase);
	if (err) {
		dev_err(dev, "Getting bridge resources failed\n");
		return err;
	}

	err = devm_request_pci_bus_resources(dev, &res);
	if (err)
		goto error;

	bus = pci_create_root_bus(dev, 0, &xilinx_pcie_ops, port, &res);
	if (!bus) {
		err = -ENOMEM;
		goto error;
	}

#ifdef CONFIG_PCI_MSI
	xilinx_pcie_msi_chip.dev = dev;
	bus->msi = &xilinx_pcie_msi_chip;
#endif
	pci_scan_child_bus(bus);
	pci_assign_unassigned_bus_resources(bus);
#ifndef CONFIG_MICROBLAZE
	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);
#endif
	pci_bus_add_devices(bus);
	return 0;

error:
	pci_free_resource_list(&res);
	return err;
}

static struct of_device_id xilinx_pcie_of_match[] = {
	{ .compatible = "xlnx,axi-pcie-host-1.00.a", },
	{}
};

static struct platform_driver xilinx_pcie_driver = {
	.driver = {
		.name = "xilinx-pcie",
		.of_match_table = xilinx_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = xilinx_pcie_probe,
};
builtin_platform_driver(xilinx_pcie_driver);
