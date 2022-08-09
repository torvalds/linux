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
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pcie-rcar.h"

struct rcar_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	struct mutex map_lock;
	spinlock_t mask_lock;
	int irq1;
	int irq2;
};

#ifdef CONFIG_ARM
/*
 * Here we keep a static copy of the remapped PCIe controller address.
 * This is only used on aarch32 systems, all of which have one single
 * PCIe controller, to provide quick access to the PCIe controller in
 * the L1 link state fixup function, called from the ARM fault handler.
 */
static void __iomem *pcie_base;
/*
 * Static copy of bus clock pointer, so we can check whether the clock
 * is enabled or not.
 */
static struct clk *pcie_bus_clk;
#endif

/* Structure representing the PCIe interface */
struct rcar_pcie_host {
	struct rcar_pcie	pcie;
	struct phy		*phy;
	struct clk		*bus_clk;
	struct			rcar_msi msi;
	int			(*phy_init_fn)(struct rcar_pcie_host *host);
};

static struct rcar_pcie_host *msi_to_host(struct rcar_msi *msi)
{
	return container_of(msi, struct rcar_pcie_host, msi);
}

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
		int ret;

		ret = generic_handle_domain_irq(msi->domain->parent, index);
		if (ret) {
			/* Unknown MSI, just clear it */
			dev_dbg(dev, "unexpected MSI\n");
			rcar_pci_write_reg(pcie, BIT(index), PCIEMSIFR);
		}

		/* see if there's any more pending in this vector */
		reg = rcar_pci_read_reg(pcie, PCIEMSIFR);
	}

	return IRQ_HANDLED;
}

static void rcar_msi_top_irq_ack(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void rcar_msi_top_irq_mask(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void rcar_msi_top_irq_unmask(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip rcar_msi_top_chip = {
	.name		= "PCIe MSI",
	.irq_ack	= rcar_msi_top_irq_ack,
	.irq_mask	= rcar_msi_top_irq_mask,
	.irq_unmask	= rcar_msi_top_irq_unmask,
};

static void rcar_msi_irq_ack(struct irq_data *d)
{
	struct rcar_msi *msi = irq_data_get_irq_chip_data(d);
	struct rcar_pcie *pcie = &msi_to_host(msi)->pcie;

	/* clear the interrupt */
	rcar_pci_write_reg(pcie, BIT(d->hwirq), PCIEMSIFR);
}

static void rcar_msi_irq_mask(struct irq_data *d)
{
	struct rcar_msi *msi = irq_data_get_irq_chip_data(d);
	struct rcar_pcie *pcie = &msi_to_host(msi)->pcie;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&msi->mask_lock, flags);
	value = rcar_pci_read_reg(pcie, PCIEMSIIER);
	value &= ~BIT(d->hwirq);
	rcar_pci_write_reg(pcie, value, PCIEMSIIER);
	spin_unlock_irqrestore(&msi->mask_lock, flags);
}

static void rcar_msi_irq_unmask(struct irq_data *d)
{
	struct rcar_msi *msi = irq_data_get_irq_chip_data(d);
	struct rcar_pcie *pcie = &msi_to_host(msi)->pcie;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&msi->mask_lock, flags);
	value = rcar_pci_read_reg(pcie, PCIEMSIIER);
	value |= BIT(d->hwirq);
	rcar_pci_write_reg(pcie, value, PCIEMSIIER);
	spin_unlock_irqrestore(&msi->mask_lock, flags);
}

static int rcar_msi_set_affinity(struct irq_data *d, const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static void rcar_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct rcar_msi *msi = irq_data_get_irq_chip_data(data);
	struct rcar_pcie *pcie = &msi_to_host(msi)->pcie;

	msg->address_lo = rcar_pci_read_reg(pcie, PCIEMSIALR) & ~MSIFE;
	msg->address_hi = rcar_pci_read_reg(pcie, PCIEMSIAUR);
	msg->data = data->hwirq;
}

static struct irq_chip rcar_msi_bottom_chip = {
	.name			= "Rcar MSI",
	.irq_ack		= rcar_msi_irq_ack,
	.irq_mask		= rcar_msi_irq_mask,
	.irq_unmask		= rcar_msi_irq_unmask,
	.irq_set_affinity 	= rcar_msi_set_affinity,
	.irq_compose_msi_msg	= rcar_compose_msi_msg,
};

static int rcar_msi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *args)
{
	struct rcar_msi *msi = domain->host_data;
	unsigned int i;
	int hwirq;

	mutex_lock(&msi->map_lock);

	hwirq = bitmap_find_free_region(msi->used, INT_PCI_MSI_NR, order_base_2(nr_irqs));

	mutex_unlock(&msi->map_lock);

	if (hwirq < 0)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &rcar_msi_bottom_chip, domain->host_data,
				    handle_edge_irq, NULL, NULL);

	return 0;
}

static void rcar_msi_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct rcar_msi *msi = domain->host_data;

	mutex_lock(&msi->map_lock);

	bitmap_release_region(msi->used, d->hwirq, order_base_2(nr_irqs));

	mutex_unlock(&msi->map_lock);
}

static const struct irq_domain_ops rcar_msi_domain_ops = {
	.alloc	= rcar_msi_domain_alloc,
	.free	= rcar_msi_domain_free,
};

static struct msi_domain_info rcar_msi_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &rcar_msi_top_chip,
};

static int rcar_allocate_domains(struct rcar_msi *msi)
{
	struct rcar_pcie *pcie = &msi_to_host(msi)->pcie;
	struct fwnode_handle *fwnode = dev_fwnode(pcie->dev);
	struct irq_domain *parent;

	parent = irq_domain_create_linear(fwnode, INT_PCI_MSI_NR,
					  &rcar_msi_domain_ops, msi);
	if (!parent) {
		dev_err(pcie->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}
	irq_domain_update_bus_token(parent, DOMAIN_BUS_NEXUS);

	msi->domain = pci_msi_create_irq_domain(fwnode, &rcar_msi_info, parent);
	if (!msi->domain) {
		dev_err(pcie->dev, "failed to create MSI domain\n");
		irq_domain_remove(parent);
		return -ENOMEM;
	}

	return 0;
}

static void rcar_free_domains(struct rcar_msi *msi)
{
	struct irq_domain *parent = msi->domain->parent;

	irq_domain_remove(msi->domain);
	irq_domain_remove(parent);
}

static int rcar_pcie_enable_msi(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;
	struct device *dev = pcie->dev;
	struct rcar_msi *msi = &host->msi;
	struct resource res;
	int err;

	mutex_init(&msi->map_lock);
	spin_lock_init(&msi->mask_lock);

	err = of_address_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;

	err = rcar_allocate_domains(msi);
	if (err)
		return err;

	/* Two irqs are for MSI, but they are also used for non-MSI irqs */
	err = devm_request_irq(dev, msi->irq1, rcar_pcie_msi_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       rcar_msi_bottom_chip.name, host);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	err = devm_request_irq(dev, msi->irq2, rcar_pcie_msi_irq,
			       IRQF_SHARED | IRQF_NO_THREAD,
			       rcar_msi_bottom_chip.name, host);
	if (err < 0) {
		dev_err(dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	/* disable all MSIs */
	rcar_pci_write_reg(pcie, 0, PCIEMSIIER);

	/*
	 * Setup MSI data target using RC base address address, which
	 * is guaranteed to be in the low 32bit range on any RCar HW.
	 */
	rcar_pci_write_reg(pcie, lower_32_bits(res.start) | MSIFE, PCIEMSIALR);
	rcar_pci_write_reg(pcie, upper_32_bits(res.start), PCIEMSIAUR);

	return 0;

err:
	rcar_free_domains(msi);
	return err;
}

static void rcar_pcie_teardown_msi(struct rcar_pcie_host *host)
{
	struct rcar_pcie *pcie = &host->pcie;

	/* Disable all MSI interrupts */
	rcar_pci_write_reg(pcie, 0, PCIEMSIIER);

	/* Disable address decoding of the MSI interrupt, MSIFE */
	rcar_pci_write_reg(pcie, 0, PCIEMSIALR);

	rcar_free_domains(&host->msi);
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

#ifdef CONFIG_ARM
	/* Cache static copy for L1 link state fixup hook on aarch32 */
	pcie_base = pcie->base;
	pcie_bus_clk = host->bus_clk;
#endif

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
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		struct resource res;
		u32 val;

		of_address_to_resource(dev->of_node, 0, &res);
		rcar_pci_write_reg(pcie, upper_32_bits(res.start), PCIEMSIAUR);
		rcar_pci_write_reg(pcie, lower_32_bits(res.start) | MSIFE, PCIEMSIALR);

		bitmap_to_arr32(&val, host->msi.used, INT_PCI_MSI_NR);
		rcar_pci_write_reg(pcie, val, PCIEMSIIER);
	}

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

#ifdef CONFIG_ARM
static DEFINE_SPINLOCK(pmsr_lock);
static int rcar_pcie_aarch32_abort_handler(unsigned long addr,
		unsigned int fsr, struct pt_regs *regs)
{
	unsigned long flags;
	u32 pmsr, val;
	int ret = 0;

	spin_lock_irqsave(&pmsr_lock, flags);

	if (!pcie_base || !__clk_is_enabled(pcie_bus_clk)) {
		ret = 1;
		goto unlock_exit;
	}

	pmsr = readl(pcie_base + PMSR);

	/*
	 * Test if the PCIe controller received PM_ENTER_L1 DLLP and
	 * the PCIe controller is not in L1 link state. If true, apply
	 * fix, which will put the controller into L1 link state, from
	 * which it can return to L0s/L0 on its own.
	 */
	if ((pmsr & PMEL1RX) && ((pmsr & PMSTATE) != PMSTATE_L1)) {
		writel(L1IATN, pcie_base + PMCTLR);
		ret = readl_poll_timeout_atomic(pcie_base + PMSR, val,
						val & L1FAEG, 10, 1000);
		WARN(ret, "Timeout waiting for L1 link state, ret=%d\n", ret);
		writel(L1FAEG | PMEL1RX, pcie_base + PMSR);
	}

unlock_exit:
	spin_unlock_irqrestore(&pmsr_lock, flags);
	return ret;
}

static const struct of_device_id rcar_pcie_abort_handler_of_match[] __initconst = {
	{ .compatible = "renesas,pcie-r8a7779" },
	{ .compatible = "renesas,pcie-r8a7790" },
	{ .compatible = "renesas,pcie-r8a7791" },
	{ .compatible = "renesas,pcie-rcar-gen2" },
	{},
};

static int __init rcar_pcie_init(void)
{
	if (of_find_matching_node(NULL, rcar_pcie_abort_handler_of_match)) {
#ifdef CONFIG_ARM_LPAE
		hook_fault_code(17, rcar_pcie_aarch32_abort_handler, SIGBUS, 0,
				"asynchronous external abort");
#else
		hook_fault_code(22, rcar_pcie_aarch32_abort_handler, SIGBUS, 0,
				"imprecise external abort");
#endif
	}

	return platform_driver_register(&rcar_pcie_driver);
}
device_initcall(rcar_pcie_init);
#else
builtin_platform_driver(rcar_pcie_driver);
#endif
