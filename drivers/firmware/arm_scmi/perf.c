// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Performance Protocol
 *
 * Copyright (C) 2018-2022 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications PERF - " fmt

#include <linux/bits.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/scmi_protocol.h>
#include <linux/sort.h>

#include <trace/events/scmi.h>

#include "protocols.h"
#include "notify.h"

#define MAX_OPPS		16

enum scmi_performance_protocol_cmd {
	PERF_DOMAIN_ATTRIBUTES = 0x3,
	PERF_DESCRIBE_LEVELS = 0x4,
	PERF_LIMITS_SET = 0x5,
	PERF_LIMITS_GET = 0x6,
	PERF_LEVEL_SET = 0x7,
	PERF_LEVEL_GET = 0x8,
	PERF_NOTIFY_LIMITS = 0x9,
	PERF_NOTIFY_LEVEL = 0xa,
	PERF_DESCRIBE_FASTCHANNEL = 0xb,
	PERF_DOMAIN_NAME_GET = 0xc,
};

enum {
	PERF_FC_LEVEL,
	PERF_FC_LIMIT,
	PERF_FC_MAX,
};

struct scmi_opp {
	u32 perf;
	u32 power;
	u32 trans_latency_us;
};

struct scmi_msg_resp_perf_attributes {
	__le16 num_domains;
	__le16 flags;
#define POWER_SCALE_IN_MILLIWATT(x)	((x) & BIT(0))
#define POWER_SCALE_IN_MICROWATT(x)	((x) & BIT(1))
	__le32 stats_addr_low;
	__le32 stats_addr_high;
	__le32 stats_size;
};

struct scmi_msg_resp_perf_domain_attributes {
	__le32 flags;
#define SUPPORTS_SET_LIMITS(x)		((x) & BIT(31))
#define SUPPORTS_SET_PERF_LVL(x)	((x) & BIT(30))
#define SUPPORTS_PERF_LIMIT_NOTIFY(x)	((x) & BIT(29))
#define SUPPORTS_PERF_LEVEL_NOTIFY(x)	((x) & BIT(28))
#define SUPPORTS_PERF_FASTCHANNELS(x)	((x) & BIT(27))
#define SUPPORTS_EXTENDED_NAMES(x)	((x) & BIT(26))
	__le32 rate_limit_us;
	__le32 sustained_freq_khz;
	__le32 sustained_perf_level;
	    u8 name[SCMI_SHORT_NAME_MAX_SIZE];
};

struct scmi_msg_perf_describe_levels {
	__le32 domain;
	__le32 level_index;
};

struct scmi_perf_set_limits {
	__le32 domain;
	__le32 max_level;
	__le32 min_level;
};

struct scmi_perf_get_limits {
	__le32 max_level;
	__le32 min_level;
};

struct scmi_perf_set_level {
	__le32 domain;
	__le32 level;
};

struct scmi_perf_notify_level_or_limits {
	__le32 domain;
	__le32 notify_enable;
};

struct scmi_perf_limits_notify_payld {
	__le32 agent_id;
	__le32 domain_id;
	__le32 range_max;
	__le32 range_min;
};

struct scmi_perf_level_notify_payld {
	__le32 agent_id;
	__le32 domain_id;
	__le32 performance_level;
};

struct scmi_msg_resp_perf_describe_levels {
	__le16 num_returned;
	__le16 num_remaining;
	struct {
		__le32 perf_val;
		__le32 power;
		__le16 transition_latency_us;
		__le16 reserved;
	} opp[];
};

struct perf_dom_info {
	bool set_limits;
	bool set_perf;
	bool perf_limit_notify;
	bool perf_level_notify;
	bool perf_fastchannels;
	u32 opp_count;
	u32 sustained_freq_khz;
	u32 sustained_perf_level;
	unsigned long mult_factor;
	char name[SCMI_MAX_STR_SIZE];
	struct scmi_opp opp[MAX_OPPS];
	struct scmi_fc_info *fc_info;
};

struct scmi_perf_info {
	u32 version;
	u16 num_domains;
	enum scmi_power_scale power_scale;
	u64 stats_addr;
	u32 stats_size;
	struct perf_dom_info *dom_info;
};

static enum scmi_performance_protocol_cmd evt_2_cmd[] = {
	PERF_NOTIFY_LIMITS,
	PERF_NOTIFY_LEVEL,
};

static int scmi_perf_attributes_get(const struct scmi_protocol_handle *ph,
				    struct scmi_perf_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_perf_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES, 0,
				      sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		u16 flags = le16_to_cpu(attr->flags);

		pi->num_domains = le16_to_cpu(attr->num_domains);

		if (POWER_SCALE_IN_MILLIWATT(flags))
			pi->power_scale = SCMI_POWER_MILLIWATTS;
		if (PROTOCOL_REV_MAJOR(pi->version) >= 0x3)
			if (POWER_SCALE_IN_MICROWATT(flags))
				pi->power_scale = SCMI_POWER_MICROWATTS;

		pi->stats_addr = le32_to_cpu(attr->stats_addr_low) |
				(u64)le32_to_cpu(attr->stats_addr_high) << 32;
		pi->stats_size = le32_to_cpu(attr->stats_size);
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_perf_domain_attributes_get(const struct scmi_protocol_handle *ph,
				u32 domain, struct perf_dom_info *dom_info,
				u32 version)
{
	int ret;
	u32 flags;
	struct scmi_xfer *t;
	struct scmi_msg_resp_perf_domain_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, PERF_DOMAIN_ATTRIBUTES,
				     sizeof(domain), sizeof(*attr), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);
	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		flags = le32_to_cpu(attr->flags);

		dom_info->set_limits = SUPPORTS_SET_LIMITS(flags);
		dom_info->set_perf = SUPPORTS_SET_PERF_LVL(flags);
		dom_info->perf_limit_notify = SUPPORTS_PERF_LIMIT_NOTIFY(flags);
		dom_info->perf_level_notify = SUPPORTS_PERF_LEVEL_NOTIFY(flags);
		dom_info->perf_fastchannels = SUPPORTS_PERF_FASTCHANNELS(flags);
		dom_info->sustained_freq_khz =
					le32_to_cpu(attr->sustained_freq_khz);
		dom_info->sustained_perf_level =
					le32_to_cpu(attr->sustained_perf_level);
		if (!dom_info->sustained_freq_khz ||
		    !dom_info->sustained_perf_level)
			/* CPUFreq converts to kHz, hence default 1000 */
			dom_info->mult_factor =	1000;
		else
			dom_info->mult_factor =
					(dom_info->sustained_freq_khz * 1000UL)
					/ dom_info->sustained_perf_level;
		strscpy(dom_info->name, attr->name, SCMI_SHORT_NAME_MAX_SIZE);
	}

	ph->xops->xfer_put(ph, t);

	/*
	 * If supported overwrite short name with the extended one;
	 * on error just carry on and use already provided short name.
	 */
	if (!ret && PROTOCOL_REV_MAJOR(version) >= 0x3 &&
	    SUPPORTS_EXTENDED_NAMES(flags))
		ph->hops->extended_name_get(ph, PERF_DOMAIN_NAME_GET, domain,
					    dom_info->name, SCMI_MAX_STR_SIZE);

	return ret;
}

static int opp_cmp_func(const void *opp1, const void *opp2)
{
	const struct scmi_opp *t1 = opp1, *t2 = opp2;

	return t1->perf - t2->perf;
}

struct scmi_perf_ipriv {
	u32 domain;
	struct perf_dom_info *perf_dom;
};

static void iter_perf_levels_prepare_message(void *message,
					     unsigned int desc_index,
					     const void *priv)
{
	struct scmi_msg_perf_describe_levels *msg = message;
	const struct scmi_perf_ipriv *p = priv;

	msg->domain = cpu_to_le32(p->domain);
	/* Set the number of OPPs to be skipped/already read */
	msg->level_index = cpu_to_le32(desc_index);
}

static int iter_perf_levels_update_state(struct scmi_iterator_state *st,
					 const void *response, void *priv)
{
	const struct scmi_msg_resp_perf_describe_levels *r = response;

	st->num_returned = le16_to_cpu(r->num_returned);
	st->num_remaining = le16_to_cpu(r->num_remaining);

	return 0;
}

static int
iter_perf_levels_process_response(const struct scmi_protocol_handle *ph,
				  const void *response,
				  struct scmi_iterator_state *st, void *priv)
{
	struct scmi_opp *opp;
	const struct scmi_msg_resp_perf_describe_levels *r = response;
	struct scmi_perf_ipriv *p = priv;

	opp = &p->perf_dom->opp[st->desc_index + st->loop_idx];
	opp->perf = le32_to_cpu(r->opp[st->loop_idx].perf_val);
	opp->power = le32_to_cpu(r->opp[st->loop_idx].power);
	opp->trans_latency_us =
		le16_to_cpu(r->opp[st->loop_idx].transition_latency_us);
	p->perf_dom->opp_count++;

	dev_dbg(ph->dev, "Level %d Power %d Latency %dus\n",
		opp->perf, opp->power, opp->trans_latency_us);

	return 0;
}

static int
scmi_perf_describe_levels_get(const struct scmi_protocol_handle *ph, u32 domain,
			      struct perf_dom_info *perf_dom)
{
	int ret;
	void *iter;
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_perf_levels_prepare_message,
		.update_state = iter_perf_levels_update_state,
		.process_response = iter_perf_levels_process_response,
	};
	struct scmi_perf_ipriv ppriv = {
		.domain = domain,
		.perf_dom = perf_dom,
	};

	iter = ph->hops->iter_response_init(ph, &ops, MAX_OPPS,
					    PERF_DESCRIBE_LEVELS,
					    sizeof(struct scmi_msg_perf_describe_levels),
					    &ppriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	ret = ph->hops->iter_response_run(iter);
	if (ret)
		return ret;

	if (perf_dom->opp_count)
		sort(perf_dom->opp, perf_dom->opp_count,
		     sizeof(struct scmi_opp), opp_cmp_func, NULL);

	return ret;
}

static int scmi_perf_mb_limits_set(const struct scmi_protocol_handle *ph,
				   u32 domain, u32 max_perf, u32 min_perf)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_set_limits *limits;

	ret = ph->xops->xfer_get_init(ph, PERF_LIMITS_SET,
				      sizeof(*limits), 0, &t);
	if (ret)
		return ret;

	limits = t->tx.buf;
	limits->domain = cpu_to_le32(domain);
	limits->max_level = cpu_to_le32(max_perf);
	limits->min_level = cpu_to_le32(min_perf);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static inline struct perf_dom_info *
scmi_perf_domain_lookup(const struct scmi_protocol_handle *ph, u32 domain)
{
	struct scmi_perf_info *pi = ph->get_priv(ph);

	if (domain >= pi->num_domains)
		return ERR_PTR(-EINVAL);

	return pi->dom_info + domain;
}

static int scmi_perf_limits_set(const struct scmi_protocol_handle *ph,
				u32 domain, u32 max_perf, u32 min_perf)
{
	struct scmi_perf_info *pi = ph->get_priv(ph);
	struct perf_dom_info *dom;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	if (PROTOCOL_REV_MAJOR(pi->version) >= 0x3 && !max_perf && !min_perf)
		return -EINVAL;

	if (dom->fc_info && dom->fc_info[PERF_FC_LIMIT].set_addr) {
		struct scmi_fc_info *fci = &dom->fc_info[PERF_FC_LIMIT];

		trace_scmi_fc_call(SCMI_PROTOCOL_PERF, PERF_LIMITS_SET,
				   domain, min_perf, max_perf);
		iowrite32(max_perf, fci->set_addr);
		iowrite32(min_perf, fci->set_addr + 4);
		ph->hops->fastchannel_db_ring(fci->set_db);
		return 0;
	}

	return scmi_perf_mb_limits_set(ph, domain, max_perf, min_perf);
}

static int scmi_perf_mb_limits_get(const struct scmi_protocol_handle *ph,
				   u32 domain, u32 *max_perf, u32 *min_perf)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_get_limits *limits;

	ret = ph->xops->xfer_get_init(ph, PERF_LIMITS_GET,
				      sizeof(__le32), 0, &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		limits = t->rx.buf;

		*max_perf = le32_to_cpu(limits->max_level);
		*min_perf = le32_to_cpu(limits->min_level);
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_perf_limits_get(const struct scmi_protocol_handle *ph,
				u32 domain, u32 *max_perf, u32 *min_perf)
{
	struct perf_dom_info *dom;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	if (dom->fc_info && dom->fc_info[PERF_FC_LIMIT].get_addr) {
		struct scmi_fc_info *fci = &dom->fc_info[PERF_FC_LIMIT];

		*max_perf = ioread32(fci->get_addr);
		*min_perf = ioread32(fci->get_addr + 4);
		trace_scmi_fc_call(SCMI_PROTOCOL_PERF, PERF_LIMITS_GET,
				   domain, *min_perf, *max_perf);
		return 0;
	}

	return scmi_perf_mb_limits_get(ph, domain, max_perf, min_perf);
}

static int scmi_perf_mb_level_set(const struct scmi_protocol_handle *ph,
				  u32 domain, u32 level, bool poll)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_set_level *lvl;

	ret = ph->xops->xfer_get_init(ph, PERF_LEVEL_SET, sizeof(*lvl), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = poll;
	lvl = t->tx.buf;
	lvl->domain = cpu_to_le32(domain);
	lvl->level = cpu_to_le32(level);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_perf_level_set(const struct scmi_protocol_handle *ph,
			       u32 domain, u32 level, bool poll)
{
	struct perf_dom_info *dom;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	if (dom->fc_info && dom->fc_info[PERF_FC_LEVEL].set_addr) {
		struct scmi_fc_info *fci = &dom->fc_info[PERF_FC_LEVEL];

		trace_scmi_fc_call(SCMI_PROTOCOL_PERF, PERF_LEVEL_SET,
				   domain, level, 0);
		iowrite32(level, fci->set_addr);
		ph->hops->fastchannel_db_ring(fci->set_db);
		return 0;
	}

	return scmi_perf_mb_level_set(ph, domain, level, poll);
}

static int scmi_perf_mb_level_get(const struct scmi_protocol_handle *ph,
				  u32 domain, u32 *level, bool poll)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, PERF_LEVEL_GET,
				     sizeof(u32), sizeof(u32), &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = poll;
	put_unaligned_le32(domain, t->tx.buf);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*level = get_unaligned_le32(t->rx.buf);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_perf_level_get(const struct scmi_protocol_handle *ph,
			       u32 domain, u32 *level, bool poll)
{
	struct perf_dom_info *dom;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	if (dom->fc_info && dom->fc_info[PERF_FC_LEVEL].get_addr) {
		*level = ioread32(dom->fc_info[PERF_FC_LEVEL].get_addr);
		trace_scmi_fc_call(SCMI_PROTOCOL_PERF, PERF_LEVEL_GET,
				   domain, *level, 0);
		return 0;
	}

	return scmi_perf_mb_level_get(ph, domain, level, poll);
}

static int scmi_perf_level_limits_notify(const struct scmi_protocol_handle *ph,
					 u32 domain, int message_id,
					 bool enable)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_notify_level_or_limits *notify;

	ret = ph->xops->xfer_get_init(ph, message_id, sizeof(*notify), 0, &t);
	if (ret)
		return ret;

	notify = t->tx.buf;
	notify->domain = cpu_to_le32(domain);
	notify->notify_enable = enable ? cpu_to_le32(BIT(0)) : 0;

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static void scmi_perf_domain_init_fc(const struct scmi_protocol_handle *ph,
				     u32 domain, struct scmi_fc_info **p_fc)
{
	struct scmi_fc_info *fc;

	fc = devm_kcalloc(ph->dev, PERF_FC_MAX, sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return;

	ph->hops->fastchannel_init(ph, PERF_DESCRIBE_FASTCHANNEL,
				   PERF_LEVEL_SET, 4, domain,
				   &fc[PERF_FC_LEVEL].set_addr,
				   &fc[PERF_FC_LEVEL].set_db);

	ph->hops->fastchannel_init(ph, PERF_DESCRIBE_FASTCHANNEL,
				   PERF_LEVEL_GET, 4, domain,
				   &fc[PERF_FC_LEVEL].get_addr, NULL);

	ph->hops->fastchannel_init(ph, PERF_DESCRIBE_FASTCHANNEL,
				   PERF_LIMITS_SET, 8, domain,
				   &fc[PERF_FC_LIMIT].set_addr,
				   &fc[PERF_FC_LIMIT].set_db);

	ph->hops->fastchannel_init(ph, PERF_DESCRIBE_FASTCHANNEL,
				   PERF_LIMITS_GET, 8, domain,
				   &fc[PERF_FC_LIMIT].get_addr, NULL);

	*p_fc = fc;
}

/* Device specific ops */
static int scmi_dev_domain_id(struct device *dev)
{
	struct of_phandle_args clkspec;

	if (of_parse_phandle_with_args(dev->of_node, "clocks", "#clock-cells",
				       0, &clkspec))
		return -EINVAL;

	return clkspec.args[0];
}

static int scmi_dvfs_device_opps_add(const struct scmi_protocol_handle *ph,
				     struct device *dev)
{
	int idx, ret, domain;
	unsigned long freq;
	struct scmi_opp *opp;
	struct perf_dom_info *dom;

	domain = scmi_dev_domain_id(dev);
	if (domain < 0)
		return -EINVAL;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	for (opp = dom->opp, idx = 0; idx < dom->opp_count; idx++, opp++) {
		freq = opp->perf * dom->mult_factor;

		ret = dev_pm_opp_add(dev, freq, 0);
		if (ret) {
			dev_warn(dev, "failed to add opp %luHz\n", freq);

			while (idx-- > 0) {
				freq = (--opp)->perf * dom->mult_factor;
				dev_pm_opp_remove(dev, freq);
			}
			return ret;
		}
	}
	return 0;
}

static int
scmi_dvfs_transition_latency_get(const struct scmi_protocol_handle *ph,
				 struct device *dev)
{
	int domain;
	struct perf_dom_info *dom;

	domain = scmi_dev_domain_id(dev);
	if (domain < 0)
		return -EINVAL;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	/* uS to nS */
	return dom->opp[dom->opp_count - 1].trans_latency_us * 1000;
}

static int scmi_dvfs_freq_set(const struct scmi_protocol_handle *ph, u32 domain,
			      unsigned long freq, bool poll)
{
	struct perf_dom_info *dom;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	return scmi_perf_level_set(ph, domain, freq / dom->mult_factor, poll);
}

static int scmi_dvfs_freq_get(const struct scmi_protocol_handle *ph, u32 domain,
			      unsigned long *freq, bool poll)
{
	int ret;
	u32 level;
	struct scmi_perf_info *pi = ph->get_priv(ph);

	ret = scmi_perf_level_get(ph, domain, &level, poll);
	if (!ret) {
		struct perf_dom_info *dom = pi->dom_info + domain;

		/* Note domain is validated implicitly by scmi_perf_level_get */
		*freq = level * dom->mult_factor;
	}

	return ret;
}

static int scmi_dvfs_est_power_get(const struct scmi_protocol_handle *ph,
				   u32 domain, unsigned long *freq,
				   unsigned long *power)
{
	struct perf_dom_info *dom;
	unsigned long opp_freq;
	int idx, ret = -EINVAL;
	struct scmi_opp *opp;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return PTR_ERR(dom);

	for (opp = dom->opp, idx = 0; idx < dom->opp_count; idx++, opp++) {
		opp_freq = opp->perf * dom->mult_factor;
		if (opp_freq < *freq)
			continue;

		*freq = opp_freq;
		*power = opp->power;
		ret = 0;
		break;
	}

	return ret;
}

static bool scmi_fast_switch_possible(const struct scmi_protocol_handle *ph,
				      struct device *dev)
{
	int domain;
	struct perf_dom_info *dom;

	domain = scmi_dev_domain_id(dev);
	if (domain < 0)
		return false;

	dom = scmi_perf_domain_lookup(ph, domain);
	if (IS_ERR(dom))
		return false;

	return dom->fc_info && dom->fc_info[PERF_FC_LEVEL].set_addr;
}

static enum scmi_power_scale
scmi_power_scale_get(const struct scmi_protocol_handle *ph)
{
	struct scmi_perf_info *pi = ph->get_priv(ph);

	return pi->power_scale;
}

static const struct scmi_perf_proto_ops perf_proto_ops = {
	.limits_set = scmi_perf_limits_set,
	.limits_get = scmi_perf_limits_get,
	.level_set = scmi_perf_level_set,
	.level_get = scmi_perf_level_get,
	.device_domain_id = scmi_dev_domain_id,
	.transition_latency_get = scmi_dvfs_transition_latency_get,
	.device_opps_add = scmi_dvfs_device_opps_add,
	.freq_set = scmi_dvfs_freq_set,
	.freq_get = scmi_dvfs_freq_get,
	.est_power_get = scmi_dvfs_est_power_get,
	.fast_switch_possible = scmi_fast_switch_possible,
	.power_scale_get = scmi_power_scale_get,
};

static int scmi_perf_set_notify_enabled(const struct scmi_protocol_handle *ph,
					u8 evt_id, u32 src_id, bool enable)
{
	int ret, cmd_id;

	if (evt_id >= ARRAY_SIZE(evt_2_cmd))
		return -EINVAL;

	cmd_id = evt_2_cmd[evt_id];
	ret = scmi_perf_level_limits_notify(ph, src_id, cmd_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);

	return ret;
}

static void *scmi_perf_fill_custom_report(const struct scmi_protocol_handle *ph,
					  u8 evt_id, ktime_t timestamp,
					  const void *payld, size_t payld_sz,
					  void *report, u32 *src_id)
{
	void *rep = NULL;

	switch (evt_id) {
	case SCMI_EVENT_PERFORMANCE_LIMITS_CHANGED:
	{
		const struct scmi_perf_limits_notify_payld *p = payld;
		struct scmi_perf_limits_report *r = report;

		if (sizeof(*p) != payld_sz)
			break;

		r->timestamp = timestamp;
		r->agent_id = le32_to_cpu(p->agent_id);
		r->domain_id = le32_to_cpu(p->domain_id);
		r->range_max = le32_to_cpu(p->range_max);
		r->range_min = le32_to_cpu(p->range_min);
		*src_id = r->domain_id;
		rep = r;
		break;
	}
	case SCMI_EVENT_PERFORMANCE_LEVEL_CHANGED:
	{
		const struct scmi_perf_level_notify_payld *p = payld;
		struct scmi_perf_level_report *r = report;

		if (sizeof(*p) != payld_sz)
			break;

		r->timestamp = timestamp;
		r->agent_id = le32_to_cpu(p->agent_id);
		r->domain_id = le32_to_cpu(p->domain_id);
		r->performance_level = le32_to_cpu(p->performance_level);
		*src_id = r->domain_id;
		rep = r;
		break;
	}
	default:
		break;
	}

	return rep;
}

static int scmi_perf_get_num_sources(const struct scmi_protocol_handle *ph)
{
	struct scmi_perf_info *pi = ph->get_priv(ph);

	if (!pi)
		return -EINVAL;

	return pi->num_domains;
}

static const struct scmi_event perf_events[] = {
	{
		.id = SCMI_EVENT_PERFORMANCE_LIMITS_CHANGED,
		.max_payld_sz = sizeof(struct scmi_perf_limits_notify_payld),
		.max_report_sz = sizeof(struct scmi_perf_limits_report),
	},
	{
		.id = SCMI_EVENT_PERFORMANCE_LEVEL_CHANGED,
		.max_payld_sz = sizeof(struct scmi_perf_level_notify_payld),
		.max_report_sz = sizeof(struct scmi_perf_level_report),
	},
};

static const struct scmi_event_ops perf_event_ops = {
	.get_num_sources = scmi_perf_get_num_sources,
	.set_notify_enabled = scmi_perf_set_notify_enabled,
	.fill_custom_report = scmi_perf_fill_custom_report,
};

static const struct scmi_protocol_events perf_protocol_events = {
	.queue_sz = SCMI_PROTO_QUEUE_SZ,
	.ops = &perf_event_ops,
	.evts = perf_events,
	.num_events = ARRAY_SIZE(perf_events),
};

static int scmi_perf_protocol_init(const struct scmi_protocol_handle *ph)
{
	int domain, ret;
	u32 version;
	struct scmi_perf_info *pinfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_dbg(ph->dev, "Performance Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(ph->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->version = version;

	ret = scmi_perf_attributes_get(ph, pinfo);
	if (ret)
		return ret;

	pinfo->dom_info = devm_kcalloc(ph->dev, pinfo->num_domains,
				       sizeof(*pinfo->dom_info), GFP_KERNEL);
	if (!pinfo->dom_info)
		return -ENOMEM;

	for (domain = 0; domain < pinfo->num_domains; domain++) {
		struct perf_dom_info *dom = pinfo->dom_info + domain;

		scmi_perf_domain_attributes_get(ph, domain, dom, version);
		scmi_perf_describe_levels_get(ph, domain, dom);

		if (dom->perf_fastchannels)
			scmi_perf_domain_init_fc(ph, domain, &dom->fc_info);
	}

	return ph->set_priv(ph, pinfo);
}

static const struct scmi_protocol scmi_perf = {
	.id = SCMI_PROTOCOL_PERF,
	.owner = THIS_MODULE,
	.instance_init = &scmi_perf_protocol_init,
	.ops = &perf_proto_ops,
	.events = &perf_protocol_events,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(perf, scmi_perf)
