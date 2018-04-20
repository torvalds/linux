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

#include "mali_kbase_ipa_vinstr_common.h"
#include "mali_kbase.h"
#include "mali_kbase_ipa_debugfs.h"


#define JM_BASE    (0 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define TILER_BASE (1 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define MMU_BASE   (2 * KBASE_IPA_NR_BYTES_PER_BLOCK)
#define SC0_BASE   (3 * KBASE_IPA_NR_BYTES_PER_BLOCK)

#define GPU_ACTIVE       (JM_BASE    + KBASE_IPA_NR_BYTES_PER_CNT *  6)
#define TILER_ACTIVE     (TILER_BASE + KBASE_IPA_NR_BYTES_PER_CNT * 45)
#define L2_ANY_LOOKUP    (MMU_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 25)
#define FRAG_ACTIVE      (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT *  4)
#define EXEC_CORE_ACTIVE (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 26)
#define EXEC_INSTR_COUNT (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 28)
#define TEX_COORD_ISSUE  (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 40)
#define VARY_SLOT_32     (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 50)
#define VARY_SLOT_16     (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 51)
#define BEATS_RD_LSC     (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 56)
#define BEATS_WR_LSC     (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 61)
#define BEATS_WR_TIB     (SC0_BASE   + KBASE_IPA_NR_BYTES_PER_CNT * 62)

static const struct kbase_ipa_group ipa_groups_def[] = {
	{
		.name = "l2_access",
		.default_value = 526300,
		.op = kbase_ipa_single_counter,
		.counter = L2_ANY_LOOKUP,
	},
	{
		.name = "exec_instr_count",
		.default_value = 301100,
		.op = kbase_ipa_sum_all_shader_cores,
		.counter = EXEC_INSTR_COUNT,
	},
	{
		.name = "tex_issue",
		.default_value = 197400,
		.op = kbase_ipa_sum_all_shader_cores,
		.counter = TEX_COORD_ISSUE,
	},
	{
		.name = "tile_wb",
		.default_value = -156400,
		.op = kbase_ipa_sum_all_shader_cores,
		.counter = BEATS_WR_TIB,
	},
	{
		.name = "gpu_active",
		.default_value = 115800,
		.op = kbase_ipa_single_counter,
		.counter = GPU_ACTIVE,
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

	model_data->scaling_factor = 15000;
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
