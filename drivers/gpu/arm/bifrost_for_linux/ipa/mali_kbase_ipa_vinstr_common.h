/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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



#ifndef _KBASE_IPA_VINSTR_COMMON_H_
#define _KBASE_IPA_VINSTR_COMMON_H_

#include "mali_kbase.h"

/* Maximum length for the name of an IPA group. */
#define KBASE_IPA_MAX_GROUP_NAME_LEN 15

/* Maximum number of IPA groups for an IPA model. */
#define KBASE_IPA_MAX_GROUP_DEF_NUM  16

/* Number of bytes per hardware counter in a vinstr_buffer. */
#define KBASE_IPA_NR_BYTES_PER_CNT    4

/* Number of hardware counters per block in a vinstr_buffer. */
#define KBASE_IPA_NR_CNT_PER_BLOCK   64

/* Number of bytes per block in a vinstr_buffer. */
#define KBASE_IPA_NR_BYTES_PER_BLOCK \
	(KBASE_IPA_NR_CNT_PER_BLOCK * KBASE_IPA_NR_BYTES_PER_CNT)



/**
 * struct kbase_ipa_model_vinstr_data - IPA context per device
 * @kbdev:               pointer to kbase device
 * @groups_def:          Array of IPA groups.
 * @groups_def_num:      Number of elements in the array of IPA groups.
 * @vinstr_cli:          vinstr client handle
 * @vinstr_buffer:       buffer to dump hardware counters onto
 * @last_sample_read_time: timestamp of last vinstr buffer read
 * @scaling_factor:      user-specified power scaling factor. This is
 *                       interpreted as a fraction where the denominator is
 *                       1000. Range approx 0.0-32.0:
 *                       0 < scaling_factor < 2^15
 */
struct kbase_ipa_model_vinstr_data {
	struct kbase_device *kbdev;
	s32 group_values[KBASE_IPA_MAX_GROUP_DEF_NUM];
	const struct kbase_ipa_group *groups_def;
	size_t groups_def_num;
	struct kbase_vinstr_client *vinstr_cli;
	void *vinstr_buffer;
	ktime_t last_sample_read_time;
	s32 scaling_factor;
};

/**
 * struct ipa_group - represents a single IPA group
 * @name:               name of the IPA group
 * @default_value:      default value of coefficient for IPA group.
 *                      Coefficients are interpreted as fractions where the
 *                      denominator is 1000000.
 * @op:                 which operation to be performed on the counter values
 * @counter:            counter used to calculate energy for IPA group
 */
struct kbase_ipa_group {
	char name[KBASE_IPA_MAX_GROUP_NAME_LEN + 1];
	s32 default_value;
	s64 (*op)(struct kbase_ipa_model_vinstr_data *, s32, u32);
	u32 counter;
};

/*
 * sum_all_shader_cores() - sum a counter over all cores
 * @model_data		pointer to model data
 * @coeff		model coefficient. Unity is ~2^20, so range approx
 * +/- 4.0: -2^22 < coeff < 2^22

 * Calculate energy estimation based on hardware counter `counter'
 * across all shader cores.
 *
 * Return: Sum of counter values. Range: -2^34 < ret < 2^34
 */
s64 kbase_ipa_sum_all_shader_cores(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter);

/*
 * sum_single_counter() - sum a single counter
 * @model_data		pointer to model data
 * @coeff		model coefficient. Unity is ~2^20, so range approx
 * +/- 4.0: -2^22 < coeff < 2^22

 * Calculate energy estimation based on hardware counter `counter'.
 *
 * Return: Counter value. Range: -2^34 < ret < 2^34
 */
s64 kbase_ipa_single_counter(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter);

/*
 * attach_vinstr() - attach a vinstr_buffer to an IPA model.
 * @model_data		pointer to model data
 *
 * Attach a vinstr_buffer to an IPA model. The vinstr_buffer
 * allows access to the hardware counters used to calculate
 * energy consumption.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_ipa_attach_vinstr(struct kbase_ipa_model_vinstr_data *model_data);

/*
 * detach_vinstr() - detach a vinstr_buffer from an IPA model.
 * @model_data		pointer to model data
 *
 * Detach a vinstr_buffer from an IPA model.
 */
void kbase_ipa_detach_vinstr(struct kbase_ipa_model_vinstr_data *model_data);

/**
 * kbase_ipa_vinstr_dynamic_coeff() - calculate dynamic power based on HW counters
 * @model:		pointer to instantiated model
 * @coeffp:		pointer to location where calculated power, in
 *			pW/(Hz V^2), is stored.
 * @current_freq:	frequency the GPU has been running at over the sample
 *			period. In Hz. Range: 10 MHz < 1GHz,
 *			2^20 < current_freq < 2^30
 *
 * This is a GPU-agnostic implementation of the get_dynamic_coeff()
 * function of an IPA model. It relies on the model being populated
 * with GPU-specific attributes at initialization time.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_ipa_vinstr_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp,
	u32 current_freq);

#if MALI_UNIT_TEST
/**
 * kbase_ipa_set_dummy_time() - set a dummy monotonic time value
 * @t: a monotonic time value
 *
 * This is only intended for use in unit tests, to ensure that the kernel time
 * values used by a power model are predictable. Deterministic behavior is
 * necessary to allow validation of the dynamic power values computed by the
 * model.
 */
void kbase_ipa_set_dummy_time(ktime_t t);
#endif /* MALI_UNIT_TEST */

#endif /* _KBASE_IPA_VINSTR_COMMON_H_ */
