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
#include <linux/netdev_features.h>
#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_ether.h>
#include <linux/dsa/8021q.h>
#include "sja1105.h"
#include "sja1105_tas.h"

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
	if (allow) {
		l2_fwd[from].bc_domain  |= BIT(to);
		l2_fwd[from].reach_port |= BIT(to);
		l2_fwd[from].fl_domain  |= BIT(to);
	} else {
		l2_fwd[from].bc_domain  &= ~BIT(to);
		l2_fwd[from].reach_port &= ~BIT(to);
		l2_fwd[from].fl_domain  &= ~BIT(to);
	}
}

/* Structure used to temporarily transport device tree
 * settings into sja1105_setup
 */
struct sja1105_dt_port {
	phy_interface_t phy_mode;
	sja1105_mii_role_t role;
};

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
		.speed = SJA1105_SPEED_AUTO,
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
	struct sja1105_table *table;
	int i;

	table = &priv->static_config.tables[BLK_IDX_MAC_CONFIG];

	/* Discard previous MAC Configuration Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_NUM_PORTS,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_NUM_PORTS;

	mac = table->entries;

	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		mac[i] = default_mac;
		if (i == dsa_upstream_port(priv->ds, i)) {
			/* STP doesn't get called for CPU port, so we need to
			 * set the I/O parameters statically.
			 */
			mac[i].dyn_learn = true;
			mac[i].ingress = true;
			mac[i].egress = true;
		}
	}

	return 0;
}

static int sja1105_init_mii_settings(struct sja1105_private *priv,
				     struct sja1105_dt_port *ports)
{
	struct device *dev = &priv->spidev->dev;
	struct sja1105_xmii_params_entry *mii;
	struct sja1105_table *table;
	int i;

	table = &priv->static_config.tables[BLK_IDX_XMII_PARAMS];

	/* Discard previous xMII Mode Parameters Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_MAX_XMII_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	/* Override table based on PHYLINK DT bindings */
	table->entry_count = SJA1105_MAX_XMII_PARAMS_COUNT;

	mii = table->entries;

	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		switch (ports[i].phy_mode) {
		case PHY_INTERFACE_MODE_MII:
			mii->xmii_mode[i] = XMII_MODE_MII;
			break;
		case PHY_INTERFACE_MODE_RMII:
			mii->xmii_mode[i] = XMII_MODE_RMII;
			break;
		case PHY_INTERFACE_MODE_RGMII:
		case PHY_INTERFACE_MODE_RGMII_ID:
		case PHY_INTERFACE_MODE_RGMII_RXID:
		case PHY_INTERFACE_MODE_RGMII_TXID:
			mii->xmii_mode[i] = XMII_MODE_RGMII;
			break;
		default:
			dev_err(dev, "Unsupported PHY mode %s!\n",
				phy_modes(ports[i].phy_mode));
		}

		mii->phy_mac[i] = ports[i].role;
	}
	return 0;
}

static int sja1105_init_static_fdb(struct sja1105_private *priv)
{
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP];

	/* We only populate the FDB table through dynamic
	 * L2 Address Lookup entries
	 */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}
	return 0;
}

static int sja1105_init_l2_lookup_params(struct sja1105_private *priv)
{
	struct sja1105_table *table;
	u64 max_fdb_entries = SJA1105_MAX_L2_LOOKUP_COUNT / SJA1105_NUM_PORTS;
	struct sja1105_l2_lookup_params_entry default_l2_lookup_params = {
		/* Learned FDB entries are forgotten after 300 seconds */
		.maxage = SJA1105_AGEING_TIME_MS(300000),
		/* All entries within a FDB bin are available for learning */
		.dyn_tbsz = SJA1105ET_FDB_BIN_SIZE,
		/* And the P/Q/R/S equivalent setting: */
		.start_dynspc = 0,
		.maxaddrp = {max_fdb_entries, max_fdb_entries, max_fdb_entries,
			     max_fdb_entries, max_fdb_entries, },
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

	table = &priv->static_config.tables[BLK_IDX_L2_LOOKUP_PARAMS];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT;

	/* This table only has a single entry */
	((struct sja1105_l2_lookup_params_entry *)table->entries)[0] =
				default_l2_lookup_params;

	return 0;
}

static int sja1105_init_static_vlan(struct sja1105_private *priv)
{
	struct sja1105_table *table;
	struct sja1105_vlan_lookup_entry pvid = {
		.ving_mirr = 0,
		.vegr_mirr = 0,
		.vmemb_port = 0,
		.vlan_bc = 0,
		.tag_port = 0,
		.vlanid = 1,
	};
	int i;

	table = &priv->static_config.tables[BLK_IDX_VLAN_LOOKUP];

	/* The static VLAN table will only contain the initial pvid of 1.
	 * All other VLANs are to be configured through dynamic entries,
	 * and kept in the static configuration table as backing memory.
	 */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(1, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = 1;

	/* VLAN 1: all DT-defined ports are members; no restrictions on
	 * forwarding; always transmit priority-tagged frames as untagged.
	 */
	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		pvid.vmemb_port |= BIT(i);
		pvid.vlan_bc |= BIT(i);
		pvid.tag_port &= ~BIT(i);
	}

	((struct sja1105_vlan_lookup_entry *)table->entries)[0] = pvid;
	return 0;
}

static int sja1105_init_l2_forwarding(struct sja1105_private *priv)
{
	struct sja1105_l2_forwarding_entry *l2fwd;
	struct sja1105_table *table;
	int i, j;

	table = &priv->static_config.tables[BLK_IDX_L2_FORWARDING];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_MAX_L2_FORWARDING_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_MAX_L2_FORWARDING_COUNT;

	l2fwd = table->entries;

	/* First 5 entries define the forwarding rules */
	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		unsigned int upstream = dsa_upstream_port(priv->ds, i);

		for (j = 0; j < SJA1105_NUM_TC; j++)
			l2fwd[i].vlan_pmap[j] = j;

		if (i == upstream)
			continue;

		sja1105_port_allow_traffic(l2fwd, i, upstream, true);
		sja1105_port_allow_traffic(l2fwd, upstream, i, true);
	}
	/* Next 8 entries define VLAN PCP mapping from ingress to egress.
	 * Create a one-to-one mapping.
	 */
	for (i = 0; i < SJA1105_NUM_TC; i++)
		for (j = 0; j < SJA1105_NUM_PORTS; j++)
			l2fwd[SJA1105_NUM_PORTS + i].vlan_pmap[j] = i;

	return 0;
}

static int sja1105_init_l2_forwarding_params(struct sja1105_private *priv)
{
	struct sja1105_l2_forwarding_params_entry default_l2fwd_params = {
		/* Disallow dynamic reconfiguration of vlan_pmap */
		.max_dynp = 0,
		/* Use a single memory partition for all ingress queues */
		.part_spc = { SJA1105_MAX_FRAME_MEMORY, 0, 0, 0, 0, 0, 0, 0 },
	};
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_L2_FORWARDING_PARAMS];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT;

	/* This table only has a single entry */
	((struct sja1105_l2_forwarding_params_entry *)table->entries)[0] =
				default_l2fwd_params;

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
		/* The destination for traffic matching mac_fltres1 and
		 * mac_fltres0 on all ports except host_port. Such traffic
		 * receieved on host_port itself would be dropped, except
		 * by installing a temporary 'management route'
		 */
		.host_port = dsa_upstream_port(priv->ds, 0),
		/* Default to an invalid value */
		.mirr_port = SJA1105_NUM_PORTS,
		/* Link-local traffic received on casc_port will be forwarded
		 * to host_port without embedding the source port and device ID
		 * info in the destination MAC address (presumably because it
		 * is a cascaded port and a downstream SJA switch already did
		 * that). Default to an invalid port (to disable the feature)
		 * and overwrite this if we find any DSA (cascaded) ports.
		 */
		.casc_port = SJA1105_NUM_PORTS,
		/* No TTEthernet */
		.vllupformat = 0,
		.vlmarker = 0,
		.vlmask = 0,
		/* Only update correctionField for 1-step PTP (L2 transport) */
		.ignore2stf = 0,
		/* Forcefully disable VLAN filtering by telling
		 * the switch that VLAN has a different EtherType.
		 */
		.tpid = ETH_P_SJA1105,
		.tpid2 = ETH_P_SJA1105,
	};
	struct sja1105_table *table;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];

	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_MAX_GENERAL_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT;

	/* This table only has a single entry */
	((struct sja1105_general_params_entry *)table->entries)[0] =
				default_general_params;

	return 0;
}

#define SJA1105_RATE_MBPS(speed) (((speed) * 64000) / 1000)

static void sja1105_setup_policer(struct sja1105_l2_policing_entry *policing,
				  int index)
{
	policing[index].sharindx = index;
	policing[index].smax = 65535; /* Burst size in bytes */
	policing[index].rate = SJA1105_RATE_MBPS(1000);
	policing[index].maxlen = ETH_FRAME_LEN + VLAN_HLEN + ETH_FCS_LEN;
	policing[index].partition = 0;
}

static int sja1105_init_l2_policing(struct sja1105_private *priv)
{
	struct sja1105_l2_policing_entry *policing;
	struct sja1105_table *table;
	int i, j, k;

	table = &priv->static_config.tables[BLK_IDX_L2_POLICING];

	/* Discard previous L2 Policing Table */
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	table->entries = kcalloc(SJA1105_MAX_L2_POLICING_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;

	table->entry_count = SJA1105_MAX_L2_POLICING_COUNT;

	policing = table->entries;

	/* k sweeps through all unicast policers (0-39).
	 * bcast sweeps through policers 40-44.
	 */
	for (i = 0, k = 0; i < SJA1105_NUM_PORTS; i++) {
		int bcast = (SJA1105_NUM_PORTS * SJA1105_NUM_TC) + i;

		for (j = 0; j < SJA1105_NUM_TC; j++, k++)
			sja1105_setup_policer(policing, k);

		/* Set up this port's policer for broadcast traffic */
		sja1105_setup_policer(policing, bcast);
	}
	return 0;
}

static int sja1105_static_config_load(struct sja1105_private *priv,
				      struct sja1105_dt_port *ports)
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
	rc = sja1105_init_mii_settings(priv, ports);
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

	/* Send initial configuration to hardware via SPI */
	return sja1105_static_config_upload(priv);
}

static int sja1105_parse_rgmii_delays(struct sja1105_private *priv,
				      const struct sja1105_dt_port *ports)
{
	int i;

	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		if (ports[i].role == XMII_MAC)
			continue;

		if (ports[i].phy_mode == PHY_INTERFACE_MODE_RGMII_RXID ||
		    ports[i].phy_mode == PHY_INTERFACE_MODE_RGMII_ID)
			priv->rgmii_rx_delay[i] = true;

		if (ports[i].phy_mode == PHY_INTERFACE_MODE_RGMII_TXID ||
		    ports[i].phy_mode == PHY_INTERFACE_MODE_RGMII_ID)
			priv->rgmii_tx_delay[i] = true;

		if ((priv->rgmii_rx_delay[i] || priv->rgmii_tx_delay[i]) &&
		     !priv->info->setup_rgmii_delay)
			return -EINVAL;
	}
	return 0;
}

static int sja1105_parse_ports_node(struct sja1105_private *priv,
				    struct sja1105_dt_port *ports,
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
		ports[index].phy_mode = phy_mode;

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
			ports[index].role = XMII_PHY;
		} else {
			/* phy-handle present => put port in MAC role */
			ports[index].role = XMII_MAC;
			of_node_put(phy_node);
		}

		/* The MAC/PHY role can be overridden with explicit bindings */
		if (of_property_read_bool(child, "sja1105,role-mac"))
			ports[index].role = XMII_MAC;
		else if (of_property_read_bool(child, "sja1105,role-phy"))
			ports[index].role = XMII_PHY;
	}

	return 0;
}

static int sja1105_parse_dt(struct sja1105_private *priv,
			    struct sja1105_dt_port *ports)
{
	struct device *dev = &priv->spidev->dev;
	struct device_node *switch_node = dev->of_node;
	struct device_node *ports_node;
	int rc;

	ports_node = of_get_child_by_name(switch_node, "ports");
	if (!ports_node) {
		dev_err(dev, "Incorrect bindings: absent \"ports\" node\n");
		return -ENODEV;
	}

	rc = sja1105_parse_ports_node(priv, ports, ports_node);
	of_node_put(ports_node);

	return rc;
}

/* Convert link speed from SJA1105 to ethtool encoding */
static int sja1105_speed[] = {
	[SJA1105_SPEED_AUTO]		= SPEED_UNKNOWN,
	[SJA1105_SPEED_10MBPS]		= SPEED_10,
	[SJA1105_SPEED_100MBPS]		= SPEED_100,
	[SJA1105_SPEED_1000MBPS]	= SPEED_1000,
};

/* Set link speed in the MAC configuration for a specific port. */
static int sja1105_adjust_port_config(struct sja1105_private *priv, int port,
				      int speed_mbps)
{
	struct sja1105_xmii_params_entry *mii;
	struct sja1105_mac_config_entry *mac;
	struct device *dev = priv->ds->dev;
	sja1105_phy_interface_t phy_mode;
	sja1105_speed_t speed;
	int rc;

	/* On P/Q/R/S, one can read from the device via the MAC reconfiguration
	 * tables. On E/T, MAC reconfig tables are not readable, only writable.
	 * We have to *know* what the MAC looks like.  For the sake of keeping
	 * the code common, we'll use the static configuration tables as a
	 * reasonable approximation for both E/T and P/Q/R/S.
	 */
	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;
	mii = priv->static_config.tables[BLK_IDX_XMII_PARAMS].entries;

	switch (speed_mbps) {
	case SPEED_UNKNOWN:
		/* PHYLINK called sja1105_mac_config() to inform us about
		 * the state->interface, but AN has not completed and the
		 * speed is not yet valid. UM10944.pdf says that setting
		 * SJA1105_SPEED_AUTO at runtime disables the port, so that is
		 * ok for power consumption in case AN will never complete -
		 * otherwise PHYLINK should come back with a new update.
		 */
		speed = SJA1105_SPEED_AUTO;
		break;
	case SPEED_10:
		speed = SJA1105_SPEED_10MBPS;
		break;
	case SPEED_100:
		speed = SJA1105_SPEED_100MBPS;
		break;
	case SPEED_1000:
		speed = SJA1105_SPEED_1000MBPS;
		break;
	default:
		dev_err(dev, "Invalid speed %iMbps\n", speed_mbps);
		return -EINVAL;
	}

	/* Overwrite SJA1105_SPEED_AUTO from the static MAC configuration
	 * table, since this will be used for the clocking setup, and we no
	 * longer need to store it in the static config (already told hardware
	 * we want auto during upload phase).
	 */
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
	phy_mode = mii->xmii_mode[port];
	if (phy_mode != XMII_MODE_RGMII)
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
	struct sja1105_xmii_params_entry *mii;
	sja1105_phy_interface_t phy_mode;

	mii = priv->static_config.tables[BLK_IDX_XMII_PARAMS].entries;
	phy_mode = mii->xmii_mode[port];

	switch (interface) {
	case PHY_INTERFACE_MODE_MII:
		return (phy_mode != XMII_MODE_MII);
	case PHY_INTERFACE_MODE_RMII:
		return (phy_mode != XMII_MODE_RMII);
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return (phy_mode != XMII_MODE_RGMII);
	default:
		return true;
	}
}

static void sja1105_mac_config(struct dsa_switch *ds, int port,
			       unsigned int link_an_mode,
			       const struct phylink_link_state *state)
{
	struct sja1105_private *priv = ds->priv;

	if (sja1105_phy_mode_mismatch(priv, port, state->interface))
		return;

	if (link_an_mode == MLO_AN_INBAND) {
		dev_err(ds->dev, "In-band AN not supported!\n");
		return;
	}

	sja1105_adjust_port_config(priv, port, state->speed);
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
	sja1105_inhibit_tx(ds->priv, BIT(port), false);
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
	if (mii->xmii_mode[port] == XMII_MODE_RGMII)
		phylink_set(mask, 1000baseT_Full);

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
	struct sja1105_l2_lookup_entry l2_lookup = {0};
	struct sja1105_private *priv = ds->priv;
	struct device *dev = ds->dev;
	int last_unused = -1;
	int bin, way, rc;

	bin = sja1105et_fdb_hash(priv, addr, vid);

	way = sja1105et_is_fdb_entry_in_bin(priv, bin, addr, vid,
					    &l2_lookup, &last_unused);
	if (way >= 0) {
		/* We have an FDB entry. Is our port in the destination
		 * mask? If yes, we need to do nothing. If not, we need
		 * to rewrite the entry by adding this port to it.
		 */
		if (l2_lookup.destports & BIT(port))
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
	l2_lookup.index = sja1105et_fdb_index(bin, way);

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					  l2_lookup.index, &l2_lookup,
					  true);
	if (rc < 0)
		return rc;

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
	struct sja1105_l2_lookup_entry l2_lookup = {0};
	struct sja1105_private *priv = ds->priv;
	int rc, i;

	/* Search for an existing entry in the FDB table */
	l2_lookup.macaddr = ether_addr_to_u64(addr);
	l2_lookup.vlanid = vid;
	l2_lookup.iotag = SJA1105_S_TAG;
	l2_lookup.mask_macaddr = GENMASK_ULL(ETH_ALEN * 8 - 1, 0);
	if (dsa_port_is_vlan_filtering(dsa_to_port(ds, port))) {
		l2_lookup.mask_vlanid = VLAN_VID_MASK;
		l2_lookup.mask_iotag = BIT(0);
	} else {
		l2_lookup.mask_vlanid = 0;
		l2_lookup.mask_iotag = 0;
	}
	l2_lookup.destports = BIT(port);

	rc = sja1105_dynamic_config_read(priv, BLK_IDX_L2_LOOKUP,
					 SJA1105_SEARCH, &l2_lookup);
	if (rc == 0) {
		/* Found and this port is already in the entry's
		 * port mask => job done
		 */
		if (l2_lookup.destports & BIT(port))
			return 0;
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
	l2_lookup.lockeds = true;
	l2_lookup.index = i;

skip_finding_an_index:
	rc = sja1105_dynamic_config_write(priv, BLK_IDX_L2_LOOKUP,
					  l2_lookup.index, &l2_lookup,
					  true);
	if (rc < 0)
		return rc;

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
	l2_lookup.iotag = SJA1105_S_TAG;
	l2_lookup.mask_macaddr = GENMASK_ULL(ETH_ALEN * 8 - 1, 0);
	if (dsa_port_is_vlan_filtering(dsa_to_port(ds, port))) {
		l2_lookup.mask_vlanid = VLAN_VID_MASK;
		l2_lookup.mask_iotag = BIT(0);
	} else {
		l2_lookup.mask_vlanid = 0;
		l2_lookup.mask_iotag = 0;
	}
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

	/* dsa_8021q is in effect when the bridge's vlan_filtering isn't,
	 * so the switch still does some VLAN processing internally.
	 * But Shared VLAN Learning (SVL) is also active, and it will take
	 * care of autonomous forwarding between the unique pvid's of each
	 * port.  Here we just make sure that users can't add duplicate FDB
	 * entries when in this mode - the actual VID doesn't matter except
	 * for what gets printed in 'bridge fdb show'.  In the case of zero,
	 * no VID gets printed at all.
	 */
	if (!dsa_port_is_vlan_filtering(dsa_to_port(ds, port)))
		vid = 0;

	return priv->info->fdb_add_cmd(ds, port, addr, vid);
}

static int sja1105_fdb_del(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid)
{
	struct sja1105_private *priv = ds->priv;

	if (!dsa_port_is_vlan_filtering(dsa_to_port(ds, port)))
		vid = 0;

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
		u64_to_ether_addr(l2_lookup.macaddr, macaddr);

		/* We need to hide the dsa_8021q VLANs from the user. */
		if (!dsa_port_is_vlan_filtering(dsa_to_port(ds, port)))
			l2_lookup.vlanid = 0;
		cb(macaddr, l2_lookup.vlanid, l2_lookup.lockeds, data);
	}
	return 0;
}

/* This callback needs to be present */
static int sja1105_mdb_prepare(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_mdb *mdb)
{
	return 0;
}

static void sja1105_mdb_add(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_mdb *mdb)
{
	sja1105_fdb_add(ds, port, mdb->addr, mdb->vid);
}

static int sja1105_mdb_del(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_mdb *mdb)
{
	return sja1105_fdb_del(ds, port, mdb->addr, mdb->vid);
}

static int sja1105_bridge_member(struct dsa_switch *ds, int port,
				 struct net_device *br, bool member)
{
	struct sja1105_l2_forwarding_entry *l2_fwd;
	struct sja1105_private *priv = ds->priv;
	int i, rc;

	l2_fwd = priv->static_config.tables[BLK_IDX_L2_FORWARDING].entries;

	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
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

	return sja1105_dynamic_config_write(priv, BLK_IDX_L2_FORWARDING,
					    port, &l2_fwd[port], true);
}

static void sja1105_bridge_stp_state_set(struct dsa_switch *ds, int port,
					 u8 state)
{
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
		mac[port].dyn_learn = true;
		break;
	case BR_STATE_FORWARDING:
		mac[port].ingress   = true;
		mac[port].egress    = true;
		mac[port].dyn_learn = true;
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

static const char * const sja1105_reset_reasons[] = {
	[SJA1105_VLAN_FILTERING] = "VLAN filtering",
	[SJA1105_RX_HWTSTAMPING] = "RX timestamping",
	[SJA1105_AGEING_TIME] = "Ageing time",
	[SJA1105_SCHEDULING] = "Time-aware scheduling",
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
	struct sja1105_mac_config_entry *mac;
	int speed_mbps[SJA1105_NUM_PORTS];
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
	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		speed_mbps[i] = sja1105_speed[mac[i].speed];
		mac[i].speed = SJA1105_SPEED_AUTO;
	}

	/* No PTP operations can run right now */
	mutex_lock(&priv->ptp_data.lock);

	rc = __sja1105_ptp_gettimex(ds, &now, &ptp_sts_before);
	if (rc < 0)
		goto out_unlock_ptp;

	/* Reset switch and send updated static configuration */
	rc = sja1105_static_config_upload(priv);
	if (rc < 0)
		goto out_unlock_ptp;

	rc = __sja1105_ptp_settime(ds, 0, &ptp_sts_after);
	if (rc < 0)
		goto out_unlock_ptp;

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

out_unlock_ptp:
	mutex_unlock(&priv->ptp_data.lock);

	dev_info(priv->ds->dev,
		 "Reset switch and programmed static config. Reason: %s\n",
		 sja1105_reset_reasons[reason]);

	/* Configure the CGU (PLLs) for MII and RMII PHYs.
	 * For these interfaces there is no dynamic configuration
	 * needed, since PLLs have same settings at all speeds.
	 */
	rc = sja1105_clocking_setup(priv);
	if (rc < 0)
		goto out;

	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		rc = sja1105_adjust_port_config(priv, i, speed_mbps[i]);
		if (rc < 0)
			goto out;
	}
out:
	mutex_unlock(&priv->mgmt_lock);

	return rc;
}

static int sja1105_pvid_apply(struct sja1105_private *priv, int port, u16 pvid)
{
	struct sja1105_mac_config_entry *mac;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	mac[port].vlanid = pvid;

	return sja1105_dynamic_config_write(priv, BLK_IDX_MAC_CONFIG, port,
					   &mac[port], true);
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

static int sja1105_vlan_apply(struct sja1105_private *priv, int port, u16 vid,
			      bool enabled, bool untagged)
{
	struct sja1105_vlan_lookup_entry *vlan;
	struct sja1105_table *table;
	bool keep = true;
	int match, rc;

	table = &priv->static_config.tables[BLK_IDX_VLAN_LOOKUP];

	match = sja1105_is_vlan_configured(priv, vid);
	if (match < 0) {
		/* Can't delete a missing entry. */
		if (!enabled)
			return 0;
		rc = sja1105_table_resize(table, table->entry_count + 1);
		if (rc)
			return rc;
		match = table->entry_count - 1;
	}
	/* Assign pointer after the resize (it's new memory) */
	vlan = table->entries;
	vlan[match].vlanid = vid;
	if (enabled) {
		vlan[match].vlan_bc |= BIT(port);
		vlan[match].vmemb_port |= BIT(port);
	} else {
		vlan[match].vlan_bc &= ~BIT(port);
		vlan[match].vmemb_port &= ~BIT(port);
	}
	/* Also unset tag_port if removing this VLAN was requested,
	 * just so we don't have a confusing bitmap (no practical purpose).
	 */
	if (untagged || !enabled)
		vlan[match].tag_port &= ~BIT(port);
	else
		vlan[match].tag_port |= BIT(port);
	/* If there's no port left as member of this VLAN,
	 * it's time for it to go.
	 */
	if (!vlan[match].vmemb_port)
		keep = false;

	dev_dbg(priv->ds->dev,
		"%s: port %d, vid %llu, broadcast domain 0x%llx, "
		"port members 0x%llx, tagged ports 0x%llx, keep %d\n",
		__func__, port, vlan[match].vlanid, vlan[match].vlan_bc,
		vlan[match].vmemb_port, vlan[match].tag_port, keep);

	rc = sja1105_dynamic_config_write(priv, BLK_IDX_VLAN_LOOKUP, vid,
					  &vlan[match], keep);
	if (rc < 0)
		return rc;

	if (!keep)
		return sja1105_table_delete_entry(table, match);

	return 0;
}

static int sja1105_setup_8021q_tagging(struct dsa_switch *ds, bool enabled)
{
	int rc, i;

	for (i = 0; i < SJA1105_NUM_PORTS; i++) {
		rc = dsa_port_setup_8021q_tagging(ds, i, enabled);
		if (rc < 0) {
			dev_err(ds->dev, "Failed to setup VLAN tagging for port %d: %d\n",
				i, rc);
			return rc;
		}
	}
	dev_info(ds->dev, "%s switch tagging\n",
		 enabled ? "Enabled" : "Disabled");
	return 0;
}

static enum dsa_tag_protocol
sja1105_get_tag_protocol(struct dsa_switch *ds, int port,
			 enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_SJA1105;
}

/* This callback needs to be present */
static int sja1105_vlan_prepare(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_vlan *vlan)
{
	return 0;
}

/* The TPID setting belongs to the General Parameters table,
 * which can only be partially reconfigured at runtime (and not the TPID).
 * So a switch reset is required.
 */
static int sja1105_vlan_filtering(struct dsa_switch *ds, int port, bool enabled)
{
	struct sja1105_l2_lookup_params_entry *l2_lookup_params;
	struct sja1105_general_params_entry *general_params;
	struct sja1105_private *priv = ds->priv;
	struct sja1105_table *table;
	u16 tpid, tpid2;
	int rc;

	if (enabled) {
		/* Enable VLAN filtering. */
		tpid  = ETH_P_8021Q;
		tpid2 = ETH_P_8021AD;
	} else {
		/* Disable VLAN filtering. */
		tpid  = ETH_P_SJA1105;
		tpid2 = ETH_P_SJA1105;
	}

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
	 * No VLAN filtering => shared VLAN learning.
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
	l2_lookup_params->shared_learn = !enabled;

	rc = sja1105_static_config_reload(priv, SJA1105_VLAN_FILTERING);
	if (rc)
		dev_err(ds->dev, "Failed to change VLAN Ethertype\n");

	/* Switch port identification based on 802.1Q is only passable
	 * if we are not under a vlan_filtering bridge. So make sure
	 * the two configurations are mutually exclusive.
	 */
	return sja1105_setup_8021q_tagging(ds, !enabled);
}

static void sja1105_vlan_add(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_vlan *vlan)
{
	struct sja1105_private *priv = ds->priv;
	u16 vid;
	int rc;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		rc = sja1105_vlan_apply(priv, port, vid, true, vlan->flags &
					BRIDGE_VLAN_INFO_UNTAGGED);
		if (rc < 0) {
			dev_err(ds->dev, "Failed to add VLAN %d to port %d: %d\n",
				vid, port, rc);
			return;
		}
		if (vlan->flags & BRIDGE_VLAN_INFO_PVID) {
			rc = sja1105_pvid_apply(ds->priv, port, vid);
			if (rc < 0) {
				dev_err(ds->dev, "Failed to set pvid %d on port %d: %d\n",
					vid, port, rc);
				return;
			}
		}
	}
}

static int sja1105_vlan_del(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_vlan *vlan)
{
	struct sja1105_private *priv = ds->priv;
	u16 vid;
	int rc;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		rc = sja1105_vlan_apply(priv, port, vid, false, vlan->flags &
					BRIDGE_VLAN_INFO_UNTAGGED);
		if (rc < 0) {
			dev_err(ds->dev, "Failed to remove VLAN %d from port %d: %d\n",
				vid, port, rc);
			return rc;
		}
	}
	return 0;
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
	struct sja1105_dt_port ports[SJA1105_NUM_PORTS];
	struct sja1105_private *priv = ds->priv;
	int rc;

	rc = sja1105_parse_dt(priv, ports);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to parse DT: %d\n", rc);
		return rc;
	}

	/* Error out early if internal delays are required through DT
	 * and we can't apply them.
	 */
	rc = sja1105_parse_rgmii_delays(priv, ports);
	if (rc < 0) {
		dev_err(ds->dev, "RGMII delay not supported\n");
		return rc;
	}

	rc = sja1105_ptp_clock_register(ds);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to register PTP clock: %d\n", rc);
		return rc;
	}
	/* Create and send configuration down to device */
	rc = sja1105_static_config_load(priv, ports);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to load static config: %d\n", rc);
		return rc;
	}
	/* Configure the CGU (PHY link modes and speeds) */
	rc = sja1105_clocking_setup(priv);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to configure MII clocking: %d\n", rc);
		return rc;
	}
	/* On SJA1105, VLAN filtering per se is always enabled in hardware.
	 * The only thing we can do to disable it is lie about what the 802.1Q
	 * EtherType is.
	 * So it will still try to apply VLAN filtering, but all ingress
	 * traffic (except frames received with EtherType of ETH_P_SJA1105)
	 * will be internally tagged with a distorted VLAN header where the
	 * TPID is ETH_P_SJA1105, and the VLAN ID is the port pvid.
	 */
	ds->vlan_filtering_is_global = true;

	/* Advertise the 8 egress queues */
	ds->num_tx_queues = SJA1105_NUM_TC;

	/* The DSA/switchdev model brings up switch ports in standalone mode by
	 * default, and that means vlan_filtering is 0 since they're not under
	 * a bridge, so it's safe to set up switch tagging at this time.
	 */
	return sja1105_setup_8021q_tagging(ds, true);
}

static void sja1105_teardown(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	int port;

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		struct sja1105_port *sp = &priv->ports[port];

		if (!dsa_is_user_port(ds, port))
			continue;

		kthread_destroy_worker(sp->xmit_worker);
	}

	sja1105_tas_teardown(ds);
	sja1105_ptp_clock_unregister(ds);
	sja1105_static_config_free(&priv->static_config);
}

static int sja1105_port_enable(struct dsa_switch *ds, int port,
			       struct phy_device *phy)
{
	struct net_device *slave;

	if (!dsa_is_user_port(ds, port))
		return 0;

	slave = dsa_to_port(ds, port)->slave;

	slave->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;

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
		struct sk_buff *clone = DSA_SKB_CB(skb)->clone;

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

static int sja1105_port_setup_tc(struct dsa_switch *ds, int port,
				 enum tc_setup_type type,
				 void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_TAPRIO:
		return sja1105_setup_tc_taprio(ds, port, type_data);
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
	struct sja1105_table *table;
	bool already_enabled;
	u64 new_mirr_port;
	int rc;

	table = &priv->static_config.tables[BLK_IDX_GENERAL_PARAMS];
	general_params = table->entries;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;

	already_enabled = (general_params->mirr_port != SJA1105_NUM_PORTS);
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
		for (port = 0; port < SJA1105_NUM_PORTS; port++) {
			if (mac[port].ing_mirr || mac[port].egr_mirr) {
				keep = true;
				break;
			}
		}
		/* Unset already_enabled for next time */
		if (!keep)
			new_mirr_port = SJA1105_NUM_PORTS;
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

static const struct dsa_switch_ops sja1105_switch_ops = {
	.get_tag_protocol	= sja1105_get_tag_protocol,
	.setup			= sja1105_setup,
	.teardown		= sja1105_teardown,
	.set_ageing_time	= sja1105_set_ageing_time,
	.phylink_validate	= sja1105_phylink_validate,
	.phylink_mac_config	= sja1105_mac_config,
	.phylink_mac_link_up	= sja1105_mac_link_up,
	.phylink_mac_link_down	= sja1105_mac_link_down,
	.get_strings		= sja1105_get_strings,
	.get_ethtool_stats	= sja1105_get_ethtool_stats,
	.get_sset_count		= sja1105_get_sset_count,
	.get_ts_info		= sja1105_get_ts_info,
	.port_enable		= sja1105_port_enable,
	.port_disable		= sja1105_port_disable,
	.port_fdb_dump		= sja1105_fdb_dump,
	.port_fdb_add		= sja1105_fdb_add,
	.port_fdb_del		= sja1105_fdb_del,
	.port_bridge_join	= sja1105_bridge_join,
	.port_bridge_leave	= sja1105_bridge_leave,
	.port_stp_state_set	= sja1105_bridge_stp_state_set,
	.port_vlan_prepare	= sja1105_vlan_prepare,
	.port_vlan_filtering	= sja1105_vlan_filtering,
	.port_vlan_add		= sja1105_vlan_add,
	.port_vlan_del		= sja1105_vlan_del,
	.port_mdb_prepare	= sja1105_mdb_prepare,
	.port_mdb_add		= sja1105_mdb_add,
	.port_mdb_del		= sja1105_mdb_del,
	.port_hwtstamp_get	= sja1105_hwtstamp_get,
	.port_hwtstamp_set	= sja1105_hwtstamp_set,
	.port_rxtstamp		= sja1105_port_rxtstamp,
	.port_txtstamp		= sja1105_port_txtstamp,
	.port_setup_tc		= sja1105_port_setup_tc,
	.port_mirror_add	= sja1105_mirror_add,
	.port_mirror_del	= sja1105_mirror_del,
};

static int sja1105_check_device_id(struct sja1105_private *priv)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 prod_id[SJA1105_SIZE_DEVICE_ID] = {0};
	struct device *dev = &priv->spidev->dev;
	u32 device_id;
	u64 part_no;
	int rc;

	rc = sja1105_xfer_u32(priv, SPI_READ, regs->device_id, &device_id,
			      NULL);
	if (rc < 0)
		return rc;

	if (device_id != priv->info->device_id) {
		dev_err(dev, "Expected device ID 0x%llx but read 0x%x\n",
			priv->info->device_id, device_id);
		return -ENODEV;
	}

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->prod_id, prod_id,
			      SJA1105_SIZE_DEVICE_ID);
	if (rc < 0)
		return rc;

	sja1105_unpack(prod_id, &part_no, 19, 4, SJA1105_SIZE_DEVICE_ID);

	if (part_no != priv->info->part_no) {
		dev_err(dev, "Expected part number 0x%llx but read 0x%llx\n",
			priv->info->part_no, part_no);
		return -ENODEV;
	}

	return 0;
}

static int sja1105_probe(struct spi_device *spi)
{
	struct sja1105_tagger_data *tagger_data;
	struct device *dev = &spi->dev;
	struct sja1105_private *priv;
	struct dsa_switch *ds;
	int rc, port;

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
	ds->num_ports = SJA1105_NUM_PORTS;
	ds->ops = &sja1105_switch_ops;
	ds->priv = priv;
	priv->ds = ds;

	tagger_data = &priv->tagger_data;

	mutex_init(&priv->ptp_data.lock);
	mutex_init(&priv->mgmt_lock);

	sja1105_tas_setup(ds);

	rc = dsa_register_switch(priv->ds);
	if (rc)
		return rc;

	/* Connections between dsa_port and sja1105_port */
	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		struct sja1105_port *sp = &priv->ports[port];
		struct dsa_port *dp = dsa_to_port(ds, port);
		struct net_device *slave;

		if (!dsa_is_user_port(ds, port))
			continue;

		dp->priv = sp;
		sp->dp = dp;
		sp->data = tagger_data;
		slave = dp->slave;
		kthread_init_work(&sp->xmit_work, sja1105_port_deferred_xmit);
		sp->xmit_worker = kthread_create_worker(0, "%s_xmit",
							slave->name);
		if (IS_ERR(sp->xmit_worker)) {
			rc = PTR_ERR(sp->xmit_worker);
			dev_err(ds->dev,
				"failed to create deferred xmit thread: %d\n",
				rc);
			goto out;
		}
		skb_queue_head_init(&sp->xmit_queue);
	}

	return 0;
out:
	while (port-- > 0) {
		struct sja1105_port *sp = &priv->ports[port];

		if (!dsa_is_user_port(ds, port))
			continue;

		kthread_destroy_worker(sp->xmit_worker);
	}
	return rc;
}

static int sja1105_remove(struct spi_device *spi)
{
	struct sja1105_private *priv = spi_get_drvdata(spi);

	dsa_unregister_switch(priv->ds);
	return 0;
}

static const struct of_device_id sja1105_dt_ids[] = {
	{ .compatible = "nxp,sja1105e", .data = &sja1105e_info },
	{ .compatible = "nxp,sja1105t", .data = &sja1105t_info },
	{ .compatible = "nxp,sja1105p", .data = &sja1105p_info },
	{ .compatible = "nxp,sja1105q", .data = &sja1105q_info },
	{ .compatible = "nxp,sja1105r", .data = &sja1105r_info },
	{ .compatible = "nxp,sja1105s", .data = &sja1105s_info },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sja1105_dt_ids);

static struct spi_driver sja1105_driver = {
	.driver = {
		.name  = "sja1105",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sja1105_dt_ids),
	},
	.probe  = sja1105_probe,
	.remove = sja1105_remove,
};

module_spi_driver(sja1105_driver);

MODULE_AUTHOR("Vladimir Oltean <olteanv@gmail.com>");
MODULE_AUTHOR("Georg Waibel <georg.waibel@sensor-technik.de>");
MODULE_DESCRIPTION("SJA1105 Driver");
MODULE_LICENSE("GPL v2");
