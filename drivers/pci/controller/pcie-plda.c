/*
 * PCIe host controller driver for Starfive JH7110 Soc.
 *
 * Based on pcie-altera.c, pcie-altera-msi.c.
 *
 * Copyright (C) Shanghai StarFive Technology Co., Ltd.
 *
 * Author: ke.zhu@starfivetech.com
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include "../pci.h"

#define PCIE_BASIC_STATUS		0x018
#define PCIE_CFGNUM			0x140
#define IMASK_LOCAL			0x180
#define ISTATUS_LOCAL			0x184
#define IMSI_ADDR			0x190
#define ISTATUS_MSI			0x194
#define CFG_SPACE			0x1000
#define GEN_SETTINGS			0x80
#define PCIE_PCI_IDS			0x9C
#define PCIE_WINROM			0xFC
#define PMSG_SUPPORT_RX			0x3F0

#define PCI_MISC			0xB4

#define PLDA_EP_ENABLE			0
#define PLDA_RP_ENABLE			1

#define IDS_REVISION_ID			0x02
#define IDS_PCI_TO_PCI_BRIDGE		0x060400
#define IDS_CLASS_CODE_SHIFT		8

#define PLDA_LINK_UP			1
#define PLDA_LINK_DOWN			0

#define PLDA_DATA_LINK_ACTIVE		BIT(5)
#define PREF_MEM_WIN_64_SUPPORT		BIT(3)
#define PMSG_LTR_SUPPORT		BIT(2)
#define PDLA_LINK_SPEED_GEN2		BIT(12)
#define PLDA_FUNCTION_DIS		BIT(15)
#define PLDA_FUNC_NUM			4
#define PLDA_PHY_FUNC_SHIFT		9
#define PHY_KVCO_FINE_TUNE_LEVEL	0x91
#define PHY_KVCO_FINE_TUNE_SIGNALS	0xc

#define XR3PCI_ATR_AXI4_SLV0		0x800
#define XR3PCI_ATR_SRC_ADDR_LOW		0x0
#define XR3PCI_ATR_SRC_ADDR_HIGH	0x4
#define XR3PCI_ATR_TRSL_ADDR_LOW	0x8
#define XR3PCI_ATR_TRSL_ADDR_HIGH	0xc
#define XR3PCI_ATR_TRSL_PARAM		0x10
#define XR3PCI_ATR_TABLE_OFFSET		0x20
#define XR3PCI_ATR_MAX_TABLE_NUM	8

#define XR3PCI_ATR_SRC_WIN_SIZE_SHIFT	1
#define XR3PCI_ATR_SRC_ADDR_MASK	0xfffff000
#define XR3PCI_ATR_TRSL_ADDR_MASK	0xfffff000
#define XR3_PCI_ECAM_SIZE		28
#define XR3PCI_ATR_TRSL_DIR		BIT(22)
/* IDs used in the XR3PCI_ATR_TRSL_PARAM */
#define XR3PCI_ATR_TRSLID_PCIE_MEMORY	0x0
#define XR3PCI_ATR_TRSLID_PCIE_CONFIG	0x1

#define CFGNUM_DEVFN_SHIFT		0
#define CFGNUM_BUS_SHIFT		8
#define CFGNUM_BE_SHIFT			16
#define CFGNUM_FBE_SHIFT		20

#define ECAM_BUS_SHIFT			20
#define ECAM_DEV_SHIFT			15
#define ECAM_FUNC_SHIFT			12

#define INT_AXI_POST_ERROR		BIT(16)
#define INT_AXI_FETCH_ERROR		BIT(17)
#define INT_AXI_DISCARD_ERROR		BIT(18)
#define INT_PCIE_POST_ERROR		BIT(20)
#define INT_PCIE_FETCH_ERROR		BIT(21)
#define INT_PCIE_DISCARD_ERROR		BIT(22)
#define INT_ERRORS		(INT_AXI_POST_ERROR | INT_AXI_FETCH_ERROR | \
				 INT_AXI_DISCARD_ERROR | INT_PCIE_POST_ERROR | \
				 INT_PCIE_FETCH_ERROR | INT_PCIE_DISCARD_ERROR)

#define INTA_OFFSET		24
#define INTA			BIT(24)
#define INTB			BIT(25)
#define INTC			BIT(26)
#define INTD			BIT(27)
#define INT_MSI			BIT(28)
#define INT_INTX_MASK		(INTA | INTB | INTC | INTD)
#define INT_MASK		(INT_INTX_MASK | INT_MSI | INT_ERRORS)

#define INT_PCI_MSI_NR		32
#define LINK_UP_MASK		0xff

#define PERST_DELAY_US		1000

/* system control */
#define STG_SYSCON_K_RP_NEP_SHIFT		0x8
#define STG_SYSCON_K_RP_NEP_MASK		0x100
#define STG_SYSCON_AXI4_SLVL_ARFUNC_MASK	0x7FFF00
#define STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT	0x8
#define STG_SYSCON_AXI4_SLVL_AWFUNC_MASK	0x7FFF
#define STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT	0x0
#define STG_SYSCON_CLKREQ_SHIFT			0x16
#define STG_SYSCON_CLKREQ_MASK			0x400000
#define STG_SYSCON_CKREF_SRC_SHIFT		0x12
#define STG_SYSCON_CKREF_SRC_MASK		0xC0000

#define PCI_DEV(d)		(((d) >> 3) & 0x1f)

/* MSI information */
struct plda_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *msi_domain;
	struct irq_domain *inner_domain;
	/* Protect bitmap variable */
	struct mutex lock;
};

struct plda_pcie {
	struct platform_device	*pdev;
	void __iomem		*reg_base;
	void __iomem		*config_base;
	struct resource *cfg_res;
	struct regmap *reg_syscon;
	struct regmap *reg_phyctrl;
	u32 stg_arfun;
	u32 stg_awfun;
	u32 stg_rp_nep;
	u32 stg_lnksta;
	u32 phy_kvco_level;
	u32 phy_kvco_tune_signals;
	int			irq;
	struct irq_domain	*legacy_irq_domain;
	struct pci_host_bridge  *bridge;
	struct plda_msi		msi;
	struct reset_control *resets;
	struct clk_bulk_data *clks;
	int num_clks;
	int atr_table_num;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
};

static inline void plda_writel(struct plda_pcie *pcie, const u32 value,
			       const u32 reg)
{
	writel_relaxed(value, pcie->reg_base + reg);
}

static inline u32 plda_readl(struct plda_pcie *pcie, const u32 reg)
{
	return readl_relaxed(pcie->reg_base + reg);
}

static bool plda_pcie_hide_rc_bar(struct pci_bus *bus, unsigned int  devfn,
				  int offset)
{
	if (pci_is_root_bus(bus) && (devfn == 0) &&
	    (offset == PCI_BASE_ADDRESS_0))
		return true;

	return false;
}

static int _plda_pcie_config_read(struct plda_pcie *pcie, unsigned char busno,
				  unsigned int devfn, int where, int size,
				  u32 *value)
{
	void __iomem *addr;

	addr = pcie->config_base;
	addr += (busno << ECAM_BUS_SHIFT);
	addr += (PCI_DEV(devfn) << ECAM_DEV_SHIFT);
	addr += (PCI_FUNC(devfn) << ECAM_FUNC_SHIFT);
	addr += where;

	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		*(unsigned char *)value = readb(addr);
		break;
	case 2:
		*(unsigned short *)value = readw(addr);
		break;
	case 4:
		*(unsigned int *)value = readl(addr);
		break;
	default:
		return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

int _plda_pcie_config_write(struct plda_pcie *pcie, unsigned char busno,
			    unsigned int devfn, int where, int size, u32 value)
{
	void __iomem *addr;

	addr = pcie->config_base;
	addr += (busno << ECAM_BUS_SHIFT);
	addr += (PCI_DEV(devfn) << ECAM_DEV_SHIFT);
	addr += (PCI_FUNC(devfn) << ECAM_FUNC_SHIFT);
	addr += where;

	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		writeb(value, addr);
		break;
	case 2:
		writew(value, addr);
		break;
	case 4:
		writel(value, addr);
		break;
	default:
		return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int plda_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *value)
{
	struct plda_pcie *pcie = bus->sysdata;

	return _plda_pcie_config_read(pcie, bus->number, devfn, where, size,
				      value);
}

int plda_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 value)
{
	struct plda_pcie *pcie = bus->sysdata;

	if (plda_pcie_hide_rc_bar(bus, devfn, where))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return _plda_pcie_config_write(pcie, bus->number, devfn, where, size,
				       value);
}

static void plda_pcie_handle_msi_irq(struct plda_pcie *pcie)
{
	struct plda_msi *msi = &pcie->msi;
	u32 bit;
	u32 virq;
	unsigned long status = plda_readl(pcie, ISTATUS_MSI);

	for_each_set_bit(bit, &status, INT_PCI_MSI_NR) {
		/* Clear interrupts */
		plda_writel(pcie, 1 << bit, ISTATUS_MSI);

		virq = irq_find_mapping(msi->inner_domain, bit);
		if (virq) {
			if (test_bit(bit, msi->used))
				generic_handle_irq(virq);
			else
				dev_err(&pcie->pdev->dev,
					"Unhandled MSI, MSI%d virq %d\n", bit,
					virq);
		} else
			dev_err(&pcie->pdev->dev, "Unexpected MSI, MSI%d\n",
				bit);

	}
	plda_writel(pcie, INT_MSI, ISTATUS_LOCAL);
}

static void plda_pcie_handle_intx_irq(struct plda_pcie *pcie,
				      unsigned long status)
{
	u32 bit;
	u32 virq;

	status >>= INTA_OFFSET;

	for_each_set_bit(bit, &status, PCI_NUM_INTX) {
		/* Clear interrupts */
		plda_writel(pcie, 1 << (bit + INTA_OFFSET), ISTATUS_LOCAL);

		virq = irq_find_mapping(pcie->legacy_irq_domain, bit);
		if (virq)
			generic_handle_irq(virq);
		else
			dev_err(&pcie->pdev->dev,
				"plda_pcie_handle_intx_irq unexpected IRQ, INT%d\n", bit);
	}
}

static void plda_pcie_handle_errors_irq(struct plda_pcie *pcie, u32 status)
{
	if (status & INT_AXI_POST_ERROR)
		dev_err(&pcie->pdev->dev, "AXI post error\n");
	if (status & INT_AXI_FETCH_ERROR)
		dev_err(&pcie->pdev->dev, "AXI fetch error\n");
	if (status & INT_AXI_DISCARD_ERROR)
		dev_err(&pcie->pdev->dev, "AXI discard error\n");
	if (status & INT_PCIE_POST_ERROR)
		dev_err(&pcie->pdev->dev, "PCIe post error\n");
	if (status & INT_PCIE_FETCH_ERROR)
		dev_err(&pcie->pdev->dev, "PCIe fetch error\n");
	if (status & INT_PCIE_DISCARD_ERROR)
		dev_err(&pcie->pdev->dev, "PCIe discard error\n");

	plda_writel(pcie, INT_ERRORS, ISTATUS_LOCAL);
}

static void plda_pcie_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct plda_pcie *pcie;
	u32 status;

	chained_irq_enter(chip, desc);
	pcie = irq_desc_get_handler_data(desc);

	status = plda_readl(pcie, ISTATUS_LOCAL);
	while ((status = (plda_readl(pcie, ISTATUS_LOCAL) & INT_MASK))) {
		if (status & INT_INTX_MASK)
			plda_pcie_handle_intx_irq(pcie, status);

		if (status & INT_MSI)
			plda_pcie_handle_msi_irq(pcie);

		if (status & INT_ERRORS)
			plda_pcie_handle_errors_irq(pcie, status);
	}

	chained_irq_exit(chip, desc);
}

#ifdef CONFIG_PCI_MSI
static struct irq_chip plda_msi_irq_chip = {
	.name = "PLDA PCIe MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

static struct msi_domain_info plda_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_PCI_MSIX),
	.chip = &plda_msi_irq_chip,
};
#endif

static void plda_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct plda_pcie *pcie = irq_data_get_irq_chip_data(data);
	phys_addr_t msi_addr = plda_readl(pcie, IMSI_ADDR);

	msg->address_lo = lower_32_bits(msi_addr);
	msg->address_hi = upper_32_bits(msi_addr);
	msg->data = data->hwirq;

	dev_info(&pcie->pdev->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static int plda_msi_set_affinity(struct irq_data *irq_data,
				 const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip plda_irq_chip = {
	.name = "PLDA MSI",
	.irq_compose_msi_msg = plda_compose_msi_msg,
	.irq_set_affinity = plda_msi_set_affinity,
};

static int plda_msi_alloc(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs, void *args)
{
	struct plda_pcie *pcie = domain->host_data;
	struct plda_msi *msi = &pcie->msi;
	int bit;

	WARN_ON(nr_irqs != 1);
	mutex_lock(&msi->lock);

	bit = find_first_zero_bit(msi->used, INT_PCI_MSI_NR);
	if (bit >= INT_PCI_MSI_NR) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	set_bit(bit, msi->used);

	irq_domain_set_info(domain, virq, bit, &plda_irq_chip,
			    domain->host_data, handle_simple_irq,
			    NULL, NULL);
	mutex_unlock(&msi->lock);

	return 0;
}

static void plda_msi_free(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);
	struct plda_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct plda_msi *msi = &pcie->msi;

	mutex_lock(&msi->lock);

	if (!test_bit(data->hwirq, msi->used))
		dev_err(&pcie->pdev->dev, "Trying to free unused MSI#%lu\n",
			data->hwirq);
	else
		__clear_bit(data->hwirq, msi->used);

	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops dev_msi_domain_ops = {
	.alloc  = plda_msi_alloc,
	.free   = plda_msi_free,
};

static void plda_msi_free_irq_domain(struct plda_pcie *pcie)
{
#ifdef CONFIG_PCI_MSI
	struct plda_msi *msi = &pcie->msi;
	u32 irq;
	int i;

	for (i = 0; i < INT_PCI_MSI_NR; i++) {
		irq = irq_find_mapping(msi->inner_domain, i);
		if (irq > 0)
			irq_dispose_mapping(irq);
	}

	if (msi->msi_domain)
		irq_domain_remove(msi->msi_domain);

	if (msi->inner_domain)
		irq_domain_remove(msi->inner_domain);
#endif
}

static void plda_pcie_free_irq_domain(struct plda_pcie *pcie)
{
	int i;
	u32 irq;

	/* Disable all interrupts */
	plda_writel(pcie, 0, IMASK_LOCAL);

	if (pcie->legacy_irq_domain) {
		for (i = 0; i < PCI_NUM_INTX; i++) {
			irq = irq_find_mapping(pcie->legacy_irq_domain, i);
			if (irq > 0)
				irq_dispose_mapping(irq);
		}
		irq_domain_remove(pcie->legacy_irq_domain);
	}

	if (pci_msi_enabled())
		plda_msi_free_irq_domain(pcie);
	irq_set_chained_handler_and_data(pcie->irq, NULL, NULL);
}

static int plda_pcie_init_msi_irq_domain(struct plda_pcie *pcie)
{
#ifdef CONFIG_PCI_MSI
	struct fwnode_handle *fwn = of_node_to_fwnode(pcie->pdev->dev.of_node);
	struct plda_msi *msi = &pcie->msi;

	msi->inner_domain = irq_domain_add_linear(NULL, INT_PCI_MSI_NR,
						  &dev_msi_domain_ops, pcie);
	if (!msi->inner_domain) {
		dev_err(&pcie->pdev->dev, "Failed to create dev IRQ domain\n");
		return -ENOMEM;
	}
	msi->msi_domain = pci_msi_create_irq_domain(fwn, &plda_msi_domain_info,
						    msi->inner_domain);
	if (!msi->msi_domain) {
		dev_err(&pcie->pdev->dev, "Failed to create msi IRQ domain\n");
		irq_domain_remove(msi->inner_domain);
		return -ENOMEM;
	}
#endif
	return 0;
}

static int plda_pcie_enable_msi(struct plda_pcie *pcie, struct pci_bus *bus)
{
	struct plda_msi *msi = &pcie->msi;
	u32 reg;

	mutex_init(&msi->lock);

	/* Enable MSI */
	reg = plda_readl(pcie, IMASK_LOCAL);
	reg |= INT_MSI;
	plda_writel(pcie, reg, IMASK_LOCAL);
	return 0;
}

static int plda_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = plda_pcie_intx_map,
	.xlate = pci_irqd_intx_xlate,
};

static int plda_pcie_init_irq_domain(struct plda_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	int ret;

	if (pci_msi_enabled()) {
		ret = plda_pcie_init_msi_irq_domain(pcie);
		if (ret != 0)
			return -ENOMEM;
	}

	/* Setup INTx */
	pcie->legacy_irq_domain = irq_domain_add_linear(node, PCI_NUM_INTX,
					&intx_domain_ops, pcie);

	if (!pcie->legacy_irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(pcie->irq, plda_pcie_isr, pcie);
	return 0;
}

static int plda_pcie_parse_dt(struct plda_pcie *pcie)
{
	struct resource *reg_res;
	struct platform_device *pdev = pcie->pdev;
	struct of_phandle_args syscon_args, phyctrl_args;
	int ret;

	reg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "reg");
	if (!reg_res) {
		dev_err(&pdev->dev, "Missing required reg address range\n");
		return -ENODEV;
	}

	pcie->reg_base = devm_ioremap_resource(&pdev->dev, reg_res);
	if (IS_ERR(pcie->reg_base)) {
		dev_err(&pdev->dev, "Failed to map reg memory\n");
		return PTR_ERR(pcie->reg_base);
	}

	pcie->cfg_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (!pcie->cfg_res) {
		dev_err(&pdev->dev, "Missing required config address range");
		return -ENODEV;
	}

	pcie->config_base = devm_ioremap_resource(&pdev->dev, pcie->cfg_res);
	if (IS_ERR(pcie->config_base)) {
		dev_err(&pdev->dev, "Failed to map config memory\n");
		return PTR_ERR(pcie->config_base);
	}

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
					       "starfive,phyctrl", 2, 0, &phyctrl_args);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to parse starfive,phyctrl\n");
		return -EINVAL;
	}

	if (!of_device_is_compatible(phyctrl_args.np, "starfive,phyctrl"))
		return -EINVAL;
	pcie->reg_phyctrl =  device_node_to_regmap(phyctrl_args.np);
	of_node_put(phyctrl_args.np);
	if (IS_ERR(pcie->reg_phyctrl))
		return PTR_ERR(pcie->reg_phyctrl);

	pcie->phy_kvco_level = phyctrl_args.args[0];
	pcie->phy_kvco_tune_signals = phyctrl_args.args[1];

	pcie->irq = platform_get_irq(pdev, 0);
	if (pcie->irq <= 0) {
		dev_err(&pdev->dev, "Failed to get IRQ: %d\n", pcie->irq);
		return -EINVAL;
	}

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
					       "starfive,stg-syscon", 4, 0, &syscon_args);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to parse starfive,stg-syscon\n");
		return -EINVAL;
	}

	pcie->reg_syscon = syscon_node_to_regmap(syscon_args.np);
	of_node_put(syscon_args.np);
	if (IS_ERR(pcie->reg_syscon))
		return PTR_ERR(pcie->reg_syscon);

	pcie->stg_arfun = syscon_args.args[0];
	pcie->stg_awfun = syscon_args.args[1];
	pcie->stg_rp_nep = syscon_args.args[2];
	pcie->stg_lnksta = syscon_args.args[3];

	/* Clear all interrupts */
	plda_writel(pcie, 0xffffffff, ISTATUS_LOCAL);
	plda_writel(pcie, INT_INTX_MASK | INT_ERRORS, IMASK_LOCAL);

	return 0;
}

static struct pci_ops plda_pcie_ops = {
	.read           = plda_pcie_config_read,
	.write          = plda_pcie_config_write,
};

void plda_set_atr_entry(struct plda_pcie *pcie, phys_addr_t src_addr,
			phys_addr_t trsl_addr, size_t window_size,
			int trsl_param)
{
	void __iomem *base =
		pcie->reg_base + XR3PCI_ATR_AXI4_SLV0;

	/* Support AXI4 Slave 0 Address Translation Tables 0-7. */
	if (pcie->atr_table_num >= XR3PCI_ATR_MAX_TABLE_NUM)
		pcie->atr_table_num = XR3PCI_ATR_MAX_TABLE_NUM - 1;
	base +=  XR3PCI_ATR_TABLE_OFFSET * pcie->atr_table_num;
	pcie->atr_table_num++;

	/* X3PCI_ATR_SRC_ADDR_LOW:
	 *   - bit 0: enable entry,
	 *   - bits 1-6: ATR window size: total size in bytes: 2^(ATR_WSIZE + 1)
	 *   - bits 7-11: reserved
	 *   - bits 12-31: start of source address
	 */
	writel((lower_32_bits(src_addr) & XR3PCI_ATR_SRC_ADDR_MASK) |
			(fls(window_size) - 1) << XR3PCI_ATR_SRC_WIN_SIZE_SHIFT | 1,
			base + XR3PCI_ATR_SRC_ADDR_LOW);
	writel(upper_32_bits(src_addr), base + XR3PCI_ATR_SRC_ADDR_HIGH);
	writel((lower_32_bits(trsl_addr) & XR3PCI_ATR_TRSL_ADDR_MASK),
			base + XR3PCI_ATR_TRSL_ADDR_LOW);
	writel(upper_32_bits(trsl_addr), base + XR3PCI_ATR_TRSL_ADDR_HIGH);
	writel(trsl_param, base + XR3PCI_ATR_TRSL_PARAM);

	pr_info("ATR entry: 0x%010llx %s 0x%010llx [0x%010llx] (param: 0x%06x)\n",
	       src_addr, (trsl_param & XR3PCI_ATR_TRSL_DIR) ? "<-" : "->",
	       trsl_addr, (u64)window_size, trsl_param);
}

static int plda_pcie_setup_windows(struct plda_pcie *pcie)
{
	struct pci_host_bridge *bridge = pcie->bridge;
	struct resource_entry *entry;
	u64 pci_addr;

	resource_list_for_each_entry(entry, &bridge->windows) {
		if (resource_type(entry->res) == IORESOURCE_MEM) {
			pci_addr = entry->res->start - entry->offset;
			plda_set_atr_entry(pcie,
						entry->res->start, pci_addr,
						resource_size(entry->res),
						XR3PCI_ATR_TRSLID_PCIE_MEMORY);
		}
	}

	return 0;
}

static int plda_clk_rst_init(struct plda_pcie *pcie)
{
	int ret;
	struct device *dev = &pcie->pdev->dev;

	pcie->num_clks = devm_clk_bulk_get_all(dev, &pcie->clks);
	if (pcie->num_clks < 0) {
		dev_err(dev, "Failed to get pcie clocks\n");
		ret = -ENODEV;
		goto exit;
	}
	ret = clk_bulk_prepare_enable(pcie->num_clks, pcie->clks);
	if (ret) {
		dev_err(&pcie->pdev->dev, "Failed to enable clocks\n");
		goto exit;
	}

	pcie->resets = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(pcie->resets)) {
		ret = PTR_ERR(pcie->resets);
		dev_err(dev, "Failed to get pcie resets");
		goto err_clk_init;
	}
	ret = reset_control_deassert(pcie->resets);
	goto exit;

err_clk_init:
	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
exit:
	return ret;
}

static void plda_clk_rst_deinit(struct plda_pcie *pcie)
{
	reset_control_assert(pcie->resets);
	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);
}

int plda_gpio_init(struct plda_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;

	pcie->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(pcie->reset_gpio)) {
		dev_warn(dev, "Failed to get reset-gpio.\n");
		return -EINVAL;
	}

	pcie->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pcie->power_gpio)) {
		dev_warn(dev, "Failed to get power-gpio, but maybe it's always on.\n");
		pcie->power_gpio = NULL;
	}

	return 0;
}

static void plda_pcie_hw_init(struct plda_pcie *pcie)
{
	unsigned int value;
	int i;

	if (pcie->power_gpio)
		gpiod_set_value_cansleep(pcie->power_gpio, 1);

	if (pcie->reset_gpio)
		gpiod_set_value_cansleep(pcie->reset_gpio, 1);

	/* Disable physical functions except #0 */
	for (i = 1; i < PLDA_FUNC_NUM; i++) {
		regmap_update_bits(pcie->reg_syscon,
				pcie->stg_arfun,
				STG_SYSCON_AXI4_SLVL_ARFUNC_MASK,
				(i << PLDA_PHY_FUNC_SHIFT) <<
				STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT);
		regmap_update_bits(pcie->reg_syscon,
				pcie->stg_awfun,
				STG_SYSCON_AXI4_SLVL_AWFUNC_MASK,
				(i << PLDA_PHY_FUNC_SHIFT) <<
				STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT);

		value = readl(pcie->reg_base + PCI_MISC);
		value |= PLDA_FUNCTION_DIS;
		writel(value, pcie->reg_base + PCI_MISC);
	}
	regmap_update_bits(pcie->reg_syscon,
				pcie->stg_arfun,
				STG_SYSCON_AXI4_SLVL_ARFUNC_MASK,
				0 << STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT);
	regmap_update_bits(pcie->reg_syscon,
				pcie->stg_awfun,
				STG_SYSCON_AXI4_SLVL_AWFUNC_MASK,
				0 << STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT);

	/* PCIe Multi-PHY PLL KVCO Gain fine tune settings: */
	regmap_write(pcie->reg_phyctrl, pcie->phy_kvco_level,
		     PHY_KVCO_FINE_TUNE_LEVEL);
	regmap_write(pcie->reg_phyctrl, pcie->phy_kvco_tune_signals,
		     PHY_KVCO_FINE_TUNE_SIGNALS);

	/* Enable root port*/
	value = readl(pcie->reg_base + GEN_SETTINGS);
	value |= PLDA_RP_ENABLE;
	writel(value, pcie->reg_base + GEN_SETTINGS);

	/* PCIe PCI Standard Configuration Identification Settings. */
	value = (IDS_PCI_TO_PCI_BRIDGE << IDS_CLASS_CODE_SHIFT) | IDS_REVISION_ID;
	writel(value, pcie->reg_base + PCIE_PCI_IDS);

	/* The LTR message forwarding of PCIe Message Reception was set by core
	 * as default, but the forward id & addr are also need to be reset.
	 * If we do not disable LTR message forwarding here, or set a legal
	 * forwarding address, the kernel will get stuck after this driver probe.
	 * To workaround, disable the LTR message forwarding support on
	 * PCIe Message Reception.
	 */
	value = readl(pcie->reg_base + PMSG_SUPPORT_RX);
	value &= ~PMSG_LTR_SUPPORT;
	writel(value, pcie->reg_base + PMSG_SUPPORT_RX);

	/* Prefetchable memory window 64-bit addressing support */
	value = readl(pcie->reg_base + PCIE_WINROM);
	value |= PREF_MEM_WIN_64_SUPPORT;
	writel(value, pcie->reg_base + PCIE_WINROM);

	/* As the two host bridges in JH7110 soc have the same default
	 * address translation table, this cause the second root port can't
	 * access it's host bridge config space correctly.
	 * To workaround, config the ATR of host bridge config space by SW.
	 */
	plda_set_atr_entry(pcie,
			pcie->cfg_res->start, 0,
			1 << XR3_PCI_ECAM_SIZE,
			XR3PCI_ATR_TRSLID_PCIE_CONFIG);

	plda_pcie_setup_windows(pcie);

	/* Ensure that PERST has been asserted for at least 100 ms */
	msleep(300);
	if (pcie->reset_gpio)
		gpiod_set_value_cansleep(pcie->reset_gpio, 0);

}

static int plda_pcie_is_link_up(struct plda_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	int ret;
	u32 stg_reg_val;

	/* 100ms timeout value should be enough for Gen1/2 training */
	ret = regmap_read_poll_timeout(pcie->reg_syscon,
					pcie->stg_lnksta,
					stg_reg_val,
					stg_reg_val & PLDA_DATA_LINK_ACTIVE,
					10 * 1000, 100 * 1000);

	/* If the link is down (no device in slot), then exit. */
	if (ret == -ETIMEDOUT) {
		dev_info(dev, "Port link down, exit.\n");
		return PLDA_LINK_DOWN;
	} else if (ret == 0) {
		dev_info(dev, "Port link up.\n");
		return PLDA_LINK_UP;
	}

	dev_warn(dev, "Read stg_linksta failed.\n");
	return ret;
}

static int plda_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct plda_pcie *pcie;
	struct pci_bus *bus;
	struct pci_host_bridge *bridge;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->pdev = pdev;
	pcie->atr_table_num = 0;

	ret = plda_pcie_parse_dt(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Parsing DT failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, pcie);

	ret = plda_gpio_init(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Init gpio failed\n");
		return ret;
	}

	regmap_update_bits(pcie->reg_syscon,
				pcie->stg_rp_nep,
				STG_SYSCON_K_RP_NEP_MASK,
				1 << STG_SYSCON_K_RP_NEP_SHIFT);

	regmap_update_bits(pcie->reg_syscon,
				pcie->stg_awfun,
				STG_SYSCON_CKREF_SRC_MASK,
				2 << STG_SYSCON_CKREF_SRC_SHIFT);

	regmap_update_bits(pcie->reg_syscon,
				pcie->stg_awfun,
				STG_SYSCON_CLKREQ_MASK,
				1 << STG_SYSCON_CLKREQ_SHIFT);

	ret = plda_clk_rst_init(pcie);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to init pcie clk reset: %d\n", ret);
		goto exit;
	}

	ret = plda_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(&pdev->dev, "Failed creating IRQ Domain\n");
		goto exit;
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge) {
		ret = -ENOMEM;
		goto exit;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* Set default bus ops */
	bridge->ops = &plda_pcie_ops;
	bridge->sysdata = pcie;
	pcie->bridge = bridge;

	plda_pcie_hw_init(pcie);

	if (plda_pcie_is_link_up(pcie) == PLDA_LINK_DOWN)
		goto release;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		ret = plda_pcie_enable_msi(pcie, bus);
		if (ret < 0) {
			dev_err(&pdev->dev,	"Failed to enable MSI support: %d\n", ret);
			goto release;
		}
	}

	ret = pci_host_probe(bridge);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to pci host probe: %d\n", ret);
		goto release;
	}

exit:
	return ret;

release:
	if (pcie->power_gpio)
		gpiod_set_value_cansleep(pcie->power_gpio, 0);

	plda_clk_rst_deinit(pcie);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	pci_free_host_bridge(pcie->bridge);
	devm_kfree(&pdev->dev, pcie);
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int plda_pcie_remove(struct platform_device *pdev)
{
	struct plda_pcie *pcie = platform_get_drvdata(pdev);

	plda_pcie_free_irq_domain(pcie);
	plda_clk_rst_deinit(pcie);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused plda_pcie_suspend_noirq(struct device *dev)
{
	struct plda_pcie *pcie = dev_get_drvdata(dev);

	if (!pcie)
		return 0;

	clk_bulk_disable_unprepare(pcie->num_clks, pcie->clks);

	return 0;
}

static int __maybe_unused plda_pcie_resume_noirq(struct device *dev)
{
	struct plda_pcie *pcie = dev_get_drvdata(dev);
	int ret;

	if (!pcie)
		return 0;

	ret = clk_bulk_prepare_enable(pcie->num_clks, pcie->clks);
	if (ret)
		dev_err(dev, "Failed to enable clocks\n");

	return ret;
}

static const struct dev_pm_ops plda_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(plda_pcie_suspend_noirq,
				      plda_pcie_resume_noirq)
};
#endif

static const struct of_device_id plda_pcie_of_match[] = {
	{ .compatible = "plda,pci-xpressrich3-axi"},
	{ .compatible = "starfive,jh7110-pcie"},
	{ },
};
MODULE_DEVICE_TABLE(of, plda_pcie_of_match);

static struct platform_driver plda_pcie_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(plda_pcie_of_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &plda_pcie_pm_ops,
#endif
	},
	.probe = plda_pcie_probe,
	.remove = plda_pcie_remove,
};
module_platform_driver(plda_pcie_driver);

MODULE_DESCRIPTION("StarFive JH7110 PCIe host driver");
MODULE_AUTHOR("ke.zhu <ke.zhu@starfivetech.com>");
MODULE_AUTHOR("Mason Huo <mason.huo@starfivetech.com>");
MODULE_LICENSE("GPL v2");
