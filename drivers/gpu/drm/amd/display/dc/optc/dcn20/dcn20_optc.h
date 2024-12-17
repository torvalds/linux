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

#ifndef __DC_OPTC_DCN20_H__
#define __DC_OPTC_DCN20_H__

#include "dcn10/dcn10_optc.h"

#define TG_COMMON_REG_LIST_DCN2_0(inst) \
	TG_COMMON_REG_LIST_DCN(inst),\
	SRI(OTG_GLOBAL_CONTROL1, OTG, inst),\
	SRI(OTG_GLOBAL_CONTROL2, OTG, inst),\
	SRI(OTG_GSL_WINDOW_X, OTG, inst),\
	SRI(OTG_GSL_WINDOW_Y, OTG, inst),\
	SRI(OTG_VUPDATE_KEEPOUT, OTG, inst),\
	SRI(OTG_DSC_START_POSITION, OTG, inst),\
	SRI(OTG_CRC_CNTL2, OTG, inst),\
	SRI(OPTC_DATA_FORMAT_CONTROL, ODM, inst),\
	SRI(OPTC_BYTES_PER_PIXEL, ODM, inst),\
	SRI(OPTC_WIDTH_CONTROL, ODM, inst),\
	SRI(OPTC_MEMORY_CONFIG, ODM, inst),\
	SR(DWB_SOURCE_SELECT),\
	SRI(OTG_MANUAL_FLOW_CONTROL, OTG, inst), \
	SRI(OTG_DRR_CONTROL, OTG, inst),\
	SRI(OTG_PIPE_UPDATE_STATUS, OTG, inst)

#define TG_COMMON_MASK_SH_LIST_DCN2_0(mask_sh)\
	TG_COMMON_MASK_SH_LIST_DCN(mask_sh),\
	SF(OTG0_OTG_GLOBAL_CONTROL1, MASTER_UPDATE_LOCK_DB_X, mask_sh),\
	SF(OTG0_OTG_GLOBAL_CONTROL1, MASTER_UPDATE_LOCK_DB_Y, mask_sh),\
	SF(OTG0_OTG_GLOBAL_CONTROL1, MASTER_UPDATE_LOCK_DB_EN, mask_sh),\
	SF(OTG0_OTG_GLOBAL_CONTROL2, GLOBAL_UPDATE_LOCK_EN, mask_sh),\
	SF(OTG0_OTG_GLOBAL_CONTROL2, DIG_UPDATE_LOCATION, mask_sh),\
	SF(OTG0_OTG_DOUBLE_BUFFER_CONTROL, OTG_RANGE_TIMING_DBUF_UPDATE_MODE, mask_sh),\
	SF(OTG0_OTG_PIPE_UPDATE_STATUS, OTG_FLIP_PENDING, mask_sh),\
	SF(OTG0_OTG_PIPE_UPDATE_STATUS, OTG_DC_REG_UPDATE_PENDING, mask_sh),\
	SF(OTG0_OTG_PIPE_UPDATE_STATUS, OTG_CURSOR_UPDATE_PENDING, mask_sh),\
	SF(OTG0_OTG_PIPE_UPDATE_STATUS, OTG_VUPDATE_KEEPOUT_STATUS, mask_sh),\
	SF(OTG0_OTG_GSL_WINDOW_X, OTG_GSL_WINDOW_START_X, mask_sh),\
	SF(OTG0_OTG_GSL_WINDOW_X, OTG_GSL_WINDOW_END_X, mask_sh), \
	SF(OTG0_OTG_GSL_WINDOW_Y, OTG_GSL_WINDOW_START_Y, mask_sh),\
	SF(OTG0_OTG_GSL_WINDOW_Y, OTG_GSL_WINDOW_END_Y, mask_sh),\
	SF(OTG0_OTG_GSL_CONTROL, OTG_GSL_MASTER_MODE, mask_sh), \
	SF(OTG0_OTG_GSL_CONTROL, OTG_MASTER_UPDATE_LOCK_GSL_EN, mask_sh), \
	SF(OTG0_OTG_DSC_START_POSITION, OTG_DSC_START_POSITION_X, mask_sh), \
	SF(OTG0_OTG_DSC_START_POSITION, OTG_DSC_START_POSITION_LINE_NUM, mask_sh),\
	SF(OTG0_OTG_CRC_CNTL2, OTG_CRC_DSC_MODE, mask_sh),\
	SF(OTG0_OTG_CRC_CNTL2, OTG_CRC_DATA_STREAM_COMBINE_MODE, mask_sh),\
	SF(OTG0_OTG_CRC_CNTL2, OTG_CRC_DATA_STREAM_SPLIT_MODE, mask_sh),\
	SF(OTG0_OTG_CRC_CNTL2, OTG_CRC_DATA_FORMAT, mask_sh),\
	SF(ODM0_OPTC_DATA_SOURCE_SELECT, OPTC_SEG0_SRC_SEL, mask_sh),\
	SF(ODM0_OPTC_DATA_SOURCE_SELECT, OPTC_SEG1_SRC_SEL, mask_sh),\
	SF(ODM0_OPTC_DATA_SOURCE_SELECT, OPTC_NUM_OF_INPUT_SEGMENT, mask_sh),\
	SF(ODM0_OPTC_MEMORY_CONFIG, OPTC_MEM_SEL, mask_sh),\
	SF(ODM0_OPTC_DATA_FORMAT_CONTROL, OPTC_DATA_FORMAT, mask_sh),\
	SF(ODM0_OPTC_DATA_FORMAT_CONTROL, OPTC_DSC_MODE, mask_sh),\
	SF(ODM0_OPTC_BYTES_PER_PIXEL, OPTC_DSC_BYTES_PER_PIXEL, mask_sh),\
	SF(ODM0_OPTC_WIDTH_CONTROL, OPTC_DSC_SLICE_WIDTH, mask_sh),\
	SF(ODM0_OPTC_WIDTH_CONTROL, OPTC_SEGMENT_WIDTH, mask_sh),\
	SF(DWB_SOURCE_SELECT, OPTC_DWB0_SOURCE_SELECT, mask_sh),\
	SF(DWB_SOURCE_SELECT, OPTC_DWB1_SOURCE_SELECT, mask_sh),\
	SF(OTG0_OTG_MANUAL_FLOW_CONTROL, MANUAL_FLOW_CONTROL, mask_sh), \
	SF(OTG0_OTG_DRR_CONTROL, OTG_V_TOTAL_LAST_USED_BY_DRR, mask_sh)

void dcn20_timing_generator_init(struct optc *optc);

void optc2_get_last_used_drr_vtotal(struct timing_generator *optc,
		uint32_t *refresh_rate);

bool optc2_enable_crtc(struct timing_generator *optc);

void optc2_set_gsl(struct timing_generator *optc,
		const struct gsl_params *params);

void optc2_set_gsl_source_select(struct timing_generator *optc,
		int group_idx,
		uint32_t gsl_ready_signal);

void optc2_set_dsc_config(struct timing_generator *optc,
					enum optc_dsc_mode dsc_mode,
					uint32_t dsc_bytes_per_pixel,
					uint32_t dsc_slice_width);

void optc2_get_dsc_status(struct timing_generator *optc,
					uint32_t *dsc_mode);

void optc2_set_odm_bypass(struct timing_generator *optc,
		const struct dc_crtc_timing *dc_crtc_timing);

void optc2_set_odm_combine(struct timing_generator *optc, int *opp_id, int opp_cnt,
		int segment_width, int last_segment_width);

void optc2_get_optc_source(struct timing_generator *optc,
		uint32_t *num_of_src_opp,
		uint32_t *src_opp_id_0,
		uint32_t *src_opp_id_1);

void optc2_triplebuffer_lock(struct timing_generator *optc);
void optc2_triplebuffer_unlock(struct timing_generator *optc);
void optc2_lock_doublebuffer_disable(struct timing_generator *optc);
void optc2_lock_doublebuffer_enable(struct timing_generator *optc);
void optc2_setup_manual_trigger(struct timing_generator *optc);
void optc2_program_manual_trigger(struct timing_generator *optc);
bool optc2_configure_crc(struct timing_generator *optc,
			  const struct crc_params *params);
#endif /* __DC_OPTC_DCN20_H__ */
