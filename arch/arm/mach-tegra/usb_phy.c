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
#include <mach/iomap.h>

#define USB_USBCMD		0x140

#define USB_USBSTS		0x144
#define   USB_USBSTS_PCI	(1 << 2)

#define USB_USBINTR		0x148
#define USB_PERIODICLISTBASE	0x154
#define USB_ASYNCLISTADDR	0x158
#define USB_TXFILLTUNING	0x164

#define USB_PORTSC1		0x184
#define   USB_PORTSC1_PTS(x)	(((x) & 0x3) << 30)
#define   USB_PORTSC1_PSPD(x)	(((x) & 0x3) << 26)
#define   USB_PORTSC1_PHCD	(1 << 23)
#define   USB_PORTSC1_PTC(x)	(((x) & 0xf) << 16)
#define   USB_PORTSC1_PP	(1 << 12)
#define   USB_PORTSC1_SUSP	(1 << 7)
#define   USB_PORTSC1_PE	(1 << 2)
#define   USB_PORTSC1_CCS	(1 << 0)

#define USB_OTGSC		0x1a4
#define USB_USBMODE		0x1a8

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

static DEFINE_SPINLOCK(utmip_pad_lock);
static int utmip_pad_count;

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

static struct tegra_utmip_config utmip_default[] = {
	[0] = {
		.hssync_start_delay = 9,
		.idle_wait_delay = 17,
		.elastic_limit = 16,
		.term_range_adj = 6,
		.xcvr_setup = 9,
		.xcvr_lsfslew = 1,
		.xcvr_lsrslew = 1,
	},
	[2] = {
		.hssync_start_delay = 9,
		.idle_wait_delay = 17,
		.elastic_limit = 16,
		.term_range_adj = 6,
		.xcvr_setup = 9,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
	},
};

static int utmip_pad_open(struct tegra_usb_phy *phy)
{
	phy->pad_clk = clk_get_sys("utmip-pad", NULL);
	if (IS_ERR(phy->pad_clk)) {
		pr_err("%s: can't get utmip pad clock\n", __func__);
		return -1;
	}

	if (phy->instance == 0) {
		phy->pad_regs = phy->regs;
	} else {
		phy->pad_regs = ioremap(TEGRA_USB_BASE, TEGRA_USB_SIZE);
		if (!phy->pad_regs) {
			pr_err("%s: can't remap usb registers\n", __func__);
			clk_put(phy->pad_clk);
			return -ENOMEM;
		}
	}
	return 0;
}

static void utmip_pad_close(struct tegra_usb_phy *phy)
{
	if (phy->instance != 0)
		iounmap(phy->pad_regs);
	clk_put(phy->pad_clk);
}

static void utmip_pad_power_on(struct tegra_usb_phy *phy)
{
	unsigned long val, flags;
	void __iomem *base = phy->pad_regs;

	clk_enable(phy->pad_clk);

	spin_lock_irqsave(&utmip_pad_lock, flags);

	if (utmip_pad_count++ == 0) {
		val = readl(base + UTMIP_BIAS_CFG0);
		val &= ~(UTMIP_OTGPD | UTMIP_BIASPD);
		writel(val, base + UTMIP_BIAS_CFG0);
	}

	spin_unlock_irqrestore(&utmip_pad_lock, flags);
}

static int utmip_pad_power_off(struct tegra_usb_phy *phy)
{
	unsigned long val, flags;
	void __iomem *base = phy->pad_regs;

	if (!utmip_pad_count) {
		pr_err("%s: utmip pad already powered off\n", __func__);
		return -1;
	}

	spin_lock_irqsave(&utmip_pad_lock, flags);

	if (--utmip_pad_count == 0) {
		val = readl(base + UTMIP_BIAS_CFG0);
		val |= UTMIP_OTGPD | UTMIP_BIASPD;
		writel(val, base + UTMIP_BIAS_CFG0);
	}

	clk_disable(phy->pad_clk);

	spin_unlock_irqrestore(&utmip_pad_lock, flags);
	return 0;
}

static int utmi_wait_register(void __iomem *reg, u32 mask, u32 result)
{
	unsigned long timeout = 1000000;
	do {
		if ((readl(reg) & mask) == result)
			return 0;
		udelay(1);
		timeout--;
	} while (timeout);
	return -1;
}

static int utmi_phy_restore_context(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	unsigned long val = 0;
	int count = 0;

	/* If any saved context is present, restore it */
	if (!phy->context.valid)
		return -1;

	/* Restore register context */
	count = phy->context.regs_count;
	count--;
	writel(phy->context.regs[count--], base + USB_USBMODE);
	writel(phy->context.regs[count--], base + USB_OTGSC);
	writel(phy->context.regs[count--], base + USB_TXFILLTUNING);
	writel(phy->context.regs[count--], base + USB_ASYNCLISTADDR);
	writel(phy->context.regs[count--], base + USB_PERIODICLISTBASE);
	/* Restore interrupt context later */
	count--;
	writel(phy->context.regs[count--], base + USB_USBCMD);

	/* Enable Port Power */
	val = readl(base + USB_PORTSC1);
	val |= USB_PORTSC1_PP;
	writel(val, base + USB_PORTSC1);
	udelay(10);

	/* Program the field PTC in PORTSC based on the saved speed mode */
	if (phy->context.port_speed == TEGRA_USB_PHY_PORT_HIGH) {
		val = readl(base + USB_PORTSC1);
		val &= ~(USB_PORTSC1_PTC(~0));
		val |= USB_PORTSC1_PTC(5);
		writel(val, base + USB_PORTSC1);
	} else if (phy->context.port_speed == TEGRA_USB_PHY_PORT_SPEED_FULL) {
		val = readl(base + USB_PORTSC1);
		val &= ~(USB_PORTSC1_PTC(~0));
		val |= USB_PORTSC1_PTC(6);
		writel(val, base + USB_PORTSC1);
	} else if (phy->context.port_speed == TEGRA_USB_PHY_PORT_SPEED_LOW) {
		val = readl(base + USB_PORTSC1);
		val &= ~(USB_PORTSC1_PTC(~0));
		val |= USB_PORTSC1_PTC(7);
		writel(val, base + USB_PORTSC1);
	} else {
		pr_err("%s: speed is not configureed properly\n", __func__);
		return -1;
	}
	udelay(10);

	/* Disable test mode by setting PTC field to NORMAL_OP */
	val = readl(base + USB_PORTSC1);
	val &= ~(USB_PORTSC1_PTC(~0));
	writel(val, base + USB_PORTSC1);

	/* Poll until CCS is enabled */
	if (utmi_wait_register(base + USB_PORTSC1, USB_PORTSC1_CCS,
						   USB_PORTSC1_CCS)) {
		pr_err("%s: timeout waiting for USB_PORTSC1_CCS\n", __func__);
		return -1;
	}

	/* Poll until PE is enabled */
	if (utmi_wait_register(base + USB_PORTSC1, USB_PORTSC1_PE,
						   USB_PORTSC1_PE)) {
		pr_err("%s: timeout waiting for USB_PORTSC1_PE\n", __func__);
		return -1;
	}

	/* Clear the PCI status, to avoid an interrupt taken upon resume */
	val = readl(base + USB_USBSTS);
	val |= USB_USBSTS_PCI;
	writel(val, base + USB_USBSTS);
	if (utmi_wait_register(base + USB_USBSTS, USB_USBSTS_PCI, 0) < 0) {
		pr_err("%s: timeout waiting for USB_USBSTS_PCI\n", __func__);
		return -1;
	}

	/* Put controller in suspend mode by writing 1 to SUSP bit of PORTSC */
	val = readl(base + USB_PORTSC1);
	if ((val & USB_PORTSC1_PP) && (val & USB_PORTSC1_PE)) {
		val |= USB_PORTSC1_SUSP;
		writel(val, base + USB_PORTSC1);

		/* Wait until port suspend completes */
		if (utmi_wait_register(base + USB_PORTSC1, USB_PORTSC1_SUSP,
							   USB_PORTSC1_SUSP)) {
			pr_err("%s: timeout waiting for USB_PORTSC1_SUSP\n",
								__func__);
			return -1;
		}
	}

	/* Restore interrupt register */
	writel(phy->context.regs[1], base + USB_USBINTR);
	udelay(10);

	return 0;
}

static int utmi_phy_save_context(struct tegra_usb_phy *phy)
{
	void __iomem *base = phy->regs;
	int count = 0;

	phy->context.port_speed = (readl(base + USB_PORTSC1)
					& USB_PORTSC1_PSPD(3)) >> 26;

	/* If no device connection or invalid speeds just return */
	if (phy->context.port_speed > TEGRA_USB_PHY_PORT_HIGH) {
		phy->context.valid = false;
		return 0;
	}

	phy->context.regs[count++] = readl(base + USB_USBCMD);
	phy->context.regs[count++] = readl(base + USB_USBINTR);
	phy->context.regs[count++] = readl(base + USB_PERIODICLISTBASE);
	phy->context.regs[count++] = readl(base + USB_ASYNCLISTADDR);
	phy->context.regs[count++] = readl(base + USB_TXFILLTUNING);
	phy->context.regs[count++] = readl(base + USB_OTGSC);
	phy->context.regs[count++] = readl(base + USB_USBMODE);
	phy->context.regs_count = count;
	phy->context.valid = true;

	return 0;
}

static void utmi_phy_init(struct tegra_usb_phy *phy, int freq_sel)
{
	unsigned long val;
	void __iomem *base = phy->regs;
	struct tegra_utmip_config *config = phy->config;

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
	val |= UTMIP_IDLE_WAIT(config->idle_wait_delay);
	val |= UTMIP_ELASTIC_LIMIT(config->elastic_limit);
	writel(val, base + UTMIP_HSRX_CFG0);

	val = readl(base + UTMIP_HSRX_CFG1);
	val &= ~UTMIP_HS_SYNC_START_DLY(~0);
	val |= UTMIP_HS_SYNC_START_DLY(config->hssync_start_delay);
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

static void utmi_phy_clk_disable(struct tegra_usb_phy *phy)
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

	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID, 0) < 0)
		pr_err("%s: timeout waiting for phy to stabilize\n", __func__);
}

static void utmi_phy_clk_enable(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	if (phy->instance == 0) {
		val = readl(base + USB_SUSP_CTRL);
		val |= USB_SUSP_CLR;
		writel(val, base + USB_SUSP_CTRL);

		udelay(10);

		val = readl(base + USB_SUSP_CTRL);
		val &= ~USB_SUSP_CLR;
		writel(val, base + USB_SUSP_CTRL);
	}

	if (phy->instance == 2) {
		val = readl(base + USB_PORTSC1);
		val &= ~USB_PORTSC1_PHCD;
		writel(val, base + USB_PORTSC1);
	}

	if (utmi_wait_register(base + USB_SUSP_CTRL, USB_PHY_CLK_VALID,
						     USB_PHY_CLK_VALID))
		pr_err("%s: timeout waiting for phy to stabilize\n", __func__);
}

static void utmi_phy_power_on(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;
	struct tegra_utmip_config *config = phy->config;

	if (phy->mode == TEGRA_USB_PHY_MODE_DEVICE) {
		val = readl(base + USB_SUSP_CTRL);
		val &= ~(USB_WAKE_ON_CNNT_EN_DEV | USB_WAKE_ON_DISCON_EN_DEV);
		writel(val, base + USB_SUSP_CTRL);
	}

	utmip_pad_power_on(phy);

	val = readl(base + UTMIP_XCVR_CFG0);
	val &= ~(UTMIP_FORCE_PD_POWERDOWN | UTMIP_FORCE_PD2_POWERDOWN |
		 UTMIP_FORCE_PDZI_POWERDOWN | UTMIP_XCVR_SETUP(~0) |
		 UTMIP_XCVR_LSFSLEW(~0) | UTMIP_XCVR_LSRSLEW(~0) |
		 UTMIP_XCVR_HSSLEW_MSB(~0));
	val |= UTMIP_XCVR_SETUP(config->xcvr_setup);
	val |= UTMIP_XCVR_LSFSLEW(config->xcvr_lsfslew);
	val |= UTMIP_XCVR_LSRSLEW(config->xcvr_lsrslew);
	writel(val, base + UTMIP_XCVR_CFG0);

	val = readl(base + UTMIP_XCVR_CFG1);
	val &= ~(UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
		 UTMIP_FORCE_PDDR_POWERDOWN | UTMIP_XCVR_TERM_RANGE_ADJ(~0));
	val |= UTMIP_XCVR_TERM_RANGE_ADJ(config->term_range_adj);
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
	}

	utmi_phy_clk_enable(phy);

	if (phy->instance == 2) {
		val = readl(base + USB_PORTSC1);
		val &= ~USB_PORTSC1_PTS(~0);
		writel(val, base + USB_PORTSC1);
	}
}

static void utmi_phy_power_off(struct tegra_usb_phy *phy)
{
	unsigned long val;
	void __iomem *base = phy->regs;

	utmi_phy_clk_disable(phy);

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

	utmip_pad_power_off(phy);
}

struct tegra_usb_phy *tegra_usb_phy_open(int instance, void __iomem *regs,
					 struct tegra_utmip_config *config,
					 enum tegra_usb_phy_mode phy_mode)
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
	phy->config = config;
	phy->context.valid = false;
	phy->mode = phy_mode;

	if (!phy->config)
		phy->config = &utmip_default[instance];

	phy->pll_u = clk_get_sys(NULL, "pll_u");
	if (IS_ERR(phy->pll_u)) {
		pr_err("Can't get pll_u clock\n");
		err = PTR_ERR(phy->pll_u);
		goto err0;
	}
	clk_enable(phy->pll_u);

	parent_rate = clk_get_rate(clk_get_parent(phy->pll_u));
	for (freq_sel = 0; freq_sel < ARRAY_SIZE(udc_freq_table); freq_sel++) {
		if (udc_freq_table[freq_sel] == parent_rate)
			break;
	}
	if (freq_sel == ARRAY_SIZE(udc_freq_table)) {
		pr_err("invalid pll_u parent rate %ld\n", parent_rate);
		err = -EINVAL;
		goto err1;
	}

	/* TODO usb2 ulpi */
	if (phy->instance != 1) {
		err = utmip_pad_open(phy);
		if (err < 0)
			goto err1;
		utmi_phy_init(phy, freq_sel);
	}

	return phy;

err1:
	clk_disable(phy->pll_u);
	clk_put(phy->pll_u);
err0:
	kfree(phy);
	return ERR_PTR(err);
}

int tegra_usb_phy_power_on(struct tegra_usb_phy *phy)
{
	/* TODO usb2 ulpi */
	if (phy->instance != 1)
		utmi_phy_power_on(phy);

	return 0;
}

int tegra_usb_phy_power_off(struct tegra_usb_phy *phy)
{
	/* TODO usb2 ulpi */
	if (phy->instance != 1)
		utmi_phy_power_off(phy);

	return 0;
}

int tegra_usb_phy_clk_disable(struct tegra_usb_phy *phy)
{
	if (phy->instance != 1)
		utmi_phy_clk_disable(phy);

	return 0;
}

int tegra_usb_phy_clk_enable(struct tegra_usb_phy *phy)
{
	if (phy->instance != 1)
		utmi_phy_clk_enable(phy);

	return 0;
}

int tegra_usb_phy_suspend(struct tegra_usb_phy *phy)
{
	if (phy->instance != 1) {
		utmi_phy_save_context(phy);
		utmi_phy_power_off(phy);
	}

	return 0;
}

int tegra_usb_phy_resume(struct tegra_usb_phy *phy)
{
	if (phy->instance != 1) {
		utmi_phy_power_on(phy);
		return utmi_phy_restore_context(phy);
	}

	return 0;
}

int tegra_usb_phy_close(struct tegra_usb_phy *phy)
{
	if (phy->instance != 1)
		utmip_pad_close(phy);
	clk_disable(phy->pll_u);
	clk_put(phy->pll_u);
	kfree(phy);
	return 0;
}
