// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) System Power Protocol
 *
 * Copyright (C) 2020 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications SYSTEM - " fmt

#include <linux/scmi_protocol.h>

#include "common.h"
#include "notify.h"

#define SCMI_SYSTEM_NUM_SOURCES		1

enum scmi_system_protocol_cmd {
	SYSTEM_POWER_STATE_NOTIFY = 0x5,
};

struct scmi_system_power_state_notify {
	__le32 notify_enable;
};

struct scmi_system_power_state_notifier_payld {
	__le32 agent_id;
	__le32 flags;
	__le32 system_state;
};

struct scmi_system_info {
	u32 version;
};

static int scmi_system_request_notify(const struct scmi_handle *handle,
				      bool enable)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_system_power_state_notify *notify;

	ret = scmi_xfer_get_init(handle, SYSTEM_POWER_STATE_NOTIFY,
				 SCMI_PROTOCOL_SYSTEM, sizeof(*notify), 0, &t);
	if (ret)
		return ret;

	notify = t->tx.buf;
	notify->notify_enable = enable ? cpu_to_le32(BIT(0)) : 0;

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_system_set_notify_enabled(const struct scmi_handle *handle,
					  u8 evt_id, u32 src_id, bool enable)
{
	int ret;

	ret = scmi_system_request_notify(handle, enable);
	if (ret)
		pr_debug("FAIL_ENABLE - evt[%X] - ret:%d\n", evt_id, ret);

	return ret;
}

static void *scmi_system_fill_custom_report(const struct scmi_handle *handle,
					    u8 evt_id, ktime_t timestamp,
					    const void *payld, size_t payld_sz,
					    void *report, u32 *src_id)
{
	const struct scmi_system_power_state_notifier_payld *p = payld;
	struct scmi_system_power_state_notifier_report *r = report;

	if (evt_id != SCMI_EVENT_SYSTEM_POWER_STATE_NOTIFIER ||
	    sizeof(*p) != payld_sz)
		return NULL;

	r->timestamp = timestamp;
	r->agent_id = le32_to_cpu(p->agent_id);
	r->flags = le32_to_cpu(p->flags);
	r->system_state = le32_to_cpu(p->system_state);
	*src_id = 0;

	return r;
}

static const struct scmi_event system_events[] = {
	{
		.id = SCMI_EVENT_SYSTEM_POWER_STATE_NOTIFIER,
		.max_payld_sz =
			sizeof(struct scmi_system_power_state_notifier_payld),
		.max_report_sz =
			sizeof(struct scmi_system_power_state_notifier_report),
	},
};

static const struct scmi_event_ops system_event_ops = {
	.set_notify_enabled = scmi_system_set_notify_enabled,
	.fill_custom_report = scmi_system_fill_custom_report,
};

static int scmi_system_protocol_init(struct scmi_handle *handle)
{
	u32 version;
	struct scmi_system_info *pinfo;

	scmi_version_get(handle, SCMI_PROTOCOL_SYSTEM, &version);

	dev_dbg(handle->dev, "System Power Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(handle->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	scmi_register_protocol_events(handle,
				      SCMI_PROTOCOL_SYSTEM, SCMI_PROTO_QUEUE_SZ,
				      &system_event_ops,
				      system_events,
				      ARRAY_SIZE(system_events),
				      SCMI_SYSTEM_NUM_SOURCES);

	pinfo->version = version;
	handle->system_priv = pinfo;

	return 0;
}

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(SCMI_PROTOCOL_SYSTEM, system)
