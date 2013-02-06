/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Yulgon Kim <yulgon.kim@samsung.com>
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
#include <mach/regs-pmu5.h>
#include <mach/regs-usb-phy.h>
#include <plat/cpu.h>
#include <plat/usb-phy.h>
#include <plat/regs-usb3-exynos-drd-phy.h>
#include <linux/interrupt.h>
#include <plat/usbgadget.h>
#include <mach/sec_modem.h>

#ifdef CONFIG_USB_OHCI_S5P
   #include <plat/devs.h>
   #include <linux/usb.h>
   #include <linux/usb/otg.h>
   #include <linux/usb/hcd.h>
#endif

#define ETC6PUD		(S5P_VA_GPIO2 + 0x228)
#define EXYNOS4_USB_CFG		(S3C_VA_SYS + 0x21C)
#define EXYNOS5_USB_CFG		(S3C_VA_SYS + 0x230)

#define PHY_ENABLE	(1 << 0)
#define PHY_DISABLE	(0)

enum usb_host_type {
	HOST_PHY_EHCI	= (0x1 << 0),
	HOST_PHY_OHCI	= (0x1 << 1),
	HOST_PHY_DEVICE	= (0x1 << 2),
};

enum usb_phy_type {
	USB_PHY		= (0x1 << 0),
	USB_PHY0	= (0x1 << 0),
	USB_PHY1	= (0x1 << 1),
	USB_PHY_HSIC0	= (0x1 << 1),
	USB_PHY_HSIC1	= (0x1 << 2),
};

struct exynos_usb_phy {
	u8 lpa_entered;
	unsigned long flags;
	unsigned long usage;
};

static struct exynos_usb_phy usb_phy_control;

static atomic_t host_usage;
static DEFINE_MUTEX(phy_lock);
static struct clk *phy_clk = NULL;

static void exynos_usb_mux_change(struct platform_device *pdev, int val)
{
	u32 is_host;
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		is_host = readl(EXYNOS4_USB_CFG);
		writel(val, EXYNOS4_USB_CFG);
	} else {
		is_host = readl(EXYNOS5_USB_CFG);
		writel(val, EXYNOS5_USB_CFG);
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

static int exynos_usb_device_phy_is_on(void)
{
	int ret;

	if (soc_is_exynos4210())
		ret = (readl(EXYNOS4_PHYPWR) & PHY0_ANALOG_POWERDOWN) ? 0 : 1;
	else if (soc_is_exynos4212() || soc_is_exynos4412())
		ret = readl(EXYNOS4_USB_CFG) ? 0 : 1;
	else
		ret = readl(EXYNOS5_USB_CFG) ? 0 : 1;

	return ret;
}

static int exynos4_usb_phy20_is_on(void)
{
	return exynos4_usb_host_phy_is_on();
}

static int exynos5_usb_host_phy20_is_on(void)
{
	return (readl(EXYNOS5_PHY_HOST_CTRL0) & HOST_CTRL0_SIDDQ) ? 0 : 1;
}

static int exynos5_usb_phy30_is_on(void)
{
	return readl(EXYNOS5_USBDEV_PHY_CONTROL) ? 1 : 0;
}

static int exynos_usb_phy_clock_enable(struct platform_device *pdev)
{
	int err;

	if (!phy_clk) {
		if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
			phy_clk = clk_get(&pdev->dev, "usbotg");
		else
			phy_clk = clk_get(&pdev->dev, "usbhost");

		if (IS_ERR(phy_clk)) {
			dev_err(&pdev->dev, "Failed to get phy clock\n");
			return PTR_ERR(phy_clk);
		}
	}

	err = clk_enable(phy_clk);

	return err;
}

static int exynos_usb_phy_clock_disable(struct platform_device *pdev)
{
	if (!phy_clk) {
		if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
			phy_clk = clk_get(&pdev->dev, "usbotg");
		else
			phy_clk = clk_get(&pdev->dev, "usbhost");
		if (IS_ERR(phy_clk)) {
			dev_err(&pdev->dev, "Failed to get phy clock\n");
			return PTR_ERR(phy_clk);
		}
	}

	clk_disable(phy_clk);

	return 0;
}

static u32 exynos_usb_phy_set_clock(struct platform_device *pdev)
{
	struct clk *ref_clk;
	u32 refclk_freq = 0;

	if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
		ref_clk = clk_get(&pdev->dev, "xusbxti");
	else
		ref_clk = clk_get(&pdev->dev, "ext_xtal");

	if (IS_ERR(ref_clk)) {
		dev_err(&pdev->dev, "Failed to get reference clock\n");
		return PTR_ERR(ref_clk);
	}

	if (soc_is_exynos4210()) {
		switch (clk_get_rate(ref_clk)) {
		case 12 * MHZ:
			refclk_freq = EXYNOS4210_CLKSEL_12M;
			break;
		case 48 * MHZ:
			refclk_freq = EXYNOS4210_CLKSEL_48M;
			break;
		case 24 * MHZ:
		default:
			/* default reference clock */
			refclk_freq = EXYNOS4210_CLKSEL_24M;
			break;
		}
	} else if (soc_is_exynos4212() | soc_is_exynos4412()) {
		switch (clk_get_rate(ref_clk)) {
		case 96 * 100000:
			refclk_freq = EXYNOS4212_CLKSEL_9600K;
			break;
		case 10 * MHZ:
			refclk_freq = EXYNOS4212_CLKSEL_10M;
			break;
		case 12 * MHZ:
			refclk_freq = EXYNOS4212_CLKSEL_12M;
			break;
		case 192 * 100000:
			refclk_freq = EXYNOS4212_CLKSEL_19200K;
			break;
		case 20 * MHZ:
			refclk_freq = EXYNOS4212_CLKSEL_20M;
			break;
		case 24 * MHZ:
		default:
			/* default reference clock */
			refclk_freq = EXYNOS4212_CLKSEL_24M;
			break;
		}
	} else {
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
	}
	clk_put(ref_clk);

	return refclk_freq;
}

static void exynos_usb_phy_control(enum usb_phy_type phy_type , int on)
{
	if (soc_is_exynos4210()) {
		if (phy_type & USB_PHY0)
			writel(on, S5P_USBOTG_PHY_CONTROL);
		if (phy_type & USB_PHY1)
			writel(on, S5P_USBHOST_PHY_CONTROL);
	} else if (soc_is_exynos4212() | soc_is_exynos4412()) {
		if (phy_type & USB_PHY)
			writel(on, S5P_USB_PHY_CONTROL);
#ifdef CONFIG_USB_S5P_HSIC0
		if (phy_type & USB_PHY_HSIC0)
			writel(on, S5P_HSIC_1_PHY_CONTROL);
#endif
#ifdef CONFIG_USB_S5P_HSIC1
		if (phy_type & USB_PHY_HSIC1)
			writel(on, S5P_HSIC_2_PHY_CONTROL);
#endif
	} else {
		if (phy_type & USB_PHY0)
			writel(on, EXYNOS5_USBDEV_PHY_CONTROL);
		if (phy_type & USB_PHY1)
			writel(on, EXYNOS5_USBHOST_PHY_CONTROL);
	}
}

static int exynos4_usb_phy0_init(struct platform_device *pdev)
{
	u32 phypwr;
	u32 phyclk;
	u32 rstcon;

	exynos_usb_phy_control(USB_PHY0, PHY_ENABLE);

	/* set clock frequency for PLL */
	phyclk = readl(EXYNOS4_PHYCLK) & ~(EXYNOS4210_CLKSEL_MASK);
	phyclk |= exynos_usb_phy_set_clock(pdev);
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

	exynos_usb_phy_control(USB_PHY0, PHY_DISABLE);

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
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB) \
		|| defined(CONFIG_MDM_HSIC_PM)
			if (!strcmp(pdev->name, "s5p-ehci"))
				set_hsic_lpa_states(STATE_HSIC_LPA_WAKE);
#endif
			usb_phy_control.lpa_entered = 0;
			err = 1;
		} else {
			err = 0;
		}
	} else {
		phypwr = readl(EXYNOS4_PHYPWR);
		/* set to normal HSIC 0 and 1 of PHY1 */
		if (soc_is_exynos4210()) {
			writel(PHY_ENABLE, S5P_USBHOST_PHY_CONTROL);

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
			exynos_usb_phy_control(USB_PHY
				| USB_PHY_HSIC0
				| USB_PHY_HSIC1,
				PHY_ENABLE);

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
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB) \
		|| defined(CONFIG_MDM_HSIC_PM)
		if (!strcmp(pdev->name, "s5p-ehci"))
			set_hsic_lpa_states(STATE_HSIC_LPA_WAKE);
#endif
		usb_phy_control.lpa_entered = 0;
		err = 1;
	}
	udelay(80);

	return err;
}

static int exynos4_usb_phy1_init(struct platform_device *pdev)
{
	u32 phypwr;
	u32 phyclk;
	u32 rstcon;


	if (!strcmp(pdev->name, "s5p-ehci"))
		set_bit(HOST_PHY_EHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		set_bit(HOST_PHY_OHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s3c-usbgadget"))
		set_bit(HOST_PHY_DEVICE, &usb_phy_control.usage);

	dev_info(&pdev->dev, "usb phy usage(%ld)\n",usb_phy_control.usage);

	if (exynos4_usb_host_phy_is_on()) {
		dev_err(&pdev->dev, "Already power on PHY\n");
		return 0;
	}

	/*
	 *  set XuhostOVERCUR to in-active by controlling ET6PUD[15:14]
	 *  0x0 : pull-up/down disabled
	 *  0x1 : pull-down enabled
	 *  0x2 : reserved
	 *  0x3 : pull-up enabled
	 */
	writel((__raw_readl(ETC6PUD) & ~(0x3 << 14)) | (0x3 << 14),
		ETC6PUD);

	exynos_usb_phy_control(USB_PHY1, PHY_ENABLE);

	/* set clock frequency for PLL */
	phyclk = readl(EXYNOS4_PHYCLK) & ~(EXYNOS4210_CLKSEL_MASK);
	phyclk |= exynos_usb_phy_set_clock(pdev);
	phyclk &= ~(PHY1_COMMON_ON_N);
	writel(phyclk, EXYNOS4_PHYCLK);

	/* set to normal HSIC 0 and 1 of PHY1 */
	phypwr = readl(EXYNOS4_PHYPWR);
	phypwr &= ~(PHY1_STD_NORMAL_MASK
		| EXYNOS4210_HSIC0_NORMAL_MASK
		| EXYNOS4210_HSIC1_NORMAL_MASK);
	writel(phypwr, EXYNOS4_PHYPWR);

	/* floating prevention logic: disable */
	writel((readl(EXYNOS4_PHY1CON) | FPENABLEN), EXYNOS4_PHY1CON);

	/* reset all ports of both PHY and Link */
	rstcon = readl(EXYNOS4_RSTCON)
		| EXYNOS4210_HOST_LINK_PORT_SWRST_MASK
		| EXYNOS4210_PHY1_SWRST_MASK;
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(10);

	rstcon &= ~(EXYNOS4210_HOST_LINK_PORT_SWRST_MASK
		| EXYNOS4210_PHY1_SWRST_MASK);
	writel(rstcon, EXYNOS4_RSTCON);
	udelay(80);

	return 0;
}

static int exynos4_usb_phy1_exit(struct platform_device *pdev)
{
	u32 phypwr;


	if (!strcmp(pdev->name, "s5p-ehci"))
		clear_bit(HOST_PHY_EHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		clear_bit(HOST_PHY_OHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s3c-usbgadget"))
		clear_bit(HOST_PHY_DEVICE, &usb_phy_control.usage);

	if (usb_phy_control.usage) {
		dev_info(&pdev->dev, "still being used(%ld)\n",usb_phy_control.usage);
		return -EBUSY;
	}

	phypwr = readl(EXYNOS4_PHYPWR)
		| PHY1_STD_NORMAL_MASK
		| EXYNOS4210_HSIC0_NORMAL_MASK
		| EXYNOS4210_HSIC1_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	exynos_usb_phy_control(USB_PHY1, PHY_DISABLE);

	return 0;
}

static int exynos4_usb_phy20_init(struct platform_device *pdev)
{
	u32 phypwr, phyclk, rstcon;

	if (!strcmp(pdev->name, "s5p-ehci"))
		set_bit(HOST_PHY_EHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		set_bit(HOST_PHY_OHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s3c-usbgadget"))
		set_bit(HOST_PHY_DEVICE, &usb_phy_control.usage);

	dev_info(&pdev->dev, "usb phy usage(%ld)\n", usb_phy_control.usage);

	if (exynos4_usb_phy20_is_on()) {
		dev_err(&pdev->dev, "Already power on PHY\n");
		return 0;
	}

	/*
	 *  set XuhostOVERCUR to in-active by controlling ET6PUD[15:14]
	 *  0x0 : pull-up/down disabled
	 *  0x1 : pull-down enabled
	 *  0x2 : reserved
	 *  0x3 : pull-up enabled
	 */
	writel((__raw_readl(ETC6PUD) & ~(0x3 << 14)) | (0x3 << 14),
		ETC6PUD);

	exynos_usb_phy_control(USB_PHY
		| USB_PHY_HSIC0
		| USB_PHY_HSIC1,
		PHY_ENABLE);

	/* USB MUX change from Device to Host */
	exynos_usb_mux_change(pdev, 1);

	/* set clock frequency for PLL */
	phyclk = exynos_usb_phy_set_clock(pdev);
	/* COMMON Block configuration during suspend */
	phyclk &= ~(PHY0_COMMON_ON_N);
	phyclk &= ~(PHY1_COMMON_ON_N);
	writel(phyclk, EXYNOS4_PHYCLK);

	/* set to normal of Device */
	phypwr = readl(EXYNOS4_PHYPWR) & ~PHY0_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	/* set to normal of Host */
	phypwr = readl(EXYNOS4_PHYPWR);
	phypwr &= ~(PHY1_STD_NORMAL_MASK
		| EXYNOS4212_HSIC0_NORMAL_MASK
		| EXYNOS4212_HSIC1_NORMAL_MASK);
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

	if (!strcmp(pdev->name, "s5p-ehci"))
		clear_bit(HOST_PHY_EHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		clear_bit(HOST_PHY_OHCI, &usb_phy_control.usage);
	else if (!strcmp(pdev->name, "s3c-usbgadget"))
		clear_bit(HOST_PHY_DEVICE, &usb_phy_control.usage);

	if (usb_phy_control.usage) {
		dev_info(&pdev->dev, "still being used(%ld)\n",
			usb_phy_control.usage);
		return -EBUSY;
	} else
		dev_info(&pdev->dev, "usb host phy off\n");

	/* unset to normal of Device */
	writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	/* unset to normal of Host */
	phypwr = readl(EXYNOS4_PHYPWR)
		| PHY1_STD_NORMAL_MASK
		| EXYNOS4212_HSIC0_NORMAL_MASK
		| EXYNOS4212_HSIC1_NORMAL_MASK;
	writel(phypwr, EXYNOS4_PHYPWR);

	exynos_usb_phy_control(USB_PHY
		| USB_PHY_HSIC0
		| USB_PHY_HSIC1,
		PHY_DISABLE);

	usb_phy_control.lpa_entered = 0;

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

	if (exynos5_usb_host_phy20_is_on()) {
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
		exynos_usb_phy_control(USB_PHY1, PHY_ENABLE);

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

	if (exynos5_usb_host_phy20_is_on()) {
		dev_err(&pdev->dev, "Already power on PHY\n");
		return 0;
	}

	exynos_usb_mux_change(pdev, 1);

	exynos_usb_phy_control(USB_PHY1, PHY_ENABLE);

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
	hostphy_ctrl0 |= HOST_CTRL0_COMMONON_N;
	otgphy_sys &= ~(OTG_SYS_COMMON_ON);

	/* otg phy reset */
	otgphy_sys &= ~(OTG_SYS_FORCE_SUSPEND | OTG_SYS_SIDDQ_UOTG | OTG_SYS_FORCE_SLEEP);
	otgphy_sys &= ~(OTG_SYS_REF_CLK_SEL_MASK);
	otgphy_sys |= (OTG_SYS_REF_CLK_SEL(0x2) | OTG_SYS_OTGDISABLE);
	otgphy_sys |= (OTG_SYS_PHY0_SW_RST | OTG_SYS_LINK_SW_RST_UOTG | OTG_SYS_PHYLINK_SW_RESET);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);
	udelay(10);
	otgphy_sys &= ~(OTG_SYS_PHY0_SW_RST | OTG_SYS_LINK_SW_RST_UOTG | OTG_SYS_PHYLINK_SW_RESET);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

	/* host phy reset */
	hostphy_ctrl0 &= ~(HOST_CTRL0_PHYSWRST | HOST_CTRL0_PHYSWRSTALL | HOST_CTRL0_SIDDQ);
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
	/* enable EHCI DMA burst  */
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

	hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) | HSIC_CTRL_REFCLKSEL(0x2) |
			HSIC_CTRL_SIDDQ | HSIC_CTRL_FORCESLEEP | HSIC_CTRL_FORCESUSPEND);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
	writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);

	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);
	hostphy_ctrl0 |= (HOST_CTRL0_SIDDQ);
	hostphy_ctrl0 |= (HOST_CTRL0_FORCESUSPEND | HOST_CTRL0_FORCESLEEP);
	hostphy_ctrl0 |= (HOST_CTRL0_PHYSWRST | HOST_CTRL0_PHYSWRSTALL);
	writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

	otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
	otgphy_sys |= (OTG_SYS_FORCE_SUSPEND | OTG_SYS_SIDDQ_UOTG | OTG_SYS_FORCE_SLEEP);
	writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

	exynos_usb_phy_control(USB_PHY1, PHY_DISABLE);

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

	exynos_usb_mux_change(pdev, 0);

	return 0;
}

static int exynos_usb_dev_phy20_exit(struct platform_device *pdev)
{
	if (soc_is_exynos4212() || soc_is_exynos4412())
		exynos4_usb_phy20_exit(pdev);
	else
		exynos5_usb_phy20_exit(pdev);

	exynos_usb_mux_change(pdev, 1);

	return 0;
}

static int __maybe_unused exynos_usb_hsic_init(struct platform_device *pdev)
{
	u32 rstcon, hsic_ctrl;

	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		exynos_usb_phy_control(USB_PHY_HSIC0
			| USB_PHY_HSIC1,
			PHY_ENABLE);

		/* reset both PHY and Link of Host */
		rstcon = readl(EXYNOS4_RSTCON)
			| EXYNOS4212_PHY1_HSIC0_SWRST
			| EXYNOS4212_PHY1_HSIC1_SWRST;
		writel(rstcon, EXYNOS4_RSTCON);
		udelay(10);

		rstcon &= ~(EXYNOS4212_PHY1_HSIC0_SWRST
			| EXYNOS4212_PHY1_HSIC1_SWRST);
		writel(rstcon, EXYNOS4_RSTCON);
	} else {
		/* HSIC phy reset */
		hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) | HSIC_CTRL_REFCLKSEL(0x2) |
			HSIC_CTRL_PHYSWRST);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
		udelay(10);
		hsic_ctrl &= ~(HSIC_CTRL_PHYSWRST);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
	}

	return 0;
}

static int __maybe_unused exynos_usb_hsic_exit(struct platform_device *pdev)
{
	u32 hsic_ctrl;

	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		exynos_usb_phy_control(USB_PHY_HSIC0
			| USB_PHY_HSIC1,
			PHY_DISABLE);
	} else {
		hsic_ctrl = (HSIC_CTRL_REFCLKDIV(0x24) | HSIC_CTRL_REFCLKSEL(0x2) |
				HSIC_CTRL_SIDDQ | HSIC_CTRL_FORCESLEEP | HSIC_CTRL_FORCESUSPEND);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL1);
		writel(hsic_ctrl, EXYNOS5_PHY_HSIC_CTRL2);
	}

	return 0;
}

static int exynos5_usb_phy30_init(struct platform_device *pdev)
{
	u32 reg;
	bool use_ext_clk = true;

	exynos_usb_phy_control(USB_PHY0, PHY_ENABLE);

	/* Reset USB 3.0 PHY */
	writel(0x00000000, EXYNOS_USB3_PHYREG0);
	writel(0x24d4e6e4, EXYNOS_USB3_PHYPARAM0);
	writel(0x03fff820, EXYNOS_USB3_PHYPARAM1);
	writel(0x00000000, EXYNOS_USB3_PHYRESUME);

	if (soc_is_exynos5250() && samsung_rev() < EXYNOS5250_REV_1_0) {
		writel(0x087fffc0, EXYNOS_USB3_LINKSYSTEM);
		writel(0x00000000, EXYNOS_USB3_PHYBATCHG);
		/* Over-current pin is inactive on SMDK5250 rev 0.0 */
		writel((readl(EXYNOS_USB3_LINKPORT) & ~(0x3<<4)) |
			(0x3<<2), EXYNOS_USB3_LINKPORT);
	} else {
		writel(0x08000000, EXYNOS_USB3_LINKSYSTEM);
		writel(0x00000004, EXYNOS_USB3_PHYBATCHG);
#ifdef CONFIG_USB_EXYNOS_SWITCH
		writel(readl(EXYNOS_USB3_LINKPORT) |
			(0xf<<2), EXYNOS_USB3_LINKPORT);
#endif
		/* REVISIT :use externel clock 100MHz */
		if (use_ext_clk)
			writel(readl(EXYNOS_USB3_PHYPARAM0) | (0x1<<31),
				EXYNOS_USB3_PHYPARAM0);
		else
			writel(readl(EXYNOS_USB3_PHYPARAM0) & ~(0x1<<31),
				EXYNOS_USB3_PHYPARAM0);
	}

	/* UTMI Power Control */
	writel(EXYNOS_USB3_PHYUTMI_OTGDISABLE, EXYNOS_USB3_PHYUTMI);

	/* Set 100MHz external clock */
	reg = EXYNOS_USB3_PHYCLKRST_PORTRESET |
		/* HS PLL uses ref_pad_clk{p,m} or ref_alt_clk_{p,m}
		* as reference */
		EXYNOS_USB3_PHYCLKRST_REFCLKSEL(2) |
		/* Digital power supply in normal operating mode */
		EXYNOS_USB3_PHYCLKRST_RETENABLEN |
		/* 0x27-100MHz, 0x2a-24MHz, 0x31-20MHz, 0x38-19.2MHz */
		EXYNOS_USB3_PHYCLKRST_FSEL(0x27) |
		/* 0x19-100MHz, 0x68-24MHz, 0x7d-20Mhz */
		EXYNOS_USB3_PHYCLKRST_MPLL_MULTIPLIER(0x19) |
		/* Enable ref clock for SS function */
		EXYNOS_USB3_PHYCLKRST_REF_SSP_EN |
		/* Enable spread spectrum */
		EXYNOS_USB3_PHYCLKRST_SSC_EN;

	if (!(soc_is_exynos5250() && samsung_rev() < EXYNOS5250_REV_1_0))
		reg |= EXYNOS_USB3_PHYCLKRST_COMMONONN;

	writel(reg, EXYNOS_USB3_PHYCLKRST);

	udelay(10);

	reg &= ~(EXYNOS_USB3_PHYCLKRST_PORTRESET);
	writel(reg, EXYNOS_USB3_PHYCLKRST);

	return 0;
}

static int exynos5_usb_phy30_exit(struct platform_device *pdev)
{
	u32 reg;

	reg = EXYNOS_USB3_PHYUTMI_OTGDISABLE |
		EXYNOS_USB3_PHYUTMI_FORCESUSPEND |
		EXYNOS_USB3_PHYUTMI_FORCESLEEP;
	writel(reg, EXYNOS_USB3_PHYUTMI);

	exynos_usb_phy_control(USB_PHY0, PHY_DISABLE);

	return 0;
}

int exynos4_check_usb_op(void)
{
	u32 phypwr;
	u32 op = 1;
	unsigned long flags;
	int ret;

#if defined(CONFIG_MDM_HSIC_PM)
	/* if it is normal boot, block lpa till modem boot */
	if (set_hsic_lpa_states(STATE_HSIC_LPA_CHECK))
		return 1;
#endif
	ret = clk_enable(phy_clk);
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
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
			/* HSIC LPA: LPA USB phy retention reume call the usb
			* reset resume, so we should let CP to HSIC L3 mode. */
			set_hsic_lpa_states(STATE_HSIC_LPA_ENTER);
#endif
			writel(readl(EXYNOS4_PHYPWR)
				| PHY1_STD_ANALOG_POWERDOWN,
				EXYNOS4_PHYPWR);
			writel(PHY_DISABLE, S5P_USBHOST_PHY_CONTROL);

			op = 0;
		}
	} else {
		if (phypwr & (PHY1_STD_FORCE_SUSPEND
			| EXYNOS4212_HSIC0_FORCE_SUSPEND
			| EXYNOS4212_HSIC1_FORCE_SUSPEND)) {
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB) \
		|| defined(CONFIG_MDM_HSIC_PM)
			/* HSIC LPA: LPA USB phy retention reume call the usb
			* reset resume, so we should let CP to HSIC L3 mode. */
			set_hsic_lpa_states(STATE_HSIC_LPA_ENTER);
#endif
			/* unset to normal of Host */
			writel(readl(EXYNOS4_PHYPWR)
				| PHY1_STD_ANALOG_POWERDOWN
				| EXYNOS4212_HSIC0_ANALOG_POWERDOWN
				| EXYNOS4212_HSIC1_ANALOG_POWERDOWN,
				EXYNOS4_PHYPWR);
			/* unset to normal of Device */
			writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
					EXYNOS4_PHYPWR);

			exynos_usb_phy_control(USB_PHY
				| USB_PHY_HSIC0
				| USB_PHY_HSIC1,
				PHY_DISABLE);

			op = 0;
			usb_phy_control.lpa_entered = 1;
		}
	}
done:
	local_irq_restore(flags);
	clk_disable(phy_clk);

	return op;
}

static int exynos5_check_usb_op(void)
{
	u32 hostphy_ctrl0, otgphy_sys;
	u32 op = 1;
	unsigned long flags;
	int ret;

	ret = clk_enable(phy_clk);
	if (ret)
		return 0;

	local_irq_save(flags);
	/* Check USB 3.0 DRD power */
	if (exynos5_usb_phy30_is_on()) {
		op = 1;
		goto done;
	}
	/*If USB Device is power on,  */
	if (exynos_usb_device_phy_is_on()) {
		op = 1;
		goto done;
	} else if (!exynos5_usb_host_phy20_is_on()) {
		op = 0;
		goto done;
	}

	hostphy_ctrl0 = readl(EXYNOS5_PHY_HOST_CTRL0);

	if (hostphy_ctrl0 & HOST_CTRL0_FORCESUSPEND) {
		/* unset to normal of Host */
		hostphy_ctrl0 |= (HOST_CTRL0_SIDDQ);
		writel(hostphy_ctrl0, EXYNOS5_PHY_HOST_CTRL0);

		/* unset to normal of Device */
		otgphy_sys = readl(EXYNOS5_PHY_OTG_SYS);
		otgphy_sys |= OTG_SYS_SIDDQ_UOTG;
		writel(otgphy_sys, EXYNOS5_PHY_OTG_SYS);

		exynos_usb_phy_control(USB_PHY1,
			PHY_DISABLE);

		op = 0;
		usb_phy_control.lpa_entered = 1;
	}
done:
	local_irq_restore(flags);
	clk_disable(phy_clk);

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

	if (exynos_usb_phy_clock_enable(pdev))
		return 0;

	mutex_lock(&phy_lock);

	if (!strcmp(pdev->name, "s5p-ehci"))
		clear_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		clear_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	if (usb_phy_control.flags)
		goto done;

	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos4210() ||
			soc_is_exynos4212() ||
			soc_is_exynos4412()) {
			dev_info(&pdev->dev, "host_phy_susp\n");
			ret = exynos4_usb_phy1_suspend(pdev);
		} else
			ret = exynos5_usb_phy_host_suspend(pdev);
	}
done:
	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
}

int s5p_usb_phy_resume(struct platform_device *pdev, int type)
{
	int ret = 0;
	u32 phyclk;

	if (exynos_usb_phy_clock_enable(pdev))
		return 0;

	mutex_lock(&phy_lock);

	if (usb_phy_control.flags)
		goto done;

	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos4210() ||
			soc_is_exynos4212() ||
			soc_is_exynos4412()) {
			ret = exynos4_usb_phy1_resume(pdev);
		} else
			ret = exynos5_usb_phy_host_resume(pdev);
	}
done:
	if (!strcmp(pdev->name, "s5p-ehci"))
		set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
}

int s5p_usb_phy0_tune(struct s5p_usbgadget_platdata *pdata, int def_mode)
{
	u32 phytune;
	static u32 def_phytune;

	if (!pdata)
		return -1;

	printk(KERN_DEBUG "usb: %s read original tune\n", __func__);
	phytune = readl(PHY0_PHYTUNE);
	if (!def_phytune) {
		def_phytune = phytune;
		printk(KERN_DEBUG "usb: %s save default phytune (0x%x)\n",
				__func__, def_phytune);
	}

	printk(KERN_DEBUG "usb: %s original tune=0x%x\n",
			__func__, phytune);
	printk(KERN_DEBUG "usb: %s tune_mask=0x%x, tune=0x%x\n",
			__func__, pdata->phy_tune_mask, pdata->phy_tune);

	if (pdata->phy_tune_mask) {
		phytune &= ~(pdata->phy_tune_mask);
		phytune |= pdata->phy_tune;
		udelay(10);
		if (def_mode) {
			printk(KERN_DEBUG "usb: %s set defult tune=0x%x\n",
					__func__, def_phytune);
			writel(def_phytune, PHY0_PHYTUNE);

		} else {
			printk(KERN_DEBUG "usb: %s custom tune=0x%x\n",
				__func__, phytune);
			writel(phytune, PHY0_PHYTUNE);
		}
		phytune = readl(PHY0_PHYTUNE);
		printk(KERN_DEBUG "usb: %s modified tune=0x%x\n",
				__func__, phytune);
	} else
		printk(KERN_DEBUG "usb: %s default tune\n", __func__);

	return 0;
}

void set_exynos_usb_phy_tune(int type)
{
	u32 phytune;
	if (soc_is_exynos4412()) {
		if (type == S5P_USB_PHY_DEVICE) {
			phytune = readl(PHY0_PHYTUNE);
			printk(KERN_DEBUG "usb: %s old phy0 tune=0x%x t=%d\n",
					__func__, phytune, type);
			/* sqrxtune [13:11] 3b110 : -15% */
			phytune &= ~(0x7 << 11);
			phytune |= (0x6 << 11);
			udelay(10);
			writel(phytune, PHY0_PHYTUNE);
			phytune = readl(PHY0_PHYTUNE);
			printk(KERN_DEBUG "usb: %s new phy0 tune=0x%x\n",
					__func__, phytune);
		} else if (type == S5P_USB_PHY_HOST) {
			phytune = readl(PHY1_PHYTUNE);
			printk(KERN_DEBUG "usb: %s old phy1 tune=0x%x t=%d\n",
					__func__, phytune, type);
			/* sqrxtune [13:11] 3b110 : -15% */
			phytune &= ~(0x7 << 11);
			phytune |= (0x6 << 11);
			udelay(10);
			writel(phytune, PHY1_PHYTUNE);
			phytune = readl(PHY1_PHYTUNE);
			printk(KERN_DEBUG "usb: %s new phy1 tune=0x%x\n",
					__func__, phytune);
		}
	} else
		printk(KERN_DEBUG "usb: %s it is not exynos4412.(t=%d)\n",
				__func__, type);
}

int s5p_usb_phy_init(struct platform_device *pdev, int type)
{
	int ret = -EINVAL;

	if (exynos_usb_phy_clock_enable(pdev))
		return ret;

	mutex_lock(&phy_lock);

	if (type == S5P_USB_PHY_HOST) {
		if (!strcmp(pdev->name, "s5p-ehci"))
			set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
		else if (!strcmp(pdev->name, "s5p-ohci"))
			set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB) \
		|| defined(CONFIG_MDM_HSIC_PM)
		/* HSIC LPA: Let CP know the slave wakeup from LPA wakeup */
		if (!strcmp(pdev->name, "s5p-ehci"))
			set_hsic_lpa_states(STATE_HSIC_LPA_PHY_INIT);
#endif
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy1_init(pdev);
		else if (soc_is_exynos4212() || soc_is_exynos4412()) {
			ret = exynos4_usb_phy20_init(pdev);
			set_exynos_usb_phy_tune(type);
		} else
			ret = exynos5_usb_phy20_init(pdev);
	} else if (type == S5P_USB_PHY_DEVICE) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy0_init(pdev);
		else {
			ret = exynos_usb_dev_phy20_init(pdev);
			set_exynos_usb_phy_tune(type);
		}
		/* set custom usb phy tune */
		if (pdev->dev.platform_data)
			ret = s5p_usb_phy0_tune(pdev->dev.platform_data, 0);
	} else if (type == S5P_USB_PHY_OTGHOST) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy0_init(pdev);
		else
			ret = exynos_usb_dev_phy20_init(pdev);
	} else if (type == S5P_USB_PHY_DRD)
		ret = exynos5_usb_phy30_init(pdev);

	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
}

int s5p_usb_phy_exit(struct platform_device *pdev, int type)
{
	int ret = -EINVAL;

	if (exynos_usb_phy_clock_enable(pdev))
		return ret;

	mutex_lock(&phy_lock);

	if (type == S5P_USB_PHY_HOST) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy1_exit(pdev);
		else if (soc_is_exynos4212() || soc_is_exynos4412())
			ret = exynos4_usb_phy20_exit(pdev);
		else
			ret = exynos5_usb_phy20_exit(pdev);

		if (!strcmp(pdev->name, "s5p-ehci"))
			clear_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
		else if (!strcmp(pdev->name, "s5p-ohci"))
			clear_bit(HOST_PHY_OHCI, &usb_phy_control.flags);
	} else if (type == S5P_USB_PHY_DEVICE) {
		/* set default usb phy tune */
		if (pdev->dev.platform_data && soc_is_exynos4210())
			ret = s5p_usb_phy0_tune(pdev->dev.platform_data, 1);
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy0_exit(pdev);
		else
			ret = exynos_usb_dev_phy20_exit(pdev);
	} else if (type == S5P_USB_PHY_DRD)
		ret = exynos5_usb_phy30_exit(pdev);
	else if (type == S5P_USB_PHY_OTGHOST) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy0_exit(pdev);
		else
			ret = exynos_usb_dev_phy20_exit(pdev);
	}

	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
}
