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

#ifndef _DMUB_REPLAY_H_
#define _DMUB_REPLAY_H_

#include "dc_types.h"
#include "dmub_cmd.h"
struct dc_link;
struct dmub_replay_funcs;

struct dmub_replay {
	struct dc_context *ctx;
	const struct dmub_replay_funcs *funcs;
};

struct dmub_replay_funcs {
	void (*replay_get_state)(struct dmub_replay *dmub, enum replay_state *state,
		uint8_t panel_inst);
	void (*replay_enable)(struct dmub_replay *dmub, bool enable, bool wait,
		uint8_t panel_inst);
	bool (*replay_copy_settings)(struct dmub_replay *dmub, struct dc_link *link,
		struct replay_context *replay_context, uint8_t panel_inst);
	void (*replay_set_power_opt)(struct dmub_replay *dmub, unsigned int power_opt,
		uint8_t panel_inst);
	void (*replay_set_coasting_vtotal)(struct dmub_replay *dmub, uint16_t coasting_vtotal,
		uint8_t panel_inst);
	void (*replay_residency)(struct dmub_replay *dmub,
		uint8_t panel_inst, uint32_t *residency, const bool is_start, const bool is_alpm);
};

struct dmub_replay *dmub_replay_create(struct dc_context *ctx);
void dmub_replay_destroy(struct dmub_replay **dmub);


#endif /* _DMUB_REPLAY_H_ */
