/*
 * linux/arch/arm/mach-at91/board-yl-9200.c
 *
 * Adapted from:
 *various board files in
 * /arch/arm/mach-at91
 * modifications  to convert to  YL-9200 platform
 *  Copyright (C) 2007 S.Birtles
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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
/*#include <linux/can_bus/candata.h>*/
#include <linux/spi/ads7846.h>
#include <linux/mtd/physmap.h>

/*#include <sound/gpio_sounder.h>*/
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/at91rm9200_mc.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include "generic.h"
#include <asm/arch/at91_pio.h>

#define YL_9200_FLASH_BASE	AT91_CHIPSELECT_0
#define YL_9200_FLASH_SIZE	0x800000

/*
 * Serial port configuration.
 *    0 .. 3 = USART0 .. USART3
 *    4      = DBGU
 *atmel_usart.0: ttyS0 at MMIO 0xfefff200 (irq = 1) is a ATMEL_SERIAL
 *atmel_usart.1: ttyS1 at MMIO 0xfffc0000 (irq = 6) is a ATMEL_SERIAL
 *atmel_usart.2: ttyS2 at MMIO 0xfffc4000 (irq = 7) is a ATMEL_SERIAL
 *atmel_usart.3: ttyS3 at MMIO 0xfffc8000 (irq = 8) is a ATMEL_SERIAL
 *atmel_usart.4: ttyS4 at MMIO 0xfffcc000 (irq = 9) is a ATMEL_SERIAL
 * on the YL-9200 we are sitting at the following
 *ttyS0 at MMIO 0xfefff200 (irq = 1) is a AT91_SERIAL
 *ttyS1 at MMIO 0xfefc4000 (irq = 7) is a AT91_SERIAL
 */

/* extern void __init yl_9200_add_device_sounder(struct gpio_sounder *sounders, int nr);*/

static struct at91_uart_config __initdata yl_9200_uart_config = {
	.console_tty	= 0,				/* ttyS0 */
	.nr_tty		= 3,
	.tty_map	= { 4, 1, 0, -1, -1 }		/* ttyS0, ..., ttyS4 */
};

static void __init yl_9200_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	/*Also initialises register clocks & gpio*/
	at91rm9200_initialize(18432000, AT91RM9200_PQFP); /*we have a 3 bank system*/

	/* Setup the serial ports and console */
	at91_init_serial(&yl_9200_uart_config);

	/* Setup the LEDs D2=PB17,D3=PB16 */
	at91_init_leds(AT91_PIN_PB16,AT91_PIN_PB17); /*cpu-led,timer-led*/
}

static void __init yl_9200_init_irq(void)
{
	at91rm9200_init_interrupts(NULL);
}

static struct at91_eth_data __initdata yl_9200_eth_data = {
	.phy_irq_pin	= AT91_PIN_PB28,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata yl_9200_usbh_data = {
	.ports		= 1,  /* this should be 1 not 2 for the Yl9200*/
};

static struct at91_udc_data __initdata yl_9200_udc_data = {
/*on sheet 7 Schemitic rev 1.0*/
	.pullup_pin	= AT91_PIN_PC4,
	.vbus_pin=  AT91_PIN_PC5,
	.pullup_active_low = 1, /*ACTIVE LOW!! due to PNP transistor on page 7*/

};
/*
static struct at91_cf_data __initdata yl_9200_cf_data = {
TODO S.BIRTLES
	.det_pin	= AT91_PIN_xxx,
	.rst_pin	= AT91_PIN_xxx,
	.irq_pin	= ... not connected
	.vcc_pin	= ... always powered

};
*/
static struct at91_mmc_data __initdata yl_9200_mmc_data = {
	.det_pin	= AT91_PIN_PB9, /*THIS LOOKS CORRECT SHEET7*/
/*	.wp_pin		= ... not connected  SHEET7*/
	.slot_b		= 0,
	.wire4		= 1,

};

/* --------------------------------------------------------------------
 *  Touch screen
 * -------------------------------------------------------------------- */
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
static int ads7843_pendown_state(void)
{
	return !at91_get_gpio_value(AT91_PIN_PB11);	/* Touchscreen PENIRQ */
}

static void __init at91_init_device_ts(void)
{
/*IMPORTANT NOTE THE SPI INTERFACE IS ALREADY CONFIGURED BY XXX_DEVICES.C
THAT IS TO SAY THAT  MISO,MOSI,SPCK AND CS  are already configured
we only need to enable the other datapins which are:
PB10/RK1 BUSY
*/
/* Touchscreen BUSY signal ,  pin,use pullup ( TODO not currently used in the ADS7843/6.c driver)*/
at91_set_gpio_input(AT91_PIN_PB10, 1);
}

#else
static void __init at91_init_device_ts(void) {}
#endif

static struct ads7846_platform_data ads_info = {
	.model			= 7843,
	.x_min			= 150,
	.x_max			= 3830,
	.y_min			= 190,
	.y_max			= 3830,
	.vref_delay_usecs	= 100,
/* for a 8" touch screen*/
	//.x_plate_ohms		= 603, //= 450, S.Birtles TODO
	//.y_plate_ohms		= 332, //= 250, S.Birtles TODO
/*for a 10.4" touch screen*/
	//.x_plate_ohms		=611,
	//.y_plate_ohms		=325,

	.x_plate_ohms	= 576,
	.y_plate_ohms	= 366,
		//
	.pressure_max		= 15000, /*generally nonsense on the 7843*/
	 /*number of times to send query to chip in a given run 0 equals one time (do not set to 0!! ,there is a bug in ADS 7846 code)*/
	.debounce_max		= 1,
	.debounce_rep		= 0,
	.debounce_tol		= (~0),
	.get_pendown_state	= ads7843_pendown_state,
};

/*static struct canbus_platform_data can_info = {
	.model			= 2510,
};
*/

static struct spi_board_info yl_9200_spi_devices[] = {
/*this sticks it at:
 /sys/devices/platform/atmel_spi.0/spi0.0
 /sys/bus/platform/devices/
Documentation/spi IIRC*/

#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
 /*(this IS correct 04-NOV-2007)*/
	{
		.modalias		= "ads7846", /* because the driver is called ads7846*/
		.chip_select	= 0, /*THIS MUST BE AN INDEX INTO AN ARRAY OF  pins */
/*this is ONLY TO BE USED if chipselect above is not used, it passes a pin directly for the chip select*/
		/*.controller_data =AT91_PIN_PA3 ,*/
		.max_speed_hz	= 5000*26, /*(4700 * 26)-125000 * 26, (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num		= 0,
		.platform_data	= &ads_info,
		.irq			= AT91_PIN_PB11,
	},
#endif
/*we need to put our CAN driver data here!!*/
/*THIS IS ALL DUMMY DATA*/
/*	{
		.modalias		= "mcp2510", //DUMMY for MCP2510 chip
		.chip_select	= 1,*/ /*THIS MUST BE AN INDEX INTO AN ARRAY OF  pins */
	/*this is ONLY TO BE USED if chipselect above is not used, it passes a pin directly for the chip select */
	/*  .controller_data =AT91_PIN_PA4 ,
		.max_speed_hz	= 25000 * 26,
		.bus_num		= 0,
		.platform_data	= &can_info,
		.irq			= AT91_PIN_PC0,
	},
	*/
	//max SPI chip needs to go here
};

static struct mtd_partition __initdata yl_9200_nand_partition[] = {
	{
		.name	= "AT91 NAND partition 1, boot",
		.offset	= 0,
		.size	= 1 * SZ_256K
	},
	{
		.name	= "AT91 NAND partition 2, kernel",
		.offset	= 1 * SZ_256K,
		.size	= 2 * SZ_1M - 1 * SZ_256K
	},
	{
		.name	= "AT91 NAND partition 3, filesystem",
		.offset	= 2 * SZ_1M,
		.size	= 14 * SZ_1M
	},
	{
		.name	= "AT91 NAND partition 4, storage",
		.offset	= 16 * SZ_1M,
		.size	= 16 * SZ_1M
	},
	{
		.name	= "AT91 NAND partition 5, ext-fs",
		.offset	= 32 * SZ_1M,
		.size	= 32 * SZ_1M
	},
};

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(yl_9200_nand_partition);
	return yl_9200_nand_partition;
}

static struct at91_nand_data __initdata yl_9200_nand_data = {
	.ale= 6,
	.cle= 7,
	/*.det_pin	= AT91_PIN_PCxx,*/   /*we don't have a det pin because NandFlash is fixed to board*/
	.rdy_pin	= AT91_PIN_PC14,  /*R/!B Sheet10*/
	.enable_pin	= AT91_PIN_PC15,  /*!CE  Sheet10 */
	.partition_info	= nand_partitions,
};



/*
TODO S.Birtles
potentially a problem with the size above
physmap platform flash device: 00800000 at 10000000
physmap-flash.0: Found 1 x16 devices at 0x0 in 16-bit bank
NOR chip too large to fit in mapping. Attempting to cope...
 Intel/Sharp Extended Query Table at 0x0031
Using buffer write method
cfi_cmdset_0001: Erase suspend on write enabled
Reducing visibility of 16384KiB chip to 8192KiB
*/

static struct mtd_partition yl_9200_flash_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x001C0000,
		.offset =	0x00040000,
	},{
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00200000
	}

};

static struct physmap_flash_data yl_9200_flash_data = {
	.width	= 2,
	.parts          = yl_9200_flash_partitions,
	.nr_parts       = ARRAY_SIZE(yl_9200_flash_partitions),
};

static struct resource yl_9200_flash_resources[] = {
{
	.start		= YL_9200_FLASH_BASE,
	.end		= YL_9200_FLASH_BASE + YL_9200_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
	}
};

static struct platform_device yl_9200_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &yl_9200_flash_data,
			},
	.resource	= yl_9200_flash_resources,
	.num_resources  = ARRAY_SIZE(yl_9200_flash_resources),
};


static struct gpio_led yl_9200_leds[] = {
/*D2 &D3 are passed directly in via at91_init_leds*/
	{
		.name			= "led4",  /*D4*/
		.gpio			= AT91_PIN_PB15,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
		/*.default_trigger	= "timer",*/
	},
	{
		.name			= "led5",  /*D5*/
		.gpio			= AT91_PIN_PB8,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	}
};

//static struct gpio_sounder yl_9200_sounder[] = {*/
/*This is a simple speaker attached to a gpo line*/

//	{
//		.name			= "Speaker",  /*LS1*/
//		.gpio			= AT91_PIN_PA22,
//		.active_low		= 0,
//		.default_trigger	= "heartbeat",
		/*.default_trigger	= "timer",*/
//	},
//};



static struct i2c_board_info __initdata yl_9200_i2c_devices[] = {
	{
	/*TODO*/
		I2C_BOARD_INFO("CS4334", 0x00),
	}
};


 /*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button yl_9200_buttons[] = {
	{
		.gpio		= AT91_PIN_PA24,
		.code	= BTN_2,
		.desc		= "SW2",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB1,
		.code	= BTN_3,
		.desc		= "SW3",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB2,
		.code	= BTN_4,
		.desc		= "SW4",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB6,
		.code	= BTN_5,
		.desc		= "SW5",
		.active_low	= 1,
		.wakeup		= 1,
	},

};

static struct gpio_keys_platform_data yl_9200_button_data = {
	.buttons	= yl_9200_buttons,
	.nbuttons	= ARRAY_SIZE(yl_9200_buttons),
};

static struct platform_device yl_9200_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
	.platform_data	= &yl_9200_button_data,
	}
};

static void __init yl_9200_add_device_buttons(void)
{
	//SW2
	at91_set_gpio_input(AT91_PIN_PA24, 0);
	at91_set_deglitch(AT91_PIN_PA24, 1);

	//SW3
	at91_set_gpio_input(AT91_PIN_PB1, 0);
	at91_set_deglitch(AT91_PIN_PB1, 1);
	//SW4
	at91_set_gpio_input(AT91_PIN_PB2, 0);
	at91_set_deglitch(AT91_PIN_PB2, 1);

	//SW5
	at91_set_gpio_input(AT91_PIN_PB6, 0);
	at91_set_deglitch(AT91_PIN_PB6, 1);


	at91_set_gpio_output(AT91_PIN_PB7, 1);	/* #TURN BUTTONS ON, SHEET 5  of schematics */
	platform_device_register(&yl_9200_button_device);
}
#else
static void __init yl_9200_add_device_buttons(void) {}
#endif

#if defined(CONFIG_FB_S1D135XX) || defined(CONFIG_FB_S1D13XXX_MODULE)
#include <video/s1d13xxxfb.h>

/* EPSON S1D13806 FB (discontinued chip)*/
/* EPSON S1D13506 FB */

#define AT91_FB_REG_BASE	0x80000000L
#define AT91_FB_REG_SIZE	0x200
#define AT91_FB_VMEM_BASE	0x80200000L
#define AT91_FB_VMEM_SIZE	0x200000L

/*#define S1D_DISPLAY_WIDTH           640*/
/*#define S1D_DISPLAY_HEIGHT          480*/


static void __init yl_9200_init_video(void)
{
	at91_sys_write(AT91_PIOC + PIO_ASR,AT91_PIN_PC6);
	at91_sys_write(AT91_PIOC + PIO_BSR,0);
	at91_sys_write(AT91_PIOC + PIO_ASR,AT91_PIN_PC6);

	at91_sys_write( AT91_SMC_CSR(2),
	AT91_SMC_NWS_(0x4) |
	AT91_SMC_WSEN |
	AT91_SMC_TDF_(0x100) |
	AT91_SMC_DBW
	);



}


static struct s1d13xxxfb_regval yl_9200_s1dfb_initregs[] =
{
	{S1DREG_MISC,				0x00},   /* Miscellaneous Register*/
	{S1DREG_COM_DISP_MODE,		0x01},   /* Display Mode Register, LCD only*/
	{S1DREG_GPIO_CNF0,			0x00},   /* General IO Pins Configuration Register*/
	{S1DREG_GPIO_CTL0,			0x00},   /* General IO Pins Control Register*/
	{S1DREG_CLK_CNF,			0x11},   /* Memory Clock Configuration Register*/
	{S1DREG_LCD_CLK_CNF,		0x10},   /* LCD Pixel Clock Configuration Register*/
	{S1DREG_CRT_CLK_CNF,		0x12},   /* CRT/TV Pixel Clock Configuration Register*/
	{S1DREG_MPLUG_CLK_CNF,		0x01},   /* MediaPlug Clock Configuration Register*/
	{S1DREG_CPU2MEM_WST_SEL,	0x02},   /* CPU To Memory Wait State Select Register*/
	{S1DREG_MEM_CNF,			0x00},   /* Memory Configuration Register*/
	{S1DREG_SDRAM_REF_RATE,		0x04},   /* DRAM Refresh Rate Register, MCLK source*/
	{S1DREG_SDRAM_TC0,			0x12},   /* DRAM Timings Control Register 0*/
	{S1DREG_SDRAM_TC1,			0x02},   /* DRAM Timings Control Register 1*/
	{S1DREG_PANEL_TYPE,			0x25},   /* Panel Type Register*/
	{S1DREG_MOD_RATE,			0x00},   /* MOD Rate Register*/
	{S1DREG_LCD_DISP_HWIDTH,	0x4F},   /* LCD Horizontal Display Width Register*/
	{S1DREG_LCD_NDISP_HPER,		0x13},   /* LCD Horizontal Non-Display Period Register*/
	{S1DREG_TFT_FPLINE_START,	0x01},   /* TFT FPLINE Start Position Register*/
	{S1DREG_TFT_FPLINE_PWIDTH,	0x0c},   /* TFT FPLINE Pulse Width Register*/
	{S1DREG_LCD_DISP_VHEIGHT0,	0xDF},   /* LCD Vertical Display Height Register 0*/
	{S1DREG_LCD_DISP_VHEIGHT1,	0x01},   /* LCD Vertical Display Height Register 1*/
	{S1DREG_LCD_NDISP_VPER,		0x2c},   /* LCD Vertical Non-Display Period Register*/
	{S1DREG_TFT_FPFRAME_START,	0x0a},   /* TFT FPFRAME Start Position Register*/
	{S1DREG_TFT_FPFRAME_PWIDTH,	0x02},   /* TFT FPFRAME Pulse Width Register*/
	{S1DREG_LCD_DISP_MODE,		0x05},   /* LCD Display Mode Register*/
	{S1DREG_LCD_MISC,			0x01},   /* LCD Miscellaneous Register*/
	{S1DREG_LCD_DISP_START0,	0x00},   /* LCD Display Start Address Register 0*/
	{S1DREG_LCD_DISP_START1,	0x00},   /* LCD Display Start Address Register 1*/
	{S1DREG_LCD_DISP_START2,	0x00},   /* LCD Display Start Address Register 2*/
	{S1DREG_LCD_MEM_OFF0,		0x80},   /* LCD Memory Address Offset Register 0*/
	{S1DREG_LCD_MEM_OFF1,		0x02},   /* LCD Memory Address Offset Register 1*/
	{S1DREG_LCD_PIX_PAN,		0x03},   /* LCD Pixel Panning Register*/
	{S1DREG_LCD_DISP_FIFO_HTC,	0x00},   /* LCD Display FIFO High Threshold Control Register*/
	{S1DREG_LCD_DISP_FIFO_LTC,	0x00},   /* LCD Display FIFO Low Threshold Control Register*/
	{S1DREG_CRT_DISP_HWIDTH,	0x4F},   /* CRT/TV Horizontal Display Width Register*/
	{S1DREG_CRT_NDISP_HPER,		0x13},   /* CRT/TV Horizontal Non-Display Period Register*/
	{S1DREG_CRT_HRTC_START,		0x01},   /* CRT/TV HRTC Start Position Register*/
	{S1DREG_CRT_HRTC_PWIDTH,	0x0B},   /* CRT/TV HRTC Pulse Width Register*/
	{S1DREG_CRT_DISP_VHEIGHT0,	0xDF},   /* CRT/TV Vertical Display Height Register 0*/
	{S1DREG_CRT_DISP_VHEIGHT1,	0x01},   /* CRT/TV Vertical Display Height Register 1*/
	{S1DREG_CRT_NDISP_VPER,		0x2B},   /* CRT/TV Vertical Non-Display Period Register*/
	{S1DREG_CRT_VRTC_START,		0x09},   /* CRT/TV VRTC Start Position Register*/
	{S1DREG_CRT_VRTC_PWIDTH,	0x01},   /* CRT/TV VRTC Pulse Width Register*/
	{S1DREG_TV_OUT_CTL,			0x18},   /* TV Output Control Register */
	{S1DREG_CRT_DISP_MODE,		0x05},   /* CRT/TV Display Mode Register, 16BPP*/
	{S1DREG_CRT_DISP_START0,	0x00},   /* CRT/TV Display Start Address Register 0*/
	{S1DREG_CRT_DISP_START1,	0x00},   /* CRT/TV Display Start Address Register 1*/
	{S1DREG_CRT_DISP_START2,	0x00},   /* CRT/TV Display Start Address Register 2*/
	{S1DREG_CRT_MEM_OFF0,		0x80},   /* CRT/TV Memory Address Offset Register 0*/
	{S1DREG_CRT_MEM_OFF1,		0x02},   /* CRT/TV Memory Address Offset Register 1*/
	{S1DREG_CRT_PIX_PAN,		0x00},   /* CRT/TV Pixel Panning Register*/
	{S1DREG_CRT_DISP_FIFO_HTC,	0x00},   /* CRT/TV Display FIFO High Threshold Control Register*/
	{S1DREG_CRT_DISP_FIFO_LTC,	0x00},   /* CRT/TV Display FIFO Low Threshold Control Register*/
	{S1DREG_LCD_CUR_CTL,		0x00},   /* LCD Ink/Cursor Control Register*/
	{S1DREG_LCD_CUR_START,		0x01},   /* LCD Ink/Cursor Start Address Register*/
	{S1DREG_LCD_CUR_XPOS0,		0x00},   /* LCD Cursor X Position Register 0*/
	{S1DREG_LCD_CUR_XPOS1,		0x00},   /* LCD Cursor X Position Register 1*/
	{S1DREG_LCD_CUR_YPOS0,		0x00},   /* LCD Cursor Y Position Register 0*/
	{S1DREG_LCD_CUR_YPOS1,		0x00},   /* LCD Cursor Y Position Register 1*/
	{S1DREG_LCD_CUR_BCTL0,		0x00},   /* LCD Ink/Cursor Blue Color 0 Register*/
	{S1DREG_LCD_CUR_GCTL0,		0x00},   /* LCD Ink/Cursor Green Color 0 Register*/
	{S1DREG_LCD_CUR_RCTL0,		0x00},   /* LCD Ink/Cursor Red Color 0 Register*/
	{S1DREG_LCD_CUR_BCTL1,		0x1F},   /* LCD Ink/Cursor Blue Color 1 Register*/
	{S1DREG_LCD_CUR_GCTL1,		0x3F},   /* LCD Ink/Cursor Green Color 1 Register*/
	{S1DREG_LCD_CUR_RCTL1,		0x1F},   /* LCD Ink/Cursor Red Color 1 Register*/
	{S1DREG_LCD_CUR_FIFO_HTC,	0x00},   /* LCD Ink/Cursor FIFO Threshold Register*/
	{S1DREG_CRT_CUR_CTL,		0x00},   /* CRT/TV Ink/Cursor Control Register*/
	{S1DREG_CRT_CUR_START,		0x01},   /* CRT/TV Ink/Cursor Start Address Register*/
	{S1DREG_CRT_CUR_XPOS0,		0x00},   /* CRT/TV Cursor X Position Register 0*/
	{S1DREG_CRT_CUR_XPOS1,		0x00},   /* CRT/TV Cursor X Position Register 1*/
	{S1DREG_CRT_CUR_YPOS0,		0x00},   /* CRT/TV Cursor Y Position Register 0*/
	{S1DREG_CRT_CUR_YPOS1,		0x00},   /* CRT/TV Cursor Y Position Register 1*/
	{S1DREG_CRT_CUR_BCTL0,		0x00},   /* CRT/TV Ink/Cursor Blue Color 0 Register*/
	{S1DREG_CRT_CUR_GCTL0,		0x00},   /* CRT/TV Ink/Cursor Green Color 0 Register*/
	{S1DREG_CRT_CUR_RCTL0,		0x00},   /* CRT/TV Ink/Cursor Red Color 0 Register*/
	{S1DREG_CRT_CUR_BCTL1,		0x1F},   /* CRT/TV Ink/Cursor Blue Color 1 Register*/
	{S1DREG_CRT_CUR_GCTL1,		0x3F},   /* CRT/TV Ink/Cursor Green Color 1 Register*/
	{S1DREG_CRT_CUR_RCTL1,		0x1F},   /* CRT/TV Ink/Cursor Red Color 1 Register*/
	{S1DREG_CRT_CUR_FIFO_HTC,	0x00},   /* CRT/TV Ink/Cursor FIFO Threshold Register*/
	{S1DREG_BBLT_CTL0,			0x00},   /* BitBlt Control Register 0*/
	{S1DREG_BBLT_CTL1,			0x01},   /* BitBlt Control Register 1*/
	{S1DREG_BBLT_CC_EXP,		0x00},   /* BitBlt ROP Code/Color Expansion Register*/
	{S1DREG_BBLT_OP,			0x00},   /* BitBlt Operation Register*/
	{S1DREG_BBLT_SRC_START0,	0x00},   /* BitBlt Source Start Address Register 0*/
	{S1DREG_BBLT_SRC_START1,	0x00},   /* BitBlt Source Start Address Register 1*/
	{S1DREG_BBLT_SRC_START2,	0x00},   /* BitBlt Source Start Address Register 2*/
	{S1DREG_BBLT_DST_START0,	0x00},   /* BitBlt Destination Start Address Register 0*/
	{S1DREG_BBLT_DST_START1,	0x00},   /* BitBlt Destination Start Address Register 1*/
	{S1DREG_BBLT_DST_START2,	0x00},   /* BitBlt Destination Start Address Register 2*/
	{S1DREG_BBLT_MEM_OFF0,		0x00},   /* BitBlt Memory Address Offset Register 0*/
	{S1DREG_BBLT_MEM_OFF1,		0x00},   /* BitBlt Memory Address Offset Register 1*/
	{S1DREG_BBLT_WIDTH0,		0x00},   /* BitBlt Width Register 0*/
	{S1DREG_BBLT_WIDTH1,		0x00},   /* BitBlt Width Register 1*/
	{S1DREG_BBLT_HEIGHT0,		0x00},   /* BitBlt Height Register 0*/
	{S1DREG_BBLT_HEIGHT1,		0x00},   /* BitBlt Height Register 1*/
	{S1DREG_BBLT_BGC0,			0x00},   /* BitBlt Background Color Register 0*/
	{S1DREG_BBLT_BGC1,			0x00},   /* BitBlt Background Color Register 1*/
	{S1DREG_BBLT_FGC0,			0x00},   /* BitBlt Foreground Color Register 0*/
	{S1DREG_BBLT_FGC1,			0x00},   /* BitBlt Foreground Color Register 1*/
	{S1DREG_LKUP_MODE,			0x00},   /* Look-Up Table Mode Register*/
	{S1DREG_LKUP_ADDR,			0x00},   /* Look-Up Table Address Register*/
	{S1DREG_PS_CNF,				0x00},   /* Power Save Configuration Register*/
	{S1DREG_PS_STATUS,			0x00},   /* Power Save Status Register*/
	{S1DREG_CPU2MEM_WDOGT,		0x00},   /* CPU-to-Memory Access Watchdog Timer Register*/
	{S1DREG_COM_DISP_MODE,		0x01},   /* Display Mode Register, LCD only*/
};

static u64 s1dfb_dmamask = 0xffffffffUL;

static struct s1d13xxxfb_pdata yl_9200_s1dfb_pdata = {
		.initregs				= yl_9200_s1dfb_initregs,
		.initregssize			= ARRAY_SIZE(yl_9200_s1dfb_initregs),
		.platform_init_video	= yl_9200_init_video,
};

static struct resource yl_9200_s1dfb_resource[] = {
	[0] = {	/* video mem */
		.name   = "s1d13xxxfb memory",
	/*	.name   = "s1d13806 memory",*/
		.start  = AT91_FB_VMEM_BASE,
		.end    = AT91_FB_VMEM_BASE + AT91_FB_VMEM_SIZE -1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {	/* video registers */
		.name   = "s1d13xxxfb registers",
	/*	.name   = "s1d13806 registers",*/
		.start  = AT91_FB_REG_BASE,
		.end    = AT91_FB_REG_BASE + AT91_FB_REG_SIZE -1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device yl_9200_s1dfb_device = {
	/*TODO S.Birtles , really we need the chip revision in here as well*/
		.name		= "s1d13806fb",
	/*  .name		= "s1d13506fb",*/
		.id			= -1,
		.dev		= {
	/*TODO theres a waring here!!*/
	/*WARNING: vmlinux.o(.data+0x2dbc): Section mismatch: reference to .init.text: (between 'yl_9200_s1dfb_pdata' and 's1dfb_dmamask')*/
		.dma_mask		= &s1dfb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &yl_9200_s1dfb_pdata,
	},
	.resource	= yl_9200_s1dfb_resource,
	.num_resources	= ARRAY_SIZE(yl_9200_s1dfb_resource),
};

void __init yl_9200_add_device_video(void)
{
	platform_device_register(&yl_9200_s1dfb_device);
}
#else
	void __init yl_9200_add_device_video(void) {}
#endif

/*this is not called first , yl_9200_map_io is called first*/
static void __init yl_9200_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&yl_9200_eth_data);
	/* USB Host */
	at91_add_device_usbh(&yl_9200_usbh_data);
	/* USB Device */
	at91_add_device_udc(&yl_9200_udc_data);
	/* pullup_pin it is  actually active low, but this is not needed, driver sets it up */
	/*at91_set_multi_drive(yl_9200_udc_data.pullup_pin, 0);*/

	/* Compact Flash */
	/*at91_add_device_cf(&yl_9200_cf_data);*/

	/* I2C */
	at91_add_device_i2c(yl_9200_i2c_devices, ARRAY_SIZE(yl_9200_i2c_devices));
	/* SPI */
	/*TODO YL9200 we have 2 spi interfaces touch screen & CAN*/
	/* AT91_PIN_PA5, AT91_PIN_PA6 , are used on the  max 485 NOT SPI*/

	/*touch screen and CAN*/
	at91_add_device_spi(yl_9200_spi_devices, ARRAY_SIZE(yl_9200_spi_devices));

	/*Basically the  TS uses  PB11 & PB10 , PB11 is configured by the SPI system BP10 IS NOT USED!!*/
	/* we need this incase the board is running without a touch screen*/
	#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	at91_init_device_ts(); /*init the touch screen device*/
	#endif
	/* DataFlash card */
	at91_add_device_mmc(0, &yl_9200_mmc_data);
	/* NAND */
	at91_add_device_nand(&yl_9200_nand_data);
	/* NOR Flash */
	platform_device_register(&yl_9200_flash);
	/* LEDs. Note!! this does not include the led's we passed for the processor status */
	at91_gpio_leds(yl_9200_leds, ARRAY_SIZE(yl_9200_leds));
	/* VGA  */
	/*this is self registered by including the s1d13xxx chip in the kernel build*/
	yl_9200_add_device_video();
	/* Push Buttons */
	yl_9200_add_device_buttons();
	/*TODO fixup the Sounder */
//	yl_9200_add_device_sounder(yl_9200_sounder,ARRAY_SIZE(yl_9200_sounder));

}

MACHINE_START(YL9200, "uCdragon YL-9200")
	/* Maintainer: S.Birtles*/
	.phys_io		= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer			= &at91rm9200_timer,
	.map_io			= yl_9200_map_io,
	.init_irq		= yl_9200_init_irq,
	.init_machine	= yl_9200_board_init,
MACHINE_END
