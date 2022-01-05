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

#ifndef DC_INC_LINK_ENC_CFG_H_
#define DC_INC_LINK_ENC_CFG_H_

/* This module implements functionality for dynamically assigning DIG link
 * encoder resources to display endpoints (links).
 */

#include "core_types.h"

/*
 * Initialise link encoder resource tracking.
 */
void link_enc_cfg_init(
		struct dc *dc,
		struct dc_state *state);

/*
 * Algorithm for assigning available DIG link encoders to streams.
 *
 * Update link_enc_assignments table and link_enc_avail list accordingly in
 * struct resource_context.
 *
 * Loop over all streams twice:
 * a) First assign encoders to unmappable endpoints.
 * b) Then assign encoders to mappable endpoints.
 */
void link_enc_cfg_link_encs_assign(
		struct dc *dc,
		struct dc_state *state,
		struct dc_stream_state *streams[],
		uint8_t stream_count);

/*
 * Unassign a link encoder from a stream.
 *
 * Update link_enc_assignments table and link_enc_avail list accordingly in
 * struct resource_context.
 */
void link_enc_cfg_link_enc_unassign(
		struct dc_state *state,
		struct dc_stream_state *stream);

/*
 * Check whether the transmitter driven by a link encoder is a mappable
 * endpoint.
 */
bool link_enc_cfg_is_transmitter_mappable(
		struct dc *dc,
		struct link_encoder *link_enc);

/* Return stream using DIG link encoder resource. NULL if unused. */
struct dc_stream_state *link_enc_cfg_get_stream_using_link_enc(
		struct dc *dc,
		enum engine_id eng_id);

/* Return link using DIG link encoder resource. NULL if unused. */
struct dc_link *link_enc_cfg_get_link_using_link_enc(
		struct dc *dc,
		enum engine_id eng_id);

/* Return DIG link encoder used by link. NULL if unused. */
struct link_encoder *link_enc_cfg_get_link_enc_used_by_link(
		struct dc *dc,
		const struct dc_link *link);

/* Return next available DIG link encoder. NULL if none available. */
struct link_encoder *link_enc_cfg_get_next_avail_link_enc(struct dc *dc);

/* Return DIG link encoder used by stream. NULL if unused. */
struct link_encoder *link_enc_cfg_get_link_enc_used_by_stream(
		struct dc *dc,
		const struct dc_stream_state *stream);

/* Return true if encoder available to use. */
bool link_enc_cfg_is_link_enc_avail(struct dc *dc, enum engine_id eng_id, struct dc_link *link);

/* Returns true if encoder assignments in supplied state pass validity checks. */
bool link_enc_cfg_validate(struct dc *dc, struct dc_state *state);

#endif /* DC_INC_LINK_ENC_CFG_H_ */
