/*
 *  Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
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

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/mfd/mc13783.h>
#include <linux/spi/spi.h>
#include <linux/spi/l4f00242t03.h>
#include <linux/regulator/machine.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/memblock.h>

#include <media/soc_camera.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <asm/memblock.h>
#include <mach/common.h>
#include <mach/iomux-mx3.h>
#include <mach/3ds_debugboard.h>
#include <mach/ulpi.h>

#include "devices-imx31.h"

static int mx31_3ds_pins[] = {
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	IOMUX_MODE(MX31_PIN_GPIO1_1, IOMUX_CONFIG_GPIO),
	/*SPI0*/
	IOMUX_MODE(MX31_PIN_DSR_DCE1, IOMUX_CONFIG_ALT1),
	IOMUX_MODE(MX31_PIN_RI_DCE1, IOMUX_CONFIG_ALT1),
	/* SPI 1 */
	MX31_PIN_CSPI2_SCLK__SCLK,
	MX31_PIN_CSPI2_MOSI__MOSI,
	MX31_PIN_CSPI2_MISO__MISO,
	MX31_PIN_CSPI2_SPI_RDY__SPI_RDY,
	MX31_PIN_CSPI2_SS0__SS0,
	MX31_PIN_CSPI2_SS2__SS2, /*CS for MC13783 */
	/* MC13783 IRQ */
	IOMUX_MODE(MX31_PIN_GPIO1_3, IOMUX_CONFIG_GPIO),
	/* USB OTG reset */
	IOMUX_MODE(MX31_PIN_USB_PWR, IOMUX_CONFIG_GPIO),
	/* USB OTG */
	MX31_PIN_USBOTG_DATA0__USBOTG_DATA0,
	MX31_PIN_USBOTG_DATA1__USBOTG_DATA1,
	MX31_PIN_USBOTG_DATA2__USBOTG_DATA2,
	MX31_PIN_USBOTG_DATA3__USBOTG_DATA3,
	MX31_PIN_USBOTG_DATA4__USBOTG_DATA4,
	MX31_PIN_USBOTG_DATA5__USBOTG_DATA5,
	MX31_PIN_USBOTG_DATA6__USBOTG_DATA6,
	MX31_PIN_USBOTG_DATA7__USBOTG_DATA7,
	MX31_PIN_USBOTG_CLK__USBOTG_CLK,
	MX31_PIN_USBOTG_DIR__USBOTG_DIR,
	MX31_PIN_USBOTG_NXT__USBOTG_NXT,
	MX31_PIN_USBOTG_STP__USBOTG_STP,
	/*Keyboard*/
	MX31_PIN_KEY_ROW0_KEY_ROW0,
	MX31_PIN_KEY_ROW1_KEY_ROW1,
	MX31_PIN_KEY_ROW2_KEY_ROW2,
	MX31_PIN_KEY_COL0_KEY_COL0,
	MX31_PIN_KEY_COL1_KEY_COL1,
	MX31_PIN_KEY_COL2_KEY_COL2,
	MX31_PIN_KEY_COL3_KEY_COL3,
	/* USB Host 2 */
	IOMUX_MODE(MX31_PIN_USBH2_CLK, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DIR, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_NXT, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_STP, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DATA0, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_USBH2_DATA1, IOMUX_CONFIG_FUNC),
	IOMUX_MODE(MX31_PIN_PC_VS2, IOMUX_CONFIG_ALT1),
	IOMUX_MODE(MX31_PIN_PC_BVD1, IOMUX_CONFIG_ALT1),
	IOMUX_MODE(MX31_PIN_PC_BVD2, IOMUX_CONFIG_ALT1),
	IOMUX_MODE(MX31_PIN_PC_RST, IOMUX_CONFIG_ALT1),
	IOMUX_MODE(MX31_PIN_IOIS16, IOMUX_CONFIG_ALT1),
	IOMUX_MODE(MX31_PIN_PC_RW_B, IOMUX_CONFIG_ALT1),
	/* USB Host2 reset */
	IOMUX_MODE(MX31_PIN_USB_BYP, IOMUX_CONFIG_GPIO),
	/* I2C1 */
	MX31_PIN_I2C_CLK__I2C1_SCL,
	MX31_PIN_I2C_DAT__I2C1_SDA,
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
	MX31_PIN_GPIO3_1__GPIO3_1, /* Card detect */
	MX31_PIN_GPIO3_0__GPIO3_0, /* OE */
	/* Framebuffer */
	MX31_PIN_LD0__LD0,
	MX31_PIN_LD1__LD1,
	MX31_PIN_LD2__LD2,
	MX31_PIN_LD3__LD3,
	MX31_PIN_LD4__LD4,
	MX31_PIN_LD5__LD5,
	MX31_PIN_LD6__LD6,
	MX31_PIN_LD7__LD7,
	MX31_PIN_LD8__LD8,
	MX31_PIN_LD9__LD9,
	MX31_PIN_LD10__LD10,
	MX31_PIN_LD11__LD11,
	MX31_PIN_LD12__LD12,
	MX31_PIN_LD13__LD13,
	MX31_PIN_LD14__LD14,
	MX31_PIN_LD15__LD15,
	MX31_PIN_LD16__LD16,
	MX31_PIN_LD17__LD17,
	MX31_PIN_VSYNC3__VSYNC3,
	MX31_PIN_HSYNC__HSYNC,
	MX31_PIN_FPSHIFT__FPSHIFT,
	MX31_PIN_CONTRAST__CONTRAST,
	/* CSI */
	MX31_PIN_CSI_D6__CSI_D6,
	MX31_PIN_CSI_D7__CSI_D7,
	MX31_PIN_CSI_D8__CSI_D8,
	MX31_PIN_CSI_D9__CSI_D9,
	MX31_PIN_CSI_D10__CSI_D10,
	MX31_PIN_CSI_D11__CSI_D11,
	MX31_PIN_CSI_D12__CSI_D12,
	MX31_PIN_CSI_D13__CSI_D13,
	MX31_PIN_CSI_D14__CSI_D14,
	MX31_PIN_CSI_D15__CSI_D15,
	MX31_PIN_CSI_HSYNC__CSI_HSYNC,
	MX31_PIN_CSI_MCLK__CSI_MCLK,
	MX31_PIN_CSI_PIXCLK__CSI_PIXCLK,
	MX31_PIN_CSI_VSYNC__CSI_VSYNC,
	MX31_PIN_CSI_D5__GPIO3_5, /* CMOS PWDN */
	IOMUX_MODE(MX31_PIN_RI_DTE1, IOMUX_CONFIG_GPIO), /* CMOS reset */
	/* SSI */
	MX31_PIN_STXD4__STXD4,
	MX31_PIN_SRXD4__SRXD4,
	MX31_PIN_SCK4__SCK4,
	MX31_PIN_SFS4__SFS4,
};

/*
 * Camera support
 */
static phys_addr_t mx3_camera_base __initdata;
#define MX31_3DS_CAMERA_BUF_SIZE SZ_8M

#define MX31_3DS_GPIO_CAMERA_PW IOMUX_TO_GPIO(MX31_PIN_CSI_D5)
#define MX31_3DS_GPIO_CAMERA_RST IOMUX_TO_GPIO(MX31_PIN_RI_DTE1)

static struct gpio mx31_3ds_camera_gpios[] = {
	{ MX31_3DS_GPIO_CAMERA_PW, GPIOF_OUT_INIT_HIGH, "camera-power" },
	{ MX31_3DS_GPIO_CAMERA_RST, GPIOF_OUT_INIT_HIGH, "camera-reset" },
};

static const struct mx3_camera_pdata mx31_3ds_camera_pdata __initconst = {
	.flags = MX3_CAMERA_DATAWIDTH_10,
	.mclk_10khz = 2600,
};

static int __init mx31_3ds_init_camera(void)
{
	int dma, ret = -ENOMEM;
	struct platform_device *pdev =
		imx31_alloc_mx3_camera(&mx31_3ds_camera_pdata);

	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	if (!mx3_camera_base)
		goto err;

	dma = dma_declare_coherent_memory(&pdev->dev,
					mx3_camera_base, mx3_camera_base,
					MX31_3DS_CAMERA_BUF_SIZE,
					DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE);

	if (!(dma & DMA_MEMORY_MAP))
		goto err;

	ret = platform_device_add(pdev);
	if (ret)
err:
		platform_device_put(pdev);

	return ret;
}

static int mx31_3ds_camera_power(struct device *dev, int on)
{
	/* enable or disable the camera */
	pr_debug("%s: %s the camera\n", __func__, on ? "ENABLE" : "DISABLE");
	gpio_set_value(MX31_3DS_GPIO_CAMERA_PW, on ? 0 : 1);

	if (!on)
		goto out;

	/* If enabled, give a reset impulse */
	gpio_set_value(MX31_3DS_GPIO_CAMERA_RST, 0);
	msleep(20);
	gpio_set_value(MX31_3DS_GPIO_CAMERA_RST, 1);
	msleep(100);

out:
	return 0;
}

static struct i2c_board_info mx31_3ds_i2c_camera = {
	I2C_BOARD_INFO("ov2640", 0x30),
};

static struct regulator_bulk_data mx31_3ds_camera_regs[] = {
	{ .supply = "cmos_vcore" },
	{ .supply = "cmos_2v8" },
};

static struct soc_camera_link iclink_ov2640 = {
	.bus_id		= 0,
	.board_info	= &mx31_3ds_i2c_camera,
	.i2c_adapter_id	= 0,
	.power		= mx31_3ds_camera_power,
	.regulators	= mx31_3ds_camera_regs,
	.num_regulators	= ARRAY_SIZE(mx31_3ds_camera_regs),
};

static struct platform_device mx31_3ds_ov2640 = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &iclink_ov2640,
	},
};

/*
 * FB support
 */
static const struct fb_videomode fb_modedb[] = {
	{	/* 480x640 @ 60 Hz */
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
		.sync		= FB_SYNC_OE_ACT_HIGH | FB_SYNC_CLK_INVERT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

static struct mx3fb_platform_data mx3fb_pdata __initdata = {
	.name		= "Epson-VGA",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

/* LCD */
static struct l4f00242t03_pdata mx31_3ds_l4f00242t03_pdata = {
	.reset_gpio		= IOMUX_TO_GPIO(MX31_PIN_LCS1),
	.data_enable_gpio	= IOMUX_TO_GPIO(MX31_PIN_SER_RS),
};

/*
 * Support for SD card slot in personality board
 */
#define MX31_3DS_GPIO_SDHC1_CD IOMUX_TO_GPIO(MX31_PIN_GPIO3_1)
#define MX31_3DS_GPIO_SDHC1_BE IOMUX_TO_GPIO(MX31_PIN_GPIO3_0)

static struct gpio mx31_3ds_sdhc1_gpios[] = {
	{ MX31_3DS_GPIO_SDHC1_CD, GPIOF_IN, "sdhc1-card-detect" },
	{ MX31_3DS_GPIO_SDHC1_BE, GPIOF_OUT_INIT_LOW, "sdhc1-bus-en" },
};

static int mx31_3ds_sdhc1_init(struct device *dev,
			       irq_handler_t detect_irq,
			       void *data)
{
	int ret;

	ret = gpio_request_array(mx31_3ds_sdhc1_gpios,
				 ARRAY_SIZE(mx31_3ds_sdhc1_gpios));
	if (ret) {
		pr_warning("Unable to request the SD/MMC GPIOs.\n");
		return ret;
	}

	ret = request_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1)),
			  detect_irq, IRQF_DISABLED |
			  IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			  "sdhc1-detect", data);
	if (ret) {
		pr_warning("Unable to request the SD/MMC card-detect IRQ.\n");
		goto gpio_free;
	}

	return 0;

gpio_free:
	gpio_free_array(mx31_3ds_sdhc1_gpios,
			ARRAY_SIZE(mx31_3ds_sdhc1_gpios));
	return ret;
}

static void mx31_3ds_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO3_1)), data);
	gpio_free_array(mx31_3ds_sdhc1_gpios,
			 ARRAY_SIZE(mx31_3ds_sdhc1_gpios));
}

static void mx31_3ds_sdhc1_setpower(struct device *dev, unsigned int vdd)
{
	/*
	 * While the voltage stuff is done by the driver, activate the
	 * Buffer Enable Pin only if there is a card in slot to fix the card
	 * voltage issue caused by bi-directional chip TXB0108 on 3Stack.
	 * Done here because at this stage we have for sure a debounced value
	 * of the presence of the card, showed by the value of vdd.
	 * 7 == ilog2(MMC_VDD_165_195)
	 */
	if (vdd > 7)
		gpio_set_value(MX31_3DS_GPIO_SDHC1_BE, 1);
	else
		gpio_set_value(MX31_3DS_GPIO_SDHC1_BE, 0);
}

static struct imxmmc_platform_data sdhc1_pdata = {
	.init		= mx31_3ds_sdhc1_init,
	.exit		= mx31_3ds_sdhc1_exit,
	.setpower	= mx31_3ds_sdhc1_setpower,
};

/*
 * Matrix keyboard
 */

static const uint32_t mx31_3ds_keymap[] = {
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

static const struct matrix_keymap_data mx31_3ds_keymap_data __initconst = {
	.keymap		= mx31_3ds_keymap,
	.keymap_size	= ARRAY_SIZE(mx31_3ds_keymap),
};

/* Regulators */
static struct regulator_init_data pwgtx_init = {
	.constraints = {
		.boot_on	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data gpo_init = {
	.constraints = {
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_consumer_supply vmmc2_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "mxc-mmc.0"),
};

static struct regulator_init_data vmmc2_init = {
	.constraints = {
		.min_uV = 3000000,
		.max_uV = 3000000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(vmmc2_consumers),
	.consumer_supplies = vmmc2_consumers,
};

static struct regulator_consumer_supply vmmc1_consumers[] = {
	REGULATOR_SUPPLY("vcore", "spi0.0"),
	REGULATOR_SUPPLY("cmos_2v8", "soc-camera-pdrv.0"),
};

static struct regulator_init_data vmmc1_init = {
	.constraints = {
		.min_uV = 2800000,
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
		.min_uV = 1800000,
		.max_uV = 1800000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
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

static struct mc13xxx_regulator_init_data mx31_3ds_regulators[] = {
	{
		.id = MC13783_REG_PWGT1SPI, /* Power Gate for ARM core. */
		.init_data = &pwgtx_init,
	}, {
		.id = MC13783_REG_PWGT2SPI, /* Power Gate for L2 Cache. */
		.init_data = &pwgtx_init,
	}, {

		.id = MC13783_REG_GPO1, /* Turn on 1.8V */
		.init_data = &gpo_init,
	}, {
		.id = MC13783_REG_GPO3, /* Turn on 3.3V */
		.init_data = &gpo_init,
	}, {
		.id = MC13783_REG_VMMC2, /* Power MMC/SD, WiFi/Bluetooth. */
		.init_data = &vmmc2_init,
	}, {
		.id = MC13783_REG_VMMC1, /* Power LCD, CMOS, FM, GPS, Accel. */
		.init_data = &vmmc1_init,
	}, {
		.id = MC13783_REG_VGEN,  /* Power LCD */
		.init_data = &vgen_init,
	}, {
		.id = MC13783_REG_VVIB,  /* Power CMOS */
		.init_data = &vvib_init,
	},
};

/* MC13783 */
static struct mc13xxx_codec_platform_data mx31_3ds_codec = {
	.dac_ssi_port = MC13783_SSI1_PORT,
	.adc_ssi_port = MC13783_SSI1_PORT,
};

static struct mc13xxx_platform_data mc13783_pdata = {
	.regulators = {
		.regulators = mx31_3ds_regulators,
		.num_regulators = ARRAY_SIZE(mx31_3ds_regulators),
	},
	.codec = &mx31_3ds_codec,
	.flags  = MC13XXX_USE_TOUCHSCREEN | MC13XXX_USE_RTC | MC13XXX_USE_CODEC,

};

static struct imx_ssi_platform_data mx31_3ds_ssi_pdata = {
	.flags = IMX_SSI_DMA | IMX_SSI_NET,
};

/* SPI */
static int spi0_internal_chipselect[] = {
	MXC_SPI_CS(2),
};

static const struct spi_imx_master spi0_pdata __initconst = {
	.chipselect	= spi0_internal_chipselect,
	.num_chipselect	= ARRAY_SIZE(spi0_internal_chipselect),
};

static int spi1_internal_chipselect[] = {
	MXC_SPI_CS(0),
	MXC_SPI_CS(2),
};

static const struct spi_imx_master spi1_pdata __initconst = {
	.chipselect	= spi1_internal_chipselect,
	.num_chipselect	= ARRAY_SIZE(spi1_internal_chipselect),
};

static struct spi_board_info mx31_3ds_spi_devs[] __initdata = {
	{
		.modalias	= "mc13783",
		.max_speed_hz	= 1000000,
		.bus_num	= 1,
		.chip_select	= 1, /* SS2 */
		.platform_data	= &mc13783_pdata,
		/* irq number is run-time assigned */
		.mode = SPI_CS_HIGH,
	}, {
		.modalias	= "l4f00242t03",
		.max_speed_hz	= 5000000,
		.bus_num	= 0,
		.chip_select	= 0, /* SS2 */
		.platform_data	= &mx31_3ds_l4f00242t03_pdata,
	},
};

/*
 * NAND Flash
 */
static const struct mxc_nand_platform_data
mx31_3ds_nand_board_info __initconst = {
	.width		= 1,
	.hw_ecc		= 1,
#ifdef CONFIG_MACH_MX31_3DS_MXC_NAND_USE_BBT
	.flash_bbt	= 1,
#endif
};

/*
 * USB OTG
 */

#define USB_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | \
		     PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)

#define USBOTG_RST_B IOMUX_TO_GPIO(MX31_PIN_USB_PWR)
#define USBH2_RST_B IOMUX_TO_GPIO(MX31_PIN_USB_BYP)

static int mx31_3ds_usbotg_init(void)
{
	int err;

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

	err = gpio_request(USBOTG_RST_B, "otgusb-reset");
	if (err) {
		pr_err("Failed to request the USB OTG reset gpio\n");
		return err;
	}

	err = gpio_direction_output(USBOTG_RST_B, 0);
	if (err) {
		pr_err("Failed to drive the USB OTG reset gpio\n");
		goto usbotg_free_reset;
	}

	mdelay(1);
	gpio_set_value(USBOTG_RST_B, 1);
	return 0;

usbotg_free_reset:
	gpio_free(USBOTG_RST_B);
	return err;
}

static int mx31_3ds_otg_init(struct platform_device *pdev)
{
	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED);
}

static int mx31_3ds_host2_init(struct platform_device *pdev)
{
	int err;

	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DIR, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_NXT, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA0, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA1, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_PC_VS2, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_PC_BVD1, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_PC_BVD2, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_PC_RST, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_IOIS16, USB_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_PC_RW_B, USB_PAD_CFG);

	err = gpio_request(USBH2_RST_B, "usbh2-reset");
	if (err) {
		pr_err("Failed to request the USB Host 2 reset gpio\n");
		return err;
	}

	err = gpio_direction_output(USBH2_RST_B, 0);
	if (err) {
		pr_err("Failed to drive the USB Host 2 reset gpio\n");
		goto usbotg_free_reset;
	}

	mdelay(1);
	gpio_set_value(USBH2_RST_B, 1);

	mdelay(10);

	return mx31_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED);

usbotg_free_reset:
	gpio_free(USBH2_RST_B);
	return err;
}

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.init	= mx31_3ds_otg_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static struct mxc_usbh_platform_data usbh2_pdata __initdata = {
	.init = mx31_3ds_host2_init,
	.portsc	= MXC_EHCI_MODE_ULPI,
};

static const struct fsl_usb2_platform_data usbotg_pdata __initconst = {
	.operating_mode = FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_ULPI,
};

static int otg_mode_host;

static int __init mx31_3ds_otg_mode(char *options)
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
__setup("otg_mode=", mx31_3ds_otg_mode);

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct imxi2c_platform_data mx31_3ds_i2c0_data __initconst = {
	.bitrate = 100000,
};

static struct platform_device *devices[] __initdata = {
	&mx31_3ds_ov2640,
};

static void __init mx31_3ds_init(void)
{
	int ret;

	imx31_soc_init();

	/* Configure SPI1 IOMUX */
	mxc_iomux_set_gpr(MUX_PGP_CSPI_BB, true);

	mxc_iomux_setup_multiple_pins(mx31_3ds_pins, ARRAY_SIZE(mx31_3ds_pins),
				      "mx31_3ds");

	imx31_add_imx_uart0(&uart_pdata);
	imx31_add_mxc_nand(&mx31_3ds_nand_board_info);

	imx31_add_spi_imx1(&spi1_pdata);
	mx31_3ds_spi_devs[0].irq = gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_3));
	spi_register_board_info(mx31_3ds_spi_devs,
						ARRAY_SIZE(mx31_3ds_spi_devs));

	platform_add_devices(devices, ARRAY_SIZE(devices));

	imx31_add_imx_keypad(&mx31_3ds_keymap_data);

	mx31_3ds_usbotg_init();
	if (otg_mode_host) {
		otg_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
				ULPI_OTG_DRVVBUS_EXT);
		if (otg_pdata.otg)
			imx31_add_mxc_ehci_otg(&otg_pdata);
	}
	usbh2_pdata.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
			ULPI_OTG_DRVVBUS_EXT);
	if (usbh2_pdata.otg)
		imx31_add_mxc_ehci_hs(2, &usbh2_pdata);

	if (!otg_mode_host)
		imx31_add_fsl_usb2_udc(&usbotg_pdata);

	if (mxc_expio_init(MX31_CS5_BASE_ADDR,
			   gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_1))))
		printk(KERN_WARNING "Init of the debug board failed, all "
				    "devices on the debug board are unusable.\n");
	imx31_add_imx2_wdt(NULL);
	imx31_add_imx_i2c0(&mx31_3ds_i2c0_data);
	imx31_add_mxc_mmc(0, &sdhc1_pdata);

	imx31_add_spi_imx0(&spi0_pdata);
	imx31_add_ipu_core();
	imx31_add_mx3_sdc_fb(&mx3fb_pdata);

	/* CSI */
	/* Camera power: default - off */
	ret = gpio_request_array(mx31_3ds_camera_gpios,
				 ARRAY_SIZE(mx31_3ds_camera_gpios));
	if (ret) {
		pr_err("Failed to request camera gpios");
		iclink_ov2640.power = NULL;
	}

	mx31_3ds_init_camera();

	imx31_add_imx_ssi(0, &mx31_3ds_ssi_pdata);

	imx_add_platform_device("imx_mc13783", 0, NULL, 0, NULL, 0);
}

static void __init mx31_3ds_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer mx31_3ds_timer = {
	.init	= mx31_3ds_timer_init,
};

static void __init mx31_3ds_reserve(void)
{
	/* reserve MX31_3DS_CAMERA_BUF_SIZE bytes for mx3-camera */
	mx3_camera_base = arm_memblock_steal(MX31_3DS_CAMERA_BUF_SIZE,
					 MX31_3DS_CAMERA_BUF_SIZE);
}

MACHINE_START(MX31_3DS, "Freescale MX31PDK (3DS)")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io = mx31_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31_init_irq,
	.handle_irq = imx31_handle_irq,
	.timer = &mx31_3ds_timer,
	.init_machine = mx31_3ds_init,
	.reserve = mx31_3ds_reserve,
	.restart	= mxc_restart,
MACHINE_END
