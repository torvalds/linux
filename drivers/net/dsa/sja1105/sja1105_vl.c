// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020, NXP Semiconductors
 */
#include <net/tc_act/tc_gate.h>
#include <linux/dsa/8021q.h>
#include "sja1105_vl.h"

#define SJA1105_SIZE_VL_STATUS			8

/* The switch flow classification core implements TTEthernet, which 'thinks' in
 * terms of Virtual Links (VL), a concept borrowed from ARINC 664 part 7.
 * However it also has one other operating mode (VLLUPFORMAT=0) where it acts
 * somewhat closer to a pre-standard implementation of IEEE 802.1Qci
 * (Per-Stream Filtering and Policing), which is what the driver is going to be
 * implementing.
 *
 *                                 VL Lookup
 *        Key = {DMAC && VLANID   +---------+  Key = { (DMAC[47:16] & VLMASK ==
 *               && VLAN PCP      |         |                         VLMARKER)
 *               && INGRESS PORT} +---------+                      (both fixed)
 *            (exact match,            |             && DMAC[15:0] == VLID
 *         all specified in rule)      |                    (specified in rule)
 *                                     v             && INGRESS PORT }
 *                               ------------
 *                    0 (PSFP)  /            \  1 (ARINC664)
 *                 +-----------/  VLLUPFORMAT \----------+
 *                 |           \    (fixed)   /          |
 *                 |            \            /           |
 *  0 (forwarding) v             ------------            |
 *           ------------                                |
 *          /            \  1 (QoS classification)       |
 *     +---/  ISCRITICAL  \-----------+                  |
 *     |   \  (per rule)  /           |                  |
 *     |    \            /   VLID taken from      VLID taken from
 *     v     ------------     index of rule       contents of rule
 *  select                     that matched         that matched
 * DESTPORTS                          |                  |
 *  |                                 +---------+--------+
 *  |                                           |
 *  |                                           v
 *  |                                     VL Forwarding
 *  |                                   (indexed by VLID)
 *  |                                      +---------+
 *  |                       +--------------|         |
 *  |                       |  select TYPE +---------+
 *  |                       v
 *  |   0 (rate      ------------    1 (time
 *  |  constrained) /            \   triggered)
 *  |       +------/     TYPE     \------------+
 *  |       |      \  (per VLID)  /            |
 *  |       v       \            /             v
 *  |  VL Policing   ------------         VL Policing
 *  | (indexed by VLID)                (indexed by VLID)
 *  |  +---------+                        +---------+
 *  |  | TYPE=0  |                        | TYPE=1  |
 *  |  +---------+                        +---------+
 *  |  select SHARINDX                 select SHARINDX to
 *  |  to rate-limit                 re-enter VL Forwarding
 *  |  groups of VL's               with new VLID for egress
 *  |  to same quota                           |
 *  |       |                                  |
 *  |  select MAXLEN -> exceed => drop    select MAXLEN -> exceed => drop
 *  |       |                                  |
 *  |       v                                  v
 *  |  VL Forwarding                      VL Forwarding
 *  | (indexed by SHARINDX)             (indexed by SHARINDX)
 *  |  +---------+                        +---------+
 *  |  | TYPE=0  |                        | TYPE=1  |
 *  |  +---------+                        +---------+
 *  |  select PRIORITY,                 select PRIORITY,
 *  | PARTITION, DESTPORTS            PARTITION, DESTPORTS
 *  |       |                                  |
 *  |       v                                  v
 *  |  VL Policing                        VL Policing
 *  | (indexed by SHARINDX)           (indexed by SHARINDX)
 *  |  +---------+                        +---------+
 *  |  | TYPE=0  |                        | TYPE=1  |
 *  |  +---------+                        +---------+
 *  |       |                                  |
 *  |       v                                  |
 *  |  select BAG, -> exceed => drop           |
 *  |    JITTER                                v
 *  |       |             ----------------------------------------------
 *  |       |            /    Reception Window is open for this VL      \
 *  |       |           /    (the Schedule Table executes an entry i     \
 *  |       |          /   M <= i < N, for which these conditions hold):  \ no
 *  |       |    +----/                                                    \-+
 *  |       |    |yes \       WINST[M] == 1 && WINSTINDEX[M] == VLID       / |
 *  |       |    |     \     WINEND[N] == 1 && WINSTINDEX[N] == VLID      /  |
 *  |       |    |      \                                                /   |
 *  |       |    |       \ (the VL window has opened and not yet closed)/    |
 *  |       |    |        ----------------------------------------------     |
 *  |       |    v                                                           v
 *  |       |  dispatch to DESTPORTS when the Schedule Table               drop
 *  |       |  executes an entry i with TXEN == 1 && VLINDEX == i
 *  v       v
 * dispatch immediately to DESTPORTS
 *
 * The per-port classification key is always composed of {DMAC, VID, PCP} and
 * is non-maskable. This 'looks like' the NULL stream identification function
 * from IEEE 802.1CB clause 6, except for the extra VLAN PCP. When the switch
 * ports operate as VLAN-unaware, we do allow the user to not specify the VLAN
 * ID and PCP, and then the port-based defaults will be used.
 *
 * In TTEthernet, routing is something that needs to be done manually for each
 * Virtual Link. So the flow action must always include one of:
 * a. 'redirect', 'trap' or 'drop': select the egress port list
 * Additionally, the following actions may be applied on a Virtual Link,
 * turning it into 'critical' traffic:
 * b. 'police': turn it into a rate-constrained VL, with bandwidth limitation
 *    given by the maximum frame length, bandwidth allocation gap (BAG) and
 *    maximum jitter.
 * c. 'gate': turn it into a time-triggered VL, which can be only be received
 *    and forwarded according to a given schedule.
 */

static bool sja1105_vl_key_lower(struct sja1105_vl_lookup_entry *a,
				 struct sja1105_vl_lookup_entry *b)
{
	if (a->macaddr < b->macaddr)
		return true;
	if (a->macaddr > b->macaddr)
		return false;
	if (a->vlanid < b->vlanid)
		return true;
	if (a->vlanid > b->vlanid)
		return false;
	if (a->port < b->port)
		return true;
	if (a->port > b->port)
		return false;
	if (a->vlanprior < b->vlanprior)
		return true;
	if (a->vlanprior > b->vlanprior)
		return false;
	/* Keys are equal */
	return false;
}

static int sja1105_init_virtual_links(struct sja1105_private *priv,
				      struct netlink_ext_ack *extack)
{
	struct sja1105_vl_policing_entry *vl_policing;
	struct sja1105_vl_forwarding_entry *vl_fwd;
	struct sja1105_vl_lookup_entry *vl_lookup;
	bool have_critical_virtual_links = false;
	struct sja1105_table *table;
	struct sja1105_rule *rule;
	int num_virtual_links = 0;
	int max_sharindx = 0;
	int i, j, k;

	/* Figure out the dimensioning of the problem */
	list_for_each_entry(rule, &priv->flow_block.rules, list) {
		if (rule->type != SJA1105_RULE_VL)
			continue;
		/* Each VL lookup entry matches on a single ingress port */
		num_virtual_links += hweight_long(rule->port_mask);

		if (rule->vl.type != SJA1105_VL_NONCRITICAL)
			have_critical_virtual_links = true;
		if (max_sharindx < rule->vl.sharindx)
			max_sharindx = rule->vl.sharindx;
	}

	if (num_virtual_links > SJA1105_MAX_VL_LOOKUP_COUNT) {
		NL_SET_ERR_MSG_MOD(extack, "Not enough VL entries available");
		return -ENOSPC;
	}

	if (max_sharindx + 1 > SJA1105_MAX_VL_LOOKUP_COUNT) {
		NL_SET_ERR_MSG_MOD(extack, "Policer index out of range");
		return -ENOSPC;
	}

	max_sharindx = max_t(int, num_virtual_links, max_sharindx) + 1;

	/* Discard previous VL Lookup Table */
	table = &priv->static_config.tables[BLK_IDX_VL_LOOKUP];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Discard previous VL Policing Table */
	table = &priv->static_config.tables[BLK_IDX_VL_POLICING];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Discard previous VL Forwarding Table */
	table = &priv->static_config.tables[BLK_IDX_VL_FORWARDING];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Discard previous VL Forwarding Parameters Table */
	table = &priv->static_config.tables[BLK_IDX_VL_FORWARDING_PARAMS];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Nothing to do */
	if (!num_virtual_links)
		return 0;

	/* Pre-allocate space in the static config tables */

	/* VL Lookup Table */
	table = &priv->static_config.tables[BLK_IDX_VL_LOOKUP];
	table->entries = kcalloc(num_virtual_links,
				 table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = num_virtual_links;
	vl_lookup = table->entries;

	k = 0;

	list_for_each_entry(rule, &priv->flow_block.rules, list) {
		unsigned long port;

		if (rule->type != SJA1105_RULE_VL)
			continue;

		for_each_set_bit(port, &rule->port_mask, SJA1105_NUM_PORTS) {
			vl_lookup[k].format = SJA1105_VL_FORMAT_PSFP;
			vl_lookup[k].port = port;
			vl_lookup[k].macaddr = rule->key.vl.dmac;
			if (rule->key.type == SJA1105_KEY_VLAN_AWARE_VL) {
				vl_lookup[k].vlanid = rule->key.vl.vid;
				vl_lookup[k].vlanprior = rule->key.vl.pcp;
			} else {
				u16 vid = dsa_8021q_rx_vid(priv->ds, port);

				vl_lookup[k].vlanid = vid;
				vl_lookup[k].vlanprior = 0;
			}
			/* For critical VLs, the DESTPORTS mask is taken from
			 * the VL Forwarding Table, so no point in putting it
			 * in the VL Lookup Table
			 */
			if (rule->vl.type == SJA1105_VL_NONCRITICAL)
				vl_lookup[k].destports = rule->vl.destports;
			else
				vl_lookup[k].iscritical = true;
			vl_lookup[k].flow_cookie = rule->cookie;
			k++;
		}
	}

	/* UM10944.pdf chapter 4.2.3 VL Lookup table:
	 * "the entries in the VL Lookup table must be sorted in ascending
	 * order (i.e. the smallest value must be loaded first) according to
	 * the following sort order: MACADDR, VLANID, PORT, VLANPRIOR."
	 */
	for (i = 0; i < num_virtual_links; i++) {
		struct sja1105_vl_lookup_entry *a = &vl_lookup[i];

		for (j = i + 1; j < num_virtual_links; j++) {
			struct sja1105_vl_lookup_entry *b = &vl_lookup[j];

			if (sja1105_vl_key_lower(b, a)) {
				struct sja1105_vl_lookup_entry tmp = *a;

				*a = *b;
				*b = tmp;
			}
		}
	}

	if (!have_critical_virtual_links)
		return 0;

	/* VL Policing Table */
	table = &priv->static_config.tables[BLK_IDX_VL_POLICING];
	table->entries = kcalloc(max_sharindx, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = max_sharindx;
	vl_policing = table->entries;

	/* VL Forwarding Table */
	table = &priv->static_config.tables[BLK_IDX_VL_FORWARDING];
	table->entries = kcalloc(max_sharindx, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = max_sharindx;
	vl_fwd = table->entries;

	/* VL Forwarding Parameters Table */
	table = &priv->static_config.tables[BLK_IDX_VL_FORWARDING_PARAMS];
	table->entries = kcalloc(1, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = 1;

	for (i = 0; i < num_virtual_links; i++) {
		unsigned long cookie = vl_lookup[i].flow_cookie;
		struct sja1105_rule *rule = sja1105_rule_find(priv, cookie);

		if (rule->vl.type == SJA1105_VL_NONCRITICAL)
			continue;
		if (rule->vl.type == SJA1105_VL_TIME_TRIGGERED) {
			int sharindx = rule->vl.sharindx;

			vl_policing[i].type = 1;
			vl_policing[i].sharindx = sharindx;
			vl_policing[i].maxlen = rule->vl.maxlen;
			vl_policing[sharindx].type = 1;

			vl_fwd[i].type = 1;
			vl_fwd[sharindx].type = 1;
			vl_fwd[sharindx].priority = rule->vl.ipv;
			vl_fwd[sharindx].partition = 0;
			vl_fwd[sharindx].destports = rule->vl.destports;
		}
	}

	sja1105_frame_memory_partitioning(priv);

	return 0;
}

int sja1105_vl_redirect(struct sja1105_private *priv, int port,
			struct netlink_ext_ack *extack, unsigned long cookie,
			struct sja1105_key *key, unsigned long destports,
			bool append)
{
	struct sja1105_rule *rule = sja1105_rule_find(priv, cookie);
	int rc;

	if (priv->vlan_state == SJA1105_VLAN_UNAWARE &&
	    key->type != SJA1105_KEY_VLAN_UNAWARE_VL) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only redirect based on DMAC");
		return -EOPNOTSUPP;
	} else if (key->type != SJA1105_KEY_VLAN_AWARE_VL) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only redirect based on {DMAC, VID, PCP}");
		return -EOPNOTSUPP;
	}

	if (!rule) {
		rule = kzalloc(sizeof(*rule), GFP_KERNEL);
		if (!rule)
			return -ENOMEM;

		rule->cookie = cookie;
		rule->type = SJA1105_RULE_VL;
		rule->key = *key;
		list_add(&rule->list, &priv->flow_block.rules);
	}

	rule->port_mask |= BIT(port);
	if (append)
		rule->vl.destports |= destports;
	else
		rule->vl.destports = destports;

	rc = sja1105_init_virtual_links(priv, extack);
	if (rc) {
		rule->port_mask &= ~BIT(port);
		if (!rule->port_mask) {
			list_del(&rule->list);
			kfree(rule);
		}
	}

	return rc;
}

int sja1105_vl_delete(struct sja1105_private *priv, int port,
		      struct sja1105_rule *rule, struct netlink_ext_ack *extack)
{
	int rc;

	rule->port_mask &= ~BIT(port);
	if (!rule->port_mask) {
		list_del(&rule->list);
		kfree(rule);
	}

	rc = sja1105_init_virtual_links(priv, extack);
	if (rc)
		return rc;

	return sja1105_static_config_reload(priv, SJA1105_VIRTUAL_LINKS);
}

/* Insert into the global gate list, sorted by gate action time. */
static int sja1105_insert_gate_entry(struct sja1105_gating_config *gating_cfg,
				     struct sja1105_rule *rule,
				     u8 gate_state, s64 entry_time,
				     struct netlink_ext_ack *extack)
{
	struct sja1105_gate_entry *e;
	int rc;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->rule = rule;
	e->gate_state = gate_state;
	e->interval = entry_time;

	if (list_empty(&gating_cfg->entries)) {
		list_add(&e->list, &gating_cfg->entries);
	} else {
		struct sja1105_gate_entry *p;

		list_for_each_entry(p, &gating_cfg->entries, list) {
			if (p->interval == e->interval) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Gate conflict");
				rc = -EBUSY;
				goto err;
			}

			if (e->interval < p->interval)
				break;
		}
		list_add(&e->list, p->list.prev);
	}

	gating_cfg->num_entries++;

	return 0;
err:
	kfree(e);
	return rc;
}

/* The gate entries contain absolute times in their e->interval field. Convert
 * that to proper intervals (i.e. "0, 5, 10, 15" to "5, 5, 5, 5").
 */
static void
sja1105_gating_cfg_time_to_interval(struct sja1105_gating_config *gating_cfg,
				    u64 cycle_time)
{
	struct sja1105_gate_entry *last_e;
	struct sja1105_gate_entry *e;
	struct list_head *prev;

	list_for_each_entry(e, &gating_cfg->entries, list) {
		struct sja1105_gate_entry *p;

		prev = e->list.prev;

		if (prev == &gating_cfg->entries)
			continue;

		p = list_entry(prev, struct sja1105_gate_entry, list);
		p->interval = e->interval - p->interval;
	}
	last_e = list_last_entry(&gating_cfg->entries,
				 struct sja1105_gate_entry, list);
	if (last_e->list.prev != &gating_cfg->entries)
		last_e->interval = cycle_time - last_e->interval;
}

static void sja1105_free_gating_config(struct sja1105_gating_config *gating_cfg)
{
	struct sja1105_gate_entry *e, *n;

	list_for_each_entry_safe(e, n, &gating_cfg->entries, list) {
		list_del(&e->list);
		kfree(e);
	}
}

static int sja1105_compose_gating_subschedule(struct sja1105_private *priv,
					      struct netlink_ext_ack *extack)
{
	struct sja1105_gating_config *gating_cfg = &priv->tas_data.gating_cfg;
	struct sja1105_rule *rule;
	s64 max_cycle_time = 0;
	s64 its_base_time = 0;
	int i, rc = 0;

	list_for_each_entry(rule, &priv->flow_block.rules, list) {
		if (rule->type != SJA1105_RULE_VL)
			continue;
		if (rule->vl.type != SJA1105_VL_TIME_TRIGGERED)
			continue;

		if (max_cycle_time < rule->vl.cycle_time) {
			max_cycle_time = rule->vl.cycle_time;
			its_base_time = rule->vl.base_time;
		}
	}

	if (!max_cycle_time)
		return 0;

	dev_dbg(priv->ds->dev, "max_cycle_time %lld its_base_time %lld\n",
		max_cycle_time, its_base_time);

	sja1105_free_gating_config(gating_cfg);

	gating_cfg->base_time = its_base_time;
	gating_cfg->cycle_time = max_cycle_time;
	gating_cfg->num_entries = 0;

	list_for_each_entry(rule, &priv->flow_block.rules, list) {
		s64 time;
		s64 rbt;

		if (rule->type != SJA1105_RULE_VL)
			continue;
		if (rule->vl.type != SJA1105_VL_TIME_TRIGGERED)
			continue;

		/* Calculate the difference between this gating schedule's
		 * base time, and the base time of the gating schedule with the
		 * longest cycle time. We call it the relative base time (rbt).
		 */
		rbt = future_base_time(rule->vl.base_time, rule->vl.cycle_time,
				       its_base_time);
		rbt -= its_base_time;

		time = rbt;

		for (i = 0; i < rule->vl.num_entries; i++) {
			u8 gate_state = rule->vl.entries[i].gate_state;
			s64 entry_time = time;

			while (entry_time < max_cycle_time) {
				rc = sja1105_insert_gate_entry(gating_cfg, rule,
							       gate_state,
							       entry_time,
							       extack);
				if (rc)
					goto err;

				entry_time += rule->vl.cycle_time;
			}
			time += rule->vl.entries[i].interval;
		}
	}

	sja1105_gating_cfg_time_to_interval(gating_cfg, max_cycle_time);

	return 0;
err:
	sja1105_free_gating_config(gating_cfg);
	return rc;
}

int sja1105_vl_gate(struct sja1105_private *priv, int port,
		    struct netlink_ext_ack *extack, unsigned long cookie,
		    struct sja1105_key *key, u32 index, s32 prio,
		    u64 base_time, u64 cycle_time, u64 cycle_time_ext,
		    u32 num_entries, struct action_gate_entry *entries)
{
	struct sja1105_rule *rule = sja1105_rule_find(priv, cookie);
	int ipv = -1;
	int i, rc;
	s32 rem;

	if (cycle_time_ext) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cycle time extension not supported");
		return -EOPNOTSUPP;
	}

	div_s64_rem(base_time, sja1105_delta_to_ns(1), &rem);
	if (rem) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Base time must be multiple of 200 ns");
		return -ERANGE;
	}

	div_s64_rem(cycle_time, sja1105_delta_to_ns(1), &rem);
	if (rem) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cycle time must be multiple of 200 ns");
		return -ERANGE;
	}

	if (priv->vlan_state == SJA1105_VLAN_UNAWARE &&
	    key->type != SJA1105_KEY_VLAN_UNAWARE_VL) {
		dev_err(priv->ds->dev, "1: vlan state %d key type %d\n",
			priv->vlan_state, key->type);
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only gate based on DMAC");
		return -EOPNOTSUPP;
	} else if (key->type != SJA1105_KEY_VLAN_AWARE_VL) {
		dev_err(priv->ds->dev, "2: vlan state %d key type %d\n",
			priv->vlan_state, key->type);
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only gate based on {DMAC, VID, PCP}");
		return -EOPNOTSUPP;
	}

	if (!rule) {
		rule = kzalloc(sizeof(*rule), GFP_KERNEL);
		if (!rule)
			return -ENOMEM;

		list_add(&rule->list, &priv->flow_block.rules);
		rule->cookie = cookie;
		rule->type = SJA1105_RULE_VL;
		rule->key = *key;
		rule->vl.type = SJA1105_VL_TIME_TRIGGERED;
		rule->vl.sharindx = index;
		rule->vl.base_time = base_time;
		rule->vl.cycle_time = cycle_time;
		rule->vl.num_entries = num_entries;
		rule->vl.entries = kcalloc(num_entries,
					   sizeof(struct action_gate_entry),
					   GFP_KERNEL);
		if (!rule->vl.entries) {
			rc = -ENOMEM;
			goto out;
		}

		for (i = 0; i < num_entries; i++) {
			div_s64_rem(entries[i].interval,
				    sja1105_delta_to_ns(1), &rem);
			if (rem) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Interval must be multiple of 200 ns");
				rc = -ERANGE;
				goto out;
			}

			if (!entries[i].interval) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Interval cannot be zero");
				rc = -ERANGE;
				goto out;
			}

			if (ns_to_sja1105_delta(entries[i].interval) >
			    SJA1105_TAS_MAX_DELTA) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Maximum interval is 52 ms");
				rc = -ERANGE;
				goto out;
			}

			if (entries[i].maxoctets != -1) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Cannot offload IntervalOctetMax");
				rc = -EOPNOTSUPP;
				goto out;
			}

			if (ipv == -1) {
				ipv = entries[i].ipv;
			} else if (ipv != entries[i].ipv) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Only support a single IPV per VL");
				rc = -EOPNOTSUPP;
				goto out;
			}

			rule->vl.entries[i] = entries[i];
		}

		if (ipv == -1) {
			if (key->type == SJA1105_KEY_VLAN_AWARE_VL)
				ipv = key->vl.pcp;
			else
				ipv = 0;
		}

		/* TODO: support per-flow MTU */
		rule->vl.maxlen = VLAN_ETH_FRAME_LEN + ETH_FCS_LEN;
		rule->vl.ipv = ipv;
	}

	rule->port_mask |= BIT(port);

	rc = sja1105_compose_gating_subschedule(priv, extack);
	if (rc)
		goto out;

	rc = sja1105_init_virtual_links(priv, extack);
	if (rc)
		goto out;

	if (sja1105_gating_check_conflicts(priv, -1, extack)) {
		NL_SET_ERR_MSG_MOD(extack, "Conflict with tc-taprio schedule");
		rc = -ERANGE;
		goto out;
	}

out:
	if (rc) {
		rule->port_mask &= ~BIT(port);
		if (!rule->port_mask) {
			list_del(&rule->list);
			kfree(rule->vl.entries);
			kfree(rule);
		}
	}

	return rc;
}

static int sja1105_find_vlid(struct sja1105_private *priv, int port,
			     struct sja1105_key *key)
{
	struct sja1105_vl_lookup_entry *vl_lookup;
	struct sja1105_table *table;
	int i;

	if (WARN_ON(key->type != SJA1105_KEY_VLAN_AWARE_VL &&
		    key->type != SJA1105_KEY_VLAN_UNAWARE_VL))
		return -1;

	table = &priv->static_config.tables[BLK_IDX_VL_LOOKUP];
	vl_lookup = table->entries;

	for (i = 0; i < table->entry_count; i++) {
		if (key->type == SJA1105_KEY_VLAN_AWARE_VL) {
			if (vl_lookup[i].port == port &&
			    vl_lookup[i].macaddr == key->vl.dmac &&
			    vl_lookup[i].vlanid == key->vl.vid &&
			    vl_lookup[i].vlanprior == key->vl.pcp)
				return i;
		} else {
			if (vl_lookup[i].port == port &&
			    vl_lookup[i].macaddr == key->vl.dmac)
				return i;
		}
	}

	return -1;
}

int sja1105_vl_stats(struct sja1105_private *priv, int port,
		     struct sja1105_rule *rule, struct flow_stats *stats,
		     struct netlink_ext_ack *extack)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 buf[SJA1105_SIZE_VL_STATUS] = {0};
	u64 unreleased;
	u64 timingerr;
	u64 lengtherr;
	int vlid, rc;
	u64 pkts;

	if (rule->vl.type != SJA1105_VL_TIME_TRIGGERED)
		return 0;

	vlid = sja1105_find_vlid(priv, port, &rule->key);
	if (vlid < 0)
		return 0;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->vl_status + 2 * vlid, buf,
			      SJA1105_SIZE_VL_STATUS);
	if (rc) {
		NL_SET_ERR_MSG_MOD(extack, "SPI access failed");
		return rc;
	}

	sja1105_unpack(buf, &timingerr,  31, 16, SJA1105_SIZE_VL_STATUS);
	sja1105_unpack(buf, &unreleased, 15,  0, SJA1105_SIZE_VL_STATUS);
	sja1105_unpack(buf, &lengtherr,  47, 32, SJA1105_SIZE_VL_STATUS);

	pkts = timingerr + unreleased + lengtherr;

	flow_stats_update(stats, 0, pkts - rule->vl.stats.pkts,
			  jiffies - rule->vl.stats.lastused,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);

	rule->vl.stats.pkts = pkts;
	rule->vl.stats.lastused = jiffies;

	return 0;
}
