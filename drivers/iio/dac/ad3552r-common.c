// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2010-2024 Analog Devices Inc.
// Copyright (c) 2024 Baylibre, SAS

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include "ad3552r.h"

static const s32 ad3552r_ch_ranges[AD3552R_MAX_RANGES][2] = {
	[AD3552R_CH_OUTPUT_RANGE_0__2P5V]	= { 0, 2500 },
	[AD3552R_CH_OUTPUT_RANGE_0__5V]		= { 0, 5000 },
	[AD3552R_CH_OUTPUT_RANGE_0__10V]	= { 0, 10000 },
	[AD3552R_CH_OUTPUT_RANGE_NEG_5__5V]	= { -5000, 5000 },
	[AD3552R_CH_OUTPUT_RANGE_NEG_10__10V]	= { -10000, 10000 }
};

static const s32 ad3542r_ch_ranges[AD3542R_MAX_RANGES][2] = {
	[AD3542R_CH_OUTPUT_RANGE_0__2P5V]	= { 0, 2500 },
	[AD3542R_CH_OUTPUT_RANGE_0__5V]		= { 0, 5000 },
	[AD3542R_CH_OUTPUT_RANGE_0__10V]	= { 0, 10000 },
	[AD3542R_CH_OUTPUT_RANGE_NEG_5__5V]	= { -5000, 5000 },
	[AD3542R_CH_OUTPUT_RANGE_NEG_2P5__7P5V]	= { -2500, 7500 }
};

/* Gain * AD3552R_GAIN_SCALE */
static const s32 gains_scaling_table[] = {
	[AD3552R_CH_GAIN_SCALING_1]		= 1000,
	[AD3552R_CH_GAIN_SCALING_0_5]		= 500,
	[AD3552R_CH_GAIN_SCALING_0_25]		= 250,
	[AD3552R_CH_GAIN_SCALING_0_125]		= 125
};

const struct ad3552r_model_data ad3541r_model_data = {
	.model_name = "ad3541r",
	.chip_id = AD3541R_ID,
	.num_hw_channels = 1,
	.ranges_table = ad3542r_ch_ranges,
	.num_ranges = ARRAY_SIZE(ad3542r_ch_ranges),
	.requires_output_range = true,
	.num_spi_data_lanes = 2,
};
EXPORT_SYMBOL_NS_GPL(ad3541r_model_data, "IIO_AD3552R");

const struct ad3552r_model_data ad3542r_model_data = {
	.model_name = "ad3542r",
	.chip_id = AD3542R_ID,
	.num_hw_channels = 2,
	.ranges_table = ad3542r_ch_ranges,
	.num_ranges = ARRAY_SIZE(ad3542r_ch_ranges),
	.requires_output_range = true,
	.num_spi_data_lanes = 2,
};
EXPORT_SYMBOL_NS_GPL(ad3542r_model_data, "IIO_AD3552R");

const struct ad3552r_model_data ad3551r_model_data = {
	.model_name = "ad3551r",
	.chip_id = AD3551R_ID,
	.num_hw_channels = 1,
	.ranges_table = ad3552r_ch_ranges,
	.num_ranges = ARRAY_SIZE(ad3552r_ch_ranges),
	.requires_output_range = false,
	.num_spi_data_lanes = 4,
};
EXPORT_SYMBOL_NS_GPL(ad3551r_model_data, "IIO_AD3552R");

const struct ad3552r_model_data ad3552r_model_data = {
	.model_name = "ad3552r",
	.chip_id = AD3552R_ID,
	.num_hw_channels = 2,
	.ranges_table = ad3552r_ch_ranges,
	.num_ranges = ARRAY_SIZE(ad3552r_ch_ranges),
	.requires_output_range = false,
	.num_spi_data_lanes = 4,
};
EXPORT_SYMBOL_NS_GPL(ad3552r_model_data, "IIO_AD3552R");

u16 ad3552r_calc_custom_gain(u8 p, u8 n, s16 goffs)
{
	return FIELD_PREP(AD3552R_MASK_CH_RANGE_OVERRIDE, 1) |
	       FIELD_PREP(AD3552R_MASK_CH_GAIN_SCALING_P, p) |
	       FIELD_PREP(AD3552R_MASK_CH_GAIN_SCALING_N, n) |
	       FIELD_PREP(AD3552R_MASK_CH_OFFSET_BIT_8, abs(goffs)) |
	       FIELD_PREP(AD3552R_MASK_CH_OFFSET_POLARITY, goffs < 0);
}
EXPORT_SYMBOL_NS_GPL(ad3552r_calc_custom_gain, "IIO_AD3552R");

static void ad3552r_get_custom_range(struct ad3552r_ch_data *ch_data,
				     s32 *v_min, s32 *v_max)
{
	s64 vref, tmp, common, offset, gn, gp;
	/*
	 * From datasheet formula (In Volts):
	 *	Vmin = 2.5 + [(GainN + Offset / 1024) * 2.5 * Rfb * 1.03]
	 *	Vmax = 2.5 - [(GainP + Offset / 1024) * 2.5 * Rfb * 1.03]
	 * Calculus are converted to milivolts
	 */
	vref = 2500;
	/* 2.5 * 1.03 * 1000 (To mV) */
	common = 2575 * ch_data->rfb;
	offset = ch_data->gain_offset;

	gn = gains_scaling_table[ch_data->n];
	tmp = (1024 * gn + AD3552R_GAIN_SCALE * offset) * common;
	tmp = div_s64(tmp, 1024  * AD3552R_GAIN_SCALE);
	*v_max = vref + tmp;

	gp = gains_scaling_table[ch_data->p];
	tmp = (1024 * gp - AD3552R_GAIN_SCALE * offset) * common;
	tmp = div_s64(tmp, 1024 * AD3552R_GAIN_SCALE);
	*v_min = vref - tmp;
}

void ad3552r_calc_gain_and_offset(struct ad3552r_ch_data *ch_data,
				  const struct ad3552r_model_data *model_data)
{
	s32 idx, v_max, v_min, span, rem;
	s64 tmp;

	if (ch_data->range_override) {
		ad3552r_get_custom_range(ch_data, &v_min, &v_max);
	} else {
		/* Normal range */
		idx = ch_data->range;
		v_min = model_data->ranges_table[idx][0];
		v_max = model_data->ranges_table[idx][1];
	}

	/*
	 * From datasheet formula:
	 *	Vout = Span * (D / 65536) + Vmin
	 * Converted to scale and offset:
	 *	Scale = Span / 65536
	 *	Offset = 65536 * Vmin / Span
	 *
	 * Reminders are in micros in order to be printed as
	 * IIO_VAL_INT_PLUS_MICRO
	 */
	span = v_max - v_min;
	ch_data->scale_int = div_s64_rem(span, 65536, &rem);
	/* Do operations in microvolts */
	ch_data->scale_dec = DIV_ROUND_CLOSEST((s64)rem * 1000000, 65536);

	ch_data->offset_int = div_s64_rem(v_min * 65536, span, &rem);
	tmp = (s64)rem * 1000000;
	ch_data->offset_dec = div_s64(tmp, span);
}
EXPORT_SYMBOL_NS_GPL(ad3552r_calc_gain_and_offset, "IIO_AD3552R");

int ad3552r_get_ref_voltage(struct device *dev, u32 *val)
{
	int voltage;
	int delta = 100000;

	voltage = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (voltage < 0 && voltage != -ENODEV)
		return dev_err_probe(dev, voltage,
				     "Error getting vref voltage\n");

	if (voltage == -ENODEV) {
		if (device_property_read_bool(dev, "adi,vref-out-en"))
			*val = AD3552R_INTERNAL_VREF_PIN_2P5V;
		else
			*val = AD3552R_INTERNAL_VREF_PIN_FLOATING;

		return 0;
	}

	if (voltage > 2500000 + delta || voltage < 2500000 - delta) {
		dev_warn(dev, "vref-supply must be 2.5V");
		return -EINVAL;
	}

	*val = AD3552R_EXTERNAL_VREF_PIN_INPUT;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ad3552r_get_ref_voltage, "IIO_AD3552R");

int ad3552r_get_drive_strength(struct device *dev, u32 *val)
{
	int err;
	u32 drive_strength;

	err = device_property_read_u32(dev, "adi,sdo-drive-strength",
				       &drive_strength);
	if (err)
		return err;

	if (drive_strength > 3) {
		dev_err_probe(dev, -EINVAL,
			      "adi,sdo-drive-strength must be less than 4\n");
		return -EINVAL;
	}

	*val = drive_strength;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ad3552r_get_drive_strength, "IIO_AD3552R");

int ad3552r_get_custom_gain(struct device *dev, struct fwnode_handle *child,
			    u8 *gs_p, u8 *gs_n, u16 *rfb, s16 *goffs)
{
	int err;
	u32 val;
	struct fwnode_handle *gain_child __free(fwnode_handle) =
		fwnode_get_named_child_node(child,
					    "custom-output-range-config");

	if (!gain_child)
		return dev_err_probe(dev, -EINVAL,
				     "custom-output-range-config mandatory\n");

	err = fwnode_property_read_u32(gain_child, "adi,gain-scaling-p", &val);
	if (err)
		return dev_err_probe(dev, err,
				     "adi,gain-scaling-p mandatory\n");
	*gs_p = val;

	err = fwnode_property_read_u32(gain_child, "adi,gain-scaling-n", &val);
	if (err)
		return dev_err_probe(dev, err,
				     "adi,gain-scaling-n property mandatory\n");
	*gs_n = val;

	err = fwnode_property_read_u32(gain_child, "adi,rfb-ohms", &val);
	if (err)
		return dev_err_probe(dev, err,
				     "adi,rfb-ohms mandatory\n");
	*rfb = val;

	err = fwnode_property_read_u32(gain_child, "adi,gain-offset", &val);
	if (err)
		return dev_err_probe(dev, err,
				     "adi,gain-offset mandatory\n");
	*goffs = val;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ad3552r_get_custom_gain, "IIO_AD3552R");

static int ad3552r_find_range(const struct ad3552r_model_data *model_info,
			      s32 *vals)
{
	int i;

	for (i = 0; i < model_info->num_ranges; i++)
		if (vals[0] == model_info->ranges_table[i][0] * 1000 &&
		    vals[1] == model_info->ranges_table[i][1] * 1000)
			return i;

	return -EINVAL;
}

int ad3552r_get_output_range(struct device *dev,
			     const struct ad3552r_model_data *model_info,
			     struct fwnode_handle *child, u32 *val)
{
	int ret;
	s32 vals[2];

	/* This property is optional, so returning -ENOENT if missing */
	if (!fwnode_property_present(child, "adi,output-range-microvolt"))
		return -ENOENT;

	ret = fwnode_property_read_u32_array(child,
					     "adi,output-range-microvolt",
					     vals, 2);
	if (ret)
		return dev_err_probe(dev, ret,
				"invalid adi,output-range-microvolt\n");

	ret = ad3552r_find_range(model_info, vals);
	if (ret < 0)
		return dev_err_probe(dev, ret,
			"invalid adi,output-range-microvolt value\n");

	*val = ret;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ad3552r_get_output_range, "IIO_AD3552R");

MODULE_DESCRIPTION("ad3552r common functions");
MODULE_LICENSE("GPL");
