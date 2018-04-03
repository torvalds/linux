/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_COMMAND_TABLE2_H__
#define __DAL_COMMAND_TABLE2_H__

struct bios_parser;
struct bp_encoder_control;

struct cmd_tbl {
	enum bp_result (*dig_encoder_control)(
		struct bios_parser *bp,
		struct bp_encoder_control *control);
	enum bp_result (*encoder_control_dig1)(
		struct bios_parser *bp,
		struct bp_encoder_control *control);
	enum bp_result (*encoder_control_dig2)(
		struct bios_parser *bp,
		struct bp_encoder_control *control);
	enum bp_result (*transmitter_control)(
		struct bios_parser *bp,
		struct bp_transmitter_control *control);
	enum bp_result (*set_pixel_clock)(
		struct bios_parser *bp,
		struct bp_pixel_clock_parameters *bp_params);
	enum bp_result (*enable_spread_spectrum_on_ppll)(
		struct bios_parser *bp,
		struct bp_spread_spectrum_parameters *bp_params,
		bool enable);
	enum bp_result (*adjust_display_pll)(
		struct bios_parser *bp,
		struct bp_adjust_pixel_clock_parameters *bp_params);
	enum bp_result (*dac1_encoder_control)(
		struct bios_parser *bp,
		bool enable,
		uint32_t pixel_clock,
		uint8_t dac_standard);
	enum bp_result (*dac2_encoder_control)(
		struct bios_parser *bp,
		bool enable,
		uint32_t pixel_clock,
		uint8_t dac_standard);
	enum bp_result (*dac1_output_control)(
		struct bios_parser *bp,
		bool enable);
	enum bp_result (*dac2_output_control)(
		struct bios_parser *bp,
		bool enable);
	enum bp_result (*set_crtc_timing)(
		struct bios_parser *bp,
		struct bp_hw_crtc_timing_parameters *bp_params);
	enum bp_result (*select_crtc_source)(
		struct bios_parser *bp,
		struct bp_crtc_source_select *bp_params);
	enum bp_result (*enable_crtc)(
		struct bios_parser *bp,
		enum controller_id controller_id,
		bool enable);
	enum bp_result (*enable_crtc_mem_req)(
		struct bios_parser *bp,
		enum controller_id controller_id,
		bool enable);
	enum bp_result (*program_clock)(
		struct bios_parser *bp,
		struct bp_pixel_clock_parameters *bp_params);
	enum bp_result (*external_encoder_control)(
			struct bios_parser *bp,
			struct bp_external_encoder_control *cntl);
	enum bp_result (*enable_disp_power_gating)(
		struct bios_parser *bp,
		enum controller_id crtc_id,
		enum bp_pipe_control_action action);
	enum bp_result (*set_dce_clock)(
		struct bios_parser *bp,
		struct bp_set_dce_clock_parameters *bp_params);
	unsigned int (*get_smu_clock_info)(
			struct bios_parser *bp, uint8_t id);

};

void dal_firmware_parser_init_cmd_tbl(struct bios_parser *bp);

#endif
