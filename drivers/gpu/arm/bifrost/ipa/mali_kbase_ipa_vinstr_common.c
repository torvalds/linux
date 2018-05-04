/*
 *
 * (C) COPYRIGHT 2017-2018 ARM Limited. All rights reserved.
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
	u8 *p = model_data->vinstr_buffer;

	return *(u32 *)&p[offset];
}

static inline s64 kbase_ipa_add_saturate(s64 a, s64 b)
{
	if (S64_MAX - a < b)
		return S64_MAX;
	return a + b;
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

s64 kbase_ipa_single_counter(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter)
{
	/* Range: 0 < counter_value < 2^27 */
	const u32 counter_value = kbase_ipa_read_hwcnt(model_data, counter);

	/* Range: -2^49 < ret < 2^49 */
	return counter_value * (s64) coeff;
}

/**
 * kbase_ipa_gpu_active - Inform IPA that GPU is now active
 * @model_data: Pointer to model data
 *
 * This function may cause vinstr to become active.
 */
static void kbase_ipa_gpu_active(struct kbase_ipa_model_vinstr_data *model_data)
{
	struct kbase_device *kbdev = model_data->kbdev;

	lockdep_assert_held(&kbdev->pm.lock);

	if (!kbdev->ipa.vinstr_active) {
		kbdev->ipa.vinstr_active = true;
		kbase_vinstr_resume_client(model_data->vinstr_cli);
	}
}

/**
 * kbase_ipa_gpu_idle - Inform IPA that GPU is now idle
 * @model_data: Pointer to model data
 *
 * This function may cause vinstr to become idle.
 */
static void kbase_ipa_gpu_idle(struct kbase_ipa_model_vinstr_data *model_data)
{
	struct kbase_device *kbdev = model_data->kbdev;

	lockdep_assert_held(&kbdev->pm.lock);

	if (kbdev->ipa.vinstr_active) {
		kbase_vinstr_suspend_client(model_data->vinstr_cli);
		kbdev->ipa.vinstr_active = false;
	}
}

int kbase_ipa_attach_vinstr(struct kbase_ipa_model_vinstr_data *model_data)
{
	struct kbase_device *kbdev = model_data->kbdev;
	struct kbase_ioctl_hwcnt_reader_setup setup;
	size_t dump_size;

	dump_size = kbase_vinstr_dump_size(kbdev);
	model_data->vinstr_buffer = kzalloc(dump_size, GFP_KERNEL);
	if (!model_data->vinstr_buffer) {
		dev_err(kbdev->dev, "Failed to allocate IPA dump buffer");
		return -1;
	}

	setup.jm_bm = ~0u;
	setup.shader_bm = ~0u;
	setup.tiler_bm = ~0u;
	setup.mmu_l2_bm = ~0u;
	model_data->vinstr_cli = kbase_vinstr_hwcnt_kernel_setup(kbdev->vinstr_ctx,
			&setup, model_data->vinstr_buffer);
	if (!model_data->vinstr_cli) {
		dev_err(kbdev->dev, "Failed to register IPA with vinstr core");
		kfree(model_data->vinstr_buffer);
		model_data->vinstr_buffer = NULL;
		return -1;
	}

	kbase_vinstr_hwc_clear(model_data->vinstr_cli);

	kbdev->ipa.gpu_active_callback = kbase_ipa_gpu_active;
	kbdev->ipa.gpu_idle_callback = kbase_ipa_gpu_idle;
	kbdev->ipa.model_data = model_data;
	kbdev->ipa.vinstr_active = false;
	/* Suspend vinstr, to ensure that the GPU is powered off until there is
	 * something to execute.
	 */
	kbase_vinstr_suspend_client(model_data->vinstr_cli);

	return 0;
}

void kbase_ipa_detach_vinstr(struct kbase_ipa_model_vinstr_data *model_data)
{
	struct kbase_device *kbdev = model_data->kbdev;

	kbdev->ipa.gpu_active_callback = NULL;
	kbdev->ipa.gpu_idle_callback = NULL;
	kbdev->ipa.model_data = NULL;
	kbdev->ipa.vinstr_active = false;

	if (model_data->vinstr_cli)
		kbase_vinstr_detach_client(model_data->vinstr_cli);

	model_data->vinstr_cli = NULL;
	kfree(model_data->vinstr_buffer);
	model_data->vinstr_buffer = NULL;
}

int kbase_ipa_vinstr_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp)
{
	struct kbase_ipa_model_vinstr_data *model_data =
			(struct kbase_ipa_model_vinstr_data *)model->model_data;
	struct kbase_device *kbdev = model_data->kbdev;
	s64 energy = 0;
	size_t i;
	u64 coeff = 0, coeff_mul = 0;
	u32 active_cycles;
	int err = 0;

	if (!kbdev->ipa.vinstr_active)
		goto err0; /* GPU powered off - no counters to collect */

	err = kbase_vinstr_hwc_dump(model_data->vinstr_cli,
				    BASE_HWCNT_READER_EVENT_MANUAL);
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

	/* Scale by user-specified factor (where unity is 1000).
	 * Range: 0 <= coeff_mul < 2^61
	 */
	coeff_mul = coeff * model_data->scaling_factor;

	/* Range: 0 <= coeff_mul < 2^51 */
	coeff_mul = div_u64(coeff_mul, 1000u);

err0:
	/* Clamp to a sensible range - 2^16 gives about 14W at 400MHz/750mV */
	*coeffp = clamp(coeff_mul, (u64) 0, (u64) 1 << 16);
	return err;
}

int kbase_ipa_vinstr_common_model_init(struct kbase_ipa_model *model,
				       const struct kbase_ipa_group *ipa_groups_def,
				       size_t ipa_group_size,
				       kbase_ipa_get_active_cycles_callback get_active_cycles)
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
