// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RISC-V RPMI Based Device Power Driver through SBI MPXY
 *
 * Copyright (C) 2026 Shanghai StarFive Technology Co., Ltd.
 *
 * Implements a Device Power driver on top of SBI RPMI Message Proxy Extension (MPXY)
 *
 * Each SBI MPXY Device Power instance is associated, through the means of a proper DT
 * entry description, to a specific Transport ID.
 */

#include <linux/bitfield.h>
#include <linux/mailbox/riscv-rpmi-message.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>

#define RPMI_PM_DOMAIN_NAME_LEN		16

/* power state for device power domains */
#define RPMI_POWER_STATE_CONTEXT_MASK		GENMASK(16, 16)
#define RPMI_POWER_STATE_CONTEXT_PRESERVED	0
#define RPMI_POWER_STATE_CONTEXT_LOST		1
#define RPMI_POWER_STATE_VALUE_MASK		GENMASK(15, 0)
#define RPMI_POWER_STATE_VALUE_ON		0
#define RPMI_POWER_STATE_VALUE_OFF		3

#define RPMI_POWER_STATE_PARAM(context_type, state_value) \
	((FIELD_PREP(RPMI_POWER_STATE_CONTEXT_MASK, context_type)) | \
	(FIELD_PREP(RPMI_POWER_STATE_VALUE_MASK, state_value)))

#define RPMI_POWER_STATE_GENERIC_ON  \
	RPMI_POWER_STATE_PARAM(RPMI_POWER_STATE_CONTEXT_PRESERVED, \
				   RPMI_POWER_STATE_VALUE_ON)

#define RPMI_POWER_STATE_GENERIC_OFF  \
	RPMI_POWER_STATE_PARAM(RPMI_POWER_STATE_CONTEXT_PRESERVED, \
				   RPMI_POWER_STATE_VALUE_OFF)

/**
 * struct rpmi_ctx - RPMI transport context shared by all power domains
 *
 * @chan: mailbox channel of the underlying SBI MPXY transport
 * @client: mailbox client bound to @chan
 * @lock: serialises RPMI transactions on @chan
 */
struct rpmi_ctx {
	struct mbox_chan *chan;
	struct mbox_client client;
	/* serialises RPMI transactions on @chan */
	struct mutex lock;
};

/**
 * struct rpmi_device_power_domain - describe one available MPXY Device Power Domain
 *
 * @id: the power domain ID as advertised by PuC
 * @transition_latency: worst case transition latency of power domain from one state
 *			to another
 * @dev: device associated with this power domain
 * @mpxy_ctx: RPMI transport context used to reach the PuC
 * @genpd: generic power domain registered with the genpd core
 * @name: device power domain name assigned by PuC
 */
struct rpmi_device_power_domain {
	u32 id;
	u32 transition_latency;
	struct device *dev;
	struct rpmi_ctx *mpxy_ctx;
	struct generic_pm_domain genpd;
	char name[RPMI_PM_DOMAIN_NAME_LEN];
};

#define to_rpmi_pd(gpd) container_of(gpd, struct rpmi_device_power_domain, genpd)

/* Service: GET_POWER_DOMAINS */
struct rpmi_pm_get_num_domain_rx {
	__le32 status;
	__le32 num_domains;
};

/* Service: GET_POWER_DOMAIN_ATTRS */
struct rpmi_pm_get_domain_attrs_tx {
	__le32 domain_id;
};

/* pm domain attributes response data */
struct rpmi_pm_get_domain_attrs_rx {
	__le32 status;
	__le32 flags;
	__le32 transition_latency;
	char name[RPMI_PM_DOMAIN_NAME_LEN];
};

/* Service: SET_POWER_DOMAIN_STATE */
struct rpmi_pm_set_power_state_tx {
	__le32 domain_id;
	__le32 power_state;
};

struct rpmi_pm_set_power_state_rx {
	__le32 status;
};

/* Service: GET_POWER_DOMAIN_STATE */
struct rpmi_pm_get_power_state_tx {
	__le32 domain_id;
};

struct rpmi_pm_get_power_state_rx {
	__le32 status;
	__le32 power_state;
};

static int rpmi_pm_send_message(struct rpmi_ctx *mpxy_ctx,
				struct rpmi_mbox_message *msg)
{
	guard(mutex)(&mpxy_ctx->lock);

	return rpmi_mbox_send_message(mpxy_ctx->chan, msg);
}

static int rpmi_power_state_get(struct rpmi_device_power_domain *mpxy_pm_domain,
				u32 domain_id, u32 *state)
{
	struct rpmi_pm_get_power_state_rx rx = { };
	struct rpmi_pm_get_power_state_tx tx;
	struct rpmi_mbox_message msg;
	int ret;

	tx.domain_id = cpu_to_le32(domain_id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_DP_SRV_GET_STATE,
					  &tx, sizeof(tx), &rx, sizeof(rx));

	ret = rpmi_pm_send_message(mpxy_pm_domain->mpxy_ctx, &msg);
	if (ret)
		return ret;

	if (rx.status)
		return rpmi_to_linux_error(le32_to_cpu(rx.status));

	if (msg.data.out_response_len < sizeof(rx))
		return -EPROTO;

	*state = le32_to_cpu(rx.power_state);

	return 0;
}

static int rpmi_power_state_set(struct rpmi_device_power_domain *mpxy_pm_domain,
				u32 domain_id, u32 state)
{
	struct rpmi_pm_set_power_state_rx rx = { };
	struct rpmi_pm_set_power_state_tx tx;
	struct rpmi_mbox_message msg;
	int ret;

	tx.domain_id = cpu_to_le32(domain_id);
	tx.power_state = cpu_to_le32(state);

	rpmi_mbox_init_send_with_response(&msg, RPMI_DP_SRV_SET_STATE,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_pm_send_message(mpxy_pm_domain->mpxy_ctx, &msg);
	if (ret)
		return ret;

	if (rx.status)
		return rpmi_to_linux_error(le32_to_cpu(rx.status));

	if (msg.data.out_response_len < sizeof(rx))
		return -EPROTO;

	return 0;
}

static int rpmi_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct rpmi_device_power_domain *mpxy_pm_domain;
	u32 state, ret_state, domain_id;
	int ret;

	if (power_on)
		state = RPMI_POWER_STATE_GENERIC_ON;
	else
		state = RPMI_POWER_STATE_GENERIC_OFF;

	mpxy_pm_domain = to_rpmi_pd(domain);
	domain_id = mpxy_pm_domain->id;

	ret = rpmi_power_state_set(mpxy_pm_domain, domain_id, state);
	if (ret)
		return ret;

	ret = rpmi_power_state_get(mpxy_pm_domain, domain_id, &ret_state);
	if (ret)
		return ret;

	if (FIELD_GET(RPMI_POWER_STATE_VALUE_MASK, ret_state) !=
	    FIELD_GET(RPMI_POWER_STATE_VALUE_MASK, state))
		return -EIO;

	return ret;
}

static int rpmi_pd_power_on(struct generic_pm_domain *domain)
{
	return rpmi_pd_power(domain, true);
}

static int rpmi_pd_power_off(struct generic_pm_domain *domain)
{
	return rpmi_pd_power(domain, false);
}

static int rpmi_pm_get_num_domains(struct rpmi_ctx *mpxy_ctx, u32 *domain)
{
	struct rpmi_pm_get_num_domain_rx rx = { };
	struct rpmi_mbox_message msg;
	int ret;

	rpmi_mbox_init_send_with_response(&msg, RPMI_DP_SRV_GET_NUM_DOMAINS,
					  NULL, 0, &rx, sizeof(rx));
	ret = rpmi_pm_send_message(mpxy_ctx, &msg);
	if (ret)
		return ret;

	if (rx.status)
		return rpmi_to_linux_error(le32_to_cpu(rx.status));

	if (msg.data.out_response_len < sizeof(rx))
		return -EPROTO;

	*domain = le32_to_cpu(rx.num_domains);

	return 0;
}

/* obtain the MPXY device power domain attributes */
static int rpmi_device_power_get_attrs(u32 domain_id,
				       struct rpmi_device_power_domain *mpxy_pm_domain)
{
	struct rpmi_pm_get_domain_attrs_rx rx = { };
	struct rpmi_pm_get_domain_attrs_tx tx;
	struct rpmi_mbox_message msg;
	int ret;

	tx.domain_id = cpu_to_le32(domain_id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_DP_SRV_GET_ATTRS,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_pm_send_message(mpxy_pm_domain->mpxy_ctx, &msg);
	if (ret)
		return ret;

	if (rx.status)
		return rpmi_to_linux_error(le32_to_cpu(rx.status));

	if (msg.data.out_response_len < sizeof(rx))
		return -EPROTO;

	mpxy_pm_domain->transition_latency = le32_to_cpu(rx.transition_latency);
	strscpy(mpxy_pm_domain->name, rx.name, sizeof(mpxy_pm_domain->name));

	return 0;
}

static int rpmi_pm_attr_setup(struct device *dev, struct rpmi_ctx *mpxy_ctx)
{
	struct rpmi_mbox_message msg;
	int ret;

	/* Validate RPMI specification version */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SPEC_VERSION);
	ret = rpmi_pm_send_message(mpxy_ctx, &msg);
	if (ret) {
		dev_dbg(dev, "Failed to get spec version\n");
		return ret;
	}

	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		dev_dbg(dev,
			"msg protocol version mismatch, expected 0x%x, found 0x%x\n",
			RPMI_MKVER(1, 0), msg.attr.value);
		return -EINVAL;
	}

	/* Validate device power service group ID */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_ID);
	ret = rpmi_pm_send_message(mpxy_ctx, &msg);
	if (ret) {
		dev_dbg(dev, "Failed to get service group ID\n");
		return ret;
	}

	if (msg.attr.value != RPMI_SRVGRP_DEVICE_POWER) {
		dev_dbg(dev,
			"service group match failed, expected 0x%x, found 0x%x\n",
			RPMI_SRVGRP_DEVICE_POWER, msg.attr.value);
		return -EINVAL;
	}

	/* Validate device power service group version */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_VERSION);
	ret = rpmi_pm_send_message(mpxy_ctx, &msg);
	if (ret) {
		dev_dbg(dev, "Failed to get service group version\n");
		return ret;
	}

	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		dev_dbg(dev,
			"service group version failed, expected 0x%x, found 0x%x\n",
			RPMI_MKVER(1, 0), msg.attr.value);
		return -EINVAL;
	}

	return 0;
}

static void rpmi_pm_domain_mbox_chan_release(void *data)
{
	mbox_free_channel((struct mbox_chan *)data);
}

static int rpmi_pm_domain_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rpmi_device_power_domain *mpxy_pd;
	struct genpd_onecell_data *mpxy_pd_data;
	struct generic_pm_domain **domains;
	struct device *dev = &pdev->dev;
	struct rpmi_ctx *mpxy_ctx;
	u32 num_domains = 0;
	int ret;
	u32 i;

	mpxy_ctx = devm_kzalloc(&pdev->dev, sizeof(*mpxy_ctx), GFP_KERNEL);
	if (!mpxy_ctx)
		return -ENOMEM;

	ret = devm_mutex_init(dev, &mpxy_ctx->lock);
	if (ret)
		return ret;

	/* Setup mailbox client */
	mpxy_ctx->client.dev		= dev;
	mpxy_ctx->client.rx_callback	= NULL;
	mpxy_ctx->client.tx_block	= false;
	mpxy_ctx->client.knows_txdone	= true;
	mpxy_ctx->client.tx_tout	= 0;

	/* Request mailbox channel */
	mpxy_ctx->chan = mbox_request_channel(&mpxy_ctx->client, 0);
	if (IS_ERR(mpxy_ctx->chan))
		return PTR_ERR(mpxy_ctx->chan);

	ret = devm_add_action_or_reset(dev, rpmi_pm_domain_mbox_chan_release,
				       mpxy_ctx->chan);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to add rpmi mbox channel cleanup\n");

	ret = rpmi_pm_attr_setup(dev, mpxy_ctx);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to verify RPMI attribute\n");

	/* Get number of device power domain */
	ret = rpmi_pm_get_num_domains(mpxy_ctx, &num_domains);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get number of pm domains\n");

	if (!num_domains)
		return dev_err_probe(dev, -EINVAL, "No PM domains found!\n");

	dev_info(&pdev->dev, "%u MPXY PM domains are found\n", num_domains);

	mpxy_pd = devm_kcalloc(&pdev->dev, num_domains, sizeof(*mpxy_pd), GFP_KERNEL);
	if (!mpxy_pd)
		return -ENOMEM;

	domains = devm_kcalloc(&pdev->dev, num_domains, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	mpxy_pd_data = devm_kzalloc(&pdev->dev, sizeof(*mpxy_pd_data), GFP_KERNEL);
	if (!mpxy_pd_data)
		return -ENOMEM;

	for (i = 0; i < num_domains; i++, mpxy_pd++) {
		u32 state;

		mpxy_pd->dev = &pdev->dev;
		mpxy_pd->mpxy_ctx = mpxy_ctx;
		mpxy_pd->id = i;

		ret = rpmi_device_power_get_attrs(i, mpxy_pd);
		if (ret) {
			dev_warn(mpxy_pd->dev,
				 "power domain %u initialization failed\n",
				 mpxy_pd->id);
			domains[i] = NULL;
			continue;
		}

		ret = rpmi_power_state_get(mpxy_pd, i, &state);
		if (ret) {
			dev_warn(mpxy_pd->dev,
				 "failed to get state for power domain %u\n",
				 mpxy_pd->id);
			domains[i] = NULL;
			continue;
		}

		state = FIELD_GET(RPMI_POWER_STATE_VALUE_MASK, state);
		if (state != RPMI_POWER_STATE_VALUE_OFF &&
		    state != RPMI_POWER_STATE_VALUE_ON) {
			dev_warn(mpxy_pd->dev,
				 "unsupported state 0x%x for power domain %u\n",
				 state, mpxy_pd->id);
			domains[i] = NULL;
			continue;
		}

		mpxy_pd->genpd.name = mpxy_pd->name;
		mpxy_pd->genpd.power_off = rpmi_pd_power_off;
		mpxy_pd->genpd.power_on = rpmi_pd_power_on;
		/*
		 * Domain names come from the PuC and are not guaranteed to be
		 * unique or non-empty, so let genpd derive unique device names
		 * from them rather than using them verbatim.
		 */
		mpxy_pd->genpd.flags = GENPD_FLAG_DEV_NAME_FW;

		ret = pm_genpd_init(&mpxy_pd->genpd, NULL,
				    state == RPMI_POWER_STATE_VALUE_OFF);
		if (ret) {
			dev_warn(mpxy_pd->dev,
				 "failed to init power domain %u\n",
				 mpxy_pd->id);
			domains[i] = NULL;
			continue;
		}

		domains[i] = &mpxy_pd->genpd;
	}

	mpxy_pd_data->domains = domains;
	mpxy_pd_data->num_domains = num_domains;

	ret = of_genpd_add_provider_onecell(np, mpxy_pd_data);
	if (ret) {
		while (i--) {
			if (domains[i])
				pm_genpd_remove(domains[i]);
		}

		return dev_err_probe(dev, ret, "failed to add genpd provider\n");
	}

	return 0;
}

static const struct of_device_id rpmi_pm_domain_of_match[] = {
	{ .compatible = "riscv,rpmi-device-power" },
	{},
};

static struct platform_driver rpmi_pm_domain_platdrv = {
	.driver = {
		.name = "riscv-rpmi-device-power",
		.of_match_table = rpmi_pm_domain_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rpmi_pm_domain_probe,
};

builtin_platform_driver(rpmi_pm_domain_platdrv);

MODULE_AUTHOR("Joshua Yeong <joshua.yeong@starfivetech.com>");
MODULE_DESCRIPTION("Device Power Driver based on RPMI message protocol");
MODULE_LICENSE("GPL");
