// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) System Power Protocol
 *
 * Copyright (C) 2020-2021 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications SYSTEM - " fmt

#include <linux/module.h>
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

static int scmi_system_request_notify(const struct scmi_protocol_handle *ph,
				      bool enable)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_system_power_state_notify *notify;

	ret = ph->xops->xfer_get_init(ph, SYSTEM_POWER_STATE_NOTIFY,
				      sizeof(*notify), 0, &t);
	if (ret)
		return ret;

	notify = t->tx.buf;
	notify->notify_enable = enable ? cpu_to_le32(BIT(0)) : 0;

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_system_set_notify_enabled(const struct scmi_protocol_handle *ph,
					  u8 evt_id, u32 src_id, bool enable)
{
	int ret;

	ret = scmi_system_request_notify(ph, enable);
	if (ret)
		pr_debug("FAIL_ENABLE - evt[%X] - ret:%d\n", evt_id, ret);

	return ret;
}

static void *
scmi_system_fill_custom_report(const struct scmi_protocol_handle *ph,
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

static const struct scmi_protocol_events system_protocol_events = {
	.queue_sz = SCMI_PROTO_QUEUE_SZ,
	.ops = &system_event_ops,
	.evts = system_events,
	.num_events = ARRAY_SIZE(system_events),
	.num_sources = SCMI_SYSTEM_NUM_SOURCES,
};

static int scmi_system_protocol_init(const struct scmi_protocol_handle *ph)
{
	u32 version;
	struct scmi_system_info *pinfo;

	ph->xops->version_get(ph, &version);

	dev_dbg(ph->dev, "System Power Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(ph->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->version = version;
	return ph->set_priv(ph, pinfo);
}

static const struct scmi_protocol scmi_system = {
	.id = SCMI_PROTOCOL_SYSTEM,
	.owner = THIS_MODULE,
	.instance_init = &scmi_system_protocol_init,
	.ops = NULL,
	.events = &system_protocol_events,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(system, scmi_system)
