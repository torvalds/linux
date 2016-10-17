#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_ssd1331"
#define WIDTH		96
#define HEIGHT		64
#define GAMMA_NUM	1
#define GAMMA_LEN	63
#define DEFAULT_GAMMA	"0 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2 2 " \
			"2 2 2 2 2 2 2" \

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	write_reg(par, 0xae); /* Display Off */
	write_reg(par, 0xa0, 0x70 | (par->bgr << 2)); /* Set Colour Depth */
	write_reg(par, 0x72); /* RGB colour */
	write_reg(par, 0xa1, 0x00); /* Set Display Start Line */
	write_reg(par, 0xa2, 0x00); /* Set Display Offset */
	write_reg(par, 0xa4); /* NORMALDISPLAY */
	write_reg(par, 0xa8, 0x3f); /* Set multiplex */
	write_reg(par, 0xad, 0x8e); /* Set master */
	/* write_reg(par, 0xb0, 0x0b);  Set power mode */
	write_reg(par, 0xb1, 0x31); /* Precharge */
	write_reg(par, 0xb3, 0xf0); /* Clock div */
	write_reg(par, 0x8a, 0x64); /* Precharge A */
	write_reg(par, 0x8b, 0x78); /* Precharge B */
	write_reg(par, 0x8c, 0x64); /* Precharge C */
	write_reg(par, 0xbb, 0x3a); /* Precharge level */
	write_reg(par, 0xbe, 0x3e); /* vcomh */
	write_reg(par, 0x87, 0x06); /* Master current */
	write_reg(par, 0x81, 0x91); /* Contrast A */
	write_reg(par, 0x82, 0x50); /* Contrast B */
	write_reg(par, 0x83, 0x7d); /* Contrast C */
	write_reg(par, 0xaf); /* Set Sleep Mode Display On */

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, 0x15, xs, xe);
	write_reg(par, 0x75, ys, ye);
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
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par, par->info->device, u8, buf, len, "%s: ", __func__);
	}

	va_start(args, len);

	*buf = (u8)va_arg(args, unsigned int);
	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, 0);
	ret = par->fbtftops.write(par, par->buf, sizeof(u8));
	if (ret < 0) {
		va_end(args);
		dev_err(par->info->device,
			"write() failed and returned %d\n", ret);
		return;
	}
	len--;

	if (len) {
		i = len;
		while (i--)
			*buf++ = (u8)va_arg(args, unsigned int);
		ret = par->fbtftops.write(par, par->buf, len * (sizeof(u8)));
		if (ret < 0) {
			va_end(args);
			dev_err(par->info->device,
				"write() failed and returned %d\n", ret);
			return;
		}
	}
	if (par->gpio.dc != -1)
		gpio_set_value(par->gpio.dc, 1);
	va_end(args);
}

/*
 * Grayscale Lookup Table
 * GS1 - GS63
 * The driver Gamma curve contains the relative values between the entries
 * in the Lookup table.
 *
 * From datasheet:
 * 8.8 Gray Scale Decoder
 *
 * there are total 180 Gamma Settings (Setting 0 to Setting 180)
 * available for the Gray Scale table.
 *
 * The gray scale is defined in incremental way, with reference
 * to the length of previous table entry:
 * Setting of GS1 has to be >= 0
 * Setting of GS2 has to be > Setting of GS1 +1
 * Setting of GS3 has to be > Setting of GS2 +1
 * :
 * Setting of GS63 has to be > Setting of GS62 +1
 *
 */
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	unsigned long tmp[GAMMA_NUM * GAMMA_LEN];
	int i, acc = 0;

	for (i = 0; i < 63; i++) {
		if (i > 0 && curves[i] < 2) {
			dev_err(par->info->device,
				"Illegal value in Grayscale Lookup Table at index %d. " \
				"Must be greater than 1\n", i);
			return -EINVAL;
		}
		acc += curves[i];
		tmp[i] = acc;
		if (acc > 180) {
			dev_err(par->info->device,
				"Illegal value(s) in Grayscale Lookup Table. " \
				"At index=%d, the accumulated value has exceeded 180\n", i);
			return -EINVAL;
		}
	}

	write_reg(par, 0xB8,
	tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5], tmp[6], tmp[7],
	tmp[8], tmp[9], tmp[10], tmp[11], tmp[12], tmp[13], tmp[14], tmp[15],
	tmp[16], tmp[17], tmp[18], tmp[19], tmp[20], tmp[21], tmp[22], tmp[23],
	tmp[24], tmp[25], tmp[26], tmp[27], tmp[28], tmp[29], tmp[30], tmp[31],
	tmp[32], tmp[33], tmp[34], tmp[35], tmp[36], tmp[37], tmp[38], tmp[39],
	tmp[40], tmp[41], tmp[42], tmp[43], tmp[44], tmp[45], tmp[46], tmp[47],
	tmp[48], tmp[49], tmp[50], tmp[51], tmp[52], tmp[53], tmp[54], tmp[55],
	tmp[56], tmp[57], tmp[58], tmp[59], tmp[60], tmp[61], tmp[62]);

	return 0;
}

static int blank(struct fbtft_par *par, bool on)
{
	fbtft_par_dbg(DEBUG_BLANK, par, "%s(blank=%s)\n",
		__func__, on ? "true" : "false");
	if (on)
		write_reg(par, 0xAE);
	else
		write_reg(par, 0xAF);
	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.gamma_num = GAMMA_NUM,
	.gamma_len = GAMMA_LEN,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.write_register = write_reg8_bus8,
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_gamma = set_gamma,
		.blank = blank,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "solomon,ssd1331", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ssd1331");
MODULE_ALIAS("platform:ssd1331");

MODULE_DESCRIPTION("SSD1331 OLED Driver");
MODULE_AUTHOR("Alec Smecher (adapted from SSD1351 by James Davies)");
MODULE_LICENSE("GPL");
