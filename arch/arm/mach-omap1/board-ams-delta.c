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
#include <linux/basic_mmio_gpio.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/serial_8250.h>
#include <linux/export.h>
#include <linux/omapfb.h>
#include <linux/io.h>
#include <linux/platform_data/gpio-omap.h>

#include <media/soc_camera.h>

#include <asm/serial.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board-ams-delta.h>
#include <linux/platform_data/keypad-omap.h>
#include <mach/mux.h>

#include <mach/hardware.h>
#include <mach/ams-delta-fiq.h>
#include <mach/camera.h>
#include <mach/usb.h>

#include "iomap.h"
#include "common.h"

static const unsigned int ams_delta_keymap[] = {
	KEY(0, 0, KEY_F1),		/* Advert    */

	KEY(0, 3, KEY_COFFEE),		/* Games     */
	KEY(0, 2, KEY_QUESTION),	/* Directory */
	KEY(2, 3, KEY_CONNECT),		/* Internet  */
	KEY(1, 2, KEY_SHOP),		/* Services  */
	KEY(1, 1, KEY_PHONE),		/* VoiceMail */

	KEY(0, 1, KEY_DELETE),		/* Delete    */
	KEY(2, 2, KEY_PLAY),		/* Play      */
	KEY(1, 0, KEY_PAGEUP),		/* Up        */
	KEY(1, 3, KEY_PAGEDOWN),	/* Down      */
	KEY(2, 0, KEY_EMAIL),		/* ReadEmail */
	KEY(2, 1, KEY_STOP),		/* Stop      */

	/* Numeric keypad portion */
	KEY(0, 7, KEY_KP1),
	KEY(0, 6, KEY_KP2),
	KEY(0, 5, KEY_KP3),
	KEY(1, 7, KEY_KP4),
	KEY(1, 6, KEY_KP5),
	KEY(1, 5, KEY_KP6),
	KEY(2, 7, KEY_KP7),
	KEY(2, 6, KEY_KP8),
	KEY(2, 5, KEY_KP9),
	KEY(3, 6, KEY_KP0),
	KEY(3, 7, KEY_KPASTERISK),
	KEY(3, 5, KEY_KPDOT),		/* # key     */
	KEY(7, 2, KEY_NUMLOCK),		/* Mute      */
	KEY(7, 1, KEY_KPMINUS),		/* Recall    */
	KEY(6, 1, KEY_KPPLUS),		/* Redial    */
	KEY(7, 6, KEY_KPSLASH),		/* Handsfree */
	KEY(6, 0, KEY_ENTER),		/* Video     */

	KEY(7, 4, KEY_CAMERA),		/* Photo     */

	KEY(0, 4, KEY_F2),		/* Home      */
	KEY(1, 4, KEY_F3),		/* Office    */
	KEY(2, 4, KEY_F4),		/* Mobile    */
	KEY(7, 7, KEY_F5),		/* SMS       */
	KEY(7, 5, KEY_F6),		/* Email     */

	/* QWERTY portion of keypad */
	KEY(3, 4, KEY_Q),
	KEY(3, 3, KEY_W),
	KEY(3, 2, KEY_E),
	KEY(3, 1, KEY_R),
	KEY(3, 0, KEY_T),
	KEY(4, 7, KEY_Y),
	KEY(4, 6, KEY_U),
	KEY(4, 5, KEY_I),
	KEY(4, 4, KEY_O),
	KEY(4, 3, KEY_P),

	KEY(4, 2, KEY_A),
	KEY(4, 1, KEY_S),
	KEY(4, 0, KEY_D),
	KEY(5, 7, KEY_F),
	KEY(5, 6, KEY_G),
	KEY(5, 5, KEY_H),
	KEY(5, 4, KEY_J),
	KEY(5, 3, KEY_K),
	KEY(5, 2, KEY_L),

	KEY(5, 1, KEY_Z),
	KEY(5, 0, KEY_X),
	KEY(6, 7, KEY_C),
	KEY(6, 6, KEY_V),
	KEY(6, 5, KEY_B),
	KEY(6, 4, KEY_N),
	KEY(6, 3, KEY_M),
	KEY(6, 2, KEY_SPACE),

	KEY(7, 0, KEY_LEFTSHIFT),	/* Vol up    */
	KEY(7, 3, KEY_LEFTCTRL),	/* Vol down  */
};

#define LATCH1_PHYS	0x01000000
#define LATCH1_VIRT	0xEA000000
#define MODEM_PHYS	0x04000000
#define MODEM_VIRT	0xEB000000
#define LATCH2_PHYS	0x08000000
#define LATCH2_VIRT	0xEC000000

static struct map_desc ams_delta_io_desc[] __initdata = {
	/* AMS_DELTA_LATCH1 */
	{
		.virtual	= LATCH1_VIRT,
		.pfn		= __phys_to_pfn(LATCH1_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	/* AMS_DELTA_LATCH2 */
	{
		.virtual	= LATCH2_VIRT,
		.pfn		= __phys_to_pfn(LATCH2_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
	/* AMS_DELTA_MODEM */
	{
		.virtual	= MODEM_VIRT,
		.pfn		= __phys_to_pfn(MODEM_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	}
};

static struct omap_lcd_config ams_delta_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_usb_config ams_delta_usb_config = {
	.register_host	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 2,
};

#define LATCH1_GPIO_BASE	232
#define LATCH1_NGPIO		8

static struct resource latch1_resources[] = {
	[0] = {
		.name	= "dat",
		.start	= LATCH1_PHYS,
		.end	= LATCH1_PHYS + (LATCH1_NGPIO - 1) / 8,
		.flags	= IORESOURCE_MEM,
	},
};

static struct bgpio_pdata latch1_pdata = {
	.base	= LATCH1_GPIO_BASE,
	.ngpio	= LATCH1_NGPIO,
};

static struct platform_device latch1_gpio_device = {
	.name		= "basic-mmio-gpio",
	.id		= 0,
	.resource	= latch1_resources,
	.num_resources	= ARRAY_SIZE(latch1_resources),
	.dev		= {
		.platform_data	= &latch1_pdata,
	},
};

static struct resource latch2_resources[] = {
	[0] = {
		.name	= "dat",
		.start	= LATCH2_PHYS,
		.end	= LATCH2_PHYS + (AMS_DELTA_LATCH2_NGPIO - 1) / 8,
		.flags	= IORESOURCE_MEM,
	},
};

static struct bgpio_pdata latch2_pdata = {
	.base	= AMS_DELTA_LATCH2_GPIO_BASE,
	.ngpio	= AMS_DELTA_LATCH2_NGPIO,
};

static struct platform_device latch2_gpio_device = {
	.name		= "basic-mmio-gpio",
	.id		= 1,
	.resource	= latch2_resources,
	.num_resources	= ARRAY_SIZE(latch2_resources),
	.dev		= {
		.platform_data	= &latch2_pdata,
	},
};

static const struct gpio latch_gpios[] __initconst = {
	{
		.gpio	= LATCH1_GPIO_BASE + 6,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "dockit1",
	},
	{
		.gpio	= LATCH1_GPIO_BASE + 7,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "dockit2",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_SCARD_RSTIN,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "scard_rstin",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_SCARD_CMDVCC,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "scard_cmdvcc",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_MODEM_CODEC,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "modem_codec",
	},
	{
		.gpio	= AMS_DELTA_LATCH2_GPIO_BASE + 14,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "hookflash1",
	},
	{
		.gpio	= AMS_DELTA_LATCH2_GPIO_BASE + 15,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "hookflash2",
	},
};

static struct regulator_consumer_supply modem_nreset_consumers[] = {
	REGULATOR_SUPPLY("RESET#", "serial8250.1"),
	REGULATOR_SUPPLY("POR", "cx20442-codec"),
};

static struct regulator_init_data modem_nreset_data = {
	.constraints		= {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
		.boot_on		= 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(modem_nreset_consumers),
	.consumer_supplies	= modem_nreset_consumers,
};

static struct fixed_voltage_config modem_nreset_config = {
	.supply_name		= "modem_nreset",
	.microvolts		= 3300000,
	.gpio			= AMS_DELTA_GPIO_PIN_MODEM_NRESET,
	.startup_delay		= 25000,
	.enable_high		= 1,
	.enabled_at_boot	= 1,
	.init_data		= &modem_nreset_data,
};

static struct platform_device modem_nreset_device = {
	.name	= "reg-fixed-voltage",
	.id	= -1,
	.dev	= {
		.platform_data	= &modem_nreset_config,
	},
};

struct modem_private_data {
	struct regulator *regulator;
};

static struct modem_private_data modem_priv;

void ams_delta_latch_write(int base, int ngpio, u16 mask, u16 value)
{
	int bit = 0;
	u16 bitpos = 1 << bit;

	for (; bit < ngpio; bit++, bitpos = bitpos << 1) {
		if (!(mask & bitpos))
			continue;
		else
			gpio_set_value(base + bit, (value & bitpos) != 0);
	}
}
EXPORT_SYMBOL(ams_delta_latch_write);

static struct resource ams_delta_nand_resources[] = {
	[0] = {
		.start	= OMAP1_MPUIO_BASE,
		.end	= OMAP1_MPUIO_BASE +
				OMAP_MPUIO_IO_CNTL + sizeof(u32) - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ams_delta_nand_device = {
	.name	= "ams-delta-nand",
	.id	= -1,
	.num_resources	= ARRAY_SIZE(ams_delta_nand_resources),
	.resource	= ams_delta_nand_resources,
};

static struct resource ams_delta_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data ams_delta_keymap_data = {
	.keymap		= ams_delta_keymap,
	.keymap_size	= ARRAY_SIZE(ams_delta_keymap),
};

static struct omap_kp_platform_data ams_delta_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &ams_delta_keymap_data,
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

static const struct gpio_led gpio_leds[] __initconst = {
	{
		.name		 = "camera",
		.gpio		 = LATCH1_GPIO_BASE + 0,
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
#ifdef CONFIG_LEDS_TRIGGERS
		.default_trigger = "ams_delta_camera",
#endif
	},
	{
		.name		 = "advert",
		.gpio		 = LATCH1_GPIO_BASE + 1,
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name		 = "email",
		.gpio		 = LATCH1_GPIO_BASE + 2,
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name		 = "handsfree",
		.gpio		 = LATCH1_GPIO_BASE + 3,
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name		 = "voicemail",
		.gpio		 = LATCH1_GPIO_BASE + 4,
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name		 = "voice",
		.gpio		 = LATCH1_GPIO_BASE + 5,
		.default_state	 = LEDS_GPIO_DEFSTATE_OFF,
	},
};

static const struct gpio_led_platform_data leds_pdata __initconst = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct i2c_board_info ams_delta_camera_board_info[] = {
	{
		I2C_BOARD_INFO("ov6650", 0x60),
	},
};

#ifdef CONFIG_LEDS_TRIGGERS
DEFINE_LED_TRIGGER(ams_delta_camera_led_trigger);

static int ams_delta_camera_power(struct device *dev, int power)
{
	/*
	 * turn on camera LED
	 */
	if (power)
		led_trigger_event(ams_delta_camera_led_trigger, LED_FULL);
	else
		led_trigger_event(ams_delta_camera_led_trigger, LED_OFF);
	return 0;
}
#else
#define ams_delta_camera_power	NULL
#endif

static struct soc_camera_link ams_delta_iclink = {
	.bus_id         = 0,	/* OMAP1 SoC camera bus */
	.i2c_adapter_id = 1,
	.board_info     = &ams_delta_camera_board_info[0],
	.module_name    = "ov6650",
	.power		= ams_delta_camera_power,
};

static struct platform_device ams_delta_camera_device = {
	.name   = "soc-camera-pdrv",
	.id     = 0,
	.dev    = {
		.platform_data = &ams_delta_iclink,
	},
};

static struct omap1_cam_platform_data ams_delta_camera_platform_data = {
	.camexclk_khz	= 12000,	/* default 12MHz clock, no extra DPLL */
	.lclk_khz_max	= 1334,		/* results in 5fps CIF, 10fps QCIF */
};

static struct platform_device *ams_delta_devices[] __initdata = {
	&latch1_gpio_device,
	&latch2_gpio_device,
	&ams_delta_kp_device,
	&ams_delta_camera_device,
};

static struct platform_device *late_devices[] __initdata = {
	&ams_delta_nand_device,
	&ams_delta_lcd_device,
};

static void __init ams_delta_init(void)
{
	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);

	/* parallel camera interface */
	omap_cfg_reg(H19_1610_CAM_EXCLK);
	omap_cfg_reg(J15_1610_CAM_LCLK);
	omap_cfg_reg(L18_1610_CAM_VS);
	omap_cfg_reg(L15_1610_CAM_HS);
	omap_cfg_reg(L19_1610_CAM_D0);
	omap_cfg_reg(K14_1610_CAM_D1);
	omap_cfg_reg(K15_1610_CAM_D2);
	omap_cfg_reg(K19_1610_CAM_D3);
	omap_cfg_reg(K18_1610_CAM_D4);
	omap_cfg_reg(J14_1610_CAM_D5);
	omap_cfg_reg(J19_1610_CAM_D6);
	omap_cfg_reg(J18_1610_CAM_D7);

	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);

	omap1_usb_init(&ams_delta_usb_config);
	omap1_set_camera_info(&ams_delta_camera_platform_data);
#ifdef CONFIG_LEDS_TRIGGERS
	led_trigger_register_simple("ams_delta_camera",
			&ams_delta_camera_led_trigger);
#endif
	gpio_led_register_device(-1, &leds_pdata);
	platform_add_devices(ams_delta_devices, ARRAY_SIZE(ams_delta_devices));

	ams_delta_init_fiq();

	omap_writew(omap_readw(ARM_RSTCT1) | 0x0004, ARM_RSTCT1);

	omapfb_set_lcd_config(&ams_delta_lcd_config);
}

static void modem_pm(struct uart_port *port, unsigned int state, unsigned old)
{
	struct modem_private_data *priv = port->private_data;

	if (IS_ERR(priv->regulator))
		return;

	if (state == old)
		return;

	if (state == 0)
		regulator_enable(priv->regulator);
	else if (old == 0)
		regulator_disable(priv->regulator);
}

static struct plat_serial8250_port ams_delta_modem_ports[] = {
	{
		.membase	= IOMEM(MODEM_VIRT),
		.mapbase	= MODEM_PHYS,
		.irq		= -EINVAL, /* changed later */
		.flags		= UPF_BOOT_AUTOCONF,
		.irqflags	= IRQF_TRIGGER_RISING,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= BASE_BAUD * 16,
		.pm		= modem_pm,
		.private_data	= &modem_priv,
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

static int __init late_init(void)
{
	int err;

	if (!machine_is_ams_delta())
		return -ENODEV;

	err = gpio_request_array(latch_gpios, ARRAY_SIZE(latch_gpios));
	if (err) {
		pr_err("Couldn't take over latch1/latch2 GPIO pins\n");
		return err;
	}

	platform_add_devices(late_devices, ARRAY_SIZE(late_devices));

	err = platform_device_register(&modem_nreset_device);
	if (err) {
		pr_err("Couldn't register the modem regulator device\n");
		return err;
	}

	omap_cfg_reg(M14_1510_GPIO2);
	ams_delta_modem_ports[0].irq =
			gpio_to_irq(AMS_DELTA_GPIO_PIN_MODEM_IRQ);

	err = gpio_request(AMS_DELTA_GPIO_PIN_MODEM_IRQ, "modem");
	if (err) {
		pr_err("Couldn't request gpio pin for modem\n");
		return err;
	}
	gpio_direction_input(AMS_DELTA_GPIO_PIN_MODEM_IRQ);

	/* Initialize the modem_nreset regulator consumer before use */
	modem_priv.regulator = ERR_PTR(-ENODEV);

	ams_delta_latch2_write(AMS_DELTA_LATCH2_MODEM_CODEC,
			AMS_DELTA_LATCH2_MODEM_CODEC);

	err = platform_device_register(&ams_delta_modem_device);
	if (err)
		goto gpio_free;

	/*
	 * Once the modem device is registered, the modem_nreset
	 * regulator can be requested on behalf of that device.
	 */
	modem_priv.regulator = regulator_get(&ams_delta_modem_device.dev,
			"RESET#");
	if (IS_ERR(modem_priv.regulator)) {
		err = PTR_ERR(modem_priv.regulator);
		goto unregister;
	}
	return 0;

unregister:
	platform_device_unregister(&ams_delta_modem_device);
gpio_free:
	gpio_free(AMS_DELTA_GPIO_PIN_MODEM_IRQ);
	return err;
}

static void __init ams_delta_init_late(void)
{
	omap1_init_late();
	late_init();
}

static void __init ams_delta_map_io(void)
{
	omap15xx_map_io();
	iotable_init(ams_delta_io_desc, ARRAY_SIZE(ams_delta_io_desc));
}

MACHINE_START(AMS_DELTA, "Amstrad E3 (Delta)")
	/* Maintainer: Jonathan McDowell <noodles@earth.li> */
	.atag_offset	= 0x100,
	.map_io		= ams_delta_map_io,
	.init_early	= omap1_init_early,
	.reserve	= omap_reserve,
	.init_irq	= omap1_init_irq,
	.init_machine	= ams_delta_init,
	.init_late	= ams_delta_init_late,
	.timer		= &omap1_timer,
	.restart	= omap1_restart,
MACHINE_END
