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
#include <linux/module.h>
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

/* Number of Memory Resources */
#define XILINX_MAX_NUM_RESOURCES	3

/**
 * struct xilinx_pcie_port - PCIe port information
 * @reg_base: IO Mapped Register Base
 * @irq: Interrupt number
 * @msi_pages: MSI pages
 * @root_busno: Root Bus number
 * @dev: Device pointer
 * @irq_domain: IRQ domain pointer
 * @bus_range: Bus range
 * @resources: Bus Resources
 */
struct xilinx_pcie_port {
	void __iomem *reg_base;
	u32 irq;
	unsigned long msi_pages;
	u8 root_busno;
	struct device *dev;
	struct irq_domain *irq_domain;
	struct resource bus_range;
	struct list_head resources;
};

static DECLARE_BITMAP(msi_irq_in_use, XILINX_NUM_MSI_IRQS);

static inline struct xilinx_pcie_port *sys_to_pcie(struct pci_sys_data *sys)
{
	return sys->private_data;
}

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
	u32 val = pcie_read(port, XILINX_PCIE_REG_RPEFR);

	if (val & XILINX_PCIE_RPEFR_ERR_VALID) {
		dev_dbg(port->dev, "Requester ID %d\n",
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
	struct xilinx_pcie_port *port = sys_to_pcie(bus->sysdata);

	/* Check if link is up when trying to access downstream ports */
	if (bus->number != port->root_busno)
		if (!xilinx_pcie_link_is_up(port))
			return false;

	/* Only one device down on each root port */
	if (bus->number == port->root_busno && devfn > 0)
		return false;

	/*
	 * Do not read more than one device on the bus directly attached
	 * to RC.
	 */
	if (bus->primary == port->root_busno && devfn > 0)
		return false;

	return true;
}

/**
 * xilinx_pcie_config_base - Get configuration base
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 *
 * Return: Base address of the configuration space needed to be
 *	   accessed.
 */
static void __iomem *xilinx_pcie_config_base(struct pci_bus *bus,
					     unsigned int devfn, int where)
{
	struct xilinx_pcie_port *port = sys_to_pcie(bus->sysdata);
	int relbus;

	relbus = (bus->number << ECAM_BUS_NUM_SHIFT) |
		 (devfn << ECAM_DEV_NUM_SHIFT);

	return port->reg_base + relbus + where;
}

/**
 * xilinx_pcie_read_config - Read configuration space
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be read
 *
 * Return: PCIBIOS_SUCCESSFUL on success
 *	   PCIBIOS_DEVICE_NOT_FOUND on failure
 */
static int xilinx_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	void __iomem *addr;

	if (!xilinx_pcie_valid_device(bus, devfn)) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	addr = xilinx_pcie_config_base(bus, devfn, where);

	switch (size) {
	case 1:
		*val = readb(addr);
		break;
	case 2:
		*val = readw(addr);
		break;
	default:
		*val = readl(addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

/**
 * xilinx_pcie_write_config - Write configuration space
 * @bus: PCI Bus structure
 * @devfn: Device/function
 * @where: Offset from base
 * @size: Byte/word/dword
 * @val: Value to be written to device
 *
 * Return: PCIBIOS_SUCCESSFUL on success
 *	   PCIBIOS_DEVICE_NOT_FOUND on failure
 */
static int xilinx_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 val)
{
	void __iomem *addr;

	if (!xilinx_pcie_valid_device(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = xilinx_pcie_config_base(bus, devfn, where);

	switch (size) {
	case 1:
		writeb(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

/* PCIe operations */
static struct pci_ops xilinx_pcie_ops = {
	.read  = xilinx_pcie_read_config,
	.write = xilinx_pcie_write_config,
};

/* MSI functions */

/**
 * xilinx_pcie_destroy_msi - Free MSI number
 * @irq: IRQ to be freed
 */
static void xilinx_pcie_destroy_msi(unsigned int irq)
{
	struct irq_desc *desc;
	struct msi_desc *msi;
	struct xilinx_pcie_port *port;

	desc = irq_to_desc(irq);
	msi = irq_desc_get_msi_desc(desc);
	port = sys_to_pcie(msi->dev->bus->sysdata);

	if (!test_bit(irq, msi_irq_in_use))
		dev_err(port->dev, "Trying to free unused MSI#%d\n", irq);
	else
		clear_bit(irq, msi_irq_in_use);
}

/**
 * xilinx_pcie_assign_msi - Allocate MSI number
 * @port: PCIe port structure
 *
 * Return: A valid IRQ on success and error value on failure.
 */
static int xilinx_pcie_assign_msi(struct xilinx_pcie_port *port)
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
	struct xilinx_pcie_port *port = sys_to_pcie(pdev->bus->sysdata);
	unsigned int irq;
	int hwirq;
	struct msi_msg msg;
	phys_addr_t msg_addr;

	hwirq = xilinx_pcie_assign_msi(port);
	if (hwirq < 0)
		return hwirq;

	irq = irq_create_mapping(port->irq_domain, hwirq);
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
	set_irq_flags(irq, IRQF_VALID);

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
	set_irq_flags(irq, IRQF_VALID);

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
	u32 val, mask, status, msi_data;

	/* Read interrupt decode and mask registers */
	val = pcie_read(port, XILINX_PCIE_REG_IDR);
	mask = pcie_read(port, XILINX_PCIE_REG_IMR);

	status = val & mask;
	if (!status)
		return IRQ_NONE;

	if (status & XILINX_PCIE_INTR_LINK_DOWN)
		dev_warn(port->dev, "Link Down\n");

	if (status & XILINX_PCIE_INTR_ECRC_ERR)
		dev_warn(port->dev, "ECRC failed\n");

	if (status & XILINX_PCIE_INTR_STR_ERR)
		dev_warn(port->dev, "Streaming error\n");

	if (status & XILINX_PCIE_INTR_HOT_RESET)
		dev_info(port->dev, "Hot reset\n");

	if (status & XILINX_PCIE_INTR_CFG_TIMEOUT)
		dev_warn(port->dev, "ECAM access timeout\n");

	if (status & XILINX_PCIE_INTR_CORRECTABLE) {
		dev_warn(port->dev, "Correctable error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_NONFATAL) {
		dev_warn(port->dev, "Non fatal error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_FATAL) {
		dev_warn(port->dev, "Fatal error message\n");
		xilinx_pcie_clear_err_interrupts(port);
	}

	if (status & XILINX_PCIE_INTR_INTX) {
		/* INTx interrupt received */
		val = pcie_read(port, XILINX_PCIE_REG_RPIFR1);

		/* Check whether interrupt valid */
		if (!(val & XILINX_PCIE_RPIFR1_INTR_VALID)) {
			dev_warn(port->dev, "RP Intr FIFO1 read error\n");
			return IRQ_HANDLED;
		}

		/* Clear interrupt FIFO register 1 */
		pcie_write(port, XILINX_PCIE_RPIFR1_ALL_MASK,
			   XILINX_PCIE_REG_RPIFR1);

		/* Handle INTx Interrupt */
		val = ((val & XILINX_PCIE_RPIFR1_INTR_MASK) >>
			XILINX_PCIE_RPIFR1_INTR_SHIFT) + 1;
		generic_handle_irq(irq_find_mapping(port->irq_domain, val));
	}

	if (status & XILINX_PCIE_INTR_MSI) {
		/* MSI Interrupt */
		val = pcie_read(port, XILINX_PCIE_REG_RPIFR1);

		if (!(val & XILINX_PCIE_RPIFR1_INTR_VALID)) {
			dev_warn(port->dev, "RP Intr FIFO1 read error\n");
			return IRQ_HANDLED;
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
		dev_warn(port->dev, "Slave unsupported request\n");

	if (status & XILINX_PCIE_INTR_SLV_UNEXP)
		dev_warn(port->dev, "Slave unexpected completion\n");

	if (status & XILINX_PCIE_INTR_SLV_COMPL)
		dev_warn(port->dev, "Slave completion timeout\n");

	if (status & XILINX_PCIE_INTR_SLV_ERRP)
		dev_warn(port->dev, "Slave Error Poison\n");

	if (status & XILINX_PCIE_INTR_SLV_CMPABT)
		dev_warn(port->dev, "Slave Completer Abort\n");

	if (status & XILINX_PCIE_INTR_SLV_ILLBUR)
		dev_warn(port->dev, "Slave Illegal Burst\n");

	if (status & XILINX_PCIE_INTR_MST_DECERR)
		dev_warn(port->dev, "Master decode error\n");

	if (status & XILINX_PCIE_INTR_MST_SLVERR)
		dev_warn(port->dev, "Master slave error\n");

	if (status & XILINX_PCIE_INTR_MST_ERRP)
		dev_warn(port->dev, "Master error poison\n");

	/* Clear the Interrupt Decode register */
	pcie_write(port, status, XILINX_PCIE_REG_IDR);

	return IRQ_HANDLED;
}

/**
 * xilinx_pcie_free_irq_domain - Free IRQ domain
 * @port: PCIe port information
 */
static void xilinx_pcie_free_irq_domain(struct xilinx_pcie_port *port)
{
	int i;
	u32 irq, num_irqs;

	/* Free IRQ Domain */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {

		free_pages(port->msi_pages, 0);

		num_irqs = XILINX_NUM_MSI_IRQS;
	} else {
		/* INTx */
		num_irqs = 4;
	}

	for (i = 0; i < num_irqs; i++) {
		irq = irq_find_mapping(port->irq_domain, i);
		if (irq > 0)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(port->irq_domain);
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
		return PTR_ERR(pcie_intc_node);
	}

	port->irq_domain = irq_domain_add_linear(pcie_intc_node, 4,
						 &intx_domain_ops,
						 port);
	if (!port->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return PTR_ERR(port->irq_domain);
	}

	/* Setup MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		port->irq_domain = irq_domain_add_linear(node,
							 XILINX_NUM_MSI_IRQS,
							 &msi_domain_ops,
							 &xilinx_pcie_msi_chip);
		if (!port->irq_domain) {
			dev_err(dev, "Failed to get a MSI IRQ domain\n");
			return PTR_ERR(port->irq_domain);
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
	if (xilinx_pcie_link_is_up(port))
		dev_info(port->dev, "PCIe Link is UP\n");
	else
		dev_info(port->dev, "PCIe Link is DOWN\n");

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
 * xilinx_pcie_setup - Setup memory resources
 * @nr: Bus number
 * @sys: Per controller structure
 *
 * Return: '1' on success and error value on failure
 */
static int xilinx_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct xilinx_pcie_port *port = sys_to_pcie(sys);

	list_splice_init(&port->resources, &sys->resources);

	return 1;
}

/**
 * xilinx_pcie_scan_bus - Scan PCIe bus for devices
 * @nr: Bus number
 * @sys: Per controller structure
 *
 * Return: Valid Bus pointer on success and NULL on failure
 */
static struct pci_bus *xilinx_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct xilinx_pcie_port *port = sys_to_pcie(sys);
	struct pci_bus *bus;

	port->root_busno = sys->busnr;
	bus = pci_scan_root_bus(port->dev, sys->busnr, &xilinx_pcie_ops,
				sys, &sys->resources);

	return bus;
}

/**
 * xilinx_pcie_parse_and_add_res - Add resources by parsing ranges
 * @port: PCIe port information
 *
 * Return: '0' on success and error value on failure
 */
static int xilinx_pcie_parse_and_add_res(struct xilinx_pcie_port *port)
{
	struct device *dev = port->dev;
	struct device_node *node = dev->of_node;
	struct resource *mem;
	resource_size_t offset;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	struct pci_host_bridge_window *win;
	int err = 0, mem_resno = 0;

	/* Get the ranges */
	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	/* Parse the ranges and add the resources found to the list */
	for_each_of_pci_range(&parser, &range) {

		if (mem_resno >= XILINX_MAX_NUM_RESOURCES) {
			dev_err(dev, "Maximum memory resources exceeded\n");
			return -EINVAL;
		}

		mem = devm_kmalloc(dev, sizeof(*mem), GFP_KERNEL);
		if (!mem) {
			err = -ENOMEM;
			goto free_resources;
		}

		of_pci_range_to_resource(&range, node, mem);

		switch (mem->flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_MEM:
			offset = range.cpu_addr - range.pci_addr;
			mem_resno++;
			break;
		default:
			err = -EINVAL;
			break;
		}

		if (err < 0) {
			dev_warn(dev, "Invalid resource found %pR\n", mem);
			continue;
		}

		err = request_resource(&iomem_resource, mem);
		if (err)
			goto free_resources;

		pci_add_resource_offset(&port->resources, mem, offset);
	}

	/* Get the bus range */
	if (of_pci_parse_bus_range(node, &port->bus_range)) {
		u32 val = pcie_read(port, XILINX_PCIE_REG_BIR);
		u8 last;

		last = (val & XILINX_PCIE_BIR_ECAM_SZ_MASK) >>
			XILINX_PCIE_BIR_ECAM_SZ_SHIFT;

		port->bus_range = (struct resource) {
			.name	= node->name,
			.start	= 0,
			.end	= last,
			.flags	= IORESOURCE_BUS,
		};
	}

	/* Register bus resource */
	pci_add_resource(&port->resources, &port->bus_range);

	return 0;

free_resources:
	release_child_resources(&iomem_resource);
	list_for_each_entry(win, &port->resources, list)
		devm_kfree(dev, win->res);
	pci_free_resource_list(&port->resources);

	return err;
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
			       IRQF_SHARED, "xilinx-pcie", port);
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
	struct xilinx_pcie_port *port;
	struct hw_pci hw;
	struct device *dev = &pdev->dev;
	int err;

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

	/*
	 * Parse PCI ranges, configuration bus range and
	 * request their resources
	 */
	INIT_LIST_HEAD(&port->resources);
	err = xilinx_pcie_parse_and_add_res(port);
	if (err) {
		dev_err(dev, "Failed adding resources\n");
		return err;
	}

	platform_set_drvdata(pdev, port);

	/* Register the device */
	memset(&hw, 0, sizeof(hw));
	hw = (struct hw_pci) {
		.nr_controllers	= 1,
		.private_data	= (void **)&port,
		.setup		= xilinx_pcie_setup,
		.map_irq	= of_irq_parse_and_map_pci,
		.scan		= xilinx_pcie_scan_bus,
		.ops		= &xilinx_pcie_ops,
	};

#ifdef CONFIG_PCI_MSI
	xilinx_pcie_msi_chip.dev = port->dev;
	hw.msi_ctrl = &xilinx_pcie_msi_chip;
#endif
	pci_common_init_dev(dev, &hw);

	return 0;
}

/**
 * xilinx_pcie_remove - Remove function
 * @pdev: Platform device pointer
 *
 * Return: '0' always
 */
static int xilinx_pcie_remove(struct platform_device *pdev)
{
	struct xilinx_pcie_port *port = platform_get_drvdata(pdev);

	xilinx_pcie_free_irq_domain(port);

	return 0;
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
	.remove = xilinx_pcie_remove,
};
module_platform_driver(xilinx_pcie_driver);

MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Xilinx AXI PCIe driver");
MODULE_LICENSE("GPL v2");
