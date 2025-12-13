// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V MPXY Based Clock Driver
 *
 * Copyright (C) 2025 Ventana Micro Systems Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/riscv-rpmi-message.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/wordpart.h>

#define RPMI_CLK_DISCRETE_MAX_NUM_RATES		16
#define RPMI_CLK_NAME_LEN			16

#define to_rpmi_clk(clk)	container_of(clk, struct rpmi_clk, hw)

enum rpmi_clk_config {
	RPMI_CLK_DISABLE = 0,
	RPMI_CLK_ENABLE = 1,
	RPMI_CLK_CONFIG_MAX_IDX
};

#define RPMI_CLK_TYPE_MASK			GENMASK(1, 0)
enum rpmi_clk_type {
	RPMI_CLK_DISCRETE = 0,
	RPMI_CLK_LINEAR = 1,
	RPMI_CLK_TYPE_MAX_IDX
};

struct rpmi_clk_context {
	struct device *dev;
	struct mbox_chan *chan;
	struct mbox_client client;
	u32 max_msg_data_size;
};

/*
 * rpmi_clk_rates represents the rates format
 * as specified by the RPMI specification.
 * No other data format (e.g., struct linear_range)
 * is required to avoid to and from conversion.
 */
union rpmi_clk_rates {
	u64 discrete[RPMI_CLK_DISCRETE_MAX_NUM_RATES];
	struct {
		u64 min;
		u64 max;
		u64 step;
	} linear;
};

struct rpmi_clk {
	struct rpmi_clk_context *context;
	u32 id;
	u32 num_rates;
	u32 transition_latency;
	enum rpmi_clk_type type;
	union rpmi_clk_rates *rates;
	char name[RPMI_CLK_NAME_LEN];
	struct clk_hw hw;
};

struct rpmi_clk_rate_discrete {
	__le32 lo;
	__le32 hi;
};

struct rpmi_clk_rate_linear {
	__le32 min_lo;
	__le32 min_hi;
	__le32 max_lo;
	__le32 max_hi;
	__le32 step_lo;
	__le32 step_hi;
};

struct rpmi_get_num_clocks_rx {
	__le32 status;
	__le32 num_clocks;
};

struct rpmi_get_attrs_tx {
	__le32 clkid;
};

struct rpmi_get_attrs_rx {
	__le32 status;
	__le32 flags;
	__le32 num_rates;
	__le32 transition_latency;
	char name[RPMI_CLK_NAME_LEN];
};

struct rpmi_get_supp_rates_tx {
	__le32 clkid;
	__le32 clk_rate_idx;
};

struct rpmi_get_supp_rates_rx {
	__le32 status;
	__le32 flags;
	__le32 remaining;
	__le32 returned;
	__le32 rates[];
};

struct rpmi_get_rate_tx {
	__le32 clkid;
};

struct rpmi_get_rate_rx {
	__le32 status;
	__le32 lo;
	__le32 hi;
};

struct rpmi_set_rate_tx {
	__le32 clkid;
	__le32 flags;
	__le32 lo;
	__le32 hi;
};

struct rpmi_set_rate_rx {
	__le32 status;
};

struct rpmi_set_config_tx {
	__le32 clkid;
	__le32 config;
};

struct rpmi_set_config_rx {
	__le32 status;
};

static inline u64 rpmi_clkrate_u64(u32 __hi, u32 __lo)
{
	return (((u64)(__hi) << 32) | (u32)(__lo));
}

static u32 rpmi_clk_get_num_clocks(struct rpmi_clk_context *context)
{
	struct rpmi_get_num_clocks_rx rx, *resp;
	struct rpmi_mbox_message msg;
	int ret;

	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_GET_NUM_CLOCKS,
					  NULL, 0, &rx, sizeof(rx));

	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return 0;

	resp = rpmi_mbox_get_msg_response(&msg);
	if (!resp || resp->status)
		return 0;

	return le32_to_cpu(resp->num_clocks);
}

static int rpmi_clk_get_attrs(u32 clkid, struct rpmi_clk *rpmi_clk)
{
	struct rpmi_clk_context *context = rpmi_clk->context;
	struct rpmi_mbox_message msg;
	struct rpmi_get_attrs_tx tx;
	struct rpmi_get_attrs_rx rx, *resp;
	u8 format;
	int ret;

	tx.clkid = cpu_to_le32(clkid);
	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_GET_ATTRIBUTES,
					  &tx, sizeof(tx), &rx, sizeof(rx));

	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	resp = rpmi_mbox_get_msg_response(&msg);
	if (!resp)
		return -EINVAL;
	if (resp->status)
		return rpmi_to_linux_error(le32_to_cpu(resp->status));

	rpmi_clk->id = clkid;
	rpmi_clk->num_rates = le32_to_cpu(resp->num_rates);
	rpmi_clk->transition_latency = le32_to_cpu(resp->transition_latency);
	strscpy(rpmi_clk->name, resp->name, RPMI_CLK_NAME_LEN);

	format = le32_to_cpu(resp->flags) & RPMI_CLK_TYPE_MASK;
	if (format >= RPMI_CLK_TYPE_MAX_IDX)
		return -EINVAL;

	rpmi_clk->type = format;

	return 0;
}

static int rpmi_clk_get_supported_rates(u32 clkid, struct rpmi_clk *rpmi_clk)
{
	struct rpmi_clk_context *context = rpmi_clk->context;
	struct rpmi_clk_rate_discrete *rate_discrete;
	struct rpmi_clk_rate_linear *rate_linear;
	struct rpmi_get_supp_rates_tx tx;
	struct rpmi_get_supp_rates_rx *resp;
	struct rpmi_mbox_message msg;
	size_t clk_rate_idx;
	int ret, rateidx, j;

	tx.clkid = cpu_to_le32(clkid);
	tx.clk_rate_idx = 0;

	/*
	 * Make sure we allocate rx buffer sufficient to be accommodate all
	 * the rates sent in one RPMI message.
	 */
	struct rpmi_get_supp_rates_rx *rx __free(kfree) =
					kzalloc(context->max_msg_data_size, GFP_KERNEL);
	if (!rx)
		return -ENOMEM;

	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_GET_SUPPORTED_RATES,
					  &tx, sizeof(tx), rx, context->max_msg_data_size);

	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	resp = rpmi_mbox_get_msg_response(&msg);
	if (!resp)
		return -EINVAL;
	if (resp->status)
		return rpmi_to_linux_error(le32_to_cpu(resp->status));
	if (!le32_to_cpu(resp->returned))
		return -EINVAL;

	if (rpmi_clk->type == RPMI_CLK_DISCRETE) {
		rate_discrete = (struct rpmi_clk_rate_discrete *)resp->rates;

		for (rateidx = 0; rateidx < le32_to_cpu(resp->returned); rateidx++) {
			rpmi_clk->rates->discrete[rateidx] =
				rpmi_clkrate_u64(le32_to_cpu(rate_discrete[rateidx].hi),
						 le32_to_cpu(rate_discrete[rateidx].lo));
		}

		/*
		 * Keep sending the request message until all
		 * the rates are received.
		 */
		clk_rate_idx = 0;
		while (le32_to_cpu(resp->remaining)) {
			clk_rate_idx += le32_to_cpu(resp->returned);
			tx.clk_rate_idx = cpu_to_le32(clk_rate_idx);

			rpmi_mbox_init_send_with_response(&msg,
							  RPMI_CLK_SRV_GET_SUPPORTED_RATES,
							  &tx, sizeof(tx),
							  rx, context->max_msg_data_size);

			ret = rpmi_mbox_send_message(context->chan, &msg);
			if (ret)
				return ret;

			resp = rpmi_mbox_get_msg_response(&msg);
			if (!resp)
				return -EINVAL;
			if (resp->status)
				return rpmi_to_linux_error(le32_to_cpu(resp->status));
			if (!le32_to_cpu(resp->returned))
				return -EINVAL;

			for (j = 0; j < le32_to_cpu(resp->returned); j++) {
				if (rateidx >= clk_rate_idx + le32_to_cpu(resp->returned))
					break;
				rpmi_clk->rates->discrete[rateidx++] =
					rpmi_clkrate_u64(le32_to_cpu(rate_discrete[j].hi),
							 le32_to_cpu(rate_discrete[j].lo));
			}
		}
	} else if (rpmi_clk->type == RPMI_CLK_LINEAR) {
		rate_linear = (struct rpmi_clk_rate_linear *)resp->rates;

		rpmi_clk->rates->linear.min = rpmi_clkrate_u64(le32_to_cpu(rate_linear->min_hi),
							       le32_to_cpu(rate_linear->min_lo));
		rpmi_clk->rates->linear.max = rpmi_clkrate_u64(le32_to_cpu(rate_linear->max_hi),
							       le32_to_cpu(rate_linear->max_lo));
		rpmi_clk->rates->linear.step = rpmi_clkrate_u64(le32_to_cpu(rate_linear->step_hi),
								le32_to_cpu(rate_linear->step_lo));
	}

	return 0;
}

static unsigned long rpmi_clk_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct rpmi_clk *rpmi_clk = to_rpmi_clk(hw);
	struct rpmi_clk_context *context = rpmi_clk->context;
	struct rpmi_mbox_message msg;
	struct rpmi_get_rate_tx tx;
	struct rpmi_get_rate_rx rx, *resp;
	int ret;

	tx.clkid = cpu_to_le32(rpmi_clk->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_GET_RATE,
					  &tx, sizeof(tx), &rx, sizeof(rx));

	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	resp = rpmi_mbox_get_msg_response(&msg);
	if (!resp)
		return -EINVAL;
	if (resp->status)
		return rpmi_to_linux_error(le32_to_cpu(resp->status));

	return rpmi_clkrate_u64(le32_to_cpu(resp->hi), le32_to_cpu(resp->lo));
}

static int rpmi_clk_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct rpmi_clk *rpmi_clk = to_rpmi_clk(hw);
	u64 fmin, fmax, ftmp;

	/*
	 * Keep the requested rate if the clock format
	 * is of discrete type. Let the platform which
	 * is actually controlling the clock handle that.
	 */
	if (rpmi_clk->type == RPMI_CLK_DISCRETE)
		return 0;

	fmin = rpmi_clk->rates->linear.min;
	fmax = rpmi_clk->rates->linear.max;

	if (req->rate <= fmin) {
		req->rate = fmin;
		return 0;
	} else if (req->rate >= fmax) {
		req->rate = fmax;
		return 0;
	}

	ftmp = req->rate - fmin;
	ftmp += rpmi_clk->rates->linear.step - 1;
	do_div(ftmp, rpmi_clk->rates->linear.step);

	req->rate = ftmp * rpmi_clk->rates->linear.step + fmin;

	return 0;
}

static int rpmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct rpmi_clk *rpmi_clk = to_rpmi_clk(hw);
	struct rpmi_clk_context *context = rpmi_clk->context;
	struct rpmi_mbox_message msg;
	struct rpmi_set_rate_tx tx;
	struct rpmi_set_rate_rx rx, *resp;
	int ret;

	tx.clkid = cpu_to_le32(rpmi_clk->id);
	tx.lo = cpu_to_le32(lower_32_bits(rate));
	tx.hi = cpu_to_le32(upper_32_bits(rate));

	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_SET_RATE,
					  &tx, sizeof(tx), &rx, sizeof(rx));

	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	resp = rpmi_mbox_get_msg_response(&msg);
	if (!resp)
		return -EINVAL;
	if (resp->status)
		return rpmi_to_linux_error(le32_to_cpu(resp->status));

	return 0;
}

static int rpmi_clk_enable(struct clk_hw *hw)
{
	struct rpmi_clk *rpmi_clk = to_rpmi_clk(hw);
	struct rpmi_clk_context *context = rpmi_clk->context;
	struct rpmi_mbox_message msg;
	struct rpmi_set_config_tx tx;
	struct rpmi_set_config_rx rx, *resp;
	int ret;

	tx.config = cpu_to_le32(RPMI_CLK_ENABLE);
	tx.clkid = cpu_to_le32(rpmi_clk->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_SET_CONFIG,
					  &tx, sizeof(tx), &rx, sizeof(rx));

	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	resp = rpmi_mbox_get_msg_response(&msg);
	if (!resp)
		return -EINVAL;
	if (resp->status)
		return rpmi_to_linux_error(le32_to_cpu(resp->status));

	return 0;
}

static void rpmi_clk_disable(struct clk_hw *hw)
{
	struct rpmi_clk *rpmi_clk = to_rpmi_clk(hw);
	struct rpmi_clk_context *context = rpmi_clk->context;
	struct rpmi_mbox_message msg;
	struct rpmi_set_config_tx tx;
	struct rpmi_set_config_rx rx;

	tx.config = cpu_to_le32(RPMI_CLK_DISABLE);
	tx.clkid = cpu_to_le32(rpmi_clk->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_CLK_SRV_SET_CONFIG,
					  &tx, sizeof(tx), &rx, sizeof(rx));

	rpmi_mbox_send_message(context->chan, &msg);
}

static const struct clk_ops rpmi_clk_ops = {
	.recalc_rate = rpmi_clk_recalc_rate,
	.determine_rate = rpmi_clk_determine_rate,
	.set_rate = rpmi_clk_set_rate,
	.prepare = rpmi_clk_enable,
	.unprepare = rpmi_clk_disable,
};

static struct clk_hw *rpmi_clk_enumerate(struct rpmi_clk_context *context, u32 clkid)
{
	struct device *dev = context->dev;
	unsigned long min_rate, max_rate;
	union rpmi_clk_rates *rates;
	struct rpmi_clk *rpmi_clk;
	struct clk_init_data init = {};
	struct clk_hw *clk_hw;
	int ret;

	rates = devm_kzalloc(dev, sizeof(*rates), GFP_KERNEL);
	if (!rates)
		return ERR_PTR(-ENOMEM);

	rpmi_clk = devm_kzalloc(dev, sizeof(*rpmi_clk), GFP_KERNEL);
	if (!rpmi_clk)
		return ERR_PTR(-ENOMEM);

	rpmi_clk->context = context;
	rpmi_clk->rates = rates;

	ret = rpmi_clk_get_attrs(clkid, rpmi_clk);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
					 "Failed to get clk-%u attributes\n",
					 clkid);

	ret = rpmi_clk_get_supported_rates(clkid, rpmi_clk);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
					 "Get supported rates failed for clk-%u\n",
					 clkid);

	init.flags = CLK_GET_RATE_NOCACHE;
	init.num_parents = 0;
	init.ops = &rpmi_clk_ops;
	init.name = rpmi_clk->name;
	clk_hw = &rpmi_clk->hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(dev, clk_hw);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
					 "Unable to register clk-%u\n",
					 clkid);

	if (rpmi_clk->type == RPMI_CLK_DISCRETE) {
		min_rate = rpmi_clk->rates->discrete[0];
		max_rate = rpmi_clk->rates->discrete[rpmi_clk->num_rates -  1];
	} else {
		min_rate = rpmi_clk->rates->linear.min;
		max_rate = rpmi_clk->rates->linear.max;
	}

	clk_hw_set_rate_range(clk_hw, min_rate, max_rate);

	return clk_hw;
}

static void rpmi_clk_mbox_chan_release(void *data)
{
	struct mbox_chan *chan = data;

	mbox_free_channel(chan);
}

static int rpmi_clk_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int num_clocks, i;
	struct clk_hw_onecell_data *clk_data;
	struct rpmi_clk_context *context;
	struct rpmi_mbox_message msg;
	struct clk_hw *hw_ptr;
	struct device *dev = &pdev->dev;

	context = devm_kzalloc(dev, sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;
	context->dev = dev;
	platform_set_drvdata(pdev, context);

	context->client.dev		= context->dev;
	context->client.rx_callback	= NULL;
	context->client.tx_block	= false;
	context->client.knows_txdone	= true;
	context->client.tx_tout		= 0;

	context->chan = mbox_request_channel(&context->client, 0);
	if (IS_ERR(context->chan))
		return PTR_ERR(context->chan);

	ret = devm_add_action_or_reset(dev, rpmi_clk_mbox_chan_release, context->chan);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add rpmi mbox channel cleanup\n");

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SPEC_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get spec version\n");
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		return dev_err_probe(dev, -EINVAL,
				     "msg protocol version mismatch, expected 0x%x, found 0x%x\n",
				     RPMI_MKVER(1, 0), msg.attr.value);
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_ID);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get service group ID\n");
	if (msg.attr.value != RPMI_SRVGRP_CLOCK) {
		return dev_err_probe(dev, -EINVAL,
				     "service group match failed, expected 0x%x, found 0x%x\n",
				     RPMI_SRVGRP_CLOCK, msg.attr.value);
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get service group version\n");
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		return dev_err_probe(dev, -EINVAL,
				     "service group version failed, expected 0x%x, found 0x%x\n",
				     RPMI_MKVER(1, 0), msg.attr.value);
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_MAX_MSG_DATA_SIZE);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get max message data size\n");

	context->max_msg_data_size = msg.attr.value;
	num_clocks = rpmi_clk_get_num_clocks(context);
	if (!num_clocks)
		return dev_err_probe(dev, -ENODEV, "No clocks found\n");

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, num_clocks),
				GFP_KERNEL);
	if (!clk_data)
		return dev_err_probe(dev, -ENOMEM, "No memory for clock data\n");
	clk_data->num = num_clocks;

	for (i = 0; i < clk_data->num; i++) {
		hw_ptr = rpmi_clk_enumerate(context, i);
		if (IS_ERR(hw_ptr)) {
			return dev_err_probe(dev, PTR_ERR(hw_ptr),
					     "Failed to register clk-%d\n", i);
		}
		clk_data->hws[i] = hw_ptr;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register clock HW provider\n");

	return 0;
}

static const struct of_device_id rpmi_clk_of_match[] = {
	{ .compatible = "riscv,rpmi-clock" },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmi_clk_of_match);

static struct platform_driver rpmi_clk_driver = {
	.driver = {
		.name = "riscv-rpmi-clock",
		.of_match_table = rpmi_clk_of_match,
	},
	.probe = rpmi_clk_probe,
};
module_platform_driver(rpmi_clk_driver);

MODULE_AUTHOR("Rahul Pathak <rpathak@ventanamicro.com>");
MODULE_DESCRIPTION("Clock Driver based on RPMI message protocol");
MODULE_LICENSE("GPL");
