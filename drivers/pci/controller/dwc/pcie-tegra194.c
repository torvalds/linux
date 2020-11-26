// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe host controller driver for Tegra194 SoC
 *
 * Copyright (C) 2019 NVIDIA Corporation.
 *
 * Author: Vidya Sagar <vidyas@nvidia.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>
#include <linux/reset.h>
#include <linux/resource.h>
#include <linux/types.h>
#include "pcie-designware.h"
#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>
#include "../../pci.h"

#define APPL_PINMUX				0x0
#define APPL_PINMUX_PEX_RST			BIT(0)
#define APPL_PINMUX_CLKREQ_OVERRIDE_EN		BIT(2)
#define APPL_PINMUX_CLKREQ_OVERRIDE		BIT(3)
#define APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN	BIT(4)
#define APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE	BIT(5)

#define APPL_CTRL				0x4
#define APPL_CTRL_SYS_PRE_DET_STATE		BIT(6)
#define APPL_CTRL_LTSSM_EN			BIT(7)
#define APPL_CTRL_HW_HOT_RST_EN			BIT(20)
#define APPL_CTRL_HW_HOT_RST_MODE_MASK		GENMASK(1, 0)
#define APPL_CTRL_HW_HOT_RST_MODE_SHIFT		22
#define APPL_CTRL_HW_HOT_RST_MODE_IMDT_RST	0x1

#define APPL_INTR_EN_L0_0			0x8
#define APPL_INTR_EN_L0_0_LINK_STATE_INT_EN	BIT(0)
#define APPL_INTR_EN_L0_0_MSI_RCV_INT_EN	BIT(4)
#define APPL_INTR_EN_L0_0_INT_INT_EN		BIT(8)
#define APPL_INTR_EN_L0_0_PCI_CMD_EN_INT_EN	BIT(15)
#define APPL_INTR_EN_L0_0_CDM_REG_CHK_INT_EN	BIT(19)
#define APPL_INTR_EN_L0_0_SYS_INTR_EN		BIT(30)
#define APPL_INTR_EN_L0_0_SYS_MSI_INTR_EN	BIT(31)

#define APPL_INTR_STATUS_L0			0xC
#define APPL_INTR_STATUS_L0_LINK_STATE_INT	BIT(0)
#define APPL_INTR_STATUS_L0_INT_INT		BIT(8)
#define APPL_INTR_STATUS_L0_PCI_CMD_EN_INT	BIT(15)
#define APPL_INTR_STATUS_L0_PEX_RST_INT		BIT(16)
#define APPL_INTR_STATUS_L0_CDM_REG_CHK_INT	BIT(18)

#define APPL_INTR_EN_L1_0_0				0x1C
#define APPL_INTR_EN_L1_0_0_LINK_REQ_RST_NOT_INT_EN	BIT(1)
#define APPL_INTR_EN_L1_0_0_RDLH_LINK_UP_INT_EN		BIT(3)
#define APPL_INTR_EN_L1_0_0_HOT_RESET_DONE_INT_EN	BIT(30)

#define APPL_INTR_STATUS_L1_0_0				0x20
#define APPL_INTR_STATUS_L1_0_0_LINK_REQ_RST_NOT_CHGED	BIT(1)
#define APPL_INTR_STATUS_L1_0_0_RDLH_LINK_UP_CHGED	BIT(3)
#define APPL_INTR_STATUS_L1_0_0_HOT_RESET_DONE		BIT(30)

#define APPL_INTR_STATUS_L1_1			0x2C
#define APPL_INTR_STATUS_L1_2			0x30
#define APPL_INTR_STATUS_L1_3			0x34
#define APPL_INTR_STATUS_L1_6			0x3C
#define APPL_INTR_STATUS_L1_7			0x40
#define APPL_INTR_STATUS_L1_15_CFG_BME_CHGED	BIT(1)

#define APPL_INTR_EN_L1_8_0			0x44
#define APPL_INTR_EN_L1_8_BW_MGT_INT_EN		BIT(2)
#define APPL_INTR_EN_L1_8_AUTO_BW_INT_EN	BIT(3)
#define APPL_INTR_EN_L1_8_INTX_EN		BIT(11)
#define APPL_INTR_EN_L1_8_AER_INT_EN		BIT(15)

#define APPL_INTR_STATUS_L1_8_0			0x4C
#define APPL_INTR_STATUS_L1_8_0_EDMA_INT_MASK	GENMASK(11, 6)
#define APPL_INTR_STATUS_L1_8_0_BW_MGT_INT_STS	BIT(2)
#define APPL_INTR_STATUS_L1_8_0_AUTO_BW_INT_STS	BIT(3)

#define APPL_INTR_STATUS_L1_9			0x54
#define APPL_INTR_STATUS_L1_10			0x58
#define APPL_INTR_STATUS_L1_11			0x64
#define APPL_INTR_STATUS_L1_13			0x74
#define APPL_INTR_STATUS_L1_14			0x78
#define APPL_INTR_STATUS_L1_15			0x7C
#define APPL_INTR_STATUS_L1_17			0x88

#define APPL_INTR_EN_L1_18				0x90
#define APPL_INTR_EN_L1_18_CDM_REG_CHK_CMPLT		BIT(2)
#define APPL_INTR_EN_L1_18_CDM_REG_CHK_CMP_ERR		BIT(1)
#define APPL_INTR_EN_L1_18_CDM_REG_CHK_LOGIC_ERR	BIT(0)

#define APPL_INTR_STATUS_L1_18				0x94
#define APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMPLT	BIT(2)
#define APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMP_ERR	BIT(1)
#define APPL_INTR_STATUS_L1_18_CDM_REG_CHK_LOGIC_ERR	BIT(0)

#define APPL_MSI_CTRL_1				0xAC

#define APPL_MSI_CTRL_2				0xB0

#define APPL_LEGACY_INTX			0xB8

#define APPL_LTR_MSG_1				0xC4
#define LTR_MSG_REQ				BIT(15)
#define LTR_MST_NO_SNOOP_SHIFT			16

#define APPL_LTR_MSG_2				0xC8
#define APPL_LTR_MSG_2_LTR_MSG_REQ_STATE	BIT(3)

#define APPL_LINK_STATUS			0xCC
#define APPL_LINK_STATUS_RDLH_LINK_UP		BIT(0)

#define APPL_DEBUG				0xD0
#define APPL_DEBUG_PM_LINKST_IN_L2_LAT		BIT(21)
#define APPL_DEBUG_PM_LINKST_IN_L0		0x11
#define APPL_DEBUG_LTSSM_STATE_MASK		GENMASK(8, 3)
#define APPL_DEBUG_LTSSM_STATE_SHIFT		3
#define LTSSM_STATE_PRE_DETECT			5

#define APPL_RADM_STATUS			0xE4
#define APPL_PM_XMT_TURNOFF_STATE		BIT(0)

#define APPL_DM_TYPE				0x100
#define APPL_DM_TYPE_MASK			GENMASK(3, 0)
#define APPL_DM_TYPE_RP				0x4
#define APPL_DM_TYPE_EP				0x0

#define APPL_CFG_BASE_ADDR			0x104
#define APPL_CFG_BASE_ADDR_MASK			GENMASK(31, 12)

#define APPL_CFG_IATU_DMA_BASE_ADDR		0x108
#define APPL_CFG_IATU_DMA_BASE_ADDR_MASK	GENMASK(31, 18)

#define APPL_CFG_MISC				0x110
#define APPL_CFG_MISC_SLV_EP_MODE		BIT(14)
#define APPL_CFG_MISC_ARCACHE_MASK		GENMASK(13, 10)
#define APPL_CFG_MISC_ARCACHE_SHIFT		10
#define APPL_CFG_MISC_ARCACHE_VAL		3

#define APPL_CFG_SLCG_OVERRIDE			0x114
#define APPL_CFG_SLCG_OVERRIDE_SLCG_EN_MASTER	BIT(0)

#define APPL_CAR_RESET_OVRD				0x12C
#define APPL_CAR_RESET_OVRD_CYA_OVERRIDE_CORE_RST_N	BIT(0)

#define IO_BASE_IO_DECODE				BIT(0)
#define IO_BASE_IO_DECODE_BIT8				BIT(8)

#define CFG_PREF_MEM_LIMIT_BASE_MEM_DECODE		BIT(0)
#define CFG_PREF_MEM_LIMIT_BASE_MEM_LIMIT_DECODE	BIT(16)

#define CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF	0x718
#define CFG_TIMER_CTRL_ACK_NAK_SHIFT	(19)

#define EVENT_COUNTER_ALL_CLEAR		0x3
#define EVENT_COUNTER_ENABLE_ALL	0x7
#define EVENT_COUNTER_ENABLE_SHIFT	2
#define EVENT_COUNTER_EVENT_SEL_MASK	GENMASK(7, 0)
#define EVENT_COUNTER_EVENT_SEL_SHIFT	16
#define EVENT_COUNTER_EVENT_Tx_L0S	0x2
#define EVENT_COUNTER_EVENT_Rx_L0S	0x3
#define EVENT_COUNTER_EVENT_L1		0x5
#define EVENT_COUNTER_EVENT_L1_1	0x7
#define EVENT_COUNTER_EVENT_L1_2	0x8
#define EVENT_COUNTER_GROUP_SEL_SHIFT	24
#define EVENT_COUNTER_GROUP_5		0x5

#define N_FTS_VAL					52
#define FTS_VAL						52

#define PORT_LOGIC_MSI_CTRL_INT_0_EN		0x828

#define GEN3_EQ_CONTROL_OFF			0x8a8
#define GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_SHIFT	8
#define GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_MASK	GENMASK(23, 8)
#define GEN3_EQ_CONTROL_OFF_FB_MODE_MASK	GENMASK(3, 0)

#define GEN3_RELATED_OFF			0x890
#define GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL	BIT(0)
#define GEN3_RELATED_OFF_GEN3_EQ_DISABLE	BIT(16)
#define GEN3_RELATED_OFF_RATE_SHADOW_SEL_SHIFT	24
#define GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK	GENMASK(25, 24)

#define PORT_LOGIC_AMBA_ERROR_RESPONSE_DEFAULT	0x8D0
#define AMBA_ERROR_RESPONSE_CRS_SHIFT		3
#define AMBA_ERROR_RESPONSE_CRS_MASK		GENMASK(1, 0)
#define AMBA_ERROR_RESPONSE_CRS_OKAY		0
#define AMBA_ERROR_RESPONSE_CRS_OKAY_FFFFFFFF	1
#define AMBA_ERROR_RESPONSE_CRS_OKAY_FFFF0001	2

#define MSIX_ADDR_MATCH_LOW_OFF			0x940
#define MSIX_ADDR_MATCH_LOW_OFF_EN		BIT(0)
#define MSIX_ADDR_MATCH_LOW_OFF_MASK		GENMASK(31, 2)

#define MSIX_ADDR_MATCH_HIGH_OFF		0x944
#define MSIX_ADDR_MATCH_HIGH_OFF_MASK		GENMASK(31, 0)

#define PORT_LOGIC_MSIX_DOORBELL			0x948

#define CAP_SPCIE_CAP_OFF			0x154
#define CAP_SPCIE_CAP_OFF_DSP_TX_PRESET0_MASK	GENMASK(3, 0)
#define CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_MASK	GENMASK(11, 8)
#define CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_SHIFT	8

#define PME_ACK_TIMEOUT 10000

#define LTSSM_TIMEOUT 50000	/* 50ms */

#define GEN3_GEN4_EQ_PRESET_INIT	5

#define GEN1_CORE_CLK_FREQ	62500000
#define GEN2_CORE_CLK_FREQ	125000000
#define GEN3_CORE_CLK_FREQ	250000000
#define GEN4_CORE_CLK_FREQ	500000000

#define LTR_MSG_TIMEOUT		(100 * 1000)

#define PERST_DEBOUNCE_TIME	(5 * 1000)

#define EP_STATE_DISABLED	0
#define EP_STATE_ENABLED	1

static const unsigned int pcie_gen_freq[] = {
	GEN1_CORE_CLK_FREQ,
	GEN2_CORE_CLK_FREQ,
	GEN3_CORE_CLK_FREQ,
	GEN4_CORE_CLK_FREQ
};

static const u32 event_cntr_ctrl_offset[] = {
	0x1d8,
	0x1a8,
	0x1a8,
	0x1a8,
	0x1c4,
	0x1d8
};

static const u32 event_cntr_data_offset[] = {
	0x1dc,
	0x1ac,
	0x1ac,
	0x1ac,
	0x1c8,
	0x1dc
};

struct tegra_pcie_dw {
	struct device *dev;
	struct resource *appl_res;
	struct resource *dbi_res;
	struct resource *atu_dma_res;
	void __iomem *appl_base;
	struct clk *core_clk;
	struct reset_control *core_apb_rst;
	struct reset_control *core_rst;
	struct dw_pcie pci;
	struct tegra_bpmp *bpmp;

	enum dw_pcie_device_mode mode;

	bool supports_clkreq;
	bool enable_cdm_check;
	bool link_state;
	bool update_fc_fixup;
	u8 init_link_width;
	u32 msi_ctrl_int;
	u32 num_lanes;
	u32 cid;
	u32 cfg_link_cap_l1sub;
	u32 pcie_cap_base;
	u32 aspm_cmrt;
	u32 aspm_pwr_on_t;
	u32 aspm_l0s_enter_lat;

	struct regulator *pex_ctl_supply;
	struct regulator *slot_ctl_3v3;
	struct regulator *slot_ctl_12v;

	unsigned int phy_count;
	struct phy **phys;

	struct dentry *debugfs;

	/* Endpoint mode specific */
	struct gpio_desc *pex_rst_gpiod;
	struct gpio_desc *pex_refclk_sel_gpiod;
	unsigned int pex_rst_irq;
	int ep_state;
};

struct tegra_pcie_dw_of_data {
	enum dw_pcie_device_mode mode;
};

static inline struct tegra_pcie_dw *to_tegra_pcie(struct dw_pcie *pci)
{
	return container_of(pci, struct tegra_pcie_dw, pci);
}

static inline void appl_writel(struct tegra_pcie_dw *pcie, const u32 value,
			       const u32 reg)
{
	writel_relaxed(value, pcie->appl_base + reg);
}

static inline u32 appl_readl(struct tegra_pcie_dw *pcie, const u32 reg)
{
	return readl_relaxed(pcie->appl_base + reg);
}

struct tegra_pcie_soc {
	enum dw_pcie_device_mode mode;
};

static void apply_bad_link_workaround(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 current_link_width;
	u16 val;

	/*
	 * NOTE:- Since this scenario is uncommon and link as such is not
	 * stable anyway, not waiting to confirm if link is really
	 * transitioning to Gen-2 speed
	 */
	val = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base + PCI_EXP_LNKSTA);
	if (val & PCI_EXP_LNKSTA_LBMS) {
		current_link_width = (val & PCI_EXP_LNKSTA_NLW) >>
				     PCI_EXP_LNKSTA_NLW_SHIFT;
		if (pcie->init_link_width > current_link_width) {
			dev_warn(pci->dev, "PCIe link is bad, width reduced\n");
			val = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base +
						PCI_EXP_LNKCTL2);
			val &= ~PCI_EXP_LNKCTL2_TLS;
			val |= PCI_EXP_LNKCTL2_TLS_2_5GT;
			dw_pcie_writew_dbi(pci, pcie->pcie_cap_base +
					   PCI_EXP_LNKCTL2, val);

			val = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base +
						PCI_EXP_LNKCTL);
			val |= PCI_EXP_LNKCTL_RL;
			dw_pcie_writew_dbi(pci, pcie->pcie_cap_base +
					   PCI_EXP_LNKCTL, val);
		}
	}
}

static irqreturn_t tegra_pcie_rp_irq_handler(int irq, void *arg)
{
	struct tegra_pcie_dw *pcie = arg;
	struct dw_pcie *pci = &pcie->pci;
	struct pcie_port *pp = &pci->pp;
	u32 val, tmp;
	u16 val_w;

	val = appl_readl(pcie, APPL_INTR_STATUS_L0);
	if (val & APPL_INTR_STATUS_L0_LINK_STATE_INT) {
		val = appl_readl(pcie, APPL_INTR_STATUS_L1_0_0);
		if (val & APPL_INTR_STATUS_L1_0_0_LINK_REQ_RST_NOT_CHGED) {
			appl_writel(pcie, val, APPL_INTR_STATUS_L1_0_0);

			/* SBR & Surprise Link Down WAR */
			val = appl_readl(pcie, APPL_CAR_RESET_OVRD);
			val &= ~APPL_CAR_RESET_OVRD_CYA_OVERRIDE_CORE_RST_N;
			appl_writel(pcie, val, APPL_CAR_RESET_OVRD);
			udelay(1);
			val = appl_readl(pcie, APPL_CAR_RESET_OVRD);
			val |= APPL_CAR_RESET_OVRD_CYA_OVERRIDE_CORE_RST_N;
			appl_writel(pcie, val, APPL_CAR_RESET_OVRD);

			val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
			val |= PORT_LOGIC_SPEED_CHANGE;
			dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);
		}
	}

	if (val & APPL_INTR_STATUS_L0_INT_INT) {
		val = appl_readl(pcie, APPL_INTR_STATUS_L1_8_0);
		if (val & APPL_INTR_STATUS_L1_8_0_AUTO_BW_INT_STS) {
			appl_writel(pcie,
				    APPL_INTR_STATUS_L1_8_0_AUTO_BW_INT_STS,
				    APPL_INTR_STATUS_L1_8_0);
			apply_bad_link_workaround(pp);
		}
		if (val & APPL_INTR_STATUS_L1_8_0_BW_MGT_INT_STS) {
			appl_writel(pcie,
				    APPL_INTR_STATUS_L1_8_0_BW_MGT_INT_STS,
				    APPL_INTR_STATUS_L1_8_0);

			val_w = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base +
						  PCI_EXP_LNKSTA);
			dev_dbg(pci->dev, "Link Speed : Gen-%u\n", val_w &
				PCI_EXP_LNKSTA_CLS);
		}
	}

	val = appl_readl(pcie, APPL_INTR_STATUS_L0);
	if (val & APPL_INTR_STATUS_L0_CDM_REG_CHK_INT) {
		val = appl_readl(pcie, APPL_INTR_STATUS_L1_18);
		tmp = dw_pcie_readl_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS);
		if (val & APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMPLT) {
			dev_info(pci->dev, "CDM check complete\n");
			tmp |= PCIE_PL_CHK_REG_CHK_REG_COMPLETE;
		}
		if (val & APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMP_ERR) {
			dev_err(pci->dev, "CDM comparison mismatch\n");
			tmp |= PCIE_PL_CHK_REG_CHK_REG_COMPARISON_ERROR;
		}
		if (val & APPL_INTR_STATUS_L1_18_CDM_REG_CHK_LOGIC_ERR) {
			dev_err(pci->dev, "CDM Logic error\n");
			tmp |= PCIE_PL_CHK_REG_CHK_REG_LOGIC_ERROR;
		}
		dw_pcie_writel_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS, tmp);
		tmp = dw_pcie_readl_dbi(pci, PCIE_PL_CHK_REG_ERR_ADDR);
		dev_err(pci->dev, "CDM Error Address Offset = 0x%08X\n", tmp);
	}

	return IRQ_HANDLED;
}

static void pex_ep_event_hot_rst_done(struct tegra_pcie_dw *pcie)
{
	u32 val;

	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_0_0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_1);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_2);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_3);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_6);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_7);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_8_0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_9);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_10);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_11);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_13);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_14);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_15);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_17);
	appl_writel(pcie, 0xFFFFFFFF, APPL_MSI_CTRL_2);

	val = appl_readl(pcie, APPL_CTRL);
	val |= APPL_CTRL_LTSSM_EN;
	appl_writel(pcie, val, APPL_CTRL);
}

static irqreturn_t tegra_pcie_ep_irq_thread(int irq, void *arg)
{
	struct tegra_pcie_dw *pcie = arg;
	struct dw_pcie *pci = &pcie->pci;
	u32 val, speed;

	speed = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base + PCI_EXP_LNKSTA) &
		PCI_EXP_LNKSTA_CLS;
	clk_set_rate(pcie->core_clk, pcie_gen_freq[speed - 1]);

	/* If EP doesn't advertise L1SS, just return */
	val = dw_pcie_readl_dbi(pci, pcie->cfg_link_cap_l1sub);
	if (!(val & (PCI_L1SS_CAP_ASPM_L1_1 | PCI_L1SS_CAP_ASPM_L1_2)))
		return IRQ_HANDLED;

	/* Check if BME is set to '1' */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	if (val & PCI_COMMAND_MASTER) {
		ktime_t timeout;

		/* 110us for both snoop and no-snoop */
		val = 110 | (2 << PCI_LTR_SCALE_SHIFT) | LTR_MSG_REQ;
		val |= (val << LTR_MST_NO_SNOOP_SHIFT);
		appl_writel(pcie, val, APPL_LTR_MSG_1);

		/* Send LTR upstream */
		val = appl_readl(pcie, APPL_LTR_MSG_2);
		val |= APPL_LTR_MSG_2_LTR_MSG_REQ_STATE;
		appl_writel(pcie, val, APPL_LTR_MSG_2);

		timeout = ktime_add_us(ktime_get(), LTR_MSG_TIMEOUT);
		for (;;) {
			val = appl_readl(pcie, APPL_LTR_MSG_2);
			if (!(val & APPL_LTR_MSG_2_LTR_MSG_REQ_STATE))
				break;
			if (ktime_after(ktime_get(), timeout))
				break;
			usleep_range(1000, 1100);
		}
		if (val & APPL_LTR_MSG_2_LTR_MSG_REQ_STATE)
			dev_err(pcie->dev, "Failed to send LTR message\n");
	}

	return IRQ_HANDLED;
}

static irqreturn_t tegra_pcie_ep_hard_irq(int irq, void *arg)
{
	struct tegra_pcie_dw *pcie = arg;
	struct dw_pcie_ep *ep = &pcie->pci.ep;
	int spurious = 1;
	u32 val, tmp;

	val = appl_readl(pcie, APPL_INTR_STATUS_L0);
	if (val & APPL_INTR_STATUS_L0_LINK_STATE_INT) {
		val = appl_readl(pcie, APPL_INTR_STATUS_L1_0_0);
		appl_writel(pcie, val, APPL_INTR_STATUS_L1_0_0);

		if (val & APPL_INTR_STATUS_L1_0_0_HOT_RESET_DONE)
			pex_ep_event_hot_rst_done(pcie);

		if (val & APPL_INTR_STATUS_L1_0_0_RDLH_LINK_UP_CHGED) {
			tmp = appl_readl(pcie, APPL_LINK_STATUS);
			if (tmp & APPL_LINK_STATUS_RDLH_LINK_UP) {
				dev_dbg(pcie->dev, "Link is up with Host\n");
				dw_pcie_ep_linkup(ep);
			}
		}

		spurious = 0;
	}

	if (val & APPL_INTR_STATUS_L0_PCI_CMD_EN_INT) {
		val = appl_readl(pcie, APPL_INTR_STATUS_L1_15);
		appl_writel(pcie, val, APPL_INTR_STATUS_L1_15);

		if (val & APPL_INTR_STATUS_L1_15_CFG_BME_CHGED)
			return IRQ_WAKE_THREAD;

		spurious = 0;
	}

	if (spurious) {
		dev_warn(pcie->dev, "Random interrupt (STATUS = 0x%08X)\n",
			 val);
		appl_writel(pcie, val, APPL_INTR_STATUS_L0);
	}

	return IRQ_HANDLED;
}

static int tegra_pcie_dw_rd_own_conf(struct pci_bus *bus, u32 devfn, int where,
				     int size, u32 *val)
{
	/*
	 * This is an endpoint mode specific register happen to appear even
	 * when controller is operating in root port mode and system hangs
	 * when it is accessed with link being in ASPM-L1 state.
	 * So skip accessing it altogether
	 */
	if (!PCI_SLOT(devfn) && where == PORT_LOGIC_MSIX_DOORBELL) {
		*val = 0x00000000;
		return PCIBIOS_SUCCESSFUL;
	}

	return pci_generic_config_read(bus, devfn, where, size, val);
}

static int tegra_pcie_dw_wr_own_conf(struct pci_bus *bus, u32 devfn, int where,
				     int size, u32 val)
{
	/*
	 * This is an endpoint mode specific register happen to appear even
	 * when controller is operating in root port mode and system hangs
	 * when it is accessed with link being in ASPM-L1 state.
	 * So skip accessing it altogether
	 */
	if (!PCI_SLOT(devfn) && where == PORT_LOGIC_MSIX_DOORBELL)
		return PCIBIOS_SUCCESSFUL;

	return pci_generic_config_write(bus, devfn, where, size, val);
}

static struct pci_ops tegra_pci_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = tegra_pcie_dw_rd_own_conf,
	.write = tegra_pcie_dw_wr_own_conf,
};

#if defined(CONFIG_PCIEASPM)
static void disable_aspm_l11(struct tegra_pcie_dw *pcie)
{
	u32 val;

	val = dw_pcie_readl_dbi(&pcie->pci, pcie->cfg_link_cap_l1sub);
	val &= ~PCI_L1SS_CAP_ASPM_L1_1;
	dw_pcie_writel_dbi(&pcie->pci, pcie->cfg_link_cap_l1sub, val);
}

static void disable_aspm_l12(struct tegra_pcie_dw *pcie)
{
	u32 val;

	val = dw_pcie_readl_dbi(&pcie->pci, pcie->cfg_link_cap_l1sub);
	val &= ~PCI_L1SS_CAP_ASPM_L1_2;
	dw_pcie_writel_dbi(&pcie->pci, pcie->cfg_link_cap_l1sub, val);
}

static inline u32 event_counter_prog(struct tegra_pcie_dw *pcie, u32 event)
{
	u32 val;

	val = dw_pcie_readl_dbi(&pcie->pci, event_cntr_ctrl_offset[pcie->cid]);
	val &= ~(EVENT_COUNTER_EVENT_SEL_MASK << EVENT_COUNTER_EVENT_SEL_SHIFT);
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	val |= event << EVENT_COUNTER_EVENT_SEL_SHIFT;
	val |= EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	dw_pcie_writel_dbi(&pcie->pci, event_cntr_ctrl_offset[pcie->cid], val);
	val = dw_pcie_readl_dbi(&pcie->pci, event_cntr_data_offset[pcie->cid]);

	return val;
}

static int aspm_state_cnt(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)
				     dev_get_drvdata(s->private);
	u32 val;

	seq_printf(s, "Tx L0s entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_Tx_L0S));

	seq_printf(s, "Rx L0s entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_Rx_L0S));

	seq_printf(s, "Link L1 entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_L1));

	seq_printf(s, "Link L1.1 entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_L1_1));

	seq_printf(s, "Link L1.2 entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_L1_2));

	/* Clear all counters */
	dw_pcie_writel_dbi(&pcie->pci, event_cntr_ctrl_offset[pcie->cid],
			   EVENT_COUNTER_ALL_CLEAR);

	/* Re-enable counting */
	val = EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	dw_pcie_writel_dbi(&pcie->pci, event_cntr_ctrl_offset[pcie->cid], val);

	return 0;
}

static void init_host_aspm(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	u32 val;

	val = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_L1SS);
	pcie->cfg_link_cap_l1sub = val + PCI_L1SS_CAP;

	/* Enable ASPM counters */
	val = EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	dw_pcie_writel_dbi(pci, event_cntr_ctrl_offset[pcie->cid], val);

	/* Program T_cmrt and T_pwr_on values */
	val = dw_pcie_readl_dbi(pci, pcie->cfg_link_cap_l1sub);
	val &= ~(PCI_L1SS_CAP_CM_RESTORE_TIME | PCI_L1SS_CAP_P_PWR_ON_VALUE);
	val |= (pcie->aspm_cmrt << 8);
	val |= (pcie->aspm_pwr_on_t << 19);
	dw_pcie_writel_dbi(pci, pcie->cfg_link_cap_l1sub, val);

	/* Program L0s and L1 entrance latencies */
	val = dw_pcie_readl_dbi(pci, PCIE_PORT_AFR);
	val &= ~PORT_AFR_L0S_ENTRANCE_LAT_MASK;
	val |= (pcie->aspm_l0s_enter_lat << PORT_AFR_L0S_ENTRANCE_LAT_SHIFT);
	val |= PORT_AFR_ENTER_ASPM;
	dw_pcie_writel_dbi(pci, PCIE_PORT_AFR, val);
}

static void init_debugfs(struct tegra_pcie_dw *pcie)
{
	debugfs_create_devm_seqfile(pcie->dev, "aspm_state_cnt", pcie->debugfs,
				    aspm_state_cnt);
}
#else
static inline void disable_aspm_l12(struct tegra_pcie_dw *pcie) { return; }
static inline void disable_aspm_l11(struct tegra_pcie_dw *pcie) { return; }
static inline void init_host_aspm(struct tegra_pcie_dw *pcie) { return; }
static inline void init_debugfs(struct tegra_pcie_dw *pcie) { return; }
#endif

static void tegra_pcie_enable_system_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 val;
	u16 val_w;

	val = appl_readl(pcie, APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_LINK_STATE_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L0_0);

	val = appl_readl(pcie, APPL_INTR_EN_L1_0_0);
	val |= APPL_INTR_EN_L1_0_0_LINK_REQ_RST_NOT_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L1_0_0);

	if (pcie->enable_cdm_check) {
		val = appl_readl(pcie, APPL_INTR_EN_L0_0);
		val |= APPL_INTR_EN_L0_0_CDM_REG_CHK_INT_EN;
		appl_writel(pcie, val, APPL_INTR_EN_L0_0);

		val = appl_readl(pcie, APPL_INTR_EN_L1_18);
		val |= APPL_INTR_EN_L1_18_CDM_REG_CHK_CMP_ERR;
		val |= APPL_INTR_EN_L1_18_CDM_REG_CHK_LOGIC_ERR;
		appl_writel(pcie, val, APPL_INTR_EN_L1_18);
	}

	val_w = dw_pcie_readw_dbi(&pcie->pci, pcie->pcie_cap_base +
				  PCI_EXP_LNKSTA);
	pcie->init_link_width = (val_w & PCI_EXP_LNKSTA_NLW) >>
				PCI_EXP_LNKSTA_NLW_SHIFT;

	val_w = dw_pcie_readw_dbi(&pcie->pci, pcie->pcie_cap_base +
				  PCI_EXP_LNKCTL);
	val_w |= PCI_EXP_LNKCTL_LBMIE;
	dw_pcie_writew_dbi(&pcie->pci, pcie->pcie_cap_base + PCI_EXP_LNKCTL,
			   val_w);
}

static void tegra_pcie_enable_legacy_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 val;

	/* Enable legacy interrupt generation */
	val = appl_readl(pcie, APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_SYS_INTR_EN;
	val |= APPL_INTR_EN_L0_0_INT_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L0_0);

	val = appl_readl(pcie, APPL_INTR_EN_L1_8_0);
	val |= APPL_INTR_EN_L1_8_INTX_EN;
	val |= APPL_INTR_EN_L1_8_AUTO_BW_INT_EN;
	val |= APPL_INTR_EN_L1_8_BW_MGT_INT_EN;
	if (IS_ENABLED(CONFIG_PCIEAER))
		val |= APPL_INTR_EN_L1_8_AER_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L1_8_0);
}

static void tegra_pcie_enable_msi_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 val;

	dw_pcie_msi_init(pp);

	/* Enable MSI interrupt generation */
	val = appl_readl(pcie, APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_SYS_MSI_INTR_EN;
	val |= APPL_INTR_EN_L0_0_MSI_RCV_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L0_0);
}

static void tegra_pcie_enable_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);

	/* Clear interrupt statuses before enabling interrupts */
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_0_0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_1);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_2);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_3);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_6);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_7);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_8_0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_9);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_10);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_11);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_13);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_14);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_15);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_17);

	tegra_pcie_enable_system_interrupts(pp);
	tegra_pcie_enable_legacy_interrupts(pp);
	if (IS_ENABLED(CONFIG_PCI_MSI))
		tegra_pcie_enable_msi_interrupts(pp);
}

static void config_gen3_gen4_eq_presets(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	u32 val, offset, i;

	/* Program init preset */
	for (i = 0; i < pcie->num_lanes; i++) {
		val = dw_pcie_readw_dbi(pci, CAP_SPCIE_CAP_OFF + (i * 2));
		val &= ~CAP_SPCIE_CAP_OFF_DSP_TX_PRESET0_MASK;
		val |= GEN3_GEN4_EQ_PRESET_INIT;
		val &= ~CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_MASK;
		val |= (GEN3_GEN4_EQ_PRESET_INIT <<
			   CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_SHIFT);
		dw_pcie_writew_dbi(pci, CAP_SPCIE_CAP_OFF + (i * 2), val);

		offset = dw_pcie_find_ext_capability(pci,
						     PCI_EXT_CAP_ID_PL_16GT) +
				PCI_PL_16GT_LE_CTRL;
		val = dw_pcie_readb_dbi(pci, offset + i);
		val &= ~PCI_PL_16GT_LE_CTRL_DSP_TX_PRESET_MASK;
		val |= GEN3_GEN4_EQ_PRESET_INIT;
		val &= ~PCI_PL_16GT_LE_CTRL_USP_TX_PRESET_MASK;
		val |= (GEN3_GEN4_EQ_PRESET_INIT <<
			PCI_PL_16GT_LE_CTRL_USP_TX_PRESET_SHIFT);
		dw_pcie_writeb_dbi(pci, offset + i, val);
	}

	val = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, val);

	val = dw_pcie_readl_dbi(pci, GEN3_EQ_CONTROL_OFF);
	val &= ~GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_MASK;
	val |= (0x3ff << GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_SHIFT);
	val &= ~GEN3_EQ_CONTROL_OFF_FB_MODE_MASK;
	dw_pcie_writel_dbi(pci, GEN3_EQ_CONTROL_OFF, val);

	val = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	val |= (0x1 << GEN3_RELATED_OFF_RATE_SHADOW_SEL_SHIFT);
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, val);

	val = dw_pcie_readl_dbi(pci, GEN3_EQ_CONTROL_OFF);
	val &= ~GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_MASK;
	val |= (0x360 << GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_SHIFT);
	val &= ~GEN3_EQ_CONTROL_OFF_FB_MODE_MASK;
	dw_pcie_writel_dbi(pci, GEN3_EQ_CONTROL_OFF, val);

	val = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, val);
}

static void tegra_pcie_prepare_host(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCI_IO_BASE);
	val &= ~(IO_BASE_IO_DECODE | IO_BASE_IO_DECODE_BIT8);
	dw_pcie_writel_dbi(pci, PCI_IO_BASE, val);

	val = dw_pcie_readl_dbi(pci, PCI_PREF_MEMORY_BASE);
	val |= CFG_PREF_MEM_LIMIT_BASE_MEM_DECODE;
	val |= CFG_PREF_MEM_LIMIT_BASE_MEM_LIMIT_DECODE;
	dw_pcie_writel_dbi(pci, PCI_PREF_MEMORY_BASE, val);

	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);

	/* Enable as 0xFFFF0001 response for CRS */
	val = dw_pcie_readl_dbi(pci, PORT_LOGIC_AMBA_ERROR_RESPONSE_DEFAULT);
	val &= ~(AMBA_ERROR_RESPONSE_CRS_MASK << AMBA_ERROR_RESPONSE_CRS_SHIFT);
	val |= (AMBA_ERROR_RESPONSE_CRS_OKAY_FFFF0001 <<
		AMBA_ERROR_RESPONSE_CRS_SHIFT);
	dw_pcie_writel_dbi(pci, PORT_LOGIC_AMBA_ERROR_RESPONSE_DEFAULT, val);

	/* Configure Max lane width from DT */
	val = dw_pcie_readl_dbi(pci, pcie->pcie_cap_base + PCI_EXP_LNKCAP);
	val &= ~PCI_EXP_LNKCAP_MLW;
	val |= (pcie->num_lanes << PCI_EXP_LNKSTA_NLW_SHIFT);
	dw_pcie_writel_dbi(pci, pcie->pcie_cap_base + PCI_EXP_LNKCAP, val);

	config_gen3_gen4_eq_presets(pcie);

	init_host_aspm(pcie);

	val = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL;
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, val);

	if (pcie->update_fc_fixup) {
		val = dw_pcie_readl_dbi(pci, CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF);
		val |= 0x1 << CFG_TIMER_CTRL_ACK_NAK_SHIFT;
		dw_pcie_writel_dbi(pci, CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF, val);
	}

	dw_pcie_setup_rc(pp);

	clk_set_rate(pcie->core_clk, GEN4_CORE_CLK_FREQ);

	/* Assert RST */
	val = appl_readl(pcie, APPL_PINMUX);
	val &= ~APPL_PINMUX_PEX_RST;
	appl_writel(pcie, val, APPL_PINMUX);

	usleep_range(100, 200);

	/* Enable LTSSM */
	val = appl_readl(pcie, APPL_CTRL);
	val |= APPL_CTRL_LTSSM_EN;
	appl_writel(pcie, val, APPL_CTRL);

	/* De-assert RST */
	val = appl_readl(pcie, APPL_PINMUX);
	val |= APPL_PINMUX_PEX_RST;
	appl_writel(pcie, val, APPL_PINMUX);

	msleep(100);
}

static int tegra_pcie_dw_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 val, tmp, offset, speed;

	pp->bridge->ops = &tegra_pci_ops;

	tegra_pcie_prepare_host(pp);

	if (dw_pcie_wait_for_link(pci)) {
		/*
		 * There are some endpoints which can't get the link up if
		 * root port has Data Link Feature (DLF) enabled.
		 * Refer Spec rev 4.0 ver 1.0 sec 3.4.2 & 7.7.4 for more info
		 * on Scaled Flow Control and DLF.
		 * So, need to confirm that is indeed the case here and attempt
		 * link up once again with DLF disabled.
		 */
		val = appl_readl(pcie, APPL_DEBUG);
		val &= APPL_DEBUG_LTSSM_STATE_MASK;
		val >>= APPL_DEBUG_LTSSM_STATE_SHIFT;
		tmp = appl_readl(pcie, APPL_LINK_STATUS);
		tmp &= APPL_LINK_STATUS_RDLH_LINK_UP;
		if (!(val == 0x11 && !tmp)) {
			/* Link is down for all good reasons */
			return 0;
		}

		dev_info(pci->dev, "Link is down in DLL");
		dev_info(pci->dev, "Trying again with DLFE disabled\n");
		/* Disable LTSSM */
		val = appl_readl(pcie, APPL_CTRL);
		val &= ~APPL_CTRL_LTSSM_EN;
		appl_writel(pcie, val, APPL_CTRL);

		reset_control_assert(pcie->core_rst);
		reset_control_deassert(pcie->core_rst);

		offset = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_DLF);
		val = dw_pcie_readl_dbi(pci, offset + PCI_DLF_CAP);
		val &= ~PCI_DLF_EXCHANGE_ENABLE;
		dw_pcie_writel_dbi(pci, offset, val);

		tegra_pcie_prepare_host(pp);

		if (dw_pcie_wait_for_link(pci))
			return 0;
	}

	speed = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base + PCI_EXP_LNKSTA) &
		PCI_EXP_LNKSTA_CLS;
	clk_set_rate(pcie->core_clk, pcie_gen_freq[speed - 1]);

	tegra_pcie_enable_interrupts(pp);

	return 0;
}

static int tegra_pcie_dw_link_up(struct dw_pcie *pci)
{
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);
	u32 val = dw_pcie_readw_dbi(pci, pcie->pcie_cap_base + PCI_EXP_LNKSTA);

	return !!(val & PCI_EXP_LNKSTA_DLLLA);
}

static void tegra_pcie_set_msi_vec_num(struct pcie_port *pp)
{
	pp->num_vectors = MAX_MSI_IRQS;
}

static int tegra_pcie_dw_start_link(struct dw_pcie *pci)
{
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);

	enable_irq(pcie->pex_rst_irq);

	return 0;
}

static void tegra_pcie_dw_stop_link(struct dw_pcie *pci)
{
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);

	disable_irq(pcie->pex_rst_irq);
}

static const struct dw_pcie_ops tegra_dw_pcie_ops = {
	.link_up = tegra_pcie_dw_link_up,
	.start_link = tegra_pcie_dw_start_link,
	.stop_link = tegra_pcie_dw_stop_link,
};

static struct dw_pcie_host_ops tegra_pcie_dw_host_ops = {
	.host_init = tegra_pcie_dw_host_init,
	.set_num_vectors = tegra_pcie_set_msi_vec_num,
};

static void tegra_pcie_disable_phy(struct tegra_pcie_dw *pcie)
{
	unsigned int phy_count = pcie->phy_count;

	while (phy_count--) {
		phy_power_off(pcie->phys[phy_count]);
		phy_exit(pcie->phys[phy_count]);
	}
}

static int tegra_pcie_enable_phy(struct tegra_pcie_dw *pcie)
{
	unsigned int i;
	int ret;

	for (i = 0; i < pcie->phy_count; i++) {
		ret = phy_init(pcie->phys[i]);
		if (ret < 0)
			goto phy_power_off;

		ret = phy_power_on(pcie->phys[i]);
		if (ret < 0)
			goto phy_exit;
	}

	return 0;

phy_power_off:
	while (i--) {
		phy_power_off(pcie->phys[i]);
phy_exit:
		phy_exit(pcie->phys[i]);
	}

	return ret;
}

static int tegra_pcie_dw_parse_dt(struct tegra_pcie_dw *pcie)
{
	struct device_node *np = pcie->dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "nvidia,aspm-cmrt-us", &pcie->aspm_cmrt);
	if (ret < 0) {
		dev_info(pcie->dev, "Failed to read ASPM T_cmrt: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "nvidia,aspm-pwr-on-t-us",
				   &pcie->aspm_pwr_on_t);
	if (ret < 0)
		dev_info(pcie->dev, "Failed to read ASPM Power On time: %d\n",
			 ret);

	ret = of_property_read_u32(np, "nvidia,aspm-l0s-entrance-latency-us",
				   &pcie->aspm_l0s_enter_lat);
	if (ret < 0)
		dev_info(pcie->dev,
			 "Failed to read ASPM L0s Entrance latency: %d\n", ret);

	ret = of_property_read_u32(np, "num-lanes", &pcie->num_lanes);
	if (ret < 0) {
		dev_err(pcie->dev, "Failed to read num-lanes: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32_index(np, "nvidia,bpmp", 1, &pcie->cid);
	if (ret) {
		dev_err(pcie->dev, "Failed to read Controller-ID: %d\n", ret);
		return ret;
	}

	ret = of_property_count_strings(np, "phy-names");
	if (ret < 0) {
		dev_err(pcie->dev, "Failed to find PHY entries: %d\n",
			ret);
		return ret;
	}
	pcie->phy_count = ret;

	if (of_property_read_bool(np, "nvidia,update-fc-fixup"))
		pcie->update_fc_fixup = true;

	pcie->supports_clkreq =
		of_property_read_bool(pcie->dev->of_node, "supports-clkreq");

	pcie->enable_cdm_check =
		of_property_read_bool(np, "snps,enable-cdm-check");

	if (pcie->mode == DW_PCIE_RC_TYPE)
		return 0;

	/* Endpoint mode specific DT entries */
	pcie->pex_rst_gpiod = devm_gpiod_get(pcie->dev, "reset", GPIOD_IN);
	if (IS_ERR(pcie->pex_rst_gpiod)) {
		int err = PTR_ERR(pcie->pex_rst_gpiod);
		const char *level = KERN_ERR;

		if (err == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, pcie->dev,
			   dev_fmt("Failed to get PERST GPIO: %d\n"),
			   err);
		return err;
	}

	pcie->pex_refclk_sel_gpiod = devm_gpiod_get(pcie->dev,
						    "nvidia,refclk-select",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(pcie->pex_refclk_sel_gpiod)) {
		int err = PTR_ERR(pcie->pex_refclk_sel_gpiod);
		const char *level = KERN_ERR;

		if (err == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, pcie->dev,
			   dev_fmt("Failed to get REFCLK select GPIOs: %d\n"),
			   err);
		pcie->pex_refclk_sel_gpiod = NULL;
	}

	return 0;
}

static int tegra_pcie_bpmp_set_ctrl_state(struct tegra_pcie_dw *pcie,
					  bool enable)
{
	struct mrq_uphy_response resp;
	struct tegra_bpmp_message msg;
	struct mrq_uphy_request req;

	/* Controller-5 doesn't need to have its state set by BPMP-FW */
	if (pcie->cid == 5)
		return 0;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	req.cmd = CMD_UPHY_PCIE_CONTROLLER_STATE;
	req.controller_state.pcie_controller = pcie->cid;
	req.controller_state.enable = enable;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_UPHY;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(resp);

	return tegra_bpmp_transfer(pcie->bpmp, &msg);
}

static int tegra_pcie_bpmp_set_pll_state(struct tegra_pcie_dw *pcie,
					 bool enable)
{
	struct mrq_uphy_response resp;
	struct tegra_bpmp_message msg;
	struct mrq_uphy_request req;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	if (enable) {
		req.cmd = CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT;
		req.ep_ctrlr_pll_init.ep_controller = pcie->cid;
	} else {
		req.cmd = CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF;
		req.ep_ctrlr_pll_off.ep_controller = pcie->cid;
	}

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_UPHY;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &resp;
	msg.rx.size = sizeof(resp);

	return tegra_bpmp_transfer(pcie->bpmp, &msg);
}

static void tegra_pcie_downstream_dev_to_D0(struct tegra_pcie_dw *pcie)
{
	struct pcie_port *pp = &pcie->pci.pp;
	struct pci_bus *child, *root_bus = NULL;
	struct pci_dev *pdev;

	/*
	 * link doesn't go into L2 state with some of the endpoints with Tegra
	 * if they are not in D0 state. So, need to make sure that immediate
	 * downstream devices are in D0 state before sending PME_TurnOff to put
	 * link into L2 state.
	 * This is as per PCI Express Base r4.0 v1.0 September 27-2017,
	 * 5.2 Link State Power Management (Page #428).
	 */

	list_for_each_entry(child, &pp->bridge->bus->children, node) {
		/* Bring downstream devices to D0 if they are not already in */
		if (child->parent == pp->bridge->bus) {
			root_bus = child;
			break;
		}
	}

	if (!root_bus) {
		dev_err(pcie->dev, "Failed to find downstream devices\n");
		return;
	}

	list_for_each_entry(pdev, &root_bus->devices, bus_list) {
		if (PCI_SLOT(pdev->devfn) == 0) {
			if (pci_set_power_state(pdev, PCI_D0))
				dev_err(pcie->dev,
					"Failed to transition %s to D0 state\n",
					dev_name(&pdev->dev));
		}
	}
}

static int tegra_pcie_get_slot_regulators(struct tegra_pcie_dw *pcie)
{
	pcie->slot_ctl_3v3 = devm_regulator_get_optional(pcie->dev, "vpcie3v3");
	if (IS_ERR(pcie->slot_ctl_3v3)) {
		if (PTR_ERR(pcie->slot_ctl_3v3) != -ENODEV)
			return PTR_ERR(pcie->slot_ctl_3v3);

		pcie->slot_ctl_3v3 = NULL;
	}

	pcie->slot_ctl_12v = devm_regulator_get_optional(pcie->dev, "vpcie12v");
	if (IS_ERR(pcie->slot_ctl_12v)) {
		if (PTR_ERR(pcie->slot_ctl_12v) != -ENODEV)
			return PTR_ERR(pcie->slot_ctl_12v);

		pcie->slot_ctl_12v = NULL;
	}

	return 0;
}

static int tegra_pcie_enable_slot_regulators(struct tegra_pcie_dw *pcie)
{
	int ret;

	if (pcie->slot_ctl_3v3) {
		ret = regulator_enable(pcie->slot_ctl_3v3);
		if (ret < 0) {
			dev_err(pcie->dev,
				"Failed to enable 3.3V slot supply: %d\n", ret);
			return ret;
		}
	}

	if (pcie->slot_ctl_12v) {
		ret = regulator_enable(pcie->slot_ctl_12v);
		if (ret < 0) {
			dev_err(pcie->dev,
				"Failed to enable 12V slot supply: %d\n", ret);
			goto fail_12v_enable;
		}
	}

	/*
	 * According to PCI Express Card Electromechanical Specification
	 * Revision 1.1, Table-2.4, T_PVPERL (Power stable to PERST# inactive)
	 * should be a minimum of 100ms.
	 */
	if (pcie->slot_ctl_3v3 || pcie->slot_ctl_12v)
		msleep(100);

	return 0;

fail_12v_enable:
	if (pcie->slot_ctl_3v3)
		regulator_disable(pcie->slot_ctl_3v3);
	return ret;
}

static void tegra_pcie_disable_slot_regulators(struct tegra_pcie_dw *pcie)
{
	if (pcie->slot_ctl_12v)
		regulator_disable(pcie->slot_ctl_12v);
	if (pcie->slot_ctl_3v3)
		regulator_disable(pcie->slot_ctl_3v3);
}

static int tegra_pcie_config_controller(struct tegra_pcie_dw *pcie,
					bool en_hw_hot_rst)
{
	int ret;
	u32 val;

	ret = tegra_pcie_bpmp_set_ctrl_state(pcie, true);
	if (ret) {
		dev_err(pcie->dev,
			"Failed to enable controller %u: %d\n", pcie->cid, ret);
		return ret;
	}

	ret = tegra_pcie_enable_slot_regulators(pcie);
	if (ret < 0)
		goto fail_slot_reg_en;

	ret = regulator_enable(pcie->pex_ctl_supply);
	if (ret < 0) {
		dev_err(pcie->dev, "Failed to enable regulator: %d\n", ret);
		goto fail_reg_en;
	}

	ret = clk_prepare_enable(pcie->core_clk);
	if (ret) {
		dev_err(pcie->dev, "Failed to enable core clock: %d\n", ret);
		goto fail_core_clk;
	}

	ret = reset_control_deassert(pcie->core_apb_rst);
	if (ret) {
		dev_err(pcie->dev, "Failed to deassert core APB reset: %d\n",
			ret);
		goto fail_core_apb_rst;
	}

	if (en_hw_hot_rst) {
		/* Enable HW_HOT_RST mode */
		val = appl_readl(pcie, APPL_CTRL);
		val &= ~(APPL_CTRL_HW_HOT_RST_MODE_MASK <<
			 APPL_CTRL_HW_HOT_RST_MODE_SHIFT);
		val |= APPL_CTRL_HW_HOT_RST_EN;
		appl_writel(pcie, val, APPL_CTRL);
	}

	ret = tegra_pcie_enable_phy(pcie);
	if (ret) {
		dev_err(pcie->dev, "Failed to enable PHY: %d\n", ret);
		goto fail_phy;
	}

	/* Update CFG base address */
	appl_writel(pcie, pcie->dbi_res->start & APPL_CFG_BASE_ADDR_MASK,
		    APPL_CFG_BASE_ADDR);

	/* Configure this core for RP mode operation */
	appl_writel(pcie, APPL_DM_TYPE_RP, APPL_DM_TYPE);

	appl_writel(pcie, 0x0, APPL_CFG_SLCG_OVERRIDE);

	val = appl_readl(pcie, APPL_CTRL);
	appl_writel(pcie, val | APPL_CTRL_SYS_PRE_DET_STATE, APPL_CTRL);

	val = appl_readl(pcie, APPL_CFG_MISC);
	val |= (APPL_CFG_MISC_ARCACHE_VAL << APPL_CFG_MISC_ARCACHE_SHIFT);
	appl_writel(pcie, val, APPL_CFG_MISC);

	if (!pcie->supports_clkreq) {
		val = appl_readl(pcie, APPL_PINMUX);
		val |= APPL_PINMUX_CLKREQ_OVERRIDE_EN;
		val &= ~APPL_PINMUX_CLKREQ_OVERRIDE;
		appl_writel(pcie, val, APPL_PINMUX);
	}

	/* Update iATU_DMA base address */
	appl_writel(pcie,
		    pcie->atu_dma_res->start & APPL_CFG_IATU_DMA_BASE_ADDR_MASK,
		    APPL_CFG_IATU_DMA_BASE_ADDR);

	reset_control_deassert(pcie->core_rst);

	pcie->pcie_cap_base = dw_pcie_find_capability(&pcie->pci,
						      PCI_CAP_ID_EXP);

	/* Disable ASPM-L1SS advertisement as there is no CLKREQ routing */
	if (!pcie->supports_clkreq) {
		disable_aspm_l11(pcie);
		disable_aspm_l12(pcie);
	}

	return ret;

fail_phy:
	reset_control_assert(pcie->core_apb_rst);
fail_core_apb_rst:
	clk_disable_unprepare(pcie->core_clk);
fail_core_clk:
	regulator_disable(pcie->pex_ctl_supply);
fail_reg_en:
	tegra_pcie_disable_slot_regulators(pcie);
fail_slot_reg_en:
	tegra_pcie_bpmp_set_ctrl_state(pcie, false);

	return ret;
}

static int __deinit_controller(struct tegra_pcie_dw *pcie)
{
	int ret;

	ret = reset_control_assert(pcie->core_rst);
	if (ret) {
		dev_err(pcie->dev, "Failed to assert \"core\" reset: %d\n",
			ret);
		return ret;
	}

	tegra_pcie_disable_phy(pcie);

	ret = reset_control_assert(pcie->core_apb_rst);
	if (ret) {
		dev_err(pcie->dev, "Failed to assert APB reset: %d\n", ret);
		return ret;
	}

	clk_disable_unprepare(pcie->core_clk);

	ret = regulator_disable(pcie->pex_ctl_supply);
	if (ret) {
		dev_err(pcie->dev, "Failed to disable regulator: %d\n", ret);
		return ret;
	}

	tegra_pcie_disable_slot_regulators(pcie);

	ret = tegra_pcie_bpmp_set_ctrl_state(pcie, false);
	if (ret) {
		dev_err(pcie->dev, "Failed to disable controller %d: %d\n",
			pcie->cid, ret);
		return ret;
	}

	return ret;
}

static int tegra_pcie_init_controller(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	struct pcie_port *pp = &pci->pp;
	int ret;

	ret = tegra_pcie_config_controller(pcie, false);
	if (ret < 0)
		return ret;

	pp->ops = &tegra_pcie_dw_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret < 0) {
		dev_err(pcie->dev, "Failed to add PCIe port: %d\n", ret);
		goto fail_host_init;
	}

	return 0;

fail_host_init:
	return __deinit_controller(pcie);
}

static int tegra_pcie_try_link_l2(struct tegra_pcie_dw *pcie)
{
	u32 val;

	if (!tegra_pcie_dw_link_up(&pcie->pci))
		return 0;

	val = appl_readl(pcie, APPL_RADM_STATUS);
	val |= APPL_PM_XMT_TURNOFF_STATE;
	appl_writel(pcie, val, APPL_RADM_STATUS);

	return readl_poll_timeout_atomic(pcie->appl_base + APPL_DEBUG, val,
				 val & APPL_DEBUG_PM_LINKST_IN_L2_LAT,
				 1, PME_ACK_TIMEOUT);
}

static void tegra_pcie_dw_pme_turnoff(struct tegra_pcie_dw *pcie)
{
	u32 data;
	int err;

	if (!tegra_pcie_dw_link_up(&pcie->pci)) {
		dev_dbg(pcie->dev, "PCIe link is not up...!\n");
		return;
	}

	if (tegra_pcie_try_link_l2(pcie)) {
		dev_info(pcie->dev, "Link didn't transition to L2 state\n");
		/*
		 * TX lane clock freq will reset to Gen1 only if link is in L2
		 * or detect state.
		 * So apply pex_rst to end point to force RP to go into detect
		 * state
		 */
		data = appl_readl(pcie, APPL_PINMUX);
		data &= ~APPL_PINMUX_PEX_RST;
		appl_writel(pcie, data, APPL_PINMUX);

		err = readl_poll_timeout_atomic(pcie->appl_base + APPL_DEBUG,
						data,
						((data &
						APPL_DEBUG_LTSSM_STATE_MASK) >>
						APPL_DEBUG_LTSSM_STATE_SHIFT) ==
						LTSSM_STATE_PRE_DETECT,
						1, LTSSM_TIMEOUT);
		if (err) {
			dev_info(pcie->dev, "Link didn't go to detect state\n");
		} else {
			/* Disable LTSSM after link is in detect state */
			data = appl_readl(pcie, APPL_CTRL);
			data &= ~APPL_CTRL_LTSSM_EN;
			appl_writel(pcie, data, APPL_CTRL);
		}
	}
	/*
	 * DBI registers may not be accessible after this as PLL-E would be
	 * down depending on how CLKREQ is pulled by end point
	 */
	data = appl_readl(pcie, APPL_PINMUX);
	data |= (APPL_PINMUX_CLKREQ_OVERRIDE_EN | APPL_PINMUX_CLKREQ_OVERRIDE);
	/* Cut REFCLK to slot */
	data |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN;
	data &= ~APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE;
	appl_writel(pcie, data, APPL_PINMUX);
}

static int tegra_pcie_deinit_controller(struct tegra_pcie_dw *pcie)
{
	tegra_pcie_downstream_dev_to_D0(pcie);
	dw_pcie_host_deinit(&pcie->pci.pp);
	tegra_pcie_dw_pme_turnoff(pcie);

	return __deinit_controller(pcie);
}

static int tegra_pcie_config_rp(struct tegra_pcie_dw *pcie)
{
	struct pcie_port *pp = &pcie->pci.pp;
	struct device *dev = pcie->dev;
	char *name;
	int ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = of_irq_get_byname(dev->of_node, "msi");
		if (!pp->msi_irq) {
			dev_err(dev, "Failed to get MSI interrupt\n");
			return -ENODEV;
		}
	}

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get runtime sync for PCIe dev: %d\n",
			ret);
		goto fail_pm_get_sync;
	}

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to configure sideband pins: %d\n", ret);
		goto fail_pm_get_sync;
	}

	tegra_pcie_init_controller(pcie);

	pcie->link_state = tegra_pcie_dw_link_up(&pcie->pci);
	if (!pcie->link_state) {
		ret = -ENOMEDIUM;
		goto fail_host_init;
	}

	name = devm_kasprintf(dev, GFP_KERNEL, "%pOFP", dev->of_node);
	if (!name) {
		ret = -ENOMEM;
		goto fail_host_init;
	}

	pcie->debugfs = debugfs_create_dir(name, NULL);
	init_debugfs(pcie);

	return ret;

fail_host_init:
	tegra_pcie_deinit_controller(pcie);
fail_pm_get_sync:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	return ret;
}

static void pex_ep_event_pex_rst_assert(struct tegra_pcie_dw *pcie)
{
	u32 val;
	int ret;

	if (pcie->ep_state == EP_STATE_DISABLED)
		return;

	/* Disable LTSSM */
	val = appl_readl(pcie, APPL_CTRL);
	val &= ~APPL_CTRL_LTSSM_EN;
	appl_writel(pcie, val, APPL_CTRL);

	ret = readl_poll_timeout(pcie->appl_base + APPL_DEBUG, val,
				 ((val & APPL_DEBUG_LTSSM_STATE_MASK) >>
				 APPL_DEBUG_LTSSM_STATE_SHIFT) ==
				 LTSSM_STATE_PRE_DETECT,
				 1, LTSSM_TIMEOUT);
	if (ret)
		dev_err(pcie->dev, "Failed to go Detect state: %d\n", ret);

	reset_control_assert(pcie->core_rst);

	tegra_pcie_disable_phy(pcie);

	reset_control_assert(pcie->core_apb_rst);

	clk_disable_unprepare(pcie->core_clk);

	pm_runtime_put_sync(pcie->dev);

	ret = tegra_pcie_bpmp_set_pll_state(pcie, false);
	if (ret)
		dev_err(pcie->dev, "Failed to turn off UPHY: %d\n", ret);

	pcie->ep_state = EP_STATE_DISABLED;
	dev_dbg(pcie->dev, "Uninitialization of endpoint is completed\n");
}

static void pex_ep_event_pex_rst_deassert(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_ep *ep = &pci->ep;
	struct device *dev = pcie->dev;
	u32 val;
	int ret;

	if (pcie->ep_state == EP_STATE_ENABLED)
		return;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get runtime sync for PCIe dev: %d\n",
			ret);
		return;
	}

	ret = tegra_pcie_bpmp_set_pll_state(pcie, true);
	if (ret) {
		dev_err(dev, "Failed to init UPHY for PCIe EP: %d\n", ret);
		goto fail_pll_init;
	}

	ret = clk_prepare_enable(pcie->core_clk);
	if (ret) {
		dev_err(dev, "Failed to enable core clock: %d\n", ret);
		goto fail_core_clk_enable;
	}

	ret = reset_control_deassert(pcie->core_apb_rst);
	if (ret) {
		dev_err(dev, "Failed to deassert core APB reset: %d\n", ret);
		goto fail_core_apb_rst;
	}

	ret = tegra_pcie_enable_phy(pcie);
	if (ret) {
		dev_err(dev, "Failed to enable PHY: %d\n", ret);
		goto fail_phy;
	}

	/* Clear any stale interrupt statuses */
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_0_0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_1);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_2);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_3);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_6);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_7);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_8_0);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_9);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_10);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_11);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_13);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_14);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_15);
	appl_writel(pcie, 0xFFFFFFFF, APPL_INTR_STATUS_L1_17);

	/* configure this core for EP mode operation */
	val = appl_readl(pcie, APPL_DM_TYPE);
	val &= ~APPL_DM_TYPE_MASK;
	val |= APPL_DM_TYPE_EP;
	appl_writel(pcie, val, APPL_DM_TYPE);

	appl_writel(pcie, 0x0, APPL_CFG_SLCG_OVERRIDE);

	val = appl_readl(pcie, APPL_CTRL);
	val |= APPL_CTRL_SYS_PRE_DET_STATE;
	val |= APPL_CTRL_HW_HOT_RST_EN;
	appl_writel(pcie, val, APPL_CTRL);

	val = appl_readl(pcie, APPL_CFG_MISC);
	val |= APPL_CFG_MISC_SLV_EP_MODE;
	val |= (APPL_CFG_MISC_ARCACHE_VAL << APPL_CFG_MISC_ARCACHE_SHIFT);
	appl_writel(pcie, val, APPL_CFG_MISC);

	val = appl_readl(pcie, APPL_PINMUX);
	val |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN;
	val |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE;
	appl_writel(pcie, val, APPL_PINMUX);

	appl_writel(pcie, pcie->dbi_res->start & APPL_CFG_BASE_ADDR_MASK,
		    APPL_CFG_BASE_ADDR);

	appl_writel(pcie, pcie->atu_dma_res->start &
		    APPL_CFG_IATU_DMA_BASE_ADDR_MASK,
		    APPL_CFG_IATU_DMA_BASE_ADDR);

	val = appl_readl(pcie, APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_SYS_INTR_EN;
	val |= APPL_INTR_EN_L0_0_LINK_STATE_INT_EN;
	val |= APPL_INTR_EN_L0_0_PCI_CMD_EN_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L0_0);

	val = appl_readl(pcie, APPL_INTR_EN_L1_0_0);
	val |= APPL_INTR_EN_L1_0_0_HOT_RESET_DONE_INT_EN;
	val |= APPL_INTR_EN_L1_0_0_RDLH_LINK_UP_INT_EN;
	appl_writel(pcie, val, APPL_INTR_EN_L1_0_0);

	reset_control_deassert(pcie->core_rst);

	if (pcie->update_fc_fixup) {
		val = dw_pcie_readl_dbi(pci, CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF);
		val |= 0x1 << CFG_TIMER_CTRL_ACK_NAK_SHIFT;
		dw_pcie_writel_dbi(pci, CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF, val);
	}

	config_gen3_gen4_eq_presets(pcie);

	init_host_aspm(pcie);

	/* Disable ASPM-L1SS advertisement if there is no CLKREQ routing */
	if (!pcie->supports_clkreq) {
		disable_aspm_l11(pcie);
		disable_aspm_l12(pcie);
	}

	val = dw_pcie_readl_dbi(pci, GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL;
	dw_pcie_writel_dbi(pci, GEN3_RELATED_OFF, val);

	pcie->pcie_cap_base = dw_pcie_find_capability(&pcie->pci,
						      PCI_CAP_ID_EXP);
	clk_set_rate(pcie->core_clk, GEN4_CORE_CLK_FREQ);

	val = (ep->msi_mem_phys & MSIX_ADDR_MATCH_LOW_OFF_MASK);
	val |= MSIX_ADDR_MATCH_LOW_OFF_EN;
	dw_pcie_writel_dbi(pci, MSIX_ADDR_MATCH_LOW_OFF, val);
	val = (lower_32_bits(ep->msi_mem_phys) & MSIX_ADDR_MATCH_HIGH_OFF_MASK);
	dw_pcie_writel_dbi(pci, MSIX_ADDR_MATCH_HIGH_OFF, val);

	ret = dw_pcie_ep_init_complete(ep);
	if (ret) {
		dev_err(dev, "Failed to complete initialization: %d\n", ret);
		goto fail_init_complete;
	}

	dw_pcie_ep_init_notify(ep);

	/* Enable LTSSM */
	val = appl_readl(pcie, APPL_CTRL);
	val |= APPL_CTRL_LTSSM_EN;
	appl_writel(pcie, val, APPL_CTRL);

	pcie->ep_state = EP_STATE_ENABLED;
	dev_dbg(dev, "Initialization of endpoint is completed\n");

	return;

fail_init_complete:
	reset_control_assert(pcie->core_rst);
	tegra_pcie_disable_phy(pcie);
fail_phy:
	reset_control_assert(pcie->core_apb_rst);
fail_core_apb_rst:
	clk_disable_unprepare(pcie->core_clk);
fail_core_clk_enable:
	tegra_pcie_bpmp_set_pll_state(pcie, false);
fail_pll_init:
	pm_runtime_put_sync(dev);
}

static irqreturn_t tegra_pcie_ep_pex_rst_irq(int irq, void *arg)
{
	struct tegra_pcie_dw *pcie = arg;

	if (gpiod_get_value(pcie->pex_rst_gpiod))
		pex_ep_event_pex_rst_assert(pcie);
	else
		pex_ep_event_pex_rst_deassert(pcie);

	return IRQ_HANDLED;
}

static int tegra_pcie_ep_raise_legacy_irq(struct tegra_pcie_dw *pcie, u16 irq)
{
	/* Tegra194 supports only INTA */
	if (irq > 1)
		return -EINVAL;

	appl_writel(pcie, 1, APPL_LEGACY_INTX);
	usleep_range(1000, 2000);
	appl_writel(pcie, 0, APPL_LEGACY_INTX);
	return 0;
}

static int tegra_pcie_ep_raise_msi_irq(struct tegra_pcie_dw *pcie, u16 irq)
{
	if (unlikely(irq > 31))
		return -EINVAL;

	appl_writel(pcie, (1 << irq), APPL_MSI_CTRL_1);

	return 0;
}

static int tegra_pcie_ep_raise_msix_irq(struct tegra_pcie_dw *pcie, u16 irq)
{
	struct dw_pcie_ep *ep = &pcie->pci.ep;

	writel(irq, ep->msi_mem);

	return 0;
}

static int tegra_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				   enum pci_epc_irq_type type,
				   u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct tegra_pcie_dw *pcie = to_tegra_pcie(pci);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return tegra_pcie_ep_raise_legacy_irq(pcie, interrupt_num);

	case PCI_EPC_IRQ_MSI:
		return tegra_pcie_ep_raise_msi_irq(pcie, interrupt_num);

	case PCI_EPC_IRQ_MSIX:
		return tegra_pcie_ep_raise_msix_irq(pcie, interrupt_num);

	default:
		dev_err(pci->dev, "Unknown IRQ type\n");
		return -EPERM;
	}

	return 0;
}

static const struct pci_epc_features tegra_pcie_epc_features = {
	.linkup_notifier = true,
	.core_init_notifier = true,
	.msi_capable = false,
	.msix_capable = false,
	.reserved_bar = 1 << BAR_2 | 1 << BAR_3 | 1 << BAR_4 | 1 << BAR_5,
	.bar_fixed_64bit = 1 << BAR_0,
	.bar_fixed_size[0] = SZ_1M,
};

static const struct pci_epc_features*
tegra_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	return &tegra_pcie_epc_features;
}

static struct dw_pcie_ep_ops pcie_ep_ops = {
	.raise_irq = tegra_pcie_ep_raise_irq,
	.get_features = tegra_pcie_ep_get_features,
};

static int tegra_pcie_config_ep(struct tegra_pcie_dw *pcie,
				struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct device *dev = pcie->dev;
	struct dw_pcie_ep *ep;
	struct resource *res;
	char *name;
	int ret;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);
	ep->page_size = SZ_64K;

	ret = gpiod_set_debounce(pcie->pex_rst_gpiod, PERST_DEBOUNCE_TIME);
	if (ret < 0) {
		dev_err(dev, "Failed to set PERST GPIO debounce time: %d\n",
			ret);
		return ret;
	}

	ret = gpiod_to_irq(pcie->pex_rst_gpiod);
	if (ret < 0) {
		dev_err(dev, "Failed to get IRQ for PERST GPIO: %d\n", ret);
		return ret;
	}
	pcie->pex_rst_irq = (unsigned int)ret;

	name = devm_kasprintf(dev, GFP_KERNEL, "tegra_pcie_%u_pex_rst_irq",
			      pcie->cid);
	if (!name) {
		dev_err(dev, "Failed to create PERST IRQ string\n");
		return -ENOMEM;
	}

	irq_set_status_flags(pcie->pex_rst_irq, IRQ_NOAUTOEN);

	pcie->ep_state = EP_STATE_DISABLED;

	ret = devm_request_threaded_irq(dev, pcie->pex_rst_irq, NULL,
					tegra_pcie_ep_pex_rst_irq,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					name, (void *)pcie);
	if (ret < 0) {
		dev_err(dev, "Failed to request IRQ for PERST: %d\n", ret);
		return ret;
	}

	name = devm_kasprintf(dev, GFP_KERNEL, "tegra_pcie_%u_ep_work",
			      pcie->cid);
	if (!name) {
		dev_err(dev, "Failed to create PCIe EP work thread string\n");
		return -ENOMEM;
	}

	pm_runtime_enable(dev);

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "Failed to initialize DWC Endpoint subsystem: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int tegra_pcie_dw_probe(struct platform_device *pdev)
{
	const struct tegra_pcie_dw_of_data *data;
	struct device *dev = &pdev->dev;
	struct resource *atu_dma_res;
	struct tegra_pcie_dw *pcie;
	struct resource *dbi_res;
	struct pcie_port *pp;
	struct dw_pcie *pci;
	struct phy **phys;
	char *name;
	int ret;
	u32 i;

	data = of_device_get_match_data(dev);

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = &pcie->pci;
	pci->dev = &pdev->dev;
	pci->ops = &tegra_dw_pcie_ops;
	pci->n_fts[0] = N_FTS_VAL;
	pci->n_fts[1] = FTS_VAL;

	pp = &pci->pp;
	pcie->dev = &pdev->dev;
	pcie->mode = (enum dw_pcie_device_mode)data->mode;

	ret = tegra_pcie_dw_parse_dt(pcie);
	if (ret < 0) {
		const char *level = KERN_ERR;

		if (ret == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, dev,
			   dev_fmt("Failed to parse device tree: %d\n"),
			   ret);
		return ret;
	}

	ret = tegra_pcie_get_slot_regulators(pcie);
	if (ret < 0) {
		const char *level = KERN_ERR;

		if (ret == -EPROBE_DEFER)
			level = KERN_DEBUG;

		dev_printk(level, dev,
			   dev_fmt("Failed to get slot regulators: %d\n"),
			   ret);
		return ret;
	}

	if (pcie->pex_refclk_sel_gpiod)
		gpiod_set_value(pcie->pex_refclk_sel_gpiod, 1);

	pcie->pex_ctl_supply = devm_regulator_get(dev, "vddio-pex-ctl");
	if (IS_ERR(pcie->pex_ctl_supply)) {
		ret = PTR_ERR(pcie->pex_ctl_supply);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get regulator: %ld\n",
				PTR_ERR(pcie->pex_ctl_supply));
		return ret;
	}

	pcie->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(pcie->core_clk)) {
		dev_err(dev, "Failed to get core clock: %ld\n",
			PTR_ERR(pcie->core_clk));
		return PTR_ERR(pcie->core_clk);
	}

	pcie->appl_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						      "appl");
	if (!pcie->appl_res) {
		dev_err(dev, "Failed to find \"appl\" region\n");
		return -ENODEV;
	}

	pcie->appl_base = devm_ioremap_resource(dev, pcie->appl_res);
	if (IS_ERR(pcie->appl_base))
		return PTR_ERR(pcie->appl_base);

	pcie->core_apb_rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(pcie->core_apb_rst)) {
		dev_err(dev, "Failed to get APB reset: %ld\n",
			PTR_ERR(pcie->core_apb_rst));
		return PTR_ERR(pcie->core_apb_rst);
	}

	phys = devm_kcalloc(dev, pcie->phy_count, sizeof(*phys), GFP_KERNEL);
	if (!phys)
		return -ENOMEM;

	for (i = 0; i < pcie->phy_count; i++) {
		name = kasprintf(GFP_KERNEL, "p2u-%u", i);
		if (!name) {
			dev_err(dev, "Failed to create P2U string\n");
			return -ENOMEM;
		}
		phys[i] = devm_phy_get(dev, name);
		kfree(name);
		if (IS_ERR(phys[i])) {
			ret = PTR_ERR(phys[i]);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to get PHY: %d\n", ret);
			return ret;
		}
	}

	pcie->phys = phys;

	dbi_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	if (!dbi_res) {
		dev_err(dev, "Failed to find \"dbi\" region\n");
		return -ENODEV;
	}
	pcie->dbi_res = dbi_res;

	pci->dbi_base = devm_ioremap_resource(dev, dbi_res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	/* Tegra HW locates DBI2 at a fixed offset from DBI */
	pci->dbi_base2 = pci->dbi_base + 0x1000;

	atu_dma_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "atu_dma");
	if (!atu_dma_res) {
		dev_err(dev, "Failed to find \"atu_dma\" region\n");
		return -ENODEV;
	}
	pcie->atu_dma_res = atu_dma_res;

	pci->atu_base = devm_ioremap_resource(dev, atu_dma_res);
	if (IS_ERR(pci->atu_base))
		return PTR_ERR(pci->atu_base);

	pcie->core_rst = devm_reset_control_get(dev, "core");
	if (IS_ERR(pcie->core_rst)) {
		dev_err(dev, "Failed to get core reset: %ld\n",
			PTR_ERR(pcie->core_rst));
		return PTR_ERR(pcie->core_rst);
	}

	pp->irq = platform_get_irq_byname(pdev, "intr");
	if (pp->irq < 0)
		return pp->irq;

	pcie->bpmp = tegra_bpmp_get(dev);
	if (IS_ERR(pcie->bpmp))
		return PTR_ERR(pcie->bpmp);

	platform_set_drvdata(pdev, pcie);

	switch (pcie->mode) {
	case DW_PCIE_RC_TYPE:
		ret = devm_request_irq(dev, pp->irq, tegra_pcie_rp_irq_handler,
				       IRQF_SHARED, "tegra-pcie-intr", pcie);
		if (ret) {
			dev_err(dev, "Failed to request IRQ %d: %d\n", pp->irq,
				ret);
			goto fail;
		}

		ret = tegra_pcie_config_rp(pcie);
		if (ret && ret != -ENOMEDIUM)
			goto fail;
		else
			return 0;
		break;

	case DW_PCIE_EP_TYPE:
		ret = devm_request_threaded_irq(dev, pp->irq,
						tegra_pcie_ep_hard_irq,
						tegra_pcie_ep_irq_thread,
						IRQF_SHARED | IRQF_ONESHOT,
						"tegra-pcie-ep-intr", pcie);
		if (ret) {
			dev_err(dev, "Failed to request IRQ %d: %d\n", pp->irq,
				ret);
			goto fail;
		}

		ret = tegra_pcie_config_ep(pcie, pdev);
		if (ret < 0)
			goto fail;
		break;

	default:
		dev_err(dev, "Invalid PCIe device type %d\n", pcie->mode);
	}

fail:
	tegra_bpmp_put(pcie->bpmp);
	return ret;
}

static int tegra_pcie_dw_remove(struct platform_device *pdev)
{
	struct tegra_pcie_dw *pcie = platform_get_drvdata(pdev);

	if (!pcie->link_state)
		return 0;

	debugfs_remove_recursive(pcie->debugfs);
	tegra_pcie_deinit_controller(pcie);
	pm_runtime_put_sync(pcie->dev);
	pm_runtime_disable(pcie->dev);
	tegra_bpmp_put(pcie->bpmp);
	if (pcie->pex_refclk_sel_gpiod)
		gpiod_set_value(pcie->pex_refclk_sel_gpiod, 0);

	return 0;
}

static int tegra_pcie_dw_suspend_late(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	u32 val;

	if (!pcie->link_state)
		return 0;

	/* Enable HW_HOT_RST mode */
	val = appl_readl(pcie, APPL_CTRL);
	val &= ~(APPL_CTRL_HW_HOT_RST_MODE_MASK <<
		 APPL_CTRL_HW_HOT_RST_MODE_SHIFT);
	val |= APPL_CTRL_HW_HOT_RST_EN;
	appl_writel(pcie, val, APPL_CTRL);

	return 0;
}

static int tegra_pcie_dw_suspend_noirq(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);

	if (!pcie->link_state)
		return 0;

	/* Save MSI interrupt vector */
	pcie->msi_ctrl_int = dw_pcie_readl_dbi(&pcie->pci,
					       PORT_LOGIC_MSI_CTRL_INT_0_EN);
	tegra_pcie_downstream_dev_to_D0(pcie);
	tegra_pcie_dw_pme_turnoff(pcie);

	return __deinit_controller(pcie);
}

static int tegra_pcie_dw_resume_noirq(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	int ret;

	if (!pcie->link_state)
		return 0;

	ret = tegra_pcie_config_controller(pcie, true);
	if (ret < 0)
		return ret;

	ret = tegra_pcie_dw_host_init(&pcie->pci.pp);
	if (ret < 0) {
		dev_err(dev, "Failed to init host: %d\n", ret);
		goto fail_host_init;
	}

	/* Restore MSI interrupt vector */
	dw_pcie_writel_dbi(&pcie->pci, PORT_LOGIC_MSI_CTRL_INT_0_EN,
			   pcie->msi_ctrl_int);

	return 0;

fail_host_init:
	return __deinit_controller(pcie);
}

static int tegra_pcie_dw_resume_early(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	u32 val;

	if (!pcie->link_state)
		return 0;

	/* Disable HW_HOT_RST mode */
	val = appl_readl(pcie, APPL_CTRL);
	val &= ~(APPL_CTRL_HW_HOT_RST_MODE_MASK <<
		 APPL_CTRL_HW_HOT_RST_MODE_SHIFT);
	val |= APPL_CTRL_HW_HOT_RST_MODE_IMDT_RST <<
	       APPL_CTRL_HW_HOT_RST_MODE_SHIFT;
	val &= ~APPL_CTRL_HW_HOT_RST_EN;
	appl_writel(pcie, val, APPL_CTRL);

	return 0;
}

static void tegra_pcie_dw_shutdown(struct platform_device *pdev)
{
	struct tegra_pcie_dw *pcie = platform_get_drvdata(pdev);

	if (!pcie->link_state)
		return;

	debugfs_remove_recursive(pcie->debugfs);
	tegra_pcie_downstream_dev_to_D0(pcie);

	disable_irq(pcie->pci.pp.irq);
	if (IS_ENABLED(CONFIG_PCI_MSI))
		disable_irq(pcie->pci.pp.msi_irq);

	tegra_pcie_dw_pme_turnoff(pcie);
	__deinit_controller(pcie);
}

static const struct tegra_pcie_dw_of_data tegra_pcie_dw_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct tegra_pcie_dw_of_data tegra_pcie_dw_ep_of_data = {
	.mode = DW_PCIE_EP_TYPE,
};

static const struct of_device_id tegra_pcie_dw_of_match[] = {
	{
		.compatible = "nvidia,tegra194-pcie",
		.data = &tegra_pcie_dw_rc_of_data,
	},
	{
		.compatible = "nvidia,tegra194-pcie-ep",
		.data = &tegra_pcie_dw_ep_of_data,
	},
	{},
};

static const struct dev_pm_ops tegra_pcie_dw_pm_ops = {
	.suspend_late = tegra_pcie_dw_suspend_late,
	.suspend_noirq = tegra_pcie_dw_suspend_noirq,
	.resume_noirq = tegra_pcie_dw_resume_noirq,
	.resume_early = tegra_pcie_dw_resume_early,
};

static struct platform_driver tegra_pcie_dw_driver = {
	.probe = tegra_pcie_dw_probe,
	.remove = tegra_pcie_dw_remove,
	.shutdown = tegra_pcie_dw_shutdown,
	.driver = {
		.name	= "tegra194-pcie",
		.pm = &tegra_pcie_dw_pm_ops,
		.of_match_table = tegra_pcie_dw_of_match,
	},
};
module_platform_driver(tegra_pcie_dw_driver);

MODULE_DEVICE_TABLE(of, tegra_pcie_dw_of_match);

MODULE_AUTHOR("Vidya Sagar <vidyas@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA PCIe host controller driver");
MODULE_LICENSE("GPL v2");
