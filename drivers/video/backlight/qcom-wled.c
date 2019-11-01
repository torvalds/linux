// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015, Sony Mobile Communications, AB.
 */

#include <linux/kernel.h>
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

/* From DT binding */
#define WLED_MAX_STRINGS				4

#define WLED_DEFAULT_BRIGHTNESS				2048

#define WLED3_SINK_REG_BRIGHT_MAX			0xFFF

/* WLED3 control registers */
#define WLED3_CTRL_REG_MOD_EN				0x46
#define  WLED3_CTRL_REG_MOD_EN_MASK			BIT(7)
#define  WLED3_CTRL_REG_MOD_EN_SHIFT			7

#define WLED3_CTRL_REG_FREQ				0x4c
#define  WLED3_CTRL_REG_FREQ_MASK			GENMASK(3, 0)

#define WLED3_CTRL_REG_OVP				0x4d
#define  WLED3_CTRL_REG_OVP_MASK				GENMASK(1, 0)

#define WLED3_CTRL_REG_ILIMIT				0x4e
#define  WLED3_CTRL_REG_ILIMIT_MASK			GENMASK(2, 0)

/* WLED3 sink registers */
#define WLED3_SINK_REG_SYNC				0x47
#define  WLED3_SINK_REG_SYNC_CLEAR			0x00

#define WLED3_SINK_REG_CURR_SINK			0x4f
#define  WLED3_SINK_REG_CURR_SINK_MASK			GENMASK(7, 5)
#define  WLED3_SINK_REG_CURR_SINK_SHFT			5

/* WLED3 specific per-'string' registers below */
#define WLED3_SINK_REG_BRIGHT(n)			(0x40 + n)

#define WLED3_SINK_REG_STR_MOD_EN(n)			(0x60 + (n * 0x10))
#define  WLED3_SINK_REG_STR_MOD_MASK			BIT(7)

#define WLED3_SINK_REG_STR_FULL_SCALE_CURR(n)		(0x62 + (n * 0x10))
#define  WLED3_SINK_REG_STR_FULL_SCALE_CURR_MASK	GENMASK(4, 0)

#define WLED3_SINK_REG_STR_MOD_SRC(n)			(0x63 + (n * 0x10))
#define  WLED3_SINK_REG_STR_MOD_SRC_MASK		BIT(0)
#define  WLED3_SINK_REG_STR_MOD_SRC_INT			0x00
#define  WLED3_SINK_REG_STR_MOD_SRC_EXT			0x01

#define WLED3_SINK_REG_STR_CABC(n)			(0x66 + (n * 0x10))
#define  WLED3_SINK_REG_STR_CABC_MASK			BIT(7)

struct wled_var_cfg {
	const u32 *values;
	u32 (*fn)(u32);
	int size;
};

struct wled_u32_opts {
	const char *name;
	u32 *val_ptr;
	const struct wled_var_cfg *cfg;
};

struct wled_bool_opts {
	const char *name;
	bool *val_ptr;
};

struct wled_config {
	u32 boost_i_limit;
	u32 ovp;
	u32 switch_freq;
	u32 num_strings;
	u32 string_i_limit;
	u32 enabled_strings[WLED_MAX_STRINGS];
	bool cs_out_en;
	bool ext_gen;
	bool cabc;
};

struct wled {
	const char *name;
	struct device *dev;
	struct regmap *regmap;
	u16 ctrl_addr;
	u16 max_string_count;
	u32 brightness;
	u32 max_brightness;

	struct wled_config cfg;
	int (*wled_set_brightness)(struct wled *wled, u16 brightness);
};

static int wled3_set_brightness(struct wled *wled, u16 brightness)
{
	int rc, i;
	u8 v[2];

	v[0] = brightness & 0xff;
	v[1] = (brightness >> 8) & 0xf;

	for (i = 0;  i < wled->cfg.num_strings; ++i) {
		rc = regmap_bulk_write(wled->regmap, wled->ctrl_addr +
				       WLED3_SINK_REG_BRIGHT(i), v, 2);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wled_module_enable(struct wled *wled, int val)
{
	int rc;

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
				WLED3_CTRL_REG_MOD_EN,
				WLED3_CTRL_REG_MOD_EN_MASK,
				val << WLED3_CTRL_REG_MOD_EN_SHIFT);
	return rc;
}

static int wled_sync_toggle(struct wled *wled)
{
	int rc;
	unsigned int mask = GENMASK(wled->max_string_count - 1, 0);

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_SINK_REG_SYNC,
				mask, mask);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_SINK_REG_SYNC,
				mask, WLED3_SINK_REG_SYNC_CLEAR);

	return rc;
}

static int wled_update_status(struct backlight_device *bl)
{
	struct wled *wled = bl_get_data(bl);
	u16 brightness = bl->props.brightness;
	int rc = 0;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	if (brightness) {
		rc = wled->wled_set_brightness(wled, brightness);
		if (rc < 0) {
			dev_err(wled->dev, "wled failed to set brightness rc:%d\n",
				rc);
			return rc;
		}

		rc = wled_sync_toggle(wled);
		if (rc < 0) {
			dev_err(wled->dev, "wled sync failed rc:%d\n", rc);
			return rc;
		}
	}

	if (!!brightness != !!wled->brightness) {
		rc = wled_module_enable(wled, !!brightness);
		if (rc < 0) {
			dev_err(wled->dev, "wled enable failed rc:%d\n", rc);
			return rc;
		}
	}

	wled->brightness = brightness;

	return rc;
}

static int wled3_setup(struct wled *wled)
{
	u16 addr;
	u8 sink_en = 0;
	int rc, i, j;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_OVP,
				WLED3_CTRL_REG_OVP_MASK, wled->cfg.ovp);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_ILIMIT,
				WLED3_CTRL_REG_ILIMIT_MASK,
				wled->cfg.boost_i_limit);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_FREQ,
				WLED3_CTRL_REG_FREQ_MASK,
				wled->cfg.switch_freq);
	if (rc)
		return rc;

	for (i = 0; i < wled->cfg.num_strings; ++i) {
		j = wled->cfg.enabled_strings[i];
		addr = wled->ctrl_addr + WLED3_SINK_REG_STR_MOD_EN(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED3_SINK_REG_STR_MOD_MASK,
					WLED3_SINK_REG_STR_MOD_MASK);
		if (rc)
			return rc;

		if (wled->cfg.ext_gen) {
			addr = wled->ctrl_addr + WLED3_SINK_REG_STR_MOD_SRC(j);
			rc = regmap_update_bits(wled->regmap, addr,
						WLED3_SINK_REG_STR_MOD_SRC_MASK,
						WLED3_SINK_REG_STR_MOD_SRC_EXT);
			if (rc)
				return rc;
		}

		addr = wled->ctrl_addr + WLED3_SINK_REG_STR_FULL_SCALE_CURR(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED3_SINK_REG_STR_FULL_SCALE_CURR_MASK,
					wled->cfg.string_i_limit);
		if (rc)
			return rc;

		addr = wled->ctrl_addr + WLED3_SINK_REG_STR_CABC(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED3_SINK_REG_STR_CABC_MASK,
					wled->cfg.cabc ?
					WLED3_SINK_REG_STR_CABC_MASK : 0);
		if (rc)
			return rc;

		sink_en |= BIT(j + WLED3_SINK_REG_CURR_SINK_SHFT);
	}

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_SINK_REG_CURR_SINK,
				WLED3_SINK_REG_CURR_SINK_MASK, sink_en);
	if (rc)
		return rc;

	return 0;
}

static const struct wled_config wled3_config_defaults = {
	.boost_i_limit = 3,
	.string_i_limit = 20,
	.ovp = 2,
	.num_strings = 3,
	.switch_freq = 5,
	.cs_out_en = false,
	.ext_gen = false,
	.cabc = false,
	.enabled_strings = {0, 1, 2, 3},
};

static const u32 wled3_boost_i_limit_values[] = {
	105, 385, 525, 805, 980, 1260, 1400, 1680,
};

static const struct wled_var_cfg wled3_boost_i_limit_cfg = {
	.values = wled3_boost_i_limit_values,
	.size = ARRAY_SIZE(wled3_boost_i_limit_values),
};

static const u32 wled3_ovp_values[] = {
	35, 32, 29, 27,
};

static const struct wled_var_cfg wled3_ovp_cfg = {
	.values = wled3_ovp_values,
	.size = ARRAY_SIZE(wled3_ovp_values),
};

static u32 wled3_num_strings_values_fn(u32 idx)
{
	return idx + 1;
}

static const struct wled_var_cfg wled3_num_strings_cfg = {
	.fn = wled3_num_strings_values_fn,
	.size = 3,
};

static u32 wled3_switch_freq_values_fn(u32 idx)
{
	return 19200 / (2 * (1 + idx));
}

static const struct wled_var_cfg wled3_switch_freq_cfg = {
	.fn = wled3_switch_freq_values_fn,
	.size = 16,
};

static const struct wled_var_cfg wled3_string_i_limit_cfg = {
	.size = 26,
};

static const struct wled_var_cfg wled3_string_cfg = {
	.size = 8,
};

static u32 wled_values(const struct wled_var_cfg *cfg, u32 idx)
{
	if (idx >= cfg->size)
		return UINT_MAX;
	if (cfg->fn)
		return cfg->fn(idx);
	if (cfg->values)
		return cfg->values[idx];
	return idx;
}

static int wled_configure(struct wled *wled, int version)
{
	struct wled_config *cfg = &wled->cfg;
	struct device *dev = wled->dev;
	const __be32 *prop_addr;
	u32 size, val, c, string_len;
	int rc, i, j;

	const struct wled_u32_opts *u32_opts = NULL;
	const struct wled_u32_opts wled3_opts[] = {
		{
			.name = "qcom,current-boost-limit",
			.val_ptr = &cfg->boost_i_limit,
			.cfg = &wled3_boost_i_limit_cfg,
		},
		{
			.name = "qcom,current-limit",
			.val_ptr = &cfg->string_i_limit,
			.cfg = &wled3_string_i_limit_cfg,
		},
		{
			.name = "qcom,ovp",
			.val_ptr = &cfg->ovp,
			.cfg = &wled3_ovp_cfg,
		},
		{
			.name = "qcom,switching-freq",
			.val_ptr = &cfg->switch_freq,
			.cfg = &wled3_switch_freq_cfg,
		},
		{
			.name = "qcom,num-strings",
			.val_ptr = &cfg->num_strings,
			.cfg = &wled3_num_strings_cfg,
		},
	};

	const struct wled_bool_opts bool_opts[] = {
		{ "qcom,cs-out", &cfg->cs_out_en, },
		{ "qcom,ext-gen", &cfg->ext_gen, },
		{ "qcom,cabc", &cfg->cabc, },
	};

	prop_addr = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!prop_addr) {
		dev_err(wled->dev, "invalid IO resources\n");
		return -EINVAL;
	}
	wled->ctrl_addr = be32_to_cpu(*prop_addr);

	rc = of_property_read_string(dev->of_node, "label", &wled->name);
	if (rc)
		wled->name = devm_kasprintf(dev, GFP_KERNEL, "%pOFn", dev->of_node);

	switch (version) {
	case 3:
		u32_opts = wled3_opts;
		size = ARRAY_SIZE(wled3_opts);
		*cfg = wled3_config_defaults;
		wled->wled_set_brightness = wled3_set_brightness;
		wled->max_string_count = 3;
		break;

	default:
		dev_err(wled->dev, "Invalid WLED version\n");
		return -EINVAL;
	}

	for (i = 0; i < size; ++i) {
		rc = of_property_read_u32(dev->of_node, u32_opts[i].name, &val);
		if (rc == -EINVAL) {
			continue;
		} else if (rc) {
			dev_err(dev, "error reading '%s'\n", u32_opts[i].name);
			return rc;
		}

		c = UINT_MAX;
		for (j = 0; c != val; j++) {
			c = wled_values(u32_opts[i].cfg, j);
			if (c == UINT_MAX) {
				dev_err(dev, "invalid value for '%s'\n",
					u32_opts[i].name);
				return -EINVAL;
			}

			if (c == val)
				break;
		}

		dev_dbg(dev, "'%s' = %u\n", u32_opts[i].name, c);
		*u32_opts[i].val_ptr = j;
	}

	for (i = 0; i < ARRAY_SIZE(bool_opts); ++i) {
		if (of_property_read_bool(dev->of_node, bool_opts[i].name))
			*bool_opts[i].val_ptr = true;
	}

	cfg->num_strings = cfg->num_strings + 1;

	string_len = of_property_count_elems_of_size(dev->of_node,
						     "qcom,enabled-strings",
						     sizeof(u32));
	if (string_len > 0)
		of_property_read_u32_array(dev->of_node,
						"qcom,enabled-strings",
						wled->cfg.enabled_strings,
						sizeof(u32));

	return 0;
}

static const struct backlight_ops wled_ops = {
	.update_status = wled_update_status,
};

static int wled_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bl;
	struct wled *wled;
	struct regmap *regmap;
	int version;
	u32 val;
	int rc;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "Unable to get regmap\n");
		return -EINVAL;
	}

	wled = devm_kzalloc(&pdev->dev, sizeof(*wled), GFP_KERNEL);
	if (!wled)
		return -ENOMEM;

	wled->regmap = regmap;
	wled->dev = &pdev->dev;

	version = (uintptr_t)of_device_get_match_data(&pdev->dev);
	if (!version) {
		dev_err(&pdev->dev, "Unknown device version\n");
		return -ENODEV;
	}

	rc = wled_configure(wled, version);
	if (rc)
		return rc;

	switch (version) {
	case 3:
		rc = wled3_setup(wled);
		if (rc) {
			dev_err(&pdev->dev, "wled3_setup failed\n");
			return rc;
		}
		break;

	default:
		dev_err(wled->dev, "Invalid WLED version\n");
		break;
	}

	val = WLED_DEFAULT_BRIGHTNESS;
	of_property_read_u32(pdev->dev.of_node, "default-brightness", &val);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.brightness = val;
	props.max_brightness = WLED3_SINK_REG_BRIGHT_MAX;
	bl = devm_backlight_device_register(&pdev->dev, wled->name,
					    &pdev->dev, wled,
					    &wled_ops, &props);
	return PTR_ERR_OR_ZERO(bl);
};

static const struct of_device_id wled_match_table[] = {
	{ .compatible = "qcom,pm8941-wled", .data = (void *)3 },
	{}
};
MODULE_DEVICE_TABLE(of, wled_match_table);

static struct platform_driver wled_driver = {
	.probe = wled_probe,
	.driver	= {
		.name = "qcom,wled",
		.of_match_table	= wled_match_table,
	},
};

module_platform_driver(wled_driver);

MODULE_DESCRIPTION("Qualcomm WLED driver");
MODULE_LICENSE("GPL v2");
