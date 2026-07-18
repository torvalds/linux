// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Clock Protocol
 *
 * Copyright (C) 2018-2022 ARM Ltd.
 */

#include <linux/math64.h>
#include <linux/module.h>
#include <linux/limits.h>
#include <linux/sort.h>

#include "protocols.h"
#include "notify.h"
#include "quirks.h"

/* Updated only after ALL the mandatory features for that version are merged */
#define SCMI_PROTOCOL_SUPPORTED_VERSION		0x30000

enum scmi_clock_protocol_cmd {
	CLOCK_ATTRIBUTES = 0x3,
	CLOCK_DESCRIBE_RATES = 0x4,
	CLOCK_RATE_SET = 0x5,
	CLOCK_RATE_GET = 0x6,
	CLOCK_CONFIG_SET = 0x7,
	CLOCK_NAME_GET = 0x8,
	CLOCK_RATE_NOTIFY = 0x9,
	CLOCK_RATE_CHANGE_REQUESTED_NOTIFY = 0xA,
	CLOCK_CONFIG_GET = 0xB,
	CLOCK_POSSIBLE_PARENTS_GET = 0xC,
	CLOCK_PARENT_SET = 0xD,
	CLOCK_PARENT_GET = 0xE,
	CLOCK_GET_PERMISSIONS = 0xF,
};

#define CLOCK_STATE_CONTROL_ALLOWED	BIT(31)
#define CLOCK_PARENT_CONTROL_ALLOWED	BIT(30)
#define CLOCK_RATE_CONTROL_ALLOWED	BIT(29)

enum clk_state {
	CLK_STATE_DISABLE,
	CLK_STATE_ENABLE,
	CLK_STATE_RESERVED,
	CLK_STATE_UNCHANGED,
};

struct scmi_msg_resp_clock_protocol_attributes {
	__le16 num_clocks;
	u8 max_async_req;
	u8 reserved;
};

struct scmi_msg_resp_clock_attributes {
	__le32 attributes;
#define SUPPORTS_RATE_CHANGED_NOTIF(x)		((x) & BIT(31))
#define SUPPORTS_RATE_CHANGE_REQUESTED_NOTIF(x)	((x) & BIT(30))
#define SUPPORTS_EXTENDED_NAMES(x)		((x) & BIT(29))
#define SUPPORTS_PARENT_CLOCK(x)		((x) & BIT(28))
#define SUPPORTS_EXTENDED_CONFIG(x)		((x) & BIT(27))
#define SUPPORTS_GET_PERMISSIONS(x)		((x) & BIT(1))
	u8 name[SCMI_SHORT_NAME_MAX_SIZE];
	__le32 clock_enable_latency;
};

struct scmi_msg_clock_possible_parents {
	__le32 id;
	__le32 skip_parents;
};

struct scmi_msg_resp_clock_possible_parents {
	__le32 num_parent_flags;
#define NUM_PARENTS_RETURNED(x)		((x) & 0xff)
#define NUM_PARENTS_REMAINING(x)	((x) >> 24)
	__le32 possible_parents[];
};

struct scmi_msg_clock_set_parent {
	__le32 id;
	__le32 parent_id;
};

struct scmi_msg_clock_config_set {
	__le32 id;
	__le32 attributes;
};

/* Valid only from SCMI clock v2.1 */
struct scmi_msg_clock_config_set_v2 {
	__le32 id;
	__le32 attributes;
#define NULL_OEM_TYPE			0
#define REGMASK_OEM_TYPE_SET		GENMASK(23, 16)
#define REGMASK_CLK_STATE		GENMASK(1, 0)
	__le32 oem_config_val;
};

struct scmi_msg_clock_config_get {
	__le32 id;
	__le32 flags;
#define REGMASK_OEM_TYPE_GET		GENMASK(7, 0)
};

struct scmi_msg_resp_clock_config_get {
	__le32 attributes;
	__le32 config;
#define IS_CLK_ENABLED(x)		le32_get_bits((x), BIT(0))
	__le32 oem_config_val;
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
	} rate[];
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

struct scmi_msg_resp_set_rate_complete {
	__le32 id;
	__le32 rate_low;
	__le32 rate_high;
};

struct scmi_msg_clock_rate_notify {
	__le32 clk_id;
	__le32 notify_enable;
};

struct scmi_clock_rate_notify_payld {
	__le32 agent_id;
	__le32 clock_id;
	__le32 rate_low;
	__le32 rate_high;
};

struct scmi_clock_desc {
	u32 id;
	unsigned int tot_rates;
	struct scmi_clock_rates r;
#define	RATE_MIN	0
#define	RATE_MAX	1
#define	RATE_STEP	2
	struct scmi_clock_info info;
};

#define to_desc(p)	(container_of(p, struct scmi_clock_desc, info))

struct clock_info {
	int num_clocks;
	int max_async_req;
	bool notify_rate_changed_cmd;
	bool notify_rate_change_requested_cmd;
	atomic_t cur_async_req;
	struct scmi_clock_desc *clkds;
#define CLOCK_INFO(c, i)	(&(((c)->clkds + (i))->info))
	int (*clock_config_set)(const struct scmi_protocol_handle *ph,
				u32 clk_id, enum clk_state state,
				enum scmi_clock_oem_config oem_type,
				u32 oem_val, bool atomic);
	int (*clock_config_get)(const struct scmi_protocol_handle *ph,
				u32 clk_id, enum scmi_clock_oem_config oem_type,
				u32 *attributes, bool *enabled, u32 *oem_val,
				bool atomic);
};

static enum scmi_clock_protocol_cmd evt_2_cmd[] = {
	CLOCK_RATE_NOTIFY,
	CLOCK_RATE_CHANGE_REQUESTED_NOTIFY,
};

static inline struct scmi_clock_info *
scmi_clock_domain_lookup(struct clock_info *ci, u32 clk_id)
{
	if (clk_id >= ci->num_clocks)
		return ERR_PTR(-EINVAL);

	return CLOCK_INFO(ci, clk_id);
}

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

	if (!ret) {
		if (!ph->hops->protocol_msg_check(ph, CLOCK_RATE_NOTIFY, NULL))
			ci->notify_rate_changed_cmd = true;

		if (!ph->hops->protocol_msg_check(ph,
						  CLOCK_RATE_CHANGE_REQUESTED_NOTIFY,
						  NULL))
			ci->notify_rate_change_requested_cmd = true;
	}

	return ret;
}

struct scmi_clk_ipriv {
	struct device *dev;
	struct scmi_clock_desc *clkd;
};

static void iter_clk_possible_parents_prepare_message(void *message, unsigned int desc_index,
						      const void *priv)
{
	struct scmi_msg_clock_possible_parents *msg = message;
	const struct scmi_clk_ipriv *p = priv;

	msg->id = cpu_to_le32(p->clkd->id);
	/* Set the number of OPPs to be skipped/already read */
	msg->skip_parents = cpu_to_le32(desc_index);
}

static int iter_clk_possible_parents_update_state(struct scmi_iterator_state *st,
						  const void *response, void *priv)
{
	const struct scmi_msg_resp_clock_possible_parents *r = response;
	struct scmi_clk_ipriv *p = priv;
	u32 flags;

	flags = le32_to_cpu(r->num_parent_flags);
	st->num_returned = NUM_PARENTS_RETURNED(flags);
	st->num_remaining = NUM_PARENTS_REMAINING(flags);

	/*
	 * num parents is not declared previously anywhere so we
	 * assume it's returned+remaining on first call.
	 */
	if (!st->max_resources) {
		int num_parents = st->num_returned + st->num_remaining;

		p->clkd->info.parents = devm_kcalloc(p->dev, num_parents,
						     sizeof(*p->clkd->info.parents),
						     GFP_KERNEL);
		if (!p->clkd->info.parents)
			return -ENOMEM;

		/* max_resources is used by the iterators to control bounds */
		st->max_resources = st->num_returned + st->num_remaining;
	}

	return 0;
}

static int iter_clk_possible_parents_process_response(const struct scmi_protocol_handle *ph,
						      const void *response,
						      struct scmi_iterator_state *st,
						      void *priv)
{
	const struct scmi_msg_resp_clock_possible_parents *r = response;
	struct scmi_clk_ipriv *p = priv;

	p->clkd->info.parents[st->desc_index + st->loop_idx] =
		le32_to_cpu(r->possible_parents[st->loop_idx]);

	/* Count only effectively discovered parents */
	p->clkd->info.num_parents++;

	return 0;
}

static int scmi_clock_possible_parents(const struct scmi_protocol_handle *ph,
				       u32 clk_id, struct clock_info *cinfo)
{
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_clk_possible_parents_prepare_message,
		.update_state = iter_clk_possible_parents_update_state,
		.process_response = iter_clk_possible_parents_process_response,
	};
	struct scmi_clock_desc *clkd = &cinfo->clkds[clk_id];
	struct scmi_clk_ipriv ppriv = {
		.clkd = clkd,
		.dev = ph->dev,
	};
	void *iter;

	iter = ph->hops->iter_response_init(ph, &ops, 0,
					    CLOCK_POSSIBLE_PARENTS_GET,
					    sizeof(struct scmi_msg_clock_possible_parents),
					    &ppriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	return ph->hops->iter_response_run(iter);
}

static int
scmi_clock_get_permissions(const struct scmi_protocol_handle *ph, u32 clk_id,
			   struct scmi_clock_info *clk)
{
	struct scmi_xfer *t;
	u32 perm;
	int ret;

	ret = ph->xops->xfer_get_init(ph, CLOCK_GET_PERMISSIONS,
				      sizeof(clk_id), sizeof(perm), &t);
	if (ret)
		return ret;

	put_unaligned_le32(clk_id, t->tx.buf);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		perm = get_unaligned_le32(t->rx.buf);

		clk->state_ctrl_forbidden = !(perm & CLOCK_STATE_CONTROL_ALLOWED);
		clk->rate_ctrl_forbidden = !(perm & CLOCK_RATE_CONTROL_ALLOWED);
		clk->parent_ctrl_forbidden = !(perm & CLOCK_PARENT_CONTROL_ALLOWED);
	}

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_clock_attributes_get(const struct scmi_protocol_handle *ph,
				     u32 clk_id, struct clock_info *cinfo)
{
	int ret;
	u32 attributes;
	struct scmi_xfer *t;
	struct scmi_msg_resp_clock_attributes *attr;
	struct scmi_clock_info *clk = CLOCK_INFO(cinfo, clk_id);

	ret = ph->xops->xfer_get_init(ph, CLOCK_ATTRIBUTES,
				      sizeof(clk_id), sizeof(*attr), &t);
	if (ret)
		return ret;

	put_unaligned_le32(clk_id, t->tx.buf);
	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		u32 latency = 0;

		attributes = le32_to_cpu(attr->attributes);
		strscpy(clk->name, attr->name, SCMI_SHORT_NAME_MAX_SIZE);
		/* clock_enable_latency field is present only since SCMI v3.1 */
		if (PROTOCOL_REV_MAJOR(ph->version) >= 0x2)
			latency = le32_to_cpu(attr->clock_enable_latency);
		clk->enable_latency = latency ? : U32_MAX;
	}

	ph->xops->xfer_put(ph, t);

	/*
	 * If supported overwrite short name with the extended one;
	 * on error just carry on and use already provided short name.
	 */
	if (!ret && PROTOCOL_REV_MAJOR(ph->version) >= 0x2) {
		if (SUPPORTS_EXTENDED_NAMES(attributes))
			ph->hops->extended_name_get(ph, CLOCK_NAME_GET, clk_id,
						    NULL, clk->name,
						    SCMI_MAX_STR_SIZE);

		if (cinfo->notify_rate_changed_cmd &&
		    SUPPORTS_RATE_CHANGED_NOTIF(attributes))
			clk->rate_changed_notifications = true;
		if (cinfo->notify_rate_change_requested_cmd &&
		    SUPPORTS_RATE_CHANGE_REQUESTED_NOTIF(attributes))
			clk->rate_change_requested_notifications = true;
		if (PROTOCOL_REV_MAJOR(ph->version) >= 0x3) {
			if (SUPPORTS_PARENT_CLOCK(attributes))
				scmi_clock_possible_parents(ph, clk_id, cinfo);
			if (SUPPORTS_GET_PERMISSIONS(attributes))
				scmi_clock_get_permissions(ph, clk_id, clk);
			if (SUPPORTS_EXTENDED_CONFIG(attributes))
				clk->extended_config = true;
		}
	}

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

static void iter_clk_describe_prepare_message(void *message,
					      const unsigned int desc_index,
					      const void *priv)
{
	struct scmi_msg_clock_describe_rates *msg = message;
	const struct scmi_clk_ipriv *p = priv;

	msg->id = cpu_to_le32(p->clkd->id);
	/* Set the number of rates to be skipped/already read */
	msg->rate_index = cpu_to_le32(desc_index);
}

#define QUIRK_OUT_OF_SPEC_TRIPLET					       \
	({								       \
		/*							       \
		 * A known quirk: a triplet is returned but num_returned != 3  \
		 * Check for a safe payload size and fix.		       \
		 */							       \
		if (st->num_returned != 3 && st->num_remaining == 0 &&	       \
		    st->rx_len == sizeof(*r) + sizeof(__le32) * 2 * 3) {       \
			st->num_returned = 3;				       \
			st->num_remaining = 0;				       \
		} else {						       \
			dev_err(p->dev,					       \
				"Cannot fix out-of-spec reply !\n");	       \
			return -EPROTO;					       \
		}							       \
	})

static int
iter_clk_describe_update_state(struct scmi_iterator_state *st,
			       const void *response, void *priv)
{
	u32 flags;
	struct scmi_clk_ipriv *p = priv;
	const struct scmi_msg_resp_clock_describe_rates *r = response;

	flags = le32_to_cpu(r->num_rates_flags);
	st->num_remaining = NUM_REMAINING(flags);
	st->num_returned = NUM_RETURNED(flags);
	p->clkd->r.rate_discrete = RATE_DISCRETE(flags);

	/* Warn about out of spec replies ... */
	if (!p->clkd->r.rate_discrete &&
	    (st->num_returned != 3 || st->num_remaining != 0)) {
		dev_warn(p->dev,
			 "Out-of-spec CLOCK_DESCRIBE_RATES reply for %s - returned:%d remaining:%d rx_len:%zd\n",
			 p->clkd->info.name, st->num_returned, st->num_remaining,
			 st->rx_len);

		SCMI_QUIRK(clock_rates_triplet_out_of_spec,
			   QUIRK_OUT_OF_SPEC_TRIPLET);
	}

	if (!st->max_resources) {
		unsigned int tot_rates = st->num_returned + st->num_remaining;

		p->clkd->r.rates = devm_kcalloc(p->dev, tot_rates,
						sizeof(*p->clkd->r.rates), GFP_KERNEL);
		if (!p->clkd->r.rates)
			return -ENOMEM;

		/* max_resources is used by the iterators to control bounds */
		p->clkd->tot_rates = tot_rates;
		st->max_resources = tot_rates;
	}

	return 0;
}

static int
iter_clk_describe_process_response(const struct scmi_protocol_handle *ph,
				   const void *response,
				   struct scmi_iterator_state *st, void *priv)
{
	struct scmi_clk_ipriv *p = priv;
	const struct scmi_msg_resp_clock_describe_rates *r = response;

	p->clkd->r.rates[p->clkd->r.num_rates] = RATE_TO_U64(r->rate[st->loop_idx]);

	/* Count only effectively discovered rates */
	p->clkd->r.num_rates++;

	return 0;
}

static int
scmi_clock_describe_rates_get_full(const struct scmi_protocol_handle *ph,
				   struct scmi_clock_desc *clkd)
{
	int ret;
	void *iter;
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_clk_describe_prepare_message,
		.update_state = iter_clk_describe_update_state,
		.process_response = iter_clk_describe_process_response,
	};
	struct scmi_clk_ipriv cpriv = {
		.clkd = clkd,
		.dev = ph->dev,
	};

	/*
	 * Using tot_rates as max_resources parameter here so as to trigger
	 * the dynamic allocation only when strictly needed: when trying a
	 * full enumeration after a lazy one tot_rates will be non-zero.
	 */
	iter = ph->hops->iter_response_init(ph, &ops, clkd->tot_rates,
					    CLOCK_DESCRIBE_RATES,
					    sizeof(struct scmi_msg_clock_describe_rates),
					    &cpriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	ret = ph->hops->iter_response_run(iter);
	if (ret)
		return ret;

	/* empty set ? */
	if (!clkd->r.num_rates)
		return 0;

	if (clkd->r.rate_discrete && PROTOCOL_REV_MAJOR(ph->version) == 0x1)
		sort(clkd->r.rates, clkd->r.num_rates,
		     sizeof(clkd->r.rates[0]), rate_cmp_func, NULL);

	return 0;
}

static int
scmi_clock_describe_rates_get_lazy(const struct scmi_protocol_handle *ph,
				   struct scmi_clock_desc *clkd)
{
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_clk_describe_prepare_message,
		.update_state = iter_clk_describe_update_state,
		.process_response = iter_clk_describe_process_response,
	};
	struct scmi_clk_ipriv cpriv = {
		.clkd = clkd,
		.dev = ph->dev,
	};
	unsigned int first, last;
	void *iter;
	int ret;

	iter = ph->hops->iter_response_init(ph, &ops, 0, CLOCK_DESCRIBE_RATES,
					    sizeof(struct scmi_msg_clock_describe_rates),
					    &cpriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	/* Try to grab a triplet, so that in case is NON-discrete we are done */
	first = 0;
	last = 2;
	ret = ph->hops->iter_response_run_bound(iter, &first, &last);
	if (ret)
		goto out;

	/*
	 * If discrete and we don't already have it, grab the last value, which
	 * should be the max
	 */
	if (clkd->r.rate_discrete && clkd->tot_rates > clkd->r.num_rates) {
		first = clkd->tot_rates - 1;
		last = clkd->tot_rates - 1;
		ret = ph->hops->iter_response_run_bound(iter, &first, &last);
	}

out:
	ph->hops->iter_response_bound_cleanup(iter);

	return ret;
}

static int
scmi_clock_describe_rates_get(const struct scmi_protocol_handle *ph,
			      u32 clk_id, struct clock_info *cinfo)
{
	struct scmi_clock_desc *clkd = &cinfo->clkds[clk_id];
	int ret;

	/*
	 * Since only after SCMI Clock v1.0 the returned rates are guaranteed to
	 * be discovered in ascending order, lazy enumeration cannot be use for
	 * SCMI Clock v1.0 protocol.
	 */
	if (PROTOCOL_REV_MAJOR(ph->version) > 0x1)
		ret = scmi_clock_describe_rates_get_lazy(ph, clkd);
	else
		ret = scmi_clock_describe_rates_get_full(ph, clkd);

	if (ret)
		return ret;

	clkd->info.min_rate = clkd->r.rates[RATE_MIN];
	if (!clkd->r.rate_discrete) {
		clkd->info.max_rate = clkd->r.rates[RATE_MAX];
		dev_dbg(ph->dev, "Min %llu Max %llu Step %llu Hz\n",
			clkd->r.rates[RATE_MIN], clkd->r.rates[RATE_MAX],
			clkd->r.rates[RATE_STEP]);
	} else {
		clkd->info.max_rate = clkd->r.rates[clkd->r.num_rates - 1];
		dev_dbg(ph->dev, "Clock:%s Num_Rates:%u -> Min %llu Max %llu\n",
			clkd->info.name, clkd->tot_rates,
			clkd->info.min_rate, clkd->info.max_rate);
	}

	return 0;
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
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (clk->rate_ctrl_forbidden)
		return -EACCES;

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

	if (flags & CLOCK_SET_ASYNC) {
		ret = ph->xops->do_xfer_with_response(ph, t);
		if (!ret) {
			struct scmi_msg_resp_set_rate_complete *resp;

			resp = t->rx.buf;
			if (le32_to_cpu(resp->id) == clk_id)
				dev_dbg(ph->dev,
					"Clk ID %d set async to %llu\n", clk_id,
					get_unaligned_le64(&resp->rate_low));
			else
				ret = -EPROTO;
		}
	} else {
		ret = ph->xops->do_xfer(ph, t);
	}

	if (ci->max_async_req)
		atomic_dec(&ci->cur_async_req);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_clock_determine_rate(const struct scmi_protocol_handle *ph,
				     u32 clk_id, unsigned long *rate)
{
	u64 fmin, fmax, ftmp;
	struct scmi_clock_info *clk;
	struct scmi_clock_desc *clkd;
	struct clock_info *ci = ph->get_priv(ph);

	if (!rate)
		return -EINVAL;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clkd = to_desc(clk);

	/*
	 * If we can't figure out what rate it will be, so just return the
	 * rate back to the caller.
	 */
	if (clkd->r.rate_discrete)
		return 0;

	fmin = clk->min_rate;
	fmax = clk->max_rate;
	if (*rate <= fmin) {
		*rate = fmin;
		return 0;
	} else if (*rate >= fmax) {
		*rate = fmax;
		return 0;
	}

	ftmp = *rate - fmin;
	ftmp += clkd->r.rates[RATE_STEP] - 1; /* to round up */
	ftmp = div64_ul(ftmp, clkd->r.rates[RATE_STEP]);

	*rate = ftmp * clkd->r.rates[RATE_STEP] + fmin;

	return 0;
}

static const struct scmi_clock_rates *
scmi_clock_all_rates_get(const struct scmi_protocol_handle *ph, u32 clk_id)
{
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_desc *clkd;
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk) || !clk->name[0])
		return NULL;

	clkd = to_desc(clk);
	/* Needs full enumeration ? */
	if (clkd->r.rate_discrete && clkd->tot_rates != clkd->r.num_rates) {
		int ret;

		/* rates[] is already allocated BUT we need to re-enumerate */
		clkd->r.num_rates = 0;
		ret = scmi_clock_describe_rates_get_full(ph, clkd);
		if (ret)
			return NULL;
	}

	return &clkd->r;
}

static int
scmi_clock_config_set(const struct scmi_protocol_handle *ph, u32 clk_id,
		      enum clk_state state,
		      enum scmi_clock_oem_config __unused0, u32 __unused1,
		      bool atomic)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_clock_config_set *cfg;

	if (state >= CLK_STATE_RESERVED)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, CLOCK_CONFIG_SET,
				      sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = atomic;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(clk_id);
	cfg->attributes = cpu_to_le32(state);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int
scmi_clock_set_parent(const struct scmi_protocol_handle *ph, u32 clk_id,
		      u32 parent_id)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_clock_set_parent *cfg;
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (parent_id >= clk->num_parents)
		return -EINVAL;

	if (clk->parent_ctrl_forbidden)
		return -EACCES;

	ret = ph->xops->xfer_get_init(ph, CLOCK_PARENT_SET,
				      sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = false;

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(clk_id);
	cfg->parent_id = cpu_to_le32(clk->parents[parent_id]);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int
scmi_clock_get_parent(const struct scmi_protocol_handle *ph, u32 clk_id,
		      u32 *parent_id)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, CLOCK_PARENT_GET,
				      sizeof(__le32), sizeof(u32), &t);
	if (ret)
		return ret;

	put_unaligned_le32(clk_id, t->tx.buf);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*parent_id = get_unaligned_le32(t->rx.buf);

	ph->xops->xfer_put(ph, t);
	return ret;
}

/* For SCMI clock v3.0 and onwards */
static int
scmi_clock_config_set_v2(const struct scmi_protocol_handle *ph, u32 clk_id,
			 enum clk_state state,
			 enum scmi_clock_oem_config oem_type, u32 oem_val,
			 bool atomic)
{
	int ret;
	u32 attrs;
	struct scmi_xfer *t;
	struct scmi_msg_clock_config_set_v2 *cfg;

	if (state == CLK_STATE_RESERVED ||
	    (!oem_type && state == CLK_STATE_UNCHANGED))
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, CLOCK_CONFIG_SET,
				      sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = atomic;

	attrs = FIELD_PREP(REGMASK_OEM_TYPE_SET, oem_type) |
		 FIELD_PREP(REGMASK_CLK_STATE, state);

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(clk_id);
	cfg->attributes = cpu_to_le32(attrs);
	/* Clear in any case */
	cfg->oem_config_val = cpu_to_le32(0);
	if (oem_type)
		cfg->oem_config_val = cpu_to_le32(oem_val);

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_clock_enable(const struct scmi_protocol_handle *ph, u32 clk_id,
			     bool atomic)
{
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (clk->state_ctrl_forbidden)
		return -EACCES;

	return ci->clock_config_set(ph, clk_id, CLK_STATE_ENABLE,
				    NULL_OEM_TYPE, 0, atomic);
}

static int scmi_clock_disable(const struct scmi_protocol_handle *ph, u32 clk_id,
			      bool atomic)
{
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (clk->state_ctrl_forbidden)
		return -EACCES;

	return ci->clock_config_set(ph, clk_id, CLK_STATE_DISABLE,
				    NULL_OEM_TYPE, 0, atomic);
}

/* For SCMI clock v3.0 and onwards */
static int
scmi_clock_config_get_v2(const struct scmi_protocol_handle *ph, u32 clk_id,
			 enum scmi_clock_oem_config oem_type, u32 *attributes,
			 bool *enabled, u32 *oem_val, bool atomic)
{
	int ret;
	u32 flags;
	struct scmi_xfer *t;
	struct scmi_msg_clock_config_get *cfg;

	ret = ph->xops->xfer_get_init(ph, CLOCK_CONFIG_GET,
				      sizeof(*cfg), 0, &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = atomic;

	flags = FIELD_PREP(REGMASK_OEM_TYPE_GET, oem_type);

	cfg = t->tx.buf;
	cfg->id = cpu_to_le32(clk_id);
	cfg->flags = cpu_to_le32(flags);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		struct scmi_msg_resp_clock_config_get *resp = t->rx.buf;

		if (attributes)
			*attributes = le32_to_cpu(resp->attributes);

		if (enabled)
			*enabled = IS_CLK_ENABLED(resp->config);

		if (oem_val && oem_type)
			*oem_val = le32_to_cpu(resp->oem_config_val);
	}

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int
scmi_clock_config_get(const struct scmi_protocol_handle *ph, u32 clk_id,
		      enum scmi_clock_oem_config oem_type, u32 *attributes,
		      bool *enabled, u32 *oem_val, bool atomic)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_resp_clock_attributes *resp;

	if (!enabled)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, CLOCK_ATTRIBUTES,
				      sizeof(clk_id), sizeof(*resp), &t);
	if (ret)
		return ret;

	t->hdr.poll_completion = atomic;
	put_unaligned_le32(clk_id, t->tx.buf);
	resp = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*enabled = IS_CLK_ENABLED(resp->attributes);

	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_clock_state_get(const struct scmi_protocol_handle *ph,
				u32 clk_id, bool *enabled, bool atomic)
{
	struct clock_info *ci = ph->get_priv(ph);

	return ci->clock_config_get(ph, clk_id, NULL_OEM_TYPE, NULL,
				    enabled, NULL, atomic);
}

static int scmi_clock_config_oem_set(const struct scmi_protocol_handle *ph,
				     u32 clk_id,
				     enum scmi_clock_oem_config oem_type,
				     u32 oem_val, bool atomic)
{
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (!clk->extended_config)
		return -EOPNOTSUPP;

	return ci->clock_config_set(ph, clk_id, CLK_STATE_UNCHANGED,
				    oem_type, oem_val, atomic);
}

static int scmi_clock_config_oem_get(const struct scmi_protocol_handle *ph,
				     u32 clk_id,
				     enum scmi_clock_oem_config oem_type,
				     u32 *oem_val, u32 *attributes, bool atomic)
{
	struct clock_info *ci = ph->get_priv(ph);
	struct scmi_clock_info *clk;

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (!clk->extended_config)
		return -EOPNOTSUPP;

	return ci->clock_config_get(ph, clk_id, oem_type, attributes,
				    NULL, oem_val, atomic);
}

static int scmi_clock_count_get(const struct scmi_protocol_handle *ph)
{
	struct clock_info *ci = ph->get_priv(ph);

	return ci->num_clocks;
}

static const struct scmi_clock_info *
scmi_clock_info_get(const struct scmi_protocol_handle *ph, u32 clk_id)
{
	struct scmi_clock_info *clk;
	struct clock_info *ci = ph->get_priv(ph);

	clk = scmi_clock_domain_lookup(ci, clk_id);
	if (IS_ERR(clk))
		return NULL;

	if (!clk->name[0])
		return NULL;

	return clk;
}

static const struct scmi_clk_proto_ops clk_proto_ops = {
	.count_get = scmi_clock_count_get,
	.info_get = scmi_clock_info_get,
	.rate_get = scmi_clock_rate_get,
	.rate_set = scmi_clock_rate_set,
	.determine_rate = scmi_clock_determine_rate,
	.all_rates_get = scmi_clock_all_rates_get,
	.enable = scmi_clock_enable,
	.disable = scmi_clock_disable,
	.state_get = scmi_clock_state_get,
	.config_oem_get = scmi_clock_config_oem_get,
	.config_oem_set = scmi_clock_config_oem_set,
	.parent_set = scmi_clock_set_parent,
	.parent_get = scmi_clock_get_parent,
};

static bool scmi_clk_notify_supported(const struct scmi_protocol_handle *ph,
				      u8 evt_id, u32 src_id)
{
	bool supported;
	struct scmi_clock_info *clk;
	struct clock_info *ci = ph->get_priv(ph);

	if (evt_id >= ARRAY_SIZE(evt_2_cmd))
		return false;

	clk = scmi_clock_domain_lookup(ci, src_id);
	if (IS_ERR(clk))
		return false;

	if (evt_id == SCMI_EVENT_CLOCK_RATE_CHANGED)
		supported = clk->rate_changed_notifications;
	else
		supported = clk->rate_change_requested_notifications;

	return supported;
}

static int scmi_clk_rate_notify(const struct scmi_protocol_handle *ph,
				u32 clk_id, int message_id, bool enable)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_clock_rate_notify *notify;

	ret = ph->xops->xfer_get_init(ph, message_id, sizeof(*notify), 0, &t);
	if (ret)
		return ret;

	notify = t->tx.buf;
	notify->clk_id = cpu_to_le32(clk_id);
	notify->notify_enable = enable ? cpu_to_le32(BIT(0)) : 0;

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_clk_set_notify_enabled(const struct scmi_protocol_handle *ph,
				       u8 evt_id, u32 src_id, bool enable)
{
	int ret, cmd_id;

	if (evt_id >= ARRAY_SIZE(evt_2_cmd))
		return -EINVAL;

	cmd_id = evt_2_cmd[evt_id];
	ret = scmi_clk_rate_notify(ph, src_id, cmd_id, enable);
	if (ret)
		pr_debug("FAIL_ENABLED - evt[%X] dom[%d] - ret:%d\n",
			 evt_id, src_id, ret);

	return ret;
}

static void *scmi_clk_fill_custom_report(const struct scmi_protocol_handle *ph,
					 u8 evt_id, ktime_t timestamp,
					 const void *payld, size_t payld_sz,
					 void *report, u32 *src_id)
{
	const struct scmi_clock_rate_notify_payld *p = payld;
	struct scmi_clock_rate_notif_report *r = report;

	if (sizeof(*p) != payld_sz ||
	    (evt_id != SCMI_EVENT_CLOCK_RATE_CHANGED &&
	     evt_id != SCMI_EVENT_CLOCK_RATE_CHANGE_REQUESTED))
		return NULL;

	r->timestamp = timestamp;
	r->agent_id = le32_to_cpu(p->agent_id);
	r->clock_id = le32_to_cpu(p->clock_id);
	r->rate = get_unaligned_le64(&p->rate_low);
	*src_id = r->clock_id;

	return r;
}

static int scmi_clk_get_num_sources(const struct scmi_protocol_handle *ph)
{
	struct clock_info *ci = ph->get_priv(ph);

	if (!ci)
		return -EINVAL;

	return ci->num_clocks;
}

static const struct scmi_event clk_events[] = {
	{
		.id = SCMI_EVENT_CLOCK_RATE_CHANGED,
		.max_payld_sz = sizeof(struct scmi_clock_rate_notify_payld),
		.max_report_sz = sizeof(struct scmi_clock_rate_notif_report),
	},
	{
		.id = SCMI_EVENT_CLOCK_RATE_CHANGE_REQUESTED,
		.max_payld_sz = sizeof(struct scmi_clock_rate_notify_payld),
		.max_report_sz = sizeof(struct scmi_clock_rate_notif_report),
	},
};

static const struct scmi_event_ops clk_event_ops = {
	.is_notify_supported = scmi_clk_notify_supported,
	.get_num_sources = scmi_clk_get_num_sources,
	.set_notify_enabled = scmi_clk_set_notify_enabled,
	.fill_custom_report = scmi_clk_fill_custom_report,
};

static const struct scmi_protocol_events clk_protocol_events = {
	.queue_sz = SCMI_PROTO_QUEUE_SZ,
	.ops = &clk_event_ops,
	.evts = clk_events,
	.num_events = ARRAY_SIZE(clk_events),
};

static int scmi_clock_protocol_init(const struct scmi_protocol_handle *ph)
{
	int clkid, ret;
	struct clock_info *cinfo;

	dev_dbg(ph->dev, "Clock Version %d.%d\n",
		PROTOCOL_REV_MAJOR(ph->version), PROTOCOL_REV_MINOR(ph->version));

	cinfo = devm_kzalloc(ph->dev, sizeof(*cinfo), GFP_KERNEL);
	if (!cinfo)
		return -ENOMEM;

	ret = scmi_clock_protocol_attributes_get(ph, cinfo);
	if (ret)
		return ret;

	cinfo->clkds = devm_kcalloc(ph->dev, cinfo->num_clocks,
				    sizeof(*cinfo->clkds), GFP_KERNEL);
	if (!cinfo->clkds)
		return -ENOMEM;

	for (clkid = 0; clkid < cinfo->num_clocks; clkid++) {
		cinfo->clkds[clkid].id = clkid;
		ret = scmi_clock_attributes_get(ph, clkid, cinfo);
		if (!ret)
			scmi_clock_describe_rates_get(ph, clkid, cinfo);
	}

	if (PROTOCOL_REV_MAJOR(ph->version) >= 0x3) {
		cinfo->clock_config_set = scmi_clock_config_set_v2;
		cinfo->clock_config_get = scmi_clock_config_get_v2;
	} else {
		cinfo->clock_config_set = scmi_clock_config_set;
		cinfo->clock_config_get = scmi_clock_config_get;
	}

	return ph->set_priv(ph, cinfo);
}

static const struct scmi_protocol scmi_clock = {
	.id = SCMI_PROTOCOL_CLOCK,
	.owner = THIS_MODULE,
	.instance_init = &scmi_clock_protocol_init,
	.ops = &clk_proto_ops,
	.events = &clk_protocol_events,
	.supported_version = SCMI_PROTOCOL_SUPPORTED_VERSION,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(clock, scmi_clock)
