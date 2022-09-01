// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Genesys Logic, Inc.
 *
 * Authors: Ben Chuang <ben.chuang@genesyslogic.com.tw>
 *
 * Version: v0.9.0 (2019-08-08)
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/pci.h>
#include <linux/mmc/mmc.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/iopoll.h>
#include "sdhci.h"
#include "sdhci-pci.h"
#include "cqhci.h"

/*  Genesys Logic extra registers */
#define SDHCI_GLI_9750_WT         0x800
#define   SDHCI_GLI_9750_WT_EN      BIT(0)
#define   GLI_9750_WT_EN_ON	    0x1
#define   GLI_9750_WT_EN_OFF	    0x0

#define SDHCI_GLI_9750_CFG2          0x848
#define   SDHCI_GLI_9750_CFG2_L1DLY    GENMASK(28, 24)
#define   GLI_9750_CFG2_L1DLY_VALUE    0x1F

#define SDHCI_GLI_9750_DRIVING      0x860
#define   SDHCI_GLI_9750_DRIVING_1    GENMASK(11, 0)
#define   SDHCI_GLI_9750_DRIVING_2    GENMASK(27, 26)
#define   GLI_9750_DRIVING_1_VALUE    0xFFF
#define   GLI_9750_DRIVING_2_VALUE    0x3
#define   SDHCI_GLI_9750_SEL_1        BIT(29)
#define   SDHCI_GLI_9750_SEL_2        BIT(31)
#define   SDHCI_GLI_9750_ALL_RST      (BIT(24)|BIT(25)|BIT(28)|BIT(30))

#define SDHCI_GLI_9750_PLL	      0x864
#define   SDHCI_GLI_9750_PLL_LDIV       GENMASK(9, 0)
#define   SDHCI_GLI_9750_PLL_PDIV       GENMASK(14, 12)
#define   SDHCI_GLI_9750_PLL_DIR        BIT(15)
#define   SDHCI_GLI_9750_PLL_TX2_INV    BIT(23)
#define   SDHCI_GLI_9750_PLL_TX2_DLY    GENMASK(22, 20)
#define   GLI_9750_PLL_TX2_INV_VALUE    0x1
#define   GLI_9750_PLL_TX2_DLY_VALUE    0x0
#define   SDHCI_GLI_9750_PLLSSC_STEP    GENMASK(28, 24)
#define   SDHCI_GLI_9750_PLLSSC_EN      BIT(31)

#define SDHCI_GLI_9750_PLLSSC        0x86C
#define   SDHCI_GLI_9750_PLLSSC_PPM    GENMASK(31, 16)

#define SDHCI_GLI_9750_SW_CTRL      0x874
#define   SDHCI_GLI_9750_SW_CTRL_4    GENMASK(7, 6)
#define   GLI_9750_SW_CTRL_4_VALUE    0x3

#define SDHCI_GLI_9750_MISC            0x878
#define   SDHCI_GLI_9750_MISC_TX1_INV    BIT(2)
#define   SDHCI_GLI_9750_MISC_RX_INV     BIT(3)
#define   SDHCI_GLI_9750_MISC_TX1_DLY    GENMASK(6, 4)
#define   GLI_9750_MISC_TX1_INV_VALUE    0x0
#define   GLI_9750_MISC_RX_INV_ON        0x1
#define   GLI_9750_MISC_RX_INV_OFF       0x0
#define   GLI_9750_MISC_RX_INV_VALUE     GLI_9750_MISC_RX_INV_OFF
#define   GLI_9750_MISC_TX1_DLY_VALUE    0x5
#define   SDHCI_GLI_9750_MISC_SSC_OFF    BIT(26)

#define SDHCI_GLI_9750_TUNING_CONTROL	          0x540
#define   SDHCI_GLI_9750_TUNING_CONTROL_EN          BIT(4)
#define   GLI_9750_TUNING_CONTROL_EN_ON             0x1
#define   GLI_9750_TUNING_CONTROL_EN_OFF            0x0
#define   SDHCI_GLI_9750_TUNING_CONTROL_GLITCH_1    BIT(16)
#define   SDHCI_GLI_9750_TUNING_CONTROL_GLITCH_2    GENMASK(20, 19)
#define   GLI_9750_TUNING_CONTROL_GLITCH_1_VALUE    0x1
#define   GLI_9750_TUNING_CONTROL_GLITCH_2_VALUE    0x2

#define SDHCI_GLI_9750_TUNING_PARAMETERS           0x544
#define   SDHCI_GLI_9750_TUNING_PARAMETERS_RX_DLY    GENMASK(2, 0)
#define   GLI_9750_TUNING_PARAMETERS_RX_DLY_VALUE    0x1

#define SDHCI_GLI_9763E_CTRL_HS400  0x7

#define SDHCI_GLI_9763E_HS400_ES_REG      0x52C
#define   SDHCI_GLI_9763E_HS400_ES_BIT      BIT(8)

#define PCIE_GLI_9763E_VHS	 0x884
#define   GLI_9763E_VHS_REV	   GENMASK(19, 16)
#define   GLI_9763E_VHS_REV_R      0x0
#define   GLI_9763E_VHS_REV_M      0x1
#define   GLI_9763E_VHS_REV_W      0x2
#define PCIE_GLI_9763E_MB	 0x888
#define   GLI_9763E_MB_CMDQ_OFF	   BIT(19)
#define   GLI_9763E_MB_ERP_ON      BIT(7)
#define PCIE_GLI_9763E_SCR	 0x8E0
#define   GLI_9763E_SCR_AXI_REQ	   BIT(9)

#define PCIE_GLI_9763E_CFG       0x8A0
#define   GLI_9763E_CFG_LPSN_DIS   BIT(12)

#define PCIE_GLI_9763E_CFG2      0x8A4
#define   GLI_9763E_CFG2_L1DLY     GENMASK(28, 19)
#define   GLI_9763E_CFG2_L1DLY_MID 0x54

#define PCIE_GLI_9763E_MMC_CTRL  0x960
#define   GLI_9763E_HS400_SLOW     BIT(3)

#define PCIE_GLI_9763E_CLKRXDLY  0x934
#define   GLI_9763E_HS400_RXDLY    GENMASK(31, 28)
#define   GLI_9763E_HS400_RXDLY_5  0x5

#define SDHCI_GLI_9763E_CQE_BASE_ADDR	 0x200
#define GLI_9763E_CQE_TRNS_MODE	   (SDHCI_TRNS_MULTI | \
				    SDHCI_TRNS_BLK_CNT_EN | \
				    SDHCI_TRNS_DMA)

#define PCI_GLI_9755_WT       0x800
#define   PCI_GLI_9755_WT_EN    BIT(0)
#define   GLI_9755_WT_EN_ON     0x1
#define   GLI_9755_WT_EN_OFF    0x0

#define PCI_GLI_9755_PECONF   0x44
#define   PCI_GLI_9755_LFCLK    GENMASK(14, 12)
#define   PCI_GLI_9755_DMACLK   BIT(29)
#define   PCI_GLI_9755_INVERT_CD  BIT(30)
#define   PCI_GLI_9755_INVERT_WP  BIT(31)

#define PCI_GLI_9755_CFG2          0x48
#define   PCI_GLI_9755_CFG2_L1DLY    GENMASK(28, 24)
#define   GLI_9755_CFG2_L1DLY_VALUE  0x1F

#define PCI_GLI_9755_PLL            0x64
#define   PCI_GLI_9755_PLL_LDIV       GENMASK(9, 0)
#define   PCI_GLI_9755_PLL_PDIV       GENMASK(14, 12)
#define   PCI_GLI_9755_PLL_DIR        BIT(15)
#define   PCI_GLI_9755_PLLSSC_STEP    GENMASK(28, 24)
#define   PCI_GLI_9755_PLLSSC_EN      BIT(31)

#define PCI_GLI_9755_PLLSSC        0x68
#define   PCI_GLI_9755_PLLSSC_PPM    GENMASK(15, 0)

#define PCI_GLI_9755_SerDes  0x70
#define PCI_GLI_9755_SCP_DIS   BIT(19)

#define PCI_GLI_9755_MISC	    0x78
#define   PCI_GLI_9755_MISC_SSC_OFF    BIT(26)

#define PCI_GLI_9755_PM_CTRL     0xFC
#define   PCI_GLI_9755_PM_STATE    GENMASK(1, 0)

#define GLI_MAX_TUNING_LOOP 40

/* Genesys Logic chipset */
static inline void gl9750_wt_on(struct sdhci_host *host)
{
	u32 wt_value;
	u32 wt_enable;

	wt_value = sdhci_readl(host, SDHCI_GLI_9750_WT);
	wt_enable = FIELD_GET(SDHCI_GLI_9750_WT_EN, wt_value);

	if (wt_enable == GLI_9750_WT_EN_ON)
		return;

	wt_value &= ~SDHCI_GLI_9750_WT_EN;
	wt_value |= FIELD_PREP(SDHCI_GLI_9750_WT_EN, GLI_9750_WT_EN_ON);

	sdhci_writel(host, wt_value, SDHCI_GLI_9750_WT);
}

static inline void gl9750_wt_off(struct sdhci_host *host)
{
	u32 wt_value;
	u32 wt_enable;

	wt_value = sdhci_readl(host, SDHCI_GLI_9750_WT);
	wt_enable = FIELD_GET(SDHCI_GLI_9750_WT_EN, wt_value);

	if (wt_enable == GLI_9750_WT_EN_OFF)
		return;

	wt_value &= ~SDHCI_GLI_9750_WT_EN;
	wt_value |= FIELD_PREP(SDHCI_GLI_9750_WT_EN, GLI_9750_WT_EN_OFF);

	sdhci_writel(host, wt_value, SDHCI_GLI_9750_WT);
}

static void gli_set_9750(struct sdhci_host *host)
{
	u32 driving_value;
	u32 pll_value;
	u32 sw_ctrl_value;
	u32 misc_value;
	u32 parameter_value;
	u32 control_value;
	u16 ctrl2;

	gl9750_wt_on(host);

	driving_value = sdhci_readl(host, SDHCI_GLI_9750_DRIVING);
	pll_value = sdhci_readl(host, SDHCI_GLI_9750_PLL);
	sw_ctrl_value = sdhci_readl(host, SDHCI_GLI_9750_SW_CTRL);
	misc_value = sdhci_readl(host, SDHCI_GLI_9750_MISC);
	parameter_value = sdhci_readl(host, SDHCI_GLI_9750_TUNING_PARAMETERS);
	control_value = sdhci_readl(host, SDHCI_GLI_9750_TUNING_CONTROL);

	driving_value &= ~(SDHCI_GLI_9750_DRIVING_1);
	driving_value &= ~(SDHCI_GLI_9750_DRIVING_2);
	driving_value |= FIELD_PREP(SDHCI_GLI_9750_DRIVING_1,
				    GLI_9750_DRIVING_1_VALUE);
	driving_value |= FIELD_PREP(SDHCI_GLI_9750_DRIVING_2,
				    GLI_9750_DRIVING_2_VALUE);
	driving_value &= ~(SDHCI_GLI_9750_SEL_1|SDHCI_GLI_9750_SEL_2|SDHCI_GLI_9750_ALL_RST);
	driving_value |= SDHCI_GLI_9750_SEL_2;
	sdhci_writel(host, driving_value, SDHCI_GLI_9750_DRIVING);

	sw_ctrl_value &= ~SDHCI_GLI_9750_SW_CTRL_4;
	sw_ctrl_value |= FIELD_PREP(SDHCI_GLI_9750_SW_CTRL_4,
				    GLI_9750_SW_CTRL_4_VALUE);
	sdhci_writel(host, sw_ctrl_value, SDHCI_GLI_9750_SW_CTRL);

	/* reset the tuning flow after reinit and before starting tuning */
	pll_value &= ~SDHCI_GLI_9750_PLL_TX2_INV;
	pll_value &= ~SDHCI_GLI_9750_PLL_TX2_DLY;
	pll_value |= FIELD_PREP(SDHCI_GLI_9750_PLL_TX2_INV,
				GLI_9750_PLL_TX2_INV_VALUE);
	pll_value |= FIELD_PREP(SDHCI_GLI_9750_PLL_TX2_DLY,
				GLI_9750_PLL_TX2_DLY_VALUE);

	misc_value &= ~SDHCI_GLI_9750_MISC_TX1_INV;
	misc_value &= ~SDHCI_GLI_9750_MISC_RX_INV;
	misc_value &= ~SDHCI_GLI_9750_MISC_TX1_DLY;
	misc_value |= FIELD_PREP(SDHCI_GLI_9750_MISC_TX1_INV,
				 GLI_9750_MISC_TX1_INV_VALUE);
	misc_value |= FIELD_PREP(SDHCI_GLI_9750_MISC_RX_INV,
				 GLI_9750_MISC_RX_INV_VALUE);
	misc_value |= FIELD_PREP(SDHCI_GLI_9750_MISC_TX1_DLY,
				 GLI_9750_MISC_TX1_DLY_VALUE);

	parameter_value &= ~SDHCI_GLI_9750_TUNING_PARAMETERS_RX_DLY;
	parameter_value |= FIELD_PREP(SDHCI_GLI_9750_TUNING_PARAMETERS_RX_DLY,
				      GLI_9750_TUNING_PARAMETERS_RX_DLY_VALUE);

	control_value &= ~SDHCI_GLI_9750_TUNING_CONTROL_GLITCH_1;
	control_value &= ~SDHCI_GLI_9750_TUNING_CONTROL_GLITCH_2;
	control_value |= FIELD_PREP(SDHCI_GLI_9750_TUNING_CONTROL_GLITCH_1,
				    GLI_9750_TUNING_CONTROL_GLITCH_1_VALUE);
	control_value |= FIELD_PREP(SDHCI_GLI_9750_TUNING_CONTROL_GLITCH_2,
				    GLI_9750_TUNING_CONTROL_GLITCH_2_VALUE);

	sdhci_writel(host, pll_value, SDHCI_GLI_9750_PLL);
	sdhci_writel(host, misc_value, SDHCI_GLI_9750_MISC);

	/* disable tuned clk */
	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 &= ~SDHCI_CTRL_TUNED_CLK;
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);

	/* enable tuning parameters control */
	control_value &= ~SDHCI_GLI_9750_TUNING_CONTROL_EN;
	control_value |= FIELD_PREP(SDHCI_GLI_9750_TUNING_CONTROL_EN,
				    GLI_9750_TUNING_CONTROL_EN_ON);
	sdhci_writel(host, control_value, SDHCI_GLI_9750_TUNING_CONTROL);

	/* write tuning parameters */
	sdhci_writel(host, parameter_value, SDHCI_GLI_9750_TUNING_PARAMETERS);

	/* disable tuning parameters control */
	control_value &= ~SDHCI_GLI_9750_TUNING_CONTROL_EN;
	control_value |= FIELD_PREP(SDHCI_GLI_9750_TUNING_CONTROL_EN,
				    GLI_9750_TUNING_CONTROL_EN_OFF);
	sdhci_writel(host, control_value, SDHCI_GLI_9750_TUNING_CONTROL);

	/* clear tuned clk */
	ctrl2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl2 &= ~SDHCI_CTRL_TUNED_CLK;
	sdhci_writew(host, ctrl2, SDHCI_HOST_CONTROL2);

	gl9750_wt_off(host);
}

static void gli_set_9750_rx_inv(struct sdhci_host *host, bool b)
{
	u32 misc_value;

	gl9750_wt_on(host);

	misc_value = sdhci_readl(host, SDHCI_GLI_9750_MISC);
	misc_value &= ~SDHCI_GLI_9750_MISC_RX_INV;
	if (b) {
		misc_value |= FIELD_PREP(SDHCI_GLI_9750_MISC_RX_INV,
					 GLI_9750_MISC_RX_INV_ON);
	} else {
		misc_value |= FIELD_PREP(SDHCI_GLI_9750_MISC_RX_INV,
					 GLI_9750_MISC_RX_INV_OFF);
	}
	sdhci_writel(host, misc_value, SDHCI_GLI_9750_MISC);

	gl9750_wt_off(host);
}

static int __sdhci_execute_tuning_9750(struct sdhci_host *host, u32 opcode)
{
	int i;
	int rx_inv;

	for (rx_inv = 0; rx_inv < 2; rx_inv++) {
		gli_set_9750_rx_inv(host, !!rx_inv);
		sdhci_start_tuning(host);

		for (i = 0; i < GLI_MAX_TUNING_LOOP; i++) {
			u16 ctrl;

			sdhci_send_tuning(host, opcode);

			if (!host->tuning_done) {
				sdhci_abort_tuning(host, opcode);
				break;
			}

			ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			if (!(ctrl & SDHCI_CTRL_EXEC_TUNING)) {
				if (ctrl & SDHCI_CTRL_TUNED_CLK)
					return 0; /* Success! */
				break;
			}
		}
	}
	if (!host->tuning_done) {
		pr_info("%s: Tuning timeout, falling back to fixed sampling clock\n",
			mmc_hostname(host->mmc));
		return -ETIMEDOUT;
	}

	pr_info("%s: Tuning failed, falling back to fixed sampling clock\n",
		mmc_hostname(host->mmc));
	sdhci_reset_tuning(host);

	return -EAGAIN;
}

static int gl9750_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	host->mmc->retune_period = 0;
	if (host->tuning_mode == SDHCI_TUNING_MODE_1)
		host->mmc->retune_period = host->tuning_count;

	gli_set_9750(host);
	host->tuning_err = __sdhci_execute_tuning_9750(host, opcode);
	sdhci_end_tuning(host);

	return 0;
}

static void gl9750_disable_ssc_pll(struct sdhci_host *host)
{
	u32 pll;

	gl9750_wt_on(host);
	pll = sdhci_readl(host, SDHCI_GLI_9750_PLL);
	pll &= ~(SDHCI_GLI_9750_PLL_DIR | SDHCI_GLI_9750_PLLSSC_EN);
	sdhci_writel(host, pll, SDHCI_GLI_9750_PLL);
	gl9750_wt_off(host);
}

static void gl9750_set_pll(struct sdhci_host *host, u8 dir, u16 ldiv, u8 pdiv)
{
	u32 pll;

	gl9750_wt_on(host);
	pll = sdhci_readl(host, SDHCI_GLI_9750_PLL);
	pll &= ~(SDHCI_GLI_9750_PLL_LDIV |
		 SDHCI_GLI_9750_PLL_PDIV |
		 SDHCI_GLI_9750_PLL_DIR);
	pll |= FIELD_PREP(SDHCI_GLI_9750_PLL_LDIV, ldiv) |
	       FIELD_PREP(SDHCI_GLI_9750_PLL_PDIV, pdiv) |
	       FIELD_PREP(SDHCI_GLI_9750_PLL_DIR, dir);
	sdhci_writel(host, pll, SDHCI_GLI_9750_PLL);
	gl9750_wt_off(host);

	/* wait for pll stable */
	mdelay(1);
}

static bool gl9750_ssc_enable(struct sdhci_host *host)
{
	u32 misc;
	u8 off;

	gl9750_wt_on(host);
	misc = sdhci_readl(host, SDHCI_GLI_9750_MISC);
	off = FIELD_GET(SDHCI_GLI_9750_MISC_SSC_OFF, misc);
	gl9750_wt_off(host);

	return !off;
}

static void gl9750_set_ssc(struct sdhci_host *host, u8 enable, u8 step, u16 ppm)
{
	u32 pll;
	u32 ssc;

	gl9750_wt_on(host);
	pll = sdhci_readl(host, SDHCI_GLI_9750_PLL);
	ssc = sdhci_readl(host, SDHCI_GLI_9750_PLLSSC);
	pll &= ~(SDHCI_GLI_9750_PLLSSC_STEP |
		 SDHCI_GLI_9750_PLLSSC_EN);
	ssc &= ~SDHCI_GLI_9750_PLLSSC_PPM;
	pll |= FIELD_PREP(SDHCI_GLI_9750_PLLSSC_STEP, step) |
	       FIELD_PREP(SDHCI_GLI_9750_PLLSSC_EN, enable);
	ssc |= FIELD_PREP(SDHCI_GLI_9750_PLLSSC_PPM, ppm);
	sdhci_writel(host, ssc, SDHCI_GLI_9750_PLLSSC);
	sdhci_writel(host, pll, SDHCI_GLI_9750_PLL);
	gl9750_wt_off(host);
}

static void gl9750_set_ssc_pll_205mhz(struct sdhci_host *host)
{
	bool enable = gl9750_ssc_enable(host);

	/* set pll to 205MHz and ssc */
	gl9750_set_ssc(host, enable, 0xF, 0x5A1D);
	gl9750_set_pll(host, 0x1, 0x246, 0x0);
}

static void gl9750_set_ssc_pll_100mhz(struct sdhci_host *host)
{
	bool enable = gl9750_ssc_enable(host);

	/* set pll to 100MHz and ssc */
	gl9750_set_ssc(host, enable, 0xE, 0x51EC);
	gl9750_set_pll(host, 0x1, 0x244, 0x1);
}

static void gl9750_set_ssc_pll_50mhz(struct sdhci_host *host)
{
	bool enable = gl9750_ssc_enable(host);

	/* set pll to 50MHz and ssc */
	gl9750_set_ssc(host, enable, 0xE, 0x51EC);
	gl9750_set_pll(host, 0x1, 0x244, 0x3);
}

static void sdhci_gl9750_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct mmc_ios *ios = &host->mmc->ios;
	u16 clk;

	host->mmc->actual_clock = 0;

	gl9750_disable_ssc_pll(host);
	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	clk = sdhci_calc_clk(host, clock, &host->mmc->actual_clock);
	if (clock == 200000000 && ios->timing == MMC_TIMING_UHS_SDR104) {
		host->mmc->actual_clock = 205000000;
		gl9750_set_ssc_pll_205mhz(host);
	} else if (clock == 100000000) {
		gl9750_set_ssc_pll_100mhz(host);
	} else if (clock == 50000000) {
		gl9750_set_ssc_pll_50mhz(host);
	}

	sdhci_enable_clk(host, clk);
}

static void gl9750_hw_setting(struct sdhci_host *host)
{
	u32 value;

	gl9750_wt_on(host);

	value = sdhci_readl(host, SDHCI_GLI_9750_CFG2);
	value &= ~SDHCI_GLI_9750_CFG2_L1DLY;
	/* set ASPM L1 entry delay to 7.9us */
	value |= FIELD_PREP(SDHCI_GLI_9750_CFG2_L1DLY,
			    GLI_9750_CFG2_L1DLY_VALUE);
	sdhci_writel(host, value, SDHCI_GLI_9750_CFG2);

	gl9750_wt_off(host);
}

static void gli_pcie_enable_msi(struct sdhci_pci_slot *slot)
{
	int ret;

	ret = pci_alloc_irq_vectors(slot->chip->pdev, 1, 1,
				    PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (ret < 0) {
		pr_warn("%s: enable PCI MSI failed, error=%d\n",
		       mmc_hostname(slot->host->mmc), ret);
		return;
	}

	slot->host->irq = pci_irq_vector(slot->chip->pdev, 0);
}

static inline void gl9755_wt_on(struct pci_dev *pdev)
{
	u32 wt_value;
	u32 wt_enable;

	pci_read_config_dword(pdev, PCI_GLI_9755_WT, &wt_value);
	wt_enable = FIELD_GET(PCI_GLI_9755_WT_EN, wt_value);

	if (wt_enable == GLI_9755_WT_EN_ON)
		return;

	wt_value &= ~PCI_GLI_9755_WT_EN;
	wt_value |= FIELD_PREP(PCI_GLI_9755_WT_EN, GLI_9755_WT_EN_ON);

	pci_write_config_dword(pdev, PCI_GLI_9755_WT, wt_value);
}

static inline void gl9755_wt_off(struct pci_dev *pdev)
{
	u32 wt_value;
	u32 wt_enable;

	pci_read_config_dword(pdev, PCI_GLI_9755_WT, &wt_value);
	wt_enable = FIELD_GET(PCI_GLI_9755_WT_EN, wt_value);

	if (wt_enable == GLI_9755_WT_EN_OFF)
		return;

	wt_value &= ~PCI_GLI_9755_WT_EN;
	wt_value |= FIELD_PREP(PCI_GLI_9755_WT_EN, GLI_9755_WT_EN_OFF);

	pci_write_config_dword(pdev, PCI_GLI_9755_WT, wt_value);
}

static void gl9755_disable_ssc_pll(struct pci_dev *pdev)
{
	u32 pll;

	gl9755_wt_on(pdev);
	pci_read_config_dword(pdev, PCI_GLI_9755_PLL, &pll);
	pll &= ~(PCI_GLI_9755_PLL_DIR | PCI_GLI_9755_PLLSSC_EN);
	pci_write_config_dword(pdev, PCI_GLI_9755_PLL, pll);
	gl9755_wt_off(pdev);
}

static void gl9755_set_pll(struct pci_dev *pdev, u8 dir, u16 ldiv, u8 pdiv)
{
	u32 pll;

	gl9755_wt_on(pdev);
	pci_read_config_dword(pdev, PCI_GLI_9755_PLL, &pll);
	pll &= ~(PCI_GLI_9755_PLL_LDIV |
		 PCI_GLI_9755_PLL_PDIV |
		 PCI_GLI_9755_PLL_DIR);
	pll |= FIELD_PREP(PCI_GLI_9755_PLL_LDIV, ldiv) |
	       FIELD_PREP(PCI_GLI_9755_PLL_PDIV, pdiv) |
	       FIELD_PREP(PCI_GLI_9755_PLL_DIR, dir);
	pci_write_config_dword(pdev, PCI_GLI_9755_PLL, pll);
	gl9755_wt_off(pdev);

	/* wait for pll stable */
	mdelay(1);
}

static bool gl9755_ssc_enable(struct pci_dev *pdev)
{
	u32 misc;
	u8 off;

	gl9755_wt_on(pdev);
	pci_read_config_dword(pdev, PCI_GLI_9755_MISC, &misc);
	off = FIELD_GET(PCI_GLI_9755_MISC_SSC_OFF, misc);
	gl9755_wt_off(pdev);

	return !off;
}

static void gl9755_set_ssc(struct pci_dev *pdev, u8 enable, u8 step, u16 ppm)
{
	u32 pll;
	u32 ssc;

	gl9755_wt_on(pdev);
	pci_read_config_dword(pdev, PCI_GLI_9755_PLL, &pll);
	pci_read_config_dword(pdev, PCI_GLI_9755_PLLSSC, &ssc);
	pll &= ~(PCI_GLI_9755_PLLSSC_STEP |
		 PCI_GLI_9755_PLLSSC_EN);
	ssc &= ~PCI_GLI_9755_PLLSSC_PPM;
	pll |= FIELD_PREP(PCI_GLI_9755_PLLSSC_STEP, step) |
	       FIELD_PREP(PCI_GLI_9755_PLLSSC_EN, enable);
	ssc |= FIELD_PREP(PCI_GLI_9755_PLLSSC_PPM, ppm);
	pci_write_config_dword(pdev, PCI_GLI_9755_PLLSSC, ssc);
	pci_write_config_dword(pdev, PCI_GLI_9755_PLL, pll);
	gl9755_wt_off(pdev);
}

static void gl9755_set_ssc_pll_205mhz(struct pci_dev *pdev)
{
	bool enable = gl9755_ssc_enable(pdev);

	/* set pll to 205MHz and ssc */
	gl9755_set_ssc(pdev, enable, 0xF, 0x5A1D);
	gl9755_set_pll(pdev, 0x1, 0x246, 0x0);
}

static void gl9755_set_ssc_pll_100mhz(struct pci_dev *pdev)
{
	bool enable = gl9755_ssc_enable(pdev);

	/* set pll to 100MHz and ssc */
	gl9755_set_ssc(pdev, enable, 0xE, 0x51EC);
	gl9755_set_pll(pdev, 0x1, 0x244, 0x1);
}

static void gl9755_set_ssc_pll_50mhz(struct pci_dev *pdev)
{
	bool enable = gl9755_ssc_enable(pdev);

	/* set pll to 50MHz and ssc */
	gl9755_set_ssc(pdev, enable, 0xE, 0x51EC);
	gl9755_set_pll(pdev, 0x1, 0x244, 0x3);
}

static void sdhci_gl9755_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pci_slot *slot = sdhci_priv(host);
	struct mmc_ios *ios = &host->mmc->ios;
	struct pci_dev *pdev;
	u16 clk;

	pdev = slot->chip->pdev;
	host->mmc->actual_clock = 0;

	gl9755_disable_ssc_pll(pdev);
	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	clk = sdhci_calc_clk(host, clock, &host->mmc->actual_clock);
	if (clock == 200000000 && ios->timing == MMC_TIMING_UHS_SDR104) {
		host->mmc->actual_clock = 205000000;
		gl9755_set_ssc_pll_205mhz(pdev);
	} else if (clock == 100000000) {
		gl9755_set_ssc_pll_100mhz(pdev);
	} else if (clock == 50000000) {
		gl9755_set_ssc_pll_50mhz(pdev);
	}

	sdhci_enable_clk(host, clk);
}

static void gl9755_hw_setting(struct sdhci_pci_slot *slot)
{
	struct pci_dev *pdev = slot->chip->pdev;
	u32 value;

	gl9755_wt_on(pdev);

	pci_read_config_dword(pdev, PCI_GLI_9755_PECONF, &value);
	/*
	 * Apple ARM64 platforms using these chips may have
	 * inverted CD/WP detection.
	 */
	if (of_property_read_bool(pdev->dev.of_node, "cd-inverted"))
		value |= PCI_GLI_9755_INVERT_CD;
	if (of_property_read_bool(pdev->dev.of_node, "wp-inverted"))
		value |= PCI_GLI_9755_INVERT_WP;
	value &= ~PCI_GLI_9755_LFCLK;
	value &= ~PCI_GLI_9755_DMACLK;
	pci_write_config_dword(pdev, PCI_GLI_9755_PECONF, value);

	/* enable short circuit protection */
	pci_read_config_dword(pdev, PCI_GLI_9755_SerDes, &value);
	value &= ~PCI_GLI_9755_SCP_DIS;
	pci_write_config_dword(pdev, PCI_GLI_9755_SerDes, value);

	pci_read_config_dword(pdev, PCI_GLI_9755_CFG2, &value);
	value &= ~PCI_GLI_9755_CFG2_L1DLY;
	/* set ASPM L1 entry delay to 7.9us */
	value |= FIELD_PREP(PCI_GLI_9755_CFG2_L1DLY,
			    GLI_9755_CFG2_L1DLY_VALUE);
	pci_write_config_dword(pdev, PCI_GLI_9755_CFG2, value);

	/* toggle PM state to allow GL9755 to enter ASPM L1.2 */
	pci_read_config_dword(pdev, PCI_GLI_9755_PM_CTRL, &value);
	value |= PCI_GLI_9755_PM_STATE;
	pci_write_config_dword(pdev, PCI_GLI_9755_PM_CTRL, value);
	value &= ~PCI_GLI_9755_PM_STATE;
	pci_write_config_dword(pdev, PCI_GLI_9755_PM_CTRL, value);

	gl9755_wt_off(pdev);
}

static int gli_probe_slot_gl9750(struct sdhci_pci_slot *slot)
{
	struct sdhci_host *host = slot->host;

	gl9750_hw_setting(host);
	gli_pcie_enable_msi(slot);
	slot->host->mmc->caps2 |= MMC_CAP2_NO_SDIO;
	sdhci_enable_v4_mode(host);

	return 0;
}

static int gli_probe_slot_gl9755(struct sdhci_pci_slot *slot)
{
	struct sdhci_host *host = slot->host;

	gl9755_hw_setting(slot);
	gli_pcie_enable_msi(slot);
	slot->host->mmc->caps2 |= MMC_CAP2_NO_SDIO;
	sdhci_enable_v4_mode(host);

	return 0;
}

static void sdhci_gli_voltage_switch(struct sdhci_host *host)
{
	/*
	 * According to Section 3.6.1 signal voltage switch procedure in
	 * SD Host Controller Simplified Spec. 4.20, steps 6~8 are as
	 * follows:
	 * (6) Set 1.8V Signal Enable in the Host Control 2 register.
	 * (7) Wait 5ms. 1.8V voltage regulator shall be stable within this
	 *     period.
	 * (8) If 1.8V Signal Enable is cleared by Host Controller, go to
	 *     step (12).
	 *
	 * Wait 5ms after set 1.8V signal enable in Host Control 2 register
	 * to ensure 1.8V signal enable bit is set by GL9750/GL9755.
	 *
	 * ...however, the controller in the NUC10i3FNK4 (a 9755) requires
	 * slightly longer than 5ms before the control register reports that
	 * 1.8V is ready, and far longer still before the card will actually
	 * work reliably.
	 */
	usleep_range(100000, 110000);
}

static void sdhci_gl9750_reset(struct sdhci_host *host, u8 mask)
{
	sdhci_reset(host, mask);
	gli_set_9750(host);
}

static u32 sdhci_gl9750_readl(struct sdhci_host *host, int reg)
{
	u32 value;

	value = readl(host->ioaddr + reg);
	if (unlikely(reg == SDHCI_MAX_CURRENT && !(value & 0xff)))
		value |= 0xc8;

	return value;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_pci_gli_resume(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot = chip->slots[0];

	pci_free_irq_vectors(slot->chip->pdev);
	gli_pcie_enable_msi(slot);

	return sdhci_pci_resume_host(chip);
}

static int sdhci_cqhci_gli_resume(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot = chip->slots[0];
	int ret;

	ret = sdhci_pci_gli_resume(chip);
	if (ret)
		return ret;

	return cqhci_resume(slot->host->mmc);
}

static int sdhci_cqhci_gli_suspend(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot = chip->slots[0];
	int ret;

	ret = cqhci_suspend(slot->host->mmc);
	if (ret)
		return ret;

	return sdhci_suspend_host(slot->host);
}
#endif

static void gl9763e_hs400_enhanced_strobe(struct mmc_host *mmc,
					  struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 val;

	val = sdhci_readl(host, SDHCI_GLI_9763E_HS400_ES_REG);
	if (ios->enhanced_strobe)
		val |= SDHCI_GLI_9763E_HS400_ES_BIT;
	else
		val &= ~SDHCI_GLI_9763E_HS400_ES_BIT;

	sdhci_writel(host, val, SDHCI_GLI_9763E_HS400_ES_REG);
}

static void sdhci_set_gl9763e_signaling(struct sdhci_host *host,
					unsigned int timing)
{
	u16 ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if (timing == MMC_TIMING_MMC_HS200)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (timing == MMC_TIMING_MMC_HS)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (timing == MMC_TIMING_MMC_DDR52)
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
	else if (timing == MMC_TIMING_MMC_HS400)
		ctrl_2 |= SDHCI_GLI_9763E_CTRL_HS400;

	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
}

static void sdhci_gl9763e_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

static void sdhci_gl9763e_cqe_pre_enable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 value;

	value = cqhci_readl(cq_host, CQHCI_CFG);
	value |= CQHCI_ENABLE;
	cqhci_writel(cq_host, value, CQHCI_CFG);
}

static void sdhci_gl9763e_cqe_enable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	sdhci_writew(host, GLI_9763E_CQE_TRNS_MODE, SDHCI_TRANSFER_MODE);
	sdhci_cqe_enable(mmc);
}

static u32 sdhci_gl9763e_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	return 0;
}

static void sdhci_gl9763e_cqe_post_disable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 value;

	value = cqhci_readl(cq_host, CQHCI_CFG);
	value &= ~CQHCI_ENABLE;
	cqhci_writel(cq_host, value, CQHCI_CFG);
	sdhci_writew(host, 0x0, SDHCI_TRANSFER_MODE);
}

static const struct cqhci_host_ops sdhci_gl9763e_cqhci_ops = {
	.enable         = sdhci_gl9763e_cqe_enable,
	.disable        = sdhci_cqe_disable,
	.dumpregs       = sdhci_gl9763e_dumpregs,
	.pre_enable     = sdhci_gl9763e_cqe_pre_enable,
	.post_disable   = sdhci_gl9763e_cqe_post_disable,
};

static int gl9763e_add_host(struct sdhci_pci_slot *slot)
{
	struct device *dev = &slot->chip->pdev->dev;
	struct sdhci_host *host = slot->host;
	struct cqhci_host *cq_host;
	bool dma64;
	int ret;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	cq_host = devm_kzalloc(dev, sizeof(*cq_host), GFP_KERNEL);
	if (!cq_host) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cq_host->mmio = host->ioaddr + SDHCI_GLI_9763E_CQE_BASE_ADDR;
	cq_host->ops = &sdhci_gl9763e_cqhci_ops;

	dma64 = host->flags & SDHCI_USE_64_BIT_DMA;
	if (dma64)
		cq_host->caps |= CQHCI_TASK_DESC_SZ_128;

	ret = cqhci_init(cq_host, host->mmc, dma64);
	if (ret)
		goto cleanup;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	sdhci_cleanup_host(host);
	return ret;
}

static void sdhci_gl9763e_reset(struct sdhci_host *host, u8 mask)
{
	if ((host->mmc->caps2 & MMC_CAP2_CQE) && (mask & SDHCI_RESET_ALL) &&
	    host->mmc->cqe_private)
		cqhci_deactivate(host->mmc);
	sdhci_reset(host, mask);
}

static void gli_set_gl9763e(struct sdhci_pci_slot *slot)
{
	struct pci_dev *pdev = slot->chip->pdev;
	u32 value;

	pci_read_config_dword(pdev, PCIE_GLI_9763E_VHS, &value);
	value &= ~GLI_9763E_VHS_REV;
	value |= FIELD_PREP(GLI_9763E_VHS_REV, GLI_9763E_VHS_REV_W);
	pci_write_config_dword(pdev, PCIE_GLI_9763E_VHS, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_SCR, &value);
	value |= GLI_9763E_SCR_AXI_REQ;
	pci_write_config_dword(pdev, PCIE_GLI_9763E_SCR, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_MMC_CTRL, &value);
	value &= ~GLI_9763E_HS400_SLOW;
	pci_write_config_dword(pdev, PCIE_GLI_9763E_MMC_CTRL, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_CFG2, &value);
	value &= ~GLI_9763E_CFG2_L1DLY;
	/* set ASPM L1 entry delay to 21us */
	value |= FIELD_PREP(GLI_9763E_CFG2_L1DLY, GLI_9763E_CFG2_L1DLY_MID);
	pci_write_config_dword(pdev, PCIE_GLI_9763E_CFG2, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_CLKRXDLY, &value);
	value &= ~GLI_9763E_HS400_RXDLY;
	value |= FIELD_PREP(GLI_9763E_HS400_RXDLY, GLI_9763E_HS400_RXDLY_5);
	pci_write_config_dword(pdev, PCIE_GLI_9763E_CLKRXDLY, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_VHS, &value);
	value &= ~GLI_9763E_VHS_REV;
	value |= FIELD_PREP(GLI_9763E_VHS_REV, GLI_9763E_VHS_REV_R);
	pci_write_config_dword(pdev, PCIE_GLI_9763E_VHS, value);
}

#ifdef CONFIG_PM
static void gl9763e_set_low_power_negotiation(struct sdhci_pci_slot *slot, bool enable)
{
	struct pci_dev *pdev = slot->chip->pdev;
	u32 value;

	pci_read_config_dword(pdev, PCIE_GLI_9763E_VHS, &value);
	value &= ~GLI_9763E_VHS_REV;
	value |= FIELD_PREP(GLI_9763E_VHS_REV, GLI_9763E_VHS_REV_W);
	pci_write_config_dword(pdev, PCIE_GLI_9763E_VHS, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_CFG, &value);

	if (enable)
		value &= ~GLI_9763E_CFG_LPSN_DIS;
	else
		value |= GLI_9763E_CFG_LPSN_DIS;

	pci_write_config_dword(pdev, PCIE_GLI_9763E_CFG, value);

	pci_read_config_dword(pdev, PCIE_GLI_9763E_VHS, &value);
	value &= ~GLI_9763E_VHS_REV;
	value |= FIELD_PREP(GLI_9763E_VHS_REV, GLI_9763E_VHS_REV_R);
	pci_write_config_dword(pdev, PCIE_GLI_9763E_VHS, value);
}

static int gl9763e_runtime_suspend(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot = chip->slots[0];
	struct sdhci_host *host = slot->host;
	u16 clock;

	/* Enable LPM negotiation to allow entering L1 state */
	gl9763e_set_low_power_negotiation(slot, true);

	clock = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clock &= ~(SDHCI_CLOCK_PLL_EN | SDHCI_CLOCK_CARD_EN);
	sdhci_writew(host, clock, SDHCI_CLOCK_CONTROL);

	return 0;
}

static int gl9763e_runtime_resume(struct sdhci_pci_chip *chip)
{
	struct sdhci_pci_slot *slot = chip->slots[0];
	struct sdhci_host *host = slot->host;
	u16 clock;

	if (host->mmc->ios.power_mode != MMC_POWER_ON)
		return 0;

	clock = sdhci_readw(host, SDHCI_CLOCK_CONTROL);

	clock |= SDHCI_CLOCK_PLL_EN;
	clock &= ~SDHCI_CLOCK_INT_STABLE;
	sdhci_writew(host, clock, SDHCI_CLOCK_CONTROL);

	/* Wait max 150 ms */
	if (read_poll_timeout(sdhci_readw, clock, (clock & SDHCI_CLOCK_INT_STABLE),
			      1000, 150000, false, host, SDHCI_CLOCK_CONTROL)) {
		pr_err("%s: PLL clock never stabilised.\n",
		       mmc_hostname(host->mmc));
		sdhci_dumpregs(host);
	}

	clock |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clock, SDHCI_CLOCK_CONTROL);

	/* Disable LPM negotiation to avoid entering L1 state. */
	gl9763e_set_low_power_negotiation(slot, false);

	return 0;
}
#endif

static int gli_probe_slot_gl9763e(struct sdhci_pci_slot *slot)
{
	struct pci_dev *pdev = slot->chip->pdev;
	struct sdhci_host *host = slot->host;
	u32 value;

	host->mmc->caps |= MMC_CAP_8_BIT_DATA |
			   MMC_CAP_1_8V_DDR |
			   MMC_CAP_NONREMOVABLE;
	host->mmc->caps2 |= MMC_CAP2_HS200_1_8V_SDR |
			    MMC_CAP2_HS400_1_8V |
			    MMC_CAP2_HS400_ES |
			    MMC_CAP2_NO_SDIO |
			    MMC_CAP2_NO_SD;

	pci_read_config_dword(pdev, PCIE_GLI_9763E_MB, &value);
	if (!(value & GLI_9763E_MB_CMDQ_OFF))
		if (value & GLI_9763E_MB_ERP_ON)
			host->mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;

	gli_pcie_enable_msi(slot);
	host->mmc_host_ops.hs400_enhanced_strobe =
					gl9763e_hs400_enhanced_strobe;
	gli_set_gl9763e(slot);
	sdhci_enable_v4_mode(host);

	return 0;
}

#define REG_OFFSET_IN_BITS(reg) ((reg) << 3 & 0x18)

static u16 sdhci_gli_readw(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + (reg & ~3));
	u16 word;

	word = (val >> REG_OFFSET_IN_BITS(reg)) & 0xffff;
	return word;
}

static u8 sdhci_gli_readb(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + (reg & ~3));
	u8 byte = (val >> REG_OFFSET_IN_BITS(reg)) & 0xff;

	return byte;
}

static const struct sdhci_ops sdhci_gl9755_ops = {
	.read_w			= sdhci_gli_readw,
	.read_b			= sdhci_gli_readb,
	.set_clock		= sdhci_gl9755_set_clock,
	.enable_dma		= sdhci_pci_enable_dma,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_reset,
	.set_uhs_signaling	= sdhci_set_uhs_signaling,
	.voltage_switch		= sdhci_gli_voltage_switch,
};

const struct sdhci_pci_fixes sdhci_gl9755 = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2	= SDHCI_QUIRK2_BROKEN_DDR50,
	.probe_slot	= gli_probe_slot_gl9755,
	.ops            = &sdhci_gl9755_ops,
#ifdef CONFIG_PM_SLEEP
	.resume         = sdhci_pci_gli_resume,
#endif
};

static const struct sdhci_ops sdhci_gl9750_ops = {
	.read_w			= sdhci_gli_readw,
	.read_b			= sdhci_gli_readb,
	.read_l                 = sdhci_gl9750_readl,
	.set_clock		= sdhci_gl9750_set_clock,
	.enable_dma		= sdhci_pci_enable_dma,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_gl9750_reset,
	.set_uhs_signaling	= sdhci_set_uhs_signaling,
	.voltage_switch		= sdhci_gli_voltage_switch,
	.platform_execute_tuning = gl9750_execute_tuning,
};

const struct sdhci_pci_fixes sdhci_gl9750 = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2	= SDHCI_QUIRK2_BROKEN_DDR50,
	.probe_slot	= gli_probe_slot_gl9750,
	.ops            = &sdhci_gl9750_ops,
#ifdef CONFIG_PM_SLEEP
	.resume         = sdhci_pci_gli_resume,
#endif
};

static const struct sdhci_ops sdhci_gl9763e_ops = {
	.set_clock		= sdhci_set_clock,
	.enable_dma		= sdhci_pci_enable_dma,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_gl9763e_reset,
	.set_uhs_signaling	= sdhci_set_gl9763e_signaling,
	.voltage_switch		= sdhci_gli_voltage_switch,
	.irq                    = sdhci_gl9763e_cqhci_irq,
};

const struct sdhci_pci_fixes sdhci_gl9763e = {
	.quirks		= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.probe_slot	= gli_probe_slot_gl9763e,
	.ops            = &sdhci_gl9763e_ops,
#ifdef CONFIG_PM_SLEEP
	.resume		= sdhci_cqhci_gli_resume,
	.suspend	= sdhci_cqhci_gli_suspend,
#endif
#ifdef CONFIG_PM
	.runtime_suspend = gl9763e_runtime_suspend,
	.runtime_resume  = gl9763e_runtime_resume,
	.allow_runtime_pm = true,
#endif
	.add_host       = gl9763e_add_host,
};
