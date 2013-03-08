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
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <mach/regs-pmu.h>
#include <mach/regs-usb-phy.h>
#include <plat/cpu.h>
#include <plat/usb-phy.h>
#include <plat/gpio-cfg.h>

#define EXYNOS4_USB_CFG		(S3C_VA_SYS + 0x21C)
#define ETC6PUD			(0x228)

enum usb_host_type {
	HOST_PHY_EHCI	= (0x1 << 0),
	HOST_PHY_OHCI	= (0x1 << 1),
};

struct exynos_usb_phy {
	u8 lpa_entered;
	unsigned long flags;
	bool phy_is_suspended;
};

static struct exynos_usb_phy usb_phy_control;
static atomic_t host_usage;
static DEFINE_SPINLOCK(phy_lock);

static int exynos4210_usb_phy1_init(struct platform_device *pdev);
static int exynos4210_usb_phy1_exit(struct platform_device *pdev);

int exynos4_usb_host_phy1_is_suspend(void)
{
	return (usb_phy_control.phy_is_suspended) ? 0 : 1;
}

static void exynos_usb_mux_change(struct platform_device *pdev, int val)
{
	u32 is_host;
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		is_host = readl(EXYNOS4_USB_CFG);
		writel(val, EXYNOS4_USB_CFG);
	}

	if (is_host != val)
		dev_dbg(&pdev->dev, "Change USB MUX from %s to %s",
			is_host ? "Host" : "Device",
			val ? "Host" : "Device");
}

static int exynos4_usb_host_phy_is_on(void)
{
	return (readl(EXYNOS4_PHYPWR) & PHY1_STD_ANALOG_POWERDOWN) ? 0 : 1;
}

static int exynos_usb_phy_clock_enable(struct platform_device *pdev)
{
	struct clk *phy_clk;
	int err;

	if (soc_is_exynos4210() || soc_is_exynos4212()
					|| soc_is_exynos4412())
		phy_clk = clk_get(&pdev->dev, "otg");
	else
		phy_clk = clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(phy_clk)) {
		dev_err(&pdev->dev, "Failed to get phy clock\n");
		return PTR_ERR(phy_clk);
	}

	err = clk_enable(phy_clk);
	if (err)
		dev_err(&pdev->dev, "Phy clock enable failed\n");

	clk_put(phy_clk);
	return err;
}

static int exynos_usb_phy_clock_disable(struct platform_device *pdev)
{
	struct clk *phy_clk;

	if (soc_is_exynos4210() || soc_is_exynos4212()
					|| soc_is_exynos4412())
		phy_clk = clk_get(&pdev->dev, "otg");
	else
		phy_clk = clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(phy_clk)) {
		dev_err(&pdev->dev, "Failed to get phy clock\n");
		return PTR_ERR(phy_clk);
	}

	clk_disable(phy_clk);
	clk_put(phy_clk);

	return 0;
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

static int exynos4_usb_phy1_suspend(struct platform_device *pdev)
{
	u32 phypwr;

	/* set to suspend HSIC 0 and 1 and standard of PHY1 */
	phypwr = readl(EXYNOS4_PHYPWR);
	phypwr |= (PHY1_STD_FORCE_SUSPEND | EXYNOS4X12_HSIC0_FORCE_SUSPEND
					| EXYNOS4X12_HSIC1_FORCE_SUSPEND);
	writel(phypwr, EXYNOS4_PHYPWR);
	usb_phy_control.phy_is_suspended = true;

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
		phypwr &= ~(PHY1_STD_FORCE_SUSPEND
			| EXYNOS4X12_HSIC0_FORCE_SUSPEND
			| EXYNOS4X12_HSIC1_FORCE_SUSPEND);
		writel(phypwr, EXYNOS4_PHYPWR);
		if (usb_phy_control.lpa_entered) {
			usb_phy_control.lpa_entered = 0;
			err = 1;
		} else
			err = 0;
	} else {
		/* set to normal HSIC 0 and 1 of PHY1 */
		writel(S5P_USBHOST_PHY_ENABLE, S5P_USBHOST_PHY_CONTROL);
#ifdef CONFIG_USB_S5P_HSIC0
		writel(S5P_HSIC_1_PHY_ENABLE, S5P_HSIC_1_PHY_CONTROL);
#endif
#ifdef CONFIG_USB_S5P_HSIC1
		writel(S5P_HSIC_2_PHY_ENABLE, S5P_HSIC_2_PHY_CONTROL);
#endif

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
			| EXYNOS4X12_HSIC0_NORMAL_MASK
			| EXYNOS4X12_HSIC1_NORMAL_MASK);
		writel(phypwr, EXYNOS4_PHYPWR);

		/* reset all ports of both PHY and Link */
		rstcon = readl(EXYNOS4_RSTCON)
			| EXYNOS4X12_HOST_LINK_PORT_SWRST_MASK
			| EXYNOS4X12_PHY1_SWRST_MASK;
		writel(rstcon, EXYNOS4_RSTCON);
		udelay(10);

		rstcon &= ~(EXYNOS4X12_HOST_LINK_PORT_SWRST_MASK
			| EXYNOS4X12_PHY1_SWRST_MASK);
		writel(rstcon, EXYNOS4_RSTCON);
		usb_phy_control.lpa_entered = 0;
		err = 1;
	}
	udelay(80);
	usb_phy_control.phy_is_suspended = false;

	return err;
}


static int exynos4210_usb_phy0_init(struct platform_device *pdev)
{
	u32 rstcon;
	if (soc_is_exynos4412()) {
		exynos4210_usb_phy1_init(pdev);
		exynos_usb_mux_change(pdev, 0);
		if (usb_phy_control.lpa_entered)
			exynos4_usb_phy1_suspend(pdev);
	} else {

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
	}
	return 0;
}

static int exynos4210_usb_phy0_exit(struct platform_device *pdev)
{
	if (soc_is_exynos4412()) {
		exynos4210_usb_phy1_exit(pdev);
		exynos_usb_mux_change(pdev, 1);
	} else {
		writel((readl(EXYNOS4_PHYPWR) | PHY0_ANALOG_POWERDOWN |
					PHY0_OTG_DISABLE), EXYNOS4_PHYPWR);

		writel(readl(S5P_USBDEVICE_PHY_CONTROL) & ~S5P_USBDEVICE_PHY_ENABLE,
				S5P_USBDEVICE_PHY_CONTROL);
	}
	return 0;
}

static int exynos4210_usb_phy1_init(struct platform_device *pdev)
{
	u32 phypwr, phyclk, rstcon;
	void __iomem *gpio2_base;
	u8 gpx2_config, gpx3_config, gpx2_dat, gpx3_dat;
	int err;

	atomic_inc(&host_usage);

	err = exynos_usb_phy_clock_enable(pdev);
	if (err)
		return err;

	if (exynos4_usb_host_phy_is_on()) {
		dev_info(&pdev->dev, "PHY already ON\n");
		exynos_usb_phy_clock_disable(pdev);
		return 0;
	}
	/*
	 *  set XuhostOVERCUR to in-active by controlling ET6PUD[15:14]
	 *  0x0 : pull-up/down disabled
	 *  0x1 : pull-down enabled
	 *  0x2 : reserved
	 *  0x3 : pull-up enabled
	 */
	gpio2_base = ioremap(EXYNOS4_PA_GPIO2, SZ_4K);
	writel((__raw_readl(gpio2_base + ETC6PUD) & ~(0x3 << 14)) | (0x3 << 14),
		gpio2_base + ETC6PUD);

	err = gpio_request(EXYNOS4_GPX2(7), "HSIC0");
	if (err)
		dev_err(&pdev->dev, "Failed to get GPIO for HSIC0\n");

	err = gpio_request(EXYNOS4_GPX3(0), "HSIC1");
	if (err)
		dev_err(&pdev->dev, "Failed to get GPIO for HSIC1\n");

	gpx2_config = s3c_gpio_getcfg(EXYNOS4_GPX2(7));
	gpx3_config = s3c_gpio_getcfg(EXYNOS4_GPX3(0));

	gpx2_dat = gpio_get_value(EXYNOS4_GPX2(7));
	gpx3_dat = gpio_get_value(EXYNOS4_GPX3(0));

	s3c_gpio_cfgpin(EXYNOS4_GPX2(7), S3C_GPIO_OUTPUT);
	s3c_gpio_cfgpin(EXYNOS4_GPX3(0), S3C_GPIO_OUTPUT);

	gpio_set_value(EXYNOS4_GPX2(7), 0);
	gpio_set_value(EXYNOS4_GPX3(0), 0);

	writel(S5P_USBHOST_PHY_ENABLE, S5P_USBHOST_PHY_CONTROL);
#ifdef CONFIG_USB_S5P_HSIC0
	writel(S5P_HSIC_1_PHY_ENABLE, S5P_HSIC_1_PHY_CONTROL);
#endif
#ifdef CONFIG_USB_S5P_HSIC1
	writel(S5P_HSIC_2_PHY_ENABLE, S5P_HSIC_2_PHY_CONTROL);
#endif
	/* Enable USB device PHY */
	writel(readl(S5P_USBDEVICE_PHY_CONTROL) | S5P_USBDEVICE_PHY_ENABLE,
			S5P_USBDEVICE_PHY_CONTROL);

	/* USB MUX change from Device to Host */
	exynos_usb_mux_change(pdev, 1);

	exynos4210_usb_phy_clkset(pdev);
	/* COMMON Block configuration during suspend */
	phyclk = readl(EXYNOS4_PHYCLK);
	phyclk &= ~(PHY0_COMMON_ON_N | PHY1_COMMON_ON_N);
	writel(phyclk, EXYNOS4_PHYCLK);

	/* floating prevention logic: disable */
	writel((readl(EXYNOS4_PHY1CON) | FPENABLEN), EXYNOS4_PHY1CON);

	/* set to normal of Device */
	phypwr = readl(EXYNOS4_PHYPWR) & ~PHY0_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	/* set to normal of Host */
	phypwr = readl(EXYNOS4_PHYPWR);
	phypwr &= ~(PHY1_STD_NORMAL_MASK
		| EXYNOS4X12_HSIC0_NORMAL_MASK
		| EXYNOS4X12_HSIC1_NORMAL_MASK);
	writel(phypwr, EXYNOS4_PHYPWR);

	/* reset both PHY and Link of Device */
	rstcon = readl(EXYNOS4_RSTCON) | PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);
	rstcon &= ~PHY0_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);

	/* reset all ports of both PHY and Link */
	rstcon = readl(EXYNOS4_RSTCON)
		| EXYNOS4X12_HOST_LINK_PORT_SWRST_MASK
		| EXYNOS4X12_PHY1_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);

	rstcon &= ~(EXYNOS4X12_HOST_LINK_PORT_SWRST_MASK
			| EXYNOS4X12_PHY1_SWRST_MASK);
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(80);

	exynos_usb_phy_clock_disable(pdev);

	mdelay(1);
	gpio_set_value(EXYNOS4_GPX2(7), 1);
	gpio_set_value(EXYNOS4_GPX3(0), 1);

	/* restore the initial value */
	s3c_gpio_cfgpin(EXYNOS4_GPX2(7), gpx2_config);
	s3c_gpio_cfgpin(EXYNOS4_GPX3(0), gpx3_config);

	gpio_set_value(EXYNOS4_GPX2(7), gpx2_dat);
	gpio_set_value(EXYNOS4_GPX3(0), gpx3_dat);

	gpio_free(EXYNOS4_GPX2(7));
	gpio_free(EXYNOS4_GPX3(0));

	return 0;
}

static int exynos4210_usb_phy1_exit(struct platform_device *pdev)
{
	u32 phypwr;
	int err;

	if (atomic_dec_return(&host_usage) > 0) {
		dev_info(&pdev->dev, "still being used\n");
		return 0;
	}

	err = exynos_usb_phy_clock_enable(pdev);
	if (err)
		return err;

	/* unset to normal of Device */
	writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	/* unset to normal of Host */
	phypwr = readl(EXYNOS4_PHYPWR)
		| PHY1_STD_NORMAL_MASK
		| EXYNOS4X12_HSIC0_NORMAL_MASK
		| EXYNOS4X12_HSIC1_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);
	writel((readl(EXYNOS4_PHYPWR) | PHY1_STD_ANALOG_POWERDOWN),
			EXYNOS4_PHYPWR);

	writel(readl(S5P_USBHOST_PHY_CONTROL) & ~S5P_USBHOST_PHY_ENABLE,
			S5P_USBHOST_PHY_CONTROL);
#ifdef CONFIG_USB_S5P_HSIC0
	writel(readl(S5P_HSIC_1_PHY_CONTROL) & (~S5P_HSIC_1_PHY_ENABLE),
			S5P_HSIC_1_PHY_CONTROL);
#endif
#ifdef CONFIG_USB_S5P_HSIC1
	writel(readl(S5P_HSIC_2_PHY_CONTROL) & (~S5P_HSIC_2_PHY_ENABLE),
			S5P_HSIC_2_PHY_CONTROL);
#endif
	/* Even for the HOST mode only we have to enable below */
	writel(readl(S5P_USBDEVICE_PHY_CONTROL) & (~S5P_USBDEVICE_PHY_ENABLE),
			S5P_USBDEVICE_PHY_CONTROL);

	exynos_usb_phy_clock_disable(pdev);

	return 0;
}

int s5p_usb_phy_suspend(struct platform_device *pdev, int type)
{
	int ret = 0;

	spin_lock(&phy_lock);
	if (!strcmp(pdev->name, "s5p-ehci"))
		clear_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "exynos-ohci"))
		clear_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	if (usb_phy_control.flags)
		goto done;

	ret = exynos_usb_phy_clock_enable(pdev);
	if (ret)
		goto done;

	if (type == S5P_USB_PHY_HOST)
		ret = exynos4_usb_phy1_suspend(pdev);

	exynos_usb_phy_clock_disable(pdev);
done:
	spin_unlock(&phy_lock);
	return ret;
}

int s5p_usb_phy_resume(struct platform_device *pdev, int type)
{
	int ret = 0;

	spin_lock(&phy_lock);
	if (usb_phy_control.flags)
		goto done;

	ret = exynos_usb_phy_clock_enable(pdev);
	if (ret) {
		spin_unlock(&phy_lock);
		return ret;
	}

	if (type == S5P_USB_PHY_HOST)
		ret = exynos4_usb_phy1_resume(pdev);

	exynos_usb_phy_clock_disable(pdev);
done:
	if (!strcmp(pdev->name, "s5p-ehci"))
		set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "exynos-ohci"))
		set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	spin_unlock(&phy_lock);
	return ret;
}

int s5p_usb_phy_init(struct platform_device *pdev, int type)
{
	if (type == S5P_USB_PHY_DEVICE)
		return exynos4210_usb_phy0_init(pdev);
	else if (type == S5P_USB_PHY_HOST) {
		if (!strcmp(pdev->name, "s5p-ehci"))
			set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
		else if (!strcmp(pdev->name, "exynos-ohci"))
			set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);
		return exynos4210_usb_phy1_init(pdev);
	}

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
