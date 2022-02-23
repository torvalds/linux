// SPDX-License-Identifier: GPL-2.0
/*
 *  console driver for LCD2S 4x20 character displays connected through i2c.
 *  The display also has a spi interface, but the driver does not support
 *  this yet.
 *
 *  This is a driver allowing you to use a LCD2S 4x20 from modtronix
 *  engineering as auxdisplay character device.
 *
 *  (C) 2019 by Lemonage Software GmbH
 *  Author: Lars Pöschel <poeschel@lemonage.de>
 *  All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "charlcd.h"

#define LCD2S_CMD_CUR_MOVES_FWD		0x09
#define LCD2S_CMD_CUR_BLINK_OFF		0x10
#define LCD2S_CMD_CUR_UL_OFF		0x11
#define LCD2S_CMD_DISPLAY_OFF		0x12
#define LCD2S_CMD_CUR_BLINK_ON		0x18
#define LCD2S_CMD_CUR_UL_ON		0x19
#define LCD2S_CMD_DISPLAY_ON		0x1a
#define LCD2S_CMD_BACKLIGHT_OFF		0x20
#define LCD2S_CMD_BACKLIGHT_ON		0x28
#define LCD2S_CMD_WRITE			0x80
#define LCD2S_CMD_MOV_CUR_RIGHT		0x83
#define LCD2S_CMD_MOV_CUR_LEFT		0x84
#define LCD2S_CMD_SHIFT_RIGHT		0x85
#define LCD2S_CMD_SHIFT_LEFT		0x86
#define LCD2S_CMD_SHIFT_UP		0x87
#define LCD2S_CMD_SHIFT_DOWN		0x88
#define LCD2S_CMD_CUR_ADDR		0x89
#define LCD2S_CMD_CUR_POS		0x8a
#define LCD2S_CMD_CUR_RESET		0x8b
#define LCD2S_CMD_CLEAR			0x8c
#define LCD2S_CMD_DEF_CUSTOM_CHAR	0x92
#define LCD2S_CMD_READ_STATUS		0xd0

#define LCD2S_CHARACTER_SIZE		8

#define LCD2S_STATUS_BUF_MASK		0x7f

struct lcd2s_data {
	struct i2c_client *i2c;
	struct charlcd *charlcd;
};

static s32 lcd2s_wait_buf_free(const struct i2c_client *client, int count)
{
	s32 status;

	status = i2c_smbus_read_byte_data(client, LCD2S_CMD_READ_STATUS);
	if (status < 0)
		return status;

	while ((status & LCD2S_STATUS_BUF_MASK) < count) {
		mdelay(1);
		status = i2c_smbus_read_byte_data(client,
						  LCD2S_CMD_READ_STATUS);
		if (status < 0)
			return status;
	}
	return 0;
}

static int lcd2s_i2c_master_send(const struct i2c_client *client,
				 const char *buf, int count)
{
	s32 status;

	status = lcd2s_wait_buf_free(client, count);
	if (status < 0)
		return status;

	return i2c_master_send(client, buf, count);
}

static int lcd2s_i2c_smbus_write_byte(const struct i2c_client *client, u8 value)
{
	s32 status;

	status = lcd2s_wait_buf_free(client, 1);
	if (status < 0)
		return status;

	return i2c_smbus_write_byte(client, value);
}

static int lcd2s_print(struct charlcd *lcd, int c)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;
	u8 buf[2] = { LCD2S_CMD_WRITE, c };

	lcd2s_i2c_master_send(lcd2s->i2c, buf, sizeof(buf));
	return 0;
}

static int lcd2s_gotoxy(struct charlcd *lcd, unsigned int x, unsigned int y)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;
	u8 buf[] = { LCD2S_CMD_CUR_POS, y + 1, x + 1};

	lcd2s_i2c_master_send(lcd2s->i2c, buf, sizeof(buf));

	return 0;
}

static int lcd2s_home(struct charlcd *lcd)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_RESET);
	return 0;
}

static int lcd2s_init_display(struct charlcd *lcd)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	/* turn everything off, but display on */
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_DISPLAY_ON);
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_BACKLIGHT_OFF);
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_MOVES_FWD);
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_BLINK_OFF);
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_UL_OFF);
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CLEAR);

	return 0;
}

static int lcd2s_shift_cursor(struct charlcd *lcd, enum charlcd_shift_dir dir)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	if (dir == CHARLCD_SHIFT_LEFT)
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_MOV_CUR_LEFT);
	else
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_MOV_CUR_RIGHT);

	return 0;
}

static int lcd2s_shift_display(struct charlcd *lcd, enum charlcd_shift_dir dir)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	if (dir == CHARLCD_SHIFT_LEFT)
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_SHIFT_LEFT);
	else
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_SHIFT_RIGHT);

	return 0;
}

static void lcd2s_backlight(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	if (on)
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_BACKLIGHT_ON);
	else
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_BACKLIGHT_OFF);
}

static int lcd2s_display(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	if (on)
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_DISPLAY_ON);
	else
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_DISPLAY_OFF);

	return 0;
}

static int lcd2s_cursor(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	if (on)
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_UL_ON);
	else
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_UL_OFF);

	return 0;
}

static int lcd2s_blink(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	if (on)
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_BLINK_ON);
	else
		lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CUR_BLINK_OFF);

	return 0;
}

static int lcd2s_fontsize(struct charlcd *lcd, enum charlcd_fontsize size)
{
	return 0;
}

static int lcd2s_lines(struct charlcd *lcd, enum charlcd_lines lines)
{
	return 0;
}

static int lcd2s_redefine_char(struct charlcd *lcd, char *esc)
{
	/* Generator : LGcxxxxx...xx; must have <c> between '0'
	 * and '7', representing the numerical ASCII code of the
	 * redefined character, and <xx...xx> a sequence of 16
	 * hex digits representing 8 bytes for each character.
	 * Most LCDs will only use 5 lower bits of the 7 first
	 * bytes.
	 */

	struct lcd2s_data *lcd2s = lcd->drvdata;
	u8 buf[LCD2S_CHARACTER_SIZE + 2] = { LCD2S_CMD_DEF_CUSTOM_CHAR };
	u8 value;
	int shift, i;

	if (!strchr(esc, ';'))
		return 0;

	esc++;

	buf[1] = *(esc++) - '0';
	if (buf[1] > 7)
		return 1;

	i = 2;
	shift = 0;
	value = 0;
	while (*esc && i < LCD2S_CHARACTER_SIZE + 2) {
		int half;

		shift ^= 4;
		half = hex_to_bin(*esc++);
		if (half < 0)
			continue;

		value |= half << shift;
		if (shift == 0) {
			buf[i++] = value;
			value = 0;
		}
	}

	lcd2s_i2c_master_send(lcd2s->i2c, buf, sizeof(buf));
	return 1;
}

static int lcd2s_clear_display(struct charlcd *lcd)
{
	struct lcd2s_data *lcd2s = lcd->drvdata;

	/* This implicitly sets cursor to first row and column */
	lcd2s_i2c_smbus_write_byte(lcd2s->i2c, LCD2S_CMD_CLEAR);
	return 0;
}

static const struct charlcd_ops lcd2s_ops = {
	.print		= lcd2s_print,
	.backlight	= lcd2s_backlight,
	.gotoxy		= lcd2s_gotoxy,
	.home		= lcd2s_home,
	.clear_display	= lcd2s_clear_display,
	.init_display	= lcd2s_init_display,
	.shift_cursor	= lcd2s_shift_cursor,
	.shift_display	= lcd2s_shift_display,
	.display	= lcd2s_display,
	.cursor		= lcd2s_cursor,
	.blink		= lcd2s_blink,
	.fontsize	= lcd2s_fontsize,
	.lines		= lcd2s_lines,
	.redefine_char	= lcd2s_redefine_char,
};

static int lcd2s_i2c_probe(struct i2c_client *i2c,
				const struct i2c_device_id *id)
{
	struct charlcd *lcd;
	struct lcd2s_data *lcd2s;
	int err;

	if (!i2c_check_functionality(i2c->adapter,
			I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
			I2C_FUNC_SMBUS_WRITE_BLOCK_DATA))
		return -EIO;

	lcd2s = devm_kzalloc(&i2c->dev, sizeof(*lcd2s), GFP_KERNEL);
	if (!lcd2s)
		return -ENOMEM;

	/* Test, if the display is responding */
	err = lcd2s_i2c_smbus_write_byte(i2c, LCD2S_CMD_DISPLAY_OFF);
	if (err < 0)
		return err;

	lcd = charlcd_alloc();
	if (!lcd)
		return -ENOMEM;

	lcd->drvdata = lcd2s;
	lcd2s->i2c = i2c;
	lcd2s->charlcd = lcd;

	/* Required properties */
	err = device_property_read_u32(&i2c->dev, "display-height-chars",
			&lcd->height);
	if (err)
		goto fail1;

	err = device_property_read_u32(&i2c->dev, "display-width-chars",
			&lcd->width);
	if (err)
		goto fail1;

	lcd->ops = &lcd2s_ops;

	err = charlcd_register(lcd2s->charlcd);
	if (err)
		goto fail1;

	i2c_set_clientdata(i2c, lcd2s);
	return 0;

fail1:
	kfree(lcd);
	return err;
}

static int lcd2s_i2c_remove(struct i2c_client *i2c)
{
	struct lcd2s_data *lcd2s = i2c_get_clientdata(i2c);

	charlcd_unregister(lcd2s->charlcd);
	kfree(lcd2s->charlcd);
	return 0;
}

static const struct i2c_device_id lcd2s_i2c_id[] = {
	{ "lcd2s", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lcd2s_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id lcd2s_of_table[] = {
	{ .compatible = "modtronix,lcd2s" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcd2s_of_table);
#endif

static struct i2c_driver lcd2s_i2c_driver = {
	.driver = {
		.name = "lcd2s",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(lcd2s_of_table),
#endif
	},
	.probe = lcd2s_i2c_probe,
	.remove = lcd2s_i2c_remove,
	.id_table = lcd2s_i2c_id,
};

static int __init lcd2s_modinit(void)
{
	int ret = 0;

	ret = i2c_add_driver(&lcd2s_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register lcd2s driver\n");

	return ret;
}
module_init(lcd2s_modinit)

static void __exit lcd2s_exit(void)
{
	i2c_del_driver(&lcd2s_i2c_driver);
}
module_exit(lcd2s_exit)

MODULE_DESCRIPTION("LCD2S character display driver");
MODULE_AUTHOR("Lars Poeschel");
MODULE_LICENSE("GPL");
