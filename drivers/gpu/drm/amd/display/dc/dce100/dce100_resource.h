/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
 *
 */
/*
 * dce100_resource.h
 *
 *  Created on: 2016-01-20
 *      Author: qyang
 */

#ifndef DCE100_RESOURCE_H_
#define DCE100_RESOURCE_H_

struct dc;
struct resource_pool;
struct dc_validation_set;

struct resource_pool *dce100_create_resource_pool(
	uint8_t num_virtual_links,
	struct dc *dc);

enum dc_status dce100_validate_plane(const struct dc_plane_state *plane_state, struct dc_caps *caps);

enum dc_status dce100_add_stream_to_ctx(
		struct dc *dc,
		struct dc_state *new_ctx,
		struct dc_stream_state *dc_stream);

#endif /* DCE100_RESOURCE_H_ */
