// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/phylink.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/of_device.h>
#include <linux/pcs/pcs-xpcs.h>
#include <linux/netdev_features.h>
#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_ether.h>
#include <linux/dsa/8021q.h>
#include "sja1105.h"
#include "sja1105_tas.h"

#define SJA1105_UNKNOWN_MULTICAST	0x010000000000ull

static void sja1105_hw_reset(struct gpio_desc *gpio, unsigned int pulse_len,
			     unsigned int startup_delay)
{
	gpiod_set_value_cansleep(gpio, 1);
	/* Wait for minimum reset pulse length */
	msleep(pulse_len);
	gpiod_set_value_cansleep(gpio, 0);
	/* Wait until chip is ready after reset */
	msleep(startup_delay);
}

static void
sja1105_port_allow_traffic(struct sja1105_l2_forwarding_entry *l2_fwd,
			   int from, int to, bool allow)
{
	if (allow)
		l2_fwd[from].reach_port |= BIT(to);
	else
		l2_fwd[from].reach_port &= ~BIT(to);
}

static bool sja1105_can_forward(struct sja1105_l2_forwarding_entry *l2_fwd,
				int from, int to)
{
	return !!(l2_fwd[from].reach_port & BIT(to));
}

static int sja1105_is_vlan_configured(struct sja1105_private *priv, u16 vid)
{
	struct sja1105_vlan_lookup_entry *vlan;
	int count, i;

	vlan = priv->static_config.tables[BLK_IDX_VLAN_LOOKUP].entries;
	count = priv->static_config.tables[BLK_IDX_VLAN_LOOKUP].entry_count;

	for (i = 0; i < count; i++)
		if (vlan[i].vlanid == vid)
			return i;

	/* Return an invalid entry index if not found */
	return -1;
}

static int sja1105_drop_untagged(struct dsa_switch *ds, int port, bool drop)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_mac_config_entry *mac;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	if (mac[port].drpuntag == drop)
		return 0;

	mac[port].drpuntag = drop;

	return sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, port,
					    &mac[port], true);
}

static int sja1105_pvid_apply(struct sja1105_private *priv, int port, u16 pvid)
{
	struct sja1105_mac_config_entry *mac;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	if (mac[port].vlanid == pvid)
		return 0;

	mac[port].vlanid = pvid;

	return sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, port,
					    &mac[port], true);
}

static int sja1105_commit_pvid(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct sja1105_private *priv = ds->priv;
	struct sja1105_vlan_lookup_entry *vlan;
	bool drop_untagged = false;
	int match, rc;
	u16 pvid;

	if (dp->bridge_dev && br_vlan_enabled(dp->bridge_dev))
		pvid = priv->bridge_pvid[port];
	else
		pvid = priv->tag_8021q_pvid[port];

	rc = sja1105_pvid_apply(priv, port, pvid);
	if (rc)
		return rc;

	/* Only force dropping of untagged packets when the port is under a
	 * VLAN-aware bridge. When the tag_8021q pvid is used, we are
	 * deliberately removing the RX VLAN from the port's VMEMB_PORT list,
	 * to prevent DSA tag spoofing from the link partner. Untagged packets
	 * are the only ones that should be received with tag_8021q, so
	 * definitely don't drop them.
	 */
	if (pvid == priv->bridge_pvid[port]) {
		vlan = priv->static_config.tables[BLK_IDX_VLAN_LOOKUP].entries;

		match = sja1105_is_vlan_configured(priv, pvid);

		if (match < 0 || !(vlan[match].vmemb_port & BIT(port)))
			drop_untagged = true;
	}

	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port))
		drop_untagged = true;

	return sja1105_drop_untagged(ds, port, drop_untagged);
}

static int sja1105_init_mac_settings(struct sja1105_private *priv)
{
	struct sja1105_mac_config_entry default_mac = {
		/* Enable all 8 priority queues on egress.
		 * Every queue i holds top[i] - base[i] frames.
		 * Sum of top[i] - base[i] is 511 (max hardware limit).
		 */
		.top  = {0x3F, 0x7F, 0xBF, 0xFF, 0x13F, 0x17F, 0x1BF, 0x1FF},
		.base = {0x0, 0x40, 0x80, 0xC0, 0x100, 0x140, 0x180, 0x1C0},
		.enabled = {true, true, true, true, true, true, true, true},
		/* Keep standard IFG of 12 bytes on egress. */
		.ifg = 0,
		/* Always put the MAC speed in automatic mode, where it can be
		 * adjusted at runtime by PHYLINK.
		 */
		.speed = priv->info->port_speed[SJA1105_SPEED_AUTO],
		/* No static correction for 1-step 1588 events */
		.tp_delin = 0,
		.tp_delout = 0,
		/* Disable aging for critical TTEthernet traffic */
		.maxage = 0xFF,
		/* Internal VLAN (pvid) to apply to untagged ingress */
		.vlanprio = 0,
		.vlanid = 1,
		.ing_mirr = false,
		.egr_mirr = false,
		/* Don't drop traffic with other EtherType than ETH_P_IP */
		.drpnona664 = false,
		/* Don't drop double-tagged traffic */
		.drpdtag = false,
		/* Don't drop untagged traffic */
		.drpuntag = false,
		/* Don't retag 802.1p (VID 0) traffic with the pvid */
		.retag = false,
		/* Disable learning and I/O on user ports by default -
		 * STP will enable it.
		 */
		.dyn_learn = false,
		.egress = false,
		.ingress = false,
	};
	struct sja1105_mac_config_entry *mac;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_table *table;
	struct dsa_port *dp;

	table = &priv->static_config.tables[BLK_IDX_MAC_CONFIG];

	/* Discard previous MAC Configuration Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	mac = table->entries;

	list_for_each_entry(dp, &ds->dst->ports, list) {
		if (dp->ds != ds)
			continue;

		mac[dp->index] = default_mac;

		/* Let sja1105_bridge_stp_state_set() keep address learning
		 * enabled for the DSA ports. CPU ports use software-assisted
		 * learning to ensure that only FDB entries belonging to the
		 * bridge are learned, and that they are learned towards all
		 * CPU ports in a cross-chip topology if multiple CPU ports
		 * exist.
		 */
		if (dsa_port_is_dsa(dp))
			dp->learning = true;

		/* Disallow untagged packets from being received on the
		 * CPU and DSA ports.
		 */
		if (dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp))
			mac[dp->index].drpuntag = true;
	}

	return 0;
}

static int sja1105_init_mii_settings(struct sja1105_private *priv)
{
	struct device *dev = &priv->spidev->dev;
	struct sja1105_xmii_params_entry *mii;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_table *table;
	int i;

	table = &priv->static_config.tables[BLK_IDX_XMII_PARAMS];

	/* Discard previous xMII Mode Parameters Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	/* Override table based on PHYLINK DT bindings */
	table->entry_count = table->ops->max_entry_count;

	mii = table->entries;

	for (i = 0; i < ds->num_ports; i++) {
		sja1105_mii_role_t role = XMII_MAC;

		if (dsa_is_unused_port(priv->ds, i))
			continue;

		switch (priv->phy_mode[i]) {
		case PHY_INTERFACE_MODE_INTERNAL:
			if (priv->info->internal_phy[i] == SJA1105_NO_PHY)
				goto unsupported;

			mii->xmii_mode[i] = XMII_MODE_MII;
			if (priv->info->internal_phy[i] == SJA1105_PHY_BASE_TX)
				mii->special[i] = true;

			break;
		case PHY_INTERFACE_MODE_REVMII:
			role = XMII_PHY;
			fallthrough;
		case PHY_INTERFACE_MODE_MII:
			if (!priv->info->supports_mii[i])
				goto unsupported;

			mii->xmii_mode[i] = XMII_MODE_MII;
			break;
		case PHY_INTERFACE_MODE_REVRMII:
			role = XMII_PHY;
			fallthrough;
		case PHY_INTERFACE_MODE_RMII:
			if (!priv->info->supports_rmii[i])
				goto unsupported;

			mii->xmii_mode[i] = XMII_MODE_RMII;
			break;
		case PHY_INTERFACE_MODE_RGMII:
		case PHY_INTERFACE_MODE_RGMII_ID:
		case PHY_INTERFACE_MODE_RGMII_RXID:
		case PHY_INTERFACE_MODE_RGMII_TXID:
			if (!priv->info->supports_rgmii[i])
				goto unsupported;

			mii->xmii_mode[i] = XMII_MODE_RGMII;
			break;
		case PHY_INTERFACE_MODE_SGMII:
			if (!priv->info->supports_sgmii[i])
				goto unsupported;

			mii->xmii_mode[i] = XMII_MODE_SGMII;
			mii->special[i] = true;
			break;
		case PHY_INTERFACE_MODE_2500BASEX:
			if (!priv->info->supports_2500basex[i])
				goto unsupported;

			mii->xmii_mode[i] = XMII_MODE_SGMII;
			mii->special[i] = true;
			break;
unsupported:
		default:
			dev_err(dev, "Unsupported PHY mode %s on port %d!\n",
				phy_modes(priv->phy_mode[i]), i);
			return -EINVAL;
		}

		mii->phy_mac[i] = role;
	}
	return 0;
}

static int sja1105_init_static_fdb(struct sja1105_private *priv)
{
	struct sja1105_l2_lookup_entry *l2_lookup;
	struct sja1105_table *table;
	int port;

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP];

	/* We only populate the FDB table through dynamic L2 Address Lookup
	 * entries, except for a special entry at the end which is a catch-all
	 * for unknown multicast and will be used to control flooding domain.
	 */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	if (!priv->info->can_limit_mcast_flood)
		return 0;

	table->entries = kcalloc(1, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = 1;
	l2_lookup = table->entries;

	/* All L2 multicast addresses have an odd first octet */
	l2_lookup[0].macaddr = SJA1105_UNKNOWN_MULTICAST;
	l2_lookup[0].mask_macaddr = SJA1105_UNKNOWN_MULTICAST;
	l2_lookup[0].lockeds = true;
	l2_lookup[0].index = SJA1105_MAX_L2_LOOKUP_COUNT - 1;

	/* Flood multicast to every port by default */
	for (port = 0; port < priv->ds->num_ports; port++)
		if (!dsa_is_unused_port(priv->ds, port))
			l2_lookup[0].destports |= BIT(port);

	return 0;
}

static int sja1105_init_l2_lookup_params(struct sja1105_private *priv)
{
	struct sja1105_l2_lookup_params_entry default_l2_lookup_params = {
		/* Learned FDB entries are forgotten after 300 seconds */
		.maxage = SJA1105_AGEING_TIME_MS(300000),
		/* All entries within a FDB bin are available for learning */
		.dyn_tbsz = SJA1105ET_FDB_BIN_SIZE,
		/* And the P/Q/R/S equivalent setting: */
		.start_dynspc = 0,
		/* 2^8 + 2^5 + 2^3 + 2^2 + 2^1 + 1 in Koopman notation */
		.poly = 0x97,
		/* This selects between Independent VLAN Learning (IVL) and
		 * Shared VLAN Learning (SVL)
		 */
		.shared_learn = true,
		/* Don't discard management traffic based on ENFPORT -
		 * we don't perform SMAC port enforcement anyway, so
		 * what we are setting here doesn't matter.
		 */
		.no_enf_hostprt = false,
		/* Don't learn SMAC for mac_fltres1 and mac_fltres0.
		 * Maybe correlate with no_linklocal_learn from bridge driver?
		 */
		.no_mgmt_learn = true,
		/* P/Q/R/S only */
		.use_static = true,
		/* Dynamically learned FDB entries can overwrite other (older)
		 * dynamic FDB entries
		 */
		.owr_dyn = true,
		.drpnolearn = true,
	};
	struct dsa_switch *ds = priv->ds;
	int port, num_used_ports = 0;
	struct sja1105_table *table;
	u64 max_fdb_entries;

	for (port = 0; port < ds->num_ports; port++)
		if (!dsa_is_unused_port(ds, port))
			num_used_ports++;

	max_fdb_entries = SJA1105_MAX_L2_LOOKUP_COUNT / num_used_ports;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		default_l2_lookup_params.maxaddrp[port] = max_fdb_entries;
	}

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP_PARAMS];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	/* This table only has a single entry */
	((struct sja1105_l2_lookup_params_entry *)table->entries)[0] =
				default_l2_lookup_params;

	return 0;
}

/* Set up a default VLAN for untagged traffic injected from the CPU
 * using management routes (e.g. STP, PTP) as opposed to tag_8021q.
 * All DT-defined ports are members of this VLAN, and there are no
 * restrictions on forwarding (since the CPU selects the destination).
 * Frames from this VLAN will always be transmitted as untagged, and
 * neither the bridge nor the 8021q module cannot create this VLAN ID.
 */
static int sja1105_init_static_vlan(struct sja1105_private *priv)
{
	struct sja1105_table *table;
	struct sja1105_vlan_lookup_entry pvid = {
		.type_entry = SJA1110_VLAN_D_TAG,
		.ving_mirr = 0,
		.vegr_mirr = 0,
		.vmemb_port = 0,
		.vlan_bc = 0,
		.tag_port = 0,
		.vlanid = SJA1105_DEFAULT_VLAN,
	};
	struct dsa_switch *ds = priv->ds;
	int port;

	table = &priv->static_config.tables[BLK_IDX_VLAN_LOOKUP];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kzalloc(table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = 1;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		pvid.vmemb_port |= BIT(port);
		pvid.vlan_bc |= BIT(port);
		pvid.tag_port &= ~BIT(port);

		if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)) {
			priv->tag_8021q_pvid[port] = SJA1105_DEFAULT_VLAN;
			priv->bridge_pvid[port] = SJA1105_DEFAULT_VLAN;
		}
	}

	((struct sja1105_vlan_lookup_entry *)table->entries)[0] = pvid;
	return 0;
}

static int sja1105_init_l2_forwarding(struct sja1105_private *priv)
{
	struct sja1105_l2_forwarding_entry *l2fwd;
	struct dsa_switch *ds = priv->ds;
	struct dsa_switch_tree *dst;
	struct sja1105_table *table;
	struct dsa_link *dl;
	int port, tc;
	int from, to;

	table = &priv->static_config.tables[BLK_IDX_L2_FORWARDING];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	l2fwd = table->entries;

	/* First 5 entries in the L2 Forwarding Table define the forwarding
	 * rules and the VLAN PCP to ingress queue mapping.
	 * Set up the ingress queue mapping first.
	 */
	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		for (tc = 0; tc < SJA1105_NUM_TC; tc++)
			l2fwd[port].vlan_pmap[tc] = tc;
	}

	/* Then manage the forwarding domain for user ports. These can forward
	 * only to the always-on domain (CPU port and DSA links)
	 */
	for (from = 0; from < ds->num_ports; from++) {
		if (!dsa_is_user_port(ds, from))
			continue;

		for (to = 0; to < ds->num_ports; to++) {
			if (!dsa_is_cpu_port(ds, to) &&
			    !dsa_is_dsa_port(ds, to))
				continue;

			l2fwd[from].bc_domain |= BIT(to);
			l2fwd[from].fl_domain |= BIT(to);

			sja1105_port_allow_traffic(l2fwd, from, to, true);
		}
	}

	/* Then manage the forwarding domain for DSA links and CPU ports (the
	 * always-on domain). These can send packets to any enabled port except
	 * themselves.
	 */
	for (from = 0; from < ds->num_ports; from++) {
		if (!dsa_is_cpu_port(ds, from) && !dsa_is_dsa_port(ds, from))
			continue;

		for (to = 0; to < ds->num_ports; to++) {
			if (dsa_is_unused_port(ds, to))
				continue;

			if (from == to)
				continue;

			l2fwd[from].bc_domain |= BIT(to);
			l2fwd[from].fl_domain |= BIT(to);

			sja1105_port_allow_traffic(l2fwd, from, to, true);
		}
	}

	/* In odd topologies ("H" connections where there is a DSA link to
	 * another switch which also has its own CPU port), TX packets can loop
	 * back into the system (they are flooded from CPU port 1 to the DSA
	 * link, and from there to CPU port 2). Prevent this from happening by
	 * cutting RX from DSA links towards our CPU port, if the remote switch
	 * has its own CPU port and therefore doesn't need ours for network
	 * stack termination.
	 */
	dst = ds->dst;

	list_for_each_entry(dl, &dst->rtable, list) {
		if (dl->dp->ds != ds || dl->link_dp->cpu_dp == dl->dp->cpu_dp)
			continue;

		from = dl->dp->index;
		to = dsa_upstream_port(ds, from);

		dev_warn(ds->dev,
			 "H topology detected, cutting RX from DSA link %d to CPU port %d to prevent TX packet loops\n",
			 from, to);

		sja1105_port_allow_traffic(l2fwd, from, to, false);

		l2fwd[from].bc_domain &= ~BIT(to);
		l2fwd[from].fl_domain &= ~BIT(to);
	}

	/* Finally, manage the egress flooding domain. All ports start up with
	 * flooding enabled, including the CPU port and DSA links.
	 */
	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		priv->ucast_egress_floods |= BIT(port);
		priv->bcast_egress_floods |= BIT(port);
	}

	/* Next 8 entries define VLAN PCP mapping from ingress to egress.
	 * Create a one-to-one mapping.
	 */
	for (tc = 0; tc < SJA1105_NUM_TC; tc++) {
		for (port = 0; port < ds->num_ports; port++) {
			if (dsa_is_unused_port(ds, port))
				continue;

			l2fwd[ds->num_ports + tc].vlan_pmap[port] = tc;
		}

		l2fwd[ds->num_ports + tc].type_egrpcp2outputq = true;
	}

	return 0;
}

static int sja1110_init_pcp_remapping(struct sja1105_private *priv)
{
	struct sja1110_pcp_remapping_entry *pcp_remap;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_table *table;
	int port, tc;

	table = &priv->static_config.tables[BLK_IDX_PCP_REMAPPING];

	/* Nothing to do for SJA1105 */
	if (!table->ops->max_entry_count)
		return 0;

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	pcp_remap = table->entries;

	/* Repeat the configuration done for vlan_pmap */
	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		for (tc = 0; tc < SJA1105_NUM_TC; tc++)
			pcp_remap[port].egrpcp[tc] = tc;
	}

	return 0;
}

static int sja1105_init_l2_forwarding_params(struct sja1105_private *priv)
{
	struct sja1105_l2_forwarding_params_entry *l2fwd_params;
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_L2_FORWARDING_PARAMS];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	/* This table only has a single entry */
	l2fwd_params = table->entries;

	/* Disallow dynamic reconfiguration of vlan_pmap */
	l2fwd_params->max_dynp = 0;
	/* Use a single memory partition for all ingress queues */
	l2fwd_params->part_spc[0] = priv->info->max_frame_mem;

	return 0;
}

void sja1105_frame_memory_partitioning(struct sja1105_private *priv)
{
	struct sja1105_l2_forwarding_params_entry *l2_fwd_params;
	struct sja1105_vl_forwarding_params_entry *vl_fwd_params;
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_L2_FORWARDING_PARAMS];
	l2_fwd_params = table->entries;
	l2_fwd_params->part_spc[0] = SJA1105_MAX_FRAME_MEMORY;

	/* If we have any critical-traffic virtual links, we need to reserve
	 * some frame buffer memory for them. At the moment, hardcode the value
	 * at 100 blocks of 128 bytes of memory each. This leaves 829 blocks
	 * remaining for best-effort traffic. TODO: figure out a more flexible
	 * way to perform the frame buffer partitioning.
	 */
	if (!priv->static_config.tables[BLK_IDX_VL_FORWARDING].entry_count)
		return;

	table = &priv->static_config.tables[BLK_IDX_VL_FORWARDING_PARAMS];
	vl_fwd_params = table->entries;

	l2_fwd_params->part_spc[0] -= SJA1105_VL_FRAME_MEMORY;
	vl_fwd_params->partspc[0] = SJA1105_VL_FRAME_MEMORY;
}

/* SJA1110 TDMACONFIGIDX values:
 *
 *      | 100 Mbps ports |  1Gbps ports  | 2.5Gbps ports | Disabled ports
 * -----+----------------+---------------+---------------+---------------
 *   0  |   0, [5:10]    |     [1:2]     |     [3:4]     |     retag
 *   1  |0, [5:10], retag|     [1:2]     |     [3:4]     |       -
 *   2  |   0, [5:10]    |  [1:3], retag |       4       |       -
 *   3  |   0, [5:10]    |[1:2], 4, retag|       3       |       -
 *   4  |  0, 2, [5:10]  |    1, retag   |     [3:4]     |       -
 *   5  |  0, 1, [5:10]  |    2, retag   |     [3:4]     |       -
 *  14  |   0, [5:10]    | [1:4], retag  |       -       |       -
 *  15  |     [5:10]     | [0:4], retag  |       -       |       -
 */
static void sja1110_select_tdmaconfigidx(struct sja1105_private *priv)
{
	struct sja1105_general_params_entry *general_params;
	struct sja1105_table *table;
	bool port_1_is_base_tx;
	bool port_3_is_2500;
	bool port_4_is_2500;
	u64 tdmaconfigidx;

	if (priv->info->device_id != SJA1110_DEVICE_ID)
		return;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];
	general_params = table->entries;

	/* All the settings below are "as opposed to SGMII", which is the
	 * other pinmuxing option.
	 */
	port_1_is_base_tx = priv->phy_mode[1] == PHY_INTERFACE_MODE_INTERNAL;
	port_3_is_2500 = priv->phy_mode[3] == PHY_INTERFACE_MODE_2500BASEX;
	port_4_is_2500 = priv->phy_mode[4] == PHY_INTERFACE_MODE_2500BASEX;

	if (port_1_is_base_tx)
		/* Retagging port will operate at 1 Gbps */
		tdmaconfigidx = 5;
	else if (port_3_is_2500 && port_4_is_2500)
		/* Retagging port will operate at 100 Mbps */
		tdmaconfigidx = 1;
	else if (port_3_is_2500)
		/* Retagging port will operate at 1 Gbps */
		tdmaconfigidx = 3;
	else if (port_4_is_2500)
		/* Retagging port will operate at 1 Gbps */
		tdmaconfigidx = 2;
	else
		/* Retagging port will operate at 1 Gbps */
		tdmaconfigidx = 14;

	general_params->tdmaconfigidx = tdmaconfigidx;
}

static int sja1105_init_topology(struct sja1105_private *priv,
				 struct sja1105_general_params_entry *general_params)
{
	struct dsa_switch *ds = priv->ds;
	int port;

	/* The host port is the destination for traffic matching mac_fltres1
	 * and mac_fltres0 on all ports except itself. Default to an invalid
	 * value.
	 */
	general_params->host_port = ds->num_ports;

	/* Link-local traffic received on casc_port will be forwarded
	 * to host_port without embedding the source port and device ID
	 * info in the destination MAC address, and no RX timestamps will be
	 * taken either (presumably because it is a cascaded port and a
	 * downstream SJA switch already did that).
	 * To disable the feature, we need to do different things depending on
	 * switch generation. On SJA1105 we need to set an invalid port, while
	 * on SJA1110 which support multiple cascaded ports, this field is a
	 * bitmask so it must be left zero.
	 */
	if (!priv->info->multiple_cascade_ports)
		general_params->casc_port = ds->num_ports;

	for (port = 0; port < ds->num_ports; port++) {
		bool is_upstream = dsa_is_upstream_port(ds, port);
		bool is_dsa_link = dsa_is_dsa_port(ds, port);

		/* Upstream ports can be dedicated CPU ports or
		 * upstream-facing DSA links
		 */
		if (is_upstream) {
			if (general_params->host_port == ds->num_ports) {
				general_params->host_port = port;
			} else {
				dev_err(ds->dev,
					"Port %llu is already a host port, configuring %d as one too is not supported\n",
					general_params->host_port, port);
				return -EINVAL;
			}
		}

		/* Cascade ports are downstream-facing DSA links */
		if (is_dsa_link && !is_upstream) {
			if (priv->info->multiple_cascade_ports) {
				general_params->casc_port |= BIT(port);
			} else if (general_params->casc_port == ds->num_ports) {
				general_params->casc_port = port;
			} else {
				dev_err(ds->dev,
					"Port %llu is already a cascade port, configuring %d as one too is not supported\n",
					general_params->casc_port, port);
				return -EINVAL;
			}
		}
	}

	if (general_params->host_port == ds->num_ports) {
		dev_err(ds->dev, "No host port configured\n");
		return -EINVAL;
	}

	return 0;
}

static int sja1105_init_general_params(struct sja1105_private *priv)
{
	struct sja1105_general_params_entry default_general_params = {
		/* Allow dynamic changing of the mirror port */
		.mirr_ptacu = true,
		.switchid = priv->ds->index,
		/* Priority queue for link-local management frames
		 * (both ingress to and egress from CPU - PTP, STP etc)
		 */
		.hostprio = 7,
		.mac_fltres1 = SJA1105_LINKLOCAL_FILTER_A,
		.mac_flt1    = SJA1105_LINKLOCAL_FILTER_A_MASK,
		.incl_srcpt1 = false,
		.send_meta1  = false,
		.mac_fltres0 = SJA1105_LINKLOCAL_FILTER_B,
		.mac_flt0    = SJA1105_LINKLOCAL_FILTER_B_MASK,
		.incl_srcpt0 = false,
		.send_meta0  = false,
		/* Default to an invalid value */
		.mirr_port = priv->ds->num_ports,
		/* No TTEthernet */
		.vllupformat = SJA1105_VL_FORMAT_PSFP,
		.vlmarker = 0,
		.vlmask = 0,
		/* Only update correctionField for 1-step PTP (L2 transport) */
		.ignore2stf = 0,
		/* Forcefully disable VLAN filtering by telling
		 * the switch that VLAN has a different EtherType.
		 */
		.tpid = ETH_P_SJA1105,
		.tpid2 = ETH_P_SJA1105,
		/* Enable the TTEthernet engine on SJA1110 */
		.tte_en = true,
		/* Set up the EtherType for control packets on SJA1110 */
		.header_type = ETH_P_SJA1110,
	};
	struct sja1105_general_params_entry *general_params;
	struct sja1105_table *table;
	int rc;

	rc = sja1105_init_topology(priv, &default_general_params);
	if (rc)
		return rc;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	general_params = table->entries;

	/* This table only has a single entry */
	general_params[0] = default_general_params;

	sja1110_select_tdmaconfigidx(priv);

	return 0;
}

static int sja1105_init_avb_params(struct sja1105_private *priv)
{
	struct sja1105_avb_params_entry *avb;
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_AVB_PARAMS];

	/* Discard previous AVB Parameters Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	avb = table->entries;

	/* Configure the MAC addresses for meta frames */
	avb->destmeta = SJA1105_META_DMAC;
	avb->srcmeta  = SJA1105_META_SMAC;
	/* On P/Q/R/S, configure the direction of the PTP_CLK pin as input by
	 * default. This is because there might be boards with a hardware
	 * layout where enabling the pin as output might cause an electrical
	 * clash. On E/T the pin is always an output, which the board designers
	 * probably already knew, so even if there are going to be electrical
	 * issues, there's nothing we can do.
	 */
	avb->cas_master = false;

	return 0;
}

/* The L2 policing table is 2-stage. The table is looked up for each frame
 * according to the ingress port, whether it was broadcast or not, and the
 * classified traffic class (given by VLAN PCP). This portion of the lookup is
 * fixed, and gives access to the SHARINDX, an indirection register pointing
 * within the policing table itself, which is used to resolve the policer that
 * will be used for this frame.
 *
 *  Stage 1                              Stage 2
 * +------------+--------+              +---------------------------------+
 * |Port 0 TC 0 |SHARINDX|              | Policer 0: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 * |Port 0 TC 1 |SHARINDX|              | Policer 1: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 *    ...                               | Policer 2: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 * |Port 0 TC 7 |SHARINDX|              | Policer 3: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 * |Port 1 TC 0 |SHARINDX|              | Policer 4: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 *    ...                               | Policer 5: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 * |Port 1 TC 7 |SHARINDX|              | Policer 6: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 *    ...                               | Policer 7: Rate, Burst, MTU     |
 * +------------+--------+              +---------------------------------+
 * |Port 4 TC 7 |SHARINDX|                 ...
 * +------------+--------+
 * |Port 0 BCAST|SHARINDX|                 ...
 * +------------+--------+
 * |Port 1 BCAST|SHARINDX|                 ...
 * +------------+--------+
 *    ...                                  ...
 * +------------+--------+              +---------------------------------+
 * |Port 4 BCAST|SHARINDX|              | Policer 44: Rate, Burst, MTU    |
 * +------------+--------+              +---------------------------------+
 *
 * In this driver, we shall use policers 0-4 as statically alocated port
 * (matchall) policers. So we need to make the SHARINDX for all lookups
 * corresponding to this ingress port (8 VLAN PCP lookups and 1 broadcast
 * lookup) equal.
 * The remaining policers (40) shall be dynamically allocated for flower
 * policers, where the key is either vlan_prio or dst_mac ff:ff:ff:ff:ff:ff.
 */
#define SJA1105_RATE_MBPS(speed) (((speed) * 64000) / 1000)

static int sja1105_init_l2_policing(struct sja1105_private *priv)
{
	struct sja1105_l2_policing_entry *policing;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_table *table;
	int port, tc;

	table = &priv->static_config.tables[BLK_IDX_L2_POLICING];

	/* Discard previous L2 Policing Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(table->ops->max_entry_count,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = table->ops->max_entry_count;

	policing = table->entries;

	/* Setup shared indices for the matchall policers */
	for (port = 0; port < ds->num_ports; port++) {
		int mcast = (ds->num_ports * (SJA1105_NUM_TC + 1)) + port;
		int bcast = (ds->num_ports * SJA1105_NUM_TC) + port;

		for (tc = 0; tc < SJA1105_NUM_TC; tc++)
			policing[port * SJA1105_NUM_TC + tc].sharindx = port;

		policing[bcast].sharindx = port;
		/* Only SJA1110 has multicast policers */
		if (mcast <= table->ops->max_entry_count)
			policing[mcast].sharindx = port;
	}

	/* Setup the matchall policer parameters */
	for (port = 0; port < ds->num_ports; port++) {
		int mtu = VLAN_ETH_FRAME_LEN + ETH_FCS_LEN;

		if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port))
			mtu += VLAN_HLEN;

		policing[port].smax = 65535; /* Burst size in bytes */
		policing[port].rate = SJA1105_RATE_MBPS(1000);
		policing[port].maxlen = mtu;
		policing[port].partition = 0;
	}

	return 0;
}

static int sja1105_static_config_load(struct sja1105_private *priv)
{
	int rc;

	sja1105_static_config_free(&priv->static_config);
	rc = sja1105_static_config_init(&priv->static_config,
					priv->info->static_ops,
					priv->info->device_id);
	if (rc)
		return rc;

	/* Build static configuration */
	rc = sja1105_init_mac_settings(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_mii_settings(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_static_fdb(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_static_vlan(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_l2_lookup_params(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_l2_forwarding(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_l2_forwarding_params(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_l2_policing(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_general_params(priv);
	if (rc < 0)
		return rc;
	rc = sja1105_init_avb_params(priv);
	if (rc < 0)
		return rc;
	rc = sja1110_init_pcp_remapping(priv);
	if (rc < 0)
		return rc;

	/* Send initial configuration to hardware via SPI */
	return sja1105_static_config_upload(priv);
}

static int sja1105_parse_rgmii_delays(struct sja1105_private *priv)
{
	struct dsa_switch *ds = priv->ds;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		if (!priv->fixed_link[port])
			continue;

		if (priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_RXID ||
		    priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_ID)
			priv->rgmii_rx_delay[port] = true;

		if (priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_TXID ||
		    priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_ID)
			priv->rgmii_tx_delay[port] = true;

		if ((priv->rgmii_rx_delay[port] || priv->rgmii_tx_delay[port]) &&
		    !priv->info->setup_rgmii_delay)
			return -EINVAL;
	}
	return 0;
}

static int sja1105_parse_ports_node(struct sja1105_private *priv,
				    struct device_node *ports_node)
{
	struct device *dev = &priv->spidev->dev;
	struct device_node *child;

	for_each_available_child_of_node(ports_node, child) {
		struct device_node *phy_node;
		phy_interface_t phy_mode;
		u32 index;
		int err;

		/* Get switch port number from DT */
		if (of_property_read_u32(child, "reg", &index) < 0) {
			dev_err(dev, "Port number not defined in device tree "
				"(property \"reg\")\n");
			of_node_put(child);
			return -ENODEV;
		}

		/* Get PHY mode from DT */
		err = of_get_phy_mode(child, &phy_mode);
		if (err) {
			dev_err(dev, "Failed to read phy-mode or "
				"phy-interface-type property for port %d\n",
				index);
			of_node_put(child);
			return -ENODEV;
		}

		phy_node = of_parse_phandle(child, "phy-handle", 0);
		if (!phy_node) {
			if (!of_phy_is_fixed_link(child)) {
				dev_err(dev, "phy-handle or fixed-link "
					"properties missing!\n");
				of_node_put(child);
				return -ENODEV;
			}
			/* phy-handle is missing, but fixed-link isn't.
			 * So it's a fixed link. Default to PHY role.
			 */
			priv->fixed_link[index] = true;
		} else {
			of_node_put(phy_node);
		}

		priv->phy_mode[index] = phy_mode;
	}

	return 0;
}

static int sja1105_parse_dt(struct sja1105_private *priv)
{
	struct device *dev = &priv->spidev->dev;
	struct device_node *switch_node = dev->of_node;
	struct device_node *ports_node;
	int rc;

	ports_node = of_get_child_by_name(switch_node, "ports");
	if (!ports_node)
		ports_node = of_get_child_by_name(switch_node, "ethernet-ports");
	if (!ports_node) {
		dev_err(dev, "Incorrect bindings: absent \"ports\" node\n");
		return -ENODEV;
	}

	rc = sja1105_parse_ports_node(priv, ports_node);
	of_node_put(ports_node);

	return rc;
}

/* Convert link speed from SJA1105 to ethtool encoding */
static int sja1105_port_speed_to_ethtool(struct sja1105_private *priv,
					 u64 speed)
{
	if (speed == priv->info->port_speed[SJA1105_SPEED_10MBPS])
		return SPEED_10;
	if (speed == priv->info->port_speed[SJA1105_SPEED_100MBPS])
		return SPEED_100;
	if (speed == priv->info->port_speed[SJA1105_SPEED_1000MBPS])
		return SPEED_1000;
	if (speed == priv->info->port_speed[SJA1105_SPEED_2500MBPS])
		return SPEED_2500;
	return SPEED_UNKNOWN;
}

/* Set link speed in the MAC configuration for a specific port. */
static int sja1105_adjust_port_config(struct sja1105_private *priv, int port,
				      int speed_mbps)
{
	struct sja1105_mac_config_entry *mac;
	struct device *dev = priv->ds->dev;
	u64 speed;
	int rc;

	/* On P/Q/R/S, one can read from the device via the MAC reconfiguration
	 * tables. On E/T, MAC reconfig tables are not readable, only writable.
	 * We have to *know* what the MAC looks like.  For the sake of keeping
	 * the code common, we'll use the static configuration tables as a
	 * reasonable approximation for both E/T and P/Q/R/S.
	 */
	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	switch (speed_mbps) {
	case SPEED_UNKNOWN:
		/* PHYLINK called sja1105_mac_config() to inform us about
		 * the state->interface, but AN has not completed and the
		 * speed is not yet valid. UM10944.pdf says that setting
		 * SJA1105_SPEED_AUTO at runtime disables the port, so that is
		 * ok for power consumption in case AN will never complete -
		 * otherwise PHYLINK should come back with a new update.
		 */
		speed = priv->info->port_speed[SJA1105_SPEED_AUTO];
		break;
	case SPEED_10:
		speed = priv->info->port_speed[SJA1105_SPEED_10MBPS];
		break;
	case SPEED_100:
		speed = priv->info->port_speed[SJA1105_SPEED_100MBPS];
		break;
	case SPEED_1000:
		speed = priv->info->port_speed[SJA1105_SPEED_1000MBPS];
		break;
	case SPEED_2500:
		speed = priv->info->port_speed[SJA1105_SPEED_2500MBPS];
		break;
	default:
		dev_err(dev, "Invalid speed %iMbps\n", speed_mbps);
		return -EINVAL;
	}

	/* Overwrite SJA1105_SPEED_AUTO from the static MAC configuration
	 * table, since this will be used for the clocking setup, and we no
	 * longer need to store it in the static config (already told hardware
	 * we want auto during upload phase).
	 * Actually for the SGMII port, the MAC is fixed at 1 Gbps and
	 * we need to configure the PCS only (if even that).
	 */
	if (priv->phy_mode[port] == PHY_INTERFACE_MODE_SGMII)
		mac[port].speed = priv->info->port_speed[SJA1105_SPEED_1000MBPS];
	else if (priv->phy_mode[port] == PHY_INTERFACE_MODE_2500BASEX)
		mac[port].speed = priv->info->port_speed[SJA1105_SPEED_2500MBPS];
	else
		mac[port].speed = speed;

	/* Write to the dynamic reconfiguration tables */
	rc = sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, port,
					  &mac[port], true);
	if (rc < 0) {
		dev_err(dev, "Failed to write MAC config: %d\n", rc);
		return rc;
	}

	/* Reconfigure the PLLs for the RGMII interfaces (required 125 MHz at
	 * gigabit, 25 MHz at 100 Mbps and 2.5 MHz at 10 Mbps). For MII and
	 * RMII no change of the clock setup is required. Actually, changing
	 * the clock setup does interrupt the clock signal for a certain time
	 * which causes trouble for all PHYs relying on this signal.
	 */
	if (!phy_interface_mode_is_rgmii(priv->phy_mode[port]))
		return 0;

	return sja1105_clocking_setup_port(priv, port);
}

/* The SJA1105 MAC programming model is through the static config (the xMII
 * Mode table cannot be dynamically reconfigured), and we have to program
 * that early (earlier than PHYLINK calls us, anyway).
 * So just error out in case the connected PHY attempts to change the initial
 * system interface MII protocol from what is defined in the DT, at least for
 * now.
 */
static bool sja1105_phy_mode_mismatch(struct sja1105_private *priv, int port,
				      phy_interface_t interface)
{
	return priv->phy_mode[port] != interface;
}

static void sja1105_mac_config(struct dsa_switch *ds, int port,
			       unsigned int mode,
			       const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct sja1105_private *priv = ds->priv;
	struct dw_xpcs *xpcs;

	if (sja1105_phy_mode_mismatch(priv, port, state->interface)) {
		dev_err(ds->dev, "Changing PHY mode to %s not supported!\n",
			phy_modes(state->interface));
		return;
	}

	xpcs = priv->xpcs[port];

	if (xpcs)
		phylink_set_pcs(dp->pl, &xpcs->pcs);
}

static void sja1105_mac_link_down(struct dsa_switch *ds, int port,
				  unsigned int mode,
				  phy_interface_t interface)
{
	sja1105_inhibit_tx(ds->priv, BIT(port), true);
}

static void sja1105_mac_link_up(struct dsa_switch *ds, int port,
				unsigned int mode,
				phy_interface_t interface,
				struct phy_device *phydev,
				int speed, int duplex,
				bool tx_pause, bool rx_pause)
{
	struct sja1105_private *priv = ds->priv;

	sja1105_adjust_port_config(priv, port, speed);

	sja1105_inhibit_tx(priv, BIT(port), false);
}

static void sja1105_phylink_validate(struct dsa_switch *ds, int port,
				     unsigned long *supported,
				     struct phylink_link_state *state)
{
	/* Construct a new mask which exhaustively contains all link features
	 * supported by the MAC, and then apply that (logical AND) to what will
	 * be sent to the PHY for "marketing".
	 */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	struct sja1105_private *priv = ds->priv;
	struct sja1105_xmii_params_entry *mii;

	mii = priv->static_config.tables[BLK_IDX_XMII_PARAMS].entries;

	/* include/linux/phylink.h says:
	 *     When @state->interface is %PHY_INTERFACE_MODE_NA, phylink
	 *     expects the MAC driver to return all supported link modes.
	 */
	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    sja1105_phy_mode_mismatch(priv, port, state->interface)) {
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		return;
	}

	/* The MAC does not support pause frames, and also doesn't
	 * support half-duplex traffic modes.
	 */
	phylink_set(mask, Autoneg);
	phylink_set(mask, MII);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 100baseT1_Full);
	if (mii->xmii_mode[port] == XMII_MODE_RGMII ||
	    mii->xmii_mode[port] == XMII_MODE_SGMII)
		phylink_set(mask, 1000baseT_Full);
	if (priv->info->supports_2500basex[port]) {
		phylink_set(mask, 2500baseT_Full);
		phylink_set(mask, 2500baseX_Full);
	}

	bitmap_and(supported, supported, mask, __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static int
sja1105_find_static_fdb_entry(struct sja1105_private *priv, int port,
			      const struct sja1105_l2_lookup_entry *requested)
{
	struct sja1105_l2_lookup_entry *l2_lookup;
	struct sja1105_table *table;
	int i;

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP];
	l2_lookup = table->entries;

	for (i = 0; i < table->entry_count; i++)
		if (l2_lookup[i].macaddr == requested->macaddr &&
		    l2_lookup[i].vlanid == requested->vlanid &&
		    l2_lookup[i].destports & BIT(port))
			return i;

	return -1;
}

/* We want FDB entries added statically through the bridge command to persist
 * across switch resets, which are a common thing during normal SJA1105
 * operation. So we have to back them up in the static configuration tables
 * and hence apply them on next static config upload... yay!
 */
static int
sja1105_static_fdb_change(struct sja1105_private *priv, int port,
			  const struct sja1105_l2_lookup_entry *requested,
			  bool keep)
{
	struct sja1105_l2_lookup_entry *l2_lookup;
	struct sja1105_table *table;
	int rc, match;

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP];

	match = sja1105_find_static_fdb_entry(priv, port, requested);
	if (match < 0) {
		/* Can't delete a missing entry. */
		if (!keep)
			return 0;

		/* No match => new entry */
		rc = sja1105_table_resize(table, table->entry_count + 1);
		if (rc)
			return rc;

		match = table->entry_count - 1;
	}

	/* Assign pointer after the resize (it may be new memory) */
	l2_lookup = table->entries;

	/* We have a match.
	 * If the job was to add this FDB entry, it's already done (mostly
	 * anyway, since the port forwarding mask may have changed, case in
	 * which we update it).
	 * Otherwise we have to delete it.
	 */
	if (keep) {
		l2_lookup[match] = *requested;
		return 0;
	}

	/* To remove, the strategy is to overwrite the element with
	 * the last one, and then reduce the array size by 1
	 */
	l2_lookup[match] = l2_lookup[table->entry_count - 1];
	return sja1105_table_resize(table, table->entry_count - 1);
}

/* First-generation switches have a 4-way set associative TCAM that
 * holds the FDB entries. An FDB index spans from 0 to 1023 and is comprised of
 * a "bin" (grouping of 4 entries) and a "way" (an entry within a bin).
 * For the placement of a newly learnt FDB entry, the switch selects the bin
 * based on a hash function, and the way within that bin incrementally.
 */
static int sja1105et_fdb_index(int bin, int way)
{
	return bin * SJA1105ET_FDB_BIN_SIZE + way;
}

static int sja1105et_is_fdb_entry_in_bin(struct sja1105_private *priv, int bin,
					 const u8 *addr, u16 vid,
					 struct sja1105_l2_lookup_entry *match,
					 int *last_unused)
{
	int way;

	for (way = 0; way < SJA1105ET_FDB_BIN_SIZE; way++) {
		struct sja1105_l2_lookup_entry l2_lookup = {0};
		int index = sja1105et_fdb_index(bin, way);

		/* Skip unused entries, optionally marking them
		 * into the return value
		 */
		if (sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
						index, &l2_lookup)) {
			if (last_unused)
				*last_unused = way;
			continue;
		}

		if (l2_lookup.macaddr == ether_addr_to_u64(addr) &&
		    l2_lookup.vlanid == vid) {
			if (match)
				*match = l2_lookup;
			return way;
		}
	}
	/* Return an invalid entry index if not found */
	return -1;
}

int sja1105et_fdb_add(struct dsa_switch *ds, int port,
		      const unsigned char *addr, u16 vid)
{
	struct sja1105_l2_lookup_entry l2_lookup = {0}, tmp;
	struct sja1105_private *priv = ds->priv;
	struct device *dev = ds->dev;
	int last_unused = -1;
	int start, end, i;
	int bin, way, rc;

	bin = sja1105et_fdb_hash(priv, addr, vid);

	way = sja1105et_is_fdb_entry_in_bin(priv, bin, addr, vid,
					    &l2_lookup, &last_unused);
	if (way >= 0) {
		/* We have an FDB entry. Is our port in the destination
		 * mask? If yes, we need to do nothing. If not, we need
		 * to rewrite the entry by adding this port to it.
		 */
		if ((l2_lookup.destports & BIT(port)) && l2_lookup.lockeds)
			return 0;
		l2_lookup.destports |= BIT(port);
	} else {
		int index = sja1105et_fdb_index(bin, way);

		/* We don't have an FDB entry. We construct a new one and
		 * try to find a place for it within the FDB table.
		 */
		l2_lookup.macaddr = ether_addr_to_u64(addr);
		l2_lookup.destports = BIT(port);
		l2_lookup.vlanid = vid;

		if (last_unused >= 0) {
			way = last_unused;
		} else {
			/* Bin is full, need to evict somebody.
			 * Choose victim at random. If you get these messages
			 * often, you may need to consider changing the
			 * distribution function:
			 * static_config[BLK_IDX_L2_LOOKUP_PARAMS].entries->poly
			 */
			get_random_bytes(&way, sizeof(u8));
			way %= SJA1105ET_FDB_BIN_SIZE;
			dev_warn(dev, "Warning, FDB bin %d full while adding entry for %pM. Evicting entry %u.\n",
				 bin, addr, way);
			/* Evict entry */
			sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
						     index, NULL, false);
		}
	}
	l2_lookup.lockeds = true;
	l2_lookup.index = sja1105et_fdb_index(bin, way);

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					  l2_lookup.index, &l2_lookup,
					  true);
	if (rc < 0)
		return rc;

	/* Invalidate a dynamically learned entry if that exists */
	start = sja1105et_fdb_index(bin, 0);
	end = sja1105et_fdb_index(bin, way);

	for (i = start; i < end; i++) {
		rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
						 i, &tmp);
		if (rc == -ENOENT)
			continue;
		if (rc)
			return rc;

		if (tmp.macaddr != ether_addr_to_u64(addr) || tmp.vlanid != vid)
			continue;

		rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
						  i, NULL, false);
		if (rc)
			return rc;

		break;
	}

	return sja1105_static_fdb_change(priv, port, &l2_lookup, true);
}

int sja1105et_fdb_del(struct dsa_switch *ds, int port,
		      const unsigned char *addr, u16 vid)
{
	struct sja1105_l2_lookup_entry l2_lookup = {0};
	struct sja1105_private *priv = ds->priv;
	int index, bin, way, rc;
	bool keep;

	bin = sja1105et_fdb_hash(priv, addr, vid);
	way = sja1105et_is_fdb_entry_in_bin(priv, bin, addr, vid,
					    &l2_lookup, NULL);
	if (way < 0)
		return 0;
	index = sja1105et_fdb_index(bin, way);

	/* We have an FDB entry. Is our port in the destination mask? If yes,
	 * we need to remove it. If the resulting port mask becomes empty, we
	 * need to completely evict the FDB entry.
	 * Otherwise we just write it back.
	 */
	l2_lookup.destports &= ~BIT(port);

	if (l2_lookup.destports)
		keep = true;
	else
		keep = false;

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					  index, &l2_lookup, keep);
	if (rc < 0)
		return rc;

	return sja1105_static_fdb_change(priv, port, &l2_lookup, keep);
}

int sja1105pqrs_fdb_add(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid)
{
	struct sja1105_l2_lookup_entry l2_lookup = {0}, tmp;
	struct sja1105_private *priv = ds->priv;
	int rc, i;

	/* Search for an existing entry in the FDB table */
	l2_lookup.macaddr = ether_addr_to_u64(addr);
	l2_lookup.vlanid = vid;
	l2_lookup.mask_macaddr = GENMASK_ULL(ETH_ALEN * 8 - 1, 0);
	l2_lookup.mask_vlanid = VLAN_VID_MASK;
	l2_lookup.destports = BIT(port);

	tmp = l2_lookup;

	rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
					 SJA1105_SEARCH, &tmp);
	if (rc == 0 && tmp.index != SJA1105_MAX_L2_LOOKUP_COUNT - 1) {
		/* Found a static entry and this port is already in the entry's
		 * port mask => job done
		 */
		if ((tmp.destports & BIT(port)) && tmp.lockeds)
			return 0;

		l2_lookup = tmp;

		/* l2_lookup.index is populated by the switch in case it
		 * found something.
		 */
		l2_lookup.destports |= BIT(port);
		goto skip_finding_an_index;
	}

	/* Not found, so try to find an unused spot in the FDB.
	 * This is slightly inefficient because the strategy is knock-knock at
	 * every possible position from 0 to 1023.
	 */
	for (i = 0; i < SJA1105_MAX_L2_LOOKUP_COUNT; i++) {
		rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
						 i, NULL);
		if (rc < 0)
			break;
	}
	if (i == SJA1105_MAX_L2_LOOKUP_COUNT) {
		dev_err(ds->dev, "FDB is full, cannot add entry.\n");
		return -EINVAL;
	}
	l2_lookup.index = i;

skip_finding_an_index:
	l2_lookup.lockeds = true;

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					  l2_lookup.index, &l2_lookup,
					  true);
	if (rc < 0)
		return rc;

	/* The switch learns dynamic entries and looks up the FDB left to
	 * right. It is possible that our addition was concurrent with the
	 * dynamic learning of the same address, so now that the static entry
	 * has been installed, we are certain that address learning for this
	 * particular address has been turned off, so the dynamic entry either
	 * is in the FDB at an index smaller than the static one, or isn't (it
	 * can also be at a larger index, but in that case it is inactive
	 * because the static FDB entry will match first, and the dynamic one
	 * will eventually age out). Search for a dynamically learned address
	 * prior to our static one and invalidate it.
	 */
	tmp = l2_lookup;

	rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
					 SJA1105_SEARCH, &tmp);
	if (rc < 0) {
		dev_err(ds->dev,
			"port %d failed to read back entry for %pM vid %d: %pe\n",
			port, addr, vid, ERR_PTR(rc));
		return rc;
	}

	if (tmp.index < l2_lookup.index) {
		rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
						  tmp.index, NULL, false);
		if (rc < 0)
			return rc;
	}

	return sja1105_static_fdb_change(priv, port, &l2_lookup, true);
}

int sja1105pqrs_fdb_del(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid)
{
	struct sja1105_l2_lookup_entry l2_lookup = {0};
	struct sja1105_private *priv = ds->priv;
	bool keep;
	int rc;

	l2_lookup.macaddr = ether_addr_to_u64(addr);
	l2_lookup.vlanid = vid;
	l2_lookup.mask_macaddr = GENMASK_ULL(ETH_ALEN * 8 - 1, 0);
	l2_lookup.mask_vlanid = VLAN_VID_MASK;
	l2_lookup.destports = BIT(port);

	rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
					 SJA1105_SEARCH, &l2_lookup);
	if (rc < 0)
		return 0;

	l2_lookup.destports &= ~BIT(port);

	/* Decide whether we remove just this port from the FDB entry,
	 * or if we remove it completely.
	 */
	if (l2_lookup.destports)
		keep = true;
	else
		keep = false;

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					  l2_lookup.index, &l2_lookup, keep);
	if (rc < 0)
		return rc;

	return sja1105_static_fdb_change(priv, port, &l2_lookup, keep);
}

static int sja1105_fdb_add(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid)
{
	struct sja1105_private *priv = ds->priv;

	return priv->info->fdb_add_cmd(ds, port, addr, vid);
}

static int sja1105_fdb_del(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid)
{
	struct sja1105_private *priv = ds->priv;

	return priv->info->fdb_del_cmd(ds, port, addr, vid);
}

static int sja1105_fdb_dump(struct dsa_switch *ds, int port,
			    dsa_fdb_dump_cb_t *cb, void *data)
{
	struct sja1105_private *priv = ds->priv;
	struct device *dev = ds->dev;
	int i;

	for (i = 0; i < SJA1105_MAX_L2_LOOKUP_COUNT; i++) {
		struct sja1105_l2_lookup_entry l2_lookup = {0};
		u8 macaddr[ETH_ALEN];
		int rc;

		rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
						 i, &l2_lookup);
		/* No fdb entry at i, not an issue */
		if (rc == -ENOENT)
			continue;
		if (rc) {
			dev_err(dev, "Failed to dump FDB: %d\n", rc);
			return rc;
		}

		/* FDB dump callback is per port. This means we have to
		 * disregard a valid entry if it's not for this port, even if
		 * only to revisit it later. This is inefficient because the
		 * 1024-sized FDB table needs to be traversed 4 times through
		 * SPI during a 'bridge fdb show' command.
		 */
		if (!(l2_lookup.destports & BIT(port)))
			continue;

		/* We need to hide the FDB entry for unknown multicast */
		if (l2_lookup.macaddr == SJA1105_UNKNOWN_MULTICAST &&
		    l2_lookup.mask_macaddr == SJA1105_UNKNOWN_MULTICAST)
			continue;

		u64_to_ether_addr(l2_lookup.macaddr, macaddr);

		/* We need to hide the dsa_8021q VLANs from the user. */
		if (!priv->vlan_aware)
			l2_lookup.vlanid = 0;
		rc = cb(macaddr, l2_lookup.vlanid, l2_lookup.lockeds, data);
		if (rc)
			return rc;
	}
	return 0;
}

static void sja1105_fast_age(struct dsa_switch *ds, int port)
{
	struct sja1105_private *priv = ds->priv;
	int i;

	for (i = 0; i < SJA1105_MAX_L2_LOOKUP_COUNT; i++) {
		struct sja1105_l2_lookup_entry l2_lookup = {0};
		u8 macaddr[ETH_ALEN];
		int rc;

		rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
						 i, &l2_lookup);
		/* No fdb entry at i, not an issue */
		if (rc == -ENOENT)
			continue;
		if (rc) {
			dev_err(ds->dev, "Failed to read FDB: %pe\n",
				ERR_PTR(rc));
			return;
		}

		if (!(l2_lookup.destports & BIT(port)))
			continue;

		/* Don't delete static FDB entries */
		if (l2_lookup.lockeds)
			continue;

		u64_to_ether_addr(l2_lookup.macaddr, macaddr);

		rc = sja1105_fdb_del(ds, port, macaddr, l2_lookup.vlanid);
		if (rc) {
			dev_err(ds->dev,
				"Failed to delete FDB entry %pM vid %lld: %pe\n",
				macaddr, l2_lookup.vlanid, ERR_PTR(rc));
			return;
		}
	}
}

static int sja1105_mdb_add(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_mdb *mdb)
{
	return sja1105_fdb_add(ds, port, mdb->addr, mdb->vid);
}

static int sja1105_mdb_del(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_mdb *mdb)
{
	return sja1105_fdb_del(ds, port, mdb->addr, mdb->vid);
}

/* Common function for unicast and broadcast flood configuration.
 * Flooding is configured between each {ingress, egress} port pair, and since
 * the bridge's semantics are those of "egress flooding", it means we must
 * enable flooding towards this port from all ingress ports that are in the
 * same forwarding domain.
 */
static int sja1105_manage_flood_domains(struct sja1105_private *priv)
{
	struct sja1105_l2_forwarding_entry *l2_fwd;
	struct dsa_switch *ds = priv->ds;
	int from, to, rc;

	l2_fwd = priv->static_config.tables[BLK_IDX_L2_FORWARDING].entries;

	for (from = 0; from < ds->num_ports; from++) {
		u64 fl_domain = 0, bc_domain = 0;

		for (to = 0; to < priv->ds->num_ports; to++) {
			if (!sja1105_can_forward(l2_fwd, from, to))
				continue;

			if (priv->ucast_egress_floods & BIT(to))
				fl_domain |= BIT(to);
			if (priv->bcast_egress_floods & BIT(to))
				bc_domain |= BIT(to);
		}

		/* Nothing changed, nothing to do */
		if (l2_fwd[from].fl_domain == fl_domain &&
		    l2_fwd[from].bc_domain == bc_domain)
			continue;

		l2_fwd[from].fl_domain = fl_domain;
		l2_fwd[from].bc_domain = bc_domain;

		rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_FORWARDING,
						  from, &l2_fwd[from], true);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int sja1105_bridge_member(struct dsa_switch *ds, int port,
				 struct net_device *br, bool member)
{
	struct sja1105_l2_forwarding_entry *l2_fwd;
	struct sja1105_private *priv = ds->priv;
	int i, rc;

	l2_fwd = priv->static_config.tables[BLK_IDX_L2_FORWARDING].entries;

	for (i = 0; i < ds->num_ports; i++) {
		/* Add this port to the forwarding matrix of the
		 * other ports in the same bridge, and viceversa.
		 */
		if (!dsa_is_user_port(ds, i))
			continue;
		/* For the ports already under the bridge, only one thing needs
		 * to be done, and that is to add this port to their
		 * reachability domain. So we can perform the SPI write for
		 * them immediately. However, for this port itself (the one
		 * that is new to the bridge), we need to add all other ports
		 * to its reachability domain. So we do that incrementally in
		 * this loop, and perform the SPI write only at the end, once
		 * the domain contains all other bridge ports.
		 */
		if (i == port)
			continue;
		if (dsa_to_port(ds, i)->bridge_dev != br)
			continue;
		sja1105_port_allow_traffic(l2_fwd, i, port, member);
		sja1105_port_allow_traffic(l2_fwd, port, i, member);

		rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_FORWARDING,
						  i, &l2_fwd[i], true);
		if (rc < 0)
			return rc;
	}

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_FORWARDING,
					  port, &l2_fwd[port], true);
	if (rc)
		return rc;

	rc = sja1105_commit_pvid(ds, port);
	if (rc)
		return rc;

	return sja1105_manage_flood_domains(priv);
}

static void sja1105_bridge_stp_state_set(struct dsa_switch *ds, int port,
					 u8 state)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct sja1105_private *priv = ds->priv;
	struct sja1105_mac_config_entry *mac;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	switch (state) {
	case BR_STATE_DISABLED:
	case BR_STATE_BLOCKING:
		/* From UM10944 description of DRPDTAG (why put this there?):
		 * "Management traffic flows to the port regardless of the state
		 * of the INGRESS flag". So BPDUs are still be allowed to pass.
		 * At the moment no difference between DISABLED and BLOCKING.
		 */
		mac[port].ingress   = false;
		mac[port].egress    = false;
		mac[port].dyn_learn = false;
		break;
	case BR_STATE_LISTENING:
		mac[port].ingress   = true;
		mac[port].egress    = false;
		mac[port].dyn_learn = false;
		break;
	case BR_STATE_LEARNING:
		mac[port].ingress   = true;
		mac[port].egress    = false;
		mac[port].dyn_learn = dp->learning;
		break;
	case BR_STATE_FORWARDING:
		mac[port].ingress   = true;
		mac[port].egress    = true;
		mac[port].dyn_learn = dp->learning;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, port,
				     &mac[port], true);
}

static int sja1105_bridge_join(struct dsa_switch *ds, int port,
			       struct net_device *br)
{
	return sja1105_bridge_member(ds, port, br, true);
}

static void sja1105_bridge_leave(struct dsa_switch *ds, int port,
				 struct net_device *br)
{
	sja1105_bridge_member(ds, port, br, false);
}

#define BYTES_PER_KBIT (1000LL / 8)

static int sja1105_find_unused_cbs_shaper(struct sja1105_private *priv)
{
	int i;

	for (i = 0; i < priv->info->num_cbs_shapers; i++)
		if (!priv->cbs[i].idle_slope && !priv->cbs[i].send_slope)
			return i;

	return -1;
}

static int sja1105_delete_cbs_shaper(struct sja1105_private *priv, int port,
				     int prio)
{
	int i;

	for (i = 0; i < priv->info->num_cbs_shapers; i++) {
		struct sja1105_cbs_entry *cbs = &priv->cbs[i];

		if (cbs->port == port && cbs->prio == prio) {
			memset(cbs, 0, sizeof(*cbs));
			return sja1105_dynamic_config_write(priv, BLK_IDX_CBS,
							    i, cbs, true);
		}
	}

	return 0;
}

static int sja1105_setup_tc_cbs(struct dsa_switch *ds, int port,
				struct tc_cbs_qopt_offload *offload)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_cbs_entry *cbs;
	int index;

	if (!offload->enable)
		return sja1105_delete_cbs_shaper(priv, port, offload->queue);

	index = sja1105_find_unused_cbs_shaper(priv);
	if (index < 0)
		return -ENOSPC;

	cbs = &priv->cbs[index];
	cbs->port = port;
	cbs->prio = offload->queue;
	/* locredit and sendslope are negative by definition. In hardware,
	 * positive values must be provided, and the negative sign is implicit.
	 */
	cbs->credit_hi = offload->hicredit;
	cbs->credit_lo = abs(offload->locredit);
	/* User space is in kbits/sec, hardware in bytes/sec */
	cbs->idle_slope = offload->idleslope * BYTES_PER_KBIT;
	cbs->send_slope = abs(offload->sendslope * BYTES_PER_KBIT);
	/* Convert the negative values from 64-bit 2's complement
	 * to 32-bit 2's complement (for the case of 0x80000000 whose
	 * negative is still negative).
	 */
	cbs->credit_lo &= GENMASK_ULL(31, 0);
	cbs->send_slope &= GENMASK_ULL(31, 0);

	return sja1105_dynamic_config_write(priv, BLK_IDX_CBS, index, cbs,
					    true);
}

static int sja1105_reload_cbs(struct sja1105_private *priv)
{
	int rc = 0, i;

	/* The credit based shapers are only allocated if
	 * CONFIG_NET_SCH_CBS is enabled.
	 */
	if (!priv->cbs)
		return 0;

	for (i = 0; i < priv->info->num_cbs_shapers; i++) {
		struct sja1105_cbs_entry *cbs = &priv->cbs[i];

		if (!cbs->idle_slope && !cbs->send_slope)
			continue;

		rc = sja1105_dynamic_config_write(priv, BLK_IDX_CBS, i, cbs,
						  true);
		if (rc)
			break;
	}

	return rc;
}

static const char * const sja1105_reset_reasons[] = {
	[SJA1105_VLAN_FILTERING] = "VLAN filtering",
	[SJA1105_RX_HWTSTAMPING] = "RX timestamping",
	[SJA1105_AGEING_TIME] = "Ageing time",
	[SJA1105_SCHEDULING] = "Time-aware scheduling",
	[SJA1105_BEST_EFFORT_POLICING] = "Best-effort policing",
	[SJA1105_VIRTUAL_LINKS] = "Virtual links",
};

/* For situations where we need to change a setting at runtime that is only
 * available through the static configuration, resetting the switch in order
 * to upload the new static config is unavoidable. Back up the settings we
 * modify at runtime (currently only MAC) and restore them after uploading,
 * such that this operation is relatively seamless.
 */
int sja1105_static_config_reload(struct sja1105_private *priv,
				 enum sja1105_reset_reason reason)
{
	struct ptp_system_timestamp ptp_sts_before;
	struct ptp_system_timestamp ptp_sts_after;
	int speed_mbps[SJA1105_MAX_NUM_PORTS];
	u16 bmcr[SJA1105_MAX_NUM_PORTS] = {0};
	struct sja1105_mac_config_entry *mac;
	struct dsa_switch *ds = priv->ds;
	s64 t1, t2, t3, t4;
	s64 t12, t34;
	int rc, i;
	s64 now;

	mutex_lock(&priv->mgmt_lock);

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	/* Back up the dynamic link speed changed by sja1105_adjust_port_config
	 * in order to temporarily restore it to SJA1105_SPEED_AUTO - which the
	 * switch wants to see in the static config in order to allow us to
	 * change it through the dynamic interface later.
	 */
	for (i = 0; i < ds->num_ports; i++) {
		u32 reg_addr = mdiobus_c45_addr(MDIO_MMD_VEND2, MDIO_CTRL1);

		speed_mbps[i] = sja1105_port_speed_to_ethtool(priv,
							      mac[i].speed);
		mac[i].speed = priv->info->port_speed[SJA1105_SPEED_AUTO];

		if (priv->xpcs[i])
			bmcr[i] = mdiobus_read(priv->mdio_pcs, i, reg_addr);
	}

	/* No PTP operations can run right now */
	mutex_lock(&priv->ptp_data.lock);

	rc = __sja1105_ptp_gettimex(ds, &now, &ptp_sts_before);
	if (rc < 0) {
		mutex_unlock(&priv->ptp_data.lock);
		goto out;
	}

	/* Reset switch and send updated static configuration */
	rc = sja1105_static_config_upload(priv);
	if (rc < 0) {
		mutex_unlock(&priv->ptp_data.lock);
		goto out;
	}

	rc = __sja1105_ptp_settime(ds, 0, &ptp_sts_after);
	if (rc < 0) {
		mutex_unlock(&priv->ptp_data.lock);
		goto out;
	}

	t1 = timespec64_to_ns(&ptp_sts_before.pre_ts);
	t2 = timespec64_to_ns(&ptp_sts_before.post_ts);
	t3 = timespec64_to_ns(&ptp_sts_after.pre_ts);
	t4 = timespec64_to_ns(&ptp_sts_after.post_ts);
	/* Mid point, corresponds to pre-reset PTPCLKVAL */
	t12 = t1 + (t2 - t1) / 2;
	/* Mid point, corresponds to post-reset PTPCLKVAL, aka 0 */
	t34 = t3 + (t4 - t3) / 2;
	/* Advance PTPCLKVAL by the time it took since its readout */
	now += (t34 - t12);

	__sja1105_ptp_adjtime(ds, now);

	mutex_unlock(&priv->ptp_data.lock);

	dev_info(priv->ds->dev,
		 "Reset switch and programmed static config. Reason: %s\n",
		 sja1105_reset_reasons[reason]);

	/* Configure the CGU (PLLs) for MII and RMII PHYs.
	 * For these interfaces there is no dynamic configuration
	 * needed, since PLLs have same settings at all speeds.
	 */
	if (priv->info->clocking_setup) {
		rc = priv->info->clocking_setup(priv);
		if (rc < 0)
			goto out;
	}

	for (i = 0; i < ds->num_ports; i++) {
		struct dw_xpcs *xpcs = priv->xpcs[i];
		unsigned int mode;

		rc = sja1105_adjust_port_config(priv, i, speed_mbps[i]);
		if (rc < 0)
			goto out;

		if (!xpcs)
			continue;

		if (bmcr[i] & BMCR_ANENABLE)
			mode = MLO_AN_INBAND;
		else if (priv->fixed_link[i])
			mode = MLO_AN_FIXED;
		else
			mode = MLO_AN_PHY;

		rc = xpcs_do_config(xpcs, priv->phy_mode[i], mode);
		if (rc < 0)
			goto out;

		if (!phylink_autoneg_inband(mode)) {
			int speed = SPEED_UNKNOWN;

			if (priv->phy_mode[i] == PHY_INTERFACE_MODE_2500BASEX)
				speed = SPEED_2500;
			else if (bmcr[i] & BMCR_SPEED1000)
				speed = SPEED_1000;
			else if (bmcr[i] & BMCR_SPEED100)
				speed = SPEED_100;
			else
				speed = SPEED_10;

			xpcs_link_up(&xpcs->pcs, mode, priv->phy_mode[i],
				     speed, DUPLEX_FULL);
		}
	}

	rc = sja1105_reload_cbs(priv);
	if (rc < 0)
		goto out;
out:
	mutex_unlock(&priv->mgmt_lock);

	return rc;
}

static enum dsa_tag_protocol
sja1105_get_tag_protocol(struct dsa_switch *ds, int port,
			 enum dsa_tag_protocol mp)
{
	struct sja1105_private *priv = ds->priv;

	return priv->info->tag_proto;
}

/* The TPID setting belongs to the General Parameters table,
 * which can only be partially reconfigured at runtime (and not the TPID).
 * So a switch reset is required.
 */
int sja1105_vlan_filtering(struct dsa_switch *ds, int port, bool enabled,
			   struct netlink_ext_ack *extack)
{
	struct sja1105_l2_lookup_params_entry *l2_lookup_params;
	struct sja1105_general_params_entry *general_params;
	struct sja1105_private *priv = ds->priv;
	struct sja1105_table *table;
	struct sja1105_rule *rule;
	u16 tpid, tpid2;
	int rc;

	list_for_each_entry(rule, &priv->flow_block.rules, list) {
		if (rule->type == SJA1105_RULE_VL) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot change VLAN filtering with active VL rules");
			return -EBUSY;
		}
	}

	if (enabled) {
		/* Enable VLAN filtering. */
		tpid  = ETH_P_8021Q;
		tpid2 = ETH_P_8021AD;
	} else {
		/* Disable VLAN filtering. */
		tpid  = ETH_P_SJA1105;
		tpid2 = ETH_P_SJA1105;
	}

	if (priv->vlan_aware == enabled)
		return 0;

	priv->vlan_aware = enabled;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];
	general_params = table->entries;
	/* EtherType used to identify inner tagged (C-tag) VLAN traffic */
	general_params->tpid = tpid;
	/* EtherType used to identify outer tagged (S-tag) VLAN traffic */
	general_params->tpid2 = tpid2;
	/* When VLAN filtering is on, we need to at least be able to
	 * decode management traffic through the "backup plan".
	 */
	general_params->incl_srcpt1 = enabled;
	general_params->incl_srcpt0 = enabled;

	/* VLAN filtering => independent VLAN learning.
	 * No VLAN filtering (or best effort) => shared VLAN learning.
	 *
	 * In shared VLAN learning mode, untagged traffic still gets
	 * pvid-tagged, and the FDB table gets populated with entries
	 * containing the "real" (pvid or from VLAN tag) VLAN ID.
	 * However the switch performs a masked L2 lookup in the FDB,
	 * effectively only looking up a frame's DMAC (and not VID) for the
	 * forwarding decision.
	 *
	 * This is extremely convenient for us, because in modes with
	 * vlan_filtering=0, dsa_8021q actually installs unique pvid's into
	 * each front panel port. This is good for identification but breaks
	 * learning badly - the VID of the learnt FDB entry is unique, aka
	 * no frames coming from any other port are going to have it. So
	 * for forwarding purposes, this is as though learning was broken
	 * (all frames get flooded).
	 */
	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP_PARAMS];
	l2_lookup_params = table->entries;
	l2_lookup_params->shared_learn = !priv->vlan_aware;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		rc = sja1105_commit_pvid(ds, port);
		if (rc)
			return rc;
	}

	rc = sja1105_static_config_reload(priv, SJA1105_VLAN_FILTERING);
	if (rc)
		NL_SET_ERR_MSG_MOD(extack, "Failed to change VLAN Ethertype");

	return rc;
}

static int sja1105_vlan_add(struct sja1105_private *priv, int port, u16 vid,
			    u16 flags, bool allowed_ingress)
{
	struct sja1105_vlan_lookup_entry *vlan;
	struct sja1105_table *table;
	int match, rc;

	table = &priv->static_config.tables[BLK_IDX_VLAN_LOOKUP];

	match = sja1105_is_vlan_configured(priv, vid);
	if (match < 0) {
		rc = sja1105_table_resize(table, table->entry_count + 1);
		if (rc)
			return rc;
		match = table->entry_count - 1;
	}

	/* Assign pointer after the resize (it's new memory) */
	vlan = table->entries;

	vlan[match].type_entry = SJA1110_VLAN_D_TAG;
	vlan[match].vlanid = vid;
	vlan[match].vlan_bc |= BIT(port);

	if (allowed_ingress)
		vlan[match].vmemb_port |= BIT(port);
	else
		vlan[match].vmemb_port &= ~BIT(port);

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED)
		vlan[match].tag_port &= ~BIT(port);
	else
		vlan[match].tag_port |= BIT(port);

	return sja1105_dynamic_config_write(priv, BLK_IDX_VLAN_LOOKUP, vid,
					    &vlan[match], true);
}

static int sja1105_vlan_del(struct sja1105_private *priv, int port, u16 vid)
{
	struct sja1105_vlan_lookup_entry *vlan;
	struct sja1105_table *table;
	bool keep = true;
	int match, rc;

	table = &priv->static_config.tables[BLK_IDX_VLAN_LOOKUP];

	match = sja1105_is_vlan_configured(priv, vid);
	/* Can't delete a missing entry. */
	if (match < 0)
		return 0;

	/* Assign pointer after the resize (it's new memory) */
	vlan = table->entries;

	vlan[match].vlanid = vid;
	vlan[match].vlan_bc &= ~BIT(port);
	vlan[match].vmemb_port &= ~BIT(port);
	/* Also unset tag_port, just so we don't have a confusing bitmap
	 * (no practical purpose).
	 */
	vlan[match].tag_port &= ~BIT(port);

	/* If there's no port left as member of this VLAN,
	 * it's time for it to go.
	 */
	if (!vlan[match].vmemb_port)
		keep = false;

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_VLAN_LOOKUP, vid,
					  &vlan[match], keep);
	if (rc < 0)
		return rc;

	if (!keep)
		return sja1105_table_delete_entry(table, match);

	return 0;
}

static int sja1105_bridge_vlan_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct netlink_ext_ack *extack)
{
	struct sja1105_private *priv = ds->priv;
	u16 flags = vlan->flags;
	int rc;

	/* Be sure to deny alterations to the configuration done by tag_8021q.
	 */
	if (vid_is_dsa_8021q(vlan->vid)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Range 1024-3071 reserved for dsa_8021q operation");
		return -EBUSY;
	}

	/* Always install bridge VLANs as egress-tagged on CPU and DSA ports */
	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port))
		flags = 0;

	rc = sja1105_vlan_add(priv, port, vlan->vid, flags, true);
	if (rc)
		return rc;

	if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
		priv->bridge_pvid[port] = vlan->vid;

	return sja1105_commit_pvid(ds, port);
}

static int sja1105_bridge_vlan_del(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct sja1105_private *priv = ds->priv;
	int rc;

	rc = sja1105_vlan_del(priv, port, vlan->vid);
	if (rc)
		return rc;

	/* In case the pvid was deleted, make sure that untagged packets will
	 * be dropped.
	 */
	return sja1105_commit_pvid(ds, port);
}

static int sja1105_dsa_8021q_vlan_add(struct dsa_switch *ds, int port, u16 vid,
				      u16 flags)
{
	struct sja1105_private *priv = ds->priv;
	bool allowed_ingress = true;
	int rc;

	/* Prevent attackers from trying to inject a DSA tag from
	 * the outside world.
	 */
	if (dsa_is_user_port(ds, port))
		allowed_ingress = false;

	rc = sja1105_vlan_add(priv, port, vid, flags, allowed_ingress);
	if (rc)
		return rc;

	if (flags & BRIDGE_VLAN_INFO_PVID)
		priv->tag_8021q_pvid[port] = vid;

	return sja1105_commit_pvid(ds, port);
}

static int sja1105_dsa_8021q_vlan_del(struct dsa_switch *ds, int port, u16 vid)
{
	struct sja1105_private *priv = ds->priv;

	return sja1105_vlan_del(priv, port, vid);
}

static int sja1105_prechangeupper(struct dsa_switch *ds, int port,
				  struct netdev_notifier_changeupper_info *info)
{
	struct netlink_ext_ack *extack = info->info.extack;
	struct net_device *upper = info->upper_dev;
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;

	if (is_vlan_dev(upper)) {
		NL_SET_ERR_MSG_MOD(extack, "8021q uppers are not supported");
		return -EBUSY;
	}

	if (netif_is_bridge_master(upper)) {
		list_for_each_entry(dp, &dst->ports, list) {
			if (dp->bridge_dev && dp->bridge_dev != upper &&
			    br_vlan_enabled(dp->bridge_dev)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Only one VLAN-aware bridge is supported");
				return -EBUSY;
			}
		}
	}

	return 0;
}

static void sja1105_port_disable(struct dsa_switch *ds, int port)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_port *sp = &priv->ports[port];

	if (!dsa_is_user_port(ds, port))
		return;

	kthread_cancel_work_sync(&sp->xmit_work);
	skb_queue_purge(&sp->xmit_queue);
}

static int sja1105_mgmt_xmit(struct dsa_switch *ds, int port, int slot,
			     struct sk_buff *skb, bool takets)
{
	struct sja1105_mgmt_entry mgmt_route = {0};
	struct sja1105_private *priv = ds->priv;
	struct ethhdr *hdr;
	int timeout = 10;
	int rc;

	hdr = eth_hdr(skb);

	mgmt_route.macaddr = ether_addr_to_u64(hdr->h_dest);
	mgmt_route.destports = BIT(port);
	mgmt_route.enfport = 1;
	mgmt_route.tsreg = 0;
	mgmt_route.takets = takets;

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_MGMT_ROUTE,
					  slot, &mgmt_route, true);
	if (rc < 0) {
		kfree_skb(skb);
		return rc;
	}

	/* Transfer skb to the host port. */
	dsa_enqueue_skb(skb, dsa_to_port(ds, port)->slave);

	/* Wait until the switch has processed the frame */
	do {
		rc = sja1105_dynamic_config_read(priv, BLK_IDX_MGMT_ROUTE,
						 slot, &mgmt_route);
		if (rc < 0) {
			dev_err_ratelimited(priv->ds->dev,
					    "failed to poll for mgmt route\n");
			continue;
		}

		/* UM10944: The ENFPORT flag of the respective entry is
		 * cleared when a match is found. The host can use this
		 * flag as an acknowledgment.
		 */
		cpu_relax();
	} while (mgmt_route.enfport && --timeout);

	if (!timeout) {
		/* Clean up the management route so that a follow-up
		 * frame may not match on it by mistake.
		 * This is only hardware supported on P/Q/R/S - on E/T it is
		 * a no-op and we are silently discarding the -EOPNOTSUPP.
		 */
		sja1105_dynamic_config_write(priv, BLK_IDX_MGMT_ROUTE,
					     slot, &mgmt_route, false);
		dev_err_ratelimited(priv->ds->dev, "xmit timed out\n");
	}

	return NETDEV_TX_OK;
}

#define work_to_port(work) \
		container_of((work), struct sja1105_port, xmit_work)
#define tagger_to_sja1105(t) \
		container_of((t), struct sja1105_private, tagger_data)

/* Deferred work is unfortunately necessary because setting up the management
 * route cannot be done from atomit context (SPI transfer takes a sleepable
 * lock on the bus)
 */
static void sja1105_port_deferred_xmit(struct kthread_work *work)
{
	struct sja1105_port *sp = work_to_port(work);
	struct sja1105_tagger_data *tagger_data = sp->data;
	struct sja1105_private *priv = tagger_to_sja1105(tagger_data);
	int port = sp - priv->ports;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sp->xmit_queue)) != NULL) {
		struct sk_buff *clone = SJA1105_SKB_CB(skb)->clone;

		mutex_lock(&priv->mgmt_lock);

		sja1105_mgmt_xmit(priv->ds, port, 0, skb, !!clone);

		/* The clone, if there, was made by dsa_skb_tx_timestamp */
		if (clone)
			sja1105_ptp_txtstamp_skb(priv->ds, port, clone);

		mutex_unlock(&priv->mgmt_lock);
	}
}

/* The MAXAGE setting belongs to the L2 Forwarding Parameters table,
 * which cannot be reconfigured at runtime. So a switch reset is required.
 */
static int sja1105_set_ageing_time(struct dsa_switch *ds,
				   unsigned int ageing_time)
{
	struct sja1105_l2_lookup_params_entry *l2_lookup_params;
	struct sja1105_private *priv = ds->priv;
	struct sja1105_table *table;
	unsigned int maxage;

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP_PARAMS];
	l2_lookup_params = table->entries;

	maxage = SJA1105_AGEING_TIME_MS(ageing_time);

	if (l2_lookup_params->maxage == maxage)
		return 0;

	l2_lookup_params->maxage = maxage;

	return sja1105_static_config_reload(priv, SJA1105_AGEING_TIME);
}

static int sja1105_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct sja1105_l2_policing_entry *policing;
	struct sja1105_private *priv = ds->priv;

	new_mtu += VLAN_ETH_HLEN + ETH_FCS_LEN;

	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port))
		new_mtu += VLAN_HLEN;

	policing = priv->static_config.tables[BLK_IDX_L2_POLICING].entries;

	if (policing[port].maxlen == new_mtu)
		return 0;

	policing[port].maxlen = new_mtu;

	return sja1105_static_config_reload(priv, SJA1105_BEST_EFFORT_POLICING);
}

static int sja1105_get_max_mtu(struct dsa_switch *ds, int port)
{
	return 2043 - VLAN_ETH_HLEN - ETH_FCS_LEN;
}

static int sja1105_port_setup_tc(struct dsa_switch *ds, int port,
				 enum tc_setup_type type,
				 void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_TAPRIO:
		return sja1105_setup_tc_taprio(ds, port, type_data);
	case TC_SETUP_QDISC_CBS:
		return sja1105_setup_tc_cbs(ds, port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

/* We have a single mirror (@to) port, but can configure ingress and egress
 * mirroring on all other (@from) ports.
 * We need to allow mirroring rules only as long as the @to port is always the
 * same, and we need to unset the @to port from mirr_port only when there is no
 * mirroring rule that references it.
 */
static int sja1105_mirror_apply(struct sja1105_private *priv, int from, int to,
				bool ingress, bool enabled)
{
	struct sja1105_general_params_entry *general_params;
	struct sja1105_mac_config_entry *mac;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_table *table;
	bool already_enabled;
	u64 new_mirr_port;
	int rc;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];
	general_params = table->entries;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	already_enabled = (general_params->mirr_port != ds->num_ports);
	if (already_enabled && enabled && general_params->mirr_port != to) {
		dev_err(priv->ds->dev,
			"Delete mirroring rules towards port %llu first\n",
			general_params->mirr_port);
		return -EBUSY;
	}

	new_mirr_port = to;
	if (!enabled) {
		bool keep = false;
		int port;

		/* Anybody still referencing mirr_port? */
		for (port = 0; port < ds->num_ports; port++) {
			if (mac[port].ing_mirr || mac[port].egr_mirr) {
				keep = true;
				break;
			}
		}
		/* Unset already_enabled for next time */
		if (!keep)
			new_mirr_port = ds->num_ports;
	}
	if (new_mirr_port != general_params->mirr_port) {
		general_params->mirr_port = new_mirr_port;

		rc = sja1105_dynamic_config_write(priv, BLK_IDX_GENERAL_PARAMS,
						  0, general_params, true);
		if (rc < 0)
			return rc;
	}

	if (ingress)
		mac[from].ing_mirr = enabled;
	else
		mac[from].egr_mirr = enabled;

	return sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, from,
					    &mac[from], true);
}

static int sja1105_mirror_add(struct dsa_switch *ds, int port,
			      struct dsa_mall_mirror_tc_entry *mirror,
			      bool ingress)
{
	return sja1105_mirror_apply(ds->priv, port, mirror->to_local_port,
				    ingress, true);
}

static void sja1105_mirror_del(struct dsa_switch *ds, int port,
			       struct dsa_mall_mirror_tc_entry *mirror)
{
	sja1105_mirror_apply(ds->priv, port, mirror->to_local_port,
			     mirror->ingress, false);
}

static int sja1105_port_policer_add(struct dsa_switch *ds, int port,
				    struct dsa_mall_policer_tc_entry *policer)
{
	struct sja1105_l2_policing_entry *policing;
	struct sja1105_private *priv = ds->priv;

	policing = priv->static_config.tables[BLK_IDX_L2_POLICING].entries;

	/* In hardware, every 8 microseconds the credit level is incremented by
	 * the value of RATE bytes divided by 64, up to a maximum of SMAX
	 * bytes.
	 */
	policing[port].rate = div_u64(512 * policer->rate_bytes_per_sec,
				      1000000);
	policing[port].smax = policer->burst;

	return sja1105_static_config_reload(priv, SJA1105_BEST_EFFORT_POLICING);
}

static void sja1105_port_policer_del(struct dsa_switch *ds, int port)
{
	struct sja1105_l2_policing_entry *policing;
	struct sja1105_private *priv = ds->priv;

	policing = priv->static_config.tables[BLK_IDX_L2_POLICING].entries;

	policing[port].rate = SJA1105_RATE_MBPS(1000);
	policing[port].smax = 65535;

	sja1105_static_config_reload(priv, SJA1105_BEST_EFFORT_POLICING);
}

static int sja1105_port_set_learning(struct sja1105_private *priv, int port,
				     bool enabled)
{
	struct sja1105_mac_config_entry *mac;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	mac[port].dyn_learn = enabled;

	return sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, port,
					    &mac[port], true);
}

static int sja1105_port_ucast_bcast_flood(struct sja1105_private *priv, int to,
					  struct switchdev_brport_flags flags)
{
	if (flags.mask & BR_FLOOD) {
		if (flags.val & BR_FLOOD)
			priv->ucast_egress_floods |= BIT(to);
		else
			priv->ucast_egress_floods &= ~BIT(to);
	}

	if (flags.mask & BR_BCAST_FLOOD) {
		if (flags.val & BR_BCAST_FLOOD)
			priv->bcast_egress_floods |= BIT(to);
		else
			priv->bcast_egress_floods &= ~BIT(to);
	}

	return sja1105_manage_flood_domains(priv);
}

static int sja1105_port_mcast_flood(struct sja1105_private *priv, int to,
				    struct switchdev_brport_flags flags,
				    struct netlink_ext_ack *extack)
{
	struct sja1105_l2_lookup_entry *l2_lookup;
	struct sja1105_table *table;
	int match;

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP];
	l2_lookup = table->entries;

	for (match = 0; match < table->entry_count; match++)
		if (l2_lookup[match].macaddr == SJA1105_UNKNOWN_MULTICAST &&
		    l2_lookup[match].mask_macaddr == SJA1105_UNKNOWN_MULTICAST)
			break;

	if (match == table->entry_count) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Could not find FDB entry for unknown multicast");
		return -ENOSPC;
	}

	if (flags.val & BR_MCAST_FLOOD)
		l2_lookup[match].destports |= BIT(to);
	else
		l2_lookup[match].destports &= ~BIT(to);

	return sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					    l2_lookup[match].index,
					    &l2_lookup[match],
					    true);
}

static int sja1105_port_pre_bridge_flags(struct dsa_switch *ds, int port,
					 struct switchdev_brport_flags flags,
					 struct netlink_ext_ack *extack)
{
	struct sja1105_private *priv = ds->priv;

	if (flags.mask & ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
			   BR_BCAST_FLOOD))
		return -EINVAL;

	if (flags.mask & (BR_FLOOD | BR_MCAST_FLOOD) &&
	    !priv->info->can_limit_mcast_flood) {
		bool multicast = !!(flags.val & BR_MCAST_FLOOD);
		bool unicast = !!(flags.val & BR_FLOOD);

		if (unicast != multicast) {
			NL_SET_ERR_MSG_MOD(extack,
					   "This chip cannot configure multicast flooding independently of unicast");
			return -EINVAL;
		}
	}

	return 0;
}

static int sja1105_port_bridge_flags(struct dsa_switch *ds, int port,
				     struct switchdev_brport_flags flags,
				     struct netlink_ext_ack *extack)
{
	struct sja1105_private *priv = ds->priv;
	int rc;

	if (flags.mask & BR_LEARNING) {
		bool learn_ena = !!(flags.val & BR_LEARNING);

		rc = sja1105_port_set_learning(priv, port, learn_ena);
		if (rc)
			return rc;
	}

	if (flags.mask & (BR_FLOOD | BR_BCAST_FLOOD)) {
		rc = sja1105_port_ucast_bcast_flood(priv, port, flags);
		if (rc)
			return rc;
	}

	/* For chips that can't offload BR_MCAST_FLOOD independently, there
	 * is nothing to do here, we ensured the configuration is in sync by
	 * offloading BR_FLOOD.
	 */
	if (flags.mask & BR_MCAST_FLOOD && priv->info->can_limit_mcast_flood) {
		rc = sja1105_port_mcast_flood(priv, port, flags,
					      extack);
		if (rc)
			return rc;
	}

	return 0;
}

static void sja1105_teardown_ports(struct sja1105_private *priv)
{
	struct dsa_switch *ds = priv->ds;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		struct sja1105_port *sp = &priv->ports[port];

		if (sp->xmit_worker)
			kthread_destroy_worker(sp->xmit_worker);
	}
}

static int sja1105_setup_ports(struct sja1105_private *priv)
{
	struct sja1105_tagger_data *tagger_data = &priv->tagger_data;
	struct dsa_switch *ds = priv->ds;
	int port, rc;

	/* Connections between dsa_port and sja1105_port */
	for (port = 0; port < ds->num_ports; port++) {
		struct sja1105_port *sp = &priv->ports[port];
		struct dsa_port *dp = dsa_to_port(ds, port);
		struct kthread_worker *worker;
		struct net_device *slave;

		if (!dsa_port_is_user(dp))
			continue;

		dp->priv = sp;
		sp->dp = dp;
		sp->data = tagger_data;
		slave = dp->slave;
		kthread_init_work(&sp->xmit_work, sja1105_port_deferred_xmit);
		worker = kthread_create_worker(0, "%s_xmit", slave->name);
		if (IS_ERR(worker)) {
			rc = PTR_ERR(worker);
			dev_err(ds->dev,
				"failed to create deferred xmit thread: %d\n",
				rc);
			goto out_destroy_workers;
		}
		sp->xmit_worker = worker;
		skb_queue_head_init(&sp->xmit_queue);
	}

	return 0;

out_destroy_workers:
	sja1105_teardown_ports(priv);
	return rc;
}

/* The programming model for the SJA1105 switch is "all-at-once" via static
 * configuration tables. Some of these can be dynamically modified at runtime,
 * but not the xMII mode parameters table.
 * Furthermode, some PHYs may not have crystals for generating their clocks
 * (e.g. RMII). Instead, their 50MHz clock is supplied via the SJA1105 port's
 * ref_clk pin. So port clocking needs to be initialized early, before
 * connecting to PHYs is attempted, otherwise they won't respond through MDIO.
 * Setting correct PHY link speed does not matter now.
 * But dsa_slave_phy_setup is called later than sja1105_setup, so the PHY
 * bindings are not yet parsed by DSA core. We need to parse early so that we
 * can populate the xMII mode parameters table.
 */
static int sja1105_setup(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	int rc;

	if (priv->info->disable_microcontroller) {
		rc = priv->info->disable_microcontroller(priv);
		if (rc < 0) {
			dev_err(ds->dev,
				"Failed to disable microcontroller: %pe\n",
				ERR_PTR(rc));
			return rc;
		}
	}

	/* Create and send configuration down to device */
	rc = sja1105_static_config_load(priv);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to load static config: %d\n", rc);
		return rc;
	}

	/* Configure the CGU (PHY link modes and speeds) */
	if (priv->info->clocking_setup) {
		rc = priv->info->clocking_setup(priv);
		if (rc < 0) {
			dev_err(ds->dev,
				"Failed to configure MII clocking: %pe\n",
				ERR_PTR(rc));
			goto out_static_config_free;
		}
	}

	rc = sja1105_setup_ports(priv);
	if (rc)
		goto out_static_config_free;

	sja1105_tas_setup(ds);
	sja1105_flower_setup(ds);

	rc = sja1105_ptp_clock_register(ds);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to register PTP clock: %d\n", rc);
		goto out_flower_teardown;
	}

	rc = sja1105_mdiobus_register(ds);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to register MDIO bus: %pe\n",
			ERR_PTR(rc));
		goto out_ptp_clock_unregister;
	}

	rc = sja1105_devlink_setup(ds);
	if (rc < 0)
		goto out_mdiobus_unregister;

	rtnl_lock();
	rc = dsa_tag_8021q_register(ds, htons(ETH_P_8021Q));
	rtnl_unlock();
	if (rc)
		goto out_devlink_teardown;

	/* On SJA1105, VLAN filtering per se is always enabled in hardware.
	 * The only thing we can do to disable it is lie about what the 802.1Q
	 * EtherType is.
	 * So it will still try to apply VLAN filtering, but all ingress
	 * traffic (except frames received with EtherType of ETH_P_SJA1105)
	 * will be internally tagged with a distorted VLAN header where the
	 * TPID is ETH_P_SJA1105, and the VLAN ID is the port pvid.
	 */
	ds->vlan_filtering_is_global = true;
	ds->untag_bridge_pvid = true;
	/* tag_8021q has 3 bits for the VBID, and the value 0 is reserved */
	ds->num_fwd_offloading_bridges = 7;

	/* Advertise the 8 egress queues */
	ds->num_tx_queues = SJA1105_NUM_TC;

	ds->mtu_enforcement_ingress = true;
	ds->assisted_learning_on_cpu_port = true;

	return 0;

out_devlink_teardown:
	sja1105_devlink_teardown(ds);
out_mdiobus_unregister:
	sja1105_mdiobus_unregister(ds);
out_ptp_clock_unregister:
	sja1105_ptp_clock_unregister(ds);
out_flower_teardown:
	sja1105_flower_teardown(ds);
	sja1105_tas_teardown(ds);
	sja1105_teardown_ports(priv);
out_static_config_free:
	sja1105_static_config_free(&priv->static_config);

	return rc;
}

static void sja1105_teardown(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;

	rtnl_lock();
	dsa_tag_8021q_unregister(ds);
	rtnl_unlock();

	sja1105_devlink_teardown(ds);
	sja1105_mdiobus_unregister(ds);
	sja1105_ptp_clock_unregister(ds);
	sja1105_flower_teardown(ds);
	sja1105_tas_teardown(ds);
	sja1105_teardown_ports(priv);
	sja1105_static_config_free(&priv->static_config);
}

static const struct dsa_switch_ops sja1105_switch_ops = {
	.get_tag_protocol	= sja1105_get_tag_protocol,
	.setup			= sja1105_setup,
	.teardown		= sja1105_teardown,
	.set_ageing_time	= sja1105_set_ageing_time,
	.port_change_mtu	= sja1105_change_mtu,
	.port_max_mtu		= sja1105_get_max_mtu,
	.phylink_validate	= sja1105_phylink_validate,
	.phylink_mac_config	= sja1105_mac_config,
	.phylink_mac_link_up	= sja1105_mac_link_up,
	.phylink_mac_link_down	= sja1105_mac_link_down,
	.get_strings		= sja1105_get_strings,
	.get_ethtool_stats	= sja1105_get_ethtool_stats,
	.get_sset_count		= sja1105_get_sset_count,
	.get_ts_info		= sja1105_get_ts_info,
	.port_disable		= sja1105_port_disable,
	.port_fdb_dump		= sja1105_fdb_dump,
	.port_fdb_add		= sja1105_fdb_add,
	.port_fdb_del		= sja1105_fdb_del,
	.port_fast_age		= sja1105_fast_age,
	.port_bridge_join	= sja1105_bridge_join,
	.port_bridge_leave	= sja1105_bridge_leave,
	.port_pre_bridge_flags	= sja1105_port_pre_bridge_flags,
	.port_bridge_flags	= sja1105_port_bridge_flags,
	.port_stp_state_set	= sja1105_bridge_stp_state_set,
	.port_vlan_filtering	= sja1105_vlan_filtering,
	.port_vlan_add		= sja1105_bridge_vlan_add,
	.port_vlan_del		= sja1105_bridge_vlan_del,
	.port_mdb_add		= sja1105_mdb_add,
	.port_mdb_del		= sja1105_mdb_del,
	.port_hwtstamp_get	= sja1105_hwtstamp_get,
	.port_hwtstamp_set	= sja1105_hwtstamp_set,
	.port_rxtstamp		= sja1105_port_rxtstamp,
	.port_txtstamp		= sja1105_port_txtstamp,
	.port_setup_tc		= sja1105_port_setup_tc,
	.port_mirror_add	= sja1105_mirror_add,
	.port_mirror_del	= sja1105_mirror_del,
	.port_policer_add	= sja1105_port_policer_add,
	.port_policer_del	= sja1105_port_policer_del,
	.cls_flower_add		= sja1105_cls_flower_add,
	.cls_flower_del		= sja1105_cls_flower_del,
	.cls_flower_stats	= sja1105_cls_flower_stats,
	.devlink_info_get	= sja1105_devlink_info_get,
	.tag_8021q_vlan_add	= sja1105_dsa_8021q_vlan_add,
	.tag_8021q_vlan_del	= sja1105_dsa_8021q_vlan_del,
	.port_prechangeupper	= sja1105_prechangeupper,
	.port_bridge_tx_fwd_offload = dsa_tag_8021q_bridge_tx_fwd_offload,
	.port_bridge_tx_fwd_unoffload = dsa_tag_8021q_bridge_tx_fwd_unoffload,
};

static const struct of_device_id sja1105_dt_ids[];

static int sja1105_check_device_id(struct sja1105_private *priv)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 prod_id[SJA1105_SIZE_DEVICE_ID] = {0};
	struct device *dev = &priv->spidev->dev;
	const struct of_device_id *match;
	u32 device_id;
	u64 part_no;
	int rc;

	rc = sja1105_xfer_u32(priv, SPI_READ, regs->device_id, &device_id,
			      NULL);
	if (rc < 0)
		return rc;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->prod_id, prod_id,
			      SJA1105_SIZE_DEVICE_ID);
	if (rc < 0)
		return rc;

	sja1105_unpack(prod_id, &part_no, 19, 4, SJA1105_SIZE_DEVICE_ID);

	for (match = sja1105_dt_ids; match->compatible[0]; match++) {
		const struct sja1105_info *info = match->data;

		/* Is what's been probed in our match table at all? */
		if (info->device_id != device_id || info->part_no != part_no)
			continue;

		/* But is it what's in the device tree? */
		if (priv->info->device_id != device_id ||
		    priv->info->part_no != part_no) {
			dev_warn(dev, "Device tree specifies chip %s but found %s, please fix it!\n",
				 priv->info->name, info->name);
			/* It isn't. No problem, pick that up. */
			priv->info = info;
		}

		return 0;
	}

	dev_err(dev, "Unexpected {device ID, part number}: 0x%x 0x%llx\n",
		device_id, part_no);

	return -ENODEV;
}

static int sja1105_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct sja1105_private *priv;
	size_t max_xfer, max_msg;
	struct dsa_switch *ds;
	int rc;

	if (!dev->of_node) {
		dev_err(dev, "No DTS bindings for SJA1105 driver\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(struct sja1105_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Configure the optional reset pin and bring up switch */
	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		dev_dbg(dev, "reset-gpios not defined, ignoring\n");
	else
		sja1105_hw_reset(priv->reset_gpio, 1, 1);

	/* Populate our driver private structure (priv) based on
	 * the device tree node that was probed (spi)
	 */
	priv->spidev = spi;
	spi_set_drvdata(spi, priv);

	/* Configure the SPI bus */
	spi->bits_per_word = 8;
	rc = spi_setup(spi);
	if (rc < 0) {
		dev_err(dev, "Could not init SPI\n");
		return rc;
	}

	/* In sja1105_xfer, we send spi_messages composed of two spi_transfers:
	 * a small one for the message header and another one for the current
	 * chunk of the packed buffer.
	 * Check that the restrictions imposed by the SPI controller are
	 * respected: the chunk buffer is smaller than the max transfer size,
	 * and the total length of the chunk plus its message header is smaller
	 * than the max message size.
	 * We do that during probe time since the maximum transfer size is a
	 * runtime invariant.
	 */
	max_xfer = spi_max_transfer_size(spi);
	max_msg = spi_max_message_size(spi);

	/* We need to send at least one 64-bit word of SPI payload per message
	 * in order to be able to make useful progress.
	 */
	if (max_msg < SJA1105_SIZE_SPI_MSG_HEADER + 8) {
		dev_err(dev, "SPI master cannot send large enough buffers, aborting\n");
		return -EINVAL;
	}

	priv->max_xfer_len = SJA1105_SIZE_SPI_MSG_MAXLEN;
	if (priv->max_xfer_len > max_xfer)
		priv->max_xfer_len = max_xfer;
	if (priv->max_xfer_len > max_msg - SJA1105_SIZE_SPI_MSG_HEADER)
		priv->max_xfer_len = max_msg - SJA1105_SIZE_SPI_MSG_HEADER;

	priv->info = of_device_get_match_data(dev);

	/* Detect hardware device */
	rc = sja1105_check_device_id(priv);
	if (rc < 0) {
		dev_err(dev, "Device ID check failed: %d\n", rc);
		return rc;
	}

	dev_info(dev, "Probed switch chip: %s\n", priv->info->name);

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->num_ports = priv->info->num_ports;
	ds->ops = &sja1105_switch_ops;
	ds->priv = priv;
	priv->ds = ds;

	mutex_init(&priv->ptp_data.lock);
	mutex_init(&priv->mgmt_lock);

	rc = sja1105_parse_dt(priv);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to parse DT: %d\n", rc);
		return rc;
	}

	/* Error out early if internal delays are required through DT
	 * and we can't apply them.
	 */
	rc = sja1105_parse_rgmii_delays(priv);
	if (rc < 0) {
		dev_err(ds->dev, "RGMII delay not supported\n");
		return rc;
	}

	if (IS_ENABLED(CONFIG_NET_SCH_CBS)) {
		priv->cbs = devm_kcalloc(dev, priv->info->num_cbs_shapers,
					 sizeof(struct sja1105_cbs_entry),
					 GFP_KERNEL);
		if (!priv->cbs)
			return -ENOMEM;
	}

	return dsa_register_switch(priv->ds);
}

static int sja1105_remove(struct spi_device *spi)
{
	struct sja1105_private *priv = spi_get_drvdata(spi);

	if (!priv)
		return 0;

	dsa_unregister_switch(priv->ds);

	spi_set_drvdata(spi, NULL);

	return 0;
}

static void sja1105_shutdown(struct spi_device *spi)
{
	struct sja1105_private *priv = spi_get_drvdata(spi);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	spi_set_drvdata(spi, NULL);
}

static const struct of_device_id sja1105_dt_ids[] = {
	{ .compatible = "nxp,sja1105e", .data = &sja1105e_info },
	{ .compatible = "nxp,sja1105t", .data = &sja1105t_info },
	{ .compatible = "nxp,sja1105p", .data = &sja1105p_info },
	{ .compatible = "nxp,sja1105q", .data = &sja1105q_info },
	{ .compatible = "nxp,sja1105r", .data = &sja1105r_info },
	{ .compatible = "nxp,sja1105s", .data = &sja1105s_info },
	{ .compatible = "nxp,sja1110a", .data = &sja1110a_info },
	{ .compatible = "nxp,sja1110b", .data = &sja1110b_info },
	{ .compatible = "nxp,sja1110c", .data = &sja1110c_info },
	{ .compatible = "nxp,sja1110d", .data = &sja1110d_info },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sja1105_dt_ids);

static const struct spi_device_id sja1105_spi_ids[] = {
	{ "sja1105e" },
	{ "sja1105t" },
	{ "sja1105p" },
	{ "sja1105q" },
	{ "sja1105r" },
	{ "sja1105s" },
	{ "sja1110a" },
	{ "sja1110b" },
	{ "sja1110c" },
	{ "sja1110d" },
	{ },
};
MODULE_DEVICE_TABLE(spi, sja1105_spi_ids);

static struct spi_driver sja1105_driver = {
	.driver = {
		.name  = "sja1105",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sja1105_dt_ids),
	},
	.id_table = sja1105_spi_ids,
	.probe  = sja1105_probe,
	.remove = sja1105_remove,
	.shutdown = sja1105_shutdown,
};

module_spi_driver(sja1105_driver);

MODULE_AUTHOR("Vladimir Oltean <olteanv@gmail.com>");
MODULE_AUTHOR("Georg Waibel <georg.waibel@sensor-technik.de>");
MODULE_DESCRIPTION("SJA1105 Driver");
MODULE_LICENSE("GPL v2");
