/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __AMDGPU_DM_IRQ_PARAMS_H__
#define __AMDGPU_DM_IRQ_PARAMS_H__

#include "amdgpu_dm_crc.h"

struct dm_irq_params {
	u32 last_flip_vblank;
	struct mod_vrr_params vrr_params;
	struct dc_stream_state *stream;
	int active_planes;
	struct mod_freesync_config freesync_config;

#ifdef CONFIG_DEBUG_FS
	enum amdgpu_dm_pipe_crc_source crc_src;
#ifdef CONFIG_DRM_AMD_SECURE_DISPLAY
	struct crc_window_parm crc_window;
#endif
#endif
};

#endif /* __AMDGPU_DM_IRQ_PARAMS_H__ */
