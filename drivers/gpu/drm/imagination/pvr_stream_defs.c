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
