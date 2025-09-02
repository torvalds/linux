// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_instance.h"
#include "iris_vpu_buffer.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_hfi_gen2_defines.h"

#define HFI_MAX_COL_FRAME 6

#ifndef SYSTEM_LAL_TILE10
#define SYSTEM_LAL_TILE10 192
#endif

static u32 size_h264d_hw_bin_buffer(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 size_yuv, size_bin_hdr, size_bin_res;

	size_yuv = ((frame_width * frame_height) <= BIN_BUFFER_THRESHOLD) ?
			((BIN_BUFFER_THRESHOLD * 3) >> 1) :
			((frame_width * frame_height * 3) >> 1);
	size_bin_hdr = size_yuv * H264_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * H264_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr / num_vpp_pipes,
			     DMA_ALIGNMENT) * num_vpp_pipes;
	size_bin_res = ALIGN(size_bin_res / num_vpp_pipes,
			     DMA_ALIGNMENT) * num_vpp_pipes;

	return size_bin_hdr + size_bin_res;
}

static u32 hfi_buffer_bin_h264d(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 n_aligned_h = ALIGN(frame_height, 16);
	u32 n_aligned_w = ALIGN(frame_width, 16);

	return size_h264d_hw_bin_buffer(n_aligned_w, n_aligned_h, num_vpp_pipes);
}

static u32 size_h265d_hw_bin_buffer(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 product = frame_width * frame_height;
	u32 size_yuv, size_bin_hdr, size_bin_res;

	size_yuv = (product <= BIN_BUFFER_THRESHOLD) ?
		((BIN_BUFFER_THRESHOLD * 3) >> 1) : ((product * 3) >> 1);
	size_bin_hdr = size_yuv * H265_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * H265_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr / num_vpp_pipes, DMA_ALIGNMENT) * num_vpp_pipes;
	size_bin_res = ALIGN(size_bin_res / num_vpp_pipes, DMA_ALIGNMENT) * num_vpp_pipes;

	return size_bin_hdr + size_bin_res;
}

static u32 hfi_buffer_bin_vp9d(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 _size_yuv = ALIGN(frame_width, 16) * ALIGN(frame_height, 16) * 3 / 2;
	u32 _size = ALIGN(((max_t(u32, _size_yuv, ((BIN_BUFFER_THRESHOLD * 3) >> 1)) *
			VPX_DECODER_FRAME_BIN_HDR_BUDGET / VPX_DECODER_FRAME_BIN_DENOMINATOR *
			VPX_DECODER_FRAME_CONCURENCY_LVL) / num_vpp_pipes), DMA_ALIGNMENT) +
			ALIGN(((max_t(u32, _size_yuv, ((BIN_BUFFER_THRESHOLD * 3) >> 1)) *
			VPX_DECODER_FRAME_BIN_RES_BUDGET / VPX_DECODER_FRAME_BIN_DENOMINATOR *
			VPX_DECODER_FRAME_CONCURENCY_LVL) / num_vpp_pipes), DMA_ALIGNMENT);

	return _size * num_vpp_pipes;
}

static u32 hfi_buffer_bin_h265d(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 n_aligned_w = ALIGN(frame_width, 16);
	u32 n_aligned_h = ALIGN(frame_height, 16);

	return size_h265d_hw_bin_buffer(n_aligned_w, n_aligned_h, num_vpp_pipes);
}

static u32 hfi_buffer_comv_h264d(u32 frame_width, u32 frame_height, u32 _comv_bufcount)
{
	u32 frame_height_in_mbs = DIV_ROUND_UP(frame_height, 16);
	u32 frame_width_in_mbs = DIV_ROUND_UP(frame_width, 16);
	u32 col_zero_aligned_width = (frame_width_in_mbs << 2);
	u32 col_mv_aligned_width = (frame_width_in_mbs << 7);
	u32 col_zero_size, size_colloc;

	col_mv_aligned_width = ALIGN(col_mv_aligned_width, 16);
	col_zero_aligned_width = ALIGN(col_zero_aligned_width, 16);
	col_zero_size = col_zero_aligned_width *
			((frame_height_in_mbs + 1) >> 1);
	col_zero_size = ALIGN(col_zero_size, 64);
	col_zero_size <<= 1;
	col_zero_size = ALIGN(col_zero_size, 512);
	size_colloc = col_mv_aligned_width * ((frame_height_in_mbs + 1) >> 1);
	size_colloc = ALIGN(size_colloc, 64);
	size_colloc <<= 1;
	size_colloc = ALIGN(size_colloc, 512);
	size_colloc += (col_zero_size + SIZE_H264D_BUFTAB_T * 2);

	return (size_colloc * (_comv_bufcount)) + 512;
}

static u32 hfi_buffer_comv_h265d(u32 frame_width, u32 frame_height, u32 _comv_bufcount)
{
	u32 frame_height_in_mbs = (frame_height + 15) >> 4;
	u32 frame_width_in_mbs = (frame_width + 15) >> 4;
	u32 _size;

	_size = ALIGN(((frame_width_in_mbs * frame_height_in_mbs) << 8), 512);

	return (_size * (_comv_bufcount)) + 512;
}

static u32 size_h264d_bse_cmd_buf(u32 frame_height)
{
	u32 height = ALIGN(frame_height, 32);

	return min_t(u32, (DIV_ROUND_UP(height, 16) * 48), H264D_MAX_SLICE) *
		SIZE_H264D_BSE_CMD_PER_BUF;
}

static u32 size_h265d_bse_cmd_buf(u32 frame_width, u32 frame_height)
{
	u32 _size = ALIGN(((ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
			   (ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS)) *
			  NUM_HW_PIC_BUF, DMA_ALIGNMENT);
	_size = min_t(u32, _size, H265D_MAX_SLICE + 1);
	_size = 2 * _size * SIZE_H265D_BSE_CMD_PER_BUF;

	return _size;
}

static u32 hfi_buffer_persist_h265d(u32 rpu_enabled)
{
	return ALIGN((SIZE_SLIST_BUF_H265 * NUM_SLIST_BUF_H265 +
		      H265_NUM_FRM_INFO * H265_DISPLAY_BUF_SIZE +
		      H265_NUM_TILE * sizeof(u32) +
		      NUM_HW_PIC_BUF * SIZE_SEI_USERDATA +
		      rpu_enabled * NUM_HW_PIC_BUF * SIZE_DOLBY_RPU_METADATA),
		     DMA_ALIGNMENT);
}

static inline
u32 hfi_iris3_vp9d_comv_size(void)
{
	return (((8192 + 63) >> 6) * ((4320 + 63) >> 6) * 8 * 8 * 2 * 8);
}

static u32 hfi_buffer_persist_vp9d(void)
{
	return ALIGN(VP9_NUM_PROBABILITY_TABLE_BUF * VP9_PROB_TABLE_SIZE, DMA_ALIGNMENT) +
		ALIGN(hfi_iris3_vp9d_comv_size(), DMA_ALIGNMENT) +
		ALIGN(MAX_SUPERFRAME_HEADER_LEN, DMA_ALIGNMENT) +
		ALIGN(VP9_UDC_HEADER_BUF_SIZE, DMA_ALIGNMENT) +
		ALIGN(VP9_NUM_FRAME_INFO_BUF * CCE_TILE_OFFSET_SIZE, DMA_ALIGNMENT) +
		ALIGN(VP9_NUM_FRAME_INFO_BUF * VP9_FRAME_INFO_BUF_SIZE, DMA_ALIGNMENT) +
		HDR10_HIST_EXTRADATA_SIZE;
}

static u32 size_h264d_vpp_cmd_buf(u32 frame_height)
{
	u32 size, height = ALIGN(frame_height, 32);

	size = min_t(u32, (DIV_ROUND_UP(height, 16) * 48), H264D_MAX_SLICE) *
			SIZE_H264D_VPP_CMD_PER_BUF;

	return size > VPP_CMD_MAX_SIZE ? VPP_CMD_MAX_SIZE : size;
}

static u32 hfi_buffer_persist_h264d(void)
{
	return ALIGN(SIZE_SLIST_BUF_H264 * NUM_SLIST_BUF_H264 +
		    H264_DISPLAY_BUF_SIZE * H264_NUM_FRM_INFO +
		    NUM_HW_PIC_BUF * SIZE_SEI_USERDATA,
		    DMA_ALIGNMENT);
}

static u32 hfi_buffer_non_comv_h264d(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 size_bse = size_h264d_bse_cmd_buf(frame_height);
	u32 size_vpp = size_h264d_vpp_cmd_buf(frame_height);
	u32 size = ALIGN(size_bse, DMA_ALIGNMENT) +
		ALIGN(size_vpp, DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H264D_HW_PIC_T), DMA_ALIGNMENT);

	return ALIGN(size, DMA_ALIGNMENT);
}

static u32 size_h265d_vpp_cmd_buf(u32 frame_width, u32 frame_height)
{
	u32 _size = ALIGN(((ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
			   (ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS)) *
			  NUM_HW_PIC_BUF, DMA_ALIGNMENT);
	_size = min_t(u32, _size, H265D_MAX_SLICE + 1);
	_size = ALIGN(_size, 4);
	_size = 2 * _size * SIZE_H265D_VPP_CMD_PER_BUF;
	if (_size > VPP_CMD_MAX_SIZE)
		_size = VPP_CMD_MAX_SIZE;

	return _size;
}

static u32 hfi_buffer_non_comv_h265d(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 _size_bse = size_h265d_bse_cmd_buf(frame_width, frame_height);
	u32 _size_vpp = size_h265d_vpp_cmd_buf(frame_width, frame_height);
	u32 _size = ALIGN(_size_bse, DMA_ALIGNMENT) +
		ALIGN(_size_vpp, DMA_ALIGNMENT) +
		ALIGN(NUM_HW_PIC_BUF * 20 * 22 * 4, DMA_ALIGNMENT) +
		ALIGN(2 * sizeof(u16) *
		(ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		(ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS), DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H265D_HW_PIC_T), DMA_ALIGNMENT) +
		HDR10_HIST_EXTRADATA_SIZE;

	return ALIGN(_size, DMA_ALIGNMENT);
}

static u32 size_vpss_lb(u32 frame_width, u32 frame_height)
{
	u32 opb_lb_wr_llb_y_buffer_size, opb_lb_wr_llb_uv_buffer_size;
	u32 opb_wr_top_line_chroma_buffer_size;
	u32 opb_wr_top_line_luma_buffer_size;
	u32 macrotiling_size = 32;

	opb_wr_top_line_luma_buffer_size =
		ALIGN(frame_width, macrotiling_size) / macrotiling_size * 256;
	opb_wr_top_line_luma_buffer_size =
		ALIGN(opb_wr_top_line_luma_buffer_size, DMA_ALIGNMENT) +
		(MAX_TILE_COLUMNS - 1) * 256;
	opb_wr_top_line_luma_buffer_size =
		max_t(u32, opb_wr_top_line_luma_buffer_size, (32 * ALIGN(frame_height, 8)));
	opb_wr_top_line_chroma_buffer_size = opb_wr_top_line_luma_buffer_size;
	opb_lb_wr_llb_uv_buffer_size =
		ALIGN((ALIGN(frame_height, 8) / (4 / 2)) * 64, 32);
	opb_lb_wr_llb_y_buffer_size =
		ALIGN((ALIGN(frame_height, 8) / (4 / 2)) * 64, 32);
	return opb_wr_top_line_luma_buffer_size +
		opb_wr_top_line_chroma_buffer_size +
		opb_lb_wr_llb_uv_buffer_size +
		opb_lb_wr_llb_y_buffer_size;
}

static inline
u32 size_h265d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE *
		(ALIGN(frame_width, 64) + 8) * 2;
}

static inline
u32 size_h265d_lb_fe_top_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE *
		(ALIGN(frame_width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS);
}

static inline
u32 size_h265d_lb_fe_left_ctrl(u32 frame_width, u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE *
		(ALIGN(frame_height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS);
}

static inline
u32 size_h265d_lb_se_top_ctrl(u32 frame_width, u32 frame_height)
{
	return (LCU_MAX_SIZE_PELS / 8 * (128 / 8)) * ((frame_width + 15) >> 4);
}

static inline
u32 size_h265d_lb_se_left_ctrl(u32 frame_width, u32 frame_height)
{
	return max_t(u32, ((frame_height + 16 - 1) / 8) *
		MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		max_t(u32, ((frame_height + 32 - 1) / 8) *
		MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
		((frame_height + 64 - 1) / 8) *
		MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE));
}

static inline
u32 size_h265d_lb_pe_top_data(u32 frame_width, u32 frame_height)
{
	return MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE *
		(ALIGN(frame_width, LCU_MIN_SIZE_PELS) / LCU_MIN_SIZE_PELS);
}

static inline
u32 size_h265d_lb_vsp_top(u32 frame_width, u32 frame_height)
{
	return ((frame_width + 63) >> 6) * 128;
}

static inline
u32 size_h265d_lb_vsp_left(u32 frame_width, u32 frame_height)
{
	return ((frame_height + 63) >> 6) * 128;
}

static inline
u32 size_h265d_lb_recon_dma_metadata_wr(u32 frame_width, u32 frame_height)
{
	return size_h264d_lb_recon_dma_metadata_wr(frame_height);
}

static inline
u32 size_h265d_qp(u32 frame_width, u32 frame_height)
{
	return size_h264d_qp(frame_width, frame_height);
}

static inline
u32 hfi_buffer_line_h265d(u32 frame_width, u32 frame_height, bool is_opb, u32 num_vpp_pipes)
{
	u32 vpss_lb_size = 0, _size;

	_size = ALIGN(size_h265d_lb_fe_top_data(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_fe_top_ctrl(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_fe_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h265d_lb_se_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h265d_lb_se_top_ctrl(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_pe_top_data(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_vsp_top(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_h265d_lb_vsp_left(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h265d_lb_recon_dma_metadata_wr(frame_width, frame_height),
		      DMA_ALIGNMENT) * 4 +
		ALIGN(size_h265d_qp(frame_width, frame_height), DMA_ALIGNMENT);
	if (is_opb)
		vpss_lb_size = size_vpss_lb(frame_width, frame_height);

	return ALIGN((_size + vpss_lb_size), DMA_ALIGNMENT);
}

static inline
u32 size_vpxd_lb_fe_left_ctrl(u32 frame_width, u32 frame_height)
{
	return max_t(u32, ((frame_height + 15) >> 4) *
		     MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		     max_t(u32, ((frame_height + 31) >> 5) *
			   MAX_FE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
			   ((frame_height + 63) >> 6) *
			   MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE));
}

static inline
u32 size_vpxd_lb_fe_top_ctrl(u32 frame_width, u32 frame_height)
{
	return ((ALIGN(frame_width, 64) + 8) * 10 * 2);
}

static inline
u32 size_vpxd_lb_se_top_ctrl(u32 frame_width, u32 frame_height)
{
	return ((frame_width + 15) >> 4) * MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE;
}

static inline
u32 size_vpxd_lb_se_left_ctrl(u32 frame_width, u32 frame_height)
{
	return max_t(u32, ((frame_height + 15) >> 4) *
		     MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		     max_t(u32, ((frame_height + 31) >> 5) *
			   MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
			   ((frame_height + 63) >> 6) *
			   MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE));
}

static inline
u32 size_vpxd_lb_recon_dma_metadata_wr(u32 frame_width, u32 frame_height)
{
	return ALIGN((ALIGN(frame_height, 8) / (4 / 2)) * 64,
		BUFFER_ALIGNMENT_32_BYTES);
}

static inline __maybe_unused
u32 size_mp2d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return ((ALIGN(frame_width, 16) + 8) * 10 * 2);
}

static inline
u32 size_vp9d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return (ALIGN(ALIGN(frame_width, 8), 64) + 8) * 10 * 2;
}

static inline
u32 size_vp9d_lb_pe_top_data(u32 frame_width, u32 frame_height)
{
	return ((ALIGN(ALIGN(frame_width, 8), 64) >> 6) * 176);
}

static inline
u32 size_vp9d_lb_vsp_top(u32 frame_width, u32 frame_height)
{
	return (((ALIGN(ALIGN(frame_width, 8), 64) >> 6) * 64 * 8) + 256);
}

static inline
u32 size_vp9d_qp(u32 frame_width, u32 frame_height)
{
	return size_h264d_qp(frame_width, frame_height);
}

static inline
u32 hfi_iris3_vp9d_lb_size(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	return ALIGN(size_vpxd_lb_fe_left_ctrl(frame_width, frame_height), DMA_ALIGNMENT) *
		num_vpp_pipes +
		ALIGN(size_vpxd_lb_se_left_ctrl(frame_width, frame_height), DMA_ALIGNMENT) *
		num_vpp_pipes +
		ALIGN(size_vp9d_lb_vsp_top(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_vpxd_lb_fe_top_ctrl(frame_width, frame_height), DMA_ALIGNMENT) +
		2 * ALIGN(size_vpxd_lb_recon_dma_metadata_wr(frame_width, frame_height),
			  DMA_ALIGNMENT) +
		ALIGN(size_vpxd_lb_se_top_ctrl(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_vp9d_lb_pe_top_data(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_vp9d_lb_fe_top_data(frame_width, frame_height), DMA_ALIGNMENT) +
		ALIGN(size_vp9d_qp(frame_width, frame_height), DMA_ALIGNMENT);
}

static inline
u32 hfi_buffer_line_vp9d(u32 frame_width, u32 frame_height, u32 _yuv_bufcount_min, bool is_opb,
			 u32 num_vpp_pipes)
{
	u32 vpss_lb_size = 0;
	u32 _lb_size;

	_lb_size = hfi_iris3_vp9d_lb_size(frame_width, frame_height, num_vpp_pipes);

	if (is_opb)
		vpss_lb_size = size_vpss_lb(frame_width, frame_height);

	return _lb_size + vpss_lb_size + 4096;
}

static u32 hfi_buffer_line_h264d(u32 frame_width, u32 frame_height,
				 bool is_opb, u32 num_vpp_pipes)
{
	u32 vpss_lb_size = 0;
	u32 size;

	size = ALIGN(size_h264d_lb_fe_top_data(frame_width), DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_fe_top_ctrl(frame_width), DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_fe_left_ctrl(frame_height), DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h264d_lb_se_top_ctrl(frame_width), DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_se_left_ctrl(frame_height), DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_h264d_lb_pe_top_data(frame_width), DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_vsp_top(frame_width), DMA_ALIGNMENT) +
		ALIGN(size_h264d_lb_recon_dma_metadata_wr(frame_height), DMA_ALIGNMENT) * 2 +
		ALIGN(size_h264d_qp(frame_width, frame_height), DMA_ALIGNMENT);
	size = ALIGN(size, DMA_ALIGNMENT);
	if (is_opb)
		vpss_lb_size = size_vpss_lb(frame_width, frame_height);

	return ALIGN((size + vpss_lb_size), DMA_ALIGNMENT);
}

static u32 iris_vpu_dec_bin_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_bin_h264d(width, height, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_bin_h265d(width, height, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_VP9)
		return hfi_buffer_bin_vp9d(width, height, num_vpp_pipes);

	return 0;
}

static u32 iris_vpu_dec_comv_size(struct iris_inst *inst)
{
	u32 num_comv = VIDEO_MAX_FRAME;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_comv_h264d(width, height, num_comv);
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_comv_h265d(width, height, num_comv);

	return 0;
}

static u32 iris_vpu_dec_persist_size(struct iris_inst *inst)
{
	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_persist_h264d();
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_persist_h265d(0);
	else if (inst->codec == V4L2_PIX_FMT_VP9)
		return hfi_buffer_persist_vp9d();

	return 0;
}

static u32 iris_vpu_dec_dpb_size(struct iris_inst *inst)
{
	if (iris_split_mode_enabled(inst))
		return iris_get_buffer_size(inst, BUF_DPB);
	else
		return 0;
}

static u32 iris_vpu_dec_non_comv_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_non_comv_h264d(width, height, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_non_comv_h265d(width, height, num_vpp_pipes);

	return 0;
}

static u32 iris_vpu_dec_line_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	bool is_opb = false;
	u32 out_min_count = inst->buffers[BUF_OUTPUT].min_count;

	if (iris_split_mode_enabled(inst))
		is_opb = true;

	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_line_h264d(width, height, is_opb, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_line_h265d(width, height, is_opb, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_VP9)
		return hfi_buffer_line_vp9d(width, height, out_min_count, is_opb,
			num_vpp_pipes);

	return 0;
}

static u32 iris_vpu_dec_scratch1_size(struct iris_inst *inst)
{
	return iris_vpu_dec_comv_size(inst) +
		iris_vpu_dec_non_comv_size(inst) +
		iris_vpu_dec_line_size(inst);
}

static inline u32 size_bin_bitstream_enc(u32 width, u32 height,
					 u32 rc_type)
{
	u32 aligned_height = ALIGN(height, 32);
	u32 aligned_width = ALIGN(width, 32);
	u32 frame_size = width * height * 3;
	u32 mbs_per_frame;

	/*
	 * Encoder output size calculation: 32 Align width/height
	 * For resolution < 720p : YUVsize * 4
	 * For resolution > 720p & <= 4K : YUVsize / 2
	 * For resolution > 4k : YUVsize / 4
	 * Initially frame_size = YUVsize * 2;
	 */

	mbs_per_frame = (ALIGN(aligned_height, 16) * ALIGN(aligned_width, 16)) / 256;

	if (mbs_per_frame < NUM_MBS_720P)
		frame_size = frame_size << 1;
	else if (mbs_per_frame <= NUM_MBS_4K)
		frame_size = frame_size >> 2;
	else
		frame_size = frame_size >> 3;

	if (rc_type == HFI_RATE_CONTROL_OFF || rc_type == HFI_RATE_CONTROL_CQ ||
	    rc_type == HFI_RC_OFF || rc_type == HFI_RC_CQ)
		frame_size = frame_size << 1;

	/*
	 * In case of opaque color format bitdepth will be known
	 * with first ETB, buffers allocated already with 8 bit
	 * won't be sufficient for 10 bit
	 * calculate size considering 10-bit by default
	 * For 10-bit cases size = size * 1.25
	 */
	frame_size *= 5;
	frame_size /= 4;

	return ALIGN(frame_size, SZ_4K);
}

static inline u32 hfi_buffer_bin_enc(u32 width, u32 height,
				     u32 work_mode, u32 lcu_size,
				     u32 num_vpp_pipes, u32 rc_type)
{
	u32 sao_bin_buffer_size, padded_bin_size, bitstream_size;
	u32 total_bitbin_buffers, size_single_pipe, bitbin_size;
	u32 aligned_height = ALIGN(height, lcu_size);
	u32 aligned_width = ALIGN(width, lcu_size);

	bitstream_size = size_bin_bitstream_enc(width, height, rc_type);
	bitstream_size = ALIGN(bitstream_size, 256);

	if (work_mode == STAGE_2) {
		total_bitbin_buffers = 3;
		bitbin_size = bitstream_size * 17 / 10;
		bitbin_size = ALIGN(bitbin_size, 256);
	} else {
		total_bitbin_buffers = 1;
		bitstream_size = aligned_width * aligned_height * 3;
		bitbin_size = ALIGN(bitstream_size, 256);
	}

	if (num_vpp_pipes > 2)
		size_single_pipe = bitbin_size / 2;
	else
		size_single_pipe = bitbin_size;

	size_single_pipe = ALIGN(size_single_pipe, 256);
	sao_bin_buffer_size = (64 * (((width + 32) * (height + 32)) >> 10)) + 384;
	padded_bin_size = ALIGN(size_single_pipe, 256);
	size_single_pipe = sao_bin_buffer_size + padded_bin_size;
	size_single_pipe = ALIGN(size_single_pipe, 256);
	bitbin_size = size_single_pipe * num_vpp_pipes;

	return ALIGN(bitbin_size, 256) * total_bitbin_buffers + 512;
}

static u32 iris_vpu_enc_bin_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	u32 stage = inst->fw_caps[STAGE].value;
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	u32 lcu_size;

	if (inst->codec == V4L2_PIX_FMT_HEVC)
		lcu_size = 32;
	else
		lcu_size = 16;

	return hfi_buffer_bin_enc(width, height, stage, lcu_size,
				  num_vpp_pipes, inst->hfi_rc_type);
}

static inline
u32 hfi_buffer_comv_enc(u32 frame_width, u32 frame_height, u32 lcu_size,
			u32 num_recon, u32 standard)
{
	u32 height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	u32 width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	u32 num_lcu_in_frame = width_in_lcus * height_in_lcus;
	u32 mb_height = ((frame_height) + 15) >> 4;
	u32 mb_width = ((frame_width) + 15) >> 4;
	u32 size_colloc_mv, size_colloc_rc;

	size_colloc_mv = (standard == HFI_CODEC_ENCODE_HEVC) ?
		(16 * ((num_lcu_in_frame << 2) + 32)) :
		(3 * 16 * (width_in_lcus * height_in_lcus + 32));
	size_colloc_mv = ALIGN(size_colloc_mv, 256) * num_recon;
	size_colloc_rc = (((mb_width + 7) >> 3) * 16 * 2 * mb_height);
	size_colloc_rc = ALIGN(size_colloc_rc, 256) * HFI_MAX_COL_FRAME;

	return size_colloc_mv + size_colloc_rc;
}

static u32 iris_vpu_enc_comv_size(struct iris_inst *inst)
{
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	u32 num_recon = 1;
	u32 lcu_size = 16;

	if (inst->codec == V4L2_PIX_FMT_HEVC) {
		lcu_size = 32;
		return hfi_buffer_comv_enc(width, height, lcu_size,
					   num_recon + 1, HFI_CODEC_ENCODE_HEVC);
	}

	return hfi_buffer_comv_enc(width, height, lcu_size,
				   num_recon + 1, HFI_CODEC_ENCODE_AVC);
}

static inline
u32 size_frame_rc_buf_size(u32 standard, u32 frame_height_coded,
			   u32 num_vpp_pipes_enc)
{
	u32 size = 0;

	size = (standard == HFI_CODEC_ENCODE_HEVC) ?
		(256 + 16 * (14 + ((((frame_height_coded) >> 5) + 7) >> 3))) :
		(256 + 16 * (14 + ((((frame_height_coded) >> 4) + 7) >> 3)));
	size *= 11;

	if (num_vpp_pipes_enc > 1)
		size = ALIGN(size, 256) * num_vpp_pipes_enc;

	return ALIGN(size, 512) * HFI_MAX_COL_FRAME;
}

static inline
u32 size_enc_slice_info_buf(u32 num_lcu_in_frame)
{
	return ALIGN((256 + (num_lcu_in_frame << 4)), 256);
}

static inline u32 enc_bitcnt_buf_size(u32 num_lcu_in_frame)
{
	return ALIGN((256 + (4 * (num_lcu_in_frame))), 256);
}

static inline u32 enc_bitmap_buf_size(u32 num_lcu_in_frame)
{
	return ALIGN((256 + ((num_lcu_in_frame) >> 3)), 256);
}

static inline u32 size_override_buf(u32 num_lcumb)
{
	return ALIGN(((16 * (((num_lcumb) + 7) >> 3))), 256) * 2;
}

static inline u32 size_ir_buf(u32 num_lcu_in_frame)
{
	return ALIGN((((((num_lcu_in_frame) << 1) + 7) & (~7)) * 3), 256);
}

static inline
u32 size_linebuff_data(bool is_ten_bit, u32 frame_width_coded)
{
	return is_ten_bit ?
		(((((10 * (frame_width_coded) + 1024) + (256 - 1)) &
		   (~(256 - 1))) * 1) +
		 (((((10 * (frame_width_coded) + 1024) >> 1) + (256 - 1)) &
		   (~(256 - 1))) * 2)) :
		(((((8 * (frame_width_coded) + 1024) + (256 - 1)) &
		   (~(256 - 1))) * 1) +
		 (((((8 * (frame_width_coded) + 1024) >> 1) + (256 - 1)) &
		   (~(256 - 1))) * 2));
}

static inline
u32 size_left_linebuff_ctrl(u32 standard, u32 frame_height_coded,
			    u32 num_vpp_pipes_enc)
{
	u32 size = 0;

	size = standard == HFI_CODEC_ENCODE_HEVC ?
		(((frame_height_coded) +
		 (32)) / 32 * 4 * 16) :
		(((frame_height_coded) + 15) / 16 * 5 * 16);

	if ((num_vpp_pipes_enc) > 1) {
		size += 512;
		size = ALIGN(size, 512) *
			num_vpp_pipes_enc;
	}

	return ALIGN(size, 256);
}

static inline
u32 size_left_linebuff_recon_pix(bool is_ten_bit, u32 frame_height_coded,
				 u32 num_vpp_pipes_enc)
{
	return (((is_ten_bit + 1) * 2 * (frame_height_coded) + 256) +
		(256 << (num_vpp_pipes_enc - 1)) - 1) &
		(~((256 << (num_vpp_pipes_enc - 1)) - 1)) * 1;
}

static inline
u32 size_top_linebuff_ctrl_fe(u32 frame_width_coded, u32 standard)
{
	return standard == HFI_CODEC_ENCODE_HEVC ?
		ALIGN((64 * ((frame_width_coded) >> 5)), 256) :
		ALIGN((256 + 16 * ((frame_width_coded) >> 4)), 256);
}

static inline
u32 size_left_linebuff_ctrl_fe(u32 frame_height_coded, u32 num_vpp_pipes_enc)
{
	return (((256 + 64 * ((frame_height_coded) >> 4)) +
		 (256 << (num_vpp_pipes_enc - 1)) - 1) &
		 (~((256 << (num_vpp_pipes_enc - 1)) - 1)) * 1) *
		num_vpp_pipes_enc;
}

static inline
u32 size_left_linebuff_metadata_recon_y(u32 frame_height_coded,
					bool is_ten_bit,
					u32 num_vpp_pipes_enc)
{
	return ALIGN(((256 + 64 * ((frame_height_coded) /
		  (8 * (is_ten_bit ? 4 : 8))))), 256) * num_vpp_pipes_enc;
}

static inline
u32 size_left_linebuff_metadata_recon_uv(u32 frame_height_coded,
					 bool is_ten_bit,
					 u32 num_vpp_pipes_enc)
{
	return ALIGN(((256 + 64 * ((frame_height_coded) /
		  (4 * (is_ten_bit ? 4 : 8))))), 256) * num_vpp_pipes_enc;
}

static inline
u32 size_linebuff_recon_pix(bool is_ten_bit, u32 frame_width_coded)
{
	return ALIGN(((is_ten_bit ? 3 : 2) * (frame_width_coded)), 256);
}

static inline
u32 size_line_buf_ctrl(u32 frame_width_coded)
{
	return ALIGN(frame_width_coded, 256);
}

static inline
u32 size_line_buf_ctrl_id2(u32 frame_width_coded)
{
	return ALIGN(frame_width_coded, 256);
}

static inline u32 size_line_buf_sde(u32 frame_width_coded)
{
	return ALIGN((256 + (16 * ((frame_width_coded) >> 4))), 256);
}

static inline
u32 size_vpss_line_buf(u32 num_vpp_pipes_enc, u32 frame_height_coded,
		       u32 frame_width_coded)
{
	return ALIGN(((((((8192) >> 2) << 5) * (num_vpp_pipes_enc)) + 64) +
		      (((((max_t(u32, (frame_width_coded),
				 (frame_height_coded)) + 3) >> 2) << 5) + 256) * 16)), 256);
}
static inline
u32 size_vpss_line_buf_vpu33(u32 num_vpp_pipes_enc, u32 frame_height_coded,
			     u32 frame_width_coded)
{
	u32 vpss_4tap_top, vpss_4tap_left, vpss_div2_top;
	u32 vpss_div2_left, vpss_top_lb, vpss_left_lb;
	u32 size_left, size_top;
	u32 max_width_height;

	max_width_height = max_t(u32, frame_width_coded, frame_height_coded);
	vpss_4tap_top = ((((max_width_height * 2) + 3) >> 2) << 4) + 256;
	vpss_4tap_left = (((8192 + 3) >> 2) << 5) + 64;
	vpss_div2_top = (((max_width_height + 3) >> 2) << 4) + 256;
	vpss_div2_left = ((((max_width_height * 2) + 3) >> 2) << 5) + 64;
	vpss_top_lb = (frame_width_coded + 1) << 3;
	vpss_left_lb = (frame_height_coded << 3) * num_vpp_pipes_enc;
	size_left = (vpss_4tap_left + vpss_div2_left) * 2 * num_vpp_pipes_enc;
	size_top = (vpss_4tap_top + vpss_div2_top) * 2;

	return ALIGN(size_left + size_top + vpss_top_lb + vpss_left_lb, DMA_ALIGNMENT);
}

static inline
u32 size_top_line_buf_first_stg_sao(u32 frame_width_coded)
{
	return ALIGN((16 * ((frame_width_coded) >> 5)), 256);
}

static inline
u32 size_enc_ref_buffer(u32 frame_width, u32 frame_height)
{
	u32 u_chroma_buffer_height = ALIGN(frame_height >> 1, 32);
	u32 u_buffer_height = ALIGN(frame_height, 32);
	u32 u_buffer_width = ALIGN(frame_width, 32);

	return (u_buffer_height + u_chroma_buffer_height) * u_buffer_width;
}

static inline
u32 size_enc_ten_bit_ref_buffer(u32 frame_width, u32 frame_height)
{
	u32 ref_luma_stride_in_bytes = ((frame_width + SYSTEM_LAL_TILE10 - 1) / SYSTEM_LAL_TILE10) *
		SYSTEM_LAL_TILE10;
	u32 ref_buf_height = (frame_height + (32 - 1)) & (~(32 - 1));
	u32 u_ref_stride, luma_size;
	u32 ref_chrm_height_in_bytes;
	u32 chroma_size;

	u_ref_stride = 4 * (ref_luma_stride_in_bytes / 3);
	u_ref_stride = (u_ref_stride + (128 - 1)) & (~(128 - 1));
	luma_size = ref_buf_height * u_ref_stride;
	luma_size = (luma_size + (4096 - 1)) & (~(4096 - 1));

	ref_chrm_height_in_bytes = (((frame_height + 1) >> 1) + (32 - 1)) & (~(32 - 1));
	chroma_size = u_ref_stride * ref_chrm_height_in_bytes;
	chroma_size = (chroma_size + (4096 - 1)) & (~(4096 - 1));

	return luma_size + chroma_size;
}

static inline
u32 hfi_ubwc_calc_metadata_plane_stride(u32 frame_width,
					u32 metadata_stride_multiple,
					u32 tile_width_in_pels)
{
	return ALIGN(((frame_width + (tile_width_in_pels - 1)) / tile_width_in_pels),
		     metadata_stride_multiple);
}

static inline
u32 hfi_ubwc_metadata_plane_bufheight(u32 frame_height,
				      u32 metadata_height_multiple,
				      u32 tile_height_in_pels)
{
	return ALIGN(((frame_height + (tile_height_in_pels - 1)) / tile_height_in_pels),
		     metadata_height_multiple);
}

static inline
u32 hfi_ubwc_metadata_plane_buffer_size(u32 _metadata_tride, u32 _metadata_buf_height)
{
	return ALIGN(_metadata_tride * _metadata_buf_height, 4096);
}

static inline
u32 hfi_buffer_non_comv_enc(u32 frame_width, u32 frame_height,
			    u32 num_vpp_pipes_enc, u32 lcu_size, u32 standard)
{
	u32 height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	u32 width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	u32 num_lcu_in_frame = width_in_lcus * height_in_lcus;
	u32 frame_height_coded = height_in_lcus * (lcu_size);
	u32 frame_width_coded = width_in_lcus * (lcu_size);
	u32 num_lcumb, frame_rc_buf_size;

	num_lcumb = (frame_height_coded / lcu_size) *
		((frame_width_coded + lcu_size * 8) / lcu_size);
	frame_rc_buf_size = size_frame_rc_buf_size(standard, frame_height_coded,
						   num_vpp_pipes_enc);
	return size_enc_slice_info_buf(num_lcu_in_frame) +
		SIZE_SLICE_CMD_BUFFER +
		SIZE_SPS_PPS_SLICE_HDR +
		frame_rc_buf_size +
		enc_bitcnt_buf_size(num_lcu_in_frame) +
		enc_bitmap_buf_size(num_lcu_in_frame) +
		SIZE_BSE_SLICE_CMD_BUF +
		SIZE_LAMBDA_LUT +
		size_override_buf(num_lcumb) +
		size_ir_buf(num_lcu_in_frame);
}

static u32 iris_vpu_enc_non_comv_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	u32 lcu_size = 16;

	if (inst->codec == V4L2_PIX_FMT_HEVC) {
		lcu_size = 32;
		return hfi_buffer_non_comv_enc(width, height, num_vpp_pipes,
					       lcu_size, HFI_CODEC_ENCODE_HEVC) +
					       SIZE_ONE_SLICE_BUF;
	}

	return hfi_buffer_non_comv_enc(width, height, num_vpp_pipes,
				       lcu_size, HFI_CODEC_ENCODE_AVC);
}

static inline
u32 hfi_buffer_line_enc_base(u32 frame_width, u32 frame_height, bool is_ten_bit,
			     u32 num_vpp_pipes_enc, u32 lcu_size, u32 standard)
{
	u32 width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	u32 height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	u32 frame_height_coded = height_in_lcus * (lcu_size);
	u32 frame_width_coded = width_in_lcus * (lcu_size);
	u32 line_buff_data_size, left_line_buff_ctrl_size;
	u32 left_line_buff_metadata_recon__uv__size;
	u32 left_line_buff_metadata_recon__y__size;
	u32 left_line_buff_recon_pix_size;
	u32 top_line_buff_ctrl_fe_size;
	u32 line_buff_recon_pix_size;

	line_buff_data_size = size_linebuff_data(is_ten_bit, frame_width_coded);
	left_line_buff_ctrl_size =
		size_left_linebuff_ctrl(standard, frame_height_coded, num_vpp_pipes_enc);
	left_line_buff_recon_pix_size =
		size_left_linebuff_recon_pix(is_ten_bit, frame_height_coded,
					     num_vpp_pipes_enc);
	top_line_buff_ctrl_fe_size =
		size_top_linebuff_ctrl_fe(frame_width_coded, standard);
	left_line_buff_metadata_recon__y__size =
		size_left_linebuff_metadata_recon_y(frame_height_coded, is_ten_bit,
						    num_vpp_pipes_enc);
	left_line_buff_metadata_recon__uv__size =
		size_left_linebuff_metadata_recon_uv(frame_height_coded, is_ten_bit,
						     num_vpp_pipes_enc);
	line_buff_recon_pix_size = size_linebuff_recon_pix(is_ten_bit, frame_width_coded);

	return size_line_buf_ctrl(frame_width_coded) +
		size_line_buf_ctrl_id2(frame_width_coded) +
		line_buff_data_size +
		left_line_buff_ctrl_size +
		left_line_buff_recon_pix_size +
		top_line_buff_ctrl_fe_size +
		left_line_buff_metadata_recon__y__size +
		left_line_buff_metadata_recon__uv__size +
		line_buff_recon_pix_size +
		size_left_linebuff_ctrl_fe(frame_height_coded, num_vpp_pipes_enc) +
		size_line_buf_sde(frame_width_coded) +
		size_top_line_buf_first_stg_sao(frame_width_coded);
}

static inline
u32 hfi_buffer_line_enc(u32 frame_width, u32 frame_height, bool is_ten_bit,
			u32 num_vpp_pipes_enc, u32 lcu_size, u32 standard)
{
	u32 width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	u32 height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	u32 frame_height_coded = height_in_lcus * (lcu_size);
	u32 frame_width_coded = width_in_lcus * (lcu_size);

	return hfi_buffer_line_enc_base(frame_width, frame_height, is_ten_bit,
					num_vpp_pipes_enc, lcu_size, standard) +
		size_vpss_line_buf(num_vpp_pipes_enc, frame_height_coded, frame_width_coded);
}

static inline
u32 hfi_buffer_line_enc_vpu33(u32 frame_width, u32 frame_height, bool is_ten_bit,
			      u32 num_vpp_pipes_enc, u32 lcu_size, u32 standard)
{
	u32 width_in_lcus = ((frame_width) + (lcu_size) - 1) / (lcu_size);
	u32 height_in_lcus = ((frame_height) + (lcu_size) - 1) / (lcu_size);
	u32 frame_height_coded = height_in_lcus * (lcu_size);
	u32 frame_width_coded = width_in_lcus * (lcu_size);

	return hfi_buffer_line_enc_base(frame_width, frame_height, is_ten_bit,
					num_vpp_pipes_enc, lcu_size, standard) +
		size_vpss_line_buf_vpu33(num_vpp_pipes_enc, frame_height_coded,
					 frame_width_coded);
}

static u32 iris_vpu_enc_line_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	u32 lcu_size = 16;

	if (inst->codec == V4L2_PIX_FMT_HEVC) {
		lcu_size = 32;
		return hfi_buffer_line_enc(width, height, 0, num_vpp_pipes,
					   lcu_size, HFI_CODEC_ENCODE_HEVC);
	}

	return hfi_buffer_line_enc(width, height, 0, num_vpp_pipes,
				   lcu_size, HFI_CODEC_ENCODE_AVC);
}

static u32 iris_vpu33_enc_line_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	u32 lcu_size = 16;

	if (inst->codec == V4L2_PIX_FMT_HEVC) {
		lcu_size = 32;
		return hfi_buffer_line_enc_vpu33(width, height, 0, num_vpp_pipes,
						 lcu_size, HFI_CODEC_ENCODE_HEVC);
	}

	return hfi_buffer_line_enc_vpu33(width, height, 0, num_vpp_pipes,
					 lcu_size, HFI_CODEC_ENCODE_AVC);
}

static inline
u32 hfi_buffer_dpb_enc(u32 frame_width, u32 frame_height, bool is_ten_bit)
{
	u32 metadata_stride, metadata_buf_height, meta_size_y, meta_size_c;
	u32 ten_bit_ref_buf_size = 0, ref_buf_size = 0;
	u32 size;

	if (!is_ten_bit) {
		ref_buf_size = size_enc_ref_buffer(frame_width, frame_height);
		metadata_stride =
			hfi_ubwc_calc_metadata_plane_stride(frame_width, 64,
							    HFI_COL_FMT_NV12C_Y_TILE_WIDTH);
		metadata_buf_height =
			hfi_ubwc_metadata_plane_bufheight(frame_height, 16,
							  HFI_COL_FMT_NV12C_Y_TILE_HEIGHT);
		meta_size_y =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		meta_size_c =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		size = ref_buf_size + meta_size_y + meta_size_c;
	} else {
		ten_bit_ref_buf_size = size_enc_ten_bit_ref_buffer(frame_width, frame_height);
		metadata_stride =
			hfi_ubwc_calc_metadata_plane_stride(frame_width,
							    IRIS_METADATA_STRIDE_MULTIPLE,
							    HFI_COL_FMT_TP10C_Y_TILE_WIDTH);
		metadata_buf_height =
			hfi_ubwc_metadata_plane_bufheight(frame_height,
							  IRIS_METADATA_HEIGHT_MULTIPLE,
							  HFI_COL_FMT_TP10C_Y_TILE_HEIGHT);
		meta_size_y =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		meta_size_c =
			hfi_ubwc_metadata_plane_buffer_size(metadata_stride, metadata_buf_height);
		size = ten_bit_ref_buf_size + meta_size_y + meta_size_c;
	}

	return size;
}

static u32 iris_vpu_enc_arp_size(struct iris_inst *inst)
{
	return HFI_BUFFER_ARP_ENC;
}

inline bool is_scaling_enabled(struct iris_inst *inst)
{
	return inst->crop.left != inst->compose.left ||
		inst->crop.top != inst->compose.top ||
		inst->crop.width != inst->compose.width ||
		inst->crop.height != inst->compose.height;
}

static inline
u32 hfi_buffer_vpss_enc(u32 dswidth, u32 dsheight, bool ds_enable,
			u32 blur, bool is_ten_bit)
{
	if (ds_enable || blur)
		return hfi_buffer_dpb_enc(dswidth, dsheight, is_ten_bit);

	return 0;
}

static inline u32 hfi_buffer_scratch1_enc(u32 frame_width, u32 frame_height,
					  u32 lcu_size, u32 num_ref,
					  bool ten_bit, u32 num_vpp_pipes,
					  bool is_h265)
{
	u32 line_buf_ctrl_size, line_buf_data_size, leftline_buf_ctrl_size;
	u32 line_buf_sde_size, sps_pps_slice_hdr, topline_buf_ctrl_size_FE;
	u32 leftline_buf_ctrl_size_FE, line_buf_recon_pix_size;
	u32 leftline_buf_recon_pix_size, lambda_lut_size, override_buffer_size;
	u32 col_mv_buf_size, vpp_reg_buffer_size, ir_buffer_size;
	u32 vpss_line_buf, leftline_buf_meta_recony, h265e_colrcbuf_size;
	u32 h265e_framerc_bufsize, h265e_lcubitcnt_bufsize;
	u32 h265e_lcubitmap_bufsize, se_stats_bufsize;
	u32 bse_reg_buffer_size, bse_slice_cmd_buffer_size, slice_info_bufsize;
	u32 line_buf_ctrl_size_buffid2, slice_cmd_buffer_size;
	u32 width_lcu_num, height_lcu_num, width_coded, height_coded;
	u32 frame_num_lcu, linebuf_meta_recon_uv, topline_bufsize_fe_1stg_sao;
	u32 vpss_line_buffer_size_1;
	u32 bit_depth, num_lcu_mb;

	width_lcu_num = (frame_width + lcu_size - 1) / lcu_size;
	height_lcu_num = (frame_height + lcu_size - 1) / lcu_size;
	frame_num_lcu = width_lcu_num * height_lcu_num;
	width_coded = width_lcu_num * lcu_size;
	height_coded = height_lcu_num * lcu_size;
	num_lcu_mb = (height_coded / lcu_size) *
		     ((width_coded + lcu_size * 8) / lcu_size);
	slice_info_bufsize = 256 + (frame_num_lcu << 4);
	slice_info_bufsize = ALIGN(slice_info_bufsize, 256);
	line_buf_ctrl_size = ALIGN(width_coded, 256);
	line_buf_ctrl_size_buffid2 = ALIGN(width_coded, 256);

	bit_depth = ten_bit ? 10 : 8;
	line_buf_data_size =
		(((((bit_depth * width_coded + 1024) + (256 - 1)) &
		   (~(256 - 1))) * 1) +
		 (((((bit_depth * width_coded + 1024) >> 1) + (256 - 1)) &
		   (~(256 - 1))) * 2));

	leftline_buf_ctrl_size = is_h265 ? ((height_coded + 32) / 32 * 4 * 16) :
					   ((height_coded + 15) / 16 * 5 * 16);

	if (num_vpp_pipes > 1) {
		leftline_buf_ctrl_size += 512;
		leftline_buf_ctrl_size =
			ALIGN(leftline_buf_ctrl_size, 512) * num_vpp_pipes;
	}

	leftline_buf_ctrl_size = ALIGN(leftline_buf_ctrl_size, 256);
	leftline_buf_recon_pix_size =
		(((ten_bit + 1) * 2 * (height_coded) + 256) +
		 (256 << (num_vpp_pipes - 1)) - 1) &
		(~((256 << (num_vpp_pipes - 1)) - 1)) * 1;

	topline_buf_ctrl_size_FE = is_h265 ? (64 * (width_coded >> 5)) :
					     (256 + 16 * (width_coded >> 4));
	topline_buf_ctrl_size_FE = ALIGN(topline_buf_ctrl_size_FE, 256);
	leftline_buf_ctrl_size_FE =
		(((256 + 64 * (height_coded >> 4)) +
		  (256 << (num_vpp_pipes - 1)) - 1) &
		 (~((256 << (num_vpp_pipes - 1)) - 1)) * 1) *
		num_vpp_pipes;
	leftline_buf_meta_recony =
		(256 + 64 * ((height_coded) / (8 * (ten_bit ? 4 : 8))));
	leftline_buf_meta_recony = ALIGN(leftline_buf_meta_recony, 256);
	leftline_buf_meta_recony = leftline_buf_meta_recony * num_vpp_pipes;
	linebuf_meta_recon_uv =
		(256 + 64 * ((height_coded) / (4 * (ten_bit ? 4 : 8))));
	linebuf_meta_recon_uv = ALIGN(linebuf_meta_recon_uv, 256);
	linebuf_meta_recon_uv = linebuf_meta_recon_uv * num_vpp_pipes;
	line_buf_recon_pix_size = ((ten_bit ? 3 : 2) * width_coded);
	line_buf_recon_pix_size = ALIGN(line_buf_recon_pix_size, 256);
	slice_cmd_buffer_size = ALIGN(20480, 256);
	sps_pps_slice_hdr = 2048 + 4096;
	col_mv_buf_size =
		is_h265 ? (16 * ((frame_num_lcu << 2) + 32)) :
			  (3 * 16 * (width_lcu_num * height_lcu_num + 32));
	col_mv_buf_size = ALIGN(col_mv_buf_size, 256) * (num_ref + 1);
	h265e_colrcbuf_size =
		(((width_lcu_num + 7) >> 3) * 16 * 2 * height_lcu_num);
	if (num_vpp_pipes > 1)
		h265e_colrcbuf_size =
			ALIGN(h265e_colrcbuf_size, 256) * num_vpp_pipes;

	h265e_colrcbuf_size =
		ALIGN(h265e_colrcbuf_size, 256) * HFI_MAX_COL_FRAME;
	h265e_framerc_bufsize =
		(is_h265) ?
			(256 + 16 * (14 + (((height_coded >> 5) + 7) >> 3))) :
			(256 + 16 * (14 + (((height_coded >> 4) + 7) >> 3)));
	h265e_framerc_bufsize *= 6;
	if (num_vpp_pipes > 1)
		h265e_framerc_bufsize =
			ALIGN(h265e_framerc_bufsize, 256) * num_vpp_pipes;

	h265e_framerc_bufsize =
		ALIGN(h265e_framerc_bufsize, 512) * HFI_MAX_COL_FRAME;
	h265e_lcubitcnt_bufsize = 256 + 4 * frame_num_lcu;
	h265e_lcubitcnt_bufsize = ALIGN(h265e_lcubitcnt_bufsize, 256);
	h265e_lcubitmap_bufsize = 256 + (frame_num_lcu >> 3);
	h265e_lcubitmap_bufsize = ALIGN(h265e_lcubitmap_bufsize, 256);
	line_buf_sde_size = 256 + 16 * (width_coded >> 4);
	line_buf_sde_size = ALIGN(line_buf_sde_size, 256);
	if ((width_coded * height_coded) > (4096 * 2160))
		se_stats_bufsize = 0;
	else if ((width_coded * height_coded) > (1920 * 1088))
		se_stats_bufsize = (40 * 4 * frame_num_lcu + 256 + 256);
	else
		se_stats_bufsize = (1024 * frame_num_lcu + 256 + 256);

	se_stats_bufsize = ALIGN(se_stats_bufsize, 256) * 2;
	bse_slice_cmd_buffer_size = (((8192 << 2) + 7) & (~7)) * 6;
	bse_reg_buffer_size = (((512 << 3) + 7) & (~7)) * 4;
	vpp_reg_buffer_size = (((2048 << 3) + 31) & (~31)) * 10;
	lambda_lut_size = 256 * 11;
	override_buffer_size = 16 * ((num_lcu_mb + 7) >> 3);
	override_buffer_size = ALIGN(override_buffer_size, 256) * 2;
	ir_buffer_size = (((frame_num_lcu << 1) + 7) & (~7)) * 3;
	vpss_line_buffer_size_1 = (((8192 >> 2) << 5) * num_vpp_pipes) + 64;
	vpss_line_buf =
		(((((max(width_coded, height_coded) + 3) >> 2) << 5) + 256) *
		 16) +
		vpss_line_buffer_size_1;
	topline_bufsize_fe_1stg_sao = 16 * (width_coded >> 5);
	topline_bufsize_fe_1stg_sao = ALIGN(topline_bufsize_fe_1stg_sao, 256);

	return line_buf_ctrl_size + line_buf_data_size +
	       line_buf_ctrl_size_buffid2 + leftline_buf_ctrl_size +
	       vpss_line_buf + col_mv_buf_size + topline_buf_ctrl_size_FE +
	       leftline_buf_ctrl_size_FE + line_buf_recon_pix_size +
	       leftline_buf_recon_pix_size + leftline_buf_meta_recony +
	       linebuf_meta_recon_uv + h265e_colrcbuf_size +
	       h265e_framerc_bufsize + h265e_lcubitcnt_bufsize +
	       h265e_lcubitmap_bufsize + line_buf_sde_size +
	       topline_bufsize_fe_1stg_sao + override_buffer_size +
	       bse_reg_buffer_size + vpp_reg_buffer_size + sps_pps_slice_hdr +
	       slice_cmd_buffer_size + bse_slice_cmd_buffer_size +
	       ir_buffer_size + slice_info_bufsize + lambda_lut_size +
	       se_stats_bufsize + 1024;
}

static u32 iris_vpu_enc_scratch1_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_dst;
	u32 frame_height = f->fmt.pix_mp.height;
	u32 frame_width = f->fmt.pix_mp.width;
	u32 num_ref = 1;
	u32 lcu_size;
	bool is_h265;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		lcu_size = 16;
		is_h265 = false;
	} else if (inst->codec == V4L2_PIX_FMT_HEVC) {
		lcu_size = 32;
		is_h265 = true;
	} else {
		return 0;
	}

	return hfi_buffer_scratch1_enc(frame_width, frame_height, lcu_size,
				       num_ref, false, num_vpp_pipes, is_h265);
}

static inline u32 ubwc_metadata_plane_stride(u32 width,
					     u32 metadata_stride_multi,
					     u32 tile_width_pels)
{
	return ALIGN(((width + (tile_width_pels - 1)) / tile_width_pels),
		     metadata_stride_multi);
}

static inline u32 ubwc_metadata_plane_bufheight(u32 height,
						u32 metadata_height_multi,
						u32 tile_height_pels)
{
	return ALIGN(((height + (tile_height_pels - 1)) / tile_height_pels),
		     metadata_height_multi);
}

static inline u32 ubwc_metadata_plane_buffer_size(u32 metadata_stride,
						  u32 metadata_buf_height)
{
	return ALIGN(metadata_stride * metadata_buf_height, SZ_4K);
}

static inline u32 hfi_buffer_scratch2_enc(u32 frame_width, u32 frame_height,
					  u32 num_ref, bool ten_bit)
{
	u32 aligned_width, aligned_height, chroma_height, ref_buf_height;
	u32 metadata_stride, meta_buf_height, meta_size_y, meta_size_c;
	u32 ref_luma_stride_bytes, ref_chroma_height_bytes;
	u32 ref_buf_size, ref_stride;
	u32 luma_size, chroma_size;
	u32 size;

	if (!ten_bit) {
		aligned_height = ALIGN(frame_height, 32);
		chroma_height = frame_height >> 1;
		chroma_height = ALIGN(chroma_height, 32);
		aligned_width = ALIGN(frame_width, 128);
		metadata_stride =
			ubwc_metadata_plane_stride(frame_width, 64, 32);
		meta_buf_height =
			ubwc_metadata_plane_bufheight(frame_height, 16, 8);
		meta_size_y = ubwc_metadata_plane_buffer_size(metadata_stride,
							      meta_buf_height);
		meta_size_c = ubwc_metadata_plane_buffer_size(metadata_stride,
							      meta_buf_height);
		size = (aligned_height + chroma_height) * aligned_width +
		       meta_size_y + meta_size_c;
		size = (size * (num_ref + 3)) + 4096;
	} else {
		ref_buf_height = (frame_height + (32 - 1)) & (~(32 - 1));
		ref_luma_stride_bytes = ((frame_width + 192 - 1) / 192) * 192;
		ref_stride = 4 * (ref_luma_stride_bytes / 3);
		ref_stride = (ref_stride + (128 - 1)) & (~(128 - 1));
		luma_size = ref_buf_height * ref_stride;
		ref_chroma_height_bytes =
			(((frame_height + 1) >> 1) + (32 - 1)) & (~(32 - 1));
		chroma_size = ref_stride * ref_chroma_height_bytes;
		luma_size = (luma_size + (SZ_4K - 1)) & (~(SZ_4K - 1));
		chroma_size = (chroma_size + (SZ_4K - 1)) & (~(SZ_4K - 1));
		ref_buf_size = luma_size + chroma_size;
		metadata_stride =
			ubwc_metadata_plane_stride(frame_width, 64, 48);
		meta_buf_height =
			ubwc_metadata_plane_bufheight(frame_height, 16, 4);
		meta_size_y = ubwc_metadata_plane_buffer_size(metadata_stride,
							      meta_buf_height);
		meta_size_c = ubwc_metadata_plane_buffer_size(metadata_stride,
							      meta_buf_height);
		size = ref_buf_size + meta_size_y + meta_size_c;
		size = (size * (num_ref + 3)) + 4096;
	}

	return size;
}

static u32 iris_vpu_enc_scratch2_size(struct iris_inst *inst)
{
	struct v4l2_format *f = inst->fmt_dst;
	u32 frame_width = f->fmt.pix_mp.width;
	u32 frame_height = f->fmt.pix_mp.height;
	u32 num_ref = 1;

	return hfi_buffer_scratch2_enc(frame_width, frame_height, num_ref,
				       false);
}

static u32 iris_vpu_enc_vpss_size(struct iris_inst *inst)
{
	u32 ds_enable = is_scaling_enabled(inst);
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	return hfi_buffer_vpss_enc(width, height, ds_enable, 0, 0);
}

static int output_min_count(struct iris_inst *inst)
{
	int output_min_count = 4;

	/* fw_min_count > 0 indicates reconfig event has already arrived */
	if (inst->fw_min_count) {
		if (iris_split_mode_enabled(inst) && inst->codec == V4L2_PIX_FMT_VP9)
			return min_t(u32, 4, inst->fw_min_count);
		else
			return inst->fw_min_count;
	}

	if (inst->codec == V4L2_PIX_FMT_VP9)
		output_min_count = 9;

	return output_min_count;
}

struct iris_vpu_buf_type_handle {
	enum iris_buffer_type type;
	u32 (*handle)(struct iris_inst *inst);
};

u32 iris_vpu_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type)
{
	const struct iris_vpu_buf_type_handle *buf_type_handle_arr = NULL;
	u32 size = 0, buf_type_handle_size = 0, i;

	static const struct iris_vpu_buf_type_handle dec_internal_buf_type_handle[] = {
		{BUF_BIN,         iris_vpu_dec_bin_size             },
		{BUF_COMV,        iris_vpu_dec_comv_size            },
		{BUF_NON_COMV,    iris_vpu_dec_non_comv_size        },
		{BUF_LINE,        iris_vpu_dec_line_size            },
		{BUF_PERSIST,     iris_vpu_dec_persist_size         },
		{BUF_DPB,         iris_vpu_dec_dpb_size             },
		{BUF_SCRATCH_1,   iris_vpu_dec_scratch1_size        },
	};

	static const struct iris_vpu_buf_type_handle enc_internal_buf_type_handle[] = {
		{BUF_BIN,         iris_vpu_enc_bin_size             },
		{BUF_COMV,        iris_vpu_enc_comv_size            },
		{BUF_NON_COMV,    iris_vpu_enc_non_comv_size        },
		{BUF_LINE,        iris_vpu_enc_line_size            },
		{BUF_ARP,         iris_vpu_enc_arp_size             },
		{BUF_VPSS,        iris_vpu_enc_vpss_size            },
		{BUF_SCRATCH_1,   iris_vpu_enc_scratch1_size        },
		{BUF_SCRATCH_2,   iris_vpu_enc_scratch2_size        },
	};

	if (inst->domain == DECODER) {
		buf_type_handle_size = ARRAY_SIZE(dec_internal_buf_type_handle);
		buf_type_handle_arr = dec_internal_buf_type_handle;
	} else if (inst->domain == ENCODER) {
		buf_type_handle_size = ARRAY_SIZE(enc_internal_buf_type_handle);
		buf_type_handle_arr = enc_internal_buf_type_handle;
	}

	for (i = 0; i < buf_type_handle_size; i++) {
		if (buf_type_handle_arr[i].type == buffer_type) {
			size = buf_type_handle_arr[i].handle(inst);
			break;
		}
	}

	return size;
}

u32 iris_vpu33_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type)
{
	u32 size = 0, i;

	static const struct iris_vpu_buf_type_handle enc_internal_buf_type_handle[] = {
		{BUF_BIN,         iris_vpu_enc_bin_size         },
		{BUF_COMV,        iris_vpu_enc_comv_size        },
		{BUF_NON_COMV,    iris_vpu_enc_non_comv_size    },
		{BUF_LINE,        iris_vpu33_enc_line_size      },
		{BUF_ARP,         iris_vpu_enc_arp_size         },
		{BUF_VPSS,        iris_vpu_enc_vpss_size        },
		{BUF_SCRATCH_1,   iris_vpu_enc_scratch1_size    },
		{BUF_SCRATCH_2,   iris_vpu_enc_scratch2_size    },
	};

	if (inst->domain == DECODER)
		return iris_vpu_buf_size(inst, buffer_type);

	for (i = 0; i < ARRAY_SIZE(enc_internal_buf_type_handle); i++) {
		if (enc_internal_buf_type_handle[i].type == buffer_type) {
			size = enc_internal_buf_type_handle[i].handle(inst);
			break;
		}
	}

	return size;
}

static u32 internal_buffer_count(struct iris_inst *inst,
				 enum iris_buffer_type buffer_type)
{
	if (buffer_type == BUF_BIN || buffer_type == BUF_LINE ||
	    buffer_type == BUF_PERSIST) {
		return 1;
	} else if (buffer_type == BUF_COMV || buffer_type == BUF_NON_COMV) {
		if (inst->codec == V4L2_PIX_FMT_H264 || inst->codec == V4L2_PIX_FMT_HEVC)
			return 1;
	}
	return 0;
}

static inline int iris_vpu_dpb_count(struct iris_inst *inst)
{
	if (iris_split_mode_enabled(inst)) {
		return inst->fw_min_count ?
			inst->fw_min_count : inst->buffers[BUF_OUTPUT].min_count;
	}

	return 0;
}

int iris_vpu_buf_count(struct iris_inst *inst, enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return MIN_BUFFERS;
	case BUF_OUTPUT:
		if (inst->domain == ENCODER)
			return MIN_BUFFERS;
		else
			return output_min_count(inst);
	case BUF_BIN:
	case BUF_COMV:
	case BUF_NON_COMV:
	case BUF_LINE:
	case BUF_PERSIST:
		return internal_buffer_count(inst, buffer_type);
	case BUF_SCRATCH_1:
	case BUF_SCRATCH_2:
	case BUF_VPSS:
	case BUF_ARP:
		return 1; /* internal buffer count needed by firmware is 1 */
	case BUF_DPB:
		return iris_vpu_dpb_count(inst);
	default:
		return 0;
	}
}
