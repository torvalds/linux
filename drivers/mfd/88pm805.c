/*
 * Base driver for Marvell 88PM805
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Haojian Zhuang <haojian.zhuang@marvell.com>
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm80x.h>
#include <linux/slab.h>
#include <linux/delay.h>

static const struct i2c_device_id pm80x_id_table[] = {
	{"88PM805", 0},
	{} /* NULL terminated */
};
MODULE_DEVICE_TABLE(i2c, pm80x_id_table);

/* Interrupt Number in 88PM805 */
enum {
	PM805_IRQ_LDO_OFF,	/*0 */
	PM805_IRQ_SRC_DPLL_LOCK,	/*1 */
	PM805_IRQ_CLIP_FAULT,
	PM805_IRQ_MIC_CONFLICT,
	PM805_IRQ_HP2_SHRT,
	PM805_IRQ_HP1_SHRT,	/*5 */
	PM805_IRQ_FINE_PLL_FAULT,
	PM805_IRQ_RAW_PLL_FAULT,
	PM805_IRQ_VOLP_BTN_DET,
	PM805_IRQ_VOLM_BTN_DET,
	PM805_IRQ_SHRT_BTN_DET,	/*10 */
	PM805_IRQ_MIC_DET,	/*11 */

	PM805_MAX_IRQ,
};

static struct resource codec_resources[] = {
	/* Headset microphone insertion or removal */
	DEFINE_RES_IRQ_NAMED(PM805_IRQ_MIC_DET, "micin"),

	/* Audio short HP1 */
	DEFINE_RES_IRQ_NAMED(PM805_IRQ_HP1_SHRT, "audio-short1"),

	/* Audio short HP2 */
	DEFINE_RES_IRQ_NAMED(PM805_IRQ_HP2_SHRT, "audio-short2"),
};

static const struct mfd_cell codec_devs[] = {
	{
	 .name = "88pm80x-codec",
	 .num_resources = ARRAY_SIZE(codec_resources),
	 .resources = &codec_resources[0],
	 .id = -1,
	 },
};

static struct regmap_irq pm805_irqs[] = {
	/* INT0 */
	[PM805_IRQ_LDO_OFF] = {
		.mask = PM805_INT1_HP1_SHRT,
	},
	[PM805_IRQ_SRC_DPLL_LOCK] = {
		.mask = PM805_INT1_HP2_SHRT,
	},
	[PM805_IRQ_CLIP_FAULT] = {
		.mask = PM805_INT1_MIC_CONFLICT,
	},
	[PM805_IRQ_MIC_CONFLICT] = {
		.mask = PM805_INT1_CLIP_FAULT,
	},
	[PM805_IRQ_HP2_SHRT] = {
		.mask = PM805_INT1_LDO_OFF,
	},
	[PM805_IRQ_HP1_SHRT] = {
		.mask = PM805_INT1_SRC_DPLL_LOCK,
	},
	/* INT1 */
	[PM805_IRQ_FINE_PLL_FAULT] = {
		.reg_offset = 1,
		.mask = PM805_INT2_MIC_DET,
	},
	[PM805_IRQ_RAW_PLL_FAULT] = {
		.reg_offset = 1,
		.mask = PM805_INT2_SHRT_BTN_DET,
	},
	[PM805_IRQ_VOLP_BTN_DET] = {
		.reg_offset = 1,
		.mask = PM805_INT2_VOLM_BTN_DET,
	},
	[PM805_IRQ_VOLM_BTN_DET] = {
		.reg_offset = 1,
		.mask = PM805_INT2_VOLP_BTN_DET,
	},
	[PM805_IRQ_SHRT_BTN_DET] = {
		.reg_offset = 1,
		.mask = PM805_INT2_RAW_PLL_FAULT,
	},
	[PM805_IRQ_MIC_DET] = {
		.reg_offset = 1,
		.mask = PM805_INT2_FINE_PLL_FAULT,
	},
};

static int device_irq_init_805(struct pm80x_chip *chip)
{
	struct regmap *map = chip->regmap;
	unsigned long flags = IRQF_ONESHOT;
	int data, mask, ret = -EINVAL;

	if (!map || !chip->irq) {
		dev_err(chip->dev, "incorrect parameters\n");
		return -EINVAL;
	}

	/*
	 * irq_mode defines the way of clearing interrupt. it's read-clear by
	 * default.
	 */
	mask =
	    PM805_STATUS0_INT_CLEAR | PM805_STATUS0_INV_INT |
	    PM800_STATUS0_INT_MASK;

	data = PM805_STATUS0_INT_CLEAR;
	ret = regmap_update_bits(map, PM805_INT_STATUS0, mask, data);
	/*
	 * PM805_INT_STATUS is under 32K clock domain, so need to
	 * add proper delay before the next I2C register access.
	 */
	usleep_range(1000, 3000);

	if (ret < 0)
		goto out;

	ret =
	    regmap_add_irq_chip(chip->regmap, chip->irq, flags, -1,
				chip->regmap_irq_chip, &chip->irq_data);

out:
	return ret;
}

static void device_irq_exit_805(struct pm80x_chip *chip)
{
	regmap_del_irq_chip(chip->irq, chip->irq_data);
}

static struct regmap_irq_chip pm805_irq_chip = {
	.name = "88pm805",
	.irqs = pm805_irqs,
	.num_irqs = ARRAY_SIZE(pm805_irqs),

	.num_regs = 2,
	.status_base = PM805_INT_STATUS1,
	.mask_base = PM805_INT_MASK1,
	.ack_base = PM805_INT_STATUS1,
};

static int device_805_init(struct pm80x_chip *chip)
{
	int ret = 0;
	struct regmap *map = chip->regmap;

	if (!map) {
		dev_err(chip->dev, "regmap is invalid\n");
		return -EINVAL;
	}

	chip->regmap_irq_chip = &pm805_irq_chip;

	ret = device_irq_init_805(chip);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to init pm805 irq!\n");
		goto out_irq_init;
	}

	ret = mfd_add_devices(chip->dev, 0, &codec_devs[0],
			      ARRAY_SIZE(codec_devs), &codec_resources[0], 0,
			      NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add codec subdev\n");
		goto out_codec;
	} else
		dev_info(chip->dev, "[%s]:Added mfd codec_devs\n", __func__);

	return 0;

out_codec:
	device_irq_exit_805(chip);
out_irq_init:
	return ret;
}

static int pm805_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int ret = 0;
	struct pm80x_chip *chip;
	struct pm80x_platform_data *pdata = dev_get_platdata(&client->dev);

	ret = pm80x_init(client);
	if (ret) {
		dev_err(&client->dev, "pm805_init fail!\n");
		goto out_init;
	}

	chip = i2c_get_clientdata(client);

	ret = device_805_init(chip);
	if (ret) {
		dev_err(chip->dev, "Failed to initialize 88pm805 devices\n");
		goto err_805_init;
	}

	if (pdata && pdata->plat_config)
		pdata->plat_config(chip, pdata);

err_805_init:
	pm80x_deinit();
out_init:
	return ret;
}

static int pm805_remove(struct i2c_client *client)
{
	struct pm80x_chip *chip = i2c_get_clientdata(client);

	mfd_remove_devices(chip->dev);
	device_irq_exit_805(chip);

	pm80x_deinit();

	return 0;
}

static struct i2c_driver pm805_driver = {
	.driver = {
		.name = "88PM805",
		.pm = &pm80x_pm_ops,
		},
	.probe = pm805_probe,
	.remove = pm805_remove,
	.id_table = pm80x_id_table,
};

static int __init pm805_i2c_init(void)
{
	return i2c_add_driver(&pm805_driver);
}
subsys_initcall(pm805_i2c_init);

static void __exit pm805_i2c_exit(void)
{
	i2c_del_driver(&pm805_driver);
}
module_exit(pm805_i2c_exit);

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM805");
MODULE_AUTHOR("Qiao Zhou <zhouqiao@marvell.com>");
MODULE_LICENSE("GPL");
