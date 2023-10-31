// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2023 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>

#include "ksz9477.h"
#include "ksz9477_reg.h"
#include "ksz_common.h"

#define ETHER_TYPE_FULL_MASK		cpu_to_be16(~0)
#define KSZ9477_MAX_TC			7

/**
 * ksz9477_flower_parse_key_l2 - Parse Layer 2 key from flow rule and configure
 *                               ACL entries accordingly.
 * @dev: Pointer to the ksz_device.
 * @port: Port number.
 * @extack: Pointer to the netlink_ext_ack.
 * @rule: Pointer to the flow_rule.
 * @cookie: The cookie to associate with the entry.
 * @prio: The priority of the entry.
 *
 * This function parses the Layer 2 key from the flow rule and configures
 * the corresponding ACL entries. It checks for unsupported offloads and
 * available entries before proceeding with the configuration.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int ksz9477_flower_parse_key_l2(struct ksz_device *dev, int port,
				       struct netlink_ext_ack *extack,
				       struct flow_rule *rule,
				       unsigned long cookie, u32 prio)
{
	struct ksz9477_acl_priv *acl = dev->ports[port].acl_priv;
	struct flow_match_eth_addrs ematch;
	struct ksz9477_acl_entries *acles;
	int required_entries;
	u8 *src_mac = NULL;
	u8 *dst_mac = NULL;
	u16 ethtype = 0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);

		if (match.key->n_proto) {
			if (match.mask->n_proto != ETHER_TYPE_FULL_MASK) {
				NL_SET_ERR_MSG_MOD(extack,
						   "ethernet type mask must be a full mask");
				return -EINVAL;
			}

			ethtype = be16_to_cpu(match.key->n_proto);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		flow_rule_match_eth_addrs(rule, &ematch);

		if (!is_zero_ether_addr(ematch.key->src)) {
			if (!is_broadcast_ether_addr(ematch.mask->src))
				goto not_full_mask_err;

			src_mac = ematch.key->src;
		}

		if (!is_zero_ether_addr(ematch.key->dst)) {
			if (!is_broadcast_ether_addr(ematch.mask->dst))
				goto not_full_mask_err;

			dst_mac = ematch.key->dst;
		}
	}

	acles = &acl->acles;
	/* ACL supports only one MAC per entry */
	required_entries = src_mac && dst_mac ? 2 : 1;

	/* Check if there are enough available entries */
	if (acles->entries_count + required_entries > KSZ9477_ACL_MAX_ENTRIES) {
		NL_SET_ERR_MSG_MOD(extack, "ACL entry limit reached");
		return -EOPNOTSUPP;
	}

	ksz9477_acl_match_process_l2(dev, port, ethtype, src_mac, dst_mac,
				     cookie, prio);

	return 0;

not_full_mask_err:
	NL_SET_ERR_MSG_MOD(extack, "MAC address mask must be a full mask");
	return -EOPNOTSUPP;
}

/**
 * ksz9477_flower_parse_key - Parse flow rule keys for a specified port on a
 *			      ksz_device.
 * @dev: The ksz_device instance.
 * @port: The port number to parse the flow rule keys for.
 * @extack: The netlink extended ACK for reporting errors.
 * @rule: The flow_rule to parse.
 * @cookie: The cookie to associate with the entry.
 * @prio: The priority of the entry.
 *
 * This function checks if the used keys in the flow rule are supported by
 * the device and parses the L2 keys if they match. If unsupported keys are
 * used, an error message is set in the extended ACK.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int ksz9477_flower_parse_key(struct ksz_device *dev, int port,
				    struct netlink_ext_ack *extack,
				    struct flow_rule *rule,
				    unsigned long cookie, u32 prio)
{
	struct flow_dissector *dissector = rule->match.dissector;
	int ret;

	if (dissector->used_keys &
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC) ||
	    flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		ret = ksz9477_flower_parse_key_l2(dev, port, extack, rule,
						  cookie, prio);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * ksz9477_flower_parse_action - Parse flow rule actions for a specified port
 *				 on a ksz_device.
 * @dev: The ksz_device instance.
 * @port: The port number to parse the flow rule actions for.
 * @extack: The netlink extended ACK for reporting errors.
 * @cls: The flow_cls_offload instance containing the flow rule.
 * @entry_idx: The index of the ACL entry to store the action.
 *
 * This function checks if the actions in the flow rule are supported by
 * the device. Currently, only actions that change priorities are supported.
 * If unsupported actions are encountered, an error message is set in the
 * extended ACK.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int ksz9477_flower_parse_action(struct ksz_device *dev, int port,
				       struct netlink_ext_ack *extack,
				       struct flow_cls_offload *cls,
				       int entry_idx)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct ksz9477_acl_priv *acl = dev->ports[port].acl_priv;
	const struct flow_action_entry *act;
	struct ksz9477_acl_entry *entry;
	bool prio_force = false;
	u8 prio_val = 0;
	int i;

	if (TC_H_MIN(cls->classid)) {
		NL_SET_ERR_MSG_MOD(extack, "hw_tc is not supported. Use: action skbedit prio");
		return -EOPNOTSUPP;
	}

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_PRIORITY:
			if (act->priority > KSZ9477_MAX_TC) {
				NL_SET_ERR_MSG_MOD(extack, "Priority value is too high");
				return -EOPNOTSUPP;
			}
			prio_force = true;
			prio_val = act->priority;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "action not supported");
			return -EOPNOTSUPP;
		}
	}

	/* pick entry to store action */
	entry = &acl->acles.entries[entry_idx];

	ksz9477_acl_action_rule_cfg(entry->entry, prio_force, prio_val);
	ksz9477_acl_processing_rule_set_action(entry->entry, entry_idx);

	return 0;
}

/**
 * ksz9477_cls_flower_add - Add a flow classification rule for a specified port
 *			    on a ksz_device.
 * @ds: The DSA switch instance.
 * @port: The port number to add the flow classification rule to.
 * @cls: The flow_cls_offload instance containing the flow rule.
 * @ingress: A flag indicating if the rule is applied on the ingress path.
 *
 * This function adds a flow classification rule for a specified port on a
 * ksz_device. It checks if the ACL offloading is supported and parses the flow
 * keys and actions. If the ACL is not supported, it returns an error. If there
 * are unprocessed entries, it parses the action for the rule.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int ksz9477_cls_flower_add(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct ksz_device *dev = ds->priv;
	struct ksz9477_acl_priv *acl;
	int action_entry_idx;
	int ret;

	acl = dev->ports[port].acl_priv;

	if (!acl) {
		NL_SET_ERR_MSG_MOD(extack, "ACL offloading is not supported");
		return -EOPNOTSUPP;
	}

	/* A complex rule set can take multiple entries. Use first entry
	 * to store the action.
	 */
	action_entry_idx = acl->acles.entries_count;

	ret = ksz9477_flower_parse_key(dev, port, extack, rule, cls->cookie,
				       cls->common.prio);
	if (ret)
		return ret;

	ret = ksz9477_flower_parse_action(dev, port, extack, cls,
					  action_entry_idx);
	if (ret)
		return ret;

	ret = ksz9477_sort_acl_entries(dev, port);
	if (ret)
		return ret;

	return ksz9477_acl_write_list(dev, port);
}

/**
 * ksz9477_cls_flower_del - Remove a flow classification rule for a specified
 *			    port on a ksz_device.
 * @ds: The DSA switch instance.
 * @port: The port number to remove the flow classification rule from.
 * @cls: The flow_cls_offload instance containing the flow rule.
 * @ingress: A flag indicating if the rule is applied on the ingress path.
 *
 * This function removes a flow classification rule for a specified port on a
 * ksz_device. It checks if the ACL is initialized, and if not, returns an
 * error. If the ACL is initialized, it removes entries with the specified
 * cookie and rewrites the ACL list.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int ksz9477_cls_flower_del(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress)
{
	unsigned long cookie = cls->cookie;
	struct ksz_device *dev = ds->priv;
	struct ksz9477_acl_priv *acl;

	acl = dev->ports[port].acl_priv;

	if (!acl)
		return -EOPNOTSUPP;

	ksz9477_acl_remove_entries(dev, port, &acl->acles, cookie);

	return ksz9477_acl_write_list(dev, port);
}
