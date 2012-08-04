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
#include <mach/regs-pmu.h>
#include <mach/regs-usb-phy.h>
#include <plat/cpu.h>
#include <plat/usb-phy.h>

static atomic_t host_usage;

static int exynos4_usb_host_phy_is_on(void)
{
	return (readl(EXYNOS4_PHYPWR) & PHY1_STD_ANALOG_POWERDOWN) ? 0 : 1;
}

static void exynos4210_usb_phy_clkset(struct platform_device *pdev)
{
	struct clk *xusbxti_clk;
	u32 phyclk;

	xusbxti_clk = clk_get(&pdev->dev, "xusbxti");
	if (xusbxti_clk && !IS_ERR(xusbxti_clk)) {
		if (soc_is_exynos4210()) {
			/* set clock frequency for PLL */
			phyclk = readl(EXYNOS4_PHYCLK) & ~EXYNOS4210_CLKSEL_MASK;

			switch (clk_get_rate(xusbxti_clk)) {
			case 12 * MHZ:
				phyclk |= EXYNOS4210_CLKSEL_12M;
				break;
			case 48 * MHZ:
				phyclk |= EXYNOS4210_CLKSEL_48M;
				break;
			default:
			case 24 * MHZ:
				phyclk |= EXYNOS4210_CLKSEL_24M;
				break;
			}
			writel(phyclk, EXYNOS4_PHYCLK);
		} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
			/* set clock frequency for PLL */
			phyclk = readl(EXYNOS4_PHYCLK) & ~EXYNOS4X12_CLKSEL_MASK;

			switch (clk_get_rate(xusbxti_clk)) {
			case 9600 * KHZ:
				phyclk |= EXYNOS4X12_CLKSEL_9600K;
				break;
			case 10 * MHZ:
				phyclk |= EXYNOS4X12_CLKSEL_10M;
				break;
			case 12 * MHZ:
				phyclk |= EXYNOS4X12_CLKSEL_12M;
				break;
			case 19200 * KHZ:
				phyclk |= EXYNOS4X12_CLKSEL_19200K;
				break;
			case 20 * MHZ:
				phyclk |= EXYNOS4X12_CLKSEL_20M;
				break;
			default:
			case 24 * MHZ:
				/* default reference clock */
				phyclk |= EXYNOS4X12_CLKSEL_24M;
				break;
			}
			writel(phyclk, EXYNOS4_PHYCLK);
		}
		clk_put(xusbxti_clk);
	}
}

static int exynos4210_usb_phy0_init(struct platform_device *pdev)
{
	u32 rstcon;

	writel(readl(S5P_USBDEVICE_PHY_CONTROL) | S5P_USBDEVICE_PHY_ENABLE,
			S5P_USBDEVICE_PHY_CONTROL);

	exynos4210_usb_phy_clkset(pdev);

	/* set to normal PHY0 */
	writel((readl(EXYNOS4_PHYPWR) & ~PHY0_NORMAL_MASK), EXYNOS4_PHYPWR);

	/* reset PHY0 and Link */
	rstcon = readl(EXYNOS4_RSTCON) | PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);

	rstcon &= ~PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);

	return 0;
}

static int exynos4210_usb_phy0_exit(struct platform_device *pdev)
{
	writel((readl(EXYNOS4_PHYPWR) | PHY0_ANALOG_POWERDOWN |
				PHY0_OTG_DISABLE), EXYNOS4_PHYPWR);

	writel(readl(S5P_USBDEVICE_PHY_CONTROL) & ~S5P_USBDEVICE_PHY_ENABLE,
			S5P_USBDEVICE_PHY_CONTROL);

	return 0;
}

static int exynos4210_usb_phy1_init(struct platform_device *pdev)
{
	struct clk *otg_clk;
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

	writel(readl(S5P_USBHOST_PHY_CONTROL) | S5P_USBHOST_PHY_ENABLE,
			S5P_USBHOST_PHY_CONTROL);

	exynos4210_usb_phy_clkset(pdev);

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

static int exynos4210_usb_phy1_exit(struct platform_device *pdev)
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

	writel(readl(S5P_USBHOST_PHY_CONTROL) & ~S5P_USBHOST_PHY_ENABLE,
			S5P_USBHOST_PHY_CONTROL);

	clk_disable(otg_clk);
	clk_put(otg_clk);

	return 0;
}

int s5p_usb_phy_init(struct platform_device *pdev, int type)
{
	if (type == S5P_USB_PHY_DEVICE)
		return exynos4210_usb_phy0_init(pdev);
	else if (type == S5P_USB_PHY_HOST)
		return exynos4210_usb_phy1_init(pdev);

	return -EINVAL;
}

int s5p_usb_phy_exit(struct platform_device *pdev, int type)
{
	if (type == S5P_USB_PHY_DEVICE)
		return exynos4210_usb_phy0_exit(pdev);
	else if (type == S5P_USB_PHY_HOST)
		return exynos4210_usb_phy1_exit(pdev);

	return -EINVAL;
}
