// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 Aspeed Technology Inc.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-msi-lib.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/phy/pcie.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "../pci.h"

#define MAX_MSI_HOST_IRQS	64
#define ASPEED_RESET_RC_WAIT_MS	10

/* AST2600 AHBC Registers */
#define ASPEED_AHBC_KEY			0x00
#define  ASPEED_AHBC_UNLOCK_KEY			0xaeed1a03
#define  ASPEED_AHBC_UNLOCK			0x01
#define ASPEED_AHBC_ADDR_MAPPING	0x8c
#define  ASPEED_PCIE_RC_MEMORY_EN		BIT(5)

/* AST2600 H2X Controller Registers */
#define ASPEED_H2X_INT_STS		0x08
#define  ASPEED_PCIE_TX_IDLE_CLEAR		BIT(0)
#define  ASPEED_PCIE_INTX_STS			GENMASK(3, 0)
#define ASPEED_H2X_HOST_RX_DESC_DATA	0x0c
#define ASPEED_H2X_TX_DESC0		0x10
#define ASPEED_H2X_TX_DESC1		0x14
#define ASPEED_H2X_TX_DESC2		0x18
#define ASPEED_H2X_TX_DESC3		0x1c
#define ASPEED_H2X_TX_DESC_DATA		0x20
#define ASPEED_H2X_STS			0x24
#define  ASPEED_PCIE_TX_IDLE			BIT(31)
#define  ASPEED_PCIE_STATUS_OF_TX		GENMASK(25, 24)
#define	ASPEED_PCIE_RC_H_TX_COMPLETE		BIT(25)
#define  ASPEED_PCIE_TRIGGER_TX			BIT(0)
#define ASPEED_H2X_AHB_ADDR_CONFIG0	0x60
#define  ASPEED_AHB_REMAP_LO_ADDR(x)		(x & GENMASK(15, 4))
#define  ASPEED_AHB_MASK_LO_ADDR(x)		FIELD_PREP(GENMASK(31, 20), x)
#define ASPEED_H2X_AHB_ADDR_CONFIG1	0x64
#define  ASPEED_AHB_REMAP_HI_ADDR(x)		(x)
#define ASPEED_H2X_AHB_ADDR_CONFIG2	0x68
#define  ASPEED_AHB_MASK_HI_ADDR(x)		(x)
#define ASPEED_H2X_DEV_CTRL		0xc0
#define  ASPEED_PCIE_RX_DMA_EN			BIT(9)
#define  ASPEED_PCIE_RX_LINEAR			BIT(8)
#define  ASPEED_PCIE_RX_MSI_SEL			BIT(7)
#define  ASPEED_PCIE_RX_MSI_EN			BIT(6)
#define  ASPEED_PCIE_UNLOCK_RX_BUFF		BIT(4)
#define  ASPEED_PCIE_WAIT_RX_TLP_CLR		BIT(2)
#define  ASPEED_PCIE_RC_RX_ENABLE		BIT(1)
#define  ASPEED_PCIE_RC_ENABLE			BIT(0)
#define ASPEED_H2X_DEV_STS		0xc8
#define  ASPEED_PCIE_RC_RX_DONE_ISR		BIT(4)
#define ASPEED_H2X_DEV_RX_DESC_DATA	0xcc
#define ASPEED_H2X_DEV_RX_DESC1		0xd4
#define ASPEED_H2X_DEV_TX_TAG		0xfc
#define  ASPEED_RC_TLP_TX_TAG_NUM		0x28

/* AST2700 H2X */
#define ASPEED_H2X_CTRL			0x00
#define  ASPEED_H2X_BRIDGE_EN			BIT(0)
#define  ASPEED_H2X_BRIDGE_DIRECT_EN		BIT(1)
#define ASPEED_H2X_CFGE_INT_STS		0x08
#define  ASPEED_CFGE_TX_IDLE			BIT(0)
#define  ASPEED_CFGE_RX_BUSY			BIT(1)
#define ASPEED_H2X_CFGI_TLP		0x20
#define  ASPEED_CFGI_BYTE_EN_MASK		GENMASK(19, 16)
#define  ASPEED_CFGI_BYTE_EN(x) \
		FIELD_PREP(ASPEED_CFGI_BYTE_EN_MASK, (x))
#define ASPEED_H2X_CFGI_WR_DATA		0x24
#define  ASPEED_CFGI_WRITE			BIT(20)
#define ASPEED_H2X_CFGI_CTRL		0x28
#define  ASPEED_CFGI_TLP_FIRE			BIT(0)
#define ASPEED_H2X_CFGI_RET_DATA	0x2c
#define ASPEED_H2X_CFGE_TLP_1ST		0x30
#define ASPEED_H2X_CFGE_TLP_NEXT	0x34
#define ASPEED_H2X_CFGE_CTRL		0x38
#define  ASPEED_CFGE_TLP_FIRE			BIT(0)
#define ASPEED_H2X_CFGE_RET_DATA	0x3c
#define ASPEED_H2X_REMAP_PREF_ADDR	0x70
#define  ASPEED_REMAP_PREF_ADDR_63_32(x)	(x)
#define ASPEED_H2X_REMAP_PCI_ADDR_HI	0x74
#define  ASPEED_REMAP_PCI_ADDR_63_32(x)		(((x) >> 32) & GENMASK(31, 0))
#define ASPEED_H2X_REMAP_PCI_ADDR_LO	0x78
#define  ASPEED_REMAP_PCI_ADDR_31_12(x)		((x) & GENMASK(31, 12))

/* AST2700 SCU */
#define ASPEED_SCU_60			0x60
#define  ASPEED_RC_E2M_PATH_EN			BIT(0)
#define  ASPEED_RC_H2XS_PATH_EN			BIT(16)
#define  ASPEED_RC_H2XD_PATH_EN			BIT(17)
#define  ASPEED_RC_H2XX_PATH_EN			BIT(18)
#define  ASPEED_RC_UPSTREAM_MEM_EN		BIT(19)
#define ASPEED_SCU_64			0x64
#define  ASPEED_RC0_DECODE_DMA_BASE(x)		FIELD_PREP(GENMASK(7, 0), x)
#define  ASPEED_RC0_DECODE_DMA_LIMIT(x)		FIELD_PREP(GENMASK(15, 8), x)
#define  ASPEED_RC1_DECODE_DMA_BASE(x)		FIELD_PREP(GENMASK(23, 16), x)
#define  ASPEED_RC1_DECODE_DMA_LIMIT(x)		FIELD_PREP(GENMASK(31, 24), x)
#define ASPEED_SCU_70			0x70
#define  ASPEED_DISABLE_EP_FUNC			0

/* Macro to combine Fmt and Type into the 8-bit field */
#define ASPEED_TLP_FMT_TYPE(fmt, type)	((((fmt) & 0x7) << 5) | ((type) & 0x1f))
#define ASPEED_TLP_COMMON_FIELDS	GENMASK(31, 24)

/* Completion status */
#define CPL_STS(x)	FIELD_GET(GENMASK(15, 13), (x))
/* TLP configuration type 0 and type 1 */
#define CFG0_READ_FMTTYPE                                        \
	FIELD_PREP(ASPEED_TLP_COMMON_FIELDS,                     \
		   ASPEED_TLP_FMT_TYPE(PCIE_TLP_FMT_3DW_NO_DATA, \
				       PCIE_TLP_TYPE_CFG0_RD))
#define CFG0_WRITE_FMTTYPE                                    \
	FIELD_PREP(ASPEED_TLP_COMMON_FIELDS,                  \
		   ASPEED_TLP_FMT_TYPE(PCIE_TLP_FMT_3DW_DATA, \
				       PCIE_TLP_TYPE_CFG0_WR))
#define CFG1_READ_FMTTYPE                                        \
	FIELD_PREP(ASPEED_TLP_COMMON_FIELDS,                     \
		   ASPEED_TLP_FMT_TYPE(PCIE_TLP_FMT_3DW_NO_DATA, \
				       PCIE_TLP_TYPE_CFG1_RD))
#define CFG1_WRITE_FMTTYPE                                    \
	FIELD_PREP(ASPEED_TLP_COMMON_FIELDS,                  \
		   ASPEED_TLP_FMT_TYPE(PCIE_TLP_FMT_3DW_DATA, \
				       PCIE_TLP_TYPE_CFG1_WR))
#define CFG_PAYLOAD_SIZE		0x01 /* 1 DWORD */
#define TLP_HEADER_BYTE_EN(x, y)	((GENMASK((x) - 1, 0) << ((y) % 4)))
#define TLP_GET_VALUE(x, y, z)	\
	(((x) >> ((((z) % 4)) * 8)) & GENMASK((8 * (y)) - 1, 0))
#define TLP_SET_VALUE(x, y, z)	\
	((((x) & GENMASK((8 * (y)) - 1, 0)) << ((((z) % 4)) * 8)))
#define AST2600_TX_DESC1_VALUE		0x00002000
#define AST2700_TX_DESC1_VALUE		0x00401000

/**
 * struct aspeed_pcie_port - PCIe port information
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @clk: pointer to the port clock gate
 * @phy: pointer to PCIe PHY
 * @perst: pointer to port reset control
 * @slot: port slot
 */
struct aspeed_pcie_port {
	struct list_head list;
	struct aspeed_pcie *pcie;
	struct clk *clk;
	struct phy *phy;
	struct reset_control *perst;
	u32 slot;
};

/**
 * struct aspeed_pcie - PCIe RC information
 * @host: pointer to PCIe host bridge
 * @dev: pointer to device structure
 * @reg: PCIe host register base address
 * @ahbc: pointer to AHHC register map
 * @cfg: pointer to Aspeed PCIe configuration register map
 * @platform: platform specific information
 * @ports: list of PCIe ports
 * @tx_tag: current TX tag for the port
 * @root_bus_nr: bus number of the host bridge
 * @h2xrst: pointer to H2X reset control
 * @intx_domain: IRQ domain for INTx interrupts
 * @msi_domain: IRQ domain for MSI interrupts
 * @lock: mutex to protect MSI bitmap variable
 * @msi_irq_in_use: bitmap to track used MSI host IRQs
 * @clear_msi_twice: AST2700 workaround to clear MSI status twice
 */
struct aspeed_pcie {
	struct pci_host_bridge *host;
	struct device *dev;
	void __iomem *reg;
	struct regmap *ahbc;
	struct regmap *cfg;
	const struct aspeed_pcie_rc_platform *platform;
	struct list_head ports;

	u8 tx_tag;
	u8 root_bus_nr;

	struct reset_control *h2xrst;

	struct irq_domain *intx_domain;
	struct irq_domain *msi_domain;
	struct mutex lock;
	DECLARE_BITMAP(msi_irq_in_use, MAX_MSI_HOST_IRQS);

	bool clear_msi_twice;		/* AST2700 workaround */
};

/**
 * struct aspeed_pcie_rc_platform - Platform information
 * @setup: initialization function
 * @pcie_map_ranges: function to map PCIe address ranges
 * @reg_intx_en: INTx enable register offset
 * @reg_intx_sts: INTx status register offset
 * @reg_msi_en: MSI enable register offset
 * @reg_msi_sts: MSI enable register offset
 * @msi_address: HW fixed MSI address
 */
struct aspeed_pcie_rc_platform {
	int (*setup)(struct platform_device *pdev);
	void (*pcie_map_ranges)(struct aspeed_pcie *pcie, u64 pci_addr);
	int reg_intx_en;
	int reg_intx_sts;
	int reg_msi_en;
	int reg_msi_sts;
	u32 msi_address;
};

static void aspeed_pcie_intx_irq_ack(struct irq_data *d)
{
	struct aspeed_pcie *pcie = irq_data_get_irq_chip_data(d);
	int intx_en = pcie->platform->reg_intx_en;
	u32 en;

	en = readl(pcie->reg + intx_en);
	en |= BIT(d->hwirq);
	writel(en, pcie->reg + intx_en);
}

static void aspeed_pcie_intx_irq_mask(struct irq_data *d)
{
	struct aspeed_pcie *pcie = irq_data_get_irq_chip_data(d);
	int intx_en = pcie->platform->reg_intx_en;
	u32 en;

	en = readl(pcie->reg + intx_en);
	en &= ~BIT(d->hwirq);
	writel(en, pcie->reg + intx_en);
}

static void aspeed_pcie_intx_irq_unmask(struct irq_data *d)
{
	struct aspeed_pcie *pcie = irq_data_get_irq_chip_data(d);
	int intx_en = pcie->platform->reg_intx_en;
	u32 en;

	en = readl(pcie->reg + intx_en);
	en |= BIT(d->hwirq);
	writel(en, pcie->reg + intx_en);
}

static struct irq_chip aspeed_intx_irq_chip = {
	.name = "INTx",
	.irq_ack = aspeed_pcie_intx_irq_ack,
	.irq_mask = aspeed_pcie_intx_irq_mask,
	.irq_unmask = aspeed_pcie_intx_irq_unmask,
};

static int aspeed_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &aspeed_intx_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

static const struct irq_domain_ops aspeed_intx_domain_ops = {
	.map = aspeed_pcie_intx_map,
};

static irqreturn_t aspeed_pcie_intr_handler(int irq, void *dev_id)
{
	struct aspeed_pcie *pcie = dev_id;
	const struct aspeed_pcie_rc_platform *platform = pcie->platform;
	unsigned long status;
	unsigned long intx;
	u32 bit;
	int i;

	intx = FIELD_GET(ASPEED_PCIE_INTX_STS,
			 readl(pcie->reg + platform->reg_intx_sts));
	for_each_set_bit(bit, &intx, PCI_NUM_INTX)
		generic_handle_domain_irq(pcie->intx_domain, bit);

	for (i = 0; i < 2; i++) {
		int msi_sts_reg = platform->reg_msi_sts + (i * 4);

		status = readl(pcie->reg + msi_sts_reg);
		writel(status, pcie->reg + msi_sts_reg);

		/*
		 * AST2700 workaround:
		 * The MSI status needs to clear one more time.
		 */
		if (pcie->clear_msi_twice)
			writel(status, pcie->reg + msi_sts_reg);

		for_each_set_bit(bit, &status, 32) {
			bit += (i * 32);
			generic_handle_domain_irq(pcie->msi_domain, bit);
		}
	}

	return IRQ_HANDLED;
}

static u32 aspeed_pcie_get_bdf_offset(struct pci_bus *bus, unsigned int devfn,
				      int where)
{
	return ((bus->number) << 24) | (PCI_SLOT(devfn) << 19) |
		(PCI_FUNC(devfn) << 16) | (where & ~3);
}

static int aspeed_ast2600_conf(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 *val, u32 fmt_type,
			       bool write)
{
	struct aspeed_pcie *pcie = bus->sysdata;
	u32 bdf_offset, cfg_val, isr;
	int ret;

	bdf_offset = aspeed_pcie_get_bdf_offset(bus, devfn, where);

	/* Driver may set unlock RX buffer before triggering next TX config */
	cfg_val = readl(pcie->reg + ASPEED_H2X_DEV_CTRL);
	writel(ASPEED_PCIE_UNLOCK_RX_BUFF | cfg_val,
	       pcie->reg + ASPEED_H2X_DEV_CTRL);

	cfg_val = fmt_type | CFG_PAYLOAD_SIZE;
	writel(cfg_val, pcie->reg + ASPEED_H2X_TX_DESC0);

	cfg_val = AST2600_TX_DESC1_VALUE |
		  FIELD_PREP(GENMASK(11, 8), pcie->tx_tag) |
		  TLP_HEADER_BYTE_EN(size, where);
	writel(cfg_val, pcie->reg + ASPEED_H2X_TX_DESC1);

	writel(bdf_offset, pcie->reg + ASPEED_H2X_TX_DESC2);
	writel(0, pcie->reg + ASPEED_H2X_TX_DESC3);
	if (write)
		writel(TLP_SET_VALUE(*val, size, where),
		       pcie->reg + ASPEED_H2X_TX_DESC_DATA);

	cfg_val = readl(pcie->reg + ASPEED_H2X_STS);
	cfg_val |= ASPEED_PCIE_TRIGGER_TX;
	writel(cfg_val, pcie->reg + ASPEED_H2X_STS);

	ret = readl_poll_timeout(pcie->reg + ASPEED_H2X_STS, cfg_val,
				 (cfg_val & ASPEED_PCIE_TX_IDLE), 0, 50);
	if (ret) {
		dev_err(pcie->dev,
			"%02x:%02x.%d CR tx timeout sts: 0x%08x\n",
			bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn), cfg_val);
		ret = PCIBIOS_SET_FAILED;
		PCI_SET_ERROR_RESPONSE(val);
		goto out;
	}

	cfg_val = readl(pcie->reg + ASPEED_H2X_INT_STS);
	cfg_val |= ASPEED_PCIE_TX_IDLE_CLEAR;
	writel(cfg_val, pcie->reg + ASPEED_H2X_INT_STS);

	cfg_val = readl(pcie->reg + ASPEED_H2X_STS);
	switch (cfg_val & ASPEED_PCIE_STATUS_OF_TX) {
	case ASPEED_PCIE_RC_H_TX_COMPLETE:
		ret = readl_poll_timeout(pcie->reg + ASPEED_H2X_DEV_STS, isr,
					 (isr & ASPEED_PCIE_RC_RX_DONE_ISR), 0,
					 50);
		if (ret) {
			dev_err(pcie->dev,
				"%02x:%02x.%d CR rx timeout sts: 0x%08x\n",
				bus->number, PCI_SLOT(devfn),
				PCI_FUNC(devfn), isr);
			ret = PCIBIOS_SET_FAILED;
			PCI_SET_ERROR_RESPONSE(val);
			goto out;
		}
		if (!write) {
			cfg_val = readl(pcie->reg + ASPEED_H2X_DEV_RX_DESC1);
			if (CPL_STS(cfg_val) != PCIE_CPL_STS_SUCCESS) {
				ret = PCIBIOS_SET_FAILED;
				PCI_SET_ERROR_RESPONSE(val);
				goto out;
			} else {
				*val = readl(pcie->reg +
					     ASPEED_H2X_DEV_RX_DESC_DATA);
			}
		}
		break;
	case ASPEED_PCIE_STATUS_OF_TX:
		ret = PCIBIOS_SET_FAILED;
		PCI_SET_ERROR_RESPONSE(val);
		goto out;
	default:
		*val = readl(pcie->reg + ASPEED_H2X_HOST_RX_DESC_DATA);
		break;
	}

	cfg_val = readl(pcie->reg + ASPEED_H2X_DEV_CTRL);
	cfg_val |= ASPEED_PCIE_UNLOCK_RX_BUFF;
	writel(cfg_val, pcie->reg + ASPEED_H2X_DEV_CTRL);

	*val = TLP_GET_VALUE(*val, size, where);

	ret = PCIBIOS_SUCCESSFUL;
out:
	cfg_val = readl(pcie->reg + ASPEED_H2X_DEV_STS);
	writel(cfg_val, pcie->reg + ASPEED_H2X_DEV_STS);
	pcie->tx_tag = (pcie->tx_tag + 1) % 0x8;
	return ret;
}

static int aspeed_ast2600_rd_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	/*
	 * AST2600 has only one Root Port on the root bus.
	 */
	if (PCI_SLOT(devfn) != 8)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return aspeed_ast2600_conf(bus, devfn, where, size, val,
				   CFG0_READ_FMTTYPE, false);
}

static int aspeed_ast2600_child_rd_conf(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 *val)
{
	return aspeed_ast2600_conf(bus, devfn, where, size, val,
				   CFG1_READ_FMTTYPE, false);
}

static int aspeed_ast2600_wr_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	/*
	 * AST2600 has only one Root Port on the root bus.
	 */
	if (PCI_SLOT(devfn) != 8)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return aspeed_ast2600_conf(bus, devfn, where, size, &val,
				   CFG0_WRITE_FMTTYPE, true);
}

static int aspeed_ast2600_child_wr_conf(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 val)
{
	return aspeed_ast2600_conf(bus, devfn, where, size, &val,
				   CFG1_WRITE_FMTTYPE, true);
}

static int aspeed_ast2700_config(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val, bool write)
{
	struct aspeed_pcie *pcie = bus->sysdata;
	u32 cfg_val;

	cfg_val = ASPEED_CFGI_BYTE_EN(TLP_HEADER_BYTE_EN(size, where)) |
		  (where & ~3);
	if (write)
		cfg_val |= ASPEED_CFGI_WRITE;
	writel(cfg_val, pcie->reg + ASPEED_H2X_CFGI_TLP);

	writel(TLP_SET_VALUE(*val, size, where),
	       pcie->reg + ASPEED_H2X_CFGI_WR_DATA);
	writel(ASPEED_CFGI_TLP_FIRE, pcie->reg + ASPEED_H2X_CFGI_CTRL);
	*val = readl(pcie->reg + ASPEED_H2X_CFGI_RET_DATA);
	*val = TLP_GET_VALUE(*val, size, where);

	return PCIBIOS_SUCCESSFUL;
}

static int aspeed_ast2700_child_config(struct pci_bus *bus, unsigned int devfn,
				       int where, int size, u32 *val,
				       bool write)
{
	struct aspeed_pcie *pcie = bus->sysdata;
	u32 bdf_offset, status, cfg_val;
	int ret;

	bdf_offset = aspeed_pcie_get_bdf_offset(bus, devfn, where);

	cfg_val = CFG_PAYLOAD_SIZE;
	if (write)
		cfg_val |= (bus->number == (pcie->root_bus_nr + 1)) ?
				   CFG0_WRITE_FMTTYPE :
				   CFG1_WRITE_FMTTYPE;
	else
		cfg_val |= (bus->number == (pcie->root_bus_nr + 1)) ?
				   CFG0_READ_FMTTYPE :
				   CFG1_READ_FMTTYPE;
	writel(cfg_val, pcie->reg + ASPEED_H2X_CFGE_TLP_1ST);

	cfg_val = AST2700_TX_DESC1_VALUE |
		  FIELD_PREP(GENMASK(11, 8), pcie->tx_tag) |
		  TLP_HEADER_BYTE_EN(size, where);
	writel(cfg_val, pcie->reg + ASPEED_H2X_CFGE_TLP_NEXT);

	writel(bdf_offset, pcie->reg + ASPEED_H2X_CFGE_TLP_NEXT);
	if (write)
		writel(TLP_SET_VALUE(*val, size, where),
		       pcie->reg + ASPEED_H2X_CFGE_TLP_NEXT);
	writel(ASPEED_CFGE_TX_IDLE | ASPEED_CFGE_RX_BUSY,
	       pcie->reg + ASPEED_H2X_CFGE_INT_STS);
	writel(ASPEED_CFGE_TLP_FIRE, pcie->reg + ASPEED_H2X_CFGE_CTRL);

	ret = readl_poll_timeout(pcie->reg + ASPEED_H2X_CFGE_INT_STS, status,
				 (status & ASPEED_CFGE_TX_IDLE), 0, 50);
	if (ret) {
		dev_err(pcie->dev,
			"%02x:%02x.%d CR tx timeout sts: 0x%08x\n",
			bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn), status);
		ret = PCIBIOS_SET_FAILED;
		PCI_SET_ERROR_RESPONSE(val);
		goto out;
	}

	ret = readl_poll_timeout(pcie->reg + ASPEED_H2X_CFGE_INT_STS, status,
				 (status & ASPEED_CFGE_RX_BUSY), 0, 50);
	if (ret) {
		dev_err(pcie->dev,
			"%02x:%02x.%d CR rx timeout sts: 0x%08x\n",
			bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn), status);
		ret = PCIBIOS_SET_FAILED;
		PCI_SET_ERROR_RESPONSE(val);
		goto out;
	}
	*val = readl(pcie->reg + ASPEED_H2X_CFGE_RET_DATA);
	*val = TLP_GET_VALUE(*val, size, where);

	ret = PCIBIOS_SUCCESSFUL;
out:
	writel(status, pcie->reg + ASPEED_H2X_CFGE_INT_STS);
	pcie->tx_tag = (pcie->tx_tag + 1) % 0xf;
	return ret;
}

static int aspeed_ast2700_rd_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	/*
	 * AST2700 has only one Root Port on the root bus.
	 */
	if (devfn != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return aspeed_ast2700_config(bus, devfn, where, size, val, false);
}

static int aspeed_ast2700_child_rd_conf(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 *val)
{
	return aspeed_ast2700_child_config(bus, devfn, where, size, val, false);
}

static int aspeed_ast2700_wr_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	/*
	 * AST2700 has only one Root Port on the root bus.
	 */
	if (devfn != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return aspeed_ast2700_config(bus, devfn, where, size, &val, true);
}

static int aspeed_ast2700_child_wr_conf(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 val)
{
	return aspeed_ast2700_child_config(bus, devfn, where, size, &val, true);
}

static struct pci_ops aspeed_ast2600_pcie_ops = {
	.read = aspeed_ast2600_rd_conf,
	.write = aspeed_ast2600_wr_conf,
};

static struct pci_ops aspeed_ast2600_pcie_child_ops = {
	.read = aspeed_ast2600_child_rd_conf,
	.write = aspeed_ast2600_child_wr_conf,
};

static struct pci_ops aspeed_ast2700_pcie_ops = {
	.read = aspeed_ast2700_rd_conf,
	.write = aspeed_ast2700_wr_conf,
};

static struct pci_ops aspeed_ast2700_pcie_child_ops = {
	.read = aspeed_ast2700_child_rd_conf,
	.write = aspeed_ast2700_child_wr_conf,
};

static void aspeed_irq_compose_msi_msg(struct irq_data *data,
				       struct msi_msg *msg)
{
	struct aspeed_pcie *pcie = irq_data_get_irq_chip_data(data);

	msg->address_hi = 0;
	msg->address_lo = pcie->platform->msi_address;
	msg->data = data->hwirq;
}

static struct irq_chip aspeed_msi_bottom_irq_chip = {
	.name = "ASPEED MSI",
	.irq_compose_msi_msg = aspeed_irq_compose_msi_msg,
};

static int aspeed_irq_msi_domain_alloc(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs,
				       void *args)
{
	struct aspeed_pcie *pcie = domain->host_data;
	int bit;
	int i;

	guard(mutex)(&pcie->lock);

	bit = bitmap_find_free_region(pcie->msi_irq_in_use, MAX_MSI_HOST_IRQS,
				      get_count_order(nr_irqs));

	if (bit < 0)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, bit + i,
				    &aspeed_msi_bottom_irq_chip,
				    domain->host_data, handle_simple_irq, NULL,
				    NULL);
	}

	return 0;
}

static void aspeed_irq_msi_domain_free(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct aspeed_pcie *pcie = irq_data_get_irq_chip_data(data);

	guard(mutex)(&pcie->lock);

	bitmap_release_region(pcie->msi_irq_in_use, data->hwirq,
			      get_count_order(nr_irqs));
}

static const struct irq_domain_ops aspeed_msi_domain_ops = {
	.alloc = aspeed_irq_msi_domain_alloc,
	.free = aspeed_irq_msi_domain_free,
};

#define ASPEED_MSI_FLAGS_REQUIRED (MSI_FLAG_USE_DEF_DOM_OPS	| \
				  MSI_FLAG_USE_DEF_CHIP_OPS	| \
				  MSI_FLAG_NO_AFFINITY)

#define ASPEED_MSI_FLAGS_SUPPORTED (MSI_GENERIC_FLAGS_MASK	| \
				   MSI_FLAG_MULTI_PCI_MSI	| \
				   MSI_FLAG_PCI_MSIX)

static const struct msi_parent_ops aspeed_msi_parent_ops = {
	.required_flags		= ASPEED_MSI_FLAGS_REQUIRED,
	.supported_flags	= ASPEED_MSI_FLAGS_SUPPORTED,
	.bus_select_token	= DOMAIN_BUS_PCI_MSI,
	.chip_flags		= MSI_CHIP_FLAG_SET_ACK,
	.prefix			= "ASPEED-",
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};

static int aspeed_pcie_msi_init(struct aspeed_pcie *pcie)
{
	writel(~0, pcie->reg + pcie->platform->reg_msi_en);
	writel(~0, pcie->reg + pcie->platform->reg_msi_en + 0x04);
	writel(~0, pcie->reg + pcie->platform->reg_msi_sts);
	writel(~0, pcie->reg + pcie->platform->reg_msi_sts + 0x04);

	struct irq_domain_info info = {
		.fwnode		= dev_fwnode(pcie->dev),
		.ops		= &aspeed_msi_domain_ops,
		.host_data	= pcie,
		.size		= MAX_MSI_HOST_IRQS,
	};

	pcie->msi_domain = msi_create_parent_irq_domain(&info,
							&aspeed_msi_parent_ops);
	if (!pcie->msi_domain)
		return dev_err_probe(pcie->dev, -ENOMEM,
				     "failed to create MSI domain\n");

	return 0;
}

static void aspeed_pcie_msi_free(struct aspeed_pcie *pcie)
{
	if (pcie->msi_domain) {
		irq_domain_remove(pcie->msi_domain);
		pcie->msi_domain = NULL;
	}
}

static void aspeed_pcie_irq_domain_free(void *d)
{
	struct aspeed_pcie *pcie = d;

	if (pcie->intx_domain) {
		irq_domain_remove(pcie->intx_domain);
		pcie->intx_domain = NULL;
	}
	aspeed_pcie_msi_free(pcie);
}

static int aspeed_pcie_init_irq_domain(struct aspeed_pcie *pcie)
{
	int ret;

	pcie->intx_domain = irq_domain_add_linear(pcie->dev->of_node,
						  PCI_NUM_INTX,
						  &aspeed_intx_domain_ops,
						  pcie);
	if (!pcie->intx_domain) {
		ret = dev_err_probe(pcie->dev, -ENOMEM,
				    "failed to get INTx IRQ domain\n");
		goto err;
	}

	writel(0, pcie->reg + pcie->platform->reg_intx_en);
	writel(~0, pcie->reg + pcie->platform->reg_intx_sts);

	ret = aspeed_pcie_msi_init(pcie);
	if (ret)
		goto err;

	return 0;
err:
	aspeed_pcie_irq_domain_free(pcie);
	return ret;
}

static int aspeed_pcie_port_init(struct aspeed_pcie_port *port)
{
	struct aspeed_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;
	int ret;

	ret = clk_prepare_enable(port->clk);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to set clock for slot (%d)\n",
				     port->slot);

	ret = phy_init(port->phy);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to init phy pcie for slot (%d)\n",
				     port->slot);

	ret = phy_set_mode_ext(port->phy, PHY_MODE_PCIE, PHY_MODE_PCIE_RC);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to set phy mode for slot (%d)\n",
				     port->slot);

	reset_control_deassert(port->perst);
	msleep(PCIE_RESET_CONFIG_WAIT_MS);

	return 0;
}

static void aspeed_host_reset(struct aspeed_pcie *pcie)
{
	reset_control_assert(pcie->h2xrst);
	mdelay(ASPEED_RESET_RC_WAIT_MS);
	reset_control_deassert(pcie->h2xrst);
}

static void aspeed_pcie_map_ranges(struct aspeed_pcie *pcie)
{
	struct pci_host_bridge *bridge = pcie->host;
	struct resource_entry *window;

	resource_list_for_each_entry(window, &bridge->windows) {
		u64 pci_addr;

		if (resource_type(window->res) != IORESOURCE_MEM)
			continue;

		pci_addr = window->res->start - window->offset;
		pcie->platform->pcie_map_ranges(pcie, pci_addr);
		break;
	}
}

static void aspeed_ast2600_pcie_map_ranges(struct aspeed_pcie *pcie,
					  u64 pci_addr)
{
	u32 pci_addr_lo = pci_addr & GENMASK(31, 0);
	u32 pci_addr_hi = (pci_addr >> 32) & GENMASK(31, 0);

	pci_addr_lo >>= 16;
	writel(ASPEED_AHB_REMAP_LO_ADDR(pci_addr_lo) |
	       ASPEED_AHB_MASK_LO_ADDR(0xe00),
	       pcie->reg + ASPEED_H2X_AHB_ADDR_CONFIG0);
	writel(ASPEED_AHB_REMAP_HI_ADDR(pci_addr_hi),
	       pcie->reg + ASPEED_H2X_AHB_ADDR_CONFIG1);
	writel(ASPEED_AHB_MASK_HI_ADDR(~0),
	       pcie->reg + ASPEED_H2X_AHB_ADDR_CONFIG2);
}

static int aspeed_ast2600_setup(struct platform_device *pdev)
{
	struct aspeed_pcie *pcie = platform_get_drvdata(pdev);
	struct device *dev = pcie->dev;

	pcie->ahbc = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "aspeed,ahbc");
	if (IS_ERR(pcie->ahbc))
		return dev_err_probe(dev, PTR_ERR(pcie->ahbc),
				     "failed to map ahbc base\n");

	aspeed_host_reset(pcie);

	regmap_write(pcie->ahbc, ASPEED_AHBC_KEY, ASPEED_AHBC_UNLOCK_KEY);
	regmap_update_bits(pcie->ahbc, ASPEED_AHBC_ADDR_MAPPING,
			   ASPEED_PCIE_RC_MEMORY_EN, ASPEED_PCIE_RC_MEMORY_EN);
	regmap_write(pcie->ahbc, ASPEED_AHBC_KEY, ASPEED_AHBC_UNLOCK);

	writel(ASPEED_H2X_BRIDGE_EN, pcie->reg + ASPEED_H2X_CTRL);

	writel(ASPEED_PCIE_RX_DMA_EN | ASPEED_PCIE_RX_LINEAR |
	       ASPEED_PCIE_RX_MSI_SEL | ASPEED_PCIE_RX_MSI_EN |
	       ASPEED_PCIE_WAIT_RX_TLP_CLR | ASPEED_PCIE_RC_RX_ENABLE |
	       ASPEED_PCIE_RC_ENABLE,
	       pcie->reg + ASPEED_H2X_DEV_CTRL);

	writel(ASPEED_RC_TLP_TX_TAG_NUM, pcie->reg + ASPEED_H2X_DEV_TX_TAG);

	pcie->host->ops = &aspeed_ast2600_pcie_ops;
	pcie->host->child_ops = &aspeed_ast2600_pcie_child_ops;

	return 0;
}

static void aspeed_ast2700_pcie_map_ranges(struct aspeed_pcie *pcie,
					  u64 pci_addr)
{
	writel(ASPEED_REMAP_PCI_ADDR_31_12(pci_addr),
		pcie->reg + ASPEED_H2X_REMAP_PCI_ADDR_LO);
	writel(ASPEED_REMAP_PCI_ADDR_63_32(pci_addr),
		pcie->reg + ASPEED_H2X_REMAP_PCI_ADDR_HI);
}

static int aspeed_ast2700_setup(struct platform_device *pdev)
{
	struct aspeed_pcie *pcie = platform_get_drvdata(pdev);
	struct device *dev = pcie->dev;

	pcie->cfg = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "aspeed,pciecfg");
	if (IS_ERR(pcie->cfg))
		return dev_err_probe(dev, PTR_ERR(pcie->cfg),
				     "failed to map pciecfg base\n");

	regmap_update_bits(pcie->cfg, ASPEED_SCU_60,
			   ASPEED_RC_E2M_PATH_EN | ASPEED_RC_H2XS_PATH_EN |
			   ASPEED_RC_H2XD_PATH_EN | ASPEED_RC_H2XX_PATH_EN |
			   ASPEED_RC_UPSTREAM_MEM_EN,
			   ASPEED_RC_E2M_PATH_EN | ASPEED_RC_H2XS_PATH_EN |
			   ASPEED_RC_H2XD_PATH_EN | ASPEED_RC_H2XX_PATH_EN |
			   ASPEED_RC_UPSTREAM_MEM_EN);
	regmap_write(pcie->cfg, ASPEED_SCU_64,
		     ASPEED_RC0_DECODE_DMA_BASE(0) |
		     ASPEED_RC0_DECODE_DMA_LIMIT(0xff) |
		     ASPEED_RC1_DECODE_DMA_BASE(0) |
		     ASPEED_RC1_DECODE_DMA_LIMIT(0xff));
	regmap_write(pcie->cfg, ASPEED_SCU_70, ASPEED_DISABLE_EP_FUNC);

	aspeed_host_reset(pcie);

	writel(0, pcie->reg + ASPEED_H2X_CTRL);
	writel(ASPEED_H2X_BRIDGE_EN | ASPEED_H2X_BRIDGE_DIRECT_EN,
	       pcie->reg + ASPEED_H2X_CTRL);

	/* Prepare for 64-bit BAR pref */
	writel(ASPEED_REMAP_PREF_ADDR_63_32(0x3),
	       pcie->reg + ASPEED_H2X_REMAP_PREF_ADDR);

	pcie->host->ops = &aspeed_ast2700_pcie_ops;
	pcie->host->child_ops = &aspeed_ast2700_pcie_child_ops;
	pcie->clear_msi_twice = true;

	return 0;
}

static void aspeed_pcie_reset_release(void *d)
{
	struct reset_control *perst = d;

	if (!perst)
		return;

	reset_control_put(perst);
}

static int aspeed_pcie_parse_port(struct aspeed_pcie *pcie,
				  struct device_node *node,
				  int slot)
{
	struct aspeed_pcie_port *port;
	struct device *dev = pcie->dev;
	int ret;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->clk = devm_get_clk_from_child(dev, node, NULL);
	if (IS_ERR(port->clk))
		return dev_err_probe(dev, PTR_ERR(port->clk),
				     "failed to get pcie%d clock\n", slot);

	port->phy = devm_of_phy_get(dev, node, NULL);
	if (IS_ERR(port->phy))
		return dev_err_probe(dev, PTR_ERR(port->phy),
				     "failed to get phy pcie%d\n", slot);

	port->perst = of_reset_control_get_exclusive(node, "perst");
	if (IS_ERR(port->perst))
		return dev_err_probe(dev, PTR_ERR(port->perst),
				     "failed to get pcie%d reset control\n",
				     slot);
	ret = devm_add_action_or_reset(dev, aspeed_pcie_reset_release,
				       port->perst);
	if (ret)
		return ret;
	reset_control_assert(port->perst);

	port->slot = slot;
	port->pcie = pcie;

	INIT_LIST_HEAD(&port->list);
	list_add_tail(&port->list, &pcie->ports);

	ret = aspeed_pcie_port_init(port);
	if (ret)
		return ret;

	return 0;
}

static int aspeed_pcie_parse_dt(struct aspeed_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	int ret;

	for_each_available_child_of_node_scoped(node, child) {
		int slot;
		const char *type;

		ret = of_property_read_string(child, "device_type", &type);
		if (ret || strcmp(type, "pci"))
			continue;

		ret = of_pci_get_devfn(child);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to parse devfn\n");

		slot = PCI_SLOT(ret);

		ret = aspeed_pcie_parse_port(pcie, child, slot);
		if (ret)
			return ret;
	}

	if (list_empty(&pcie->ports))
		return dev_err_probe(dev, -ENODEV,
				     "No PCIe port found in DT\n");

	return 0;
}

static int aspeed_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *host;
	struct aspeed_pcie *pcie;
	struct resource_entry *entry;
	const struct aspeed_pcie_rc_platform *md;
	int irq, ret;

	md = of_device_get_match_data(dev);
	if (!md)
		return -ENODEV;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);
	pcie->dev = dev;
	pcie->tx_tag = 0;
	platform_set_drvdata(pdev, pcie);

	pcie->platform = md;
	pcie->host = host;
	INIT_LIST_HEAD(&pcie->ports);

	/* Get root bus num for cfg command to decide tlp type 0 or type 1 */
	entry = resource_list_first_type(&host->windows, IORESOURCE_BUS);
	if (entry)
		pcie->root_bus_nr = entry->res->start;

	pcie->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pcie->reg))
		return PTR_ERR(pcie->reg);

	pcie->h2xrst = devm_reset_control_get_exclusive(dev, "h2x");
	if (IS_ERR(pcie->h2xrst))
		return dev_err_probe(dev, PTR_ERR(pcie->h2xrst),
				     "failed to get h2x reset\n");

	ret = devm_mutex_init(dev, &pcie->lock);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init mutex\n");

	ret = pcie->platform->setup(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to setup PCIe RC\n");

	aspeed_pcie_map_ranges(pcie);

	ret = aspeed_pcie_parse_dt(pcie);
	if (ret)
		return ret;

	host->sysdata = pcie;

	ret = aspeed_pcie_init_irq_domain(pcie);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_add_action_or_reset(dev, aspeed_pcie_irq_domain_free, pcie);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, irq, aspeed_pcie_intr_handler, IRQF_SHARED,
			       dev_name(dev), pcie);
	if (ret)
		return ret;

	return pci_host_probe(host);
}

static const struct aspeed_pcie_rc_platform pcie_rc_ast2600 = {
	.setup = aspeed_ast2600_setup,
	.pcie_map_ranges = aspeed_ast2600_pcie_map_ranges,
	.reg_intx_en = 0xc4,
	.reg_intx_sts = 0xc8,
	.reg_msi_en = 0xe0,
	.reg_msi_sts = 0xe8,
	.msi_address = 0x1e77005c,
};

static const struct aspeed_pcie_rc_platform pcie_rc_ast2700 = {
	.setup = aspeed_ast2700_setup,
	.pcie_map_ranges = aspeed_ast2700_pcie_map_ranges,
	.reg_intx_en = 0x40,
	.reg_intx_sts = 0x48,
	.reg_msi_en = 0x50,
	.reg_msi_sts = 0x58,
	.msi_address = 0x000000f0,
};

static const struct of_device_id aspeed_pcie_of_match[] = {
	{ .compatible = "aspeed,ast2600-pcie", .data = &pcie_rc_ast2600 },
	{ .compatible = "aspeed,ast2700-pcie", .data = &pcie_rc_ast2700 },
	{}
};

static struct platform_driver aspeed_pcie_driver = {
	.driver = {
		.name = "aspeed-pcie",
		.of_match_table = aspeed_pcie_of_match,
		.suppress_bind_attrs = true,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = aspeed_pcie_probe,
};

builtin_platform_driver(aspeed_pcie_driver);

MODULE_AUTHOR("Jacky Chou <jacky_chou@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED PCIe Root Complex");
MODULE_LICENSE("GPL");
