/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Linaro Ltd. */
#ifndef __VENUS_HFI_PARSER_H__
#define __VENUS_HFI_PARSER_H__

#include "core.h"

u32 hfi_parser(struct venus_core *core, struct venus_inst *inst,
	       void *buf, u32 size);

#define WHICH_CAP_MIN	0
#define WHICH_CAP_MAX	1
#define WHICH_CAP_STEP	2

static inline u32 get_cap(struct venus_inst *inst, u32 type, u32 which)
{
	struct venus_core *core = inst->core;
	struct hfi_capability *cap = NULL;
	struct venus_caps *caps;
	unsigned int i;

	caps = venus_caps_by_codec(core, inst->hfi_codec, inst->session_type);
	if (!caps)
		return 0;

	for (i = 0; i < caps->num_caps; i++) {
		if (caps->caps[i].capability_type == type) {
			cap = &caps->caps[i];
			break;
		}
	}

	if (!cap)
		return 0;

	switch (which) {
	case WHICH_CAP_MIN:
		return cap->min;
	case WHICH_CAP_MAX:
		return cap->max;
	case WHICH_CAP_STEP:
		return cap->step_size;
	default:
		break;
	}

	return 0;
}

static inline u32 cap_min(struct venus_inst *inst, u32 type)
{
	return get_cap(inst, type, WHICH_CAP_MIN);
}

static inline u32 cap_max(struct venus_inst *inst, u32 type)
{
	return get_cap(inst, type, WHICH_CAP_MAX);
}

static inline u32 cap_step(struct venus_inst *inst, u32 type)
{
	return get_cap(inst, type, WHICH_CAP_STEP);
}

static inline u32 frame_width_min(struct venus_inst *inst)
{
	return cap_min(inst, HFI_CAPABILITY_FRAME_WIDTH);
}

static inline u32 frame_width_max(struct venus_inst *inst)
{
	return cap_max(inst, HFI_CAPABILITY_FRAME_WIDTH);
}

static inline u32 frame_width_step(struct venus_inst *inst)
{
	return cap_step(inst, HFI_CAPABILITY_FRAME_WIDTH);
}

static inline u32 frame_height_min(struct venus_inst *inst)
{
	return cap_min(inst, HFI_CAPABILITY_FRAME_HEIGHT);
}

static inline u32 frame_height_max(struct venus_inst *inst)
{
	return cap_max(inst, HFI_CAPABILITY_FRAME_HEIGHT);
}

static inline u32 frame_height_step(struct venus_inst *inst)
{
	return cap_step(inst, HFI_CAPABILITY_FRAME_HEIGHT);
}

static inline u32 frate_min(struct venus_inst *inst)
{
	return cap_min(inst, HFI_CAPABILITY_FRAMERATE);
}

static inline u32 frate_max(struct venus_inst *inst)
{
	return cap_max(inst, HFI_CAPABILITY_FRAMERATE);
}

static inline u32 frate_step(struct venus_inst *inst)
{
	return cap_step(inst, HFI_CAPABILITY_FRAMERATE);
}

#endif
