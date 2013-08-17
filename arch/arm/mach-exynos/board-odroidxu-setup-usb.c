/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/platform_data/dwc3-exynos.h>
#include <mach/regs-pmu.h>
#include <mach/regs-usb-phy.h>
#include <mach/regs-usb3-drd-phy.h>
#include <plat/cpu.h>
#include <plat/usb-phy.h>
#include <plat/udc-hs.h>

#define EXYNOS4_USB_CFG	(S3C_VA_SYS + 0x21C)
#define EXYNOS5_USB_CFG	(S3C_VA_SYS + 0x230)
#define PHY_ENABLE	(1 << 0)
#define PHY_DISABLE	(0 << 0)

enum usb_host_type {
	HOST_PHY_EHCI	= (0x1 << 0),
	HOST_PHY_OHCI	= (0x1 << 1),
	HOST_PHY_DEVICE	= (0x1 << 2),
};

struct exynos_usb_phy {
	struct clk *phy_clk;
	struct mutex phy_lock;
	u8 lpa_entered;
	unsigned long flags;
	unsigned long usage;
};

static struct exynos_usb_phy usb_phy_control = {
	.phy_lock = __MUTEX_INITIALIZER(usb_phy_control.phy_lock)
};

static atomic_t host_usage;

static void exynos5_usb_phy_crport_handshake(struct platform_device *pdev,
				u32 reg, void __iomem *reg_base, u32 cmd)
{
	u32 usec = 100;
	u32 result;

	writel(reg | cmd, reg_base + EXYNOS_USB3_PHYREG0);

	do {
		result = readl(reg_base + EXYNOS_USB3_PHYREG1);
		if (result & EXYNOS_USB3_PHYREG1_CR_ACK)
			break;

		udelay(1);
	} while (usec-- > 0);

	if (!usec)
		dev_err(&pdev->dev, "CRPORT handshake timeout1 (0x%08x)\n", reg);

	usec = 100;

	writel(reg, reg_base + EXYNOS_USB3_PHYREG0);

	do {
		result = readl(reg_base + EXYNOS_USB3_PHYREG1);
		if (!(result & EXYNOS_USB3_PHYREG1_CR_ACK))
			break;

		udelay(1);
	} while (usec-- > 0);

	if (!usec)
		dev_err(&pdev->dev, "CRPORT handshake timeout2 (0x%08x)\n", reg);
}

static void exynos_usb_mux_change(struct platform_device *pdev, int val)
{
	u32 is_host;

	/*
	 * Exynos4x12 and Exynos5250 has a USB 2.0 PHY for host and device.
	 * So, host and device cannot be used simultaneously except HSIC.
	 * USB mode can be changed by USB_CFG register.
	 * USB_CFG 1:host mode, 0:device mode.
	 */
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		is_host = readl(EXYNOS4_USB_CFG);
		writel(val, EXYNOS4_USB_CFG);
	} else {
		is_host = readl(EXYNOS5_USB_CFG);
		writel(1, EXYNOS5_USB_CFG);
	}

	if (is_host != val)
		dev_dbg(&pdev->dev, "Change USB MUX from %s to %s",
			is_host ? "Host" : "Device",
			val ? "Host" : "Device");
}

static int exynos_usb_phy_clock_enable(struct platform_device *pdev,
					int phy_type)
{
	struct clk *clk;

	/* DRD PHY is included in clock domain of DRD Link */
	if (phy_type == S5P_USB_PHY_DRD)
		return 0;

	if (!usb_phy_control.phy_clk) {
		/*
		 * PHY clock domain is 'usbhost' on exynos5250.
		 * But, PHY clock domain is 'otg' on others.
		 */
		if (soc_is_exynos5250() || soc_is_exynos5410())
			clk = clk_get(&pdev->dev, "usbhost");
		else
			clk = clk_get(&pdev->dev, "otg");

		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Failed to get phy clock\n");
			return PTR_ERR(clk);
		} else
			usb_phy_control.phy_clk = clk;

	}

	return clk_enable(usb_phy_control.phy_clk);
}

static int exynos_usb_phy_clock_disable(struct platform_device *pdev,
					int phy_type)
{
	struct clk *clk;

	/* DRD PHY is included in clock domain of DRD Link */
	if (phy_type == S5P_USB_PHY_DRD)
		return 0;

	if (!usb_phy_control.phy_clk) {
		if (soc_is_exynos5250() || soc_is_exynos5410())
			clk = clk_get(&pdev->dev, "usbhost");
		else
			clk = clk_get(&pdev->dev, "otg");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Failed to get phy clock\n");
			return PTR_ERR(clk);
		} else
			usb_phy_control.phy_clk = clk;
	}

	clk_disable(usb_phy_control.phy_clk);

	return 0;
}

static u32 exynos_usb_phy_set_clock(struct platform_device *pdev)
{
	struct clk *ref_clk;
	u32 refclk_freq = 0;

	ref_clk = clk_get(&pdev->dev, "ext_xtal");

	if (IS_ERR(ref_clk)) {
		dev_err(&pdev->dev, "Failed to get reference clock\n");
		return PTR_ERR(ref_clk);
	}

	switch (clk_get_rate(ref_clk)) {
	case 96 * 100000:
		refclk_freq = EXYNOS5_CLKSEL_9600K;
		break;
	case 10 * MHZ:
		refclk_freq = EXYNOS5_CLKSEL_10M;
		break;
	case 12 * MHZ:
		refclk_freq = EXYNOS5_CLKSEL_12M;
		break;
	case 192 * 100000:
		refclk_freq = EXYNOS5_CLKSEL_19200K;
		break;
	case 20 * MHZ:
		refclk_freq = EXYNOS5_CLKSEL_20M;
		break;
	case 50 * MHZ:
		refclk_freq = EXYNOS5_CLKSEL_50M;
		break;
	case 24 * MHZ:
	default:
		/* default reference clock */
		refclk_freq = EXYNOS5_CLKSEL_24M;
		break;
	}
	clk_put(ref_clk);

	return refclk_freq;
}

static int exynos4_usb_host_phy_is_on(void)
{
	return (readl(EXYNOS4_PHYPWR) & PHY1_STD_ANALOG_POWERDOWN) ? 0 : 1;
}

static int exynos4_usb_phy20_is_on(void)
{
	return exynos4_usb_host_phy_is_on();
}

static int exynos5_usb_phy20_is_on(void)
{
	return (readl(EXYNOS5_PHY_HOST_CTRL0) & HOST_CTRL0_SIDDQ) ? 0 : 1;
}

static int exynos5_usb_phy30_is_on(void)
{
	if (soc_is_exynos5250())
		return readl(EXYNOS5_USBDEV_PHY_CONTROL) ? 1 : 0;
	else
		return readl(EXYNOS5_USBDEV_PHY_CONTROL) ? 1 :
			(readl(EXYNOS5_USBDEV1_PHY_CONTROL) ? 1 : 0);
}

static int exynos_usb_device_phy_is_on(void)
{
	int ret = 0;

	if (soc_is_exynos4210()) {
		ret = (readl(EXYNOS4_PHYPWR) & PHY0_ANALOG_POWERDOWN) ? 0 : 1;
	} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
		ret = exynos4_usb_phy20_is_on() ?
			(readl(EXYNOS4_USB_CFG) ? 0 : 1) : 0;
	} else if (soc_is_exynos5250()) {
		ret = exynos5_usb_phy20_is_on() ?
			(readl(EXYNOS5_USB_CFG) ? 0 : 1) : 0;
	}
	return ret;
}

static int exynos4_usb_phy0_init(struct platform_device *pdev)
{
	u32 phypwr;
	u32 phyclk;
	u32 rstcon;

	writel(PHY_ENABLE, EXYNOS4210_USBDEV_PHY_CONTROL);

	/* set clock frequency for PLL */
	phyclk = exynos_usb_phy_set_clock(pdev);
	phyclk &= ~(PHY0_COMMON_ON_N);
	writel(phyclk, EXYNOS4_PHYCLK);

	/* set to normal of PHY0 */
	phypwr = readl(EXYNOS4_PHYPWR) & ~PHY0_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	/* reset all ports of both PHY and Link */
	rstcon = readl(EXYNOS4_RSTCON) | PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);
	rstcon &= ~PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);

	return 0;
}

static int exynos4_usb_phy0_exit(struct platform_device *pdev)
{
	/* unset to normal of PHY0 */
	writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	writel(PHY_DISABLE, EXYNOS4210_USBDEV_PHY_CONTROL);

	return 0;
}

static int exynos4_usb_phy1_init(struct platform_device *pdev)
{
	struct clk *otg_clk;
	struct clk *xusbxti_clk;
	u32 phyclk;
	u32 rstcon;
	int err;

	atomic_inc(&host_usage);

	otg_clk = clk_get(&pdev->dev, "otg");
	if (IS_ERR(otg_clk)) {
		dev_err(&pdev->dev, "Failed to get otg clock\n");
		return PTR_ERR(otg_clk);
	}

	err = clk_enable(otg_clk);
	if (err) {
		clk_put(otg_clk);
		return err;
	}

	if (exynos4_usb_host_phy_is_on())
		return 0;

	writel(PHY_ENABLE, EXYNOS4210_USBHOST_PHY_CONTROL);

	/* set clock frequency for PLL */
	phyclk = readl(EXYNOS4_PHYCLK) & ~CLKSEL_MASK;

	xusbxti_clk = clk_get(&pdev->dev, "xusbxti");
	if (xusbxti_clk && !IS_ERR(xusbxti_clk)) {
		switch (clk_get_rate(xusbxti_clk)) {
		case 12 * MHZ:
			phyclk |= CLKSEL_12M;
			break;
		case 24 * MHZ:
			phyclk |= CLKSEL_24M;
			break;
		default:
		case 48 * MHZ:
			/* default reference clock */
			break;
		}
		clk_put(xusbxti_clk);
	}

	writel(phyclk, EXYNOS4_PHYCLK);

	/* floating prevention logic: disable */
	writel((readl(EXYNOS4_PHY1CON) | FPENABLEN), EXYNOS4_PHY1CON);

	/* set to normal HSIC 0 and 1 of PHY1 */
	writel((readl(EXYNOS4_PHYPWR) & ~PHY1_HSIC_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	/* set to normal standard USB of PHY1 */
	writel((readl(EXYNOS4_PHYPWR) & ~PHY1_STD_NORMAL_MASK), EXYNOS4_PHYPWR);

	/* reset all ports of both PHY and Link */
	rstcon = readl(EXYNOS4_RSTCON) | HOST_LINK_PORT_SWRST_MASK |
		PHY1_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);

	rstcon &= ~(HOST_LINK_PORT_SWRST_MASK | PHY1_SWRST_MASK);
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(80);

	clk_disable(otg_clk);
	clk_put(otg_clk);

	return 0;
}

static int exynos4_usb_phy1_exit(struct platform_device *pdev)
{
	struct clk *otg_clk;
	int err;

	if (atomic_dec_return(&host_usage) > 0)
		return 0;

	otg_clk = clk_get(&pdev->dev, "otg");
	if (IS_ERR(otg_clk)) {
		dev_err(&pdev->dev, "Failed to get otg clock\n");
		return PTR_ERR(otg_clk);
	}

	err = clk_enable(otg_clk);
	if (err) {
		clk_put(otg_clk);
		return err;
	}

	writel((readl(EXYNOS4_PHYPWR) | PHY1_STD_ANALOG_POWERDOWN),
			EXYNOS4_PHYPWR);

	writel(PHY_DISABLE, EXYNOS4210_USBHOST_PHY_CONTROL);

	clk_disable(otg_clk);
	clk_put(otg_clk);

	return 0;
}

static int exynos4_usb_phy1_suspend(struct platform_device *pdev)
{
	u32 phypwr;

	/* set to suspend HSIC 0 and 1 and standard of PHY1 */
	phypwr = readl(EXYNOS4_PHYPWR);
	if (soc_is_exynos4210()) {
		phypwr |= (PHY1_STD_FORCE_SUSPEND
			| EXYNOS4210_HSIC0_FORCE_SUSPEND
			| EXYNOS4210_HSIC1_FORCE_SUSPEND);
	} else {
		phypwr = readl(EXYNOS4_PHYPWR);
		phypwr |= (PHY1_STD_FORCE_SUSPEND
			| EXYNOS4212_HSIC0_FORCE_SUSPEND
			| EXYNOS4212_HSIC1_FORCE_SUSPEND);
	}
	writel(phypwr, EXYNOS4_PHYPWR);

	return 0;
}

static int exynos4_usb_phy1_resume(struct platform_device *pdev)
{
	u32 rstcon;
	u32 phypwr;
	int err;

	if (exynos4_usb_host_phy_is_on()) {
		/* set to resume HSIC 0 and 1 and standard of PHY1 */
		phypwr = readl(EXYNOS4_PHYPWR);
		if (soc_is_exynos4210()) {
			phypwr &= ~(PHY1_STD_FORCE_SUSPEND
				| EXYNOS4210_HSIC0_FORCE_SUSPEND
				| EXYNOS4210_HSIC1_FORCE_SUSPEND);
		} else {
			phypwr = readl(EXYNOS4_PHYPWR);
			phypwr &= ~(PHY1_STD_FORCE_SUSPEND
				| EXYNOS4212_HSIC0_FORCE_SUSPEND
				| EXYNOS4212_HSIC1_FORCE_SUSPEND);
		}
		writel(phypwr, EXYNOS4_PHYPWR);
		if (usb_phy_control.lpa_entered) {
			usb_phy_control.lpa_entered = 0;
			err = 1;
		} else
			err = 0;
	} else {
		phypwr = readl(EXYNOS4_PHYPWR);
		/* set to normal HSIC 0 and 1 of PHY1 */
		if (soc_is_exynos4210()) {
			writel(PHY_ENABLE, EXYNOS4210_USBHOST_PHY_CONTROL);

			phypwr &= ~(PHY1_STD_NORMAL_MASK
				| EXYNOS4210_HSIC0_NORMAL_MASK
				| EXYNOS4210_HSIC1_NORMAL_MASK);
			writel(phypwr, EXYNOS4_PHYPWR);

			/* reset all ports of both PHY and Link */
			rstcon = readl(EXYNOS4_RSTCON)
				| EXYNOS4210_HOST_LINK_PORT_SWRST_MASK
				| EXYNOS4210_PHY1_SWRST_MASK;
			writel(rstcon, EXYNOS4_RSTCON);
			udelay(10);

			rstcon &= ~(EXYNOS4210_HOST_LINK_PORT_SWRST_MASK
				| EXYNOS4210_PHY1_SWRST_MASK);
			writel(rstcon, EXYNOS4_RSTCON);
		} else {
			writel(PHY_ENABLE, EXYNOS4x12_USB_PHY_CONTROL);
			writel(PHY_ENABLE, EXYNOS4x12_HSIC0_PHY_CONTROL);
			writel(PHY_ENABLE, EXYNOS4x12_HSIC1_PHY_CONTROL);

			/* set to normal of Device */
			phypwr = readl(EXYNOS4_PHYPWR) & ~PHY0_NORMAL_MASK;
			writel(phypwr, EXYNOS4_PHYPWR);

			/* reset both PHY and Link of Device */
			rstcon = readl(EXYNOS4_RSTCON) | PHY0_SWRST_MASK;
			writel(rstcon, EXYNOS4_RSTCON);
			udelay(10);
			rstcon &= ~PHY0_SWRST_MASK;
			writel(rstcon, EXYNOS4_RSTCON);

			/* set to normal of Host */
			phypwr &= ~(PHY1_STD_NORMAL_MASK
				| EXYNOS4212_HSIC0_NORMAL_MASK
				| EXYNOS4212_HSIC1_NORMAL_MASK);
			writel(phypwr, EXYNOS4_PHYPWR);

			/* reset all ports of both PHY and Link */
			rstcon = readl(EXYNOS4_RSTCON)
				| EXYNOS4212_HOST_LINK_PORT_SWRST_MASK
				| EXYNOS4212_PHY1_SWRST_MASK;
			writel(rstcon, EXYNOS4_RSTCON);
			udelay(10);

			rstcon &= ~(EXYNOS4212_HOST_LINK_PORT_SWRST_MASK
				| EXYNOS4212_PHY1_SWRST_MASK);
			writel(rstcon, EXYNOS4_RSTCON);
		}
		usb_phy_control.lpa_entered = 0;
		err = 1;
	}
	udelay(80);

	return err;
}

static int exynos4_usb_phy20_init(struct platform_device *pdev)
{
	u32 phypwr, phyclk, rstcon;

	atomic_inc(&host_usage);

	if (exynos4_usb_phy20_is_on()) {
		exynos4_usb_phy1_resume(pdev);
		dev_err(&pdev->dev, "Already power on PHY\n");
		return 0;
	}

	writel(PHY_ENABLE, EXYNOS4x12_USB_PHY_CONTROL);
	writel(PHY_ENABLE, EXYNOS4x12_HSIC0_PHY_CONTROL);
	writel(PHY_ENABLE, EXYNOS4x12_HSIC1_PHY_CONTROL);

	/* USB MUX change from Device to Host */
	exynos_usb_mux_change(pdev, 1);

	/* set clock frequency for PLL */
	phyclk = exynos_usb_phy_set_clock(pdev);
	/* COMMON Block configuration during suspend */
	phyclk &= ~(PHY0_COMMON_ON_N | PHY1_COMMON_ON_N);
	writel(phyclk, EXYNOS4_PHYCLK);

	/* set to normal of Device */
	phypwr = readl(EXYNOS4_PHYPWR) & ~PHY0_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	/* set to normal of Host */
	phypwr = readl(EXYNOS4_PHYPWR);
	phypwr &= ~(PHY1_STD_NORMAL_MASK |
		EXYNOS4212_HSIC0_NORMAL_MASK |
		EXYNOS4212_HSIC1_NORMAL_MASK);
	writel(phypwr, EXYNOS4_PHYPWR);

	/* reset both PHY and Link of Device */
	rstcon = readl(EXYNOS4_RSTCON) | PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);
	rstcon &= ~PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);

	/* reset both PHY and Link of Host */
	rstcon = readl(EXYNOS4_RSTCON)
		| EXYNOS4212_HOST_LINK_PORT_SWRST_MASK
		| EXYNOS4212_PHY1_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);

	rstcon &= ~(EXYNOS4212_HOST_LINK_PORT_SWRST_MASK
		| EXYNOS4212_PHY1_SWRST_MASK);
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(80);

	return 0;
}

static int exynos4_usb_phy20_exit(struct platform_device *pdev)
{
	u32 phypwr;

	if (atomic_dec_return(&host_usage) > 0) {
		dev_info(&pdev->dev, "still being used\n");
		return -EBUSY;
	}

	/* unset to normal of Device */
	writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	/* unset to normal of Host */
	phypwr = readl(EXYNOS4_PHYPWR)
		| PHY1_STD_NORMAL_MASK
		| EXYNOS4212_HSIC0_NORMAL_MASK
		| EXYNOS4212_HSIC1_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	writel(PHY_DISABLE, EXYNOS4x12_HSIC0_PHY_CONTROL);
	writel(PHY_DISABLE, EXYNOS4x12_HSIC1_PHY_CONTROL);
	writel(PHY_DISABLE, EXYNOS4x12_USB_PHY_CONTROL);

	return 0;
}

static int exynos5_usb_phy_host_suspend(struct platform_device *pdev)
{
	u32 hostphy_ctrl0;

	/* set to suspend HSIC 1 and 2 */
	writel(readl(EXYNOS5_PHY_HSIC_CTRL1) | HSIC_CTRL_FORCESUSPEND,
		EXYNOS5_PHY_HSIC_CTRL1);
	writel(readl(EXYNOS5_PHY_HSIC_CTRL2) | HSIC_CTRL_FORCESUSPEND,
		EXYNOS5_PHY_HSIC_CTRL2);

	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	/* set to suspend standard of PHY20 */
	hostphy_ctrl0 |= HOST_CTRL0_FORCESUSPEND;
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

	return 0;
}

static int exynos5_usb_phy_host_resume(struct platform_device *pdev)
{
	u32 hostphy_ctrl0, otgphy_sys, hsic_ctrl;
	int err;

	if (exynos5_usb_phy20_is_on()) {
		/* set to suspend HSIC 0 and 1 and standard of PHY1 */
		hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
		hostphy_ctrl0 &= ~(HOST_CTRL0_FORCESUSPEND);
		writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

		/* set common_on_n of PHY1 for power consumption */
		hsic_ctrl = readl(EXYNOS5_PHY_HSIC_CTRL1);
		hsic_ctrl &= ~(HSIC_CTRL_FORCESUSPEND);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
		if (usb_phy_control.lpa_entered) {
			usb_phy_control.lpa_entered = 0;
			err = 1;
		} else
			err = 0;
	} else {
		writel(PHY_ENABLE, EXYNOS5_USBHOST_PHY_CONTROL);

		/* otg phy reset */
		otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
		otgphy_sys &= ~(OTG_SYS_SIDDQ_UOTG);
		otgphy_sys |= (OTG_SYS_PHY0_SW_RST |
				OTG_SYS_LINK_SW_RST_UOTG |
				OTG_SYS_PHYLINK_SW_RESET);
		writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);
		udelay(10);
		otgphy_sys &= ~(OTG_SYS_PHY0_SW_RST |
				OTG_SYS_LINK_SW_RST_UOTG |
				OTG_SYS_PHYLINK_SW_RESET);
		writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

		/* reset all ports of both PHY and Link */
		hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
		hostphy_ctrl0 &= ~(HOST_CTRL0_SIDDQ | HOST_CTRL0_FORCESUSPEND);
		hostphy_ctrl0 |= (HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST);
		writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);
		udelay(10);
		hostphy_ctrl0 &= ~(HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST);
		writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

		/* HSIC phy reset */
		hsic_ctrl = readl(EXYNOS5_PHY_HSIC_CTRL1);
		hsic_ctrl &= ~(HSIC_CTRL_SIDDQ | HSIC_CTRL_FORCESUSPEND);
		hsic_ctrl |= (HSIC_CTRL_PHYSWRST | HSIC_CTRL_UTMISWRST);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
		udelay(10);
		hsic_ctrl &= ~(HSIC_CTRL_PHYSWRST | HSIC_CTRL_UTMISWRST);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);

		usb_phy_control.lpa_entered = 0;
		err = 1;
	}
	udelay(80);

	return err;
}

static int exynos5_usb_phy20_init(struct platform_device *pdev)
{
	u32 refclk_freq;
	u32 hostphy_ctrl0, otgphy_sys, hsic_ctrl, ehcictrl, ohcictrl;

	atomic_inc(&host_usage);

	if (exynos5_usb_phy20_is_on()) {
		exynos5_usb_phy_host_resume(pdev);
		dev_err(&pdev->dev, "Already power on PHY\n");
		return 0;
	}

	exynos_usb_mux_change(pdev, 1);

	writel(PHY_ENABLE, EXYNOS5_USBHOST_PHY_CONTROL);

	/* Host and Device should be set at the same time */
	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	hostphy_ctrl0 &= ~(HOST_CTRL0_FSEL_MASK);
	otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
	otgphy_sys &= ~(OTG_SYS_CTRL0_FSEL_MASK);

	/* 2.0 phy reference clock configuration */
	refclk_freq = exynos_usb_phy_set_clock(pdev);
	hostphy_ctrl0 |= (refclk_freq << HOST_CTRL0_CLKSEL_SHIFT);
	otgphy_sys |= (refclk_freq << OTG_SYS_CLKSEL_SHIFT);

	/* COMMON Block configuration during suspend */
	hostphy_ctrl0 |= (HOST_CTRL0_COMMONON_N);
	if (soc_is_exynos5250())
		otgphy_sys &= ~(OTG_SYS_COMMON_ON);
	else
		otgphy_sys |= (OTG_SYS_COMMON_ON);

	/* otg phy reset */
	otgphy_sys &= ~(OTG_SYS_FORCE_SUSPEND |
			OTG_SYS_SIDDQ_UOTG |
			OTG_SYS_FORCE_SLEEP);
	otgphy_sys &= ~(OTG_SYS_REF_CLK_SEL_MASK);
	otgphy_sys |= (OTG_SYS_REF_CLK_SEL(0x2) | OTG_SYS_OTGDISABLE);
	otgphy_sys |= (OTG_SYS_PHY0_SW_RST |
			OTG_SYS_LINK_SW_RST_UOTG |
			OTG_SYS_PHYLINK_SW_RESET);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);
	udelay(10);
	otgphy_sys &= ~(OTG_SYS_PHY0_SW_RST |
			OTG_SYS_LINK_SW_RST_UOTG |
			OTG_SYS_PHYLINK_SW_RESET);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

	/* host phy reset */
	hostphy_ctrl0 &= ~(HOST_CTRL0_PHYSWRST |
			HOST_CTRL0_PHYSWRSTALL |
			HOST_CTRL0_SIDDQ);
	hostphy_ctrl0 &= ~(HOST_CTRL0_FORCESUSPEND | HOST_CTRL0_FORCESLEEP);
	hostphy_ctrl0 |= (HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);
	udelay(10);
	hostphy_ctrl0 &= ~(HOST_CTRL0_LINKSWRST | HOST_CTRL0_UTMISWRST);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

	/* HSIC phy reset */
	hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) | HSIC_CTRL_REFCLKSEL(0x2) |
		HSIC_CTRL_PHYSWRST);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
	udelay(10);
	hsic_ctrl &= ~(HSIC_CTRL_PHYSWRST);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);

	udelay(80);

	/* Enable DMA burst bus configuration */
	ehcictrl = readl(EXYNOS5_PHY_HOST_EHCICTRL);
	ehcictrl |= (EHCICTRL_ENAINCRXALIGN | EHCICTRL_ENAINCR4 |
			EHCICTRL_ENAINCR8 | EHCICTRL_ENAINCR16);
	writel(ehcictrl, EXYNOS5_PHY_HOST_EHCICTRL);

	/* set ohci_suspend_on_n */
	ohcictrl = readl(EXYNOS5_PHY_HOST_OHCICTRL);
	ohcictrl |= OHCICTRL_SUSPLGCY;
	writel(ohcictrl, EXYNOS5_PHY_HOST_OHCICTRL);

	return 0;
}

static int exynos5_usb_phy20_exit(struct platform_device *pdev)
{
	u32 hostphy_ctrl0, otgphy_sys, hsic_ctrl;

	if (atomic_dec_return(&host_usage) > 0) {
		dev_info(&pdev->dev, "still being used\n");
		return -EBUSY;
	}

	hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) |
			HSIC_CTRL_REFCLKSEL(0x2) |
			HSIC_CTRL_SIDDQ |
			HSIC_CTRL_FORCESLEEP |
			HSIC_CTRL_FORCESUSPEND);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);

	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	hostphy_ctrl0 |= (HOST_CTRL0_SIDDQ);
	hostphy_ctrl0 |= (HOST_CTRL0_FORCESUSPEND | HOST_CTRL0_FORCESLEEP);
	hostphy_ctrl0 |= (HOST_CTRL0_PHYSWRST | HOST_CTRL0_PHYSWRSTALL);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

	otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
	otgphy_sys |= (OTG_SYS_FORCE_SUSPEND |
			OTG_SYS_SIDDQ_UOTG |
			OTG_SYS_FORCE_SLEEP);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

	writel(PHY_DISABLE, EXYNOS5_USBHOST_PHY_CONTROL);

	return 0;
}

static int exynos_usb_dev_phy20_init(struct platform_device *pdev)
{
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		exynos4_usb_phy20_init(pdev);
		if (usb_phy_control.lpa_entered)
			exynos4_usb_phy1_suspend(pdev);
	} else {
		exynos5_usb_phy20_init(pdev);
		if (usb_phy_control.lpa_entered)
			exynos5_usb_phy_host_suspend(pdev);
	}

	/* usb mode change from host to device */
	exynos_usb_mux_change(pdev, 0);

	return 0;
}

static int exynos_usb_dev_phy20_exit(struct platform_device *pdev)
{
	if (soc_is_exynos4212() || soc_is_exynos4412())
		exynos4_usb_phy20_exit(pdev);
	else
		exynos5_usb_phy20_exit(pdev);

	/* usb mode change from device to host */
	exynos_usb_mux_change(pdev, 1);

	return 0;
}

static u32 exynos_usb_phy30_set_clock(struct platform_device *pdev)
{
	u32 reg, refclk;

	refclk = exynos_usb_phy_set_clock(pdev);
	reg = EXYNOS_USB3_PHYCLKRST_REFCLKSEL(3) |
		EXYNOS_USB3_PHYCLKRST_FSEL(refclk);

	switch (refclk) {
	case EXYNOS5_CLKSEL_50M:
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x02) |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x00));
		break;
	case EXYNOS5_CLKSEL_20M:
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x7d) |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x00));
		break;
	case EXYNOS5_CLKSEL_19200K:
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x02) |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x88));
		break;
	case EXYNOS5_CLKSEL_24M:
	default:
		reg |= (EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x68) |
			EXYNOS_USB3_PHYCLKRST_SSC_REF_CLK_SEL(0x88));
		break;
	}

	return reg;
}

static int exynos5_usb_phy30_tune(struct platform_device *pdev)
{
	struct device *parent = pdev->dev.parent;
	struct dwc3_exynos_data *pdata;
	int phy_num = pdev->id;
	void __iomem *reg_base;

	if (!parent || !parent->platform_data)
		return -EINVAL;

	pdata = parent->platform_data;

	switch (phy_num) {
	case 0:
		reg_base = S5P_VA_USB3_DRD0_PHY;
		break;
	case 1:
		reg_base = S5P_VA_USB3_DRD1_PHY;
		break;
	default:
		return -ENODEV;
	}

	if (!strcmp(pdata->udc_name, pdev->name)) {
		/* TODO: Peripheral mode */

	} else if (!strcmp(pdata->xhci_name, pdev->name)) {
		/* TODO: Host mode */

	} else {
		return -EINVAL;
	}

	return 0;
}

static int exynos5_usb_phy30_init(struct platform_device *pdev)
{
	int phy_num = pdev->id;
	void __iomem *reg_base;
	u32 reg;

	switch (phy_num) {
	case 0:
		reg_base = S5P_VA_USB3_DRD0_PHY;
		writel(1, EXYNOS5_USBDEV_PHY_CONTROL);
		break;
	case 1:
		reg_base = S5P_VA_USB3_DRD1_PHY;
		writel(1, EXYNOS5_USBDEV1_PHY_CONTROL);
		break;
	default:
		return -1;
	}

	/* Reset USB 3.0 PHY */
	writel(0x00000000, reg_base + EXYNOS_USB3_PHYREG0);
	writel(0x24d4e6e4, reg_base + EXYNOS_USB3_PHYPARAM0);
	writel(0x00000000, reg_base + EXYNOS_USB3_PHYRESUME);

	/*
	 * Setting Frame Length Adjustment(FLADJ) Register.
	 * See xHCI 1.0 spec 5.2.4
	 */
	reg = EXYNOS_USB3_LINKSYSTEM_XHCI_VERSION_CONTROL |
		EXYNOS_USB3_LINKSYSTEM_FLADJ(0x20);
	writel(reg, reg_base + EXYNOS_USB3_LINKSYSTEM);
	writel(0x03fff81C, reg_base + EXYNOS_USB3_PHYPARAM1);
	writel(0x00000004, reg_base + EXYNOS_USB3_PHYBATCHG);
#ifdef CONFIG_USB_EXYNOS_SWITCH
	if (soc_is_exynos5250())
		writel(readl(reg_base + EXYNOS_USB3_LINKPORT) |
			(0xf<<2), reg_base + EXYNOS_USB3_LINKPORT);
#endif

	/* PHYTEST POWERDOWN Control */
	reg = readl(reg_base + EXYNOS_USB3_PHYTEST);
	reg &= ~(EXYNOS_USB3_PHYTEST_POWERDOWN_SSP |
		 EXYNOS_USB3_PHYTEST_POWERDOWN_HSP);
	writel(reg, reg_base + EXYNOS_USB3_PHYTEST);
	/* UTMI Power Control */
	writel(EXYNOS_USB3_PHYUTMI_OTGDISABLE, reg_base + EXYNOS_USB3_PHYUTMI);

	reg = exynos_usb_phy30_set_clock(pdev);

	reg |= (EXYNOS_USB3_PHYCLKRST_PORTRESET |
		/* Digital power supply in normal operating mode */
		EXYNOS_USB3_PHYCLKRST_RETENABLEN |
		/* Enable ref clock for SS function */
		EXYNOS_USB3_PHYCLKRST_REF_SSP_EN |
		/* Enable spread spectrum */
		EXYNOS_USB3_PHYCLKRST_SSC_EN |
		EXYNOS_USB3_PHYCLKRST_COMMONONN);

	writel(reg, reg_base + EXYNOS_USB3_PHYCLKRST);

	udelay(10);

	reg &= ~(EXYNOS_USB3_PHYCLKRST_PORTRESET);
	writel(reg, reg_base + EXYNOS_USB3_PHYCLKRST);

	return 0;
}

static int exynos5_usb_phy30_exit(struct platform_device *pdev)
{
	int phy_num = pdev->id;
	void __iomem *reg_base;
	u32 reg;

	switch (phy_num) {
	case 0:
		reg_base = S5P_VA_USB3_DRD0_PHY;
		writel(0, EXYNOS5_USBDEV_PHY_CONTROL);
		break;
	case 1:
		reg_base = S5P_VA_USB3_DRD1_PHY;
		writel(0, EXYNOS5_USBDEV1_PHY_CONTROL);
		break;
	default:
		return -1;
	}

	reg = EXYNOS_USB3_PHYUTMI_OTGDISABLE |
		EXYNOS_USB3_PHYUTMI_FORCESUSPEND |
		EXYNOS_USB3_PHYUTMI_FORCESLEEP;
	writel(reg, reg_base + EXYNOS_USB3_PHYUTMI);

	reg = readl(reg_base + EXYNOS_USB3_PHYCLKRST);
	reg &= ~(EXYNOS_USB3_PHYCLKRST_REF_SSP_EN |
		EXYNOS_USB3_PHYCLKRST_SSC_EN |
		EXYNOS_USB3_PHYCLKRST_COMMONONN);
	writel(reg, reg_base + EXYNOS_USB3_PHYCLKRST);

	/* Control PHYTEST to remove leakage current */
	reg = readl(reg_base + EXYNOS_USB3_PHYTEST);
	reg |= (EXYNOS_USB3_PHYTEST_POWERDOWN_SSP |
		EXYNOS_USB3_PHYTEST_POWERDOWN_HSP);
	writel(reg, reg_base + EXYNOS_USB3_PHYTEST);

	return 0;
}

int exynos5_usb_phy_crport_ctrl(struct platform_device *pdev,
				u32 addr, u32 data)
{
	int phy_num = pdev->id;
	void __iomem *reg_base;
	u32 reg;

	if (phy_num == 0)
		reg_base = S5P_VA_USB3_DRD0_PHY;
	else if (phy_num == 1)
		reg_base = S5P_VA_USB3_DRD1_PHY;
	else
		return -EINVAL;

	/* los_bias setting */
	/* Write Address */
	reg = EXYNOS_USB3_PHYREG0_CR_DATA_IN(addr);
	writel(reg, reg_base + EXYNOS_USB3_PHYREG0);
	exynos5_usb_phy_crport_handshake(pdev, reg, reg_base,
			EXYNOS_USB3_PHYREG0_CR_CAP_ADDR);

	/* Write Data */
	reg = EXYNOS_USB3_PHYREG0_CR_DATA_IN(data);
	writel(reg, reg_base + EXYNOS_USB3_PHYREG0);
	exynos5_usb_phy_crport_handshake(pdev, reg, reg_base,
			EXYNOS_USB3_PHYREG0_CR_CAP_DATA);
	exynos5_usb_phy_crport_handshake(pdev, reg, reg_base,
			EXYNOS_USB3_PHYREG0_CR_WRITE);

	return 0;
}

static int s5p_usb_otg_phy_tune(struct s3c_hsotg_plat *pdata, int def_mode)
{
	u32 phytune;

	if (!pdata)
		return -EINVAL;

	pr_debug("usb: %s read original tune\n", __func__);
	phytune = readl(EXYNOS5_PHY_OTG_TUNE);
	if (!pdata->def_phytune) {
		pdata->def_phytune = phytune;
		pr_debug("usb: %s save default phytune (0x%x)\n",
				__func__, pdata->def_phytune);
	}

	pr_debug("usb: %s original tune=0x%x\n",
			__func__, phytune);

	pr_debug("usb: %s tune_mask=0x%x, tune=0x%x\n",
			__func__, pdata->phy_tune_mask, pdata->phy_tune);

	if (pdata->phy_tune_mask) {
		if (def_mode) {
			pr_debug("usb: %s set defult tune=0x%x\n",
					__func__, pdata->def_phytune);
			writel(pdata->def_phytune, EXYNOS5_PHY_OTG_TUNE);
		} else {
			phytune &= ~(pdata->phy_tune_mask);
			phytune |= pdata->phy_tune;
			udelay(10);
			pr_debug("usb: %s custom tune=0x%x\n",
					__func__, phytune);
			writel(phytune, EXYNOS5_PHY_OTG_TUNE);
		}
		phytune = readl(EXYNOS5_PHY_OTG_TUNE);
		pr_debug("usb: %s modified tune=0x%x\n",
				__func__, phytune);
	} else {
		pr_debug("usb: %s default tune\n", __func__);
	}

	return 0;
}

static void set_exynos_usb_phy_tune(int type)
{
	u32 phytune;

	if (!soc_is_exynos5250()) {
		pr_debug("usb: %s it is not exynos5250.(t=%d)\n",
						__func__, type);
		return;
	}

	if (type == S5P_USB_PHY_DEVICE) {
		phytune = readl(EXYNOS5_PHY_OTG_TUNE);
		pr_debug("usb: %s old phy tune for device =0x%x\n",
					__func__, phytune);
		/* sqrxtune [13:11] 3b110 : -15% */
		phytune &= ~OTG_TUNE_SQRXTUNE(0x7);
		phytune |= OTG_TUNE_SQRXTUNE(0x6);
		udelay(10);
		writel(phytune, EXYNOS5_PHY_OTG_TUNE);
		phytune = readl(EXYNOS5_PHY_OTG_TUNE);
		pr_debug("usb: %s new phy tune for device =0x%x\n",
					__func__, phytune);
	} else if (type == S5P_USB_PHY_HOST) {
		phytune = readl(EXYNOS5_PHY_HOST_TUNE0);
		pr_debug("usb: %s old phy tune for host =0x%x\n",
				__func__, phytune);
		/* sqrxtune [14:12] 3b110 : -15% */
		phytune &= ~HOST_TUNE0_SQRXTUNE(0x7);
		phytune |= HOST_TUNE0_SQRXTUNE(0x6);
		udelay(10);
		writel(phytune, EXYNOS5_PHY_HOST_TUNE0);
		phytune = readl(EXYNOS5_PHY_HOST_TUNE0);
		pr_debug("usb: %s new phy tune for host =0x%x\n",
					__func__, phytune);
	}
}

static int exynos4_check_usb_op(void)
{
	u32 phypwr;
	u32 op = 1;
	unsigned long flags;
	int ret;

	ret = clk_enable(usb_phy_control.phy_clk);
	if (ret)
		return 0;

	local_irq_save(flags);
	phypwr = readl(EXYNOS4_PHYPWR);

	/*If USB Device is power on,  */
	if (exynos_usb_device_phy_is_on()) {
		op = 1;
		goto done;
	} else if (!exynos4_usb_host_phy_is_on()) {
		op = 0;
		goto done;
	}

	/*If USB Device & Host is suspended,  */
	if (soc_is_exynos4210()) {
		if (phypwr & (PHY1_STD_FORCE_SUSPEND
			| EXYNOS4210_HSIC0_FORCE_SUSPEND
			| EXYNOS4210_HSIC1_FORCE_SUSPEND)) {
			writel(readl(EXYNOS4_PHYPWR)
				| PHY1_STD_ANALOG_POWERDOWN,
				EXYNOS4_PHYPWR);
			writel(PHY_DISABLE, EXYNOS4210_USBHOST_PHY_CONTROL);

			op = 0;
		}
	} else {
		if (phypwr & (PHY1_STD_FORCE_SUSPEND
			| EXYNOS4212_HSIC0_FORCE_SUSPEND
			| EXYNOS4212_HSIC1_FORCE_SUSPEND)) {
			/* unset to normal of Host */
			writel(readl(EXYNOS4_PHYPWR)
				| PHY1_STD_ANALOG_POWERDOWN
				| EXYNOS4212_HSIC0_ANALOG_POWERDOWN
				| EXYNOS4212_HSIC1_ANALOG_POWERDOWN,
				EXYNOS4_PHYPWR);
			/* unset to normal of Device */
			writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
					EXYNOS4_PHYPWR);

			writel(PHY_DISABLE, EXYNOS4x12_HSIC0_PHY_CONTROL);
			writel(PHY_DISABLE, EXYNOS4x12_HSIC1_PHY_CONTROL);
			writel(PHY_DISABLE, EXYNOS4x12_USB_PHY_CONTROL);

			op = 0;
			usb_phy_control.lpa_entered = 1;
		}
	}
done:
	local_irq_restore(flags);
	clk_disable(usb_phy_control.phy_clk);

	return op;
}

static int exynos5_check_usb_op(void)
{
	u32 hostphy_ctrl0, otgphy_sys, hsic_ctrl1, hsic_ctrl2;
	u32 op = 1;
	unsigned long flags;
	int ret;

	local_irq_save(flags);

	/* Check USB 3.0 DRD power first */
	if (exynos5_usb_phy30_is_on()) {
		op = 1;
		goto irq_res;
	}

	ret = clk_enable(usb_phy_control.phy_clk);
	if (ret) {
		op = 0;
		goto irq_res;
	}

	/*If USB Device is power on,  */
	if (exynos_usb_device_phy_is_on()) {
		op = 1;
		goto done;
	} else if (!exynos5_usb_phy20_is_on()) {
		op = 0;
		goto done;
	}

	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	hsic_ctrl1 = readl(EXYNOS5_PHY_HSIC_CTRL1);
	hsic_ctrl2 = readl(EXYNOS5_PHY_HSIC_CTRL2);

	if (hostphy_ctrl0 & HOST_CTRL0_FORCESUSPEND &&
		hsic_ctrl1 & HSIC_CTRL_FORCESUSPEND &&
		hsic_ctrl2 & HSIC_CTRL_FORCESUSPEND) {
		/* unset to normal of Host */
		hostphy_ctrl0 |= (HOST_CTRL0_SIDDQ);
		writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

		/* unset to normal of HSIC */
		writel(hsic_ctrl1 | HSIC_CTRL_SIDDQ, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl2 | HSIC_CTRL_SIDDQ, EXYNOS5_PHY_HSIC_CTRL2);

		/* unset to normal of Device */
		otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
		otgphy_sys |= OTG_SYS_SIDDQ_UOTG;
		writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

		writel(PHY_DISABLE, EXYNOS5_USBHOST_PHY_CONTROL);

		op = 0;
		usb_phy_control.lpa_entered = 1;
	}
done:
	clk_disable(usb_phy_control.phy_clk);
irq_res:
	local_irq_restore(flags);

	return op;
}

/**
 * exynos_check_usb_op - Check usb operation
 *
 * USB operation is checked for AP Power mode.
 * NOTE: Should be checked before Entering AP power mode.
 * exynos4 - USB Host & Device
 * exynos5 - USB Host & Device & DRD
 *
 * return 1 : operation, 0 : stop.
 */
int exynos_check_usb_op(void)
{
	if (soc_is_exynos4210() ||
	    soc_is_exynos4212() ||
	    soc_is_exynos4412())
		return exynos4_check_usb_op();
	else
		return exynos5_check_usb_op();
}

int s5p_usb_phy_suspend(struct platform_device *pdev, int type)
{
	int ret = 0;

	if (exynos_usb_phy_clock_enable(pdev, type))
		return 0;

	mutex_lock(&usb_phy_control.phy_lock);
	if (!strcmp(pdev->name, "s5p-ehci"))
		clear_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "exynos-ohci"))
		clear_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	if (usb_phy_control.flags)
		goto done;

	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos4210() ||
			soc_is_exynos4212() ||
			soc_is_exynos4412())
			ret = exynos4_usb_phy1_suspend(pdev);
		else
			ret = exynos5_usb_phy_host_suspend(pdev);
	}
done:
	mutex_unlock(&usb_phy_control.phy_lock);
	exynos_usb_phy_clock_disable(pdev, type);

	return ret;
}

int s5p_usb_phy_resume(struct platform_device *pdev, int type)
{
	int ret = 0;

	if (exynos_usb_phy_clock_enable(pdev, type))
		return 0;

	mutex_lock(&usb_phy_control.phy_lock);
	if (usb_phy_control.flags)
		goto done;

	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos4210() ||
			soc_is_exynos4212() ||
			soc_is_exynos4412())
			ret = exynos4_usb_phy1_resume(pdev);
		else
			ret = exynos5_usb_phy_host_resume(pdev);
	}
done:
	if (!strcmp(pdev->name, "s5p-ehci"))
		set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "exynos-ohci"))
		set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	mutex_unlock(&usb_phy_control.phy_lock);
	exynos_usb_phy_clock_disable(pdev, type);

	return ret;
}

int s5p_usb_phy_tune(struct platform_device *pdev, int type)
{
	int ret = -EINVAL;

	mutex_lock(&usb_phy_control.phy_lock);

	if (type == S5P_USB_PHY_DRD)
		ret = exynos5_usb_phy30_tune(pdev);

	mutex_unlock(&usb_phy_control.phy_lock);

	return ret;
}

int s5p_usb_phy_init(struct platform_device *pdev, int type)
{
	int ret = -EINVAL;

	if (exynos_usb_phy_clock_enable(pdev, type))
		return ret;

	mutex_lock(&usb_phy_control.phy_lock);
	if (type == S5P_USB_PHY_HOST) {
		if (!strcmp(pdev->name, "s5p-ehci"))
			set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
		else if (!strcmp(pdev->name, "exynos-ohci"))
			set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

		if (soc_is_exynos4210()) {
			ret = exynos4_usb_phy1_init(pdev);
		} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
			ret = exynos4_usb_phy20_init(pdev);
		} else {
			ret = exynos5_usb_phy20_init(pdev);
			set_exynos_usb_phy_tune(type);
		}
	} else if (type == S5P_USB_PHY_DEVICE) {
		if (soc_is_exynos4210()) {
			ret = exynos4_usb_phy0_init(pdev);
		} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
			ret = exynos_usb_dev_phy20_init(pdev);
			s5p_usb_otg_phy_tune(pdev->dev.platform_data, 0);
		} else {
			ret = exynos_usb_dev_phy20_init(pdev);
			set_exynos_usb_phy_tune(type);
		}
	} else if (type == S5P_USB_PHY_DRD) {
		ret = exynos5_usb_phy30_init(pdev);
	}
	mutex_unlock(&usb_phy_control.phy_lock);
	exynos_usb_phy_clock_disable(pdev, type);

	return ret;
}

int s5p_usb_phy_exit(struct platform_device *pdev, int type)
{
	int ret = -EINVAL;

	if (exynos_usb_phy_clock_enable(pdev, type))
		return ret;

	mutex_lock(&usb_phy_control.phy_lock);
	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy1_exit(pdev);
		else if (soc_is_exynos4212() || soc_is_exynos4412())
			ret = exynos4_usb_phy20_exit(pdev);
		else
			ret = exynos5_usb_phy20_exit(pdev);

		if (!strcmp(pdev->name, "s5p-ehci"))
			clear_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
		else if (!strcmp(pdev->name, "exynos-ohci"))
			clear_bit(HOST_PHY_OHCI, &usb_phy_control.flags);
	} else if (type == S5P_USB_PHY_DEVICE) {
		if (soc_is_exynos4210()) {
			ret = exynos4_usb_phy0_exit(pdev);
		} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
			s5p_usb_otg_phy_tune(pdev->dev.platform_data, 1);
			ret = exynos_usb_dev_phy20_exit(pdev);
		} else {
			ret = exynos_usb_dev_phy20_exit(pdev);
		}
	} else if (type == S5P_USB_PHY_DRD) {
		ret = exynos5_usb_phy30_exit(pdev);
	}

	mutex_unlock(&usb_phy_control.phy_lock);
	exynos_usb_phy_clock_disable(pdev, type);

	return ret;
}
