/* Speyside modules for Cragganmore - board data probing
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *	Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>

#include <linux/mfd/wm831x/irq.h>
#include <linux/mfd/wm831x/gpio.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/arizona/pdata.h>

#include <linux/regulator/machine.h>

#include <sound/wm0010.h>
#include <sound/wm2200.h>
#include <sound/wm5100.h>
#include <sound/wm8996.h>
#include <sound/wm8962.h>
#include <sound/wm9081.h>

#include <linux/platform_data/spi-s3c64xx.h>

#include "crag6410.h"

static struct s3c64xx_spi_csinfo wm0010_spi_csinfo = {
	.line = S3C64XX_GPC(3),
};

static struct wm0010_pdata wm0010_pdata = {
	.gpio_reset = S3C64XX_GPN(6),
	.reset_active_high = 1, /* Active high for Glenfarclas Rev 2 */
};

static struct spi_board_info wm1253_devs[] = {
	[0] = {
		.modalias	= "wm0010",
		.max_speed_hz	= 26 * 1000 * 1000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
		.irq		= S3C_EINT(4),
		.controller_data = &wm0010_spi_csinfo,
		.platform_data = &wm0010_pdata,
	},
};

static struct spi_board_info balblair_devs[] = {
	[0] = {
		.modalias	= "wm0010",
		.max_speed_hz	= 26 * 1000 * 1000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
		.irq		= S3C_EINT(4),
		.controller_data = &wm0010_spi_csinfo,
		.platform_data = &wm0010_pdata,
	},
};

static struct wm5100_pdata wm5100_pdata = {
	.ldo_ena = S3C64XX_GPN(7),
	.irq_flags = IRQF_TRIGGER_HIGH,
	.gpio_base = CODEC_GPIO_BASE,

	.in_mode = {
		WM5100_IN_DIFF,
		WM5100_IN_DIFF,
		WM5100_IN_DIFF,
		WM5100_IN_SE,
	},

	.hp_pol = CODEC_GPIO_BASE + 3,
	.jack_modes = {
		{ WM5100_MICDET_MICBIAS3, 0, 0 },
		{ WM5100_MICDET_MICBIAS2, 1, 1 },
	},

	.gpio_defaults = {
		0,
		0,
		0,
		0,
		0x2, /* IRQ: CMOS output */
		0x3, /* CLKOUT: CMOS output */
	},
};

static struct wm8996_retune_mobile_config wm8996_retune[] = {
	{
		.name = "Sub LPF",
		.rate = 48000,
		.regs = {
			0x6318, 0x6300, 0x1000, 0x0000, 0x0004, 0x2000, 0xF000,
			0x0000, 0x0004, 0x2000, 0xF000, 0x0000, 0x0004, 0x2000,
			0xF000, 0x0000, 0x0004, 0x1000, 0x0800, 0x4000
		},
	},
	{
		.name = "Sub HPF",
		.rate = 48000,
		.regs = {
			0x000A, 0x6300, 0x1000, 0x0000, 0x0004, 0x2000, 0xF000,
			0x0000, 0x0004, 0x2000, 0xF000, 0x0000, 0x0004, 0x2000,
			0xF000, 0x0000, 0x0004, 0x1000, 0x0800, 0x4000
		},
	},
};

static struct wm8996_pdata wm8996_pdata __initdata = {
	.ldo_ena = S3C64XX_GPN(7),
	.gpio_base = CODEC_GPIO_BASE,
	.micdet_def = 1,
	.inl_mode = WM8996_DIFFERRENTIAL_1,
	.inr_mode = WM8996_DIFFERRENTIAL_1,

	.irq_flags = IRQF_TRIGGER_RISING,

	.gpio_default = {
		0x8001, /* GPIO1 == ADCLRCLK1 */
		0x8001, /* GPIO2 == ADCLRCLK2, input due to CPU */
		0x0141, /* GPIO3 == HP_SEL */
		0x0002, /* GPIO4 == IRQ */
		0x020e, /* GPIO5 == CLKOUT */
	},

	.retune_mobile_cfgs = wm8996_retune,
	.num_retune_mobile_cfgs = ARRAY_SIZE(wm8996_retune),
};

static struct wm8962_pdata wm8962_pdata __initdata = {
	.gpio_init = {
		0,
		WM8962_GPIO_FN_OPCLK,
		WM8962_GPIO_FN_DMICCLK,
		0,
		0x8000 | WM8962_GPIO_FN_DMICDAT,
		WM8962_GPIO_FN_IRQ,    /* Open drain mode */
	},
	.in4_dc_measure = true,
};

static struct wm9081_pdata wm9081_pdata __initdata = {
	.irq_high = false,
	.irq_cmos = false,
};

static const struct i2c_board_info wm1254_devs[] = {
	{ I2C_BOARD_INFO("wm8996", 0x1a),
	  .platform_data = &wm8996_pdata,
	  .irq = GLENFARCLAS_PMIC_IRQ_BASE + WM831X_IRQ_GPIO_2,
	},
	{ I2C_BOARD_INFO("wm9081", 0x6c),
	  .platform_data = &wm9081_pdata, },
};

static const struct i2c_board_info wm1255_devs[] = {
	{ I2C_BOARD_INFO("wm5100", 0x1a),
	  .platform_data = &wm5100_pdata,
	  .irq = GLENFARCLAS_PMIC_IRQ_BASE + WM831X_IRQ_GPIO_2,
	},
	{ I2C_BOARD_INFO("wm9081", 0x6c),
	  .platform_data = &wm9081_pdata, },
};

static const struct i2c_board_info wm1259_devs[] = {
	{ I2C_BOARD_INFO("wm8962", 0x1a),
	  .platform_data = &wm8962_pdata,
	  .irq = GLENFARCLAS_PMIC_IRQ_BASE + WM831X_IRQ_GPIO_2,
	},
};

static struct regulator_init_data wm8994_ldo1 = {
	.supply_regulator = "WALLVDD",
};

static struct regulator_init_data wm8994_ldo2 = {
	.supply_regulator = "WALLVDD",
};

static struct wm8994_pdata wm8994_pdata = {
	.gpio_base = CODEC_GPIO_BASE,
	.micb2_delay = 150,
	.gpio_defaults = {
		0x3,          /* IRQ out, active high, CMOS */
	},
	.ldo = {
		 { .enable = S3C64XX_GPN(6), .init_data = &wm8994_ldo1, },
		 { .enable = S3C64XX_GPN(4), .init_data = &wm8994_ldo2, },
	},
};

static const struct i2c_board_info wm1277_devs[] = {
	{ I2C_BOARD_INFO("wm8958", 0x1a),  /* WM8958 is the superset */
	  .platform_data = &wm8994_pdata,
	  .irq = GLENFARCLAS_PMIC_IRQ_BASE + WM831X_IRQ_GPIO_2,
	},
};

static struct arizona_pdata wm5102_reva_pdata = {
	.ldoena = S3C64XX_GPN(7),
	.gpio_base = CODEC_GPIO_BASE,
	.irq_flags = IRQF_TRIGGER_HIGH,
	.micd_pol_gpio = CODEC_GPIO_BASE + 4,
	.micd_rate = 6,
	.gpio_defaults = {
		[2] = 0x10000, /* AIF3TXLRCLK */
		[3] = 0x4,     /* OPCLK */
	},
};

static struct s3c64xx_spi_csinfo codec_spi_csinfo = {
	.line = S3C64XX_GPN(5),
};

static struct spi_board_info wm5102_reva_spi_devs[] = {
	[0] = {
		.modalias	= "wm5102",
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= SPI_MODE_0,
		.irq		= GLENFARCLAS_PMIC_IRQ_BASE +
				  WM831X_IRQ_GPIO_2,
		.controller_data = &codec_spi_csinfo,
		.platform_data = &wm5102_reva_pdata,
	},
};

static struct arizona_pdata wm5102_pdata = {
	.ldoena = S3C64XX_GPN(7),
	.gpio_base = CODEC_GPIO_BASE,
	.irq_flags = IRQF_TRIGGER_HIGH,
	.micd_pol_gpio = CODEC_GPIO_BASE + 2,
	.gpio_defaults = {
		[2] = 0x10000, /* AIF3TXLRCLK */
		[3] = 0x4,     /* OPCLK */
	},
};

static struct spi_board_info wm5102_spi_devs[] = {
	[0] = {
		.modalias	= "wm5102",
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= SPI_MODE_0,
		.irq		= GLENFARCLAS_PMIC_IRQ_BASE +
				  WM831X_IRQ_GPIO_2,
		.controller_data = &codec_spi_csinfo,
		.platform_data = &wm5102_pdata,
	},
};

static struct spi_board_info wm5110_spi_devs[] = {
	[0] = {
		.modalias	= "wm5110",
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= SPI_MODE_0,
		.irq		= GLENFARCLAS_PMIC_IRQ_BASE +
				  WM831X_IRQ_GPIO_2,
		.controller_data = &codec_spi_csinfo,
		.platform_data = &wm5102_reva_pdata,
	},
};

static const struct i2c_board_info wm6230_i2c_devs[] = {
	{ I2C_BOARD_INFO("wm9081", 0x6c),
	  .platform_data = &wm9081_pdata, },
};

static struct wm2200_pdata wm2200_pdata = {
	.ldo_ena = S3C64XX_GPN(7),
	.gpio_defaults = {
		[2] = 0x0005,  /* GPIO3 24.576MHz output clock */
	},
};

static const struct i2c_board_info wm2200_i2c[] = {
	{ I2C_BOARD_INFO("wm2200", 0x3a),
	  .platform_data = &wm2200_pdata, },
};

static const struct {
	u8 id;
	u8 rev;
	const char *name;
	const struct i2c_board_info *i2c_devs;
	int num_i2c_devs;
	const struct spi_board_info *spi_devs;
	int num_spi_devs;
} gf_mods[] = {
	{ .id = 0x01, .rev = 0xff, .name = "1250-EV1 Springbank" },
	{ .id = 0x02, .rev = 0xff, .name = "1251-EV1 Jura" },
	{ .id = 0x03, .rev = 0xff, .name = "1252-EV1 Glenlivet" },
	{ .id = 0x06, .rev = 0xff, .name = "WM8997-6721-CS96-EV1 Lapraoig" },
	{ .id = 0x07, .rev = 0xff, .name = "WM5110-6271 Deanston",
	  .spi_devs = wm5110_spi_devs,
	  .num_spi_devs = ARRAY_SIZE(wm5110_spi_devs) },
	{ .id = 0x08, .rev = 0xff, .name = "WM8903-6102 Tamdhu" },
	{ .id = 0x09, .rev = 0xff, .name = "WM1811A-6305 Adelphi" },
	{ .id = 0x0a, .rev = 0xff, .name = "WM8996-6272 Blackadder" },
	{ .id = 0x0b, .rev = 0xff, .name = "WM8994-6235 Benromach" },
	{ .id = 0x11, .rev = 0xff, .name = "6249-EV2 Glenfarclas", },
	{ .id = 0x14, .rev = 0xff, .name = "6271-EV1 Lochnagar" },
	{ .id = 0x15, .rev = 0xff, .name = "6320-EV1 Bells",
	  .i2c_devs = wm6230_i2c_devs,
	  .num_i2c_devs = ARRAY_SIZE(wm6230_i2c_devs) },
	{ .id = 0x21, .rev = 0xff, .name = "1275-EV1 Mortlach" },
	{ .id = 0x25, .rev = 0xff, .name = "1274-EV1 Glencadam" },
	{ .id = 0x31, .rev = 0xff, .name = "1253-EV1 Tomatin",
	  .spi_devs = wm1253_devs, .num_spi_devs = ARRAY_SIZE(wm1253_devs) },
	{ .id = 0x32, .rev = 0xff, .name = "XXXX-EV1 Caol Illa" },
	{ .id = 0x33, .rev = 0xff, .name = "XXXX-EV1 Oban" },
	{ .id = 0x34, .rev = 0xff, .name = "WM0010-6320-CS42 Balblair",
	  .spi_devs = balblair_devs,
	  .num_spi_devs = ARRAY_SIZE(balblair_devs) },
	{ .id = 0x39, .rev = 0xff, .name = "1254-EV1 Dallas Dhu",
	  .i2c_devs = wm1254_devs, .num_i2c_devs = ARRAY_SIZE(wm1254_devs) },
	{ .id = 0x3a, .rev = 0xff, .name = "1259-EV1 Tobermory",
	  .i2c_devs = wm1259_devs, .num_i2c_devs = ARRAY_SIZE(wm1259_devs) },
	{ .id = 0x3b, .rev = 0xff, .name = "1255-EV1 Kilchoman",
	  .i2c_devs = wm1255_devs, .num_i2c_devs = ARRAY_SIZE(wm1255_devs) },
	{ .id = 0x3c, .rev = 0xff, .name = "1273-EV1 Longmorn" },
	{ .id = 0x3d, .rev = 0xff, .name = "1277-EV1 Littlemill",
	  .i2c_devs = wm1277_devs, .num_i2c_devs = ARRAY_SIZE(wm1277_devs) },
	{ .id = 0x3e, .rev = 0, .name = "WM5102-6271-EV1-CS127 Amrut",
	  .spi_devs = wm5102_reva_spi_devs,
	  .num_spi_devs = ARRAY_SIZE(wm5102_reva_spi_devs) },
	{ .id = 0x3e, .rev = -1, .name = "WM5102-6271-EV1-CS127 Amrut",
	  .spi_devs = wm5102_spi_devs,
	  .num_spi_devs = ARRAY_SIZE(wm5102_spi_devs) },
	{ .id = 0x3f, .rev = -1, .name = "WM2200-6271-CS90-M-REV1",
	  .i2c_devs = wm2200_i2c, .num_i2c_devs = ARRAY_SIZE(wm2200_i2c) },
};

static int wlf_gf_module_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *i2c_id)
{
	int ret, i, j, id, rev;

	ret = i2c_smbus_read_byte_data(i2c, 0);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID: %d\n", ret);
		return ret;
	}

	id = (ret & 0xfe) >> 2;
	rev = ret & 0x3;
	for (i = 0; i < ARRAY_SIZE(gf_mods); i++)
		if (id == gf_mods[i].id && (gf_mods[i].rev == 0xff ||
					    rev == gf_mods[i].rev))
			break;

	if (i < ARRAY_SIZE(gf_mods)) {
		dev_info(&i2c->dev, "%s revision %d\n",
			 gf_mods[i].name, rev + 1);

		for (j = 0; j < gf_mods[i].num_i2c_devs; j++) {
			if (!i2c_new_device(i2c->adapter,
					    &(gf_mods[i].i2c_devs[j])))
				dev_err(&i2c->dev,
					"Failed to register dev: %d\n", ret);
		}

		spi_register_board_info(gf_mods[i].spi_devs,
					gf_mods[i].num_spi_devs);
	} else {
		dev_warn(&i2c->dev, "Unknown module ID 0x%x revision %d\n",
			 id, rev + 1);
	}

	return 0;
}

static const struct i2c_device_id wlf_gf_module_id[] = {
	{ "wlf-gf-module", 0 },
	{ }
};

static struct i2c_driver wlf_gf_module_driver = {
	.driver = {
		.name = "wlf-gf-module",
		.owner = THIS_MODULE,
	},
	.probe = wlf_gf_module_probe,
	.id_table = wlf_gf_module_id,
};

static int __init wlf_gf_module_register(void)
{
	return i2c_add_driver(&wlf_gf_module_driver);
}
device_initcall(wlf_gf_module_register);
