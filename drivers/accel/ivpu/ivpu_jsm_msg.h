/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_JSM_MSG_H__
#define __IVPU_JSM_MSG_H__

#include "vpu_jsm_api.h"

const char *ivpu_jsm_msg_type_to_str(enum vpu_ipc_msg_type type);

int ivpu_jsm_register_db(struct ivpu_device *vdev, u32 ctx_id, u32 db_id,
			 u64 jobq_base, u32 jobq_size);
int ivpu_jsm_unregister_db(struct ivpu_device *vdev, u32 db_id);
int ivpu_jsm_get_heartbeat(struct ivpu_device *vdev, u32 engine, u64 *heartbeat);
int ivpu_jsm_reset_engine(struct ivpu_device *vdev, u32 engine);
int ivpu_jsm_preempt_engine(struct ivpu_device *vdev, u32 engine, u32 preempt_id);
int ivpu_jsm_dyndbg_control(struct ivpu_device *vdev, char *command, size_t size);
int ivpu_jsm_trace_get_capability(struct ivpu_device *vdev, u32 *trace_destination_mask,
				  u64 *trace_hw_component_mask);
int ivpu_jsm_trace_set_config(struct ivpu_device *vdev, u32 trace_level, u32 trace_destination_mask,
			      u64 trace_hw_component_mask);
int ivpu_jsm_context_release(struct ivpu_device *vdev, u32 host_ssid);
#endif
