// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 */

#include <drm/drm_print.h>

#include "sti_awg_utils.h"

#define AWG_DELAY (-5)

#define AWG_OPCODE_OFFSET 10
#define AWG_MAX_ARG       0x3ff

enum opcode {
	SET,
	RPTSET,
	RPLSET,
	SKIP,
	STOP,
	REPEAT,
	REPLAY,
	JUMP,
	HOLD,
};

static int awg_generate_instr(enum opcode opcode,
			      long int arg,
			      long int mux_sel,
			      long int data_en,
			      struct awg_code_generation_params *fwparams)
{
	u32 instruction = 0;
	u32 mux = (mux_sel << 8) & 0x1ff;
	u32 data_enable = (data_en << 9) & 0x2ff;
	long int arg_tmp = arg;

	/* skip, repeat and replay arg should not exceed 1023.
	 * If user wants to exceed this value, the instruction should be
	 * duplicate and arg should be adjust for each duplicated instruction.
	 *
	 * mux_sel is used in case of SAV/EAV synchronization.
	 */

	while (arg_tmp > 0) {
		arg = arg_tmp;
		if (fwparams->instruction_offset >= AWG_MAX_INST) {
			DRM_ERROR("too many number of instructions\n");
			return -EINVAL;
		}

		switch (opcode) {
		case SKIP:
			/* leave 'arg' + 1 pixel elapsing without changing
			 * output bus */
			arg--; /* pixel adjustment */
			arg_tmp--;

			if (arg < 0) {
				/* SKIP instruction not needed */
				return 0;
			}

			if (arg == 0) {
				/* SKIP 0 not permitted but we want to skip 1
				 * pixel. So we transform SKIP into SET
				 * instruction */
				opcode = SET;
				break;
			}

			mux = 0;
			data_enable = 0;
			arg &= AWG_MAX_ARG;
			break;
		case REPEAT:
		case REPLAY:
			if (arg == 0) {
				/* REPEAT or REPLAY instruction not needed */
				return 0;
			}

			mux = 0;
			data_enable = 0;
			arg &= AWG_MAX_ARG;
			break;
		case JUMP:
			mux = 0;
			data_enable = 0;
			arg |= 0x40; /* for jump instruction 7th bit is 1 */
			arg &= AWG_MAX_ARG;
			break;
		case STOP:
			arg = 0;
			break;
		case SET:
		case RPTSET:
		case RPLSET:
		case HOLD:
			arg &= (0x0ff);
			break;
		default:
			DRM_ERROR("instruction %d does not exist\n", opcode);
			return -EINVAL;
		}

		arg_tmp = arg_tmp - arg;

		arg = ((arg + mux) + data_enable);

		instruction = ((opcode) << AWG_OPCODE_OFFSET) | arg;
		fwparams->ram_code[fwparams->instruction_offset] =
			instruction & (0x3fff);
		fwparams->instruction_offset++;
	}
	return 0;
}

static int awg_generate_line_signal(
		struct awg_code_generation_params *fwparams,
		struct awg_timing *timing)
{
	long int val;
	int ret = 0;

	if (timing->trailing_pixels > 0) {
		/* skip trailing pixel */
		val = timing->blanking_level;
		ret |= awg_generate_instr(RPLSET, val, 0, 0, fwparams);

		val = timing->trailing_pixels - 1 + AWG_DELAY;
		ret |= awg_generate_instr(SKIP, val, 0, 0, fwparams);
	}

	/* set DE signal high */
	val = timing->blanking_level;
	ret |= awg_generate_instr((timing->trailing_pixels > 0) ? SET : RPLSET,
			val, 0, 1, fwparams);

	if (timing->blanking_pixels > 0) {
		/* skip the number of active pixel */
		val = timing->active_pixels - 1;
		ret |= awg_generate_instr(SKIP, val, 0, 1, fwparams);

		/* set DE signal low */
		val = timing->blanking_level;
		ret |= awg_generate_instr(SET, val, 0, 0, fwparams);
	}

	return ret;
}

int sti_awg_generate_code_data_enable_mode(
		struct awg_code_generation_params *fwparams,
		struct awg_timing *timing)
{
	long int val, tmp_val;
	int ret = 0;

	if (timing->trailing_lines > 0) {
		/* skip trailing lines */
		val = timing->blanking_level;
		ret |= awg_generate_instr(RPLSET, val, 0, 0, fwparams);

		val = timing->trailing_lines - 1;
		ret |= awg_generate_instr(REPLAY, val, 0, 0, fwparams);
	}

	tmp_val = timing->active_lines - 1;

	while (tmp_val > 0) {
		/* generate DE signal for each line */
		ret |= awg_generate_line_signal(fwparams, timing);
		/* replay the sequence as many active lines defined */
		ret |= awg_generate_instr(REPLAY,
					  min_t(int, AWG_MAX_ARG, tmp_val),
					  0, 0, fwparams);
		tmp_val -= AWG_MAX_ARG;
	}

	if (timing->blanking_lines > 0) {
		/* skip blanking lines */
		val = timing->blanking_level;
		ret |= awg_generate_instr(RPLSET, val, 0, 0, fwparams);

		val = timing->blanking_lines - 1;
		ret |= awg_generate_instr(REPLAY, val, 0, 0, fwparams);
	}

	return ret;
}
