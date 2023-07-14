// SPDX-License-Identifier: GPL-2.0-only
/*
* linux/arch/arm/mach-omap1/board-sx1.c
*
* Modified from board-generic.c
*
* Support for the Siemens SX1 mobile phone.
*
* Original version : Vladimir Ananiev (Vovan888-at-gmail com)
*
* Maintainters : Vladimir Ananiev (aka Vovan888), Sergge
*		oslik.ru
*/
#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/omapfb.h>
#include <linux/platform_data/keypad-omap.h>
#include <linux/omap-dma.h>
#include "tc.h"

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "flash.h"
#include "mux.h"
#include "board-sx1.h"
#include "hardware.h"
#include "usb.h"
#include "common.h"

/* Write to I2C device */
int sx1_i2c_write_byte(u8 devaddr, u8 regoffset, u8 value)
{
	struct i2c_adapter *adap;
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	adap = i2c_get_adapter(0);
	if (!adap)
		return -ENODEV;
	msg->addr = devaddr;	/* I2C address of chip */
	msg->flags = 0;
	msg->len = 2;
	msg->buf = data;
	data[0] = regoffset;	/* register num */
	data[1] = value;		/* register data */
	err = i2c_transfer(adap, msg, 1);
	i2c_put_adapter(adap);
	if (err >= 0)
		return 0;
	return err;
}

/* Read from I2C device */
int sx1_i2c_read_byte(u8 devaddr, u8 regoffset, u8 *value)
{
	struct i2c_adapter *adap;
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	adap = i2c_get_adapter(0);
	if (!adap)
		return -ENODEV;

	msg->addr = devaddr;	/* I2C address of chip */
	msg->flags = 0;
	msg->len = 1;
	msg->buf = data;
	data[0] = regoffset;	/* register num */
	err = i2c_transfer(adap, msg, 1);

	msg->addr = devaddr;	/* I2C address */
	msg->flags = I2C_M_RD;
	msg->len = 1;
	msg->buf = data;
	err = i2c_transfer(adap, msg, 1);
	*value = data[0];
	i2c_put_adapter(adap);

	if (err >= 0)
		return 0;
	return err;
}
/* set keyboard backlight intensity */
int sx1_setkeylight(u8 keylight)
{
	if (keylight > SOFIA_MAX_LIGHT_VAL)
		keylight = SOFIA_MAX_LIGHT_VAL;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_KEYLIGHT_REG, keylight);
}
/* get current keylight intensity */
int sx1_getkeylight(u8 * keylight)
{
	return sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_KEYLIGHT_REG, keylight);
}
/* set LCD backlight intensity */
int sx1_setbacklight(u8 backlight)
{
	if (backlight > SOFIA_MAX_LIGHT_VAL)
		backlight = SOFIA_MAX_LIGHT_VAL;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_BACKLIGHT_REG,
				  backlight);
}
/* get current LCD backlight intensity */
int sx1_getbacklight (u8 * backlight)
{
	return sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_BACKLIGHT_REG,
				 backlight);
}
/* set LCD backlight power on/off */
int sx1_setmmipower(u8 onoff)
{
	int err;
	u8 dat = 0;
	err = sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, &dat);
	if (err < 0)
		return err;
	if (onoff)
		dat |= SOFIA_MMILIGHT_POWER;
	else
		dat &= ~SOFIA_MMILIGHT_POWER;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, dat);
}

/* set USB power on/off */
int sx1_setusbpower(u8 onoff)
{
	int err;
	u8 dat = 0;
	err = sx1_i2c_read_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, &dat);
	if (err < 0)
		return err;
	if (onoff)
		dat |= SOFIA_USB_POWER;
	else
		dat &= ~SOFIA_USB_POWER;
	return sx1_i2c_write_byte(SOFIA_I2C_ADDR, SOFIA_POWER1_REG, dat);
}

EXPORT_SYMBOL(sx1_setkeylight);
EXPORT_SYMBOL(sx1_getkeylight);
EXPORT_SYMBOL(sx1_setbacklight);
EXPORT_SYMBOL(sx1_getbacklight);
EXPORT_SYMBOL(sx1_setmmipower);
EXPORT_SYMBOL(sx1_setusbpower);

/*----------- Keypad -------------------------*/

static const unsigned int sx1_keymap[] = {
	KEY(3, 5, GROUP_0 | 117), /* camera Qt::Key_F17 */
	KEY(4, 0, GROUP_0 | 114), /* voice memo Qt::Key_F14 */
	KEY(4, 1, GROUP_2 | 114), /* voice memo */
	KEY(4, 2, GROUP_3 | 114), /* voice memo */
	KEY(0, 0, GROUP_1 | KEY_F12),	/* red button Qt::Key_Hangup */
	KEY(3, 4, GROUP_1 | KEY_LEFT),
	KEY(3, 2, GROUP_1 | KEY_DOWN),
	KEY(3, 1, GROUP_1 | KEY_RIGHT),
	KEY(3, 0, GROUP_1 | KEY_UP),
	KEY(3, 3, GROUP_1 | KEY_POWER), /* joystick press or Qt::Key_Select */
	KEY(0, 5, GROUP_1 | KEY_1),
	KEY(0, 4, GROUP_1 | KEY_2),
	KEY(0, 3, GROUP_1 | KEY_3),
	KEY(4, 3, GROUP_1 | KEY_4),
	KEY(4, 4, GROUP_1 | KEY_5),
	KEY(4, 5, GROUP_1 | KEY_KPASTERISK),/* "*" */
	KEY(1, 4, GROUP_1 | KEY_6),
	KEY(1, 5, GROUP_1 | KEY_7),
	KEY(1, 3, GROUP_1 | KEY_8),
	KEY(2, 3, GROUP_1 | KEY_9),
	KEY(2, 5, GROUP_1 | KEY_0),
	KEY(2, 4, GROUP_1 | 113), /* # F13 Toggle input method Qt::Key_F13 */
	KEY(1, 0, GROUP_1 | KEY_F11),	/* green button Qt::Key_Call */
	KEY(2, 1, GROUP_1 | KEY_YEN),	/* left soft Qt::Key_Context1 */
	KEY(2, 2, GROUP_1 | KEY_F8),	/* right soft Qt::Key_Back */
	KEY(1, 2, GROUP_1 | KEY_LEFTSHIFT), /* shift */
	KEY(1, 1, GROUP_1 | KEY_BACKSPACE), /* C (clear) */
	KEY(2, 0, GROUP_1 | KEY_F7),	/* menu Qt::Key_Menu */
};

static struct resource sx1_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data sx1_keymap_data = {
	.keymap		= sx1_keymap,
	.keymap_size	= ARRAY_SIZE(sx1_keymap),
};

static struct omap_kp_platform_data sx1_kp_data = {
	.rows		= 6,
	.cols		= 6,
	.keymap_data	= &sx1_keymap_data,
	.delay	= 80,
};

static struct platform_device sx1_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &sx1_kp_data,
	},
	.num_resources	= ARRAY_SIZE(sx1_kp_resources),
	.resource	= sx1_kp_resources,
};

/*----------- MTD -------------------------*/

static struct mtd_partition sx1_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
		.name		= "bootloader",
		.offset		= 0x01800000,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M - 2 * SZ_128K,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data sx1_flash_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= sx1_partitions,
	.nr_parts	= ARRAY_SIZE(sx1_partitions),
};

/* MTD Intel 4000 flash - new flashes */
static struct resource sx1_new_flash_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device sx1_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &sx1_flash_data,
	},
	.num_resources	= 1,
	.resource	= &sx1_new_flash_resource,
};

/*----------- USB -------------------------*/

static struct omap_usb_config sx1_usb_config __initdata = {
	.otg		= 0,
	.register_dev	= 1,
	.register_host	= 0,
	.hmc_mode	= 0,
	.pins[0]	= 2,
	.pins[1]	= 0,
	.pins[2]	= 0,
};

/*----------- LCD -------------------------*/

static const struct omap_lcd_config sx1_lcd_config __initconst = {
	.ctrl_name	= "internal",
};

/*-----------------------------------------*/
static struct platform_device *sx1_devices[] __initdata = {
	&sx1_flash_device,
	&sx1_kp_device,
};

/*-----------------------------------------*/

static struct gpiod_lookup_table sx1_gpio_table = {
	.dev_id = NULL,
	.table = {
		GPIO_LOOKUP("gpio-0-15", 1, "irda_off",
			    GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-0-15", 11, "switch",
			    GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-0-15", 15, "usb_on",
			    GPIO_ACTIVE_HIGH),
		{ }
	},
};

static void __init omap_sx1_init(void)
{
	struct gpio_desc *d;

	/* mux pins for uarts */
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);
	omap_cfg_reg(UART2_TX);
	omap_cfg_reg(UART2_RTS);
	omap_cfg_reg(UART3_TX);
	omap_cfg_reg(UART3_RX);

	platform_add_devices(sx1_devices, ARRAY_SIZE(sx1_devices));

	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	omap1_usb_init(&sx1_usb_config);
	sx1_mmc_init();
	gpiod_add_lookup_table(&sx1_gpio_table);

	/* turn on USB power */
	/* sx1_setusbpower(1); can't do it here because i2c is not ready */
	d = gpiod_get(NULL, "irda_off", GPIOD_OUT_HIGH);
	if (IS_ERR(d))
		pr_err("Unable to get IRDA OFF GPIO descriptor\n");
	else
		gpiod_put(d);
	d = gpiod_get(NULL, "switch", GPIOD_OUT_LOW);
	if (IS_ERR(d))
		pr_err("Unable to get SWITCH GPIO descriptor\n");
	else
		gpiod_put(d);
	d = gpiod_get(NULL, "usb_on", GPIOD_OUT_LOW);
	if (IS_ERR(d))
		pr_err("Unable to get USB ON GPIO descriptor\n");
	else
		gpiod_put(d);

	omapfb_set_lcd_config(&sx1_lcd_config);
}

MACHINE_START(SX1, "OMAP310 based Siemens SX1")
	.atag_offset	= 0x100,
	.map_io		= omap1_map_io,
	.init_early     = omap1_init_early,
	.init_irq	= omap1_init_irq,
	.init_machine	= omap_sx1_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
