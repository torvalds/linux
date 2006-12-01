/*
 * linux/arch/arm/mach-at91rm9200/board-ek.c
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

#include "generic.h"


/*
 * Serial port configuration.
 *    0 .. 3 = USART0 .. USART3
 *    4      = DBGU
 */
static struct at91_uart_config __initdata ek_uart_config = {
	.console_tty	= 0,				/* ttyS0 */
	.nr_tty		= 2,
	.tty_map	= { 4, 1, -1, -1, -1 }		/* ttyS0, ..., ttyS4 */
};

static void __init ek_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91rm9200_initialize(18432000, AT91RM9200_BGA);

	/* Setup the LEDs */
	at91_init_leds(AT91_PIN_PB1, AT91_PIN_PB2);

	/* Setup the serial ports and console */
	at91_init_serial(&ek_uart_config);
}

static void __init ek_init_irq(void)
{
	at91rm9200_init_interrupts(NULL);
}

static struct at91_eth_data __initdata ek_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC4,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata ek_usbh_data = {
	.ports		= 2,
};

static struct at91_udc_data __initdata ek_udc_data = {
	.vbus_pin	= AT91_PIN_PD4,
	.pullup_pin	= AT91_PIN_PD5,
};

static struct at91_mmc_data __initdata ek_mmc_data = {
	.det_pin	= AT91_PIN_PB27,
	.is_b		= 0,
	.wire4		= 1,
	.wp_pin		= AT91_PIN_PA17,
};

static struct spi_board_info ek_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
	},
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	{	/* DataFlash card */
		.modalias	= "mtd_dataflash",
		.chip_select	= 3,
		.max_speed_hz	= 15 * 1000 * 1000,
	},
#endif
};

#define EK_FLASH_BASE	AT91_CHIPSELECT_0
#define EK_FLASH_SIZE	0x200000

static struct physmap_flash_data ek_flash_data = {
	.width	= 2,
};

static struct resource ek_flash_resource = {
	.start		= EK_FLASH_BASE,
	.end		= EK_FLASH_BASE + EK_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ek_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &ek_flash_data,
			},
	.resource	= &ek_flash_resource,
	.num_resources	= 1,
};


static void __init ek_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* Ethernet */
	at91_add_device_eth(&ek_eth_data);
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	at91_add_device_udc(&ek_udc_data);
	at91_set_multi_drive(ek_udc_data.pullup_pin, 1);	/* pullup_pin is connected to reset */
	/* I2C */
	at91_add_device_i2c();
	/* SPI */
	at91_add_device_spi(ek_spi_devices, ARRAY_SIZE(ek_spi_devices));
#ifdef CONFIG_MTD_AT91_DATAFLASH_CARD
	/* DataFlash card */
	at91_set_gpio_output(AT91_PIN_PB22, 0);
#else
	/* MMC */
	at91_set_gpio_output(AT91_PIN_PB22, 1);	/* this MMC card slot can optionally use SPI signaling (CS3). */
	at91_add_device_mmc(&ek_mmc_data);
#endif
	/* NOR Flash */
	platform_device_register(&ek_flash);
	/* VGA */
//	ek_add_device_video();
}

MACHINE_START(AT91RM9200EK, "Atmel AT91RM9200-EK")
	/* Maintainer: SAN People/Atmel */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91rm9200_timer,
	.map_io		= ek_map_io,
	.init_irq	= ek_init_irq,
	.init_machine	= ek_board_init,
MACHINE_END
