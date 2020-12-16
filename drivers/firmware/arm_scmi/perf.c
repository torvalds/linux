// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Performance Protocol
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#define pr_fmt(fmt) "SCMI Notifications PERF - " fmt

#include <linux/bits.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/scmi_protocol.h>
#include <linux/sort.h>

#include "common.h"
#include "notify.h"

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
#define SUPPORTS_PERF_FASTCHANNELS(x)	((x) & BIT(27))
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

struct scmi_perf_get_fc_info {
	__le32 domain;
	__le32 message_id;
};

struct scmi_msg_resp_perf_desc_fc {
	__le32 attr;
#define SUPPORTS_DOORBELL(x)		((x) & BIT(0))
#define DOORBELL_REG_WIDTH(x)		FIELD_GET(GENMASK(2, 1), (x))
	__le32 rate_limit;
	__le32 chan_addr_low;
	__le32 chan_addr_high;
	__le32 chan_size;
	__le32 db_addr_low;
	__le32 db_addr_high;
	__le32 db_set_lmask;
	__le32 db_set_hmask;
	__le32 db_preserve_lmask;
	__le32 db_preserve_hmask;
};

struct scmi_fc_db_info {
	int width;
	u64 set;
	u64 mask;
	void __iomem *addr;
};

struct scmi_fc_info {
	void __iomem *level_set_addr;
	void __iomem *limit_set_addr;
	void __iomem *level_get_addr;
	void __iomem *limit_get_addr;
	struct scmi_fc_db_info *level_set_db;
	struct scmi_fc_db_info *limit_set_db;
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
	u32 mult_factor;
	char name[SCMI_MAX_STR_SIZE];
	struct scmi_opp opp[MAX_OPPS];
	struct scmi_fc_info *fc_info;
};

struct scmi_perf_info {
	u32 version;
	int num_domains;
	bool power_scale_mw;
	u64 stats_addr;
	u32 stats_size;
	struct perf_dom_info *dom_info;
};

static enum scmi_performance_protocol_cmd evt_2_cmd[] = {
	PERF_NOTIFY_LIMITS,
	PERF_NOTIFY_LEVEL,
};

static int scmi_perf_attributes_get(const struct scmi_handle *handle,
				    struct scmi_perf_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_perf_attributes *attr;

	ret = scmi_xfer_get_init(handle, PROTOCOL_ATTRIBUTES,
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

	scmi_xfer_put(handle, t);
	return ret;
}

static int
scmi_perf_domain_attributes_get(const struct scmi_handle *handle, u32 domain,
				struct perf_dom_info *dom_info)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_perf_domain_attributes *attr;

	ret = scmi_xfer_get_init(handle, PERF_DOMAIN_ATTRIBUTES,
				 SCMI_PROTOCOL_PERF, sizeof(domain),
				 sizeof(*attr), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);
	attr = t->rx.buf;

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		u32 flags = le32_to_cpu(attr->flags);

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
					(dom_info->sustained_freq_khz * 1000) /
					dom_info->sustained_perf_level;
		strlcpy(dom_info->name, attr->name, SCMI_MAX_STR_SIZE);
	}

	scmi_xfer_put(handle, t);
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

	ret = scmi_xfer_get_init(handle, PERF_DESCRIBE_LEVELS,
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

		scmi_reset_rx_to_maxsz(handle, t);
		/*
		 * check for both returned and remaining to avoid infinite
		 * loop due to buggy firmware
		 */
	} while (num_returned && num_remaining);

	perf_dom->opp_count = tot_opp_cnt;
	scmi_xfer_put(handle, t);

	sort(perf_dom->opp, tot_opp_cnt, sizeof(*opp), opp_cmp_func, NULL);
	return ret;
}

#define SCMI_PERF_FC_RING_DB(w)				\
do {							\
	u##w val = 0;					\
							\
	if (db->mask)					\
		val = ioread##w(db->addr) & db->mask;	\
	iowrite##w((u##w)db->set | val, db->addr);	\
} while (0)

static void scmi_perf_fc_ring_db(struct scmi_fc_db_info *db)
{
	if (!db || !db->addr)
		return;

	if (db->width == 1)
		SCMI_PERF_FC_RING_DB(8);
	else if (db->width == 2)
		SCMI_PERF_FC_RING_DB(16);
	else if (db->width == 4)
		SCMI_PERF_FC_RING_DB(32);
	else /* db->width == 8 */
#ifdef CONFIG_64BIT
		SCMI_PERF_FC_RING_DB(64);
#else
	{
		u64 val = 0;

		if (db->mask)
			val = ioread64_hi_lo(db->addr) & db->mask;
		iowrite64_hi_lo(db->set | val, db->addr);
	}
#endif
}

static int scmi_perf_mb_limits_set(const struct scmi_handle *handle, u32 domain,
				   u32 max_perf, u32 min_perf)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_set_limits *limits;

	ret = scmi_xfer_get_init(handle, PERF_LIMITS_SET, SCMI_PROTOCOL_PERF,
				 sizeof(*limits), 0, &t);
	if (ret)
		return ret;

	limits = t->tx.buf;
	limits->domain = cpu_to_le32(domain);
	limits->max_level = cpu_to_le32(max_perf);
	limits->min_level = cpu_to_le32(min_perf);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_limits_set(const struct scmi_handle *handle, u32 domain,
				u32 max_perf, u32 min_perf)
{
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom = pi->dom_info + domain;

	if (dom->fc_info && dom->fc_info->limit_set_addr) {
		iowrite32(max_perf, dom->fc_info->limit_set_addr);
		iowrite32(min_perf, dom->fc_info->limit_set_addr + 4);
		scmi_perf_fc_ring_db(dom->fc_info->limit_set_db);
		return 0;
	}

	return scmi_perf_mb_limits_set(handle, domain, max_perf, min_perf);
}

static int scmi_perf_mb_limits_get(const struct scmi_handle *handle, u32 domain,
				   u32 *max_perf, u32 *min_perf)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_get_limits *limits;

	ret = scmi_xfer_get_init(handle, PERF_LIMITS_GET, SCMI_PROTOCOL_PERF,
				 sizeof(__le32), 0, &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);

	ret = scmi_do_xfer(handle, t);
	if (!ret) {
		limits = t->rx.buf;

		*max_perf = le32_to_cpu(limits->max_level);
		*min_perf = le32_to_cpu(limits->min_level);
	}

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_limits_get(const struct scmi_handle *handle, u32 domain,
				u32 *max_perf, u32 *min_perf)
{
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom = pi->dom_info + domain;

	if (dom->fc_info && dom->fc_info->limit_get_addr) {
		*max_perf = ioread32(dom->fc_info->limit_get_addr);
		*min_perf = ioread32(dom->fc_info->limit_get_addr + 4);
		return 0;
	}

	return scmi_perf_mb_limits_get(handle, domain, max_perf, min_perf);
}

static int scmi_perf_mb_level_set(const struct scmi_handle *handle, u32 domain,
				  u32 level, bool poll)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_set_level *lvl;

	ret = scmi_xfer_get_init(handle, PERF_LEVEL_SET, SCMI_PROTOCOL_PERF,
				 sizeof(*lvl), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = poll;
	lvl = t->tx.buf;
	lvl->domain = cpu_to_le32(domain);
	lvl->level = cpu_to_le32(level);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_level_set(const struct scmi_handle *handle, u32 domain,
			       u32 level, bool poll)
{
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom = pi->dom_info + domain;

	if (dom->fc_info && dom->fc_info->level_set_addr) {
		iowrite32(level, dom->fc_info->level_set_addr);
		scmi_perf_fc_ring_db(dom->fc_info->level_set_db);
		return 0;
	}

	return scmi_perf_mb_level_set(handle, domain, level, poll);
}

static int scmi_perf_mb_level_get(const struct scmi_handle *handle, u32 domain,
				  u32 *level, bool poll)
{
	int ret;
	struct scmi_xfer *t;

	ret = scmi_xfer_get_init(handle, PERF_LEVEL_GET, SCMI_PROTOCOL_PERF,
				 sizeof(u32), sizeof(u32), &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = poll;
	put_unaligned_le32(domain, t->tx.buf);

	ret = scmi_do_xfer(handle, t);
	if (!ret)
		*level = get_unaligned_le32(t->rx.buf);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_perf_level_get(const struct scmi_handle *handle, u32 domain,
			       u32 *level, bool poll)
{
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom = pi->dom_info + domain;

	if (dom->fc_info && dom->fc_info->level_get_addr) {
		*level = ioread32(dom->fc_info->level_get_addr);
		return 0;
	}

	return scmi_perf_mb_level_get(handle, domain, level, poll);
}

static int scmi_perf_level_limits_notify(const struct scmi_handle *handle,
					 u32 domain, int message_id,
					 bool enable)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_perf_notify_level_or_limits *notify;

	ret = scmi_xfer_get_init(handle, message_id, SCMI_PROTOCOL_PERF,
				 sizeof(*notify), 0, &t);
	if (ret)
		return ret;

	notify = t->tx.buf;
	notify->domain = cpu_to_le32(domain);
	notify->notify_enable = enable ? cpu_to_le32(BIT(0)) : 0;

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static bool scmi_perf_fc_size_is_valid(u32 msg, u32 size)
{
	if ((msg == PERF_LEVEL_GET || msg == PERF_LEVEL_SET) && size == 4)
		return true;
	if ((msg == PERF_LIMITS_GET || msg == PERF_LIMITS_SET) && size == 8)
		return true;
	return false;
}

static void
scmi_perf_domain_desc_fc(const struct scmi_handle *handle, u32 domain,
			 u32 message_id, void __iomem **p_addr,
			 struct scmi_fc_db_info **p_db)
{
	int ret;
	u32 flags;
	u64 phys_addr;
	u8 size;
	void __iomem *addr;
	struct scmi_xfer *t;
	struct scmi_fc_db_info *db;
	struct scmi_perf_get_fc_info *info;
	struct scmi_msg_resp_perf_desc_fc *resp;

	if (!p_addr)
		return;

	ret = scmi_xfer_get_init(handle, PERF_DESCRIBE_FASTCHANNEL,
				 SCMI_PROTOCOL_PERF,
				 sizeof(*info), sizeof(*resp), &t);
	if (ret)
		return;

	info = t->tx.buf;
	info->domain = cpu_to_le32(domain);
	info->message_id = cpu_to_le32(message_id);

	ret = scmi_do_xfer(handle, t);
	if (ret)
		goto err_xfer;

	resp = t->rx.buf;
	flags = le32_to_cpu(resp->attr);
	size = le32_to_cpu(resp->chan_size);
	if (!scmi_perf_fc_size_is_valid(message_id, size))
		goto err_xfer;

	phys_addr = le32_to_cpu(resp->chan_addr_low);
	phys_addr |= (u64)le32_to_cpu(resp->chan_addr_high) << 32;
	addr = devm_ioremap(handle->dev, phys_addr, size);
	if (!addr)
		goto err_xfer;
	*p_addr = addr;

	if (p_db && SUPPORTS_DOORBELL(flags)) {
		db = devm_kzalloc(handle->dev, sizeof(*db), GFP_KERNEL);
		if (!db)
			goto err_xfer;

		size = 1 << DOORBELL_REG_WIDTH(flags);
		phys_addr = le32_to_cpu(resp->db_addr_low);
		phys_addr |= (u64)le32_to_cpu(resp->db_addr_high) << 32;
		addr = devm_ioremap(handle->dev, phys_addr, size);
		if (!addr)
			goto err_xfer;

		db->addr = addr;
		db->width = size;
		db->set = le32_to_cpu(resp->db_set_lmask);
		db->set |= (u64)le32_to_cpu(resp->db_set_hmask) << 32;
		db->mask = le32_to_cpu(resp->db_preserve_lmask);
		db->mask |= (u64)le32_to_cpu(resp->db_preserve_hmask) << 32;
		*p_db = db;
	}
err_xfer:
	scmi_xfer_put(handle, t);
}

static void scmi_perf_domain_init_fc(const struct scmi_handle *handle,
				     u32 domain, struct scmi_fc_info **p_fc)
{
	struct scmi_fc_info *fc;

	fc = devm_kzalloc(handle->dev, sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return;

	scmi_perf_domain_desc_fc(handle, domain, PERF_LEVEL_SET,
				 &fc->level_set_addr, &fc->level_set_db);
	scmi_perf_domain_desc_fc(handle, domain, PERF_LEVEL_GET,
				 &fc->level_get_addr, NULL);
	scmi_perf_domain_desc_fc(handle, domain, PERF_LIMITS_SET,
				 &fc->limit_set_addr, &fc->limit_set_db);
	scmi_perf_domain_desc_fc(handle, domain, PERF_LIMITS_GET,
				 &fc->limit_get_addr, NULL);
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

static int scmi_dvfs_device_opps_add(const struct scmi_handle *handle,
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

static int scmi_dvfs_transition_latency_get(const struct scmi_handle *handle,
					    struct device *dev)
{
	struct perf_dom_info *dom;
	struct scmi_perf_info *pi = handle->perf_priv;
	int domain = scmi_dev_domain_id(dev);

	if (domain < 0)
		return domain;

	dom = pi->dom_info + domain;
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

static int scmi_dvfs_est_power_get(const struct scmi_handle *handle, u32 domain,
				   unsigned long *freq, unsigned long *power)
{
	struct scmi_perf_info *pi = handle->perf_priv;
	struct perf_dom_info *dom;
	unsigned long opp_freq;
	int idx, ret = -EINVAL;
	struct scmi_opp *opp;

	dom = pi->dom_info + domain;
	if (!dom)
		return -EIO;

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

static bool scmi_fast_switch_possible(const struct scmi_handle *handle,
				      struct device *dev)
{
	struct perf_dom_info *dom;
	struct scmi_perf_info *pi = handle->perf_priv;

	dom = pi->dom_info + scmi_dev_domain_id(dev);

	return dom->fc_info && dom->fc_info->level_set_addr;
}

static const struct scmi_perf_ops perf_ops = {
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
};

static int scmi_perf_set_notify_enabled(const struct scmi_handle *handle,
					u8 evt_id, u32 src_id, bool enable)
{
	int ret, cmd_id;

	if (evt_id >= ARRAY_SIZE(evt_2_cmd))
		return -EINVAL;

	cmd_id = evt_2_cmd[evt_id];
	ret = scmi_perf_level_limits_notify(handle, src_id, cmd_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);

	return ret;
}

static void *scmi_perf_fill_custom_report(const struct scmi_handle *handle,
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
	.set_notify_enabled = scmi_perf_set_notify_enabled,
	.fill_custom_report = scmi_perf_fill_custom_report,
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

		if (dom->perf_fastchannels)
			scmi_perf_domain_init_fc(handle, domain, &dom->fc_info);
	}

	scmi_register_protocol_events(handle,
				      SCMI_PROTOCOL_PERF, SCMI_PROTO_QUEUE_SZ,
				      &perf_event_ops, perf_events,
				      ARRAY_SIZE(perf_events),
				      pinfo->num_domains);

	pinfo->version = version;
	handle->perf_ops = &perf_ops;
	handle->perf_priv = pinfo;

	return 0;
}

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(SCMI_PROTOCOL_PERF, perf)
