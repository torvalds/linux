// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 David Yang
 */

#include "chip.h"
#include "smi.h"
#include "tc.h"

static int yt921x_mtu_fetch(struct yt921x_priv *priv, int port)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);

	return dp->user ? READ_ONCE(dp->user->mtu) : ETH_DATA_LEN;
}

/* v * 2^e */
static u64 ldexpu64(u64 v, int e)
{
	return e >= 0 ? v << e : v >> -e;
}

/* slot (ns) * rate (/s) / 10^9 (ns/s) = 2^C * token * 4^unit */
static u32 rate2token(u64 rate, unsigned int slot_ns, int unit, int C)
{
	int e = 2 * unit + C + YT921X_TOKEN_RATE_C;

	return div_u64(ldexpu64(slot_ns * rate, -e), NSEC_PER_SEC);
}

static u64 token2rate(u32 token, unsigned int slot_ns, int unit, int C)
{
	int e = 2 * unit + C + YT921X_TOKEN_RATE_C;

	return div_u64(ldexpu64(mul_u32_u32(NSEC_PER_SEC, token), e), slot_ns);
}

/* burst = 2^C * token * 4^unit */
static u32 burst2token(u64 burst, int unit, int C)
{
	return ldexpu64(burst, -(2 * unit + C));
}

static u64 token2burst(u32 token, int unit, int C)
{
	return ldexpu64(token, 2 * unit + C);
}

struct yt921x_marker {
	u32 cir;
	u32 cbs;
	u32 ebs;
	int unit;
	bool pkt_mode;
};

#define YT921X_MARKER_PKT_MODE		BIT(0)
#define YT921X_MARKER_SINGLE_BUCKET	BIT(1)

static int
yt921x_marker_tfm(struct yt921x_marker *marker, u64 rate, u64 burst,
		  unsigned int flags, unsigned int slot_ns, u32 cir_max,
		  u32 cbs_max, int unit_max, struct yt921x_priv *priv, int port,
		  struct netlink_ext_ack *extack)
{
	const int C = flags & YT921X_MARKER_PKT_MODE ? YT921X_TOKEN_PKT_C :
		      YT921X_TOKEN_BYTE_C;
	struct device *dev = yt921x_priv_to_device(priv);
	struct yt921x_marker m;
	u64 burst_est;
	u64 burst_sug;
	u64 burst_max;
	u64 rate_max;

	m.unit = unit_max;
	rate_max = token2rate(cir_max, slot_ns, m.unit, C);
	burst_max = token2burst(cbs_max, m.unit, C);

	/* Check for unusual values */
	if (rate > rate_max || burst > burst_max) {
		NL_SET_ERR_MSG_MOD(extack, "Unexpected tremendous rate");
		return -ERANGE;
	}

	/* Check for matching burst */
	burst_est = div_u64(slot_ns * rate, NSEC_PER_SEC);
	burst_sug = burst_est;
	if (flags & YT921X_MARKER_PKT_MODE)
		burst_sug++;
	else
		burst_sug += ETH_HLEN + yt921x_mtu_fetch(priv, port) +
			     ETH_FCS_LEN;
	if (burst_sug > burst)
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Consider match rate %llu with burst at least %llu",
				       rate, burst_sug);

	/* Select unit */
	for (; m.unit > 0; m.unit--) {
		if (rate > (rate_max >> 2) || burst > (burst_max >> 2))
			break;
		rate_max >>= 2;
		burst_max >>= 2;
	}

	/* Calculate information rate and bucket size */
	m.cir = rate2token(rate, slot_ns, m.unit, C);
	if (!m.cir)
		m.cir = 1;
	else if (WARN_ON(m.cir > cir_max))
		m.cir = cir_max;
	m.cbs = burst2token(burst, m.unit, C);
	if (!m.cbs)
		m.cbs = 1;
	else if (WARN_ON(m.cbs > cbs_max))
		m.cbs = cbs_max;

	/* Cut EBS */
	m.ebs = 0;
	if (!(flags & YT921X_MARKER_SINGLE_BUCKET)) {
		/* We don't have a chance to adjust rate when MTU is changed */
		if (flags & YT921X_MARKER_PKT_MODE)
			burst_est++;
		else
			burst_est += YT921X_FRAME_SIZE_MAX;

		if (burst_est < burst) {
			u32 pbs = m.cbs;

			m.cbs = burst2token(burst_est, m.unit, C);
			if (!m.cbs)
				m.cbs = 1;
			else if (WARN_ON(m.cbs > cbs_max))
				m.cbs = cbs_max;

			if (pbs > m.cbs)
				m.ebs = pbs - m.cbs;
		}
	}

	dev_dbg(dev,
		"slot %u ns, rate %llu, burst %llu -> unit %d, cir %u, cbs %u, ebs %u\n",
		slot_ns, rate, burst, m.unit, m.cir, m.cbs, m.ebs);

	m.pkt_mode = flags & YT921X_MARKER_PKT_MODE;
	*marker = m;
	return 0;
}

static int
yt921x_marker_tfm_police(struct yt921x_marker *marker,
			 const struct flow_action_police *police,
			 unsigned int flags, struct yt921x_priv *priv, int port,
			 struct netlink_ext_ack *extack)
{
	bool pkt_mode = !!police->rate_pkt_ps;
	u64 burst;
	u64 rate;

	rate = pkt_mode ? police->rate_pkt_ps : police->rate_bytes_ps;
	burst = pkt_mode ? police->burst_pkt : police->burst;
	if (pkt_mode)
		flags |= YT921X_MARKER_PKT_MODE;

	return yt921x_marker_tfm(marker, rate, burst, flags,
				 priv->meter_slot_ns, YT921X_METER_CIR_MAX,
				 YT921X_METER_CBS_MAX, YT921X_METER_UNIT_MAX,
				 priv, port, extack);
}

static int
yt921x_marker_tfm_shape(struct yt921x_marker *marker, u64 rate, u64 burst,
			unsigned int flags, struct yt921x_priv *priv, int port,
			struct netlink_ext_ack *extack)
{
	return yt921x_marker_tfm(marker, rate, burst, flags,
				 priv->port_shape_slot_ns, YT921X_SHAPE_CIR_MAX,
				 YT921X_SHAPE_CBS_MAX, YT921X_SHAPE_UNIT_MAX,
				 priv, port, extack);
}

static int
yt921x_police_validate(const struct flow_action_police *police,
		       const struct flow_action *action,
		       const struct flow_action_entry *act,
		       struct netlink_ext_ack *extack)
{
	if (police->exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (police->notexceed.act_id != FLOW_ACTION_PIPE &&
	    police->notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (police->notexceed.act_id == FLOW_ACTION_ACCEPT && action && act &&
	    !flow_action_is_last_entry(action, act)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but action is not last");
		return -EOPNOTSUPP;
	}

	/* mtu defaults to unlimited but we got 2040 here, don't know why */
	if (police->peakrate_bytes_ps || police->avrate || police->overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
yt921x_meter_config(struct yt921x_priv *priv, unsigned int id,
		    const struct yt921x_marker *marker)
{
	u32 ctrls[3];

	ctrls[0] = 0;
	ctrls[1] = YT921X_METER_CTRLb_CIR(marker->cir);
	ctrls[2] = YT921X_METER_CTRLc_UNIT(marker->unit) |
		   YT921X_METER_CTRLc_DROP_R |
		   YT921X_METER_CTRLc_TOKEN_OVERFLOW_EN |
		   YT921X_METER_CTRLc_METER_EN;
	if (marker->pkt_mode)
		ctrls[2] |= YT921X_METER_CTRLc_PKT_MODE;
	update_ctrls_unaligned(&ctrls[0], &ctrls[1],
			       YT921X_METER_CTRLab_EBS_M,
			       YT921X_METER_CTRLab_EBS(marker->ebs));
	update_ctrls_unaligned(&ctrls[1], &ctrls[2],
			       YT921X_METER_CTRLbc_CBS_M,
			       YT921X_METER_CTRLbc_CBS(marker->cbs));

	return yt921x_reg96_write(priv, YT921X_METERn_CTRL(id), ctrls);
}

void yt921x_dsa_port_policer_del(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_PORTn_METER(port), 0);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "delete policer on",
			port, res);
}

int
yt921x_dsa_port_policer_add(struct dsa_switch *ds, int port,
			    const struct flow_action_police *police,
			    struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_marker marker;
	u32 ctrl;
	int res;

	res = yt921x_police_validate(police, NULL, NULL, extack);
	if (res)
		return res;

	res = yt921x_marker_tfm_police(&marker, police, 0, priv, port, extack);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_meter_config(priv, port + YT921X_METER_NUM, &marker);
	if (res)
		goto end;

	ctrl = YT921X_PORT_METER_ID(port) | YT921X_PORT_METER_EN;
	res = yt921x_reg_write(priv, YT921X_PORTn_METER(port), ctrl);
end:
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_setup_tc_tbf_port(struct dsa_switch *ds, int port,
				  const struct tc_tbf_qopt_offload *qopt)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct netlink_ext_ack *extack = qopt->extack;
	u32 ctrls[2];
	int res;

	if (qopt->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	switch (qopt->command) {
	case TC_TBF_STATS:
		/* Unfortunately the convention for TC_*_STATS is a mess,
		 * neither 0 nor -EOPNOTSUPP is perfect here.
		 */
		return -EOPNOTSUPP;
	case TC_TBF_DESTROY:
		ctrls[0] = 0;
		ctrls[1] = 0;
		break;
	case TC_TBF_REPLACE: {
		const struct tc_tbf_qopt_offload_replace_params *p;
		struct yt921x_marker marker;

		p = &qopt->replace_params;

		res = yt921x_marker_tfm_shape(&marker, p->rate.rate_bytes_ps,
					      p->max_size,
					      YT921X_MARKER_SINGLE_BUCKET,
					      priv, port, extack);
		if (res)
			return res;

		ctrls[0] = YT921X_PORT_SHAPE_CTRLa_CIR(marker.cir) |
			   YT921X_PORT_SHAPE_CTRLa_CBS(marker.cbs);
		ctrls[1] = YT921X_PORT_SHAPE_CTRLb_UNIT(marker.unit) |
			   YT921X_PORT_SHAPE_CTRLb_EN;
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg64_write(priv, YT921X_PORTn_SHAPE_CTRL(port), ctrls);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int
yt921x_dsa_port_setup_tc(struct dsa_switch *ds, int port,
			 enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_TBF: {
		const struct tc_tbf_qopt_offload *qopt = type_data;

		return yt921x_dsa_port_setup_tc_tbf_port(ds, port, qopt);
	}
	default:
		return -EOPNOTSUPP;
	}
}

/* ACL: 48 blocks * 8 entries
 *
 * One rule can span multiple entries, but within a block.
 */

static void
yt921x_acl_entry_set(struct yt921x_acl_entry *entry, unsigned int offset,
		     u32 flags, bool set)
{
	if (set)
		entry->key[offset] |= flags;
	entry->mask[offset] |= flags;
}

static unsigned int
yt921x_acl_entries_set_is_fragment(struct yt921x_acl_entry *entries,
				   unsigned int size, bool set)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_IPV4_DA:
		case YT921X_ACL_TYPE_IPV4_SA:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_IPV4_FRAG, set);
			return size;
		case YT921X_ACL_TYPE_IPV6_DA3:
		case YT921X_ACL_TYPE_IPV6_SA3:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_IPV6_xA3_FRAG,
					     set);
			return size;
		case YT921X_ACL_TYPE_MISC:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_MISC_FRAG, set);
			return size;
		case YT921X_ACL_TYPE_L4:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_L4_FRAG, set);
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	yt921x_acl_entry_set(&entries[size], 1, YT921X_ACL_BINb_MISC_FRAG, set);

	return size + 1;
}

static unsigned int
yt921x_acl_entries_set_first_frag(struct yt921x_acl_entry *entries,
				  unsigned int size, bool set)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_IPV6_DA2:
		case YT921X_ACL_TYPE_IPV6_SA2:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_IPV6_xA2_FIRST_FRAG,
					     set);
			return size;
		case YT921X_ACL_TYPE_MISC:
			yt921x_acl_entry_set(&entries[i], 0,
					     YT921X_ACL_BINa_MISC_FIRST_FRAG,
					     set);
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	yt921x_acl_entry_set(&entries[size], 0,
			     YT921X_ACL_BINa_MISC_FIRST_FRAG, set);

	return size + 1;
}

static unsigned int
yt921x_acl_entries_set_l3_type(struct yt921x_acl_entry *entries,
			       unsigned int size, enum yt921x_l3_type type)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_MAC_DA0:
		case YT921X_ACL_TYPE_MAC_SA0:
			entries[i].key[1] |= YT921X_ACL_BINb_MAC_xA0_L3_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_MAC_xA0_L3_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_MISC:
			entries[i].key[0] |= YT921X_ACL_BINa_MISC_L3_TYPE(type);
			entries[i].mask[0] |= YT921X_ACL_BINa_MISC_L3_TYPE_M;
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[0] = YT921X_ACL_BINa_MISC_L3_TYPE(type);
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	entries[size].mask[0] = YT921X_ACL_BINa_MISC_L3_TYPE_M;

	return size + 1;
}

static unsigned int
yt921x_acl_entries_set_l4_type(struct yt921x_acl_entry *entries,
			       unsigned int size, enum yt921x_l4_type type)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_IPV4_DA:
		case YT921X_ACL_TYPE_IPV4_SA:
			entries[i].key[1] |= YT921X_ACL_BINb_IPV4_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_IPV4_L4_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_IPV6_DA0:
		case YT921X_ACL_TYPE_IPV6_DA1:
		case YT921X_ACL_TYPE_IPV6_DA2:
		case YT921X_ACL_TYPE_IPV6_DA3:
		case YT921X_ACL_TYPE_IPV6_SA0:
		case YT921X_ACL_TYPE_IPV6_SA1:
		case YT921X_ACL_TYPE_IPV6_SA2:
		case YT921X_ACL_TYPE_IPV6_SA3:
			entries[i].key[1] |= YT921X_ACL_BINb_IPV6_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_IPV6_L4_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_L4:
			entries[i].key[1] |= YT921X_ACL_BINb_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_L4_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_MISC:
			entries[i].key[1] |= YT921X_ACL_BINb_MISC_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_MISC_L4_TYPE_M;
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_BINb_MISC_L4_TYPE(type) |
			       YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	entries[size].mask[1] = YT921X_ACL_BINb_MISC_L4_TYPE_M;

	return size + 1;
}

static struct yt921x_acl_entry *
yt921x_acl_entries_new(struct yt921x_acl_entry *entries, unsigned int *sizep,
		       u32 type)
{
	unsigned int size = *sizep;

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return NULL;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(type);

	(*sizep)++;
	return &entries[size];
}

static struct yt921x_acl_entry *
yt921x_acl_entries_find(struct yt921x_acl_entry *entries, unsigned int *sizep,
			u32 type)
{
	for (unsigned int i = 0; i < *sizep; i++)
		if (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1]) ==
		    type)
			return &entries[i];
	return yt921x_acl_entries_new(entries, sizep, type);
}

static void
yt921x_acl_rule_set_ports(struct yt921x_acl_rule *aclrule, u16 ord,
			  u16 ports_mask)
{
	struct yt921x_acl_entry *entries = aclrule->entries;

	for (unsigned int i = 0; i < hweight8(aclrule->mask); i++) {
		entries[i].key[1] |= YT921X_ACL_KEYb_SPORTS(ports_mask) |
				     YT921X_ACL_KEYb_ORD(ord);
	}
}

struct yt921x_acl_rule_ext {
	struct yt921x_acl_rule r;

	struct yt921x_marker marker;
};

static int
yt921x_acl_rule_ext_parse_flow_entries(struct yt921x_acl_rule_ext *ruleext,
				       const struct flow_cls_offload *cls)
{
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct yt921x_acl_entry *entries = ruleext->r.entries;
	struct netlink_ext_ack *extack = cls->common.extack;
	const struct flow_dissector *dissector;
	struct yt921x_acl_entry *entry;
	unsigned int size = 0;
	bool use_dport;
	bool use_sport;

	/* Incomplete and probably won't, since it supports custom u32 filters.
	 * New adapters are welcome.
	 */
	dissector = rule->match.dissector;
	if (dissector->used_keys &
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS_RANGE) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IP) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_TCP))) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	/* Entries */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);

		if (match.mask->dst) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV4_DA);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->dst);
			entry->mask[0] |= ntohl(match.mask->dst);
		}

		if (match.mask->src) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV4_SA);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->src);
			entry->mask[0] |= ntohl(match.mask->src);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);

		for (unsigned int i = 0; i < 4; i++) {
			if (!match.mask->dst.s6_addr32[i])
				continue;

			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV6_DA0 + i);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->dst.s6_addr32[i]);
			entry->mask[0] |= ntohl(match.mask->dst.s6_addr32[i]);
		}

		for (unsigned int i = 0; i < 4; i++) {
			if (!match.mask->src.s6_addr32[i])
				continue;

			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV6_SA0 + i);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->src.s6_addr32[i]);
			entry->mask[0] |= ntohl(match.mask->src.s6_addr32[i]);
		}
	}

	use_dport = false;
	use_sport = false;
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		entry = yt921x_acl_entries_new(entries, &size,
					       YT921X_ACL_TYPE_L4);
		if (!entry)
			goto err;

		flow_rule_match_ports(rule, &match);

		use_dport = !!match.mask->dst;
		use_sport = !!match.mask->src;

		entry->key[0] |= (ntohs(match.key->dst) << 16) |
				 ntohs(match.key->src);
		entry->mask[0] |= (ntohs(match.mask->dst) << 16) |
				  ntohs(match.mask->src);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS_RANGE)) {
		struct flow_match_ports_range match;

		entry = yt921x_acl_entries_find(entries, &size,
						YT921X_ACL_TYPE_L4);
		if (!entry)
			goto err;

		flow_rule_match_ports_range(rule, &match);

		if ((use_dport && match.mask->tp.dst) ||
		    (use_sport && match.mask->tp.src)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Port mask and range are mutually exclusive");
			return -EINVAL;
		}

		if (match.mask->tp.dst) {
			entry->key[0] |= ntohs(match.key->tp_min.dst) << 16;
			entry->key[1] |= YT921X_ACL_KEYb_L4_DPORT_RANGE_EN;
			entry->mask[0] |= ntohs(match.key->tp_max.dst) << 16;
		}

		if (match.mask->tp.src) {
			entry->key[0] |= ntohs(match.key->tp_min.src);
			entry->key[1] |= YT921X_ACL_KEYb_L4_SPORT_RANGE_EN;
			entry->mask[0] |= ntohs(match.key->tp_max.src);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;
		u32 mask;

		flow_rule_match_eth_addrs(rule, &match);

		mask = ethaddr_hi4_to_u32(match.mask->dst);
		if (mask) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_MAC_DA0);
			if (!entry)
				goto err;

			entry->key[0] |= ethaddr_hi4_to_u32(match.key->dst);
			entry->mask[0] |= mask;
		}

		mask = ethaddr_hi4_to_u32(match.mask->src);
		if (mask) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_MAC_SA0);
			if (!entry)
				goto err;

			entry->key[0] |= ethaddr_hi4_to_u32(match.key->src);
			entry->mask[0] |= mask;
		}

		mask = (ethaddr_lo2_to_u32(match.mask->dst) << 16) |
		       ethaddr_lo2_to_u32(match.mask->src);
		if (mask) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_MAC_DA1_SA1);
			if (!entry)
				goto err;

			entry->key[0] |= (ethaddr_lo2_to_u32(match.key->dst) << 16) |
					 ethaddr_lo2_to_u32(match.key->src);
			entry->mask[0] |= mask;
		}
	}

	/* Entries + Misc */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);

		if (match.mask->n_proto) {
			enum yt921x_l3_type l3type = YT921X_L3_TYPE_OTHER;

			if (match.mask->n_proto == htons(~0))
				switch (match.key->n_proto) {
				case htons(ETH_P_IP):
					l3type = YT921X_L3_TYPE_IPV4;
					break;
				case htons(ETH_P_IPV6):
					l3type = YT921X_L3_TYPE_IPV6;
					break;
				case htons(ETH_P_ARP):
					l3type = YT921X_L3_TYPE_ARP;
					break;
				case htons(ETH_P_LLDP):
					l3type = YT921X_L3_TYPE_LLDP;
					break;
				case htons(ETH_P_PAE):
					l3type = YT921X_L3_TYPE_PAE;
					break;
				case htons(ETH_P_CFM):
					l3type = YT921X_L3_TYPE_ERP;
					break;
				}

			if (l3type != YT921X_L3_TYPE_OTHER) {
				size = yt921x_acl_entries_set_l3_type(entries,
								      size,
								      l3type);
				if (!size)
					goto err;
			} else {
				entry = yt921x_acl_entries_new(entries, &size,
							       YT921X_ACL_TYPE_ETHERTYPE);
				if (!entry)
					goto err;

				entry->key[0] |= ntohs(match.key->n_proto);
				entry->mask[0] |= ntohs(match.mask->n_proto);
			}
		}

		if (match.mask->ip_proto) {
			enum yt921x_l4_type l4type = YT921X_L4_TYPE_OTHER;

			if (match.mask->ip_proto == (u8)~0)
				switch (match.key->ip_proto) {
				case IPPROTO_TCP:
					l4type = YT921X_L4_TYPE_TCP;
					break;
				case IPPROTO_UDP:
					l4type = YT921X_L4_TYPE_UDP;
					break;
				case IPPROTO_UDPLITE:
					l4type = YT921X_L4_TYPE_UDPLITE;
					break;
				case IPPROTO_ICMP:
					l4type = YT921X_L4_TYPE_ICMP;
					break;
				case IPPROTO_IGMP:
					l4type = YT921X_L4_TYPE_IGMP;
					break;
				}

			if (l4type != YT921X_L4_TYPE_OTHER) {
				size = yt921x_acl_entries_set_l4_type(entries,
								      size,
								      l4type);
				if (!size)
					goto err;
			} else {
				entry = yt921x_acl_entries_find(entries, &size,
								YT921X_ACL_TYPE_MISC);
				if (!entry)
					goto err;

				entry->key[0] |= YT921X_ACL_BINa_MISC_IP_PROTO(match.key->ip_proto);
				entry->mask[0] |= YT921X_ACL_BINa_MISC_IP_PROTO(match.mask->ip_proto);
			}
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		u32 supp_flags = FLOW_DIS_IS_FRAGMENT | FLOW_DIS_FIRST_FRAG;
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		if (!flow_rule_is_supp_control_flags(supp_flags,
						     match.mask->flags, extack))
			return -EOPNOTSUPP;

		if (match.mask->flags & FLOW_DIS_IS_FRAGMENT) {
			bool set = match.key->flags & FLOW_DIS_IS_FRAGMENT;

			size = yt921x_acl_entries_set_is_fragment(entries, size,
								  set);
			if (!size)
				goto err;
		}
		if (match.mask->flags & FLOW_DIS_FIRST_FRAG) {
			bool set = match.key->flags & FLOW_DIS_FIRST_FRAG;

			size = yt921x_acl_entries_set_first_frag(entries, size,
								 set);
			if (!size)
				goto err;
		}
	}

	/* Misc only */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		if (match.mask->ttl) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on TTL not supported");
			return -EOPNOTSUPP;
		}

		if (match.mask->tos) {
			entry = yt921x_acl_entries_find(entries, &size,
							YT921X_ACL_TYPE_MISC);
			if (!entry)
				goto err;

			entry->key[0] |= YT921X_ACL_BINa_MISC_TOS(match.key->tos);
			entry->mask[0] |= YT921X_ACL_BINa_MISC_TOS(match.mask->tos);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp match;

		flow_rule_match_tcp(rule, &match);
		if (match.mask->flags & htons(~0xff)) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported TCP flags");
			return -EOPNOTSUPP;
		}

		if (match.mask->flags) {
			entry = yt921x_acl_entries_find(entries, &size,
							YT921X_ACL_TYPE_MISC);
			if (!entry)
				goto err;

			entry->key[0] |= YT921X_ACL_BINa_MISC_TCP_FLAGS(ntohs(match.key->flags));
			entry->mask[0] |= YT921X_ACL_BINa_MISC_TCP_FLAGS(ntohs(match.mask->flags));
		}
	}

	if (!size) {
		NL_SET_ERR_MSG_MOD(extack, "Empty rule generated, this should not happen");
		return -EOPNOTSUPP;
	}

	ruleext->r.mask = (1 << size) - 1;
	return 0;

err:
	NL_SET_ERR_MSG_MOD(extack, "Rule too complex");
	return -EOPNOTSUPP;
}

static int
yt921x_acl_rule_ext_parse_flow_action(struct yt921x_acl_rule_ext *ruleext,
				      const struct flow_cls_offload *cls,
				      struct yt921x_priv *priv, int port)
{
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	const struct flow_action *flow_action = &rule->action;
	struct netlink_ext_ack *extack = cls->common.extack;
	enum flow_action_id redir_act = NUM_FLOW_ACTIONS;
	const struct flow_action_entry *act;
	u32 *action = ruleext->r.action;
	bool seen_priority = false;
	const char *reason = NULL;
	bool seen_police = false;
	unsigned int i;
	int res;

	memset(action, 0, 3 * sizeof(*action));
	flow_action_for_each(i, act, flow_action)
		switch (act->id) {
		case FLOW_ACTION_ACCEPT:
		case FLOW_ACTION_DROP:
		case FLOW_ACTION_REDIRECT:
			if (redir_act != NUM_FLOW_ACTIONS &&
			    redir_act != act->id) {
				reason = "Different redirect actions";
				goto fallback;
			}
			redir_act = act->id;

			switch (act->id) {
			case FLOW_ACTION_ACCEPT:
				action[2] |= YT921X_ACL_ACTc_FWD_EN |
					     YT921X_ACL_ACTc_FWD_FWD;
				break;
			case FLOW_ACTION_DROP:
				action[2] |= YT921X_ACL_ACTc_FWD_EN |
					     YT921X_ACL_ACTc_FWD_REDIR;
				break;
			case FLOW_ACTION_REDIRECT: {
				struct dsa_port *to_dp;

				to_dp = dsa_port_from_netdev(act->dev);
				if (IS_ERR(to_dp) || to_dp->ds != &priv->ds) {
					reason = "Redirect to non-local port";
					goto fallback;
				}

				action[2] |= YT921X_ACL_ACTc_FWD_EN |
					     YT921X_ACL_ACTc_FWD_REDIR |
					     YT921X_ACL_ACTc_FWD_REDIR_DPORTn(to_dp->index);
				break;
			}
			default:
				break;
			}
			break;
		case FLOW_ACTION_PRIORITY:
			if (seen_priority) {
				action[0] &= ~YT921X_ACL_ACTa_PRIO_EN;
				action[1] &= ~YT921X_ACL_ACTb_PRIO_M;

				reason = "Multiple priority actions";
				goto fallback;
			}
			seen_priority = true;

			if (act->priority >= YT921X_PRIO_NUM) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Priority value is too high");
				return -EOPNOTSUPP;
			}
			action[0] |= YT921X_ACL_ACTa_PRIO_EN;
			action[1] |= YT921X_ACL_ACTb_PRIO(act->priority);
			break;
		case FLOW_ACTION_POLICE: {
			const struct flow_action_police *police = &act->police;

			if (seen_police) {
				action[0] &= ~YT921X_ACL_ACTa_METER_EN;

				reason = "Multiple police actions";
				goto fallback;
			}
			seen_police = true;

			res = yt921x_police_validate(police, flow_action, act,
						     extack);
			if (res)
				return res;

			res = yt921x_marker_tfm_police(&ruleext->marker, police,
						       0, priv, port, extack);
			if (res)
				return res;

			action[0] |= YT921X_ACL_ACTa_METER_EN;
			break;
		}
		default:
fallback:
			if (cls->common.skip_sw) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Action not supported when skip_sw: %s",
						       reason);
				return -EOPNOTSUPP;
			}
			fallthrough;
		case FLOW_ACTION_TRAP:
			redir_act = FLOW_ACTION_TRAP;

			action[2] &= ~YT921X_ACL_ACTc_FWD_REDIR_DPORTS_M &
				     ~YT921X_ACL_ACTc_FWD_M;
			action[2] |= YT921X_ACL_ACTc_FWD_EN |
				     YT921X_ACL_ACTc_FWD_TRAP;
			break;
		}

	ruleext->r.sw_assisted = !cls->common.skip_sw;
	return 0;
}

static int
yt921x_acl_rule_ext_parse_flow(struct yt921x_acl_rule_ext *ruleext, int port,
			       const struct flow_cls_offload *cls, bool ingress,
			       struct yt921x_priv *priv)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	int res;

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack, "Only ingress is supported");
		return -EOPNOTSUPP;
	}

	if (cls->common.chain_index) {
		NL_SET_ERR_MSG(extack, "Only chain 0 is supported");
		return -EOPNOTSUPP;
	}

	res = yt921x_acl_rule_ext_parse_flow_action(ruleext, cls, priv, port);
	if (res)
		return res;
	res = yt921x_acl_rule_ext_parse_flow_entries(ruleext, cls);
	if (res)
		return res;

	yt921x_acl_rule_set_ports(&ruleext->r, 0, BIT(port));
	ruleext->r.tag = cls->cookie;
	ruleext->r.type = TC_SETUP_CLSFLOWER;
	return 0;
}

static unsigned int
yt921x_acl_find(const struct yt921x_priv *priv, enum tc_setup_type type,
		unsigned long tag)
{
	for (unsigned int blkid = 0; blkid < YT921X_ACL_BLK_NUM; blkid++) {
		const struct yt921x_acl_blk *aclblk = priv->acl_blks[blkid];

		if (!aclblk)
			continue;

		for (unsigned int i = 0; i < YT921X_ACL_ENT_PER_BLK; i++)
			if (aclblk->rules[i] && aclblk->rules[i]->tag == tag &&
			    aclblk->rules[i]->type == type)
				return YT921X_ACL_ENT_PER_BLK * blkid + i;
	}

	return UINT_MAX;
}

static unsigned int
yt921x_acl_reserve(struct yt921x_priv *priv, unsigned int entscnt,
		   struct netlink_ext_ack *extack)
{
	int candidates[YT921X_ACL_ENT_PER_BLK + 1];
	unsigned int acl_used_cnt = 0;

	if (WARN_ON(entscnt > YT921X_ACL_ENT_PER_BLK))
		return UINT_MAX;

	for (unsigned int i = 0; i < ARRAY_SIZE(candidates); i++)
		candidates[i] = -1;
	for (unsigned int i = YT921X_ACL_BLK_NUM; i-- > 0;) {
		unsigned int blk_used_cnt = hweight8(priv->acl_masks[i]);

		candidates[blk_used_cnt] = i;
		acl_used_cnt += blk_used_cnt;
	}

	if (acl_used_cnt >= YT921X_ACL_NUM) {
		NL_SET_ERR_MSG_MOD(extack, "ACL entry limit reached");
		return UINT_MAX;
	}
	if (acl_used_cnt + entscnt <= YT921X_ACL_NUM)
		for (unsigned int i = YT921X_ACL_ENT_PER_BLK - entscnt + 1;
		     i-- > 0;)
			if (candidates[i] >= 0)
				return YT921X_ACL_ENT_PER_BLK * candidates[i] +
				       ffz(priv->acl_masks[candidates[i]]);

	NL_SET_ERR_MSG_MOD(extack,
			   "ACL entry allocation failed, simplify your rules or remove existing rules");
	return UINT_MAX;
}

static int
yt921x_acl_commit(struct yt921x_priv *priv, unsigned int entid, u8 entsmask)
{
	const struct yt921x_acl_rule *aclrule;
	const struct yt921x_acl_blk *aclblk;
	unsigned int blkid;
	unsigned int binid;
	unsigned long mask;
	u32 zeros[3] = {};
	unsigned int i;
	unsigned int o;
	u32 ctrl;
	int res;

	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	binid = entid % YT921X_ACL_ENT_PER_BLK;
	aclblk = priv->acl_blks[blkid];
	aclrule = aclblk->rules[binid];

	/* Write actions */
	res = yt921x_reg96_write(priv, YT921X_ACLn_ACT(entid),
				 aclrule ? aclrule->action : zeros);
	if (res)
		return res;

	/* Select the block */
	ctrl = YT921X_ACL_BLK_CMD_MODIFY | YT921X_ACL_BLK_CMD_BLKID(blkid);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD, ctrl);
	if (res)
		return res;

	/* Write keys and masks */
	ctrl = 0;
	for (unsigned int i = 0; i < YT921X_ACL_ENT_PER_BLK; i++)
		ctrl |= YT921X_ACL_BLK_KEEP_KEEPn(i);

	mask = entsmask;
	i = 0;
	for_each_set_bit(o, &mask, YT921X_ACL_ENT_PER_BLK) {
		res = yt921x_reg64_write(priv, YT921X_ACLn_KEYm(blkid, o),
					 aclrule ? aclrule->entries[i].key :
					 zeros);
		if (res)
			return res;

		res = yt921x_reg64_write(priv, YT921X_ACLn_MASKm(blkid, o),
					 aclrule ? aclrule->entries[i].mask :
					 zeros);
		if (res)
			return res;

		ctrl &= ~YT921X_ACL_BLK_KEEP_KEEPn(o);
		i++;
	}

	res = yt921x_reg_write(priv, YT921X_ACL_BLK_KEEP, ctrl);
	if (res)
		return res;

	ctrl = 0;
	for (unsigned int i = 0; i < YT921X_ACL_ENT_PER_BLK; i++) {
		const struct yt921x_acl_rule *other = aclblk->rules[i];

		if (!other)
			continue;

		mask = other->mask;
		for_each_set_bit(o, &mask, YT921X_ACL_ENT_PER_BLK)
			ctrl |= YT921X_ACL_ENTRY_ENm(o) |
				YT921X_ACL_ENTRY_GRPIDm(o, i);
	}
	res = yt921x_reg_write(priv, YT921X_ACLn_ENTRY(blkid), ctrl);
	if (res)
		return res;

	/* Commit the block */
	ctrl = YT921X_ACL_BLK_CMD_BLKID(blkid);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD, ctrl);
	if (res)
		return res;

	return 0;
}

static int
yt921x_acl_del(struct yt921x_priv *priv, enum tc_setup_type type,
	       unsigned long tag)
{
	struct yt921x_acl_rule *aclrule;
	struct yt921x_acl_blk *aclblk;
	unsigned int binid;
	unsigned int blkid;
	unsigned int entid;
	int res;

	entid = yt921x_acl_find(priv, type, tag);
	if (entid == UINT_MAX)
		return -ENOENT;

	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	binid = entid % YT921X_ACL_ENT_PER_BLK;
	aclblk = priv->acl_blks[blkid];
	aclrule = aclblk->rules[binid];

	aclblk->rules[binid] = NULL;
	res = yt921x_acl_commit(priv, entid, aclrule->mask);
	/* the kernel never rolls back on failure */

	if (aclrule->action[0] & YT921X_ACL_ACTa_METER_EN)
		clear_bit(FIELD_GET(YT921X_ACL_ACTa_METER_ID_M,
				    aclrule->action[0]),
			  priv->meters_map);
	priv->acl_masks[blkid] &= ~aclrule->mask;
	kvfree(aclrule);
	if (!priv->acl_masks[blkid]) {
		kvfree(aclblk);
		priv->acl_blks[blkid] = NULL;
	}
	return res;
}

static int
yt921x_acl_add(struct yt921x_priv *priv,
	       const struct yt921x_acl_rule_ext *ruleext,
	       struct netlink_ext_ack *extack)
{
	unsigned int entscnt = hweight8(ruleext->r.mask);
	struct yt921x_acl_rule *aclrule;
	struct yt921x_acl_blk *aclblk;
	bool use_trap = false;
	unsigned int meterid;
	unsigned long mask;
	unsigned int binid;
	unsigned int blkid;
	unsigned int entid;
	unsigned int o;
	int res;

	/* Allocate resources */
	entid = yt921x_acl_reserve(priv, entscnt, extack);
	if (entid == UINT_MAX)
		return -EOPNOTSUPP;

	if (!(ruleext->r.action[0] & YT921X_ACL_ACTa_METER_EN)) {
		meterid = YT921X_METER_NUM;
	} else {
		meterid = find_first_zero_bit(priv->meters_map,
					      YT921X_METER_NUM);
		if (meterid < YT921X_METER_NUM) {
			res = yt921x_meter_config(priv, meterid,
						  &ruleext->marker);
			if (res)
				return res;
		} else if (ruleext->r.sw_assisted) {
			use_trap = true;
		} else {
			NL_SET_ERR_MSG_MOD(extack,
					   "No more meters available");
			return -EOPNOTSUPP;
		}
	}

	/* Prepare acl block ctrlblk */
	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	binid = entid % YT921X_ACL_ENT_PER_BLK;
	aclblk = priv->acl_blks[blkid];
	if (!aclblk) {
		aclblk = kvzalloc_obj(*aclblk);
		if (!aclblk)
			return -ENOMEM;
		priv->acl_blks[blkid] = aclblk;
	}

	/* Prepare acl rule ctrlblk */
	aclrule = kvmemdup(&ruleext->r,
			   offsetof(struct yt921x_acl_rule, entries[entscnt]),
			   GFP_KERNEL);
	if (!aclrule) {
		res = -ENOMEM;
		goto err;
	}

	/* Replace the placeholder resource IDs */
	aclrule->mask = 0;
	mask = priv->acl_masks[blkid];
	for_each_clear_bit(o, &mask, YT921X_ACL_ENT_PER_BLK) {
		aclrule->mask |= BIT(o);
		entscnt--;
		if (!entscnt)
			break;
	}

	if (use_trap) {
		aclrule->action[2] &= ~YT921X_ACL_ACTc_FWD_REDIR_DPORTS_M &
				      ~YT921X_ACL_ACTc_FWD_M;
		aclrule->action[2] |= YT921X_ACL_ACTc_FWD_EN |
				      YT921X_ACL_ACTc_FWD_TRAP;
	}
	if (meterid < YT921X_METER_NUM)
		aclrule->action[0] |= YT921X_ACL_ACTa_METER_ID(meterid);
	else
		aclrule->action[0] &= ~YT921X_ACL_ACTa_METER_EN;

	/* Write rules */
	aclblk->rules[binid] = aclrule;
	res = yt921x_acl_commit(priv, entid, aclrule->mask);
	if (res) {
		aclblk->rules[binid] = NULL;
		kvfree(aclrule);
		goto err;
	}

	if (meterid < YT921X_METER_NUM)
		set_bit(meterid, priv->meters_map);
	priv->acl_masks[blkid] |= aclrule->mask;
	return 0;

err:
	if (!priv->acl_masks[blkid]) {
		kvfree(aclblk);
		priv->acl_blks[blkid] = NULL;
	}
	return res;
}

int
yt921x_dsa_cls_flower_del(struct dsa_switch *ds, int port,
			  struct flow_cls_offload *cls, bool ingress)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_acl_del(priv, TC_SETUP_CLSFLOWER, cls->cookie);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int
yt921x_dsa_cls_flower_add(struct dsa_switch *ds, int port,
			  struct flow_cls_offload *cls, bool ingress)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_acl_rule_ext ruleext;
	int res;

	res = yt921x_acl_rule_ext_parse_flow(&ruleext, port, cls, ingress,
					     priv);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_acl_add(priv, &ruleext, extack);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_mirror_del(struct yt921x_priv *priv, int port, bool ingress)
{
	u32 mask;

	if (ingress)
		mask = YT921X_MIRROR_IGR_PORTn(port);
	else
		mask = YT921X_MIRROR_EGR_PORTn(port);
	return yt921x_reg_clear_bits(priv, YT921X_MIRROR, mask);
}

static int
yt921x_mirror_add(struct yt921x_priv *priv, int port, bool ingress,
		  int to_local_port, struct netlink_ext_ack *extack)
{
	u32 srcs;
	u32 ctrl;
	u32 val;
	u32 dst;
	int res;

	if (ingress)
		srcs = YT921X_MIRROR_IGR_PORTn(port);
	else
		srcs = YT921X_MIRROR_EGR_PORTn(port);
	dst = YT921X_MIRROR_PORT(to_local_port);

	res = yt921x_reg_read(priv, YT921X_MIRROR, &val);
	if (res)
		return res;

	/* other mirror tasks & different dst port -> conflict */
	if ((val & ~srcs & (YT921X_MIRROR_EGR_PORTS_M |
			    YT921X_MIRROR_IGR_PORTS_M)) &&
	    (val & YT921X_MIRROR_PORT_M) != dst) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Sniffer port is already configured, delete existing rules & retry");
		return -EBUSY;
	}

	ctrl = val & ~YT921X_MIRROR_PORT_M;
	ctrl |= srcs;
	ctrl |= dst;

	if (ctrl == val)
		return 0;

	return yt921x_reg_write(priv, YT921X_MIRROR, ctrl);
}

void
yt921x_dsa_port_mirror_del(struct dsa_switch *ds, int port,
			   struct dsa_mall_mirror_tc_entry *mirror)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mirror_del(priv, port, mirror->ingress);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "unmirror",
			port, res);
}

int
yt921x_dsa_port_mirror_add(struct dsa_switch *ds, int port,
			   struct dsa_mall_mirror_tc_entry *mirror,
			   bool ingress, struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mirror_add(priv, port, ingress,
				mirror->to_local_port, extack);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int yt921x_chip_setup_tc(struct yt921x_priv *priv)
{
	unsigned int op_ns;
	u32 ctrl;
	int res;

	op_ns = 8 * priv->cycle_ns;

	ctrl = max(priv->meter_slot_ns / op_ns, YT921X_METER_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_METER_SLOT, ctrl);
	if (res)
		return res;
	priv->meter_slot_ns = ctrl * op_ns;

	ctrl = max(priv->port_shape_slot_ns / op_ns,
		   YT921X_PORT_SHAPE_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_PORT_SHAPE_SLOT, ctrl);
	if (res)
		return res;
	priv->port_shape_slot_ns = ctrl * op_ns;

	return 0;
}

int yt921x_chip_setup_acl(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	ctrl = YT921X_ACL_PERMIT_UNMATCH_PORTS_M;
	res = yt921x_reg_write(priv, YT921X_ACL_PERMIT_UNMATCH, ctrl);
	if (res)
		return res;

	ctrl = YT921X_ACL_PORT_PORTS_M;
	res = yt921x_reg_write(priv, YT921X_ACL_PORT, ctrl);
	if (res)
		return res;

	return 0;
}
