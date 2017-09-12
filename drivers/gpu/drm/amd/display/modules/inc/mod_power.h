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

#ifndef MODULES_INC_MOD_POWER_H_
#define MODULES_INC_MOD_POWER_H_

#include "dm_services.h"

struct mod_power {
	int dummy;
};

/* VariBright related commands */
enum varibright_command {
	VariBright_Cmd__SetVBLevel = 0,
	VariBright_Cmd__UserEnable,
	VariBright_Cmd__PreDisplayConfigChange,
	VariBright_Cmd__PostDisplayConfigChange,
	VariBright_Cmd__SuspendABM,
	VariBright_Cmd__ResumeABM,

	VariBright_Cmd__Unknown,
};

/* VariBright settings structure */
struct varibright_info {
	enum varibright_command cmd;

	unsigned int level;
	bool enable;
	bool activate;
};

enum dmcu_block_psr_reason {
	/* This is a bitfield mask */
	dmcu_block_psr_reason_invalid = 0x0,
	dmcu_block_psr_reason_vsync_int = 0x1,
	dmcu_block_psr_reason_shared_primary = 0x2,
	dmcu_block_psr_reason_unsupported_link_rate = 0x4
};

struct mod_power *mod_power_create(struct dc *dc);

void mod_power_destroy(struct mod_power *mod_power);

bool mod_power_add_sink(struct mod_power *mod_power,
		const struct dc_sink *sink);

bool mod_power_remove_sink(struct mod_power *mod_power,
		const struct dc_sink *sink);

bool mod_power_set_backlight(struct mod_power *mod_power,
		const struct dc_stream **streams, int num_streams,
		unsigned int backlight_8bit);

bool mod_power_get_backlight(struct mod_power *mod_power,
		const struct dc_sink *sink,
		unsigned int *backlight_8bit);

void mod_power_initialize_backlight_caps
		(struct mod_power *mod_power);

unsigned int mod_power_backlight_level_percentage_to_signal
		(struct mod_power *mod_power, unsigned int percentage);

unsigned int mod_power_backlight_level_signal_to_percentage
	(struct mod_power *mod_power, unsigned int signalLevel8bit);

bool mod_power_get_panel_backlight_boundaries
				(struct mod_power *mod_power,
				unsigned int *min_backlight,
				unsigned int *max_backlight,
				unsigned int *output_ac_level_percentage,
				unsigned int *output_dc_level_percentage);

bool mod_power_set_smooth_brightness(struct mod_power *mod_power,
		const struct dc_sink *sink, bool enable_brightness);

bool mod_power_notify_mode_change(struct mod_power *mod_power,
		const struct dc_stream *stream);

bool mod_power_varibright_control(struct mod_power *mod_power,
		struct varibright_info *input_varibright_info);

bool mod_power_block_psr(bool block_enable, enum dmcu_block_psr_reason reason);

bool mod_power_set_psr_enable(struct mod_power *mod_power,
		bool psr_enable);

#endif /* MODULES_INC_MOD_POWER_H_ */
