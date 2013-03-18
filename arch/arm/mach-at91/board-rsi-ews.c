/*
 * board-rsi-ews.c
 *
 *  Copyright (C)
 *  2005 SAN People,
 *  2008-2011 R-S-I Elektrotechnik GmbH & Co. KG
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/mtd/physmap.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>

#include <linux/gpio.h>

#include "at91_aic.h"
#include "board.h"
#include "generic.h"

static void __init rsi_ews_init_early(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91_initialize(18432000);
}

/*
 * Ethernet
 */
static struct macb_platform_data rsi_ews_eth_data __initdata = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

/*
 * USB Host
 */
static struct at91_usbh_data rsi_ews_usbh_data __initdata = {
	.ports		= 1,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

/*
 * SD/MC
 */
static struct mci_platform_data __initdata rsi_ews_mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= AT91_PIN_PB27,
		.wp_pin		= AT91_PIN_PB29,
	},
};

/*
 * I2C
 */
static struct i2c_board_info rsi_ews_i2c_devices[] __initdata = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
	},
	{
		I2C_BOARD_INFO("24c01", 0x50),
	}
};

/*
 * LEDs
 */
static struct gpio_led rsi_ews_leds[] = {
	{
		.name			= "led0",
		.gpio			= AT91_PIN_PB6,
		.active_low		= 0,
	},
	{
		.name			= "led1",
		.gpio			= AT91_PIN_PB7,
		.active_low		= 0,
	},
	{
		.name			= "led2",
		.gpio			= AT91_PIN_PB8,
		.active_low		= 0,
	},
	{
		.name			= "led3",
		.gpio			= AT91_PIN_PB9,
		.active_low		= 0,
	},
};

/*
 * DataFlash
 */
static struct spi_board_info rsi_ews_spi_devices[] = {
	{	/* DataFlash chip 1*/
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 5 * 1000 * 1000,
	},
	{	/* DataFlash chip 2*/
		.modalias	= "mtd_dataflash",
		.chip_select	= 1,
		.max_speed_hz	= 5 * 1000 * 1000,
	},
};

/*
 * NOR flash
 */
static struct mtd_partition rsiews_nor_partitions[] = {
	{
		.name		= "boot",
		.offset		= 0,
		.size		= 3 * SZ_128K,
		.mask_flags	= MTD_WRITEABLE
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= SZ_2M - (3 * SZ_128K)
	},
	{
		.name		= "root",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= SZ_8M
	},
	{
		.name		= "kernelupd",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= 3 * SZ_512K,
		.mask_flags	= MTD_WRITEABLE
	},
	{
		.name		= "rootupd",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= 9 * SZ_512K,
		.mask_flags	= MTD_WRITEABLE
	},
};

static struct physmap_flash_data rsiews_nor_data = {
	.width		= 2,
	.parts		= rsiews_nor_partitions,
	.nr_parts	= ARRAY_SIZE(rsiews_nor_partitions),
};

#define NOR_BASE	AT91_CHIPSELECT_0
#define NOR_SIZE	SZ_16M

static struct resource nor_flash_resources[] = {
	{
		.start	= NOR_BASE,
		.end	= NOR_BASE + NOR_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device rsiews_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &rsiews_nor_data,
	},
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
};

/*
 * Init Func
 */
static void __init rsi_ews_board_init(void)
{
	/* Serial */
	/* DBGU on ttyS0. (Rx & Tx only) */
	/* This one is for debugging */
	at91_register_uart(0, 0, 0);

	/* USART1 on ttyS2. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	/* Dialin/-out modem interface */
	at91_register_uart(AT91RM9200_ID_US1, 2, ATMEL_UART_CTS | ATMEL_UART_RTS
			   | ATMEL_UART_DTR | ATMEL_UART_DSR | ATMEL_UART_DCD
			   | ATMEL_UART_RI);

	/* USART3 on ttyS4. (Rx, Tx, RTS) */
	/* RS485 communication */
	at91_register_uart(AT91RM9200_ID_US3, 4, ATMEL_UART_RTS);
	at91_add_device_serial();
	at91_set_gpio_output(AT91_PIN_PA21, 0);
	/* Ethernet */
	at91_add_device_eth(&rsi_ews_eth_data);
	/* USB Host */
	at91_add_device_usbh(&rsi_ews_usbh_data);
	/* I2C */
	at91_add_device_i2c(rsi_ews_i2c_devices,
			ARRAY_SIZE(rsi_ews_i2c_devices));
	/* SPI */
	at91_add_device_spi(rsi_ews_spi_devices,
			ARRAY_SIZE(rsi_ews_spi_devices));
	/* MMC */
	at91_add_device_mci(0, &rsi_ews_mci0_data);
	/* NOR Flash */
	platform_device_register(&rsiews_nor_flash);
	/* LEDs */
	at91_gpio_leds(rsi_ews_leds, ARRAY_SIZE(rsi_ews_leds));
}

MACHINE_START(RSI_EWS, "RSI EWS")
	/* Maintainer: Josef Holzmayr <holzmayr@rsi-elektrotechnik.de> */
	.init_time	= at91rm9200_timer_init,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= rsi_ews_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= rsi_ews_board_init,
MACHINE_END
