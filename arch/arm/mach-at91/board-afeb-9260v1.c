/*
 * linux/arch/arm/mach-at91/board-afeb-9260v1.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2006 Atmel
 *  Copyright (C) 2008 Sergey Lapin
 *
 * A custom board designed as open hardware; PCBs and various information
 * is available at http://groups.google.com/group/arm9fpga-evolution-board/
 * Subversion repository: svn://194.85.238.22/home/users/george/svn/arm9eb
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
#include <linux/clk.h>
#include <linux/dma-mapping.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/board.h>
#include <mach/gpio.h>

#include "generic.h"


static void __init afeb9260_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91sam9260_initialize(18432000);

	/* DGBU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91SAM9260_ID_US0, 1,
			     ATMEL_UART_CTS | ATMEL_UART_RTS
			   | ATMEL_UART_DTR | ATMEL_UART_DSR
			   | ATMEL_UART_DCD | ATMEL_UART_RI);

	/* USART1 on ttyS2. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9260_ID_US1, 2,
			ATMEL_UART_CTS | ATMEL_UART_RTS);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init afeb9260_init_irq(void)
{
	at91sam9260_init_interrupts(NULL);
}


/*
 * USB Host port
 */
static struct at91_usbh_data __initdata afeb9260_usbh_data = {
	.ports		= 1,
};

/*
 * USB Device port
 */
static struct at91_udc_data __initdata afeb9260_udc_data = {
	.vbus_pin	= AT91_PIN_PC5,
	.pullup_pin	= 0,		/* pull-up driven by UDC */
};



/*
 * SPI devices.
 */
static struct spi_board_info afeb9260_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 1,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
};



/*
 * NAND flash
 */
static struct mtd_partition __initdata afeb9260_nand_partition[] = {
	{
		.name	= "bootloader",
		.offset	= 0,
		.size	= (640 * SZ_1K),
	},
	{
		.name	= "kernel",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= SZ_2M,
	},
	{
		.name	= "rootfs",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(afeb9260_nand_partition);
	return afeb9260_nand_partition;
}

static struct atmel_nand_data __initdata afeb9260_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.rdy_pin	= AT91_PIN_PC13,
	.enable_pin	= AT91_PIN_PC14,
	.partition_info	= nand_partitions,
	.bus_width_16	= 0,
};


/*
 * MCI (SD/MMC)
 */
static struct at91_mmc_data __initdata afeb9260_mmc_data = {
	.slot_b		= 1,
	.wire4		= 1,
};



static struct i2c_board_info __initdata afeb9260_i2c_devices[] = {
	{
		I2C_BOARD_INFO("fm3130", 0x68),
		I2C_BOARD_INFO("24c64", 0x50),
	},
};

static void __init afeb9260_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&afeb9260_usbh_data);
	/* USB Device */
	at91_add_device_udc(&afeb9260_udc_data);
	/* SPI */
	at91_add_device_spi(afeb9260_spi_devices,
			ARRAY_SIZE(afeb9260_spi_devices));
	/* NAND */
	at91_add_device_nand(&afeb9260_nand_data);
	/* MMC */
	at91_add_device_mmc(0, &afeb9260_mmc_data);
	/* I2C */
	at91_add_device_i2c(afeb9260_i2c_devices,
			ARRAY_SIZE(afeb9260_i2c_devices));
}

MACHINE_START(AFEB9260, "Custom afeb9260 board")
	/* Maintainer: Sergey Lapin <slapin@ossfans.org> */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= afeb9260_map_io,
	.init_irq	= afeb9260_init_irq,
	.init_machine	= afeb9260_board_init,
MACHINE_END

