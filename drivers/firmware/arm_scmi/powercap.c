// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Powercap Protocol
 *
 * Copyright (C) 2022 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications POWERCAP - " fmt

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/scmi_protocol.h>

#include <trace/events/scmi.h>

#include "protocols.h"
#include "notify.h"

enum scmi_powercap_protocol_cmd {
	POWERCAP_DOMAIN_ATTRIBUTES = 0x3,
	POWERCAP_CAP_GET = 0x4,
	POWERCAP_CAP_SET = 0x5,
	POWERCAP_PAI_GET = 0x6,
	POWERCAP_PAI_SET = 0x7,
	POWERCAP_DOMAIN_NAME_GET = 0x8,
	POWERCAP_MEASUREMENTS_GET = 0x9,
	POWERCAP_CAP_NOTIFY = 0xa,
	POWERCAP_MEASUREMENTS_NOTIFY = 0xb,
	POWERCAP_DESCRIBE_FASTCHANNEL = 0xc,
};

enum {
	POWERCAP_FC_CAP,
	POWERCAP_FC_PAI,
	POWERCAP_FC_MAX,
};

struct scmi_msg_resp_powercap_domain_attributes {
	__le32 attributes;
#define SUPPORTS_POWERCAP_CAP_CHANGE_NOTIFY(x)		((x) & BIT(31))
#define SUPPORTS_POWERCAP_MEASUREMENTS_CHANGE_NOTIFY(x)	((x) & BIT(30))
#define SUPPORTS_ASYNC_POWERCAP_CAP_SET(x)		((x) & BIT(29))
#define SUPPORTS_EXTENDED_NAMES(x)			((x) & BIT(28))
#define SUPPORTS_POWERCAP_CAP_CONFIGURATION(x)		((x) & BIT(27))
#define SUPPORTS_POWERCAP_MONITORING(x)			((x) & BIT(26))
#define SUPPORTS_POWERCAP_PAI_CONFIGURATION(x)		((x) & BIT(25))
#define SUPPORTS_POWERCAP_FASTCHANNELS(x)		((x) & BIT(22))
#define POWERCAP_POWER_UNIT(x)				\
		(FIELD_GET(GENMASK(24, 23), (x)))
#define	SUPPORTS_POWER_UNITS_MW(x)			\
		(POWERCAP_POWER_UNIT(x) == 0x2)
#define	SUPPORTS_POWER_UNITS_UW(x)			\
		(POWERCAP_POWER_UNIT(x) == 0x1)
	u8 name[SCMI_SHORT_NAME_MAX_SIZE];
	__le32 min_pai;
	__le32 max_pai;
	__le32 pai_step;
	__le32 min_power_cap;
	__le32 max_power_cap;
	__le32 power_cap_step;
	__le32 sustainable_power;
	__le32 accuracy;
	__le32 parent_id;
};

struct scmi_msg_powercap_set_cap_or_pai {
	__le32 domain;
	__le32 flags;
#define CAP_SET_ASYNC		BIT(1)
#define CAP_SET_IGNORE_DRESP	BIT(0)
	__le32 value;
};

struct scmi_msg_resp_powercap_cap_set_complete {
	__le32 domain;
	__le32 power_cap;
};

struct scmi_msg_resp_powercap_meas_get {
	__le32 power;
	__le32 pai;
};

struct scmi_msg_powercap_notify_cap {
	__le32 domain;
	__le32 notify_enable;
};

struct scmi_msg_powercap_notify_thresh {
	__le32 domain;
	__le32 notify_enable;
	__le32 power_thresh_low;
	__le32 power_thresh_high;
};

struct scmi_powercap_cap_changed_notify_payld {
	__le32 agent_id;
	__le32 domain_id;
	__le32 power_cap;
	__le32 pai;
};

struct scmi_powercap_meas_changed_notify_payld {
	__le32 agent_id;
	__le32 domain_id;
	__le32 power;
};

struct scmi_powercap_state {
	bool enabled;
	u32 last_pcap;
	bool meas_notif_enabled;
	u64 thresholds;
#define THRESH_LOW(p, id)				\
	(lower_32_bits((p)->states[(id)].thresholds))
#define THRESH_HIGH(p, id)				\
	(upper_32_bits((p)->states[(id)].thresholds))
};

struct powercap_info {
	u32 version;
	int num_domains;
	struct scmi_powercap_state *states;
	struct scmi_powercap_info *powercaps;
};

static enum scmi_powercap_protocol_cmd evt_2_cmd[] = {
	POWERCAP_CAP_NOTIFY,
	POWERCAP_MEASUREMENTS_NOTIFY,
};

static int scmi_powercap_notify(const struct scmi_protocol_handle *ph,
				u32 domain, int message_id, bool enable);

static int
scmi_powercap_attributes_get(const struct scmi_protocol_handle *ph,
			     struct powercap_info *pi)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES, 0,
				      sizeof(u32), &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		u32 attributes;

		attributes = get_unaligned_le32(t->rx.buf);
		pi->num_domains = FIELD_GET(GENMASK(15, 0), attributes);
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static inline int
scmi_powercap_validate(unsigned int min_val, unsigned int max_val,
		       unsigned int step_val, bool configurable)
{
	if (!min_val || !max_val)
		return -EPROTO;

	if ((configurable && min_val == max_val) ||
	    (!configurable && min_val != max_val))
		return -EPROTO;

	if (min_val != max_val && !step_val)
		return -EPROTO;

	return 0;
}

static int
scmi_powercap_domain_attributes_get(const struct scmi_protocol_handle *ph,
				    struct powercap_info *pinfo, u32 domain)
{
	int ret;
	u32 flags;
	struct scmi_xfer *t;
	struct scmi_powercap_info *dom_info = pinfo->powercaps + domain;
	struct scmi_msg_resp_powercap_domain_attributes *resp;

	ret = ph->xops->xfer_get_init(ph, POWERCAP_DOMAIN_ATTRIBUTES,
				      sizeof(domain), sizeof(*resp), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);
	resp = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		flags = le32_to_cpu(resp->attributes);

		dom_info->id = domain;
		dom_info->notify_powercap_cap_change =
			SUPPORTS_POWERCAP_CAP_CHANGE_NOTIFY(flags);
		dom_info->notify_powercap_measurement_change =
			SUPPORTS_POWERCAP_MEASUREMENTS_CHANGE_NOTIFY(flags);
		dom_info->async_powercap_cap_set =
			SUPPORTS_ASYNC_POWERCAP_CAP_SET(flags);
		dom_info->powercap_cap_config =
			SUPPORTS_POWERCAP_CAP_CONFIGURATION(flags);
		dom_info->powercap_monitoring =
			SUPPORTS_POWERCAP_MONITORING(flags);
		dom_info->powercap_pai_config =
			SUPPORTS_POWERCAP_PAI_CONFIGURATION(flags);
		dom_info->powercap_scale_mw =
			SUPPORTS_POWER_UNITS_MW(flags);
		dom_info->powercap_scale_uw =
			SUPPORTS_POWER_UNITS_UW(flags);
		dom_info->fastchannels =
			SUPPORTS_POWERCAP_FASTCHANNELS(flags);

		strscpy(dom_info->name, resp->name, SCMI_SHORT_NAME_MAX_SIZE);

		dom_info->min_pai = le32_to_cpu(resp->min_pai);
		dom_info->max_pai = le32_to_cpu(resp->max_pai);
		dom_info->pai_step = le32_to_cpu(resp->pai_step);
		ret = scmi_powercap_validate(dom_info->min_pai,
					     dom_info->max_pai,
					     dom_info->pai_step,
					     dom_info->powercap_pai_config);
		if (ret) {
			dev_err(ph->dev,
				"Platform reported inconsistent PAI config for domain %d - %s\n",
				dom_info->id, dom_info->name);
			goto clean;
		}

		dom_info->min_power_cap = le32_to_cpu(resp->min_power_cap);
		dom_info->max_power_cap = le32_to_cpu(resp->max_power_cap);
		dom_info->power_cap_step = le32_to_cpu(resp->power_cap_step);
		ret = scmi_powercap_validate(dom_info->min_power_cap,
					     dom_info->max_power_cap,
					     dom_info->power_cap_step,
					     dom_info->powercap_cap_config);
		if (ret) {
			dev_err(ph->dev,
				"Platform reported inconsistent CAP config for domain %d - %s\n",
				dom_info->id, dom_info->name);
			goto clean;
		}

		dom_info->sustainable_power =
			le32_to_cpu(resp->sustainable_power);
		dom_info->accuracy = le32_to_cpu(resp->accuracy);

		dom_info->parent_id = le32_to_cpu(resp->parent_id);
		if (dom_info->parent_id != SCMI_POWERCAP_ROOT_ZONE_ID &&
		    (dom_info->parent_id >= pinfo->num_domains ||
		     dom_info->parent_id == dom_info->id)) {
			dev_err(ph->dev,
				"Platform reported inconsistent parent ID for domain %d - %s\n",
				dom_info->id, dom_info->name);
			ret = -ENODEV;
		}
	}

clean:
	ph->xops->xfer_put(ph, t);

	/*
	 * If supported overwrite short name with the extended one;
	 * on error just carry on and use already provided short name.
	 */
	if (!ret && SUPPORTS_EXTENDED_NAMES(flags))
		ph->hops->extended_name_get(ph, POWERCAP_DOMAIN_NAME_GET,
					    domain, dom_info->name,
					    SCMI_MAX_STR_SIZE);

	return ret;
}

static int scmi_powercap_num_domains_get(const struct scmi_protocol_handle *ph)
{
	struct powercap_info *pi = ph->get_priv(ph);

	return pi->num_domains;
}

static const struct scmi_powercap_info *
scmi_powercap_dom_info_get(const struct scmi_protocol_handle *ph, u32 domain_id)
{
	struct powercap_info *pi = ph->get_priv(ph);

	if (domain_id >= pi->num_domains)
		return NULL;

	return pi->powercaps + domain_id;
}

static int scmi_powercap_xfer_cap_get(const struct scmi_protocol_handle *ph,
				      u32 domain_id, u32 *power_cap)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, POWERCAP_CAP_GET, sizeof(u32),
				      sizeof(u32), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain_id, t->tx.buf);
	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*power_cap = get_unaligned_le32(t->rx.buf);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int __scmi_powercap_cap_get(const struct scmi_protocol_handle *ph,
				   const struct scmi_powercap_info *dom,
				   u32 *power_cap)
{
	if (dom->fc_info && dom->fc_info[POWERCAP_FC_CAP].get_addr) {
		*power_cap = ioread32(dom->fc_info[POWERCAP_FC_CAP].get_addr);
		trace_scmi_fc_call(SCMI_PROTOCOL_POWERCAP, POWERCAP_CAP_GET,
				   dom->id, *power_cap, 0);
		return 0;
	}

	return scmi_powercap_xfer_cap_get(ph, dom->id, power_cap);
}

static int scmi_powercap_cap_get(const struct scmi_protocol_handle *ph,
				 u32 domain_id, u32 *power_cap)
{
	const struct scmi_powercap_info *dom;

	if (!power_cap)
		return -EINVAL;

	dom = scmi_powercap_dom_info_get(ph, domain_id);
	if (!dom)
		return -EINVAL;

	return __scmi_powercap_cap_get(ph, dom, power_cap);
}

static int scmi_powercap_xfer_cap_set(const struct scmi_protocol_handle *ph,
				      const struct scmi_powercap_info *pc,
				      u32 power_cap, bool ignore_dresp)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_powercap_set_cap_or_pai *msg;

	ret = ph->xops->xfer_get_init(ph, POWERCAP_CAP_SET,
				      sizeof(*msg), 0, &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->domain = cpu_to_le32(pc->id);
	msg->flags =
		cpu_to_le32(FIELD_PREP(CAP_SET_ASYNC, pc->async_powercap_cap_set) |
			    FIELD_PREP(CAP_SET_IGNORE_DRESP, ignore_dresp));
	msg->value = cpu_to_le32(power_cap);

	if (!pc->async_powercap_cap_set || ignore_dresp) {
		ret = ph->xops->do_xfer(ph, t);
	} else {
		ret = ph->xops->do_xfer_with_response(ph, t);
		if (!ret) {
			struct scmi_msg_resp_powercap_cap_set_complete *resp;

			resp = t->rx.buf;
			if (le32_to_cpu(resp->domain) == pc->id)
				dev_dbg(ph->dev,
					"Powercap ID %d CAP set async to %u\n",
					pc->id,
					get_unaligned_le32(&resp->power_cap));
			else
				ret = -EPROTO;
		}
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int __scmi_powercap_cap_set(const struct scmi_protocol_handle *ph,
				   struct powercap_info *pi, u32 domain_id,
				   u32 power_cap, bool ignore_dresp)
{
	int ret = -EINVAL;
	const struct scmi_powercap_info *pc;

	pc = scmi_powercap_dom_info_get(ph, domain_id);
	if (!pc || !pc->powercap_cap_config)
		return ret;

	if (power_cap &&
	    (power_cap < pc->min_power_cap || power_cap > pc->max_power_cap))
		return ret;

	if (pc->fc_info && pc->fc_info[POWERCAP_FC_CAP].set_addr) {
		struct scmi_fc_info *fci = &pc->fc_info[POWERCAP_FC_CAP];

		iowrite32(power_cap, fci->set_addr);
		ph->hops->fastchannel_db_ring(fci->set_db);
		trace_scmi_fc_call(SCMI_PROTOCOL_POWERCAP, POWERCAP_CAP_SET,
				   domain_id, power_cap, 0);
		ret = 0;
	} else {
		ret = scmi_powercap_xfer_cap_set(ph, pc, power_cap,
						 ignore_dresp);
	}

	/* Save the last explicitly set non-zero powercap value */
	if (PROTOCOL_REV_MAJOR(pi->version) >= 0x2 && !ret && power_cap)
		pi->states[domain_id].last_pcap = power_cap;

	return ret;
}

static int scmi_powercap_cap_set(const struct scmi_protocol_handle *ph,
				 u32 domain_id, u32 power_cap,
				 bool ignore_dresp)
{
	struct powercap_info *pi = ph->get_priv(ph);

	/*
	 * Disallow zero as a possible explicitly requested powercap:
	 * there are enable/disable operations for this.
	 */
	if (!power_cap)
		return -EINVAL;

	/* Just log the last set request if acting on a disabled domain */
	if (PROTOCOL_REV_MAJOR(pi->version) >= 0x2 &&
	    !pi->states[domain_id].enabled) {
		pi->states[domain_id].last_pcap = power_cap;
		return 0;
	}

	return __scmi_powercap_cap_set(ph, pi, domain_id,
				       power_cap, ignore_dresp);
}

static int scmi_powercap_xfer_pai_get(const struct scmi_protocol_handle *ph,
				      u32 domain_id, u32 *pai)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, POWERCAP_PAI_GET, sizeof(u32),
				      sizeof(u32), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain_id, t->tx.buf);
	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*pai = get_unaligned_le32(t->rx.buf);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_powercap_pai_get(const struct scmi_protocol_handle *ph,
				 u32 domain_id, u32 *pai)
{
	struct scmi_powercap_info *dom;
	struct powercap_info *pi = ph->get_priv(ph);

	if (!pai || domain_id >= pi->num_domains)
		return -EINVAL;

	dom = pi->powercaps + domain_id;
	if (dom->fc_info && dom->fc_info[POWERCAP_FC_PAI].get_addr) {
		*pai = ioread32(dom->fc_info[POWERCAP_FC_PAI].get_addr);
		trace_scmi_fc_call(SCMI_PROTOCOL_POWERCAP, POWERCAP_PAI_GET,
				   domain_id, *pai, 0);
		return 0;
	}

	return scmi_powercap_xfer_pai_get(ph, domain_id, pai);
}

static int scmi_powercap_xfer_pai_set(const struct scmi_protocol_handle *ph,
				      u32 domain_id, u32 pai)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_powercap_set_cap_or_pai *msg;

	ret = ph->xops->xfer_get_init(ph, POWERCAP_PAI_SET,
				      sizeof(*msg), 0, &t);
	if (ret)
		return ret;

	msg = t->tx.buf;
	msg->domain = cpu_to_le32(domain_id);
	msg->flags = cpu_to_le32(0);
	msg->value = cpu_to_le32(pai);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_powercap_pai_set(const struct scmi_protocol_handle *ph,
				 u32 domain_id, u32 pai)
{
	const struct scmi_powercap_info *pc;

	pc = scmi_powercap_dom_info_get(ph, domain_id);
	if (!pc || !pc->powercap_pai_config || !pai ||
	    pai < pc->min_pai || pai > pc->max_pai)
		return -EINVAL;

	if (pc->fc_info && pc->fc_info[POWERCAP_FC_PAI].set_addr) {
		struct scmi_fc_info *fci = &pc->fc_info[POWERCAP_FC_PAI];

		trace_scmi_fc_call(SCMI_PROTOCOL_POWERCAP, POWERCAP_PAI_SET,
				   domain_id, pai, 0);
		iowrite32(pai, fci->set_addr);
		ph->hops->fastchannel_db_ring(fci->set_db);
		return 0;
	}

	return scmi_powercap_xfer_pai_set(ph, domain_id, pai);
}

static int scmi_powercap_measurements_get(const struct scmi_protocol_handle *ph,
					  u32 domain_id, u32 *average_power,
					  u32 *pai)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_powercap_meas_get *resp;
	const struct scmi_powercap_info *pc;

	pc = scmi_powercap_dom_info_get(ph, domain_id);
	if (!pc || !pc->powercap_monitoring || !pai || !average_power)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, POWERCAP_MEASUREMENTS_GET,
				      sizeof(u32), sizeof(*resp), &t);
	if (ret)
		return ret;

	resp = t->rx.buf;
	put_unaligned_le32(domain_id, t->tx.buf);
	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		*average_power = le32_to_cpu(resp->power);
		*pai = le32_to_cpu(resp->pai);
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_powercap_measurements_threshold_get(const struct scmi_protocol_handle *ph,
					 u32 domain_id, u32 *power_thresh_low,
					 u32 *power_thresh_high)
{
	struct powercap_info *pi = ph->get_priv(ph);

	if (!power_thresh_low || !power_thresh_high ||
	    domain_id >= pi->num_domains)
		return -EINVAL;

	*power_thresh_low =  THRESH_LOW(pi, domain_id);
	*power_thresh_high = THRESH_HIGH(pi, domain_id);

	return 0;
}

static int
scmi_powercap_measurements_threshold_set(const struct scmi_protocol_handle *ph,
					 u32 domain_id, u32 power_thresh_low,
					 u32 power_thresh_high)
{
	int ret = 0;
	struct powercap_info *pi = ph->get_priv(ph);

	if (domain_id >= pi->num_domains ||
	    power_thresh_low > power_thresh_high)
		return -EINVAL;

	/* Anything to do ? */
	if (THRESH_LOW(pi, domain_id) == power_thresh_low &&
	    THRESH_HIGH(pi, domain_id) == power_thresh_high)
		return ret;

	pi->states[domain_id].thresholds =
		(FIELD_PREP(GENMASK_ULL(31, 0), power_thresh_low) |
		 FIELD_PREP(GENMASK_ULL(63, 32), power_thresh_high));

	/* Update thresholds if notification already enabled */
	if (pi->states[domain_id].meas_notif_enabled)
		ret = scmi_powercap_notify(ph, domain_id,
					   POWERCAP_MEASUREMENTS_NOTIFY,
					   true);

	return ret;
}

static int scmi_powercap_cap_enable_set(const struct scmi_protocol_handle *ph,
					u32 domain_id, bool enable)
{
	int ret;
	u32 power_cap;
	struct powercap_info *pi = ph->get_priv(ph);

	if (PROTOCOL_REV_MAJOR(pi->version) < 0x2)
		return -EINVAL;

	if (enable == pi->states[domain_id].enabled)
		return 0;

	if (enable) {
		/* Cannot enable with a zero powercap. */
		if (!pi->states[domain_id].last_pcap)
			return -EINVAL;

		ret = __scmi_powercap_cap_set(ph, pi, domain_id,
					      pi->states[domain_id].last_pcap,
					      true);
	} else {
		ret = __scmi_powercap_cap_set(ph, pi, domain_id, 0, true);
	}

	if (ret)
		return ret;

	/*
	 * Update our internal state to reflect final platform state: the SCMI
	 * server could have ignored a disable request and kept enforcing some
	 * powercap limit requested by other agents.
	 */
	ret = scmi_powercap_cap_get(ph, domain_id, &power_cap);
	if (!ret)
		pi->states[domain_id].enabled = !!power_cap;

	return ret;
}

static int scmi_powercap_cap_enable_get(const struct scmi_protocol_handle *ph,
					u32 domain_id, bool *enable)
{
	int ret;
	u32 power_cap;
	struct powercap_info *pi = ph->get_priv(ph);

	*enable = true;
	if (PROTOCOL_REV_MAJOR(pi->version) < 0x2)
		return 0;

	/*
	 * Report always real platform state; platform could have ignored
	 * a previous disable request. Default true on any error.
	 */
	ret = scmi_powercap_cap_get(ph, domain_id, &power_cap);
	if (!ret)
		*enable = !!power_cap;

	/* Update internal state with current real platform state */
	pi->states[domain_id].enabled = *enable;

	return 0;
}

static const struct scmi_powercap_proto_ops powercap_proto_ops = {
	.num_domains_get = scmi_powercap_num_domains_get,
	.info_get = scmi_powercap_dom_info_get,
	.cap_get = scmi_powercap_cap_get,
	.cap_set = scmi_powercap_cap_set,
	.cap_enable_set = scmi_powercap_cap_enable_set,
	.cap_enable_get = scmi_powercap_cap_enable_get,
	.pai_get = scmi_powercap_pai_get,
	.pai_set = scmi_powercap_pai_set,
	.measurements_get = scmi_powercap_measurements_get,
	.measurements_threshold_set = scmi_powercap_measurements_threshold_set,
	.measurements_threshold_get = scmi_powercap_measurements_threshold_get,
};

static void scmi_powercap_domain_init_fc(const struct scmi_protocol_handle *ph,
					 u32 domain, struct scmi_fc_info **p_fc)
{
	struct scmi_fc_info *fc;

	fc = devm_kcalloc(ph->dev, POWERCAP_FC_MAX, sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return;

	ph->hops->fastchannel_init(ph, POWERCAP_DESCRIBE_FASTCHANNEL,
				   POWERCAP_CAP_SET, 4, domain,
				   &fc[POWERCAP_FC_CAP].set_addr,
				   &fc[POWERCAP_FC_CAP].set_db);

	ph->hops->fastchannel_init(ph, POWERCAP_DESCRIBE_FASTCHANNEL,
				   POWERCAP_CAP_GET, 4, domain,
				   &fc[POWERCAP_FC_CAP].get_addr, NULL);

	ph->hops->fastchannel_init(ph, POWERCAP_DESCRIBE_FASTCHANNEL,
				   POWERCAP_PAI_SET, 4, domain,
				   &fc[POWERCAP_FC_PAI].set_addr,
				   &fc[POWERCAP_FC_PAI].set_db);

	ph->hops->fastchannel_init(ph, POWERCAP_DESCRIBE_FASTCHANNEL,
				   POWERCAP_PAI_GET, 4, domain,
				   &fc[POWERCAP_FC_PAI].get_addr, NULL);

	*p_fc = fc;
}

static int scmi_powercap_notify(const struct scmi_protocol_handle *ph,
				u32 domain, int message_id, bool enable)
{
	int ret;
	struct scmi_xfer *t;

	switch (message_id) {
	case POWERCAP_CAP_NOTIFY:
	{
		struct scmi_msg_powercap_notify_cap *notify;

		ret = ph->xops->xfer_get_init(ph, message_id,
					      sizeof(*notify), 0, &t);
		if (ret)
			return ret;

		notify = t->tx.buf;
		notify->domain = cpu_to_le32(domain);
		notify->notify_enable = cpu_to_le32(enable ? BIT(0) : 0);
		break;
	}
	case POWERCAP_MEASUREMENTS_NOTIFY:
	{
		u32 low, high;
		struct scmi_msg_powercap_notify_thresh *notify;

		/*
		 * Note that we have to pick the most recently configured
		 * thresholds to build a proper POWERCAP_MEASUREMENTS_NOTIFY
		 * enable request and we fail, complaining, if no thresholds
		 * were ever set, since this is an indication the API has been
		 * used wrongly.
		 */
		ret = scmi_powercap_measurements_threshold_get(ph, domain,
							       &low, &high);
		if (ret)
			return ret;

		if (enable && !low && !high) {
			dev_err(ph->dev,
				"Invalid Measurements Notify thresholds: %u/%u\n",
				low, high);
			return -EINVAL;
		}

		ret = ph->xops->xfer_get_init(ph, message_id,
					      sizeof(*notify), 0, &t);
		if (ret)
			return ret;

		notify = t->tx.buf;
		notify->domain = cpu_to_le32(domain);
		notify->notify_enable = cpu_to_le32(enable ? BIT(0) : 0);
		notify->power_thresh_low = cpu_to_le32(low);
		notify->power_thresh_high = cpu_to_le32(high);
		break;
	}
	default:
		return -EINVAL;
	}

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_powercap_set_notify_enabled(const struct scmi_protocol_handle *ph,
				 u8 evt_id, u32 src_id, bool enable)
{
	int ret, cmd_id;
	struct powercap_info *pi = ph->get_priv(ph);

	if (evt_id >= ARRAY_SIZE(evt_2_cmd) || src_id >= pi->num_domains)
		return -EINVAL;

	cmd_id = evt_2_cmd[evt_id];
	ret = scmi_powercap_notify(ph, src_id, cmd_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);
	else if (cmd_id == POWERCAP_MEASUREMENTS_NOTIFY)
		/*
		 * On success save the current notification enabled state, so
		 * as to be able to properly update the notification thresholds
		 * when they are modified on a domain for which measurement
		 * notifications were currently enabled.
		 *
		 * This is needed because the SCMI Notification core machinery
		 * and API does not support passing per-notification custom
		 * arguments at callback registration time.
		 *
		 * Note that this can be done here with a simple flag since the
		 * SCMI core Notifications code takes care of keeping proper
		 * per-domain enables refcounting, so that this helper function
		 * will be called only once (for enables) when the first user
		 * registers a callback on this domain and once more (disable)
		 * when the last user de-registers its callback.
		 */
		pi->states[src_id].meas_notif_enabled = enable;

	return ret;
}

static void *
scmi_powercap_fill_custom_report(const struct scmi_protocol_handle *ph,
				 u8 evt_id, ktime_t timestamp,
				 const void *payld, size_t payld_sz,
				 void *report, u32 *src_id)
{
	void *rep = NULL;

	switch (evt_id) {
	case SCMI_EVENT_POWERCAP_CAP_CHANGED:
	{
		const struct scmi_powercap_cap_changed_notify_payld *p = payld;
		struct scmi_powercap_cap_changed_report *r = report;

		if (sizeof(*p) != payld_sz)
			break;

		r->timestamp = timestamp;
		r->agent_id = le32_to_cpu(p->agent_id);
		r->domain_id = le32_to_cpu(p->domain_id);
		r->power_cap = le32_to_cpu(p->power_cap);
		r->pai = le32_to_cpu(p->pai);
		*src_id = r->domain_id;
		rep = r;
		break;
	}
	case SCMI_EVENT_POWERCAP_MEASUREMENTS_CHANGED:
	{
		const struct scmi_powercap_meas_changed_notify_payld *p = payld;
		struct scmi_powercap_meas_changed_report *r = report;

		if (sizeof(*p) != payld_sz)
			break;

		r->timestamp = timestamp;
		r->agent_id = le32_to_cpu(p->agent_id);
		r->domain_id = le32_to_cpu(p->domain_id);
		r->power = le32_to_cpu(p->power);
		*src_id = r->domain_id;
		rep = r;
		break;
	}
	default:
		break;
	}

	return rep;
}

static int
scmi_powercap_get_num_sources(const struct scmi_protocol_handle *ph)
{
	struct powercap_info *pi = ph->get_priv(ph);

	if (!pi)
		return -EINVAL;

	return pi->num_domains;
}

static const struct scmi_event powercap_events[] = {
	{
		.id = SCMI_EVENT_POWERCAP_CAP_CHANGED,
		.max_payld_sz =
			sizeof(struct scmi_powercap_cap_changed_notify_payld),
		.max_report_sz =
			sizeof(struct scmi_powercap_cap_changed_report),
	},
	{
		.id = SCMI_EVENT_POWERCAP_MEASUREMENTS_CHANGED,
		.max_payld_sz =
			sizeof(struct scmi_powercap_meas_changed_notify_payld),
		.max_report_sz =
			sizeof(struct scmi_powercap_meas_changed_report),
	},
};

static const struct scmi_event_ops powercap_event_ops = {
	.get_num_sources = scmi_powercap_get_num_sources,
	.set_notify_enabled = scmi_powercap_set_notify_enabled,
	.fill_custom_report = scmi_powercap_fill_custom_report,
};

static const struct scmi_protocol_events powercap_protocol_events = {
	.queue_sz = SCMI_PROTO_QUEUE_SZ,
	.ops = &powercap_event_ops,
	.evts = powercap_events,
	.num_events = ARRAY_SIZE(powercap_events),
};

static int
scmi_powercap_protocol_init(const struct scmi_protocol_handle *ph)
{
	int domain, ret;
	u32 version;
	struct powercap_info *pinfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_dbg(ph->dev, "Powercap Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(ph->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	ret = scmi_powercap_attributes_get(ph, pinfo);
	if (ret)
		return ret;

	pinfo->powercaps = devm_kcalloc(ph->dev, pinfo->num_domains,
					sizeof(*pinfo->powercaps),
					GFP_KERNEL);
	if (!pinfo->powercaps)
		return -ENOMEM;

	pinfo->states = devm_kcalloc(ph->dev, pinfo->num_domains,
				     sizeof(*pinfo->states), GFP_KERNEL);
	if (!pinfo->states)
		return -ENOMEM;

	/*
	 * Note that any failure in retrieving any domain attribute leads to
	 * the whole Powercap protocol initialization failure: this way the
	 * reported Powercap domains are all assured, when accessed, to be well
	 * formed and correlated by sane parent-child relationship (if any).
	 */
	for (domain = 0; domain < pinfo->num_domains; domain++) {
		ret = scmi_powercap_domain_attributes_get(ph, pinfo, domain);
		if (ret)
			return ret;

		if (pinfo->powercaps[domain].fastchannels)
			scmi_powercap_domain_init_fc(ph, domain,
						     &pinfo->powercaps[domain].fc_info);

		/* Grab initial state when disable is supported. */
		if (PROTOCOL_REV_MAJOR(version) >= 0x2) {
			ret = __scmi_powercap_cap_get(ph,
						      &pinfo->powercaps[domain],
						      &pinfo->states[domain].last_pcap);
			if (ret)
				return ret;

			pinfo->states[domain].enabled =
				!!pinfo->states[domain].last_pcap;
		}
	}

	pinfo->version = version;
	return ph->set_priv(ph, pinfo);
}

static const struct scmi_protocol scmi_powercap = {
	.id = SCMI_PROTOCOL_POWERCAP,
	.owner = THIS_MODULE,
	.instance_init = &scmi_powercap_protocol_init,
	.ops = &powercap_proto_ops,
	.events = &powercap_protocol_events,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(powercap, scmi_powercap)
