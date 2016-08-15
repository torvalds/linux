/*
 * Copyright Altera Corporation (C) 2013-2015. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define RP_TX_REG0			0x2000
#define RP_TX_REG1			0x2004
#define RP_TX_CNTRL			0x2008
#define RP_TX_EOP			0x2
#define RP_TX_SOP			0x1
#define RP_RXCPL_STATUS			0x2010
#define RP_RXCPL_EOP			0x2
#define RP_RXCPL_SOP			0x1
#define RP_RXCPL_REG0			0x2014
#define RP_RXCPL_REG1			0x2018
#define P2A_INT_STATUS			0x3060
#define P2A_INT_STS_ALL			0xf
#define P2A_INT_ENABLE			0x3070
#define P2A_INT_ENA_ALL			0xf
#define RP_LTSSM			0x3c64
#define RP_LTSSM_MASK			0x1f
#define LTSSM_L0			0xf

/* TLP configuration type 0 and 1 */
#define TLP_FMTTYPE_CFGRD0		0x04	/* Configuration Read Type 0 */
#define TLP_FMTTYPE_CFGWR0		0x44	/* Configuration Write Type 0 */
#define TLP_FMTTYPE_CFGRD1		0x05	/* Configuration Read Type 1 */
#define TLP_FMTTYPE_CFGWR1		0x45	/* Configuration Write Type 1 */
#define TLP_PAYLOAD_SIZE		0x01
#define TLP_READ_TAG			0x1d
#define TLP_WRITE_TAG			0x10
#define TLP_CFG_DW0(fmttype)		(((fmttype) << 24) | TLP_PAYLOAD_SIZE)
#define TLP_CFG_DW1(reqid, tag, be)	(((reqid) << 16) | (tag << 8) | (be))
#define TLP_CFG_DW2(bus, devfn, offset)	\
				(((bus) << 24) | ((devfn) << 16) | (offset))
#define TLP_REQ_ID(bus, devfn)		(((bus) << 8) | (devfn))
#define TLP_COMP_STATUS(s)		(((s) >> 12) & 7)
#define TLP_HDR_SIZE			3
#define TLP_LOOP			500
#define RP_DEVFN			0

#define LINK_UP_TIMEOUT			HZ
#define LINK_RETRAIN_TIMEOUT		HZ

#define INTX_NUM			4

#define DWORD_MASK			3

struct altera_pcie {
	struct platform_device	*pdev;
	void __iomem		*cra_base;
	int			irq;
	u8			root_bus_nr;
	struct irq_domain	*irq_domain;
	struct resource		bus_range;
	struct list_head	resources;
};

struct tlp_rp_regpair_t {
	u32 ctrl;
	u32 reg0;
	u32 reg1;
};

static inline void cra_writel(struct altera_pcie *pcie, const u32 value,
			      const u32 reg)
{
	writel_relaxed(value, pcie->cra_base + reg);
}

static inline u32 cra_readl(struct altera_pcie *pcie, const u32 reg)
{
	return readl_relaxed(pcie->cra_base + reg);
}

static bool altera_pcie_link_is_up(struct altera_pcie *pcie)
{
	return !!((cra_readl(pcie, RP_LTSSM) & RP_LTSSM_MASK) == LTSSM_L0);
}

static void altera_wait_link_retrain(struct pci_dev *dev)
{
	u16 reg16;
	unsigned long start_jiffies;
	struct altera_pcie *pcie = dev->bus->sysdata;

	/* Wait for link training end. */
	start_jiffies = jiffies;
	for (;;) {
		pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &reg16);
		if (!(reg16 & PCI_EXP_LNKSTA_LT))
			break;

		if (time_after(jiffies, start_jiffies + LINK_RETRAIN_TIMEOUT)) {
			dev_err(&pcie->pdev->dev, "link retrain timeout\n");
			break;
		}
		udelay(100);
	}

	/* Wait for link is up */
	start_jiffies = jiffies;
	for (;;) {
		if (altera_pcie_link_is_up(pcie))
			break;

		if (time_after(jiffies, start_jiffies + LINK_UP_TIMEOUT)) {
			dev_err(&pcie->pdev->dev, "link up timeout\n");
			break;
		}
		udelay(100);
	}
}

static void altera_pcie_retrain(struct pci_dev *dev)
{
	u16 linkcap, linkstat;
	struct altera_pcie *pcie = dev->bus->sysdata;

	if (!altera_pcie_link_is_up(pcie))
		return;

	/*
	 * Set the retrain bit if the PCIe rootport support > 2.5GB/s, but
	 * current speed is 2.5 GB/s.
	 */
	pcie_capability_read_word(dev, PCI_EXP_LNKCAP, &linkcap);

	if ((linkcap & PCI_EXP_LNKCAP_SLS) <= PCI_EXP_LNKCAP_SLS_2_5GB)
		return;

	pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &linkstat);
	if ((linkstat & PCI_EXP_LNKSTA_CLS) == PCI_EXP_LNKSTA_CLS_2_5GB) {
		pcie_capability_set_word(dev, PCI_EXP_LNKCTL,
					 PCI_EXP_LNKCTL_RL);
		altera_wait_link_retrain(dev);
	}
}
DECLARE_PCI_FIXUP_EARLY(0x1172, PCI_ANY_ID, altera_pcie_retrain);

/*
 * Altera PCIe port uses BAR0 of RC's configuration space as the translation
 * from PCI bus to native BUS.  Entire DDR region is mapped into PCIe space
 * using these registers, so it can be reached by DMA from EP devices.
 * This BAR0 will also access to MSI vector when receiving MSI/MSIX interrupt
 * from EP devices, eventually trigger interrupt to GIC.  The BAR0 of bridge
 * should be hidden during enumeration to avoid the sizing and resource
 * allocation by PCIe core.
 */
static bool altera_pcie_hide_rc_bar(struct pci_bus *bus, unsigned int  devfn,
				    int offset)
{
	if (pci_is_root_bus(bus) && (devfn == 0) &&
	    (offset == PCI_BASE_ADDRESS_0))
		return true;

	return false;
}

static void tlp_write_tx(struct altera_pcie *pcie,
			 struct tlp_rp_regpair_t *tlp_rp_regdata)
{
	cra_writel(pcie, tlp_rp_regdata->reg0, RP_TX_REG0);
	cra_writel(pcie, tlp_rp_regdata->reg1, RP_TX_REG1);
	cra_writel(pcie, tlp_rp_regdata->ctrl, RP_TX_CNTRL);
}

static bool altera_pcie_valid_config(struct altera_pcie *pcie,
				     struct pci_bus *bus, int dev)
{
	/* If there is no link, then there is no device */
	if (bus->number != pcie->root_bus_nr) {
		if (!altera_pcie_link_is_up(pcie))
			return false;
	}

	/* access only one slot on each root port */
	if (bus->number == pcie->root_bus_nr && dev > 0)
		return false;

	/*
	 * Do not read more than one device on the bus directly attached
	 * to root port, root port can only attach to one downstream port.
	 */
	if (bus->primary == pcie->root_bus_nr && dev > 0)
		return false;

	 return true;
}

static int tlp_read_packet(struct altera_pcie *pcie, u32 *value)
{
	int i;
	bool sop = 0;
	u32 ctrl;
	u32 reg0, reg1;
	u32 comp_status = 1;

	/*
	 * Minimum 2 loops to read TLP headers and 1 loop to read data
	 * payload.
	 */
	for (i = 0; i < TLP_LOOP; i++) {
		ctrl = cra_readl(pcie, RP_RXCPL_STATUS);
		if ((ctrl & RP_RXCPL_SOP) || (ctrl & RP_RXCPL_EOP) || sop) {
			reg0 = cra_readl(pcie, RP_RXCPL_REG0);
			reg1 = cra_readl(pcie, RP_RXCPL_REG1);

			if (ctrl & RP_RXCPL_SOP) {
				sop = true;
				comp_status = TLP_COMP_STATUS(reg1);
			}

			if (ctrl & RP_RXCPL_EOP) {
				if (comp_status)
					return PCIBIOS_DEVICE_NOT_FOUND;

				if (value)
					*value = reg0;

				return PCIBIOS_SUCCESSFUL;
			}
		}
		udelay(5);
	}

	return PCIBIOS_DEVICE_NOT_FOUND;
}

static void tlp_write_packet(struct altera_pcie *pcie, u32 *headers,
			     u32 data, bool align)
{
	struct tlp_rp_regpair_t tlp_rp_regdata;

	tlp_rp_regdata.reg0 = headers[0];
	tlp_rp_regdata.reg1 = headers[1];
	tlp_rp_regdata.ctrl = RP_TX_SOP;
	tlp_write_tx(pcie, &tlp_rp_regdata);

	if (align) {
		tlp_rp_regdata.reg0 = headers[2];
		tlp_rp_regdata.reg1 = 0;
		tlp_rp_regdata.ctrl = 0;
		tlp_write_tx(pcie, &tlp_rp_regdata);

		tlp_rp_regdata.reg0 = data;
		tlp_rp_regdata.reg1 = 0;
	} else {
		tlp_rp_regdata.reg0 = headers[2];
		tlp_rp_regdata.reg1 = data;
	}

	tlp_rp_regdata.ctrl = RP_TX_EOP;
	tlp_write_tx(pcie, &tlp_rp_regdata);
}

static int tlp_cfg_dword_read(struct altera_pcie *pcie, u8 bus, u32 devfn,
			      int where, u8 byte_en, u32 *value)
{
	u32 headers[TLP_HDR_SIZE];

	if (bus == pcie->root_bus_nr)
		headers[0] = TLP_CFG_DW0(TLP_FMTTYPE_CFGRD0);
	else
		headers[0] = TLP_CFG_DW0(TLP_FMTTYPE_CFGRD1);

	headers[1] = TLP_CFG_DW1(TLP_REQ_ID(pcie->root_bus_nr, RP_DEVFN),
					TLP_READ_TAG, byte_en);
	headers[2] = TLP_CFG_DW2(bus, devfn, where);

	tlp_write_packet(pcie, headers, 0, false);

	return tlp_read_packet(pcie, value);
}

static int tlp_cfg_dword_write(struct altera_pcie *pcie, u8 bus, u32 devfn,
			       int where, u8 byte_en, u32 value)
{
	u32 headers[TLP_HDR_SIZE];
	int ret;

	if (bus == pcie->root_bus_nr)
		headers[0] = TLP_CFG_DW0(TLP_FMTTYPE_CFGWR0);
	else
		headers[0] = TLP_CFG_DW0(TLP_FMTTYPE_CFGWR1);

	headers[1] = TLP_CFG_DW1(TLP_REQ_ID(pcie->root_bus_nr, RP_DEVFN),
					TLP_WRITE_TAG, byte_en);
	headers[2] = TLP_CFG_DW2(bus, devfn, where);

	/* check alignment to Qword */
	if ((where & 0x7) == 0)
		tlp_write_packet(pcie, headers, value, true);
	else
		tlp_write_packet(pcie, headers, value, false);

	ret = tlp_read_packet(pcie, NULL);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	/*
	 * Monitor changes to PCI_PRIMARY_BUS register on root port
	 * and update local copy of root bus number accordingly.
	 */
	if ((bus == pcie->root_bus_nr) && (where == PCI_PRIMARY_BUS))
		pcie->root_bus_nr = (u8)(value);

	return PCIBIOS_SUCCESSFUL;
}

static int altera_pcie_cfg_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *value)
{
	struct altera_pcie *pcie = bus->sysdata;
	int ret;
	u32 data;
	u8 byte_en;

	if (altera_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!altera_pcie_valid_config(pcie, bus, PCI_SLOT(devfn))) {
		*value = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	switch (size) {
	case 1:
		byte_en = 1 << (where & 3);
		break;
	case 2:
		byte_en = 3 << (where & 3);
		break;
	default:
		byte_en = 0xf;
		break;
	}

	ret = tlp_cfg_dword_read(pcie, bus->number, devfn,
				 (where & ~DWORD_MASK), byte_en, &data);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	switch (size) {
	case 1:
		*value = (data >> (8 * (where & 0x3))) & 0xff;
		break;
	case 2:
		*value = (data >> (8 * (where & 0x2))) & 0xffff;
		break;
	default:
		*value = data;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int altera_pcie_cfg_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 value)
{
	struct altera_pcie *pcie = bus->sysdata;
	u32 data32;
	u32 shift = 8 * (where & 3);
	u8 byte_en;

	if (altera_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!altera_pcie_valid_config(pcie, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		data32 = (value & 0xff) << shift;
		byte_en = 1 << (where & 3);
		break;
	case 2:
		data32 = (value & 0xffff) << shift;
		byte_en = 3 << (where & 3);
		break;
	default:
		data32 = value;
		byte_en = 0xf;
		break;
	}

	return tlp_cfg_dword_write(pcie, bus->number, devfn,
		(where & ~DWORD_MASK), byte_en, data32);
}

static struct pci_ops altera_pcie_ops = {
	.read = altera_pcie_cfg_read,
	.write = altera_pcie_cfg_write,
};

static int altera_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = altera_pcie_intx_map,
};

static void altera_pcie_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct altera_pcie *pcie;
	unsigned long status;
	u32 bit;
	u32 virq;

	chained_irq_enter(chip, desc);
	pcie = irq_desc_get_handler_data(desc);

	while ((status = cra_readl(pcie, P2A_INT_STATUS)
		& P2A_INT_STS_ALL) != 0) {
		for_each_set_bit(bit, &status, INTX_NUM) {
			/* clear interrupts */
			cra_writel(pcie, 1 << bit, P2A_INT_STATUS);

			virq = irq_find_mapping(pcie->irq_domain, bit + 1);
			if (virq)
				generic_handle_irq(virq);
			else
				dev_err(&pcie->pdev->dev,
					"unexpected IRQ, INT%d\n", bit);
		}
	}

	chained_irq_exit(chip, desc);
}

static int altera_pcie_parse_request_of_pci_ranges(struct altera_pcie *pcie)
{
	int err, res_valid = 0;
	struct device *dev = &pcie->pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource_entry *win;

	err = of_pci_get_host_bridge_resources(np, 0, 0xff, &pcie->resources,
					       NULL);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, &pcie->resources);
	if (err)
		goto out_release_res;

	resource_list_for_each_entry(win, &pcie->resources) {
		struct resource *res = win->res;

		if (resource_type(res) == IORESOURCE_MEM)
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);
	}

	if (res_valid)
		return 0;

	dev_err(dev, "non-prefetchable memory resource required\n");
	err = -EINVAL;

out_release_res:
	pci_free_resource_list(&pcie->resources);
	return err;
}

static int altera_pcie_init_irq_domain(struct altera_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;

	/* Setup INTx */
	pcie->irq_domain = irq_domain_add_linear(node, INTX_NUM + 1,
					&intx_domain_ops, pcie);
	if (!pcie->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	return 0;
}

static int altera_pcie_parse_dt(struct altera_pcie *pcie)
{
	struct resource *cra;
	struct platform_device *pdev = pcie->pdev;

	cra = platform_get_resource_byname(pdev, IORESOURCE_MEM, "Cra");
	if (!cra) {
		dev_err(&pdev->dev, "no Cra memory resource defined\n");
		return -ENODEV;
	}

	pcie->cra_base = devm_ioremap_resource(&pdev->dev, cra);
	if (IS_ERR(pcie->cra_base)) {
		dev_err(&pdev->dev, "failed to map cra memory\n");
		return PTR_ERR(pcie->cra_base);
	}

	/* setup IRQ */
	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq <= 0) {
		dev_err(&pdev->dev, "failed to get IRQ: %d\n", pcie->irq);
		return -EINVAL;
	}

	irq_set_chained_handler_and_data(pcie->irq, altera_pcie_isr, pcie);

	return 0;
}

static int altera_pcie_probe(struct platform_device *pdev)
{
	struct altera_pcie *pcie;
	struct pci_bus *bus;
	struct pci_bus *child;
	int ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->pdev = pdev;

	ret = altera_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Parsing DT failed\n");
		return ret;
	}

	INIT_LIST_HEAD(&pcie->resources);

	ret = altera_pcie_parse_request_of_pci_ranges(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Failed add resources\n");
		return ret;
	}

	ret = altera_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Failed creating IRQ Domain\n");
		return ret;
	}

	/* clear all interrupts */
	cra_writel(pcie, P2A_INT_STS_ALL, P2A_INT_STATUS);
	/* enable all interrupts */
	cra_writel(pcie, P2A_INT_ENA_ALL, P2A_INT_ENABLE);

	bus = pci_scan_root_bus(&pdev->dev, pcie->root_bus_nr, &altera_pcie_ops,
				pcie, &pcie->resources);
	if (!bus)
		return -ENOMEM;

	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);
	pci_assign_unassigned_bus_resources(bus);

	/* Configure PCI Express setting. */
	list_for_each_entry(child, &bus->children, node)
		pcie_bus_configure_settings(child);

	pci_bus_add_devices(bus);

	platform_set_drvdata(pdev, pcie);
	return ret;
}

static const struct of_device_id altera_pcie_of_match[] = {
	{ .compatible = "altr,pcie-root-port-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_pcie_of_match);

static struct platform_driver altera_pcie_driver = {
	.probe		= altera_pcie_probe,
	.driver = {
		.name	= "altera-pcie",
		.of_match_table = altera_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

static int altera_pcie_init(void)
{
	return platform_driver_register(&altera_pcie_driver);
}
module_init(altera_pcie_init);

MODULE_AUTHOR("Ley Foon Tan <lftan@altera.com>");
MODULE_DESCRIPTION("Altera PCIe host controller driver");
MODULE_LICENSE("GPL v2");
