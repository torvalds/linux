/*
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/at91rm9200_mc.h>
#include <mach/at91_ramc.h>
#include <mach/cpu.h>

#include "generic.h"

static void __init eco920_init_early(void)
{
	/* Set cpu type: PQFP */
	at91rm9200_set_type(ARCH_REVISON_9200_PQFP);

	at91_initialize(18432000);

	/* Setup the LEDs */
	at91_init_leds(AT91_PIN_PB0, AT91_PIN_PB1);

	/* DBGU on ttyS0. (Rx & Tx only */
	at91_register_uart(0, 0, 0);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static struct macb_platform_data __initdata eco920_eth_data = {
	.phy_irq_pin	= AT91_PIN_PC2,
	.is_rmii	= 1,
};

static struct at91_usbh_data __initdata eco920_usbh_data = {
	.ports		= 1,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

static struct at91_udc_data __initdata eco920_udc_data = {
	.vbus_pin	= AT91_PIN_PB12,
	.pullup_pin	= AT91_PIN_PB13,
};

static struct at91_mmc_data __initdata eco920_mmc_data = {
	.slot_b		= 0,
	.wire4		= 0,
	.det_pin	= -EINVAL,
	.wp_pin		= -EINVAL,
	.vcc_pin	= -EINVAL,
};

static struct physmap_flash_data eco920_flash_data = {
	.width  = 2,
};

static struct resource eco920_flash_resource = {
	.start          = 0x11000000,
	.end            = 0x11ffffff,
	.flags          = IORESOURCE_MEM,
};

static struct platform_device eco920_flash = {
	.name           = "physmap-flash",
	.id             = 0,
	.dev            = {
		.platform_data  = &eco920_flash_data,
	},
	.resource       = &eco920_flash_resource,
	.num_resources  = 1,
};

static struct spi_board_info eco920_spi_devices[] = {
	{	/* CAN controller */
		.modalias	= "tlv5638",
		.chip_select	= 3,
		.max_speed_hz	= 20 * 1000 * 1000,
		.mode		= SPI_CPHA,
	},
};

static void __init eco920_board_init(void)
{
	at91_add_device_serial();
	at91_add_device_eth(&eco920_eth_data);
	at91_add_device_usbh(&eco920_usbh_data);
	at91_add_device_udc(&eco920_udc_data);

	at91_add_device_mmc(0, &eco920_mmc_data);
	platform_device_register(&eco920_flash);

	at91_ramc_write(0, AT91_SMC_CSR(7),	AT91_SMC_RWHOLD_(1)
				| AT91_SMC_RWSETUP_(1)
				| AT91_SMC_DBW_8
				| AT91_SMC_WSEN
				| AT91_SMC_NWS_(15));

	at91_set_A_periph(AT91_PIN_PC6, 1);

	at91_set_gpio_input(AT91_PIN_PA23, 0);
	at91_set_deglitch(AT91_PIN_PA23, 1);

/* Initialization of the Static Memory Controller for Chip Select 3 */
	at91_ramc_write(0, AT91_SMC_CSR(3),
		AT91_SMC_DBW_16  |	/* 16 bit */
		AT91_SMC_WSEN    |
		AT91_SMC_NWS_(5) |	/* wait states */
		AT91_SMC_TDF_(1)	/* float time */
	);

	at91_add_device_spi(eco920_spi_devices, ARRAY_SIZE(eco920_spi_devices));
}

MACHINE_START(ECO920, "eco920")
	/* Maintainer: Sascha Hauer */
	.timer		= &at91rm9200_timer,
	.map_io		= at91_map_io,
	.init_early	= eco920_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= eco920_board_init,
MACHINE_END
