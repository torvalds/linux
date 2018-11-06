// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/thermal.h>
#include <soc/rockchip/rockchip_ipa.h>

#define FALLBACK_STATIC_TEMPERATURE 55000

int rockchip_ipa_power_model_init(struct device *dev,
				  struct ipa_power_model_data **data)
{
	struct device_node *model_node;
	struct ipa_power_model_data *model_data;
	const char *tz_name;
	int ret;

	model_data = devm_kzalloc(dev, sizeof(*model_data), GFP_KERNEL);
	if (!model_data)
		return -ENOMEM;

	model_node = of_find_compatible_node(dev->of_node,
					     NULL, "simple-power-model");
	if (!model_node) {
		dev_err(dev, "failed to find power_model node\n");
		ret = -ENODEV;
		goto err;
	}

	if (of_property_read_string(model_node, "thermal-zone", &tz_name)) {
		dev_err(dev, "ts in power_model not available\n");
		ret = -EINVAL;
		goto err;
	}
	model_data->tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR_OR_NULL(model_data->tz)) {
		dev_err(dev, "failed to get thermal zone\n");
		model_data->tz = NULL;
		ret = -EPROBE_DEFER;
		goto err;
	}
	if (of_property_read_u32(model_node, "static-coefficient",
				 &model_data->static_coefficient)) {
		dev_err(dev, "static-coefficient not available\n");
		ret = -EINVAL;
		goto err;
	}
	/* cpu power model node doesn't contain dynamic-coefficient */
	of_property_read_u32(model_node, "dynamic-coefficient",
			     &model_data->dynamic_coefficient);
	if (of_property_read_u32_array
	    (model_node, "ts", (u32 *)model_data->ts, 4)) {
		dev_err(dev, "ts in power_model not available\n");
		ret = -EINVAL;
		goto err;
	}
	*data = model_data;

	return 0;
err:
	devm_kfree(dev, model_data);

	return ret;
}
EXPORT_SYMBOL(rockchip_ipa_power_model_init);

/**
 * calculate_temp_scaling_factor() - Calculate temperature scaling coefficient
 * @ts:		Signed coefficients, in order t^0 to t^3, with units Deg^-N
 * @t:		Temperature, in mDeg C. Range: -2^17 < t < 2^17
 *
 * Scale the temperature according to a cubic polynomial whose coefficients are
 * provided in the device tree. The result is used to scale the static power
 * coefficient, where 1000000 means no change.
 *
 * Return: Temperature scaling factor. Range 0 <= ret <= 10,000,000.
 */
static u32 calculate_temp_scaling_factor(s32 ts[4], s64 t)
{
	/* Range: -2^24 < t2 < 2^24 m(Deg^2) */
	const s64 t2 = div_s64((t * t), 1000);

	/* Range: -2^31 < t3 < 2^31 m(Deg^3) */
	const s64 t3 = div_s64((t * t2), 1000);

	/*
	 * Sum the parts. t^[1-3] are in m(Deg^N), but the coefficients are in
	 * Deg^-N, so we need to multiply the last coefficient by 1000.
	 * Range: -2^63 < res_big < 2^63
	 */
	const s64 res_big = ts[3] * t3    /* +/- 2^62 */
			  + ts[2] * t2    /* +/- 2^55 */
			  + ts[1] * t     /* +/- 2^48 */
			  + ts[0] * 1000; /* +/- 2^41 */

	/* Range: -2^60 < res_unclamped < 2^60 */
	s64 res_unclamped = div_s64(res_big, 1000);

	/* Clamp to range of 0x to 10x the static power */
	return clamp(res_unclamped, (s64)0, (s64)10000000);
}

/**
 * scale_static_power() - Scale a static power coefficient to an OPP
 * @c:		Static model coefficient, in uW/V^3. Should be in range
 *		0 < c < 2^32 to prevent overflow.
 * @voltage:	Voltage, in mV. Range: 2^9 < voltage < 2^13 (~0.5V to ~8V)
 *
 * Return: Power consumption, in mW. Range: 0 < p < 2^13 (0W to ~8W)
 */
static u32 scale_static_power(const u32 c, const u32 voltage)
{
	/* Range: 2^8 < v2 < 2^16 m(V^2) */
	const u32 v2 = (voltage * voltage) / 1000;

	/* Range: 2^17 < v3_big < 2^29 m(V^2) mV */
	const u32 v3_big = v2 * voltage;

	/* Range: 2^7 < v3 < 2^19 m(V^3) */
	const u32 v3 = v3_big / 1000;

	/*
	 * Range (working backwards from next line): 0 < v3c_big < 2^33 nW.
	 * The result should be < 2^52 to avoid overflowing the return value.
	 */
	const u64 v3c_big = (u64)c * (u64)v3;

	/* Range: 0 < v3c_big / 1000000 < 2^13 mW */
	return div_u64(v3c_big, 1000000);
}

unsigned long
rockchip_ipa_get_static_power(struct ipa_power_model_data *data,
			      unsigned long voltage)
{
	u32 temp_scaling_factor, coeffp;
	u64 coeff_big;
	int temp;
	int ret;

	ret = data->tz->ops->get_temp(data->tz, &temp);
	if (ret) {
		pr_err("%s:failed to read %s temp\n", __func__, data->tz->type);
		temp = FALLBACK_STATIC_TEMPERATURE;
	}

	/* Range: 0 <= temp_scaling_factor < 2^24 */
	temp_scaling_factor = calculate_temp_scaling_factor(data->ts, temp);
	/*
	 * Range: 0 <= coeff_big < 2^52 to avoid overflowing *coeffp. This
	 * means static_coefficient must be in range
	 * 0 <= static_coefficient < 2^28.
	 */
	coeff_big = (u64)data->static_coefficient * (u64)temp_scaling_factor;
	coeffp = div_u64(coeff_big, 1000000);

	return scale_static_power(coeffp, (u32)voltage);
}
EXPORT_SYMBOL(rockchip_ipa_get_static_power);
