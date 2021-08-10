// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include "mali_kbase_ipa_counter_common_csf.h"
#include "ipa/mali_kbase_ipa_debugfs.h"

#define DEFAULT_SCALING_FACTOR 5

/* If the value of GPU_ACTIVE is below this, use the simple model
 * instead, to avoid extrapolating small amounts of counter data across
 * large sample periods.
 */
#define DEFAULT_MIN_SAMPLE_CYCLES 10000

/* Typical value for the sampling interval is expected to be less than 100ms,
 * So 5 seconds is a reasonable upper limit for the time gap between the
 * 2 samples.
 */
#define MAX_SAMPLE_INTERVAL_MS ((s64)5000)

/* Maximum increment that is expected for a counter value during a sampling
 * interval is derived assuming
 * - max sampling interval of 1 second.
 * - max GPU frequency of 2 GHz.
 * - max number of cores as 32.
 * - max increment of 4 in per core counter value at every clock cycle.
 *
 * So max increment = 2 * 10^9 * 32 * 4 = ~2^38.
 * If a counter increases by an amount greater than this value, then an error
 * will be returned and the simple power model will be used.
 */
#define MAX_COUNTER_INCREMENT (((u64)1 << 38) - 1)

static inline s64 kbase_ipa_add_saturate(s64 a, s64 b)
{
	s64 rtn;

	if (a > 0 && (S64_MAX - a) < b)
		rtn = S64_MAX;
	else if (a < 0 && (S64_MIN - a) > b)
		rtn = S64_MIN;
	else
		rtn = a + b;

	return rtn;
}

static s64 kbase_ipa_group_energy(s32 coeff, u64 counter_value)
{
	/* Range: 0 < counter_value < 2^38 */

	/* Range: -2^59 < ret < 2^59 (as -2^21 < coeff < 2^21) */
	return counter_value * (s64)coeff;
}

/**
 * kbase_ipa_attach_ipa_control() - register with kbase_ipa_control
 * @model_data: Pointer to counter model data
 *
 * Register IPA counter model as a client of kbase_ipa_control, which
 * provides an interface to retreive the accumulated value of hardware
 * counters to calculate energy consumption.
 *
 * Return: 0 on success, or an error code.
 */
static int
kbase_ipa_attach_ipa_control(struct kbase_ipa_counter_model_data *model_data)
{
	struct kbase_device *kbdev = model_data->kbdev;
	struct kbase_ipa_control_perf_counter *perf_counters;
	u32 cnt_idx = 0;
	int err;
	size_t i;

	/* Value for GPU_ACTIVE counter also needs to be queried. It is required
	 * for the normalization of top-level and shader core counters.
	 */
	model_data->num_counters = 1 + model_data->num_top_level_cntrs +
				   model_data->num_shader_cores_cntrs;

	perf_counters = kcalloc(model_data->num_counters,
				sizeof(*perf_counters), GFP_KERNEL);

	if (!perf_counters) {
		dev_err(kbdev->dev,
			"Failed to allocate memory for perf_counters array");
		return -ENOMEM;
	}

	/* Fill in the description for GPU_ACTIVE counter which is always
	 * needed, as mentioned above, regardless of the energy model used
	 * by the CSF GPUs.
	 */
	perf_counters[cnt_idx].type = KBASE_IPA_CORE_TYPE_CSHW;
	perf_counters[cnt_idx].idx = GPU_ACTIVE_CNT_IDX;
	perf_counters[cnt_idx].gpu_norm = false;
	perf_counters[cnt_idx].scaling_factor = 1;
	cnt_idx++;

	for (i = 0; i < model_data->num_top_level_cntrs; ++i) {
		const struct kbase_ipa_counter *counter =
			&model_data->top_level_cntrs_def[i];

		perf_counters[cnt_idx].type = counter->counter_block_type;
		perf_counters[cnt_idx].idx = counter->counter_block_offset;
		perf_counters[cnt_idx].gpu_norm = false;
		perf_counters[cnt_idx].scaling_factor = 1;
		cnt_idx++;
	}

	for (i = 0; i < model_data->num_shader_cores_cntrs; ++i) {
		const struct kbase_ipa_counter *counter =
			&model_data->shader_cores_cntrs_def[i];

		perf_counters[cnt_idx].type = counter->counter_block_type;
		perf_counters[cnt_idx].idx = counter->counter_block_offset;
		perf_counters[cnt_idx].gpu_norm = false;
		perf_counters[cnt_idx].scaling_factor = 1;
		cnt_idx++;
	}

	err = kbase_ipa_control_register(kbdev, perf_counters,
					 model_data->num_counters,
					 &model_data->ipa_control_client);
	if (err)
		dev_err(kbdev->dev,
			"Failed to register IPA with kbase_ipa_control");

	kfree(perf_counters);
	return err;
}

/**
 * kbase_ipa_detach_ipa_control() - De-register from kbase_ipa_control.
 * @model_data: Pointer to counter model data
 */
static void
kbase_ipa_detach_ipa_control(struct kbase_ipa_counter_model_data *model_data)
{
	if (model_data->ipa_control_client) {
		kbase_ipa_control_unregister(model_data->kbdev,
					     model_data->ipa_control_client);
		model_data->ipa_control_client = NULL;
	}
}

static int calculate_coeff(struct kbase_ipa_counter_model_data *model_data,
			   const struct kbase_ipa_counter *const cnt_defs,
			   size_t num_counters, s32 *counter_coeffs,
			   u64 *counter_values, u32 active_cycles, u32 *coeffp)
{
	u64 coeff = 0, coeff_mul = 0;
	s64 total_energy = 0;
	size_t i;

	/* Range for the 'counter_value' is [0, 2^38)
	 * Range for the 'coeff' is [-2^21, 2^21]
	 * So range for the 'group_energy' is [-2^59, 2^59) and range for the
	 * 'total_energy' is +/- 2^59 * number of IPA groups (~16), i.e.
	 * [-2^63, 2^63).
	 */
	for (i = 0; i < num_counters; i++) {
		s32 coeff = counter_coeffs[i];
		u64 counter_value = counter_values[i];
		s64 group_energy = kbase_ipa_group_energy(coeff, counter_value);

		if (counter_value > MAX_COUNTER_INCREMENT) {
			dev_warn(model_data->kbdev->dev,
				 "Increment in counter %s more than expected",
				 cnt_defs[i].name);
			return -ERANGE;
		}

		total_energy =
			kbase_ipa_add_saturate(total_energy, group_energy);
	}

	/* Range: 0 <= coeff < 2^63 */
	if (total_energy >= 0)
		coeff = total_energy;
	else
		dev_dbg(model_data->kbdev->dev,
			"Energy value came negative as %lld", total_energy);

	/* Range: 0 <= coeff < 2^63 (because active_cycles >= 1). However, this
	 * can be constrained further: the value of counters that are being
	 * used for dynamic power estimation can only increment by about 128
	 * maximum per clock cycle. This is because max number of shader
	 * cores is expected to be 32 (max number of L2 slices is expected to
	 * be 8) and some counters (per shader core) like SC_BEATS_RD_TEX_EXT &
	 * SC_EXEC_STARVE_ARITH can increment by 4 every clock cycle.
	 * Each "beat" is defined as 128 bits and each shader core can
	 * (currently) do 512 bits read and 512 bits write to/from the L2
	 * cache per cycle, so the SC_BEATS_RD_TEX_EXT counter can increment
	 * [0, 4] per shader core per cycle.
	 * We can thus write the range of 'coeff' in terms of active_cycles:
	 *
	 * coeff = SUM(coeffN * counterN * num_cores_for_counterN)
	 * coeff <= SUM(coeffN * counterN) * max_cores
	 * coeff <= num_IPA_groups * max_coeff * max_counter * max_cores
	 *       (substitute max_counter = 2^2 * active_cycles)
	 * coeff <= num_IPA_groups * max_coeff * 2^2 * active_cycles * max_cores
	 * coeff <=    2^4         *    2^21   * 2^2 * active_cycles * 2^5
	 * coeff <= 2^32 * active_cycles
	 *
	 * So after the division: 0 <= coeff <= 2^32
	 */
	coeff = div_u64(coeff, active_cycles);

	/* Not all models were derived at the same reference voltage. Voltage
	 * scaling is done by multiplying by V^2, so we need to *divide* by
	 * Vref^2 here.
	 * Range: 0 <= coeff <= 2^35
	 */
	coeff = div_u64(coeff * 1000, max(model_data->reference_voltage, 1));
	/* Range: 0 <= coeff <= 2^38 */
	coeff = div_u64(coeff * 1000, max(model_data->reference_voltage, 1));

	/* Scale by user-specified integer factor.
	 * Range: 0 <= coeff_mul < 2^43
	 */
	coeff_mul = coeff * model_data->scaling_factor;

	/* The power models have results with units
	 * mW/(MHz V^2), i.e. nW/(Hz V^2). With precision of 1/1000000, this
	 * becomes fW/(Hz V^2), which are the units of coeff_mul. However,
	 * kbase_scale_dynamic_power() expects units of pW/(Hz V^2), so divide
	 * by 1000.
	 * Range: 0 <= coeff_mul < 2^33
	 */
	coeff_mul = div_u64(coeff_mul, 1000u);

	/* Clamp to a sensible range - 2^16 gives about 14W at 400MHz/750mV */
	*coeffp = clamp(coeff_mul, (u64)0, (u64)1 << 16);

	return 0;
}

int kbase_ipa_counter_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
	struct kbase_ipa_counter_model_data *model_data =
		(struct kbase_ipa_counter_model_data *)model->model_data;
	struct kbase_device *kbdev = model->kbdev;
	s32 *counter_coeffs_p = model_data->counter_coeffs;
	u64 *cnt_values_p = model_data->counter_values;
	const u64 num_counters = model_data->num_counters;
	u32 active_cycles;
	ktime_t now, diff;
	s64 diff_ms;
	int ret;

	lockdep_assert_held(&kbdev->ipa.lock);

	/* The last argument is supposed to be a pointer to the location that
	 * will store the time for which GPU has been in protected mode since
	 * last query. This can be passed as NULL as counter model itself will
	 * not be used when GPU enters protected mode, as IPA is supposed to
	 * switch to the simple power model.
	 */
	ret = kbase_ipa_control_query(kbdev,
				      model_data->ipa_control_client,
				      cnt_values_p, num_counters, NULL);
	if (WARN_ON(ret))
		return ret;

	now = ktime_get();
	diff = ktime_sub(now, kbdev->ipa.last_sample_time);
	diff_ms = ktime_to_ms(diff);

	kbdev->ipa.last_sample_time = now;

	/* The counter values cannot be relied upon if the sampling interval was
	 * too long. Typically this will happen when the polling is started
	 * after the temperature has risen above a certain trip point. After
	 * that regular calls every 25-100 ms interval are expected.
	 */
	if (diff_ms > MAX_SAMPLE_INTERVAL_MS) {
		dev_dbg(kbdev->dev,
			"Last sample was taken %lld milli seconds ago",
			diff_ms);
		return -EOVERFLOW;
	}

	/* Range: 0 (GPU not used at all), to the max sampling interval, say
	 * 1 seconds, * max GPU frequency (GPU 100% utilized).
	 * 0 <= active_cycles <= 1 * ~2GHz
	 * 0 <= active_cycles < 2^31
	 */
	if (*cnt_values_p > U32_MAX) {
		dev_warn(kbdev->dev,
			 "Increment in GPU_ACTIVE counter more than expected");
		return -ERANGE;
	}

	active_cycles = (u32)*cnt_values_p;

	/* If the value of the active_cycles is less than the threshold, then
	 * return an error so that IPA framework can approximate using the
	 * cached simple model results instead. This may be more accurate
	 * than extrapolating using a very small counter dump.
	 */
	if (active_cycles < (u32)max(model_data->min_sample_cycles, 0))
		return -ENODATA;

	/* Range: 1 <= active_cycles < 2^31 */
	active_cycles = max(1u, active_cycles);

	cnt_values_p++;
	ret = calculate_coeff(model_data, model_data->top_level_cntrs_def,
			      model_data->num_top_level_cntrs,
			      counter_coeffs_p, cnt_values_p, active_cycles,
			      &coeffp[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL]);
	if (ret)
		return ret;

	cnt_values_p += model_data->num_top_level_cntrs;
	counter_coeffs_p += model_data->num_top_level_cntrs;
	ret = calculate_coeff(model_data, model_data->shader_cores_cntrs_def,
			      model_data->num_shader_cores_cntrs,
			      counter_coeffs_p, cnt_values_p, active_cycles,
			      &coeffp[KBASE_IPA_BLOCK_TYPE_SHADER_CORES]);

	return ret;
}

void kbase_ipa_counter_reset_data(struct kbase_ipa_model *model)
{
	struct kbase_ipa_counter_model_data *model_data =
		(struct kbase_ipa_counter_model_data *)model->model_data;
	u64 *cnt_values_p = model_data->counter_values;
	const u64 num_counters = model_data->num_counters;
	int ret;

	lockdep_assert_held(&model->kbdev->ipa.lock);

	ret = kbase_ipa_control_query(model->kbdev,
				      model_data->ipa_control_client,
				      cnt_values_p, num_counters, NULL);
	WARN_ON(ret);
}

int kbase_ipa_counter_common_model_init(struct kbase_ipa_model *model,
		const struct kbase_ipa_counter *top_level_cntrs_def,
		size_t num_top_level_cntrs,
		const struct kbase_ipa_counter *shader_cores_cntrs_def,
		size_t num_shader_cores_cntrs,
		s32 reference_voltage)
{
	struct kbase_ipa_counter_model_data *model_data;
	s32 *counter_coeffs_p;
	int err = 0;
	size_t i;

	if (!model || !top_level_cntrs_def || !shader_cores_cntrs_def ||
	    !num_top_level_cntrs || !num_shader_cores_cntrs)
		return -EINVAL;

	model_data = kzalloc(sizeof(*model_data), GFP_KERNEL);
	if (!model_data)
		return -ENOMEM;

	model_data->kbdev = model->kbdev;

	model_data->top_level_cntrs_def = top_level_cntrs_def;
	model_data->num_top_level_cntrs = num_top_level_cntrs;

	model_data->shader_cores_cntrs_def = shader_cores_cntrs_def;
	model_data->num_shader_cores_cntrs = num_shader_cores_cntrs;

	model->model_data = (void *)model_data;

	counter_coeffs_p = model_data->counter_coeffs;

	for (i = 0; i < model_data->num_top_level_cntrs; ++i) {
		const struct kbase_ipa_counter *counter =
			&model_data->top_level_cntrs_def[i];

		*counter_coeffs_p = counter->coeff_default_value;

		err = kbase_ipa_model_add_param_s32(
			model, counter->name, counter_coeffs_p, 1, false);
		if (err)
			goto exit;

		counter_coeffs_p++;
	}

	for (i = 0; i < model_data->num_shader_cores_cntrs; ++i) {
		const struct kbase_ipa_counter *counter =
			&model_data->shader_cores_cntrs_def[i];

		*counter_coeffs_p = counter->coeff_default_value;

		err = kbase_ipa_model_add_param_s32(
			model, counter->name, counter_coeffs_p, 1, false);
		if (err)
			goto exit;

		counter_coeffs_p++;
	}

	model_data->scaling_factor = DEFAULT_SCALING_FACTOR;
	err = kbase_ipa_model_add_param_s32(
		model, "scale", &model_data->scaling_factor, 1, false);
	if (err)
		goto exit;

	model_data->min_sample_cycles = DEFAULT_MIN_SAMPLE_CYCLES;
	err = kbase_ipa_model_add_param_s32(model, "min_sample_cycles",
					    &model_data->min_sample_cycles, 1,
					    false);
	if (err)
		goto exit;

	model_data->reference_voltage = reference_voltage;
	err = kbase_ipa_model_add_param_s32(model, "reference_voltage",
					    &model_data->reference_voltage, 1,
					    false);
	if (err)
		goto exit;

	err = kbase_ipa_attach_ipa_control(model_data);

exit:
	if (err) {
		kbase_ipa_model_param_free_all(model);
		kfree(model_data);
	}
	return err;
}

void kbase_ipa_counter_common_model_term(struct kbase_ipa_model *model)
{
	struct kbase_ipa_counter_model_data *model_data =
		(struct kbase_ipa_counter_model_data *)model->model_data;

	kbase_ipa_detach_ipa_control(model_data);
	kfree(model_data);
}
