/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2017-2018, 2020-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_IPA_COUNTER_COMMON_JM_H_
#define _KBASE_IPA_COUNTER_COMMON_JM_H_

#include "mali_kbase.h"
#include "mali_kbase_hwcnt_virtualizer.h"
#include "mali_kbase_hwcnt_types.h"

/* Maximum number of IPA groups for an IPA model. */
#define KBASE_IPA_MAX_GROUP_DEF_NUM  16

/* Number of bytes per hardware counter in a vinstr_buffer. */
#define KBASE_IPA_NR_BYTES_PER_CNT    4

/* Number of hardware counters per block in a vinstr_buffer. */
#define KBASE_IPA_NR_CNT_PER_BLOCK   64

/* Number of bytes per block in a vinstr_buffer. */
#define KBASE_IPA_NR_BYTES_PER_BLOCK \
	(KBASE_IPA_NR_CNT_PER_BLOCK * KBASE_IPA_NR_BYTES_PER_CNT)

struct kbase_ipa_model_vinstr_data;

typedef u32
kbase_ipa_get_active_cycles_callback(struct kbase_ipa_model_vinstr_data *);

/**
 * struct kbase_ipa_model_vinstr_data - IPA context per device
 * @kbdev:               pointer to kbase device
 * @group_values:        values of coefficients for IPA groups
 * @groups_def:          Array of IPA groups.
 * @groups_def_num:      Number of elements in the array of IPA groups.
 * @get_active_cycles:   Callback to return number of active cycles during
 *                       counter sample period
 * @hvirt_cli:           hardware counter virtualizer client handle
 * @dump_buf:            buffer to dump hardware counters onto
 * @reference_voltage:   voltage, in mV, of the operating point used when
 *                       deriving the power model coefficients. Range approx
 *                       0.1V - 5V (~= 8V): 2^7 <= reference_voltage <= 2^13
 * @scaling_factor:      User-specified power scaling factor. This is an
 *                       integer, which is multiplied by the power coefficient
 *                       just before OPP scaling.
 *                       Range approx 0-32: 0 < scaling_factor < 2^5
 * @min_sample_cycles:   If the value of the GPU_ACTIVE counter (the number of
 *                       cycles the GPU was working) is less than
 *                       min_sample_cycles, the counter model will return an
 *                       error, causing the IPA framework to approximate using
 *                       the cached simple model results instead. This may be
 *                       more accurate than extrapolating  using a very small
 *                       counter dump.
 */
struct kbase_ipa_model_vinstr_data {
	struct kbase_device *kbdev;
	s32 group_values[KBASE_IPA_MAX_GROUP_DEF_NUM];
	const struct kbase_ipa_group *groups_def;
	size_t groups_def_num;
	kbase_ipa_get_active_cycles_callback *get_active_cycles;
	struct kbase_hwcnt_virtualizer_client *hvirt_cli;
	struct kbase_hwcnt_dump_buffer dump_buf;
	s32 reference_voltage;
	s32 scaling_factor;
	s32 min_sample_cycles;
};

/**
 * struct ipa_group - represents a single IPA group
 * @name:               name of the IPA group
 * @default_value:      default value of coefficient for IPA group.
 *                      Coefficients are interpreted as fractions where the
 *                      denominator is 1000000.
 * @op:                 which operation to be performed on the counter values
 * @counter_block_offset:  block offset in bytes of the counter used to calculate energy for IPA group
 */
struct kbase_ipa_group {
	const char *name;
	s32 default_value;
	s64 (*op)(struct kbase_ipa_model_vinstr_data *, s32, u32);
	u32 counter_block_offset;
};

/**
 * kbase_ipa_sum_all_shader_cores() - sum a counter over all cores
 * @model_data:		pointer to model data
 * @coeff:		model coefficient. Unity is ~2^20, so range approx
 *			+/- 4.0: -2^22 < coeff < 2^22
 * @counter:		offset in bytes of the counter used to calculate energy
 *			for IPA group
 *
 * Calculate energy estimation based on hardware counter `counter'
 * across all shader cores.
 *
 * Return: Sum of counter values. Range: -2^54 < ret < 2^54
 */
s64 kbase_ipa_sum_all_shader_cores(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter);

/**
 * kbase_ipa_sum_all_memsys_blocks() - sum a counter over all mem system blocks
 * @model_data:		pointer to model data
 * @coeff:		model coefficient. Unity is ~2^20, so range approx
 *			+/- 4.0: -2^22 < coeff < 2^22
 * @counter:		offset in bytes of the counter used to calculate energy
 *			for IPA group
 *
 * Calculate energy estimation based on hardware counter `counter' across all
 * memory system blocks.
 *
 * Return: Sum of counter values. Range: -2^51 < ret < 2^51
 */
s64 kbase_ipa_sum_all_memsys_blocks(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter);

/**
 * kbase_ipa_single_counter() - sum a single counter
 * @model_data:		pointer to model data
 * @coeff:		model coefficient. Unity is ~2^20, so range approx
 *			+/- 4.0: -2^22 < coeff < 2^22
 * @counter:		offset in bytes of the counter used to calculate energy
 *			for IPA group
 *
 * Calculate energy estimation based on hardware counter `counter'.
 *
 * Return: Counter value. Range: -2^49 < ret < 2^49
 */
s64 kbase_ipa_single_counter(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff, u32 counter);

/**
 * attach_vinstr() - attach a vinstr_buffer to an IPA model.
 * @model_data:		pointer to model data
 *
 * Attach a vinstr_buffer to an IPA model. The vinstr_buffer
 * allows access to the hardware counters used to calculate
 * energy consumption.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_ipa_attach_vinstr(struct kbase_ipa_model_vinstr_data *model_data);

/**
 * detach_vinstr() - detach a vinstr_buffer from an IPA model.
 * @model_data:		pointer to model data
 *
 * Detach a vinstr_buffer from an IPA model.
 */
void kbase_ipa_detach_vinstr(struct kbase_ipa_model_vinstr_data *model_data);

/**
 * kbase_ipa_vinstr_dynamic_coeff() - calculate dynamic power based on HW counters
 * @model:		pointer to instantiated model
 * @coeffp:		pointer to location where calculated power, in
 *			pW/(Hz V^2), is stored.
 *
 * This is a GPU-agnostic implementation of the get_dynamic_coeff()
 * function of an IPA model. It relies on the model being populated
 * with GPU-specific attributes at initialization time.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_ipa_vinstr_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp);

/**
 * kbase_ipa_vinstr_reset_data() - Reset the counters data used for dynamic
 *                                 power estimation
 * @model:		pointer to instantiated model
 *
 * Currently it is not implemented for JM GPUs.
 * When implemented it is expected to retrieve the accumulated value of HW
 * counters from the Vinstr component, without doing any processing, which is
 * effectively a reset as the next call to kbase_ipa_counter_dynamic_coeff()
 * will see the increment in counter values from this point onwards.
 */
void kbase_ipa_vinstr_reset_data(struct kbase_ipa_model *model);

/**
 * kbase_ipa_vinstr_common_model_init() - initialize ipa power model
 * @model:		ipa power model to initialize
 * @ipa_groups_def:	array of ipa groups which sets coefficients for
 *			the corresponding counters used in the ipa model
 * @ipa_group_size:     number of elements in the array @ipa_groups_def
 * @get_active_cycles:  callback to return the number of cycles the GPU was
 *			active during the counter sample period.
 * @reference_voltage:  voltage, in mV, of the operating point used when
 *                      deriving the power model coefficients.
 *
 * This initialization function performs initialization steps common
 * for ipa models based on counter values. In each call, the model
 * passes its specific coefficient values per ipa counter group via
 * @ipa_groups_def array.
 *
 * Return: 0 on success, error code otherwise
 */
int kbase_ipa_vinstr_common_model_init(struct kbase_ipa_model *model,
				       const struct kbase_ipa_group *ipa_groups_def,
				       size_t ipa_group_size,
				       kbase_ipa_get_active_cycles_callback *get_active_cycles,
				       s32 reference_voltage);

/**
 * kbase_ipa_vinstr_common_model_term() - terminate ipa power model
 * @model: ipa power model to terminate
 *
 * This function performs all necessary steps to terminate ipa power model
 * including clean up of resources allocated to hold model data.
 */
void kbase_ipa_vinstr_common_model_term(struct kbase_ipa_model *model);

#endif /* _KBASE_IPA_COUNTER_COMMON_JM_H_ */
