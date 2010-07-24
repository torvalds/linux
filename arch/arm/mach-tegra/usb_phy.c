/*
 * arch/arm/mach-tegra/usb_phy.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 *	Benoit Goby <benoit@android.com>
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
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/mach-types.h>
#include <mach/usb_phy.h>

#define USB_PORTSC1		0x184
#define   USB_PORTSC1_PTS(x)	(((x) & 0x3) << 30)
#define   USB_PORTSC1_PHCD	(1 << 23)

#define USB_SUSP_CTRL		0x400
#define   USB_WAKE_ON_CNNT_EN_DEV	(1 << 3)
#define   USB_WAKE_ON_DISCON_EN_DEV	(1 << 4)
#define   USB_SUSP_CLR		(1 << 5)
#define   USB_PHY_CLK_VALID	(1 << 7)
#define   UTMIP_RESET			(1 << 11)
#define   UTMIP_PHY_ENABLE		(1 << 12)
#define   USB_SUSP_SET		(1 << 14)

#define USB1_LEGACY_CTRL	0x410
#define   USB1_NO_LEGACY_MODE			(1 << 0)
#define   USB1_VBUS_SENSE_CTL_MASK		(3 << 1)
#define   USB1_VBUS_SENSE_CTL_VBUS_WAKEUP	(0 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD_OR_VBUS_WAKEUP \
						(1 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD	(2 << 1)
#define   USB1_VBUS_SENSE_CTL_A_SESS_VLD	(3 << 1)

#define UTMIP_PLL_CFG1		0x804
#define   UTMIP_XTAL_FREQ_COUNT(x)		(((x) & 0xfff) << 0)
#define   UTMIP_PLLU_ENABLE_DLY_COUNT(x)	(((x) & 0x1f) << 27)

#define UTMIP_XCVR_CFG0		0x808
#define   UTMIP_XCVR_SETUP(x)			(((x) & 0xf) << 0)
#define   UTMIP_XCVR_LSRSLEW(x)			(((x) & 0x3) << 8)
#define   UTMIP_XCVR_LSFSLEW(x)			(((x) & 0x3) << 10)
#define   UTMIP_FORCE_PD_POWERDOWN		(1 << 14)
#define   UTMIP_FORCE_PD2_POWERDOWN		(1 << 16)
#define   UTMIP_FORCE_PDZI_POWERDOWN		(1 << 18)
#define   UTMIP_XCVR_HSSLEW_MSB(x)		(((x) & 0x7f) << 25)

#define UTMIP_BIAS_CFG0		0x80c
#define   UTMIP_OTGPD			(1 << 11)
#define   UTMIP_BIASPD			(1 << 10)

#define UTMIP_HSRX_CFG0		0x810
#define   UTMIP_ELASTIC_LIMIT(x)	(((x) & 0x1f) << 10)
#define   UTMIP_IDLE_WAIT(x)		(((x) & 0x1f) << 15)

#define UTMIP_HSRX_CFG1		0x814
#define   UTMIP_HS_SYNC_START_DLY(x)	(((x) & 0x1f) << 1)

#define UTMIP_TX_CFG0		0x820
#define   UTMIP_FS_PREABMLE_J		(1 << 19)

#define UTMIP_MISC_CFG0		0x824
#define   UTMIP_SUSPEND_EXIT_ON_EDGE	(1 << 22)

#define UTMIP_MISC_CFG1		0x828
#define   UTMIP_PLL_ACTIVE_DLY_COUNT(x)	(((x) & 0x1f) << 18)
#define   UTMIP_PLLU_STABLE_COUNT(x)	(((x) & 0xfff) << 6)

#define UTMIP_DEBOUNCE_CFG0	0x82c
#define   UTMIP_BIAS_DEBOUNCE_A(x)	(((x) & 0xffff) << 0)

#define UTMIP_BAT_CHRG_CFG0	0x830
#define   UTMIP_PD_CHRG			(1 << 0)

#define UTMIP_SPARE_CFG0	0x834
#define   FUSE_SETUP_SEL		(1 << 3);

#define UTMIP_XCVR_CFG1		0x838
#define   UTMIP_FORCE_PDDISC_POWERDOWN	(1 << 0)
#define   UTMIP_FORCE_PDCHRP_POWERDOWN	(1 << 2)
#define   UTMIP_FORCE_PDDR_POWERDOWN	(1 << 4)
#define   UTMIP_XCVR_TERM_RANGE_ADJ(x)	(((x) & 0xf) << 18)

static const int udc_freq_table[] = {
	12000000,
	13000000,
	19200000,
	26000000,
};

static const u8 udc_delay_table[][4] = {
	/* ENABLE_DLY, STABLE_CNT, ACTIVE_DLY, XTAL_FREQ_CNT */
	{0x02,         0x2F,       0x04,       0x76}, /* 12 MHz */
	{0x02,         0x33,       0x05,       0x7F}, /* 13 MHz */
	{0x03,         0x4B,       0x06,       0xBB}, /* 19.2 MHz */
	{0x04,         0x66,       0x09,       0xFE}, /* 26 Mhz */
};

static const u16 udc_debounce_table[] = {
	0x7530, /* 12 MHz */
	0x7EF4, /* 13 MHz */
	0xBB80, /* 19.2 MHz */
	0xFDE8, /* 26 MHz */
};

static int utmi_phy_wait_stable(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	unsigned long timeout = jiffies + HZ;

	while (time_before(jiffies, timeout)) {
		if (readl(base + USB_SUSP_CTRL) & USB_PHY_CLK_VALID)
			return 0;
		udelay(10);
		cpu_relax();
	}
	if (readl(base + USB_SUSP_CTRL) & USB_PHY_CLK_VALID)
		return 0;
	else
		return -ETIMEDOUT;
}

static void utmi_phy_init(struct tegra_usb_phy *phy, int freq_sel)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	val = readl(base + USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	writel(val, base + USB_SUSP_CTRL);

	if (phy->instance == 0) {
		val = readl(base + USB1_LEGACY_CTRL);
		val |= USB1_NO_LEGACY_MODE;
		writel(val, base + USB1_LEGACY_CTRL);
	}

	val = readl(base + UTMIP_HSRX_CFG0);
	val &= ~(UTMIP_IDLE_WAIT(~0) | UTMIP_ELASTIC_LIMIT(~0));
	val |= UTMIP_IDLE_WAIT(17) | UTMIP_ELASTIC_LIMIT(16);
	writel(val, base + UTMIP_HSRX_CFG0);

	val = readl(base + UTMIP_HSRX_CFG1);
	val &= ~UTMIP_HS_SYNC_START_DLY(~0);
	val |= UTMIP_HS_SYNC_START_DLY(9);
	writel(val, base + UTMIP_HSRX_CFG1);

	val = readl(base + UTMIP_DEBOUNCE_CFG0);
	val &= ~UTMIP_BIAS_DEBOUNCE_A(~0);
	val |= UTMIP_BIAS_DEBOUNCE_A(udc_debounce_table[freq_sel]);
	writel(val, base + UTMIP_DEBOUNCE_CFG0);

	val = readl(base + UTMIP_MISC_CFG0);
	val &= ~UTMIP_SUSPEND_EXIT_ON_EDGE;
	writel(val, base + UTMIP_MISC_CFG0);

	val = readl(base + UTMIP_MISC_CFG1);
	val &= ~(UTMIP_PLL_ACTIVE_DLY_COUNT(~0) | UTMIP_PLLU_STABLE_COUNT(~0));
	val |= UTMIP_PLL_ACTIVE_DLY_COUNT(udc_delay_table[freq_sel][2]) |
		UTMIP_PLLU_STABLE_COUNT(udc_delay_table[freq_sel][1]);
	writel(val, base + UTMIP_MISC_CFG1);

	val = readl(base + UTMIP_PLL_CFG1);
	val &= ~(UTMIP_XTAL_FREQ_COUNT(~0) | UTMIP_PLLU_ENABLE_DLY_COUNT(~0));
	val |= UTMIP_XTAL_FREQ_COUNT(udc_delay_table[freq_sel][3]) |
		UTMIP_PLLU_ENABLE_DLY_COUNT(udc_delay_table[freq_sel][0]);
	writel(val, base + UTMIP_PLL_CFG1);
}

void utmi_phy_power_on(struct tegra_usb_phy *phy,
		       enum tegra_usb_phy_mode phy_mode)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	if (phy_mode == TEGRA_USB_PHY_MODE_DEVICE) {
		val = readl(base + USB_SUSP_CTRL);
		val &= ~(USB_WAKE_ON_CNNT_EN_DEV | USB_WAKE_ON_DISCON_EN_DEV);
		writel(val, base + USB_SUSP_CTRL);
	}

	val = readl(base + UTMIP_XCVR_CFG0);
	val &= ~(UTMIP_FORCE_PD_POWERDOWN | UTMIP_FORCE_PD2_POWERDOWN |
		 UTMIP_FORCE_PDZI_POWERDOWN | UTMIP_XCVR_SETUP(~0));
	if (phy_mode == TEGRA_USB_PHY_MODE_HOST) {
		val &= ~(UTMIP_XCVR_LSFSLEW(~0) | UTMIP_XCVR_LSRSLEW(~0));
		val |= UTMIP_XCVR_LSFSLEW(2) | UTMIP_XCVR_LSRSLEW(2);
		val |= UTMIP_XCVR_SETUP(0x9);
	} else {
		val &= ~(UTMIP_XCVR_HSSLEW_MSB(~0));
		val |= UTMIP_XCVR_SETUP(0xF);
	}
	writel(val, base + UTMIP_XCVR_CFG0);

	val = readl(base + UTMIP_BIAS_CFG0);
	val &= ~(UTMIP_OTGPD | UTMIP_BIASPD);
	writel(val, base + UTMIP_BIAS_CFG0);

	val = readl(base + UTMIP_XCVR_CFG1);
	val &= ~(UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
		 UTMIP_FORCE_PDDR_POWERDOWN | UTMIP_XCVR_TERM_RANGE_ADJ(~0));
	val |= UTMIP_XCVR_TERM_RANGE_ADJ(0x6);
	writel(val, base + UTMIP_XCVR_CFG1);

	val = readl(base + UTMIP_BAT_CHRG_CFG0);
	val &= ~UTMIP_PD_CHRG;
	writel(val, base + UTMIP_BAT_CHRG_CFG0);

	if (phy->instance == 2) {
		val = readl(base + USB_SUSP_CTRL);
		val |= UTMIP_PHY_ENABLE;
		writel(val, base + USB_SUSP_CTRL);
	}

	val = readl(base + USB_SUSP_CTRL);
	val &= ~UTMIP_RESET;
	writel(val, base + USB_SUSP_CTRL);

	if (phy->instance == 0) {
		val = readl(base + USB1_LEGACY_CTRL);
		val &= ~USB1_VBUS_SENSE_CTL_MASK;
		val |= USB1_VBUS_SENSE_CTL_A_SESS_VLD;
		writel(val, base + USB1_LEGACY_CTRL);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_SET;
		writel(val, base + USB_SUSP_CTRL);

		val = readl(base + USB_SUSP_CTRL);
		val |= USB_SUSP_CLR;
		writel(val, base + USB_SUSP_CTRL);

		udelay(10);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_CLR;
		writel(val, base + USB_SUSP_CTRL);
	}

	if (utmi_phy_wait_stable(phy))
		pr_err("%s: timeout waiting for phy to stabilize\n", __func__);

	if (phy->instance == 2) {
		val = readl(base + USB_PORTSC1);
		val &= ~USB_PORTSC1_PTS(~0);
		writel(val, base + USB_PORTSC1);
	}
}

void utmi_phy_power_off(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	if (phy->instance == 0) {
		val = readl(base + USB_SUSP_CTRL);
		val |= USB_SUSP_SET;
		writel(val, base + USB_SUSP_CTRL);

		udelay(10);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_SET;
		writel(val, base + USB_SUSP_CTRL);
	}

	if (phy->instance == 2) {
		val = readl(base + USB_PORTSC1);
		val |= USB_PORTSC1_PHCD;
		writel(val, base + USB_PORTSC1);
	}

	val = readl(base + USB_SUSP_CTRL);
	val |= USB_WAKE_ON_CNNT_EN_DEV | USB_WAKE_ON_DISCON_EN_DEV;
	writel(val, base + USB_SUSP_CTRL);

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
}

struct tegra_usb_phy *tegra_usb_phy_open(int instance, void __iomem *regs)
{
	struct tegra_usb_phy *phy;
	unsigned long parent_rate;
	int freq_sel;
	int err;

	phy = kmalloc(sizeof(struct tegra_usb_phy), GFP_KERNEL);
	if (!phy)
		return ERR_PTR(-ENOMEM);

	phy->instance = instance;
	phy->regs = regs;

	phy->pll_u = clk_get_sys(NULL, "pll_u");
	if (IS_ERR(phy->pll_u)) {
		printk(KERN_ERR "Can't get pll_u clock\n");
		err = PTR_ERR(phy->pll_u);
		goto err0;
	}

	parent_rate = clk_get_rate(clk_get_parent(phy->pll_u));
	for (freq_sel = 0; freq_sel < ARRAY_SIZE(udc_freq_table); freq_sel++) {
		if (udc_freq_table[freq_sel] == parent_rate)
			break;
	}
	if (freq_sel == ARRAY_SIZE(udc_freq_table)) {
		printk(KERN_ERR "invalid pll_u parent rate %ld\n", parent_rate);
		err = -EINVAL;
		goto err1;
	}

	/* TODO usb2 ulpi */
	if (phy->instance != 1)
		utmi_phy_init(phy, freq_sel);

	return phy;

err1:
	clk_disable(phy->pll_u);
	clk_put(phy->pll_u);
err0:
	kfree(phy);
	return ERR_PTR(err);
}

int tegra_usb_phy_power_on(struct tegra_usb_phy *phy,
			   enum tegra_usb_phy_mode phy_mode)
{
	/* TODO usb2 ulpi */
	clk_enable(phy->pll_u);
	if (phy->instance != 1)
		utmi_phy_power_on(phy, phy_mode);

	return 0;
}

int tegra_usb_phy_power_off(struct tegra_usb_phy *phy)
{
	/* TODO usb2 ulpi */
	if (phy->instance != 1)
		utmi_phy_power_off(phy);
	clk_disable(phy->pll_u);

	return 0;
}

int tegra_usb_phy_close(struct tegra_usb_phy *phy)
{
	clk_put(phy->pll_u);
	kfree(phy);
	return 0;
}
