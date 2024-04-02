/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_ENCODER__DCN32_H__
#define __DC_LINK_ENCODER__DCN32_H__

#include "dcn31/dcn31_dio_link_encoder.h"

#define LE_DCN32_REG_LIST(id)\
	LE_DCN31_REG_LIST(id),\
	SRI(DIG_FIFO_CTRL0, DIG, id)

#define LINK_ENCODER_MASK_SH_LIST_DCN32(mask_sh) \
	LINK_ENCODER_MASK_SH_LIST_DCN31(mask_sh),\
	LE_SF(DIG0_DIG_FIFO_CTRL0, DIG_FIFO_OUTPUT_PIXEL_MODE, mask_sh)

void dcn32_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

void enc32_hw_init(struct link_encoder *enc);

void dcn32_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

bool dcn32_link_encoder_is_in_alt_mode(struct link_encoder *enc);

void dcn32_link_encoder_get_max_link_cap(struct link_encoder *enc,
	struct dc_link_settings *link_settings);

#endif /* __DC_LINK_ENCODER__DCN32_H__ */
