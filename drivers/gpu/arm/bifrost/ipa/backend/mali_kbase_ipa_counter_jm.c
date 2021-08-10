// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2016-2021 ARM Limited. All rights reserved.
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

#include <linux/thermal.h>

#include "mali_kbase_ipa_counter_common_jm.h"
#include "mali_kbase.h"


/* Performance counter blocks base offsets */
#define JM_BASE             (0 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define TILER_BASE          (1 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define MEMSYS_BASE         (2 * KBASE_IPA_NR_BYTES_PER_BLOCK)

/* JM counter block offsets */
#define JM_GPU_ACTIVE (KBASE_IPA_NR_BYTES_PER_CNT *  6)

/* Tiler counter block offsets */
#define TILER_ACTIVE (KBASE_IPA_NR_BYTES_PER_CNT * 45)

/* MEMSYS counter block offsets */
#define MEMSYS_L2_ANY_LOOKUP (KBASE_IPA_NR_BYTES_PER_CNT * 25)

/* SC counter block offsets */
#define SC_FRAG_ACTIVE             (KBASE_IPA_NR_BYTES_PER_CNT *  4)
#define SC_EXEC_CORE_ACTIVE        (KBASE_IPA_NR_BYTES_PER_CNT * 26)
#define SC_EXEC_INSTR_FMA          (KBASE_IPA_NR_BYTES_PER_CNT * 27)
#define SC_EXEC_INSTR_COUNT        (KBASE_IPA_NR_BYTES_PER_CNT * 28)
#define SC_EXEC_INSTR_MSG          (KBASE_IPA_NR_BYTES_PER_CNT * 30)
#define SC_TEX_FILT_NUM_OPERATIONS (KBASE_IPA_NR_BYTES_PER_CNT * 39)
#define SC_TEX_COORD_ISSUE         (KBASE_IPA_NR_BYTES_PER_CNT * 40)
#define SC_TEX_TFCH_NUM_OPERATIONS (KBASE_IPA_NR_BYTES_PER_CNT * 42)
#define SC_VARY_INSTR              (KBASE_IPA_NR_BYTES_PER_CNT * 49)
#define SC_VARY_SLOT_32            (KBASE_IPA_NR_BYTES_PER_CNT * 50)
#define SC_VARY_SLOT_16            (KBASE_IPA_NR_BYTES_PER_CNT * 51)
#define SC_BEATS_RD_LSC            (KBASE_IPA_NR_BYTES_PER_CNT * 56)
#define SC_BEATS_WR_LSC            (KBASE_IPA_NR_BYTES_PER_CNT * 61)
#define SC_BEATS_WR_TIB            (KBASE_IPA_NR_BYTES_PER_CNT * 62)

/**
 * get_jm_counter() - get performance counter offset inside the Job Manager block
 * @model_data:            pointer to GPU model data.
 * @counter_block_offset:  offset in bytes of the performance counter inside the Job Manager block.
 *
 * Return: Block offset in bytes of the required performance counter.
 */
static u32 kbase_g7x_power_model_get_jm_counter(struct kbase_ipa_model_vinstr_data *model_data,
						u32 counter_block_offset)
{
	return JM_BASE + counter_block_offset;
}

/**
 * get_memsys_counter() - get performance counter offset inside the Memory System block
 * @model_data:            pointer to GPU model data.
 * @counter_block_offset:  offset in bytes of the performance counter inside the (first) Memory System block.
 *
 * Return: Block offset in bytes of the required performance counter.
 */
static u32 kbase_g7x_power_model_get_memsys_counter(struct kbase_ipa_model_vinstr_data *model_data,
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
static u32 kbase_g7x_power_model_get_sc_counter(struct kbase_ipa_model_vinstr_data *model_data,
						u32 counter_block_offset)
{
	const u32 sc_base = MEMSYS_BASE +
		(model_data->kbdev->gpu_props.props.l2_props.num_l2_slices *
		 KBASE_IPA_NR_BYTES_PER_BLOCK);
	return sc_base + counter_block_offset;
}

/**
 * memsys_single_counter() - calculate energy for a single Memory System performance counter.
 * @model_data:   pointer to GPU model data.
 * @coeff:        default value of coefficient for IPA group.
 * @offset:       offset in bytes of the counter inside the block it belongs to.
 *
 * Return: Energy estimation for a single Memory System performance counter.
 */
static s64 kbase_g7x_sum_all_memsys_blocks(
		struct kbase_ipa_model_vinstr_data *model_data,
		s32 coeff,
		u32 offset)
{
	u32 counter;

	counter = kbase_g7x_power_model_get_memsys_counter(model_data, offset);
	return kbase_ipa_sum_all_memsys_blocks(model_data, coeff, counter);
}

/**
 * sum_all_shader_cores() - calculate energy for a Shader Cores performance counter for all cores.
 * @model_data:            pointer to GPU model data.
 * @coeff:                 default value of coefficient for IPA group.
 * @counter_block_offset:  offset in bytes of the counter inside the block it belongs to.
 *
 * Return: Energy estimation for a Shader Cores performance counter for all cores.
 */
static s64 kbase_g7x_sum_all_shader_cores(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff,
	u32 counter_block_offset)
{
	u32 counter;

	counter = kbase_g7x_power_model_get_sc_counter(model_data,
						       counter_block_offset);
	return kbase_ipa_sum_all_shader_cores(model_data, coeff, counter);
}

/**
 * jm_single_counter() - calculate energy for a single Job Manager performance counter.
 * @model_data:            pointer to GPU model data.
 * @coeff:                 default value of coefficient for IPA group.
 * @counter_block_offset:  offset in bytes of the counter inside the block it belongs to.
 *
 * Return: Energy estimation for a single Job Manager performance counter.
 */
static s64 kbase_g7x_jm_single_counter(
	struct kbase_ipa_model_vinstr_data *model_data,
	s32 coeff,
	u32 counter_block_offset)
{
	u32 counter;

	counter = kbase_g7x_power_model_get_jm_counter(model_data,
						     counter_block_offset);
	return kbase_ipa_single_counter(model_data, coeff, counter);
}

/**
 * get_active_cycles() - return the GPU_ACTIVE counter
 * @model_data:            pointer to GPU model data.
 *
 * Return: the number of cycles the GPU was active during the counter sampling
 * period.
 */
static u32 kbase_g7x_get_active_cycles(
	struct kbase_ipa_model_vinstr_data *model_data)
{
	u32 counter = kbase_g7x_power_model_get_jm_counter(model_data, JM_GPU_ACTIVE);

	/* Counters are only 32-bit, so we can safely multiply by 1 then cast
	 * the 64-bit result back to a u32.
	 */
	return kbase_ipa_single_counter(model_data, 1, counter);
}

/* Table of IPA group definitions.
 *
 * For each IPA group, this table defines a function to access the given performance block counter (or counters,
 * if the operation needs to be iterated on multiple blocks) and calculate energy estimation.
 */

static const struct kbase_ipa_group ipa_groups_def_g71[] = {
	{
		.name = "l2_access",
		.default_value = 526300,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_count",
		.default_value = 301100,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_COUNT,
	},
	{
		.name = "tex_issue",
		.default_value = 197400,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_COORD_ISSUE,
	},
	{
		.name = "tile_wb",
		.default_value = -156400,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_BEATS_WR_TIB,
	},
	{
		.name = "gpu_active",
		.default_value = 115800,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
};

static const struct kbase_ipa_group ipa_groups_def_g72[] = {
	{
		.name = "l2_access",
		.default_value = 393000,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_count",
		.default_value = 227000,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_COUNT,
	},
	{
		.name = "tex_issue",
		.default_value = 181900,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_COORD_ISSUE,
	},
	{
		.name = "tile_wb",
		.default_value = -120200,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_BEATS_WR_TIB,
	},
	{
		.name = "gpu_active",
		.default_value = 133100,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
};

static const struct kbase_ipa_group ipa_groups_def_g76[] = {
	{
		.name = "gpu_active",
		.default_value = 122000,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
	{
		.name = "exec_instr_count",
		.default_value = 488900,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_COUNT,
	},
	{
		.name = "vary_instr",
		.default_value = 212100,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_VARY_INSTR,
	},
	{
		.name = "tex_tfch_num_operations",
		.default_value = 288000,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_TFCH_NUM_OPERATIONS,
	},
	{
		.name = "l2_access",
		.default_value = 378100,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
};

static const struct kbase_ipa_group ipa_groups_def_g52_r1[] = {
	{
		.name = "gpu_active",
		.default_value = 224200,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
	{
		.name = "exec_instr_count",
		.default_value = 384700,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_COUNT,
	},
	{
		.name = "vary_instr",
		.default_value = 271900,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_VARY_INSTR,
	},
	{
		.name = "tex_tfch_num_operations",
		.default_value = 477700,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_TFCH_NUM_OPERATIONS,
	},
	{
		.name = "l2_access",
		.default_value = 551400,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
};

static const struct kbase_ipa_group ipa_groups_def_g51[] = {
	{
		.name = "gpu_active",
		.default_value = 201400,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
	{
		.name = "exec_instr_count",
		.default_value = 392700,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_COUNT,
	},
	{
		.name = "vary_instr",
		.default_value = 274000,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_VARY_INSTR,
	},
	{
		.name = "tex_tfch_num_operations",
		.default_value = 528000,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_TFCH_NUM_OPERATIONS,
	},
	{
		.name = "l2_access",
		.default_value = 506400,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
};

static const struct kbase_ipa_group ipa_groups_def_g77[] = {
	{
		.name = "l2_access",
		.default_value = 710800,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_msg",
		.default_value = 2375300,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_MSG,
	},
	{
		.name = "exec_instr_fma",
		.default_value = 656100,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_FMA,
	},
	{
		.name = "tex_filt_num_operations",
		.default_value = 318800,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_FILT_NUM_OPERATIONS,
	},
	{
		.name = "gpu_active",
		.default_value = 172800,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
};

static const struct kbase_ipa_group ipa_groups_def_tbex[] = {
	{
		.name = "l2_access",
		.default_value = 599800,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_msg",
		.default_value = 1830200,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_MSG,
	},
	{
		.name = "exec_instr_fma",
		.default_value = 407300,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_FMA,
	},
	{
		.name = "tex_filt_num_operations",
		.default_value = 224500,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_FILT_NUM_OPERATIONS,
	},
	{
		.name = "gpu_active",
		.default_value = 153800,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
};

static const struct kbase_ipa_group ipa_groups_def_tbax[] = {
	{
		.name = "l2_access",
		.default_value = 599800,
		.op = kbase_g7x_sum_all_memsys_blocks,
		.counter_block_offset = MEMSYS_L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_msg",
		.default_value = 1830200,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_MSG,
	},
	{
		.name = "exec_instr_fma",
		.default_value = 407300,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_EXEC_INSTR_FMA,
	},
	{
		.name = "tex_filt_num_operations",
		.default_value = 224500,
		.op = kbase_g7x_sum_all_shader_cores,
		.counter_block_offset = SC_TEX_FILT_NUM_OPERATIONS,
	},
	{
		.name = "gpu_active",
		.default_value = 153800,
		.op = kbase_g7x_jm_single_counter,
		.counter_block_offset = JM_GPU_ACTIVE,
	},
};


#define IPA_POWER_MODEL_OPS(gpu, init_token) \
	const struct kbase_ipa_model_ops kbase_ ## gpu ## _ipa_model_ops = { \
		.name = "mali-" #gpu "-power-model", \
		.init = kbase_ ## init_token ## _power_model_init, \
		.term = kbase_ipa_vinstr_common_model_term, \
		.get_dynamic_coeff = kbase_ipa_vinstr_dynamic_coeff, \
		.reset_counter_data = kbase_ipa_vinstr_reset_data, \
	}; \
	KBASE_EXPORT_TEST_API(kbase_ ## gpu ## _ipa_model_ops)

#define STANDARD_POWER_MODEL(gpu, reference_voltage) \
	static int kbase_ ## gpu ## _power_model_init(\
			struct kbase_ipa_model *model) \
	{ \
		BUILD_BUG_ON(ARRAY_SIZE(ipa_groups_def_ ## gpu) > \
				KBASE_IPA_MAX_GROUP_DEF_NUM); \
		return kbase_ipa_vinstr_common_model_init(model, \
				ipa_groups_def_ ## gpu, \
				ARRAY_SIZE(ipa_groups_def_ ## gpu), \
				kbase_g7x_get_active_cycles, \
				(reference_voltage)); \
	} \
	IPA_POWER_MODEL_OPS(gpu, gpu)

#define ALIAS_POWER_MODEL(gpu, as_gpu) \
	IPA_POWER_MODEL_OPS(gpu, as_gpu)

STANDARD_POWER_MODEL(g71, 800);
STANDARD_POWER_MODEL(g72, 800);
STANDARD_POWER_MODEL(g76, 800);
STANDARD_POWER_MODEL(g52_r1, 1000);
STANDARD_POWER_MODEL(g51, 1000);
STANDARD_POWER_MODEL(g77, 1000);
STANDARD_POWER_MODEL(tbex, 1000);
STANDARD_POWER_MODEL(tbax, 1000);

/* g52 is an alias of g76 (TNOX) for IPA */
ALIAS_POWER_MODEL(g52, g76);
/* tnax is an alias of g77 (TTRX) for IPA */
ALIAS_POWER_MODEL(tnax, g77);

static const struct kbase_ipa_model_ops *ipa_counter_model_ops[] = {
	&kbase_g71_ipa_model_ops,
	&kbase_g72_ipa_model_ops,
	&kbase_g76_ipa_model_ops,
	&kbase_g52_ipa_model_ops,
	&kbase_g52_r1_ipa_model_ops,
	&kbase_g51_ipa_model_ops,
	&kbase_g77_ipa_model_ops,
	&kbase_tnax_ipa_model_ops,
	&kbase_tbex_ipa_model_ops,
	&kbase_tbax_ipa_model_ops
};

const struct kbase_ipa_model_ops *kbase_ipa_counter_model_ops_find(
		struct kbase_device *kbdev, const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ipa_counter_model_ops); ++i) {
		const struct kbase_ipa_model_ops *ops =
			ipa_counter_model_ops[i];

		if (!strcmp(ops->name, name))
			return ops;
	}

	dev_err(kbdev->dev, "power model \'%s\' not found\n", name);

	return NULL;
}

const char *kbase_ipa_counter_model_name_from_id(u32 gpu_id)
{
	const u32 prod_id = (gpu_id & GPU_ID_VERSION_PRODUCT_ID) >>
			GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	switch (GPU_ID2_MODEL_MATCH_VALUE(prod_id)) {
	case GPU_ID2_PRODUCT_TMIX:
		return "mali-g71-power-model";
	case GPU_ID2_PRODUCT_THEX:
		return "mali-g72-power-model";
	case GPU_ID2_PRODUCT_TNOX:
		return "mali-g76-power-model";
	case GPU_ID2_PRODUCT_TSIX:
		return "mali-g51-power-model";
	case GPU_ID2_PRODUCT_TGOX:
		if ((gpu_id & GPU_ID2_VERSION_MAJOR) ==
				(0 << GPU_ID2_VERSION_MAJOR_SHIFT))
			/* g52 aliased to g76 power-model's ops */
			return "mali-g52-power-model";
		else
			return "mali-g52_r1-power-model";
	case GPU_ID2_PRODUCT_TNAX:
		return "mali-tnax-power-model";
	case GPU_ID2_PRODUCT_TTRX:
		return "mali-g77-power-model";
	case GPU_ID2_PRODUCT_TBEX:
		return "mali-tbex-power-model";
	case GPU_ID2_PRODUCT_TBAX:
		return "mali-tbax-power-model";
	default:
		return NULL;
	}
}
