/*
* Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __DC_RESOURCE_DCN10_H__
#define __DC_RESOURCE_DCN10_H__

#include "core_types.h"
#include "dml/dcn10/dcn10_fpu.h"

#define TO_DCN10_RES_POOL(pool)\
	container_of(pool, struct dcn10_resource_pool, base)

struct dc;
struct resource_pool;
struct _vcs_dpi_display_pipe_params_st;

extern struct _vcs_dpi_ip_params_st dcn1_0_ip;
extern struct _vcs_dpi_soc_bounding_box_st dcn1_0_soc;

struct dcn10_resource_pool {
	struct resource_pool base;
};
struct resource_pool *dcn10_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

struct stream_encoder *dcn10_find_first_free_match_stream_enc_for_link(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream);


#endif /* __DC_RESOURCE_DCN10_H__ */

