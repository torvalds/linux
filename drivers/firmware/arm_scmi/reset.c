// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Reset Protocol
 *
 * Copyright (C) 2019-2022 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications RESET - " fmt

#include <linux/module.h>
#include <linux/scmi_protocol.h>

#include "protocols.h"
#include "notify.h"

enum scmi_reset_protocol_cmd {
	RESET_DOMAIN_ATTRIBUTES = 0x3,
	RESET = 0x4,
	RESET_NOTIFY = 0x5,
	RESET_DOMAIN_NAME_GET = 0x6,
};

#define NUM_RESET_DOMAIN_MASK	0xffff
#define RESET_NOTIFY_ENABLE	BIT(0)

struct scmi_msg_resp_reset_domain_attributes {
	__le32 attributes;
#define SUPPORTS_ASYNC_RESET(x)		((x) & BIT(31))
#define SUPPORTS_NOTIFY_RESET(x)	((x) & BIT(30))
#define SUPPORTS_EXTENDED_NAMES(x)	((x) & BIT(29))
	__le32 latency;
	u8 name[SCMI_SHORT_NAME_MAX_SIZE];
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

static int scmi_reset_attributes_get(const struct scmi_protocol_handle *ph,
				     struct scmi_reset_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	u32 attr;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES,
				      0, sizeof(attr), &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		attr = get_unaligned_le32(t->rx.buf);
		pi->num_domains = attr & NUM_RESET_DOMAIN_MASK;
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_reset_domain_attributes_get(const struct scmi_protocol_handle *ph,
				 u32 domain, struct reset_dom_info *dom_info,
				 u32 version)
{
	int ret;
	u32 attributes;
	struct scmi_xfer *t;
	struct scmi_msg_resp_reset_domain_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, RESET_DOMAIN_ATTRIBUTES,
				      sizeof(domain), sizeof(*attr), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);
	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		attributes = le32_to_cpu(attr->attributes);

		dom_info->async_reset = SUPPORTS_ASYNC_RESET(attributes);
		dom_info->reset_notify = SUPPORTS_NOTIFY_RESET(attributes);
		dom_info->latency_us = le32_to_cpu(attr->latency);
		if (dom_info->latency_us == U32_MAX)
			dom_info->latency_us = 0;
		strscpy(dom_info->name, attr->name, SCMI_SHORT_NAME_MAX_SIZE);
	}

	ph->xops->xfer_put(ph, t);

	/*
	 * If supported overwrite short name with the extended one;
	 * on error just carry on and use already provided short name.
	 */
	if (!ret && PROTOCOL_REV_MAJOR(version) >= 0x3 &&
	    SUPPORTS_EXTENDED_NAMES(attributes))
		ph->hops->extended_name_get(ph, RESET_DOMAIN_NAME_GET, domain,
					    dom_info->name, SCMI_MAX_STR_SIZE);

	return ret;
}

static int scmi_reset_num_domains_get(const struct scmi_protocol_handle *ph)
{
	struct scmi_reset_info *pi = ph->get_priv(ph);

	return pi->num_domains;
}

static const char *
scmi_reset_name_get(const struct scmi_protocol_handle *ph, u32 domain)
{
	struct scmi_reset_info *pi = ph->get_priv(ph);

	struct reset_dom_info *dom = pi->dom_info + domain;

	return dom->name;
}

static int scmi_reset_latency_get(const struct scmi_protocol_handle *ph,
				  u32 domain)
{
	struct scmi_reset_info *pi = ph->get_priv(ph);
	struct reset_dom_info *dom = pi->dom_info + domain;

	return dom->latency_us;
}

static int scmi_domain_reset(const struct scmi_protocol_handle *ph, u32 domain,
			     u32 flags, u32 state)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_reset_domain_reset *dom;
	struct scmi_reset_info *pi = ph->get_priv(ph);
	struct reset_dom_info *rdom = pi->dom_info + domain;

	if (rdom->async_reset)
		flags |= ASYNCHRONOUS_RESET;

	ret = ph->xops->xfer_get_init(ph, RESET, sizeof(*dom), 0, &t);
	if (ret)
		return ret;

	dom = t->tx.buf;
	dom->domain_id = cpu_to_le32(domain);
	dom->flags = cpu_to_le32(flags);
	dom->reset_state = cpu_to_le32(state);

	if (rdom->async_reset)
		ret = ph->xops->do_xfer_with_response(ph, t);
	else
		ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_reset_domain_reset(const struct scmi_protocol_handle *ph,
				   u32 domain)
{
	return scmi_domain_reset(ph, domain, AUTONOMOUS_RESET,
				 ARCH_COLD_RESET);
}

static int
scmi_reset_domain_assert(const struct scmi_protocol_handle *ph, u32 domain)
{
	return scmi_domain_reset(ph, domain, EXPLICIT_RESET_ASSERT,
				 ARCH_COLD_RESET);
}

static int
scmi_reset_domain_deassert(const struct scmi_protocol_handle *ph, u32 domain)
{
	return scmi_domain_reset(ph, domain, 0, ARCH_COLD_RESET);
}

static const struct scmi_reset_proto_ops reset_proto_ops = {
	.num_domains_get = scmi_reset_num_domains_get,
	.name_get = scmi_reset_name_get,
	.latency_get = scmi_reset_latency_get,
	.reset = scmi_reset_domain_reset,
	.assert = scmi_reset_domain_assert,
	.deassert = scmi_reset_domain_deassert,
};

static int scmi_reset_notify(const struct scmi_protocol_handle *ph,
			     u32 domain_id, bool enable)
{
	int ret;
	u32 evt_cntl = enable ? RESET_TP_NOTIFY_ALL : 0;
	struct scmi_xfer *t;
	struct scmi_msg_reset_notify *cfg;

	ret = ph->xops->xfer_get_init(ph, RESET_NOTIFY, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(domain_id);
	cfg->event_control = cpu_to_le32(evt_cntl);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_reset_set_notify_enabled(const struct scmi_protocol_handle *ph,
					 u8 evt_id, u32 src_id, bool enable)
{
	int ret;

	ret = scmi_reset_notify(ph, src_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);

	return ret;
}

static void *
scmi_reset_fill_custom_report(const struct scmi_protocol_handle *ph,
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

static int scmi_reset_get_num_sources(const struct scmi_protocol_handle *ph)
{
	struct scmi_reset_info *pinfo = ph->get_priv(ph);

	if (!pinfo)
		return -EINVAL;

	return pinfo->num_domains;
}

static const struct scmi_event reset_events[] = {
	{
		.id = SCMI_EVENT_RESET_ISSUED,
		.max_payld_sz = sizeof(struct scmi_reset_issued_notify_payld),
		.max_report_sz = sizeof(struct scmi_reset_issued_report),
	},
};

static const struct scmi_event_ops reset_event_ops = {
	.get_num_sources = scmi_reset_get_num_sources,
	.set_notify_enabled = scmi_reset_set_notify_enabled,
	.fill_custom_report = scmi_reset_fill_custom_report,
};

static const struct scmi_protocol_events reset_protocol_events = {
	.queue_sz = SCMI_PROTO_QUEUE_SZ,
	.ops = &reset_event_ops,
	.evts = reset_events,
	.num_events = ARRAY_SIZE(reset_events),
};

static int scmi_reset_protocol_init(const struct scmi_protocol_handle *ph)
{
	int domain, ret;
	u32 version;
	struct scmi_reset_info *pinfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_dbg(ph->dev, "Reset Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(ph->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	ret = scmi_reset_attributes_get(ph, pinfo);
	if (ret)
		return ret;

	pinfo->dom_info = devm_kcalloc(ph->dev, pinfo->num_domains,
				       sizeof(*pinfo->dom_info), GFP_KERNEL);
	if (!pinfo->dom_info)
		return -ENOMEM;

	for (domain = 0; domain < pinfo->num_domains; domain++) {
		struct reset_dom_info *dom = pinfo->dom_info + domain;

		scmi_reset_domain_attributes_get(ph, domain, dom, version);
	}

	pinfo->version = version;
	return ph->set_priv(ph, pinfo);
}

static const struct scmi_protocol scmi_reset = {
	.id = SCMI_PROTOCOL_RESET,
	.owner = THIS_MODULE,
	.instance_init = &scmi_reset_protocol_init,
	.ops = &reset_proto_ops,
	.events = &reset_protocol_events,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(reset, scmi_reset)
