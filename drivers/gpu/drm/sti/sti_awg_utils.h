/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 */

#ifndef _STI_AWG_UTILS_H_
#define _STI_AWG_UTILS_H_

#include <drm/drmP.h>

#define AWG_MAX_INST 64

struct awg_code_generation_params {
	u32 *ram_code;
	u8 instruction_offset;
};

struct awg_timing {
	u32 total_lines;
	u32 active_lines;
	u32 blanking_lines;
	u32 trailing_lines;
	u32 total_pixels;
	u32 active_pixels;
	u32 blanking_pixels;
	u32 trailing_pixels;
	u32 blanking_level;
};

int sti_awg_generate_code_data_enable_mode(
		struct awg_code_generation_params *fw_gen_params,
		struct awg_timing *timing);
#endif
