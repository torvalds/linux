// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe host controller driver for Tegra SoCs
 *
 * Copyright (c) 2010, CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on NVIDIA PCIe driver
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * Bits taken from arch/arm/mach-dove/pcie.c
 *
 * Author: Thierry Reding <treding@nvidia.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/regulator/consumer.h>

#include <soc/tegra/cpuidle.h>
#include <soc/tegra/pmc.h>

#include "../pci.h"

#define INT_PCI_MSI_NR (8 * 32)

/* register definitions */

#define AFI_AXI_BAR0_SZ	0x00
#define AFI_AXI_BAR1_SZ	0x04
#define AFI_AXI_BAR2_SZ	0x08
#define AFI_AXI_BAR3_SZ	0x0c
#define AFI_AXI_BAR4_SZ	0x10
#define AFI_AXI_BAR5_SZ	0x14

#define AFI_AXI_BAR0_START	0x18
#define AFI_AXI_BAR1_START	0x1c
#define AFI_AXI_BAR2_START	0x20
#define AFI_AXI_BAR3_START	0x24
#define AFI_AXI_BAR4_START	0x28
#define AFI_AXI_BAR5_START	0x2c

#define AFI_FPCI_BAR0	0x30
#define AFI_FPCI_BAR1	0x34
#define AFI_FPCI_BAR2	0x38
#define AFI_FPCI_BAR3	0x3c
#define AFI_FPCI_BAR4	0x40
#define AFI_FPCI_BAR5	0x44

#define AFI_CACHE_BAR0_SZ	0x48
#define AFI_CACHE_BAR0_ST	0x4c
#define AFI_CACHE_BAR1_SZ	0x50
#define AFI_CACHE_BAR1_ST	0x54

#define AFI_MSI_BAR_SZ		0x60
#define AFI_MSI_FPCI_BAR_ST	0x64
#define AFI_MSI_AXI_BAR_ST	0x68

#define AFI_MSI_VEC(x)		(0x6c + ((x) * 4))
#define AFI_MSI_EN_VEC(x)	(0x8c + ((x) * 4))

#define AFI_CONFIGURATION		0xac
#define  AFI_CONFIGURATION_EN_FPCI		(1 << 0)
#define  AFI_CONFIGURATION_CLKEN_OVERRIDE	(1 << 31)

#define AFI_FPCI_ERROR_MASKS	0xb0

#define AFI_INTR_MASK		0xb4
#define  AFI_INTR_MASK_INT_MASK	(1 << 0)
#define  AFI_INTR_MASK_MSI_MASK	(1 << 8)

#define AFI_INTR_CODE			0xb8
#define  AFI_INTR_CODE_MASK		0xf
#define  AFI_INTR_INI_SLAVE_ERROR	1
#define  AFI_INTR_INI_DECODE_ERROR	2
#define  AFI_INTR_TARGET_ABORT		3
#define  AFI_INTR_MASTER_ABORT		4
#define  AFI_INTR_INVALID_WRITE		5
#define  AFI_INTR_LEGACY		6
#define  AFI_INTR_FPCI_DECODE_ERROR	7
#define  AFI_INTR_AXI_DECODE_ERROR	8
#define  AFI_INTR_FPCI_TIMEOUT		9
#define  AFI_INTR_PE_PRSNT_SENSE	10
#define  AFI_INTR_PE_CLKREQ_SENSE	11
#define  AFI_INTR_CLKCLAMP_SENSE	12
#define  AFI_INTR_RDY4PD_SENSE		13
#define  AFI_INTR_P2P_ERROR		14

#define AFI_INTR_SIGNATURE	0xbc
#define AFI_UPPER_FPCI_ADDRESS	0xc0
#define AFI_SM_INTR_ENABLE	0xc4
#define  AFI_SM_INTR_INTA_ASSERT	(1 << 0)
#define  AFI_SM_INTR_INTB_ASSERT	(1 << 1)
#define  AFI_SM_INTR_INTC_ASSERT	(1 << 2)
#define  AFI_SM_INTR_INTD_ASSERT	(1 << 3)
#define  AFI_SM_INTR_INTA_DEASSERT	(1 << 4)
#define  AFI_SM_INTR_INTB_DEASSERT	(1 << 5)
#define  AFI_SM_INTR_INTC_DEASSERT	(1 << 6)
#define  AFI_SM_INTR_INTD_DEASSERT	(1 << 7)

#define AFI_AFI_INTR_ENABLE		0xc8
#define  AFI_INTR_EN_INI_SLVERR		(1 << 0)
#define  AFI_INTR_EN_INI_DECERR		(1 << 1)
#define  AFI_INTR_EN_TGT_SLVERR		(1 << 2)
#define  AFI_INTR_EN_TGT_DECERR		(1 << 3)
#define  AFI_INTR_EN_TGT_WRERR		(1 << 4)
#define  AFI_INTR_EN_DFPCI_DECERR	(1 << 5)
#define  AFI_INTR_EN_AXI_DECERR		(1 << 6)
#define  AFI_INTR_EN_FPCI_TIMEOUT	(1 << 7)
#define  AFI_INTR_EN_PRSNT_SENSE	(1 << 8)

#define AFI_PCIE_PME		0xf0

#define AFI_PCIE_CONFIG					0x0f8
#define  AFI_PCIE_CONFIG_PCIE_DISABLE(x)		(1 << ((x) + 1))
#define  AFI_PCIE_CONFIG_PCIE_DISABLE_ALL		0xe
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_MASK	(0xf << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_SINGLE	(0x0 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_420	(0x0 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_X2_X1	(0x0 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_401	(0x0 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_DUAL	(0x1 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_222	(0x1 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_X4_X1	(0x1 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_211	(0x1 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_411	(0x2 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_111	(0x2 << 20)
#define  AFI_PCIE_CONFIG_PCIE_CLKREQ_GPIO(x)		(1 << ((x) + 29))
#define  AFI_PCIE_CONFIG_PCIE_CLKREQ_GPIO_ALL		(0x7 << 29)

#define AFI_FUSE			0x104
#define  AFI_FUSE_PCIE_T0_GEN2_DIS	(1 << 2)

#define AFI_PEX0_CTRL			0x110
#define AFI_PEX1_CTRL			0x118
#define  AFI_PEX_CTRL_RST		(1 << 0)
#define  AFI_PEX_CTRL_CLKREQ_EN		(1 << 1)
#define  AFI_PEX_CTRL_REFCLK_EN		(1 << 3)
#define  AFI_PEX_CTRL_OVERRIDE_EN	(1 << 4)

#define AFI_PLLE_CONTROL		0x160
#define  AFI_PLLE_CONTROL_BYPASS_PADS2PLLE_CONTROL (1 << 9)
#define  AFI_PLLE_CONTROL_PADS2PLLE_CONTROL_EN (1 << 1)

#define AFI_PEXBIAS_CTRL_0		0x168

#define RP_ECTL_2_R1	0x00000e84
#define  RP_ECTL_2_R1_RX_CTLE_1C_MASK		0xffff

#define RP_ECTL_4_R1	0x00000e8c
#define  RP_ECTL_4_R1_RX_CDR_CTRL_1C_MASK	(0xffff << 16)
#define  RP_ECTL_4_R1_RX_CDR_CTRL_1C_SHIFT	16

#define RP_ECTL_5_R1	0x00000e90
#define  RP_ECTL_5_R1_RX_EQ_CTRL_L_1C_MASK	0xffffffff

#define RP_ECTL_6_R1	0x00000e94
#define  RP_ECTL_6_R1_RX_EQ_CTRL_H_1C_MASK	0xffffffff

#define RP_ECTL_2_R2	0x00000ea4
#define  RP_ECTL_2_R2_RX_CTLE_1C_MASK	0xffff

#define RP_ECTL_4_R2	0x00000eac
#define  RP_ECTL_4_R2_RX_CDR_CTRL_1C_MASK	(0xffff << 16)
#define  RP_ECTL_4_R2_RX_CDR_CTRL_1C_SHIFT	16

#define RP_ECTL_5_R2	0x00000eb0
#define  RP_ECTL_5_R2_RX_EQ_CTRL_L_1C_MASK	0xffffffff

#define RP_ECTL_6_R2	0x00000eb4
#define  RP_ECTL_6_R2_RX_EQ_CTRL_H_1C_MASK	0xffffffff

#define RP_VEND_XP	0x00000f00
#define  RP_VEND_XP_DL_UP			(1 << 30)
#define  RP_VEND_XP_OPPORTUNISTIC_ACK		(1 << 27)
#define  RP_VEND_XP_OPPORTUNISTIC_UPDATEFC	(1 << 28)
#define  RP_VEND_XP_UPDATE_FC_THRESHOLD_MASK	(0xff << 18)

#define RP_VEND_CTL0	0x00000f44
#define  RP_VEND_CTL0_DSK_RST_PULSE_WIDTH_MASK	(0xf << 12)
#define  RP_VEND_CTL0_DSK_RST_PULSE_WIDTH	(0x9 << 12)

#define RP_VEND_CTL1	0x00000f48
#define  RP_VEND_CTL1_ERPT	(1 << 13)

#define RP_VEND_XP_BIST	0x00000f4c
#define  RP_VEND_XP_BIST_GOTO_L1_L2_AFTER_DLLP_DONE	(1 << 28)

#define RP_VEND_CTL2 0x00000fa8
#define  RP_VEND_CTL2_PCA_ENABLE (1 << 7)

#define RP_PRIV_MISC	0x00000fe0
#define  RP_PRIV_MISC_PRSNT_MAP_EP_PRSNT		(0xe << 0)
#define  RP_PRIV_MISC_PRSNT_MAP_EP_ABSNT		(0xf << 0)
#define  RP_PRIV_MISC_CTLR_CLK_CLAMP_THRESHOLD_MASK	(0x7f << 16)
#define  RP_PRIV_MISC_CTLR_CLK_CLAMP_THRESHOLD		(0xf << 16)
#define  RP_PRIV_MISC_CTLR_CLK_CLAMP_ENABLE		(1 << 23)
#define  RP_PRIV_MISC_TMS_CLK_CLAMP_THRESHOLD_MASK	(0x7f << 24)
#define  RP_PRIV_MISC_TMS_CLK_CLAMP_THRESHOLD		(0xf << 24)
#define  RP_PRIV_MISC_TMS_CLK_CLAMP_ENABLE		(1 << 31)

#define RP_LINK_CONTROL_STATUS			0x00000090
#define  RP_LINK_CONTROL_STATUS_DL_LINK_ACTIVE	0x20000000
#define  RP_LINK_CONTROL_STATUS_LINKSTAT_MASK	0x3fff0000

#define RP_LINK_CONTROL_STATUS_2		0x000000b0

#define PADS_CTL_SEL		0x0000009c

#define PADS_CTL		0x000000a0
#define  PADS_CTL_IDDQ_1L	(1 << 0)
#define  PADS_CTL_TX_DATA_EN_1L	(1 << 6)
#define  PADS_CTL_RX_DATA_EN_1L	(1 << 10)

#define PADS_PLL_CTL_TEGRA20			0x000000b8
#define PADS_PLL_CTL_TEGRA30			0x000000b4
#define  PADS_PLL_CTL_RST_B4SM			(1 << 1)
#define  PADS_PLL_CTL_LOCKDET			(1 << 8)
#define  PADS_PLL_CTL_REFCLK_MASK		(0x3 << 16)
#define  PADS_PLL_CTL_REFCLK_INTERNAL_CML	(0 << 16)
#define  PADS_PLL_CTL_REFCLK_INTERNAL_CMOS	(1 << 16)
#define  PADS_PLL_CTL_REFCLK_EXTERNAL		(2 << 16)
#define  PADS_PLL_CTL_TXCLKREF_MASK		(0x1 << 20)
#define  PADS_PLL_CTL_TXCLKREF_DIV10		(0 << 20)
#define  PADS_PLL_CTL_TXCLKREF_DIV5		(1 << 20)
#define  PADS_PLL_CTL_TXCLKREF_BUF_EN		(1 << 22)

#define PADS_REFCLK_CFG0			0x000000c8
#define PADS_REFCLK_CFG1			0x000000cc
#define PADS_REFCLK_BIAS			0x000000d0

/*
 * Fields in PADS_REFCLK_CFG*. Those registers form an array of 16-bit
 * entries, one entry per PCIe port. These field definitions and desired
 * values aren't in the TRM, but do come from NVIDIA.
 */
#define PADS_REFCLK_CFG_TERM_SHIFT		2  /* 6:2 */
#define PADS_REFCLK_CFG_E_TERM_SHIFT		7
#define PADS_REFCLK_CFG_PREDI_SHIFT		8  /* 11:8 */
#define PADS_REFCLK_CFG_DRVI_SHIFT		12 /* 15:12 */

#define PME_ACK_TIMEOUT 10000
#define LINK_RETRAIN_TIMEOUT 100000 /* in usec */

struct tegra_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	struct mutex map_lock;
	spinlock_t mask_lock;
	void *virt;
	dma_addr_t phys;
	int irq;
};

/* used to differentiate between Tegra SoC generations */
struct tegra_pcie_port_soc {
	struct {
		u8 turnoff_bit;
		u8 ack_bit;
	} pme;
};

struct tegra_pcie_soc {
	unsigned int num_ports;
	const struct tegra_pcie_port_soc *ports;
	unsigned int msi_base_shift;
	unsigned long afi_pex2_ctrl;
	u32 pads_pll_ctl;
	u32 tx_ref_sel;
	u32 pads_refclk_cfg0;
	u32 pads_refclk_cfg1;
	u32 update_fc_threshold;
	bool has_pex_clkreq_en;
	bool has_pex_bias_ctrl;
	bool has_intr_prsnt_sense;
	bool has_cml_clk;
	bool has_gen2;
	bool force_pca_enable;
	bool program_uphy;
	bool update_clamp_threshold;
	bool program_deskew_time;
	bool update_fc_timer;
	bool has_cache_bars;
	struct {
		struct {
			u32 rp_ectl_2_r1;
			u32 rp_ectl_4_r1;
			u32 rp_ectl_5_r1;
			u32 rp_ectl_6_r1;
			u32 rp_ectl_2_r2;
			u32 rp_ectl_4_r2;
			u32 rp_ectl_5_r2;
			u32 rp_ectl_6_r2;
		} regs;
		bool enable;
	} ectl;
};

struct tegra_pcie {
	struct device *dev;

	void __iomem *pads;
	void __iomem *afi;
	void __iomem *cfg;
	int irq;

	struct resource cs;

	struct clk *pex_clk;
	struct clk *afi_clk;
	struct clk *pll_e;
	struct clk *cml_clk;

	struct reset_control *pex_rst;
	struct reset_control *afi_rst;
	struct reset_control *pcie_xrst;

	bool legacy_phy;
	struct phy *phy;

	struct tegra_msi msi;

	struct list_head ports;
	u32 xbar_config;

	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;

	const struct tegra_pcie_soc *soc;
	struct dentry *debugfs;
};

static inline struct tegra_pcie *msi_to_pcie(struct tegra_msi *msi)
{
	return container_of(msi, struct tegra_pcie, msi);
}

struct tegra_pcie_port {
	struct tegra_pcie *pcie;
	struct device_node *np;
	struct list_head list;
	struct resource regs;
	void __iomem *base;
	unsigned int index;
	unsigned int lanes;

	struct phy **phys;

	struct gpio_desc *reset_gpio;
};

static inline void afi_writel(struct tegra_pcie *pcie, u32 value,
			      unsigned long offset)
{
	writel(value, pcie->afi + offset);
}

static inline u32 afi_readl(struct tegra_pcie *pcie, unsigned long offset)
{
	return readl(pcie->afi + offset);
}

static inline void pads_writel(struct tegra_pcie *pcie, u32 value,
			       unsigned long offset)
{
	writel(value, pcie->pads + offset);
}

static inline u32 pads_readl(struct tegra_pcie *pcie, unsigned long offset)
{
	return readl(pcie->pads + offset);
}

/*
 * The configuration space mapping on Tegra is somewhat similar to the ECAM
 * defined by PCIe. However it deviates a bit in how the 4 bits for extended
 * register accesses are mapped:
 *
 *    [27:24] extended register number
 *    [23:16] bus number
 *    [15:11] device number
 *    [10: 8] function number
 *    [ 7: 0] register number
 *
 * Mapping the whole extended configuration space would require 256 MiB of
 * virtual address space, only a small part of which will actually be used.
 *
 * To work around this, a 4 KiB region is used to generate the required
 * configuration transaction with relevant B:D:F and register offset values.
 * This is achieved by dynamically programming base address and size of
 * AFI_AXI_BAR used for end point config space mapping to make sure that the
 * address (access to which generates correct config transaction) falls in
 * this 4 KiB region.
 */
static unsigned int tegra_pcie_conf_offset(u8 bus, unsigned int devfn,
					   unsigned int where)
{
	return ((where & 0xf00) << 16) | (bus << 16) | (PCI_SLOT(devfn) << 11) |
	       (PCI_FUNC(devfn) << 8) | (where & 0xff);
}

static void __iomem *tegra_pcie_map_bus(struct pci_bus *bus,
					unsigned int devfn,
					int where)
{
	struct tegra_pcie *pcie = bus->sysdata;
	void __iomem *addr = NULL;

	if (bus->number == 0) {
		unsigned int slot = PCI_SLOT(devfn);
		struct tegra_pcie_port *port;

		list_for_each_entry(port, &pcie->ports, list) {
			if (port->index + 1 == slot) {
				addr = port->base + (where & ~3);
				break;
			}
		}
	} else {
		unsigned int offset;
		u32 base;

		offset = tegra_pcie_conf_offset(bus->number, devfn, where);

		/* move 4 KiB window to offset within the FPCI region */
		base = 0xfe100000 + ((offset & ~(SZ_4K - 1)) >> 8);
		afi_writel(pcie, base, AFI_FPCI_BAR0);

		/* move to correct offset within the 4 KiB page */
		addr = pcie->cfg + (offset & (SZ_4K - 1));
	}

	return addr;
}

static int tegra_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *value)
{
	if (bus->number == 0)
		return pci_generic_config_read32(bus, devfn, where, size,
						 value);

	return pci_generic_config_read(bus, devfn, where, size, value);
}

static int tegra_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 value)
{
	if (bus->number == 0)
		return pci_generic_config_write32(bus, devfn, where, size,
						  value);

	return pci_generic_config_write(bus, devfn, where, size, value);
}

static struct pci_ops tegra_pcie_ops = {
	.map_bus = tegra_pcie_map_bus,
	.read = tegra_pcie_config_read,
	.write = tegra_pcie_config_write,
};

static unsigned long tegra_pcie_port_get_pex_ctrl(struct tegra_pcie_port *port)
{
	const struct tegra_pcie_soc *soc = port->pcie->soc;
	unsigned long ret = 0;

	switch (port->index) {
	case 0:
		ret = AFI_PEX0_CTRL;
		break;

	case 1:
		ret = AFI_PEX1_CTRL;
		break;

	case 2:
		ret = soc->afi_pex2_ctrl;
		break;
	}

	return ret;
}

static void tegra_pcie_port_reset(struct tegra_pcie_port *port)
{
	unsigned long ctrl = tegra_pcie_port_get_pex_ctrl(port);
	unsigned long value;

	/* pulse reset signal */
	if (port->reset_gpio) {
		gpiod_set_value(port->reset_gpio, 1);
	} else {
		value = afi_readl(port->pcie, ctrl);
		value &= ~AFI_PEX_CTRL_RST;
		afi_writel(port->pcie, value, ctrl);
	}

	usleep_range(1000, 2000);

	if (port->reset_gpio) {
		gpiod_set_value(port->reset_gpio, 0);
	} else {
		value = afi_readl(port->pcie, ctrl);
		value |= AFI_PEX_CTRL_RST;
		afi_writel(port->pcie, value, ctrl);
	}
}

static void tegra_pcie_enable_rp_features(struct tegra_pcie_port *port)
{
	const struct tegra_pcie_soc *soc = port->pcie->soc;
	u32 value;

	/* Enable AER capability */
	value = readl(port->base + RP_VEND_CTL1);
	value |= RP_VEND_CTL1_ERPT;
	writel(value, port->base + RP_VEND_CTL1);

	/* Optimal settings to enhance bandwidth */
	value = readl(port->base + RP_VEND_XP);
	value |= RP_VEND_XP_OPPORTUNISTIC_ACK;
	value |= RP_VEND_XP_OPPORTUNISTIC_UPDATEFC;
	writel(value, port->base + RP_VEND_XP);

	/*
	 * LTSSM will wait for DLLP to finish before entering L1 or L2,
	 * to avoid truncation of PM messages which results in receiver errors
	 */
	value = readl(port->base + RP_VEND_XP_BIST);
	value |= RP_VEND_XP_BIST_GOTO_L1_L2_AFTER_DLLP_DONE;
	writel(value, port->base + RP_VEND_XP_BIST);

	value = readl(port->base + RP_PRIV_MISC);
	value |= RP_PRIV_MISC_CTLR_CLK_CLAMP_ENABLE;
	value |= RP_PRIV_MISC_TMS_CLK_CLAMP_ENABLE;

	if (soc->update_clamp_threshold) {
		value &= ~(RP_PRIV_MISC_CTLR_CLK_CLAMP_THRESHOLD_MASK |
				RP_PRIV_MISC_TMS_CLK_CLAMP_THRESHOLD_MASK);
		value |= RP_PRIV_MISC_CTLR_CLK_CLAMP_THRESHOLD |
			RP_PRIV_MISC_TMS_CLK_CLAMP_THRESHOLD;
	}

	writel(value, port->base + RP_PRIV_MISC);
}

static void tegra_pcie_program_ectl_settings(struct tegra_pcie_port *port)
{
	const struct tegra_pcie_soc *soc = port->pcie->soc;
	u32 value;

	value = readl(port->base + RP_ECTL_2_R1);
	value &= ~RP_ECTL_2_R1_RX_CTLE_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_2_r1;
	writel(value, port->base + RP_ECTL_2_R1);

	value = readl(port->base + RP_ECTL_4_R1);
	value &= ~RP_ECTL_4_R1_RX_CDR_CTRL_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_4_r1 <<
				RP_ECTL_4_R1_RX_CDR_CTRL_1C_SHIFT;
	writel(value, port->base + RP_ECTL_4_R1);

	value = readl(port->base + RP_ECTL_5_R1);
	value &= ~RP_ECTL_5_R1_RX_EQ_CTRL_L_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_5_r1;
	writel(value, port->base + RP_ECTL_5_R1);

	value = readl(port->base + RP_ECTL_6_R1);
	value &= ~RP_ECTL_6_R1_RX_EQ_CTRL_H_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_6_r1;
	writel(value, port->base + RP_ECTL_6_R1);

	value = readl(port->base + RP_ECTL_2_R2);
	value &= ~RP_ECTL_2_R2_RX_CTLE_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_2_r2;
	writel(value, port->base + RP_ECTL_2_R2);

	value = readl(port->base + RP_ECTL_4_R2);
	value &= ~RP_ECTL_4_R2_RX_CDR_CTRL_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_4_r2 <<
				RP_ECTL_4_R2_RX_CDR_CTRL_1C_SHIFT;
	writel(value, port->base + RP_ECTL_4_R2);

	value = readl(port->base + RP_ECTL_5_R2);
	value &= ~RP_ECTL_5_R2_RX_EQ_CTRL_L_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_5_r2;
	writel(value, port->base + RP_ECTL_5_R2);

	value = readl(port->base + RP_ECTL_6_R2);
	value &= ~RP_ECTL_6_R2_RX_EQ_CTRL_H_1C_MASK;
	value |= soc->ectl.regs.rp_ectl_6_r2;
	writel(value, port->base + RP_ECTL_6_R2);
}

static void tegra_pcie_apply_sw_fixup(struct tegra_pcie_port *port)
{
	const struct tegra_pcie_soc *soc = port->pcie->soc;
	u32 value;

	/*
	 * Sometimes link speed change from Gen2 to Gen1 fails due to
	 * instability in deskew logic on lane-0. Increase the deskew
	 * retry time to resolve this issue.
	 */
	if (soc->program_deskew_time) {
		value = readl(port->base + RP_VEND_CTL0);
		value &= ~RP_VEND_CTL0_DSK_RST_PULSE_WIDTH_MASK;
		value |= RP_VEND_CTL0_DSK_RST_PULSE_WIDTH;
		writel(value, port->base + RP_VEND_CTL0);
	}

	if (soc->update_fc_timer) {
		value = readl(port->base + RP_VEND_XP);
		value &= ~RP_VEND_XP_UPDATE_FC_THRESHOLD_MASK;
		value |= soc->update_fc_threshold;
		writel(value, port->base + RP_VEND_XP);
	}

	/*
	 * PCIe link doesn't come up with few legacy PCIe endpoints if
	 * root port advertises both Gen-1 and Gen-2 speeds in Tegra.
	 * Hence, the strategy followed here is to initially advertise
	 * only Gen-1 and after link is up, retrain link to Gen-2 speed
	 */
	value = readl(port->base + RP_LINK_CONTROL_STATUS_2);
	value &= ~PCI_EXP_LNKSTA_CLS;
	value |= PCI_EXP_LNKSTA_CLS_2_5GB;
	writel(value, port->base + RP_LINK_CONTROL_STATUS_2);
}

static void tegra_pcie_port_enable(struct tegra_pcie_port *port)
{
	unsigned long ctrl = tegra_pcie_port_get_pex_ctrl(port);
	const struct tegra_pcie_soc *soc = port->pcie->soc;
	unsigned long value;

	/* enable reference clock */
	value = afi_readl(port->pcie, ctrl);
	value |= AFI_PEX_CTRL_REFCLK_EN;

	if (soc->has_pex_clkreq_en)
		value |= AFI_PEX_CTRL_CLKREQ_EN;

	value |= AFI_PEX_CTRL_OVERRIDE_EN;

	afi_writel(port->pcie, value, ctrl);

	tegra_pcie_port_reset(port);

	if (soc->force_pca_enable) {
		value = readl(port->base + RP_VEND_CTL2);
		value |= RP_VEND_CTL2_PCA_ENABLE;
		writel(value, port->base + RP_VEND_CTL2);
	}

	tegra_pcie_enable_rp_features(port);

	if (soc->ectl.enable)
		tegra_pcie_program_ectl_settings(port);

	tegra_pcie_apply_sw_fixup(port);
}

static void tegra_pcie_port_disable(struct tegra_pcie_port *port)
{
	unsigned long ctrl = tegra_pcie_port_get_pex_ctrl(port);
	const struct tegra_pcie_soc *soc = port->pcie->soc;
	unsigned long value;

	/* assert port reset */
	value = afi_readl(port->pcie, ctrl);
	value &= ~AFI_PEX_CTRL_RST;
	afi_writel(port->pcie, value, ctrl);

	/* disable reference clock */
	value = afi_readl(port->pcie, ctrl);

	if (soc->has_pex_clkreq_en)
		value &= ~AFI_PEX_CTRL_CLKREQ_EN;

	value &= ~AFI_PEX_CTRL_REFCLK_EN;
	afi_writel(port->pcie, value, ctrl);

	/* disable PCIe port and set CLKREQ# as GPIO to allow PLLE power down */
	value = afi_readl(port->pcie, AFI_PCIE_CONFIG);
	value |= AFI_PCIE_CONFIG_PCIE_DISABLE(port->index);
	value |= AFI_PCIE_CONFIG_PCIE_CLKREQ_GPIO(port->index);
	afi_writel(port->pcie, value, AFI_PCIE_CONFIG);
}

static void tegra_pcie_port_free(struct tegra_pcie_port *port)
{
	struct tegra_pcie *pcie = port->pcie;
	struct device *dev = pcie->dev;

	devm_iounmap(dev, port->base);
	devm_release_mem_region(dev, port->regs.start,
				resource_size(&port->regs));
	list_del(&port->list);
	devm_kfree(dev, port);
}

/* Tegra PCIE root complex wrongly reports device class */
static void tegra_pcie_fixup_class(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI_NORMAL;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0bf0, tegra_pcie_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0bf1, tegra_pcie_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0e1c, tegra_pcie_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0e1d, tegra_pcie_fixup_class);

/* Tegra20 and Tegra30 PCIE requires relaxed ordering */
static void tegra_pcie_relax_enable(struct pci_dev *dev)
{
	pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, 0x0bf0, tegra_pcie_relax_enable);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, 0x0bf1, tegra_pcie_relax_enable);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, 0x0e1c, tegra_pcie_relax_enable);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, 0x0e1d, tegra_pcie_relax_enable);

static int tegra_pcie_map_irq(const struct pci_dev *pdev, u8 slot, u8 pin)
{
	struct tegra_pcie *pcie = pdev->bus->sysdata;
	int irq;

	tegra_cpuidle_pcie_irqs_in_use();

	irq = of_irq_parse_and_map_pci(pdev, slot, pin);
	if (!irq)
		irq = pcie->irq;

	return irq;
}

static irqreturn_t tegra_pcie_isr(int irq, void *arg)
{
	static const char * const err_msg[] = {
		"Unknown",
		"AXI slave error",
		"AXI decode error",
		"Target abort",
		"Master abort",
		"Invalid write",
		"Legacy interrupt",
		"Response decoding error",
		"AXI response decoding error",
		"Transaction timeout",
		"Slot present pin change",
		"Slot clock request change",
		"TMS clock ramp change",
		"TMS ready for power down",
		"Peer2Peer error",
	};
	struct tegra_pcie *pcie = arg;
	struct device *dev = pcie->dev;
	u32 code, signature;

	code = afi_readl(pcie, AFI_INTR_CODE) & AFI_INTR_CODE_MASK;
	signature = afi_readl(pcie, AFI_INTR_SIGNATURE);
	afi_writel(pcie, 0, AFI_INTR_CODE);

	if (code == AFI_INTR_LEGACY)
		return IRQ_NONE;

	if (code >= ARRAY_SIZE(err_msg))
		code = 0;

	/*
	 * do not pollute kernel log with master abort reports since they
	 * happen a lot during enumeration
	 */
	if (code == AFI_INTR_MASTER_ABORT || code == AFI_INTR_PE_PRSNT_SENSE)
		dev_dbg(dev, "%s, signature: %08x\n", err_msg[code], signature);
	else
		dev_err(dev, "%s, signature: %08x\n", err_msg[code], signature);

	if (code == AFI_INTR_TARGET_ABORT || code == AFI_INTR_MASTER_ABORT ||
	    code == AFI_INTR_FPCI_DECODE_ERROR) {
		u32 fpci = afi_readl(pcie, AFI_UPPER_FPCI_ADDRESS) & 0xff;
		u64 address = (u64)fpci << 32 | (signature & 0xfffffffc);

		if (code == AFI_INTR_MASTER_ABORT)
			dev_dbg(dev, "  FPCI address: %10llx\n", address);
		else
			dev_err(dev, "  FPCI address: %10llx\n", address);
	}

	return IRQ_HANDLED;
}

/*
 * FPCI map is as follows:
 * - 0xfdfc000000: I/O space
 * - 0xfdfe000000: type 0 configuration space
 * - 0xfdff000000: type 1 configuration space
 * - 0xfe00000000: type 0 extended configuration space
 * - 0xfe10000000: type 1 extended configuration space
 */
static void tegra_pcie_setup_translations(struct tegra_pcie *pcie)
{
	u32 size;
	struct resource_entry *entry;
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(pcie);

	/* Bar 0: type 1 extended configuration space */
	size = resource_size(&pcie->cs);
	afi_writel(pcie, pcie->cs.start, AFI_AXI_BAR0_START);
	afi_writel(pcie, size >> 12, AFI_AXI_BAR0_SZ);

	resource_list_for_each_entry(entry, &bridge->windows) {
		u32 fpci_bar, axi_address;
		struct resource *res = entry->res;

		size = resource_size(res);

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			/* Bar 1: downstream IO bar */
			fpci_bar = 0xfdfc0000;
			axi_address = pci_pio_to_address(res->start);
			afi_writel(pcie, axi_address, AFI_AXI_BAR1_START);
			afi_writel(pcie, size >> 12, AFI_AXI_BAR1_SZ);
			afi_writel(pcie, fpci_bar, AFI_FPCI_BAR1);
			break;
		case IORESOURCE_MEM:
			fpci_bar = (((res->start >> 12) & 0x0fffffff) << 4) | 0x1;
			axi_address = res->start;

			if (res->flags & IORESOURCE_PREFETCH) {
				/* Bar 2: prefetchable memory BAR */
				afi_writel(pcie, axi_address, AFI_AXI_BAR2_START);
				afi_writel(pcie, size >> 12, AFI_AXI_BAR2_SZ);
				afi_writel(pcie, fpci_bar, AFI_FPCI_BAR2);

			} else {
				/* Bar 3: non prefetchable memory BAR */
				afi_writel(pcie, axi_address, AFI_AXI_BAR3_START);
				afi_writel(pcie, size >> 12, AFI_AXI_BAR3_SZ);
				afi_writel(pcie, fpci_bar, AFI_FPCI_BAR3);
			}
			break;
		}
	}

	/* NULL out the remaining BARs as they are not used */
	afi_writel(pcie, 0, AFI_AXI_BAR4_START);
	afi_writel(pcie, 0, AFI_AXI_BAR4_SZ);
	afi_writel(pcie, 0, AFI_FPCI_BAR4);

	afi_writel(pcie, 0, AFI_AXI_BAR5_START);
	afi_writel(pcie, 0, AFI_AXI_BAR5_SZ);
	afi_writel(pcie, 0, AFI_FPCI_BAR5);

	if (pcie->soc->has_cache_bars) {
		/* map all upstream transactions as uncached */
		afi_writel(pcie, 0, AFI_CACHE_BAR0_ST);
		afi_writel(pcie, 0, AFI_CACHE_BAR0_SZ);
		afi_writel(pcie, 0, AFI_CACHE_BAR1_ST);
		afi_writel(pcie, 0, AFI_CACHE_BAR1_SZ);
	}

	/* MSI translations are setup only when needed */
	afi_writel(pcie, 0, AFI_MSI_FPCI_BAR_ST);
	afi_writel(pcie, 0, AFI_MSI_BAR_SZ);
	afi_writel(pcie, 0, AFI_MSI_AXI_BAR_ST);
	afi_writel(pcie, 0, AFI_MSI_BAR_SZ);
}

static int tegra_pcie_pll_wait(struct tegra_pcie *pcie, unsigned long timeout)
{
	const struct tegra_pcie_soc *soc = pcie->soc;
	u32 value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		value = pads_readl(pcie, soc->pads_pll_ctl);
		if (value & PADS_PLL_CTL_LOCKDET)
			return 0;
	}

	return -ETIMEDOUT;
}

static int tegra_pcie_phy_enable(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	const struct tegra_pcie_soc *soc = pcie->soc;
	u32 value;
	int err;

	/* initialize internal PHY, enable up to 16 PCIE lanes */
	pads_writel(pcie, 0x0, PADS_CTL_SEL);

	/* override IDDQ to 1 on all 4 lanes */
	value = pads_readl(pcie, PADS_CTL);
	value |= PADS_CTL_IDDQ_1L;
	pads_writel(pcie, value, PADS_CTL);

	/*
	 * Set up PHY PLL inputs select PLLE output as refclock,
	 * set TX ref sel to div10 (not div5).
	 */
	value = pads_readl(pcie, soc->pads_pll_ctl);
	value &= ~(PADS_PLL_CTL_REFCLK_MASK | PADS_PLL_CTL_TXCLKREF_MASK);
	value |= PADS_PLL_CTL_REFCLK_INTERNAL_CML | soc->tx_ref_sel;
	pads_writel(pcie, value, soc->pads_pll_ctl);

	/* reset PLL */
	value = pads_readl(pcie, soc->pads_pll_ctl);
	value &= ~PADS_PLL_CTL_RST_B4SM;
	pads_writel(pcie, value, soc->pads_pll_ctl);

	usleep_range(20, 100);

	/* take PLL out of reset  */
	value = pads_readl(pcie, soc->pads_pll_ctl);
	value |= PADS_PLL_CTL_RST_B4SM;
	pads_writel(pcie, value, soc->pads_pll_ctl);

	/* wait for the PLL to lock */
	err = tegra_pcie_pll_wait(pcie, 500);
	if (err < 0) {
		dev_err(dev, "PLL failed to lock: %d\n", err);
		return err;
	}

	/* turn off IDDQ override */
	value = pads_readl(pcie, PADS_CTL);
	value &= ~PADS_CTL_IDDQ_1L;
	pads_writel(pcie, value, PADS_CTL);

	/* enable TX/RX data */
	value = pads_readl(pcie, PADS_CTL);
	value |= PADS_CTL_TX_DATA_EN_1L | PADS_CTL_RX_DATA_EN_1L;
	pads_writel(pcie, value, PADS_CTL);

	return 0;
}

static int tegra_pcie_phy_disable(struct tegra_pcie *pcie)
{
	const struct tegra_pcie_soc *soc = pcie->soc;
	u32 value;

	/* disable TX/RX data */
	value = pads_readl(pcie, PADS_CTL);
	value &= ~(PADS_CTL_TX_DATA_EN_1L | PADS_CTL_RX_DATA_EN_1L);
	pads_writel(pcie, value, PADS_CTL);

	/* override IDDQ */
	value = pads_readl(pcie, PADS_CTL);
	value |= PADS_CTL_IDDQ_1L;
	pads_writel(pcie, value, PADS_CTL);

	/* reset PLL */
	value = pads_readl(pcie, soc->pads_pll_ctl);
	value &= ~PADS_PLL_CTL_RST_B4SM;
	pads_writel(pcie, value, soc->pads_pll_ctl);

	usleep_range(20, 100);

	return 0;
}

static int tegra_pcie_port_phy_power_on(struct tegra_pcie_port *port)
{
	struct device *dev = port->pcie->dev;
	unsigned int i;
	int err;

	for (i = 0; i < port->lanes; i++) {
		err = phy_power_on(port->phys[i]);
		if (err < 0) {
			dev_err(dev, "failed to power on PHY#%u: %d\n", i, err);
			return err;
		}
	}

	return 0;
}

static int tegra_pcie_port_phy_power_off(struct tegra_pcie_port *port)
{
	struct device *dev = port->pcie->dev;
	unsigned int i;
	int err;

	for (i = 0; i < port->lanes; i++) {
		err = phy_power_off(port->phys[i]);
		if (err < 0) {
			dev_err(dev, "failed to power off PHY#%u: %d\n", i,
				err);
			return err;
		}
	}

	return 0;
}

static int tegra_pcie_phy_power_on(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct tegra_pcie_port *port;
	int err;

	if (pcie->legacy_phy) {
		if (pcie->phy)
			err = phy_power_on(pcie->phy);
		else
			err = tegra_pcie_phy_enable(pcie);

		if (err < 0)
			dev_err(dev, "failed to power on PHY: %d\n", err);

		return err;
	}

	list_for_each_entry(port, &pcie->ports, list) {
		err = tegra_pcie_port_phy_power_on(port);
		if (err < 0) {
			dev_err(dev,
				"failed to power on PCIe port %u PHY: %d\n",
				port->index, err);
			return err;
		}
	}

	return 0;
}

static int tegra_pcie_phy_power_off(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct tegra_pcie_port *port;
	int err;

	if (pcie->legacy_phy) {
		if (pcie->phy)
			err = phy_power_off(pcie->phy);
		else
			err = tegra_pcie_phy_disable(pcie);

		if (err < 0)
			dev_err(dev, "failed to power off PHY: %d\n", err);

		return err;
	}

	list_for_each_entry(port, &pcie->ports, list) {
		err = tegra_pcie_port_phy_power_off(port);
		if (err < 0) {
			dev_err(dev,
				"failed to power off PCIe port %u PHY: %d\n",
				port->index, err);
			return err;
		}
	}

	return 0;
}

static void tegra_pcie_enable_controller(struct tegra_pcie *pcie)
{
	const struct tegra_pcie_soc *soc = pcie->soc;
	struct tegra_pcie_port *port;
	unsigned long value;

	/* enable PLL power down */
	if (pcie->phy) {
		value = afi_readl(pcie, AFI_PLLE_CONTROL);
		value &= ~AFI_PLLE_CONTROL_BYPASS_PADS2PLLE_CONTROL;
		value |= AFI_PLLE_CONTROL_PADS2PLLE_CONTROL_EN;
		afi_writel(pcie, value, AFI_PLLE_CONTROL);
	}

	/* power down PCIe slot clock bias pad */
	if (soc->has_pex_bias_ctrl)
		afi_writel(pcie, 0, AFI_PEXBIAS_CTRL_0);

	/* configure mode and disable all ports */
	value = afi_readl(pcie, AFI_PCIE_CONFIG);
	value &= ~AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_MASK;
	value |= AFI_PCIE_CONFIG_PCIE_DISABLE_ALL | pcie->xbar_config;
	value |= AFI_PCIE_CONFIG_PCIE_CLKREQ_GPIO_ALL;

	list_for_each_entry(port, &pcie->ports, list) {
		value &= ~AFI_PCIE_CONFIG_PCIE_DISABLE(port->index);
		value &= ~AFI_PCIE_CONFIG_PCIE_CLKREQ_GPIO(port->index);
	}

	afi_writel(pcie, value, AFI_PCIE_CONFIG);

	if (soc->has_gen2) {
		value = afi_readl(pcie, AFI_FUSE);
		value &= ~AFI_FUSE_PCIE_T0_GEN2_DIS;
		afi_writel(pcie, value, AFI_FUSE);
	} else {
		value = afi_readl(pcie, AFI_FUSE);
		value |= AFI_FUSE_PCIE_T0_GEN2_DIS;
		afi_writel(pcie, value, AFI_FUSE);
	}

	/* Disable AFI dynamic clock gating and enable PCIe */
	value = afi_readl(pcie, AFI_CONFIGURATION);
	value |= AFI_CONFIGURATION_EN_FPCI;
	value |= AFI_CONFIGURATION_CLKEN_OVERRIDE;
	afi_writel(pcie, value, AFI_CONFIGURATION);

	value = AFI_INTR_EN_INI_SLVERR | AFI_INTR_EN_INI_DECERR |
		AFI_INTR_EN_TGT_SLVERR | AFI_INTR_EN_TGT_DECERR |
		AFI_INTR_EN_TGT_WRERR | AFI_INTR_EN_DFPCI_DECERR;

	if (soc->has_intr_prsnt_sense)
		value |= AFI_INTR_EN_PRSNT_SENSE;

	afi_writel(pcie, value, AFI_AFI_INTR_ENABLE);
	afi_writel(pcie, 0xffffffff, AFI_SM_INTR_ENABLE);

	/* don't enable MSI for now, only when needed */
	afi_writel(pcie, AFI_INTR_MASK_INT_MASK, AFI_INTR_MASK);

	/* disable all exceptions */
	afi_writel(pcie, 0, AFI_FPCI_ERROR_MASKS);
}

static void tegra_pcie_power_off(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	const struct tegra_pcie_soc *soc = pcie->soc;
	int err;

	reset_control_assert(pcie->afi_rst);

	clk_disable_unprepare(pcie->pll_e);
	if (soc->has_cml_clk)
		clk_disable_unprepare(pcie->cml_clk);
	clk_disable_unprepare(pcie->afi_clk);

	if (!dev->pm_domain)
		tegra_powergate_power_off(TEGRA_POWERGATE_PCIE);

	err = regulator_bulk_disable(pcie->num_supplies, pcie->supplies);
	if (err < 0)
		dev_warn(dev, "failed to disable regulators: %d\n", err);
}

static int tegra_pcie_power_on(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	const struct tegra_pcie_soc *soc = pcie->soc;
	int err;

	reset_control_assert(pcie->pcie_xrst);
	reset_control_assert(pcie->afi_rst);
	reset_control_assert(pcie->pex_rst);

	if (!dev->pm_domain)
		tegra_powergate_power_off(TEGRA_POWERGATE_PCIE);

	/* enable regulators */
	err = regulator_bulk_enable(pcie->num_supplies, pcie->supplies);
	if (err < 0)
		dev_err(dev, "failed to enable regulators: %d\n", err);

	if (!dev->pm_domain) {
		err = tegra_powergate_power_on(TEGRA_POWERGATE_PCIE);
		if (err) {
			dev_err(dev, "failed to power ungate: %d\n", err);
			goto regulator_disable;
		}
		err = tegra_powergate_remove_clamping(TEGRA_POWERGATE_PCIE);
		if (err) {
			dev_err(dev, "failed to remove clamp: %d\n", err);
			goto powergate;
		}
	}

	err = clk_prepare_enable(pcie->afi_clk);
	if (err < 0) {
		dev_err(dev, "failed to enable AFI clock: %d\n", err);
		goto powergate;
	}

	if (soc->has_cml_clk) {
		err = clk_prepare_enable(pcie->cml_clk);
		if (err < 0) {
			dev_err(dev, "failed to enable CML clock: %d\n", err);
			goto disable_afi_clk;
		}
	}

	err = clk_prepare_enable(pcie->pll_e);
	if (err < 0) {
		dev_err(dev, "failed to enable PLLE clock: %d\n", err);
		goto disable_cml_clk;
	}

	reset_control_deassert(pcie->afi_rst);

	return 0;

disable_cml_clk:
	if (soc->has_cml_clk)
		clk_disable_unprepare(pcie->cml_clk);
disable_afi_clk:
	clk_disable_unprepare(pcie->afi_clk);
powergate:
	if (!dev->pm_domain)
		tegra_powergate_power_off(TEGRA_POWERGATE_PCIE);
regulator_disable:
	regulator_bulk_disable(pcie->num_supplies, pcie->supplies);

	return err;
}

static void tegra_pcie_apply_pad_settings(struct tegra_pcie *pcie)
{
	const struct tegra_pcie_soc *soc = pcie->soc;

	/* Configure the reference clock driver */
	pads_writel(pcie, soc->pads_refclk_cfg0, PADS_REFCLK_CFG0);

	if (soc->num_ports > 2)
		pads_writel(pcie, soc->pads_refclk_cfg1, PADS_REFCLK_CFG1);
}

static int tegra_pcie_clocks_get(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	const struct tegra_pcie_soc *soc = pcie->soc;

	pcie->pex_clk = devm_clk_get(dev, "pex");
	if (IS_ERR(pcie->pex_clk))
		return PTR_ERR(pcie->pex_clk);

	pcie->afi_clk = devm_clk_get(dev, "afi");
	if (IS_ERR(pcie->afi_clk))
		return PTR_ERR(pcie->afi_clk);

	pcie->pll_e = devm_clk_get(dev, "pll_e");
	if (IS_ERR(pcie->pll_e))
		return PTR_ERR(pcie->pll_e);

	if (soc->has_cml_clk) {
		pcie->cml_clk = devm_clk_get(dev, "cml");
		if (IS_ERR(pcie->cml_clk))
			return PTR_ERR(pcie->cml_clk);
	}

	return 0;
}

static int tegra_pcie_resets_get(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;

	pcie->pex_rst = devm_reset_control_get_exclusive(dev, "pex");
	if (IS_ERR(pcie->pex_rst))
		return PTR_ERR(pcie->pex_rst);

	pcie->afi_rst = devm_reset_control_get_exclusive(dev, "afi");
	if (IS_ERR(pcie->afi_rst))
		return PTR_ERR(pcie->afi_rst);

	pcie->pcie_xrst = devm_reset_control_get_exclusive(dev, "pcie_x");
	if (IS_ERR(pcie->pcie_xrst))
		return PTR_ERR(pcie->pcie_xrst);

	return 0;
}

static int tegra_pcie_phys_get_legacy(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	int err;

	pcie->phy = devm_phy_optional_get(dev, "pcie");
	if (IS_ERR(pcie->phy)) {
		err = PTR_ERR(pcie->phy);
		dev_err(dev, "failed to get PHY: %d\n", err);
		return err;
	}

	err = phy_init(pcie->phy);
	if (err < 0) {
		dev_err(dev, "failed to initialize PHY: %d\n", err);
		return err;
	}

	pcie->legacy_phy = true;

	return 0;
}

static struct phy *devm_of_phy_optional_get_index(struct device *dev,
						  struct device_node *np,
						  const char *consumer,
						  unsigned int index)
{
	struct phy *phy;
	char *name;

	name = kasprintf(GFP_KERNEL, "%s-%u", consumer, index);
	if (!name)
		return ERR_PTR(-ENOMEM);

	phy = devm_of_phy_get(dev, np, name);
	kfree(name);

	if (PTR_ERR(phy) == -ENODEV)
		phy = NULL;

	return phy;
}

static int tegra_pcie_port_get_phys(struct tegra_pcie_port *port)
{
	struct device *dev = port->pcie->dev;
	struct phy *phy;
	unsigned int i;
	int err;

	port->phys = devm_kcalloc(dev, sizeof(phy), port->lanes, GFP_KERNEL);
	if (!port->phys)
		return -ENOMEM;

	for (i = 0; i < port->lanes; i++) {
		phy = devm_of_phy_optional_get_index(dev, port->np, "pcie", i);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to get PHY#%u: %ld\n", i,
				PTR_ERR(phy));
			return PTR_ERR(phy);
		}

		err = phy_init(phy);
		if (err < 0) {
			dev_err(dev, "failed to initialize PHY#%u: %d\n", i,
				err);
			return err;
		}

		port->phys[i] = phy;
	}

	return 0;
}

static int tegra_pcie_phys_get(struct tegra_pcie *pcie)
{
	const struct tegra_pcie_soc *soc = pcie->soc;
	struct device_node *np = pcie->dev->of_node;
	struct tegra_pcie_port *port;
	int err;

	if (!soc->has_gen2 || of_find_property(np, "phys", NULL) != NULL)
		return tegra_pcie_phys_get_legacy(pcie);

	list_for_each_entry(port, &pcie->ports, list) {
		err = tegra_pcie_port_get_phys(port);
		if (err < 0)
			return err;
	}

	return 0;
}

static void tegra_pcie_phys_put(struct tegra_pcie *pcie)
{
	struct tegra_pcie_port *port;
	struct device *dev = pcie->dev;
	int err, i;

	if (pcie->legacy_phy) {
		err = phy_exit(pcie->phy);
		if (err < 0)
			dev_err(dev, "failed to teardown PHY: %d\n", err);
		return;
	}

	list_for_each_entry(port, &pcie->ports, list) {
		for (i = 0; i < port->lanes; i++) {
			err = phy_exit(port->phys[i]);
			if (err < 0)
				dev_err(dev, "failed to teardown PHY#%u: %d\n",
					i, err);
		}
	}
}

static int tegra_pcie_get_resources(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	const struct tegra_pcie_soc *soc = pcie->soc;
	int err;

	err = tegra_pcie_clocks_get(pcie);
	if (err) {
		dev_err(dev, "failed to get clocks: %d\n", err);
		return err;
	}

	err = tegra_pcie_resets_get(pcie);
	if (err) {
		dev_err(dev, "failed to get resets: %d\n", err);
		return err;
	}

	if (soc->program_uphy) {
		err = tegra_pcie_phys_get(pcie);
		if (err < 0) {
			dev_err(dev, "failed to get PHYs: %d\n", err);
			return err;
		}
	}

	pcie->pads = devm_platform_ioremap_resource_byname(pdev, "pads");
	if (IS_ERR(pcie->pads)) {
		err = PTR_ERR(pcie->pads);
		goto phys_put;
	}

	pcie->afi = devm_platform_ioremap_resource_byname(pdev, "afi");
	if (IS_ERR(pcie->afi)) {
		err = PTR_ERR(pcie->afi);
		goto phys_put;
	}

	/* request configuration space, but remap later, on demand */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cs");
	if (!res) {
		err = -EADDRNOTAVAIL;
		goto phys_put;
	}

	pcie->cs = *res;

	/* constrain configuration space to 4 KiB */
	pcie->cs.end = pcie->cs.start + SZ_4K - 1;

	pcie->cfg = devm_ioremap_resource(dev, &pcie->cs);
	if (IS_ERR(pcie->cfg)) {
		err = PTR_ERR(pcie->cfg);
		goto phys_put;
	}

	/* request interrupt */
	err = platform_get_irq_byname(pdev, "intr");
	if (err < 0)
		goto phys_put;

	pcie->irq = err;

	err = request_irq(pcie->irq, tegra_pcie_isr, IRQF_SHARED, "PCIE", pcie);
	if (err) {
		dev_err(dev, "failed to register IRQ: %d\n", err);
		goto phys_put;
	}

	return 0;

phys_put:
	if (soc->program_uphy)
		tegra_pcie_phys_put(pcie);

	return err;
}

static int tegra_pcie_put_resources(struct tegra_pcie *pcie)
{
	const struct tegra_pcie_soc *soc = pcie->soc;

	if (pcie->irq > 0)
		free_irq(pcie->irq, pcie);

	if (soc->program_uphy)
		tegra_pcie_phys_put(pcie);

	return 0;
}

static void tegra_pcie_pme_turnoff(struct tegra_pcie_port *port)
{
	struct tegra_pcie *pcie = port->pcie;
	const struct tegra_pcie_soc *soc = pcie->soc;
	int err;
	u32 val;
	u8 ack_bit;

	val = afi_readl(pcie, AFI_PCIE_PME);
	val |= (0x1 << soc->ports[port->index].pme.turnoff_bit);
	afi_writel(pcie, val, AFI_PCIE_PME);

	ack_bit = soc->ports[port->index].pme.ack_bit;
	err = readl_poll_timeout(pcie->afi + AFI_PCIE_PME, val,
				 val & (0x1 << ack_bit), 1, PME_ACK_TIMEOUT);
	if (err)
		dev_err(pcie->dev, "PME Ack is not received on port: %d\n",
			port->index);

	usleep_range(10000, 11000);

	val = afi_readl(pcie, AFI_PCIE_PME);
	val &= ~(0x1 << soc->ports[port->index].pme.turnoff_bit);
	afi_writel(pcie, val, AFI_PCIE_PME);
}

static void tegra_pcie_msi_irq(struct irq_desc *desc)
{
	struct tegra_pcie *pcie = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct tegra_msi *msi = &pcie->msi;
	struct device *dev = pcie->dev;
	unsigned int i;

	chained_irq_enter(chip, desc);

	for (i = 0; i < 8; i++) {
		unsigned long reg = afi_readl(pcie, AFI_MSI_VEC(i));

		while (reg) {
			unsigned int offset = find_first_bit(&reg, 32);
			unsigned int index = i * 32 + offset;
			int ret;

			ret = generic_handle_domain_irq(msi->domain->parent, index);
			if (ret) {
				/*
				 * that's weird who triggered this?
				 * just clear it
				 */
				dev_info(dev, "unexpected MSI\n");
				afi_writel(pcie, BIT(index % 32), AFI_MSI_VEC(index));
			}

			/* see if there's any more pending in this vector */
			reg = afi_readl(pcie, AFI_MSI_VEC(i));
		}
	}

	chained_irq_exit(chip, desc);
}

static void tegra_msi_top_irq_ack(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void tegra_msi_top_irq_mask(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void tegra_msi_top_irq_unmask(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip tegra_msi_top_chip = {
	.name		= "Tegra PCIe MSI",
	.irq_ack	= tegra_msi_top_irq_ack,
	.irq_mask	= tegra_msi_top_irq_mask,
	.irq_unmask	= tegra_msi_top_irq_unmask,
};

static void tegra_msi_irq_ack(struct irq_data *d)
{
	struct tegra_msi *msi = irq_data_get_irq_chip_data(d);
	struct tegra_pcie *pcie = msi_to_pcie(msi);
	unsigned int index = d->hwirq / 32;

	/* clear the interrupt */
	afi_writel(pcie, BIT(d->hwirq % 32), AFI_MSI_VEC(index));
}

static void tegra_msi_irq_mask(struct irq_data *d)
{
	struct tegra_msi *msi = irq_data_get_irq_chip_data(d);
	struct tegra_pcie *pcie = msi_to_pcie(msi);
	unsigned int index = d->hwirq / 32;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&msi->mask_lock, flags);
	value = afi_readl(pcie, AFI_MSI_EN_VEC(index));
	value &= ~BIT(d->hwirq % 32);
	afi_writel(pcie, value, AFI_MSI_EN_VEC(index));
	spin_unlock_irqrestore(&msi->mask_lock, flags);
}

static void tegra_msi_irq_unmask(struct irq_data *d)
{
	struct tegra_msi *msi = irq_data_get_irq_chip_data(d);
	struct tegra_pcie *pcie = msi_to_pcie(msi);
	unsigned int index = d->hwirq / 32;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&msi->mask_lock, flags);
	value = afi_readl(pcie, AFI_MSI_EN_VEC(index));
	value |= BIT(d->hwirq % 32);
	afi_writel(pcie, value, AFI_MSI_EN_VEC(index));
	spin_unlock_irqrestore(&msi->mask_lock, flags);
}

static int tegra_msi_set_affinity(struct irq_data *d, const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static void tegra_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct tegra_msi *msi = irq_data_get_irq_chip_data(data);

	msg->address_lo = lower_32_bits(msi->phys);
	msg->address_hi = upper_32_bits(msi->phys);
	msg->data = data->hwirq;
}

static struct irq_chip tegra_msi_bottom_chip = {
	.name			= "Tegra MSI",
	.irq_ack		= tegra_msi_irq_ack,
	.irq_mask		= tegra_msi_irq_mask,
	.irq_unmask		= tegra_msi_irq_unmask,
	.irq_set_affinity 	= tegra_msi_set_affinity,
	.irq_compose_msi_msg	= tegra_compose_msi_msg,
};

static int tegra_msi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *args)
{
	struct tegra_msi *msi = domain->host_data;
	unsigned int i;
	int hwirq;

	mutex_lock(&msi->map_lock);

	hwirq = bitmap_find_free_region(msi->used, INT_PCI_MSI_NR, order_base_2(nr_irqs));

	mutex_unlock(&msi->map_lock);

	if (hwirq < 0)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &tegra_msi_bottom_chip, domain->host_data,
				    handle_edge_irq, NULL, NULL);

	tegra_cpuidle_pcie_irqs_in_use();

	return 0;
}

static void tegra_msi_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct tegra_msi *msi = domain->host_data;

	mutex_lock(&msi->map_lock);

	bitmap_release_region(msi->used, d->hwirq, order_base_2(nr_irqs));

	mutex_unlock(&msi->map_lock);
}

static const struct irq_domain_ops tegra_msi_domain_ops = {
	.alloc = tegra_msi_domain_alloc,
	.free = tegra_msi_domain_free,
};

static struct msi_domain_info tegra_msi_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX),
	.chip	= &tegra_msi_top_chip,
};

static int tegra_allocate_domains(struct tegra_msi *msi)
{
	struct tegra_pcie *pcie = msi_to_pcie(msi);
	struct fwnode_handle *fwnode = dev_fwnode(pcie->dev);
	struct irq_domain *parent;

	parent = irq_domain_create_linear(fwnode, INT_PCI_MSI_NR,
					  &tegra_msi_domain_ops, msi);
	if (!parent) {
		dev_err(pcie->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}
	irq_domain_update_bus_token(parent, DOMAIN_BUS_NEXUS);

	msi->domain = pci_msi_create_irq_domain(fwnode, &tegra_msi_info, parent);
	if (!msi->domain) {
		dev_err(pcie->dev, "failed to create MSI domain\n");
		irq_domain_remove(parent);
		return -ENOMEM;
	}

	return 0;
}

static void tegra_free_domains(struct tegra_msi *msi)
{
	struct irq_domain *parent = msi->domain->parent;

	irq_domain_remove(msi->domain);
	irq_domain_remove(parent);
}

static int tegra_pcie_msi_setup(struct tegra_pcie *pcie)
{
	struct platform_device *pdev = to_platform_device(pcie->dev);
	struct tegra_msi *msi = &pcie->msi;
	struct device *dev = pcie->dev;
	int err;

	mutex_init(&msi->map_lock);
	spin_lock_init(&msi->mask_lock);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		err = tegra_allocate_domains(msi);
		if (err)
			return err;
	}

	err = platform_get_irq_byname(pdev, "msi");
	if (err < 0)
		goto free_irq_domain;

	msi->irq = err;

	irq_set_chained_handler_and_data(msi->irq, tegra_pcie_msi_irq, pcie);

	/* Though the PCIe controller can address >32-bit address space, to
	 * facilitate endpoints that support only 32-bit MSI target address,
	 * the mask is set to 32-bit to make sure that MSI target address is
	 * always a 32-bit address
	 */
	err = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (err < 0) {
		dev_err(dev, "failed to set DMA coherent mask: %d\n", err);
		goto free_irq;
	}

	msi->virt = dma_alloc_attrs(dev, PAGE_SIZE, &msi->phys, GFP_KERNEL,
				    DMA_ATTR_NO_KERNEL_MAPPING);
	if (!msi->virt) {
		dev_err(dev, "failed to allocate DMA memory for MSI\n");
		err = -ENOMEM;
		goto free_irq;
	}

	return 0;

free_irq:
	irq_set_chained_handler_and_data(msi->irq, NULL, NULL);
free_irq_domain:
	if (IS_ENABLED(CONFIG_PCI_MSI))
		tegra_free_domains(msi);

	return err;
}

static void tegra_pcie_enable_msi(struct tegra_pcie *pcie)
{
	const struct tegra_pcie_soc *soc = pcie->soc;
	struct tegra_msi *msi = &pcie->msi;
	u32 reg, msi_state[INT_PCI_MSI_NR / 32];
	int i;

	afi_writel(pcie, msi->phys >> soc->msi_base_shift, AFI_MSI_FPCI_BAR_ST);
	afi_writel(pcie, msi->phys, AFI_MSI_AXI_BAR_ST);
	/* this register is in 4K increments */
	afi_writel(pcie, 1, AFI_MSI_BAR_SZ);

	/* Restore the MSI allocation state */
	bitmap_to_arr32(msi_state, msi->used, INT_PCI_MSI_NR);
	for (i = 0; i < ARRAY_SIZE(msi_state); i++)
		afi_writel(pcie, msi_state[i], AFI_MSI_EN_VEC(i));

	/* and unmask the MSI interrupt */
	reg = afi_readl(pcie, AFI_INTR_MASK);
	reg |= AFI_INTR_MASK_MSI_MASK;
	afi_writel(pcie, reg, AFI_INTR_MASK);
}

static void tegra_pcie_msi_teardown(struct tegra_pcie *pcie)
{
	struct tegra_msi *msi = &pcie->msi;
	unsigned int i, irq;

	dma_free_attrs(pcie->dev, PAGE_SIZE, msi->virt, msi->phys,
		       DMA_ATTR_NO_KERNEL_MAPPING);

	for (i = 0; i < INT_PCI_MSI_NR; i++) {
		irq = irq_find_mapping(msi->domain, i);
		if (irq > 0)
			irq_domain_free_irqs(irq, 1);
	}

	irq_set_chained_handler_and_data(msi->irq, NULL, NULL);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		tegra_free_domains(msi);
}

static int tegra_pcie_disable_msi(struct tegra_pcie *pcie)
{
	u32 value;

	/* mask the MSI interrupt */
	value = afi_readl(pcie, AFI_INTR_MASK);
	value &= ~AFI_INTR_MASK_MSI_MASK;
	afi_writel(pcie, value, AFI_INTR_MASK);

	return 0;
}

static void tegra_pcie_disable_interrupts(struct tegra_pcie *pcie)
{
	u32 value;

	value = afi_readl(pcie, AFI_INTR_MASK);
	value &= ~AFI_INTR_MASK_INT_MASK;
	afi_writel(pcie, value, AFI_INTR_MASK);
}

static int tegra_pcie_get_xbar_config(struct tegra_pcie *pcie, u32 lanes,
				      u32 *xbar)
{
	struct device *dev = pcie->dev;
	struct device_node *np = dev->of_node;

	if (of_device_is_compatible(np, "nvidia,tegra186-pcie")) {
		switch (lanes) {
		case 0x010004:
			dev_info(dev, "4x1, 1x1 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_401;
			return 0;

		case 0x010102:
			dev_info(dev, "2x1, 1X1, 1x1 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_211;
			return 0;

		case 0x010101:
			dev_info(dev, "1x1, 1x1, 1x1 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_111;
			return 0;

		default:
			dev_info(dev, "wrong configuration updated in DT, "
				 "switching to default 2x1, 1x1, 1x1 "
				 "configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_211;
			return 0;
		}
	} else if (of_device_is_compatible(np, "nvidia,tegra124-pcie") ||
		   of_device_is_compatible(np, "nvidia,tegra210-pcie")) {
		switch (lanes) {
		case 0x0000104:
			dev_info(dev, "4x1, 1x1 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_X4_X1;
			return 0;

		case 0x0000102:
			dev_info(dev, "2x1, 1x1 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_X2_X1;
			return 0;
		}
	} else if (of_device_is_compatible(np, "nvidia,tegra30-pcie")) {
		switch (lanes) {
		case 0x00000204:
			dev_info(dev, "4x1, 2x1 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_420;
			return 0;

		case 0x00020202:
			dev_info(dev, "2x3 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_222;
			return 0;

		case 0x00010104:
			dev_info(dev, "4x1, 1x2 configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_411;
			return 0;
		}
	} else if (of_device_is_compatible(np, "nvidia,tegra20-pcie")) {
		switch (lanes) {
		case 0x00000004:
			dev_info(dev, "single-mode configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_SINGLE;
			return 0;

		case 0x00000202:
			dev_info(dev, "dual-mode configuration\n");
			*xbar = AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_DUAL;
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * Check whether a given set of supplies is available in a device tree node.
 * This is used to check whether the new or the legacy device tree bindings
 * should be used.
 */
static bool of_regulator_bulk_available(struct device_node *np,
					struct regulator_bulk_data *supplies,
					unsigned int num_supplies)
{
	char property[32];
	unsigned int i;

	for (i = 0; i < num_supplies; i++) {
		snprintf(property, 32, "%s-supply", supplies[i].supply);

		if (of_find_property(np, property, NULL) == NULL)
			return false;
	}

	return true;
}

/*
 * Old versions of the device tree binding for this device used a set of power
 * supplies that didn't match the hardware inputs. This happened to work for a
 * number of cases but is not future proof. However to preserve backwards-
 * compatibility with old device trees, this function will try to use the old
 * set of supplies.
 */
static int tegra_pcie_get_legacy_regulators(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *np = dev->of_node;

	if (of_device_is_compatible(np, "nvidia,tegra30-pcie"))
		pcie->num_supplies = 3;
	else if (of_device_is_compatible(np, "nvidia,tegra20-pcie"))
		pcie->num_supplies = 2;

	if (pcie->num_supplies == 0) {
		dev_err(dev, "device %pOF not supported in legacy mode\n", np);
		return -ENODEV;
	}

	pcie->supplies = devm_kcalloc(dev, pcie->num_supplies,
				      sizeof(*pcie->supplies),
				      GFP_KERNEL);
	if (!pcie->supplies)
		return -ENOMEM;

	pcie->supplies[0].supply = "pex-clk";
	pcie->supplies[1].supply = "vdd";

	if (pcie->num_supplies > 2)
		pcie->supplies[2].supply = "avdd";

	return devm_regulator_bulk_get(dev, pcie->num_supplies, pcie->supplies);
}

/*
 * Obtains the list of regulators required for a particular generation of the
 * IP block.
 *
 * This would've been nice to do simply by providing static tables for use
 * with the regulator_bulk_*() API, but unfortunately Tegra30 is a bit quirky
 * in that it has two pairs or AVDD_PEX and VDD_PEX supplies (PEXA and PEXB)
 * and either seems to be optional depending on which ports are being used.
 */
static int tegra_pcie_get_regulators(struct tegra_pcie *pcie, u32 lane_mask)
{
	struct device *dev = pcie->dev;
	struct device_node *np = dev->of_node;
	unsigned int i = 0;

	if (of_device_is_compatible(np, "nvidia,tegra186-pcie")) {
		pcie->num_supplies = 4;

		pcie->supplies = devm_kcalloc(pcie->dev, pcie->num_supplies,
					      sizeof(*pcie->supplies),
					      GFP_KERNEL);
		if (!pcie->supplies)
			return -ENOMEM;

		pcie->supplies[i++].supply = "dvdd-pex";
		pcie->supplies[i++].supply = "hvdd-pex-pll";
		pcie->supplies[i++].supply = "hvdd-pex";
		pcie->supplies[i++].supply = "vddio-pexctl-aud";
	} else if (of_device_is_compatible(np, "nvidia,tegra210-pcie")) {
		pcie->num_supplies = 3;

		pcie->supplies = devm_kcalloc(pcie->dev, pcie->num_supplies,
					      sizeof(*pcie->supplies),
					      GFP_KERNEL);
		if (!pcie->supplies)
			return -ENOMEM;

		pcie->supplies[i++].supply = "hvddio-pex";
		pcie->supplies[i++].supply = "dvddio-pex";
		pcie->supplies[i++].supply = "vddio-pex-ctl";
	} else if (of_device_is_compatible(np, "nvidia,tegra124-pcie")) {
		pcie->num_supplies = 4;

		pcie->supplies = devm_kcalloc(dev, pcie->num_supplies,
					      sizeof(*pcie->supplies),
					      GFP_KERNEL);
		if (!pcie->supplies)
			return -ENOMEM;

		pcie->supplies[i++].supply = "avddio-pex";
		pcie->supplies[i++].supply = "dvddio-pex";
		pcie->supplies[i++].supply = "hvdd-pex";
		pcie->supplies[i++].supply = "vddio-pex-ctl";
	} else if (of_device_is_compatible(np, "nvidia,tegra30-pcie")) {
		bool need_pexa = false, need_pexb = false;

		/* VDD_PEXA and AVDD_PEXA supply lanes 0 to 3 */
		if (lane_mask & 0x0f)
			need_pexa = true;

		/* VDD_PEXB and AVDD_PEXB supply lanes 4 to 5 */
		if (lane_mask & 0x30)
			need_pexb = true;

		pcie->num_supplies = 4 + (need_pexa ? 2 : 0) +
					 (need_pexb ? 2 : 0);

		pcie->supplies = devm_kcalloc(dev, pcie->num_supplies,
					      sizeof(*pcie->supplies),
					      GFP_KERNEL);
		if (!pcie->supplies)
			return -ENOMEM;

		pcie->supplies[i++].supply = "avdd-pex-pll";
		pcie->supplies[i++].supply = "hvdd-pex";
		pcie->supplies[i++].supply = "vddio-pex-ctl";
		pcie->supplies[i++].supply = "avdd-plle";

		if (need_pexa) {
			pcie->supplies[i++].supply = "avdd-pexa";
			pcie->supplies[i++].supply = "vdd-pexa";
		}

		if (need_pexb) {
			pcie->supplies[i++].supply = "avdd-pexb";
			pcie->supplies[i++].supply = "vdd-pexb";
		}
	} else if (of_device_is_compatible(np, "nvidia,tegra20-pcie")) {
		pcie->num_supplies = 5;

		pcie->supplies = devm_kcalloc(dev, pcie->num_supplies,
					      sizeof(*pcie->supplies),
					      GFP_KERNEL);
		if (!pcie->supplies)
			return -ENOMEM;

		pcie->supplies[0].supply = "avdd-pex";
		pcie->supplies[1].supply = "vdd-pex";
		pcie->supplies[2].supply = "avdd-pex-pll";
		pcie->supplies[3].supply = "avdd-plle";
		pcie->supplies[4].supply = "vddio-pex-clk";
	}

	if (of_regulator_bulk_available(dev->of_node, pcie->supplies,
					pcie->num_supplies))
		return devm_regulator_bulk_get(dev, pcie->num_supplies,
					       pcie->supplies);

	/*
	 * If not all regulators are available for this new scheme, assume
	 * that the device tree complies with an older version of the device
	 * tree binding.
	 */
	dev_info(dev, "using legacy DT binding for power supplies\n");

	devm_kfree(dev, pcie->supplies);
	pcie->num_supplies = 0;

	return tegra_pcie_get_legacy_regulators(pcie);
}

static int tegra_pcie_parse_dt(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *np = dev->of_node, *port;
	const struct tegra_pcie_soc *soc = pcie->soc;
	u32 lanes = 0, mask = 0;
	unsigned int lane = 0;
	int err;

	/* parse root ports */
	for_each_child_of_node(np, port) {
		struct tegra_pcie_port *rp;
		unsigned int index;
		u32 value;
		char *label;

		err = of_pci_get_devfn(port);
		if (err < 0) {
			dev_err(dev, "failed to parse address: %d\n", err);
			goto err_node_put;
		}

		index = PCI_SLOT(err);

		if (index < 1 || index > soc->num_ports) {
			dev_err(dev, "invalid port number: %d\n", index);
			err = -EINVAL;
			goto err_node_put;
		}

		index--;

		err = of_property_read_u32(port, "nvidia,num-lanes", &value);
		if (err < 0) {
			dev_err(dev, "failed to parse # of lanes: %d\n",
				err);
			goto err_node_put;
		}

		if (value > 16) {
			dev_err(dev, "invalid # of lanes: %u\n", value);
			err = -EINVAL;
			goto err_node_put;
		}

		lanes |= value << (index << 3);

		if (!of_device_is_available(port)) {
			lane += value;
			continue;
		}

		mask |= ((1 << value) - 1) << lane;
		lane += value;

		rp = devm_kzalloc(dev, sizeof(*rp), GFP_KERNEL);
		if (!rp) {
			err = -ENOMEM;
			goto err_node_put;
		}

		err = of_address_to_resource(port, 0, &rp->regs);
		if (err < 0) {
			dev_err(dev, "failed to parse address: %d\n", err);
			goto err_node_put;
		}

		INIT_LIST_HEAD(&rp->list);
		rp->index = index;
		rp->lanes = value;
		rp->pcie = pcie;
		rp->np = port;

		rp->base = devm_pci_remap_cfg_resource(dev, &rp->regs);
		if (IS_ERR(rp->base)) {
			err = PTR_ERR(rp->base);
			goto err_node_put;
		}

		label = devm_kasprintf(dev, GFP_KERNEL, "pex-reset-%u", index);
		if (!label) {
			err = -ENOMEM;
			goto err_node_put;
		}

		/*
		 * Returns -ENOENT if reset-gpios property is not populated
		 * and in this case fall back to using AFI per port register
		 * to toggle PERST# SFIO line.
		 */
		rp->reset_gpio = devm_fwnode_gpiod_get(dev,
						       of_fwnode_handle(port),
						       "reset",
						       GPIOD_OUT_LOW,
						       label);
		if (IS_ERR(rp->reset_gpio)) {
			if (PTR_ERR(rp->reset_gpio) == -ENOENT) {
				rp->reset_gpio = NULL;
			} else {
				dev_err(dev, "failed to get reset GPIO: %ld\n",
					PTR_ERR(rp->reset_gpio));
				err = PTR_ERR(rp->reset_gpio);
				goto err_node_put;
			}
		}

		list_add_tail(&rp->list, &pcie->ports);
	}

	err = tegra_pcie_get_xbar_config(pcie, lanes, &pcie->xbar_config);
	if (err < 0) {
		dev_err(dev, "invalid lane configuration\n");
		return err;
	}

	err = tegra_pcie_get_regulators(pcie, mask);
	if (err < 0)
		return err;

	return 0;

err_node_put:
	of_node_put(port);
	return err;
}

/*
 * FIXME: If there are no PCIe cards attached, then calling this function
 * can result in the increase of the bootup time as there are big timeout
 * loops.
 */
#define TEGRA_PCIE_LINKUP_TIMEOUT	200	/* up to 1.2 seconds */
static bool tegra_pcie_port_check_link(struct tegra_pcie_port *port)
{
	struct device *dev = port->pcie->dev;
	unsigned int retries = 3;
	unsigned long value;

	/* override presence detection */
	value = readl(port->base + RP_PRIV_MISC);
	value &= ~RP_PRIV_MISC_PRSNT_MAP_EP_ABSNT;
	value |= RP_PRIV_MISC_PRSNT_MAP_EP_PRSNT;
	writel(value, port->base + RP_PRIV_MISC);

	do {
		unsigned int timeout = TEGRA_PCIE_LINKUP_TIMEOUT;

		do {
			value = readl(port->base + RP_VEND_XP);

			if (value & RP_VEND_XP_DL_UP)
				break;

			usleep_range(1000, 2000);
		} while (--timeout);

		if (!timeout) {
			dev_dbg(dev, "link %u down, retrying\n", port->index);
			goto retry;
		}

		timeout = TEGRA_PCIE_LINKUP_TIMEOUT;

		do {
			value = readl(port->base + RP_LINK_CONTROL_STATUS);

			if (value & RP_LINK_CONTROL_STATUS_DL_LINK_ACTIVE)
				return true;

			usleep_range(1000, 2000);
		} while (--timeout);

retry:
		tegra_pcie_port_reset(port);
	} while (--retries);

	return false;
}

static void tegra_pcie_change_link_speed(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct tegra_pcie_port *port;
	ktime_t deadline;
	u32 value;

	list_for_each_entry(port, &pcie->ports, list) {
		/*
		 * "Supported Link Speeds Vector" in "Link Capabilities 2"
		 * is not supported by Tegra. tegra_pcie_change_link_speed()
		 * is called only for Tegra chips which support Gen2.
		 * So there no harm if supported link speed is not verified.
		 */
		value = readl(port->base + RP_LINK_CONTROL_STATUS_2);
		value &= ~PCI_EXP_LNKSTA_CLS;
		value |= PCI_EXP_LNKSTA_CLS_5_0GB;
		writel(value, port->base + RP_LINK_CONTROL_STATUS_2);

		/*
		 * Poll until link comes back from recovery to avoid race
		 * condition.
		 */
		deadline = ktime_add_us(ktime_get(), LINK_RETRAIN_TIMEOUT);

		while (ktime_before(ktime_get(), deadline)) {
			value = readl(port->base + RP_LINK_CONTROL_STATUS);
			if ((value & PCI_EXP_LNKSTA_LT) == 0)
				break;

			usleep_range(2000, 3000);
		}

		if (value & PCI_EXP_LNKSTA_LT)
			dev_warn(dev, "PCIe port %u link is in recovery\n",
				 port->index);

		/* Retrain the link */
		value = readl(port->base + RP_LINK_CONTROL_STATUS);
		value |= PCI_EXP_LNKCTL_RL;
		writel(value, port->base + RP_LINK_CONTROL_STATUS);

		deadline = ktime_add_us(ktime_get(), LINK_RETRAIN_TIMEOUT);

		while (ktime_before(ktime_get(), deadline)) {
			value = readl(port->base + RP_LINK_CONTROL_STATUS);
			if ((value & PCI_EXP_LNKSTA_LT) == 0)
				break;

			usleep_range(2000, 3000);
		}

		if (value & PCI_EXP_LNKSTA_LT)
			dev_err(dev, "failed to retrain link of port %u\n",
				port->index);
	}
}

static void tegra_pcie_enable_ports(struct tegra_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct tegra_pcie_port *port, *tmp;

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		dev_info(dev, "probing port %u, using %u lanes\n",
			 port->index, port->lanes);

		tegra_pcie_port_enable(port);
	}

	/* Start LTSSM from Tegra side */
	reset_control_deassert(pcie->pcie_xrst);

	list_for_each_entry_safe(port, tmp, &pcie->ports, list) {
		if (tegra_pcie_port_check_link(port))
			continue;

		dev_info(dev, "link %u down, ignoring\n", port->index);

		tegra_pcie_port_disable(port);
		tegra_pcie_port_free(port);
	}

	if (pcie->soc->has_gen2)
		tegra_pcie_change_link_speed(pcie);
}

static void tegra_pcie_disable_ports(struct tegra_pcie *pcie)
{
	struct tegra_pcie_port *port, *tmp;

	reset_control_assert(pcie->pcie_xrst);

	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		tegra_pcie_port_disable(port);
}

static const struct tegra_pcie_port_soc tegra20_pcie_ports[] = {
	{ .pme.turnoff_bit = 0, .pme.ack_bit =  5 },
	{ .pme.turnoff_bit = 8, .pme.ack_bit = 10 },
};

static const struct tegra_pcie_soc tegra20_pcie = {
	.num_ports = 2,
	.ports = tegra20_pcie_ports,
	.msi_base_shift = 0,
	.pads_pll_ctl = PADS_PLL_CTL_TEGRA20,
	.tx_ref_sel = PADS_PLL_CTL_TXCLKREF_DIV10,
	.pads_refclk_cfg0 = 0xfa5cfa5c,
	.has_pex_clkreq_en = false,
	.has_pex_bias_ctrl = false,
	.has_intr_prsnt_sense = false,
	.has_cml_clk = false,
	.has_gen2 = false,
	.force_pca_enable = false,
	.program_uphy = true,
	.update_clamp_threshold = false,
	.program_deskew_time = false,
	.update_fc_timer = false,
	.has_cache_bars = true,
	.ectl.enable = false,
};

static const struct tegra_pcie_port_soc tegra30_pcie_ports[] = {
	{ .pme.turnoff_bit =  0, .pme.ack_bit =  5 },
	{ .pme.turnoff_bit =  8, .pme.ack_bit = 10 },
	{ .pme.turnoff_bit = 16, .pme.ack_bit = 18 },
};

static const struct tegra_pcie_soc tegra30_pcie = {
	.num_ports = 3,
	.ports = tegra30_pcie_ports,
	.msi_base_shift = 8,
	.afi_pex2_ctrl = 0x128,
	.pads_pll_ctl = PADS_PLL_CTL_TEGRA30,
	.tx_ref_sel = PADS_PLL_CTL_TXCLKREF_BUF_EN,
	.pads_refclk_cfg0 = 0xfa5cfa5c,
	.pads_refclk_cfg1 = 0xfa5cfa5c,
	.has_pex_clkreq_en = true,
	.has_pex_bias_ctrl = true,
	.has_intr_prsnt_sense = true,
	.has_cml_clk = true,
	.has_gen2 = false,
	.force_pca_enable = false,
	.program_uphy = true,
	.update_clamp_threshold = false,
	.program_deskew_time = false,
	.update_fc_timer = false,
	.has_cache_bars = false,
	.ectl.enable = false,
};

static const struct tegra_pcie_soc tegra124_pcie = {
	.num_ports = 2,
	.ports = tegra20_pcie_ports,
	.msi_base_shift = 8,
	.pads_pll_ctl = PADS_PLL_CTL_TEGRA30,
	.tx_ref_sel = PADS_PLL_CTL_TXCLKREF_BUF_EN,
	.pads_refclk_cfg0 = 0x44ac44ac,
	.has_pex_clkreq_en = true,
	.has_pex_bias_ctrl = true,
	.has_intr_prsnt_sense = true,
	.has_cml_clk = true,
	.has_gen2 = true,
	.force_pca_enable = false,
	.program_uphy = true,
	.update_clamp_threshold = true,
	.program_deskew_time = false,
	.update_fc_timer = false,
	.has_cache_bars = false,
	.ectl.enable = false,
};

static const struct tegra_pcie_soc tegra210_pcie = {
	.num_ports = 2,
	.ports = tegra20_pcie_ports,
	.msi_base_shift = 8,
	.pads_pll_ctl = PADS_PLL_CTL_TEGRA30,
	.tx_ref_sel = PADS_PLL_CTL_TXCLKREF_BUF_EN,
	.pads_refclk_cfg0 = 0x90b890b8,
	/* FC threshold is bit[25:18] */
	.update_fc_threshold = 0x01800000,
	.has_pex_clkreq_en = true,
	.has_pex_bias_ctrl = true,
	.has_intr_prsnt_sense = true,
	.has_cml_clk = true,
	.has_gen2 = true,
	.force_pca_enable = true,
	.program_uphy = true,
	.update_clamp_threshold = true,
	.program_deskew_time = true,
	.update_fc_timer = true,
	.has_cache_bars = false,
	.ectl = {
		.regs = {
			.rp_ectl_2_r1 = 0x0000000f,
			.rp_ectl_4_r1 = 0x00000067,
			.rp_ectl_5_r1 = 0x55010000,
			.rp_ectl_6_r1 = 0x00000001,
			.rp_ectl_2_r2 = 0x0000008f,
			.rp_ectl_4_r2 = 0x000000c7,
			.rp_ectl_5_r2 = 0x55010000,
			.rp_ectl_6_r2 = 0x00000001,
		},
		.enable = true,
	},
};

static const struct tegra_pcie_port_soc tegra186_pcie_ports[] = {
	{ .pme.turnoff_bit =  0, .pme.ack_bit =  5 },
	{ .pme.turnoff_bit =  8, .pme.ack_bit = 10 },
	{ .pme.turnoff_bit = 12, .pme.ack_bit = 14 },
};

static const struct tegra_pcie_soc tegra186_pcie = {
	.num_ports = 3,
	.ports = tegra186_pcie_ports,
	.msi_base_shift = 8,
	.afi_pex2_ctrl = 0x19c,
	.pads_pll_ctl = PADS_PLL_CTL_TEGRA30,
	.tx_ref_sel = PADS_PLL_CTL_TXCLKREF_BUF_EN,
	.pads_refclk_cfg0 = 0x80b880b8,
	.pads_refclk_cfg1 = 0x000480b8,
	.has_pex_clkreq_en = true,
	.has_pex_bias_ctrl = true,
	.has_intr_prsnt_sense = true,
	.has_cml_clk = false,
	.has_gen2 = true,
	.force_pca_enable = false,
	.program_uphy = false,
	.update_clamp_threshold = false,
	.program_deskew_time = false,
	.update_fc_timer = false,
	.has_cache_bars = false,
	.ectl.enable = false,
};

static const struct of_device_id tegra_pcie_of_match[] = {
	{ .compatible = "nvidia,tegra186-pcie", .data = &tegra186_pcie },
	{ .compatible = "nvidia,tegra210-pcie", .data = &tegra210_pcie },
	{ .compatible = "nvidia,tegra124-pcie", .data = &tegra124_pcie },
	{ .compatible = "nvidia,tegra30-pcie", .data = &tegra30_pcie },
	{ .compatible = "nvidia,tegra20-pcie", .data = &tegra20_pcie },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_pcie_of_match);

static void *tegra_pcie_ports_seq_start(struct seq_file *s, loff_t *pos)
{
	struct tegra_pcie *pcie = s->private;

	if (list_empty(&pcie->ports))
		return NULL;

	seq_puts(s, "Index  Status\n");

	return seq_list_start(&pcie->ports, *pos);
}

static void *tegra_pcie_ports_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct tegra_pcie *pcie = s->private;

	return seq_list_next(v, &pcie->ports, pos);
}

static void tegra_pcie_ports_seq_stop(struct seq_file *s, void *v)
{
}

static int tegra_pcie_ports_seq_show(struct seq_file *s, void *v)
{
	bool up = false, active = false;
	struct tegra_pcie_port *port;
	unsigned int value;

	port = list_entry(v, struct tegra_pcie_port, list);

	value = readl(port->base + RP_VEND_XP);

	if (value & RP_VEND_XP_DL_UP)
		up = true;

	value = readl(port->base + RP_LINK_CONTROL_STATUS);

	if (value & RP_LINK_CONTROL_STATUS_DL_LINK_ACTIVE)
		active = true;

	seq_printf(s, "%2u     ", port->index);

	if (up)
		seq_puts(s, "up");

	if (active) {
		if (up)
			seq_puts(s, ", ");

		seq_puts(s, "active");
	}

	seq_puts(s, "\n");
	return 0;
}

static const struct seq_operations tegra_pcie_ports_sops = {
	.start = tegra_pcie_ports_seq_start,
	.next = tegra_pcie_ports_seq_next,
	.stop = tegra_pcie_ports_seq_stop,
	.show = tegra_pcie_ports_seq_show,
};

DEFINE_SEQ_ATTRIBUTE(tegra_pcie_ports);

static void tegra_pcie_debugfs_exit(struct tegra_pcie *pcie)
{
	debugfs_remove_recursive(pcie->debugfs);
	pcie->debugfs = NULL;
}

static void tegra_pcie_debugfs_init(struct tegra_pcie *pcie)
{
	pcie->debugfs = debugfs_create_dir("pcie", NULL);

	debugfs_create_file("ports", S_IFREG | S_IRUGO, pcie->debugfs, pcie,
			    &tegra_pcie_ports_fops);
}

static int tegra_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *host;
	struct tegra_pcie *pcie;
	int err;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!host)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(host);
	host->sysdata = pcie;
	platform_set_drvdata(pdev, pcie);

	pcie->soc = of_device_get_match_data(dev);
	INIT_LIST_HEAD(&pcie->ports);
	pcie->dev = dev;

	err = tegra_pcie_parse_dt(pcie);
	if (err < 0)
		return err;

	err = tegra_pcie_get_resources(pcie);
	if (err < 0) {
		dev_err(dev, "failed to request resources: %d\n", err);
		return err;
	}

	err = tegra_pcie_msi_setup(pcie);
	if (err < 0) {
		dev_err(dev, "failed to enable MSI support: %d\n", err);
		goto put_resources;
	}

	pm_runtime_enable(pcie->dev);
	err = pm_runtime_get_sync(pcie->dev);
	if (err < 0) {
		dev_err(dev, "fail to enable pcie controller: %d\n", err);
		goto pm_runtime_put;
	}

	host->ops = &tegra_pcie_ops;
	host->map_irq = tegra_pcie_map_irq;

	err = pci_host_probe(host);
	if (err < 0) {
		dev_err(dev, "failed to register host: %d\n", err);
		goto pm_runtime_put;
	}

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		tegra_pcie_debugfs_init(pcie);

	return 0;

pm_runtime_put:
	pm_runtime_put_sync(pcie->dev);
	pm_runtime_disable(pcie->dev);
	tegra_pcie_msi_teardown(pcie);
put_resources:
	tegra_pcie_put_resources(pcie);
	return err;
}

static int tegra_pcie_remove(struct platform_device *pdev)
{
	struct tegra_pcie *pcie = platform_get_drvdata(pdev);
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct tegra_pcie_port *port, *tmp;

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		tegra_pcie_debugfs_exit(pcie);

	pci_stop_root_bus(host->bus);
	pci_remove_root_bus(host->bus);
	pm_runtime_put_sync(pcie->dev);
	pm_runtime_disable(pcie->dev);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		tegra_pcie_msi_teardown(pcie);

	tegra_pcie_put_resources(pcie);

	list_for_each_entry_safe(port, tmp, &pcie->ports, list)
		tegra_pcie_port_free(port);

	return 0;
}

static int tegra_pcie_pm_suspend(struct device *dev)
{
	struct tegra_pcie *pcie = dev_get_drvdata(dev);
	struct tegra_pcie_port *port;
	int err;

	list_for_each_entry(port, &pcie->ports, list)
		tegra_pcie_pme_turnoff(port);

	tegra_pcie_disable_ports(pcie);

	/*
	 * AFI_INTR is unmasked in tegra_pcie_enable_controller(), mask it to
	 * avoid unwanted interrupts raised by AFI after pex_rst is asserted.
	 */
	tegra_pcie_disable_interrupts(pcie);

	if (pcie->soc->program_uphy) {
		err = tegra_pcie_phy_power_off(pcie);
		if (err < 0)
			dev_err(dev, "failed to power off PHY(s): %d\n", err);
	}

	reset_control_assert(pcie->pex_rst);
	clk_disable_unprepare(pcie->pex_clk);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		tegra_pcie_disable_msi(pcie);

	pinctrl_pm_select_idle_state(dev);
	tegra_pcie_power_off(pcie);

	return 0;
}

static int tegra_pcie_pm_resume(struct device *dev)
{
	struct tegra_pcie *pcie = dev_get_drvdata(dev);
	int err;

	err = tegra_pcie_power_on(pcie);
	if (err) {
		dev_err(dev, "tegra pcie power on fail: %d\n", err);
		return err;
	}

	err = pinctrl_pm_select_default_state(dev);
	if (err < 0) {
		dev_err(dev, "failed to disable PCIe IO DPD: %d\n", err);
		goto poweroff;
	}

	tegra_pcie_enable_controller(pcie);
	tegra_pcie_setup_translations(pcie);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		tegra_pcie_enable_msi(pcie);

	err = clk_prepare_enable(pcie->pex_clk);
	if (err) {
		dev_err(dev, "failed to enable PEX clock: %d\n", err);
		goto pex_dpd_enable;
	}

	reset_control_deassert(pcie->pex_rst);

	if (pcie->soc->program_uphy) {
		err = tegra_pcie_phy_power_on(pcie);
		if (err < 0) {
			dev_err(dev, "failed to power on PHY(s): %d\n", err);
			goto disable_pex_clk;
		}
	}

	tegra_pcie_apply_pad_settings(pcie);
	tegra_pcie_enable_ports(pcie);

	return 0;

disable_pex_clk:
	reset_control_assert(pcie->pex_rst);
	clk_disable_unprepare(pcie->pex_clk);
pex_dpd_enable:
	pinctrl_pm_select_idle_state(dev);
poweroff:
	tegra_pcie_power_off(pcie);

	return err;
}

static const struct dev_pm_ops tegra_pcie_pm_ops = {
	RUNTIME_PM_OPS(tegra_pcie_pm_suspend, tegra_pcie_pm_resume, NULL)
	NOIRQ_SYSTEM_SLEEP_PM_OPS(tegra_pcie_pm_suspend, tegra_pcie_pm_resume)
};

static struct platform_driver tegra_pcie_driver = {
	.driver = {
		.name = "tegra-pcie",
		.of_match_table = tegra_pcie_of_match,
		.suppress_bind_attrs = true,
		.pm = &tegra_pcie_pm_ops,
	},
	.probe = tegra_pcie_probe,
	.remove = tegra_pcie_remove,
};
module_platform_driver(tegra_pcie_driver);
MODULE_LICENSE("GPL");
