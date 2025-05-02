/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VPU_BUFFER_H__
#define __IRIS_VPU_BUFFER_H__

struct iris_inst;

#define MIN_BUFFERS			4

#define DMA_ALIGNMENT			256

#define NUM_HW_PIC_BUF			32
#define SIZE_HW_PIC(size_per_buf)	(NUM_HW_PIC_BUF * (size_per_buf))

#define MAX_TILE_COLUMNS		32
#define BIN_BUFFER_THRESHOLD		(1280 * 736)
#define VPP_CMD_MAX_SIZE		(BIT(20))
#define H264D_MAX_SLICE			1800

#define SIZE_H264D_BUFTAB_T		256
#define SIZE_H264D_BSE_CMD_PER_BUF	(32 * 4)
#define SIZE_H264D_VPP_CMD_PER_BUF	512

#define NUM_SLIST_BUF_H264		(256 + 32)
#define SIZE_SLIST_BUF_H264		512
#define H264_DISPLAY_BUF_SIZE		3328
#define H264_NUM_FRM_INFO		66

#define SIZE_SEI_USERDATA		4096

#define H264_CABAC_HDR_RATIO_HD_TOT	1
#define H264_CABAC_RES_RATIO_HD_TOT	3
#define SIZE_H264D_HW_PIC_T		(BIT(11))

#define MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE	64
#define MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE	16
#define MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE	384
#define MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE	640

static inline u32 size_h264d_lb_fe_top_data(u32 frame_width)
{
	return MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE * ALIGN(frame_width, 16) * 3;
}

static inline u32 size_h264d_lb_fe_top_ctrl(u32 frame_width)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * DIV_ROUND_UP(frame_width, 16);
}

static inline u32 size_h264d_lb_fe_left_ctrl(u32 frame_height)
{
	return MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * DIV_ROUND_UP(frame_height, 16);
}

static inline u32 size_h264d_lb_se_top_ctrl(u32 frame_width)
{
	return MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * DIV_ROUND_UP(frame_width, 16);
}

static inline u32 size_h264d_lb_se_left_ctrl(u32 frame_height)
{
	return MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * DIV_ROUND_UP(frame_height, 16);
}

static inline u32 size_h264d_lb_pe_top_data(u32 frame_width)
{
	return MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE * DIV_ROUND_UP(frame_width, 16);
}

static inline u32 size_h264d_lb_vsp_top(u32 frame_width)
{
	return (DIV_ROUND_UP(frame_width, 16) << 7);
}

static inline u32 size_h264d_lb_recon_dma_metadata_wr(u32 frame_height)
{
	return ALIGN(frame_height, 16) * 32;
}

static inline u32 size_h264d_qp(u32 frame_width, u32 frame_height)
{
	return DIV_ROUND_UP(frame_width, 64) * DIV_ROUND_UP(frame_height, 64) * 128;
}

int iris_vpu_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type);
int iris_vpu_buf_count(struct iris_inst *inst, enum iris_buffer_type buffer_type);

#endif
