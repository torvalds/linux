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
#include <linux/delay.h>
#include <linux/kthread.h>

#include "mali_kbase.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_ipa_simple.h"
#include "mali_kbase_ipa_debugfs.h"

#if MALI_UNIT_TEST

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
static unsigned long dummy_temp;

static int kbase_simple_power_model_get_dummy_temp(
	struct thermal_zone_device *tz,
	unsigned long *temp)
{
	*temp = ACCESS_ONCE(dummy_temp);
	return 0;
}

#else
static int dummy_temp;

static int kbase_simple_power_model_get_dummy_temp(
	struct thermal_zone_device *tz,
	int *dummy_temp)
{
	*temp = ACCESS_ONCE(dummy_temp);
	return 0;
}
#endif

/* Intercept calls to the kernel function using a macro */
#ifdef thermal_zone_get_temp
#undef thermal_zone_get_temp
#endif
#define thermal_zone_get_temp(tz, temp) \
	kbase_simple_power_model_get_dummy_temp(tz, temp)

void kbase_simple_power_model_set_dummy_temp(int temp)
{
	ACCESS_ONCE(dummy_temp) = temp;
}
KBASE_EXPORT_TEST_API(kbase_simple_power_model_set_dummy_temp);

#endif /* MALI_UNIT_TEST */

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
 * @poll_temperature_thread: Handle for temperature polling thread
 * @current_temperature: Most recent value of polled temperature
 * @temperature_poll_interval_ms: How often temperature should be checked, in ms
 */

struct kbase_ipa_model_simple_data {
	u32 dynamic_coefficient;
	u32 static_coefficient;
	s32 ts[4];
	char tz_name[16];
	struct thermal_zone_device *gpu_tz;
	struct task_struct *poll_temperature_thread;
	int current_temperature;
	int temperature_poll_interval_ms;
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
 * Return: Temperature scaling factor. Range 0 <= ret <= 10,000,000.
 */
static u32 calculate_temp_scaling_factor(s32 ts[4], s64 t)
{
	/* Range: -2^24 < t2 < 2^24 m(Deg^2) */
	const s64 t2 = (t * t) / 1000;

	/* Range: -2^31 < t3 < 2^31 m(Deg^3) */
	const s64 t3 = (t * t2) / 1000;

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
	s64 res_unclamped = res_big / 1000;

	/* Clamp to range of 0x to 10x the static power */
	return clamp(res_unclamped, (s64) 0, (s64) 10000000);
}

/* We can't call thermal_zone_get_temp() directly in model_static_coeff(),
 * because we don't know if tz->lock is held in the same thread. So poll it in
 * a separate thread to get around this. */
static int poll_temperature(void *data)
{
	struct kbase_ipa_model_simple_data *model_data =
			(struct kbase_ipa_model_simple_data *) data;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
	unsigned long temp;
#else
	int temp;
#endif

	while (!kthread_should_stop()) {
		struct thermal_zone_device *tz = ACCESS_ONCE(model_data->gpu_tz);

		if (tz) {
			int ret;

			ret = thermal_zone_get_temp(tz, &temp);
			if (ret) {
				pr_warn_ratelimited("Error reading temperature for gpu thermal zone: %d\n",
						    ret);
				temp = FALLBACK_STATIC_TEMPERATURE;
			}
		} else {
			temp = FALLBACK_STATIC_TEMPERATURE;
		}

		ACCESS_ONCE(model_data->current_temperature) = temp;

		msleep_interruptible(ACCESS_ONCE(model_data->temperature_poll_interval_ms));
	}

	return 0;
}

static int model_static_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
	u32 temp_scaling_factor;
	struct kbase_ipa_model_simple_data *model_data =
		(struct kbase_ipa_model_simple_data *) model->model_data;
	u64 coeff_big;
	int temp;

	temp = ACCESS_ONCE(model_data->current_temperature);

	/* Range: 0 <= temp_scaling_factor < 2^24 */
	temp_scaling_factor = calculate_temp_scaling_factor(model_data->ts,
							    temp);

	/*
	 * Range: 0 <= coeff_big < 2^52 to avoid overflowing *coeffp. This
	 * means static_coefficient must be in range
	 * 0 <= static_coefficient < 2^28.
	 */
	coeff_big = (u64) model_data->static_coefficient * (u64) temp_scaling_factor;
	*coeffp = coeff_big / 1000000;

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
	if (err)
		goto end;

	model_data->temperature_poll_interval_ms = 200;
	err = kbase_ipa_model_add_param_s32(model, "temp-poll-interval-ms",
					    &model_data->temperature_poll_interval_ms,
					    1, false);

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

	model_data->current_temperature = FALLBACK_STATIC_TEMPERATURE;
	model_data->poll_temperature_thread = kthread_run(poll_temperature,
							  (void *) model_data,
							  "mali-simple-power-model-temp-poll");
	if (IS_ERR(model_data->poll_temperature_thread)) {
		kfree(model_data);
		return PTR_ERR(model_data->poll_temperature_thread);
	}

	err = add_params(model);
	if (err) {
		kbase_ipa_model_param_free_all(model);
		kthread_stop(model_data->poll_temperature_thread);
		kfree(model_data);
	}

	return err;
}

static int kbase_simple_power_model_recalculate(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_simple_data *model_data =
			(struct kbase_ipa_model_simple_data *)model->model_data;
	struct thermal_zone_device *tz;

	if (!strnlen(model_data->tz_name, sizeof(model_data->tz_name))) {
		tz = NULL;
	} else {
		tz = thermal_zone_get_zone_by_name(model_data->tz_name);

		if (IS_ERR_OR_NULL(tz)) {
			pr_warn_ratelimited("Error %ld getting thermal zone \'%s\', not yet ready?\n",
					    PTR_ERR(tz), model_data->tz_name);
			tz = NULL;
			return -EPROBE_DEFER;
		}
	}

	ACCESS_ONCE(model_data->gpu_tz) = tz;

	return 0;
}

static void kbase_simple_power_model_term(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_simple_data *model_data =
			(struct kbase_ipa_model_simple_data *)model->model_data;

	kthread_stop(model_data->poll_temperature_thread);

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
