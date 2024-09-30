/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
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
int ivpu_jsm_pwr_d0i3_enter(struct ivpu_device *vdev);
int ivpu_jsm_hws_create_cmdq(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_group, u32 cmdq_id,
			     u32 pid, u32 engine, u64 cmdq_base, u32 cmdq_size);
int ivpu_jsm_hws_destroy_cmdq(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id);
int ivpu_jsm_hws_register_db(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id, u32 db_id,
			     u64 cmdq_base, u32 cmdq_size);
int ivpu_jsm_hws_resume_engine(struct ivpu_device *vdev, u32 engine);
int ivpu_jsm_hws_set_context_sched_properties(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id,
					      u32 priority);
int ivpu_jsm_hws_set_scheduling_log(struct ivpu_device *vdev, u32 engine_idx, u32 host_ssid,
				    u64 vpu_log_buffer_va);
int ivpu_jsm_hws_setup_priority_bands(struct ivpu_device *vdev);
int ivpu_jsm_metric_streamer_start(struct ivpu_device *vdev, u64 metric_group_mask,
				   u64 sampling_rate, u64 buffer_addr, u64 buffer_size);
int ivpu_jsm_metric_streamer_stop(struct ivpu_device *vdev, u64 metric_group_mask);
int ivpu_jsm_metric_streamer_update(struct ivpu_device *vdev, u64 metric_group_mask,
				    u64 buffer_addr, u64 buffer_size, u64 *bytes_written);
int ivpu_jsm_metric_streamer_info(struct ivpu_device *vdev, u64 metric_group_mask, u64 buffer_addr,
				  u64 buffer_size, u32 *sample_size, u64 *info_size);
int ivpu_jsm_dct_enable(struct ivpu_device *vdev, u32 active_us, u32 inactive_us);
int ivpu_jsm_dct_disable(struct ivpu_device *vdev);
int ivpu_jsm_state_dump(struct ivpu_device *vdev);

#endif
