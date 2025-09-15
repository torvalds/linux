// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_instance.h"
#include "iris_vpu_buffer.h"

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

int iris_vpu_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type)
{
	const struct iris_vpu_buf_type_handle *buf_type_handle_arr;
	u32 size = 0, buf_type_handle_size, i;

	static const struct iris_vpu_buf_type_handle dec_internal_buf_type_handle[] = {
		{BUF_BIN,         iris_vpu_dec_bin_size             },
		{BUF_COMV,        iris_vpu_dec_comv_size            },
		{BUF_NON_COMV,    iris_vpu_dec_non_comv_size        },
		{BUF_LINE,        iris_vpu_dec_line_size            },
		{BUF_PERSIST,     iris_vpu_dec_persist_size         },
		{BUF_DPB,         iris_vpu_dec_dpb_size             },
		{BUF_SCRATCH_1,   iris_vpu_dec_scratch1_size        },
	};

	buf_type_handle_size = ARRAY_SIZE(dec_internal_buf_type_handle);
	buf_type_handle_arr = dec_internal_buf_type_handle;

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
		return output_min_count(inst);
	case BUF_BIN:
	case BUF_COMV:
	case BUF_NON_COMV:
	case BUF_LINE:
	case BUF_PERSIST:
		return internal_buffer_count(inst, buffer_type);
	case BUF_SCRATCH_1:
		return 1; /* internal buffer count needed by firmware is 1 */
	case BUF_DPB:
		return iris_vpu_dpb_count(inst);
	default:
		return 0;
	}
}
