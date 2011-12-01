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

#include <linux/mfd/wm831x/irq.h>
#include <linux/mfd/wm831x/gpio.h>

#include <sound/wm8996.h>
#include <sound/wm8962.h>
#include <sound/wm9081.h>

#include <mach/crag6410.h>

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
	.irq_active_low = true,
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


static __devinitdata const struct {
	u8 id;
	const char *name;
	const struct i2c_board_info *i2c_devs;
	int num_i2c_devs;
} gf_mods[] = {
	{ .id = 0x01, .name = "1250-EV1 Springbank" },
	{ .id = 0x02, .name = "1251-EV1 Jura" },
	{ .id = 0x03, .name = "1252-EV1 Glenlivet" },
	{ .id = 0x11, .name = "6249-EV2 Glenfarclas", },
	{ .id = 0x21, .name = "1275-EV1 Mortlach" },
	{ .id = 0x25, .name = "1274-EV1 Glencadam" },
	{ .id = 0x31, .name = "1253-EV1 Tomatin", },
	{ .id = 0x39, .name = "1254-EV1 Dallas Dhu",
	  .i2c_devs = wm1254_devs, .num_i2c_devs = ARRAY_SIZE(wm1254_devs) },
	{ .id = 0x3a, .name = "1259-EV1 Tobermory",
	  .i2c_devs = wm1259_devs, .num_i2c_devs = ARRAY_SIZE(wm1259_devs) },
	{ .id = 0x3b, .name = "1255-EV1 Kilchoman",
	  .i2c_devs = wm1255_devs, .num_i2c_devs = ARRAY_SIZE(wm1255_devs) },
	{ .id = 0x3c, .name = "1273-EV1 Longmorn" },
};

static __devinit int wlf_gf_module_probe(struct i2c_client *i2c,
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
		if (id == gf_mods[i].id)
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
	} else {
		dev_warn(&i2c->dev, "Unknown module ID %d revision %d\n",
			 id, rev);
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
module_init(wlf_gf_module_register);
