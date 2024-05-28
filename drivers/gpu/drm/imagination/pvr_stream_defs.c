// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device_info.h"
#include "pvr_rogue_fwif_client.h"
#include "pvr_rogue_fwif_stream.h"
#include "pvr_stream.h"
#include "pvr_stream_defs.h"

#include <linux/stddef.h>
#include <uapi/drm/pvr_drm.h>

#define PVR_STREAM_DEF_SET(owner, member, _size, _array_size, _feature) \
	{ .offset = offsetof(struct owner, member), \
	  .size = (_size),  \
	  .array_size = (_array_size), \
	  .feature = (_feature) }

#define PVR_STREAM_DEF(owner, member, member_size)  \
	PVR_STREAM_DEF_SET(owner, member, PVR_STREAM_SIZE_ ## member_size, 0, PVR_FEATURE_NONE)

#define PVR_STREAM_DEF_FEATURE(owner, member, member_size, feature) \
	PVR_STREAM_DEF_SET(owner, member, PVR_STREAM_SIZE_ ## member_size, 0, feature)

#define PVR_STREAM_DEF_NOT_FEATURE(owner, member, member_size, feature)       \
	PVR_STREAM_DEF_SET(owner, member, PVR_STREAM_SIZE_ ## member_size, 0, \
			   (feature) | PVR_FEATURE_NOT)

#define PVR_STREAM_DEF_ARRAY(owner, member)                                       \
	PVR_STREAM_DEF_SET(owner, member, PVR_STREAM_SIZE_ARRAY,                  \
			   sizeof(((struct owner *)0)->member), PVR_FEATURE_NONE)

#define PVR_STREAM_DEF_ARRAY_FEATURE(owner, member, feature)            \
	PVR_STREAM_DEF_SET(owner, member, PVR_STREAM_SIZE_ARRAY,         \
			   sizeof(((struct owner *)0)->member), feature)

#define PVR_STREAM_DEF_ARRAY_NOT_FEATURE(owner, member, feature)                             \
	PVR_STREAM_DEF_SET(owner, member, PVR_STREAM_SIZE_ARRAY,                             \
			   sizeof(((struct owner *)0)->member), (feature) | PVR_FEATURE_NOT)

/*
 * When adding new parameters to the stream definition, the new parameters must go after the
 * existing parameters, to preserve order. As parameters are naturally aligned, care must be taken
 * with respect to implicit padding in the stream; padding should be minimised as much as possible.
 */
static const struct pvr_stream_def rogue_fwif_cmd_geom_stream[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.vdm_ctrl_stream_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.tpu_border_colour_table, 64),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_geom, regs.vdm_draw_indirect0, 64,
			       PVR_FEATURE_VDM_DRAWINDIRECT),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_geom, regs.vdm_draw_indirect1, 32,
			       PVR_FEATURE_VDM_DRAWINDIRECT),
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.ppp_ctrl, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.te_psg, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.vdm_context_resume_task0_size, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_geom, regs.vdm_context_resume_task3_size, 32,
			       PVR_FEATURE_VDM_OBJECT_LEVEL_LLS),
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.view_idx, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_geom, regs.pds_coeff_free_prog, 32,
			       PVR_FEATURE_TESSELLATION),
};

static const struct pvr_stream_def rogue_fwif_cmd_geom_stream_brn49927[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_geom, regs.tpu, 32),
};

static const struct pvr_stream_ext_def cmd_geom_ext_streams_0[] = {
	{
		.stream = rogue_fwif_cmd_geom_stream_brn49927,
		.stream_len = ARRAY_SIZE(rogue_fwif_cmd_geom_stream_brn49927),
		.header_mask = PVR_STREAM_EXTHDR_GEOM0_BRN49927,
		.quirk = 49927,
	},
};

static const struct pvr_stream_ext_header cmd_geom_ext_headers[] = {
	{
		.ext_streams = cmd_geom_ext_streams_0,
		.ext_streams_num = ARRAY_SIZE(cmd_geom_ext_streams_0),
		.valid_mask = PVR_STREAM_EXTHDR_GEOM0_VALID,
	},
};

const struct pvr_stream_cmd_defs pvr_cmd_geom_stream = {
	.type = PVR_STREAM_TYPE_GEOM,

	.main_stream = rogue_fwif_cmd_geom_stream,
	.main_stream_len = ARRAY_SIZE(rogue_fwif_cmd_geom_stream),

	.ext_nr_headers = ARRAY_SIZE(cmd_geom_ext_headers),
	.ext_headers = cmd_geom_ext_headers,

	.dest_size = sizeof(struct rogue_fwif_cmd_geom),
};

static const struct pvr_stream_def rogue_fwif_cmd_frag_stream[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_scissor_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_dbias_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_oclqry_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_zlsctl, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_zload_store_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_stencil_load_store_base, 64),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, regs.fb_cdc_zls, 64,
			       PVR_FEATURE_REQUIRES_FB_CDC_ZLS_SETUP),
	PVR_STREAM_DEF_ARRAY(rogue_fwif_cmd_frag, regs.pbe_word),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.tpu_border_colour_table, 64),
	PVR_STREAM_DEF_ARRAY(rogue_fwif_cmd_frag, regs.pds_bgnd),
	PVR_STREAM_DEF_ARRAY(rogue_fwif_cmd_frag, regs.pds_pr_bgnd),
	PVR_STREAM_DEF_ARRAY(rogue_fwif_cmd_frag, regs.usc_clear_register),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.usc_pixel_output_ctrl, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_bgobjdepth, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_bgobjvals, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_aa, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, regs.isp_xtp_pipe_enable, 32,
			       PVR_FEATURE_S7_TOP_INFRASTRUCTURE),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_ctl, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.event_pixel_pds_info, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, regs.pixel_phantom, 32,
			       PVR_FEATURE_CLUSTER_GROUPING),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.view_idx, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.event_pixel_pds_data, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, regs.isp_oclqry_stride, 32,
			       PVR_FEATURE_GPU_MULTICORE_SUPPORT),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, regs.isp_zls_pixels, 32,
			       PVR_FEATURE_ZLS_SUBTILE),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, regs.rgx_cr_blackpearl_fix, 32,
			       PVR_FEATURE_ISP_ZLS_D24_S8_PACKING_OGL_MODE),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, zls_stride, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, sls_stride, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_frag, execute_count, 32,
			       PVR_FEATURE_GPU_MULTICORE_SUPPORT),
};

static const struct pvr_stream_def rogue_fwif_cmd_frag_stream_brn47217[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.isp_oclqry_stride, 32),
};

static const struct pvr_stream_def rogue_fwif_cmd_frag_stream_brn49927[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_frag, regs.tpu, 32),
};

static const struct pvr_stream_ext_def cmd_frag_ext_streams_0[] = {
	{
		.stream = rogue_fwif_cmd_frag_stream_brn47217,
		.stream_len = ARRAY_SIZE(rogue_fwif_cmd_frag_stream_brn47217),
		.header_mask = PVR_STREAM_EXTHDR_FRAG0_BRN47217,
		.quirk = 47217,
	},
	{
		.stream = rogue_fwif_cmd_frag_stream_brn49927,
		.stream_len = ARRAY_SIZE(rogue_fwif_cmd_frag_stream_brn49927),
		.header_mask = PVR_STREAM_EXTHDR_FRAG0_BRN49927,
		.quirk = 49927,
	},
};

static const struct pvr_stream_ext_header cmd_frag_ext_headers[] = {
	{
		.ext_streams = cmd_frag_ext_streams_0,
		.ext_streams_num = ARRAY_SIZE(cmd_frag_ext_streams_0),
		.valid_mask = PVR_STREAM_EXTHDR_FRAG0_VALID,
	},
};

const struct pvr_stream_cmd_defs pvr_cmd_frag_stream = {
	.type = PVR_STREAM_TYPE_FRAG,

	.main_stream = rogue_fwif_cmd_frag_stream,
	.main_stream_len = ARRAY_SIZE(rogue_fwif_cmd_frag_stream),

	.ext_nr_headers = ARRAY_SIZE(cmd_frag_ext_headers),
	.ext_headers = cmd_frag_ext_headers,

	.dest_size = sizeof(struct rogue_fwif_cmd_frag),
};

static const struct pvr_stream_def rogue_fwif_cmd_compute_stream[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_compute, regs.tpu_border_colour_table, 64),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, regs.cdm_cb_queue, 64,
			       PVR_FEATURE_CDM_USER_MODE_QUEUE),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, regs.cdm_cb_base, 64,
			       PVR_FEATURE_CDM_USER_MODE_QUEUE),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, regs.cdm_cb, 64,
			       PVR_FEATURE_CDM_USER_MODE_QUEUE),
	PVR_STREAM_DEF_NOT_FEATURE(rogue_fwif_cmd_compute, regs.cdm_ctrl_stream_base, 64,
				   PVR_FEATURE_CDM_USER_MODE_QUEUE),
	PVR_STREAM_DEF(rogue_fwif_cmd_compute, regs.cdm_context_state_base_addr, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_compute, regs.cdm_resume_pds1, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, regs.cdm_item, 32,
			       PVR_FEATURE_COMPUTE_MORTON_CAPABLE),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, regs.compute_cluster, 32,
			       PVR_FEATURE_CLUSTER_GROUPING),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, regs.tpu_tag_cdm_ctrl, 32,
			       PVR_FEATURE_TPU_DM_GLOBAL_REGISTERS),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, stream_start_offset, 32,
			       PVR_FEATURE_CDM_USER_MODE_QUEUE),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_compute, execute_count, 32,
			       PVR_FEATURE_GPU_MULTICORE_SUPPORT),
};

static const struct pvr_stream_def rogue_fwif_cmd_compute_stream_brn49927[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_compute, regs.tpu, 32),
};

static const struct pvr_stream_ext_def cmd_compute_ext_streams_0[] = {
	{
		.stream = rogue_fwif_cmd_compute_stream_brn49927,
		.stream_len = ARRAY_SIZE(rogue_fwif_cmd_compute_stream_brn49927),
		.header_mask = PVR_STREAM_EXTHDR_COMPUTE0_BRN49927,
		.quirk = 49927,
	},
};

static const struct pvr_stream_ext_header cmd_compute_ext_headers[] = {
	{
		.ext_streams = cmd_compute_ext_streams_0,
		.ext_streams_num = ARRAY_SIZE(cmd_compute_ext_streams_0),
		.valid_mask = PVR_STREAM_EXTHDR_COMPUTE0_VALID,
	},
};

const struct pvr_stream_cmd_defs pvr_cmd_compute_stream = {
	.type = PVR_STREAM_TYPE_COMPUTE,

	.main_stream = rogue_fwif_cmd_compute_stream,
	.main_stream_len = ARRAY_SIZE(rogue_fwif_cmd_compute_stream),

	.ext_nr_headers = ARRAY_SIZE(cmd_compute_ext_headers),
	.ext_headers = cmd_compute_ext_headers,

	.dest_size = sizeof(struct rogue_fwif_cmd_compute),
};

static const struct pvr_stream_def rogue_fwif_cmd_transfer_stream[] = {
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.pds_bgnd0_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.pds_bgnd1_base, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.pds_bgnd3_sizeinfo, 64),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_mtile_base, 64),
	PVR_STREAM_DEF_ARRAY(rogue_fwif_cmd_transfer, regs.pbe_wordx_mrty),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_bgobjvals, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.usc_pixel_output_ctrl, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.usc_clear_register0, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.usc_clear_register1, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.usc_clear_register2, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.usc_clear_register3, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_mtile_size, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_render_origin, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_ctl, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_aa, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.event_pixel_pds_info, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.event_pixel_pds_code, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.event_pixel_pds_data, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_render, 32),
	PVR_STREAM_DEF(rogue_fwif_cmd_transfer, regs.isp_rgn, 32),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_transfer, regs.isp_xtp_pipe_enable, 32,
			       PVR_FEATURE_S7_TOP_INFRASTRUCTURE),
	PVR_STREAM_DEF_FEATURE(rogue_fwif_cmd_transfer, regs.frag_screen, 32,
			       PVR_FEATURE_GPU_MULTICORE_SUPPORT),
};

const struct pvr_stream_cmd_defs pvr_cmd_transfer_stream = {
	.type = PVR_STREAM_TYPE_TRANSFER,

	.main_stream = rogue_fwif_cmd_transfer_stream,
	.main_stream_len = ARRAY_SIZE(rogue_fwif_cmd_transfer_stream),

	.ext_nr_headers = 0,

	.dest_size = sizeof(struct rogue_fwif_cmd_transfer),
};

static const struct pvr_stream_def rogue_fwif_static_render_context_state_stream[] = {
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_reg_vdm_context_state_base_addr, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_reg_vdm_context_state_resume_addr, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_reg_ta_context_state_base_addr, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_store_task0, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_store_task1, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_store_task2, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_store_task3, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_store_task4, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_resume_task0, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_resume_task1, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_resume_task2, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_resume_task3, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[0].geom_reg_vdm_context_resume_task4, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_store_task0, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_store_task1, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_store_task2, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_store_task3, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_store_task4, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_resume_task0, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_resume_task1, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_resume_task2, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_resume_task3, 64),
	PVR_STREAM_DEF(rogue_fwif_geom_registers_caswitch,
		       geom_state[1].geom_reg_vdm_context_resume_task4, 64),
};

const struct pvr_stream_cmd_defs pvr_static_render_context_state_stream = {
	.type = PVR_STREAM_TYPE_STATIC_RENDER_CONTEXT,

	.main_stream = rogue_fwif_static_render_context_state_stream,
	.main_stream_len = ARRAY_SIZE(rogue_fwif_static_render_context_state_stream),

	.ext_nr_headers = 0,

	.dest_size = sizeof(struct rogue_fwif_geom_registers_caswitch),
};

static const struct pvr_stream_def rogue_fwif_static_compute_context_state_stream[] = {
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_context_pds0, 64),
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_context_pds1, 64),
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_terminate_pds, 64),
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_terminate_pds1, 64),
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_resume_pds0, 64),
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_context_pds0_b, 64),
	PVR_STREAM_DEF(rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_resume_pds0_b, 64),
};

const struct pvr_stream_cmd_defs pvr_static_compute_context_state_stream = {
	.type = PVR_STREAM_TYPE_STATIC_COMPUTE_CONTEXT,

	.main_stream = rogue_fwif_static_compute_context_state_stream,
	.main_stream_len = ARRAY_SIZE(rogue_fwif_static_compute_context_state_stream),

	.ext_nr_headers = 0,

	.dest_size = sizeof(struct rogue_fwif_cdm_registers_cswitch),
};
