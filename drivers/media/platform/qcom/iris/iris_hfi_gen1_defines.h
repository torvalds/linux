/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_GEN1_DEFINES_H__
#define __IRIS_HFI_GEN1_DEFINES_H__

#include <linux/types.h>

#define HFI_VIDEO_ARCH_OX				0x1

#define HFI_SESSION_TYPE_DEC				2

#define HFI_VIDEO_CODEC_H264				0x00000002

#define HFI_ERR_NONE					0x0

#define HFI_CMD_SYS_INIT				0x10001
#define HFI_CMD_SYS_PC_PREP				0x10002
#define HFI_CMD_SYS_SET_PROPERTY			0x10005
#define HFI_CMD_SYS_GET_PROPERTY			0x10006
#define HFI_CMD_SYS_SESSION_INIT			0x10007
#define HFI_CMD_SYS_SESSION_END				0x10008

#define HFI_CMD_SESSION_SET_PROPERTY			0x11001
#define HFI_CMD_SESSION_SET_BUFFERS			0x11002

#define HFI_CMD_SESSION_LOAD_RESOURCES			0x211001
#define HFI_CMD_SESSION_START				0x211002
#define HFI_CMD_SESSION_STOP				0x211003
#define HFI_CMD_SESSION_EMPTY_BUFFER			0x211004
#define HFI_CMD_SESSION_FILL_BUFFER			0x211005
#define HFI_CMD_SESSION_FLUSH				0x211008
#define HFI_CMD_SESSION_RELEASE_BUFFERS			0x21100b
#define HFI_CMD_SESSION_RELEASE_RESOURCES		0x21100c
#define HFI_CMD_SESSION_CONTINUE			0x21100d

#define HFI_ERR_SESSION_UNSUPPORTED_SETTING		0x1008
#define HFI_ERR_SESSION_UNSUPPORTED_STREAM		0x100d
#define HFI_ERR_SESSION_UNSUPPORT_BUFFERTYPE		0x1010
#define HFI_ERR_SESSION_INVALID_SCALE_FACTOR		0x1012
#define HFI_ERR_SESSION_UPSCALE_NOT_SUPPORTED		0x1013

#define HFI_EVENT_SYS_ERROR				0x1
#define HFI_EVENT_SESSION_ERROR				0x2

#define HFI_EVENT_DATA_SEQUENCE_CHANGED_SUFFICIENT_BUF_RESOURCES   0x1000001
#define HFI_EVENT_DATA_SEQUENCE_CHANGED_INSUFFICIENT_BUF_RESOURCES 0x1000002
#define HFI_EVENT_SESSION_SEQUENCE_CHANGED			   0x1000003

#define HFI_BUFFERFLAG_EOS				0x00000001
#define HFI_BUFFERFLAG_TIMESTAMPINVALID			0x00000100

#define HFI_FLUSH_OUTPUT				0x1000002
#define HFI_FLUSH_OUTPUT2				0x1000003
#define HFI_FLUSH_ALL					0x1000004

#define HFI_INDEX_EXTRADATA_INPUT_CROP			0x0700000e

#define HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL				0x201001
#define HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO	0x201002
#define HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE				0x201008
#define HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL				0x20100c

#define HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS		0x202001

#define HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER	0x1200001
#define HFI_PROPERTY_PARAM_VDEC_DPB_COUNTS		0x120300e
#define HFI_PROPERTY_CONFIG_VDEC_ENTROPY		0x1204004

#define HFI_BUFFER_INPUT				0x1
#define HFI_BUFFER_OUTPUT				0x2
#define HFI_BUFFER_OUTPUT2				0x3
#define HFI_BUFFER_INTERNAL_PERSIST_1			0x5
#define HFI_BUFFER_INTERNAL_SCRATCH			0x6
#define HFI_BUFFER_INTERNAL_SCRATCH_1			0x7

#define HFI_PROPERTY_SYS_CODEC_POWER_PLANE_CTRL		0x5
#define HFI_PROPERTY_SYS_IMAGE_VERSION			0x6

#define HFI_PROPERTY_PARAM_FRAME_SIZE			0x1001
#define HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT	0x1003
#define HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT	0x1005
#define HFI_PROPERTY_PARAM_WORK_MODE			0x1015
#define HFI_PROPERTY_PARAM_WORK_ROUTE			0x1017
#define HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE		0x2002

#define HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM		0x1003001
#define HFI_PROPERTY_PARAM_VDEC_PIXEL_BITDEPTH		0x1003007
#define HFI_PROPERTY_PARAM_VDEC_PIC_STRUCT		0x1003009
#define HFI_PROPERTY_PARAM_VDEC_COLOUR_SPACE		0x100300a
#define HFI_CORE_ID_1					1
#define HFI_COLOR_FORMAT_NV12				0x02
#define HFI_COLOR_FORMAT_NV12_UBWC			0x8002

#define HFI_MSG_SYS_INIT				0x20001
#define HFI_MSG_SYS_SESSION_INIT			0x20006
#define HFI_MSG_SYS_SESSION_END				0x20007
#define HFI_MSG_SYS_COV					0x20009
#define HFI_MSG_SYS_PROPERTY_INFO			0x2000a

#define HFI_MSG_EVENT_NOTIFY				0x21001
#define HFI_MSG_SESSION_LOAD_RESOURCES			0x221001
#define HFI_MSG_SESSION_START				0x221002
#define HFI_MSG_SESSION_STOP				0x221003
#define HFI_MSG_SESSION_FLUSH				0x221006
#define HFI_MSG_SESSION_EMPTY_BUFFER			0x221007
#define HFI_MSG_SESSION_FILL_BUFFER			0x221008
#define HFI_MSG_SESSION_RELEASE_RESOURCES		0x22100a
#define HFI_MSG_SESSION_RELEASE_BUFFERS			0x22100c

#define HFI_PICTURE_I					0x00000001
#define HFI_PICTURE_P					0x00000002
#define HFI_PICTURE_B					0x00000004
#define HFI_PICTURE_IDR					0x00000008
#define HFI_FRAME_NOTCODED				0x7f002000
#define HFI_FRAME_YUV					0x7f004000
#define HFI_UNUSED_PICT					0x10000000

struct hfi_pkt_hdr {
	u32 size;
	u32 pkt_type;
};

struct hfi_session_hdr_pkt {
	struct hfi_pkt_hdr hdr;
	u32 session_id;
};

struct hfi_session_open_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 session_domain;
	u32 session_codec;
};

struct hfi_session_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_sys_init_pkt {
	struct hfi_pkt_hdr hdr;
	u32 arch_type;
};

struct hfi_sys_set_property_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data[];
};

struct hfi_sys_get_property_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data;
};

struct hfi_session_set_property_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 num_properties;
	u32 data[];
};

struct hfi_sys_pc_prep_pkt {
	struct hfi_pkt_hdr hdr;
};

struct hfi_session_set_buffers_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 buffer_type;
	u32 buffer_size;
	u32 extradata_size;
	u32 min_buffer_size;
	u32 num_buffers;
	u32 buffer_info[];
};

struct hfi_session_empty_buffer_compressed_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u32 flags;
	u32 mark_target;
	u32 mark_data;
	u32 offset;
	u32 alloc_len;
	u32 filled_len;
	u32 input_tag;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data;
};

struct hfi_session_fill_buffer_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 stream_id;
	u32 offset;
	u32 alloc_len;
	u32 filled_len;
	u32 output_tag;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data;
};

struct hfi_session_flush_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 flush_type;
};

struct hfi_session_release_buffer_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 buffer_type;
	u32 buffer_size;
	u32 extradata_size;
	u32 response_req;
	u32 num_buffers;
	u32 buffer_info[];
};

struct hfi_buffer_info {
	u32 buffer_addr;
	u32 extradata_addr;
};

struct hfi_msg_event_notify_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 event_id;
	u32 event_data1;
	u32 event_data2;
	u32 ext_event_data[];
};

struct hfi_msg_sys_init_done_pkt {
	struct hfi_pkt_hdr hdr;
	u32 error_type;
	u32 num_properties;
	u32 data[];
};

struct hfi_msg_session_hdr_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_init_done_pkt {
	struct hfi_msg_session_hdr_pkt shdr;
	u32 num_properties;
	u32 data[];
};

struct hfi_msg_sys_property_info_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 property;
	u8 data[];
};

struct hfi_msg_session_flush_done_pkt {
	struct hfi_msg_session_hdr_pkt shdr;
	u32 flush_type;
};

struct hfi_enable {
	u32 enable;
};

struct hfi_profile_level {
	u32 profile;
	u32 level;
};

struct hfi_framesize {
	u32 buffer_type;
	u32 width;
	u32 height;
};

struct hfi_videocores_usage_type {
	u32 video_core_enable_mask;
};

struct hfi_video_work_mode {
	u32 video_work_mode;
};

struct hfi_video_work_route {
	u32 video_work_route;
};

struct hfi_bit_depth {
	u32 buffer_type;
	u32 bit_depth;
};

struct hfi_pic_struct {
	u32 progressive_only;
};

struct hfi_colour_space {
	u32 colour_space;
};

struct hfi_extradata_input_crop {
	u32 size;
	u32 version;
	u32 port_index;
	u32 left;
	u32 top;
	u32 width;
	u32 height;
};

struct hfi_dpb_counts {
	u32 max_dpb_count;
	u32 max_ref_frames;
	u32 max_dec_buffering;
	u32 max_reorder_frames;
	u32 fw_min_count;
};

struct hfi_uncompressed_format_select {
	u32 buffer_type;
	u32 format;
};

struct hfi_uncompressed_plane_constraints {
	u32 stride_multiples;
	u32 max_stride;
	u32 min_plane_buffer_height_multiple;
	u32 buffer_alignment;
};

struct hfi_uncompressed_plane_actual_constraints_info {
	u32 buffer_type;
	u32 num_planes;
	struct hfi_uncompressed_plane_constraints plane_format[2];
};

struct hfi_buffer_count_actual {
	u32 type;
	u32 count_actual;
	u32 count_min_host;
};

struct hfi_buffer_size_actual {
	u32 type;
	u32 size;
};

struct hfi_multi_stream {
	u32 buffer_type;
	u32 enable;
};

struct hfi_buffer_requirements {
	u32 type;
	u32 size;
	u32 region_size;
	u32 hold_count;
	u32 count_min;
	u32 count_actual;
	u32 contiguous;
	u32 alignment;
};

struct hfi_event_data {
	u32 error;
	u32 height;
	u32 width;
	u32 event_type;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 tag;
	u32 profile;
	u32 level;
	u32 bit_depth;
	u32 pic_struct;
	u32 colour_space;
	u32 entropy_mode;
	u32 buf_count;
	struct {
		u32 left, top;
		u32 width, height;
	} input_crop;
};

struct hfi_msg_session_empty_buffer_done_pkt {
	struct hfi_msg_session_hdr_pkt shdr;
	u32 offset;
	u32 filled_len;
	u32 input_tag;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data[];
};

struct hfi_msg_session_fbd_uncompressed_plane0_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 stream_id;
	u32 view_id;
	u32 error_type;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u32 flags;
	u32 mark_target;
	u32 mark_data;
	u32 stats;
	u32 alloc_len;
	u32 filled_len;
	u32 offset;
	u32 frame_width;
	u32 frame_height;
	u32 start_x_coord;
	u32 start_y_coord;
	u32 input_tag;
	u32 input_tag2;
	u32 output_tag;
	u32 picture_type;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data[];
};

struct hfi_msg_session_release_buffers_done_pkt {
	struct hfi_msg_session_hdr_pkt shdr;
	u32 num_buffers;
	u32 buffer_info[];
};

struct hfi_msg_sys_debug_pkt {
	struct hfi_pkt_hdr hdr;
	u32 msg_type;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 msg_data[];
};

struct hfi_msg_sys_coverage_pkt {
	struct hfi_pkt_hdr hdr;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 msg_data[];
};

#endif
