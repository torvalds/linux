/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
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

#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/physmap.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "common.h"
#include "devices-imx21.h"
#include "hardware.h"
#include "iomux-mx21.h"

#define MX21ADS_CS8900A_REG		(MX21_CS1_BASE_ADDR + 0x000000)
#define MX21ADS_ST16C255_IOBASE_REG	(MX21_CS1_BASE_ADDR + 0x200000)
#define MX21ADS_VERSION_REG		(MX21_CS1_BASE_ADDR + 0x400000)
#define MX21ADS_IO_REG			(MX21_CS1_BASE_ADDR + 0x800000)

#define MX21ADS_MMC_CD			IMX_GPIO_NR(4, 25)
#define MX21ADS_CS8900A_IRQ_GPIO	IMX_GPIO_NR(5, 11)
#define MX21ADS_MMGPIO_BASE		(6 * 32)

/* MX21ADS_IO_REG bit definitions */
#define MX21ADS_IO_SD_WP		(MX21ADS_MMGPIO_BASE + 0)
#define MX21ADS_IO_TP6			(MX21ADS_IO_SD_WP)
#define MX21ADS_IO_SW_SEL		(MX21ADS_MMGPIO_BASE + 1)
#define MX21ADS_IO_TP7			(MX21ADS_IO_SW_SEL)
#define MX21ADS_IO_RESET_E_UART		(MX21ADS_MMGPIO_BASE + 2)
#define MX21ADS_IO_RESET_BASE		(MX21ADS_MMGPIO_BASE + 3)
#define MX21ADS_IO_CSI_CTL2		(MX21ADS_MMGPIO_BASE + 4)
#define MX21ADS_IO_CSI_CTL1		(MX21ADS_MMGPIO_BASE + 5)
#define MX21ADS_IO_CSI_CTL0		(MX21ADS_MMGPIO_BASE + 6)
#define MX21ADS_IO_UART1_EN		(MX21ADS_MMGPIO_BASE + 7)
#define MX21ADS_IO_UART4_EN		(MX21ADS_MMGPIO_BASE + 8)
#define MX21ADS_IO_LCDON		(MX21ADS_MMGPIO_BASE + 9)
#define MX21ADS_IO_IRDA_EN		(MX21ADS_MMGPIO_BASE + 10)
#define MX21ADS_IO_IRDA_FIR_SEL		(MX21ADS_MMGPIO_BASE + 11)
#define MX21ADS_IO_IRDA_MD0_B		(MX21ADS_MMGPIO_BASE + 12)
#define MX21ADS_IO_IRDA_MD1		(MX21ADS_MMGPIO_BASE + 13)
#define MX21ADS_IO_LED4_ON		(MX21ADS_MMGPIO_BASE + 14)
#define MX21ADS_IO_LED3_ON		(MX21ADS_MMGPIO_BASE + 15)

static const int mx21ads_pins[] __initconst = {

	/* CS8900A */
	(GPIO_PORTE | GPIO_GPIO | GPIO_IN | 11),

	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,

	/* UART3 (IrDA) - only TXD and RXD */
	PE8_PF_UART3_TXD,
	PE9_PF_UART3_RXD,

	/* UART4 */
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD,

	/* LCDC */
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
	PA24_PF_REV,     /* Sharp panel dedicated signal */
	PA25_PF_CLS,     /* Sharp panel dedicated signal */
	PA26_PF_PS,      /* Sharp panel dedicated signal */
	PA27_PF_SPL_SPR, /* Sharp panel dedicated signal */
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA30_PF_CONTRAST,
	PA31_PF_OE_ACD,

	/* MMC/SDHC */
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,

	/* NFC */
	PF0_PF_NRFB,
	PF1_PF_NFCE,
	PF2_PF_NFWP,
	PF3_PF_NFCLE,
	PF4_PF_NFALE,
	PF5_PF_NFRE,
	PF6_PF_NFWE,
	PF7_PF_NFIO0,
	PF8_PF_NFIO1,
	PF9_PF_NFIO2,
	PF10_PF_NFIO3,
	PF11_PF_NFIO4,
	PF12_PF_NFIO5,
	PF13_PF_NFIO6,
	PF14_PF_NFIO7,
};

/* ADS's NOR flash: 2x AM29BDS128HE9VKI on 32-bit bus */
static struct physmap_flash_data mx21ads_flash_data = {
	.width = 4,
};

static struct resource mx21ads_flash_resource =
	DEFINE_RES_MEM(MX21_CS0_BASE_ADDR, SZ_32M);

static struct platform_device mx21ads_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &mx21ads_flash_data,
	},
	.num_resources = 1,
	.resource = &mx21ads_flash_resource,
};

static struct resource mx21ads_cs8900_resources[] __initdata = {
	DEFINE_RES_MEM(MX21ADS_CS8900A_REG, SZ_1K),
	/* irq number is run-time assigned */
	DEFINE_RES_IRQ(-1),
};

static const struct platform_device_info mx21ads_cs8900_devinfo __initconst = {
	.name = "cs89x0",
	.id = 0,
	.res = mx21ads_cs8900_resources,
	.num_res = ARRAY_SIZE(mx21ads_cs8900_resources),
};

static const struct imxuart_platform_data uart_pdata_rts __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const struct imxuart_platform_data uart_pdata_norts __initconst = {
};

static struct resource mx21ads_mmgpio_resource =
	DEFINE_RES_MEM_NAMED(MX21ADS_IO_REG, SZ_2, "dat");

static struct bgpio_pdata mx21ads_mmgpio_pdata = {
	.base	= MX21ADS_MMGPIO_BASE,
	.ngpio	= 16,
};

static struct platform_device mx21ads_mmgpio = {
	.name = "basic-mmio-gpio",
	.id = PLATFORM_DEVID_AUTO,
	.resource = &mx21ads_mmgpio_resource,
	.num_resources = 1,
	.dev = {
		.platform_data = &mx21ads_mmgpio_pdata,
	},
};

static struct regulator_consumer_supply mx21ads_lcd_regulator_consumer =
	REGULATOR_SUPPLY("lcd", "imx-fb.0");

static struct regulator_init_data mx21ads_lcd_regulator_init_data = {
	.constraints = {
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.consumer_supplies	= &mx21ads_lcd_regulator_consumer,
	.num_consumer_supplies	= 1,
};

static struct fixed_voltage_config mx21ads_lcd_regulator_pdata = {
	.supply_name	= "LCD",
	.microvolts	= 3300000,
	.gpio		= MX21ADS_IO_LCDON,
	.enable_high	= 1,
	.init_data	= &mx21ads_lcd_regulator_init_data,
};

static struct platform_device mx21ads_lcd_regulator = {
	.name = "reg-fixed-voltage",
	.id = PLATFORM_DEVID_AUTO,
	.dev = {
		.platform_data = &mx21ads_lcd_regulator_pdata,
	},
};

/*
 * Connected is a portrait Sharp-QVGA display
 * of type: LQ035Q7DB02
 */
static struct imx_fb_videomode mx21ads_modes[] = {
	{
		.mode = {
			.name		= "Sharp-LQ035Q7",
			.refresh	= 60,
			.xres		= 240,
			.yres		= 320,
			.pixclock	= 188679, /* in ps (5.3MHz) */
			.hsync_len	= 2,
			.left_margin	= 6,
			.right_margin	= 16,
			.vsync_len	= 1,
			.upper_margin	= 8,
			.lower_margin	= 10,
		},
		.pcr		= 0xfb108bc7,
		.bpp		= 16,
	},
};

static const struct imx_fb_platform_data mx21ads_fb_data __initconst = {
	.mode = mx21ads_modes,
	.num_modes = ARRAY_SIZE(mx21ads_modes),

	.pwmr		= 0x00a903ff,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020008,
};

static int mx21ads_sdhc_get_ro(struct device *dev)
{
	return gpio_get_value(MX21ADS_IO_SD_WP);
}

static int mx21ads_sdhc_init(struct device *dev, irq_handler_t detect_irq,
	void *data)
{
	int ret;

	ret = gpio_request(MX21ADS_IO_SD_WP, "mmc-ro");
	if (ret)
		return ret;

	return request_irq(gpio_to_irq(MX21ADS_MMC_CD), detect_irq,
			   IRQF_TRIGGER_FALLING, "mmc-detect", data);
}

static void mx21ads_sdhc_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(MX21ADS_MMC_CD), data);
	gpio_free(MX21ADS_IO_SD_WP);
}

static const struct imxmmc_platform_data mx21ads_sdhc_pdata __initconst = {
	.ocr_avail = MMC_VDD_29_30 | MMC_VDD_30_31, /* 3.0V */
	.get_ro = mx21ads_sdhc_get_ro,
	.init = mx21ads_sdhc_init,
	.exit = mx21ads_sdhc_exit,
};

static const struct mxc_nand_platform_data
mx21ads_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
};

static struct platform_device *platform_devices[] __initdata = {
	&mx21ads_mmgpio,
	&mx21ads_lcd_regulator,
	&mx21ads_nor_mtd_device,
};

static void __init mx21ads_board_init(void)
{
	imx21_soc_init();

	mxc_gpio_setup_multiple_pins(mx21ads_pins, ARRAY_SIZE(mx21ads_pins),
			"mx21ads");

	imx21_add_imx_uart0(&uart_pdata_rts);
	imx21_add_imx_uart2(&uart_pdata_norts);
	imx21_add_imx_uart3(&uart_pdata_rts);
	imx21_add_mxc_mmc(0, &mx21ads_sdhc_pdata);
	imx21_add_mxc_nand(&mx21ads_nand_board_info);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	imx21_add_imx_fb(&mx21ads_fb_data);

	mx21ads_cs8900_resources[1].start =
			gpio_to_irq(MX21ADS_CS8900A_IRQ_GPIO);
	mx21ads_cs8900_resources[1].end =
			gpio_to_irq(MX21ADS_CS8900A_IRQ_GPIO);
	platform_device_register_full(&mx21ads_cs8900_devinfo);
}

static void __init mx21ads_timer_init(void)
{
	mx21_clocks_init(32768, 26000000);
}

MACHINE_START(MX21ADS, "Freescale i.MX21ADS")
	/* maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io		= mx21_map_io,
	.init_early = imx21_init_early,
	.init_irq = mx21_init_irq,
	.init_time	= mx21ads_timer_init,
	.init_machine = mx21ads_board_init,
	.restart	= mxc_restart,
MACHINE_END
