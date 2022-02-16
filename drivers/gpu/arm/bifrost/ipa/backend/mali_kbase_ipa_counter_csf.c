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
#include "mali_kbase.h"

/* MEMSYS counter block offsets */
#define L2_RD_MSG_IN            (16)
#define L2_WR_MSG_IN            (18)
#define L2_RD_MSG_OUT           (22)
#define L2_READ_LOOKUP          (26)
#define L2_EXT_WRITE_NOSNP_FULL (43)

/* SC counter block offsets */
#define FRAG_STARVING           (8)
#define FRAG_PARTIAL_QUADS_RAST (10)
#define FRAG_QUADS_EZS_UPDATE   (13)
#define FULL_QUAD_WARPS         (21)
#define EXEC_INSTR_FMA          (27)
#define EXEC_INSTR_CVT          (28)
#define EXEC_INSTR_MSG          (30)
#define TEX_FILT_NUM_OPS        (39)
#define LS_MEM_READ_SHORT       (45)
#define LS_MEM_WRITE_SHORT      (47)
#define VARY_SLOT_16            (51)

/* Tiler counter block offsets */
#define IDVS_POS_SHAD_STALL     (23)
#define PREFETCH_STALL          (25)
#define VFETCH_POS_READ_WAIT    (29)
#define VFETCH_VERTEX_WAIT      (30)
#define IDVS_VAR_SHAD_STALL     (38)
#define ITER_STALL              (40)
#define PMGR_PTR_RD_STALL       (48)

#define COUNTER_DEF(cnt_name, coeff, cnt_idx, block_type)	\
	{							\
		.name = cnt_name,				\
		.coeff_default_value = coeff,			\
		.counter_block_offset = cnt_idx,		\
		.counter_block_type = block_type,		\
	}

#define CSHW_COUNTER_DEF(cnt_name, coeff, cnt_idx)	\
	COUNTER_DEF(cnt_name, coeff, cnt_idx, KBASE_IPA_CORE_TYPE_CSHW)

#define MEMSYS_COUNTER_DEF(cnt_name, coeff, cnt_idx)	\
	COUNTER_DEF(cnt_name, coeff, cnt_idx, KBASE_IPA_CORE_TYPE_MEMSYS)

#define SC_COUNTER_DEF(cnt_name, coeff, cnt_idx)	\
	COUNTER_DEF(cnt_name, coeff, cnt_idx, KBASE_IPA_CORE_TYPE_SHADER)

#define TILER_COUNTER_DEF(cnt_name, coeff, cnt_idx)	\
	COUNTER_DEF(cnt_name, coeff, cnt_idx, KBASE_IPA_CORE_TYPE_TILER)

/* Tables of description of HW counters used by IPA counter model.
 *
 * These tables provide a description of each performance counter
 * used by the top level counter model for energy estimation.
 */
static const struct kbase_ipa_counter ipa_top_level_cntrs_def_todx[] = {
	MEMSYS_COUNTER_DEF("l2_rd_msg_in", 295631, L2_RD_MSG_IN),
	MEMSYS_COUNTER_DEF("l2_ext_write_nosnp_ull", 325168, L2_EXT_WRITE_NOSNP_FULL),

	TILER_COUNTER_DEF("prefetch_stall", 145435, PREFETCH_STALL),
	TILER_COUNTER_DEF("idvs_var_shad_stall", -171917, IDVS_VAR_SHAD_STALL),
	TILER_COUNTER_DEF("idvs_pos_shad_stall", 109980, IDVS_POS_SHAD_STALL),
	TILER_COUNTER_DEF("vfetch_pos_read_wait", -119118, VFETCH_POS_READ_WAIT),
};

static const struct kbase_ipa_counter ipa_top_level_cntrs_def_tgrx[] = {
	MEMSYS_COUNTER_DEF("l2_rd_msg_in", 295631, L2_RD_MSG_IN),
	MEMSYS_COUNTER_DEF("l2_ext_write_nosnp_ull", 325168, L2_EXT_WRITE_NOSNP_FULL),

	TILER_COUNTER_DEF("prefetch_stall", 145435, PREFETCH_STALL),
	TILER_COUNTER_DEF("idvs_var_shad_stall", -171917, IDVS_VAR_SHAD_STALL),
	TILER_COUNTER_DEF("idvs_pos_shad_stall", 109980, IDVS_POS_SHAD_STALL),
	TILER_COUNTER_DEF("vfetch_pos_read_wait", -119118, VFETCH_POS_READ_WAIT),
};

static const struct kbase_ipa_counter ipa_top_level_cntrs_def_tvax[] = {
	MEMSYS_COUNTER_DEF("l2_rd_msg_out", 491414, L2_RD_MSG_OUT),
	MEMSYS_COUNTER_DEF("l2_wr_msg_in", 408645, L2_WR_MSG_IN),

	TILER_COUNTER_DEF("iter_stall", 893324, ITER_STALL),
	TILER_COUNTER_DEF("pmgr_ptr_rd_stall", -975117, PMGR_PTR_RD_STALL),
	TILER_COUNTER_DEF("idvs_pos_shad_stall", 22555, IDVS_POS_SHAD_STALL),
};

static const struct kbase_ipa_counter ipa_top_level_cntrs_def_ttux[] = {
	MEMSYS_COUNTER_DEF("l2_rd_msg_in", 800836, L2_RD_MSG_IN),
	MEMSYS_COUNTER_DEF("l2_wr_msg_in", 415579, L2_WR_MSG_IN),
	MEMSYS_COUNTER_DEF("l2_read_lookup", -198124, L2_READ_LOOKUP),

	TILER_COUNTER_DEF("idvs_pos_shad_stall", 117358, IDVS_POS_SHAD_STALL),
	TILER_COUNTER_DEF("vfetch_vertex_wait", -391964, VFETCH_VERTEX_WAIT),
};

/* These tables provide a description of each performance counter
 * used by the shader cores counter model for energy estimation.
 */
static const struct kbase_ipa_counter ipa_shader_core_cntrs_def_todx[] = {
	SC_COUNTER_DEF("exec_instr_fma", 505449, EXEC_INSTR_FMA),
	SC_COUNTER_DEF("tex_filt_num_operations", 574869, TEX_FILT_NUM_OPS),
	SC_COUNTER_DEF("ls_mem_read_short", 60917, LS_MEM_READ_SHORT),
	SC_COUNTER_DEF("frag_quads_ezs_update", 694555, FRAG_QUADS_EZS_UPDATE),
	SC_COUNTER_DEF("ls_mem_write_short", 698290, LS_MEM_WRITE_SHORT),
	SC_COUNTER_DEF("vary_slot_16", 181069, VARY_SLOT_16),
};

static const struct kbase_ipa_counter ipa_shader_core_cntrs_def_tgrx[] = {
	SC_COUNTER_DEF("exec_instr_fma", 505449, EXEC_INSTR_FMA),
	SC_COUNTER_DEF("tex_filt_num_operations", 574869, TEX_FILT_NUM_OPS),
	SC_COUNTER_DEF("ls_mem_read_short", 60917, LS_MEM_READ_SHORT),
	SC_COUNTER_DEF("frag_quads_ezs_update", 694555, FRAG_QUADS_EZS_UPDATE),
	SC_COUNTER_DEF("ls_mem_write_short", 698290, LS_MEM_WRITE_SHORT),
	SC_COUNTER_DEF("vary_slot_16", 181069, VARY_SLOT_16),
};

static const struct kbase_ipa_counter ipa_shader_core_cntrs_def_tvax[] = {
	SC_COUNTER_DEF("tex_filt_num_operations", 142536, TEX_FILT_NUM_OPS),
	SC_COUNTER_DEF("exec_instr_fma", 243497, EXEC_INSTR_FMA),
	SC_COUNTER_DEF("exec_instr_msg", 1344410, EXEC_INSTR_MSG),
	SC_COUNTER_DEF("vary_slot_16", -119612, VARY_SLOT_16),
	SC_COUNTER_DEF("frag_partial_quads_rast", 676201, FRAG_PARTIAL_QUADS_RAST),
	SC_COUNTER_DEF("frag_starving", 62421, FRAG_STARVING),
};

static const struct kbase_ipa_counter ipa_shader_core_cntrs_def_ttux[] = {
	SC_COUNTER_DEF("exec_instr_fma", 457012, EXEC_INSTR_FMA),
	SC_COUNTER_DEF("tex_filt_num_operations", 441911, TEX_FILT_NUM_OPS),
	SC_COUNTER_DEF("ls_mem_read_short", 322525, LS_MEM_READ_SHORT),
	SC_COUNTER_DEF("full_quad_warps", 844124, FULL_QUAD_WARPS),
	SC_COUNTER_DEF("exec_instr_cvt", 226411, EXEC_INSTR_CVT),
	SC_COUNTER_DEF("frag_quads_ezs_update", 372032, FRAG_QUADS_EZS_UPDATE),
};

#define IPA_POWER_MODEL_OPS(gpu, init_token) \
	const struct kbase_ipa_model_ops kbase_ ## gpu ## _ipa_model_ops = { \
		.name = "mali-" #gpu "-power-model", \
		.init = kbase_ ## init_token ## _power_model_init, \
		.term = kbase_ipa_counter_common_model_term, \
		.get_dynamic_coeff = kbase_ipa_counter_dynamic_coeff, \
		.reset_counter_data = kbase_ipa_counter_reset_data, \
	}; \
	KBASE_EXPORT_TEST_API(kbase_ ## gpu ## _ipa_model_ops)

#define STANDARD_POWER_MODEL(gpu, reference_voltage) \
	static int kbase_ ## gpu ## _power_model_init(\
			struct kbase_ipa_model *model) \
	{ \
		BUILD_BUG_ON((1 + \
			      ARRAY_SIZE(ipa_top_level_cntrs_def_ ## gpu) +\
			      ARRAY_SIZE(ipa_shader_core_cntrs_def_ ## gpu)) > \
			      KBASE_IPA_MAX_COUNTER_DEF_NUM); \
		return kbase_ipa_counter_common_model_init(model, \
			ipa_top_level_cntrs_def_ ## gpu, \
			ARRAY_SIZE(ipa_top_level_cntrs_def_ ## gpu), \
			ipa_shader_core_cntrs_def_ ## gpu, \
			ARRAY_SIZE(ipa_shader_core_cntrs_def_ ## gpu), \
			(reference_voltage)); \
	} \
	IPA_POWER_MODEL_OPS(gpu, gpu)


#define ALIAS_POWER_MODEL(gpu, as_gpu) \
	IPA_POWER_MODEL_OPS(gpu, as_gpu)

/* Reference voltage value is 750 mV.
 */
STANDARD_POWER_MODEL(todx, 750);
STANDARD_POWER_MODEL(tgrx, 750);
STANDARD_POWER_MODEL(tvax, 750);

STANDARD_POWER_MODEL(ttux, 750);

/* Assuming LODX is an alias of TODX for IPA */
ALIAS_POWER_MODEL(lodx, todx);

/* Assuming LTUX is an alias of TTUX for IPA */
ALIAS_POWER_MODEL(ltux, ttux);

static const struct kbase_ipa_model_ops *ipa_counter_model_ops[] = {
	&kbase_todx_ipa_model_ops, &kbase_lodx_ipa_model_ops,
	&kbase_tgrx_ipa_model_ops, &kbase_tvax_ipa_model_ops,
	&kbase_ttux_ipa_model_ops, &kbase_ltux_ipa_model_ops
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
	const u32 prod_id =
		(gpu_id & GPU_ID_VERSION_PRODUCT_ID) >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	switch (GPU_ID2_MODEL_MATCH_VALUE(prod_id)) {
	case GPU_ID2_PRODUCT_TODX:
		return "mali-todx-power-model";
	case GPU_ID2_PRODUCT_LODX:
		return "mali-lodx-power-model";
	case GPU_ID2_PRODUCT_TGRX:
		return "mali-tgrx-power-model";
	case GPU_ID2_PRODUCT_TVAX:
		return "mali-tvax-power-model";
	case GPU_ID2_PRODUCT_TTUX:
		return "mali-ttux-power-model";
	case GPU_ID2_PRODUCT_LTUX:
		return "mali-ltux-power-model";
	default:
		return NULL;
	}
}
