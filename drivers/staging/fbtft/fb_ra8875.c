/*
 * FBTFT driver for the RA8875 LCD Controller
 * Copyright by Pf@nne & NOTRO
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include "fbtft.h"

#define DRVNAME "fb_ra8875"

static int write_spi(struct fbtft_par *par, void *buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf = buf,
		.len = len,
		.speed_hz = 1000000,
	};
	struct spi_message m;

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
			  "%s(len=%d): ", __func__, len);

	if (!par->spi) {
		dev_err(par->info->device,
			"%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(par->spi, &m);
}

static int init_display(struct fbtft_par *par)
{
	gpio_set_value(par->gpio.dc, 1);

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par,
		      "%s()\n", __func__);
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par,
		      "display size %dx%d\n",
		par->info->var.xres,
		par->info->var.yres);

	par->fbtftops.reset(par);

	if ((par->info->var.xres == 320) && (par->info->var.yres == 240)) {
		/* PLL clock frequency */
		write_reg(par, 0x88, 0x0A);
		write_reg(par, 0x89, 0x02);
		mdelay(10);
		/* color deep / MCU Interface */
		write_reg(par, 0x10, 0x0C);
		/* pixel clock period  */
		write_reg(par, 0x04, 0x03);
		mdelay(1);
		/* horizontal settings */
		write_reg(par, 0x14, 0x27);
		write_reg(par, 0x15, 0x00);
		write_reg(par, 0x16, 0x05);
		write_reg(par, 0x17, 0x04);
		write_reg(par, 0x18, 0x03);
		/* vertical settings */
		write_reg(par, 0x19, 0xEF);
		write_reg(par, 0x1A, 0x00);
		write_reg(par, 0x1B, 0x05);
		write_reg(par, 0x1C, 0x00);
		write_reg(par, 0x1D, 0x0E);
		write_reg(par, 0x1E, 0x00);
		write_reg(par, 0x1F, 0x02);
	} else if ((par->info->var.xres == 480) &&
		   (par->info->var.yres == 272)) {
		/* PLL clock frequency  */
		write_reg(par, 0x88, 0x0A);
		write_reg(par, 0x89, 0x02);
		mdelay(10);
		/* color deep / MCU Interface */
		write_reg(par, 0x10, 0x0C);
		/* pixel clock period  */
		write_reg(par, 0x04, 0x82);
		mdelay(1);
		/* horizontal settings */
		write_reg(par, 0x14, 0x3B);
		write_reg(par, 0x15, 0x00);
		write_reg(par, 0x16, 0x01);
		write_reg(par, 0x17, 0x00);
		write_reg(par, 0x18, 0x05);
		/* vertical settings */
		write_reg(par, 0x19, 0x0F);
		write_reg(par, 0x1A, 0x01);
		write_reg(par, 0x1B, 0x02);
		write_reg(par, 0x1C, 0x00);
		write_reg(par, 0x1D, 0x07);
		write_reg(par, 0x1E, 0x00);
		write_reg(par, 0x1F, 0x09);
	} else if ((par->info->var.xres == 640) &&
		   (par->info->var.yres == 480)) {
		/* PLL clock frequency */
		write_reg(par, 0x88, 0x0B);
		write_reg(par, 0x89, 0x02);
		mdelay(10);
		/* color deep / MCU Interface */
		write_reg(par, 0x10, 0x0C);
		/* pixel clock period */
		write_reg(par, 0x04, 0x01);
		mdelay(1);
		/* horizontal settings */
		write_reg(par, 0x14, 0x4F);
		write_reg(par, 0x15, 0x05);
		write_reg(par, 0x16, 0x0F);
		write_reg(par, 0x17, 0x01);
		write_reg(par, 0x18, 0x00);
		/* vertical settings */
		write_reg(par, 0x19, 0xDF);
		write_reg(par, 0x1A, 0x01);
		write_reg(par, 0x1B, 0x0A);
		write_reg(par, 0x1C, 0x00);
		write_reg(par, 0x1D, 0x0E);
		write_reg(par, 0x1E, 0x00);
		write_reg(par, 0x1F, 0x01);
	} else if ((par->info->var.xres == 800) &&
		   (par->info->var.yres == 480)) {
		/* PLL clock frequency */
		write_reg(par, 0x88, 0x0B);
		write_reg(par, 0x89, 0x02);
		mdelay(10);
		/* color deep / MCU Interface */
		write_reg(par, 0x10, 0x0C);
		/* pixel clock period */
		write_reg(par, 0x04, 0x81);
		mdelay(1);
		/* horizontal settings */
		write_reg(par, 0x14, 0x63);
		write_reg(par, 0x15, 0x03);
		write_reg(par, 0x16, 0x03);
		write_reg(par, 0x17, 0x02);
		write_reg(par, 0x18, 0x00);
		/* vertical settings */
		write_reg(par, 0x19, 0xDF);
		write_reg(par, 0x1A, 0x01);
		write_reg(par, 0x1B, 0x14);
		write_reg(par, 0x1C, 0x00);
		write_reg(par, 0x1D, 0x06);
		write_reg(par, 0x1E, 0x00);
		write_reg(par, 0x1F, 0x01);
	} else {
		dev_err(par->info->device, "display size is not supported!!");
		return -1;
	}

	/* PWM clock */
	write_reg(par, 0x8a, 0x81);
	write_reg(par, 0x8b, 0xFF);
	mdelay(10);

	/* Display ON */
	write_reg(par, 0x01, 0x80);
	mdelay(10);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* Set_Active_Window */
	write_reg(par, 0x30, xs & 0x00FF);
	write_reg(par, 0x31, (xs & 0xFF00) >> 8);
	write_reg(par, 0x32, ys & 0x00FF);
	write_reg(par, 0x33, (ys & 0xFF00) >> 8);
	write_reg(par, 0x34, (xs + xe) & 0x00FF);
	write_reg(par, 0x35, ((xs + xe) & 0xFF00) >> 8);
	write_reg(par, 0x36, (ys + ye) & 0x00FF);
	write_reg(par, 0x37, ((ys + ye) & 0xFF00) >> 8);

	/* Set_Memory_Write_Cursor */
	write_reg(par, 0x46,  xs & 0xff);
	write_reg(par, 0x47, (xs >> 8) & 0x03);
	write_reg(par, 0x48,  ys & 0xff);
	write_reg(par, 0x49, (ys >> 8) & 0x01);

	write_reg(par, 0x02);
}

static void write_reg8_bus8(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i, ret;
	u8 *buf = par->buf;

	/* slow down spi-speed for writing registers */
	par->fbtftops.write = write_spi;

	if (unlikely(par->debug & DEBUG_WRITE_REGISTER)) {
		va_start(args, len);
		for (i = 0; i < len; i++)
			buf[i] = (u8)va_arg(args, unsigned int);
		va_end(args);
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par, par->info->device,
				  u8, buf, len, "%s: ", __func__);
	}

	va_start(args, len);
	*buf++ = 0x80;
	*buf = (u8)va_arg(args, unsigned int);
	ret = par->fbtftops.write(par, par->buf, 2);
	if (ret < 0) {
		va_end(args);
		dev_err(par->info->device, "write() failed and returned %dn",
			ret);
		return;
	}
	len--;

	udelay(100);

	if (len) {
		buf = (u8 *)par->buf;
		*buf++ = 0x00;
		i = len;
		while (i--)
			*buf++ = (u8)va_arg(args, unsigned int);

		ret = par->fbtftops.write(par, par->buf, len + 1);
		if (ret < 0) {
			va_end(args);
			dev_err(par->info->device,
				"write() failed and returned %dn", ret);
			return;
		}
	}
	va_end(args);

	/* restore user spi-speed */
	par->fbtftops.write = fbtft_write_spi;
	udelay(100);
}

static int write_vmem16_bus8(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16;
	__be16 *txbuf16;
	size_t remain;
	size_t to_copy;
	size_t tx_array_size;
	int i;
	int ret = 0;
	size_t startbyte_size = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s(offset=%zu, len=%zu)\n",
		      __func__, offset, len);

	remain = len / 2;
	vmem16 = (u16 *)(par->info->screen_buffer + offset);
	tx_array_size = par->txbuf.len / 2;
	txbuf16 = par->txbuf.buf + 1;
	tx_array_size -= 2;
	*(u8 *)(par->txbuf.buf) = 0x00;
	startbyte_size = 1;

	while (remain) {
		to_copy = min(tx_array_size, remain);
		dev_dbg(par->info->device, "    to_copy=%zu, remain=%zu\n",
			to_copy, remain - to_copy);

		for (i = 0; i < to_copy; i++)
			txbuf16[i] = cpu_to_be16(vmem16[i]);

		vmem16 = vmem16 + to_copy;
		ret = par->fbtftops.write(par, par->txbuf.buf,
			startbyte_size + to_copy * 2);
		if (ret < 0)
			return ret;
		remain -= to_copy;
	}

	return ret;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.write_register = write_reg8_bus8,
		.write_vmem = write_vmem16_bus8,
		.write = write_spi,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "raio,ra8875", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ra8875");
MODULE_ALIAS("platform:ra8875");

MODULE_DESCRIPTION("FB driver for the RA8875 LCD Controller");
MODULE_AUTHOR("Pf@nne");
MODULE_LICENSE("GPL");
