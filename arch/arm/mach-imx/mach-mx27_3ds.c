/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
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

/*
 * This machine is known as:
 *  - i.MX27 3-Stack Development System
 *  - i.MX27 Platform Development Kit (i.MX27 PDK)
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/delay.h>
#include <linux/mfd/mc13783.h>
#include <linux/spi/spi.h>
#include <linux/regulator/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx27.h>
#include <mach/ulpi.h>

#include "devices-imx27.h"

#define SD1_EN_GPIO (GPIO_PORTB + 25)
#define OTG_PHY_RESET_GPIO (GPIO_PORTB + 23)
#define SPI2_SS0 (GPIO_PORTD + 21)

static const int mx27pdk_pins[] __initconst = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* FEC */
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_RX_CLK,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN,
	/* SDHC1 */
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,
	SD1_EN_GPIO | GPIO_GPIO | GPIO_OUT,
	/* OTG */
	OTG_PHY_RESET_GPIO | GPIO_GPIO | GPIO_OUT,
	PC7_PF_USBOTG_DATA5,
	PC8_PF_USBOTG_DATA6,
	PC9_PF_USBOTG_DATA0,
	PC10_PF_USBOTG_DATA2,
	PC11_PF_USBOTG_DATA1,
	PC12_PF_USBOTG_DATA4,
	PC13_PF_USBOTG_DATA3,
	PE0_PF_USBOTG_NXT,
	PE1_PF_USBOTG_STP,
	PE2_PF_USBOTG_DIR,
	PE24_PF_USBOTG_CLK,
	PE25_PF_USBOTG_DATA7,
	/* CSPI2 */
	PD22_PF_CSPI2_SCLK,
	PD23_PF_CSPI2_MISO,
	PD24_PF_CSPI2_MOSI,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/*
 * Matrix keyboard
 */

static const uint32_t mx27_3ds_keymap[] = {
	KEY(0, 0, KEY_UP),
	KEY(0, 1, KEY_DOWN),
	KEY(1, 0, KEY_RIGHT),
	KEY(1, 1, KEY_LEFT),
	KEY(1, 2, KEY_ENTER),
	KEY(2, 0, KEY_F6),
	KEY(2, 1, KEY_F8),
	KEY(2, 2, KEY_F9),
	KEY(2, 3, KEY_F10),
};

static const struct matrix_keymap_data mx27_3ds_keymap_data __initconst = {
	.keymap		= mx27_3ds_keymap,
	.keymap_size	= ARRAY_SIZE(mx27_3ds_keymap),
};

static int mx27_3ds_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
				void *data)
{
	return request_irq(IRQ_GPIOB(26), detect_irq, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING, "sdhc1-card-detect", data);
}

static void mx27_3ds_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(IRQ_GPIOB(26), data);
}

static const struct imxmmc_platform_data sdhc1_pdata __initconst = {
	.init = mx27_3ds_sdhc1_init,
	.exit = mx27_3ds_sdhc1_exit,
};

static void mx27_3ds_sdhc1_enable_level_translator(void)
{
	/* Turn on TXB0108 OE pin */
	gpio_request(SD1_EN_GPIO, "sd1_enable");
	gpio_direction_output(SD1_EN_GPIO, 1);
}


static int otg_phy_init(void)
{
	gpio_request(OTG_PHY_RESET_GPIO, "usb-otg-reset");
	gpio_direction_output(OTG_PHY_RESET_GPIO, 0);
	mdelay(1);
	gpio_set_value(OTG_PHY_RESET_GPIO, 1);
	return 0;
}

#if defined(CONFIG_USB_ULPI)

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.portsc	= MXC_EHCI_MODE_ULPI,
	.flags	= MXC_EHCI_INTERFACE_DIFF_UNI,
};
#endif

static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_ULPI,
};

static int otg_mode_host;

static int __init mx27_3ds_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = 1;
	else if (!strcmp(options, "device"))
		otg_mode_host = 0;
	else
		pr_info("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 0;
}
__setup("otg_mode=", mx27_3ds_otg_mode);

/* Regulators */
static struct regulator_consumer_supply vmmc1_consumers[] = {
	REGULATOR_SUPPLY("lcd_2v8", NULL),
};

static struct regulator_init_data vmmc1_init = {
	.constraints = {
		.min_uV	= 2800000,
		.max_uV = 2800000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(vmmc1_consumers),
	.consumer_supplies = vmmc1_consumers,
};

static struct regulator_consumer_supply vgen_consumers[] = {
	REGULATOR_SUPPLY("vdd_lcdio", NULL),
};

static struct regulator_init_data vgen_init = {
	.constraints = {
		.min_uV	= 1800000,
		.max_uV = 1800000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(vgen_consumers),
	.consumer_supplies = vgen_consumers,
};

static struct mc13783_regulator_init_data mx27_3ds_regulators[] = {
	{
		.id = MC13783_REGU_VMMC1,
		.init_data = &vmmc1_init,
	}, {
		.id = MC13783_REGU_VGEN,
		.init_data = &vgen_init,
	},
};

/* MC13783 */
static struct mc13783_platform_data mc13783_pdata __initdata = {
	.regulators = mx27_3ds_regulators,
	.num_regulators = ARRAY_SIZE(mx27_3ds_regulators),
	.flags  = MC13783_USE_REGULATOR,
};

/* SPI */
static int spi2_internal_chipselect[] = {SPI2_SS0};

static const struct spi_imx_master spi2_pdata __initconst = {
	.chipselect	= spi2_internal_chipselect,
	.num_chipselect	= ARRAY_SIZE(spi2_internal_chipselect),
};

static struct spi_board_info mx27_3ds_spi_devs[] __initdata = {
	{
		.modalias	= "mc13783",
		.max_speed_hz	= 1000000,
		.bus_num	= 1,
		.chip_select	= 0, /* SS0 */
		.platform_data	= &mc13783_pdata,
		.irq = IRQ_GPIOC(14),
		.mode = SPI_CS_HIGH,
	},
};


static void __init mx27pdk_init(void)
{
	mxc_gpio_setup_multiple_pins(mx27pdk_pins, ARRAY_SIZE(mx27pdk_pins),
		"mx27pdk");
	mx27_3ds_sdhc1_enable_level_translator();
	imx27_add_imx_uart0(&uart_pdata);
	imx27_add_fec(NULL);
	imx27_add_imx_keypad(&mx27_3ds_keymap_data);
	imx27_add_mxc_mmc(0, &sdhc1_pdata);
	imx27_add_imx2_wdt(NULL);
	otg_phy_init();
#if defined(CONFIG_USB_ULPI)
	if (otg_mode_host) {
		otg_pdata.otg = otg_ulpi_create(&mxc_ulpi_access_ops,
				ULPI_OTG_DRVVBUS | ULPI_OTG_DRVVBUS_EXT);

		imx27_add_mxc_ehci_otg(&otg_pdata);
	}
#endif
	if (!otg_mode_host)
		imx27_add_fsl_usb2_udc(&otg_device_pdata);

	imx27_add_spi_imx1(&spi2_pdata);
	spi_register_board_info(mx27_3ds_spi_devs,
						ARRAY_SIZE(mx27_3ds_spi_devs));
}

static void __init mx27pdk_timer_init(void)
{
	mx27_clocks_init(26000000);
}

static struct sys_timer mx27pdk_timer = {
	.init	= mx27pdk_timer_init,
};

MACHINE_START(MX27_3DS, "Freescale MX27PDK")
	/* maintainer: Freescale Semiconductor, Inc. */
	.boot_params    = MX27_PHYS_OFFSET + 0x100,
	.map_io         = mx27_map_io,
	.init_irq       = mx27_init_irq,
	.init_machine   = mx27pdk_init,
	.timer          = &mx27pdk_timer,
MACHINE_END
