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

#ifndef _DC_STREAM_PRIV_H_
#define _DC_STREAM_PRIV_H_

#include "dc_stream.h"

bool dc_stream_construct(struct dc_stream_state *stream,
	struct dc_sink *dc_sink_data);
void dc_stream_destruct(struct dc_stream_state *stream);

void dc_stream_assign_stream_id(struct dc_stream_state *stream);

/*
 * Finds the highest refresh rate that can be achieved
 * from starting_freq while staying within flicker criteria
 */
int dc_stream_calculate_max_flickerless_refresh_rate(struct dc_stream_state *stream,
						      int starting_refresh_hz,
						      bool is_gaming);

/*
 * Finds the lowest refresh rate that can be achieved
 * from starting_freq while staying within flicker criteria
 */
int dc_stream_calculate_min_flickerless_refresh_rate(struct dc_stream_state *stream,
						      int starting_refresh_hz,
						      bool is_gaming);

/*
 * Determines if there will be a flicker when moving between 2 refresh rates
 */
bool dc_stream_is_refresh_rate_range_flickerless(struct dc_stream_state *stream,
						  int hz1,
						  int hz2,
						  bool is_gaming);

/*
 * Determines the max instant vtotal delta increase that can be applied without
 * flickering for a given stream
 */
unsigned int dc_stream_get_max_flickerless_instant_vtotal_decrease(struct dc_stream_state *stream,
									  bool is_gaming);

/*
 * Determines the max instant vtotal delta decrease that can be applied without
 * flickering for a given stream
 */
unsigned int dc_stream_get_max_flickerless_instant_vtotal_increase(struct dc_stream_state *stream,
									  bool is_gaming);

#endif // _DC_STREAM_PRIV_H_
