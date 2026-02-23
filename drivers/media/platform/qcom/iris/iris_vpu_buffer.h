/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VPU_BUFFER_H__
#define __IRIS_VPU_BUFFER_H__

struct iris_inst;

#define MIN_BUFFERS			4

#define DMA_ALIGNMENT			256
#define HFI_ALIGNMENT_4096      4096

#define NUM_HW_PIC_BUF			32
#define LCU_MAX_SIZE_PELS 64
#define LCU_MIN_SIZE_PELS 16
#define HDR10_HIST_EXTRADATA_SIZE (4 * 1024)

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
#define H265_NUM_TILE_COL 32
#define H265_NUM_TILE_ROW 128
#define H265_NUM_TILE (H265_NUM_TILE_ROW * H265_NUM_TILE_COL + 1)
#define SIZE_H265D_BSE_CMD_PER_BUF (16 * sizeof(u32))

#define NUM_SLIST_BUF_H265 (80 + 20)
#define SIZE_SLIST_BUF_H265 (BIT(10))
#define H265_DISPLAY_BUF_SIZE (3072)
#define H265_NUM_FRM_INFO (48)
#define SIZE_ONE_SLICE_BUF 256

#define VP9_NUM_FRAME_INFO_BUF 32
#define VP9_NUM_PROBABILITY_TABLE_BUF (VP9_NUM_FRAME_INFO_BUF + 4)
#define VP9_PROB_TABLE_SIZE (3840)
#define VP9_FRAME_INFO_BUF_SIZE (6144)
#define VP9_FRAME_INFO_BUF_SIZE_VPU4X (6400)
#define BUFFER_ALIGNMENT_16_BYTES 16
#define BUFFER_ALIGNMENT_32_BYTES 32
#define BUFFER_ALIGNMENT_64_BYTES 64
#define BUFFER_ALIGNMENT_256_BYTES 256
#define BUFFER_ALIGNMENT_512_BYTES 512
#define CCE_TILE_OFFSET_SIZE ALIGN(32 * 4 * 4, BUFFER_ALIGNMENT_32_BYTES)
#define MAX_SUPERFRAME_HEADER_LEN (34)
#define MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE 64
#define MAX_FE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE 64
#define MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE 64
#define MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE (128 / 8)
#define MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE (128 / 8)
#define VP9_UDC_HEADER_BUF_SIZE	(3 * 128)

#define SIZE_SEI_USERDATA			4096
#define SIZE_DOLBY_RPU_METADATA (41 * 1024)
#define H264_CABAC_HDR_RATIO_HD_TOT	1
#define H264_CABAC_RES_RATIO_HD_TOT	3
#define H265D_MAX_SLICE	1200
#define SIZE_H265D_HW_PIC_T SIZE_H264D_HW_PIC_T
#define H265_CABAC_HDR_RATIO_HD_TOT 2
#define H265_CABAC_RES_RATIO_HD_TOT 2
#define SIZE_H265D_VPP_CMD_PER_BUF (256)
#define SIZE_THREE_DIMENSION_USERDATA 768
#define SIZE_H265D_ARP 9728

#define VPX_DECODER_FRAME_CONCURENCY_LVL (2)
#define VPX_DECODER_FRAME_BIN_HDR_BUDGET 1
#define VPX_DECODER_FRAME_BIN_RES_BUDGET 3
#define VPX_DECODER_FRAME_BIN_DENOMINATOR 2

#define VPX_DECODER_FRAME_BIN_RES_BUDGET_RATIO (3 / 2)

#define SIZE_H264D_HW_PIC_T		(BIT(11))

#define FE_LFT_CTRL_LINE_NUMBERS 4
#define FE_LFT_DB_DATA_LINE_NUMBERS 2
#define FE_LFT_LR_DATA_LINE_NUMBERS 4
#define FE_TOP_CTRL_LINE_NUMBERS 3
#define FE_TOP_DATA_LUMA_LINE_NUMBERS 2
#define FE_TOP_DATA_CHROMA_LINE_NUMBERS 3
#define FE_SDC_DATA_PER_BLOCK 16
#define SE_CTRL_DATA_PER_BLOCK 2020

#define MAX_PE_NBR_DATA_LCU16_LINE_BUFFER_SIZE 96
#define MAX_PE_NBR_DATA_LCU32_LINE_BUFFER_SIZE 192

#define MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE	64
#define MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE	16
#define MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE	384
#define MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE	640

#define AV1_CABAC_HDR_RATIO_HD_TOT 2
#define AV1_CABAC_RES_RATIO_HD_TOT 2
#define AV1D_LCU_MAX_SIZE_PELS 128
#define AV1D_LCU_MIN_SIZE_PELS 64
#define AV1D_MAX_TILE_COLS     64
#define MAX_PE_NBR_DATA_LCU32_LINE_BUFFER_SIZE 192
#define MAX_PE_NBR_DATA_LCU16_LINE_BUFFER_SIZE 96
#define AV1D_NUM_HW_PIC_BUF    16
#define AV1D_NUM_FRAME_HEADERS 16
#define SIZE_AV1D_SEQUENCE_HEADER 768
#define SIZE_AV1D_METADATA        512
#define SIZE_AV1D_FRAME_HEADER    1280
#define SIZE_AV1D_TILE_OFFSET     65536
#define SIZE_AV1D_QM              3328
#define SIZE_AV1D_PROB_TABLE      22784

#define SIZE_SLICE_CMD_BUFFER (ALIGN(20480, 256))
#define SIZE_SPS_PPS_SLICE_HDR (2048 + 4096)
#define SIZE_BSE_SLICE_CMD_BUF ((((8192 << 2) + 7) & (~7)) * 3)
#define SIZE_LAMBDA_LUT (256 * 11)

#define HFI_COL_FMT_NV12C_Y_TILE_HEIGHT (8)
#define HFI_COL_FMT_NV12C_Y_TILE_WIDTH (32)
#define HFI_COL_FMT_TP10C_Y_TILE_HEIGHT (4)
#define HFI_COL_FMT_TP10C_Y_TILE_WIDTH (48)

#define IRIS_METADATA_STRIDE_MULTIPLE 64
#define IRIS_METADATA_HEIGHT_MULTIPLE 16

#define HFI_BUFFER_ARP_ENC 204800

#define LOG2_16 4
#define LOG2_32 5
#define LLB_UNIT_SIZE 16

#define MAX_WIDTH 4096
#define MAX_HEIGHT 2304
#define NUM_MBS_4K (DIV_ROUND_UP(MAX_WIDTH, 16) * DIV_ROUND_UP(MAX_HEIGHT, 16))
#define NUM_MBS_720P	(((ALIGN(1280, 16)) >> 4) * ((ALIGN(736, 16)) >> 4))

#define BITS_PER_PIX                   16
#define NUM_LINES_LUMA                 10
#define NUM_LINES_CHROMA               6
#define AV1D_LCU_MAX_SIZE_PELS         128
#define AV1D_LCU_MIN_SIZE_PELS         64
#define AV1D_MAX_TILE_COLS             64
#define BITS_PER_CTRL_PACK             128
#define NUM_CTRL_PACK_LCU              10

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

static inline u32 size_av1d_lb_fe_top_data(u32 frame_width, u32 frame_height)
{
	return (ALIGN(frame_width, AV1D_LCU_MAX_SIZE_PELS) *
		((BITS_PER_PIX * NUM_LINES_LUMA) >> 3) +
		  ALIGN(frame_width, AV1D_LCU_MAX_SIZE_PELS) / 2 *
		((BITS_PER_PIX * NUM_LINES_CHROMA) >> 3) * 2);
}

static inline u32 size_av1d_lb_fe_left_data(u32 frame_width, u32 frame_height)
{
	return (32 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 16) +
		16 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) / 2 +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 8) * 2 +
		24 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 16) +
		24 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) / 2 +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 12) * 2 +
		24 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 16) +
		16 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 16) +
		16 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) / 2 +
			  ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
			  AV1D_LCU_MIN_SIZE_PELS * 12) * 2);
}

static inline u32 size_av1d_lb_fe_top_ctrl(u32 frame_width, u32 frame_height)
{
	return (NUM_CTRL_PACK_LCU * ((frame_width + AV1D_LCU_MIN_SIZE_PELS - 1) /
		AV1D_LCU_MIN_SIZE_PELS) * BITS_PER_CTRL_PACK / 8);
}

static inline u32 size_av1d_lb_fe_left_ctrl(u32 frame_width, u32 frame_height)
{
	return (16 * ((ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) / 16) +
		(ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
		 AV1D_LCU_MIN_SIZE_PELS)) +
		 3 * 16 * (ALIGN(frame_height, AV1D_LCU_MAX_SIZE_PELS) /
		 AV1D_LCU_MIN_SIZE_PELS));
}

static inline u32 size_av1d_lb_se_top_ctrl(u32 frame_width, u32 frame_height)
{
	return (((frame_width + 7) / 8) * MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE);
}

static inline u32 size_av1d_lb_se_left_ctrl(u32 frame_width, u32 frame_height)
{
	return (max(((frame_height + 15) / 16) *
		MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,
		max(((frame_height + 31) / 32) *
		MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE,
		((frame_height + 63) / 64) *
		MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE)));
}

static inline u32 size_av1d_lb_pe_top_data(u32 frame_width, u32 frame_height)
{
	return (max(((frame_width + 15) / 16) *
		MAX_PE_NBR_DATA_LCU16_LINE_BUFFER_SIZE,
		max(((frame_width + 31) / 32) *
		MAX_PE_NBR_DATA_LCU32_LINE_BUFFER_SIZE,
		((frame_width + 63) / 64) *
		MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE)));
}

static inline u32 size_av1d_lb_vsp_top(u32 frame_width, u32 frame_height)
{
	return (max(((frame_width + 63) / 64) * 1280,
		    ((frame_width + 127) / 128) * MAX_HEIGHT));
}

static inline u32 size_av1d_lb_recon_dma_metadata_wr(u32 frame_width,
						     u32 frame_height)
{
	return ((ALIGN(frame_height, 8) / (4 / 2)) * 64);
}

static inline u32 size_av1d_qp(u32 frame_width, u32 frame_height)
{
	return size_h264d_qp(frame_width, frame_height);
}

u32 iris_vpu_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type);
u32 iris_vpu33_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type);
u32 iris_vpu4x_buf_size(struct iris_inst *inst, enum iris_buffer_type buffer_type);
int iris_vpu_buf_count(struct iris_inst *inst, enum iris_buffer_type buffer_type);

#endif
