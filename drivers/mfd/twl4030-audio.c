/*
 * MFD driver for twl4030 audio submodule, which contains an audio codec, and
 * the vibra control.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
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
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/core.h>
#include <linux/mfd/twl4030-audio.h>

#define TWL4030_AUDIO_CELLS	2

static struct platform_device *twl4030_audio_dev;

struct twl4030_audio_resource {
	int request_count;
	u8 reg;
	u8 mask;
};

struct twl4030_audio {
	unsigned int audio_mclk;
	struct mutex mutex;
	struct twl4030_audio_resource resource[TWL4030_AUDIO_RES_MAX];
	struct mfd_cell cells[TWL4030_AUDIO_CELLS];
};

/*
 * Modify the resource, the function returns the content of the register
 * after the modification.
 */
static int twl4030_audio_set_resource(enum twl4030_audio_res id, int enable)
{
	struct twl4030_audio *audio = platform_get_drvdata(twl4030_audio_dev);
	u8 val;

	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &val,
			audio->resource[id].reg);

	if (enable)
		val |= audio->resource[id].mask;
	else
		val &= ~audio->resource[id].mask;

	twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
					val, audio->resource[id].reg);

	return val;
}

static inline int twl4030_audio_get_resource(enum twl4030_audio_res id)
{
	struct twl4030_audio *audio = platform_get_drvdata(twl4030_audio_dev);
	u8 val;

	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE, &val,
			audio->resource[id].reg);

	return val;
}

/*
 * Enable the resource.
 * The function returns with error or the content of the register
 */
int twl4030_audio_enable_resource(enum twl4030_audio_res id)
{
	struct twl4030_audio *audio = platform_get_drvdata(twl4030_audio_dev);
	int val;

	if (id >= TWL4030_AUDIO_RES_MAX) {
		dev_err(&twl4030_audio_dev->dev,
				"Invalid resource ID (%u)\n", id);
		return -EINVAL;
	}

	mutex_lock(&audio->mutex);
	if (!audio->resource[id].request_count)
		/* Resource was disabled, enable it */
		val = twl4030_audio_set_resource(id, 1);
	else
		val = twl4030_audio_get_resource(id);

	audio->resource[id].request_count++;
	mutex_unlock(&audio->mutex);

	return val;
}
EXPORT_SYMBOL_GPL(twl4030_audio_enable_resource);

/*
 * Disable the resource.
 * The function returns with error or the content of the register
 */
int twl4030_audio_disable_resource(unsigned id)
{
	struct twl4030_audio *audio = platform_get_drvdata(twl4030_audio_dev);
	int val;

	if (id >= TWL4030_AUDIO_RES_MAX) {
		dev_err(&twl4030_audio_dev->dev,
				"Invalid resource ID (%u)\n", id);
		return -EINVAL;
	}

	mutex_lock(&audio->mutex);
	if (!audio->resource[id].request_count) {
		dev_err(&twl4030_audio_dev->dev,
			"Resource has been disabled already (%u)\n", id);
		mutex_unlock(&audio->mutex);
		return -EPERM;
	}
	audio->resource[id].request_count--;

	if (!audio->resource[id].request_count)
		/* Resource can be disabled now */
		val = twl4030_audio_set_resource(id, 0);
	else
		val = twl4030_audio_get_resource(id);

	mutex_unlock(&audio->mutex);

	return val;
}
EXPORT_SYMBOL_GPL(twl4030_audio_disable_resource);

unsigned int twl4030_audio_get_mclk(void)
{
	struct twl4030_audio *audio = platform_get_drvdata(twl4030_audio_dev);

	return audio->audio_mclk;
}
EXPORT_SYMBOL_GPL(twl4030_audio_get_mclk);

static int __devinit twl4030_audio_probe(struct platform_device *pdev)
{
	struct twl4030_audio *audio;
	struct twl4030_audio_data *pdata = pdev->dev.platform_data;
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

	audio = devm_kzalloc(&pdev->dev, sizeof(struct twl4030_audio),
			     GFP_KERNEL);
	if (!audio)
		return -ENOMEM;

	platform_set_drvdata(pdev, audio);

	twl4030_audio_dev = pdev;
	mutex_init(&audio->mutex);
	audio->audio_mclk = pdata->audio_mclk;

	/* Codec power */
	audio->resource[TWL4030_AUDIO_RES_POWER].reg = TWL4030_REG_CODEC_MODE;
	audio->resource[TWL4030_AUDIO_RES_POWER].mask = TWL4030_CODECPDZ;

	/* PLL */
	audio->resource[TWL4030_AUDIO_RES_APLL].reg = TWL4030_REG_APLL_CTL;
	audio->resource[TWL4030_AUDIO_RES_APLL].mask = TWL4030_APLL_EN;

	if (pdata->codec) {
		cell = &audio->cells[childs];
		cell->name = "twl4030-codec";
		cell->platform_data = pdata->codec;
		cell->pdata_size = sizeof(*pdata->codec);
		childs++;
	}
	if (pdata->vibra) {
		cell = &audio->cells[childs];
		cell->name = "twl4030-vibra";
		cell->platform_data = pdata->vibra;
		cell->pdata_size = sizeof(*pdata->vibra);
		childs++;
	}

	if (childs)
		ret = mfd_add_devices(&pdev->dev, pdev->id, audio->cells,
				      childs, NULL, 0, NULL);
	else {
		dev_err(&pdev->dev, "No platform data found for childs\n");
		ret = -ENODEV;
	}

	if (ret) {
		platform_set_drvdata(pdev, NULL);
		twl4030_audio_dev = NULL;
	}

	return ret;
}

static int __devexit twl4030_audio_remove(struct platform_device *pdev)
{
	mfd_remove_devices(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	twl4030_audio_dev = NULL;

	return 0;
}

static struct platform_driver twl4030_audio_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "twl4030-audio",
	},
	.probe		= twl4030_audio_probe,
	.remove		= __devexit_p(twl4030_audio_remove),
};

module_platform_driver(twl4030_audio_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("TWL4030 audio block MFD driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030-audio");
