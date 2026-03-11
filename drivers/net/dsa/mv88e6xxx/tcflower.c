// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch flower support
 *
 * Copyright (c) 2026 Luminex Network Intelligence
 */

#include "chip.h"
#include "tcflower.h"
#include "tcam.h"

#define MV88E6XXX_ETHTYPE_OFFSET 16
#define MV88E6XXX_IP_PROTO_OFFSET 27
#define MV88E6XXX_IPV4_SRC_OFFSET 30
#define MV88E6XXX_IPV4_DST_OFFSET 34

static int mv88e6xxx_flower_parse_key(struct mv88e6xxx_chip *chip,
				      struct netlink_ext_ack *extack,
				      struct flow_cls_offload *cls,
				      struct mv88e6xxx_tcam_key *key)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	u16 addr_type = 0;

	if (dissector->used_keys &
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;

		if (flow_rule_has_control_flags(match.mask->flags,
						cls->common.extack))
			return -EOPNOTSUPP;
	}
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		mv88e6xxx_tcam_match_set(key, MV88E6XXX_ETHTYPE_OFFSET,
					 match.key->n_proto,
					 match.mask->n_proto);
		mv88e6xxx_tcam_match_set(key, MV88E6XXX_IP_PROTO_OFFSET,
					 match.key->ip_proto,
					 match.mask->ip_proto);
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(cls->rule, &match);
		mv88e6xxx_tcam_match_set(key, MV88E6XXX_IPV4_SRC_OFFSET,
					 match.key->src,
					 match.mask->src);
		mv88e6xxx_tcam_match_set(key, MV88E6XXX_IPV4_DST_OFFSET,
					 match.key->dst,
					 match.mask->dst);
	}

	return 0;
}

int mv88e6xxx_cls_flower_add(struct dsa_switch *ds, int port,
			     struct flow_cls_offload *cls, bool ingress)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_tcam_key key = { 0 };
	const struct flow_action_entry *act;
	unsigned long cookie = cls->cookie;
	struct mv88e6xxx_tcam_entry *entry;
	int err, i;

	if (!mv88e6xxx_has_tcam(chip)) {
		NL_SET_ERR_MSG_MOD(extack, "hardware offload not supported");
		return -EOPNOTSUPP;
	}

	err = mv88e6xxx_flower_parse_key(chip, extack, cls, &key);
	if (err)
		return err;

	mv88e6xxx_reg_lock(chip);
	entry = mv88e6xxx_tcam_entry_find(chip, cookie);
	if (entry) {
		err = -EEXIST;
		goto err_unlock;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		err = -ENOMEM;
		goto err_unlock;
	}

	entry->cookie = cookie;
	entry->prio = cls->common.prio;
	entry->key = key;

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_TRAP: {
			int cpu = dsa_upstream_port(ds, port);

			entry->action.dpv_mode = DPV_MODE_REPLACE;
			entry->action.dpv = BIT(cpu);
			break;
		}
		default:
			NL_SET_ERR_MSG_MOD(extack, "action not supported");
			err = -EOPNOTSUPP;
			goto err_free_entry;
		}
	}

	entry->key.spv = BIT(port);
	entry->key.spv_mask = mv88e6xxx_port_mask(chip);

	err = mv88e6xxx_tcam_entry_add(chip, entry);
	if (err)
		goto err_free_entry;

	mv88e6xxx_reg_unlock(chip);
	return  0;

err_free_entry:
	kfree(entry);
err_unlock:
	mv88e6xxx_reg_unlock(chip);
	return err;
}

int mv88e6xxx_cls_flower_del(struct dsa_switch *ds, int port,
			     struct flow_cls_offload *cls, bool ingress)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_tcam_entry *entry;
	int err = 0;

	mv88e6xxx_reg_lock(chip);
	entry = mv88e6xxx_tcam_entry_find(chip, cls->cookie);

	if (entry)
		err = mv88e6xxx_tcam_entry_del(chip, entry);
	mv88e6xxx_reg_unlock(chip);
	return  err;
}

void mv88e6xxx_flower_teardown(struct mv88e6xxx_chip *chip)
{
	struct mv88e6xxx_tcam_entry *pos, *n;

	list_for_each_entry_safe(pos, n, &chip->tcam.entries, list) {
		list_del(&pos->list);
		kfree(pos);
	}
}
