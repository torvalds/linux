/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_SHARED_CHECK_H
#define PVR_ROGUE_FWIF_SHARED_CHECK_H

#include <linux/build_bug.h>

#define OFFSET_CHECK(type, member, offset) \
	static_assert(offsetof(type, member) == (offset), \
		      "offsetof(" #type ", " #member ") incorrect")

#define SIZE_CHECK(type, size) \
	static_assert(sizeof(type) == (size), #type " is incorrect size")

OFFSET_CHECK(struct rogue_fwif_dma_addr, dev_addr, 0);
OFFSET_CHECK(struct rogue_fwif_dma_addr, fw_addr, 8);
SIZE_CHECK(struct rogue_fwif_dma_addr, 16);

OFFSET_CHECK(struct rogue_fwif_ufo, addr, 0);
OFFSET_CHECK(struct rogue_fwif_ufo, value, 4);
SIZE_CHECK(struct rogue_fwif_ufo, 8);

OFFSET_CHECK(struct rogue_fwif_cleanup_ctl, submitted_commands, 0);
OFFSET_CHECK(struct rogue_fwif_cleanup_ctl, executed_commands, 4);
SIZE_CHECK(struct rogue_fwif_cleanup_ctl, 8);

OFFSET_CHECK(struct rogue_fwif_cccb_ctl, write_offset, 0);
OFFSET_CHECK(struct rogue_fwif_cccb_ctl, read_offset, 4);
OFFSET_CHECK(struct rogue_fwif_cccb_ctl, dep_offset, 8);
OFFSET_CHECK(struct rogue_fwif_cccb_ctl, wrap_mask, 12);
OFFSET_CHECK(struct rogue_fwif_cccb_ctl, read_offset2, 16);
OFFSET_CHECK(struct rogue_fwif_cccb_ctl, read_offset3, 20);
OFFSET_CHECK(struct rogue_fwif_cccb_ctl, read_offset4, 24);
SIZE_CHECK(struct rogue_fwif_cccb_ctl, 32);

OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_reg_vdm_context_state_base_addr, 0);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_reg_vdm_context_state_resume_addr, 8);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_reg_ta_context_state_base_addr, 16);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_store_task0, 24);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_store_task1, 32);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_store_task2, 40);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_resume_task0, 48);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_resume_task1, 56);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_resume_task2, 64);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_store_task3, 72);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_store_task4, 80);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_resume_task3, 88);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[0].geom_reg_vdm_context_resume_task4, 96);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_store_task0, 104);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_store_task1, 112);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_store_task2, 120);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_resume_task0, 128);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_resume_task1, 136);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_resume_task2, 144);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_store_task3, 152);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_store_task4, 160);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_resume_task3, 168);
OFFSET_CHECK(struct rogue_fwif_geom_registers_caswitch,
	     geom_state[1].geom_reg_vdm_context_resume_task4, 176);
SIZE_CHECK(struct rogue_fwif_geom_registers_caswitch, 184);

OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_context_pds0, 0);
OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_context_pds1, 8);
OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_terminate_pds, 16);
OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_terminate_pds1, 24);
OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_resume_pds0, 32);
OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_context_pds0_b, 40);
OFFSET_CHECK(struct rogue_fwif_cdm_registers_cswitch, cdmreg_cdm_resume_pds0_b, 48);
SIZE_CHECK(struct rogue_fwif_cdm_registers_cswitch, 56);

OFFSET_CHECK(struct rogue_fwif_static_rendercontext_state, ctxswitch_regs, 0);
SIZE_CHECK(struct rogue_fwif_static_rendercontext_state, 368);

OFFSET_CHECK(struct rogue_fwif_static_computecontext_state, ctxswitch_regs, 0);
SIZE_CHECK(struct rogue_fwif_static_computecontext_state, 56);

OFFSET_CHECK(struct rogue_fwif_cmd_common, frame_num, 0);
SIZE_CHECK(struct rogue_fwif_cmd_common, 4);

OFFSET_CHECK(struct rogue_fwif_cmd_geom_frag_shared, cmn, 0);
OFFSET_CHECK(struct rogue_fwif_cmd_geom_frag_shared, hwrt_data_fw_addr, 4);
OFFSET_CHECK(struct rogue_fwif_cmd_geom_frag_shared, pr_buffer_fw_addr, 8);
SIZE_CHECK(struct rogue_fwif_cmd_geom_frag_shared, 16);

#endif /* PVR_ROGUE_FWIF_SHARED_CHECK_H */
