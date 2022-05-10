// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Clock Protocol
 *
 * Copyright (C) 2018-2021 ARM Ltd.
 */

#include <linux/module.h>
#include <linux/sort.h>

#include "common.h"

enum scmi_clock_protocol_cmd {
	CLOCK_ATTRIBUTES = 0x3,
	CLOCK_DESCRIBE_RATES = 0x4,
	CLOCK_RATE_SET = 0x5,
	CLOCK_RATE_GET = 0x6,
	CLOCK_CONFIG_SET = 0x7,
};

struct scmi_msg_resp_clock_protocol_attributes {
	__le16 num_clocks;
	u8 max_async_req;
	u8 reserved;
};

struct scmi_msg_resp_clock_attributes {
	__le32 attributes;
#define	CLOCK_ENABLE	BIT(0)
	    u8 name[SCMI_MAX_STR_SIZE];
};

struct scmi_clock_set_config {
	__le32 id;
	__le32 attributes;
};

struct scmi_msg_clock_describe_rates {
	__le32 id;
	__le32 rate_index;
};

struct scmi_msg_resp_clock_describe_rates {
	__le32 num_rates_flags;
#define NUM_RETURNED(x)		((x) & 0xfff)
#define RATE_DISCRETE(x)	!((x) & BIT(12))
#define NUM_REMAINING(x)	((x) >> 16)
	struct {
		__le32 value_low;
		__le32 value_high;
	} rate[0];
#define RATE_TO_U64(X)		\
({				\
	typeof(X) x = (X);	\
	le32_to_cpu((x).value_low) | (u64)le32_to_cpu((x).value_high) << 32; \
})
};

struct scmi_clock_set_rate {
	__le32 flags;
#define CLOCK_SET_ASYNC		BIT(0)
#define CLOCK_SET_IGNORE_RESP	BIT(1)
#define CLOCK_SET_ROUND_UP	BIT(2)
#define CLOCK_SET_ROUND_AUTO	BIT(3)
	__le32 id;
	__le32 value_low;
	__le32 value_high;
};

struct clock_info {
	u32 version;
	int num_clocks;
	int max_async_req;
	atomic_t cur_async_req;
	struct scmi_clock_info *clk;
};

static int
scmi_clock_protocol_attributes_get(const struct scmi_protocol_handle *ph,
				   struct clock_info *ci)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_clock_protocol_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES,
				      0, sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		ci->num_clocks = le16_to_cpu(attr->num_clocks);
		ci->max_async_req = attr->max_async_req;
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_clock_attributes_get(const struct scmi_protocol_handle *ph,
				     u32 clk_id, struct scmi_clock_info *clk)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_clock_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, CLOCK_ATTRIBUTES,
				      sizeof(clk_id), sizeof(*attr), &t);
	if (ret)
		return ret;

	put_unaligned_le32(clk_id, t->tx.buf);
	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		strlcpy(clk->name, attr->name, SCMI_MAX_STR_SIZE);
	else
		clk->name[0] = '\0';

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int rate_cmp_func(const void *_r1, const void *_r2)
{
	const u64 *r1 = _r1, *r2 = _r2;

	if (*r1 < *r2)
		return -1;
	else if (*r1 == *r2)
		return 0;
	else
		return 1;
}

static int
scmi_clock_describe_rates_get(const struct scmi_protocol_handle *ph, u32 clk_id,
			      struct scmi_clock_info *clk)
{
	u64 *rate = NULL;
	int ret, cnt;
	bool rate_discrete = false;
	u32 tot_rate_cnt = 0, rates_flag;
	u16 num_returned, num_remaining;
	struct scmi_xfer *t;
	struct scmi_msg_clock_describe_rates *clk_desc;
	struct scmi_msg_resp_clock_describe_rates *rlist;

	ret = ph->xops->xfer_get_init(ph, CLOCK_DESCRIBE_RATES,
				      sizeof(*clk_desc), 0, &t);
	if (ret)
		return ret;

	clk_desc = t->tx.buf;
	rlist = t->rx.buf;

	do {
		clk_desc->id = cpu_to_le32(clk_id);
		/* Set the number of rates to be skipped/already read */
		clk_desc->rate_index = cpu_to_le32(tot_rate_cnt);

		ret = ph->xops->do_xfer(ph, t);
		if (ret)
			goto err;

		rates_flag = le32_to_cpu(rlist->num_rates_flags);
		num_remaining = NUM_REMAINING(rates_flag);
		rate_discrete = RATE_DISCRETE(rates_flag);
		num_returned = NUM_RETURNED(rates_flag);

		if (tot_rate_cnt + num_returned > SCMI_MAX_NUM_RATES) {
			dev_err(ph->dev, "No. of rates > MAX_NUM_RATES");
			break;
		}

		if (!rate_discrete) {
			clk->range.min_rate = RATE_TO_U64(rlist->rate[0]);
			clk->range.max_rate = RATE_TO_U64(rlist->rate[1]);
			clk->range.step_size = RATE_TO_U64(rlist->rate[2]);
			dev_dbg(ph->dev, "Min %llu Max %llu Step %llu Hz\n",
				clk->range.min_rate, clk->range.max_rate,
				clk->range.step_size);
			break;
		}

		rate = &clk->list.rates[tot_rate_cnt];
		for (cnt = 0; cnt < num_returned; cnt++, rate++) {
			*rate = RATE_TO_U64(rlist->rate[cnt]);
			dev_dbg(ph->dev, "Rate %llu Hz\n", *rate);
		}

		tot_rate_cnt += num_returned;

		ph->xops->reset_rx_to_maxsz(ph, t);
		/*
		 * check for both returned and remaining to avoid infinite
		 * loop due to buggy firmware
		 */
	} while (num_returned && num_remaining);

	if (rate_discrete && rate) {
		clk->list.num_rates = tot_rate_cnt;
		sort(clk->list.rates, tot_rate_cnt, sizeof(*rate),
		     rate_cmp_func, NULL);
	}

	clk->rate_discrete = rate_discrete;

err:
	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_clock_rate_get(const struct scmi_protocol_handle *ph,
		    u32 clk_id, u64 *value)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, CLOCK_RATE_GET,
				      sizeof(__le32), sizeof(u64), &t);
	if (ret)
		return ret;

	put_unaligned_le32(clk_id, t->tx.buf);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*value = get_unaligned_le64(t->rx.buf);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_clock_rate_set(const struct scmi_protocol_handle *ph,
			       u32 clk_id, u64 rate)
{
	int ret;
	u32 flags = 0;
	struct scmi_xfer *t;
	struct scmi_clock_set_rate *cfg;
	struct clock_info *ci = ph->get_priv(ph);

	ret = ph->xops->xfer_get_init(ph, CLOCK_RATE_SET, sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	if (ci->max_async_req &&
	    atomic_inc_return(&ci->cur_async_req) < ci->max_async_req)
		flags |= CLOCK_SET_ASYNC;

	cfg = t->tx.buf;
	cfg->flags = cpu_to_le32(flags);
	cfg->id = cpu_to_le32(clk_id);
	cfg->value_low = cpu_to_le32(rate & 0xffffffff);
	cfg->value_high = cpu_to_le32(rate >> 32);

	if (flags & CLOCK_SET_ASYNC)
		ret = ph->xops->do_xfer_with_response(ph, t);
	else
		ret = ph->xops->do_xfer(ph, t);

	if (ci->max_async_req)
		atomic_dec(&ci->cur_async_req);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_clock_config_set(const struct scmi_protocol_handle *ph, u32 clk_id,
		      u32 config)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_clock_set_config *cfg;

	ret = ph->xops->xfer_get_init(ph, CLOCK_CONFIG_SET,
				      sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(clk_id);
	cfg->attributes = cpu_to_le32(config);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_clock_enable(const struct scmi_protocol_handle *ph, u32 clk_id)
{
	return scmi_clock_config_set(ph, clk_id, CLOCK_ENABLE);
}

static int scmi_clock_disable(const struct scmi_protocol_handle *ph, u32 clk_id)
{
	return scmi_clock_config_set(ph, clk_id, 0);
}

static int scmi_clock_count_get(const struct scmi_protocol_handle *ph)
{
	struct clock_info *ci = ph->get_priv(ph);

	return ci->num_clocks;
}

static const struct scmi_clock_info *
scmi_clock_info_get(const struct scmi_protocol_handle *ph, u32 clk_id)
{
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_info *clk = ci->clk + clk_id;

	if (!clk->name[0])
		return NULL;

	return clk;
}

static const struct scmi_clk_proto_ops clk_proto_ops = {
	.count_get = scmi_clock_count_get,
	.info_get = scmi_clock_info_get,
	.rate_get = scmi_clock_rate_get,
	.rate_set = scmi_clock_rate_set,
	.enable = scmi_clock_enable,
	.disable = scmi_clock_disable,
};

static int scmi_clock_protocol_init(const struct scmi_protocol_handle *ph)
{
	u32 version;
	int clkid, ret;
	struct clock_info *cinfo;

	ph->xops->version_get(ph, &version);

	dev_dbg(ph->dev, "Clock Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	cinfo = devm_kzalloc(ph->dev, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	scmi_clock_protocol_attributes_get(ph, cinfo);

	cinfo->clk = devm_kcalloc(ph->dev, cinfo->num_clocks,
				  sizeof(*cinfo->clk), GFP_KERNEL);
	if (!cinfo->clk)
		return -ENOMEM;

	for (clkid = 0; clkid < cinfo->num_clocks; clkid++) {
		struct scmi_clock_info *clk = cinfo->clk + clkid;

		ret = scmi_clock_attributes_get(ph, clkid, clk);
		if (!ret)
			scmi_clock_describe_rates_get(ph, clkid, clk);
	}

	cinfo->version = version;
	return ph->set_priv(ph, cinfo);
}

static const struct scmi_protocol scmi_clock = {
	.id = SCMI_PROTOCOL_CLOCK,
	.owner = THIS_MODULE,
	.instance_init = &scmi_clock_protocol_init,
	.ops = &clk_proto_ops,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(clock, scmi_clock)
