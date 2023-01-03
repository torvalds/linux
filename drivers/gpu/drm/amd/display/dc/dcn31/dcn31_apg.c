/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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


#include "dc_bios_types.h"
#include "hw_shared.h"
#include "dcn31_apg.h"
#include "reg_helper.h"

#define DC_LOGGER \
		apg31->base.ctx->logger

#define REG(reg)\
	(apg31->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	apg31->apg_shift->field_name, apg31->apg_mask->field_name


#define CTX \
	apg31->base.ctx


static void apg31_enable(
	struct apg *apg)
{
	struct dcn31_apg *apg31 = DCN31_APG_FROM_APG(apg);

	/* Reset APG */
	REG_UPDATE(APG_CONTROL, APG_RESET, 1);
	REG_WAIT(APG_CONTROL,
			APG_RESET_DONE, 1,
			1, 10);
	REG_UPDATE(APG_CONTROL, APG_RESET, 0);
	REG_WAIT(APG_CONTROL,
			APG_RESET_DONE, 0,
			1, 10);

	/* Enable APG */
	REG_UPDATE(APG_CONTROL2, APG_ENABLE, 1);
}

static void apg31_disable(
	struct apg *apg)
{
	struct dcn31_apg *apg31 = DCN31_APG_FROM_APG(apg);

	/* Disable APG */
	REG_UPDATE(APG_CONTROL2, APG_ENABLE, 0);
}

static union audio_cea_channels speakers_to_channels(
	struct audio_speaker_flags speaker_flags)
{
	union audio_cea_channels cea_channels = {0};

	/* these are one to one */
	cea_channels.channels.FL = speaker_flags.FL_FR;
	cea_channels.channels.FR = speaker_flags.FL_FR;
	cea_channels.channels.LFE = speaker_flags.LFE;
	cea_channels.channels.FC = speaker_flags.FC;

	/* if Rear Left and Right exist move RC speaker to channel 7
	 * otherwise to channel 5
	 */
	if (speaker_flags.RL_RR) {
		cea_channels.channels.RL_RC = speaker_flags.RL_RR;
		cea_channels.channels.RR = speaker_flags.RL_RR;
		cea_channels.channels.RC_RLC_FLC = speaker_flags.RC;
	} else {
		cea_channels.channels.RL_RC = speaker_flags.RC;
	}

	/* FRONT Left Right Center and REAR Left Right Center are exclusive */
	if (speaker_flags.FLC_FRC) {
		cea_channels.channels.RC_RLC_FLC = speaker_flags.FLC_FRC;
		cea_channels.channels.RRC_FRC = speaker_flags.FLC_FRC;
	} else {
		cea_channels.channels.RC_RLC_FLC = speaker_flags.RLC_RRC;
		cea_channels.channels.RRC_FRC = speaker_flags.RLC_RRC;
	}

	return cea_channels;
}

static void apg31_se_audio_setup(
	struct apg *apg,
	unsigned int az_inst,
	struct audio_info *audio_info)
{
	struct dcn31_apg *apg31 = DCN31_APG_FROM_APG(apg);

	uint32_t speakers = 0;
	uint32_t channels = 0;

	ASSERT(audio_info);
	/* This should not happen.it does so we don't get BSOD*/
	if (audio_info == NULL)
		return;

	speakers = audio_info->flags.info.ALLSPEAKERS;
	channels = speakers_to_channels(audio_info->flags.speaker_flags).all;

	/* DisplayPort only allows for one audio stream with stream ID 0 */
	REG_UPDATE(APG_CONTROL2, APG_DP_AUDIO_STREAM_ID, 0);

	/* When running in "pair mode", pairs of audio channels have their own enable
	 * this is for really old audio drivers */
	REG_UPDATE(APG_DBG_GEN_CONTROL, APG_DBG_AUDIO_CHANNEL_ENABLE, 0xFF);
	// REG_UPDATE(APG_DBG_GEN_CONTROL, APG_DBG_AUDIO_CHANNEL_ENABLE, channels);

	/* Disable forced mem power off */
	REG_UPDATE(APG_MEM_PWR, APG_MEM_PWR_FORCE, 0);
}

static struct apg_funcs dcn31_apg_funcs = {
	.se_audio_setup			= apg31_se_audio_setup,
	.enable_apg			= apg31_enable,
	.disable_apg			= apg31_disable,
};

void apg31_construct(struct dcn31_apg *apg31,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn31_apg_registers *apg_regs,
	const struct dcn31_apg_shift *apg_shift,
	const struct dcn31_apg_mask *apg_mask)
{
	apg31->base.ctx = ctx;

	apg31->base.inst = inst;
	apg31->base.funcs = &dcn31_apg_funcs;

	apg31->regs = apg_regs;
	apg31->apg_shift = apg_shift;
	apg31->apg_mask = apg_mask;
}
