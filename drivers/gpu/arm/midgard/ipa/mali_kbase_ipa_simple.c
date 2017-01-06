/*
 *
 * (C) COPYRIGHT 2016-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/thermal.h>
#ifdef CONFIG_DEVFREQ_THERMAL
#include <linux/devfreq_cooling.h>
#endif
#include <linux/of.h>
#include <linux/math64.h>

#include "mali_kbase.h"
#include "mali_kbase_defs.h"

/*
 * This model is primarily designed for the Juno platform. It may not be
 * suitable for other platforms. The additional resources in this model
 * should preferably be minimal, as this model is rarely used when a dynamic
 * model is available.
 */

/**
 * struct kbase_ipa_model_simple_data - IPA context per device
 * @dynamic_coefficient: dynamic coefficient of the model
 * @static_coefficient:  static coefficient of the model
 * @ts:                  Thermal scaling coefficients of the model
 * @tz_name:             Thermal zone name
 * @gpu_tz:              thermal zone device
 */

struct kbase_ipa_model_simple_data {
	u32 dynamic_coefficient;
	u32 static_coefficient;
	s32 ts[4];
	char tz_name[16];
	struct thermal_zone_device *gpu_tz;
};
#define FALLBACK_STATIC_TEMPERATURE 55000

/**
 * calculate_temp_scaling_factor() - Calculate temperature scaling coefficient
 * @ts:		Signed coefficients, in order t^0 to t^3, with units Deg^-N
 * @t:		Temperature, in mDeg C. Range: -2^17 < t < 2^17
 *
 * Scale the temperature according to a cubic polynomial whose coefficients are
 * provided in the device tree. The result is used to scale the static power
 * coefficient, where 1000000 means no change.
 *
 * Return: Temperature scaling factor. Approx range 0 < ret < 10,000,000.
 */
static u32 calculate_temp_scaling_factor(s32 ts[4], s64 t)
{
	/* Range: -2^24 < t2 < 2^24 m(Deg^2) */
	u32 remainder;
	// static inline s64 div_s64_rem(s64 dividend, s32 divisor, s32 *remainder)
	const s64 t2 = div_s64_rem((t * t), 1000, &remainder);

	/* Range: -2^31 < t3 < 2^31 m(Deg^3) */
	const s64 t3 = div_s64_rem((t * t2), 1000, &remainder);

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
	s64 res_unclamped = div_s64_rem(res_big, 1000, &remainder);

	/* Clamp to range of 0x to 10x the static power */
	return clamp(res_unclamped, (s64) 0, (s64) 10000000);
}

static int model_static_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
	unsigned long temp;
#else
	int temp;
#endif
	u32 temp_scaling_factor;
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *) model->model_data;
	struct thermal_zone_device *gpu_tz = model_data->gpu_tz;

	if (gpu_tz) {
		int ret;

		ret = gpu_tz->ops->get_temp(gpu_tz, &temp);
		if (ret) {
			pr_warn_ratelimited("Error reading temperature for gpu thermal zone: %d\n",
					ret);
			temp = FALLBACK_STATIC_TEMPERATURE;
		}
	} else {
		temp = FALLBACK_STATIC_TEMPERATURE;
	}

	temp_scaling_factor = calculate_temp_scaling_factor(model_data->ts,
							    temp);

	*coeffp = model_data->static_coefficient * temp_scaling_factor;
	*coeffp /= 1000000;

	return 0;
}

static int model_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp,
			       u32 current_freq)
{
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *) model->model_data;

	*coeffp = model_data->dynamic_coefficient;

	return 0;
}

static int add_params(struct kbase_ipa_model *model)
{
	int err = 0;
	struct kbase_ipa_model_simple_data *model_data =
			(struct kbase_ipa_model_simple_data *)model->model_data;

	err = kbase_ipa_model_add_param_s32(model, "static-coefficient",
					    &model_data->static_coefficient,
					    1, true);
	if (err)
		goto end;

	err = kbase_ipa_model_add_param_s32(model, "dynamic-coefficient",
					    &model_data->dynamic_coefficient,
					    1, true);
	if (err)
		goto end;

	err = kbase_ipa_model_add_param_s32(model, "ts",
					    model_data->ts, 4, true);
	if (err)
		goto end;

	err = kbase_ipa_model_add_param_string(model, "thermal-zone",
					       model_data->tz_name,
					       sizeof(model_data->tz_name), true);

end:
	return err;
}

static int kbase_simple_power_model_init(struct kbase_ipa_model *model)
{
	int err;
	struct kbase_ipa_model_simple_data *model_data;

	model_data = kzalloc(sizeof(struct kbase_ipa_model_simple_data),
			     GFP_KERNEL);
	if (!model_data)
		return -ENOMEM;

	model->model_data = (void *) model_data;

	err = add_params(model);

	return err;
}

static int kbase_simple_power_model_recalculate(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_simple_data *model_data =
			(struct kbase_ipa_model_simple_data *)model->model_data;

	if (!strnlen(model_data->tz_name, sizeof(model_data->tz_name))) {
		model_data->gpu_tz = NULL;
	} else {
		model_data->gpu_tz = thermal_zone_get_zone_by_name(model_data->tz_name);

		if (IS_ERR(model_data->gpu_tz)) {
			pr_warn_ratelimited("Error %ld getting thermal zone \'%s\', not yet ready?\n",
					    PTR_ERR(model_data->gpu_tz),
					    model_data->tz_name);
			model_data->gpu_tz = NULL;
			return -EPROBE_DEFER;
		}
	}

	return 0;
}

static void kbase_simple_power_model_term(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_simple_data *model_data =
			(struct kbase_ipa_model_simple_data *)model->model_data;

	kfree(model_data);
}

struct kbase_ipa_model_ops kbase_simple_ipa_model_ops = {
		.name = "mali-simple-power-model",
		.init = &kbase_simple_power_model_init,
		.recalculate = &kbase_simple_power_model_recalculate,
		.term = &kbase_simple_power_model_term,
		.get_dynamic_coeff = &model_dynamic_coeff,
		.get_static_coeff = &model_static_coeff,
		.do_utilization_scaling_in_framework = true,
};
