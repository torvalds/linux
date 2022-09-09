// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/teton_bga.c
 *
 *  Support for the Marvell PXA168 Teton BGA Development Platform.
 *
 *  Author: Mark F. Brown <mark.brown314@gmail.com>
 *
 *  This code is based on aspenite.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio-pxa.h>
#include <linux/input.h>
#include <linux/platform_data/keypad-pxa27x.h>
#include <linux/i2c.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "addr-map.h"
#include "mfp-pxa168.h"
#include "pxa168.h"
#include "teton_bga.h"
#include "irqs.h"

#include "common.h"

static unsigned long teton_bga_pin_config[] __initdata = {
	/* UART1 */
	GPIO107_UART1_TXD,
	GPIO108_UART1_RXD,

	/* Keypad */
	GPIO109_KP_MKIN1,
	GPIO110_KP_MKIN0,
	GPIO111_KP_MKOUT7,
	GPIO112_KP_MKOUT6,

	/* I2C Bus */
	GPIO105_CI2C_SDA,
	GPIO106_CI2C_SCL,

	/* RTC */
	GPIO78_GPIO,
};

static struct pxa_gpio_platform_data pxa168_gpio_pdata = {
	.irq_base	= MMP_GPIO_TO_IRQ(0),
};

static unsigned int teton_bga_matrix_key_map[] = {
	KEY(0, 6, KEY_ESC),
	KEY(0, 7, KEY_ENTER),
	KEY(1, 6, KEY_LEFT),
	KEY(1, 7, KEY_RIGHT),
};

static struct matrix_keymap_data teton_bga_matrix_keymap_data = {
	.keymap			= teton_bga_matrix_key_map,
	.keymap_size		= ARRAY_SIZE(teton_bga_matrix_key_map),
};

static struct pxa27x_keypad_platform_data teton_bga_keypad_info __initdata = {
	.matrix_key_rows        = 2,
	.matrix_key_cols        = 8,
	.matrix_keymap_data	= &teton_bga_matrix_keymap_data,
	.debounce_interval      = 30,
};

static struct i2c_board_info teton_bga_i2c_info[] __initdata = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
		.irq = MMP_GPIO_TO_IRQ(RTC_INT_GPIO)
	},
};

static void __init teton_bga_init(void)
{
	mfp_config(ARRAY_AND_SIZE(teton_bga_pin_config));

	/* on-chip devices */
	pxa168_add_uart(1);
	pxa168_add_keypad(&teton_bga_keypad_info);
	pxa168_add_twsi(0, NULL, ARRAY_AND_SIZE(teton_bga_i2c_info));
	platform_device_add_data(&pxa168_device_gpio, &pxa168_gpio_pdata,
				 sizeof(struct pxa_gpio_platform_data));
	platform_device_register(&pxa168_device_gpio);
}

MACHINE_START(TETON_BGA, "PXA168-based Teton BGA Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= MMP_NR_IRQS,
	.init_irq       = pxa168_init_irq,
	.init_time	= pxa168_timer_init,
	.init_machine   = teton_bga_init,
	.restart	= pxa168_restart,
MACHINE_END
