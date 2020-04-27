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
#include "sdhci.h"
#include "sdhci-pci.h"

/*  Genesys Logic extra registers */
#define SDHCI_GLI_9750_WT         0x800
#define   SDHCI_GLI_9750_WT_EN      BIT(0)
#define   GLI_9750_WT_EN_ON	    0x1
#define   GLI_9750_WT_EN_OFF	    0x0

#define SDHCI_GLI_9750_DRIVING      0x860
#define   SDHCI_GLI_9750_DRIVING_1    GENMASK(11, 0)
#define   SDHCI_GLI_9750_DRIVING_2    GENMASK(27, 26)
#define   GLI_9750_DRIVING_1_VALUE    0xFFF
#define   GLI_9750_DRIVING_2_VALUE    0x3

#define SDHCI_GLI_9750_PLL	      0x864
#define   SDHCI_GLI_9750_PLL_TX2_INV    BIT(23)
#define   SDHCI_GLI_9750_PLL_TX2_DLY    GENMASK(22, 20)
#define   GLI_9750_PLL_TX2_INV_VALUE    0x1
#define   GLI_9750_PLL_TX2_DLY_VALUE    0x0

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

static int gli_probe_slot_gl9750(struct sdhci_pci_slot *slot)
{
	struct sdhci_host *host = slot->host;

	gli_pcie_enable_msi(slot);
	slot->host->mmc->caps2 |= MMC_CAP2_NO_SDIO;
	sdhci_enable_v4_mode(host);

	return 0;
}

static int gli_probe_slot_gl9755(struct sdhci_pci_slot *slot)
{
	struct sdhci_host *host = slot->host;

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
	 */
	usleep_range(5000, 5500);
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
#endif

static const struct sdhci_ops sdhci_gl9755_ops = {
	.set_clock		= sdhci_set_clock,
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
	.read_l                 = sdhci_gl9750_readl,
	.set_clock		= sdhci_set_clock,
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
