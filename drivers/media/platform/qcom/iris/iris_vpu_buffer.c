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

static u32 size_h264d_bse_cmd_buf(u32 frame_height)
{
	u32 height = ALIGN(frame_height, 32);

	return min_t(u32, (DIV_ROUND_UP(height, 16) * 48), H264D_MAX_SLICE) *
		SIZE_H264D_BSE_CMD_PER_BUF;
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
	u32 size_bse, size_vpp, size;

	size_bse = size_h264d_bse_cmd_buf(frame_height);
	size_vpp = size_h264d_vpp_cmd_buf(frame_height);
	size = ALIGN(size_bse, DMA_ALIGNMENT) +
		ALIGN(size_vpp, DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H264D_HW_PIC_T), DMA_ALIGNMENT);

	return ALIGN(size, DMA_ALIGNMENT);
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

	return hfi_buffer_bin_h264d(width, height, num_vpp_pipes);
}

static u32 iris_vpu_dec_comv_size(struct iris_inst *inst)
{
	u32 num_comv = VIDEO_MAX_FRAME;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;

	return hfi_buffer_comv_h264d(width, height, num_comv);
}

static u32 iris_vpu_dec_persist_size(struct iris_inst *inst)
{
	return hfi_buffer_persist_h264d();
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

	return hfi_buffer_non_comv_h264d(width, height, num_vpp_pipes);
}

static u32 iris_vpu_dec_line_size(struct iris_inst *inst)
{
	u32 num_vpp_pipes = inst->core->iris_platform_data->num_vpp_pipe;
	struct v4l2_format *f = inst->fmt_src;
	u32 height = f->fmt.pix_mp.height;
	u32 width = f->fmt.pix_mp.width;
	bool is_opb = false;

	if (iris_split_mode_enabled(inst))
		is_opb = true;

	return hfi_buffer_line_h264d(width, height, is_opb, num_vpp_pipes);
}

static u32 iris_vpu_dec_scratch1_size(struct iris_inst *inst)
{
	return iris_vpu_dec_comv_size(inst) +
		iris_vpu_dec_non_comv_size(inst) +
		iris_vpu_dec_line_size(inst);
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
		return inst->fw_min_count;
	case BUF_BIN:
	case BUF_COMV:
	case BUF_NON_COMV:
	case BUF_LINE:
	case BUF_PERSIST:
	case BUF_SCRATCH_1:
		return 1; /* internal buffer count needed by firmware is 1 */
	case BUF_DPB:
		return iris_vpu_dpb_count(inst);
	default:
		return 0;
	}
}
