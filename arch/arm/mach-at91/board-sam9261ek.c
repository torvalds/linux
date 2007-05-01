/*
 * linux/arch/arm/mach-at91/board-sam9261ek.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2006 Atmel
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
#include <linux/dm9000.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/at91sam926x_mc.h>

#include "generic.h"


/*
 * Serial port configuration.
 *    0 .. 2 = USART0 .. USART2
 *    3      = DBGU
 */
static struct at91_uart_config __initdata ek_uart_config = {
	.console_tty	= 0,				/* ttyS0 */
	.nr_tty		= 1,
	.tty_map	= { 3, -1, -1, -1 }		/* ttyS0, ..., ttyS3 */
};

static void __init ek_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91sam9261_initialize(18432000);

	/* Setup the serial ports and console */
	at91_init_serial(&ek_uart_config);
}

static void __init ek_init_irq(void)
{
	at91sam9261_init_interrupts(NULL);
}


/*
 * DM9000 ethernet device
 */
#if defined(CONFIG_DM9000)
static struct resource at91sam9261_dm9000_resource[] = {
	[0] = {
		.start	= AT91_CHIPSELECT_2,
		.end	= AT91_CHIPSELECT_2 + 3,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= AT91_CHIPSELECT_2 + 0x44,
		.end	= AT91_CHIPSELECT_2 + 0xFF,
		.flags	= IORESOURCE_MEM
	},
	[2] = {
		.start	= AT91_PIN_PC11,
		.end	= AT91_PIN_PC11,
		.flags	= IORESOURCE_IRQ
	}
};

static struct dm9000_plat_data dm9000_platdata = {
	.flags		= DM9000_PLATF_16BITONLY,
};

static struct platform_device at91sam9261_dm9000_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(at91sam9261_dm9000_resource),
	.resource	= at91sam9261_dm9000_resource,
	.dev		= {
		.platform_data	= &dm9000_platdata,
	}
};

static void __init ek_add_device_dm9000(void)
{
	/*
	 * Configure Chip-Select 2 on SMC for the DM9000.
	 * Note: These timings were calculated for MASTER_CLOCK = 100000000
	 *  according to the DM9000 timings.
	 */
	at91_sys_write(AT91_SMC_SETUP(2), AT91_SMC_NWESETUP_(2) | AT91_SMC_NCS_WRSETUP_(0) | AT91_SMC_NRDSETUP_(2) | AT91_SMC_NCS_RDSETUP_(0));
	at91_sys_write(AT91_SMC_PULSE(2), AT91_SMC_NWEPULSE_(4) | AT91_SMC_NCS_WRPULSE_(8) | AT91_SMC_NRDPULSE_(4) | AT91_SMC_NCS_RDPULSE_(8));
	at91_sys_write(AT91_SMC_CYCLE(2), AT91_SMC_NWECYCLE_(16) | AT91_SMC_NRDCYCLE_(16));
	at91_sys_write(AT91_SMC_MODE(2), AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_WRITE | AT91_SMC_DBW_16 | AT91_SMC_TDF_(1));

	/* Configure Reset signal as output */
	at91_set_gpio_output(AT91_PIN_PC10, 0);

	/* Configure Interrupt pin as input, no pull-up */
	at91_set_gpio_input(AT91_PIN_PC11, 0);

	platform_device_register(&at91sam9261_dm9000_device);
}
#else
static void __init ek_add_device_dm9000(void) {}
#endif /* CONFIG_DM9000 */


/*
 * USB Host Port
 */
static struct at91_usbh_data __initdata ek_usbh_data = {
	.ports		= 2,
};


/*
 * USB Device Port
 */
static struct at91_udc_data __initdata ek_udc_data = {
	.vbus_pin	= AT91_PIN_PB29,
	.pullup_pin	= 0,		/* pull-up driven by UDC */
};


/*
 * MCI (SD/MMC)
 */
static struct at91_mmc_data __initdata ek_mmc_data = {
	.wire4		= 1,
//	.det_pin	= ... not connected
//	.wp_pin		= ... not connected
//	.vcc_pin	= ... not connected
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata ek_nand_partition[] = {
	{
		.name	= "Partition 1",
		.offset	= 0,
		.size	= 256 * 1024,
	},
	{
		.name	= "Partition 2",
		.offset	= 256 * 1024 ,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition *nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(ek_nand_partition);
	return ek_nand_partition;
}

static struct at91_nand_data __initdata ek_nand_data = {
	.ale		= 22,
	.cle		= 21,
//	.det_pin	= ... not connected
	.rdy_pin	= AT91_PIN_PC15,
	.enable_pin	= AT91_PIN_PC14,
	.partition_info	= nand_partitions,
#if defined(CONFIG_MTD_NAND_AT91_BUSWIDTH_16)
	.bus_width_16	= 1,
#else
	.bus_width_16	= 0,
#endif
};

/*
 * SPI devices
 */
static struct spi_board_info ek_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#if defined(CONFIG_MTD_AT91_DATAFLASH_CARD)
	{	/* DataFlash card - jumper (J12) configurable to CS3 or CS0 */
		.modalias	= "mtd_dataflash",
		.chip_select	= 3,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#elif defined(CONFIG_SND_AT73C213)
	{	/* AT73C213 DAC */
		.modalias	= "snd_at73c213",
		.chip_select	= 3,
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 0,
	},
#endif
};


static void __init ek_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	at91_add_device_udc(&ek_udc_data);
	/* I2C */
	at91_add_device_i2c();
	/* NAND */
	at91_add_device_nand(&ek_nand_data);
	/* DM9000 ethernet */
	ek_add_device_dm9000();

	/* spi0 and mmc/sd share the same PIO pins */
#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
	/* SPI */
	at91_add_device_spi(ek_spi_devices, ARRAY_SIZE(ek_spi_devices));
#else
	/* MMC */
	at91_add_device_mmc(0, &ek_mmc_data);
#endif
}

MACHINE_START(AT91SAM9261EK, "Atmel AT91SAM9261-EK")
	/* Maintainer: Atmel */
	.phys_io	= AT91_BASE_SYS,
	.io_pg_offst	= (AT91_VA_BASE_SYS >> 18) & 0xfffc,
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= ek_map_io,
	.init_irq	= ek_init_irq,
	.init_machine	= ek_board_init,
MACHINE_END
