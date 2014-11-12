/* linux/drivers/video/exynos/s6e8ax0.c
 *
 * MIPI-DSI based s6e8ax0 AMOLED lcd 4.65 inch panel driver.
 *
 * Inki Dae, <inki.dae@samsung.com>
 * Donghwa Lee, <dh09.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/lcd.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/exynos_mipi_dsim.h>

#define LDI_MTP_LENGTH		24
#define DSIM_PM_STABLE_TIME	10
#define MIN_BRIGHTNESS		0
#define MAX_BRIGHTNESS		24
#define GAMMA_TABLE_COUNT	26

#define POWER_IS_ON(pwr)	((pwr) == FB_BLANK_UNBLANK)
#define POWER_IS_OFF(pwr)	((pwr) == FB_BLANK_POWERDOWN)
#define POWER_IS_NRM(pwr)	((pwr) == FB_BLANK_NORMAL)

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

enum {
	DSIM_NONE_STATE = 0,
	DSIM_RESUME_COMPLETE = 1,
	DSIM_FRAME_DONE = 2,
};

struct s6e8ax0 {
	struct device	*dev;
	unsigned int			power;
	unsigned int			id;
	unsigned int			gamma;
	unsigned int			acl_enable;
	unsigned int			cur_acl;

	struct lcd_device	*ld;
	struct backlight_device	*bd;

	struct mipi_dsim_lcd_device	*dsim_dev;
	struct lcd_platform_data	*ddi_pd;
	struct mutex			lock;
	bool  enabled;
};


static struct regulator_bulk_data supplies[] = {
	{ .supply = "vdd3", },
	{ .supply = "vci", },
};

static void s6e8ax0_regulator_enable(struct s6e8ax0 *lcd)
{
	int ret = 0;
	struct lcd_platform_data *pd = NULL;

	pd = lcd->ddi_pd;
	mutex_lock(&lcd->lock);
	if (!lcd->enabled) {
		ret = regulator_bulk_enable(ARRAY_SIZE(supplies), supplies);
		if (ret)
			goto out;

		lcd->enabled = true;
	}
	msleep(pd->power_on_delay);
out:
	mutex_unlock(&lcd->lock);
}

static void s6e8ax0_regulator_disable(struct s6e8ax0 *lcd)
{
	int ret = 0;

	mutex_lock(&lcd->lock);
	if (lcd->enabled) {
		ret = regulator_bulk_disable(ARRAY_SIZE(supplies), supplies);
		if (ret)
			goto out;

		lcd->enabled = false;
	}
out:
	mutex_unlock(&lcd->lock);
}

static const unsigned char s6e8ax0_22_gamma_30[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xf5, 0x00, 0xff, 0xad, 0xaf,
	0xbA, 0xc3, 0xd8, 0xc5, 0x9f, 0xc6, 0x9e, 0xc1, 0xdc, 0xc0,
	0x00, 0x61, 0x00, 0x5a, 0x00, 0x74,
};

static const unsigned char s6e8ax0_22_gamma_50[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xe8, 0x1f, 0xf7, 0xad, 0xc0,
	0xb5, 0xc4, 0xdc, 0xc4, 0x9e, 0xc6, 0x9c, 0xbb, 0xd8, 0xbb,
	0x00, 0x70, 0x00, 0x68, 0x00, 0x86,
};

static const unsigned char s6e8ax0_22_gamma_60[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xde, 0x1f, 0xef, 0xad, 0xc4,
	0xb3, 0xc3, 0xdd, 0xc4, 0x9e, 0xc6, 0x9c, 0xbc, 0xd6, 0xba,
	0x00, 0x75, 0x00, 0x6e, 0x00, 0x8d,
};

static const unsigned char s6e8ax0_22_gamma_70[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xd8, 0x1f, 0xe7, 0xaf, 0xc8,
	0xb4, 0xc4, 0xdd, 0xc3, 0x9d, 0xc6, 0x9c, 0xbb, 0xd6, 0xb9,
	0x00, 0x7a, 0x00, 0x72, 0x00, 0x93,
};

static const unsigned char s6e8ax0_22_gamma_80[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xc9, 0x1f, 0xde, 0xae, 0xc9,
	0xb1, 0xc3, 0xdd, 0xc2, 0x9d, 0xc5, 0x9b, 0xbc, 0xd6, 0xbb,
	0x00, 0x7f, 0x00, 0x77, 0x00, 0x99,
};

static const unsigned char s6e8ax0_22_gamma_90[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xc7, 0x1f, 0xd9, 0xb0, 0xcc,
	0xb2, 0xc3, 0xdc, 0xc1, 0x9c, 0xc6, 0x9c, 0xbc, 0xd4, 0xb9,
	0x00, 0x83, 0x00, 0x7b, 0x00, 0x9e,
};

static const unsigned char s6e8ax0_22_gamma_100[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xbd, 0x80, 0xcd, 0xba, 0xce,
	0xb3, 0xc4, 0xde, 0xc3, 0x9c, 0xc4, 0x9, 0xb8, 0xd3, 0xb6,
	0x00, 0x88, 0x00, 0x80, 0x00, 0xa5,
};

static const unsigned char s6e8ax0_22_gamma_120[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb9, 0x95, 0xc8, 0xb1, 0xcf,
	0xb2, 0xc6, 0xdf, 0xc5, 0x9b, 0xc3, 0x99, 0xb6, 0xd2, 0xb6,
	0x00, 0x8f, 0x00, 0x86, 0x00, 0xac,
};

static const unsigned char s6e8ax0_22_gamma_130[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb7, 0xa0, 0xc7, 0xb1, 0xd0,
	0xb2, 0xc4, 0xdd, 0xc3, 0x9a, 0xc3, 0x98, 0xb6, 0xd0, 0xb4,
	0x00, 0x92, 0x00, 0x8a, 0x00, 0xb1,
};

static const unsigned char s6e8ax0_22_gamma_140[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb7, 0xa0, 0xc5, 0xb2, 0xd0,
	0xb3, 0xc3, 0xde, 0xc3, 0x9b, 0xc2, 0x98, 0xb6, 0xd0, 0xb4,
	0x00, 0x95, 0x00, 0x8d, 0x00, 0xb5,
};

static const unsigned char s6e8ax0_22_gamma_150[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb3, 0xa0, 0xc2, 0xb2, 0xd0,
	0xb2, 0xc1, 0xdd, 0xc2, 0x9b, 0xc2, 0x98, 0xb4, 0xcf, 0xb1,
	0x00, 0x99, 0x00, 0x90, 0x00, 0xba,
};

static const unsigned char s6e8ax0_22_gamma_160[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xaf, 0xa5, 0xbf, 0xb0, 0xd0,
	0xb1, 0xc3, 0xde, 0xc2, 0x99, 0xc1, 0x97, 0xb4, 0xce, 0xb1,
	0x00, 0x9c, 0x00, 0x93, 0x00, 0xbe,
};

static const unsigned char s6e8ax0_22_gamma_170[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xaf, 0xb5, 0xbf, 0xb1, 0xd1,
	0xb1, 0xc3, 0xde, 0xc3, 0x99, 0xc0, 0x96, 0xb4, 0xce, 0xb1,
	0x00, 0x9f, 0x00, 0x96, 0x00, 0xc2,
};

static const unsigned char s6e8ax0_22_gamma_180[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xaf, 0xb7, 0xbe, 0xb3, 0xd2,
	0xb3, 0xc3, 0xde, 0xc2, 0x97, 0xbf, 0x95, 0xb4, 0xcd, 0xb1,
	0x00, 0xa2, 0x00, 0x99, 0x00, 0xc5,
};

static const unsigned char s6e8ax0_22_gamma_190[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xaf, 0xb9, 0xbe, 0xb2, 0xd2,
	0xb2, 0xc3, 0xdd, 0xc3, 0x98, 0xbf, 0x95, 0xb2, 0xcc, 0xaf,
	0x00, 0xa5, 0x00, 0x9c, 0x00, 0xc9,
};

static const unsigned char s6e8ax0_22_gamma_200[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xaf, 0xb9, 0xbc, 0xb2, 0xd2,
	0xb1, 0xc4, 0xdd, 0xc3, 0x97, 0xbe, 0x95, 0xb1, 0xcb, 0xae,
	0x00, 0xa8, 0x00, 0x9f, 0x00, 0xcd,
};

static const unsigned char s6e8ax0_22_gamma_210[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb1, 0xc1, 0xbd, 0xb1, 0xd1,
	0xb1, 0xc2, 0xde, 0xc2, 0x97, 0xbe, 0x94, 0xB0, 0xc9, 0xad,
	0x00, 0xae, 0x00, 0xa4, 0x00, 0xd4,
};

static const unsigned char s6e8ax0_22_gamma_220[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb1, 0xc7, 0xbd, 0xb1, 0xd1,
	0xb1, 0xc2, 0xdd, 0xc2, 0x97, 0xbd, 0x94, 0xb0, 0xc9, 0xad,
	0x00, 0xad, 0x00, 0xa2, 0x00, 0xd3,
};

static const unsigned char s6e8ax0_22_gamma_230[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb1, 0xc3, 0xbd, 0xb2, 0xd1,
	0xb1, 0xc3, 0xdd, 0xc1, 0x96, 0xbd, 0x94, 0xb0, 0xc9, 0xad,
	0x00, 0xb0, 0x00, 0xa7, 0x00, 0xd7,
};

static const unsigned char s6e8ax0_22_gamma_240[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb1, 0xcb, 0xbd, 0xb1, 0xd2,
	0xb1, 0xc3, 0xdD, 0xc2, 0x95, 0xbd, 0x93, 0xaf, 0xc8, 0xab,
	0x00, 0xb3, 0x00, 0xa9, 0x00, 0xdb,
};

static const unsigned char s6e8ax0_22_gamma_250[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb3, 0xcc, 0xbe, 0xb0, 0xd2,
	0xb0, 0xc3, 0xdD, 0xc2, 0x94, 0xbc, 0x92, 0xae, 0xc8, 0xab,
	0x00, 0xb6, 0x00, 0xab, 0x00, 0xde,
};

static const unsigned char s6e8ax0_22_gamma_260[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb3, 0xd0, 0xbe, 0xaf, 0xd1,
	0xaf, 0xc2, 0xdd, 0xc1, 0x96, 0xbc, 0x93, 0xaf, 0xc8, 0xac,
	0x00, 0xb7, 0x00, 0xad, 0x00, 0xe0,
};

static const unsigned char s6e8ax0_22_gamma_270[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb2, 0xcF, 0xbd, 0xb0, 0xd2,
	0xaf, 0xc2, 0xdc, 0xc1, 0x95, 0xbd, 0x93, 0xae, 0xc6, 0xaa,
	0x00, 0xba, 0x00, 0xb0, 0x00, 0xe4,
};

static const unsigned char s6e8ax0_22_gamma_280[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb2, 0xd0, 0xbd, 0xaf, 0xd0,
	0xad, 0xc4, 0xdd, 0xc3, 0x95, 0xbd, 0x93, 0xac, 0xc5, 0xa9,
	0x00, 0xbd, 0x00, 0xb2, 0x00, 0xe7,
};

static const unsigned char s6e8ax0_22_gamma_300[] = {
	0xfa, 0x01, 0x60, 0x10, 0x60, 0xb5, 0xd3, 0xbd, 0xb1, 0xd2,
	0xb0, 0xc0, 0xdc, 0xc0, 0x94, 0xba, 0x91, 0xac, 0xc5, 0xa9,
	0x00, 0xc2, 0x00, 0xb7, 0x00, 0xed,
};

static const unsigned char *s6e8ax0_22_gamma_table[] = {
	s6e8ax0_22_gamma_30,
	s6e8ax0_22_gamma_50,
	s6e8ax0_22_gamma_60,
	s6e8ax0_22_gamma_70,
	s6e8ax0_22_gamma_80,
	s6e8ax0_22_gamma_90,
	s6e8ax0_22_gamma_100,
	s6e8ax0_22_gamma_120,
	s6e8ax0_22_gamma_130,
	s6e8ax0_22_gamma_140,
	s6e8ax0_22_gamma_150,
	s6e8ax0_22_gamma_160,
	s6e8ax0_22_gamma_170,
	s6e8ax0_22_gamma_180,
	s6e8ax0_22_gamma_190,
	s6e8ax0_22_gamma_200,
	s6e8ax0_22_gamma_210,
	s6e8ax0_22_gamma_220,
	s6e8ax0_22_gamma_230,
	s6e8ax0_22_gamma_240,
	s6e8ax0_22_gamma_250,
	s6e8ax0_22_gamma_260,
	s6e8ax0_22_gamma_270,
	s6e8ax0_22_gamma_280,
	s6e8ax0_22_gamma_300,
};

static void s6e8ax0_panel_cond(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);

	static const unsigned char data_to_send[] = {
		0xf8, 0x3d, 0x35, 0x00, 0x00, 0x00, 0x93, 0x00, 0x3c, 0x7d,
		0x08, 0x27, 0x7d, 0x3f, 0x00, 0x00, 0x00, 0x20, 0x04, 0x08,
		0x6e, 0x00, 0x00, 0x00, 0x02, 0x08, 0x08, 0x23, 0x23, 0xc0,
		0xc8, 0x08, 0x48, 0xc1, 0x00, 0xc1, 0xff, 0xff, 0xc8
	};
	static const unsigned char data_to_send_panel_reverse[] = {
		0xf8, 0x19, 0x35, 0x00, 0x00, 0x00, 0x93, 0x00, 0x3c, 0x7d,
		0x08, 0x27, 0x7d, 0x3f, 0x00, 0x00, 0x00, 0x20, 0x04, 0x08,
		0x6e, 0x00, 0x00, 0x00, 0x02, 0x08, 0x08, 0x23, 0x23, 0xc0,
		0xc1, 0x01, 0x41, 0xc1, 0x00, 0xc1, 0xf6, 0xf6, 0xc1
	};

	if (lcd->dsim_dev->panel_reverse)
		ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
				data_to_send_panel_reverse,
				ARRAY_SIZE(data_to_send_panel_reverse));
	else
		ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
				data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_display_cond(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xf2, 0x80, 0x03, 0x0d
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

/* Gamma 2.2 Setting (200cd, 7500K, 10MPCD) */
static void s6e8ax0_gamma_cond(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	unsigned int gamma = lcd->bd->props.brightness;

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
			s6e8ax0_22_gamma_table[gamma],
			GAMMA_TABLE_COUNT);
}

static void s6e8ax0_gamma_update(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xf7, 0x03
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE_PARAM, data_to_send,
		ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_etc_cond1(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xd1, 0xfe, 0x80, 0x00, 0x01, 0x0b, 0x00, 0x00, 0x40,
		0x0d, 0x00, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_etc_cond2(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xb6, 0x0c, 0x02, 0x03, 0x32, 0xff, 0x44, 0x44, 0xc0,
		0x00
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_etc_cond3(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xe1, 0x10, 0x1c, 0x17, 0x08, 0x1d
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_etc_cond4(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xe2, 0xed, 0x07, 0xc3, 0x13, 0x0d, 0x03
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_etc_cond5(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xf4, 0xcf, 0x0a, 0x12, 0x10, 0x19, 0x33, 0x02
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}
static void s6e8ax0_etc_cond6(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xe3, 0x40
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE_PARAM,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_etc_cond7(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xe4, 0x00, 0x00, 0x14, 0x80, 0x00, 0x00, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_elvss_set(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xb1, 0x04, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_elvss_nvm_set(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xd9, 0x5c, 0x20, 0x0c, 0x0f, 0x41, 0x00, 0x10, 0x11,
		0x12, 0xd1, 0x00, 0x00, 0x00, 0x00, 0x80, 0xcb, 0xed,
		0x64, 0xaf
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_sleep_in(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0x10, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_sleep_out(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0x11, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_display_on(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0x29, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_display_off(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0x28, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_apply_level2_key(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xf0, 0x5a, 0x5a
	};

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_acl_on(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xc0, 0x01
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

static void s6e8ax0_acl_off(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	static const unsigned char data_to_send[] = {
		0xc0, 0x00
	};

	ops->cmd_write(lcd_to_master(lcd),
		MIPI_DSI_DCS_SHORT_WRITE,
		data_to_send, ARRAY_SIZE(data_to_send));
}

/* Full white 50% reducing setting */
static void s6e8ax0_acl_ctrl_set(struct s6e8ax0 *lcd)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	/* Full white 50% reducing setting */
	static const unsigned char cutoff_50[] = {
		0xc1, 0x47, 0x53, 0x13, 0x53, 0x00, 0x00, 0x02, 0xcf,
		0x00, 0x00, 0x04, 0xff,	0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x08, 0x0f, 0x16, 0x1d, 0x24, 0x2a, 0x31, 0x38,
		0x3f, 0x46
	};
	/* Full white 45% reducing setting */
	static const unsigned char cutoff_45[] = {
		0xc1, 0x47, 0x53, 0x13, 0x53, 0x00, 0x00, 0x02, 0xcf,
		0x00, 0x00, 0x04, 0xff,	0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x07, 0x0d, 0x13, 0x19, 0x1f, 0x25, 0x2b, 0x31,
		0x37, 0x3d
	};
	/* Full white 40% reducing setting */
	static const unsigned char cutoff_40[] = {
		0xc1, 0x47, 0x53, 0x13, 0x53, 0x00, 0x00, 0x02, 0xcf,
		0x00, 0x00, 0x04, 0xff,	0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x06, 0x0c, 0x11, 0x16, 0x1c, 0x21, 0x26, 0x2b,
		0x31, 0x36
	};

	if (lcd->acl_enable) {
		if (lcd->cur_acl == 0) {
			if (lcd->gamma == 0 || lcd->gamma == 1) {
				s6e8ax0_acl_off(lcd);
				dev_dbg(&lcd->ld->dev,
					"cur_acl=%d\n", lcd->cur_acl);
			} else
				s6e8ax0_acl_on(lcd);
		}
		switch (lcd->gamma) {
		case 0: /* 30cd */
			s6e8ax0_acl_off(lcd);
			lcd->cur_acl = 0;
			break;
		case 1 ... 3: /* 50cd ~ 90cd */
			ops->cmd_write(lcd_to_master(lcd),
				MIPI_DSI_DCS_LONG_WRITE,
				cutoff_40,
				ARRAY_SIZE(cutoff_40));
			lcd->cur_acl = 40;
			break;
		case 4 ... 7: /* 120cd ~ 210cd */
			ops->cmd_write(lcd_to_master(lcd),
				MIPI_DSI_DCS_LONG_WRITE,
				cutoff_45,
				ARRAY_SIZE(cutoff_45));
			lcd->cur_acl = 45;
			break;
		case 8 ... 10: /* 220cd ~ 300cd */
			ops->cmd_write(lcd_to_master(lcd),
				MIPI_DSI_DCS_LONG_WRITE,
				cutoff_50,
				ARRAY_SIZE(cutoff_50));
			lcd->cur_acl = 50;
			break;
		default:
			break;
		}
	} else {
		s6e8ax0_acl_off(lcd);
		lcd->cur_acl = 0;
		dev_dbg(&lcd->ld->dev, "cur_acl = %d\n", lcd->cur_acl);
	}
}

static void s6e8ax0_read_id(struct s6e8ax0 *lcd, u8 *mtp_id)
{
	unsigned int ret;
	unsigned int addr = 0xd1;	/* MTP ID */
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);

	ret = ops->cmd_read(lcd_to_master(lcd),
			MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM,
			addr, 3, mtp_id);
}

static int s6e8ax0_panel_init(struct s6e8ax0 *lcd)
{
	s6e8ax0_apply_level2_key(lcd);
	s6e8ax0_sleep_out(lcd);
	msleep(1);
	s6e8ax0_panel_cond(lcd);
	s6e8ax0_display_cond(lcd);
	s6e8ax0_gamma_cond(lcd);
	s6e8ax0_gamma_update(lcd);

	s6e8ax0_etc_cond1(lcd);
	s6e8ax0_etc_cond2(lcd);
	s6e8ax0_etc_cond3(lcd);
	s6e8ax0_etc_cond4(lcd);
	s6e8ax0_etc_cond5(lcd);
	s6e8ax0_etc_cond6(lcd);
	s6e8ax0_etc_cond7(lcd);

	s6e8ax0_elvss_nvm_set(lcd);
	s6e8ax0_elvss_set(lcd);

	s6e8ax0_acl_ctrl_set(lcd);
	s6e8ax0_acl_on(lcd);

	/* if ID3 value is not 33h, branch private elvss mode */
	msleep(lcd->ddi_pd->power_on_delay);

	return 0;
}

static int s6e8ax0_update_gamma_ctrl(struct s6e8ax0 *lcd, int brightness)
{
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);

	ops->cmd_write(lcd_to_master(lcd), MIPI_DSI_DCS_LONG_WRITE,
			s6e8ax0_22_gamma_table[brightness],
			ARRAY_SIZE(s6e8ax0_22_gamma_table));

	/* update gamma table. */
	s6e8ax0_gamma_update(lcd);
	lcd->gamma = brightness;

	return 0;
}

static int s6e8ax0_gamma_ctrl(struct s6e8ax0 *lcd, int gamma)
{
	s6e8ax0_update_gamma_ctrl(lcd, gamma);

	return 0;
}

static int s6e8ax0_set_power(struct lcd_device *ld, int power)
{
	struct s6e8ax0 *lcd = lcd_get_data(ld);
	struct mipi_dsim_master_ops *ops = lcd_to_master_ops(lcd);
	int ret = 0;

	if (power != FB_BLANK_UNBLANK && power != FB_BLANK_POWERDOWN &&
			power != FB_BLANK_NORMAL) {
		dev_err(lcd->dev, "power value should be 0, 1 or 4.\n");
		return -EINVAL;
	}

	if ((power == FB_BLANK_UNBLANK) && ops->set_blank_mode) {
		/* LCD power on */
		if ((POWER_IS_ON(power) && POWER_IS_OFF(lcd->power))
			|| (POWER_IS_ON(power) && POWER_IS_NRM(lcd->power))) {
			ret = ops->set_blank_mode(lcd_to_master(lcd), power);
			if (!ret && lcd->power != power)
				lcd->power = power;
		}
	} else if ((power == FB_BLANK_POWERDOWN) && ops->set_early_blank_mode) {
		/* LCD power off */
		if ((POWER_IS_OFF(power) && POWER_IS_ON(lcd->power)) ||
		(POWER_IS_ON(lcd->power) && POWER_IS_NRM(power))) {
			ret = ops->set_early_blank_mode(lcd_to_master(lcd),
							power);
			if (!ret && lcd->power != power)
				lcd->power = power;
		}
	}

	return ret;
}

static int s6e8ax0_get_power(struct lcd_device *ld)
{
	struct s6e8ax0 *lcd = lcd_get_data(ld);

	return lcd->power;
}

static int s6e8ax0_set_brightness(struct backlight_device *bd)
{
	int ret = 0, brightness = bd->props.brightness;
	struct s6e8ax0 *lcd = bl_get_data(bd);

	if (brightness < MIN_BRIGHTNESS ||
		brightness > bd->props.max_brightness) {
		dev_err(lcd->dev, "lcd brightness should be %d to %d.\n",
			MIN_BRIGHTNESS, MAX_BRIGHTNESS);
		return -EINVAL;
	}

	ret = s6e8ax0_gamma_ctrl(lcd, brightness);
	if (ret) {
		dev_err(&bd->dev, "lcd brightness setting failed.\n");
		return -EIO;
	}

	return ret;
}

static struct lcd_ops s6e8ax0_lcd_ops = {
	.set_power = s6e8ax0_set_power,
	.get_power = s6e8ax0_get_power,
};

static const struct backlight_ops s6e8ax0_backlight_ops = {
	.update_status = s6e8ax0_set_brightness,
};

static void s6e8ax0_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct s6e8ax0 *lcd = dev_get_drvdata(&dsim_dev->dev);

	msleep(lcd->ddi_pd->power_on_delay);

	/* lcd power on */
	if (power)
		s6e8ax0_regulator_enable(lcd);
	else
		s6e8ax0_regulator_disable(lcd);

	msleep(lcd->ddi_pd->reset_delay);

	/* lcd reset */
	if (lcd->ddi_pd->reset)
		lcd->ddi_pd->reset(lcd->ld);
	msleep(5);
}

static void s6e8ax0_set_sequence(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct s6e8ax0 *lcd = dev_get_drvdata(&dsim_dev->dev);

	s6e8ax0_panel_init(lcd);
	s6e8ax0_display_on(lcd);

	lcd->power = FB_BLANK_UNBLANK;
}

static int s6e8ax0_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct s6e8ax0 *lcd;
	int ret;
	u8 mtp_id[3] = {0, };

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct s6e8ax0), GFP_KERNEL);
	if (!lcd) {
		dev_err(&dsim_dev->dev, "failed to allocate s6e8ax0 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->ddi_pd = (struct lcd_platform_data *)dsim_dev->platform_data;
	lcd->dev = &dsim_dev->dev;

	mutex_init(&lcd->lock);

	ret = devm_regulator_bulk_get(lcd->dev, ARRAY_SIZE(supplies), supplies);
	if (ret) {
		dev_err(lcd->dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	lcd->ld = devm_lcd_device_register(lcd->dev, "s6e8ax0", lcd->dev, lcd,
			&s6e8ax0_lcd_ops);
	if (IS_ERR(lcd->ld)) {
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	lcd->bd = devm_backlight_device_register(lcd->dev, "s6e8ax0-bl",
				lcd->dev, lcd, &s6e8ax0_backlight_ops, NULL);
	if (IS_ERR(lcd->bd)) {
		dev_err(lcd->dev, "failed to register backlight ops.\n");
		return PTR_ERR(lcd->bd);
	}

	lcd->bd->props.max_brightness = MAX_BRIGHTNESS;
	lcd->bd->props.brightness = MAX_BRIGHTNESS;

	s6e8ax0_read_id(lcd, mtp_id);
	if (mtp_id[0] == 0x00)
		dev_err(lcd->dev, "read id failed\n");

	dev_info(lcd->dev, "Read ID : %x, %x, %x\n",
			mtp_id[0], mtp_id[1], mtp_id[2]);

	if (mtp_id[2] == 0x33)
		dev_info(lcd->dev,
			"ID-3 is 0xff does not support dynamic elvss\n");
	else
		dev_info(lcd->dev,
			"ID-3 is 0x%x support dynamic elvss\n", mtp_id[2]);

	lcd->acl_enable = 1;
	lcd->cur_acl = 0;

	dev_set_drvdata(&dsim_dev->dev, lcd);

	dev_dbg(lcd->dev, "probed s6e8ax0 panel driver.\n");

	return 0;
}

#ifdef CONFIG_PM
static int s6e8ax0_suspend(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct s6e8ax0 *lcd = dev_get_drvdata(&dsim_dev->dev);

	s6e8ax0_sleep_in(lcd);
	msleep(lcd->ddi_pd->power_off_delay);
	s6e8ax0_display_off(lcd);

	s6e8ax0_regulator_disable(lcd);

	return 0;
}

static int s6e8ax0_resume(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct s6e8ax0 *lcd = dev_get_drvdata(&dsim_dev->dev);

	s6e8ax0_sleep_out(lcd);
	msleep(lcd->ddi_pd->power_on_delay);

	s6e8ax0_regulator_enable(lcd);
	s6e8ax0_set_sequence(dsim_dev);

	return 0;
}
#else
#define s6e8ax0_suspend		NULL
#define s6e8ax0_resume		NULL
#endif

static struct mipi_dsim_lcd_driver s6e8ax0_dsim_ddi_driver = {
	.name = "s6e8ax0",
	.id = -1,

	.power_on = s6e8ax0_power_on,
	.set_sequence = s6e8ax0_set_sequence,
	.probe = s6e8ax0_probe,
	.suspend = s6e8ax0_suspend,
	.resume = s6e8ax0_resume,
};

static int s6e8ax0_init(void)
{
	exynos_mipi_dsi_register_lcd_driver(&s6e8ax0_dsim_ddi_driver);

	return 0;
}

static void s6e8ax0_exit(void)
{
	return;
}

module_init(s6e8ax0_init);
module_exit(s6e8ax0_exit);

MODULE_AUTHOR("Donghwa Lee <dh09.lee@samsung.com>");
MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based s6e8ax0 AMOLED LCD Panel Driver");
MODULE_LICENSE("GPL");
