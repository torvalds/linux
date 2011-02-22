/*
 * linux/arch/arm/mach-at91/board-rm9200dk.c
 *
 *  Copyright (C) 2005 SAN People
 *
 *  Epson S1D framebuffer glue code is:
 *     Copyright (C) 2005 Thibaut VARENE <varenet@parisc-linux.org>
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
#include <linux/mtd/physmap.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at91rm9200_mc.h>

#include "generic.h"


static void __init dk_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91rm9200_initialize(18432000, AT91RM9200_BGA);

	/* Setup the LEDs */
	at91_init_leds(AT91_PIN_PB2, AT91_PIN_PB2);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART1 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91RM9200_ID_US1, 1, ATMEL_UART_CTS | ATMEL_UART_RTS
			   | ATMEL_UART_DTR | ATMEL_UART_DSR | ATMEL_UART_DCD
			   | ATMEL_UART_RI);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init dk_init_irq(void)
{
	at91rm9200_init_interrupts(NULL);
}

static struct at91_eth_data __initdata dk_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata dk_usbh_data = {
	.ports		= 2,
};

static struct at91_udc_data __initdata dk_udc_data = {
	.vbus_pin	= AT91_PIN_PD4,
	.pullup_pin	= AT91_PIN_PD5,
};

static struct at91_cf_data __initdata dk_cf_data = {
	.det_pin	= AT91_PIN_PB0,
	.rst_pin	= AT91_PIN_PC5,
	// .irq_pin	= ... not connected
	// .vcc_pin	= ... always powered
};

#ifndef CONFIG_MTD_AT91_DATAFLASH_CARD
static struct at91_mmc_data __initdata dk_mmc_data = {
	.slot_b		= 0,
	.wire4		= 1,
};
#endif

static struct spi_board_info dk_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
	},
	{	/* UR6HCPS2-SP40 PS2-to-SPI adapter */
		.modalias	= "ur6hcps2",
		.chip_select	= 1,
		.max_speed_hz	= 250 *  1000,
	},
	{	/* TLV1504 ADC, 4 channels, 10 bits; one is a temp sensor */
		.modalias	= "tlv1504",
		.chip_select	= 2,
		.max_speed_hz	= 20 * 1000 * 1000,
	},
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	{	/* DataFlash card */
		.modalias	= "mtd_dataflash",
		.chip_select	= 3,
		.max_speed_hz	= 15 * 1000 * 1000,
	}
#endif
};

static struct i2c_board_info __initdata dk_i2c_devices[] = {
	{
		I2C_BOARD_INFO("ics1523", 0x26),
	},
	{
		I2C_BOARD_INFO("x9429", 0x28),
	},
	{
		I2C_BOARD_INFO("24c1024", 0x50),
	}
};

static struct mtd_partition __initdata dk_nand_partition[] = {
	{
		.name	= "NAND Partition 1",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(dk_nand_partition);
	return dk_nand_partition;
}

static struct atmel_nand_data __initdata dk_nand_data = {
	.ale		= 22,
	.cle		= 21,
	.det_pin	= AT91_PIN_PB1,
	.rdy_pin	= AT91_PIN_PC2,
	// .enable_pin	= ... not there
	.partition_info	= nand_partitions,
};

#define DK_FLASH_BASE	AT91_CHIPSELECT_0
#define DK_FLASH_SIZE	SZ_2M

static struct physmap_flash_data dk_flash_data = {
	.width		= 2,
};

static struct resource dk_flash_resource = {
	.start		= DK_FLASH_BASE,
	.end		= DK_FLASH_BASE + DK_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device dk_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &dk_flash_data,
			},
	.resource	= &dk_flash_resource,
	.num_resources	= 1,
};

static struct gpio_led dk_leds[] = {
	{
		.name			= "led0",
		.gpio			= AT91_PIN_PB2,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	}
};

static void __init dk_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&dk_eth_data);
	/* USB Host */
	at91_add_device_usbh(&dk_usbh_data);
	/* USB Device */
	at91_add_device_udc(&dk_udc_data);
	at91_set_multi_drive(dk_udc_data.pullup_pin, 1);	/* pullup_pin is connected to reset */
	/* Compact Flash */
	at91_add_device_cf(&dk_cf_data);
	/* I2C */
	at91_add_device_i2c(dk_i2c_devices, ARRAY_SIZE(dk_i2c_devices));
	/* SPI */
	at91_add_device_spi(dk_spi_devices, ARRAY_SIZE(dk_spi_devices));
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	/* DataFlash card */
	at91_set_gpio_output(AT91_PIN_PB7, 0);
#else
	/* MMC */
	at91_set_gpio_output(AT91_PIN_PB7, 1);	/* this MMC card slot can optionally use SPI signaling (CS3). */
	at91_add_device_mmc(0, &dk_mmc_data);
#endif
	/* NAND */
	at91_add_device_nand(&dk_nand_data);
	/* NOR Flash */
	platform_device_register(&dk_flash);
	/* LEDs */
	at91_gpio_leds(dk_leds, ARRAY_SIZE(dk_leds));
	/* VGA */
//	dk_add_device_video();
}

MACHINE_START(AT91RM9200DK, "Atmel AT91RM9200-DK")
	/* Maintainer: SAN People/Atmel */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= dk_map_io,
	.init_irq	= dk_init_irq,
	.init_machine	= dk_board_init,
MACHINE_END
