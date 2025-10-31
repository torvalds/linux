/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DML2_DC_RESOURCE_MGMT_H__
#define __DML2_DC_RESOURCE_MGMT_H__

#include "dml2_dc_types.h"

struct dml2_context;
struct dml2_dml_to_dc_pipe_mapping;
struct dml_display_cfg_st;

/*
 * dml2_map_dc_pipes - Creates a pipe linkage in dc_state based on current display config.
 * @ctx: Input dml2 context
 * @state: Current dc_state to be updated.
 * @disp_cfg: Current display config.
 * @mapping: Pipe mapping logic structure to keep a track of pipes to be used.
 *
 * Based on ODM and DPPPersurface outputs calculated by the DML for the current display
 * config, create a pipe linkage in dc_state which is then used by DC core.
 * Make this function generic to be used by multiple DML versions.
 *
 * Return: True if pipe mapping and linking is successful, false otherwise.
 */

bool dml2_map_dc_pipes(struct dml2_context *ctx, struct dc_state *state, const struct dml_display_cfg_st *disp_cfg, struct dml2_dml_to_dc_pipe_mapping *mapping, const struct dc_state *existing_state);

#endif
