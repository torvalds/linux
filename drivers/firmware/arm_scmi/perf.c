// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Performance Protocol
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/sort.h>

#include "common.h"

enum scmi_performance_protocol_cmd {
	PERF_DOMAIN_ATTRIBUTES = 0x3,
	PERF_DESCRIBE_LEVELS = 0x4,
	PERF_LIMITS_SET = 0x5,
	PERF_LIMITS_GET = 0x6,
	PERF_LEVEL_SET = 0x7,
	PERF_LEVEL_GET = 0x8,
	PERF_NOTIFY_LIMITS = 0x9,
	PERF_NOTIFY_LEVEL = 0xa,
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
	__le32 rate_limit_us;
	__le32 sustained_freq_khz;
	__le32 sustained_perf_level;
	    u8 name[SCMI_MAX_STR_SIZE];
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

struct scmi_msg_resp_perf_describe_levels {
	__le16 num_returned;
	__le16 num_remaining;
	struct {
		__le32 perf_val;
		__le32 power;
		__le16 transition_latency_us;
		__le16 reserved;
	} opp[0];
};

struct perf_dom_info {
	bool set_limits;
	bool set_perf;
	bool perf_limit_notify;
	bool perf_level_notify;
	u32 opp_count;
	u32 sustained_freq_khz;
	u32 sustained_perf_level;
	u32 mult_factor;
	char name[SCMI_MAX_STR_SIZE];
	struct scmi_opp opp[MAX_OPPS];
};

struct scmi_perf_info {
	int num_domains;
	bool power_scale_mw;
	u64 stats_addr;
	u32 stats_size;
	struct perf_dom_info *dom_info;
};

static int scmi_perf_attributes_get(const struct scmi_handle *handle,
				    struct scmi_perf_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_perf_attributes *attr;

	ret = scmi_one_xfer_init(handle, PROTOCOL_ATTRIBUTES,
				 SCMI_PROTOCOL_PERF, 0, sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		u16 flags = le16_to_cpu(attr->flags);

		pi->num_domains = le16_to_cpu(attr->num_domains);
		pi->power_scale_mw = POWER_SCALE_IN_MILLIWATT(flags);
		pi->stats_addr = le32_to_cpu(attr->stats_addr_low) |
				(u64)le32_to_cpu(attr->stats_addr_high) << 32;
		pi->stats_size = le32_to_cpu(attr->stats_size);
	}

	scmi_one_xfer_put(handle, t);
	return ret;
}

static int
scmi_perf_domain_attributes_get(const struct scmi_handle *handle, u32 domain,
				struct perf_dom_info *dom_info)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_perf_domain_attributes *attr;

	ret = scmi_one_xfer_init(handle, PERF_DOMAIN_ATTRIBUTES,
				 SCMI_PROTOCOL_PERF, sizeof(domain),
				 sizeof(*attr), &t);
	if (ret)
		return ret;

	*(__le32 *)t->tx.buf = cpu_to_le32(domain);
	attr = t->rx.buf;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		u32 flags = le32_to_cpu(attr->flags);

		dom_info->set_limits = SUPPORTS_SET_LIMITS(flags);
		dom_info->set_perf = SUPPORTS_SET_PERF_LVL(flags);
		dom_info->perf_limit_notify = SUPPORTS_PERF_LIMIT_NOTIFY(flags);
		dom_info->perf_level_notify = SUPPORTS_PERF_LEVEL_NOTIFY(flags);
		dom_info->sustained_freq_khz =
					le32_to_cpu(attr->sustained_freq_khz);
		dom_info->sustained_perf_level =
					le32_to_cpu(attr->sustained_perf_level);
		dom_info->mult_factor =	(dom_info->sustained_freq_khz * 1000) /
					dom_info->sustained_perf_level;
		memcpy(dom_info->name, attr->name, SCMI_MAX_STR_SIZE);
	}

	scmi_one_xfer_put(handle, t);
	return ret;
}

static int opp_cmp_func(const void *opp1, const void *opp2)
{
	const struct scmi_opp *t1 = opp1, *t2 = opp2;

	return t1->perf - t2->perf;
}

static int
scmi_perf_describe_levels_get(const struct scmi_handle *handle, u32 domain,
			      struct perf_dom_info *perf_dom)
{
	int ret, cnt;
	u32 tot_opp_cnt = 0;
	u16 num_returned, num_remaining;
	struct scmi_xfer *t;
	struct scmi_opp *opp;
	struct scmi_msg_perf_describe_levels *dom_info;
	struct scmi_msg_resp_perf_describe_levels *level_info;

	ret = scmi_one_xfer_init(handle, PERF_DESCRIBE_LEVELS,
				 SCMI_PROTOCOL_PERF, sizeof(*dom_info), 0, &t);
	if (ret)
		return ret;

	dom_info = t->tx.buf;
	level_info = t->rx.buf;

	do {
		dom_info->domain = cpu_to_le32(domain);
		/* Set the number of OPPs to be skipped/already read */
		dom_info->level_index = cpu_to_le32(tot_opp_cnt);

		ret = scmi_do_xfer(handle, t);
		if (ret)
			break;

		num_returned = le16_to_cpu(level_info->num_returned);
		num_remaining = le16_to_cpu(level_info->num_remaining);
		if (tot_opp_cnt + num_returned > MAX_OPPS) {
			dev_err(handle->dev, "No. of OPPs exceeded MAX_OPPS");
			break;
		}

		opp = &perf_dom->opp[tot_opp_cnt];
		for (cnt = 0; cnt < num_returned; cnt++, opp++) {
			opp->perf = le32_to_cpu(level_info->opp[cnt].perf_val);
			opp->power = le32_to_cpu(level_info->opp[cnt].power);
			opp->trans_latency_us = le16_to_cpu
				(level_info->opp[cnt].transition_latency_us);

			dev_dbg(handle->dev, "Level %d Power %d Latency %dus\n",
				opp->perf, opp->power, opp->trans_latency_us);
		}

		tot_opp_cnt += num_returned;
		/*
		 * check for both returned and remaining to avoid infinite
		 * loop due to buggy firmware
		 */
	} while (num_returned && num_remaining);

	perf_dom->opp_count = tot_opp_cnt;
	scmi_one_xfer_put(handle, t);

	sort(perf_dom->opp, tot_opp_cnt, sizeof(*opp), opp_cmp_func, NULL);
	return ret;
}

static int scmi_perf_limits_set(const struct scmi_handle *handle, u32 domain,
				u32 max_perf, u32 min_perf)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_set_limits *limits;

	ret = scmi_one_xfer_init(handle, PERF_LIMITS_SET, SCMI_PROTOCOL_PERF,
				 sizeof(*limits), 0, &t);
	if (ret)
		return ret;

	limits = t->tx.buf;
	limits->domain = cpu_to_le32(domain);
	limits->max_level = cpu_to_le32(max_perf);
	limits->min_level = cpu_to_le32(min_perf);

	ret = scmi_do_xfer(handle, t);

	scmi_one_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_limits_get(const struct scmi_handle *handle, u32 domain,
				u32 *max_perf, u32 *min_perf)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_get_limits *limits;

	ret = scmi_one_xfer_init(handle, PERF_LIMITS_GET, SCMI_PROTOCOL_PERF,
				 sizeof(__le32), 0, &t);
	if (ret)
		return ret;

	*(__le32 *)t->tx.buf = cpu_to_le32(domain);

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		limits = t->rx.buf;

		*max_perf = le32_to_cpu(limits->max_level);
		*min_perf = le32_to_cpu(limits->min_level);
	}

	scmi_one_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_level_set(const struct scmi_handle *handle, u32 domain,
			       u32 level, bool poll)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_set_level *lvl;

	ret = scmi_one_xfer_init(handle, PERF_LEVEL_SET, SCMI_PROTOCOL_PERF,
				 sizeof(*lvl), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = poll;
	lvl = t->tx.buf;
	lvl->domain = cpu_to_le32(domain);
	lvl->level = cpu_to_le32(level);

	ret = scmi_do_xfer(handle, t);

	scmi_one_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_level_get(const struct scmi_handle *handle, u32 domain,
			       u32 *level, bool poll)
{
	int ret;
	struct scmi_xfer *t;

	ret = scmi_one_xfer_init(handle, PERF_LEVEL_GET, SCMI_PROTOCOL_PERF,
				 sizeof(u32), sizeof(u32), &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = poll;
	*(__le32 *)t->tx.buf = cpu_to_le32(domain);

	ret = scmi_do_xfer(handle, t);
	if (!ret)
		*level = le32_to_cpu(*(__le32 *)t->rx.buf);

	scmi_one_xfer_put(handle, t);
	return ret;
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

static int scmi_dvfs_add_opps_to_device(const struct scmi_handle *handle,
					struct device *dev)
{
	int idx, ret, domain;
	unsigned long freq;
	struct scmi_opp *opp;
	struct perf_dom_info *dom;
	struct scmi_perf_info *pi = handle->perf_priv;

	domain = scmi_dev_domain_id(dev);
	if (domain < 0)
		return domain;

	dom = pi->dom_info + domain;
	if (!dom)
		return -EIO;

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

static int scmi_dvfs_get_transition_latency(const struct scmi_handle *handle,
					    struct device *dev)
{
	struct perf_dom_info *dom;
	struct scmi_perf_info *pi = handle->perf_priv;
	int domain = scmi_dev_domain_id(dev);

	if (domain < 0)
		return domain;

	dom = pi->dom_info + domain;
	if (!dom)
		return -EIO;

	/* uS to nS */
	return dom->opp[dom->opp_count - 1].trans_latency_us * 1000;
}

static int scmi_dvfs_freq_set(const struct scmi_handle *handle, u32 domain,
			      unsigned long freq, bool poll)
{
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom = pi->dom_info + domain;

	return scmi_perf_level_set(handle, domain, freq / dom->mult_factor,
				   poll);
}

static int scmi_dvfs_freq_get(const struct scmi_handle *handle, u32 domain,
			      unsigned long *freq, bool poll)
{
	int ret;
	u32 level;
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom = pi->dom_info + domain;

	ret = scmi_perf_level_get(handle, domain, &level, poll);
	if (!ret)
		*freq = level * dom->mult_factor;

	return ret;
}

static struct scmi_perf_ops perf_ops = {
	.limits_set = scmi_perf_limits_set,
	.limits_get = scmi_perf_limits_get,
	.level_set = scmi_perf_level_set,
	.level_get = scmi_perf_level_get,
	.device_domain_id = scmi_dev_domain_id,
	.get_transition_latency = scmi_dvfs_get_transition_latency,
	.add_opps_to_device = scmi_dvfs_add_opps_to_device,
	.freq_set = scmi_dvfs_freq_set,
	.freq_get = scmi_dvfs_freq_get,
};

static int scmi_perf_protocol_init(struct scmi_handle *handle)
{
	int domain;
	u32 version;
	struct scmi_perf_info *pinfo;

	scmi_version_get(handle, SCMI_PROTOCOL_PERF, &version);

	dev_dbg(handle->dev, "Performance Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(handle->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	scmi_perf_attributes_get(handle, pinfo);

	pinfo->dom_info = devm_kcalloc(handle->dev, pinfo->num_domains,
				       sizeof(*pinfo->dom_info), GFP_KERNEL);
	if (!pinfo->dom_info)
		return -ENOMEM;

	for (domain = 0; domain < pinfo->num_domains; domain++) {
		struct perf_dom_info *dom = pinfo->dom_info + domain;

		scmi_perf_domain_attributes_get(handle, domain, dom);
		scmi_perf_describe_levels_get(handle, domain, dom);
	}

	handle->perf_ops = &perf_ops;
	handle->perf_priv = pinfo;

	return 0;
}

static int __init scmi_perf_init(void)
{
	return scmi_protocol_register(SCMI_PROTOCOL_PERF,
				      &scmi_perf_protocol_init);
}
subsys_initcall(scmi_perf_init);
