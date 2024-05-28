/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_CLIENT_CHECK_H
#define PVR_ROGUE_FWIF_CLIENT_CHECK_H

#include <linux/build_bug.h>

#define OFFSET_CHECK(type, member, offset) \
	static_assert(offsetof(type, member) == (offset), \
		      "offsetof(" #type ", " #member ") incorrect")

#define SIZE_CHECK(type, size) \
	static_assert(sizeof(type) == (size), #type " is incorrect size")

OFFSET_CHECK(struct rogue_fwif_geom_regs, vdm_ctrl_stream_base, 0);
OFFSET_CHECK(struct rogue_fwif_geom_regs, tpu_border_colour_table, 8);
OFFSET_CHECK(struct rogue_fwif_geom_regs, vdm_draw_indirect0, 16);
OFFSET_CHECK(struct rogue_fwif_geom_regs, vdm_draw_indirect1, 24);
OFFSET_CHECK(struct rogue_fwif_geom_regs, ppp_ctrl, 28);
OFFSET_CHECK(struct rogue_fwif_geom_regs, te_psg, 32);
OFFSET_CHECK(struct rogue_fwif_geom_regs, tpu, 36);
OFFSET_CHECK(struct rogue_fwif_geom_regs, vdm_context_resume_task0_size, 40);
OFFSET_CHECK(struct rogue_fwif_geom_regs, vdm_context_resume_task3_size, 44);
OFFSET_CHECK(struct rogue_fwif_geom_regs, pds_ctrl, 48);
OFFSET_CHECK(struct rogue_fwif_geom_regs, view_idx, 52);
OFFSET_CHECK(struct rogue_fwif_geom_regs, pds_coeff_free_prog, 56);
SIZE_CHECK(struct rogue_fwif_geom_regs, 64);

OFFSET_CHECK(struct rogue_fwif_dummy_rgnhdr_init_geom_regs, te_psgregion_addr, 0);
SIZE_CHECK(struct rogue_fwif_dummy_rgnhdr_init_geom_regs, 8);

OFFSET_CHECK(struct rogue_fwif_cmd_geom, cmd_shared, 0);
OFFSET_CHECK(struct rogue_fwif_cmd_geom, regs, 16);
OFFSET_CHECK(struct rogue_fwif_cmd_geom, flags, 80);
OFFSET_CHECK(struct rogue_fwif_cmd_geom, partial_render_geom_frag_fence, 84);
OFFSET_CHECK(struct rogue_fwif_cmd_geom, dummy_rgnhdr_init_geom_regs, 96);
OFFSET_CHECK(struct rogue_fwif_cmd_geom, brn61484_66333_live_rt, 104);
SIZE_CHECK(struct rogue_fwif_cmd_geom, 112);

OFFSET_CHECK(struct rogue_fwif_frag_regs, usc_pixel_output_ctrl, 0);
OFFSET_CHECK(struct rogue_fwif_frag_regs, usc_clear_register, 4);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_bgobjdepth, 36);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_bgobjvals, 40);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_aa, 44);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_xtp_pipe_enable, 48);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_ctl, 52);
OFFSET_CHECK(struct rogue_fwif_frag_regs, tpu, 56);
OFFSET_CHECK(struct rogue_fwif_frag_regs, event_pixel_pds_info, 60);
OFFSET_CHECK(struct rogue_fwif_frag_regs, pixel_phantom, 64);
OFFSET_CHECK(struct rogue_fwif_frag_regs, view_idx, 68);
OFFSET_CHECK(struct rogue_fwif_frag_regs, event_pixel_pds_data, 72);
OFFSET_CHECK(struct rogue_fwif_frag_regs, brn65101_event_pixel_pds_data, 76);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_oclqry_stride, 80);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_zls_pixels, 84);
OFFSET_CHECK(struct rogue_fwif_frag_regs, rgx_cr_blackpearl_fix, 88);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_scissor_base, 96);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_dbias_base, 104);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_oclqry_base, 112);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_zlsctl, 120);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_zload_store_base, 128);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_stencil_load_store_base, 136);
OFFSET_CHECK(struct rogue_fwif_frag_regs, fb_cdc_zls, 144);
OFFSET_CHECK(struct rogue_fwif_frag_regs, pbe_word, 152);
OFFSET_CHECK(struct rogue_fwif_frag_regs, tpu_border_colour_table, 344);
OFFSET_CHECK(struct rogue_fwif_frag_regs, pds_bgnd, 352);
OFFSET_CHECK(struct rogue_fwif_frag_regs, pds_bgnd_brn65101, 376);
OFFSET_CHECK(struct rogue_fwif_frag_regs, pds_pr_bgnd, 400);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_dummy_stencil_store_base, 424);
OFFSET_CHECK(struct rogue_fwif_frag_regs, isp_dummy_depth_store_base, 432);
OFFSET_CHECK(struct rogue_fwif_frag_regs, rgnhdr_single_rt_size, 440);
OFFSET_CHECK(struct rogue_fwif_frag_regs, rgnhdr_scratch_offset, 444);
SIZE_CHECK(struct rogue_fwif_frag_regs, 448);

OFFSET_CHECK(struct rogue_fwif_cmd_frag, cmd_shared, 0);
OFFSET_CHECK(struct rogue_fwif_cmd_frag, regs, 16);
OFFSET_CHECK(struct rogue_fwif_cmd_frag, flags, 464);
OFFSET_CHECK(struct rogue_fwif_cmd_frag, zls_stride, 468);
OFFSET_CHECK(struct rogue_fwif_cmd_frag, sls_stride, 472);
OFFSET_CHECK(struct rogue_fwif_cmd_frag, execute_count, 476);
SIZE_CHECK(struct rogue_fwif_cmd_frag, 480);

OFFSET_CHECK(struct rogue_fwif_compute_regs, tpu_border_colour_table, 0);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_cb_queue, 8);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_cb_base, 16);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_cb, 24);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_ctrl_stream_base, 32);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_context_state_base_addr, 40);
OFFSET_CHECK(struct rogue_fwif_compute_regs, tpu, 48);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_resume_pds1, 52);
OFFSET_CHECK(struct rogue_fwif_compute_regs, cdm_item, 56);
OFFSET_CHECK(struct rogue_fwif_compute_regs, compute_cluster, 60);
OFFSET_CHECK(struct rogue_fwif_compute_regs, tpu_tag_cdm_ctrl, 64);
SIZE_CHECK(struct rogue_fwif_compute_regs, 72);

OFFSET_CHECK(struct rogue_fwif_cmd_compute, common, 0);
OFFSET_CHECK(struct rogue_fwif_cmd_compute, regs, 8);
OFFSET_CHECK(struct rogue_fwif_cmd_compute, flags, 80);
OFFSET_CHECK(struct rogue_fwif_cmd_compute, num_temp_regions, 84);
OFFSET_CHECK(struct rogue_fwif_cmd_compute, stream_start_offset, 88);
OFFSET_CHECK(struct rogue_fwif_cmd_compute, execute_count, 92);
SIZE_CHECK(struct rogue_fwif_cmd_compute, 96);

OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_bgobjvals, 0);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, usc_pixel_output_ctrl, 4);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, usc_clear_register0, 8);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, usc_clear_register1, 12);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, usc_clear_register2, 16);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, usc_clear_register3, 20);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_mtile_size, 24);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_render_origin, 28);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_ctl, 32);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_xtp_pipe_enable, 36);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_aa, 40);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, event_pixel_pds_info, 44);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, event_pixel_pds_code, 48);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, event_pixel_pds_data, 52);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_render, 56);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_rgn, 60);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, frag_screen, 64);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, pds_bgnd0_base, 72);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, pds_bgnd1_base, 80);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, pds_bgnd3_sizeinfo, 88);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, isp_mtile_base, 96);
OFFSET_CHECK(struct rogue_fwif_transfer_regs, pbe_wordx_mrty, 104);
SIZE_CHECK(struct rogue_fwif_transfer_regs, 176);

OFFSET_CHECK(struct rogue_fwif_cmd_transfer, common, 0);
OFFSET_CHECK(struct rogue_fwif_cmd_transfer, regs, 8);
OFFSET_CHECK(struct rogue_fwif_cmd_transfer, flags, 184);
SIZE_CHECK(struct rogue_fwif_cmd_transfer, 192);

#endif /* PVR_ROGUE_FWIF_CLIENT_CHECK_H */
