/*
 * linux/arch/arm/mach-at91/board-yl-9200.c
 *
 * Adapted from various board files in arch/arm/mach-at91
 *
 * Modifications for YL-9200 platform:
 *  Copyright (C) 2007 S. Birtles
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

#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/at91rm9200_mc.h>
#include <mach/cpu.h>

#include "generic.h"


static void __init yl9200_init_early(void)
{
	/* Set cpu type: PQFP */
	at91rm9200_set_type(ARCH_REVISON_9200_PQFP);

	/* Initialize processor: 18.432 MHz crystal */
	at91_initialize(18432000);

	/* Setup the LEDs D2=PB17 (timer), D3=PB16 (cpu) */
	at91_init_leds(AT91_PIN_PB16, AT91_PIN_PB17);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART1 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91RM9200_ID_US1, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
			| ATMEL_UART_DTR | ATMEL_UART_DSR | ATMEL_UART_DCD
			| ATMEL_UART_RI);

	/* USART0 on ttyS2. (Rx & Tx only to JP3) */
	at91_register_uart(AT91RM9200_ID_US0, 2, 0);

	/* USART3 on ttyS3. (Rx, Tx, RTS - RS485 interface) */
	at91_register_uart(AT91RM9200_ID_US3, 3, ATMEL_UART_RTS);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

/*
 * LEDs
 */
static struct gpio_led yl9200_leds[] = {
	{	/* D2 */
		.name			= "led2",
		.gpio			= AT91_PIN_PB17,
		.active_low		= 1,
		.default_trigger	= "timer",
	},
	{	/* D3 */
		.name			= "led3",
		.gpio			= AT91_PIN_PB16,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	},
	{	/* D4 */
		.name			= "led4",
		.gpio			= AT91_PIN_PB15,
		.active_low		= 1,
	},
	{	/* D5 */
		.name			= "led5",
		.gpio			= AT91_PIN_PB8,
		.active_low		= 1,
	}
};

/*
 * Ethernet
 */
static struct at91_eth_data __initdata yl9200_eth_data = {
	.phy_irq_pin		= AT91_PIN_PB28,
	.is_rmii		= 1,
};

/*
 * USB Host
 */
static struct at91_usbh_data __initdata yl9200_usbh_data = {
	.ports			= 1,	/* PQFP version of AT91RM9200 */
};

/*
 * USB Device
 */
static struct at91_udc_data __initdata yl9200_udc_data = {
	.pullup_pin		= AT91_PIN_PC4,
	.vbus_pin		= AT91_PIN_PC5,
	.pullup_active_low	= 1,	/* Active Low due to PNP transistor (pg 7) */

};

/*
 * MMC
 */
static struct at91_mmc_data __initdata yl9200_mmc_data = {
	.det_pin	= AT91_PIN_PB9,
	// .wp_pin	= ... not connected
	.wire4		= 1,
};

/*
 * NAND Flash
 */
static struct mtd_partition __initdata yl9200_nand_partition[] = {
	{
		.name	= "AT91 NAND partition 1, boot",
		.offset	= 0,
		.size	= SZ_256K
	},
	{
		.name	= "AT91 NAND partition 2, kernel",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= (2 * SZ_1M) - SZ_256K
	},
	{
		.name	= "AT91 NAND partition 3, filesystem",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= 14 * SZ_1M
	},
	{
		.name	= "AT91 NAND partition 4, storage",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= SZ_16M
	},
	{
		.name	= "AT91 NAND partition 5, ext-fs",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= SZ_32M
	}
};

static struct atmel_nand_data __initdata yl9200_nand_data = {
	.ale		= 6,
	.cle		= 7,
	// .det_pin	= ... not connected
	.rdy_pin	= AT91_PIN_PC14,	/* R/!B (Sheet10) */
	.enable_pin	= AT91_PIN_PC15,	/* !CE  (Sheet10) */
	.parts		= yl9200_nand_partition,
	.num_parts	= ARRAY_SIZE(yl9200_nand_partition),
};

/*
 * NOR Flash
 */
#define YL9200_FLASH_BASE	AT91_CHIPSELECT_0
#define YL9200_FLASH_SIZE	SZ_16M

static struct mtd_partition yl9200_flash_partitions[] = {
	{
		.name		= "Bootloader",
		.offset		= 0,
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= (2 * SZ_1M) - SZ_256K
	},
	{
		.name		= "Filesystem",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= MTDPART_SIZ_FULL
	}
};

static struct physmap_flash_data yl9200_flash_data = {
	.width		= 2,
	.parts		= yl9200_flash_partitions,
	.nr_parts	= ARRAY_SIZE(yl9200_flash_partitions),
};

static struct resource yl9200_flash_resources[] = {
	{
		.start	= YL9200_FLASH_BASE,
		.end	= YL9200_FLASH_BASE + YL9200_FLASH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device yl9200_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &yl9200_flash_data,
			},
	.resource	= yl9200_flash_resources,
	.num_resources	= ARRAY_SIZE(yl9200_flash_resources),
};

/*
 * I2C (TWI)
 */
static struct i2c_board_info __initdata yl9200_i2c_devices[] = {
	{	/* EEPROM */
		I2C_BOARD_INFO("24c128", 0x50),
	}
};

/*
 * GPIO Buttons
*/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button yl9200_buttons[] = {
	{
		.gpio		= AT91_PIN_PA24,
		.code		= BTN_2,
		.desc		= "SW2",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB1,
		.code		= BTN_3,
		.desc		= "SW3",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB2,
		.code		= BTN_4,
		.desc		= "SW4",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB6,
		.code		= BTN_5,
		.desc		= "SW5",
		.active_low	= 1,
		.wakeup		= 1,
	}
};

static struct gpio_keys_platform_data yl9200_button_data = {
	.buttons	= yl9200_buttons,
	.nbuttons	= ARRAY_SIZE(yl9200_buttons),
};

static struct platform_device yl9200_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &yl9200_button_data,
	}
};

static void __init yl9200_add_device_buttons(void)
{
	at91_set_gpio_input(AT91_PIN_PA24, 1);	/* SW2 */
	at91_set_deglitch(AT91_PIN_PA24, 1);
	at91_set_gpio_input(AT91_PIN_PB1, 1);	/* SW3 */
	at91_set_deglitch(AT91_PIN_PB1, 1);
	at91_set_gpio_input(AT91_PIN_PB2, 1);	/* SW4 */
	at91_set_deglitch(AT91_PIN_PB2, 1);
	at91_set_gpio_input(AT91_PIN_PB6, 1);	/* SW5 */
	at91_set_deglitch(AT91_PIN_PB6, 1);

	/* Enable buttons (Sheet 5) */
	at91_set_gpio_output(AT91_PIN_PB7, 1);

	platform_device_register(&yl9200_button_device);
}
#else
static void __init yl9200_add_device_buttons(void) {}
#endif

/*
 * Touchscreen
 */
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
static int ads7843_pendown_state(void)
{
	return !at91_get_gpio_value(AT91_PIN_PB11);	/* Touchscreen PENIRQ */
}

static struct ads7846_platform_data ads_info = {
	.model			= 7843,
	.x_min			= 150,
	.x_max			= 3830,
	.y_min			= 190,
	.y_max			= 3830,
	.vref_delay_usecs	= 100,

	/* For a 8" touch-screen */
	// .x_plate_ohms		= 603,
	// .y_plate_ohms		= 332,

	/* For a 10.4" touch-screen */
	// .x_plate_ohms		= 611,
	// .y_plate_ohms		= 325,

	.x_plate_ohms		= 576,
	.y_plate_ohms		= 366,

	.pressure_max		= 15000, /* generally nonsense on the 7843 */
	.debounce_max		= 1,
	.debounce_rep		= 0,
	.debounce_tol		= (~0),
	.get_pendown_state	= ads7843_pendown_state,
};

static void __init yl9200_add_device_ts(void)
{
	at91_set_gpio_input(AT91_PIN_PB11, 1);	/* Touchscreen interrupt pin */
	at91_set_gpio_input(AT91_PIN_PB10, 1);	/* Touchscreen BUSY signal - not used! */
}
#else
static void __init yl9200_add_device_ts(void) {}
#endif

/*
 * SPI devices
 */
static struct spi_board_info yl9200_spi_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	{	/* Touchscreen */
		.modalias	= "ads7846",
		.chip_select	= 0,
		.max_speed_hz	= 5000 * 26,
		.platform_data	= &ads_info,
		.irq		= AT91_PIN_PB11,
	},
#endif
	{	/* CAN */
		.modalias	= "mcp2510",
		.chip_select	= 1,
		.max_speed_hz	= 25000 * 26,
		.irq		= AT91_PIN_PC0,
	}
};

/*
 * LCD / VGA
 *
 * EPSON S1D13806 FB (discontinued chip)
 * EPSON S1D13506 FB
 */
#if defined(CONFIG_FB_S1D13XXX) || defined(CONFIG_FB_S1D13XXX_MODULE)
#include <video/s1d13xxxfb.h>


static void __init yl9200_init_video(void)
{
	/* NWAIT Signal */
	at91_set_A_periph(AT91_PIN_PC6, 0);

	/* Initialization of the Static Memory Controller for Chip Select 2 */
	at91_sys_write(AT91_SMC_CSR(2), AT91_SMC_DBW_16		/* 16 bit */
			| AT91_SMC_WSEN | AT91_SMC_NWS_(0x4)	/* wait states */
			| AT91_SMC_TDF_(0x100)			/* float time */
	);
}

static struct s1d13xxxfb_regval yl9200_s1dfb_initregs[] =
{
	{S1DREG_MISC,			0x00},	/* Miscellaneous Register*/
	{S1DREG_COM_DISP_MODE,		0x01},	/* Display Mode Register, LCD only*/
	{S1DREG_GPIO_CNF0,		0x00},	/* General IO Pins Configuration Register*/
	{S1DREG_GPIO_CTL0,		0x00},	/* General IO Pins Control Register*/
	{S1DREG_CLK_CNF,		0x11},	/* Memory Clock Configuration Register*/
	{S1DREG_LCD_CLK_CNF,		0x10},	/* LCD Pixel Clock Configuration Register*/
	{S1DREG_CRT_CLK_CNF,		0x12},	/* CRT/TV Pixel Clock Configuration Register*/
	{S1DREG_MPLUG_CLK_CNF,		0x01},	/* MediaPlug Clock Configuration Register*/
	{S1DREG_CPU2MEM_WST_SEL,	0x02},	/* CPU To Memory Wait State Select Register*/
	{S1DREG_MEM_CNF,		0x00},	/* Memory Configuration Register*/
	{S1DREG_SDRAM_REF_RATE,		0x04},	/* DRAM Refresh Rate Register, MCLK source*/
	{S1DREG_SDRAM_TC0,		0x12},	/* DRAM Timings Control Register 0*/
	{S1DREG_SDRAM_TC1,		0x02},	/* DRAM Timings Control Register 1*/
	{S1DREG_PANEL_TYPE,		0x25},	/* Panel Type Register*/
	{S1DREG_MOD_RATE,		0x00},	/* MOD Rate Register*/
	{S1DREG_LCD_DISP_HWIDTH,	0x4F},	/* LCD Horizontal Display Width Register*/
	{S1DREG_LCD_NDISP_HPER,		0x13},	/* LCD Horizontal Non-Display Period Register*/
	{S1DREG_TFT_FPLINE_START,	0x01},	/* TFT FPLINE Start Position Register*/
	{S1DREG_TFT_FPLINE_PWIDTH,	0x0c},	/* TFT FPLINE Pulse Width Register*/
	{S1DREG_LCD_DISP_VHEIGHT0,	0xDF},	/* LCD Vertical Display Height Register 0*/
	{S1DREG_LCD_DISP_VHEIGHT1,	0x01},	/* LCD Vertical Display Height Register 1*/
	{S1DREG_LCD_NDISP_VPER,		0x2c},	/* LCD Vertical Non-Display Period Register*/
	{S1DREG_TFT_FPFRAME_START,	0x0a},	/* TFT FPFRAME Start Position Register*/
	{S1DREG_TFT_FPFRAME_PWIDTH,	0x02},	/* TFT FPFRAME Pulse Width Register*/
	{S1DREG_LCD_DISP_MODE,		0x05},	/* LCD Display Mode Register*/
	{S1DREG_LCD_MISC,		0x01},	/* LCD Miscellaneous Register*/
	{S1DREG_LCD_DISP_START0,	0x00},	/* LCD Display Start Address Register 0*/
	{S1DREG_LCD_DISP_START1,	0x00},	/* LCD Display Start Address Register 1*/
	{S1DREG_LCD_DISP_START2,	0x00},	/* LCD Display Start Address Register 2*/
	{S1DREG_LCD_MEM_OFF0,		0x80},	/* LCD Memory Address Offset Register 0*/
	{S1DREG_LCD_MEM_OFF1,		0x02},	/* LCD Memory Address Offset Register 1*/
	{S1DREG_LCD_PIX_PAN,		0x03},	/* LCD Pixel Panning Register*/
	{S1DREG_LCD_DISP_FIFO_HTC,	0x00},	/* LCD Display FIFO High Threshold Control Register*/
	{S1DREG_LCD_DISP_FIFO_LTC,	0x00},	/* LCD Display FIFO Low Threshold Control Register*/
	{S1DREG_CRT_DISP_HWIDTH,	0x4F},	/* CRT/TV Horizontal Display Width Register*/
	{S1DREG_CRT_NDISP_HPER,		0x13},	/* CRT/TV Horizontal Non-Display Period Register*/
	{S1DREG_CRT_HRTC_START,		0x01},	/* CRT/TV HRTC Start Position Register*/
	{S1DREG_CRT_HRTC_PWIDTH,	0x0B},	/* CRT/TV HRTC Pulse Width Register*/
	{S1DREG_CRT_DISP_VHEIGHT0,	0xDF},	/* CRT/TV Vertical Display Height Register 0*/
	{S1DREG_CRT_DISP_VHEIGHT1,	0x01},	/* CRT/TV Vertical Display Height Register 1*/
	{S1DREG_CRT_NDISP_VPER,		0x2B},	/* CRT/TV Vertical Non-Display Period Register*/
	{S1DREG_CRT_VRTC_START,		0x09},	/* CRT/TV VRTC Start Position Register*/
	{S1DREG_CRT_VRTC_PWIDTH,	0x01},	/* CRT/TV VRTC Pulse Width Register*/
	{S1DREG_TV_OUT_CTL,		0x18},	/* TV Output Control Register */
	{S1DREG_CRT_DISP_MODE,		0x05},	/* CRT/TV Display Mode Register, 16BPP*/
	{S1DREG_CRT_DISP_START0,	0x00},	/* CRT/TV Display Start Address Register 0*/
	{S1DREG_CRT_DISP_START1,	0x00},	/* CRT/TV Display Start Address Register 1*/
	{S1DREG_CRT_DISP_START2,	0x00},	/* CRT/TV Display Start Address Register 2*/
	{S1DREG_CRT_MEM_OFF0,		0x80},	/* CRT/TV Memory Address Offset Register 0*/
	{S1DREG_CRT_MEM_OFF1,		0x02},	/* CRT/TV Memory Address Offset Register 1*/
	{S1DREG_CRT_PIX_PAN,		0x00},	/* CRT/TV Pixel Panning Register*/
	{S1DREG_CRT_DISP_FIFO_HTC,	0x00},	/* CRT/TV Display FIFO High Threshold Control Register*/
	{S1DREG_CRT_DISP_FIFO_LTC,	0x00},	/* CRT/TV Display FIFO Low Threshold Control Register*/
	{S1DREG_LCD_CUR_CTL,		0x00},	/* LCD Ink/Cursor Control Register*/
	{S1DREG_LCD_CUR_START,		0x01},	/* LCD Ink/Cursor Start Address Register*/
	{S1DREG_LCD_CUR_XPOS0,		0x00},	/* LCD Cursor X Position Register 0*/
	{S1DREG_LCD_CUR_XPOS1,		0x00},	/* LCD Cursor X Position Register 1*/
	{S1DREG_LCD_CUR_YPOS0,		0x00},	/* LCD Cursor Y Position Register 0*/
	{S1DREG_LCD_CUR_YPOS1,		0x00},	/* LCD Cursor Y Position Register 1*/
	{S1DREG_LCD_CUR_BCTL0,		0x00},	/* LCD Ink/Cursor Blue Color 0 Register*/
	{S1DREG_LCD_CUR_GCTL0,		0x00},	/* LCD Ink/Cursor Green Color 0 Register*/
	{S1DREG_LCD_CUR_RCTL0,		0x00},	/* LCD Ink/Cursor Red Color 0 Register*/
	{S1DREG_LCD_CUR_BCTL1,		0x1F},	/* LCD Ink/Cursor Blue Color 1 Register*/
	{S1DREG_LCD_CUR_GCTL1,		0x3F},	/* LCD Ink/Cursor Green Color 1 Register*/
	{S1DREG_LCD_CUR_RCTL1,		0x1F},	/* LCD Ink/Cursor Red Color 1 Register*/
	{S1DREG_LCD_CUR_FIFO_HTC,	0x00},	/* LCD Ink/Cursor FIFO Threshold Register*/
	{S1DREG_CRT_CUR_CTL,		0x00},	/* CRT/TV Ink/Cursor Control Register*/
	{S1DREG_CRT_CUR_START,		0x01},	/* CRT/TV Ink/Cursor Start Address Register*/
	{S1DREG_CRT_CUR_XPOS0,		0x00},	/* CRT/TV Cursor X Position Register 0*/
	{S1DREG_CRT_CUR_XPOS1,		0x00},	/* CRT/TV Cursor X Position Register 1*/
	{S1DREG_CRT_CUR_YPOS0,		0x00},	/* CRT/TV Cursor Y Position Register 0*/
	{S1DREG_CRT_CUR_YPOS1,		0x00},	/* CRT/TV Cursor Y Position Register 1*/
	{S1DREG_CRT_CUR_BCTL0,		0x00},	/* CRT/TV Ink/Cursor Blue Color 0 Register*/
	{S1DREG_CRT_CUR_GCTL0,		0x00},	/* CRT/TV Ink/Cursor Green Color 0 Register*/
	{S1DREG_CRT_CUR_RCTL0,		0x00},	/* CRT/TV Ink/Cursor Red Color 0 Register*/
	{S1DREG_CRT_CUR_BCTL1,		0x1F},	/* CRT/TV Ink/Cursor Blue Color 1 Register*/
	{S1DREG_CRT_CUR_GCTL1,		0x3F},	/* CRT/TV Ink/Cursor Green Color 1 Register*/
	{S1DREG_CRT_CUR_RCTL1,		0x1F},	/* CRT/TV Ink/Cursor Red Color 1 Register*/
	{S1DREG_CRT_CUR_FIFO_HTC,	0x00},	/* CRT/TV Ink/Cursor FIFO Threshold Register*/
	{S1DREG_BBLT_CTL0,		0x00},	/* BitBlt Control Register 0*/
	{S1DREG_BBLT_CTL1,		0x01},	/* BitBlt Control Register 1*/
	{S1DREG_BBLT_CC_EXP,		0x00},	/* BitBlt ROP Code/Color Expansion Register*/
	{S1DREG_BBLT_OP,		0x00},	/* BitBlt Operation Register*/
	{S1DREG_BBLT_SRC_START0,	0x00},	/* BitBlt Source Start Address Register 0*/
	{S1DREG_BBLT_SRC_START1,	0x00},	/* BitBlt Source Start Address Register 1*/
	{S1DREG_BBLT_SRC_START2,	0x00},	/* BitBlt Source Start Address Register 2*/
	{S1DREG_BBLT_DST_START0,	0x00},	/* BitBlt Destination Start Address Register 0*/
	{S1DREG_BBLT_DST_START1,	0x00},	/* BitBlt Destination Start Address Register 1*/
	{S1DREG_BBLT_DST_START2,	0x00},	/* BitBlt Destination Start Address Register 2*/
	{S1DREG_BBLT_MEM_OFF0,		0x00},	/* BitBlt Memory Address Offset Register 0*/
	{S1DREG_BBLT_MEM_OFF1,		0x00},	/* BitBlt Memory Address Offset Register 1*/
	{S1DREG_BBLT_WIDTH0,		0x00},	/* BitBlt Width Register 0*/
	{S1DREG_BBLT_WIDTH1,		0x00},	/* BitBlt Width Register 1*/
	{S1DREG_BBLT_HEIGHT0,		0x00},	/* BitBlt Height Register 0*/
	{S1DREG_BBLT_HEIGHT1,		0x00},	/* BitBlt Height Register 1*/
	{S1DREG_BBLT_BGC0,		0x00},	/* BitBlt Background Color Register 0*/
	{S1DREG_BBLT_BGC1,		0x00},	/* BitBlt Background Color Register 1*/
	{S1DREG_BBLT_FGC0,		0x00},	/* BitBlt Foreground Color Register 0*/
	{S1DREG_BBLT_FGC1,		0x00},	/* BitBlt Foreground Color Register 1*/
	{S1DREG_LKUP_MODE,		0x00},	/* Look-Up Table Mode Register*/
	{S1DREG_LKUP_ADDR,		0x00},	/* Look-Up Table Address Register*/
	{S1DREG_PS_CNF,			0x00},	/* Power Save Configuration Register*/
	{S1DREG_PS_STATUS,		0x00},	/* Power Save Status Register*/
	{S1DREG_CPU2MEM_WDOGT,		0x00},	/* CPU-to-Memory Access Watchdog Timer Register*/
	{S1DREG_COM_DISP_MODE,		0x01},	/* Display Mode Register, LCD only*/
};

static struct s1d13xxxfb_pdata yl9200_s1dfb_pdata = {
	.initregs		= yl9200_s1dfb_initregs,
	.initregssize		= ARRAY_SIZE(yl9200_s1dfb_initregs),
	.platform_init_video	= yl9200_init_video,
};

#define YL9200_FB_REG_BASE	AT91_CHIPSELECT_7
#define YL9200_FB_VMEM_BASE	YL9200_FB_REG_BASE + SZ_2M
#define YL9200_FB_VMEM_SIZE	SZ_2M

static struct resource yl9200_s1dfb_resource[] = {
	[0] = {	/* video mem */
		.name	= "s1d13xxxfb memory",
		.start	= YL9200_FB_VMEM_BASE,
		.end	= YL9200_FB_VMEM_BASE + YL9200_FB_VMEM_SIZE -1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {	/* video registers */
		.name	= "s1d13xxxfb registers",
		.start	= YL9200_FB_REG_BASE,
		.end	= YL9200_FB_REG_BASE + SZ_512 -1,
		.flags	= IORESOURCE_MEM,
	},
};

static u64 s1dfb_dmamask = DMA_BIT_MASK(32);

static struct platform_device yl9200_s1dfb_device = {
	.name		= "s1d13806fb",
	.id		= -1,
	.dev	= {
		.dma_mask		= &s1dfb_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &yl9200_s1dfb_pdata,
	},
	.resource	= yl9200_s1dfb_resource,
	.num_resources	= ARRAY_SIZE(yl9200_s1dfb_resource),
};

void __init yl9200_add_device_video(void)
{
	platform_device_register(&yl9200_s1dfb_device);
}
#else
void __init yl9200_add_device_video(void) {}
#endif


static void __init yl9200_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&yl9200_eth_data);
	/* USB Host */
	at91_add_device_usbh(&yl9200_usbh_data);
	/* USB Device */
	at91_add_device_udc(&yl9200_udc_data);
	/* I2C */
	at91_add_device_i2c(yl9200_i2c_devices, ARRAY_SIZE(yl9200_i2c_devices));
	/* MMC */
	at91_add_device_mmc(0, &yl9200_mmc_data);
	/* NAND */
	at91_add_device_nand(&yl9200_nand_data);
	/* NOR Flash */
	platform_device_register(&yl9200_flash);
#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
	/* SPI */
	at91_add_device_spi(yl9200_spi_devices, ARRAY_SIZE(yl9200_spi_devices));
	/* Touchscreen */
	yl9200_add_device_ts();
#endif
	/* LEDs. */
	at91_gpio_leds(yl9200_leds, ARRAY_SIZE(yl9200_leds));
	/* Push Buttons */
	yl9200_add_device_buttons();
	/* VGA */
	yl9200_add_device_video();
}

MACHINE_START(YL9200, "uCdragon YL-9200")
	/* Maintainer: S.Birtles */
	.timer		= &at91rm9200_timer,
	.map_io		= at91_map_io,
	.init_early	= yl9200_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= yl9200_board_init,
MACHINE_END
