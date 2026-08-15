/* SPDX-License-Identifier: MIT */
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

#ifndef AMDGPU_DM_AMDGPU_DM_PSR_H_
#define AMDGPU_DM_AMDGPU_DM_PSR_H_

#include "dc.h"
#include "modules/inc/mod_power.h"

struct amdgpu_display_manager;
struct amdgpu_dm_connector;

/* the number of pageflips before enabling psr */
#define AMDGPU_DM_PSR_ENTRY_DELAY 5

bool amdgpu_dm_set_psr_caps(struct dc_link *link, struct amdgpu_dm_connector *aconnector);
bool amdgpu_dm_psr_is_active_allowed(struct amdgpu_display_manager *dm);
bool amdgpu_dm_psr_set_event(struct amdgpu_display_manager *dm,
		struct dc_stream_state *stream, bool set_event,	enum psr_event event,
		bool wait_for_disable);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
void amdgpu_dm_psr_fill_caps(struct dc_link *link, struct psr_caps *caps);
#endif

#endif /* AMDGPU_DM_AMDGPU_DM_PSR_H_ */
