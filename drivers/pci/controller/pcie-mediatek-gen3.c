// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek PCIe host controller driver.
 *
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Jianjun Wang <jianjun.wang@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_device.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "../pci.h"

#define PCIE_SETTING_REG		0x80
#define PCIE_PCI_IDS_1			0x9c
#define PCI_CLASS(class)		(class << 8)
#define PCIE_RC_MODE			BIT(0)

#define PCIE_EQ_PRESET_01_REG		0x100
#define PCIE_VAL_LN0_DOWNSTREAM		GENMASK(6, 0)
#define PCIE_VAL_LN0_UPSTREAM		GENMASK(14, 8)
#define PCIE_VAL_LN1_DOWNSTREAM		GENMASK(22, 16)
#define PCIE_VAL_LN1_UPSTREAM		GENMASK(30, 24)

#define PCIE_CFGNUM_REG			0x140
#define PCIE_CFG_DEVFN(devfn)		((devfn) & GENMASK(7, 0))
#define PCIE_CFG_BUS(bus)		(((bus) << 8) & GENMASK(15, 8))
#define PCIE_CFG_BYTE_EN(bytes)		(((bytes) << 16) & GENMASK(19, 16))
#define PCIE_CFG_FORCE_BYTE_EN		BIT(20)
#define PCIE_CFG_OFFSET_ADDR		0x1000
#define PCIE_CFG_HEADER(bus, devfn) \
	(PCIE_CFG_BUS(bus) | PCIE_CFG_DEVFN(devfn))

#define PCIE_RST_CTRL_REG		0x148
#define PCIE_MAC_RSTB			BIT(0)
#define PCIE_PHY_RSTB			BIT(1)
#define PCIE_BRG_RSTB			BIT(2)
#define PCIE_PE_RSTB			BIT(3)

#define PCIE_LTSSM_STATUS_REG		0x150
#define PCIE_LTSSM_STATE_MASK		GENMASK(28, 24)
#define PCIE_LTSSM_STATE(val)		((val & PCIE_LTSSM_STATE_MASK) >> 24)
#define PCIE_LTSSM_STATE_L2_IDLE	0x14

#define PCIE_LINK_STATUS_REG		0x154
#define PCIE_PORT_LINKUP		BIT(8)

#define PCIE_MSI_SET_NUM		8
#define PCIE_MSI_IRQS_PER_SET		32
#define PCIE_MSI_IRQS_NUM \
	(PCIE_MSI_IRQS_PER_SET * PCIE_MSI_SET_NUM)

#define PCIE_INT_ENABLE_REG		0x180
#define PCIE_MSI_ENABLE			GENMASK(PCIE_MSI_SET_NUM + 8 - 1, 8)
#define PCIE_MSI_SHIFT			8
#define PCIE_INTX_SHIFT			24
#define PCIE_INTX_ENABLE \
	GENMASK(PCIE_INTX_SHIFT + PCI_NUM_INTX - 1, PCIE_INTX_SHIFT)

#define PCIE_INT_STATUS_REG		0x184
#define PCIE_MSI_SET_ENABLE_REG		0x190
#define PCIE_MSI_SET_ENABLE		GENMASK(PCIE_MSI_SET_NUM - 1, 0)

#define PCIE_PIPE4_PIE8_REG		0x338
#define PCIE_K_FINETUNE_MAX		GENMASK(5, 0)
#define PCIE_K_FINETUNE_ERR		GENMASK(7, 6)
#define PCIE_K_PRESET_TO_USE		GENMASK(18, 8)
#define PCIE_K_PHYPARAM_QUERY		BIT(19)
#define PCIE_K_QUERY_TIMEOUT		BIT(20)
#define PCIE_K_PRESET_TO_USE_16G	GENMASK(31, 21)

#define PCIE_MSI_SET_BASE_REG		0xc00
#define PCIE_MSI_SET_OFFSET		0x10
#define PCIE_MSI_SET_STATUS_OFFSET	0x04
#define PCIE_MSI_SET_ENABLE_OFFSET	0x08

#define PCIE_MSI_SET_ADDR_HI_BASE	0xc80
#define PCIE_MSI_SET_ADDR_HI_OFFSET	0x04

#define PCIE_ICMD_PM_REG		0x198
#define PCIE_TURN_OFF_LINK		BIT(4)

#define PCIE_MISC_CTRL_REG		0x348
#define PCIE_DISABLE_DVFSRC_VLT_REQ	BIT(1)

#define PCIE_TRANS_TABLE_BASE_REG	0x800
#define PCIE_ATR_SRC_ADDR_MSB_OFFSET	0x4
#define PCIE_ATR_TRSL_ADDR_LSB_OFFSET	0x8
#define PCIE_ATR_TRSL_ADDR_MSB_OFFSET	0xc
#define PCIE_ATR_TRSL_PARAM_OFFSET	0x10
#define PCIE_ATR_TLB_SET_OFFSET		0x20

#define PCIE_MAX_TRANS_TABLES		8
#define PCIE_ATR_EN			BIT(0)
#define PCIE_ATR_SIZE(size) \
	(((((size) - 1) << 1) & GENMASK(6, 1)) | PCIE_ATR_EN)
#define PCIE_ATR_ID(id)			((id) & GENMASK(3, 0))
#define PCIE_ATR_TYPE_MEM		PCIE_ATR_ID(0)
#define PCIE_ATR_TYPE_IO		PCIE_ATR_ID(1)
#define PCIE_ATR_TLP_TYPE(type)		(((type) << 16) & GENMASK(18, 16))
#define PCIE_ATR_TLP_TYPE_MEM		PCIE_ATR_TLP_TYPE(0)
#define PCIE_ATR_TLP_TYPE_IO		PCIE_ATR_TLP_TYPE(2)

#define MAX_NUM_PHY_RESETS		3

/* Time in ms needed to complete PCIe reset on EN7581 SoC */
#define PCIE_EN7581_RESET_TIME_MS	100

struct mtk_gen3_pcie;

/**
 * struct mtk_gen3_pcie_pdata - differentiate between host generations
 * @power_up: pcie power_up callback
 * @phy_resets: phy reset lines SoC data.
 */
struct mtk_gen3_pcie_pdata {
	int (*power_up)(struct mtk_gen3_pcie *pcie);
	struct {
		const char *id[MAX_NUM_PHY_RESETS];
		int num_resets;
	} phy_resets;
};

/**
 * struct mtk_msi_set - MSI information for each set
 * @base: IO mapped register base
 * @msg_addr: MSI message address
 * @saved_irq_state: IRQ enable state saved at suspend time
 */
struct mtk_msi_set {
	void __iomem *base;
	phys_addr_t msg_addr;
	u32 saved_irq_state;
};

/**
 * struct mtk_gen3_pcie - PCIe port information
 * @dev: pointer to PCIe device
 * @base: IO mapped register base
 * @reg_base: physical register base
 * @mac_reset: MAC reset control
 * @phy_resets: PHY reset controllers
 * @phy: PHY controller block
 * @clks: PCIe clocks
 * @num_clks: PCIe clocks count for this port
 * @irq: PCIe controller interrupt number
 * @saved_irq_state: IRQ enable state saved at suspend time
 * @irq_lock: lock protecting IRQ register access
 * @intx_domain: legacy INTx IRQ domain
 * @msi_domain: MSI IRQ domain
 * @msi_bottom_domain: MSI IRQ bottom domain
 * @msi_sets: MSI sets information
 * @lock: lock protecting IRQ bit map
 * @msi_irq_in_use: bit map for assigned MSI IRQ
 * @soc: pointer to SoC-dependent operations
 */
struct mtk_gen3_pcie {
	struct device *dev;
	void __iomem *base;
	phys_addr_t reg_base;
	struct reset_control *mac_reset;
	struct reset_control_bulk_data phy_resets[MAX_NUM_PHY_RESETS];
	struct phy *phy;
	struct clk_bulk_data *clks;
	int num_clks;

	int irq;
	u32 saved_irq_state;
	raw_spinlock_t irq_lock;
	struct irq_domain *intx_domain;
	struct irq_domain *msi_domain;
	struct irq_domain *msi_bottom_domain;
	struct mtk_msi_set msi_sets[PCIE_MSI_SET_NUM];
	struct mutex lock;
	DECLARE_BITMAP(msi_irq_in_use, PCIE_MSI_IRQS_NUM);

	const struct mtk_gen3_pcie_pdata *soc;
};

/* LTSSM state in PCIE_LTSSM_STATUS_REG bit[28:24] */
static const char *const ltssm_str[] = {
	"detect.quiet",			/* 0x00 */
	"detect.active",		/* 0x01 */
	"polling.active",		/* 0x02 */
	"polling.compliance",		/* 0x03 */
	"polling.configuration",	/* 0x04 */
	"config.linkwidthstart",	/* 0x05 */
	"config.linkwidthaccept",	/* 0x06 */
	"config.lanenumwait",		/* 0x07 */
	"config.lanenumaccept",		/* 0x08 */
	"config.complete",		/* 0x09 */
	"config.idle",			/* 0x0A */
	"recovery.receiverlock",	/* 0x0B */
	"recovery.equalization",	/* 0x0C */
	"recovery.speed",		/* 0x0D */
	"recovery.receiverconfig",	/* 0x0E */
	"recovery.idle",		/* 0x0F */
	"L0",				/* 0x10 */
	"L0s",				/* 0x11 */
	"L1.entry",			/* 0x12 */
	"L1.idle",			/* 0x13 */
	"L2.idle",			/* 0x14 */
	"L2.transmitwake",		/* 0x15 */
	"disable",			/* 0x16 */
	"loopback.entry",		/* 0x17 */
	"loopback.active",		/* 0x18 */
	"loopback.exit",		/* 0x19 */
	"hotreset",			/* 0x1A */
};

/**
 * mtk_pcie_config_tlp_header() - Configure a configuration TLP header
 * @bus: PCI bus to query
 * @devfn: device/function number
 * @where: offset in config space
 * @size: data size in TLP header
 *
 * Set byte enable field and device information in configuration TLP header.
 */
static void mtk_pcie_config_tlp_header(struct pci_bus *bus, unsigned int devfn,
					int where, int size)
{
	struct mtk_gen3_pcie *pcie = bus->sysdata;
	int bytes;
	u32 val;

	bytes = (GENMASK(size - 1, 0) & 0xf) << (where & 0x3);

	val = PCIE_CFG_FORCE_BYTE_EN | PCIE_CFG_BYTE_EN(bytes) |
	      PCIE_CFG_HEADER(bus->number, devfn);

	writel_relaxed(val, pcie->base + PCIE_CFGNUM_REG);
}

static void __iomem *mtk_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
				      int where)
{
	struct mtk_gen3_pcie *pcie = bus->sysdata;

	return pcie->base + PCIE_CFG_OFFSET_ADDR + where;
}

static int mtk_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	mtk_pcie_config_tlp_header(bus, devfn, where, size);

	return pci_generic_config_read32(bus, devfn, where, size, val);
}

static int mtk_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	mtk_pcie_config_tlp_header(bus, devfn, where, size);

	if (size <= 2)
		val <<= (where & 0x3) * 8;

	return pci_generic_config_write32(bus, devfn, where, 4, val);
}

static struct pci_ops mtk_pcie_ops = {
	.map_bus = mtk_pcie_map_bus,
	.read  = mtk_pcie_config_read,
	.write = mtk_pcie_config_write,
};

static int mtk_pcie_set_trans_table(struct mtk_gen3_pcie *pcie,
				    resource_size_t cpu_addr,
				    resource_size_t pci_addr,
				    resource_size_t size,
				    unsigned long type, int *num)
{
	resource_size_t remaining = size;
	resource_size_t table_size;
	resource_size_t addr_align;
	const char *range_type;
	void __iomem *table;
	u32 val;

	while (remaining && (*num < PCIE_MAX_TRANS_TABLES)) {
		/* Table size needs to be a power of 2 */
		table_size = BIT(fls(remaining) - 1);

		if (cpu_addr > 0) {
			addr_align = BIT(ffs(cpu_addr) - 1);
			table_size = min(table_size, addr_align);
		}

		/* Minimum size of translate table is 4KiB */
		if (table_size < 0x1000) {
			dev_err(pcie->dev, "illegal table size %#llx\n",
				(unsigned long long)table_size);
			return -EINVAL;
		}

		table = pcie->base + PCIE_TRANS_TABLE_BASE_REG + *num * PCIE_ATR_TLB_SET_OFFSET;
		writel_relaxed(lower_32_bits(cpu_addr) | PCIE_ATR_SIZE(fls(table_size) - 1), table);
		writel_relaxed(upper_32_bits(cpu_addr), table + PCIE_ATR_SRC_ADDR_MSB_OFFSET);
		writel_relaxed(lower_32_bits(pci_addr), table + PCIE_ATR_TRSL_ADDR_LSB_OFFSET);
		writel_relaxed(upper_32_bits(pci_addr), table + PCIE_ATR_TRSL_ADDR_MSB_OFFSET);

		if (type == IORESOURCE_IO) {
			val = PCIE_ATR_TYPE_IO | PCIE_ATR_TLP_TYPE_IO;
			range_type = "IO";
		} else {
			val = PCIE_ATR_TYPE_MEM | PCIE_ATR_TLP_TYPE_MEM;
			range_type = "MEM";
		}

		writel_relaxed(val, table + PCIE_ATR_TRSL_PARAM_OFFSET);

		dev_dbg(pcie->dev, "set %s trans window[%d]: cpu_addr = %#llx, pci_addr = %#llx, size = %#llx\n",
			range_type, *num, (unsigned long long)cpu_addr,
			(unsigned long long)pci_addr, (unsigned long long)table_size);

		cpu_addr += table_size;
		pci_addr += table_size;
		remaining -= table_size;
		(*num)++;
	}

	if (remaining)
		dev_warn(pcie->dev, "not enough translate table for addr: %#llx, limited to [%d]\n",
			 (unsigned long long)cpu_addr, PCIE_MAX_TRANS_TABLES);

	return 0;
}

static void mtk_pcie_enable_msi(struct mtk_gen3_pcie *pcie)
{
	int i;
	u32 val;

	for (i = 0; i < PCIE_MSI_SET_NUM; i++) {
		struct mtk_msi_set *msi_set = &pcie->msi_sets[i];

		msi_set->base = pcie->base + PCIE_MSI_SET_BASE_REG +
				i * PCIE_MSI_SET_OFFSET;
		msi_set->msg_addr = pcie->reg_base + PCIE_MSI_SET_BASE_REG +
				    i * PCIE_MSI_SET_OFFSET;

		/* Configure the MSI capture address */
		writel_relaxed(lower_32_bits(msi_set->msg_addr), msi_set->base);
		writel_relaxed(upper_32_bits(msi_set->msg_addr),
			       pcie->base + PCIE_MSI_SET_ADDR_HI_BASE +
			       i * PCIE_MSI_SET_ADDR_HI_OFFSET);
	}

	val = readl_relaxed(pcie->base + PCIE_MSI_SET_ENABLE_REG);
	val |= PCIE_MSI_SET_ENABLE;
	writel_relaxed(val, pcie->base + PCIE_MSI_SET_ENABLE_REG);

	val = readl_relaxed(pcie->base + PCIE_INT_ENABLE_REG);
	val |= PCIE_MSI_ENABLE;
	writel_relaxed(val, pcie->base + PCIE_INT_ENABLE_REG);
}

static int mtk_pcie_startup_port(struct mtk_gen3_pcie *pcie)
{
	struct resource_entry *entry;
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	unsigned int table_index = 0;
	int err;
	u32 val;

	/* Set as RC mode */
	val = readl_relaxed(pcie->base + PCIE_SETTING_REG);
	val |= PCIE_RC_MODE;
	writel_relaxed(val, pcie->base + PCIE_SETTING_REG);

	/* Set class code */
	val = readl_relaxed(pcie->base + PCIE_PCI_IDS_1);
	val &= ~GENMASK(31, 8);
	val |= PCI_CLASS(PCI_CLASS_BRIDGE_PCI_NORMAL);
	writel_relaxed(val, pcie->base + PCIE_PCI_IDS_1);

	/* Mask all INTx interrupts */
	val = readl_relaxed(pcie->base + PCIE_INT_ENABLE_REG);
	val &= ~PCIE_INTX_ENABLE;
	writel_relaxed(val, pcie->base + PCIE_INT_ENABLE_REG);

	/* Disable DVFSRC voltage request */
	val = readl_relaxed(pcie->base + PCIE_MISC_CTRL_REG);
	val |= PCIE_DISABLE_DVFSRC_VLT_REQ;
	writel_relaxed(val, pcie->base + PCIE_MISC_CTRL_REG);

	/* Assert all reset signals */
	val = readl_relaxed(pcie->base + PCIE_RST_CTRL_REG);
	val |= PCIE_MAC_RSTB | PCIE_PHY_RSTB | PCIE_BRG_RSTB | PCIE_PE_RSTB;
	writel_relaxed(val, pcie->base + PCIE_RST_CTRL_REG);

	/*
	 * Described in PCIe CEM specification sections 2.2 (PERST# Signal)
	 * and 2.2.1 (Initial Power-Up (G3 to S0)).
	 * The deassertion of PERST# should be delayed 100ms (TPVPERL)
	 * for the power and clock to become stable.
	 */
	msleep(100);

	/* De-assert reset signals */
	val &= ~(PCIE_MAC_RSTB | PCIE_PHY_RSTB | PCIE_BRG_RSTB | PCIE_PE_RSTB);
	writel_relaxed(val, pcie->base + PCIE_RST_CTRL_REG);

	/* Check if the link is up or not */
	err = readl_poll_timeout(pcie->base + PCIE_LINK_STATUS_REG, val,
				 !!(val & PCIE_PORT_LINKUP), 20,
				 PCI_PM_D3COLD_WAIT * USEC_PER_MSEC);
	if (err) {
		const char *ltssm_state;
		int ltssm_index;

		val = readl_relaxed(pcie->base + PCIE_LTSSM_STATUS_REG);
		ltssm_index = PCIE_LTSSM_STATE(val);
		ltssm_state = ltssm_index >= ARRAY_SIZE(ltssm_str) ?
			      "Unknown state" : ltssm_str[ltssm_index];
		dev_err(pcie->dev,
			"PCIe link down, current LTSSM state: %s (%#x)\n",
			ltssm_state, val);
		return err;
	}

	mtk_pcie_enable_msi(pcie);

	/* Set PCIe translation windows */
	resource_list_for_each_entry(entry, &host->windows) {
		struct resource *res = entry->res;
		unsigned long type = resource_type(res);
		resource_size_t cpu_addr;
		resource_size_t pci_addr;
		resource_size_t size;

		if (type == IORESOURCE_IO)
			cpu_addr = pci_pio_to_address(res->start);
		else if (type == IORESOURCE_MEM)
			cpu_addr = res->start;
		else
			continue;

		pci_addr = res->start - entry->offset;
		size = resource_size(res);
		err = mtk_pcie_set_trans_table(pcie, cpu_addr, pci_addr, size,
					       type, &table_index);
		if (err)
			return err;
	}

	return 0;
}

static void mtk_pcie_msi_irq_mask(struct irq_data *data)
{
	pci_msi_mask_irq(data);
	irq_chip_mask_parent(data);
}

static void mtk_pcie_msi_irq_unmask(struct irq_data *data)
{
	pci_msi_unmask_irq(data);
	irq_chip_unmask_parent(data);
}

static struct irq_chip mtk_msi_irq_chip = {
	.irq_ack = irq_chip_ack_parent,
	.irq_mask = mtk_pcie_msi_irq_mask,
	.irq_unmask = mtk_pcie_msi_irq_unmask,
	.name = "MSI",
};

static struct msi_domain_info mtk_msi_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_NO_AFFINITY | MSI_FLAG_PCI_MSIX |
		  MSI_FLAG_MULTI_PCI_MSI,
	.chip	= &mtk_msi_irq_chip,
};

static void mtk_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct mtk_msi_set *msi_set = irq_data_get_irq_chip_data(data);
	struct mtk_gen3_pcie *pcie = data->domain->host_data;
	unsigned long hwirq;

	hwirq =	data->hwirq % PCIE_MSI_IRQS_PER_SET;

	msg->address_hi = upper_32_bits(msi_set->msg_addr);
	msg->address_lo = lower_32_bits(msi_set->msg_addr);
	msg->data = hwirq;
	dev_dbg(pcie->dev, "msi#%#lx address_hi %#x address_lo %#x data %d\n",
		hwirq, msg->address_hi, msg->address_lo, msg->data);
}

static void mtk_msi_bottom_irq_ack(struct irq_data *data)
{
	struct mtk_msi_set *msi_set = irq_data_get_irq_chip_data(data);
	unsigned long hwirq;

	hwirq =	data->hwirq % PCIE_MSI_IRQS_PER_SET;

	writel_relaxed(BIT(hwirq), msi_set->base + PCIE_MSI_SET_STATUS_OFFSET);
}

static void mtk_msi_bottom_irq_mask(struct irq_data *data)
{
	struct mtk_msi_set *msi_set = irq_data_get_irq_chip_data(data);
	struct mtk_gen3_pcie *pcie = data->domain->host_data;
	unsigned long hwirq, flags;
	u32 val;

	hwirq =	data->hwirq % PCIE_MSI_IRQS_PER_SET;

	raw_spin_lock_irqsave(&pcie->irq_lock, flags);
	val = readl_relaxed(msi_set->base + PCIE_MSI_SET_ENABLE_OFFSET);
	val &= ~BIT(hwirq);
	writel_relaxed(val, msi_set->base + PCIE_MSI_SET_ENABLE_OFFSET);
	raw_spin_unlock_irqrestore(&pcie->irq_lock, flags);
}

static void mtk_msi_bottom_irq_unmask(struct irq_data *data)
{
	struct mtk_msi_set *msi_set = irq_data_get_irq_chip_data(data);
	struct mtk_gen3_pcie *pcie = data->domain->host_data;
	unsigned long hwirq, flags;
	u32 val;

	hwirq =	data->hwirq % PCIE_MSI_IRQS_PER_SET;

	raw_spin_lock_irqsave(&pcie->irq_lock, flags);
	val = readl_relaxed(msi_set->base + PCIE_MSI_SET_ENABLE_OFFSET);
	val |= BIT(hwirq);
	writel_relaxed(val, msi_set->base + PCIE_MSI_SET_ENABLE_OFFSET);
	raw_spin_unlock_irqrestore(&pcie->irq_lock, flags);
}

static struct irq_chip mtk_msi_bottom_irq_chip = {
	.irq_ack		= mtk_msi_bottom_irq_ack,
	.irq_mask		= mtk_msi_bottom_irq_mask,
	.irq_unmask		= mtk_msi_bottom_irq_unmask,
	.irq_compose_msi_msg	= mtk_compose_msi_msg,
	.name			= "MSI",
};

static int mtk_msi_bottom_domain_alloc(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs,
				       void *arg)
{
	struct mtk_gen3_pcie *pcie = domain->host_data;
	struct mtk_msi_set *msi_set;
	int i, hwirq, set_idx;

	mutex_lock(&pcie->lock);

	hwirq = bitmap_find_free_region(pcie->msi_irq_in_use, PCIE_MSI_IRQS_NUM,
					order_base_2(nr_irqs));

	mutex_unlock(&pcie->lock);

	if (hwirq < 0)
		return -ENOSPC;

	set_idx = hwirq / PCIE_MSI_IRQS_PER_SET;
	msi_set = &pcie->msi_sets[set_idx];

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &mtk_msi_bottom_irq_chip, msi_set,
				    handle_edge_irq, NULL, NULL);

	return 0;
}

static void mtk_msi_bottom_domain_free(struct irq_domain *domain,
				       unsigned int virq, unsigned int nr_irqs)
{
	struct mtk_gen3_pcie *pcie = domain->host_data;
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);

	mutex_lock(&pcie->lock);

	bitmap_release_region(pcie->msi_irq_in_use, data->hwirq,
			      order_base_2(nr_irqs));

	mutex_unlock(&pcie->lock);

	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static const struct irq_domain_ops mtk_msi_bottom_domain_ops = {
	.alloc = mtk_msi_bottom_domain_alloc,
	.free = mtk_msi_bottom_domain_free,
};

static void mtk_intx_mask(struct irq_data *data)
{
	struct mtk_gen3_pcie *pcie = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pcie->irq_lock, flags);
	val = readl_relaxed(pcie->base + PCIE_INT_ENABLE_REG);
	val &= ~BIT(data->hwirq + PCIE_INTX_SHIFT);
	writel_relaxed(val, pcie->base + PCIE_INT_ENABLE_REG);
	raw_spin_unlock_irqrestore(&pcie->irq_lock, flags);
}

static void mtk_intx_unmask(struct irq_data *data)
{
	struct mtk_gen3_pcie *pcie = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pcie->irq_lock, flags);
	val = readl_relaxed(pcie->base + PCIE_INT_ENABLE_REG);
	val |= BIT(data->hwirq + PCIE_INTX_SHIFT);
	writel_relaxed(val, pcie->base + PCIE_INT_ENABLE_REG);
	raw_spin_unlock_irqrestore(&pcie->irq_lock, flags);
}

/**
 * mtk_intx_eoi() - Clear INTx IRQ status at the end of interrupt
 * @data: pointer to chip specific data
 *
 * As an emulated level IRQ, its interrupt status will remain
 * until the corresponding de-assert message is received; hence that
 * the status can only be cleared when the interrupt has been serviced.
 */
static void mtk_intx_eoi(struct irq_data *data)
{
	struct mtk_gen3_pcie *pcie = irq_data_get_irq_chip_data(data);
	unsigned long hwirq;

	hwirq = data->hwirq + PCIE_INTX_SHIFT;
	writel_relaxed(BIT(hwirq), pcie->base + PCIE_INT_STATUS_REG);
}

static struct irq_chip mtk_intx_irq_chip = {
	.irq_mask		= mtk_intx_mask,
	.irq_unmask		= mtk_intx_unmask,
	.irq_eoi		= mtk_intx_eoi,
	.name			= "INTx",
};

static int mtk_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, domain->host_data);
	irq_set_chip_and_handler_name(irq, &mtk_intx_irq_chip,
				      handle_fasteoi_irq, "INTx");
	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = mtk_pcie_intx_map,
};

static int mtk_pcie_init_irq_domains(struct mtk_gen3_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *intc_node, *node = dev->of_node;
	int ret;

	raw_spin_lock_init(&pcie->irq_lock);

	/* Setup INTx */
	intc_node = of_get_child_by_name(node, "interrupt-controller");
	if (!intc_node) {
		dev_err(dev, "missing interrupt-controller node\n");
		return -ENODEV;
	}

	pcie->intx_domain = irq_domain_add_linear(intc_node, PCI_NUM_INTX,
						  &intx_domain_ops, pcie);
	if (!pcie->intx_domain) {
		dev_err(dev, "failed to create INTx IRQ domain\n");
		ret = -ENODEV;
		goto out_put_node;
	}

	/* Setup MSI */
	mutex_init(&pcie->lock);

	pcie->msi_bottom_domain = irq_domain_add_linear(node, PCIE_MSI_IRQS_NUM,
				  &mtk_msi_bottom_domain_ops, pcie);
	if (!pcie->msi_bottom_domain) {
		dev_err(dev, "failed to create MSI bottom domain\n");
		ret = -ENODEV;
		goto err_msi_bottom_domain;
	}

	pcie->msi_domain = pci_msi_create_irq_domain(dev->fwnode,
						     &mtk_msi_domain_info,
						     pcie->msi_bottom_domain);
	if (!pcie->msi_domain) {
		dev_err(dev, "failed to create MSI domain\n");
		ret = -ENODEV;
		goto err_msi_domain;
	}

	of_node_put(intc_node);
	return 0;

err_msi_domain:
	irq_domain_remove(pcie->msi_bottom_domain);
err_msi_bottom_domain:
	irq_domain_remove(pcie->intx_domain);
out_put_node:
	of_node_put(intc_node);
	return ret;
}

static void mtk_pcie_irq_teardown(struct mtk_gen3_pcie *pcie)
{
	irq_set_chained_handler_and_data(pcie->irq, NULL, NULL);

	if (pcie->intx_domain)
		irq_domain_remove(pcie->intx_domain);

	if (pcie->msi_domain)
		irq_domain_remove(pcie->msi_domain);

	if (pcie->msi_bottom_domain)
		irq_domain_remove(pcie->msi_bottom_domain);

	irq_dispose_mapping(pcie->irq);
}

static void mtk_pcie_msi_handler(struct mtk_gen3_pcie *pcie, int set_idx)
{
	struct mtk_msi_set *msi_set = &pcie->msi_sets[set_idx];
	unsigned long msi_enable, msi_status;
	irq_hw_number_t bit, hwirq;

	msi_enable = readl_relaxed(msi_set->base + PCIE_MSI_SET_ENABLE_OFFSET);

	do {
		msi_status = readl_relaxed(msi_set->base +
					   PCIE_MSI_SET_STATUS_OFFSET);
		msi_status &= msi_enable;
		if (!msi_status)
			break;

		for_each_set_bit(bit, &msi_status, PCIE_MSI_IRQS_PER_SET) {
			hwirq = bit + set_idx * PCIE_MSI_IRQS_PER_SET;
			generic_handle_domain_irq(pcie->msi_bottom_domain, hwirq);
		}
	} while (true);
}

static void mtk_pcie_irq_handler(struct irq_desc *desc)
{
	struct mtk_gen3_pcie *pcie = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long status;
	irq_hw_number_t irq_bit = PCIE_INTX_SHIFT;

	chained_irq_enter(irqchip, desc);

	status = readl_relaxed(pcie->base + PCIE_INT_STATUS_REG);
	for_each_set_bit_from(irq_bit, &status, PCI_NUM_INTX +
			      PCIE_INTX_SHIFT)
		generic_handle_domain_irq(pcie->intx_domain,
					  irq_bit - PCIE_INTX_SHIFT);

	irq_bit = PCIE_MSI_SHIFT;
	for_each_set_bit_from(irq_bit, &status, PCIE_MSI_SET_NUM +
			      PCIE_MSI_SHIFT) {
		mtk_pcie_msi_handler(pcie, irq_bit - PCIE_MSI_SHIFT);

		writel_relaxed(BIT(irq_bit), pcie->base + PCIE_INT_STATUS_REG);
	}

	chained_irq_exit(irqchip, desc);
}

static int mtk_pcie_setup_irq(struct mtk_gen3_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int err;

	err = mtk_pcie_init_irq_domains(pcie);
	if (err)
		return err;

	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq < 0)
		return pcie->irq;

	irq_set_chained_handler_and_data(pcie->irq, mtk_pcie_irq_handler, pcie);

	return 0;
}

static int mtk_pcie_parse_port(struct mtk_gen3_pcie *pcie)
{
	int i, ret, num_resets = pcie->soc->phy_resets.num_resets;
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *regs;

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcie-mac");
	if (!regs)
		return -EINVAL;
	pcie->base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(pcie->base)) {
		dev_err(dev, "failed to map register base\n");
		return PTR_ERR(pcie->base);
	}

	pcie->reg_base = regs->start;

	for (i = 0; i < num_resets; i++)
		pcie->phy_resets[i].id = pcie->soc->phy_resets.id[i];

	ret = devm_reset_control_bulk_get_optional_shared(dev, num_resets, pcie->phy_resets);
	if (ret) {
		dev_err(dev, "failed to get PHY bulk reset\n");
		return ret;
	}

	pcie->mac_reset = devm_reset_control_get_optional_exclusive(dev, "mac");
	if (IS_ERR(pcie->mac_reset)) {
		ret = PTR_ERR(pcie->mac_reset);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get MAC reset\n");

		return ret;
	}

	pcie->phy = devm_phy_optional_get(dev, "pcie-phy");
	if (IS_ERR(pcie->phy)) {
		ret = PTR_ERR(pcie->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get PHY\n");

		return ret;
	}

	pcie->num_clks = devm_clk_bulk_get_all(dev, &pcie->clks);
	if (pcie->num_clks < 0) {
		dev_err(dev, "failed to get clocks\n");
		return pcie->num_clks;
	}

	return 0;
}

static int mtk_pcie_en7581_power_up(struct mtk_gen3_pcie *pcie)
{
	struct device *dev = pcie->dev;
	int err;
	u32 val;

	/*
	 * Wait for the time needed to complete the bulk assert in
	 * mtk_pcie_setup for EN7581 SoC.
	 */
	mdelay(PCIE_EN7581_RESET_TIME_MS);

	err = phy_init(pcie->phy);
	if (err) {
		dev_err(dev, "failed to initialize PHY\n");
		return err;
	}

	err = phy_power_on(pcie->phy);
	if (err) {
		dev_err(dev, "failed to power on PHY\n");
		goto err_phy_on;
	}

	err = reset_control_bulk_deassert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);
	if (err) {
		dev_err(dev, "failed to deassert PHYs\n");
		goto err_phy_deassert;
	}

	/*
	 * Wait for the time needed to complete the bulk de-assert above.
	 * This time is specific for EN7581 SoC.
	 */
	mdelay(PCIE_EN7581_RESET_TIME_MS);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	err = clk_bulk_prepare(pcie->num_clks, pcie->clks);
	if (err) {
		dev_err(dev, "failed to prepare clock\n");
		goto err_clk_prepare;
	}

	val = FIELD_PREP(PCIE_VAL_LN0_DOWNSTREAM, 0x47) |
	      FIELD_PREP(PCIE_VAL_LN1_DOWNSTREAM, 0x47) |
	      FIELD_PREP(PCIE_VAL_LN0_UPSTREAM, 0x41) |
	      FIELD_PREP(PCIE_VAL_LN1_UPSTREAM, 0x41);
	writel_relaxed(val, pcie->base + PCIE_EQ_PRESET_01_REG);

	val = PCIE_K_PHYPARAM_QUERY | PCIE_K_QUERY_TIMEOUT |
	      FIELD_PREP(PCIE_K_PRESET_TO_USE_16G, 0x80) |
	      FIELD_PREP(PCIE_K_PRESET_TO_USE, 0x2) |
	      FIELD_PREP(PCIE_K_FINETUNE_MAX, 0xf);
	writel_relaxed(val, pcie->base + PCIE_PIPE4_PIE8_REG);

	err = clk_bulk_enable(pcie->num_clks, pcie->clks);
	if (err) {
		dev_err(dev, "failed to prepare clock\n");
		goto err_clk_enable;
	}

	return 0;

err_clk_enable:
	clk_bulk_unprepare(pcie->num_clks, pcie->clks);
err_clk_prepare:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	reset_control_bulk_assert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);
err_phy_deassert:
	phy_power_off(pcie->phy);
err_phy_on:
	phy_exit(pcie->phy);

	return err;
}

static int mtk_pcie_power_up(struct mtk_gen3_pcie *pcie)
{
	struct device *dev = pcie->dev;
	int err;

	/* PHY power on and enable pipe clock */
	err = reset_control_bulk_deassert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);
	if (err) {
		dev_err(dev, "failed to deassert PHYs\n");
		return err;
	}

	err = phy_init(pcie->phy);
	if (err) {
		dev_err(dev, "failed to initialize PHY\n");
		goto err_phy_init;
	}

	err = phy_power_on(pcie->phy);
	if (err) {
		dev_err(dev, "failed to power on PHY\n");
		goto err_phy_on;
	}

	/* MAC power on and enable transaction layer clocks */
	reset_control_deassert(pcie->mac_reset);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	err = clk_bulk_prepare_enable(pcie->num_clks, pcie->clks);
	if (err) {
		dev_err(dev, "failed to enable clocks\n");
		goto err_clk_init;
	}

	return 0;

err_clk_init:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	reset_control_assert(pcie->mac_reset);
	phy_power_off(pcie->phy);
err_phy_on:
	phy_exit(pcie->phy);
err_phy_init:
	reset_control_bulk_assert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);

	return err;
}

static void mtk_pcie_power_down(struct mtk_gen3_pcie *pcie)
{
	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);

	pm_runtime_put_sync(pcie->dev);
	pm_runtime_disable(pcie->dev);
	reset_control_assert(pcie->mac_reset);

	phy_power_off(pcie->phy);
	phy_exit(pcie->phy);
	reset_control_bulk_assert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);
}

static int mtk_pcie_setup(struct mtk_gen3_pcie *pcie)
{
	int err;

	err = mtk_pcie_parse_port(pcie);
	if (err)
		return err;

	/*
	 * Deassert the line in order to avoid unbalance in deassert_count
	 * counter since the bulk is shared.
	 */
	reset_control_bulk_deassert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);
	/*
	 * The controller may have been left out of reset by the bootloader
	 * so make sure that we get a clean start by asserting resets here.
	 */
	reset_control_bulk_assert(pcie->soc->phy_resets.num_resets, pcie->phy_resets);

	reset_control_assert(pcie->mac_reset);
	usleep_range(10, 20);

	/* Don't touch the hardware registers before power up */
	err = pcie->soc->power_up(pcie);
	if (err)
		return err;

	/* Try link up */
	err = mtk_pcie_startup_port(pcie);
	if (err)
		goto err_setup;

	err = mtk_pcie_setup_irq(pcie);
	if (err)
		goto err_setup;

	return 0;

err_setup:
	mtk_pcie_power_down(pcie);

	return err;
}

static int mtk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_gen3_pcie *pcie;
	struct pci_host_bridge *host;
	int err;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);

	pcie->dev = dev;
	pcie->soc = device_get_match_data(dev);
	platform_set_drvdata(pdev, pcie);

	err = mtk_pcie_setup(pcie);
	if (err)
		return err;

	host->ops = &mtk_pcie_ops;
	host->sysdata = pcie;

	err = pci_host_probe(host);
	if (err) {
		mtk_pcie_irq_teardown(pcie);
		mtk_pcie_power_down(pcie);
		return err;
	}

	return 0;
}

static void mtk_pcie_remove(struct platform_device *pdev)
{
	struct mtk_gen3_pcie *pcie = platform_get_drvdata(pdev);
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);

	pci_lock_rescan_remove();
	pci_stop_root_bus(host->bus);
	pci_remove_root_bus(host->bus);
	pci_unlock_rescan_remove();

	mtk_pcie_irq_teardown(pcie);
	mtk_pcie_power_down(pcie);
}

static void mtk_pcie_irq_save(struct mtk_gen3_pcie *pcie)
{
	int i;

	raw_spin_lock(&pcie->irq_lock);

	pcie->saved_irq_state = readl_relaxed(pcie->base + PCIE_INT_ENABLE_REG);

	for (i = 0; i < PCIE_MSI_SET_NUM; i++) {
		struct mtk_msi_set *msi_set = &pcie->msi_sets[i];

		msi_set->saved_irq_state = readl_relaxed(msi_set->base +
					   PCIE_MSI_SET_ENABLE_OFFSET);
	}

	raw_spin_unlock(&pcie->irq_lock);
}

static void mtk_pcie_irq_restore(struct mtk_gen3_pcie *pcie)
{
	int i;

	raw_spin_lock(&pcie->irq_lock);

	writel_relaxed(pcie->saved_irq_state, pcie->base + PCIE_INT_ENABLE_REG);

	for (i = 0; i < PCIE_MSI_SET_NUM; i++) {
		struct mtk_msi_set *msi_set = &pcie->msi_sets[i];

		writel_relaxed(msi_set->saved_irq_state,
			       msi_set->base + PCIE_MSI_SET_ENABLE_OFFSET);
	}

	raw_spin_unlock(&pcie->irq_lock);
}

static int mtk_pcie_turn_off_link(struct mtk_gen3_pcie *pcie)
{
	u32 val;

	val = readl_relaxed(pcie->base + PCIE_ICMD_PM_REG);
	val |= PCIE_TURN_OFF_LINK;
	writel_relaxed(val, pcie->base + PCIE_ICMD_PM_REG);

	/* Check the link is L2 */
	return readl_poll_timeout(pcie->base + PCIE_LTSSM_STATUS_REG, val,
				  (PCIE_LTSSM_STATE(val) ==
				   PCIE_LTSSM_STATE_L2_IDLE), 20,
				   50 * USEC_PER_MSEC);
}

static int mtk_pcie_suspend_noirq(struct device *dev)
{
	struct mtk_gen3_pcie *pcie = dev_get_drvdata(dev);
	int err;
	u32 val;

	/* Trigger link to L2 state */
	err = mtk_pcie_turn_off_link(pcie);
	if (err) {
		dev_err(pcie->dev, "cannot enter L2 state\n");
		return err;
	}

	/* Pull down the PERST# pin */
	val = readl_relaxed(pcie->base + PCIE_RST_CTRL_REG);
	val |= PCIE_PE_RSTB;
	writel_relaxed(val, pcie->base + PCIE_RST_CTRL_REG);

	dev_dbg(pcie->dev, "entered L2 states successfully");

	mtk_pcie_irq_save(pcie);
	mtk_pcie_power_down(pcie);

	return 0;
}

static int mtk_pcie_resume_noirq(struct device *dev)
{
	struct mtk_gen3_pcie *pcie = dev_get_drvdata(dev);
	int err;

	err = pcie->soc->power_up(pcie);
	if (err)
		return err;

	err = mtk_pcie_startup_port(pcie);
	if (err) {
		mtk_pcie_power_down(pcie);
		return err;
	}

	mtk_pcie_irq_restore(pcie);

	return 0;
}

static const struct dev_pm_ops mtk_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(mtk_pcie_suspend_noirq,
				  mtk_pcie_resume_noirq)
};

static const struct mtk_gen3_pcie_pdata mtk_pcie_soc_mt8192 = {
	.power_up = mtk_pcie_power_up,
	.phy_resets = {
		.id[0] = "phy",
		.num_resets = 1,
	},
};

static const struct mtk_gen3_pcie_pdata mtk_pcie_soc_en7581 = {
	.power_up = mtk_pcie_en7581_power_up,
	.phy_resets = {
		.id[0] = "phy-lane0",
		.id[1] = "phy-lane1",
		.id[2] = "phy-lane2",
		.num_resets = 3,
	},
};

static const struct of_device_id mtk_pcie_of_match[] = {
	{ .compatible = "airoha,en7581-pcie", .data = &mtk_pcie_soc_en7581 },
	{ .compatible = "mediatek,mt8192-pcie", .data = &mtk_pcie_soc_mt8192 },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_pcie_of_match);

static struct platform_driver mtk_pcie_driver = {
	.probe = mtk_pcie_probe,
	.remove_new = mtk_pcie_remove,
	.driver = {
		.name = "mtk-pcie-gen3",
		.of_match_table = mtk_pcie_of_match,
		.pm = &mtk_pcie_pm_ops,
	},
};

module_platform_driver(mtk_pcie_driver);
MODULE_DESCRIPTION("MediaTek Gen3 PCIe host controller driver");
MODULE_LICENSE("GPL v2");
