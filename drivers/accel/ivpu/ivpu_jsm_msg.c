// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_pm.h"
#include "vpu_jsm_api.h"

const char *ivpu_jsm_msg_type_to_str(enum vpu_ipc_msg_type type)
{
	#define IVPU_CASE_TO_STR(x) case x: return #x
	switch (type) {
	IVPU_CASE_TO_STR(VPU_JSM_MSG_UNKNOWN);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_ENGINE_RESET);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_ENGINE_PREEMPT);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_REGISTER_DB);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_UNREGISTER_DB);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_QUERY_ENGINE_HB);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_GET_POWER_LEVEL_COUNT);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_GET_POWER_LEVEL);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SET_POWER_LEVEL);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_OPEN);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_CLOSE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_SET_CONFIG);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_GET_CONFIG);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_GET_CAPABILITY);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_GET_NAME);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SSID_RELEASE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_START);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_STOP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_UPDATE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_INFO);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SET_PRIORITY_BAND_SETUP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_CREATE_CMD_QUEUE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DESTROY_CMD_QUEUE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SET_CONTEXT_SCHED_PROPERTIES);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_REGISTER_DB);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_RESUME_CMDQ);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_SUSPEND_CMDQ);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_RESUME_CMDQ_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_SUSPEND_CMDQ_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_SET_SCHEDULING_LOG);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_SET_SCHEDULING_LOG_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_SCHEDULING_LOG_NOTIFICATION);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_ENGINE_RESUME);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_HWS_RESUME_ENGINE_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_STATE_DUMP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_STATE_DUMP_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_BLOB_DEINIT_DEPRECATED);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DYNDBG_CONTROL);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_JOB_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_NATIVE_FENCE_SIGNALLED);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_ENGINE_RESET_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_ENGINE_PREEMPT_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_REGISTER_DB_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_UNREGISTER_DB_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_QUERY_ENGINE_HB_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_GET_POWER_LEVEL_COUNT_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_GET_POWER_LEVEL_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SET_POWER_LEVEL_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_OPEN_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_CLOSE_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_SET_CONFIG_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_GET_CONFIG_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_GET_CAPABILITY_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_TRACE_GET_NAME_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SSID_RELEASE_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_START_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_STOP_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_UPDATE_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_INFO_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_METRIC_STREAMER_NOTIFICATION);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SET_PRIORITY_BAND_SETUP_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_CREATE_CMD_QUEUE_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DESTROY_CMD_QUEUE_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_SET_CONTEXT_SCHED_PROPERTIES_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_BLOB_DEINIT_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DYNDBG_CONTROL_RSP);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_PWR_D0I3_ENTER);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_PWR_D0I3_ENTER_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DCT_ENABLE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DCT_ENABLE_DONE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DCT_DISABLE);
	IVPU_CASE_TO_STR(VPU_JSM_MSG_DCT_DISABLE_DONE);
	}
	#undef IVPU_CASE_TO_STR

	return "Unknown JSM message type";
}

int ivpu_jsm_register_db(struct ivpu_device *vdev, u32 ctx_id, u32 db_id,
			 u64 jobq_base, u32 jobq_size)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_REGISTER_DB };
	struct vpu_jsm_msg resp;
	int ret = 0;

	req.payload.register_db.db_idx = db_id;
	req.payload.register_db.jobq_base = jobq_base;
	req.payload.register_db.jobq_size = jobq_size;
	req.payload.register_db.host_ssid = ctx_id;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_REGISTER_DB_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_err_ratelimited(vdev, "Failed to register doorbell %u: %d\n", db_id, ret);

	return ret;
}

int ivpu_jsm_unregister_db(struct ivpu_device *vdev, u32 db_id)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_UNREGISTER_DB };
	struct vpu_jsm_msg resp;
	int ret = 0;

	req.payload.unregister_db.db_idx = db_id;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_UNREGISTER_DB_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to unregister doorbell %u: %d\n", db_id, ret);

	return ret;
}

int ivpu_jsm_get_heartbeat(struct ivpu_device *vdev, u32 engine, u64 *heartbeat)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_QUERY_ENGINE_HB };
	struct vpu_jsm_msg resp;
	int ret;

	if (engine != VPU_ENGINE_COMPUTE)
		return -EINVAL;

	req.payload.query_engine_hb.engine_idx = engine;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_QUERY_ENGINE_HB_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_err_ratelimited(vdev, "Failed to get heartbeat from engine %d: %d\n",
				     engine, ret);
		return ret;
	}

	*heartbeat = resp.payload.query_engine_hb_done.heartbeat;
	return ret;
}

int ivpu_jsm_reset_engine(struct ivpu_device *vdev, u32 engine)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_ENGINE_RESET };
	struct vpu_jsm_msg resp;
	int ret;

	if (engine != VPU_ENGINE_COMPUTE)
		return -EINVAL;

	req.payload.engine_reset.engine_idx = engine;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_ENGINE_RESET_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_err_ratelimited(vdev, "Failed to reset engine %d: %d\n", engine, ret);
		ivpu_pm_trigger_recovery(vdev, "Engine reset failed");
	}

	return ret;
}

int ivpu_jsm_preempt_engine(struct ivpu_device *vdev, u32 engine, u32 preempt_id)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_ENGINE_PREEMPT };
	struct vpu_jsm_msg resp;
	int ret;

	if (engine != VPU_ENGINE_COMPUTE)
		return -EINVAL;

	req.payload.engine_preempt.engine_idx = engine;
	req.payload.engine_preempt.preempt_id = preempt_id;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_ENGINE_PREEMPT_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_err_ratelimited(vdev, "Failed to preempt engine %d: %d\n", engine, ret);

	return ret;
}

int ivpu_jsm_dyndbg_control(struct ivpu_device *vdev, char *command, size_t size)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_DYNDBG_CONTROL };
	struct vpu_jsm_msg resp;
	int ret;

	strscpy(req.payload.dyndbg_control.dyndbg_cmd, command, VPU_DYNDBG_CMD_MAX_LEN);

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_DYNDBG_CONTROL_RSP, &resp,
				    VPU_IPC_CHAN_GEN_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to send command \"%s\": ret %d\n",
				      command, ret);

	return ret;
}

int ivpu_jsm_trace_get_capability(struct ivpu_device *vdev, u32 *trace_destination_mask,
				  u64 *trace_hw_component_mask)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_TRACE_GET_CAPABILITY };
	struct vpu_jsm_msg resp;
	int ret;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_TRACE_GET_CAPABILITY_RSP, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "Failed to get trace capability: %d\n", ret);
		return ret;
	}

	*trace_destination_mask = resp.payload.trace_capability.trace_destination_mask;
	*trace_hw_component_mask = resp.payload.trace_capability.trace_hw_component_mask;

	return ret;
}

int ivpu_jsm_trace_set_config(struct ivpu_device *vdev, u32 trace_level, u32 trace_destination_mask,
			      u64 trace_hw_component_mask)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_TRACE_SET_CONFIG };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.trace_config.trace_level = trace_level;
	req.payload.trace_config.trace_destination_mask = trace_destination_mask;
	req.payload.trace_config.trace_hw_component_mask = trace_hw_component_mask;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_TRACE_SET_CONFIG_RSP, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to set config: %d\n", ret);

	return ret;
}

int ivpu_jsm_context_release(struct ivpu_device *vdev, u32 host_ssid)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_SSID_RELEASE };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.ssid_release.host_ssid = host_ssid;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_SSID_RELEASE_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to release context: %d\n", ret);

	return ret;
}

int ivpu_jsm_pwr_d0i3_enter(struct ivpu_device *vdev)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_PWR_D0I3_ENTER };
	struct vpu_jsm_msg resp;
	int ret;

	if (IVPU_WA(disable_d0i3_msg))
		return 0;

	req.payload.pwr_d0i3_enter.send_response = 1;

	ret = ivpu_ipc_send_receive_internal(vdev, &req, VPU_JSM_MSG_PWR_D0I3_ENTER_DONE, &resp,
					     VPU_IPC_CHAN_GEN_CMD, vdev->timeout.d0i3_entry_msg);
	if (ret)
		return ret;

	return ivpu_hw_wait_for_idle(vdev);
}

int ivpu_jsm_hws_create_cmdq(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_group, u32 cmdq_id,
			     u32 pid, u32 engine, u64 cmdq_base, u32 cmdq_size)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_CREATE_CMD_QUEUE };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.hws_create_cmdq.host_ssid = ctx_id;
	req.payload.hws_create_cmdq.process_id = pid;
	req.payload.hws_create_cmdq.engine_idx = engine;
	req.payload.hws_create_cmdq.cmdq_group = cmdq_group;
	req.payload.hws_create_cmdq.cmdq_id = cmdq_id;
	req.payload.hws_create_cmdq.cmdq_base = cmdq_base;
	req.payload.hws_create_cmdq.cmdq_size = cmdq_size;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_CREATE_CMD_QUEUE_RSP, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to create command queue: %d\n", ret);

	return ret;
}

int ivpu_jsm_hws_destroy_cmdq(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_DESTROY_CMD_QUEUE };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.hws_destroy_cmdq.host_ssid = ctx_id;
	req.payload.hws_destroy_cmdq.cmdq_id = cmdq_id;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_DESTROY_CMD_QUEUE_RSP, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to destroy command queue: %d\n", ret);

	return ret;
}

int ivpu_jsm_hws_register_db(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id, u32 db_id,
			     u64 cmdq_base, u32 cmdq_size)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_HWS_REGISTER_DB };
	struct vpu_jsm_msg resp;
	int ret = 0;

	req.payload.hws_register_db.db_id = db_id;
	req.payload.hws_register_db.host_ssid = ctx_id;
	req.payload.hws_register_db.cmdq_id = cmdq_id;
	req.payload.hws_register_db.cmdq_base = cmdq_base;
	req.payload.hws_register_db.cmdq_size = cmdq_size;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_REGISTER_DB_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_err_ratelimited(vdev, "Failed to register doorbell %u: %d\n", db_id, ret);

	return ret;
}

int ivpu_jsm_hws_resume_engine(struct ivpu_device *vdev, u32 engine)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_HWS_ENGINE_RESUME };
	struct vpu_jsm_msg resp;
	int ret;

	if (engine != VPU_ENGINE_COMPUTE)
		return -EINVAL;

	req.payload.hws_resume_engine.engine_idx = engine;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_HWS_RESUME_ENGINE_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_err_ratelimited(vdev, "Failed to resume engine %d: %d\n", engine, ret);
		ivpu_pm_trigger_recovery(vdev, "Engine resume failed");
	}

	return ret;
}

int ivpu_jsm_hws_set_context_sched_properties(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id,
					      u32 priority)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_SET_CONTEXT_SCHED_PROPERTIES };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.hws_set_context_sched_properties.host_ssid = ctx_id;
	req.payload.hws_set_context_sched_properties.cmdq_id = cmdq_id;
	req.payload.hws_set_context_sched_properties.priority_band = priority;
	req.payload.hws_set_context_sched_properties.realtime_priority_level = 0;
	req.payload.hws_set_context_sched_properties.in_process_priority = 0;
	req.payload.hws_set_context_sched_properties.context_quantum = 20000;
	req.payload.hws_set_context_sched_properties.grace_period_same_priority = 10000;
	req.payload.hws_set_context_sched_properties.grace_period_lower_priority = 0;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_SET_CONTEXT_SCHED_PROPERTIES_RSP, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to set context sched properties: %d\n", ret);

	return ret;
}

int ivpu_jsm_hws_set_scheduling_log(struct ivpu_device *vdev, u32 engine_idx, u32 host_ssid,
				    u64 vpu_log_buffer_va)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_HWS_SET_SCHEDULING_LOG };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.hws_set_scheduling_log.engine_idx = engine_idx;
	req.payload.hws_set_scheduling_log.host_ssid = host_ssid;
	req.payload.hws_set_scheduling_log.vpu_log_buffer_va = vpu_log_buffer_va;
	req.payload.hws_set_scheduling_log.notify_index = 0;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_HWS_SET_SCHEDULING_LOG_RSP, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to set scheduling log: %d\n", ret);

	return ret;
}

int ivpu_jsm_hws_setup_priority_bands(struct ivpu_device *vdev)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_SET_PRIORITY_BAND_SETUP };
	struct vpu_jsm_msg resp;
	struct ivpu_hw_info *hw = vdev->hw;
	struct vpu_ipc_msg_payload_hws_priority_band_setup *setup =
		&req.payload.hws_priority_band_setup;
	int ret;

	for (int band = VPU_JOB_SCHEDULING_PRIORITY_BAND_IDLE;
	     band < VPU_JOB_SCHEDULING_PRIORITY_BAND_COUNT; band++) {
		setup->grace_period[band] = hw->hws.grace_period[band];
		setup->process_grace_period[band] = hw->hws.process_grace_period[band];
		setup->process_quantum[band] = hw->hws.process_quantum[band];
	}
	setup->normal_band_percentage = 10;

	ret = ivpu_ipc_send_receive_internal(vdev, &req, VPU_JSM_MSG_SET_PRIORITY_BAND_SETUP_RSP,
					     &resp, VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to set priority bands: %d\n", ret);

	return ret;
}

int ivpu_jsm_metric_streamer_start(struct ivpu_device *vdev, u64 metric_group_mask,
				   u64 sampling_rate, u64 buffer_addr, u64 buffer_size)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_METRIC_STREAMER_START };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.metric_streamer_start.metric_group_mask = metric_group_mask;
	req.payload.metric_streamer_start.sampling_rate = sampling_rate;
	req.payload.metric_streamer_start.buffer_addr = buffer_addr;
	req.payload.metric_streamer_start.buffer_size = buffer_size;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_METRIC_STREAMER_START_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "Failed to start metric streamer: ret %d\n", ret);
		return ret;
	}

	return ret;
}

int ivpu_jsm_metric_streamer_stop(struct ivpu_device *vdev, u64 metric_group_mask)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_METRIC_STREAMER_STOP };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.metric_streamer_stop.metric_group_mask = metric_group_mask;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_METRIC_STREAMER_STOP_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret)
		ivpu_warn_ratelimited(vdev, "Failed to stop metric streamer: ret %d\n", ret);

	return ret;
}

int ivpu_jsm_metric_streamer_update(struct ivpu_device *vdev, u64 metric_group_mask,
				    u64 buffer_addr, u64 buffer_size, u64 *bytes_written)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_METRIC_STREAMER_UPDATE };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.metric_streamer_update.metric_group_mask = metric_group_mask;
	req.payload.metric_streamer_update.buffer_addr = buffer_addr;
	req.payload.metric_streamer_update.buffer_size = buffer_size;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_METRIC_STREAMER_UPDATE_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "Failed to update metric streamer: ret %d\n", ret);
		return ret;
	}

	if (buffer_size && resp.payload.metric_streamer_done.bytes_written > buffer_size) {
		ivpu_warn_ratelimited(vdev, "MS buffer overflow: bytes_written %#llx > buffer_size %#llx\n",
				      resp.payload.metric_streamer_done.bytes_written, buffer_size);
		return -EOVERFLOW;
	}

	*bytes_written = resp.payload.metric_streamer_done.bytes_written;

	return ret;
}

int ivpu_jsm_metric_streamer_info(struct ivpu_device *vdev, u64 metric_group_mask, u64 buffer_addr,
				  u64 buffer_size, u32 *sample_size, u64 *info_size)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_METRIC_STREAMER_INFO };
	struct vpu_jsm_msg resp;
	int ret;

	req.payload.metric_streamer_start.metric_group_mask = metric_group_mask;
	req.payload.metric_streamer_start.buffer_addr = buffer_addr;
	req.payload.metric_streamer_start.buffer_size = buffer_size;

	ret = ivpu_ipc_send_receive(vdev, &req, VPU_JSM_MSG_METRIC_STREAMER_INFO_DONE, &resp,
				    VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "Failed to get metric streamer info: ret %d\n", ret);
		return ret;
	}

	if (!resp.payload.metric_streamer_done.sample_size) {
		ivpu_warn_ratelimited(vdev, "Invalid sample size\n");
		return -EBADMSG;
	}

	if (sample_size)
		*sample_size = resp.payload.metric_streamer_done.sample_size;
	if (info_size)
		*info_size = resp.payload.metric_streamer_done.bytes_written;

	return ret;
}

int ivpu_jsm_dct_enable(struct ivpu_device *vdev, u32 active_us, u32 inactive_us)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_DCT_ENABLE };
	struct vpu_jsm_msg resp;

	req.payload.pwr_dct_control.dct_active_us = active_us;
	req.payload.pwr_dct_control.dct_inactive_us = inactive_us;

	return ivpu_ipc_send_receive_internal(vdev, &req, VPU_JSM_MSG_DCT_ENABLE_DONE, &resp,
					      VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
}

int ivpu_jsm_dct_disable(struct ivpu_device *vdev)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_DCT_DISABLE };
	struct vpu_jsm_msg resp;

	return ivpu_ipc_send_receive_internal(vdev, &req, VPU_JSM_MSG_DCT_DISABLE_DONE, &resp,
					      VPU_IPC_CHAN_ASYNC_CMD, vdev->timeout.jsm);
}

int ivpu_jsm_state_dump(struct ivpu_device *vdev)
{
	struct vpu_jsm_msg req = { .type = VPU_JSM_MSG_STATE_DUMP };

	return ivpu_ipc_send_and_wait(vdev, &req, VPU_IPC_CHAN_ASYNC_CMD,
				      vdev->timeout.state_dump_msg);
}
