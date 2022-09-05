// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 *	Benoit Goby <benoit@android.com>
 *	Venu Byravarasu <vbyravarasu@nvidia.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/regulator/consumer.h>

#include <linux/usb/ehci_def.h>
#include <linux/usb/of.h>
#include <linux/usb/tegra_usb_phy.h>
#include <linux/usb/ulpi.h>

#define ULPI_VIEWPORT				0x170

/* PORTSC PTS/PHCD bits, Tegra20 only */
#define TEGRA_USB_PORTSC1			0x184
#define TEGRA_USB_PORTSC1_PTS(x)		(((x) & 0x3) << 30)
#define TEGRA_USB_PORTSC1_PHCD			BIT(23)

/* HOSTPC1 PTS/PHCD bits, Tegra30 and above */
#define TEGRA_USB_HOSTPC1_DEVLC			0x1b4
#define TEGRA_USB_HOSTPC1_DEVLC_PTS(x)		(((x) & 0x7) << 29)
#define TEGRA_USB_HOSTPC1_DEVLC_PHCD		BIT(22)

/* Bits of PORTSC1, which will get cleared by writing 1 into them */
#define TEGRA_PORTSC1_RWC_BITS	(PORT_CSC | PORT_PEC | PORT_OCC)

#define USB_SUSP_CTRL				0x400
#define   USB_WAKE_ON_RESUME_EN			BIT(2)
#define   USB_WAKE_ON_CNNT_EN_DEV		BIT(3)
#define   USB_WAKE_ON_DISCON_EN_DEV		BIT(4)
#define   USB_SUSP_CLR				BIT(5)
#define   USB_PHY_CLK_VALID			BIT(7)
#define   UTMIP_RESET				BIT(11)
#define   UHSIC_RESET				BIT(11)
#define   UTMIP_PHY_ENABLE			BIT(12)
#define   ULPI_PHY_ENABLE			BIT(13)
#define   USB_SUSP_SET				BIT(14)
#define   USB_WAKEUP_DEBOUNCE_COUNT(x)		(((x) & 0x7) << 16)

#define USB_PHY_VBUS_SENSORS			0x404
#define   B_SESS_VLD_WAKEUP_EN			BIT(14)
#define   A_SESS_VLD_WAKEUP_EN			BIT(22)
#define   A_VBUS_VLD_WAKEUP_EN			BIT(30)

#define USB_PHY_VBUS_WAKEUP_ID			0x408
#define   ID_INT_EN				BIT(0)
#define   ID_CHG_DET				BIT(1)
#define   VBUS_WAKEUP_INT_EN			BIT(8)
#define   VBUS_WAKEUP_CHG_DET			BIT(9)
#define   VBUS_WAKEUP_STS			BIT(10)
#define   VBUS_WAKEUP_WAKEUP_EN			BIT(30)

#define USB1_LEGACY_CTRL			0x410
#define   USB1_NO_LEGACY_MODE			BIT(0)
#define   USB1_VBUS_SENSE_CTL_MASK		(3 << 1)
#define   USB1_VBUS_SENSE_CTL_VBUS_WAKEUP	(0 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD_OR_VBUS_WAKEUP \
						(1 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD	(2 << 1)
#define   USB1_VBUS_SENSE_CTL_A_SESS_VLD	(3 << 1)

#define ULPI_TIMING_CTRL_0			0x424
#define   ULPI_OUTPUT_PINMUX_BYP		BIT(10)
#define   ULPI_CLKOUT_PINMUX_BYP		BIT(11)

#define ULPI_TIMING_CTRL_1			0x428
#define   ULPI_DATA_TRIMMER_LOAD		BIT(0)
#define   ULPI_DATA_TRIMMER_SEL(x)		(((x) & 0x7) << 1)
#define   ULPI_STPDIRNXT_TRIMMER_LOAD		BIT(16)
#define   ULPI_STPDIRNXT_TRIMMER_SEL(x)		(((x) & 0x7) << 17)
#define   ULPI_DIR_TRIMMER_LOAD			BIT(24)
#define   ULPI_DIR_TRIMMER_SEL(x)		(((x) & 0x7) << 25)

#define UTMIP_PLL_CFG1				0x804
#define   UTMIP_XTAL_FREQ_COUNT(x)		(((x) & 0xfff) << 0)
#define   UTMIP_PLLU_ENABLE_DLY_COUNT(x)	(((x) & 0x1f) << 27)

#define UTMIP_XCVR_CFG0				0x808
#define   UTMIP_XCVR_SETUP(x)			(((x) & 0xf) << 0)
#define   UTMIP_XCVR_SETUP_MSB(x)		((((x) & 0x70) >> 4) << 22)
#define   UTMIP_XCVR_LSRSLEW(x)			(((x) & 0x3) << 8)
#define   UTMIP_XCVR_LSFSLEW(x)			(((x) & 0x3) << 10)
#define   UTMIP_FORCE_PD_POWERDOWN		BIT(14)
#define   UTMIP_FORCE_PD2_POWERDOWN		BIT(16)
#define   UTMIP_FORCE_PDZI_POWERDOWN		BIT(18)
#define   UTMIP_XCVR_LSBIAS_SEL			BIT(21)
#define   UTMIP_XCVR_HSSLEW(x)			(((x) & 0x3) << 4)
#define   UTMIP_XCVR_HSSLEW_MSB(x)		((((x) & 0x1fc) >> 2) << 25)

#define UTMIP_BIAS_CFG0				0x80c
#define   UTMIP_OTGPD				BIT(11)
#define   UTMIP_BIASPD				BIT(10)
#define   UTMIP_HSSQUELCH_LEVEL(x)		(((x) & 0x3) << 0)
#define   UTMIP_HSDISCON_LEVEL(x)		(((x) & 0x3) << 2)
#define   UTMIP_HSDISCON_LEVEL_MSB(x)		((((x) & 0x4) >> 2) << 24)

#define UTMIP_HSRX_CFG0				0x810
#define   UTMIP_ELASTIC_LIMIT(x)		(((x) & 0x1f) << 10)
#define   UTMIP_IDLE_WAIT(x)			(((x) & 0x1f) << 15)

#define UTMIP_HSRX_CFG1				0x814
#define   UTMIP_HS_SYNC_START_DLY(x)		(((x) & 0x1f) << 1)

#define UTMIP_TX_CFG0				0x820
#define   UTMIP_FS_PREABMLE_J			BIT(19)
#define   UTMIP_HS_DISCON_DISABLE		BIT(8)

#define UTMIP_MISC_CFG0				0x824
#define   UTMIP_DPDM_OBSERVE			BIT(26)
#define   UTMIP_DPDM_OBSERVE_SEL(x)		(((x) & 0xf) << 27)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_J		UTMIP_DPDM_OBSERVE_SEL(0xf)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_K		UTMIP_DPDM_OBSERVE_SEL(0xe)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_SE1		UTMIP_DPDM_OBSERVE_SEL(0xd)
#define   UTMIP_DPDM_OBSERVE_SEL_FS_SE0		UTMIP_DPDM_OBSERVE_SEL(0xc)
#define   UTMIP_SUSPEND_EXIT_ON_EDGE		BIT(22)

#define UTMIP_MISC_CFG1				0x828
#define   UTMIP_PLL_ACTIVE_DLY_COUNT(x)		(((x) & 0x1f) << 18)
#define   UTMIP_PLLU_STABLE_COUNT(x)		(((x) & 0xfff) << 6)

#define UTMIP_DEBOUNCE_CFG0			0x82c
#define   UTMIP_BIAS_DEBOUNCE_A(x)		(((x) & 0xffff) << 0)

#define UTMIP_BAT_CHRG_CFG0			0x830
#define   UTMIP_PD_CHRG				BIT(0)

#define UTMIP_SPARE_CFG0			0x834
#define   FUSE_SETUP_SEL			BIT(3)

#define UTMIP_XCVR_CFG1				0x838
#define   UTMIP_FORCE_PDDISC_POWERDOWN		BIT(0)
#define   UTMIP_FORCE_PDCHRP_POWERDOWN		BIT(2)
#define   UTMIP_FORCE_PDDR_POWERDOWN		BIT(4)
#define   UTMIP_XCVR_TERM_RANGE_ADJ(x)		(((x) & 0xf) << 18)

#define UTMIP_BIAS_CFG1				0x83c
#define   UTMIP_BIAS_PDTRK_COUNT(x)		(((x) & 0x1f) << 3)

/* For Tegra30 and above only, the address is different in Tegra20 */
#define USB_USBMODE				0x1f8
#define   USB_USBMODE_MASK			(3 << 0)
#define   USB_USBMODE_HOST			(3 << 0)
#define   USB_USBMODE_DEVICE			(2 << 0)

#define PMC_USB_AO				0xf0
#define   VBUS_WAKEUP_PD_P0			BIT(2)
#define   ID_PD_P0				BIT(3)

static DEFINE_SPINLOCK(utmip_pad_lock);
static unsigned int utmip_pad_count;

struct tegra_xtal_freq {
	unsigned int freq;
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

static inline struct tegra_usb_phy *to_tegra_usb_phy(struct usb_phy *u_phy)
{
	return container_of(u_phy, struct tegra_usb_phy, u_phy);
}

static void set_pts(struct tegra_usb_phy *phy, u8 pts_val)
{
	void __iomem *base = phy->regs;
	u32 val;

	if (phy->soc_config->has_hostpc) {
		val = readl_relaxed(base + TEGRA_USB_HOSTPC1_DEVLC);
		val &= ~TEGRA_USB_HOSTPC1_DEVLC_PTS(~0);
		val |= TEGRA_USB_HOSTPC1_DEVLC_PTS(pts_val);
		writel_relaxed(val, base + TEGRA_USB_HOSTPC1_DEVLC);
	} else {
		val = readl_relaxed(base + TEGRA_USB_PORTSC1);
		val &= ~TEGRA_PORTSC1_RWC_BITS;
		val &= ~TEGRA_USB_PORTSC1_PTS(~0);
		val |= TEGRA_USB_PORTSC1_PTS(pts_val);
		writel_relaxed(val, base + TEGRA_USB_PORTSC1);
	}
}

static void set_phcd(struct tegra_usb_phy *phy, bool enable)
{
	void __iomem *base = phy->regs;
	u32 val;

	if (phy->soc_config->has_hostpc) {
		val = readl_relaxed(base + TEGRA_USB_HOSTPC1_DEVLC);
		if (enable)
			val |= TEGRA_USB_HOSTPC1_DEVLC_PHCD;
		else
			val &= ~TEGRA_USB_HOSTPC1_DEVLC_PHCD;
		writel_relaxed(val, base + TEGRA_USB_HOSTPC1_DEVLC);
	} else {
		val = readl_relaxed(base + TEGRA_USB_PORTSC1) & ~PORT_RWC_BITS;
		if (enable)
			val |= TEGRA_USB_PORTSC1_PHCD;
		else
			val &= ~TEGRA_USB_PORTSC1_PHCD;
		writel_relaxed(val, base + TEGRA_USB_PORTSC1);
	}
}

static int utmip_pad_open(struct tegra_usb_phy *phy)
{
	int ret;

	ret = clk_prepare_enable(phy->pad_clk);
	if (ret) {
		dev_err(phy->u_phy.dev,
			"Failed to enable UTMI-pads clock: %d\n", ret);
		return ret;
	}

	spin_lock(&utmip_pad_lock);

	ret = reset_control_deassert(phy->pad_rst);
	if (ret) {
		dev_err(phy->u_phy.dev,
			"Failed to initialize UTMI-pads reset: %d\n", ret);
		goto unlock;
	}

	ret = reset_control_assert(phy->pad_rst);
	if (ret) {
		dev_err(phy->u_phy.dev,
			"Failed to assert UTMI-pads reset: %d\n", ret);
		goto unlock;
	}

	udelay(1);

	ret = reset_control_deassert(phy->pad_rst);
	if (ret)
		dev_err(phy->u_phy.dev,
			"Failed to deassert UTMI-pads reset: %d\n", ret);
unlock:
	spin_unlock(&utmip_pad_lock);

	clk_disable_unprepare(phy->pad_clk);

	return ret;
}

static int utmip_pad_close(struct tegra_usb_phy *phy)
{
	int ret;

	ret = clk_prepare_enable(phy->pad_clk);
	if (ret) {
		dev_err(phy->u_phy.dev,
			"Failed to enable UTMI-pads clock: %d\n", ret);
		return ret;
	}

	ret = reset_control_assert(phy->pad_rst);
	if (ret)
		dev_err(phy->u_phy.dev,
			"Failed to assert UTMI-pads reset: %d\n", ret);

	udelay(1);

	clk_disable_unprepare(phy->pad_clk);

	return ret;
}

static int utmip_pad_power_on(struct tegra_usb_phy *phy)
{
	struct tegra_utmip_config *config = phy->config;
	void __iomem *base = phy->pad_regs;
	u32 val;
	int err;

	err = clk_prepare_enable(phy->pad_clk);
	if (err)
		return err;

	spin_lock(&utmip_pad_lock);

	if (utmip_pad_count++ == 0) {
		val = readl_relaxed(base + UTMIP_BIAS_CFG0);
		val &= ~(UTMIP_OTGPD | UTMIP_BIASPD);

		if (phy->soc_config->requires_extra_tuning_parameters) {
			val &= ~(UTMIP_HSSQUELCH_LEVEL(~0) |
				UTMIP_HSDISCON_LEVEL(~0) |
				UTMIP_HSDISCON_LEVEL_MSB(~0));

			val |= UTMIP_HSSQUELCH_LEVEL(config->hssquelch_level);
			val |= UTMIP_HSDISCON_LEVEL(config->hsdiscon_level);
			val |= UTMIP_HSDISCON_LEVEL_MSB(config->hsdiscon_level);
		}
		writel_relaxed(val, base + UTMIP_BIAS_CFG0);
	}

	if (phy->pad_wakeup) {
		phy->pad_wakeup = false;
		utmip_pad_count--;
	}

	spin_unlock(&utmip_pad_lock);

	clk_disable_unprepare(phy->pad_clk);

	return 0;
}

static int utmip_pad_power_off(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->pad_regs;
	u32 val;
	int ret;

	ret = clk_prepare_enable(phy->pad_clk);
	if (ret)
		return ret;

	spin_lock(&utmip_pad_lock);

	if (!utmip_pad_count) {
		dev_err(phy->u_phy.dev, "UTMIP pad already powered off\n");
		ret = -EINVAL;
		goto ulock;
	}

	/*
	 * In accordance to TRM, OTG and Bias pad circuits could be turned off
	 * to save power if wake is enabled, but the VBUS-change detection
	 * method is board-specific and these circuits may need to be enabled
	 * to generate wakeup event, hence we will just keep them both enabled.
	 */
	if (phy->wakeup_enabled) {
		phy->pad_wakeup = true;
		utmip_pad_count++;
	}

	if (--utmip_pad_count == 0) {
		val = readl_relaxed(base + UTMIP_BIAS_CFG0);
		val |= UTMIP_OTGPD | UTMIP_BIASPD;
		writel_relaxed(val, base + UTMIP_BIAS_CFG0);
	}
ulock:
	spin_unlock(&utmip_pad_lock);

	clk_disable_unprepare(phy->pad_clk);

	return ret;
}

static int utmi_wait_register(void __iomem *reg, u32 mask, u32 result)
{
	u32 tmp;

	return readl_relaxed_poll_timeout(reg, tmp, (tmp & mask) == result,
					  2000, 6000);
}

static void utmi_phy_clk_disable(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;

	/*
	 * The USB driver may have already initiated the phy clock
	 * disable so wait to see if the clock turns off and if not
	 * then proceed with gating the clock.
	 */
	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID, 0) == 0)
		return;

	if (phy->is_legacy_phy) {
		val = readl_relaxed(base + USB_SUSP_CTRL);
		val |= USB_SUSP_SET;
		writel_relaxed(val, base + USB_SUSP_CTRL);

		usleep_range(10, 100);

		val = readl_relaxed(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_SET;
		writel_relaxed(val, base + USB_SUSP_CTRL);
	} else {
		set_phcd(phy, true);
	}

	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID, 0))
		dev_err(phy->u_phy.dev,
			"Timeout waiting for PHY to stabilize on disable\n");
}

static void utmi_phy_clk_enable(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;

	/*
	 * The USB driver may have already initiated the phy clock
	 * enable so wait to see if the clock turns on and if not
	 * then proceed with ungating the clock.
	 */
	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID,
			       USB_PHY_CLK_VALID) == 0)
		return;

	if (phy->is_legacy_phy) {
		val = readl_relaxed(base + USB_SUSP_CTRL);
		val |= USB_SUSP_CLR;
		writel_relaxed(val, base + USB_SUSP_CTRL);

		usleep_range(10, 100);

		val = readl_relaxed(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_CLR;
		writel_relaxed(val, base + USB_SUSP_CTRL);
	} else {
		set_phcd(phy, false);
	}

	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID,
			       USB_PHY_CLK_VALID))
		dev_err(phy->u_phy.dev,
			"Timeout waiting for PHY to stabilize on enable\n");
}

static int utmi_phy_power_on(struct tegra_usb_phy *phy)
{
	struct tegra_utmip_config *config = phy->config;
	void __iomem *base = phy->regs;
	u32 val;
	int err;

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	writel_relaxed(val, base + USB_SUSP_CTRL);

	if (phy->is_legacy_phy) {
		val = readl_relaxed(base + USB1_LEGACY_CTRL);
		val |= USB1_NO_LEGACY_MODE;
		writel_relaxed(val, base + USB1_LEGACY_CTRL);
	}

	val = readl_relaxed(base + UTMIP_TX_CFG0);
	val |= UTMIP_FS_PREABMLE_J;
	writel_relaxed(val, base + UTMIP_TX_CFG0);

	val = readl_relaxed(base + UTMIP_HSRX_CFG0);
	val &= ~(UTMIP_IDLE_WAIT(~0) | UTMIP_ELASTIC_LIMIT(~0));
	val |= UTMIP_IDLE_WAIT(config->idle_wait_delay);
	val |= UTMIP_ELASTIC_LIMIT(config->elastic_limit);
	writel_relaxed(val, base + UTMIP_HSRX_CFG0);

	val = readl_relaxed(base + UTMIP_HSRX_CFG1);
	val &= ~UTMIP_HS_SYNC_START_DLY(~0);
	val |= UTMIP_HS_SYNC_START_DLY(config->hssync_start_delay);
	writel_relaxed(val, base + UTMIP_HSRX_CFG1);

	val = readl_relaxed(base + UTMIP_DEBOUNCE_CFG0);
	val &= ~UTMIP_BIAS_DEBOUNCE_A(~0);
	val |= UTMIP_BIAS_DEBOUNCE_A(phy->freq->debounce);
	writel_relaxed(val, base + UTMIP_DEBOUNCE_CFG0);

	val = readl_relaxed(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_SUSPEND_EXIT_ON_EDGE;
	writel_relaxed(val, base + UTMIP_MISC_CFG0);

	if (!phy->soc_config->utmi_pll_config_in_car_module) {
		val = readl_relaxed(base + UTMIP_MISC_CFG1);
		val &= ~(UTMIP_PLL_ACTIVE_DLY_COUNT(~0) |
			UTMIP_PLLU_STABLE_COUNT(~0));
		val |= UTMIP_PLL_ACTIVE_DLY_COUNT(phy->freq->active_delay) |
			UTMIP_PLLU_STABLE_COUNT(phy->freq->stable_count);
		writel_relaxed(val, base + UTMIP_MISC_CFG1);

		val = readl_relaxed(base + UTMIP_PLL_CFG1);
		val &= ~(UTMIP_XTAL_FREQ_COUNT(~0) |
			UTMIP_PLLU_ENABLE_DLY_COUNT(~0));
		val |= UTMIP_XTAL_FREQ_COUNT(phy->freq->xtal_freq_count) |
			UTMIP_PLLU_ENABLE_DLY_COUNT(phy->freq->enable_delay);
		writel_relaxed(val, base + UTMIP_PLL_CFG1);
	}

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val &= ~USB_WAKE_ON_RESUME_EN;
	writel_relaxed(val, base + USB_SUSP_CTRL);

	if (phy->mode != USB_DR_MODE_HOST) {
		val = readl_relaxed(base + USB_SUSP_CTRL);
		val &= ~(USB_WAKE_ON_CNNT_EN_DEV | USB_WAKE_ON_DISCON_EN_DEV);
		writel_relaxed(val, base + USB_SUSP_CTRL);

		val = readl_relaxed(base + USB_PHY_VBUS_WAKEUP_ID);
		val &= ~VBUS_WAKEUP_WAKEUP_EN;
		val &= ~(ID_CHG_DET | VBUS_WAKEUP_CHG_DET);
		writel_relaxed(val, base + USB_PHY_VBUS_WAKEUP_ID);

		val = readl_relaxed(base + USB_PHY_VBUS_SENSORS);
		val &= ~(A_VBUS_VLD_WAKEUP_EN | A_SESS_VLD_WAKEUP_EN);
		val &= ~(B_SESS_VLD_WAKEUP_EN);
		writel_relaxed(val, base + USB_PHY_VBUS_SENSORS);

		val = readl_relaxed(base + UTMIP_BAT_CHRG_CFG0);
		val &= ~UTMIP_PD_CHRG;
		writel_relaxed(val, base + UTMIP_BAT_CHRG_CFG0);
	} else {
		val = readl_relaxed(base + UTMIP_BAT_CHRG_CFG0);
		val |= UTMIP_PD_CHRG;
		writel_relaxed(val, base + UTMIP_BAT_CHRG_CFG0);
	}

	err = utmip_pad_power_on(phy);
	if (err)
		return err;

	val = readl_relaxed(base + UTMIP_XCVR_CFG0);
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
	writel_relaxed(val, base + UTMIP_XCVR_CFG0);

	val = readl_relaxed(base + UTMIP_XCVR_CFG1);
	val &= ~(UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
		 UTMIP_FORCE_PDDR_POWERDOWN | UTMIP_XCVR_TERM_RANGE_ADJ(~0));
	val |= UTMIP_XCVR_TERM_RANGE_ADJ(config->term_range_adj);
	writel_relaxed(val, base + UTMIP_XCVR_CFG1);

	val = readl_relaxed(base + UTMIP_BIAS_CFG1);
	val &= ~UTMIP_BIAS_PDTRK_COUNT(~0);
	val |= UTMIP_BIAS_PDTRK_COUNT(0x5);
	writel_relaxed(val, base + UTMIP_BIAS_CFG1);

	val = readl_relaxed(base + UTMIP_SPARE_CFG0);
	if (config->xcvr_setup_use_fuses)
		val |= FUSE_SETUP_SEL;
	else
		val &= ~FUSE_SETUP_SEL;
	writel_relaxed(val, base + UTMIP_SPARE_CFG0);

	if (!phy->is_legacy_phy) {
		val = readl_relaxed(base + USB_SUSP_CTRL);
		val |= UTMIP_PHY_ENABLE;
		writel_relaxed(val, base + USB_SUSP_CTRL);
	}

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val &= ~UTMIP_RESET;
	writel_relaxed(val, base + USB_SUSP_CTRL);

	if (phy->is_legacy_phy) {
		val = readl_relaxed(base + USB1_LEGACY_CTRL);
		val &= ~USB1_VBUS_SENSE_CTL_MASK;
		val |= USB1_VBUS_SENSE_CTL_A_SESS_VLD;
		writel_relaxed(val, base + USB1_LEGACY_CTRL);

		val = readl_relaxed(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_SET;
		writel_relaxed(val, base + USB_SUSP_CTRL);
	}

	utmi_phy_clk_enable(phy);

	if (phy->soc_config->requires_usbmode_setup) {
		val = readl_relaxed(base + USB_USBMODE);
		val &= ~USB_USBMODE_MASK;
		if (phy->mode == USB_DR_MODE_HOST)
			val |= USB_USBMODE_HOST;
		else
			val |= USB_USBMODE_DEVICE;
		writel_relaxed(val, base + USB_USBMODE);
	}

	if (!phy->is_legacy_phy)
		set_pts(phy, 0);

	return 0;
}

static int utmi_phy_power_off(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;

	/*
	 * Give hardware time to settle down after VBUS disconnection,
	 * otherwise PHY will immediately wake up from suspend.
	 */
	if (phy->wakeup_enabled && phy->mode != USB_DR_MODE_HOST)
		readl_relaxed_poll_timeout(base + USB_PHY_VBUS_WAKEUP_ID,
					   val, !(val & VBUS_WAKEUP_STS),
					   5000, 100000);

	utmi_phy_clk_disable(phy);

	/* PHY won't resume if reset is asserted */
	if (!phy->wakeup_enabled) {
		val = readl_relaxed(base + USB_SUSP_CTRL);
		val |= UTMIP_RESET;
		writel_relaxed(val, base + USB_SUSP_CTRL);
	}

	val = readl_relaxed(base + UTMIP_BAT_CHRG_CFG0);
	val |= UTMIP_PD_CHRG;
	writel_relaxed(val, base + UTMIP_BAT_CHRG_CFG0);

	if (!phy->wakeup_enabled) {
		val = readl_relaxed(base + UTMIP_XCVR_CFG0);
		val |= UTMIP_FORCE_PD_POWERDOWN | UTMIP_FORCE_PD2_POWERDOWN |
		       UTMIP_FORCE_PDZI_POWERDOWN;
		writel_relaxed(val, base + UTMIP_XCVR_CFG0);
	}

	val = readl_relaxed(base + UTMIP_XCVR_CFG1);
	val |= UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
	       UTMIP_FORCE_PDDR_POWERDOWN;
	writel_relaxed(val, base + UTMIP_XCVR_CFG1);

	if (phy->wakeup_enabled) {
		val = readl_relaxed(base + USB_SUSP_CTRL);
		val &= ~USB_WAKEUP_DEBOUNCE_COUNT(~0);
		val |= USB_WAKEUP_DEBOUNCE_COUNT(5);
		val |= USB_WAKE_ON_RESUME_EN;
		writel_relaxed(val, base + USB_SUSP_CTRL);

		/*
		 * Ask VBUS sensor to generate wake event once cable is
		 * connected.
		 */
		if (phy->mode != USB_DR_MODE_HOST) {
			val = readl_relaxed(base + USB_PHY_VBUS_WAKEUP_ID);
			val |= VBUS_WAKEUP_WAKEUP_EN;
			val &= ~(ID_CHG_DET | VBUS_WAKEUP_CHG_DET);
			writel_relaxed(val, base + USB_PHY_VBUS_WAKEUP_ID);

			val = readl_relaxed(base + USB_PHY_VBUS_SENSORS);
			val |= A_VBUS_VLD_WAKEUP_EN;
			writel_relaxed(val, base + USB_PHY_VBUS_SENSORS);
		}
	}

	return utmip_pad_power_off(phy);
}

static void utmi_phy_preresume(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;

	val = readl_relaxed(base + UTMIP_TX_CFG0);
	val |= UTMIP_HS_DISCON_DISABLE;
	writel_relaxed(val, base + UTMIP_TX_CFG0);
}

static void utmi_phy_postresume(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;

	val = readl_relaxed(base + UTMIP_TX_CFG0);
	val &= ~UTMIP_HS_DISCON_DISABLE;
	writel_relaxed(val, base + UTMIP_TX_CFG0);
}

static void utmi_phy_restore_start(struct tegra_usb_phy *phy,
				   enum tegra_usb_phy_port_speed port_speed)
{
	void __iomem *base = phy->regs;
	u32 val;

	val = readl_relaxed(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_DPDM_OBSERVE_SEL(~0);
	if (port_speed == TEGRA_USB_PHY_PORT_SPEED_LOW)
		val |= UTMIP_DPDM_OBSERVE_SEL_FS_K;
	else
		val |= UTMIP_DPDM_OBSERVE_SEL_FS_J;
	writel_relaxed(val, base + UTMIP_MISC_CFG0);
	usleep_range(1, 10);

	val = readl_relaxed(base + UTMIP_MISC_CFG0);
	val |= UTMIP_DPDM_OBSERVE;
	writel_relaxed(val, base + UTMIP_MISC_CFG0);
	usleep_range(10, 100);
}

static void utmi_phy_restore_end(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;

	val = readl_relaxed(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_DPDM_OBSERVE;
	writel_relaxed(val, base + UTMIP_MISC_CFG0);
	usleep_range(10, 100);
}

static int ulpi_phy_power_on(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	u32 val;
	int err;

	gpiod_set_value_cansleep(phy->reset_gpio, 1);

	err = clk_prepare_enable(phy->clk);
	if (err)
		return err;

	usleep_range(5000, 6000);

	gpiod_set_value_cansleep(phy->reset_gpio, 0);

	usleep_range(1000, 2000);

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val |= UHSIC_RESET;
	writel_relaxed(val, base + USB_SUSP_CTRL);

	val = readl_relaxed(base + ULPI_TIMING_CTRL_0);
	val |= ULPI_OUTPUT_PINMUX_BYP | ULPI_CLKOUT_PINMUX_BYP;
	writel_relaxed(val, base + ULPI_TIMING_CTRL_0);

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val |= ULPI_PHY_ENABLE;
	writel_relaxed(val, base + USB_SUSP_CTRL);

	val = 0;
	writel_relaxed(val, base + ULPI_TIMING_CTRL_1);

	val |= ULPI_DATA_TRIMMER_SEL(4);
	val |= ULPI_STPDIRNXT_TRIMMER_SEL(4);
	val |= ULPI_DIR_TRIMMER_SEL(4);
	writel_relaxed(val, base + ULPI_TIMING_CTRL_1);
	usleep_range(10, 100);

	val |= ULPI_DATA_TRIMMER_LOAD;
	val |= ULPI_STPDIRNXT_TRIMMER_LOAD;
	val |= ULPI_DIR_TRIMMER_LOAD;
	writel_relaxed(val, base + ULPI_TIMING_CTRL_1);

	/* Fix VbusInvalid due to floating VBUS */
	err = usb_phy_io_write(phy->ulpi, 0x40, 0x08);
	if (err) {
		dev_err(phy->u_phy.dev, "ULPI write failed: %d\n", err);
		goto disable_clk;
	}

	err = usb_phy_io_write(phy->ulpi, 0x80, 0x0B);
	if (err) {
		dev_err(phy->u_phy.dev, "ULPI write failed: %d\n", err);
		goto disable_clk;
	}

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val |= USB_SUSP_CLR;
	writel_relaxed(val, base + USB_SUSP_CTRL);
	usleep_range(100, 1000);

	val = readl_relaxed(base + USB_SUSP_CTRL);
	val &= ~USB_SUSP_CLR;
	writel_relaxed(val, base + USB_SUSP_CTRL);

	return 0;

disable_clk:
	clk_disable_unprepare(phy->clk);

	return err;
}

static int ulpi_phy_power_off(struct tegra_usb_phy *phy)
{
	gpiod_set_value_cansleep(phy->reset_gpio, 1);
	usleep_range(5000, 6000);
	clk_disable_unprepare(phy->clk);

	/*
	 * Wakeup currently unimplemented for ULPI, thus PHY needs to be
	 * force-resumed.
	 */
	if (WARN_ON_ONCE(phy->wakeup_enabled)) {
		ulpi_phy_power_on(phy);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int tegra_usb_phy_power_on(struct tegra_usb_phy *phy)
{
	int err;

	if (phy->powered_on)
		return 0;

	if (phy->is_ulpi_phy)
		err = ulpi_phy_power_on(phy);
	else
		err = utmi_phy_power_on(phy);
	if (err)
		return err;

	phy->powered_on = true;

	/* Let PHY settle down */
	usleep_range(2000, 2500);

	return 0;
}

static int tegra_usb_phy_power_off(struct tegra_usb_phy *phy)
{
	int err;

	if (!phy->powered_on)
		return 0;

	if (phy->is_ulpi_phy)
		err = ulpi_phy_power_off(phy);
	else
		err = utmi_phy_power_off(phy);
	if (err)
		return err;

	phy->powered_on = false;

	return 0;
}

static void tegra_usb_phy_shutdown(struct usb_phy *u_phy)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);

	if (WARN_ON(!phy->freq))
		return;

	usb_phy_set_wakeup(u_phy, false);
	tegra_usb_phy_power_off(phy);

	if (!phy->is_ulpi_phy)
		utmip_pad_close(phy);

	regulator_disable(phy->vbus);
	clk_disable_unprepare(phy->pll_u);

	phy->freq = NULL;
}

static irqreturn_t tegra_usb_phy_isr(int irq, void *data)
{
	u32 val, int_mask = ID_CHG_DET | VBUS_WAKEUP_CHG_DET;
	struct tegra_usb_phy *phy = data;
	void __iomem *base = phy->regs;

	/*
	 * The PHY interrupt also wakes the USB controller driver since
	 * interrupt is shared. We don't do anything in the PHY driver,
	 * so just clear the interrupt.
	 */
	val = readl_relaxed(base + USB_PHY_VBUS_WAKEUP_ID);
	writel_relaxed(val, base + USB_PHY_VBUS_WAKEUP_ID);

	return val & int_mask ? IRQ_HANDLED : IRQ_NONE;
}

static int tegra_usb_phy_set_wakeup(struct usb_phy *u_phy, bool enable)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);
	void __iomem *base = phy->regs;
	int ret = 0;
	u32 val;

	if (phy->wakeup_enabled && phy->mode != USB_DR_MODE_HOST &&
	    phy->irq > 0) {
		disable_irq(phy->irq);

		val = readl_relaxed(base + USB_PHY_VBUS_WAKEUP_ID);
		val &= ~(ID_INT_EN | VBUS_WAKEUP_INT_EN);
		writel_relaxed(val, base + USB_PHY_VBUS_WAKEUP_ID);

		enable_irq(phy->irq);

		free_irq(phy->irq, phy);

		phy->wakeup_enabled = false;
	}

	if (enable && phy->mode != USB_DR_MODE_HOST && phy->irq > 0) {
		ret = request_irq(phy->irq, tegra_usb_phy_isr, IRQF_SHARED,
				  dev_name(phy->u_phy.dev), phy);
		if (!ret) {
			disable_irq(phy->irq);

			/*
			 * USB clock will be resumed once wake event will be
			 * generated.  The ID-change event requires to have
			 * interrupts enabled, otherwise it won't be generated.
			 */
			val = readl_relaxed(base + USB_PHY_VBUS_WAKEUP_ID);
			val |= ID_INT_EN | VBUS_WAKEUP_INT_EN;
			writel_relaxed(val, base + USB_PHY_VBUS_WAKEUP_ID);

			enable_irq(phy->irq);
		} else {
			dev_err(phy->u_phy.dev,
				"Failed to request interrupt: %d", ret);
			enable = false;
		}
	}

	phy->wakeup_enabled = enable;

	return ret;
}

static int tegra_usb_phy_set_suspend(struct usb_phy *u_phy, int suspend)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);
	int ret;

	if (WARN_ON(!phy->freq))
		return -EINVAL;

	/*
	 * PHY is sharing IRQ with the CI driver, hence here we either
	 * disable interrupt for both PHY and CI or for CI only.  The
	 * interrupt needs to be disabled while hardware is reprogrammed
	 * because interrupt touches the programmed registers, and thus,
	 * there could be a race condition.
	 */
	if (phy->irq > 0)
		disable_irq(phy->irq);

	if (suspend)
		ret = tegra_usb_phy_power_off(phy);
	else
		ret = tegra_usb_phy_power_on(phy);

	if (phy->irq > 0)
		enable_irq(phy->irq);

	return ret;
}

static int tegra_usb_phy_configure_pmc(struct tegra_usb_phy *phy)
{
	int err, val = 0;

	/* older device-trees don't have PMC regmap */
	if (!phy->pmc_regmap)
		return 0;

	/*
	 * Tegra20 has a different layout of PMC USB register bits and AO is
	 * enabled by default after system reset on Tegra20, so assume nothing
	 * to do on Tegra20.
	 */
	if (!phy->soc_config->requires_pmc_ao_power_up)
		return 0;

	/* enable VBUS wake-up detector */
	if (phy->mode != USB_DR_MODE_HOST)
		val |= VBUS_WAKEUP_PD_P0 << phy->instance * 4;

	/* enable ID-pin ACC detector for OTG mode switching */
	if (phy->mode == USB_DR_MODE_OTG)
		val |= ID_PD_P0 << phy->instance * 4;

	/* disable detectors to reset them */
	err = regmap_set_bits(phy->pmc_regmap, PMC_USB_AO, val);
	if (err) {
		dev_err(phy->u_phy.dev, "Failed to disable PMC AO: %d\n", err);
		return err;
	}

	usleep_range(10, 100);

	/* enable detectors */
	err = regmap_clear_bits(phy->pmc_regmap, PMC_USB_AO, val);
	if (err) {
		dev_err(phy->u_phy.dev, "Failed to enable PMC AO: %d\n", err);
		return err;
	}

	/* detectors starts to work after 10ms */
	usleep_range(10000, 15000);

	return 0;
}

static int tegra_usb_phy_init(struct usb_phy *u_phy)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);
	unsigned long parent_rate;
	unsigned int i;
	int err;

	if (WARN_ON(phy->freq))
		return 0;

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
		dev_err(phy->u_phy.dev, "Invalid pll_u parent rate %ld\n",
			parent_rate);
		err = -EINVAL;
		goto disable_clk;
	}

	err = regulator_enable(phy->vbus);
	if (err) {
		dev_err(phy->u_phy.dev,
			"Failed to enable USB VBUS regulator: %d\n", err);
		goto disable_clk;
	}

	if (!phy->is_ulpi_phy) {
		err = utmip_pad_open(phy);
		if (err)
			goto disable_vbus;
	}

	err = tegra_usb_phy_configure_pmc(phy);
	if (err)
		goto close_phy;

	err = tegra_usb_phy_power_on(phy);
	if (err)
		goto close_phy;

	return 0;

close_phy:
	if (!phy->is_ulpi_phy)
		utmip_pad_close(phy);

disable_vbus:
	regulator_disable(phy->vbus);

disable_clk:
	clk_disable_unprepare(phy->pll_u);

	phy->freq = NULL;

	return err;
}

void tegra_usb_phy_preresume(struct usb_phy *u_phy)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_preresume(phy);
}
EXPORT_SYMBOL_GPL(tegra_usb_phy_preresume);

void tegra_usb_phy_postresume(struct usb_phy *u_phy)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_postresume(phy);
}
EXPORT_SYMBOL_GPL(tegra_usb_phy_postresume);

void tegra_ehci_phy_restore_start(struct usb_phy *u_phy,
				  enum tegra_usb_phy_port_speed port_speed)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_restore_start(phy, port_speed);
}
EXPORT_SYMBOL_GPL(tegra_ehci_phy_restore_start);

void tegra_ehci_phy_restore_end(struct usb_phy *u_phy)
{
	struct tegra_usb_phy *phy = to_tegra_usb_phy(u_phy);

	if (!phy->is_ulpi_phy)
		utmi_phy_restore_end(phy);
}
EXPORT_SYMBOL_GPL(tegra_ehci_phy_restore_end);

static int read_utmi_param(struct platform_device *pdev, const char *param,
			   u8 *dest)
{
	u32 value;
	int err;

	err = of_property_read_u32(pdev->dev.of_node, param, &value);
	if (err)
		dev_err(&pdev->dev,
			"Failed to read USB UTMI parameter %s: %d\n",
			param, err);
	else
		*dest = value;

	return err;
}

static int utmi_phy_probe(struct tegra_usb_phy *tegra_phy,
			  struct platform_device *pdev)
{
	struct tegra_utmip_config *config;
	struct resource *res;
	int err;

	tegra_phy->is_ulpi_phy = false;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get UTMI pad regs\n");
		return  -ENXIO;
	}

	/*
	 * Note that UTMI pad registers are shared by all PHYs, therefore
	 * devm_platform_ioremap_resource() can't be used here.
	 */
	tegra_phy->pad_regs = devm_ioremap(&pdev->dev, res->start,
					   resource_size(res));
	if (!tegra_phy->pad_regs) {
		dev_err(&pdev->dev, "Failed to remap UTMI pad regs\n");
		return -ENOMEM;
	}

	tegra_phy->config = devm_kzalloc(&pdev->dev, sizeof(*config),
					 GFP_KERNEL);
	if (!tegra_phy->config)
		return -ENOMEM;

	config = tegra_phy->config;

	err = read_utmi_param(pdev, "nvidia,hssync-start-delay",
			      &config->hssync_start_delay);
	if (err)
		return err;

	err = read_utmi_param(pdev, "nvidia,elastic-limit",
			      &config->elastic_limit);
	if (err)
		return err;

	err = read_utmi_param(pdev, "nvidia,idle-wait-delay",
			      &config->idle_wait_delay);
	if (err)
		return err;

	err = read_utmi_param(pdev, "nvidia,term-range-adj",
			      &config->term_range_adj);
	if (err)
		return err;

	err = read_utmi_param(pdev, "nvidia,xcvr-lsfslew",
			      &config->xcvr_lsfslew);
	if (err)
		return err;

	err = read_utmi_param(pdev, "nvidia,xcvr-lsrslew",
			      &config->xcvr_lsrslew);
	if (err)
		return err;

	if (tegra_phy->soc_config->requires_extra_tuning_parameters) {
		err = read_utmi_param(pdev, "nvidia,xcvr-hsslew",
				      &config->xcvr_hsslew);
		if (err)
			return err;

		err = read_utmi_param(pdev, "nvidia,hssquelch-level",
				      &config->hssquelch_level);
		if (err)
			return err;

		err = read_utmi_param(pdev, "nvidia,hsdiscon-level",
				      &config->hsdiscon_level);
		if (err)
			return err;
	}

	config->xcvr_setup_use_fuses = of_property_read_bool(
		pdev->dev.of_node, "nvidia,xcvr-setup-use-fuses");

	if (!config->xcvr_setup_use_fuses) {
		err = read_utmi_param(pdev, "nvidia,xcvr-setup",
				      &config->xcvr_setup);
		if (err)
			return err;
	}

	return 0;
}

static void tegra_usb_phy_put_pmc_device(void *dev)
{
	put_device(dev);
}

static int tegra_usb_phy_parse_pmc(struct device *dev,
				   struct tegra_usb_phy *phy)
{
	struct platform_device *pmc_pdev;
	struct of_phandle_args args;
	int err;

	err = of_parse_phandle_with_fixed_args(dev->of_node, "nvidia,pmc",
					       1, 0, &args);
	if (err) {
		if (err != -ENOENT)
			return err;

		dev_warn_once(dev, "nvidia,pmc is missing, please update your device-tree\n");
		return 0;
	}

	pmc_pdev = of_find_device_by_node(args.np);
	of_node_put(args.np);
	if (!pmc_pdev)
		return -ENODEV;

	err = devm_add_action_or_reset(dev, tegra_usb_phy_put_pmc_device,
				       &pmc_pdev->dev);
	if (err)
		return err;

	if (!platform_get_drvdata(pmc_pdev))
		return -EPROBE_DEFER;

	phy->pmc_regmap = dev_get_regmap(&pmc_pdev->dev, "usb_sleepwalk");
	if (!phy->pmc_regmap)
		return -EINVAL;

	phy->instance = args.args[0];

	return 0;
}

static const struct tegra_phy_soc_config tegra20_soc_config = {
	.utmi_pll_config_in_car_module = false,
	.has_hostpc = false,
	.requires_usbmode_setup = false,
	.requires_extra_tuning_parameters = false,
	.requires_pmc_ao_power_up = false,
};

static const struct tegra_phy_soc_config tegra30_soc_config = {
	.utmi_pll_config_in_car_module = true,
	.has_hostpc = true,
	.requires_usbmode_setup = true,
	.requires_extra_tuning_parameters = true,
	.requires_pmc_ao_power_up = true,
};

static const struct of_device_id tegra_usb_phy_id_table[] = {
	{ .compatible = "nvidia,tegra30-usb-phy", .data = &tegra30_soc_config },
	{ .compatible = "nvidia,tegra20-usb-phy", .data = &tegra20_soc_config },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_usb_phy_id_table);

static int tegra_usb_phy_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct tegra_usb_phy *tegra_phy;
	enum usb_phy_interface phy_type;
	struct reset_control *reset;
	struct gpio_desc *gpiod;
	struct resource *res;
	struct usb_phy *phy;
	int err;

	tegra_phy = devm_kzalloc(&pdev->dev, sizeof(*tegra_phy), GFP_KERNEL);
	if (!tegra_phy)
		return -ENOMEM;

	tegra_phy->soc_config = of_device_get_match_data(&pdev->dev);
	tegra_phy->irq = platform_get_irq_optional(pdev, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		return  -ENXIO;
	}

	/*
	 * Note that PHY and USB controller are using shared registers,
	 * therefore devm_platform_ioremap_resource() can't be used here.
	 */
	tegra_phy->regs = devm_ioremap(&pdev->dev, res->start,
				       resource_size(res));
	if (!tegra_phy->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		return -ENOMEM;
	}

	tegra_phy->is_legacy_phy =
		of_property_read_bool(np, "nvidia,has-legacy-mode");

	if (of_find_property(np, "dr_mode", NULL))
		tegra_phy->mode = usb_get_dr_mode(&pdev->dev);
	else
		tegra_phy->mode = USB_DR_MODE_HOST;

	if (tegra_phy->mode == USB_DR_MODE_UNKNOWN) {
		dev_err(&pdev->dev, "dr_mode is invalid\n");
		return -EINVAL;
	}

	/* On some boards, the VBUS regulator doesn't need to be controlled */
	tegra_phy->vbus = devm_regulator_get(&pdev->dev, "vbus");
	if (IS_ERR(tegra_phy->vbus))
		return PTR_ERR(tegra_phy->vbus);

	tegra_phy->pll_u = devm_clk_get(&pdev->dev, "pll_u");
	err = PTR_ERR_OR_ZERO(tegra_phy->pll_u);
	if (err) {
		dev_err(&pdev->dev, "Failed to get pll_u clock: %d\n", err);
		return err;
	}

	err = tegra_usb_phy_parse_pmc(&pdev->dev, tegra_phy);
	if (err) {
		dev_err_probe(&pdev->dev, err, "Failed to get PMC regmap\n");
		return err;
	}

	phy_type = of_usb_get_phy_mode(np);
	switch (phy_type) {
	case USBPHY_INTERFACE_MODE_UTMI:
		err = utmi_phy_probe(tegra_phy, pdev);
		if (err)
			return err;

		tegra_phy->pad_clk = devm_clk_get(&pdev->dev, "utmi-pads");
		err = PTR_ERR_OR_ZERO(tegra_phy->pad_clk);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to get UTMIP pad clock: %d\n", err);
			return err;
		}

		reset = devm_reset_control_get_optional_shared(&pdev->dev,
							       "utmi-pads");
		err = PTR_ERR_OR_ZERO(reset);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to get UTMI-pads reset: %d\n", err);
			return err;
		}
		tegra_phy->pad_rst = reset;
		break;

	case USBPHY_INTERFACE_MODE_ULPI:
		tegra_phy->is_ulpi_phy = true;

		tegra_phy->clk = devm_clk_get(&pdev->dev, "ulpi-link");
		err = PTR_ERR_OR_ZERO(tegra_phy->clk);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to get ULPI clock: %d\n", err);
			return err;
		}

		gpiod = devm_gpiod_get(&pdev->dev, "nvidia,phy-reset",
				       GPIOD_OUT_HIGH);
		err = PTR_ERR_OR_ZERO(gpiod);
		if (err) {
			dev_err(&pdev->dev,
				"Request failed for reset GPIO: %d\n", err);
			return err;
		}

		err = gpiod_set_consumer_name(gpiod, "ulpi_phy_reset_b");
		if (err) {
			dev_err(&pdev->dev,
				"Failed to set up reset GPIO name: %d\n", err);
			return err;
		}

		tegra_phy->reset_gpio = gpiod;

		phy = devm_otg_ulpi_create(&pdev->dev,
					   &ulpi_viewport_access_ops, 0);
		if (!phy) {
			dev_err(&pdev->dev, "Failed to create ULPI OTG\n");
			return -ENOMEM;
		}

		tegra_phy->ulpi = phy;
		tegra_phy->ulpi->io_priv = tegra_phy->regs + ULPI_VIEWPORT;
		break;

	default:
		dev_err(&pdev->dev, "phy_type %u is invalid or unsupported\n",
			phy_type);
		return -EINVAL;
	}

	tegra_phy->u_phy.dev = &pdev->dev;
	tegra_phy->u_phy.init = tegra_usb_phy_init;
	tegra_phy->u_phy.shutdown = tegra_usb_phy_shutdown;
	tegra_phy->u_phy.set_wakeup = tegra_usb_phy_set_wakeup;
	tegra_phy->u_phy.set_suspend = tegra_usb_phy_set_suspend;

	platform_set_drvdata(pdev, tegra_phy);

	return usb_add_phy_dev(&tegra_phy->u_phy);
}

static int tegra_usb_phy_remove(struct platform_device *pdev)
{
	struct tegra_usb_phy *tegra_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&tegra_phy->u_phy);

	return 0;
}

static struct platform_driver tegra_usb_phy_driver = {
	.probe		= tegra_usb_phy_probe,
	.remove		= tegra_usb_phy_remove,
	.driver		= {
		.name	= "tegra-phy",
		.of_match_table = tegra_usb_phy_id_table,
	},
};
module_platform_driver(tegra_usb_phy_driver);

MODULE_DESCRIPTION("Tegra USB PHY driver");
MODULE_LICENSE("GPL v2");
