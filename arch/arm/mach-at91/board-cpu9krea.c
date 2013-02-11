/*
 * linux/arch/arm/mach-at91/board-cpu9krea.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2006 Atmel
 *  Copyright (C) 2009 Eric Benard - eric@eukrea.com
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
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mtd/physmap.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91sam9260_matrix.h>
#include <mach/at91_matrix.h>

#include "at91_aic.h"
#include "board.h"
#include "sam9_smc.h"
#include "generic.h"

static void __init cpu9krea_init_early(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91_initialize(18432000);
}

/*
 * USB Host port
 */
static struct at91_usbh_data __initdata cpu9krea_usbh_data = {
	.ports		= 2,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

/*
 * USB Device port
 */
static struct at91_udc_data __initdata cpu9krea_udc_data = {
	.vbus_pin	= AT91_PIN_PC8,
	.pullup_pin	= -EINVAL,		/* pull-up driven by UDC */
};

/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata cpu9krea_macb_data = {
	.phy_irq_pin	= -EINVAL,
	.is_rmii	= 1,
};

/*
 * NAND flash
 */
static struct atmel_nand_data __initdata cpu9krea_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.rdy_pin	= AT91_PIN_PC13,
	.enable_pin	= AT91_PIN_PC14,
	.bus_width_16	= 0,
	.det_pin	= -EINVAL,
	.ecc_mode	= NAND_ECC_SOFT,
};

#ifdef CONFIG_MACH_CPU9260
static struct sam9_smc_config __initdata cpu9krea_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 3,
	.nrd_pulse		= 3,
	.ncs_write_pulse	= 3,
	.nwe_pulse		= 3,

	.read_cycle		= 5,
	.write_cycle		= 5,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE
		| AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_DBW_8,
	.tdf_cycles		= 2,
};
#else
static struct sam9_smc_config __initdata cpu9krea_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 2,
	.ncs_write_setup	= 0,
	.nwe_setup		= 2,

	.ncs_read_pulse		= 4,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 4,
	.nwe_pulse		= 4,

	.read_cycle		= 7,
	.write_cycle		= 7,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE
		| AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_DBW_8,
	.tdf_cycles		= 3,
};
#endif

static void __init cpu9krea_add_device_nand(void)
{
	sam9_smc_configure(0, 3, &cpu9krea_nand_smc_config);
	at91_add_device_nand(&cpu9krea_nand_data);
}

/*
 * NOR flash
 */
static struct physmap_flash_data cpuat9260_nor_data = {
	.width		= 2,
};

#define NOR_BASE	AT91_CHIPSELECT_0
#define NOR_SIZE	SZ_64M

static struct resource nor_flash_resources[] = {
	{
		.start	= NOR_BASE,
		.end	= NOR_BASE + NOR_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device cpu9krea_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &cpuat9260_nor_data,
	},
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
};

#ifdef CONFIG_MACH_CPU9260
static struct sam9_smc_config __initdata cpu9krea_nor_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 10,
	.nrd_pulse		= 10,
	.ncs_write_pulse	= 6,
	.nwe_pulse		= 6,

	.read_cycle		= 12,
	.write_cycle		= 8,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE
			| AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_WRITE
			| AT91_SMC_DBW_16,
	.tdf_cycles		= 2,
};
#else
static struct sam9_smc_config __initdata cpu9krea_nor_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 13,
	.nrd_pulse		= 13,
	.ncs_write_pulse	= 8,
	.nwe_pulse		= 8,

	.read_cycle		= 15,
	.write_cycle		= 10,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE
			| AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_WRITE
			| AT91_SMC_DBW_16,
	.tdf_cycles		= 2,
};
#endif

static __init void cpu9krea_add_device_nor(void)
{
	unsigned long csa;

	csa = at91_matrix_read(AT91_MATRIX_EBICSA);
	at91_matrix_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_VDDIOMSEL_3_3V);

	/* configure chip-select 0 (NOR) */
	sam9_smc_configure(0, 0, &cpu9krea_nor_smc_config);

	platform_device_register(&cpu9krea_nor_flash);
}

/*
 * LEDs
 */
static struct gpio_led cpu9krea_leds[] = {
	{	/* LED1 */
		.name			= "LED1",
		.gpio			= AT91_PIN_PC11,
		.active_low		= 1,
		.default_trigger	= "timer",
	},
	{	/* LED2 */
		.name			= "LED2",
		.gpio			= AT91_PIN_PC12,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	},
	{	/* LED3 */
		.name			= "LED3",
		.gpio			= AT91_PIN_PC7,
		.active_low		= 1,
		.default_trigger	= "none",
	},
	{	/* LED4 */
		.name			= "LED4",
		.gpio			= AT91_PIN_PC9,
		.active_low		= 1,
		.default_trigger	= "none",
	}
};

static struct i2c_board_info __initdata cpu9krea_i2c_devices[] = {
	{
		I2C_BOARD_INFO("ds1339", 0x68),
	},
};

/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button cpu9krea_buttons[] = {
	{
		.gpio		= AT91_PIN_PC3,
		.code		= BTN_0,
		.desc		= "BP1",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PB20,
		.code		= BTN_1,
		.desc		= "BP2",
		.active_low	= 1,
		.wakeup		= 1,
	}
};

static struct gpio_keys_platform_data cpu9krea_button_data = {
	.buttons	= cpu9krea_buttons,
	.nbuttons	= ARRAY_SIZE(cpu9krea_buttons),
};

static struct platform_device cpu9krea_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &cpu9krea_button_data,
	}
};

static void __init cpu9krea_add_device_buttons(void)
{
	at91_set_gpio_input(AT91_PIN_PC3, 1);	/* BP1 */
	at91_set_deglitch(AT91_PIN_PC3, 1);
	at91_set_gpio_input(AT91_PIN_PB20, 1);	/* BP2 */
	at91_set_deglitch(AT91_PIN_PB20, 1);

	platform_device_register(&cpu9krea_button_device);
}
#else
static void __init cpu9krea_add_device_buttons(void)
{
}
#endif

/*
 * MCI (SD/MMC)
 */
static struct mci_platform_data __initdata cpu9krea_mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= AT91_PIN_PA29,
		.wp_pin		= -EINVAL,
	},
};

static void __init cpu9krea_board_init(void)
{
	/* NOR */
	cpu9krea_add_device_nor();
	/* Serial */
	/* DGBU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI) */
	at91_register_uart(AT91SAM9260_ID_US0, 1, ATMEL_UART_CTS |
		ATMEL_UART_RTS | ATMEL_UART_DTR | ATMEL_UART_DSR |
		ATMEL_UART_DCD | ATMEL_UART_RI);

	/* USART1 on ttyS2. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9260_ID_US1, 2, ATMEL_UART_CTS |
		ATMEL_UART_RTS);

	/* USART2 on ttyS3. (Rx, Tx, RTS, CTS) */
	at91_register_uart(AT91SAM9260_ID_US2, 3, ATMEL_UART_CTS |
		ATMEL_UART_RTS);

	/* USART3 on ttyS4. (Rx, Tx) */
	at91_register_uart(AT91SAM9260_ID_US3, 4, 0);

	/* USART4 on ttyS5. (Rx, Tx) */
	at91_register_uart(AT91SAM9260_ID_US4, 5, 0);

	/* USART5 on ttyS6. (Rx, Tx) */
	at91_register_uart(AT91SAM9260_ID_US5, 6, 0);
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&cpu9krea_usbh_data);
	/* USB Device */
	at91_add_device_udc(&cpu9krea_udc_data);
	/* NAND */
	cpu9krea_add_device_nand();
	/* Ethernet */
	at91_add_device_eth(&cpu9krea_macb_data);
	/* MMC */
	at91_add_device_mci(0, &cpu9krea_mci0_data);
	/* I2C */
	at91_add_device_i2c(cpu9krea_i2c_devices,
		ARRAY_SIZE(cpu9krea_i2c_devices));
	/* LEDs */
	at91_gpio_leds(cpu9krea_leds, ARRAY_SIZE(cpu9krea_leds));
	/* Push Buttons */
	cpu9krea_add_device_buttons();
}

#ifdef CONFIG_MACH_CPU9260
MACHINE_START(CPUAT9260, "Eukrea CPU9260")
#else
MACHINE_START(CPUAT9G20, "Eukrea CPU9G20")
#endif
	/* Maintainer: Eric Benard - EUKREA Electromatique */
	.init_time	= at91sam926x_pit_init,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= cpu9krea_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= cpu9krea_board_init,
MACHINE_END
