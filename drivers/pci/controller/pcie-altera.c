// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright Altera Corporation (C) 2013-2015. All rights reserved
 *
 * Author: Ley Foon Tan <lftan@altera.com>
 * Description: Altera PCIe host controller driver
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../pci.h"

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

#define S10_RP_TX_CNTRL			0x2004
#define S10_RP_RXCPL_REG		0x2008
#define S10_RP_RXCPL_STATUS		0x200C
#define S10_RP_CFG_ADDR(pcie, reg)	\
	(((pcie)->hip_base) + (reg) + (1 << 20))
#define S10_RP_SECONDARY(pcie)		\
	readb(S10_RP_CFG_ADDR(pcie, PCI_SECONDARY_BUS))

/* TLP configuration type 0 and 1 */
#define TLP_FMTTYPE_CFGRD0		0x04	/* Configuration Read Type 0 */
#define TLP_FMTTYPE_CFGWR0		0x44	/* Configuration Write Type 0 */
#define TLP_FMTTYPE_CFGRD1		0x05	/* Configuration Read Type 1 */
#define TLP_FMTTYPE_CFGWR1		0x45	/* Configuration Write Type 1 */
#define TLP_PAYLOAD_SIZE		0x01
#define TLP_READ_TAG			0x1d
#define TLP_WRITE_TAG			0x10
#define RP_DEVFN			0
#define TLP_REQ_ID(bus, devfn)		(((bus) << 8) | (devfn))
#define TLP_CFG_DW0(pcie, cfg)		\
		(((cfg) << 24) |	\
		  TLP_PAYLOAD_SIZE)
#define TLP_CFG_DW1(pcie, tag, be)	\
	(((TLP_REQ_ID(pcie->root_bus_nr,  RP_DEVFN)) << 16) | (tag << 8) | (be))
#define TLP_CFG_DW2(bus, devfn, offset)	\
				(((bus) << 24) | ((devfn) << 16) | (offset))
#define TLP_COMP_STATUS(s)		(((s) >> 13) & 7)
#define TLP_BYTE_COUNT(s)		(((s) >> 0) & 0xfff)
#define TLP_HDR_SIZE			3
#define TLP_LOOP			500

#define LINK_UP_TIMEOUT			HZ
#define LINK_RETRAIN_TIMEOUT		HZ

#define DWORD_MASK			3

#define S10_TLP_FMTTYPE_CFGRD0		0x05
#define S10_TLP_FMTTYPE_CFGRD1		0x04
#define S10_TLP_FMTTYPE_CFGWR0		0x45
#define S10_TLP_FMTTYPE_CFGWR1		0x44

enum altera_pcie_version {
	ALTERA_PCIE_V1 = 0,
	ALTERA_PCIE_V2,
};

struct altera_pcie {
	struct platform_device	*pdev;
	void __iomem		*cra_base;
	void __iomem		*hip_base;
	int			irq;
	u8			root_bus_nr;
	struct irq_domain	*irq_domain;
	struct resource		bus_range;
	const struct altera_pcie_data	*pcie_data;
};

struct altera_pcie_ops {
	int (*tlp_read_pkt)(struct altera_pcie *pcie, u32 *value);
	void (*tlp_write_pkt)(struct altera_pcie *pcie, u32 *headers,
			      u32 data, bool align);
	bool (*get_link_status)(struct altera_pcie *pcie);
	int (*rp_read_cfg)(struct altera_pcie *pcie, int where,
			   int size, u32 *value);
	int (*rp_write_cfg)(struct altera_pcie *pcie, u8 busno,
			    int where, int size, u32 value);
};

struct altera_pcie_data {
	const struct altera_pcie_ops *ops;
	enum altera_pcie_version version;
	u32 cap_offset;		/* PCIe capability structure register offset */
	u32 cfgrd0;
	u32 cfgrd1;
	u32 cfgwr0;
	u32 cfgwr1;
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

static bool altera_pcie_link_up(struct altera_pcie *pcie)
{
	return !!((cra_readl(pcie, RP_LTSSM) & RP_LTSSM_MASK) == LTSSM_L0);
}

static bool s10_altera_pcie_link_up(struct altera_pcie *pcie)
{
	void __iomem *addr = S10_RP_CFG_ADDR(pcie,
				   pcie->pcie_data->cap_offset +
				   PCI_EXP_LNKSTA);

	return !!(readw(addr) & PCI_EXP_LNKSTA_DLLLA);
}

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

static void s10_tlp_write_tx(struct altera_pcie *pcie, u32 reg0, u32 ctrl)
{
	cra_writel(pcie, reg0, RP_TX_REG0);
	cra_writel(pcie, ctrl, S10_RP_TX_CNTRL);
}

static bool altera_pcie_valid_device(struct altera_pcie *pcie,
				     struct pci_bus *bus, int dev)
{
	/* If there is no link, then there is no device */
	if (bus->number != pcie->root_bus_nr) {
		if (!pcie->pcie_data->ops->get_link_status(pcie))
			return false;
	}

	/* access only one slot on each root port */
	if (bus->number == pcie->root_bus_nr && dev > 0)
		return false;

	return true;
}

static int tlp_read_packet(struct altera_pcie *pcie, u32 *value)
{
	int i;
	bool sop = false;
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

static int s10_tlp_read_packet(struct altera_pcie *pcie, u32 *value)
{
	u32 ctrl;
	u32 comp_status;
	u32 dw[4];
	u32 count;
	struct device *dev = &pcie->pdev->dev;

	for (count = 0; count < TLP_LOOP; count++) {
		ctrl = cra_readl(pcie, S10_RP_RXCPL_STATUS);
		if (ctrl & RP_RXCPL_SOP) {
			/* Read first DW */
			dw[0] = cra_readl(pcie, S10_RP_RXCPL_REG);
			break;
		}

		udelay(5);
	}

	/* SOP detection failed, return error */
	if (count == TLP_LOOP)
		return PCIBIOS_DEVICE_NOT_FOUND;

	count = 1;

	/* Poll for EOP */
	while (count < ARRAY_SIZE(dw)) {
		ctrl = cra_readl(pcie, S10_RP_RXCPL_STATUS);
		dw[count++] = cra_readl(pcie, S10_RP_RXCPL_REG);
		if (ctrl & RP_RXCPL_EOP) {
			comp_status = TLP_COMP_STATUS(dw[1]);
			if (comp_status)
				return PCIBIOS_DEVICE_NOT_FOUND;

			if (value && TLP_BYTE_COUNT(dw[1]) == sizeof(u32) &&
			    count == 4)
				*value = dw[3];

			return PCIBIOS_SUCCESSFUL;
		}
	}

	dev_warn(dev, "Malformed TLP packet\n");

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

static void s10_tlp_write_packet(struct altera_pcie *pcie, u32 *headers,
				 u32 data, bool dummy)
{
	s10_tlp_write_tx(pcie, headers[0], RP_TX_SOP);
	s10_tlp_write_tx(pcie, headers[1], 0);
	s10_tlp_write_tx(pcie, headers[2], 0);
	s10_tlp_write_tx(pcie, data, RP_TX_EOP);
}

static void get_tlp_header(struct altera_pcie *pcie, u8 bus, u32 devfn,
			   int where, u8 byte_en, bool read, u32 *headers)
{
	u8 cfg;
	u8 cfg0 = read ? pcie->pcie_data->cfgrd0 : pcie->pcie_data->cfgwr0;
	u8 cfg1 = read ? pcie->pcie_data->cfgrd1 : pcie->pcie_data->cfgwr1;
	u8 tag = read ? TLP_READ_TAG : TLP_WRITE_TAG;

	if (pcie->pcie_data->version == ALTERA_PCIE_V1)
		cfg = (bus == pcie->root_bus_nr) ? cfg0 : cfg1;
	else
		cfg = (bus > S10_RP_SECONDARY(pcie)) ? cfg0 : cfg1;

	headers[0] = TLP_CFG_DW0(pcie, cfg);
	headers[1] = TLP_CFG_DW1(pcie, tag, byte_en);
	headers[2] = TLP_CFG_DW2(bus, devfn, where);
}

static int tlp_cfg_dword_read(struct altera_pcie *pcie, u8 bus, u32 devfn,
			      int where, u8 byte_en, u32 *value)
{
	u32 headers[TLP_HDR_SIZE];

	get_tlp_header(pcie, bus, devfn, where, byte_en, true,
		       headers);

	pcie->pcie_data->ops->tlp_write_pkt(pcie, headers, 0, false);

	return pcie->pcie_data->ops->tlp_read_pkt(pcie, value);
}

static int tlp_cfg_dword_write(struct altera_pcie *pcie, u8 bus, u32 devfn,
			       int where, u8 byte_en, u32 value)
{
	u32 headers[TLP_HDR_SIZE];
	int ret;

	get_tlp_header(pcie, bus, devfn, where, byte_en, false,
		       headers);

	/* check alignment to Qword */
	if ((where & 0x7) == 0)
		pcie->pcie_data->ops->tlp_write_pkt(pcie, headers,
						    value, true);
	else
		pcie->pcie_data->ops->tlp_write_pkt(pcie, headers,
						    value, false);

	ret = pcie->pcie_data->ops->tlp_read_pkt(pcie, NULL);
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

static int s10_rp_read_cfg(struct altera_pcie *pcie, int where,
			   int size, u32 *value)
{
	void __iomem *addr = S10_RP_CFG_ADDR(pcie, where);

	switch (size) {
	case 1:
		*value = readb(addr);
		break;
	case 2:
		*value = readw(addr);
		break;
	default:
		*value = readl(addr);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int s10_rp_write_cfg(struct altera_pcie *pcie, u8 busno,
			    int where, int size, u32 value)
{
	void __iomem *addr = S10_RP_CFG_ADDR(pcie, where);

	switch (size) {
	case 1:
		writeb(value, addr);
		break;
	case 2:
		writew(value, addr);
		break;
	default:
		writel(value, addr);
		break;
	}

	/*
	 * Monitor changes to PCI_PRIMARY_BUS register on root port
	 * and update local copy of root bus number accordingly.
	 */
	if (busno == pcie->root_bus_nr && where == PCI_PRIMARY_BUS)
		pcie->root_bus_nr = value & 0xff;

	return PCIBIOS_SUCCESSFUL;
}

static int _altera_pcie_cfg_read(struct altera_pcie *pcie, u8 busno,
				 unsigned int devfn, int where, int size,
				 u32 *value)
{
	int ret;
	u32 data;
	u8 byte_en;

	if (busno == pcie->root_bus_nr && pcie->pcie_data->ops->rp_read_cfg)
		return pcie->pcie_data->ops->rp_read_cfg(pcie, where,
							 size, value);

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

	ret = tlp_cfg_dword_read(pcie, busno, devfn,
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

static int _altera_pcie_cfg_write(struct altera_pcie *pcie, u8 busno,
				  unsigned int devfn, int where, int size,
				  u32 value)
{
	u32 data32;
	u32 shift = 8 * (where & 3);
	u8 byte_en;

	if (busno == pcie->root_bus_nr && pcie->pcie_data->ops->rp_write_cfg)
		return pcie->pcie_data->ops->rp_write_cfg(pcie, busno,
						     where, size, value);

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

	return tlp_cfg_dword_write(pcie, busno, devfn, (where & ~DWORD_MASK),
				   byte_en, data32);
}

static int altera_pcie_cfg_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *value)
{
	struct altera_pcie *pcie = bus->sysdata;

	if (altera_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!altera_pcie_valid_device(pcie, bus, PCI_SLOT(devfn))) {
		*value = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	return _altera_pcie_cfg_read(pcie, bus->number, devfn, where, size,
				     value);
}

static int altera_pcie_cfg_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 value)
{
	struct altera_pcie *pcie = bus->sysdata;

	if (altera_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!altera_pcie_valid_device(pcie, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return _altera_pcie_cfg_write(pcie, bus->number, devfn, where, size,
				     value);
}

static struct pci_ops altera_pcie_ops = {
	.read = altera_pcie_cfg_read,
	.write = altera_pcie_cfg_write,
};

static int altera_read_cap_word(struct altera_pcie *pcie, u8 busno,
				unsigned int devfn, int offset, u16 *value)
{
	u32 data;
	int ret;

	ret = _altera_pcie_cfg_read(pcie, busno, devfn,
				    pcie->pcie_data->cap_offset + offset,
				    sizeof(*value),
				    &data);
	*value = data;
	return ret;
}

static int altera_write_cap_word(struct altera_pcie *pcie, u8 busno,
				 unsigned int devfn, int offset, u16 value)
{
	return _altera_pcie_cfg_write(pcie, busno, devfn,
				      pcie->pcie_data->cap_offset + offset,
				      sizeof(value),
				      value);
}

static void altera_wait_link_retrain(struct altera_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	u16 reg16;
	unsigned long start_jiffies;

	/* Wait for link training end. */
	start_jiffies = jiffies;
	for (;;) {
		altera_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN,
				     PCI_EXP_LNKSTA, &reg16);
		if (!(reg16 & PCI_EXP_LNKSTA_LT))
			break;

		if (time_after(jiffies, start_jiffies + LINK_RETRAIN_TIMEOUT)) {
			dev_err(dev, "link retrain timeout\n");
			break;
		}
		udelay(100);
	}

	/* Wait for link is up */
	start_jiffies = jiffies;
	for (;;) {
		if (pcie->pcie_data->ops->get_link_status(pcie))
			break;

		if (time_after(jiffies, start_jiffies + LINK_UP_TIMEOUT)) {
			dev_err(dev, "link up timeout\n");
			break;
		}
		udelay(100);
	}
}

static void altera_pcie_retrain(struct altera_pcie *pcie)
{
	u16 linkcap, linkstat, linkctl;

	if (!pcie->pcie_data->ops->get_link_status(pcie))
		return;

	/*
	 * Set the retrain bit if the PCIe rootport support > 2.5GB/s, but
	 * current speed is 2.5 GB/s.
	 */
	altera_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN, PCI_EXP_LNKCAP,
			     &linkcap);
	if ((linkcap & PCI_EXP_LNKCAP_SLS) <= PCI_EXP_LNKCAP_SLS_2_5GB)
		return;

	altera_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN, PCI_EXP_LNKSTA,
			     &linkstat);
	if ((linkstat & PCI_EXP_LNKSTA_CLS) == PCI_EXP_LNKSTA_CLS_2_5GB) {
		altera_read_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN,
				     PCI_EXP_LNKCTL, &linkctl);
		linkctl |= PCI_EXP_LNKCTL_RL;
		altera_write_cap_word(pcie, pcie->root_bus_nr, RP_DEVFN,
				      PCI_EXP_LNKCTL, linkctl);

		altera_wait_link_retrain(pcie);
	}
}

static int altera_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);
	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = altera_pcie_intx_map,
	.xlate = pci_irqd_intx_xlate,
};

static void altera_pcie_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct altera_pcie *pcie;
	struct device *dev;
	unsigned long status;
	u32 bit;
	int ret;

	chained_irq_enter(chip, desc);
	pcie = irq_desc_get_handler_data(desc);
	dev = &pcie->pdev->dev;

	while ((status = cra_readl(pcie, P2A_INT_STATUS)
		& P2A_INT_STS_ALL) != 0) {
		for_each_set_bit(bit, &status, PCI_NUM_INTX) {
			/* clear interrupts */
			cra_writel(pcie, 1 << bit, P2A_INT_STATUS);

			ret = generic_handle_domain_irq(pcie->irq_domain, bit);
			if (ret)
				dev_err_ratelimited(dev, "unexpected IRQ, INT%d\n", bit);
		}
	}

	chained_irq_exit(chip, desc);
}

static int altera_pcie_init_irq_domain(struct altera_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;

	/* Setup INTx */
	pcie->irq_domain = irq_domain_add_linear(node, PCI_NUM_INTX,
					&intx_domain_ops, pcie);
	if (!pcie->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	return 0;
}

static void altera_pcie_irq_teardown(struct altera_pcie *pcie)
{
	irq_set_chained_handler_and_data(pcie->irq, NULL, NULL);
	irq_domain_remove(pcie->irq_domain);
	irq_dispose_mapping(pcie->irq);
}

static int altera_pcie_parse_dt(struct altera_pcie *pcie)
{
	struct platform_device *pdev = pcie->pdev;

	pcie->cra_base = devm_platform_ioremap_resource_byname(pdev, "Cra");
	if (IS_ERR(pcie->cra_base))
		return PTR_ERR(pcie->cra_base);

	if (pcie->pcie_data->version == ALTERA_PCIE_V2) {
		pcie->hip_base =
			devm_platform_ioremap_resource_byname(pdev, "Hip");
		if (IS_ERR(pcie->hip_base))
			return PTR_ERR(pcie->hip_base);
	}

	/* setup IRQ */
	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq < 0)
		return pcie->irq;

	irq_set_chained_handler_and_data(pcie->irq, altera_pcie_isr, pcie);
	return 0;
}

static void altera_pcie_host_init(struct altera_pcie *pcie)
{
	altera_pcie_retrain(pcie);
}

static const struct altera_pcie_ops altera_pcie_ops_1_0 = {
	.tlp_read_pkt = tlp_read_packet,
	.tlp_write_pkt = tlp_write_packet,
	.get_link_status = altera_pcie_link_up,
};

static const struct altera_pcie_ops altera_pcie_ops_2_0 = {
	.tlp_read_pkt = s10_tlp_read_packet,
	.tlp_write_pkt = s10_tlp_write_packet,
	.get_link_status = s10_altera_pcie_link_up,
	.rp_read_cfg = s10_rp_read_cfg,
	.rp_write_cfg = s10_rp_write_cfg,
};

static const struct altera_pcie_data altera_pcie_1_0_data = {
	.ops = &altera_pcie_ops_1_0,
	.cap_offset = 0x80,
	.version = ALTERA_PCIE_V1,
	.cfgrd0 = TLP_FMTTYPE_CFGRD0,
	.cfgrd1 = TLP_FMTTYPE_CFGRD1,
	.cfgwr0 = TLP_FMTTYPE_CFGWR0,
	.cfgwr1 = TLP_FMTTYPE_CFGWR1,
};

static const struct altera_pcie_data altera_pcie_2_0_data = {
	.ops = &altera_pcie_ops_2_0,
	.version = ALTERA_PCIE_V2,
	.cap_offset = 0x70,
	.cfgrd0 = S10_TLP_FMTTYPE_CFGRD0,
	.cfgrd1 = S10_TLP_FMTTYPE_CFGRD1,
	.cfgwr0 = S10_TLP_FMTTYPE_CFGWR0,
	.cfgwr1 = S10_TLP_FMTTYPE_CFGWR1,
};

static const struct of_device_id altera_pcie_of_match[] = {
	{.compatible = "altr,pcie-root-port-1.0",
	 .data = &altera_pcie_1_0_data },
	{.compatible = "altr,pcie-root-port-2.0",
	 .data = &altera_pcie_2_0_data },
	{},
};

static int altera_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct altera_pcie *pcie;
	struct pci_host_bridge *bridge;
	int ret;
	const struct of_device_id *match;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->pdev = pdev;
	platform_set_drvdata(pdev, pcie);

	match = of_match_device(altera_pcie_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	pcie->pcie_data = match->data;

	ret = altera_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(dev, "Parsing DT failed\n");
		return ret;
	}

	ret = altera_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed creating IRQ Domain\n");
		return ret;
	}

	/* clear all interrupts */
	cra_writel(pcie, P2A_INT_STS_ALL, P2A_INT_STATUS);
	/* enable all interrupts */
	cra_writel(pcie, P2A_INT_ENA_ALL, P2A_INT_ENABLE);
	altera_pcie_host_init(pcie);

	bridge->sysdata = pcie;
	bridge->busnr = pcie->root_bus_nr;
	bridge->ops = &altera_pcie_ops;

	return pci_host_probe(bridge);
}

static int altera_pcie_remove(struct platform_device *pdev)
{
	struct altera_pcie *pcie = platform_get_drvdata(pdev);
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(pcie);

	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);
	altera_pcie_irq_teardown(pcie);

	return 0;
}

static struct platform_driver altera_pcie_driver = {
	.probe		= altera_pcie_probe,
	.remove		= altera_pcie_remove,
	.driver = {
		.name	= "altera-pcie",
		.of_match_table = altera_pcie_of_match,
	},
};

MODULE_DEVICE_TABLE(of, altera_pcie_of_match);
module_platform_driver(altera_pcie_driver);
MODULE_LICENSE("GPL v2");
