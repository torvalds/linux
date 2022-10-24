// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/tcp.h>

#include "sparx5_tc.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"

struct sparx5_tc_flower_parse_usage {
	struct flow_cls_offload *fco;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	unsigned int used_keys;
};

static int sparx5_tc_flower_handler_ethaddr_usage(struct sparx5_tc_flower_parse_usage *st)
{
	enum vcap_key_field smac_key = VCAP_KF_L2_SMAC;
	enum vcap_key_field dmac_key = VCAP_KF_L2_DMAC;
	struct flow_match_eth_addrs match;
	struct vcap_u48_key smac, dmac;
	int err = 0;

	flow_rule_match_eth_addrs(st->frule, &match);

	if (!is_zero_ether_addr(match.mask->src)) {
		vcap_netbytes_copy(smac.value, match.key->src, ETH_ALEN);
		vcap_netbytes_copy(smac.mask, match.mask->src, ETH_ALEN);
		err = vcap_rule_add_key_u48(st->vrule, smac_key, &smac);
		if (err)
			goto out;
	}

	if (!is_zero_ether_addr(match.mask->dst)) {
		vcap_netbytes_copy(dmac.value, match.key->dst, ETH_ALEN);
		vcap_netbytes_copy(dmac.mask, match.mask->dst, ETH_ALEN);
		err = vcap_rule_add_key_u48(st->vrule, dmac_key, &dmac);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "eth_addr parse error");
	return err;
}

static int (*sparx5_tc_flower_usage_handlers[])(struct sparx5_tc_flower_parse_usage *st) = {
	/* More dissector handlers will be added here later */
	[FLOW_DISSECTOR_KEY_ETH_ADDRS] = sparx5_tc_flower_handler_ethaddr_usage,
};

static int sparx5_tc_use_dissectors(struct flow_cls_offload *fco,
				    struct vcap_admin *admin,
				    struct vcap_rule *vrule)
{
	struct sparx5_tc_flower_parse_usage state = {
		.fco = fco,
		.vrule = vrule,
	};
	int idx, err = 0;

	state.frule = flow_cls_offload_flow_rule(fco);
	for (idx = 0; idx < ARRAY_SIZE(sparx5_tc_flower_usage_handlers); ++idx) {
		if (!flow_rule_match_key(state.frule, idx))
			continue;
		if (!sparx5_tc_flower_usage_handlers[idx])
			continue;
		err = sparx5_tc_flower_usage_handlers[idx](&state);
		if (err)
			return err;
	}
	return err;
}

static int sparx5_tc_flower_replace(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct flow_action_entry *act;
	struct vcap_control *vctrl;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	int err, idx;

	frule = flow_cls_offload_flow_rule(fco);
	if (!flow_action_has_entries(&frule->action)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack, "No actions");
		return -EINVAL;
	}

	if (!flow_action_basic_hw_stats_check(&frule->action, fco->common.extack))
		return -EOPNOTSUPP;

	vctrl = port->sparx5->vcap_ctrl;
	vrule = vcap_alloc_rule(vctrl, ndev, fco->common.chain_index, VCAP_USER_TC,
				fco->common.prio, 0);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	vrule->cookie = fco->cookie;
	sparx5_tc_use_dissectors(fco, admin, vrule);
	flow_action_for_each(idx, act, &frule->action) {
		switch (act->id) {
		case FLOW_ACTION_TRAP:
			err = vcap_rule_add_action_bit(vrule,
						       VCAP_AF_CPU_COPY_ENA,
						       VCAP_BIT_1);
			if (err)
				goto out;
			err = vcap_rule_add_action_u32(vrule,
						       VCAP_AF_CPU_QUEUE_NUM, 0);
			if (err)
				goto out;
			err = vcap_rule_add_action_u32(vrule, VCAP_AF_MASK_MODE,
						       SPX5_PMM_REPLACE_ALL);
			if (err)
				goto out;
			/* For now the actionset is hardcoded */
			err = vcap_set_rule_set_actionset(vrule,
							  VCAP_AFS_BASE_TYPE);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_ACCEPT:
			/* For now the actionset is hardcoded */
			err = vcap_set_rule_set_actionset(vrule,
							  VCAP_AFS_BASE_TYPE);
			if (err)
				goto out;
			break;
		default:
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "Unsupported TC action");
			err = -EOPNOTSUPP;
			goto out;
		}
	}
	/* For now the keyset is hardcoded */
	err = vcap_set_rule_set_keyset(vrule, VCAP_KFS_MAC_ETYPE);
	if (err) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No matching port keyset for filter protocol and keys");
		goto out;
	}
	err = vcap_val_rule(vrule, ETH_P_ALL);
	if (err) {
		vcap_set_tc_exterr(fco, vrule);
		goto out;
	}
	err = vcap_add_rule(vrule);
	if (err)
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Could not add the filter");
out:
	vcap_free_rule(vrule);
	return err;
}

static int sparx5_tc_flower_destroy(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_control *vctrl;
	int err = -ENOENT, rule_id;

	vctrl = port->sparx5->vcap_ctrl;
	while (true) {
		rule_id = vcap_lookup_rule_by_cookie(vctrl, fco->cookie);
		if (rule_id <= 0)
			break;
		err = vcap_del_rule(vctrl, ndev, rule_id);
		if (err) {
			pr_err("%s:%d: could not delete rule %d\n",
			       __func__, __LINE__, rule_id);
			break;
		}
	}
	return err;
}

int sparx5_tc_flower(struct net_device *ndev, struct flow_cls_offload *fco,
		     bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_control *vctrl;
	struct vcap_admin *admin;
	int err = -EINVAL;

	/* Get vcap instance from the chain id */
	vctrl = port->sparx5->vcap_ctrl;
	admin = vcap_find_admin(vctrl, fco->common.chain_index);
	if (!admin) {
		NL_SET_ERR_MSG_MOD(fco->common.extack, "Invalid chain");
		return err;
	}

	switch (fco->command) {
	case FLOW_CLS_REPLACE:
		return sparx5_tc_flower_replace(ndev, fco, admin);
	case FLOW_CLS_DESTROY:
		return sparx5_tc_flower_destroy(ndev, fco, admin);
	default:
		return -EOPNOTSUPP;
	}
}
