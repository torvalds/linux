/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#ifndef __VENUS_HFI_CMDS_H__
#define __VENUS_HFI_CMDS_H__

#include "hfi.h"

/* commands */
#define HFI_CMD_SYS_INIT			0x10001
#define HFI_CMD_SYS_PC_PREP			0x10002
#define HFI_CMD_SYS_SET_RESOURCE		0x10003
#define HFI_CMD_SYS_RELEASE_RESOURCE		0x10004
#define HFI_CMD_SYS_SET_PROPERTY		0x10005
#define HFI_CMD_SYS_GET_PROPERTY		0x10006
#define HFI_CMD_SYS_SESSION_INIT		0x10007
#define HFI_CMD_SYS_SESSION_END			0x10008
#define HFI_CMD_SYS_SET_BUFFERS			0x10009
#define HFI_CMD_SYS_TEST_SSR			0x10101

#define HFI_CMD_SESSION_SET_PROPERTY		0x11001
#define HFI_CMD_SESSION_SET_BUFFERS		0x11002
#define HFI_CMD_SESSION_GET_SEQUENCE_HEADER	0x11003

#define HFI_CMD_SYS_SESSION_ABORT		0x210001
#define HFI_CMD_SYS_PING			0x210002

#define HFI_CMD_SESSION_LOAD_RESOURCES		0x211001
#define HFI_CMD_SESSION_START			0x211002
#define HFI_CMD_SESSION_STOP			0x211003
#define HFI_CMD_SESSION_EMPTY_BUFFER		0x211004
#define HFI_CMD_SESSION_FILL_BUFFER		0x211005
#define HFI_CMD_SESSION_SUSPEND			0x211006
#define HFI_CMD_SESSION_RESUME			0x211007
#define HFI_CMD_SESSION_FLUSH			0x211008
#define HFI_CMD_SESSION_GET_PROPERTY		0x211009
#define HFI_CMD_SESSION_PARSE_SEQUENCE_HEADER	0x21100a
#define HFI_CMD_SESSION_RELEASE_BUFFERS		0x21100b
#define HFI_CMD_SESSION_RELEASE_RESOURCES	0x21100c
#define HFI_CMD_SESSION_CONTINUE		0x21100d
#define HFI_CMD_SESSION_SYNC			0x21100e

/* command packets */
struct hfi_sys_init_pkt {
	struct hfi_pkt_hdr hdr;
	u32 arch_type;
};

struct hfi_sys_pc_prep_pkt {
	struct hfi_pkt_hdr hdr;
};

struct hfi_sys_set_resource_pkt {
	struct hfi_pkt_hdr hdr;
	u32 resource_handle;
	u32 resource_type;
	u32 resource_data[];
};

struct hfi_sys_release_resource_pkt {
	struct hfi_pkt_hdr hdr;
	u32 resource_type;
	u32 resource_handle;
};

struct hfi_sys_set_property_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data[];
};

struct hfi_sys_get_property_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data[1];
};

struct hfi_sys_set_buffers_pkt {
	struct hfi_pkt_hdr hdr;
	u32 buffer_type;
	u32 buffer_size;
	u32 num_buffers;
	u32 buffer_addr[1];
};

struct hfi_sys_ping_pkt {
	struct hfi_pkt_hdr hdr;
	u32 client_data;
};

struct hfi_session_init_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 session_domain;
	u32 session_codec;
};

struct hfi_session_end_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_abort_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_set_property_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 num_properties;
	u32 data[];
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

struct hfi_session_get_sequence_header_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 buffer_len;
	u32 packet_buffer;
};

struct hfi_session_load_resources_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_start_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_stop_pkt {
	struct hfi_session_hdr_pkt shdr;
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
	u32 data[1];
};

struct hfi_session_empty_buffer_uncompressed_plane0_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 view_id;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u32 flags;
	u32 mark_target;
	u32 mark_data;
	u32 alloc_len;
	u32 filled_len;
	u32 offset;
	u32 input_tag;
	u32 packet_buffer;
	u32 extradata_buffer;
	u32 data[1];
};

struct hfi_session_empty_buffer_uncompressed_plane1_pkt {
	u32 flags;
	u32 alloc_len;
	u32 filled_len;
	u32 offset;
	u32 packet_buffer2;
	u32 data[1];
};

struct hfi_session_empty_buffer_uncompressed_plane2_pkt {
	u32 flags;
	u32 alloc_len;
	u32 filled_len;
	u32 offset;
	u32 packet_buffer3;
	u32 data[1];
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
	u32 data[1];
};

struct hfi_session_flush_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 flush_type;
};

struct hfi_session_suspend_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_resume_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_get_property_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 num_properties;
	u32 data[1];
};

struct hfi_session_release_buffer_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 buffer_type;
	u32 buffer_size;
	u32 extradata_size;
	u32 response_req;
	u32 num_buffers;
	u32 buffer_info[] __counted_by(num_buffers);
};

struct hfi_session_release_resources_pkt {
	struct hfi_session_hdr_pkt shdr;
};

struct hfi_session_parse_sequence_header_pkt {
	struct hfi_session_hdr_pkt shdr;
	u32 header_len;
	u32 packet_buffer;
};

struct hfi_sfr {
	u32 buf_size;
	u8 data[] __counted_by(buf_size);
};

struct hfi_sys_test_ssr_pkt {
	struct hfi_pkt_hdr hdr;
	u32 trigger_type;
};

void pkt_set_version(enum hfi_version version);

void pkt_sys_init(struct hfi_sys_init_pkt *pkt, u32 arch_type);
void pkt_sys_pc_prep(struct hfi_sys_pc_prep_pkt *pkt);
void pkt_sys_idle_indicator(struct hfi_sys_set_property_pkt *pkt, u32 enable);
void pkt_sys_power_control(struct hfi_sys_set_property_pkt *pkt, u32 enable);
void pkt_sys_ubwc_config(struct hfi_sys_set_property_pkt *pkt, const struct hfi_ubwc_config *hfi);
int pkt_sys_set_resource(struct hfi_sys_set_resource_pkt *pkt, u32 id, u32 size,
			 u32 addr, void *cookie);
int pkt_sys_unset_resource(struct hfi_sys_release_resource_pkt *pkt, u32 id,
			   u32 size, void *cookie);
void pkt_sys_debug_config(struct hfi_sys_set_property_pkt *pkt, u32 mode,
			  u32 config);
void pkt_sys_coverage_config(struct hfi_sys_set_property_pkt *pkt, u32 mode);
void pkt_sys_ping(struct hfi_sys_ping_pkt *pkt, u32 cookie);
void pkt_sys_image_version(struct hfi_sys_get_property_pkt *pkt);
int pkt_sys_ssr_cmd(struct hfi_sys_test_ssr_pkt *pkt, u32 trigger_type);
int pkt_session_init(struct hfi_session_init_pkt *pkt, void *cookie,
		     u32 session_type, u32 codec);
void pkt_session_cmd(struct hfi_session_pkt *pkt, u32 pkt_type, void *cookie);
int pkt_session_set_buffers(struct hfi_session_set_buffers_pkt *pkt,
			    void *cookie, struct hfi_buffer_desc *bd);
int pkt_session_unset_buffers(struct hfi_session_release_buffer_pkt *pkt,
			      void *cookie, struct hfi_buffer_desc *bd);
int pkt_session_etb_decoder(struct hfi_session_empty_buffer_compressed_pkt *pkt,
			    void *cookie, struct hfi_frame_data *input_frame);
int pkt_session_etb_encoder(
		struct hfi_session_empty_buffer_uncompressed_plane0_pkt *pkt,
		void *cookie, struct hfi_frame_data *input_frame);
int pkt_session_ftb(struct hfi_session_fill_buffer_pkt *pkt,
		    void *cookie, struct hfi_frame_data *output_frame);
int pkt_session_parse_seq_header(
		struct hfi_session_parse_sequence_header_pkt *pkt,
		void *cookie, u32 seq_hdr, u32 seq_hdr_len);
int pkt_session_get_seq_hdr(struct hfi_session_get_sequence_header_pkt *pkt,
			    void *cookie, u32 seq_hdr, u32 seq_hdr_len);
int pkt_session_flush(struct hfi_session_flush_pkt *pkt, void *cookie,
		      u32 flush_mode);
int pkt_session_get_property(struct hfi_session_get_property_pkt *pkt,
			     void *cookie, u32 ptype);
int pkt_session_set_property(struct hfi_session_set_property_pkt *pkt,
			     void *cookie, u32 ptype, void *pdata);

#endif
