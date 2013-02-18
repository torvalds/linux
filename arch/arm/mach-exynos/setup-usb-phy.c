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

#define ETC6PUD		(S5P_VA_GPIO2 + 0x228)
#define EXYNOS4_USB_CFG		(S3C_VA_SYS + 0x21C)
#define EXYNOS5_USB_CFG		(S3C_VA_SYS + 0x230)

#define PHY_ENABLE	(1 << 0)
#define PHY_DISABLE	(0)

enum usb_host_type {
	HOST_PHY_EHCI	= (0x1 << 0),
	HOST_PHY_OHCI	= (0x1 << 1),
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
};

static struct exynos_usb_phy usb_phy_control;
static atomic_t host_usage;
static DEFINE_MUTEX(phy_lock);
static struct clk *phy_clk = NULL;

static int exynos4_usb_host_phy_is_on(void)
{
	return (readl(EXYNOS4_PHYPWR) & PHY1_STD_ANALOG_POWERDOWN) ? 0 : 1;
}

static int exynos4_usb_device_phy_is_on(void)
{
	int ret;

	if (soc_is_exynos4210())
		ret = (readl(EXYNOS4_PHYPWR) & PHY0_ANALOG_POWERDOWN) ? 0 : 1;
	else
		ret = readl(EXYNOS4_USB_CFG) ? 0 : 1;

	return ret;
}

static int exynos4_usb_phy20_is_on(void)
{
	return exynos4_usb_host_phy_is_on();
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
			usb_phy_control.lpa_entered = 0;
			err = 1;
		} else
			err = 0;
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

	atomic_inc(&host_usage);

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
	phyclk = exynos_usb_phy_set_clock(pdev);
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

	if (atomic_dec_return(&host_usage) > 0) {
		dev_info(&pdev->dev, "still being used\n");
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

	atomic_inc(&host_usage);

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

	exynos_usb_phy_control(USB_PHY
		| USB_PHY_HSIC0
		| USB_PHY_HSIC1,
		PHY_DISABLE);

	usb_phy_control.lpa_entered = 0;

	return 0;
}

static int exynos_usb_dev_phy20_init(struct platform_device *pdev)
{
	if (soc_is_exynos4212() || soc_is_exynos4412()) {
		exynos4_usb_phy20_init(pdev);
		if (usb_phy_control.lpa_entered)
			exynos4_usb_phy1_suspend(pdev);
	}
	writel(0, EXYNOS4_USB_CFG);

	return 0;
}

static int exynos_usb_dev_phy20_exit(struct platform_device *pdev)
{
	if (soc_is_exynos4212() || soc_is_exynos4412())
		exynos4_usb_phy20_exit(pdev);

	return 0;
}

int exynos4_check_usb_op(void)
{
	u32 phypwr;
	u32 op = 1;
	unsigned long flags;
	int ret;

	ret = clk_enable(phy_clk);
	if (ret)
		return 0;

	local_irq_save(flags);
	phypwr = readl(EXYNOS4_PHYPWR);

	/*If USB Device is power on,  */
	if (exynos4_usb_device_phy_is_on()) {
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
			writel(PHY_DISABLE, S5P_USBHOST_PHY_CONTROL);

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

	return 0;
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

	if (type == S5P_USB_PHY_HOST)
		ret = exynos4_usb_phy1_suspend(pdev);
done:
	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
}

int s5p_usb_phy_resume(struct platform_device *pdev, int type)
{
	int ret = 0;

	if (exynos_usb_phy_clock_enable(pdev))
		return 0;

	mutex_lock(&phy_lock);
	if (usb_phy_control.flags)
		goto done;

	if (type == S5P_USB_PHY_HOST)
		ret = exynos4_usb_phy1_resume(pdev);
done:
	if (!strcmp(pdev->name, "s5p-ehci"))
		set_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
	else if (!strcmp(pdev->name, "s5p-ohci"))
		set_bit(HOST_PHY_OHCI, &usb_phy_control.flags);

	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
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

		if (soc_is_exynos4210())
			ret = exynos4_usb_phy1_init(pdev);
		else if (soc_is_exynos4212() || soc_is_exynos4412())
			ret = exynos4_usb_phy20_init(pdev);
	} else if (type == S5P_USB_PHY_DEVICE) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy0_init(pdev);
		else
			ret = exynos_usb_dev_phy20_init(pdev);
	}
	
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

		if (!strcmp(pdev->name, "s5p-ehci"))
			clear_bit(HOST_PHY_EHCI, &usb_phy_control.flags);
		else if (!strcmp(pdev->name, "s5p-ohci"))
			clear_bit(HOST_PHY_OHCI, &usb_phy_control.flags);
	} else if (type == S5P_USB_PHY_DEVICE) {
		if (soc_is_exynos4210())
			ret = exynos4_usb_phy0_exit(pdev);
		else
			ret = exynos_usb_dev_phy20_exit(pdev);
	}
	
	mutex_unlock(&phy_lock);
	exynos_usb_phy_clock_disable(pdev);

	return ret;
}
