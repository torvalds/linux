/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#ifndef AMDGPU_DM_AMDGPU_DM_REPLAY_H_
#define AMDGPU_DM_AMDGPU_DM_REPLAY_H_

#include "amdgpu.h"

enum replay_enable_option {
	pr_enable_option_static_screen = 0x1,
	pr_enable_option_mpo_video = 0x2,
	pr_enable_option_full_screen_video = 0x4,
	pr_enable_option_general_ui = 0x8,
	pr_enable_option_static_screen_coasting = 0x10000,
	pr_enable_option_mpo_video_coasting = 0x20000,
	pr_enable_option_full_screen_video_coasting = 0x40000,
};


bool amdgpu_dm_replay_enable(struct dc_stream_state *stream, bool enable);
bool amdgpu_dm_setup_replay(struct dc_link *link, struct amdgpu_dm_connector *aconnector);
bool amdgpu_dm_replay_disable(struct dc_stream_state *stream);

#endif /* AMDGPU_DM_AMDGPU_DM_REPLAY_H_ */
