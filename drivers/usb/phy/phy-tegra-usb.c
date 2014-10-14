/*
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 *	Benoit Goby <benoit@android.com>
 *	Venu Byravarasu <vbyravarasu@nvidia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/resource.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/of.h>
#include <linux/usb/ehci_def.h>
#include <linux/usb/tegra_usb_phy.h>
#include <linux/regulator/consumer.h>

#define ULPI_VIEWPORT		0x170

/* PORTSC PTS/PHCD bits, Tegra20 only */
#define TEGRA_USB_PORTSC1				0x184
#define TEGRA_USB_PORTSC1_PTS(x)			(((x) & 0x3) << 30)
#define TEGRA_USB_PORTSC1_PHCD				(1 << 23)

/* HOSTPC1 PTS/PHCD bits, Tegra30 and above */
#define TEGRA_USB_HOSTPC1_DEVLC		0x1b4
#define TEGRA_USB_HOSTPC1_DEVLC_PTS(x)	(((x) & 0x7) << 29)
#define TEGRA_USB_HOSTPC1_DEVLC_PHCD	(1 << 22)

/* Bits of PORTSC1, which will get cleared by writing 1 into them */
#define TEGRA_PORTSC1_RWC_BITS	(PORT_CSC | PORT_PEC | PORT_OCC)

#define USB_SUSP_CTRL		0x400
#define   USB_WAKE_ON_CNNT_EN_DEV	(1 << 3)
#define   USB_WAKE_ON_DISCON_EN_DEV	(1 << 4)
#define   USB_SUSP_CLR		(1 << 5)
#define   USB_PHY_CLK_VALID	(1 << 7)
#define   UTMIP_RESET			(1 << 11)
#define   UHSIC_RESET			(1 << 11)
#define   UTMIP_PHY_ENABLE		(1 << 12)
#define   ULPI_PHY_ENABLE	(1 << 13)
#define   USB_SUSP_SET		(1 << 14)
#define   USB_WAKEUP_DEBOUNCE_COUNT(x)	(((x) & 0x7) << 16)

#define USB1_LEGACY_CTRL	0x410
#define   USB1_NO_LEGACY_MODE			(1 << 0)
#define   USB1_VBUS_SENSE_CTL_MASK		(3 << 1)
#define   USB1_VBUS_SENSE_CTL_VBUS_WAKEUP	(0 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD_OR_VBUS_WAKEUP \
						(1 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD	(2 << 1)
#define   USB1_VBUS_SENSE_CTL_A_SESS_VLD	(3 << 1)

#define ULPI_TIMING_CTRL_0	0x424
#define   ULPI_OUTPUT_PINMUX_BYP	(1 << 10)
#define   ULPI_CLKOUT_PINMUX_BYP	(1 << 11)

#define ULPI_TIMING_CTRL_1	0x428
#define   ULPI_DATA_TRIMMER_LOAD	(1 << 0)
#define   ULPI_DATA_TRIMMER_SEL(x)	(((x) & 0x7) << 1)
#define   ULPI_STPDIRNXT_TRIMMER_LOAD	(1 << 16)
#define   ULPI_STPDIRNXT_TRIMMER_SEL(x)	(((x) & 0x7) << 17)
#define   ULPI_DIR_TRIMMER_LOAD		(1 << 24)
#define   ULPI_DIR_TRIMMER_SEL(x)	(((x) & 0x7) << 25)

#define UTMIP_PLL_CFG1		0x804
#define   UTMIP_XTAL_FREQ_COUNT(x)		(((x) & 0xfff) << 0)
#define   UTMIP_PLLU_ENABLE_DLY_COUNT(x)	(((x) & 0x1f) << 27)

#define UTMIP_XCVR_CFG0		0x808
#define   UTMIP_XCVR_SETUP(x)			(((x) & 0xf) << 0)
#define   UTMIP_XCVR_SETUP_MSB(x)		((((x) & 0x70) >> 4) << 22)
#define   UTMIP_XCVR_LSRSLEW(x)			(((x) & 0x3) << 8)
#define   UTMIP_XCVR_LSFSLEW(x)			(((x) & 0x3) << 10)
#define   UTMIP_FORCE_PD_POWERDOWN		(1 << 14)
#define   UTMIP_FORCE_PD2_POWERDOWN		(1 << 16)
#define   UTMIP_FORCE_PDZI_POWERDOWN		(1 << 18)
#define   UTMIP_XCVR_LSBIAS_SEL			(1 << 21)
#define   UTMIP_XCVR_HSSLEW(x)			(((x) & 0x3) << 4)
#define   UTMIP_XCVR_HSSLEW_MSB(x)		((((x) & 0x1fc) >> 2) << 25)

#define UTMIP_BIAS_CFG0		0x80c
#define   UTMIP_OTGPD			(1 << 11)
#define   UTMIP_BIASPD			(1 << 10)
#define   UTMIP_HSSQUELCH_LEVEL(x)	(((x) & 0x3) << 0)
#define   UTMIP_HSDISCON_LEVEL(x)	(((x) & 0x3) << 2)
#define   UTMIP_HSDISCON_LEVEL_MSB(x)	((((x) & 0x4) >> 2) << 24)

#define UTMIP_HSRX_CFG0		0x810
#define   UTMIP_ELASTIC_LIMIT(x)	(((x) & 0x1f) << 10)
#define   UTMIP_IDLE_WAIT(x)		(((x) & 0x1f) << 15)

#define UTMIP_HSRX_CFG1		0x814
#define   UTMIP_HS_SYNC_START_DLY(x)	(((x) & 0x1f) << 1)

#define UTMIP_TX_CFG0		0x820
#define   UTMIP_FS_PREABMLE_J		(1 << 19)
#define   UTMIP_HS_DISCON_DISABLE	(1 << 8)

#define UTMIP_MISC_CFG0		0x824
#define   UTMIP_DPDM_OBSERVE		(1 << 26)
#define   UTMIP_DPDM_OBSERVE_SEL(x)	(((x) & 0xf) << 27)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_J	UTMIP_DPDM_OBSERVE_SEL(0xf)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_K	UTMIP_DPDM_OBSERVE_SEL(0xe)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_SE1 UTMIP_DPDM_OBSERVE_SEL(0xd)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_SE0 UTMIP_DPDM_OBSERVE_SEL(0xc)
#define   UTMIP_SUSPEND_EXIT_ON_EDGE	(1 << 22)

#define UTMIP_MISC_CFG1		0x828
#define   UTMIP_PLL_ACTIVE_DLY_COUNT(x)	(((x) & 0x1f) << 18)
#define   UTMIP_PLLU_STABLE_COUNT(x)	(((x) & 0xfff) << 6)

#define UTMIP_DEBOUNCE_CFG0	0x82c
#define   UTMIP_BIAS_DEBOUNCE_A(x)	(((x) & 0xffff) << 0)

#define UTMIP_BAT_CHRG_CFG0	0x830
#define   UTMIP_PD_CHRG			(1 << 0)

#define UTMIP_SPARE_CFG0	0x834
#define   FUSE_SETUP_SEL		(1 << 3)

#define UTMIP_XCVR_CFG1		0x838
#define   UTMIP_FORCE_PDDISC_POWERDOWN	(1 << 0)
#define   UTMIP_FORCE_PDCHRP_POWERDOWN	(1 << 2)
#define   UTMIP_FORCE_PDDR_POWERDOWN	(1 << 4)
#define   UTMIP_XCVR_TERM_RANGE_ADJ(x)	(((x) & 0xf) << 18)

#define UTMIP_BIAS_CFG1		0x83c
#define   UTMIP_BIAS_PDTRK_COUNT(x)	(((x) & 0x1f) << 3)

/* For Tegra30 and above only, the address is different in Tegra20 */
#define USB_USBMODE		0x1f8
#define   USB_USBMODE_MASK		(3 << 0)
#define   USB_USBMODE_HOST		(3 << 0)
#define   USB_USBMODE_DEVICE		(2 << 0)

static DEFINE_SPINLOCK(utmip_pad_lock);
static int utmip_pad_count;

struct tegra_xtal_freq {
	int freq;
	u8 enable_delay;
	u8 stable_count;
	u8 active_delay;
	u8 xtal_freq_count;
	u16 debounce;
};

static const struct tegra_xtal_freq tegra_freq_table[] = {
	{
		.freq = 12000000,
		.enable_delay = 0x02,
		.stable_count = 0x2F,
		.active_delay = 0x04,
		.xtal_freq_count = 0x76,
		.debounce = 0x7530,
	},
	{
		.freq = 13000000,
		.enable_delay = 0x02,
		.stable_count = 0x33,
		.active_delay = 0x05,
		.xtal_freq_count = 0x7F,
		.debounce = 0x7EF4,
	},
	{
		.freq = 19200000,
		.enable_delay = 0x03,
		.stable_count = 0x4B,
		.active_delay = 0x06,
		.xtal_freq_count = 0xBB,
		.debounce = 0xBB80,
	},
	{
		.freq = 26000000,
		.enable_delay = 0x04,
		.stable_count = 0x66,
		.active_delay = 0x09,
		.xtal_freq_count = 0xFE,
		.debounce = 0xFDE8,
	},
};

static void set_pts(struct tegra_usb_phy *phy, u8 pts_val)
{
	void __iomem *base = phy->regs;
	unsigned long val;

	if (phy->soc_config->has_hostpc) {
		val = readl(base + TEGRA_USB_HOSTPC1_DEVLC);
		val &= ~TEGRA_USB_HOSTPC1_DEVLC_PTS(~0);
		val |= TEGRA_USB_HOSTPC1_DEVLC_PTS(pts_val);
		writel(val, base + TEGRA_USB_HOSTPC1_DEVLC);
	} else {
		val = readl(base + TEGRA_USB_PORTSC1) & ~TEGRA_PORTSC1_RWC_BITS;
		val &= ~TEGRA_USB_PORTSC1_PTS(~0);
		val |= TEGRA_USB_PORTSC1_PTS(pts_val);
		writel(val, base + TEGRA_USB_PORTSC1);
	}
}

static void set_phcd(struct tegra_usb_phy *phy, bool enable)
{
	void __iomem *base = phy->regs;
	unsigned long val;

	if (phy->soc_config->has_hostpc) {
		val = readl(base + TEGRA_USB_HOSTPC1_DEVLC);
		if (enable)
			val |= TEGRA_USB_HOSTPC1_DEVLC_PHCD;
		else
			val &= ~TEGRA_USB_HOSTPC1_DEVLC_PHCD;
		writel(val, base + TEGRA_USB_HOSTPC1_DEVLC);
	} else {
		val = readl(base + TEGRA_USB_PORTSC1) & ~PORT_RWC_BITS;
		if (enable)
			val |= TEGRA_USB_PORTSC1_PHCD;
		else
			val &= ~TEGRA_USB_PORTSC1_PHCD;
		writel(val, base + TEGRA_USB_PORTSC1);
	}
}

static int utmip_pad_open(struct tegra_usb_phy *phy)
{
	phy->pad_clk = devm_clk_get(phy->u_phy.dev, "utmi-pads");
	if (IS_ERR(phy->pad_clk)) {
		pr_err("%s: can't get utmip pad clock\n", __func__);
		return PTR_ERR(phy->pad_clk);
	}

	return 0;
}

static void utmip_pad_power_on(struct tegra_usb_phy *phy)
{
	unsigned long val, flags;
	void __iomem *base = phy->pad_regs;
	struct tegra_utmip_config *config = phy->config;

	clk_prepare_enable(phy->pad_clk);

	spin_lock_irqsave(&utmip_pad_lock, flags);

	if (utmip_pad_count++ == 0) {
		val = readl(base + UTMIP_BIAS_CFG0);
		val &= ~(UTMIP_OTGPD | UTMIP_BIASPD);

		if (phy->soc_config->requires_extra_tuning_parameters) {
			val &= ~(UTMIP_HSSQUELCH_LEVEL(~0) |
				UTMIP_HSDISCON_LEVEL(~0) |
				UTMIP_HSDISCON_LEVEL_MSB(~0));

			val |= UTMIP_HSSQUELCH_LEVEL(config->hssquelch_level);
			val |= UTMIP_HSDISCON_LEVEL(config->hsdiscon_level);
			val |= UTMIP_HSDISCON_LEVEL_MSB(config->hsdiscon_level);
		}
		writel(val, base + UTMIP_BIAS_CFG0);
	}

	spin_unlock_irqrestore(&utmip_pad_lock, flags);

	clk_disable_unprepare(phy->pad_clk);
}

static int utmip_pad_power_off(struct tegra_usb_phy *phy)
{
	unsigned long val, flags;
	void __iomem *base = phy->pad_regs;

	if (!utmip_pad_count) {
		pr_err("%s: utmip pad already powered off\n", __func__);
		return -EINVAL;
	}

	clk_prepare_enable(phy->pad_clk);

	spin_lock_irqsave(&utmip_pad_lock, flags);

	if (--utmip_pad_count == 0) {
		val = readl(base + UTMIP_BIAS_CFG0);
		val |= UTMIP_OTGPD | UTMIP_BIASPD;
		writel(val, base + UTMIP_BIAS_CFG0);
	}

	spin_unlock_irqrestore(&utmip_pad_lock, flags);

	clk_disable_unprepare(phy->pad_clk);

	return 0;
}

static int utmi_wait_register(void __iomem *reg, u32 mask, u32 result)
{
	unsigned long timeout = 2000;
	do {
		if ((readl(reg) & mask) == result)
			return 0;
		udelay(1);
		timeout--;
	} while (timeout);
	return -1;
}

static void utmi_phy_clk_disable(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	if (phy->is_legacy_phy) {
		val = readl(base + USB_SUSP_CTRL);
		val |= USB_SUSP_SET;
		writel(val, base + USB_SUSP_CTRL);

		udelay(10);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_SET;
		writel(val, base + USB_SUSP_CTRL);
	} else
		set_phcd(phy, true);

	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID, 0) < 0)
		pr_err("%s: timeout waiting for phy to stabilize\n", __func__);
}

static void utmi_phy_clk_enable(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	if (phy->is_legacy_phy) {
		val = readl(base + USB_SUSP_CTRL);
		val |= USB_SUSP_CLR;
		writel(val, base + USB_SUSP_CTRL);

		udelay(10);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_CLR;
		writel(val, base + USB_SUSP_CTRL);
	} else
		set_phcd(phy, false);

	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID,
						     USB_PHY_CLK_VALID))
		pr_err("%s: timeout waiting for phy to stabilize\n", __func__);
}

static int utmi_phy_power_on(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;
	struct tegra_utmip_config *config = phy->config;

	val = readl(base + USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	writel(val, base + USB_SUSP_CTRL);

	if (phy->is_legacy_phy) {
		val = readl(base + USB1_LEGACY_CTRL);
		val |= USB1_NO_LEGACY_MODE;
		writel(val, base + USB1_LEGACY_CTRL);
	}

	val = readl(base + UTMIP_TX_CFG0);
	val |= UTMIP_FS_PREABMLE_J;
	writel(val, base + UTMIP_TX_CFG0);

	val = readl(base + UTMIP_HSRX_CFG0);
	val &= ~(UTMIP_IDLE_WAIT(~0) | UTMIP_ELASTIC_LIMIT(~0));
	val |= UTMIP_IDLE_WAIT(config->idle_wait_delay);
	val |= UTMIP_ELASTIC_LIMIT(config->elastic_limit);
	writel(val, base + UTMIP_HSRX_CFG0);

	val = readl(base + UTMIP_HSRX_CFG1);
	val &= ~UTMIP_HS_SYNC_START_DLY(~0);
	val |= UTMIP_HS_SYNC_START_DLY(config->hssync_start_delay);
	writel(val, base + UTMIP_HSRX_CFG1);

	val = readl(base + UTMIP_DEBOUNCE_CFG0);
	val &= ~UTMIP_BIAS_DEBOUNCE_A(~0);
	val |= UTMIP_BIAS_DEBOUNCE_A(phy->freq->debounce);
	writel(val, base + UTMIP_DEBOUNCE_CFG0);

	val = readl(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_SUSPEND_EXIT_ON_EDGE;
	writel(val, base + UTMIP_MISC_CFG0);

	if (!phy->soc_config->utmi_pll_config_in_car_module) {
		val = readl(base + UTMIP_MISC_CFG1);
		val &= ~(UTMIP_PLL_ACTIVE_DLY_COUNT(~0) |
			UTMIP_PLLU_STABLE_COUNT(~0));
		val |= UTMIP_PLL_ACTIVE_DLY_COUNT(phy->freq->active_delay) |
			UTMIP_PLLU_STABLE_COUNT(phy->freq->stable_count);
		writel(val, base + UTMIP_MISC_CFG1);

		val = readl(base + UTMIP_PLL_CFG1);
		val &= ~(UTMIP_XTAL_FREQ_COUNT(~0) |
			UTMIP_PLLU_ENABLE_DLY_COUNT(~0));
		val |= UTMIP_XTAL_FREQ_COUNT(phy->freq->xtal_freq_count) |
			UTMIP_PLLU_ENABLE_DLY_COUNT(phy->freq->enable_delay);
		writel(val, base + UTMIP_PLL_CFG1);
	}

	if (phy->mode == USB_DR_MODE_PERIPHERAL) {
		val = readl(base + USB_SUSP_CTRL);
		val &= ~(USB_WAKE_ON_CNNT_EN_DEV | USB_WAKE_ON_DISCON_EN_DEV);
		writel(val, base + USB_SUSP_CTRL);

		val = readl(base + UTMIP_BAT_CHRG_CFG0);
		val &= ~UTMIP_PD_CHRG;
		writel(val, base + UTMIP_BAT_CHRG_CFG0);
	} else {
		val = readl(base + UTMIP_BAT_CHRG_CFG0);
		val |= UTMIP_PD_CHRG;
		writel(val, base + UTMIP_BAT_CHRG_CFG0);
	}

	utmip_pad_power_on(phy);

	val = readl(base + UTMIP_XCVR_CFG0);
	val &= ~(UTMIP_FORCE_PD_POWERDOWN | UTMIP_FORCE_PD2_POWERDOWN |
		 UTMIP_FORCE_PDZI_POWERDOWN | UTMIP_XCVR_LSBIAS_SEL |
		 UTMIP_XCVR_SETUP(~0) | UTMIP_XCVR_SETUP_MSB(~0) |
		 UTMIP_XCVR_LSFSLEW(~0) | UTMIP_XCVR_LSRSLEW(~0));

	if (!config->xcvr_setup_use_fuses) {
		val |= UTMIP_XCVR_SETUP(config->xcvr_setup);
		val |= UTMIP_XCVR_SETUP_MSB(config->xcvr_setup);
	}
	val |= UTMIP_XCVR_LSFSLEW(config->xcvr_lsfslew);
	val |= UTMIP_XCVR_LSRSLEW(config->xcvr_lsrslew);

	if (phy->soc_config->requires_extra_tuning_parameters) {
		val &= ~(UTMIP_XCVR_HSSLEW(~0) | UTMIP_XCVR_HSSLEW_MSB(~0));
		val |= UTMIP_XCVR_HSSLEW(config->xcvr_hsslew);
		val |= UTMIP_XCVR_HSSLEW_MSB(config->xcvr_hsslew);
	}
	writel(val, base + UTMIP_XCVR_CFG0);

	val = readl(base + UTMIP_XCVR_CFG1);
	val &= ~(UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
		 UTMIP_FORCE_PDDR_POWERDOWN | UTMIP_XCVR_TERM_RANGE_ADJ(~0));
	val |= UTMIP_XCVR_TERM_RANGE_ADJ(config->term_range_adj);
	writel(val, base + UTMIP_XCVR_CFG1);

	val = readl(base + UTMIP_BIAS_CFG1);
	val &= ~UTMIP_BIAS_PDTRK_COUNT(~0);
	val |= UTMIP_BIAS_PDTRK_COUNT(0x5);
	writel(val, base + UTMIP_BIAS_CFG1);

	val = readl(base + UTMIP_SPARE_CFG0);
	if (config->xcvr_setup_use_fuses)
		val |= FUSE_SETUP_SEL;
	else
		val &= ~FUSE_SETUP_SEL;
	writel(val, base + UTMIP_SPARE_CFG0);

	if (!phy->is_legacy_phy) {
		val = readl(base + USB_SUSP_CTRL);
		val |= UTMIP_PHY_ENABLE;
		writel(val, base + USB_SUSP_CTRL);
	}

	val = readl(base + USB_SUSP_CTRL);
	val &= ~UTMIP_RESET;
	writel(val, base + USB_SUSP_CTRL);

	if (phy->is_legacy_phy) {
		val = readl(base + USB1_LEGACY_CTRL);
		val &= ~USB1_VBUS_SENSE_CTL_MASK;
		val |= USB1_VBUS_SENSE_CTL_A_SESS_VLD;
		writel(val, base + USB1_LEGACY_CTRL);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_SET;
		writel(val, base + USB_SUSP_CTRL);
	}

	utmi_phy_clk_enable(phy);

	if (phy->soc_config->requires_usbmode_setup) {
		val = readl(base + USB_USBMODE);
		val &= ~USB_USBMODE_MASK;
		if (phy->mode == USB_DR_MODE_HOST)
			val |= USB_USBMODE_HOST;
		else
			val |= USB_USBMODE_DEVICE;
		writel(val, base + USB_USBMODE);
	}

	if (!phy->is_legacy_phy)
		set_pts(phy, 0);

	return 0;
}

static int utmi_phy_power_off(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	utmi_phy_clk_disable(phy);

	if (phy->mode == USB_DR_MODE_PERIPHERAL) {
		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_WAKEUP_DEBOUNCE_COUNT(~0);
		val |= USB_WAKE_ON_CNNT_EN_DEV | USB_WAKEUP_DEBOUNCE_COUNT(5);
		writel(val, base + USB_SUSP_CTRL);
	}

	val = readl(base + USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	writel(val, base + USB_SUSP_CTRL);

	val = readl(base + UTMIP_BAT_CHRG_CFG0);
	val |= UTMIP_PD_CHRG;
	writel(val, base + UTMIP_BAT_CHRG_CFG0);

	val = readl(base + UTMIP_XCVR_CFG0);
	val |= UTMIP_FORCE_PD_POWERDOWN | UTMIP_FORCE_PD2_POWERDOWN |
	       UTMIP_FORCE_PDZI_POWERDOWN;
	writel(val, base + UTMIP_XCVR_CFG0);

	val = readl(base + UTMIP_XCVR_CFG1);
	val |= UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
	       UTMIP_FORCE_PDDR_POWERDOWN;
	writel(val, base + UTMIP_XCVR_CFG1);

	return utmip_pad_power_off(phy);
}

static void utmi_phy_preresume(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	val = readl(base + UTMIP_TX_CFG0);
	val |= UTMIP_HS_DISCON_DISABLE;
	writel(val, base + UTMIP_TX_CFG0);
}

static void utmi_phy_postresume(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	val = readl(base + UTMIP_TX_CFG0);
	val &= ~UTMIP_HS_DISCON_DISABLE;
	writel(val, base + UTMIP_TX_CFG0);
}

static void utmi_phy_restore_start(struct tegra_usb_phy *phy,
				   enum tegra_usb_phy_port_speed port_speed)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	val = readl(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_DPDM_OBSERVE_SEL(~0);
	if (port_speed == TEGRA_USB_PHY_PORT_SPEED_LOW)
		val |= UTMIP_DPDM_OBSERVE_SEL_FS_K;
	else
		val |= UTMIP_DPDM_OBSERVE_SEL_FS_J;
	writel(val, base + UTMIP_MISC_CFG0);
	udelay(1);

	val = readl(base + UTMIP_MISC_CFG0);
	val |= UTMIP_DPDM_OBSERVE;
	writel(val, base + UTMIP_MISC_CFG0);
	udelay(10);
}

static void utmi_phy_restore_end(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	val = readl(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_DPDM_OBSERVE;
	writel(val, base + UTMIP_MISC_CFG0);
	udelay(10);
}

static int ulpi_phy_power_on(struct tegra_usb_phy *phy)
{
	int ret;
	unsigned long val;
	void __iomem *base = phy->regs;

	ret = gpio_direction_output(phy->reset_gpio, 0);
	if (ret < 0) {
		dev_err(phy->u_phy.dev, "gpio %d not set to 0\n",
			phy->reset_gpio);
		return ret;
	}
	msleep(5);
	ret = gpio_direction_output(phy->reset_gpio, 1);
	if (ret < 0) {
		dev_err(phy->u_phy.dev, "gpio %d not set to 1\n",
			phy->reset_gpio);
		return ret;
	}

	clk_prepare_enable(phy->clk);
	msleep(1);

	val = readl(base + USB_SUSP_CTRL);
	val |= UHSIC_RESET;
	writel(val, base + USB_SUSP_CTRL);

	val = readl(base + ULPI_TIMING_CTRL_0);
	val |= ULPI_OUTPUT_PINMUX_BYP | ULPI_CLKOUT_PINMUX_BYP;
	writel(val, base + ULPI_TIMING_CTRL_0);

	val = readl(base + USB_SUSP_CTRL);
	val |= ULPI_PHY_ENABLE;
	writel(val, base + USB_SUSP_CTRL);

	val = 0;
	writel(val, base + ULPI_TIMING_CTRL_1);

	val |= ULPI_DATA_TRIMMER_SEL(4);
	val |= ULPI_STPDIRNXT_TRIMMER_SEL(4);
	val |= ULPI_DIR_TRIMMER_SEL(4);
	writel(val, base + ULPI_TIMING_CTRL_1);
	udelay(10);

	val |= ULPI_DATA_TRIMMER_LOAD;
	val |= ULPI_STPDIRNXT_TRIMMER_LOAD;
	val |= ULPI_DIR_TRIMMER_LOAD;
	writel(val, base + ULPI_TIMING_CTRL_1);

	/* Fix VbusInvalid due to floating VBUS */
	ret = usb_phy_io_write(phy->ulpi, 0x40, 0x08);
	if (ret) {
		pr_err("%s: ulpi write failed\n", __func__);
		return ret;
	}

	ret = usb_phy_io_write(phy->ulpi, 0x80, 0x0B);
	if (ret) {
		pr_err("%s: ulpi write failed\n", __func__);
		return ret;
	}

	val = readl(base + USB_SUSP_CTRL);
	val |= USB_SUSP_CLR;
	writel(val, base + USB_SUSP_CTRL);
	udelay(100);

	val = readl(base + USB_SUSP_CTRL);
	val &= ~USB_SUSP_CLR;
	writel(val, base + USB_SUSP_CTRL);

	return 0;
}

static int ulpi_phy_power_off(struct tegra_usb_phy *phy)
{
	clk_disable(phy->clk);
	return gpio_direction_output(phy->reset_gpio, 0);
}

static void tegra_usb_phy_close(struct tegra_usb_phy *phy)
{
	if (!IS_ERR(phy->vbus))
		regulator_disable(phy->vbus);

	clk_disable_unprepare(phy->pll_u);
}

static int tegra_usb_phy_power_on(struct tegra_usb_phy *phy)
{
	if (phy->is_ulpi_phy)
		return ulpi_phy_power_on(phy);
	else
		return utmi_phy_power_on(phy);
}

static int tegra_usb_phy_power_off(struct tegra_usb_phy *phy)
{
	if (phy->is_ulpi_phy)
		return ulpi_phy_power_off(phy);
	else
		return utmi_phy_power_off(phy);
}

static int	tegra_usb_phy_suspend(struct usb_phy *x, int suspend)
{
	struct tegra_usb_phy *phy = container_of(x, struct tegra_usb_phy, u_phy);
	if (suspend)
		return tegra_usb_phy_power_off(phy);
	else
		return tegra_usb_phy_power_on(phy);
}

static int ulpi_open(struct tegra_usb_phy *phy)
{
	int err;

	phy->clk = devm_clk_get(phy->u_phy.dev, "ulpi-link");
	if (IS_ERR(phy->clk)) {
		pr_err("%s: can't get ulpi clock\n", __func__);
		return PTR_ERR(phy->clk);
	}

	err = devm_gpio_request(phy->u_phy.dev, phy->reset_gpio,
		"ulpi_phy_reset_b");
	if (err < 0) {
		dev_err(phy->u_phy.dev, "request failed for gpio: %d\n",
		       phy->reset_gpio);
		return err;
	}

	err = gpio_direction_output(phy->reset_gpio, 0);
	if (err < 0) {
		dev_err(phy->u_phy.dev, "gpio %d direction not set to output\n",
		       phy->reset_gpio);
		return err;
	}

	phy->ulpi = otg_ulpi_create(&ulpi_viewport_access_ops, 0);
	if (!phy->ulpi) {
		dev_err(phy->u_phy.dev, "otg_ulpi_create returned NULL\n");
		err = -ENOMEM;
		return err;
	}

	phy->ulpi->io_priv = phy->regs + ULPI_VIEWPORT;
	return 0;
}

static int tegra_usb_phy_init(struct tegra_usb_phy *phy)
{
	unsigned long parent_rate;
	int i;
	int err;

	phy->pll_u = devm_clk_get(phy->u_phy.dev, "pll_u");
	if (IS_ERR(phy->pll_u)) {
		pr_err("Can't get pll_u clock\n");
		return PTR_ERR(phy->pll_u);
	}

	err = clk_prepare_enable(phy->pll_u);
	if (err)
		return err;

	parent_rate = clk_get_rate(clk_get_parent(phy->pll_u));
	for (i = 0; i < ARRAY_SIZE(tegra_freq_table); i++) {
		if (tegra_freq_table[i].freq == parent_rate) {
			phy->freq = &tegra_freq_table[i];
			break;
		}
	}
	if (!phy->freq) {
		pr_err("invalid pll_u parent rate %ld\n", parent_rate);
		err = -EINVAL;
		goto fail;
	}

	if (!IS_ERR(phy->vbus)) {
		err = regulator_enable(phy->vbus);
		if (err) {
			dev_err(phy->u_phy.dev,
				"failed to enable usb vbus regulator: %d\n",
				err);
			goto fail;
		}
	}

	if (phy->is_ulpi_phy)
		err = ulpi_open(phy);
	else
		err = utmip_pad_open(phy);
	if (err < 0)
		goto fail;

	return 0;

fail:
	clk_disable_unprepare(phy->pll_u);
	return err;
}

void tegra_usb_phy_preresume(struct usb_phy *x)
{
	struct tegra_usb_phy *phy = container_of(x, struct tegra_usb_phy, u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_preresume(phy);
}
EXPORT_SYMBOL_GPL(tegra_usb_phy_preresume);

void tegra_usb_phy_postresume(struct usb_phy *x)
{
	struct tegra_usb_phy *phy = container_of(x, struct tegra_usb_phy, u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_postresume(phy);
}
EXPORT_SYMBOL_GPL(tegra_usb_phy_postresume);

void tegra_ehci_phy_restore_start(struct usb_phy *x,
				 enum tegra_usb_phy_port_speed port_speed)
{
	struct tegra_usb_phy *phy = container_of(x, struct tegra_usb_phy, u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_restore_start(phy, port_speed);
}
EXPORT_SYMBOL_GPL(tegra_ehci_phy_restore_start);

void tegra_ehci_phy_restore_end(struct usb_phy *x)
{
	struct tegra_usb_phy *phy = container_of(x, struct tegra_usb_phy, u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_restore_end(phy);
}
EXPORT_SYMBOL_GPL(tegra_ehci_phy_restore_end);

static int read_utmi_param(struct platform_device *pdev, const char *param,
			   u8 *dest)
{
	u32 value;
	int err = of_property_read_u32(pdev->dev.of_node, param, &value);
	*dest = (u8)value;
	if (err < 0)
		dev_err(&pdev->dev, "Failed to read USB UTMI parameter %s: %d\n",
			param, err);
	return err;
}

static int utmi_phy_probe(struct tegra_usb_phy *tegra_phy,
			  struct platform_device *pdev)
{
	struct resource *res;
	int err;
	struct tegra_utmip_config *config;

	tegra_phy->is_ulpi_phy = false;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get UTMI Pad regs\n");
		return  -ENXIO;
	}

	tegra_phy->pad_regs = devm_ioremap(&pdev->dev, res->start,
		resource_size(res));
	if (!tegra_phy->pad_regs) {
		dev_err(&pdev->dev, "Failed to remap UTMI Pad regs\n");
		return -ENOMEM;
	}

	tegra_phy->config = devm_kzalloc(&pdev->dev, sizeof(*config),
					 GFP_KERNEL);
	if (!tegra_phy->config)
		return -ENOMEM;

	config = tegra_phy->config;

	err = read_utmi_param(pdev, "nvidia,hssync-start-delay",
		&config->hssync_start_delay);
	if (err < 0)
		return err;

	err = read_utmi_param(pdev, "nvidia,elastic-limit",
		&config->elastic_limit);
	if (err < 0)
		return err;

	err = read_utmi_param(pdev, "nvidia,idle-wait-delay",
		&config->idle_wait_delay);
	if (err < 0)
		return err;

	err = read_utmi_param(pdev, "nvidia,term-range-adj",
		&config->term_range_adj);
	if (err < 0)
		return err;

	err = read_utmi_param(pdev, "nvidia,xcvr-lsfslew",
		&config->xcvr_lsfslew);
	if (err < 0)
		return err;

	err = read_utmi_param(pdev, "nvidia,xcvr-lsrslew",
		&config->xcvr_lsrslew);
	if (err < 0)
		return err;

	if (tegra_phy->soc_config->requires_extra_tuning_parameters) {
		err = read_utmi_param(pdev, "nvidia,xcvr-hsslew",
			&config->xcvr_hsslew);
		if (err < 0)
			return err;

		err = read_utmi_param(pdev, "nvidia,hssquelch-level",
			&config->hssquelch_level);
		if (err < 0)
			return err;

		err = read_utmi_param(pdev, "nvidia,hsdiscon-level",
			&config->hsdiscon_level);
		if (err < 0)
			return err;
	}

	config->xcvr_setup_use_fuses = of_property_read_bool(
		pdev->dev.of_node, "nvidia,xcvr-setup-use-fuses");

	if (!config->xcvr_setup_use_fuses) {
		err = read_utmi_param(pdev, "nvidia,xcvr-setup",
			&config->xcvr_setup);
		if (err < 0)
			return err;
	}

	return 0;
}

static const struct tegra_phy_soc_config tegra20_soc_config = {
	.utmi_pll_config_in_car_module = false,
	.has_hostpc = false,
	.requires_usbmode_setup = false,
	.requires_extra_tuning_parameters = false,
};

static const struct tegra_phy_soc_config tegra30_soc_config = {
	.utmi_pll_config_in_car_module = true,
	.has_hostpc = true,
	.requires_usbmode_setup = true,
	.requires_extra_tuning_parameters = true,
};

static const struct of_device_id tegra_usb_phy_id_table[] = {
	{ .compatible = "nvidia,tegra30-usb-phy", .data = &tegra30_soc_config },
	{ .compatible = "nvidia,tegra20-usb-phy", .data = &tegra20_soc_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_usb_phy_id_table);

static int tegra_usb_phy_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct resource *res;
	struct tegra_usb_phy *tegra_phy = NULL;
	struct device_node *np = pdev->dev.of_node;
	enum usb_phy_interface phy_type;
	int err;

	tegra_phy = devm_kzalloc(&pdev->dev, sizeof(*tegra_phy), GFP_KERNEL);
	if (!tegra_phy)
		return -ENOMEM;

	match = of_match_device(tegra_usb_phy_id_table, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}
	tegra_phy->soc_config = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		return  -ENXIO;
	}

	tegra_phy->regs = devm_ioremap(&pdev->dev, res->start,
		resource_size(res));
	if (!tegra_phy->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		return -ENOMEM;
	}

	tegra_phy->is_legacy_phy =
		of_property_read_bool(np, "nvidia,has-legacy-mode");

	phy_type = of_usb_get_phy_mode(np);
	switch (phy_type) {
	case USBPHY_INTERFACE_MODE_UTMI:
		err = utmi_phy_probe(tegra_phy, pdev);
		if (err < 0)
			return err;
		break;

	case USBPHY_INTERFACE_MODE_ULPI:
		tegra_phy->is_ulpi_phy = true;

		tegra_phy->reset_gpio =
			of_get_named_gpio(np, "nvidia,phy-reset-gpio", 0);
		if (!gpio_is_valid(tegra_phy->reset_gpio)) {
			dev_err(&pdev->dev, "invalid gpio: %d\n",
				tegra_phy->reset_gpio);
			return tegra_phy->reset_gpio;
		}
		tegra_phy->config = NULL;
		break;

	default:
		dev_err(&pdev->dev, "phy_type is invalid or unsupported\n");
		return -EINVAL;
	}

	if (of_find_property(np, "dr_mode", NULL))
		tegra_phy->mode = of_usb_get_dr_mode(np);
	else
		tegra_phy->mode = USB_DR_MODE_HOST;

	if (tegra_phy->mode == USB_DR_MODE_UNKNOWN) {
		dev_err(&pdev->dev, "dr_mode is invalid\n");
		return -EINVAL;
	}

	/* On some boards, the VBUS regulator doesn't need to be controlled */
	if (of_find_property(np, "vbus-supply", NULL)) {
		tegra_phy->vbus = devm_regulator_get(&pdev->dev, "vbus");
		if (IS_ERR(tegra_phy->vbus))
			return PTR_ERR(tegra_phy->vbus);
	} else {
		dev_notice(&pdev->dev, "no vbus regulator");
		tegra_phy->vbus = ERR_PTR(-ENODEV);
	}

	tegra_phy->u_phy.dev = &pdev->dev;
	err = tegra_usb_phy_init(tegra_phy);
	if (err < 0)
		return err;

	tegra_phy->u_phy.set_suspend = tegra_usb_phy_suspend;

	platform_set_drvdata(pdev, tegra_phy);

	err = usb_add_phy_dev(&tegra_phy->u_phy);
	if (err < 0) {
		tegra_usb_phy_close(tegra_phy);
		return err;
	}

	return 0;
}

static int tegra_usb_phy_remove(struct platform_device *pdev)
{
	struct tegra_usb_phy *tegra_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&tegra_phy->u_phy);
	tegra_usb_phy_close(tegra_phy);

	return 0;
}

static struct platform_driver tegra_usb_phy_driver = {
	.probe		= tegra_usb_phy_probe,
	.remove		= tegra_usb_phy_remove,
	.driver		= {
		.name	= "tegra-phy",
		.owner	= THIS_MODULE,
		.of_match_table = tegra_usb_phy_id_table,
	},
};
module_platform_driver(tegra_usb_phy_driver);

MODULE_DESCRIPTION("Tegra USB PHY driver");
MODULE_LICENSE("GPL v2");
