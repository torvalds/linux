// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020, NXP Semiconductors
 */
#include <linux/dsa/8021q.h>
#include "sja1105.h"

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
	struct sja1105_vl_lookup_entry *vl_lookup;
	struct sja1105_table *table;
	struct sja1105_rule *rule;
	int num_virtual_links = 0;
	int i, j, k;

	/* Figure out the dimensioning of the problem */
	list_for_each_entry(rule, &priv->flow_block.rules, list) {
		if (rule->type != SJA1105_RULE_VL)
			continue;
		/* Each VL lookup entry matches on a single ingress port */
		num_virtual_links += hweight_long(rule->port_mask);
	}

	if (num_virtual_links > SJA1105_MAX_VL_LOOKUP_COUNT) {
		NL_SET_ERR_MSG_MOD(extack, "Not enough VL entries available");
		return -ENOSPC;
	}

	/* Discard previous VL Lookup Table */
	table = &priv->static_config.tables[BLK_IDX_VL_LOOKUP];
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

	return 0;
}

int sja1105_vl_redirect(struct sja1105_private *priv, int port,
			struct netlink_ext_ack *extack, unsigned long cookie,
			struct sja1105_key *key, unsigned long destports,
			bool append)
{
	struct sja1105_rule *rule = sja1105_rule_find(priv, cookie);
	int rc;

	if (dsa_port_is_vlan_filtering(dsa_to_port(priv->ds, port)) &&
	    key->type != SJA1105_KEY_VLAN_AWARE_VL) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only redirect based on {DMAC, VID, PCP}");
		return -EOPNOTSUPP;
	} else if (key->type != SJA1105_KEY_VLAN_UNAWARE_VL) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only redirect based on DMAC");
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
