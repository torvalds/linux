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
#include <linux/gpio/driver.h>
/* Needed for gpio_to_irq() */
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
#include <linux/irq.h>

#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include "common.h"
#include "devices-imx27.h"
#include "hardware.h"
#include "iomux-mx27.h"

/*
 * Base address of PBC controller, CS4
 */
#define PBC_BASE_ADDRESS        0xf4300000
#define PBC_REG_ADDR(offset)    (void __force __iomem *) \
		(PBC_BASE_ADDRESS + (offset))

/* When the PBC address connection is fixed in h/w, defined as 1 */
#define PBC_ADDR_SH             0

/* Offsets for the PBC Controller register */
/*
 * PBC Board version register offset
 */
#define PBC_VERSION_REG         PBC_REG_ADDR(0x00000 >> PBC_ADDR_SH)
/*
 * PBC Board control register 1 set address.
 */
#define PBC_BCTRL1_SET_REG      PBC_REG_ADDR(0x00008 >> PBC_ADDR_SH)
/*
 * PBC Board control register 1 clear address.
 */
#define PBC_BCTRL1_CLEAR_REG    PBC_REG_ADDR(0x0000C >> PBC_ADDR_SH)

/* PBC Board Control Register 1 bit definitions */
#define PBC_BCTRL1_LCDON        0x0800	/* Enable the LCD */

/* to determine the correct external crystal reference */
#define CKIH_27MHZ_BIT_SET      (1 << 3)

static const int mx27ads_pins[] __initconst = {
	/* UART0 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* UART1 */
	PE3_PF_UART2_CTS,
	PE4_PF_UART2_RTS,
	PE6_PF_UART2_TXD,
	PE7_PF_UART2_RXD,
	/* UART2 */
	PE8_PF_UART3_TXD,
	PE9_PF_UART3_RXD,
	PE10_PF_UART3_CTS,
	PE11_PF_UART3_RTS,
	/* UART3 */
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD,
	/* UART4 */
	PB18_AF_UART5_TXD,
	PB19_AF_UART5_RXD,
	PB20_AF_UART5_CTS,
	PB21_AF_UART5_RTS,
	/* UART5 */
	PB10_AF_UART6_TXD,
	PB12_AF_UART6_CTS,
	PB11_AF_UART6_RXD,
	PB13_AF_UART6_RTS,
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
	/* I2C2 */
	PC5_PF_I2C2_SDA,
	PC6_PF_I2C2_SCL,
	/* FB */
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
	PA24_PF_REV,
	PA25_PF_CLS,
	PA26_PF_PS,
	PA27_PF_SPL_SPR,
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA30_PF_CONTRAST,
	PA31_PF_OE_ACD,
	/* OWIRE */
	PE16_AF_OWIRE,
	/* SDHC1*/
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,
	/* SDHC2*/
	PB4_PF_SD2_D0,
	PB5_PF_SD2_D1,
	PB6_PF_SD2_D2,
	PB7_PF_SD2_D3,
	PB8_PF_SD2_CMD,
	PB9_PF_SD2_CLK,
};

static const struct mxc_nand_platform_data
mx27ads_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
};

/* ADS's NOR flash */
static struct physmap_flash_data mx27ads_flash_data = {
	.width = 2,
};

static struct resource mx27ads_flash_resource = {
	.start = 0xc0000000,
	.end = 0xc0000000 + 0x02000000 - 1,
	.flags = IORESOURCE_MEM,

};

static struct platform_device mx27ads_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &mx27ads_flash_data,
	},
	.num_resources = 1,
	.resource = &mx27ads_flash_resource,
};

static const struct imxi2c_platform_data mx27ads_i2c1_data __initconst = {
	.bitrate = 100000,
};

static struct i2c_board_info mx27ads_i2c_devices[] = {
};

static void vgpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value)
		imx_writew(PBC_BCTRL1_LCDON, PBC_BCTRL1_SET_REG);
	else
		imx_writew(PBC_BCTRL1_LCDON, PBC_BCTRL1_CLEAR_REG);
}

static int vgpio_dir_out(struct gpio_chip *chip, unsigned offset, int value)
{
	return 0;
}

#define MX27ADS_LCD_GPIO	(6 * 32)

static struct regulator_consumer_supply mx27ads_lcd_regulator_consumer =
	REGULATOR_SUPPLY("lcd", "imx-fb.0");

static struct regulator_init_data mx27ads_lcd_regulator_init_data = {
	.constraints	= {
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
},
	.consumer_supplies	= &mx27ads_lcd_regulator_consumer,
	.num_consumer_supplies	= 1,
};

static struct fixed_voltage_config mx27ads_lcd_regulator_pdata = {
	.supply_name	= "LCD",
	.microvolts	= 3300000,
	.gpio		= MX27ADS_LCD_GPIO,
	.init_data	= &mx27ads_lcd_regulator_init_data,
};

static void __init mx27ads_regulator_init(void)
{
	struct gpio_chip *vchip;

	vchip = kzalloc(sizeof(*vchip), GFP_KERNEL);
	vchip->owner		= THIS_MODULE;
	vchip->label		= "LCD";
	vchip->base		= MX27ADS_LCD_GPIO;
	vchip->ngpio		= 1;
	vchip->direction_output	= vgpio_dir_out;
	vchip->set		= vgpio_set;
	gpiochip_add_data(vchip, NULL);

	platform_device_register_data(NULL, "reg-fixed-voltage",
				      PLATFORM_DEVID_AUTO,
				      &mx27ads_lcd_regulator_pdata,
				      sizeof(mx27ads_lcd_regulator_pdata));
}

static struct imx_fb_videomode mx27ads_modes[] = {
	{
		.mode = {
			.name		= "Sharp-LQ035Q7",
			.refresh	= 60,
			.xres		= 240,
			.yres		= 320,
			.pixclock	= 188679, /* in ps (5.3MHz) */
			.hsync_len	= 1,
			.left_margin	= 9,
			.right_margin	= 16,
			.vsync_len	= 1,
			.upper_margin	= 7,
			.lower_margin	= 9,
		},
		.bpp		= 16,
		.pcr		= 0xFB008BC0,
	},
};

static const struct imx_fb_platform_data mx27ads_fb_data __initconst = {
	.mode = mx27ads_modes,
	.num_modes = ARRAY_SIZE(mx27ads_modes),

	/*
	 * - HSYNC active high
	 * - VSYNC active high
	 * - clk notenabled while idle
	 * - clock inverted
	 * - data not inverted
	 * - data enable low active
	 * - enable sharp mode
	 */
	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020010,
};

static int mx27ads_sdhc1_init(struct device *dev, irq_handler_t detect_irq,
			      void *data)
{
	return request_irq(gpio_to_irq(IMX_GPIO_NR(5, 21)), detect_irq,
			   IRQF_TRIGGER_RISING, "sdhc1-card-detect", data);
}

static int mx27ads_sdhc2_init(struct device *dev, irq_handler_t detect_irq,
			      void *data)
{
	return request_irq(gpio_to_irq(IMX_GPIO_NR(2, 7)), detect_irq,
			   IRQF_TRIGGER_RISING, "sdhc2-card-detect", data);
}

static void mx27ads_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IMX_GPIO_NR(5, 21)), data);
}

static void mx27ads_sdhc2_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IMX_GPIO_NR(2, 7)), data);
}

static const struct imxmmc_platform_data sdhc1_pdata __initconst = {
	.init = mx27ads_sdhc1_init,
	.exit = mx27ads_sdhc1_exit,
};

static const struct imxmmc_platform_data sdhc2_pdata __initconst = {
	.init = mx27ads_sdhc2_init,
	.exit = mx27ads_sdhc2_exit,
};

static struct platform_device *platform_devices[] __initdata = {
	&mx27ads_nor_mtd_device,
};

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static void __init mx27ads_board_init(void)
{
	imx27_soc_init();

	mxc_gpio_setup_multiple_pins(mx27ads_pins, ARRAY_SIZE(mx27ads_pins),
			"mx27ads");

	imx27_add_imx_uart0(&uart_pdata);
	imx27_add_imx_uart1(&uart_pdata);
	imx27_add_imx_uart2(&uart_pdata);
	imx27_add_imx_uart3(&uart_pdata);
	imx27_add_imx_uart4(&uart_pdata);
	imx27_add_imx_uart5(&uart_pdata);
	imx27_add_mxc_nand(&mx27ads_nand_board_info);

	/* only the i2c master 1 is used on this CPU card */
	i2c_register_board_info(1, mx27ads_i2c_devices,
				ARRAY_SIZE(mx27ads_i2c_devices));
	imx27_add_imx_i2c(1, &mx27ads_i2c1_data);
	mx27ads_regulator_init();
	imx27_add_imx_fb(&mx27ads_fb_data);
	imx27_add_mxc_mmc(0, &sdhc1_pdata);
	imx27_add_mxc_mmc(1, &sdhc2_pdata);

	imx27_add_fec(NULL);
	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
	imx27_add_mxc_w1();
}

static void __init mx27ads_timer_init(void)
{
	unsigned long fref = 26000000;

	if ((imx_readw(PBC_VERSION_REG) & CKIH_27MHZ_BIT_SET) == 0)
		fref = 27000000;

	mx27_clocks_init(fref);
}

static struct map_desc mx27ads_io_desc[] __initdata = {
	{
		.virtual = PBC_BASE_ADDRESS,
		.pfn = __phys_to_pfn(MX27_CS4_BASE_ADDR),
		.length = SZ_1M,
		.type = MT_DEVICE,
	},
};

static void __init mx27ads_map_io(void)
{
	mx27_map_io();
	iotable_init(mx27ads_io_desc, ARRAY_SIZE(mx27ads_io_desc));
}

MACHINE_START(MX27ADS, "Freescale i.MX27ADS")
	/* maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io = mx27ads_map_io,
	.init_early = imx27_init_early,
	.init_irq = mx27_init_irq,
	.init_time	= mx27ads_timer_init,
	.init_machine = mx27ads_board_init,
	.restart	= mxc_restart,
MACHINE_END
