/*
 *  Copyright (C) 2008 Valentin Longchamp, EPFL Mobots group
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/delay.h>
#include <linux/fsl_devices.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/memory.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/board-mx31moboard.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/i2c.h>
#include <mach/mmc.h>
#include <mach/mx31.h>

#include "devices.h"

static unsigned int moboard_pins[] = {
	/* UART0 */
	MX31_PIN_CTS1__CTS1, MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1, MX31_PIN_RXD1__RXD1,
	/* UART4 */
	MX31_PIN_PC_RST__CTS5, MX31_PIN_PC_VS2__RTS5,
	MX31_PIN_PC_BVD2__TXD5, MX31_PIN_PC_BVD1__RXD5,
	/* I2C0 */
	MX31_PIN_I2C_DAT__I2C1_SDA, MX31_PIN_I2C_CLK__I2C1_SCL,
	/* I2C1 */
	MX31_PIN_DCD_DTE1__I2C2_SDA, MX31_PIN_RI_DTE1__I2C2_SCL,
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3, MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1, MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK, MX31_PIN_SD1_CMD__SD1_CMD,
	MX31_PIN_ATA_CS0__GPIO3_26, MX31_PIN_ATA_CS1__GPIO3_27,
	/* USB reset */
	MX31_PIN_GPIO1_0__GPIO1_0,
	/* USB OTG */
	MX31_PIN_USBOTG_DATA0__USBOTG_DATA0,
	MX31_PIN_USBOTG_DATA1__USBOTG_DATA1,
	MX31_PIN_USBOTG_DATA2__USBOTG_DATA2,
	MX31_PIN_USBOTG_DATA3__USBOTG_DATA3,
	MX31_PIN_USBOTG_DATA4__USBOTG_DATA4,
	MX31_PIN_USBOTG_DATA5__USBOTG_DATA5,
	MX31_PIN_USBOTG_DATA6__USBOTG_DATA6,
	MX31_PIN_USBOTG_DATA7__USBOTG_DATA7,
	MX31_PIN_USBOTG_CLK__USBOTG_CLK, MX31_PIN_USBOTG_DIR__USBOTG_DIR,
	MX31_PIN_USBOTG_NXT__USBOTG_NXT, MX31_PIN_USBOTG_STP__USBOTG_STP,
	MX31_PIN_USB_OC__GPIO1_30,
	/* LEDs */
	MX31_PIN_SVEN0__GPIO2_0, MX31_PIN_STX0__GPIO2_1,
	MX31_PIN_SRX0__GPIO2_2, MX31_PIN_SIMPD0__GPIO2_3,
	/* SEL */
	MX31_PIN_DTR_DCE1__GPIO2_8, MX31_PIN_DSR_DCE1__GPIO2_9,
	MX31_PIN_RI_DCE1__GPIO2_10, MX31_PIN_DCD_DCE1__GPIO2_11,
};

static struct physmap_flash_data mx31moboard_flash_data = {
	.width  	= 2,
};

static struct resource mx31moboard_flash_resource = {
	.start	= 0xa0000000,
	.end	= 0xa1ffffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device mx31moboard_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &mx31moboard_flash_data,
	},
	.resource = &mx31moboard_flash_resource,
	.num_resources = 1,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct imxi2c_platform_data moboard_i2c0_pdata = {
	.bitrate = 400000,
};

static struct imxi2c_platform_data moboard_i2c1_pdata = {
	.bitrate = 100000,
};

#define SDHC1_CD IOMUX_TO_GPIO(MX31_PIN_ATA_CS0)
#define SDHC1_WP IOMUX_TO_GPIO(MX31_PIN_ATA_CS1)

static int moboard_sdhc1_get_ro(struct device *dev)
{
	return !gpio_get_value(SDHC1_WP);
}

static int moboard_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
		void *data)
{
	int ret;

	ret = gpio_request(SDHC1_CD, "sdhc-detect");
	if (ret)
		return ret;

	gpio_direction_input(SDHC1_CD);

	ret = gpio_request(SDHC1_WP, "sdhc-wp");
	if (ret)
		goto err_gpio_free;
	gpio_direction_input(SDHC1_WP);

	ret = request_irq(gpio_to_irq(SDHC1_CD), detect_irq,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"sdhc1-card-detect", data);
	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
	gpio_free(SDHC1_WP);
err_gpio_free:
	gpio_free(SDHC1_CD);

	return ret;
}

static void moboard_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(SDHC1_CD), data);
	gpio_free(SDHC1_WP);
	gpio_free(SDHC1_CD);
}

static struct imxmmc_platform_data sdhc1_pdata = {
	.get_ro	= moboard_sdhc1_get_ro,
	.init	= moboard_sdhc1_init,
	.exit	= moboard_sdhc1_exit,
};

/*
 * this pin is dedicated for all mx31moboard systems, so we do it here
 */
#define USB_RESET_B	IOMUX_TO_GPIO(MX31_PIN_GPIO1_0)

static void usb_xcvr_reset(void)
{
	gpio_request(USB_RESET_B, "usb-reset");
	gpio_direction_output(USB_RESET_B, 0);
	mdelay(1);
	gpio_set_value(USB_RESET_B, 1);
}

#define USB_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
			PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)

#define OTG_EN_B IOMUX_TO_GPIO(MX31_PIN_USB_OC)

static void moboard_usbotg_init(void)
{
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP, USB_PAD_CFG);

	gpio_request(OTG_EN_B, "usb-udc-en");
	gpio_direction_output(OTG_EN_B, 0);
}

static struct fsl_usb2_platform_data usb_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_ULPI,
};

static struct gpio_led mx31moboard_leds[] = {
	{
		.name 	= "coreboard-led-0:red:running",
		.default_trigger = "heartbeat",
		.gpio 	= IOMUX_TO_GPIO(MX31_PIN_SVEN0),
	}, {
		.name	= "coreboard-led-1:red",
		.gpio	= IOMUX_TO_GPIO(MX31_PIN_STX0),
	}, {
		.name	= "coreboard-led-2:red",
		.gpio	= IOMUX_TO_GPIO(MX31_PIN_SRX0),
	}, {
		.name	= "coreboard-led-3:red",
		.gpio	= IOMUX_TO_GPIO(MX31_PIN_SIMPD0),
	},
};

static struct gpio_led_platform_data mx31moboard_led_pdata = {
	.num_leds 	= ARRAY_SIZE(mx31moboard_leds),
	.leds		= mx31moboard_leds,
};

static struct platform_device mx31moboard_leds_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &mx31moboard_led_pdata,
	},
};

#define SEL0 IOMUX_TO_GPIO(MX31_PIN_DTR_DCE1)
#define SEL1 IOMUX_TO_GPIO(MX31_PIN_DSR_DCE1)
#define SEL2 IOMUX_TO_GPIO(MX31_PIN_RI_DCE1)
#define SEL3 IOMUX_TO_GPIO(MX31_PIN_DCD_DCE1)

static void mx31moboard_init_sel_gpios(void)
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

static struct platform_device *devices[] __initdata = {
	&mx31moboard_flash,
	&mx31moboard_leds_device,
};

static int mx31moboard_baseboard;
core_param(mx31moboard_baseboard, mx31moboard_baseboard, int, 0444);

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_iomux_setup_multiple_pins(moboard_pins, ARRAY_SIZE(moboard_pins),
		"moboard");

	platform_add_devices(devices, ARRAY_SIZE(devices));

	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_uart_device4, &uart_pdata);

	mx31moboard_init_sel_gpios();

	mxc_register_device(&mxc_i2c_device0, &moboard_i2c0_pdata);
	mxc_register_device(&mxc_i2c_device1, &moboard_i2c1_pdata);

	mxc_register_device(&mxcsdhc_device0, &sdhc1_pdata);

	usb_xcvr_reset();

	moboard_usbotg_init();
	mxc_register_device(&mxc_otg_udc_device, &usb_pdata);

	switch (mx31moboard_baseboard) {
	case MX31NOBOARD:
		break;
	case MX31DEVBOARD:
		mx31moboard_devboard_init();
		break;
	case MX31MARXBOT:
		mx31moboard_marxbot_init();
		break;
	default:
		printk(KERN_ERR "Illegal mx31moboard_baseboard type %d\n",
			mx31moboard_baseboard);
	}
}

static void __init mx31moboard_timer_init(void)
{
	mx31_clocks_init(26000000);
}

struct sys_timer mx31moboard_timer = {
	.init	= mx31moboard_timer_init,
};

MACHINE_START(MX31MOBOARD, "EPFL Mobots mx31moboard")
	/* Maintainer: Valentin Longchamp, EPFL Mobots group */
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx31_map_io,
	.init_irq       = mx31_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx31moboard_timer,
MACHINE_END

