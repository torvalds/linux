// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Wolfson WM97xx -- Core device
 *
 * Copyright (C) 2017 Robert Jarzmik
 *
 * Features:
 *  - an AC97 audio codec
 *  - a touchscreen driver
 *  - a GPIO block
 */

#include <linux/device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/wm97xx.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/wm97xx.h>
#include <sound/ac97/codec.h>
#include <sound/ac97/compat.h>

#define WM9705_VENDOR_ID 0x574d4c05
#define WM9712_VENDOR_ID 0x574d4c12
#define WM9713_VENDOR_ID 0x574d4c13
#define WM97xx_VENDOR_ID_MASK 0xffffffff

struct wm97xx_priv {
	struct regmap *regmap;
	struct snd_ac97 *ac97;
	struct device *dev;
	struct wm97xx_platform_data codec_pdata;
};

static bool wm97xx_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_RESET ... AC97_PCM_SURR_DAC_RATE:
	case AC97_PCM_LR_ADC_RATE:
	case AC97_CENTER_LFE_MASTER:
	case AC97_SPDIF ... AC97_LINE1_LEVEL:
	case AC97_GPIO_CFG ... 0x5c:
	case AC97_CODEC_CLASS_REV ... AC97_PCI_SID:
	case 0x74 ... AC97_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static bool wm97xx_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_VENDOR_ID1:
	case AC97_VENDOR_ID2:
		return false;
	default:
		return wm97xx_readable_reg(dev, reg);
	}
}

static const struct reg_default wm9705_reg_defaults[] = {
	{ 0x02, 0x8000 },
	{ 0x04, 0x8000 },
	{ 0x06, 0x8000 },
	{ 0x0a, 0x8000 },
	{ 0x0c, 0x8008 },
	{ 0x0e, 0x8008 },
	{ 0x10, 0x8808 },
	{ 0x12, 0x8808 },
	{ 0x14, 0x8808 },
	{ 0x16, 0x8808 },
	{ 0x18, 0x8808 },
	{ 0x1a, 0x0000 },
	{ 0x1c, 0x8000 },
	{ 0x20, 0x0000 },
	{ 0x22, 0x0000 },
	{ 0x26, 0x000f },
	{ 0x28, 0x0605 },
	{ 0x2a, 0x0000 },
	{ 0x2c, 0xbb80 },
	{ 0x32, 0xbb80 },
	{ 0x34, 0x2000 },
	{ 0x5a, 0x0000 },
	{ 0x5c, 0x0000 },
	{ 0x72, 0x0808 },
	{ 0x74, 0x0000 },
	{ 0x76, 0x0006 },
	{ 0x78, 0x0000 },
	{ 0x7a, 0x0000 },
};

static const struct regmap_config wm9705_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x7e,
	.cache_type = REGCACHE_RBTREE,

	.reg_defaults = wm9705_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm9705_reg_defaults),
	.volatile_reg = regmap_ac97_default_volatile,
	.readable_reg = wm97xx_readable_reg,
	.writeable_reg = wm97xx_writeable_reg,
};

static struct mfd_cell wm9705_cells[] = {
	{ .name = "wm9705-codec", },
	{ .name = "wm97xx-ts", },
};

static bool wm9712_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_REC_GAIN:
		return true;
	default:
		return regmap_ac97_default_volatile(dev, reg);
	}
}

static const struct reg_default wm9712_reg_defaults[] = {
	{ 0x02, 0x8000 },
	{ 0x04, 0x8000 },
	{ 0x06, 0x8000 },
	{ 0x08, 0x0f0f },
	{ 0x0a, 0xaaa0 },
	{ 0x0c, 0xc008 },
	{ 0x0e, 0x6808 },
	{ 0x10, 0xe808 },
	{ 0x12, 0xaaa0 },
	{ 0x14, 0xad00 },
	{ 0x16, 0x8000 },
	{ 0x18, 0xe808 },
	{ 0x1a, 0x3000 },
	{ 0x1c, 0x8000 },
	{ 0x20, 0x0000 },
	{ 0x22, 0x0000 },
	{ 0x26, 0x000f },
	{ 0x28, 0x0605 },
	{ 0x2a, 0x0410 },
	{ 0x2c, 0xbb80 },
	{ 0x2e, 0xbb80 },
	{ 0x32, 0xbb80 },
	{ 0x34, 0x2000 },
	{ 0x4c, 0xf83e },
	{ 0x4e, 0xffff },
	{ 0x50, 0x0000 },
	{ 0x52, 0x0000 },
	{ 0x56, 0xf83e },
	{ 0x58, 0x0008 },
	{ 0x5c, 0x0000 },
	{ 0x60, 0xb032 },
	{ 0x62, 0x3e00 },
	{ 0x64, 0x0000 },
	{ 0x76, 0x0006 },
	{ 0x78, 0x0001 },
	{ 0x7a, 0x0000 },
};

static const struct regmap_config wm9712_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x7e,
	.cache_type = REGCACHE_RBTREE,

	.reg_defaults = wm9712_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm9712_reg_defaults),
	.volatile_reg = wm9712_volatile_reg,
	.readable_reg = wm97xx_readable_reg,
	.writeable_reg = wm97xx_writeable_reg,
};

static struct mfd_cell wm9712_cells[] = {
	{ .name = "wm9712-codec", },
	{ .name = "wm97xx-ts", },
};

static const struct reg_default wm9713_reg_defaults[] = {
	{ 0x02, 0x8080 },	/* Speaker Output Volume */
	{ 0x04, 0x8080 },	/* Headphone Output Volume */
	{ 0x06, 0x8080 },	/* Out3/OUT4 Volume */
	{ 0x08, 0xc880 },	/* Mono Volume */
	{ 0x0a, 0xe808 },	/* LINEIN Volume */
	{ 0x0c, 0xe808 },	/* DAC PGA Volume */
	{ 0x0e, 0x0808 },	/* MIC PGA Volume */
	{ 0x10, 0x00da },	/* MIC Routing Control */
	{ 0x12, 0x8000 },	/* Record PGA Volume */
	{ 0x14, 0xd600 },	/* Record Routing */
	{ 0x16, 0xaaa0 },	/* PCBEEP Volume */
	{ 0x18, 0xaaa0 },	/* VxDAC Volume */
	{ 0x1a, 0xaaa0 },	/* AUXDAC Volume */
	{ 0x1c, 0x0000 },	/* Output PGA Mux */
	{ 0x1e, 0x0000 },	/* DAC 3D control */
	{ 0x20, 0x0f0f },	/* DAC Tone Control*/
	{ 0x22, 0x0040 },	/* MIC Input Select & Bias */
	{ 0x24, 0x0000 },	/* Output Volume Mapping & Jack */
	{ 0x26, 0x7f00 },	/* Powerdown Ctrl/Stat*/
	{ 0x28, 0x0405 },	/* Extended Audio ID */
	{ 0x2a, 0x0410 },	/* Extended Audio Start/Ctrl */
	{ 0x2c, 0xbb80 },	/* Audio DACs Sample Rate */
	{ 0x2e, 0xbb80 },	/* AUXDAC Sample Rate */
	{ 0x32, 0xbb80 },	/* Audio ADCs Sample Rate */
	{ 0x36, 0x4523 },	/* PCM codec control */
	{ 0x3a, 0x2000 },	/* SPDIF control */
	{ 0x3c, 0xfdff },	/* Powerdown 1 */
	{ 0x3e, 0xffff },	/* Powerdown 2 */
	{ 0x40, 0x0000 },	/* General Purpose */
	{ 0x42, 0x0000 },	/* Fast Power-Up Control */
	{ 0x44, 0x0080 },	/* MCLK/PLL Control */
	{ 0x46, 0x0000 },	/* MCLK/PLL Control */

	{ 0x4c, 0xfffe },	/* GPIO Pin Configuration */
	{ 0x4e, 0xffff },	/* GPIO Pin Polarity / Type */
	{ 0x50, 0x0000 },	/* GPIO Pin Sticky */
	{ 0x52, 0x0000 },	/* GPIO Pin Wake-Up */
				/* GPIO Pin Status */
	{ 0x56, 0xfffe },	/* GPIO Pin Sharing */
	{ 0x58, 0x4000 },	/* GPIO PullUp/PullDown */
	{ 0x5a, 0x0000 },	/* Additional Functions 1 */
	{ 0x5c, 0x0000 },	/* Additional Functions 2 */
	{ 0x60, 0xb032 },	/* ALC Control */
	{ 0x62, 0x3e00 },	/* ALC / Noise Gate Control */
	{ 0x64, 0x0000 },	/* AUXDAC input control */
	{ 0x74, 0x0000 },	/* Digitiser Reg 1 */
	{ 0x76, 0x0006 },	/* Digitiser Reg 2 */
	{ 0x78, 0x0001 },	/* Digitiser Reg 3 */
	{ 0x7a, 0x0000 },	/* Digitiser Read Back */
};

static const struct regmap_config wm9713_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x7e,
	.cache_type = REGCACHE_RBTREE,

	.reg_defaults = wm9713_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm9713_reg_defaults),
	.volatile_reg = regmap_ac97_default_volatile,
	.readable_reg = wm97xx_readable_reg,
	.writeable_reg = wm97xx_writeable_reg,
};

static struct mfd_cell wm9713_cells[] = {
	{ .name = "wm9713-codec", },
	{ .name = "wm97xx-ts", },
};

static int wm97xx_ac97_probe(struct ac97_codec_device *adev)
{
	struct wm97xx_priv *wm97xx;
	const struct regmap_config *config;
	struct wm97xx_platform_data *codec_pdata;
	struct mfd_cell *cells;
	int ret = -ENODEV, nb_cells, i;
	struct wm97xx_pdata *pdata = snd_ac97_codec_get_platdata(adev);

	wm97xx = devm_kzalloc(ac97_codec_dev2dev(adev),
			      sizeof(*wm97xx), GFP_KERNEL);
	if (!wm97xx)
		return -ENOMEM;

	wm97xx->dev = ac97_codec_dev2dev(adev);
	wm97xx->ac97 = snd_ac97_compat_alloc(adev);
	if (IS_ERR(wm97xx->ac97))
		return PTR_ERR(wm97xx->ac97);


	ac97_set_drvdata(adev, wm97xx);
	dev_info(wm97xx->dev, "wm97xx core found, id=0x%x\n",
		 adev->vendor_id);

	codec_pdata = &wm97xx->codec_pdata;
	codec_pdata->ac97 = wm97xx->ac97;
	codec_pdata->batt_pdata = pdata ? pdata->batt_pdata : NULL;

	switch (adev->vendor_id) {
	case WM9705_VENDOR_ID:
		config = &wm9705_regmap_config;
		cells = wm9705_cells;
		nb_cells = ARRAY_SIZE(wm9705_cells);
		break;
	case WM9712_VENDOR_ID:
		config = &wm9712_regmap_config;
		cells = wm9712_cells;
		nb_cells = ARRAY_SIZE(wm9712_cells);
		break;
	case WM9713_VENDOR_ID:
		config = &wm9713_regmap_config;
		cells = wm9713_cells;
		nb_cells = ARRAY_SIZE(wm9713_cells);
		break;
	default:
		goto err_free_compat;
	}

	for (i = 0; i < nb_cells; i++) {
		cells[i].platform_data = codec_pdata;
		cells[i].pdata_size = sizeof(*codec_pdata);
	}

	codec_pdata->regmap = devm_regmap_init_ac97(wm97xx->ac97, config);
	if (IS_ERR(codec_pdata->regmap)) {
		ret = PTR_ERR(codec_pdata->regmap);
		goto err_free_compat;
	}

	ret = devm_mfd_add_devices(wm97xx->dev, PLATFORM_DEVID_NONE,
				   cells, nb_cells, NULL, 0, NULL);
	if (ret)
		goto err_free_compat;

	return ret;

err_free_compat:
	snd_ac97_compat_release(wm97xx->ac97);
	return ret;
}

static int wm97xx_ac97_remove(struct ac97_codec_device *adev)
{
	struct wm97xx_priv *wm97xx = ac97_get_drvdata(adev);

	snd_ac97_compat_release(wm97xx->ac97);

	return 0;
}

static const struct ac97_id wm97xx_ac97_ids[] = {
	{ .id = WM9705_VENDOR_ID, .mask = WM97xx_VENDOR_ID_MASK },
	{ .id = WM9712_VENDOR_ID, .mask = WM97xx_VENDOR_ID_MASK },
	{ .id = WM9713_VENDOR_ID, .mask = WM97xx_VENDOR_ID_MASK },
	{ }
};

static struct ac97_codec_driver wm97xx_ac97_driver = {
	.driver = {
		.name = "wm97xx-core",
	},
	.probe		= wm97xx_ac97_probe,
	.remove		= wm97xx_ac97_remove,
	.id_table	= wm97xx_ac97_ids,
};

static int __init wm97xx_module_init(void)
{
	return snd_ac97_codec_driver_register(&wm97xx_ac97_driver);
}
module_init(wm97xx_module_init);

static void __exit wm97xx_module_exit(void)
{
	snd_ac97_codec_driver_unregister(&wm97xx_ac97_driver);
}
module_exit(wm97xx_module_exit);

MODULE_DESCRIPTION("WM9712, WM9713 core driver");
MODULE_AUTHOR("Robert Jarzmik <robert.jarzmik@free.fr>");
MODULE_LICENSE("GPL");

