// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Valentin Longchamp, EPFL Mobots group
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>

#include "board-mx31moboard.h"
#include "common.h"
#include "devices-imx31.h"
#include "ehci.h"
#include "hardware.h"
#include "iomux-mx3.h"
#include "ulpi.h"

static unsigned int smartbot_pins[] = {
	/* UART1 */
	MX31_PIN_CTS2__CTS2, MX31_PIN_RTS2__RTS2,
	MX31_PIN_TXD2__TXD2, MX31_PIN_RXD2__RXD2,
	/* ENABLES */
	MX31_PIN_DTR_DCE1__GPIO2_8, MX31_PIN_DSR_DCE1__GPIO2_9,
	MX31_PIN_RI_DCE1__GPIO2_10, MX31_PIN_DCD_DCE1__GPIO2_11,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct fsl_usb2_platform_data usb_pdata __initconst = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_ULPI,
};

#if defined(CONFIG_USB_ULPI)

static int smartbot_otg_init(struct platform_device *pdev)
{
	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED);
}

static struct mxc_usbh_platform_data otg_host_pdata __initdata = {
	.init	= smartbot_otg_init,
	.portsc = MXC_EHCI_MODE_ULPI | MXC_EHCI_UTMI_8BIT,
};

static int __init smartbot_otg_host_init(void)
{
	struct platform_device *pdev;

	otg_host_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
		ULPI_OTG_DRVVBUS_EXT);
	if (!otg_host_pdata.otg)
		return -ENODEV;

	pdev = imx31_add_mxc_ehci_otg(&otg_host_pdata);

	return PTR_ERR_OR_ZERO(pdev);
}
#else
static inline int smartbot_otg_host_init(void) { return 0; }
#endif

#define POWER_EN IOMUX_TO_GPIO(MX31_PIN_DTR_DCE1)
#define DSPIC_RST_B IOMUX_TO_GPIO(MX31_PIN_DSR_DCE1)
#define TRSLAT_RST_B IOMUX_TO_GPIO(MX31_PIN_RI_DCE1)
#define TRSLAT_SRC_CHOICE IOMUX_TO_GPIO(MX31_PIN_DCD_DCE1)

static void smartbot_resets_init(void)
{
	if (!gpio_request(POWER_EN, "power-enable")) {
		gpio_direction_output(POWER_EN, 0);
		gpio_export(POWER_EN, false);
	}

	if (!gpio_request(DSPIC_RST_B, "dspic-rst")) {
		gpio_direction_output(DSPIC_RST_B, 0);
		gpio_export(DSPIC_RST_B, false);
	}

	if (!gpio_request(TRSLAT_RST_B, "translator-rst")) {
		gpio_direction_output(TRSLAT_RST_B, 0);
		gpio_export(TRSLAT_RST_B, false);
	}

	if (!gpio_request(TRSLAT_SRC_CHOICE, "translator-src-choice")) {
		gpio_direction_output(TRSLAT_SRC_CHOICE, 0);
		gpio_export(TRSLAT_SRC_CHOICE, false);
	}
}
/*
 * system init for baseboard usage. Will be called by mx31moboard init.
 */
void __init mx31moboard_smartbot_init(int board)
{
	printk(KERN_INFO "Initializing mx31smartbot peripherals\n");

	mxc_iomux_setup_multiple_pins(smartbot_pins, ARRAY_SIZE(smartbot_pins),
		"smartbot");

	imx31_add_imx_uart1(&uart_pdata);

	switch (board) {
	case MX31SMARTBOT:
		imx31_add_fsl_usb2_udc(&usb_pdata);
		break;
	case MX31EYEBOT:
		smartbot_otg_host_init();
		break;
	default:
		printk(KERN_WARNING "Unknown board %d, USB OTG not initialized",
			board);
	}

	smartbot_resets_init();
}
