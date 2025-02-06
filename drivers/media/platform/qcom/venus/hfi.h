/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#ifndef __HFI_H__
#define __HFI_H__

#include <linux/interrupt.h>

#include "hfi_helper.h"

#define VIDC_SESSION_TYPE_VPE			0
#define VIDC_SESSION_TYPE_ENC			1
#define VIDC_SESSION_TYPE_DEC			2

#define VIDC_RESOURCE_NONE			0
#define VIDC_RESOURCE_OCMEM			1
#define VIDC_RESOURCE_VMEM			2

struct hfi_buffer_desc {
	u32 buffer_type;
	u32 buffer_size;
	u32 num_buffers;
	u32 device_addr;
	u32 extradata_addr;
	u32 extradata_size;
	u32 response_required;
};

struct hfi_frame_data {
	u32 buffer_type;
	u32 device_addr;
	u32 extradata_addr;
	u64 timestamp;
	u32 flags;
	u32 offset;
	u32 alloc_len;
	u32 filled_len;
	u32 mark_target;
	u32 mark_data;
	u32 clnt_data;
	u32 extradata_size;
};

union hfi_get_property {
	struct hfi_profile_level profile_level;
	struct hfi_buffer_requirements bufreq[HFI_BUFFER_TYPE_MAX];
};

/* HFI events */
#define EVT_SYS_EVENT_CHANGE			1
#define EVT_SYS_WATCHDOG_TIMEOUT		2
#define EVT_SYS_ERROR				3
#define EVT_SESSION_ERROR			4

/* HFI event callback structure */
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
	/* the following properties start appear from v4 onwards */
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

/* define core states */
#define CORE_UNINIT				0
#define CORE_INIT				1

/* define instance states */
#define INST_UNINIT				2
#define INST_INIT				3
#define INST_LOAD_RESOURCES			4
#define INST_START				5
#define INST_STOP				6
#define INST_RELEASE_RESOURCES			7

struct venus_core;
struct venus_inst;

struct hfi_core_ops {
	void (*event_notify)(struct venus_core *core, u32 event);
};

struct hfi_inst_ops {
	void (*buf_done)(struct venus_inst *inst, unsigned int buf_type,
			 u32 tag, u32 bytesused, u32 data_offset, u32 flags,
			 u32 hfi_flags, u64 timestamp_us);
	void (*event_notify)(struct venus_inst *inst, u32 event,
			     struct hfi_event_data *data);
	void (*flush_done)(struct venus_inst *inst);
};

struct hfi_ops {
	int (*core_init)(struct venus_core *core);
	int (*core_deinit)(struct venus_core *core);
	int (*core_trigger_ssr)(struct venus_core *core, u32 trigger_type);

	int (*session_init)(struct venus_inst *inst, u32 session_type,
			    u32 codec);
	int (*session_end)(struct venus_inst *inst);
	int (*session_abort)(struct venus_inst *inst);
	int (*session_flush)(struct venus_inst *inst, u32 flush_mode);
	int (*session_start)(struct venus_inst *inst);
	int (*session_stop)(struct venus_inst *inst);
	int (*session_continue)(struct venus_inst *inst);
	int (*session_etb)(struct venus_inst *inst, struct hfi_frame_data *fd);
	int (*session_ftb)(struct venus_inst *inst, struct hfi_frame_data *fd);
	int (*session_set_buffers)(struct venus_inst *inst,
				   struct hfi_buffer_desc *bd);
	int (*session_unset_buffers)(struct venus_inst *inst,
				     struct hfi_buffer_desc *bd);
	int (*session_load_res)(struct venus_inst *inst);
	int (*session_release_res)(struct venus_inst *inst);
	int (*session_parse_seq_hdr)(struct venus_inst *inst, u32 seq_hdr,
				     u32 seq_hdr_len);
	int (*session_get_seq_hdr)(struct venus_inst *inst, u32 seq_hdr,
				   u32 seq_hdr_len);
	int (*session_set_property)(struct venus_inst *inst, u32 ptype,
				    void *pdata);
	int (*session_get_property)(struct venus_inst *inst, u32 ptype);

	int (*resume)(struct venus_core *core);
	int (*suspend)(struct venus_core *core);

	/* interrupt operations */
	irqreturn_t (*isr)(struct venus_core *core);
	irqreturn_t (*isr_thread)(struct venus_core *core);
};

int hfi_create(struct venus_core *core, const struct hfi_core_ops *ops);
void hfi_destroy(struct venus_core *core);
void hfi_reinit(struct venus_core *core);

int hfi_core_init(struct venus_core *core);
int hfi_core_deinit(struct venus_core *core, bool blocking);
int hfi_core_suspend(struct venus_core *core);
int hfi_core_resume(struct venus_core *core, bool force);
int hfi_core_trigger_ssr(struct venus_core *core, u32 type);
int hfi_session_create(struct venus_inst *inst, const struct hfi_inst_ops *ops);
void hfi_session_destroy(struct venus_inst *inst);
int hfi_session_init(struct venus_inst *inst, u32 pixfmt);
int hfi_session_deinit(struct venus_inst *inst);
int hfi_session_start(struct venus_inst *inst);
int hfi_session_stop(struct venus_inst *inst);
int hfi_session_continue(struct venus_inst *inst);
int hfi_session_abort(struct venus_inst *inst);
int hfi_session_load_res(struct venus_inst *inst);
int hfi_session_unload_res(struct venus_inst *inst);
int hfi_session_flush(struct venus_inst *inst, u32 type, bool block);
int hfi_session_set_buffers(struct venus_inst *inst,
			    struct hfi_buffer_desc *bd);
int hfi_session_unset_buffers(struct venus_inst *inst,
			      struct hfi_buffer_desc *bd);
int hfi_session_get_property(struct venus_inst *inst, u32 ptype,
			     union hfi_get_property *hprop);
int hfi_session_set_property(struct venus_inst *inst, u32 ptype, void *pdata);
int hfi_session_process_buf(struct venus_inst *inst, struct hfi_frame_data *f);
irqreturn_t hfi_isr_thread(int irq, void *dev_id);
irqreturn_t hfi_isr(int irq, void *dev);

#endif
