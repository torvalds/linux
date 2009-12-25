/*
 * MFD driver for twl4030 codec submodule
 *
 * Author:	Peter Ujfalusi <peter.ujfalusi@nokia.com>
 *
 * Copyright:   (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/core.h>
#include <linux/mfd/twl4030-codec.h>

#define TWL4030_CODEC_CELLS	2

static struct platform_device *twl4030_codec_dev;

struct twl4030_codec_resource {
	int request_count;
	u8 reg;
	u8 mask;
};

struct twl4030_codec {
	unsigned int audio_mclk;
	struct mutex mutex;
	struct twl4030_codec_resource resource[TWL4030_CODEC_RES_MAX];
	struct mfd_cell cells[TWL4030_CODEC_CELLS];
};

/*
 * Modify the resource, the function returns the content of the register
 * after the modification.
 */
static int twl4030_codec_set_resource(enum twl4030_codec_res id, int enable)
{
	struct twl4030_codec *codec = platform_get_drvdata(twl4030_codec_dev);
	u8 val;

	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &val,
			codec->resource[id].reg);

	if (enable)
		val |= codec->resource[id].mask;
	else
		val &= ~codec->resource[id].mask;

	twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					val, codec->resource[id].reg);

	return val;
}

static inline int twl4030_codec_get_resource(enum twl4030_codec_res id)
{
	struct twl4030_codec *codec = platform_get_drvdata(twl4030_codec_dev);
	u8 val;

	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &val,
			codec->resource[id].reg);

	return val;
}

/*
 * Enable the resource.
 * The function returns with error or the content of the register
 */
int twl4030_codec_enable_resource(enum twl4030_codec_res id)
{
	struct twl4030_codec *codec = platform_get_drvdata(twl4030_codec_dev);
	int val;

	if (id >= TWL4030_CODEC_RES_MAX) {
		dev_err(&twl4030_codec_dev->dev,
				"Invalid resource ID (%u)\n", id);
		return -EINVAL;
	}

	mutex_lock(&codec->mutex);
	if (!codec->resource[id].request_count)
		/* Resource was disabled, enable it */
		val = twl4030_codec_set_resource(id, 1);
	else
		val = twl4030_codec_get_resource(id);

	codec->resource[id].request_count++;
	mutex_unlock(&codec->mutex);

	return val;
}
EXPORT_SYMBOL_GPL(twl4030_codec_enable_resource);

/*
 * Disable the resource.
 * The function returns with error or the content of the register
 */
int twl4030_codec_disable_resource(unsigned id)
{
	struct twl4030_codec *codec = platform_get_drvdata(twl4030_codec_dev);
	int val;

	if (id >= TWL4030_CODEC_RES_MAX) {
		dev_err(&twl4030_codec_dev->dev,
				"Invalid resource ID (%u)\n", id);
		return -EINVAL;
	}

	mutex_lock(&codec->mutex);
	if (!codec->resource[id].request_count) {
		dev_err(&twl4030_codec_dev->dev,
			"Resource has been disabled already (%u)\n", id);
		mutex_unlock(&codec->mutex);
		return -EPERM;
	}
	codec->resource[id].request_count--;

	if (!codec->resource[id].request_count)
		/* Resource can be disabled now */
		val = twl4030_codec_set_resource(id, 0);
	else
		val = twl4030_codec_get_resource(id);

	mutex_unlock(&codec->mutex);

	return val;
}
EXPORT_SYMBOL_GPL(twl4030_codec_disable_resource);

unsigned int twl4030_codec_get_mclk(void)
{
	struct twl4030_codec *codec = platform_get_drvdata(twl4030_codec_dev);

	return codec->audio_mclk;
}
EXPORT_SYMBOL_GPL(twl4030_codec_get_mclk);

static int __devinit twl4030_codec_probe(struct platform_device *pdev)
{
	struct twl4030_codec *codec;
	struct twl4030_codec_data *pdata = pdev->dev.platform_data;
	struct mfd_cell *cell = NULL;
	int ret, childs = 0;
	u8 val;

	if (!pdata) {
		dev_err(&pdev->dev, "Platform data is missing\n");
		return -EINVAL;
	}

	/* Configure APLL_INFREQ and disable APLL if enabled */
	val = 0;
	switch (pdata->audio_mclk) {
	case 19200000:
		val |= TWL4030_APLL_INFREQ_19200KHZ;
		break;
	case 26000000:
		val |= TWL4030_APLL_INFREQ_26000KHZ;
		break;
	case 38400000:
		val |= TWL4030_APLL_INFREQ_38400KHZ;
		break;
	default:
		dev_err(&pdev->dev, "Invalid audio_mclk\n");
		return -EINVAL;
	}
	twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					val, TWL4030_REG_APLL_CTL);

	codec = kzalloc(sizeof(struct twl4030_codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	platform_set_drvdata(pdev, codec);

	twl4030_codec_dev = pdev;
	mutex_init(&codec->mutex);
	codec->audio_mclk = pdata->audio_mclk;

	/* Codec power */
	codec->resource[TWL4030_CODEC_RES_POWER].reg = TWL4030_REG_CODEC_MODE;
	codec->resource[TWL4030_CODEC_RES_POWER].mask = TWL4030_CODECPDZ;

	/* PLL */
	codec->resource[TWL4030_CODEC_RES_APLL].reg = TWL4030_REG_APLL_CTL;
	codec->resource[TWL4030_CODEC_RES_APLL].mask = TWL4030_APLL_EN;

	if (pdata->audio) {
		cell = &codec->cells[childs];
		cell->name = "twl4030_codec_audio";
		cell->platform_data = pdata->audio;
		cell->data_size = sizeof(*pdata->audio);
		childs++;
	}
	if (pdata->vibra) {
		cell = &codec->cells[childs];
		cell->name = "twl4030_codec_vibra";
		cell->platform_data = pdata->vibra;
		cell->data_size = sizeof(*pdata->vibra);
		childs++;
	}

	if (childs)
		ret = mfd_add_devices(&pdev->dev, pdev->id, codec->cells,
				      childs, NULL, 0);
	else {
		dev_err(&pdev->dev, "No platform data found for childs\n");
		ret = -ENODEV;
	}

	if (!ret)
		return 0;

	platform_set_drvdata(pdev, NULL);
	kfree(codec);
	twl4030_codec_dev = NULL;
	return ret;
}

static int __devexit twl4030_codec_remove(struct platform_device *pdev)
{
	struct twl4030_codec *codec = platform_get_drvdata(pdev);

	mfd_remove_devices(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	kfree(codec);
	twl4030_codec_dev = NULL;

	return 0;
}

MODULE_ALIAS("platform:twl4030_codec");

static struct platform_driver twl4030_codec_driver = {
	.probe		= twl4030_codec_probe,
	.remove		= __devexit_p(twl4030_codec_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "twl4030_codec",
	},
};

static int __devinit twl4030_codec_init(void)
{
	return platform_driver_register(&twl4030_codec_driver);
}
module_init(twl4030_codec_init);

static void __devexit twl4030_codec_exit(void)
{
	platform_driver_unregister(&twl4030_codec_driver);
}
module_exit(twl4030_codec_exit);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@nokia.com>");
MODULE_LICENSE("GPL");

