// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen1.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_instance.h"

static void
iris_hfi_gen1_sys_event_notify(struct iris_core *core, void *packet)
{
	struct hfi_msg_event_notify_pkt *pkt = packet;

	if (pkt->event_id == HFI_EVENT_SYS_ERROR)
		dev_err(core->dev, "sys error (type: %x, session id:%x, data1:%x, data2:%x)\n",
			pkt->event_id, pkt->shdr.session_id, pkt->event_data1,
			pkt->event_data2);

	core->state = IRIS_CORE_ERROR;
	schedule_delayed_work(&core->sys_error_handler, msecs_to_jiffies(10));
}

static void
iris_hfi_gen1_event_session_error(struct iris_inst *inst, struct hfi_msg_event_notify_pkt *pkt)
{
	switch (pkt->event_data1) {
	/* non fatal session errors */
	case HFI_ERR_SESSION_INVALID_SCALE_FACTOR:
	case HFI_ERR_SESSION_UNSUPPORT_BUFFERTYPE:
	case HFI_ERR_SESSION_UNSUPPORTED_SETTING:
	case HFI_ERR_SESSION_UPSCALE_NOT_SUPPORTED:
		dev_dbg(inst->core->dev, "session error: event id:%x, session id:%x\n",
			pkt->event_data1, pkt->shdr.session_id);
		break;
	/* fatal session errors */
	default:
		/*
		 * firmware fills event_data2 as an additional information about the
		 * hfi command for which session error has ouccured.
		 */
		dev_err(inst->core->dev,
			"session error for command: %x, event id:%x, session id:%x\n",
			pkt->event_data2, pkt->event_data1,
			pkt->shdr.session_id);
		iris_vb2_queue_error(inst);
		break;
	}
}

static void iris_hfi_gen1_session_event_notify(struct iris_inst *inst, void *packet)
{
	struct hfi_msg_event_notify_pkt *pkt = packet;

	switch (pkt->event_id) {
	case HFI_EVENT_SESSION_ERROR:
		iris_hfi_gen1_event_session_error(inst, pkt);
		break;
	default:
		break;
	}
}

static void iris_hfi_gen1_sys_init_done(struct iris_core *core, void *packet)
{
	struct hfi_msg_sys_init_done_pkt *pkt = packet;

	if (pkt->error_type != HFI_ERR_NONE) {
		core->state = IRIS_CORE_ERROR;
		return;
	}

	complete(&core->core_init_done);
}

static void
iris_hfi_gen1_sys_get_prop_image_version(struct iris_core *core,
					 struct hfi_msg_sys_property_info_pkt *pkt)
{
	int req_bytes = pkt->hdr.size - sizeof(*pkt);
	char fw_version[IRIS_FW_VERSION_LENGTH];
	u8 *str_image_version;
	u32 i;

	if (req_bytes < IRIS_FW_VERSION_LENGTH - 1 || !pkt->data[0] || pkt->num_properties > 1) {
		dev_err(core->dev, "bad packet\n");
		return;
	}

	str_image_version = pkt->data;
	if (!str_image_version) {
		dev_err(core->dev, "firmware version not available\n");
		return;
	}

	for (i = 0; i < IRIS_FW_VERSION_LENGTH - 1; i++) {
		if (str_image_version[i] != '\0')
			fw_version[i] = str_image_version[i];
		else
			fw_version[i] = ' ';
	}
	fw_version[i] = '\0';
	dev_dbg(core->dev, "firmware version: %s\n", fw_version);
}

static void iris_hfi_gen1_sys_property_info(struct iris_core *core, void *packet)
{
	struct hfi_msg_sys_property_info_pkt *pkt = packet;

	if (!pkt->num_properties) {
		dev_dbg(core->dev, "no properties\n");
		return;
	}

	switch (pkt->property) {
	case HFI_PROPERTY_SYS_IMAGE_VERSION:
		iris_hfi_gen1_sys_get_prop_image_version(core, pkt);
		break;
	default:
		dev_dbg(core->dev, "unknown property data\n");
		break;
	}
}

struct iris_hfi_gen1_response_pkt_info {
	u32 pkt;
	u32 pkt_sz;
};

static const struct iris_hfi_gen1_response_pkt_info pkt_infos[] = {
	{
	 .pkt = HFI_MSG_EVENT_NOTIFY,
	 .pkt_sz = sizeof(struct hfi_msg_event_notify_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_INIT,
	 .pkt_sz = sizeof(struct hfi_msg_sys_init_done_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_PROPERTY_INFO,
	 .pkt_sz = sizeof(struct hfi_msg_sys_property_info_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_SESSION_INIT,
	 .pkt_sz = sizeof(struct hfi_msg_session_init_done_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_SESSION_END,
	 .pkt_sz = sizeof(struct hfi_msg_session_hdr_pkt),
	},
};

static void iris_hfi_gen1_handle_response(struct iris_core *core, void *response)
{
	struct hfi_pkt_hdr *hdr = (struct hfi_pkt_hdr *)response;
	const struct iris_hfi_gen1_response_pkt_info *pkt_info;
	struct device *dev = core->dev;
	struct hfi_session_pkt *pkt;
	struct iris_inst *inst;
	bool found = false;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(pkt_infos); i++) {
		pkt_info = &pkt_infos[i];
		if (pkt_info->pkt != hdr->pkt_type)
			continue;
		found = true;
		break;
	}

	if (!found || hdr->size < pkt_info->pkt_sz) {
		dev_err(dev, "bad packet size (%d should be %d, pkt type:%x, found %d)\n",
			hdr->size, pkt_info->pkt_sz, hdr->pkt_type, found);

		return;
	}

	switch (hdr->pkt_type) {
	case HFI_MSG_SYS_INIT:
		iris_hfi_gen1_sys_init_done(core, hdr);
		break;
	case HFI_MSG_SYS_PROPERTY_INFO:
		iris_hfi_gen1_sys_property_info(core, hdr);
		break;
	case HFI_MSG_EVENT_NOTIFY:
		pkt = (struct hfi_session_pkt *)hdr;
		inst = iris_get_instance(core, pkt->shdr.session_id);
		if (inst) {
			mutex_lock(&inst->lock);
			iris_hfi_gen1_session_event_notify(inst, hdr);
			mutex_unlock(&inst->lock);
		} else {
			iris_hfi_gen1_sys_event_notify(core, hdr);
		}

		break;
	default:
		pkt = (struct hfi_session_pkt *)hdr;
		inst = iris_get_instance(core, pkt->shdr.session_id);
		if (!inst) {
			dev_warn(dev, "no valid instance(pkt session_id:%x, pkt:%x)\n",
				 pkt->shdr.session_id,
				 pkt_info ? pkt_info->pkt : 0);
			return;
		}

		mutex_lock(&inst->lock);
		complete(&inst->completion);
		mutex_unlock(&inst->lock);

		break;
	}
}

static void iris_hfi_gen1_flush_debug_queue(struct iris_core *core, u8 *packet)
{
	struct hfi_msg_sys_coverage_pkt *pkt;

	while (!iris_hfi_queue_dbg_read(core, packet)) {
		pkt = (struct hfi_msg_sys_coverage_pkt *)packet;

		if (pkt->hdr.pkt_type != HFI_MSG_SYS_COV) {
			struct hfi_msg_sys_debug_pkt *pkt =
				(struct hfi_msg_sys_debug_pkt *)packet;

			dev_dbg(core->dev, "%s", pkt->msg_data);
		}
	}
}

static void iris_hfi_gen1_response_handler(struct iris_core *core)
{
	memset(core->response_packet, 0, sizeof(struct hfi_pkt_hdr));
	while (!iris_hfi_queue_msg_read(core, core->response_packet)) {
		iris_hfi_gen1_handle_response(core, core->response_packet);
		memset(core->response_packet, 0, sizeof(struct hfi_pkt_hdr));
	}

	iris_hfi_gen1_flush_debug_queue(core, core->response_packet);
}

static const struct iris_hfi_response_ops iris_hfi_gen1_response_ops = {
	.hfi_response_handler = iris_hfi_gen1_response_handler,
};

void iris_hfi_gen1_response_ops_init(struct iris_core *core)
{
	core->hfi_response_ops = &iris_hfi_gen1_response_ops;
}
