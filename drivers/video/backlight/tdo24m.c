// SPDX-License-Identifier: GPL-2.0-only
/*
 * tdo24m - SPI-based drivers for Toppoly TDO24M series LCD panels
 *
 * Copyright (C) 2008 Marvell International Ltd.
 *	Eric Miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/tdo24m.h>
#include <linux/fb.h>
#include <linux/lcd.h>
#include <linux/slab.h>

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

#define TDO24M_SPI_BUFF_SIZE	(4)
#define MODE_QVGA	0
#define MODE_VGA	1

struct tdo24m {
	struct spi_device	*spi_dev;
	struct lcd_device	*lcd_dev;

	struct spi_message	msg;
	struct spi_transfer	xfer;
	uint8_t			*buf;

	int (*adj_mode)(struct tdo24m *lcd, int mode);
	int color_invert;

	int			power;
	int			mode;
};

/* use bit 30, 31 as the indicator of command parameter number */
#define CMD0(x)		((0 << 30) | (x))
#define CMD1(x, x1)	((1 << 30) | ((x) << 9) | 0x100 | (x1))
#define CMD2(x, x1, x2)	((2 << 30) | ((x) << 18) | 0x20000 |\
			((x1) << 9) | 0x100 | (x2))
#define CMD_NULL	(-1)

static const uint32_t lcd_panel_reset[] = {
	CMD0(0x1), /* reset */
	CMD0(0x0), /* nop */
	CMD0(0x0), /* nop */
	CMD0(0x0), /* nop */
	CMD_NULL,
};

static const uint32_t lcd_panel_on[] = {
	CMD0(0x29),		/* Display ON */
	CMD2(0xB8, 0xFF, 0xF9),	/* Output Control */
	CMD0(0x11),		/* Sleep out */
	CMD1(0xB0, 0x16),	/* Wake */
	CMD_NULL,
};

static const uint32_t lcd_panel_off[] = {
	CMD0(0x28),		/* Display OFF */
	CMD2(0xB8, 0x80, 0x02),	/* Output Control */
	CMD0(0x10),		/* Sleep in */
	CMD1(0xB0, 0x00),	/* Deep stand by in */
	CMD_NULL,
};

static const uint32_t lcd_vga_pass_through_tdo24m[] = {
	CMD1(0xB0, 0x16),
	CMD1(0xBC, 0x80),
	CMD1(0xE1, 0x00),
	CMD1(0x36, 0x50),
	CMD1(0x3B, 0x00),
	CMD_NULL,
};

static const uint32_t lcd_qvga_pass_through_tdo24m[] = {
	CMD1(0xB0, 0x16),
	CMD1(0xBC, 0x81),
	CMD1(0xE1, 0x00),
	CMD1(0x36, 0x50),
	CMD1(0x3B, 0x22),
	CMD_NULL,
};

static const uint32_t lcd_vga_transfer_tdo24m[] = {
	CMD1(0xcf, 0x02),	/* Blanking period control (1) */
	CMD2(0xd0, 0x08, 0x04),	/* Blanking period control (2) */
	CMD1(0xd1, 0x01),	/* CKV timing control on/off */
	CMD2(0xd2, 0x14, 0x00),	/* CKV 1,2 timing control */
	CMD2(0xd3, 0x1a, 0x0f),	/* OEV timing control */
	CMD2(0xd4, 0x1f, 0xaf),	/* ASW timing control (1) */
	CMD1(0xd5, 0x14),	/* ASW timing control (2) */
	CMD0(0x21),		/* Invert for normally black display */
	CMD0(0x29),		/* Display on */
	CMD_NULL,
};

static const uint32_t lcd_qvga_transfer[] = {
	CMD1(0xd6, 0x02),	/* Blanking period control (1) */
	CMD2(0xd7, 0x08, 0x04),	/* Blanking period control (2) */
	CMD1(0xd8, 0x01),	/* CKV timing control on/off */
	CMD2(0xd9, 0x00, 0x08),	/* CKV 1,2 timing control */
	CMD2(0xde, 0x05, 0x0a),	/* OEV timing control */
	CMD2(0xdf, 0x0a, 0x19),	/* ASW timing control (1) */
	CMD1(0xe0, 0x0a),	/* ASW timing control (2) */
	CMD0(0x21),		/* Invert for normally black display */
	CMD0(0x29),		/* Display on */
	CMD_NULL,
};

static const uint32_t lcd_vga_pass_through_tdo35s[] = {
	CMD1(0xB0, 0x16),
	CMD1(0xBC, 0x80),
	CMD1(0xE1, 0x00),
	CMD1(0x3B, 0x00),
	CMD_NULL,
};

static const uint32_t lcd_qvga_pass_through_tdo35s[] = {
	CMD1(0xB0, 0x16),
	CMD1(0xBC, 0x81),
	CMD1(0xE1, 0x00),
	CMD1(0x3B, 0x22),
	CMD_NULL,
};

static const uint32_t lcd_vga_transfer_tdo35s[] = {
	CMD1(0xcf, 0x02),	/* Blanking period control (1) */
	CMD2(0xd0, 0x08, 0x04),	/* Blanking period control (2) */
	CMD1(0xd1, 0x01),	/* CKV timing control on/off */
	CMD2(0xd2, 0x00, 0x1e),	/* CKV 1,2 timing control */
	CMD2(0xd3, 0x14, 0x28),	/* OEV timing control */
	CMD2(0xd4, 0x28, 0x64),	/* ASW timing control (1) */
	CMD1(0xd5, 0x28),	/* ASW timing control (2) */
	CMD0(0x21),		/* Invert for normally black display */
	CMD0(0x29),		/* Display on */
	CMD_NULL,
};

static const uint32_t lcd_panel_config[] = {
	CMD2(0xb8, 0xff, 0xf9),	/* Output control */
	CMD0(0x11),		/* sleep out */
	CMD1(0xba, 0x01),	/* Display mode (1) */
	CMD1(0xbb, 0x00),	/* Display mode (2) */
	CMD1(0x3a, 0x60),	/* Display mode 18-bit RGB */
	CMD1(0xbf, 0x10),	/* Drive system change control */
	CMD1(0xb1, 0x56),	/* Booster operation setup */
	CMD1(0xb2, 0x33),	/* Booster mode setup */
	CMD1(0xb3, 0x11),	/* Booster frequency setup */
	CMD1(0xb4, 0x02),	/* Op amp/system clock */
	CMD1(0xb5, 0x35),	/* VCS voltage */
	CMD1(0xb6, 0x40),	/* VCOM voltage */
	CMD1(0xb7, 0x03),	/* External display signal */
	CMD1(0xbd, 0x00),	/* ASW slew rate */
	CMD1(0xbe, 0x00),	/* Dummy data for QuadData operation */
	CMD1(0xc0, 0x11),	/* Sleep out FR count (A) */
	CMD1(0xc1, 0x11),	/* Sleep out FR count (B) */
	CMD1(0xc2, 0x11),	/* Sleep out FR count (C) */
	CMD2(0xc3, 0x20, 0x40),	/* Sleep out FR count (D) */
	CMD2(0xc4, 0x60, 0xc0),	/* Sleep out FR count (E) */
	CMD2(0xc5, 0x10, 0x20),	/* Sleep out FR count (F) */
	CMD1(0xc6, 0xc0),	/* Sleep out FR count (G) */
	CMD2(0xc7, 0x33, 0x43),	/* Gamma 1 fine tuning (1) */
	CMD1(0xc8, 0x44),	/* Gamma 1 fine tuning (2) */
	CMD1(0xc9, 0x33),	/* Gamma 1 inclination adjustment */
	CMD1(0xca, 0x00),	/* Gamma 1 blue offset adjustment */
	CMD2(0xec, 0x01, 0xf0),	/* Horizontal clock cycles */
	CMD_NULL,
};

static int tdo24m_writes(struct tdo24m *lcd, const uint32_t *array)
{
	struct spi_transfer *x = &lcd->xfer;
	const uint32_t *p = array;
	uint32_t data;
	int nparams, err = 0;

	for (; *p != CMD_NULL; p++) {
		if (!lcd->color_invert && *p == CMD0(0x21))
			continue;

		nparams = (*p >> 30) & 0x3;

		data = *p << (7 - nparams);
		switch (nparams) {
		case 0:
			lcd->buf[0] = (data >> 8) & 0xff;
			lcd->buf[1] = data & 0xff;
			break;
		case 1:
			lcd->buf[0] = (data >> 16) & 0xff;
			lcd->buf[1] = (data >> 8) & 0xff;
			lcd->buf[2] = data & 0xff;
			break;
		case 2:
			lcd->buf[0] = (data >> 24) & 0xff;
			lcd->buf[1] = (data >> 16) & 0xff;
			lcd->buf[2] = (data >> 8) & 0xff;
			lcd->buf[3] = data & 0xff;
			break;
		default:
			continue;
		}
		x->len = nparams + 2;
		err = spi_sync(lcd->spi_dev, &lcd->msg);
		if (err)
			break;
	}

	return err;
}

static int tdo24m_adj_mode(struct tdo24m *lcd, int mode)
{
	switch (mode) {
	case MODE_VGA:
		tdo24m_writes(lcd, lcd_vga_pass_through_tdo24m);
		tdo24m_writes(lcd, lcd_panel_config);
		tdo24m_writes(lcd, lcd_vga_transfer_tdo24m);
		break;
	case MODE_QVGA:
		tdo24m_writes(lcd, lcd_qvga_pass_through_tdo24m);
		tdo24m_writes(lcd, lcd_panel_config);
		tdo24m_writes(lcd, lcd_qvga_transfer);
		break;
	default:
		return -EINVAL;
	}

	lcd->mode = mode;
	return 0;
}

static int tdo35s_adj_mode(struct tdo24m *lcd, int mode)
{
	switch (mode) {
	case MODE_VGA:
		tdo24m_writes(lcd, lcd_vga_pass_through_tdo35s);
		tdo24m_writes(lcd, lcd_panel_config);
		tdo24m_writes(lcd, lcd_vga_transfer_tdo35s);
		break;
	case MODE_QVGA:
		tdo24m_writes(lcd, lcd_qvga_pass_through_tdo35s);
		tdo24m_writes(lcd, lcd_panel_config);
		tdo24m_writes(lcd, lcd_qvga_transfer);
		break;
	default:
		return -EINVAL;
	}

	lcd->mode = mode;
	return 0;
}

static int tdo24m_power_on(struct tdo24m *lcd)
{
	int err;

	err = tdo24m_writes(lcd, lcd_panel_on);
	if (err)
		goto out;

	err = tdo24m_writes(lcd, lcd_panel_reset);
	if (err)
		goto out;

	err = lcd->adj_mode(lcd, lcd->mode);
out:
	return err;
}

static int tdo24m_power_off(struct tdo24m *lcd)
{
	return tdo24m_writes(lcd, lcd_panel_off);
}

static int tdo24m_power(struct tdo24m *lcd, int power)
{
	int ret = 0;

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = tdo24m_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = tdo24m_power_off(lcd);

	if (!ret)
		lcd->power = power;

	return ret;
}


static int tdo24m_set_power(struct lcd_device *ld, int power)
{
	struct tdo24m *lcd = lcd_get_data(ld);

	return tdo24m_power(lcd, power);
}

static int tdo24m_get_power(struct lcd_device *ld)
{
	struct tdo24m *lcd = lcd_get_data(ld);

	return lcd->power;
}

static int tdo24m_set_mode(struct lcd_device *ld, struct fb_videomode *m)
{
	struct tdo24m *lcd = lcd_get_data(ld);
	int mode = MODE_QVGA;

	if (m->xres == 640 || m->xres == 480)
		mode = MODE_VGA;

	if (lcd->mode == mode)
		return 0;

	return lcd->adj_mode(lcd, mode);
}

static const struct lcd_ops tdo24m_ops = {
	.get_power	= tdo24m_get_power,
	.set_power	= tdo24m_set_power,
	.set_mode	= tdo24m_set_mode,
};

static int tdo24m_probe(struct spi_device *spi)
{
	struct tdo24m *lcd;
	struct spi_message *m;
	struct spi_transfer *x;
	struct tdo24m_platform_data *pdata;
	enum tdo24m_model model;
	int err;

	pdata = dev_get_platdata(&spi->dev);
	if (pdata)
		model = pdata->model;
	else
		model = TDO24M;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3;
	err = spi_setup(spi);
	if (err)
		return err;

	lcd = devm_kzalloc(&spi->dev, sizeof(struct tdo24m), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	lcd->spi_dev = spi;
	lcd->power = FB_BLANK_POWERDOWN;
	lcd->mode = MODE_VGA;	/* default to VGA */

	lcd->buf = devm_kzalloc(&spi->dev, TDO24M_SPI_BUFF_SIZE, GFP_KERNEL);
	if (lcd->buf == NULL)
		return -ENOMEM;

	m = &lcd->msg;
	x = &lcd->xfer;

	spi_message_init(m);

	x->cs_change = 0;
	x->tx_buf = &lcd->buf[0];
	spi_message_add_tail(x, m);

	switch (model) {
	case TDO24M:
		lcd->color_invert = 1;
		lcd->adj_mode = tdo24m_adj_mode;
		break;
	case TDO35S:
		lcd->adj_mode = tdo35s_adj_mode;
		lcd->color_invert = 0;
		break;
	default:
		dev_err(&spi->dev, "Unsupported model");
		return -EINVAL;
	}

	lcd->lcd_dev = devm_lcd_device_register(&spi->dev, "tdo24m", &spi->dev,
						lcd, &tdo24m_ops);
	if (IS_ERR(lcd->lcd_dev))
		return PTR_ERR(lcd->lcd_dev);

	spi_set_drvdata(spi, lcd);
	err = tdo24m_power(lcd, FB_BLANK_UNBLANK);
	if (err)
		return err;

	return 0;
}

static void tdo24m_remove(struct spi_device *spi)
{
	struct tdo24m *lcd = spi_get_drvdata(spi);

	tdo24m_power(lcd, FB_BLANK_POWERDOWN);
}

#ifdef CONFIG_PM_SLEEP
static int tdo24m_suspend(struct device *dev)
{
	struct tdo24m *lcd = dev_get_drvdata(dev);

	return tdo24m_power(lcd, FB_BLANK_POWERDOWN);
}

static int tdo24m_resume(struct device *dev)
{
	struct tdo24m *lcd = dev_get_drvdata(dev);

	return tdo24m_power(lcd, FB_BLANK_UNBLANK);
}
#endif

static SIMPLE_DEV_PM_OPS(tdo24m_pm_ops, tdo24m_suspend, tdo24m_resume);

/* Power down all displays on reboot, poweroff or halt */
static void tdo24m_shutdown(struct spi_device *spi)
{
	struct tdo24m *lcd = spi_get_drvdata(spi);

	tdo24m_power(lcd, FB_BLANK_POWERDOWN);
}

static struct spi_driver tdo24m_driver = {
	.driver = {
		.name		= "tdo24m",
		.pm		= &tdo24m_pm_ops,
	},
	.probe		= tdo24m_probe,
	.remove		= tdo24m_remove,
	.shutdown	= tdo24m_shutdown,
};

module_spi_driver(tdo24m_driver);

MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>");
MODULE_DESCRIPTION("Driver for Toppoly TDO24M LCD Panel");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:tdo24m");
