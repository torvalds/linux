/*
 * linux/arch/arm/mach-omap1/board-ams-delta.c
 *
 * Modified from board-generic.c
 *
 * Board specific inits for the Amstrad E3 (codename Delta) videophone
 *
 * Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>

#include <asm/serial.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board-ams-delta.h>
#include <mach/gpio.h>
#include <plat/keypad.h>
#include <plat/mux.h>
#include <plat/usb.h>
#include <plat/board.h>
#include <plat/common.h>

static u8 ams_delta_latch1_reg;
static u16 ams_delta_latch2_reg;

static int ams_delta_keymap[] = {
	KEY(0, 0, KEY_F1),		/* Advert    */

	KEY(3, 0, KEY_COFFEE),		/* Games     */
	KEY(2, 0, KEY_QUESTION),	/* Directory */
	KEY(3, 2, KEY_CONNECT),		/* Internet  */
	KEY(2, 1, KEY_SHOP),		/* Services  */
	KEY(1, 1, KEY_PHONE),		/* VoiceMail */

	KEY(1, 0, KEY_DELETE),		/* Delete    */
	KEY(2, 2, KEY_PLAY),		/* Play      */
	KEY(0, 1, KEY_PAGEUP),		/* Up        */
	KEY(3, 1, KEY_PAGEDOWN),	/* Down      */
	KEY(0, 2, KEY_EMAIL),		/* ReadEmail */
	KEY(1, 2, KEY_STOP),		/* Stop      */

	/* Numeric keypad portion */
	KEY(7, 0, KEY_KP1),
	KEY(6, 0, KEY_KP2),
	KEY(5, 0, KEY_KP3),
	KEY(7, 1, KEY_KP4),
	KEY(6, 1, KEY_KP5),
	KEY(5, 1, KEY_KP6),
	KEY(7, 2, KEY_KP7),
	KEY(6, 2, KEY_KP8),
	KEY(5, 2, KEY_KP9),
	KEY(6, 3, KEY_KP0),
	KEY(7, 3, KEY_KPASTERISK),
	KEY(5, 3, KEY_KPDOT),		/* # key     */
	KEY(2, 7, KEY_NUMLOCK),		/* Mute      */
	KEY(1, 7, KEY_KPMINUS),		/* Recall    */
	KEY(1, 6, KEY_KPPLUS),		/* Redial    */
	KEY(6, 7, KEY_KPSLASH),		/* Handsfree */
	KEY(0, 6, KEY_ENTER),		/* Video     */

	KEY(4, 7, KEY_CAMERA),		/* Photo     */

	KEY(4, 0, KEY_F2),		/* Home      */
	KEY(4, 1, KEY_F3),		/* Office    */
	KEY(4, 2, KEY_F4),		/* Mobile    */
	KEY(7, 7, KEY_F5),		/* SMS       */
	KEY(5, 7, KEY_F6),		/* Email     */

	/* QWERTY portion of keypad */
	KEY(4, 3, KEY_Q),
	KEY(3, 3, KEY_W),
	KEY(2, 3, KEY_E),
	KEY(1, 3, KEY_R),
	KEY(0, 3, KEY_T),
	KEY(7, 4, KEY_Y),
	KEY(6, 4, KEY_U),
	KEY(5, 4, KEY_I),
	KEY(4, 4, KEY_O),
	KEY(3, 4, KEY_P),

	KEY(2, 4, KEY_A),
	KEY(1, 4, KEY_S),
	KEY(0, 4, KEY_D),
	KEY(7, 5, KEY_F),
	KEY(6, 5, KEY_G),
	KEY(5, 5, KEY_H),
	KEY(4, 5, KEY_J),
	KEY(3, 5, KEY_K),
	KEY(2, 5, KEY_L),

	KEY(1, 5, KEY_Z),
	KEY(0, 5, KEY_X),
	KEY(7, 6, KEY_C),
	KEY(6, 6, KEY_V),
	KEY(5, 6, KEY_B),
	KEY(4, 6, KEY_N),
	KEY(3, 6, KEY_M),
	KEY(2, 6, KEY_SPACE),

	KEY(0, 7, KEY_LEFTSHIFT),	/* Vol up    */
	KEY(3, 7, KEY_LEFTCTRL),	/* Vol down  */

	0
};

void ams_delta_latch1_write(u8 mask, u8 value)
{
	ams_delta_latch1_reg &= ~mask;
	ams_delta_latch1_reg |= value;
	*(volatile __u8 *) AMS_DELTA_LATCH1_VIRT = ams_delta_latch1_reg;
}

void ams_delta_latch2_write(u16 mask, u16 value)
{
	ams_delta_latch2_reg &= ~mask;
	ams_delta_latch2_reg |= value;
	*(volatile __u16 *) AMS_DELTA_LATCH2_VIRT = ams_delta_latch2_reg;
}

static void __init ams_delta_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static struct map_desc ams_delta_io_desc[] __initdata = {
	/* AMS_DELTA_LATCH1 */
	{
		.virtual	= AMS_DELTA_LATCH1_VIRT,
		.pfn		= __phys_to_pfn(AMS_DELTA_LATCH1_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	/* AMS_DELTA_LATCH2 */
	{
		.virtual	= AMS_DELTA_LATCH2_VIRT,
		.pfn		= __phys_to_pfn(AMS_DELTA_LATCH2_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	/* AMS_DELTA_MODEM */
	{
		.virtual	= AMS_DELTA_MODEM_VIRT,
		.pfn		= __phys_to_pfn(AMS_DELTA_MODEM_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	}
};

static struct omap_lcd_config ams_delta_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_usb_config ams_delta_usb_config __initdata = {
	.register_host	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 2,
};

static struct omap_board_config_kernel ams_delta_config[] = {
	{ OMAP_TAG_LCD,		&ams_delta_lcd_config },
};

static struct resource ams_delta_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_kp_platform_data ams_delta_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap 	= ams_delta_keymap,
	.keymapsize	= ARRAY_SIZE(ams_delta_keymap),
	.delay		= 9,
};

static struct platform_device ams_delta_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &ams_delta_kp_data,
	},
	.num_resources	= ARRAY_SIZE(ams_delta_kp_resources),
	.resource	= ams_delta_kp_resources,
};

static struct platform_device ams_delta_lcd_device = {
	.name	= "lcd_ams_delta",
	.id	= -1,
};

static struct platform_device ams_delta_led_device = {
	.name	= "ams-delta-led",
	.id	= -1
};

static struct platform_device *ams_delta_devices[] __initdata = {
	&ams_delta_kp_device,
	&ams_delta_lcd_device,
	&ams_delta_led_device,
};

static void __init ams_delta_init(void)
{
	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);

	iotable_init(ams_delta_io_desc, ARRAY_SIZE(ams_delta_io_desc));

	omap_board_config = ams_delta_config;
	omap_board_config_size = ARRAY_SIZE(ams_delta_config);
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);

	/* Clear latch2 (NAND, LCD, modem enable) */
	ams_delta_latch2_write(~0, 0);

	omap_usb_init(&ams_delta_usb_config);
	platform_add_devices(ams_delta_devices, ARRAY_SIZE(ams_delta_devices));

	omap_writew(omap_readw(ARM_RSTCT1) | 0x0004, ARM_RSTCT1);
}

static struct plat_serial8250_port ams_delta_modem_ports[] = {
	{
		.membase	= (void *) AMS_DELTA_MODEM_VIRT,
		.mapbase	= AMS_DELTA_MODEM_PHYS,
		.irq		= -EINVAL, /* changed later */
		.flags		= UPF_BOOT_AUTOCONF,
		.irqflags	= IRQF_TRIGGER_RISING,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= BASE_BAUD * 16,
	},
	{ },
};

static struct platform_device ams_delta_modem_device = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM1,
	.dev		= {
		.platform_data = ams_delta_modem_ports,
	},
};

static int __init ams_delta_modem_init(void)
{
	omap_cfg_reg(M14_1510_GPIO2);
	ams_delta_modem_ports[0].irq = gpio_to_irq(2);

	ams_delta_latch2_write(
		AMS_DELTA_LATCH2_MODEM_NRESET | AMS_DELTA_LATCH2_MODEM_CODEC,
		AMS_DELTA_LATCH2_MODEM_NRESET | AMS_DELTA_LATCH2_MODEM_CODEC);

	return platform_device_register(&ams_delta_modem_device);
}
arch_initcall(ams_delta_modem_init);

static void __init ams_delta_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(AMS_DELTA, "Amstrad E3 (Delta)")
	/* Maintainer: Jonathan McDowell <noodles@earth.li> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= ams_delta_map_io,
	.init_irq	= ams_delta_init_irq,
	.init_machine	= ams_delta_init,
	.timer		= &omap_timer,
MACHINE_END

EXPORT_SYMBOL(ams_delta_latch1_write);
EXPORT_SYMBOL(ams_delta_latch2_write);
