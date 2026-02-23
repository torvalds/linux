// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_instance.h"
#include "iris_vpu_buffer.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_hfi_gen2_defines.h"

#define HFI_MAX_COL_FRAME 6
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_HEIGHT (8)
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_WIDTH (32)
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_UV_TILE_HEIGHT (8)
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_UV_TILE_WIDTH (16)
#define HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_HEIGHT (4)
#define HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_WIDTH (48)
#define HFI_COLOR_FORMAT_YUV420_TP10_UBWC_UV_TILE_HEIGHT (4)
#define HFI_COLOR_FORMAT_YUV420_TP10_UBWC_UV_TILE_WIDTH (24)
#define AV1D_SIZE_BSE_COL_MV_64x64 512
#define AV1D_SIZE_BSE_COL_MV_128x128 2816
#define UBWC_TILE_SIZE 256

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

static u32 size_av1d_hw_bin_buffer(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 size_yuv, size_bin_hdr, size_bin_res;

	size_yuv = ((frame_width * frame_height) <= BIN_BUFFER_THRESHOLD) ?
		((BIN_BUFFER_THRESHOLD * 3) >> 1) :
		((frame_width * frame_height * 3) >> 1);
	size_bin_hdr = size_yuv * AV1_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * AV1_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr / num_vpp_pipes,
			     DMA_ALIGNMENT) * num_vpp_pipes;
	size_bin_res = ALIGN(size_bin_res / num_vpp_pipes,
			     DMA_ALIGNMENT) * num_vpp_pipes;

	return size_bin_hdr + size_bin_res;
}

static u32 hfi_buffer_bin_av1d(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 n_aligned_h = ALIGN(frame_height, 16);
	u32 n_aligned_w = ALIGN(frame_width, 16);

	return size_av1d_hw_bin_buffer(n_aligned_w, n_aligned_h, num_vpp_pipes);
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

static u32 num_lcu(u32 frame_width, u32 frame_height, u32 lcu_size)
{
	return ((frame_width + lcu_size - 1) / lcu_size) *
		((frame_height + lcu_size - 1) / lcu_size);
}

static u32 hfi_buffer_comv_av1d(u32 frame_width, u32 frame_height, u32 comv_bufcount)
{
	u32 size;

	size =  2 * ALIGN(max(num_lcu(frame_width, frame_height, 64) *
			  AV1D_SIZE_BSE_COL_MV_64x64,
			  num_lcu(frame_width, frame_height, 128) *
			  AV1D_SIZE_BSE_COL_MV_128x128),
			  DMA_ALIGNMENT);
	size *= comv_bufcount;

	return size;
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

static u32 hfi_buffer_persist_av1d(u32 max_width, u32 max_height, u32 total_ref_count)
{
	u32 comv_size, size;

	comv_size =  hfi_buffer_comv_av1d(max_width, max_height, total_ref_count);
	size = ALIGN((SIZE_AV1D_SEQUENCE_HEADER * 2 + SIZE_AV1D_METADATA +
	AV1D_NUM_HW_PIC_BUF * (SIZE_AV1D_TILE_OFFSET + SIZE_AV1D_QM) +
	AV1D_NUM_FRAME_HEADERS * (SIZE_AV1D_FRAME_HEADER +
	2 * SIZE_AV1D_PROB_TABLE) + comv_size + HDR10_HIST_EXTRADATA_SIZE +
	SIZE_AV1D_METADATA * AV1D_NUM_HW_PIC_BUF), DMA_ALIGNMENT);

	return ALIGN(size, DMA_ALIGNMENT);
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

static u32 size_av1d_lb_opb_wr1_nv12_ubwc(u32 frame_width, u32 frame_height)
{
	u32 size, y_width, y_width_a = 128;

	y_width = ALIGN(frame_width, y_width_a);

	size = ((y_width + HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_WIDTH - 1) /
		 HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_WIDTH  +
		 (AV1D_MAX_TILE_COLS - 1));
	return size * UBWC_TILE_SIZE;
}

static u32 size_av1d_lb_opb_wr1_tp10_ubwc(u32 frame_width, u32 frame_height)
{
	u32 size, y_width, y_width_a = 256;

	y_width = ALIGN(frame_width, y_width_a);

	size = ((y_width + HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_WIDTH - 1) /
		 HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_WIDTH +
		 (AV1D_MAX_TILE_COLS - 1));

	return size * UBWC_TILE_SIZE;
}

static u32 hfi_buffer_line_av1d(u32 frame_width, u32 frame_height,
				bool is_opb, u32 num_vpp_pipes)
{
	u32 size, vpss_lb_size, opbwrbufsize, opbwr8, opbwr10;

	size = ALIGN(size_av1d_lb_fe_top_data(frame_width, frame_height),
		     DMA_ALIGNMENT) +
		ALIGN(size_av1d_lb_fe_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_av1d_lb_fe_left_data(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_av1d_lb_fe_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_av1d_lb_se_left_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(size_av1d_lb_se_top_ctrl(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_av1d_lb_pe_top_data(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_av1d_lb_vsp_top(frame_width, frame_height),
		      DMA_ALIGNMENT) +
		ALIGN(size_av1d_lb_recon_dma_metadata_wr
		      (frame_width, frame_height), DMA_ALIGNMENT) * 2 +
		ALIGN(size_av1d_qp(frame_width, frame_height), DMA_ALIGNMENT);
	opbwr8 = size_av1d_lb_opb_wr1_nv12_ubwc(frame_width, frame_height);
	opbwr10 = size_av1d_lb_opb_wr1_tp10_ubwc(frame_width, frame_height);
	opbwrbufsize = opbwr8 >= opbwr10 ? opbwr8 : opbwr10;
	size = ALIGN((size + opbwrbufsize), DMA_ALIGNMENT);
	if (is_opb) {
		vpss_lb_size = size_vpss_lb(frame_width, frame_height);
		size = ALIGN((size + vpss_lb_size) * 2, DMA_ALIGNMENT);
	}

	return size;
}

static u32 size_av1d_ibc_nv12_ubwc(u32 frame_width, u32 frame_height)
{
	u32 size;
	u32 y_width_a = 128, y_height_a = 32;
	u32 uv_width_a = 128, uv_height_a = 32;
	u32 ybufsize, uvbufsize, y_width, y_height, uv_width, uv_height;
	u32 y_meta_width_a = 64, y_meta_height_a = 16;
	u32 uv_meta_width_a = 64, uv_meta_height_a = 16;
	u32 meta_height, meta_stride, meta_size;
	u32 tile_width_y = HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_WIDTH;
	u32 tile_height_y = HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_HEIGHT;
	u32 tile_width_uv = HFI_COLOR_FORMAT_YUV420_NV12_UBWC_UV_TILE_WIDTH;
	u32 tile_height_uv = HFI_COLOR_FORMAT_YUV420_NV12_UBWC_UV_TILE_HEIGHT;

	y_width = ALIGN(frame_width, y_width_a);
	y_height = ALIGN(frame_height, y_height_a);
	uv_width = ALIGN(frame_width, uv_width_a);
	uv_height = ALIGN(((frame_height + 1) >> 1), uv_height_a);
	ybufsize = ALIGN((y_width * y_height), HFI_ALIGNMENT_4096);
	uvbufsize = ALIGN(uv_width * uv_height, HFI_ALIGNMENT_4096);
	size = ybufsize + uvbufsize;
	meta_stride = ALIGN(((frame_width + (tile_width_y - 1)) / tile_width_y),
			    y_meta_width_a);
	meta_height = ALIGN(((frame_height + (tile_height_y - 1)) / tile_height_y),
			    y_meta_height_a);
	meta_size = ALIGN(meta_stride * meta_height, HFI_ALIGNMENT_4096);
	size += meta_size;
	meta_stride = ALIGN(((((frame_width + 1) >> 1) + (tile_width_uv - 1)) /
				tile_width_uv),	uv_meta_width_a);
	meta_height = ALIGN(((((frame_height + 1) >> 1) + (tile_height_uv - 1)) /
				tile_height_uv), uv_meta_height_a);
	meta_size = ALIGN(meta_stride * meta_height, HFI_ALIGNMENT_4096);
	size += meta_size;

	return size;
}

static u32 hfi_yuv420_tp10_calc_y_stride(u32 frame_width, u32 stride_multiple)
{
	u32 stride;

	stride = ALIGN(frame_width, 192);
	stride = ALIGN(stride * 4 / 3, stride_multiple);

	return stride;
}

static u32 hfi_yuv420_tp10_calc_y_bufheight(u32 frame_height, u32 min_buf_height_multiple)
{
	return ALIGN(frame_height, min_buf_height_multiple);
}

static u32 hfi_yuv420_tp10_calc_uv_stride(u32 frame_width, u32 stride_multiple)
{
	u32 stride;

	stride = ALIGN(frame_width, 192);
	stride = ALIGN(stride * 4 / 3, stride_multiple);

	return stride;
}

static u32 hfi_yuv420_tp10_calc_uv_bufheight(u32 frame_height, u32 min_buf_height_multiple)
{
	return ALIGN(((frame_height + 1) >> 1),	min_buf_height_multiple);
}

static u32 size_av1d_ibc_tp10_ubwc(u32 frame_width, u32 frame_height)
{
	u32 size;
	u32 y_width_a = 256, y_height_a = 16,
		uv_width_a = 256, uv_height_a = 16;
	u32 ybufsize, uvbufsize, y_width, y_height, uv_width, uv_height;
	u32 y_meta_width_a = 64, y_meta_height_a = 16,
		uv_meta_width_a = 64, uv_meta_height_a = 16;
	u32 meta_height, meta_stride, meta_size;
	u32 tile_width_y = HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_WIDTH;
	u32 tile_height_y = HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_HEIGHT;
	u32 tile_width_uv = HFI_COLOR_FORMAT_YUV420_TP10_UBWC_UV_TILE_WIDTH;
	u32 tile_height_uv = HFI_COLOR_FORMAT_YUV420_TP10_UBWC_UV_TILE_HEIGHT;

	y_width = hfi_yuv420_tp10_calc_y_stride(frame_width, y_width_a);
	y_height = hfi_yuv420_tp10_calc_y_bufheight(frame_height, y_height_a);
	uv_width = hfi_yuv420_tp10_calc_uv_stride(frame_width, uv_width_a);
	uv_height = hfi_yuv420_tp10_calc_uv_bufheight(frame_height, uv_height_a);
	ybufsize = ALIGN(y_width * y_height, HFI_ALIGNMENT_4096);
	uvbufsize = ALIGN(uv_width * uv_height, HFI_ALIGNMENT_4096);
	size = ybufsize + uvbufsize;
	meta_stride = ALIGN(((frame_width + (tile_width_y - 1)) / tile_width_y),
			    y_meta_width_a);
	meta_height = ALIGN(((frame_height + (tile_height_y - 1)) / tile_height_y),
			    y_meta_height_a);
	meta_size = ALIGN(meta_stride * meta_height, HFI_ALIGNMENT_4096);
	size += meta_size;
	meta_stride = ALIGN(((((frame_width + 1) >> 1) + (tile_width_uv - 1)) /
				tile_width_uv), uv_meta_width_a);
	meta_height = ALIGN(((((frame_height + 1) >> 1) + (tile_height_uv - 1)) /
				tile_height_uv), uv_meta_height_a);
	meta_size = ALIGN(meta_stride * meta_height, HFI_ALIGNMENT_4096);
	size += meta_size;

	return size;
}

static u32 hfi_buffer_ibc_av1d(u32 frame_width, u32 frame_height)
{
	u32 size, ibc8, ibc10;

	ibc8 = size_av1d_ibc_nv12_ubwc(frame_width, frame_height);
	ibc10 = size_av1d_ibc_tp10_ubwc(frame_width, frame_height);
	size = ibc8 >= ibc10 ? ibc8 : ibc10;

	return ALIGN(size, DMA_ALIGNMENT);
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
	else if (inst->codec == V4L2_PIX_FMT_AV1)
		return hfi_buffer_bin_av1d(width, height, num_vpp_pipes);

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
	else if (inst->codec == V4L2_PIX_FMT_AV1) {
		if (inst->fw_caps[DRAP].value)
			return 0;
		else
			return hfi_buffer_comv_av1d(width, height, num_comv);
	}

	return 0;
}

static u32 iris_vpu_dec_persist_size(struct iris_inst *inst)
{
	struct platform_inst_caps *caps;

	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_persist_h264d();
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_persist_h265d(0);
	else if (inst->codec == V4L2_PIX_FMT_VP9)
		return hfi_buffer_persist_vp9d();
	else if (inst->codec == V4L2_PIX_FMT_AV1) {
		caps = inst->core->iris_platform_data->inst_caps;
		if (inst->fw_caps[DRAP].value)
			return hfi_buffer_persist_av1d(caps->max_frame_width,
			caps->max_frame_height, 16);
		else
			return hfi_buffer_persist_av1d(0, 0, 0);
	}

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
	else if (inst->codec == V4L2_PIX_FMT_AV1)
		return hfi_buffer_line_av1d(width, height, is_opb, num_vpp_pipes);

	return 0;
}

static u32 iris_vpu_dec_scratch1_size(struct iris_inst *inst)
{
	return iris_vpu_dec_comv_size(inst) +
		iris_vpu_dec_non_comv_size(inst) +
		iris_vpu_dec_line_size(inst);
}

static inline u32 iris_vpu_enc_get_bitstream_width(struct iris_inst *inst)
{
	if (is_rotation_90_or_270(inst))
		return inst->fmt_dst->fmt.pix_mp.height;
	else
		return inst->fmt_dst->fmt.pix_mp.width;
}

static inline u32 iris_vpu_enc_get_bitstream_height(struct iris_inst *inst)
{
	if (is_rotation_90_or_270(inst))
		return inst->fmt_dst->fmt.pix_mp.width;
	else
		return inst->fmt_dst->fmt.pix_mp.height;
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
	u32 height = iris_vpu_enc_get_bitstream_height(inst);
	u32 width = iris_vpu_enc_get_bitstream_width(inst);
	u32 stage = inst->fw_caps[STAGE].value;
	u32 lcu_size;

	if (inst->codec == V4L2_PIX_FMT_HEVC)
		lcu_size = 32;
	else
		lcu_size = 16;

	return hfi_buffer_bin_enc(width, height, stage, lcu_size,
				  num_vpp_pipes, inst->hfi_rc_type);
}

static u32 iris_vpu_dec_partial_size(struct iris_inst *inst)
{
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	return hfi_buffer_ibc_av1d(width, height);
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
	u32 height = iris_vpu_enc_get_bitstream_height(inst);
	u32 width = iris_vpu_enc_get_bitstream_width(inst);
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
	u32 height = iris_vpu_enc_get_bitstream_height(inst);
	u32 width = iris_vpu_enc_get_bitstream_width(inst);
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
	u32 height = iris_vpu_enc_get_bitstream_height(inst);
	u32 width = iris_vpu_enc_get_bitstream_width(inst);
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
	u32 height = iris_vpu_enc_get_bitstream_height(inst);
	u32 width = iris_vpu_enc_get_bitstream_width(inst);
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
	struct v4l2_pix_format_mplane *dst_fmt = &inst->fmt_dst->fmt.pix_mp;
	struct v4l2_pix_format_mplane *src_fmt = &inst->fmt_src->fmt.pix_mp;

	return dst_fmt->width != src_fmt->width ||
		dst_fmt->height != src_fmt->height;
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
	u32 frame_height = iris_vpu_enc_get_bitstream_height(inst);
	u32 frame_width = iris_vpu_enc_get_bitstream_width(inst);
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
	u32 frame_height = iris_vpu_enc_get_bitstream_height(inst);
	u32 frame_width = iris_vpu_enc_get_bitstream_width(inst);
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

static inline u32 size_dpb_opb(u32 height, u32 lcu_size)
{
	u32 max_tile_height = ((height + lcu_size - 1) / lcu_size) * lcu_size + 8;
	u32 dpb_opb = 3 * ((max_tile_height >> 3) * DMA_ALIGNMENT);
	u32 num_luma_chrome_plane = 2;

	return ALIGN(dpb_opb, DMA_ALIGNMENT) * num_luma_chrome_plane;
}

static u32 hfi_vpu4x_vp9d_lb_size(u32 frame_width, u32 frame_height, u32 num_vpp_pipes)
{
	u32 vp9_top_lb, vp9_fe_left_lb, vp9_se_left_lb, dpb_opb, vp9d_qp, num_lcu_per_pipe;
	u32 lcu_size = 64;

	vp9_top_lb = ALIGN(size_vp9d_lb_vsp_top(frame_width, frame_height), DMA_ALIGNMENT);
	vp9_top_lb += ALIGN(size_vpxd_lb_se_top_ctrl(frame_width, frame_height), DMA_ALIGNMENT);
	vp9_top_lb += max3(DIV_ROUND_UP(frame_width, BUFFER_ALIGNMENT_16_BYTES) *
			   MAX_PE_NBR_DATA_LCU16_LINE_BUFFER_SIZE,
			   DIV_ROUND_UP(frame_width, BUFFER_ALIGNMENT_32_BYTES) *
			   MAX_PE_NBR_DATA_LCU32_LINE_BUFFER_SIZE,
			   DIV_ROUND_UP(frame_width, BUFFER_ALIGNMENT_64_BYTES) *
			   MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE);
	vp9_top_lb = ALIGN(vp9_top_lb, DMA_ALIGNMENT);
	vp9_top_lb += ALIGN((DMA_ALIGNMENT * DIV_ROUND_UP(frame_width, lcu_size)),
			    DMA_ALIGNMENT) * FE_TOP_CTRL_LINE_NUMBERS;
	vp9_top_lb += ALIGN(DMA_ALIGNMENT * 8 * DIV_ROUND_UP(frame_width, lcu_size),
			    DMA_ALIGNMENT) * (FE_TOP_DATA_LUMA_LINE_NUMBERS +
			    FE_TOP_DATA_CHROMA_LINE_NUMBERS);

	num_lcu_per_pipe = (DIV_ROUND_UP(frame_height, lcu_size) / num_vpp_pipes) +
			      (DIV_ROUND_UP(frame_height, lcu_size) % num_vpp_pipes);
	vp9_fe_left_lb = ALIGN((DMA_ALIGNMENT * num_lcu_per_pipe), DMA_ALIGNMENT) *
				FE_LFT_CTRL_LINE_NUMBERS;
	vp9_fe_left_lb += ((ALIGN((DMA_ALIGNMENT * 8 * num_lcu_per_pipe), DMA_ALIGNMENT) *
				FE_LFT_DB_DATA_LINE_NUMBERS) +
				ALIGN((DMA_ALIGNMENT * 3 * num_lcu_per_pipe), DMA_ALIGNMENT) +
				ALIGN((DMA_ALIGNMENT * 4 * num_lcu_per_pipe), DMA_ALIGNMENT) +
				(ALIGN((DMA_ALIGNMENT * 24 * num_lcu_per_pipe), DMA_ALIGNMENT) *
				FE_LFT_LR_DATA_LINE_NUMBERS));
	vp9_fe_left_lb = vp9_fe_left_lb * num_vpp_pipes;

	vp9_se_left_lb = ALIGN(size_vpxd_lb_se_left_ctrl(frame_width, frame_height),
			       DMA_ALIGNMENT);
	dpb_opb = size_dpb_opb(frame_height, lcu_size);
	vp9d_qp = ALIGN(size_vp9d_qp(frame_width, frame_height), DMA_ALIGNMENT);

	return vp9_top_lb + vp9_fe_left_lb + (vp9_se_left_lb * num_vpp_pipes) +
			(dpb_opb * num_vpp_pipes) + vp9d_qp;
}

static u32 hfi_vpu4x_buffer_line_vp9d(u32 frame_width, u32 frame_height, u32 _yuv_bufcount_min,
				      bool is_opb, u32 num_vpp_pipes)
{
	u32 lb_size = hfi_vpu4x_vp9d_lb_size(frame_width, frame_height, num_vpp_pipes);
	u32 dpb_obp_size = 0, lcu_size = 64;

	if (is_opb)
		dpb_obp_size = size_dpb_opb(frame_height, lcu_size) * num_vpp_pipes;

	return lb_size + dpb_obp_size;
}

static u32 iris_vpu4x_dec_line_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	u32 out_min_count = inst->buffers[BUF_OUTPUT].min_count;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	bool is_opb = false;

	if (iris_split_mode_enabled(inst))
		is_opb = true;

	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_line_h264d(width, height, is_opb, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_buffer_line_h265d(width, height, is_opb, num_vpp_pipes);
	else if (inst->codec == V4L2_PIX_FMT_VP9)
		return hfi_vpu4x_buffer_line_vp9d(width, height, out_min_count, is_opb,
						  num_vpp_pipes);

	return 0;
}

static u32 hfi_vpu4x_buffer_persist_h265d(u32 rpu_enabled)
{
	return ALIGN((SIZE_SLIST_BUF_H265 * NUM_SLIST_BUF_H265 + H265_NUM_FRM_INFO *
		H265_DISPLAY_BUF_SIZE + (H265_NUM_TILE * sizeof(u32)) + (NUM_HW_PIC_BUF *
		(SIZE_SEI_USERDATA + SIZE_H265D_ARP + SIZE_THREE_DIMENSION_USERDATA)) +
		rpu_enabled * NUM_HW_PIC_BUF * SIZE_DOLBY_RPU_METADATA), DMA_ALIGNMENT);
}

static u32 hfi_vpu4x_buffer_persist_vp9d(void)
{
	return ALIGN(VP9_NUM_PROBABILITY_TABLE_BUF * VP9_PROB_TABLE_SIZE, DMA_ALIGNMENT) +
		(ALIGN(hfi_iris3_vp9d_comv_size(), DMA_ALIGNMENT) * 2) +
		ALIGN(MAX_SUPERFRAME_HEADER_LEN, DMA_ALIGNMENT) +
		ALIGN(VP9_UDC_HEADER_BUF_SIZE, DMA_ALIGNMENT) +
		ALIGN(VP9_NUM_FRAME_INFO_BUF * CCE_TILE_OFFSET_SIZE, DMA_ALIGNMENT) +
		ALIGN(VP9_NUM_FRAME_INFO_BUF * VP9_FRAME_INFO_BUF_SIZE_VPU4X, DMA_ALIGNMENT) +
		HDR10_HIST_EXTRADATA_SIZE;
}

static u32 iris_vpu4x_dec_persist_size(struct iris_inst *inst)
{
	if (inst->codec == V4L2_PIX_FMT_H264)
		return hfi_buffer_persist_h264d();
	else if (inst->codec == V4L2_PIX_FMT_HEVC)
		return hfi_vpu4x_buffer_persist_h265d(0);
	else if (inst->codec == V4L2_PIX_FMT_VP9)
		return hfi_vpu4x_buffer_persist_vp9d();

	return 0;
}

static u32 size_se_lb(u32 standard, u32 num_vpp_pipes_enc,
		      u32 frame_width_coded, u32 frame_height_coded)
{
	u32 se_tlb_size = ALIGN(frame_width_coded, DMA_ALIGNMENT);
	u32 se_llb_size = (standard == HFI_CODEC_ENCODE_HEVC) ?
			   ((frame_height_coded + BUFFER_ALIGNMENT_32_BYTES - 1) /
			    BUFFER_ALIGNMENT_32_BYTES) * LOG2_16 * LLB_UNIT_SIZE :
			   ((frame_height_coded + BUFFER_ALIGNMENT_16_BYTES - 1) /
			    BUFFER_ALIGNMENT_16_BYTES) * LOG2_32 * LLB_UNIT_SIZE;

	se_llb_size = ALIGN(se_llb_size, BUFFER_ALIGNMENT_32_BYTES);

	if (num_vpp_pipes_enc > 1)
		se_llb_size = ALIGN(se_llb_size + BUFFER_ALIGNMENT_512_BYTES,
				    DMA_ALIGNMENT) * num_vpp_pipes_enc;

	return ALIGN(se_tlb_size + se_llb_size, DMA_ALIGNMENT);
}

static u32 size_te_lb(bool is_ten_bit, u32 num_vpp_pipes_enc, u32 width_in_lcus,
		      u32 frame_height_coded, u32 frame_width_coded)
{
	u32 num_pixel_10_bit = 3, num_pixel_8_bit = 2, num_pixel_te_llb = 3;
	u32 te_llb_col_rc_size = ALIGN(32 * width_in_lcus / num_vpp_pipes_enc,
				       DMA_ALIGNMENT) * num_vpp_pipes_enc;
	u32 te_tlb_recon_data_size = ALIGN((is_ten_bit ? num_pixel_10_bit : num_pixel_8_bit) *
					frame_width_coded, DMA_ALIGNMENT);
	u32 te_llb_recon_data_size = ((1 + is_ten_bit) * num_pixel_te_llb * frame_height_coded +
				      num_vpp_pipes_enc - 1) / num_vpp_pipes_enc;
	te_llb_recon_data_size = ALIGN(te_llb_recon_data_size, DMA_ALIGNMENT) * num_vpp_pipes_enc;

	return ALIGN(te_llb_recon_data_size + te_llb_col_rc_size + te_tlb_recon_data_size,
		     DMA_ALIGNMENT);
}

static inline u32 calc_fe_tlb_size(u32 size_per_lcu, bool is_ten_bit)
{
	u32 num_pixels_fe_tlb_10_bit = 128, num_pixels_fe_tlb_8_bit = 64;

	return is_ten_bit ? (num_pixels_fe_tlb_10_bit * (size_per_lcu + 1)) :
			(size_per_lcu * num_pixels_fe_tlb_8_bit);
}

static u32 size_fe_lb(bool is_ten_bit, u32 standard, u32 num_vpp_pipes_enc,
		      u32 frame_height_coded, u32 frame_width_coded)
{
	u32 log2_lcu_size, num_cu_in_height_pipe, num_cu_in_width,
	    fb_llb_db_ctrl_size, fb_llb_db_luma_size, fb_llb_db_chroma_size,
	    fb_tlb_db_ctrl_size, fb_tlb_db_luma_size, fb_tlb_db_chroma_size,
	    fb_llb_sao_ctrl_size, fb_llb_sao_luma_size, fb_llb_sao_chroma_size,
	    fb_tlb_sao_ctrl_size, fb_tlb_sao_luma_size, fb_tlb_sao_chroma_size,
	    fb_lb_top_sdc_size, fb_lb_se_ctrl_size, fe_tlb_size, size_per_lcu;

	log2_lcu_size = (standard == HFI_CODEC_ENCODE_HEVC) ? 5 : 4;
	num_cu_in_height_pipe = ((frame_height_coded >> log2_lcu_size) + num_vpp_pipes_enc - 1) /
				 num_vpp_pipes_enc;
	num_cu_in_width = frame_width_coded >> log2_lcu_size;

	size_per_lcu = 2;
	fe_tlb_size = calc_fe_tlb_size(size_per_lcu, 1);
	fb_llb_db_ctrl_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_height_pipe;
	fb_llb_db_ctrl_size = ALIGN(fb_llb_db_ctrl_size, DMA_ALIGNMENT) * num_vpp_pipes_enc;

	size_per_lcu = (1 << (log2_lcu_size - 3));
	fe_tlb_size = calc_fe_tlb_size(size_per_lcu, is_ten_bit);
	fb_llb_db_luma_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_height_pipe;
	fb_llb_db_luma_size = ALIGN(fb_llb_db_luma_size, DMA_ALIGNMENT) * num_vpp_pipes_enc;

	size_per_lcu = ((1 << (log2_lcu_size - 4)) * 2);
	fe_tlb_size = calc_fe_tlb_size(size_per_lcu, is_ten_bit);
	fb_llb_db_chroma_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_height_pipe;
	fb_llb_db_chroma_size = ALIGN(fb_llb_db_chroma_size, DMA_ALIGNMENT) * num_vpp_pipes_enc;

	size_per_lcu = 1;
	fe_tlb_size = calc_fe_tlb_size(size_per_lcu, 1);
	fb_tlb_db_ctrl_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_width;
	fb_llb_sao_ctrl_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_height_pipe;
	fb_llb_sao_ctrl_size = fb_llb_sao_ctrl_size * num_vpp_pipes_enc;
	fb_tlb_sao_ctrl_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_width;

	size_per_lcu = ((1 << (log2_lcu_size - 3)) + 1);
	fe_tlb_size = calc_fe_tlb_size(size_per_lcu, is_ten_bit);
	fb_tlb_db_luma_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_width;

	size_per_lcu = (2 * ((1 << (log2_lcu_size - 4)) + 1));
	fe_tlb_size = calc_fe_tlb_size(size_per_lcu, is_ten_bit);
	fb_tlb_db_chroma_size = ALIGN(fe_tlb_size, DMA_ALIGNMENT) * num_cu_in_width;

	fb_llb_sao_luma_size = BUFFER_ALIGNMENT_256_BYTES * num_vpp_pipes_enc;
	fb_llb_sao_chroma_size = BUFFER_ALIGNMENT_256_BYTES * num_vpp_pipes_enc;
	fb_tlb_sao_luma_size = BUFFER_ALIGNMENT_256_BYTES;
	fb_tlb_sao_chroma_size = BUFFER_ALIGNMENT_256_BYTES;
	fb_lb_top_sdc_size = ALIGN((FE_SDC_DATA_PER_BLOCK * (frame_width_coded >> 5)),
				   DMA_ALIGNMENT);
	fb_lb_se_ctrl_size = ALIGN((SE_CTRL_DATA_PER_BLOCK * (frame_width_coded >> 5)),
				   DMA_ALIGNMENT);

	return fb_llb_db_ctrl_size + fb_llb_db_luma_size + fb_llb_db_chroma_size +
		fb_tlb_db_ctrl_size + fb_tlb_db_luma_size + fb_tlb_db_chroma_size +
		fb_llb_sao_ctrl_size + fb_llb_sao_luma_size + fb_llb_sao_chroma_size +
		fb_tlb_sao_ctrl_size + fb_tlb_sao_luma_size + fb_tlb_sao_chroma_size +
		fb_lb_top_sdc_size + fb_lb_se_ctrl_size;
}

static u32 size_md_lb(u32 standard, u32 frame_width_coded,
		      u32 frame_height_coded, u32 num_vpp_pipes_enc)
{
	u32 md_tlb_size = ALIGN(frame_width_coded, DMA_ALIGNMENT);
	u32 md_llb_size = (standard == HFI_CODEC_ENCODE_HEVC) ?
			   ((frame_height_coded + BUFFER_ALIGNMENT_32_BYTES - 1) /
			    BUFFER_ALIGNMENT_32_BYTES) * LOG2_16 * LLB_UNIT_SIZE :
			   ((frame_height_coded + BUFFER_ALIGNMENT_16_BYTES - 1) /
			    BUFFER_ALIGNMENT_16_BYTES) * LOG2_32 * LLB_UNIT_SIZE;

	md_llb_size = ALIGN(md_llb_size, BUFFER_ALIGNMENT_32_BYTES);

	if (num_vpp_pipes_enc > 1)
		md_llb_size = ALIGN(md_llb_size + BUFFER_ALIGNMENT_512_BYTES,
				    DMA_ALIGNMENT) * num_vpp_pipes_enc;

	md_llb_size = ALIGN(md_llb_size, DMA_ALIGNMENT);

	return ALIGN(md_tlb_size + md_llb_size, DMA_ALIGNMENT);
}

static u32 size_dma_opb_lb(u32 num_vpp_pipes_enc, u32 frame_width_coded,
			   u32 frame_height_coded)
{
	u32 opb_packet_bytes = 128, opb_bpp = 128, opb_size_per_row = 6;
	u32 dma_opb_wr_tlb_y_size = DIV_ROUND_UP(frame_width_coded, 16) * opb_packet_bytes;
	u32 dma_opb_wr_tlb_uv_size = DIV_ROUND_UP(frame_width_coded, 16) * opb_packet_bytes;
	u32 dma_opb_wr2_tlb_y_size = ALIGN((opb_bpp * opb_size_per_row * frame_height_coded / 8),
					   DMA_ALIGNMENT) * num_vpp_pipes_enc;
	u32 dma_opb_wr2_tlb_uv_size = ALIGN((opb_bpp * opb_size_per_row * frame_height_coded / 8),
					    DMA_ALIGNMENT) * num_vpp_pipes_enc;

	dma_opb_wr2_tlb_y_size = max(dma_opb_wr2_tlb_y_size, dma_opb_wr_tlb_y_size << 1);
	dma_opb_wr2_tlb_uv_size = max(dma_opb_wr2_tlb_uv_size, dma_opb_wr_tlb_uv_size << 1);

	return ALIGN(dma_opb_wr_tlb_y_size + dma_opb_wr_tlb_uv_size + dma_opb_wr2_tlb_y_size +
		     dma_opb_wr2_tlb_uv_size, DMA_ALIGNMENT);
}

static u32 hfi_vpu4x_buffer_line_enc(u32 frame_width, u32 frame_height,
				     bool is_ten_bit, u32 num_vpp_pipes_enc,
				     u32 lcu_size, u32 standard)
{
	u32 width_in_lcus = (frame_width + lcu_size - 1) / lcu_size;
	u32 height_in_lcus = (frame_height + lcu_size - 1) / lcu_size;
	u32 frame_width_coded = width_in_lcus * lcu_size;
	u32 frame_height_coded = height_in_lcus * lcu_size;

	u32 se_lb_size = size_se_lb(standard, num_vpp_pipes_enc, frame_width_coded,
				    frame_height_coded);
	u32 te_lb_size = size_te_lb(is_ten_bit, num_vpp_pipes_enc, width_in_lcus,
				    frame_height_coded, frame_width_coded);
	u32 fe_lb_size = size_fe_lb(is_ten_bit, standard, num_vpp_pipes_enc, frame_height_coded,
				    frame_width_coded);
	u32 md_lb_size = size_md_lb(standard, frame_width_coded, frame_height_coded,
				    num_vpp_pipes_enc);
	u32 dma_opb_lb_size = size_dma_opb_lb(num_vpp_pipes_enc, frame_width_coded,
					      frame_height_coded);
	u32 dse_lb_size = ALIGN((256 + (16 * (frame_width_coded >> 4))), DMA_ALIGNMENT);
	u32 size_vpss_lb_enc = size_vpss_line_buf_vpu33(num_vpp_pipes_enc, frame_width_coded,
							frame_height_coded);

	return se_lb_size + te_lb_size + fe_lb_size + md_lb_size + dma_opb_lb_size +
		dse_lb_size + size_vpss_lb_enc;
}

static u32 iris_vpu4x_enc_line_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	u32 lcu_size = inst->codec == V4L2_PIX_FMT_HEVC ? 32 : 16;
	struct v4l2_format *f = inst->fmt_dst;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	return hfi_vpu4x_buffer_line_enc(width, height, 0, num_vpp_pipes,
					 lcu_size, inst->codec);
}

static int output_min_count(struct iris_inst *inst)
{
	int output_min_count = 4;

	/* fw_min_count > 0 indicates reconfig event has already arrived */
	if (inst->fw_min_count) {
		if (iris_split_mode_enabled(inst) &&
		    (inst->codec == V4L2_PIX_FMT_VP9 ||
		     inst->codec == V4L2_PIX_FMT_AV1))
			return min_t(u32, 4, inst->fw_min_count);
		else
			return inst->fw_min_count;
	}

	if (inst->codec == V4L2_PIX_FMT_VP9)
		output_min_count = 9;
	else if (inst->codec == V4L2_PIX_FMT_AV1)
		output_min_count = 11;

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
		{BUF_PARTIAL,     iris_vpu_dec_partial_size         },
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

u32 iris_vpu4x_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type)
{
	const struct iris_vpu_buf_type_handle *buf_type_handle_arr = NULL;
	u32 size = 0, buf_type_handle_size = 0, i;

	static const struct iris_vpu_buf_type_handle dec_internal_buf_type_handle[] = {
		{BUF_BIN,         iris_vpu_dec_bin_size         },
		{BUF_COMV,        iris_vpu_dec_comv_size        },
		{BUF_NON_COMV,    iris_vpu_dec_non_comv_size    },
		{BUF_LINE,        iris_vpu4x_dec_line_size      },
		{BUF_PERSIST,     iris_vpu4x_dec_persist_size   },
		{BUF_DPB,         iris_vpu_dec_dpb_size         },
		{BUF_SCRATCH_1,   iris_vpu_dec_scratch1_size    },
	};

	static const struct iris_vpu_buf_type_handle enc_internal_buf_type_handle[] = {
		{BUF_BIN,         iris_vpu_enc_bin_size         },
		{BUF_COMV,        iris_vpu_enc_comv_size        },
		{BUF_NON_COMV,    iris_vpu_enc_non_comv_size    },
		{BUF_LINE,        iris_vpu4x_enc_line_size      },
		{BUF_ARP,         iris_vpu_enc_arp_size         },
		{BUF_VPSS,        iris_vpu_enc_vpss_size        },
		{BUF_SCRATCH_1,   iris_vpu_enc_scratch1_size    },
		{BUF_SCRATCH_2,   iris_vpu_enc_scratch2_size    },
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

static u32 internal_buffer_count(struct iris_inst *inst,
				 enum iris_buffer_type buffer_type)
{
	if (buffer_type == BUF_BIN || buffer_type == BUF_LINE ||
	    buffer_type == BUF_PERSIST) {
		return 1;
	} else if (buffer_type == BUF_COMV || buffer_type == BUF_NON_COMV) {
		if (inst->codec == V4L2_PIX_FMT_H264 ||
		    inst->codec == V4L2_PIX_FMT_HEVC ||
		    inst->codec == V4L2_PIX_FMT_AV1)
			return 1;
	}

	return 0;
}

static inline int iris_vpu_dpb_count(struct iris_inst *inst)
{
	if (inst->codec == V4L2_PIX_FMT_AV1)
		return 11;

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
	case BUF_NON_COMV:
		if (inst->codec == V4L2_PIX_FMT_AV1)
			return 0;
		else
			return 1;
	case BUF_BIN:
	case BUF_COMV:
	case BUF_LINE:
	case BUF_PERSIST:
		return internal_buffer_count(inst, buffer_type);
	case BUF_SCRATCH_1:
	case BUF_SCRATCH_2:
	case BUF_VPSS:
	case BUF_ARP:
	case BUF_PARTIAL:
		return 1; /* internal buffer count needed by firmware is 1 */
	case BUF_DPB:
		return iris_vpu_dpb_count(inst);
	default:
		return 0;
	}
}
