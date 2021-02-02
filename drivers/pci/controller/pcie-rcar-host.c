// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2014-2020 Renesas Electronics Europe Ltd
 *
 * Based on:
 *  arch/sh/drivers/pci/pcie-sh7786.c
 *  arch/sh/drivers/pci/ops-sh7786.c
 *  Copyright (C) 2009 - 2011  Paul Mundt
 *
 * Author: Phil Edworthy <phil.edworthy@renesas.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "pcie-rcar.h"

struct rcar_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	struct msi_controller chip;
	unsigned long pages;
	struct mutex lock;
	int irq1;
	int irq2;
};

static inline struct rcar_msi *to_rcar_msi(struct msi_controller *chip)
{
	return container_of(chip, struct rcar_msi, chip);
}

/* Structure representing the PCIe interface */
struct rcar_pcie_host {
	struct rcar_pcie	pcie;
	struct phy		*phy;
	struct clk		*bus_clk;
	struct			rcar_msi msi;
	int			(*phy_init_fn)(struct rcar_pcie_host *host);
};

static u32 rcar_read_conf(struct rcar_pcie *pcie, int where)
{
	unsigned int shift = BITS_PER_BYTE * (where & 3);
	u32 val = rcar_pci_read_reg(pcie, where & ~3);

	return val >> shift;
}

/* Serialization is provided by 'pci_lock' in drivers/pci/access.c */
static int rcar_pcie_config_access(struct rcar_pcie_host *host,
		unsigned char access_type, struct pci_bus *bus,
		unsigned int devfn, int where, u32 *data)
{
	struct rcar_pcie *pcie = &host->pcie;
	unsigned int dev, func, reg, index;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);
	reg = where & ~3;
	index = reg / 4;

	/*
	 * While each channel has its own memory-mapped extended config
	 * space, it's generally only accessible when in endpoint mode.
	 * When in root complex mode, the controller is unable to target
	 * itself with either type 0 or type 1 accesses, and indeed, any
	 * controller initiated target transfer to its own config space
	 * result in a completer abort.
	 *
	 * Each channel effectively only supports a single device, but as
	 * the same channel <-> device access works for any PCI_SLOT()
	 * value, we cheat a bit here and bind the controller's config
	 * space to devfn 0 in order to enable self-enumeration. In this
	 * case the regular ECAR/ECDR path is sidelined and the mangled
	 * config access itself is initiated as an internal bus transaction.
	 */
	if (pci_is_root_bus(bus)) {
		if (dev != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;

		if (access_type == RCAR_PCI_ACCESS_READ)
			*data = rcar_pci_read_reg(pcie, PCICONF(index));
		else
			rcar_pci_write_reg(pcie, *data, PCICONF(index));

		return PCIBIOS_SUCCESSFUL;
	}

	/* Clear errors */
	rcar_pci_write_reg(pcie, rcar_pci_read_reg(pcie, PCIEERRFR), PCIEERRFR);

	/* Set the PIO address */
	rcar_pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) |
		PCIE_CONF_DEV(dev) | PCIE_CONF_FUNC(func) | reg, PCIECAR);

	/* Enable the configuration access */
	if (pci_is_root_bus(bus->parent))
		rcar_pci_write_reg(pcie, CONFIG_SEND_ENABLE | TYPE0, PCIECCTLR);
	else
		rcar_pci_write_reg(pcie, CONFIG_SEND_ENABLE | TYPE1, PCIECCTLR);

	/* Check for errors */
	if (rcar_pci_read_reg(pcie, PCIEERRFR) & UNSUPPORTED_REQUEST)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check for master and target aborts */
	if (rcar_read_conf(pcie, RCONF(PCI_STATUS)) &
		(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (access_type == RCAR_PCI_ACCESS_READ)
		*data = rcar_pci_read_reg(pcie, PCIECDR);
	else
		rcar_pci_write_reg(pcie, *data, PCIECDR);

	/* Disable the configuration access */
	rcar_pci_write_reg(pcie, 0, PCIECCTLR);

	return PCIBIOS_SUCCESSFUL;
}

static int rcar_pcie_read_conf(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 *val)
{
	struct rcar_pcie_host *host = bus->sysdata;
	int ret;

	ret = rcar_pcie_config_access(host, RCAR_PCI_ACCESS_READ,
				      bus, devfn, where, val);
	if (ret != PCIBIOS_SUCCESSFUL) {
		*val = 0xffffffff;
		return ret;
	}

	if (size == 1)
		*val = (*val >> (BITS_PER_BYTE * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (BITS_PER_BYTE * (where & 2))) & 0xffff;

	dev_dbg(&bus->dev, "pcie-config-read: bus=%3d devfn=0x%04x where=0x%04x size=%d val=0x%08x\n",
		bus->number, devfn, where, size, *val);

	return ret;
}

/* Serialization is provided by 'pci_lock' in drivers/pci/access.c */
static int rcar_pcie_write_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	struct rcar_pcie_host *host = bus->sysdata;
	unsigned int shift;
	u32 data;
	int ret;

	ret = rcar_pcie_config_access(host, RCAR_PCI_ACCESS_READ,
				      bus, devfn, where, &data);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	dev_dbg(&bus->dev, "pcie-config-write: bus=%3d devfn=0x%04x where=0x%04x size=%d val=0x%08x\n",
		bus->number, devfn, where, size, val);

	if (size == 1) {
		shift = BITS_PER_BYTE * (where & 3);
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
	} else if (size == 2) {
		shift = BITS_PER_BYTE * (where & 2);
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
	} else
		data = val;

	ret = rcar_pcie_config_access(host, RCAR_PCI_ACCESS_WRITE,
				      bus, devfn, where, &data);

	return ret;
}

static struct pci_ops rcar_pcie_ops = {
	.read	= rcar_pcie_read_conf,
	.write	= rcar_pcie_write_conf,
};

static void rcar_pcie_force_speedup(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	unsigned int timeout = 1000;
	u32 macsr;

	if ((rcar_pci_read_reg(pcie, MACS2R) & LINK_SPEED) != LINK_SPEED_5_0GTS)
		return;

	if (rcar_pci_read_reg(pcie, MACCTLR) & SPEED_CHANGE) {
		dev_err(dev, "Speed change already in progress\n");
		return;
	}

	macsr = rcar_pci_read_reg(pcie, MACSR);
	if ((macsr & LINK_SPEED) == LINK_SPEED_5_0GTS)
		goto done;

	/* Set target link speed to 5.0 GT/s */
	rcar_rmw32(pcie, EXPCAP(12), PCI_EXP_LNKSTA_CLS,
		   PCI_EXP_LNKSTA_CLS_5_0GB);

	/* Set speed change reason as intentional factor */
	rcar_rmw32(pcie, MACCGSPSETR, SPCNGRSN, 0);

	/* Clear SPCHGFIN, SPCHGSUC, and SPCHGFAIL */
	if (macsr & (SPCHGFIN | SPCHGSUC | SPCHGFAIL))
		rcar_pci_write_reg(pcie, macsr, MACSR);

	/* Start link speed change */
	rcar_rmw32(pcie, MACCTLR, SPEED_CHANGE, SPEED_CHANGE);

	while (timeout--) {
		macsr = rcar_pci_read_reg(pcie, MACSR);
		if (macsr & SPCHGFIN) {
			/* Clear the interrupt bits */
			rcar_pci_write_reg(pcie, macsr, MACSR);

			if (macsr & SPCHGFAIL)
				dev_err(dev, "Speed change failed\n");

			goto done;
		}

		msleep(1);
	}

	dev_err(dev, "Speed change timed out\n");

done:
	dev_info(dev, "Current link speed is %s GT/s\n",
		 (macsr & LINK_SPEED) == LINK_SPEED_5_0GTS ? "5" : "2.5");
}

static void rcar_pcie_hw_enable(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(host);
	struct resource_entry *win;
	LIST_HEAD(res);
	int i = 0;

	/* Try setting 5 GT/s link speed */
	rcar_pcie_force_speedup(pcie);

	/* Setup PCI resources */
	resource_list_for_each_entry(win, &bridge->windows) {
		struct resource *res = win->res;

		if (!res->flags)
			continue;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
		case IORESOURCE_MEM:
			rcar_pcie_set_outbound(pcie, i, win);
			i++;
			break;
		}
	}
}

static int rcar_pcie_enable(struct rcar_pcie_host *host)
{
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(host);

	rcar_pcie_hw_enable(host);

	pci_add_flags(PCI_REASSIGN_ALL_BUS);

	bridge->sysdata = host;
	bridge->ops = &rcar_pcie_ops;
	if (IS_ENABLED(CONFIG_PCI_MSI))
		bridge->msi = &host->msi.chip;

	return pci_host_probe(bridge);
}

static int phy_wait_for_ack(struct rcar_pcie *pcie)
{
	struct device *dev = pcie->dev;
	unsigned int timeout = 100;

	while (timeout--) {
		if (rcar_pci_read_reg(pcie, H1_PCIEPHYADRR) & PHY_ACK)
			return 0;

		udelay(100);
	}

	dev_err(dev, "Access to PCIe phy timed out\n");

	return -ETIMEDOUT;
}

static void phy_write_reg(struct rcar_pcie *pcie,
			  unsigned int rate, u32 addr,
			  unsigned int lane, u32 data)
{
	u32 phyaddr;

	phyaddr = WRITE_CMD |
		((rate & 1) << RATE_POS) |
		((lane & 0xf) << LANE_POS) |
		((addr & 0xff) << ADR_POS);

	/* Set write data */
	rcar_pci_write_reg(pcie, data, H1_PCIEPHYDOUTR);
	rcar_pci_write_reg(pcie, phyaddr, H1_PCIEPHYADRR);

	/* Ignore errors as they will be dealt with if the data link is down */
	phy_wait_for_ack(pcie);

	/* Clear command */
	rcar_pci_write_reg(pcie, 0, H1_PCIEPHYDOUTR);
	rcar_pci_write_reg(pcie, 0, H1_PCIEPHYADRR);

	/* Ignore errors as they will be dealt with if the data link is down */
	phy_wait_for_ack(pcie);
}

static int rcar_pcie_hw_init(struct rcar_pcie *pcie)
{
	int err;

	/* Begin initialization */
	rcar_pci_write_reg(pcie, 0, PCIETCTLR);

	/* Set mode */
	rcar_pci_write_reg(pcie, 1, PCIEMSR);

	err = rcar_pcie_wait_for_phyrdy(pcie);
	if (err)
		return err;

	/*
	 * Initial header for port config space is type 1, set the device
	 * class to match. Hardware takes care of propagating the IDSETR
	 * settings, so there is no need to bother with a quirk.
	 */
	rcar_pci_write_reg(pcie, PCI_CLASS_BRIDGE_PCI << 16, IDSETR1);

	/*
	 * Setup Secondary Bus Number & Subordinate Bus Number, even though
	 * they aren't used, to avoid bridge being detected as broken.
	 */
	rcar_rmw32(pcie, RCONF(PCI_SECONDARY_BUS), 0xff, 1);
	rcar_rmw32(pcie, RCONF(PCI_SUBORDINATE_BUS), 0xff, 1);

	/* Initialize default capabilities. */
	rcar_rmw32(pcie, REXPCAP(0), 0xff, PCI_CAP_ID_EXP);
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_FLAGS),
		PCI_EXP_FLAGS_TYPE, PCI_EXP_TYPE_ROOT_PORT << 4);
	rcar_rmw32(pcie, RCONF(PCI_HEADER_TYPE), 0x7f,
		PCI_HEADER_TYPE_BRIDGE);

	/* Enable data link layer active state reporting */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_LNKCAP), PCI_EXP_LNKCAP_DLLLARC,
		PCI_EXP_LNKCAP_DLLLARC);

	/* Write out the physical slot number = 0 */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_SLTCAP), PCI_EXP_SLTCAP_PSN, 0);

	/* Set the completion timer timeout to the maximum 50ms. */
	rcar_rmw32(pcie, TLCTLR + 1, 0x3f, 50);

	/* Terminate list of capabilities (Next Capability Offset=0) */
	rcar_rmw32(pcie, RVCCAP(0), 0xfff00000, 0);

	/* Enable MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI))
		rcar_pci_write_reg(pcie, 0x801f0000, PCIEMSITXR);

	rcar_pci_write_reg(pcie, MACCTLR_INIT_VAL, MACCTLR);

	/* Finish initialization - establish a PCI Express link */
	rcar_pci_write_reg(pcie, CFINIT, PCIETCTLR);

	/* This will timeout if we don't have a link. */
	err = rcar_pcie_wait_for_dl(pcie);
	if (err)
		return err;

	/* Enable INTx interrupts */
	rcar_rmw32(pcie, PCIEINTXR, 0, 0xF << 8);

	wmb();

	return 0;
}

static int rcar_pcie_phy_init_h1(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;

	/* Initialize the phy */
	phy_write_reg(pcie, 0, 0x42, 0x1, 0x0EC34191);
	phy_write_reg(pcie, 1, 0x42, 0x1, 0x0EC34180);
	phy_write_reg(pcie, 0, 0x43, 0x1, 0x00210188);
	phy_write_reg(pcie, 1, 0x43, 0x1, 0x00210188);
	phy_write_reg(pcie, 0, 0x44, 0x1, 0x015C0014);
	phy_write_reg(pcie, 1, 0x44, 0x1, 0x015C0014);
	phy_write_reg(pcie, 1, 0x4C, 0x1, 0x786174A0);
	phy_write_reg(pcie, 1, 0x4D, 0x1, 0x048000BB);
	phy_write_reg(pcie, 0, 0x51, 0x1, 0x079EC062);
	phy_write_reg(pcie, 0, 0x52, 0x1, 0x20000000);
	phy_write_reg(pcie, 1, 0x52, 0x1, 0x20000000);
	phy_write_reg(pcie, 1, 0x56, 0x1, 0x00003806);

	phy_write_reg(pcie, 0, 0x60, 0x1, 0x004B03A5);
	phy_write_reg(pcie, 0, 0x64, 0x1, 0x3F0F1F0F);
	phy_write_reg(pcie, 0, 0x66, 0x1, 0x00008000);

	return 0;
}

static int rcar_pcie_phy_init_gen2(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;

	/*
	 * These settings come from the R-Car Series, 2nd Generation User's
	 * Manual, section 50.3.1 (2) Initialization of the physical layer.
	 */
	rcar_pci_write_reg(pcie, 0x000f0030, GEN2_PCIEPHYADDR);
	rcar_pci_write_reg(pcie, 0x00381203, GEN2_PCIEPHYDATA);
	rcar_pci_write_reg(pcie, 0x00000001, GEN2_PCIEPHYCTRL);
	rcar_pci_write_reg(pcie, 0x00000006, GEN2_PCIEPHYCTRL);

	rcar_pci_write_reg(pcie, 0x000f0054, GEN2_PCIEPHYADDR);
	/* The following value is for DC connection, no termination resistor */
	rcar_pci_write_reg(pcie, 0x13802007, GEN2_PCIEPHYDATA);
	rcar_pci_write_reg(pcie, 0x00000001, GEN2_PCIEPHYCTRL);
	rcar_pci_write_reg(pcie, 0x00000006, GEN2_PCIEPHYCTRL);

	return 0;
}

static int rcar_pcie_phy_init_gen3(struct rcar_pcie_host *host)
{
	int err;

	err = phy_init(host->phy);
	if (err)
		return err;

	err = phy_power_on(host->phy);
	if (err)
		phy_exit(host->phy);

	return err;
}

static int rcar_msi_alloc(struct rcar_msi *chip)
{
	int msi;

	mutex_lock(&chip->lock);

	msi = find_first_zero_bit(chip->used, INT_PCI_MSI_NR);
	if (msi < INT_PCI_MSI_NR)
		set_bit(msi, chip->used);
	else
		msi = -ENOSPC;

	mutex_unlock(&chip->lock);

	return msi;
}

static int rcar_msi_alloc_region(struct rcar_msi *chip, int no_irqs)
{
	int msi;

	mutex_lock(&chip->lock);
	msi = bitmap_find_free_region(chip->used, INT_PCI_MSI_NR,
				      order_base_2(no_irqs));
	mutex_unlock(&chip->lock);

	return msi;
}

static void rcar_msi_free(struct rcar_msi *chip, unsigned long irq)
{
	mutex_lock(&chip->lock);
	clear_bit(irq, chip->used);
	mutex_unlock(&chip->lock);
}

static irqreturn_t rcar_pcie_msi_irq(int irq, void *data)
{
	struct rcar_pcie_host *host = data;
	struct rcar_pcie *pcie = &host->pcie;
	struct rcar_msi *msi = &host->msi;
	struct device *dev = pcie->dev;
	unsigned long reg;

	reg = rcar_pci_read_reg(pcie, PCIEMSIFR);

	/* MSI & INTx share an interrupt - we only handle MSI here */
	if (!reg)
		return IRQ_NONE;

	while (reg) {
		unsigned int index = find_first_bit(&reg, 32);
		unsigned int msi_irq;

		/* clear the interrupt */
		rcar_pci_write_reg(pcie, 1 << index, PCIEMSIFR);

		msi_irq = irq_find_mapping(msi->domain, index);
		if (msi_irq) {
			if (test_bit(index, msi->used))
				generic_handle_irq(msi_irq);
			else
				dev_info(dev, "unhandled MSI\n");
		} else {
			/* Unknown MSI, just clear it */
			dev_dbg(dev, "unexpected MSI\n");
		}

		/* see if there's any more pending in this vector */
		reg = rcar_pci_read_reg(pcie, PCIEMSIFR);
	}

	return IRQ_HANDLED;
}

static int rcar_msi_setup_irq(struct msi_controller *chip, struct pci_dev *pdev,
			      struct msi_desc *desc)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct rcar_pcie_host *host = container_of(chip, struct rcar_pcie_host,
						   msi.chip);
	struct rcar_pcie *pcie = &host->pcie;
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;

	hwirq = rcar_msi_alloc(msi);
	if (hwirq < 0)
		return hwirq;

	irq = irq_find_mapping(msi->domain, hwirq);
	if (!irq) {
		rcar_msi_free(msi, hwirq);
		return -EINVAL;
	}

	irq_set_msi_desc(irq, desc);

	msg.address_lo = rcar_pci_read_reg(pcie, PCIEMSIALR) & ~MSIFE;
	msg.address_hi = rcar_pci_read_reg(pcie, PCIEMSIAUR);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static int rcar_msi_setup_irqs(struct msi_controller *chip,
			       struct pci_dev *pdev, int nvec, int type)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct rcar_pcie_host *host = container_of(chip, struct rcar_pcie_host,
						   msi.chip);
	struct rcar_pcie *pcie = &host->pcie;
	struct msi_desc *desc;
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;
	int i;

	/* MSI-X interrupts are not supported */
	if (type == PCI_CAP_ID_MSIX)
		return -EINVAL;

	WARN_ON(!list_is_singular(&pdev->dev.msi_list));
	desc = list_entry(pdev->dev.msi_list.next, struct msi_desc, list);

	hwirq = rcar_msi_alloc_region(msi, nvec);
	if (hwirq < 0)
		return -ENOSPC;

	irq = irq_find_mapping(msi->domain, hwirq);
	if (!irq)
		return -ENOSPC;

	for (i = 0; i < nvec; i++) {
		/*
		 * irq_create_mapping() called from rcar_pcie_probe() pre-
		 * allocates descs,  so there is no need to allocate descs here.
		 * We can therefore assume that if irq_find_mapping() above
		 * returns non-zero, then the descs are also successfully
		 * allocated.
		 */
		if (irq_set_msi_desc_off(irq, i, desc)) {
			/* TODO: clear */
			return -EINVAL;
		}
	}

	desc->nvec_used = nvec;
	desc->msi_attrib.multiple = order_base_2(nvec);

	msg.address_lo = rcar_pci_read_reg(pcie, PCIEMSIALR) & ~MSIFE;
	msg.address_hi = rcar_pci_read_reg(pcie, PCIEMSIAUR);
	msg.data = hwirq;

	pci_write_msi_msg(irq, &msg);

	return 0;
}

static void rcar_msi_teardown_irq(struct msi_controller *chip, unsigned int irq)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct irq_data *d = irq_get_irq_data(irq);

	rcar_msi_free(msi, d->hwirq);
}

static struct irq_chip rcar_msi_irq_chip = {
	.name = "R-Car PCIe MSI",
	.irq_enable = pci_msi_unmask_irq,
	.irq_disable = pci_msi_mask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static int rcar_msi_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &rcar_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = rcar_msi_map,
};

static void rcar_pcie_unmap_msi(struct rcar_pcie_host *host)
{
	struct rcar_msi *msi = &host->msi;
	int i, irq;

	for (i = 0; i < INT_PCI_MSI_NR; i++) {
		irq = irq_find_mapping(msi->domain, i);
		if (irq > 0)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(msi->domain);
}

static void rcar_pcie_hw_enable_msi(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;
	struct rcar_msi *msi = &host->msi;
	unsigned long base;

	/* setup MSI data target */
	base = virt_to_phys((void *)msi->pages);

	rcar_pci_write_reg(pcie, lower_32_bits(base) | MSIFE, PCIEMSIALR);
	rcar_pci_write_reg(pcie, upper_32_bits(base), PCIEMSIAUR);

	/* enable all MSI interrupts */
	rcar_pci_write_reg(pcie, 0xffffffff, PCIEMSIIER);
}

static int rcar_pcie_enable_msi(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;
	struct device *dev = pcie->dev;
	struct rcar_msi *msi = &host->msi;
	int err, i;

	mutex_init(&msi->lock);

	msi->chip.dev = dev;
	msi->chip.setup_irq = rcar_msi_setup_irq;
	msi->chip.setup_irqs = rcar_msi_setup_irqs;
	msi->chip.teardown_irq = rcar_msi_teardown_irq;

	msi->domain = irq_domain_add_linear(dev->of_node, INT_PCI_MSI_NR,
					    &msi_domain_ops, &msi->chip);
	if (!msi->domain) {
		dev_err(dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < INT_PCI_MSI_NR; i++)
		irq_create_mapping(msi->domain, i);

	/* Two irqs are for MSI, but they are also used for non-MSI irqs */
	err = devm_request_irq(dev, msi->irq1, rcar_pcie_msi_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       rcar_msi_irq_chip.name, host);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	err = devm_request_irq(dev, msi->irq2, rcar_pcie_msi_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       rcar_msi_irq_chip.name, host);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	/* setup MSI data target */
	msi->pages = __get_free_pages(GFP_KERNEL, 0);
	rcar_pcie_hw_enable_msi(host);

	return 0;

err:
	rcar_pcie_unmap_msi(host);
	return err;
}

static void rcar_pcie_teardown_msi(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;
	struct rcar_msi *msi = &host->msi;

	/* Disable all MSI interrupts */
	rcar_pci_write_reg(pcie, 0, PCIEMSIIER);

	/* Disable address decoding of the MSI interrupt, MSIFE */
	rcar_pci_write_reg(pcie, 0, PCIEMSIALR);

	free_pages(msi->pages, 0);

	rcar_pcie_unmap_msi(host);
}

static int rcar_pcie_get_resources(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;
	struct device *dev = pcie->dev;
	struct resource res;
	int err, i;

	host->phy = devm_phy_optional_get(dev, "pcie");
	if (IS_ERR(host->phy))
		return PTR_ERR(host->phy);

	err = of_address_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;

	pcie->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	host->bus_clk = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(host->bus_clk)) {
		dev_err(dev, "cannot get pcie bus clock\n");
		return PTR_ERR(host->bus_clk);
	}

	i = irq_of_parse_and_map(dev->of_node, 0);
	if (!i) {
		dev_err(dev, "cannot get platform resources for msi interrupt\n");
		err = -ENOENT;
		goto err_irq1;
	}
	host->msi.irq1 = i;

	i = irq_of_parse_and_map(dev->of_node, 1);
	if (!i) {
		dev_err(dev, "cannot get platform resources for msi interrupt\n");
		err = -ENOENT;
		goto err_irq2;
	}
	host->msi.irq2 = i;

	return 0;

err_irq2:
	irq_dispose_mapping(host->msi.irq1);
err_irq1:
	return err;
}

static int rcar_pcie_inbound_ranges(struct rcar_pcie *pcie,
				    struct resource_entry *entry,
				    int *index)
{
	u64 restype = entry->res->flags;
	u64 cpu_addr = entry->res->start;
	u64 cpu_end = entry->res->end;
	u64 pci_addr = entry->res->start - entry->offset;
	u32 flags = LAM_64BIT | LAR_ENABLE;
	u64 mask;
	u64 size = resource_size(entry->res);
	int idx = *index;

	if (restype & IORESOURCE_PREFETCH)
		flags |= LAM_PREFETCH;

	while (cpu_addr < cpu_end) {
		if (idx >= MAX_NR_INBOUND_MAPS - 1) {
			dev_err(pcie->dev, "Failed to map inbound regions!\n");
			return -EINVAL;
		}
		/*
		 * If the size of the range is larger than the alignment of
		 * the start address, we have to use multiple entries to
		 * perform the mapping.
		 */
		if (cpu_addr > 0) {
			unsigned long nr_zeros = __ffs64(cpu_addr);
			u64 alignment = 1ULL << nr_zeros;

			size = min(size, alignment);
		}
		/* Hardware supports max 4GiB inbound region */
		size = min(size, 1ULL << 32);

		mask = roundup_pow_of_two(size) - 1;
		mask &= ~0xf;

		rcar_pcie_set_inbound(pcie, cpu_addr, pci_addr,
				      lower_32_bits(mask) | flags, idx, true);

		pci_addr += size;
		cpu_addr += size;
		idx += 2;
	}
	*index = idx;

	return 0;
}

static int rcar_pcie_parse_map_dma_ranges(struct rcar_pcie_host *host)
{
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(host);
	struct resource_entry *entry;
	int index = 0, err = 0;

	resource_list_for_each_entry(entry, &bridge->dma_ranges) {
		err = rcar_pcie_inbound_ranges(&host->pcie, entry, &index);
		if (err)
			break;
	}

	return err;
}

static const struct of_device_id rcar_pcie_of_match[] = {
	{ .compatible = "renesas,pcie-r8a7779",
	  .data = rcar_pcie_phy_init_h1 },
	{ .compatible = "renesas,pcie-r8a7790",
	  .data = rcar_pcie_phy_init_gen2 },
	{ .compatible = "renesas,pcie-r8a7791",
	  .data = rcar_pcie_phy_init_gen2 },
	{ .compatible = "renesas,pcie-rcar-gen2",
	  .data = rcar_pcie_phy_init_gen2 },
	{ .compatible = "renesas,pcie-r8a7795",
	  .data = rcar_pcie_phy_init_gen3 },
	{ .compatible = "renesas,pcie-rcar-gen3",
	  .data = rcar_pcie_phy_init_gen3 },
	{},
};

static int rcar_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_pcie_host *host;
	struct rcar_pcie *pcie;
	u32 data;
	int err;
	struct pci_host_bridge *bridge;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*host));
	if (!bridge)
		return -ENOMEM;

	host = pci_host_bridge_priv(bridge);
	pcie = &host->pcie;
	pcie->dev = dev;
	platform_set_drvdata(pdev, host);

	pm_runtime_enable(pcie->dev);
	err = pm_runtime_get_sync(pcie->dev);
	if (err < 0) {
		dev_err(pcie->dev, "pm_runtime_get_sync failed\n");
		goto err_pm_put;
	}

	err = rcar_pcie_get_resources(host);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		goto err_pm_put;
	}

	err = clk_prepare_enable(host->bus_clk);
	if (err) {
		dev_err(dev, "failed to enable bus clock: %d\n", err);
		goto err_unmap_msi_irqs;
	}

	err = rcar_pcie_parse_map_dma_ranges(host);
	if (err)
		goto err_clk_disable;

	host->phy_init_fn = of_device_get_match_data(dev);
	err = host->phy_init_fn(host);
	if (err) {
		dev_err(dev, "failed to init PCIe PHY\n");
		goto err_clk_disable;
	}

	/* Failure to get a link might just be that no cards are inserted */
	if (rcar_pcie_hw_init(pcie)) {
		dev_info(dev, "PCIe link down\n");
		err = -ENODEV;
		goto err_phy_shutdown;
	}

	data = rcar_pci_read_reg(pcie, MACSR);
	dev_info(dev, "PCIe x%d: link up\n", (data >> 20) & 0x3f);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		err = rcar_pcie_enable_msi(host);
		if (err < 0) {
			dev_err(dev,
				"failed to enable MSI support: %d\n",
				err);
			goto err_phy_shutdown;
		}
	}

	err = rcar_pcie_enable(host);
	if (err)
		goto err_msi_teardown;

	return 0;

err_msi_teardown:
	if (IS_ENABLED(CONFIG_PCI_MSI))
		rcar_pcie_teardown_msi(host);

err_phy_shutdown:
	if (host->phy) {
		phy_power_off(host->phy);
		phy_exit(host->phy);
	}

err_clk_disable:
	clk_disable_unprepare(host->bus_clk);

err_unmap_msi_irqs:
	irq_dispose_mapping(host->msi.irq2);
	irq_dispose_mapping(host->msi.irq1);

err_pm_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return err;
}

static int __maybe_unused rcar_pcie_resume(struct device *dev)
{
	struct rcar_pcie_host *host = dev_get_drvdata(dev);
	struct rcar_pcie *pcie = &host->pcie;
	unsigned int data;
	int err;

	err = rcar_pcie_parse_map_dma_ranges(host);
	if (err)
		return 0;

	/* Failure to get a link might just be that no cards are inserted */
	err = host->phy_init_fn(host);
	if (err) {
		dev_info(dev, "PCIe link down\n");
		return 0;
	}

	data = rcar_pci_read_reg(pcie, MACSR);
	dev_info(dev, "PCIe x%d: link up\n", (data >> 20) & 0x3f);

	/* Enable MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI))
		rcar_pcie_hw_enable_msi(host);

	rcar_pcie_hw_enable(host);

	return 0;
}

static int rcar_pcie_resume_noirq(struct device *dev)
{
	struct rcar_pcie_host *host = dev_get_drvdata(dev);
	struct rcar_pcie *pcie = &host->pcie;

	if (rcar_pci_read_reg(pcie, PMSR) &&
	    !(rcar_pci_read_reg(pcie, PCIETCTLR) & DL_DOWN))
		return 0;

	/* Re-establish the PCIe link */
	rcar_pci_write_reg(pcie, MACCTLR_INIT_VAL, MACCTLR);
	rcar_pci_write_reg(pcie, CFINIT, PCIETCTLR);
	return rcar_pcie_wait_for_dl(pcie);
}

static const struct dev_pm_ops rcar_pcie_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, rcar_pcie_resume)
	.resume_noirq = rcar_pcie_resume_noirq,
};

static struct platform_driver rcar_pcie_driver = {
	.driver = {
		.name = "rcar-pcie",
		.of_match_table = rcar_pcie_of_match,
		.pm = &rcar_pcie_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe = rcar_pcie_probe,
};
builtin_platform_driver(rcar_pcie_driver);
