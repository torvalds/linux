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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fsl_devices.h>

#include <linux/usb/otg.h>

#include <mach/common.h>
#include <mach/iomux-mx3.h>
#include <mach/hardware.h>
#include <mach/mmc.h>
#include <mach/mxc_ehci.h>
#include <mach/ulpi.h>

#include "devices-imx31.h"
#include "devices.h"

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

static struct imxmmc_platform_data sdhc2_pdata = {
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

	return 0;
}

#define USBH1_VBUSEN_B	IOMUX_TO_GPIO(MX31_PIN_NFRE_B)
#define USBH1_MODE	IOMUX_TO_GPIO(MX31_PIN_NFALE)

static int devboard_isp1105_init(struct otg_transceiver *otg)
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


static int devboard_isp1105_set_vbus(struct otg_transceiver *otg, bool on)
{
	if (on)
		gpio_set_value(USBH1_VBUSEN_B, 0);
	else
		gpio_set_value(USBH1_VBUSEN_B, 1);

	return 0;
}

static struct mxc_usbh_platform_data usbh1_pdata = {
	.init	= devboard_usbh1_hw_init,
	.portsc	= MXC_EHCI_MODE_UTMI | MXC_EHCI_SERIAL,
	.flags	= MXC_EHCI_POWER_PINS_ENABLED | MXC_EHCI_INTERFACE_SINGLE_UNI,
};

static int __init devboard_usbh1_init(void)
{
	struct otg_transceiver *otg;

	otg = kzalloc(sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	otg->label	= "ISP1105";
	otg->init	= devboard_isp1105_init;
	otg->set_vbus	= devboard_isp1105_set_vbus;

	usbh1_pdata.otg = otg;

	return mxc_register_device(&mxc_usbh1, &usbh1_pdata);
}


static struct fsl_usb2_platform_data usb_pdata = {
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

	mxc_register_device(&mxcsdhc_device1, &sdhc2_pdata);

	devboard_init_sel_gpios();

	mxc_register_device(&mxc_otg_udc_device, &usb_pdata);

	devboard_usbh1_init();
}
