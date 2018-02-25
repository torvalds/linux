/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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

#if MALI_UNIT_TEST
static ktime_t dummy_time;

/* Intercept calls to the kernel function using a macro */
#ifdef ktime_get
#undef ktime_get
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
#define ktime_get() (ACCESS_ONCE(dummy_time))

void kbase_ipa_set_dummy_time(ktime_t t)
{
	ACCESS_ONCE(dummy_time) = t;
}
KBASE_EXPORT_TEST_API(kbase_ipa_set_dummy_time);
#else
#define ktime_get() (READ_ONCE(dummy_time))

void kbase_ipa_set_dummy_time(ktime_t t)
{
	WRITE_ONCE(dummy_time, t);
}
KBASE_EXPORT_TEST_API(kbase_ipa_set_dummy_time);

#endif

#endif /* MALI_UNIT_TEST */

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

	/* Range: -2^54 < ret < 2^54 */
	ret *= coeff;

	return div_s64(ret, 1000000);
}

s64 kbase_ipa_single_counter(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter)
{
	/* Range: 0 < counter_value < 2^27 */
	const u32 counter_value = kbase_ipa_read_hwcnt(model_data, counter);

	/* Range: -2^49 < ret < 2^49 */
	const s64 multiplied = (s64) counter_value * (s64) coeff;

	/* Range: -2^29 < return < 2^29 */
	return div_s64(multiplied, 1000000);
}

int kbase_ipa_attach_vinstr(struct kbase_ipa_model_vinstr_data *model_data)
{
	struct kbase_device *kbdev = model_data->kbdev;
	struct kbase_uk_hwcnt_reader_setup setup;
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

	model_data->last_sample_read_time = ktime_get();
	kbase_vinstr_hwc_clear(model_data->vinstr_cli);

	return 0;
}

void kbase_ipa_detach_vinstr(struct kbase_ipa_model_vinstr_data *model_data)
{
	if (model_data->vinstr_cli)
		kbase_vinstr_detach_client(model_data->vinstr_cli);
	model_data->vinstr_cli = NULL;
	kfree(model_data->vinstr_buffer);
	model_data->vinstr_buffer = NULL;
}

int kbase_ipa_vinstr_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp,
	u32 current_freq)
{
	struct kbase_ipa_model_vinstr_data *model_data =
			(struct kbase_ipa_model_vinstr_data *)model->model_data;
	s64 energy = 0;
	size_t i;
	ktime_t now = ktime_get();
	ktime_t time_since_last_sample =
			ktime_sub(now, model_data->last_sample_read_time);
	/* Range: 2^0 < time_since_last_sample_ms < 2^10 (1-1000ms) */
	s64 time_since_last_sample_ms = ktime_to_ms(time_since_last_sample);
	u64 coeff = 0;
	u64 num_cycles;
	int err = 0;

	err = kbase_vinstr_hwc_dump(model_data->vinstr_cli,
				    BASE_HWCNT_READER_EVENT_MANUAL);
	if (err)
		goto err0;

	model_data->last_sample_read_time = now;

	/* Range of 'energy' is +/- 2^34 * number of IPA groups, so around
	 * -2^38 < energy < 2^38 */
	for (i = 0; i < model_data->groups_def_num; i++) {
		const struct kbase_ipa_group *group = &model_data->groups_def[i];
		s32 coeff, group_energy;

		coeff = model_data->group_values[i];
		group_energy = group->op(model_data, coeff, group->counter_block_offset);

		energy = kbase_ipa_add_saturate(energy, group_energy);
	}

	/* Range: 0 <= coeff < 2^38 */
	if (energy > 0)
		coeff = energy;

	/* Scale by user-specified factor and divide by 1000. But actually
	 * cancel the division out, because we want the num_cycles in KHz and
	 * don't want to lose precision. */

	/* Range: 0 < coeff < 2^53 */
	coeff = coeff * model_data->scaling_factor;

	if (time_since_last_sample_ms == 0) {
		time_since_last_sample_ms = 1;
	} else if (time_since_last_sample_ms < 0) {
		err = -ERANGE;
		goto err0;
	}

	/* Range: 2^20 < num_cycles < 2^40 mCycles */
	num_cycles = (u64) current_freq * (u64) time_since_last_sample_ms;
	/* Range: 2^10 < num_cycles < 2^30 Cycles */
	num_cycles = div_u64(num_cycles, 1000000);

	/* num_cycles should never be 0 in _normal_ usage (because we expect
	 * frequencies on the order of MHz and >10ms polling intervals), but
	 * protect against divide-by-zero anyway. */
	if (num_cycles == 0)
		num_cycles = 1;

	/* Range: 0 < coeff < 2^43 */
	coeff = div_u64(coeff, num_cycles);

err0:
	/* Clamp to a sensible range - 2^16 gives about 14W at 400MHz/750mV */
	*coeffp = clamp(coeff, (u64) 0, (u64) 1 << 16);
	return err;
}
