/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_PIPE_STAGEDESC_H__
#define __IA_CSS_PIPE_STAGEDESC_H__

#include <ia_css_acc_types.h> /* ia_css_fw_info */
#include <ia_css_frame_public.h>
#include <ia_css_binary.h>
#include "ia_css_pipeline.h"
#include "ia_css_pipeline_common.h"

extern void ia_css_pipe_get_generic_stage_desc(
	struct ia_css_pipeline_stage_desc *stage_desc,
	struct ia_css_binary *binary,
	struct ia_css_frame *out_frame[],
	struct ia_css_frame *in_frame,
	struct ia_css_frame *vf_frame);

extern void ia_css_pipe_get_firmwares_stage_desc(
	struct ia_css_pipeline_stage_desc *stage_desc,
	struct ia_css_binary *binary,
	struct ia_css_frame *out_frame[],
	struct ia_css_frame *in_frame,
	struct ia_css_frame *vf_frame,
	const struct ia_css_fw_info *fw,
	unsigned int mode);

extern void ia_css_pipe_get_acc_stage_desc(
	struct ia_css_pipeline_stage_desc *stage_desc,
	struct ia_css_binary *binary,
	struct ia_css_fw_info *fw);

extern void ia_css_pipe_get_sp_func_stage_desc(
	struct ia_css_pipeline_stage_desc *stage_desc,
	struct ia_css_frame *out_frame,
	enum ia_css_pipeline_stage_sp_func sp_func,
	unsigned max_input_width);

#endif /*__IA_CSS_PIPE_STAGEDESC__H__ */

