// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Reset Protocol
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications RESET - " fmt

#include <linux/scmi_protocol.h>

#include "common.h"
#include "notify.h"

enum scmi_reset_protocol_cmd {
	RESET_DOMAIN_ATTRIBUTES = 0x3,
	RESET = 0x4,
	RESET_NOTIFY = 0x5,
};

#define NUM_RESET_DOMAIN_MASK	0xffff
#define RESET_NOTIFY_ENABLE	BIT(0)

struct scmi_msg_resp_reset_domain_attributes {
	__le32 attributes;
#define SUPPORTS_ASYNC_RESET(x)		((x) & BIT(31))
#define SUPPORTS_NOTIFY_RESET(x)	((x) & BIT(30))
	__le32 latency;
	    u8 name[SCMI_MAX_STR_SIZE];
};

struct scmi_msg_reset_domain_reset {
	__le32 domain_id;
	__le32 flags;
#define AUTONOMOUS_RESET	BIT(0)
#define EXPLICIT_RESET_ASSERT	BIT(1)
#define ASYNCHRONOUS_RESET	BIT(2)
	__le32 reset_state;
#define ARCH_COLD_RESET		0
};

struct scmi_msg_reset_notify {
	__le32 id;
	__le32 event_control;
#define RESET_TP_NOTIFY_ALL	BIT(0)
};

struct scmi_reset_issued_notify_payld {
	__le32 agent_id;
	__le32 domain_id;
	__le32 reset_state;
};

struct reset_dom_info {
	bool async_reset;
	bool reset_notify;
	u32 latency_us;
	char name[SCMI_MAX_STR_SIZE];
};

struct scmi_reset_info {
	u32 version;
	int num_domains;
	struct reset_dom_info *dom_info;
};

static int scmi_reset_attributes_get(const struct scmi_handle *handle,
				     struct scmi_reset_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	u32 attr;

	ret = scmi_xfer_get_init(handle, PROTOCOL_ATTRIBUTES,
				 SCMI_PROTOCOL_RESET, 0, sizeof(attr), &t);
	if (ret)
		return ret;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		attr = get_unaligned_le32(t->rx.buf);
		pi->num_domains = attr & NUM_RESET_DOMAIN_MASK;
	}

	scmi_xfer_put(handle, t);
	return ret;
}

static int
scmi_reset_domain_attributes_get(const struct scmi_handle *handle, u32 domain,
				 struct reset_dom_info *dom_info)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_reset_domain_attributes *attr;

	ret = scmi_xfer_get_init(handle, RESET_DOMAIN_ATTRIBUTES,
				 SCMI_PROTOCOL_RESET, sizeof(domain),
				 sizeof(*attr), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);
	attr = t->rx.buf;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		u32 attributes = le32_to_cpu(attr->attributes);

		dom_info->async_reset = SUPPORTS_ASYNC_RESET(attributes);
		dom_info->reset_notify = SUPPORTS_NOTIFY_RESET(attributes);
		dom_info->latency_us = le32_to_cpu(attr->latency);
		if (dom_info->latency_us == U32_MAX)
			dom_info->latency_us = 0;
		strlcpy(dom_info->name, attr->name, SCMI_MAX_STR_SIZE);
	}

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_reset_num_domains_get(const struct scmi_handle *handle)
{
	struct scmi_reset_info *pi = handle->reset_priv;

	return pi->num_domains;
}

static char *scmi_reset_name_get(const struct scmi_handle *handle, u32 domain)
{
	struct scmi_reset_info *pi = handle->reset_priv;
	struct reset_dom_info *dom = pi->dom_info + domain;

	return dom->name;
}

static int scmi_reset_latency_get(const struct scmi_handle *handle, u32 domain)
{
	struct scmi_reset_info *pi = handle->reset_priv;
	struct reset_dom_info *dom = pi->dom_info + domain;

	return dom->latency_us;
}

static int scmi_domain_reset(const struct scmi_handle *handle, u32 domain,
			     u32 flags, u32 state)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_reset_domain_reset *dom;
	struct scmi_reset_info *pi = handle->reset_priv;
	struct reset_dom_info *rdom = pi->dom_info + domain;

	if (rdom->async_reset)
		flags |= ASYNCHRONOUS_RESET;

	ret = scmi_xfer_get_init(handle, RESET, SCMI_PROTOCOL_RESET,
				 sizeof(*dom), 0, &t);
	if (ret)
		return ret;

	dom = t->tx.buf;
	dom->domain_id = cpu_to_le32(domain);
	dom->flags = cpu_to_le32(flags);
	dom->reset_state = cpu_to_le32(state);

	if (rdom->async_reset)
		ret = scmi_do_xfer_with_response(handle, t);
	else
		ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_reset_domain_reset(const struct scmi_handle *handle, u32 domain)
{
	return scmi_domain_reset(handle, domain, AUTONOMOUS_RESET,
				 ARCH_COLD_RESET);
}

static int
scmi_reset_domain_assert(const struct scmi_handle *handle, u32 domain)
{
	return scmi_domain_reset(handle, domain, EXPLICIT_RESET_ASSERT,
				 ARCH_COLD_RESET);
}

static int
scmi_reset_domain_deassert(const struct scmi_handle *handle, u32 domain)
{
	return scmi_domain_reset(handle, domain, 0, ARCH_COLD_RESET);
}

static const struct scmi_reset_ops reset_ops = {
	.num_domains_get = scmi_reset_num_domains_get,
	.name_get = scmi_reset_name_get,
	.latency_get = scmi_reset_latency_get,
	.reset = scmi_reset_domain_reset,
	.assert = scmi_reset_domain_assert,
	.deassert = scmi_reset_domain_deassert,
};

static int scmi_reset_notify(const struct scmi_handle *handle, u32 domain_id,
			     bool enable)
{
	int ret;
	u32 evt_cntl = enable ? RESET_TP_NOTIFY_ALL : 0;
	struct scmi_xfer *t;
	struct scmi_msg_reset_notify *cfg;

	ret = scmi_xfer_get_init(handle, RESET_NOTIFY,
				 SCMI_PROTOCOL_RESET, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(domain_id);
	cfg->event_control = cpu_to_le32(evt_cntl);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_reset_set_notify_enabled(const struct scmi_handle *handle,
					 u8 evt_id, u32 src_id, bool enable)
{
	int ret;

	ret = scmi_reset_notify(handle, src_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);

	return ret;
}

static void *scmi_reset_fill_custom_report(const struct scmi_handle *handle,
					   u8 evt_id, ktime_t timestamp,
					   const void *payld, size_t payld_sz,
					   void *report, u32 *src_id)
{
	const struct scmi_reset_issued_notify_payld *p = payld;
	struct scmi_reset_issued_report *r = report;

	if (evt_id != SCMI_EVENT_RESET_ISSUED || sizeof(*p) != payld_sz)
		return NULL;

	r->timestamp = timestamp;
	r->agent_id = le32_to_cpu(p->agent_id);
	r->domain_id = le32_to_cpu(p->domain_id);
	r->reset_state = le32_to_cpu(p->reset_state);
	*src_id = r->domain_id;

	return r;
}

static const struct scmi_event reset_events[] = {
	{
		.id = SCMI_EVENT_RESET_ISSUED,
		.max_payld_sz = sizeof(struct scmi_reset_issued_notify_payld),
		.max_report_sz = sizeof(struct scmi_reset_issued_report),
	},
};

static const struct scmi_event_ops reset_event_ops = {
	.set_notify_enabled = scmi_reset_set_notify_enabled,
	.fill_custom_report = scmi_reset_fill_custom_report,
};

static int scmi_reset_protocol_init(struct scmi_handle *handle)
{
	int domain;
	u32 version;
	struct scmi_reset_info *pinfo;

	scmi_version_get(handle, SCMI_PROTOCOL_RESET, &version);

	dev_dbg(handle->dev, "Reset Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(handle->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	scmi_reset_attributes_get(handle, pinfo);

	pinfo->dom_info = devm_kcalloc(handle->dev, pinfo->num_domains,
				       sizeof(*pinfo->dom_info), GFP_KERNEL);
	if (!pinfo->dom_info)
		return -ENOMEM;

	for (domain = 0; domain < pinfo->num_domains; domain++) {
		struct reset_dom_info *dom = pinfo->dom_info + domain;

		scmi_reset_domain_attributes_get(handle, domain, dom);
	}

	scmi_register_protocol_events(handle,
				      SCMI_PROTOCOL_RESET, SCMI_PROTO_QUEUE_SZ,
				      &reset_event_ops, reset_events,
				      ARRAY_SIZE(reset_events),
				      pinfo->num_domains);

	pinfo->version = version;
	handle->reset_ops = &reset_ops;
	handle->reset_priv = pinfo;

	return 0;
}

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(SCMI_PROTOCOL_RESET, reset)
