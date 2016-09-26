/*
 * FBTFT driver for the RA8873 LCD Controller
 * Copyright(c) 2016 mountainH & henryT
 * Base on Pf@nne & NOTRO FBTFT driver for the RA8875 LCD Controller
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include "fbtft.h"

#define DRVNAME "fb_ra8873"

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

	if ((par->info->var.xres == 800) && (par->info->var.yres == 480)) {
		/* PLL clock initial */
		write_reg(par, 0x05, 0x06);
                write_reg(par, 0x06, 0x17);/*((30*8/10)-1);(SCAN_FREQ*8/10)-1; SCAN_FREQ=30MHz*/ 
		write_reg(par, 0x07, 0x02);
                write_reg(par, 0x08, 0x0F);/*((80*2/10)-1);(DRAM_FREQ*2/10)-1; DRAM_FREQ=80MHz*/
                write_reg(par, 0x09, 0x02);            
                write_reg(par, 0x0A, 0x0F);/*((80*2/10)-1);(CORE_FREQ*2/10)-1; CORE_FREQ=80MHz*/
                write_reg(par, 0x01, 0x80);
                mdelay(10);
                /* SDRAM initial 64Mbit w9864g6 */
                write_reg(par, 0xe0, 0x28);
                write_reg(par, 0xe1, 0x02);
                write_reg(par, 0xe2, 0xE2);/*Auto_Refresh=(64*DRAM_FREQ*1000)/(4096);*/
                write_reg(par, 0xe3, 0x04);/*Auto_Refresh>>8*/
                write_reg(par, 0xe4, 0x01);
                mdelay(10);

		/* LCD output / MCU Interface */
		write_reg(par, 0x01, 0x80);
                /*MCU interface and data format*/
                write_reg(par, 0x02, 0x00);
                /*Main window color depth for display and rgb interface output mode*/
                write_reg(par, 0x10, 0x04);
                /*rgb interface timing setting for AT070TN92*/
                write_reg(par, 0x12, 0x80);
                write_reg(par, 0x13, 0x00);

                write_reg(par, 0x14, 0x63);
                write_reg(par, 0x15, 0x00);
                write_reg(par, 0x1A, 0xDF);
                write_reg(par, 0x1B, 0x01);
                
                write_reg(par, 0x16, 0x03);
                write_reg(par, 0x17, 0x06);
                write_reg(par, 0x18, 0x01);
                write_reg(par, 0x19, 0x00);
                
                write_reg(par, 0x1C, 0x0E);
                write_reg(par, 0x1D, 0x00);
                write_reg(par, 0x1E, 0x0B);
                write_reg(par, 0x1F, 0x07);

                /*Main image start address 0x00180000*/
                write_reg(par, 0x20, 0x00);
                write_reg(par, 0x21, 0x00);
                write_reg(par, 0x22, 0x18);
                write_reg(par, 0x23, 0x00);
                /*Main image width*/
                write_reg(par, 0x24, 0x20);
                write_reg(par, 0x25, 0x03);

                /*Main window start address*/
                write_reg(par, 0x26, 0x00);
                write_reg(par, 0x27, 0x00);
                write_reg(par, 0x28, 0x00);
                write_reg(par, 0x29, 0x00);

                /*Canvas image start address 0x00180000*/
                write_reg(par, 0x50, 0x00);
                write_reg(par, 0x51, 0x00);
                write_reg(par, 0x52, 0x18);
                write_reg(par, 0x53, 0x00);

                /*Canvas image width*/
                write_reg(par, 0x54, 0x20);
                write_reg(par, 0x55, 0x03);

                /*Active window setting*/
                write_reg(par, 0x56, 0x00);
                write_reg(par, 0x57, 0x00);
                write_reg(par, 0x58, 0x00);
                write_reg(par, 0x59, 0x00);
                write_reg(par, 0x5A, 0x20);
                write_reg(par, 0x5B, 0x03);
                write_reg(par, 0x5C, 0xE0);
                write_reg(par, 0x5D, 0x01);

                /*Canvas mode and color depth*/
                write_reg(par, 0x5E, 0x01);

  
	} else {
		dev_err(par->info->device, "display size is not supported!!");
		return -1;
	}


	 /* PWM0 clock prescaler*/
         write_reg(par, 0x84, 0x03);  
         write_reg(par, 0x85, 0xA2);
         /* PWM0 clock per period*/  
         write_reg(par, 0x8a, 0xff);  
         write_reg(par, 0x8b, 0x00);
         /* PWM0 clock duty*/  
         write_reg(par, 0x88, 0xff);  
         write_reg(par, 0x89, 0x00);
         /* PWM0 config and start*/
         write_reg(par, 0x86, 0x07);


	/* Display ON */
	write_reg(par, 0x12, 0xC0);
	mdelay(20);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{

	/* Set_Memory_Write_Cursor */
	write_reg(par, 0x5f,  xs & 0xff);
	write_reg(par, 0x60, (xs >> 8) & 0x1f);
	write_reg(par, 0x61,  ys & 0xff);
	write_reg(par, 0x62, (ys >> 8) & 0x1f);

	write_reg(par, 0x04);
}

static void write_reg8_bus8(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i, ret;
	u8 *buf = par->buf;


	if (unlikely(par->debug & DEBUG_WRITE_REGISTER)) {
		va_start(args, len);
		for (i = 0; i < len; i++)
			buf[i] = (u8)va_arg(args, unsigned int);
		va_end(args);
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par, par->info->device,
			u8, buf, len, "%s: ", __func__);
	}

	va_start(args, len);
	*buf++ = 0x00;
	*buf = (u8)va_arg(args, unsigned int);
	ret = par->fbtftops.write(par, par->buf, 2);
	if (ret < 0) {
		va_end(args);
		dev_err(par->info->device, "write() failed and returned %dn",
			ret);
		return;
	}
	len--;

	udelay(1);

	if (len) {
		buf = (u8 *)par->buf;
		*buf++ = 0x80;
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

}

static int write_vmem16_bus8(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16;
	u16 *txbuf16 = (u16 *)par->txbuf.buf;
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
		txbuf16 = (u16 *)(par->txbuf.buf + 1);
		tx_array_size -= 2;
		*(u8 *)(par->txbuf.buf) = 0x80;
		startbyte_size = 1;

	while (remain) {
		to_copy = remain > tx_array_size ? tx_array_size : remain;
		dev_dbg(par->info->device, "    to_copy=%zu, remain=%zu\n",
			to_copy, remain - to_copy);

		for (i = 0; i < to_copy; i++)
			txbuf16[i] = cpu_to_le16(vmem16[i]);

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

	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "raio,ra8873", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ra8873");
MODULE_ALIAS("platform:ra8873");

MODULE_DESCRIPTION("FB driver for the RA8873 LCD Controller");
MODULE_AUTHOR("mountainH & henryT");
MODULE_LICENSE("GPL");
