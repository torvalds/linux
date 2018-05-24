// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the Watterott LCD Controller
 *
 * Copyright (C) 2013 Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME			"fb_watterott"
#define WIDTH			320
#define HEIGHT			240
#define FPS			5
#define TXBUFLEN		1024
#define DEFAULT_BRIGHTNESS	50

#define CMD_VERSION		0x01
#define CMD_LCD_LED		0x10
#define CMD_LCD_RESET		0x11
#define CMD_LCD_ORIENTATION	0x20
#define CMD_LCD_DRAWIMAGE	0x27
#define COLOR_RGB323		8
#define COLOR_RGB332		9
#define COLOR_RGB233		10
#define COLOR_RGB565		16

static short mode = 565;
module_param(mode, short, 0000);
MODULE_PARM_DESC(mode, "RGB color transfer mode: 332, 565 (default)");

static void write_reg8_bus8(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i, ret;
	u8 *buf = par->buf;

	va_start(args, len);
	for (i = 0; i < len; i++)
		*buf++ = (u8)va_arg(args, unsigned int);
	va_end(args);

	fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par,
		par->info->device, u8, par->buf, len, "%s: ", __func__);

	ret = par->fbtftops.write(par, par->buf, len);
	if (ret < 0) {
		dev_err(par->info->device,
			"write() failed and returned %d\n", ret);
		return;
	}
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	unsigned int start_line, end_line;
	u16 *vmem16 = (u16 *)(par->info->screen_buffer + offset);
	__be16 *pos = par->txbuf.buf + 1;
	__be16 *buf16 = par->txbuf.buf + 10;
	int i, j;
	int ret = 0;

	start_line = offset / par->info->fix.line_length;
	end_line = start_line + (len / par->info->fix.line_length) - 1;

	/* Set command header. pos: x, y, w, h */
	((u8 *)par->txbuf.buf)[0] = CMD_LCD_DRAWIMAGE;
	pos[0] = 0;
	pos[2] = cpu_to_be16(par->info->var.xres);
	pos[3] = cpu_to_be16(1);
	((u8 *)par->txbuf.buf)[9] = COLOR_RGB565;

	for (i = start_line; i <= end_line; i++) {
		pos[1] = cpu_to_be16(i);
		for (j = 0; j < par->info->var.xres; j++)
			buf16[j] = cpu_to_be16(*vmem16++);
		ret = par->fbtftops.write(par,
			par->txbuf.buf, 10 + par->info->fix.line_length);
		if (ret < 0)
			return ret;
		udelay(300);
	}

	return 0;
}

#define RGB565toRGB323(c) (((c&0xE000)>>8) | ((c&0600)>>6) | ((c&0x001C)>>2))
#define RGB565toRGB332(c) (((c&0xE000)>>8) | ((c&0700)>>6) | ((c&0x0018)>>3))
#define RGB565toRGB233(c) (((c&0xC000)>>8) | ((c&0700)>>5) | ((c&0x001C)>>2))

static int write_vmem_8bit(struct fbtft_par *par, size_t offset, size_t len)
{
	unsigned int start_line, end_line;
	u16 *vmem16 = (u16 *)(par->info->screen_buffer + offset);
	__be16 *pos = par->txbuf.buf + 1;
	u8 *buf8 = par->txbuf.buf + 10;
	int i, j;
	int ret = 0;

	start_line = offset / par->info->fix.line_length;
	end_line = start_line + (len / par->info->fix.line_length) - 1;

	/* Set command header. pos: x, y, w, h */
	((u8 *)par->txbuf.buf)[0] = CMD_LCD_DRAWIMAGE;
	pos[0] = 0;
	pos[2] = cpu_to_be16(par->info->var.xres);
	pos[3] = cpu_to_be16(1);
	((u8 *)par->txbuf.buf)[9] = COLOR_RGB332;

	for (i = start_line; i <= end_line; i++) {
		pos[1] = cpu_to_be16(i);
		for (j = 0; j < par->info->var.xres; j++) {
			buf8[j] = RGB565toRGB332(*vmem16);
			vmem16++;
		}
		ret = par->fbtftops.write(par,
			par->txbuf.buf, 10 + par->info->var.xres);
		if (ret < 0)
			return ret;
		udelay(700);
	}

	return 0;
}

static unsigned int firmware_version(struct fbtft_par *par)
{
	u8 rxbuf[4] = {0, };

	write_reg(par, CMD_VERSION);
	par->fbtftops.read(par, rxbuf, 4);
	if (rxbuf[1] != '.')
		return 0;

	return (rxbuf[0] - '0') << 8 | (rxbuf[2] - '0') << 4 | (rxbuf[3] - '0');
}

static int init_display(struct fbtft_par *par)
{
	int ret;
	unsigned int version;
	u8 save_mode;

	/* enable SPI interface by having CS and MOSI low during reset */
	save_mode = par->spi->mode;
	par->spi->mode |= SPI_CS_HIGH;
	ret = spi_setup(par->spi); /* set CS inactive low */
	if (ret) {
		dev_err(par->info->device, "Could not set SPI_CS_HIGH\n");
		return ret;
	}
	write_reg(par, 0x00); /* make sure mode is set */

	mdelay(50);
	par->fbtftops.reset(par);
	mdelay(1000);
	par->spi->mode = save_mode;
	ret = spi_setup(par->spi);
	if (ret) {
		dev_err(par->info->device, "Could not restore SPI mode\n");
		return ret;
	}
	write_reg(par, 0x00);

	version = firmware_version(par);
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "Firmware version: %x.%02x\n",
						version >> 8, version & 0xFF);

	if (mode == 332)
		par->fbtftops.write_vmem = write_vmem_8bit;
	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* not used on this controller */
}

static int set_var(struct fbtft_par *par)
{
	u8 rotate;

	/* this controller rotates clock wise */
	switch (par->info->var.rotate) {
	case 90:
		rotate = 27;
		break;
	case 180:
		rotate = 18;
		break;
	case 270:
		rotate = 9;
		break;
	default:
		rotate = 0;
	}
	write_reg(par, CMD_LCD_ORIENTATION, rotate);

	return 0;
}

static int verify_gpios(struct fbtft_par *par)
{
	if (par->gpio.reset < 0) {
		dev_err(par->info->device, "Missing 'reset' gpio. Aborting.\n");
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_FB_BACKLIGHT
static int backlight_chip_update_status(struct backlight_device *bd)
{
	struct fbtft_par *par = bl_get_data(bd);
	int brightness = bd->props.brightness;

	fbtft_par_dbg(DEBUG_BACKLIGHT, par,
		"%s: brightness=%d, power=%d, fb_blank=%d\n",
		__func__, bd->props.brightness, bd->props.power,
		bd->props.fb_blank);

	if (bd->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	write_reg(par, CMD_LCD_LED, brightness);

	return 0;
}

static const struct backlight_ops bl_ops = {
	.update_status = backlight_chip_update_status,
};

static void register_chip_backlight(struct fbtft_par *par)
{
	struct backlight_device *bd;
	struct backlight_properties bl_props = { 0, };

	bl_props.type = BACKLIGHT_RAW;
	bl_props.power = FB_BLANK_POWERDOWN;
	bl_props.max_brightness = 100;
	bl_props.brightness = DEFAULT_BRIGHTNESS;

	bd = backlight_device_register(dev_driver_string(par->info->device),
				par->info->device, par, &bl_ops, &bl_props);
	if (IS_ERR(bd)) {
		dev_err(par->info->device,
			"cannot register backlight device (%ld)\n",
			PTR_ERR(bd));
		return;
	}
	par->info->bl_dev = bd;

	if (!par->fbtftops.unregister_backlight)
		par->fbtftops.unregister_backlight = fbtft_unregister_backlight;
}
#else
#define register_chip_backlight NULL
#endif

static struct fbtft_display display = {
	.regwidth = 8,
	.buswidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.fps = FPS,
	.txbuflen = TXBUFLEN,
	.fbtftops = {
		.write_register = write_reg8_bus8,
		.write_vmem = write_vmem,
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.verify_gpios = verify_gpios,
		.register_backlight = register_chip_backlight,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "watterott,openlcd", &display);

MODULE_ALIAS("spi:" DRVNAME);

MODULE_DESCRIPTION("FB driver for the Watterott LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
