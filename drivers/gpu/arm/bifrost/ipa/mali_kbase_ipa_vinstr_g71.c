/*
 *
 * (C) COPYRIGHT 2016-2017 ARM Limited. All rights reserved.
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
#include <linux/thermal.h>

#include "mali_kbase_ipa_vinstr_common.h"
#include "mali_kbase.h"
#include "mali_kbase_ipa_debugfs.h"


/* Performance counter blocks base offsets */
#define JM_BASE             (0 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define TILER_BASE          (1 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define MEMSYS_BASE         (2 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define SC0_BASE_ONE_MEMSYS (3 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define SC0_BASE_TWO_MEMSYS (4 * KBASE_IPA_NR_BYTES_PER_BLOCK)

/* JM counter block offsets */
#define JM_GPU_ACTIVE (KBASE_IPA_NR_BYTES_PER_CNT *  6)

/* Tiler counter block offsets */
#define TILER_ACTIVE (KBASE_IPA_NR_BYTES_PER_CNT * 45)

/* MEMSYS counter block offsets */
#define MEMSYS_L2_ANY_LOOKUP (KBASE_IPA_NR_BYTES_PER_CNT * 25)

/* SC counter block offsets */
#define SC_FRAG_ACTIVE      (KBASE_IPA_NR_BYTES_PER_CNT *  4)
#define SC_EXEC_CORE_ACTIVE (KBASE_IPA_NR_BYTES_PER_CNT * 26)
#define SC_EXEC_INSTR_COUNT (KBASE_IPA_NR_BYTES_PER_CNT * 28)
#define SC_TEX_COORD_ISSUE  (KBASE_IPA_NR_BYTES_PER_CNT * 40)
#define SC_VARY_SLOT_32     (KBASE_IPA_NR_BYTES_PER_CNT * 50)
#define SC_VARY_SLOT_16     (KBASE_IPA_NR_BYTES_PER_CNT * 51)
#define SC_BEATS_RD_LSC     (KBASE_IPA_NR_BYTES_PER_CNT * 56)
#define SC_BEATS_WR_LSC     (KBASE_IPA_NR_BYTES_PER_CNT * 61)
#define SC_BEATS_WR_TIB     (KBASE_IPA_NR_BYTES_PER_CNT * 62)

/** Maximum number of cores for which a single Memory System block of performance counters is present. */
#define KBASE_G71_SINGLE_MEMSYS_MAX_NUM_CORES ((u8)4)


/**
 * get_jm_counter() - get performance counter offset inside the Job Manager block
 * @model_data:            pointer to GPU model data.
 * @counter_block_offset:  offset in bytes of the performance counter inside the Job Manager block.
 *
 * Return: Block offset in bytes of the required performance counter.
 */
static u32 kbase_g71_power_model_get_jm_counter(struct kbase_ipa_model_vinstr_data *model_data,
                                                u32 counter_block_offset)
{
	return JM_BASE + counter_block_offset;
}

/**
 * get_memsys_counter() - get peformance counter offset inside the Memory System block
 * @model_data:            pointer to GPU model data.
 * @counter_block_offset:  offset in bytes of the performance counter inside the (first) Memory System block.
 *
 * Return: Block offset in bytes of the required performance counter.
 */
static u32 kbase_g71_power_model_get_memsys_counter(struct kbase_ipa_model_vinstr_data *model_data,
                                                    u32 counter_block_offset)
{
	/* The base address of Memory System performance counters is always the same, although their number
	 * may vary based on the number of cores. For the moment it's ok to return a constant.
	 */
	return MEMSYS_BASE + counter_block_offset;
}

/**
 * get_sc_counter() - get performance counter offset inside the Shader Cores block
 * @model_data:            pointer to GPU model data.
 * @counter_block_offset:  offset in bytes of the performance counter inside the (first) Shader Cores block.
 *
 * Return: Block offset in bytes of the required performance counter.
 */
static u32 kbase_g71_power_model_get_sc_counter(struct kbase_ipa_model_vinstr_data *model_data,
                                                u32 counter_block_offset)
{
	const u32 sc_base = model_data->kbdev->gpu_props.num_cores <= KBASE_G71_SINGLE_MEMSYS_MAX_NUM_CORES ?
	                    SC0_BASE_ONE_MEMSYS :
	                    SC0_BASE_TWO_MEMSYS;

	return sc_base + counter_block_offset;
}

/**
 * memsys_single_counter() - calculate energy for a single Memory System performance counter.
 * @model_data:            pointer to GPU model data.
 * @coeff:                 default value of coefficient for IPA group.
 * @counter_block_offset:  offset in bytes of the counter inside the block it belongs to.
 *
 * Return: Energy estimation for a single Memory System performance counter.
 */
static s64 kbase_g71_memsys_single_counter(
    struct kbase_ipa_model_vinstr_data *model_data,
    s32 coeff,
    u32 counter_block_offset)
{
	return kbase_ipa_single_counter(model_data, coeff,
	                                kbase_g71_power_model_get_memsys_counter(model_data, counter_block_offset));
}

/**
 * sum_all_shader_cores() - calculate energy for a Shader Cores performance counter for all cores.
 * @model_data:            pointer to GPU model data.
 * @coeff:                 default value of coefficient for IPA group.
 * @counter_block_offset:  offset in bytes of the counter inside the block it belongs to.
 *
 * Return: Energy estimation for a Shader Cores performance counter for all cores.
 */
static s64 kbase_g71_sum_all_shader_cores(
    struct kbase_ipa_model_vinstr_data *model_data,
    s32 coeff,
    u32 counter_block_offset)
{
	return kbase_ipa_sum_all_shader_cores(model_data, coeff,
	                                      kbase_g71_power_model_get_sc_counter(model_data, counter_block_offset));
}

/**
 * jm_single_counter() - calculate energy for a single Job Manager performance counter.
 * @model_data:            pointer to GPU model data.
 * @coeff:                 default value of coefficient for IPA group.
 * @counter_block_offset:  offset in bytes of the counter inside the block it belongs to.
 *
 * Return: Energy estimation for a single Job Manager performance counter.
 */
static s64 kbase_g71_jm_single_counter(
    struct kbase_ipa_model_vinstr_data *model_data,
    s32 coeff,
    u32 counter_block_offset)
{
	return kbase_ipa_single_counter(model_data, coeff,
	                                kbase_g71_power_model_get_jm_counter(model_data, counter_block_offset));
}

/** Table of IPA group definitions.
 *
 * For each IPA group, this table defines a function to access the given performance block counter (or counters,
 * if the operation needs to be iterated on multiple blocks) and calculate energy estimation.
 */
static const struct kbase_ipa_group ipa_groups_def[] = {
	{
		.name = "l2_access",
		.default_value = 526300,
		.op = kbase_g71_memsys_single_counter,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_count",
		.default_value = 301100,
		.op = kbase_g71_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_COUNT,
	},
	{
		.name = "tex_issue",
		.default_value = 197400,
		.op = kbase_g71_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_COORD_ISSUE,
	},
	{
		.name = "tile_wb",
		.default_value = -156400,
		.op = kbase_g71_sum_all_shader_cores,
		.counter_block_offset = SC_BEATS_WR_TIB,
	},
	{
		.name = "gpu_active",
		.default_value = 115800,
		.op = kbase_g71_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
};

static int kbase_g71_power_model_init(struct kbase_ipa_model *model)
{
	int i, err = 0;
	struct kbase_ipa_model_vinstr_data *model_data;

	model_data = kzalloc(sizeof(*model_data), GFP_KERNEL);
	if (!model_data)
		return -ENOMEM;

	model_data->kbdev = model->kbdev;
	model_data->groups_def = ipa_groups_def;
	BUILD_BUG_ON(ARRAY_SIZE(ipa_groups_def) > KBASE_IPA_MAX_GROUP_DEF_NUM);
	model_data->groups_def_num = ARRAY_SIZE(ipa_groups_def);

	model->model_data = (void *) model_data;

	for (i = 0; i < ARRAY_SIZE(ipa_groups_def); ++i) {
		const struct kbase_ipa_group *group = &ipa_groups_def[i];

		model_data->group_values[i] = group->default_value;
		err = kbase_ipa_model_add_param_s32(model, group->name,
					&model_data->group_values[i],
					1, false);
		if (err)
			goto exit;
	}

	model_data->scaling_factor = 5;
	err = kbase_ipa_model_add_param_s32(model, "scale",
					    &model_data->scaling_factor,
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

static void kbase_g71_power_model_term(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_vinstr_data *model_data =
			(struct kbase_ipa_model_vinstr_data *)model->model_data;

	kbase_ipa_detach_vinstr(model_data);
	kfree(model_data);
}


struct kbase_ipa_model_ops kbase_g71_ipa_model_ops = {
		.name = "mali-g71-power-model",
		.init = kbase_g71_power_model_init,
		.term = kbase_g71_power_model_term,
		.get_dynamic_coeff = kbase_ipa_vinstr_dynamic_coeff,
		.do_utilization_scaling_in_framework = false,
};
KBASE_EXPORT_TEST_API(kbase_g71_ipa_model_ops);
