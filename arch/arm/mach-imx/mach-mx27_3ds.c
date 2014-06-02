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
#include <linux/spi/l4f00242t03.h>

#include <media/soc_camera.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "3ds_debugboard.h"
#include "common.h"
#include "devices-imx27.h"
#include "hardware.h"
#include "iomux-mx27.h"
#include "ulpi.h"

#define SD1_EN_GPIO		IMX_GPIO_NR(2, 25)
#define OTG_PHY_RESET_GPIO	IMX_GPIO_NR(2, 23)
#define SPI2_SS0		IMX_GPIO_NR(4, 21)
#define PMIC_INT		IMX_GPIO_NR(3, 14)
#define SPI1_SS0		IMX_GPIO_NR(4, 28)
#define SD1_CD			IMX_GPIO_NR(2, 26)
#define LCD_RESET		IMX_GPIO_NR(1, 3)
#define LCD_ENABLE		IMX_GPIO_NR(1, 31)
#define CSI_PWRDWN		IMX_GPIO_NR(4, 19)
#define CSI_RESET		IMX_GPIO_NR(3, 6)

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
	/* CSPI1 */
	PD31_PF_CSPI1_MOSI,
	PD30_PF_CSPI1_MISO,
	PD29_PF_CSPI1_SCLK,
	PD25_PF_CSPI1_RDY,
	SPI1_SS0 | GPIO_GPIO | GPIO_OUT,
	/* CSPI2 */
	PD22_PF_CSPI2_SCLK,
	PD23_PF_CSPI2_MISO,
	PD24_PF_CSPI2_MOSI,
	SPI2_SS0 | GPIO_GPIO | GPIO_OUT,
	/* I2C1 */
	PD17_PF_I2C_DATA,
	PD18_PF_I2C_CLK,
	/* PMIC INT */
	PMIC_INT | GPIO_GPIO | GPIO_IN,
	/* LCD */
	PA5_PF_LSCLK,
	PA6_PF_LD0,
	PA7_PF_LD1,
	PA8_PF_LD2,
	PA9_PF_LD3,
	PA10_PF_LD4,
	PA11_PF_LD5,
	PA12_PF_LD6,
	PA13_PF_LD7,
	PA14_PF_LD8,
	PA15_PF_LD9,
	PA16_PF_LD10,
	PA17_PF_LD11,
	PA18_PF_LD12,
	PA19_PF_LD13,
	PA20_PF_LD14,
	PA21_PF_LD15,
	PA22_PF_LD16,
	PA23_PF_LD17,
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA30_PF_CONTRAST,
	LCD_ENABLE | GPIO_GPIO | GPIO_OUT,
	LCD_RESET | GPIO_GPIO | GPIO_OUT,
	/* CSI */
	PB10_PF_CSI_D0,
	PB11_PF_CSI_D1,
	PB12_PF_CSI_D2,
	PB13_PF_CSI_D3,
	PB14_PF_CSI_D4,
	PB15_PF_CSI_MCLK,
	PB16_PF_CSI_PIXCLK,
	PB17_PF_CSI_D5,
	PB18_PF_CSI_D6,
	PB19_PF_CSI_D7,
	PB20_PF_CSI_VSYNC,
	PB21_PF_CSI_HSYNC,
	CSI_PWRDWN | GPIO_GPIO | GPIO_OUT,
	CSI_RESET | GPIO_GPIO | GPIO_OUT,
	/* SSI4 */
	PC16_PF_SSI4_FS,
	PC17_PF_SSI4_RXD,
	PC18_PF_SSI4_TXD,
	PC19_PF_SSI4_CLK,
};

static struct gpio mx27_3ds_camera_gpios[] = {
	{ CSI_PWRDWN, GPIOF_OUT_INIT_HIGH, "camera-power" },
	{ CSI_RESET, GPIOF_OUT_INIT_HIGH, "camera-reset" },
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
	return request_irq(gpio_to_irq(SD1_CD), detect_irq,
	IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "sdhc1-card-detect", data);
}

static void mx27_3ds_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(SD1_CD), data);
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

static int mx27_3ds_otg_init(struct platform_device *pdev)
{
	return mx27_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_DIFF_UNI);
}

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.init	= mx27_3ds_otg_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode       = FSL_USB2_PHY_ULPI,
};

static bool otg_mode_host __initdata;

static int __init mx27_3ds_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = true;
	else if (!strcmp(options, "device"))
		otg_mode_host = false;
	else
		pr_info("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 1;
}
__setup("otg_mode=", mx27_3ds_otg_mode);

/* Regulators */
static struct regulator_init_data gpo_init = {
	.constraints = {
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_consumer_supply vmmc1_consumers[] = {
	REGULATOR_SUPPLY("vcore", "spi0.0"),
	REGULATOR_SUPPLY("cmos_2v8", "soc-camera-pdrv.0"),
};

static struct regulator_init_data vmmc1_init = {
	.constraints = {
		.min_uV	= 2800000,
		.max_uV = 2800000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(vmmc1_consumers),
	.consumer_supplies = vmmc1_consumers,
};

static struct regulator_consumer_supply vgen_consumers[] = {
	REGULATOR_SUPPLY("vdd", "spi0.0"),
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

static struct regulator_consumer_supply vvib_consumers[] = {
	REGULATOR_SUPPLY("cmos_vcore", "soc-camera-pdrv.0"),
};

static struct regulator_init_data vvib_init = {
	.constraints = {
		.min_uV = 1300000,
		.max_uV = 1300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(vvib_consumers),
	.consumer_supplies = vvib_consumers,
};

static struct mc13xxx_regulator_init_data mx27_3ds_regulators[] = {
	{
		.id = MC13783_REG_VMMC1,
		.init_data = &vmmc1_init,
	}, {
		.id = MC13783_REG_VGEN,
		.init_data = &vgen_init,
	}, {
		.id = MC13783_REG_GPO1, /* Turn on 1.8V */
		.init_data = &gpo_init,
	}, {
		.id = MC13783_REG_GPO3, /* Turn on 3.3V */
		.init_data = &gpo_init,
	}, {
		.id = MC13783_REG_VVIB,  /* Power OV2640 */
		.init_data = &vvib_init,
	},
};

/* MC13783 */
static struct mc13xxx_codec_platform_data mx27_3ds_codec = {
	.dac_ssi_port = MC13783_SSI1_PORT,
	.adc_ssi_port = MC13783_SSI1_PORT,
};

static struct mc13xxx_platform_data mc13783_pdata = {
	.regulators = {
		.regulators = mx27_3ds_regulators,
		.num_regulators = ARRAY_SIZE(mx27_3ds_regulators),

	},
	.flags  = MC13XXX_USE_TOUCHSCREEN | MC13XXX_USE_RTC |
						MC13XXX_USE_CODEC,
	.codec = &mx27_3ds_codec,
};

static struct imx_ssi_platform_data mx27_3ds_ssi_pdata = {
	.flags = IMX_SSI_DMA | IMX_SSI_NET,
};

/* SPI */
static int spi1_chipselect[] = {SPI1_SS0};

static const struct spi_imx_master spi1_pdata __initconst = {
	.chipselect	= spi1_chipselect,
	.num_chipselect	= ARRAY_SIZE(spi1_chipselect),
};

static int spi2_chipselect[] = {SPI2_SS0};

static const struct spi_imx_master spi2_pdata __initconst = {
	.chipselect	= spi2_chipselect,
	.num_chipselect	= ARRAY_SIZE(spi2_chipselect),
};

static int mx27_3ds_camera_power(struct device *dev, int on)
{
	/* enable or disable the camera */
	pr_debug("%s: %s the camera\n", __func__, on ? "ENABLE" : "DISABLE");
	gpio_set_value(CSI_PWRDWN, on ? 0 : 1);

	if (!on)
		goto out;

	/* If enabled, give a reset impulse */
	gpio_set_value(CSI_RESET, 0);
	msleep(20);
	gpio_set_value(CSI_RESET, 1);
	msleep(100);

out:
	return 0;
}

static struct i2c_board_info mx27_3ds_i2c_camera = {
	I2C_BOARD_INFO("ov2640", 0x30),
};

static struct regulator_bulk_data mx27_3ds_camera_regs[] = {
	{ .supply = "cmos_vcore" },
	{ .supply = "cmos_2v8" },
};

static struct soc_camera_link iclink_ov2640 = {
	.bus_id		= 0,
	.board_info	= &mx27_3ds_i2c_camera,
	.i2c_adapter_id	= 0,
	.power		= mx27_3ds_camera_power,
	.regulators	= mx27_3ds_camera_regs,
	.num_regulators	= ARRAY_SIZE(mx27_3ds_camera_regs),
};

static struct platform_device mx27_3ds_ov2640 = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &iclink_ov2640,
	},
};

static struct imx_fb_videomode mx27_3ds_modes[] = {
	{	/* 480x640 @ 60 Hz */
		.mode = {
			.name		= "Epson-VGA",
			.refresh	= 60,
			.xres		= 480,
			.yres		= 640,
			.pixclock	= 41701,
			.left_margin	= 20,
			.right_margin	= 41,
			.upper_margin	= 10,
			.lower_margin	= 5,
			.hsync_len	= 20,
			.vsync_len	= 10,
			.sync		= FB_SYNC_OE_ACT_HIGH |
						FB_SYNC_CLK_INVERT,
			.vmode		= FB_VMODE_NONINTERLACED,
			.flag		= 0,
		},
		.bpp		= 16,
		.pcr		= 0xFAC08B82,
	},
};

static const struct imx_fb_platform_data mx27_3ds_fb_data __initconst = {
	.mode = mx27_3ds_modes,
	.num_modes = ARRAY_SIZE(mx27_3ds_modes),
	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020010,
};

/* LCD */
static struct l4f00242t03_pdata mx27_3ds_lcd_pdata = {
	.reset_gpio		= LCD_RESET,
	.data_enable_gpio	= LCD_ENABLE,
};

static struct spi_board_info mx27_3ds_spi_devs[] __initdata = {
	{
		.modalias	= "mc13783",
		.max_speed_hz	= 1000000,
		.bus_num	= 1,
		.chip_select	= 0, /* SS0 */
		.platform_data	= &mc13783_pdata,
		/* irq number is run-time assigned */
		.mode = SPI_CS_HIGH,
	}, {
		.modalias	= "l4f00242t03",
		.max_speed_hz	= 5000000,
		.bus_num	= 0,
		.chip_select	= 0, /* SS0 */
		.platform_data	= &mx27_3ds_lcd_pdata,
	},
};

static struct platform_device *devices[] __initdata = {
	&mx27_3ds_ov2640,
};

static const struct mx2_camera_platform_data mx27_3ds_cam_pdata __initconst = {
	.clk = 26000000,
};

static const struct imxi2c_platform_data mx27_3ds_i2c0_data __initconst = {
	.bitrate = 100000,
};

static void __init mx27pdk_init(void)
{
	int ret;
	imx27_soc_init();

	mxc_gpio_setup_multiple_pins(mx27pdk_pins, ARRAY_SIZE(mx27pdk_pins),
		"mx27pdk");
	mx27_3ds_sdhc1_enable_level_translator();
	imx27_add_imx_uart0(&uart_pdata);
	imx27_add_fec(NULL);
	imx27_add_imx_keypad(&mx27_3ds_keymap_data);
	imx27_add_mxc_mmc(0, &sdhc1_pdata);
	imx27_add_imx2_wdt();
	otg_phy_init();

	if (otg_mode_host) {
		otg_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
				ULPI_OTG_DRVVBUS_EXT);

		if (otg_pdata.otg)
			imx27_add_mxc_ehci_otg(&otg_pdata);
	}

	if (!otg_mode_host)
		imx27_add_fsl_usb2_udc(&otg_device_pdata);

	imx27_add_spi_imx1(&spi2_pdata);
	imx27_add_spi_imx0(&spi1_pdata);
	mx27_3ds_spi_devs[0].irq = gpio_to_irq(PMIC_INT);
	spi_register_board_info(mx27_3ds_spi_devs,
						ARRAY_SIZE(mx27_3ds_spi_devs));

	if (mxc_expio_init(MX27_CS5_BASE_ADDR, IMX_GPIO_NR(3, 28)))
		pr_warn("Init of the debugboard failed, all devices on the debugboard are unusable.\n");
	imx27_add_imx_i2c(0, &mx27_3ds_i2c0_data);
	platform_add_devices(devices, ARRAY_SIZE(devices));
	imx27_add_imx_fb(&mx27_3ds_fb_data);

	ret = gpio_request_array(mx27_3ds_camera_gpios,
				 ARRAY_SIZE(mx27_3ds_camera_gpios));
	if (ret) {
		pr_err("Failed to request camera gpios");
		iclink_ov2640.power = NULL;
	}

	imx27_add_mx2_camera(&mx27_3ds_cam_pdata);
	imx27_add_imx_ssi(0, &mx27_3ds_ssi_pdata);

	imx_add_platform_device("imx_mc13783", 0, NULL, 0, NULL, 0);
}

static void __init mx27pdk_timer_init(void)
{
	mx27_clocks_init(26000000);
}

MACHINE_START(MX27_3DS, "Freescale MX27PDK")
	/* maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io = mx27_map_io,
	.init_early = imx27_init_early,
	.init_irq = mx27_init_irq,
	.init_time	= mx27pdk_timer_init,
	.init_machine = mx27pdk_init,
	.restart	= mxc_restart,
MACHINE_END
