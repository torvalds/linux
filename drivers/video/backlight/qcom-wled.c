// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015, Sony Mobile Communications, AB.
 */

#include <linux/kernel.h>
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

/* From DT binding */
#define WLED_DEFAULT_BRIGHTNESS				2048

#define WLED3_SINK_REG_BRIGHT_MAX			0xFFF
#define WLED3_CTRL_REG_VAL_BASE				0x40

/* WLED3 control registers */
#define WLED3_CTRL_REG_MOD_EN				0x46
#define  WLED3_CTRL_REG_MOD_EN_BIT			BIT(7)
#define  WLED3_CTRL_REG_MOD_EN_MASK			BIT(7)

#define WLED3_CTRL_REG_FREQ				0x4c
#define  WLED3_CTRL_REG_FREQ_MASK			0x0f

#define WLED3_CTRL_REG_OVP				0x4d
#define  WLED3_CTRL_REG_OVP_MASK			0x03

#define WLED3_CTRL_REG_ILIMIT				0x4e
#define  WLED3_CTRL_REG_ILIMIT_MASK			0x07

/* WLED3 sink registers */
#define WLED3_SINK_REG_SYNC				0x47
#define  WLED3_SINK_REG_SYNC_MASK			0x07
#define  WLED3_SINK_REG_SYNC_LED1			BIT(0)
#define  WLED3_SINK_REG_SYNC_LED2			BIT(1)
#define  WLED3_SINK_REG_SYNC_LED3			BIT(2)
#define  WLED3_SINK_REG_SYNC_ALL			0x07
#define  WLED3_SINK_REG_SYNC_CLEAR			0x00

#define WLED3_SINK_REG_CURR_SINK			0x4f
#define  WLED3_SINK_REG_CURR_SINK_MASK			0xe0
#define  WLED3_SINK_REG_CURR_SINK_SHFT			0x05

/* WLED3 per-'string' registers below */
#define WLED3_SINK_REG_STR_OFFSET			0x10

#define WLED3_SINK_REG_STR_MOD_EN_BASE			0x60
#define  WLED3_SINK_REG_STR_MOD_MASK			BIT(7)
#define  WLED3_SINK_REG_STR_MOD_EN			BIT(7)

#define WLED3_SINK_REG_STR_FULL_SCALE_CURR		0x62
#define  WLED3_SINK_REG_STR_FULL_SCALE_CURR_MASK	0x1f

#define WLED3_SINK_REG_STR_MOD_SRC_BASE			0x63
#define  WLED3_SINK_REG_STR_MOD_SRC_MASK		0x01
#define  WLED3_SINK_REG_STR_MOD_SRC_INT			0x00
#define  WLED3_SINK_REG_STR_MOD_SRC_EXT			0x01

#define WLED3_SINK_REG_STR_CABC_BASE			0x66
#define  WLED3_SINK_REG_STR_CABC_MASK			BIT(7)
#define  WLED3_SINK_REG_STR_CABC_EN			BIT(7)

struct wled_config {
	u32 boost_i_limit;
	u32 ovp;
	u32 switch_freq;
	u32 num_strings;
	u32 string_i_limit;
	bool cs_out_en;
	bool ext_gen;
	bool cabc_en;
};

struct wled {
	const char *name;
	struct regmap *regmap;
	u16 addr;

	struct wled_config cfg;
};

static int wled_update_status(struct backlight_device *bl)
{
	struct wled *wled = bl_get_data(bl);
	u16 val = bl->props.brightness;
	u8 ctrl = 0;
	int rc;
	int i;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		val = 0;

	if (val != 0)
		ctrl = WLED3_CTRL_REG_MOD_EN_BIT;

	rc = regmap_update_bits(wled->regmap,
			wled->addr + WLED3_CTRL_REG_MOD_EN,
			WLED3_CTRL_REG_MOD_EN_MASK, ctrl);
	if (rc)
		return rc;

	for (i = 0; i < wled->cfg.num_strings; ++i) {
		u8 v[2] = { val & 0xff, (val >> 8) & 0xf };

		rc = regmap_bulk_write(wled->regmap,
				wled->addr + WLED3_CTRL_REG_VAL_BASE + 2 * i,
				v, 2);
		if (rc)
			return rc;
	}

	rc = regmap_update_bits(wled->regmap,
			wled->addr + WLED3_SINK_REG_SYNC,
			WLED3_SINK_REG_SYNC_MASK, WLED3_SINK_REG_SYNC_ALL);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
			wled->addr + WLED3_SINK_REG_SYNC,
			WLED3_SINK_REG_SYNC_MASK, WLED3_SINK_REG_SYNC_CLEAR);
	return rc;
}

static int wled_setup(struct wled *wled)
{
	int rc;
	int i;

	rc = regmap_update_bits(wled->regmap,
			wled->addr + WLED3_CTRL_REG_OVP,
			WLED3_CTRL_REG_OVP_MASK, wled->cfg.ovp);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
			wled->addr + WLED3_CTRL_REG_ILIMIT,
			WLED3_CTRL_REG_ILIMIT_MASK, wled->cfg.boost_i_limit);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
			wled->addr + WLED3_CTRL_REG_FREQ,
			WLED3_CTRL_REG_FREQ_MASK, wled->cfg.switch_freq);
	if (rc)
		return rc;

	if (wled->cfg.cs_out_en) {
		u8 all = (BIT(wled->cfg.num_strings) - 1)
				<< WLED3_SINK_REG_CURR_SINK_SHFT;

		rc = regmap_update_bits(wled->regmap,
				wled->addr + WLED3_SINK_REG_CURR_SINK,
				WLED3_SINK_REG_CURR_SINK_MASK, all);
		if (rc)
			return rc;
	}

	for (i = 0; i < wled->cfg.num_strings; ++i) {
		u16 addr = wled->addr + WLED3_SINK_REG_STR_OFFSET * i;

		rc = regmap_update_bits(wled->regmap,
				addr + WLED3_SINK_REG_STR_MOD_EN_BASE,
				WLED3_SINK_REG_STR_MOD_MASK,
				WLED3_SINK_REG_STR_MOD_EN);
		if (rc)
			return rc;

		if (wled->cfg.ext_gen) {
			rc = regmap_update_bits(wled->regmap,
					addr + WLED3_SINK_REG_STR_MOD_SRC_BASE,
					WLED3_SINK_REG_STR_MOD_SRC_MASK,
					WLED3_SINK_REG_STR_MOD_SRC_EXT);
			if (rc)
				return rc;
		}

		rc = regmap_update_bits(wled->regmap,
				addr + WLED3_SINK_REG_STR_FULL_SCALE_CURR,
				WLED3_SINK_REG_STR_FULL_SCALE_CURR_MASK,
				wled->cfg.string_i_limit);
		if (rc)
			return rc;

		rc = regmap_update_bits(wled->regmap,
				addr + WLED3_SINK_REG_STR_CABC_BASE,
				WLED3_SINK_REG_STR_CABC_MASK,
				wled->cfg.cabc_en ?
					WLED3_SINK_REG_STR_CABC_EN : 0);
		if (rc)
			return rc;
	}

	return 0;
}

static const struct wled_config wled3_config_defaults = {
	.boost_i_limit = 3,
	.string_i_limit = 20,
	.ovp = 2,
	.switch_freq = 5,
	.num_strings = 0,
	.cs_out_en = false,
	.ext_gen = false,
	.cabc_en = false,
};

struct wled_var_cfg {
	const u32 *values;
	u32 (*fn)(u32);
	int size;
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

static u32 wled3_values(const struct wled_var_cfg *cfg, u32 idx)
{
	if (idx >= cfg->size)
		return UINT_MAX;
	if (cfg->fn)
		return cfg->fn(idx);
	if (cfg->values)
		return cfg->values[idx];
	return idx;
}

static int wled_configure(struct wled *wled, struct device *dev)
{
	struct wled_config *cfg = &wled->cfg;
	u32 val;
	int rc;
	u32 c;
	int i;
	int j;

	const struct {
		const char *name;
		u32 *val_ptr;
		const struct wled_var_cfg *cfg;
	} u32_opts[] = {
		{
			"qcom,current-boost-limit",
			&cfg->boost_i_limit,
			.cfg = &wled3_boost_i_limit_cfg,
		},
		{
			"qcom,current-limit",
			&cfg->string_i_limit,
			.cfg = &wled3_string_i_limit_cfg,
		},
		{
			"qcom,ovp",
			&cfg->ovp,
			.cfg = &wled3_ovp_cfg,
		},
		{
			"qcom,switching-freq",
			&cfg->switch_freq,
			.cfg = &wled3_switch_freq_cfg,
		},
		{
			"qcom,num-strings",
			&cfg->num_strings,
			.cfg = &wled3_num_strings_cfg,
		},
	};
	const struct {
		const char *name;
		bool *val_ptr;
	} bool_opts[] = {
		{ "qcom,cs-out", &cfg->cs_out_en, },
		{ "qcom,ext-gen", &cfg->ext_gen, },
		{ "qcom,cabc", &cfg->cabc_en, },
	};

	rc = of_property_read_u32(dev->of_node, "reg", &val);
	if (rc || val > 0xffff) {
		dev_err(dev, "invalid IO resources\n");
		return rc ? rc : -EINVAL;
	}
	wled->addr = val;

	rc = of_property_read_string(dev->of_node, "label", &wled->name);
	if (rc)
		wled->name = devm_kasprintf(dev, GFP_KERNEL, "%pOFn", dev->of_node);

	*cfg = wled3_config_defaults;
	for (i = 0; i < ARRAY_SIZE(u32_opts); ++i) {
		rc = of_property_read_u32(dev->of_node, u32_opts[i].name, &val);
		if (rc == -EINVAL) {
			continue;
		} else if (rc) {
			dev_err(dev, "error reading '%s'\n", u32_opts[i].name);
			return rc;
		}

		c = UINT_MAX;
		for (j = 0; c != val; j++) {
			c = wled3_values(u32_opts[i].cfg, j);
			if (c == UINT_MAX) {
				dev_err(dev, "invalid value for '%s'\n",
					u32_opts[i].name);
				return -EINVAL;
			}
		}

		dev_dbg(dev, "'%s' = %u\n", u32_opts[i].name, c);
		*u32_opts[i].val_ptr = j;
	}

	for (i = 0; i < ARRAY_SIZE(bool_opts); ++i) {
		if (of_property_read_bool(dev->of_node, bool_opts[i].name))
			*bool_opts[i].val_ptr = true;
	}

	cfg->num_strings = cfg->num_strings + 1;

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

	rc = wled_configure(wled, &pdev->dev);
	if (rc)
		return rc;

	rc = wled_setup(wled);
	if (rc)
		return rc;

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
	{ .compatible = "qcom,pm8941-wled" },
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
