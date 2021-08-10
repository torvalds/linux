/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#ifndef _KBASE_IPA_COUNTER_COMMON_CSF_H_
#define _KBASE_IPA_COUNTER_COMMON_CSF_H_

#include "mali_kbase.h"
#include "csf/ipa_control/mali_kbase_csf_ipa_control.h"

/* Maximum number of HW counters used by the IPA counter model. */
#define KBASE_IPA_MAX_COUNTER_DEF_NUM 24

struct kbase_ipa_counter_model_data;

/**
 * struct kbase_ipa_counter_model_data - IPA counter model context per device
 * @kbdev:               Pointer to kbase device
 * @ipa_control_client:  Handle returned on registering IPA counter model as a
 *                       client of kbase_ipa_control.
 * @top_level_cntrs_def: Array of description of HW counters used by the IPA
 *                       counter model for top-level.
 * @num_top_level_cntrs: Number of elements in @top_level_cntrs_def array.
 * @shader_cores_cntrs_def: Array of description of HW counters used by the IPA
 *                       counter model for shader cores.
 * @num_shader_cores_cntrs: Number of elements in @shader_cores_cntrs_def array.
 * @counter_coeffs:      Buffer to store coefficient value used for HW counters
 * @counter_values:      Buffer to store the accumulated value of HW counters
 *                       retreived from kbase_ipa_control.
 * @num_counters:        Number of counters queried from kbase_ipa_control.
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
 *                       more accurate than extrapolating using a very small
 *                       counter dump.
 */
struct kbase_ipa_counter_model_data {
	struct kbase_device *kbdev;
	void *ipa_control_client;
	const struct kbase_ipa_counter *top_level_cntrs_def;
	size_t num_top_level_cntrs;
	const struct kbase_ipa_counter *shader_cores_cntrs_def;
	size_t num_shader_cores_cntrs;
	s32 counter_coeffs[KBASE_IPA_MAX_COUNTER_DEF_NUM];
	u64 counter_values[KBASE_IPA_MAX_COUNTER_DEF_NUM];
	u64 num_counters;
	s32 reference_voltage;
	s32 scaling_factor;
	s32 min_sample_cycles;
};

/**
 * struct kbase_ipa_counter - represents a single HW counter used by IPA model
 * @name:                 Name of the HW counter used by IPA counter model
 *                        for energy estimation.
 * @coeff_default_value:  Default value of coefficient for the counter.
 *                        Coefficients are interpreted as fractions where the
 *                        denominator is 1000000.
 * @counter_block_offset: Index to the counter within the counter block of
 *                        type @counter_block_type.
 * @counter_block_type:   Type of the counter block.
 */
struct kbase_ipa_counter {
	const char *name;
	s32 coeff_default_value;
	u32 counter_block_offset;
	enum kbase_ipa_core_type counter_block_type;
};

/**
 * kbase_ipa_counter_dynamic_coeff() - calculate dynamic power based on HW counters
 * @model:		pointer to instantiated model
 * @coeffp:		pointer to location where calculated power, in
 *			pW/(Hz V^2), is stored for top level and shader cores.
 *
 * This is a GPU-agnostic implementation of the get_dynamic_coeff()
 * function of an IPA model. It relies on the model being populated
 * with GPU-specific attributes at initialization time.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_ipa_counter_dynamic_coeff(struct kbase_ipa_model *model, u32 *coeffp);

/**
 * kbase_ipa_counter_reset_data() - Reset the counters data used for dynamic
 *                                  power estimation
 * @model:		pointer to instantiated model
 *
 * Retrieve the accumulated value of HW counters from the kbase_ipa_control
 * component, without doing any processing, which is effectively a reset as the
 * next call to kbase_ipa_counter_dynamic_coeff() will see the increment in
 * counter values from this point onwards.
 */
void kbase_ipa_counter_reset_data(struct kbase_ipa_model *model);

/**
 * kbase_ipa_counter_common_model_init() - initialize ipa power model
 * @model:		 Pointer to the ipa power model to initialize
 * @top_level_cntrs_def: Array corresponding to the HW counters used in the
 *                       top level counter model, contains the counter index,
 *                       default value of the coefficient.
 * @num_top_level_cntrs: Number of elements in the array @top_level_cntrs_def
 * @shader_cores_cntrs_def: Array corresponding to the HW counters used in the
 *                       shader cores counter model, contains the counter index,
 *                       default value of the coefficient.
 * @num_shader_cores_cntrs: Number of elements in the array
 *                          @shader_cores_cntrs_def.
 * @reference_voltage:   voltage, in mV, of the operating point used when
 *                       deriving the power model coefficients.
 *
 * This function performs initialization steps common for ipa counter based
 * model of all CSF GPUs. The set of counters and their respective weights
 * could be different for each GPU. The tuple of counter index and weight
 * is passed via  @top_level_cntrs_def and @shader_cores_cntrs_def array.
 *
 * Return: 0 on success, error code otherwise
 */
int kbase_ipa_counter_common_model_init(struct kbase_ipa_model *model,
		const struct kbase_ipa_counter *top_level_cntrs_def,
		size_t num_top_level_cntrs,
		const struct kbase_ipa_counter *shader_cores_cntrs_def,
		size_t num_shader_cores_cntrs,
		s32 reference_voltage);
/**
 * kbase_ipa_counter_common_model_term() - terminate ipa power model
 * @model: ipa power model to terminate
 *
 * This function performs all necessary steps to terminate ipa power model
 * including clean up of resources allocated to hold model data.
 */
void kbase_ipa_counter_common_model_term(struct kbase_ipa_model *model);

#endif /* _KBASE_IPA_COUNTER_COMMON_CSF_H_ */
