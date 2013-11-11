/*
 * Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/usb/otg.h>

#include "common.h"
#include "devices-imx31.h"
#include "hardware.h"
#include "iomux-mx3.h"
#include "ulpi.h"

static unsigned int devboard_pins[] = {
	/* UART1 */
	MX31_PIN_CTS2__CTS2, MX31_PIN_RTS2__RTS2,
	MX31_PIN_TXD2__TXD2, MX31_PIN_RXD2__RXD2,
	/* SDHC2 */
	MX31_PIN_PC_PWRON__SD2_DATA3, MX31_PIN_PC_VS1__SD2_DATA2,
	MX31_PIN_PC_READY__SD2_DATA1, MX31_PIN_PC_WAIT_B__SD2_DATA0,
	MX31_PIN_PC_CD2_B__SD2_CLK, MX31_PIN_PC_CD1_B__SD2_CMD,
	MX31_PIN_ATA_DIOR__GPIO3_28, MX31_PIN_ATA_DIOW__GPIO3_29,
	/* USB H1 */
	MX31_PIN_CSPI1_MISO__USBH1_RXDP, MX31_PIN_CSPI1_MOSI__USBH1_RXDM,
	MX31_PIN_CSPI1_SS0__USBH1_TXDM, MX31_PIN_CSPI1_SS1__USBH1_TXDP,
	MX31_PIN_CSPI1_SS2__USBH1_RCV, MX31_PIN_CSPI1_SCLK__USBH1_OEB,
	MX31_PIN_CSPI1_SPI_RDY__USBH1_FS, MX31_PIN_SFS6__USBH1_SUSPEND,
	MX31_PIN_NFRE_B__GPIO1_11, MX31_PIN_NFALE__GPIO1_12,
	/* SEL */
	MX31_PIN_DTR_DCE1__GPIO2_8, MX31_PIN_DSR_DCE1__GPIO2_9,
	MX31_PIN_RI_DCE1__GPIO2_10, MX31_PIN_DCD_DCE1__GPIO2_11,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

#define SDHC2_CD IOMUX_TO_GPIO(MX31_PIN_ATA_DIOR)
#define SDHC2_WP IOMUX_TO_GPIO(MX31_PIN_ATA_DIOW)

static int devboard_sdhc2_get_ro(struct device *dev)
{
	return !gpio_get_value(SDHC2_WP);
}

static int devboard_sdhc2_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = gpio_request(SDHC2_CD, "sdhc-detect");
	if (ret)
		return ret;

	gpio_direction_input(SDHC2_CD);

	ret = gpio_request(SDHC2_WP, "sdhc-wp");
	if (ret)
		goto err_gpio_free;
	gpio_direction_input(SDHC2_WP);

	ret = request_irq(gpio_to_irq(SDHC2_CD), detect_irq,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"sdhc2-card-detect", data);
	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
	gpio_free(SDHC2_WP);
err_gpio_free:
	gpio_free(SDHC2_CD);

	return ret;
}

static void devboard_sdhc2_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(SDHC2_CD), data);
	gpio_free(SDHC2_WP);
	gpio_free(SDHC2_CD);
}

static const struct imxmmc_platform_data sdhc2_pdata __initconst = {
	.get_ro	= devboard_sdhc2_get_ro,
	.init	= devboard_sdhc2_init,
	.exit	= devboard_sdhc2_exit,
};

#define SEL0 IOMUX_TO_GPIO(MX31_PIN_DTR_DCE1)
#define SEL1 IOMUX_TO_GPIO(MX31_PIN_DSR_DCE1)
#define SEL2 IOMUX_TO_GPIO(MX31_PIN_RI_DCE1)
#define SEL3 IOMUX_TO_GPIO(MX31_PIN_DCD_DCE1)

static void devboard_init_sel_gpios(void)
{
	if (!gpio_request(SEL0, "sel0")) {
		gpio_direction_input(SEL0);
		gpio_export(SEL0, true);
	}

	if (!gpio_request(SEL1, "sel1")) {
		gpio_direction_input(SEL1);
		gpio_export(SEL1, true);
	}

	if (!gpio_request(SEL2, "sel2")) {
		gpio_direction_input(SEL2);
		gpio_export(SEL2, true);
	}

	if (!gpio_request(SEL3, "sel3")) {
		gpio_direction_input(SEL3);
		gpio_export(SEL3, true);
	}
}
#define USB_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
			PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)

static int devboard_usbh1_hw_init(struct platform_device *pdev)
{
	mxc_iomux_set_gpr(MUX_PGP_USB_SUSPEND, true);

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MISO, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_CSPI1_MOSI, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS0, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS1, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS2, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SCLK, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SPI_RDY, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_SFS6, USB_PAD_CFG);

	mdelay(10);

	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED |
			MXC_EHCI_INTERFACE_SINGLE_UNI);
}

#define USBH1_VBUSEN_B	IOMUX_TO_GPIO(MX31_PIN_NFRE_B)
#define USBH1_MODE	IOMUX_TO_GPIO(MX31_PIN_NFALE)

static int devboard_isp1105_init(struct usb_phy *otg)
{
	int ret = gpio_request(USBH1_MODE, "usbh1-mode");
	if (ret)
		return ret;
	/* single ended */
	gpio_direction_output(USBH1_MODE, 0);

	ret = gpio_request(USBH1_VBUSEN_B, "usbh1-vbusen");
	if (ret) {
		gpio_free(USBH1_MODE);
		return ret;
	}
	gpio_direction_output(USBH1_VBUSEN_B, 1);

	return 0;
}


static int devboard_isp1105_set_vbus(struct usb_otg *otg, bool on)
{
	if (on)
		gpio_set_value(USBH1_VBUSEN_B, 0);
	else
		gpio_set_value(USBH1_VBUSEN_B, 1);

	return 0;
}

static struct mxc_usbh_platform_data usbh1_pdata __initdata = {
	.init	= devboard_usbh1_hw_init,
	.portsc	= MXC_EHCI_MODE_UTMI | MXC_EHCI_SERIAL,
};

static int __init devboard_usbh1_init(void)
{
	struct usb_phy *phy;
	struct platform_device *pdev;

	phy = kzalloc(sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->otg = kzalloc(sizeof(struct usb_otg), GFP_KERNEL);
	if (!phy->otg) {
		kfree(phy);
		return -ENOMEM;
	}

	phy->label	= "ISP1105";
	phy->init	= devboard_isp1105_init;
	phy->otg->set_vbus	= devboard_isp1105_set_vbus;

	usbh1_pdata.otg = phy;

	pdev = imx31_add_mxc_ehci_hs(1, &usbh1_pdata);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
}


static const struct fsl_usb2_platform_data usb_pdata __initconst = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_ULPI,
};

/*
 * system init for baseboard usage. Will be called by mx31moboard init.
 */
void __init mx31moboard_devboard_init(void)
{
	printk(KERN_INFO "Initializing mx31devboard peripherals\n");

	mxc_iomux_setup_multiple_pins(devboard_pins, ARRAY_SIZE(devboard_pins),
		"devboard");

	imx31_add_imx_uart1(&uart_pdata);

	imx31_add_mxc_mmc(1, &sdhc2_pdata);

	devboard_init_sel_gpios();

	imx31_add_fsl_usb2_udc(&usb_pdata);

	devboard_usbh1_init();
}
