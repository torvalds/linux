/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/platform_data/dwc3-exynos.h>

#include <plat/ehci.h>
#include <plat/devs.h>
#include <plat/usb-phy.h>
#include <plat/gpio-cfg.h>

#include <mach/ohci.h>
#include <mach/usb3-drd.h>
#include <mach/usb-switch.h>

static int smdk5410_vbus_ctrl(struct platform_device *pdev, int on)
{
	int phy_num = pdev->id;
	unsigned gpio;
	int ret = -EINVAL;

	if (phy_num == 0)
		gpio = EXYNOS5410_GPK3(3);
	else if (phy_num == 1)
		gpio = EXYNOS5410_GPK2(7);
	else
		return ret;

	ret = gpio_request(gpio, "UDRD3_VBUSCTRL_U3");
	if (ret < 0) {
		pr_err("failed to request UDRD3_%d_VBUSCTRL_U3\n",
				phy_num);
		return ret;
	}

	gpio_set_value(gpio, !!on);
	gpio_free(gpio);

	return ret;
}

#define SMDK5410_ID0_GPIO	EXYNOS5410_GPX1(7)
#define SMDK5410_ID1_GPIO	EXYNOS5410_GPX2(4)
#define SMDK5410_VBUS0_GPIO	EXYNOS5410_GPX0(6)
#define SMDK5410_VBUS1_GPIO	EXYNOS5410_GPX2(3)

static int smdk5410_get_id_state(struct platform_device *pdev)
{
	int phy_num = pdev->id;
	unsigned gpio;

	if (phy_num == 0)
		gpio = SMDK5410_ID0_GPIO;
	else if (phy_num == 1)
		gpio = SMDK5410_ID1_GPIO;
	else
		return -EINVAL;

	return gpio_get_value(gpio);
}

static bool smdk5410_get_bsession_valid(struct platform_device *pdev)
{
	int phy_num = pdev->id;
	unsigned gpio;

	if (phy_num == 0)
		gpio = SMDK5410_VBUS0_GPIO;
	else if (phy_num == 1)
		gpio = SMDK5410_VBUS1_GPIO;
	else
		/*
		 * If something goes wrong, we return true,
		 * because we don't want switch stop working.
		 */
		return true;

	return !!gpio_get_value(gpio);
}

static struct exynos4_ohci_platdata smdk5410_ohci_pdata __initdata;
static struct s5p_ehci_platdata smdk5410_ehci_pdata __initdata;
static struct dwc3_exynos_data smdk5410_drd_pdata __initdata = {
	.udc_name		= "exynos-ss-udc",
	.xhci_name		= "exynos-xhci",
	.phy_type		= S5P_USB_PHY_DRD,
	.phy_init		= s5p_usb_phy_init,
	.phy_exit		= s5p_usb_phy_exit,
	.phy_crport_ctrl	= exynos5_usb_phy_crport_ctrl,
	.vbus_ctrl		= smdk5410_vbus_ctrl,
	.get_id_state		= smdk5410_get_id_state,
	.get_bses_vld		= smdk5410_get_bsession_valid,
	.irq_flags		= IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
};

static void __init smdk5410_ohci_init(void)
{
	exynos4_ohci_set_platdata(&smdk5410_ohci_pdata);
}

static void __init smdk5410_ehci_init(void)
{
	s5p_ehci_set_platdata(&smdk5410_ehci_pdata);
}

static void __init smdk5410_drd_phy_shutdown(struct platform_device *pdev)
{
	int phy_num = pdev->id;
	struct clk *clk;

	switch (phy_num) {
	case 0:
		clk = clk_get_sys("exynos-dwc3.0", "usbdrd30");
		break;
	case 1:
		clk = clk_get_sys("exynos-dwc3.1", "usbdrd30");
		break;
	default:
		clk = NULL;
		break;
	}

	if (IS_ERR_OR_NULL(clk)) {
		printk(KERN_ERR "failed to get DRD%d phy clock\n", phy_num);
		return;
	}

	if (clk_enable(clk)) {
		printk(KERN_ERR "failed to enable DRD%d clock\n", phy_num);
		return;
	}

	s5p_usb_phy_exit(pdev, S5P_USB_PHY_DRD);

	clk_disable(clk);
}

static void __init __maybe_unused smdk5410_drd0_init(void)
{
	/* Initialize DRD0 gpio */
	if (gpio_request(EXYNOS5410_GPK3(0), "UDRD3_0_OVERCUR_U2")) {
		pr_err("failed to request UDRD3_0_OVERCUR_U2\n");
	} else {
		s3c_gpio_cfgpin(EXYNOS5410_GPK3(0), (0x2 << 0));
		s3c_gpio_setpull(EXYNOS5410_GPK3(0), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK3(0));
	}
	if (gpio_request(EXYNOS5410_GPK3(1), "UDRD3_0_OVERCUR_U3")) {
		pr_err("failed to request UDRD3_0_OVERCUR_U3\n");
	} else {
		s3c_gpio_cfgpin(EXYNOS5410_GPK3(1), (0x2 << 4));
		s3c_gpio_setpull(EXYNOS5410_GPK3(1), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK3(1));
	}

	if (gpio_request_one(EXYNOS5410_GPK3(2), GPIOF_OUT_INIT_LOW,
						"UDRD3_0_VBUSCTRL_U2")) {
		pr_err("failed to request UDRD3_0_VBUSCTRL_U2\n");
	} else {
		s3c_gpio_setpull(EXYNOS5410_GPK3(2), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK3(2));
	}

	if (gpio_request_one(EXYNOS5410_GPK3(3), GPIOF_OUT_INIT_LOW,
						 "UDRD3_0_VBUSCTRL_U3")) {
		pr_err("failed to request UDRD3_0_VBUSCTRL_U3\n");
	} else {
		s3c_gpio_setpull(EXYNOS5410_GPK3(3), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK3(3));
	}

#if defined(CONFIG_USB_EXYNOS5_USB3_DRD_CH0)
	if (gpio_request_one(SMDK5410_ID0_GPIO, GPIOF_IN, "UDRD3_0_ID")) {
		pr_err("failed to request UDRD3_0_ID\n");
		smdk5410_drd_pdata.id_irq = -1;
	} else {
		s3c_gpio_cfgpin(SMDK5410_ID0_GPIO, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(SMDK5410_ID0_GPIO, S3C_GPIO_PULL_NONE);
		gpio_free(SMDK5410_ID0_GPIO);

		smdk5410_drd_pdata.id_irq = gpio_to_irq(SMDK5410_ID0_GPIO);
	}

	if (gpio_request_one(SMDK5410_VBUS0_GPIO, GPIOF_IN, "UDRD3_0_VBUS")) {
		pr_err("failed to request UDRD3_0_VBUS\n");
		smdk5410_drd_pdata.vbus_irq = -1;
	} else {
		s3c_gpio_cfgpin(SMDK5410_VBUS0_GPIO, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(SMDK5410_VBUS0_GPIO, S3C_GPIO_PULL_NONE);
		gpio_free(SMDK5410_VBUS0_GPIO);

		smdk5410_drd_pdata.vbus_irq = gpio_to_irq(SMDK5410_VBUS0_GPIO);
	}

	smdk5410_drd_pdata.quirks = 0;
#if !defined(CONFIG_USB_XHCI_EXYNOS)
	smdk5410_drd_pdata.quirks |= FORCE_RUN_PERIPHERAL;
#endif
#else
	smdk5410_drd_pdata.id_irq = -1;
	smdk5410_drd_pdata.vbus_irq = -1;
	smdk5410_drd_pdata.quirks = DUMMY_DRD;
#endif

	exynos5_usb3_drd0_set_platdata(&smdk5410_drd_pdata);
}

static void __init __maybe_unused smdk5410_drd1_init(void)
{
	/* Initialize DRD1 gpio */
	if (gpio_request(EXYNOS5410_GPK2(4), "UDRD3_1_OVERCUR_U2")) {
		pr_err("failed to request UDRD3_1_OVERCUR_U2\n");
	} else {
		s3c_gpio_cfgpin(EXYNOS5410_GPK2(4), (0x2 << 16));
		s3c_gpio_setpull(EXYNOS5410_GPK2(4), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK2(4));
	}

	if (gpio_request(EXYNOS5410_GPK2(5), "UDRD3_1_OVERCUR_U3")) {
		pr_err("failed to request UDRD3_1_OVERCUR_U3\n");
	} else {
		s3c_gpio_cfgpin(EXYNOS5410_GPK2(5), (0x2 << 20));
		s3c_gpio_setpull(EXYNOS5410_GPK2(5), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK2(5));
	}

	if (gpio_request_one(EXYNOS5410_GPK2(6), GPIOF_OUT_INIT_LOW,
				"UDRD3_1_VBUSCTRL_U2")) {
		pr_err("failed to request UDRD3_1_VBUSCTRL_U2\n");
	} else {
		s3c_gpio_setpull(EXYNOS5410_GPK2(6), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK2(6));
	}

	if (gpio_request_one(EXYNOS5410_GPK2(7), GPIOF_OUT_INIT_LOW,
				"UDRD3_1_VBUSCTRL_U3")) {
		pr_err("failed to request UDRD3_1_VBUSCTRL_U3\n");
	} else {
		s3c_gpio_setpull(EXYNOS5410_GPK2(7), S3C_GPIO_PULL_NONE);
		gpio_free(EXYNOS5410_GPK2(7));
	}

#if defined(CONFIG_USB_EXYNOS5_USB3_DRD_CH1)
	if (gpio_request_one(SMDK5410_ID1_GPIO, GPIOF_IN, "UDRD3_1_ID")) {
		pr_err("failed to request UDRD3_1_ID\n");
		smdk5410_drd_pdata.id_irq = -1;
	} else {
		s3c_gpio_cfgpin(SMDK5410_ID1_GPIO, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(SMDK5410_ID1_GPIO, S3C_GPIO_PULL_NONE);
		gpio_free(SMDK5410_ID1_GPIO);

		smdk5410_drd_pdata.id_irq = gpio_to_irq(SMDK5410_ID1_GPIO);
	}

	if (gpio_request_one(SMDK5410_VBUS1_GPIO, GPIOF_IN, "UDRD3_1_VBUS")) {
		pr_err("failed to request UDRD3_1_VBUS\n");
		smdk5410_drd_pdata.vbus_irq = -1;
	} else {
		s3c_gpio_cfgpin(SMDK5410_VBUS1_GPIO, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(SMDK5410_VBUS1_GPIO, S3C_GPIO_PULL_NONE);
		gpio_free(SMDK5410_VBUS1_GPIO);

		smdk5410_drd_pdata.vbus_irq = gpio_to_irq(SMDK5410_VBUS1_GPIO);
	}

	smdk5410_drd_pdata.quirks = 0;
#if !defined(CONFIG_USB_XHCI_EXYNOS)
	smdk5410_drd_pdata.quirks |= FORCE_RUN_PERIPHERAL;
#endif
#else
	smdk5410_drd_pdata.id_irq = -1;
	smdk5410_drd_pdata.vbus_irq = -1;
	smdk5410_drd_pdata.quirks = DUMMY_DRD;
#endif

	exynos5_usb3_drd1_set_platdata(&smdk5410_drd_pdata);
}

static struct s5p_usbswitch_platdata smdk5410_usbswitch_pdata __initdata;

static void __init smdk5410_usbswitch_init(void)
{
	struct s5p_usbswitch_platdata *pdata = &smdk5410_usbswitch_pdata;
	int err;

#if defined(CONFIG_USB_EHCI_S5P) || defined(CONFIG_USB_OHCI_EXYNOS)
		pdata->gpio_host_detect = EXYNOS5410_GPX1(6);
		err = gpio_request_one(pdata->gpio_host_detect, GPIOF_IN,
			"HOST_DETECT");
		if (err) {
			pr_err("failed to request host gpio\n");
			return;
		}

		s3c_gpio_cfgpin(pdata->gpio_host_detect, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(pdata->gpio_host_detect, S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_host_detect);

		pdata->gpio_host_vbus = EXYNOS5410_GPX2(6);
		err = gpio_request_one(pdata->gpio_host_vbus,
			GPIOF_OUT_INIT_LOW,
			"HOST_VBUS_CONTROL");
		if (err) {
			pr_err("failed to request host_vbus gpio\n");
			return;
		}

		s3c_gpio_setpull(pdata->gpio_host_vbus, S3C_GPIO_PULL_NONE);
		gpio_free(pdata->gpio_host_vbus);
#endif

	s5p_usbswitch_set_platdata(pdata);
}

static struct platform_device *smdk5410_usb_devices[] __initdata = {
	&s5p_device_ehci,
	&exynos4_device_ohci,
	&exynos5_device_usb3_drd0,
	&exynos5_device_usb3_drd1,
};

void __init exynos5_smdk5410_usb_init(void)
{
	smdk5410_ohci_init();
	smdk5410_ehci_init();

	if (soc_is_exynos5410() && samsung_rev() == 0)
		smdk5410_drd_pdata.quirks |= EXYNOS_PHY20_NO_SUSPEND;

	/*
	 * Shutdown DRD PHYs to reduce power consumption.
	 * Later, DRD driver will turn on only the PHY it needs.
	 */
	smdk5410_drd_phy_shutdown(&exynos5_device_usb3_drd0);
	smdk5410_drd_phy_shutdown(&exynos5_device_usb3_drd1);
	smdk5410_drd0_init();
	smdk5410_drd1_init();
#ifdef CONFIG_USB_EXYNOS_SWITCH
	smdk5410_usbswitch_init();
#endif
	platform_add_devices(smdk5410_usb_devices,
			ARRAY_SIZE(smdk5410_usb_devices));
}
