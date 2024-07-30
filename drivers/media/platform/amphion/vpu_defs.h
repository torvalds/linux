/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_DEFS_H
#define _AMPHION_VPU_DEFS_H

enum MSG_TYPE {
	INIT_DONE = 1,
	PRC_BUF_OFFSET,
	BOOT_ADDRESS,
	COMMAND,
	EVENT,
};

enum {
	VPU_IRQ_CODE_BOOT_DONE = 0x55,
	VPU_IRQ_CODE_SNAPSHOT_DONE = 0xa5,
	VPU_IRQ_CODE_SYNC = 0xaa,
};

enum {
	VPU_CMD_ID_NOOP = 0x0,
	VPU_CMD_ID_CONFIGURE_CODEC,
	VPU_CMD_ID_START,
	VPU_CMD_ID_STOP,
	VPU_CMD_ID_ABORT,
	VPU_CMD_ID_RST_BUF,
	VPU_CMD_ID_SNAPSHOT,
	VPU_CMD_ID_FIRM_RESET,
	VPU_CMD_ID_UPDATE_PARAMETER,
	VPU_CMD_ID_FRAME_ENCODE,
	VPU_CMD_ID_SKIP,
	VPU_CMD_ID_PARSE_NEXT_SEQ,
	VPU_CMD_ID_PARSE_NEXT_I,
	VPU_CMD_ID_PARSE_NEXT_IP,
	VPU_CMD_ID_PARSE_NEXT_ANY,
	VPU_CMD_ID_DEC_PIC,
	VPU_CMD_ID_FS_ALLOC,
	VPU_CMD_ID_FS_RELEASE,
	VPU_CMD_ID_TIMESTAMP,
	VPU_CMD_ID_DEBUG
};

enum {
	VPU_MSG_ID_NOOP = 0x100,
	VPU_MSG_ID_RESET_DONE,
	VPU_MSG_ID_START_DONE,
	VPU_MSG_ID_STOP_DONE,
	VPU_MSG_ID_ABORT_DONE,
	VPU_MSG_ID_BUF_RST,
	VPU_MSG_ID_MEM_REQUEST,
	VPU_MSG_ID_PARAM_UPD_DONE,
	VPU_MSG_ID_FRAME_INPUT_DONE,
	VPU_MSG_ID_ENC_DONE,
	VPU_MSG_ID_DEC_DONE,
	VPU_MSG_ID_FRAME_REQ,
	VPU_MSG_ID_FRAME_RELEASE,
	VPU_MSG_ID_SEQ_HDR_FOUND,
	VPU_MSG_ID_RES_CHANGE,
	VPU_MSG_ID_PIC_HDR_FOUND,
	VPU_MSG_ID_PIC_DECODED,
	VPU_MSG_ID_PIC_EOS,
	VPU_MSG_ID_FIFO_LOW,
	VPU_MSG_ID_FIFO_HIGH,
	VPU_MSG_ID_FIFO_EMPTY,
	VPU_MSG_ID_FIFO_FULL,
	VPU_MSG_ID_BS_ERROR,
	VPU_MSG_ID_UNSUPPORTED,
	VPU_MSG_ID_TIMESTAMP_INFO,
	VPU_MSG_ID_FIRMWARE_XCPT,
	VPU_MSG_ID_PIC_SKIPPED,
	VPU_MSG_ID_DBG_MSG,
};

enum VPU_ENC_MEMORY_RESOURSE {
	MEM_RES_ENC,
	MEM_RES_REF,
	MEM_RES_ACT
};

enum VPU_DEC_MEMORY_RESOURCE {
	MEM_RES_FRAME,
	MEM_RES_MBI,
	MEM_RES_DCP
};

enum VPU_SCODE_TYPE {
	SCODE_PADDING_EOS = 1,
	SCODE_PADDING_BUFFLUSH = 2,
	SCODE_PADDING_ABORT = 3,
	SCODE_SEQUENCE = 0x31,
	SCODE_PICTURE = 0x32,
	SCODE_SLICE = 0x33
};

struct vpu_pkt_mem_req_data {
	u32 enc_frame_size;
	u32 enc_frame_num;
	u32 ref_frame_size;
	u32 ref_frame_num;
	u32 act_buf_size;
	u32 act_buf_num;
};

struct vpu_enc_pic_info {
	u32 frame_id;
	u32 pic_type;
	u32 skipped_frame;
	u32 error_flag;
	u32 psnr;
	u32 frame_size;
	u32 wptr;
	u32 crc;
	s64 timestamp;
	u32 average_qp;
};

struct vpu_dec_codec_info {
	u32 pixfmt;
	u32 num_ref_frms;
	u32 num_dpb_frms;
	u32 num_dfe_area;
	u32 color_primaries;
	u32 transfer_chars;
	u32 matrix_coeffs;
	u32 full_range;
	u32 vui_present;
	u32 progressive;
	u32 width;
	u32 height;
	u32 decoded_width;
	u32 decoded_height;
	struct v4l2_fract frame_rate;
	u32 dsp_asp_ratio;
	u32 level_idc;
	u32 bit_depth_luma;
	u32 bit_depth_chroma;
	u32 chroma_fmt;
	u32 mvc_num_views;
	u32 offset_x;
	u32 offset_y;
	u32 tag;
	u32 sizeimage[VIDEO_MAX_PLANES];
	u32 bytesperline[VIDEO_MAX_PLANES];
	u32 mbi_size;
	u32 dcp_size;
	u32 stride;
};

struct vpu_dec_pic_info {
	u32 id;
	u32 luma;
	u32 start;
	u32 end;
	u32 pic_size;
	u32 stride;
	u32 skipped;
	s64 timestamp;
	u32 consumed_count;
};

struct vpu_fs_info {
	u32 id;
	u32 type;
	u32 tag;
	u32 luma_addr;
	u32 luma_size;
	u32 chroma_addr;
	u32 chromau_size;
	u32 chromav_addr;
	u32 chromav_size;
	u32 bytesperline;
	u32 not_displayed;
};

struct vpu_ts_info {
	s64 timestamp;
	u32 size;
};

#define BITRATE_STEP		(1024)
#define BITRATE_MIN		(16 * BITRATE_STEP)
#define BITRATE_MAX		(240 * 1024 * BITRATE_STEP)
#define BITRATE_DEFAULT		(2 * 1024 * BITRATE_STEP)
#define BITRATE_DEFAULT_PEAK	(BITRATE_DEFAULT * 2)

#endif
