/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#ifndef __VENUS_HFI_MSGS_H__
#define __VENUS_HFI_MSGS_H__

/* message calls */
#define HFI_MSG_SYS_INIT			0x20001
#define HFI_MSG_SYS_PC_PREP			0x20002
#define HFI_MSG_SYS_RELEASE_RESOURCE		0x20003
#define HFI_MSG_SYS_DEBUG			0x20004
#define HFI_MSG_SYS_SESSION_INIT		0x20006
#define HFI_MSG_SYS_SESSION_END			0x20007
#define HFI_MSG_SYS_IDLE			0x20008
#define HFI_MSG_SYS_COV				0x20009
#define HFI_MSG_SYS_PROPERTY_INFO		0x2000a

#define HFI_MSG_EVENT_NOTIFY			0x21001
#define HFI_MSG_SESSION_GET_SEQUENCE_HEADER	0x21002

#define HFI_MSG_SYS_PING_ACK			0x220002
#define HFI_MSG_SYS_SESSION_ABORT		0x220004

#define HFI_MSG_SESSION_LOAD_RESOURCES		0x221001
#define HFI_MSG_SESSION_START			0x221002
#define HFI_MSG_SESSION_STOP			0x221003
#define HFI_MSG_SESSION_SUSPEND			0x221004
#define HFI_MSG_SESSION_RESUME			0x221005
#define HFI_MSG_SESSION_FLUSH			0x221006
#define HFI_MSG_SESSION_EMPTY_BUFFER		0x221007
#define HFI_MSG_SESSION_FILL_BUFFER		0x221008
#define HFI_MSG_SESSION_PROPERTY_INFO		0x221009
#define HFI_MSG_SESSION_RELEASE_RESOURCES	0x22100a
#define HFI_MSG_SESSION_PARSE_SEQUENCE_HEADER	0x22100b
#define HFI_MSG_SESSION_RELEASE_BUFFERS		0x22100c

#define HFI_PICTURE_I				0x00000001
#define HFI_PICTURE_P				0x00000002
#define HFI_PICTURE_B				0x00000004
#define HFI_PICTURE_IDR				0x00000008
#define HFI_FRAME_NOTCODED			0x7f002000
#define HFI_FRAME_YUV				0x7f004000
#define HFI_UNUSED_PICT				0x10000000

/* message packets */
struct hfi_msg_event_notify_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 event_id;
	u32 event_data1;
	u32 event_data2;
	u32 ext_event_data[1];
};

struct hfi_msg_event_release_buffer_ref_pkt {
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 output_tag;
};

struct hfi_msg_sys_init_done_pkt {
	struct hfi_pkt_hdr hdr;
	u32 error_type;
	u32 num_properties;
	u32 data[1];
};

struct hfi_msg_sys_pc_prep_done_pkt {
	struct hfi_pkt_hdr hdr;
	u32 error_type;
};

struct hfi_msg_sys_release_resource_done_pkt {
	struct hfi_pkt_hdr hdr;
	u32 resource_handle;
	u32 error_type;
};

struct hfi_msg_session_init_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
	u32 num_properties;
	u32 data[1];
};

struct hfi_msg_session_end_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_get_sequence_hdr_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
	u32 header_len;
	u32 sequence_header;
};

struct hfi_msg_sys_session_abort_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_sys_idle_pkt {
	struct hfi_pkt_hdr hdr;
};

struct hfi_msg_sys_ping_ack_pkt {
	struct hfi_pkt_hdr hdr;
	u32 client_data;
};

struct hfi_msg_sys_property_info_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data[1];
};

struct hfi_msg_session_load_resources_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_start_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_stop_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_suspend_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_resume_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_flush_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
	u32 flush_type;
};

struct hfi_msg_session_empty_buffer_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
	u32 offset;
	u32 filled_len;
	u32 input_tag;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data[0];
};

struct hfi_msg_session_fbd_compressed_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u32 error_type;
	u32 flags;
	u32 mark_target;
	u32 mark_data;
	u32 stats;
	u32 offset;
	u32 alloc_len;
	u32 filled_len;
	u32 input_tag;
	u32 output_tag;
	u32 picture_type;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data[0];
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
	u32 data[0];
};

struct hfi_msg_session_fbd_uncompressed_plane1_pkt {
	u32 flags;
	u32 alloc_len;
	u32 filled_len;
	u32 offset;
	u32 packet_buffer2;
	u32 data[0];
};

struct hfi_msg_session_fbd_uncompressed_plane2_pkt {
	u32 flags;
	u32 alloc_len;
	u32 filled_len;
	u32 offset;
	u32 packet_buffer3;
	u32 data[0];
};

struct hfi_msg_session_parse_sequence_header_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
	u32 num_properties;
	u32 data[1];
};

struct hfi_msg_session_property_info_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 num_properties;
	u32 data[1];
};

struct hfi_msg_session_release_resources_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
};

struct hfi_msg_session_release_buffers_done_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 error_type;
	u32 num_buffers;
	u32 buffer_info[1];
};

struct hfi_msg_sys_debug_pkt {
	struct hfi_pkt_hdr hdr;
	u32 msg_type;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 msg_data[1];
};

struct hfi_msg_sys_coverage_pkt {
	struct hfi_pkt_hdr hdr;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 msg_data[1];
};

struct venus_core;
struct hfi_pkt_hdr;

void hfi_process_watchdog_timeout(struct venus_core *core);
u32 hfi_process_msg_packet(struct venus_core *core, struct hfi_pkt_hdr *hdr);

#endif
