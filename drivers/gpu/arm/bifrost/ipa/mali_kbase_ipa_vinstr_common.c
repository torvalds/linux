/*
 *
 * (C) COPYRIGHT 2017-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "mali_kbase_ipa_vinstr_common.h"
#include "mali_kbase_ipa_debugfs.h"

#define DEFAULT_SCALING_FACTOR 5

/* If the value of GPU_ACTIVE is below this, use the simple model
 * instead, to avoid extrapolating small amounts of counter data across
 * large sample periods.
 */
#define DEFAULT_MIN_SAMPLE_CYCLES 10000

/**
 * read_hwcnt() - read a counter value
 * @model_data:		pointer to model data
 * @offset:		offset, in bytes, into vinstr buffer
 *
 * Return: A 32-bit counter value. Range: 0 < value < 2^27 (worst case would be
 * incrementing every cycle over a ~100ms sample period at a high frequency,
 * e.g. 1 GHz: 2^30 * 0.1seconds ~= 2^27.
 */
static inline u32 kbase_ipa_read_hwcnt(
	struct kbase_ipa_model_vinstr_data *model_data,
	u32 offset)
{
	u8 *p = (u8 *)model_data->dump_buf.dump_buf;

	return *(u32 *)&p[offset];
}

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

s64 kbase_ipa_sum_all_shader_cores(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter)
{
	struct kbase_device *kbdev = model_data->kbdev;
	u64 core_mask;
	u32 base = 0;
	s64 ret = 0;

	core_mask = kbdev->gpu_props.props.coherency_info.group[0].core_mask;
	while (core_mask != 0ull) {
		if ((core_mask & 1ull) != 0ull) {
			/* 0 < counter_value < 2^27 */
			u32 counter_value = kbase_ipa_read_hwcnt(model_data,
						       base + counter);

			/* 0 < ret < 2^27 * max_num_cores = 2^32 */
			ret = kbase_ipa_add_saturate(ret, counter_value);
		}
		base += KBASE_IPA_NR_BYTES_PER_BLOCK;
		core_mask >>= 1;
	}

	/* Range: -2^54 < ret * coeff < 2^54 */
	return ret * coeff;
}

s64 kbase_ipa_sum_all_memsys_blocks(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter)
{
	struct kbase_device *kbdev = model_data->kbdev;
	const u32 num_blocks = kbdev->gpu_props.props.l2_props.num_l2_slices;
	u32 base = 0;
	s64 ret = 0;
	u32 i;

	for (i = 0; i < num_blocks; i++) {
		/* 0 < counter_value < 2^27 */
		u32 counter_value = kbase_ipa_read_hwcnt(model_data,
					       base + counter);

		/* 0 < ret < 2^27 * max_num_memsys_blocks = 2^29 */
		ret = kbase_ipa_add_saturate(ret, counter_value);
		base += KBASE_IPA_NR_BYTES_PER_BLOCK;
	}

	/* Range: -2^51 < ret * coeff < 2^51 */
	return ret * coeff;
}

s64 kbase_ipa_single_counter(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter)
{
	/* Range: 0 < counter_value < 2^27 */
	const u32 counter_value = kbase_ipa_read_hwcnt(model_data, counter);

	/* Range: -2^49 < ret < 2^49 */
	return counter_value * (s64) coeff;
}

int kbase_ipa_attach_vinstr(struct kbase_ipa_model_vinstr_data *model_data)
{
	int errcode;
	struct kbase_device *kbdev = model_data->kbdev;
	struct kbase_hwcnt_virtualizer *hvirt = kbdev->hwcnt_gpu_virt;
	struct kbase_hwcnt_enable_map enable_map;
	const struct kbase_hwcnt_metadata *metadata =
		kbase_hwcnt_virtualizer_metadata(hvirt);

	if (!metadata)
		return -1;

	errcode = kbase_hwcnt_enable_map_alloc(metadata, &enable_map);
	if (errcode) {
		dev_err(kbdev->dev, "Failed to allocate IPA enable map");
		return errcode;
	}

	kbase_hwcnt_enable_map_enable_all(&enable_map);

	/* Disable cycle counter only. */
	enable_map.clk_enable_map = 0;

	errcode = kbase_hwcnt_virtualizer_client_create(
		hvirt, &enable_map, &model_data->hvirt_cli);
	kbase_hwcnt_enable_map_free(&enable_map);
	if (errcode) {
		dev_err(kbdev->dev, "Failed to register IPA with virtualizer");
		model_data->hvirt_cli = NULL;
		return errcode;
	}

	errcode = kbase_hwcnt_dump_buffer_alloc(
		metadata, &model_data->dump_buf);
	if (errcode) {
		dev_err(kbdev->dev, "Failed to allocate IPA dump buffer");
		kbase_hwcnt_virtualizer_client_destroy(model_data->hvirt_cli);
		model_data->hvirt_cli = NULL;
		return errcode;
	}

	return 0;
}

void kbase_ipa_detach_vinstr(struct kbase_ipa_model_vinstr_data *model_data)
{
	if (model_data->hvirt_cli) {
		kbase_hwcnt_virtualizer_client_destroy(model_data->hvirt_cli);
		kbase_hwcnt_dump_buffer_free(&model_data->dump_buf);
		model_data->hvirt_cli = NULL;
	}
}

int kbase_ipa_vinstr_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
	struct kbase_ipa_model_vinstr_data *model_data =
			(struct kbase_ipa_model_vinstr_data *)model->model_data;
	s64 energy = 0;
	size_t i;
	u64 coeff = 0, coeff_mul = 0;
	u64 start_ts_ns, end_ts_ns;
	u32 active_cycles;
	int err = 0;

	err = kbase_hwcnt_virtualizer_client_dump(model_data->hvirt_cli,
		&start_ts_ns, &end_ts_ns, &model_data->dump_buf);
	if (err)
		goto err0;

	/* Range: 0 (GPU not used at all), to the max sampling interval, say
	 * 1s, * max GPU frequency (GPU 100% utilized).
	 * 0 <= active_cycles <= 1 * ~2GHz
	 * 0 <= active_cycles < 2^31
	 */
	active_cycles = model_data->get_active_cycles(model_data);

	if (active_cycles < (u32) max(model_data->min_sample_cycles, 0)) {
		err = -ENODATA;
		goto err0;
	}

	/* Range: 1 <= active_cycles < 2^31 */
	active_cycles = max(1u, active_cycles);

	/* Range of 'energy' is +/- 2^54 * number of IPA groups (~8), so around
	 * -2^57 < energy < 2^57
	 */
	for (i = 0; i < model_data->groups_def_num; i++) {
		const struct kbase_ipa_group *group = &model_data->groups_def[i];
		s32 coeff = model_data->group_values[i];
		s64 group_energy = group->op(model_data, coeff,
					     group->counter_block_offset);

		energy = kbase_ipa_add_saturate(energy, group_energy);
	}

	/* Range: 0 <= coeff < 2^57 */
	if (energy > 0)
		coeff = energy;

	/* Range: 0 <= coeff < 2^57 (because active_cycles >= 1). However, this
	 * can be constrained further: Counter values can only be increased by
	 * a theoretical maximum of about 64k per clock cycle. Beyond this,
	 * we'd have to sample every 1ms to avoid them overflowing at the
	 * lowest clock frequency (say 100MHz). Therefore, we can write the
	 * range of 'coeff' in terms of active_cycles:
	 *
	 * coeff = SUM(coeffN * counterN * num_cores_for_counterN)
	 * coeff <= SUM(coeffN * counterN) * max_num_cores
	 * coeff <= num_IPA_groups * max_coeff * max_counter * max_num_cores
	 *       (substitute max_counter = 2^16 * active_cycles)
	 * coeff <= num_IPA_groups * max_coeff * 2^16 * active_cycles * max_num_cores
	 * coeff <=    2^3         *    2^22   * 2^16 * active_cycles * 2^5
	 * coeff <= 2^46 * active_cycles
	 *
	 * So after the division: 0 <= coeff <= 2^46
	 */
	coeff = div_u64(coeff, active_cycles);

	/* Not all models were derived at the same reference voltage. Voltage
	 * scaling is done by multiplying by V^2, so we need to *divide* by
	 * Vref^2 here.
	 * Range: 0 <= coeff <= 2^49
	 */
	coeff = div_u64(coeff * 1000, max(model_data->reference_voltage, 1));
	/* Range: 0 <= coeff <= 2^52 */
	coeff = div_u64(coeff * 1000, max(model_data->reference_voltage, 1));

	/* Scale by user-specified integer factor.
	 * Range: 0 <= coeff_mul < 2^57
	 */
	coeff_mul = coeff * model_data->scaling_factor;

	/* The power models have results with units
	 * mW/(MHz V^2), i.e. nW/(Hz V^2). With precision of 1/1000000, this
	 * becomes fW/(Hz V^2), which are the units of coeff_mul. However,
	 * kbase_scale_dynamic_power() expects units of pW/(Hz V^2), so divide
	 * by 1000.
	 * Range: 0 <= coeff_mul < 2^47
	 */
	coeff_mul = div_u64(coeff_mul, 1000u);

err0:
	/* Clamp to a sensible range - 2^16 gives about 14W at 400MHz/750mV */
	*coeffp = clamp(coeff_mul, (u64) 0, (u64) 1 << 16);
	return err;
}

int kbase_ipa_vinstr_common_model_init(struct kbase_ipa_model *model,
				       const struct kbase_ipa_group *ipa_groups_def,
				       size_t ipa_group_size,
				       kbase_ipa_get_active_cycles_callback get_active_cycles,
				       s32 reference_voltage)
{
	int err = 0;
	size_t i;
	struct kbase_ipa_model_vinstr_data *model_data;

	if (!model || !ipa_groups_def || !ipa_group_size || !get_active_cycles)
		return -EINVAL;

	model_data = kzalloc(sizeof(*model_data), GFP_KERNEL);
	if (!model_data)
		return -ENOMEM;

	model_data->kbdev = model->kbdev;
	model_data->groups_def = ipa_groups_def;
	model_data->groups_def_num = ipa_group_size;
	model_data->get_active_cycles = get_active_cycles;

	model->model_data = (void *) model_data;

	for (i = 0; i < model_data->groups_def_num; ++i) {
		const struct kbase_ipa_group *group = &model_data->groups_def[i];

		model_data->group_values[i] = group->default_value;
		err = kbase_ipa_model_add_param_s32(model, group->name,
					&model_data->group_values[i],
					1, false);
		if (err)
			goto exit;
	}

	model_data->scaling_factor = DEFAULT_SCALING_FACTOR;
	err = kbase_ipa_model_add_param_s32(model, "scale",
					    &model_data->scaling_factor,
					    1, false);
	if (err)
		goto exit;

	model_data->min_sample_cycles = DEFAULT_MIN_SAMPLE_CYCLES;
	err = kbase_ipa_model_add_param_s32(model, "min_sample_cycles",
					    &model_data->min_sample_cycles,
					    1, false);
	if (err)
		goto exit;

	model_data->reference_voltage = reference_voltage;
	err = kbase_ipa_model_add_param_s32(model, "reference_voltage",
					    &model_data->reference_voltage,
					    1, false);
	if (err)
		goto exit;

	err = kbase_ipa_attach_vinstr(model_data);

exit:
	if (err) {
		kbase_ipa_model_param_free_all(model);
		kfree(model_data);
	}
	return err;
}

void kbase_ipa_vinstr_common_model_term(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_vinstr_data *model_data =
			(struct kbase_ipa_model_vinstr_data *)model->model_data;

	kbase_ipa_detach_vinstr(model_data);
	kfree(model_data);
}
