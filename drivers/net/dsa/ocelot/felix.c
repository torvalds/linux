// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 NXP Semiconductors
 *
 * This is an umbrella module for all network switches that are
 * register-compatible with Ocelot and that perform I/O to their host CPU
 * through an NPI (Node Processor Interface) Ethernet port.
 */
#include <uapi/linux/if_bridge.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_ana.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot.h>
#include <linux/platform_device.h>
#include <linux/packing.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/pcs-lynx.h>
#include <net/pkt_sched.h>
#include <net/dsa.h>
#include "felix.h"

static enum dsa_tag_protocol felix_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_OCELOT;
}

static int felix_set_ageing_time(struct dsa_switch *ds,
				 unsigned int ageing_time)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_set_ageing_time(ocelot, ageing_time);

	return 0;
}

static int felix_fdb_dump(struct dsa_switch *ds, int port,
			  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_dump(ocelot, port, cb, data);
}

static int felix_fdb_add(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_add(ocelot, port, addr, vid);
}

static int felix_fdb_del(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_del(ocelot, port, addr, vid);
}

/* This callback needs to be present */
static int felix_mdb_prepare(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_mdb *mdb)
{
	return 0;
}

static void felix_mdb_add(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_mdb_add(ocelot, port, mdb);
}

static int felix_mdb_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_mdb_del(ocelot, port, mdb);
}

static void felix_bridge_stp_state_set(struct dsa_switch *ds, int port,
				       u8 state)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_bridge_stp_state_set(ocelot, port, state);
}

static int felix_bridge_join(struct dsa_switch *ds, int port,
			     struct net_device *br)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_bridge_join(ocelot, port, br);
}

static void felix_bridge_leave(struct dsa_switch *ds, int port,
			       struct net_device *br)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_bridge_leave(ocelot, port, br);
}

/* This callback needs to be present */
static int felix_vlan_prepare(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan)
{
	return 0;
}

static int felix_vlan_filtering(struct dsa_switch *ds, int port, bool enabled)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_vlan_filtering(ocelot, port, enabled);

	return 0;
}

static void felix_vlan_add(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_vlan *vlan)
{
	struct ocelot *ocelot = ds->priv;
	u16 flags = vlan->flags;
	u16 vid;
	int err;

	if (dsa_is_cpu_port(ds, port))
		flags &= ~BRIDGE_VLAN_INFO_UNTAGGED;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		err = ocelot_vlan_add(ocelot, port, vid,
				      flags & BRIDGE_VLAN_INFO_PVID,
				      flags & BRIDGE_VLAN_INFO_UNTAGGED);
		if (err) {
			dev_err(ds->dev, "Failed to add VLAN %d to port %d: %d\n",
				vid, port, err);
			return;
		}
	}
}

static int felix_vlan_del(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan)
{
	struct ocelot *ocelot = ds->priv;
	u16 vid;
	int err;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		err = ocelot_vlan_del(ocelot, port, vid);
		if (err) {
			dev_err(ds->dev, "Failed to remove VLAN %d from port %d: %d\n",
				vid, port, err);
			return err;
		}
	}
	return 0;
}

static int felix_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_enable(ocelot, port, phy);

	return 0;
}

static void felix_port_disable(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_disable(ocelot, port);
}

static void felix_phylink_validate(struct dsa_switch *ds, int port,
				   unsigned long *supported,
				   struct phylink_link_state *state)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->info->phylink_validate)
		felix->info->phylink_validate(ocelot, port, supported, state);
}

static void felix_phylink_mac_config(struct dsa_switch *ds, int port,
				     unsigned int link_an_mode,
				     const struct phylink_link_state *state)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (felix->pcs[port])
		phylink_set_pcs(dp->pl, &felix->pcs[port]->pcs);
}

static void felix_phylink_mac_link_down(struct dsa_switch *ds, int port,
					unsigned int link_an_mode,
					phy_interface_t interface)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	ocelot_port_writel(ocelot_port, 0, DEV_MAC_ENA_CFG);
	ocelot_fields_write(ocelot, port, QSYS_SWITCH_PORT_MODE_PORT_ENA, 0);
}

static void felix_phylink_mac_link_up(struct dsa_switch *ds, int port,
				      unsigned int link_an_mode,
				      phy_interface_t interface,
				      struct phy_device *phydev,
				      int speed, int duplex,
				      bool tx_pause, bool rx_pause)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct felix *felix = ocelot_to_felix(ocelot);
	u32 mac_fc_cfg;

	/* Take port out of reset by clearing the MAC_TX_RST, MAC_RX_RST and
	 * PORT_RST bits in DEV_CLOCK_CFG. Note that the way this system is
	 * integrated is that the MAC speed is fixed and it's the PCS who is
	 * performing the rate adaptation, so we have to write "1000Mbps" into
	 * the LINK_SPEED field of DEV_CLOCK_CFG (which is also its default
	 * value).
	 */
	ocelot_port_writel(ocelot_port,
			   DEV_CLOCK_CFG_LINK_SPEED(OCELOT_SPEED_1000),
			   DEV_CLOCK_CFG);

	switch (speed) {
	case SPEED_10:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(3);
		break;
	case SPEED_100:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(2);
		break;
	case SPEED_1000:
	case SPEED_2500:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(1);
		break;
	default:
		dev_err(ocelot->dev, "Unsupported speed on port %d: %d\n",
			port, speed);
		return;
	}

	/* handle Rx pause in all cases, with 2500base-X this is used for rate
	 * adaptation.
	 */
	mac_fc_cfg |= SYS_MAC_FC_CFG_RX_FC_ENA;

	if (tx_pause)
		mac_fc_cfg |= SYS_MAC_FC_CFG_TX_FC_ENA |
			      SYS_MAC_FC_CFG_PAUSE_VAL_CFG(0xffff) |
			      SYS_MAC_FC_CFG_FC_LATENCY_CFG(0x7) |
			      SYS_MAC_FC_CFG_ZERO_PAUSE_ENA;

	/* Flow control. Link speed is only used here to evaluate the time
	 * specification in incoming pause frames.
	 */
	ocelot_write_rix(ocelot, mac_fc_cfg, SYS_MAC_FC_CFG, port);

	ocelot_write_rix(ocelot, 0, ANA_POL_FLOWC, port);

	/* Undo the effects of felix_phylink_mac_link_down:
	 * enable MAC module
	 */
	ocelot_port_writel(ocelot_port, DEV_MAC_ENA_CFG_RX_ENA |
			   DEV_MAC_ENA_CFG_TX_ENA, DEV_MAC_ENA_CFG);

	/* Enable receiving frames on the port, and activate auto-learning of
	 * MAC addresses.
	 */
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_LEARNAUTO |
			 ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(port),
			 ANA_PORT_PORT_CFG, port);

	/* Core: Enable port for frame transfer */
	ocelot_fields_write(ocelot, port,
			    QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);

	if (felix->info->port_sched_speed_set)
		felix->info->port_sched_speed_set(ocelot, port, speed);
}

static void felix_port_qos_map_init(struct ocelot *ocelot, int port)
{
	int i;

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_QOS_CFG_QOS_PCP_ENA,
		       ANA_PORT_QOS_CFG_QOS_PCP_ENA,
		       ANA_PORT_QOS_CFG,
		       port);

	for (i = 0; i < FELIX_NUM_TC * 2; i++) {
		ocelot_rmw_ix(ocelot,
			      (ANA_PORT_PCP_DEI_MAP_DP_PCP_DEI_VAL & i) |
			      ANA_PORT_PCP_DEI_MAP_QOS_PCP_DEI_VAL(i),
			      ANA_PORT_PCP_DEI_MAP_DP_PCP_DEI_VAL |
			      ANA_PORT_PCP_DEI_MAP_QOS_PCP_DEI_VAL_M,
			      ANA_PORT_PCP_DEI_MAP,
			      port, i);
	}
}

static void felix_get_strings(struct dsa_switch *ds, int port,
			      u32 stringset, u8 *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_strings(ocelot, port, stringset, data);
}

static void felix_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_get_ethtool_stats(ocelot, port, data);
}

static int felix_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_sset_count(ocelot, port, sset);
}

static int felix_get_ts_info(struct dsa_switch *ds, int port,
			     struct ethtool_ts_info *info)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_ts_info(ocelot, port, info);
}

static int felix_parse_ports_node(struct felix *felix,
				  struct device_node *ports_node,
				  phy_interface_t *port_phy_modes)
{
	struct ocelot *ocelot = &felix->ocelot;
	struct device *dev = felix->ocelot.dev;
	struct device_node *child;

	for_each_available_child_of_node(ports_node, child) {
		phy_interface_t phy_mode;
		u32 port;
		int err;

		/* Get switch port number from DT */
		if (of_property_read_u32(child, "reg", &port) < 0) {
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
				port);
			of_node_put(child);
			return -ENODEV;
		}

		err = felix->info->prevalidate_phy_mode(ocelot, port, phy_mode);
		if (err < 0) {
			dev_err(dev, "Unsupported PHY mode %s on port %d\n",
				phy_modes(phy_mode), port);
			of_node_put(child);
			return err;
		}

		port_phy_modes[port] = phy_mode;
	}

	return 0;
}

static int felix_parse_dt(struct felix *felix, phy_interface_t *port_phy_modes)
{
	struct device *dev = felix->ocelot.dev;
	struct device_node *switch_node;
	struct device_node *ports_node;
	int err;

	switch_node = dev->of_node;

	ports_node = of_get_child_by_name(switch_node, "ports");
	if (!ports_node) {
		dev_err(dev, "Incorrect bindings: absent \"ports\" node\n");
		return -ENODEV;
	}

	err = felix_parse_ports_node(felix, ports_node, port_phy_modes);
	of_node_put(ports_node);

	return err;
}

static int felix_init_structs(struct felix *felix, int num_phys_ports)
{
	struct ocelot *ocelot = &felix->ocelot;
	phy_interface_t *port_phy_modes;
	struct resource res;
	int port, i, err;

	ocelot->num_phys_ports = num_phys_ports;
	ocelot->ports = devm_kcalloc(ocelot->dev, num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->map		= felix->info->map;
	ocelot->stats_layout	= felix->info->stats_layout;
	ocelot->num_stats	= felix->info->num_stats;
	ocelot->shared_queue_sz	= felix->info->shared_queue_sz;
	ocelot->num_mact_rows	= felix->info->num_mact_rows;
	ocelot->vcap_is2_keys	= felix->info->vcap_is2_keys;
	ocelot->vcap_is2_actions= felix->info->vcap_is2_actions;
	ocelot->vcap		= felix->info->vcap;
	ocelot->ops		= felix->info->ops;
	ocelot->inj_prefix	= OCELOT_TAG_PREFIX_SHORT;
	ocelot->xtr_prefix	= OCELOT_TAG_PREFIX_SHORT;

	port_phy_modes = kcalloc(num_phys_ports, sizeof(phy_interface_t),
				 GFP_KERNEL);
	if (!port_phy_modes)
		return -ENOMEM;

	err = felix_parse_dt(felix, port_phy_modes);
	if (err) {
		kfree(port_phy_modes);
		return err;
	}

	for (i = 0; i < TARGET_MAX; i++) {
		struct regmap *target;

		if (!felix->info->target_io_res[i].name)
			continue;

		memcpy(&res, &felix->info->target_io_res[i], sizeof(res));
		res.flags = IORESOURCE_MEM;
		res.start += felix->switch_base;
		res.end += felix->switch_base;

		target = ocelot_regmap_init(ocelot, &res);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map device memory space\n");
			kfree(port_phy_modes);
			return PTR_ERR(target);
		}

		ocelot->targets[i] = target;
	}

	err = ocelot_regfields_init(ocelot, felix->info->regfields);
	if (err) {
		dev_err(ocelot->dev, "failed to init reg fields map\n");
		kfree(port_phy_modes);
		return err;
	}

	for (port = 0; port < num_phys_ports; port++) {
		struct ocelot_port *ocelot_port;
		struct regmap *target;
		u8 *template;

		ocelot_port = devm_kzalloc(ocelot->dev,
					   sizeof(struct ocelot_port),
					   GFP_KERNEL);
		if (!ocelot_port) {
			dev_err(ocelot->dev,
				"failed to allocate port memory\n");
			kfree(port_phy_modes);
			return -ENOMEM;
		}

		memcpy(&res, &felix->info->port_io_res[port], sizeof(res));
		res.flags = IORESOURCE_MEM;
		res.start += felix->switch_base;
		res.end += felix->switch_base;

		target = ocelot_regmap_init(ocelot, &res);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map memory space for port %d\n",
				port);
			kfree(port_phy_modes);
			return PTR_ERR(target);
		}

		template = devm_kzalloc(ocelot->dev, OCELOT_TOTAL_TAG_LEN,
					GFP_KERNEL);
		if (!template) {
			dev_err(ocelot->dev,
				"Failed to allocate memory for DSA tag\n");
			kfree(port_phy_modes);
			return -ENOMEM;
		}

		ocelot_port->phy_mode = port_phy_modes[port];
		ocelot_port->ocelot = ocelot;
		ocelot_port->target = target;
		ocelot_port->xmit_template = template;
		ocelot->ports[port] = ocelot_port;

		felix->info->xmit_template_populate(ocelot, port);
	}

	kfree(port_phy_modes);

	if (felix->info->mdio_bus_alloc) {
		err = felix->info->mdio_bus_alloc(ocelot);
		if (err < 0)
			return err;
	}

	return 0;
}

/* The CPU port module is connected to the Node Processor Interface (NPI). This
 * is the mode through which frames can be injected from and extracted to an
 * external CPU, over Ethernet.
 */
static void felix_npi_port_init(struct ocelot *ocelot, int port)
{
	ocelot->npi = port;

	ocelot_write(ocelot, QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_M |
		     QSYS_EXT_CPU_CFG_EXT_CPU_PORT(port),
		     QSYS_EXT_CPU_CFG);

	/* NPI port Injection/Extraction configuration */
	ocelot_fields_write(ocelot, port, SYS_PORT_MODE_INCL_XTR_HDR,
			    ocelot->xtr_prefix);
	ocelot_fields_write(ocelot, port, SYS_PORT_MODE_INCL_INJ_HDR,
			    ocelot->inj_prefix);

	/* Disable transmission of pause frames */
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, 0);
}

/* Hardware initialization done here so that we can allocate structures with
 * devm without fear of dsa_register_switch returning -EPROBE_DEFER and causing
 * us to allocate structures twice (leak memory) and map PCI memory twice
 * (which will not work).
 */
static int felix_setup(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	int port, err;
	int tc;

	err = felix_init_structs(felix, ds->num_ports);
	if (err)
		return err;

	err = ocelot_init(ocelot);
	if (err)
		return err;

	if (ocelot->ptp) {
		err = ocelot_init_timestamp(ocelot, felix->info->ptp_caps);
		if (err) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			ocelot->ptp = 0;
		}
	}

	for (port = 0; port < ds->num_ports; port++) {
		ocelot_init_port(ocelot, port);

		if (dsa_is_cpu_port(ds, port))
			felix_npi_port_init(ocelot, port);

		/* Set the default QoS Classification based on PCP and DEI
		 * bits of vlan tag.
		 */
		felix_port_qos_map_init(ocelot, port);
	}

	/* Include the CPU port module in the forwarding mask for unknown
	 * unicast - the hardware default value for ANA_FLOODING_FLD_UNICAST
	 * excludes BIT(ocelot->num_phys_ports), and so does ocelot_init, since
	 * Ocelot relies on whitelisting MAC addresses towards PGID_CPU.
	 */
	ocelot_write_rix(ocelot,
			 ANA_PGID_PGID_PGID(GENMASK(ocelot->num_phys_ports, 0)),
			 ANA_PGID_PGID, PGID_UC);
	/* Setup the per-traffic class flooding PGIDs */
	for (tc = 0; tc < FELIX_NUM_TC; tc++)
		ocelot_write_rix(ocelot, ANA_FLOODING_FLD_MULTICAST(PGID_MC) |
				 ANA_FLOODING_FLD_BROADCAST(PGID_MC) |
				 ANA_FLOODING_FLD_UNICAST(PGID_UC),
				 ANA_FLOODING, tc);

	ds->mtu_enforcement_ingress = true;
	ds->configure_vlan_while_not_filtering = true;

	return 0;
}

static void felix_teardown(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	int port;

	if (felix->info->mdio_bus_free)
		felix->info->mdio_bus_free(ocelot);

	for (port = 0; port < ocelot->num_phys_ports; port++)
		ocelot_deinit_port(ocelot, port);
	ocelot_deinit_timestamp(ocelot);
	/* stop workqueue thread */
	ocelot_deinit(ocelot);
}

static int felix_hwtstamp_get(struct dsa_switch *ds, int port,
			      struct ifreq *ifr)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_hwstamp_get(ocelot, port, ifr);
}

static int felix_hwtstamp_set(struct dsa_switch *ds, int port,
			      struct ifreq *ifr)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_hwstamp_set(ocelot, port, ifr);
}

static bool felix_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct ocelot *ocelot = ds->priv;
	u8 *extraction = skb->data - ETH_HLEN - OCELOT_TAG_LEN;
	u32 tstamp_lo, tstamp_hi;
	struct timespec64 ts;
	u64 tstamp, val;

	ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);
	tstamp = ktime_set(ts.tv_sec, ts.tv_nsec);

	packing(extraction, &val,  116, 85, OCELOT_TAG_LEN, UNPACK, 0);
	tstamp_lo = (u32)val;

	tstamp_hi = tstamp >> 32;
	if ((tstamp & 0xffffffff) < tstamp_lo)
		tstamp_hi--;

	tstamp = ((u64)tstamp_hi << 32) | tstamp_lo;

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = tstamp;
	return false;
}

static bool felix_txtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *clone, unsigned int type)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	if (ocelot->ptp && (skb_shinfo(clone)->tx_flags & SKBTX_HW_TSTAMP) &&
	    ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
		ocelot_port_add_txtstamp_skb(ocelot, port, clone);
		return true;
	}

	return false;
}

static int felix_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_set_maxlen(ocelot, port, new_mtu);

	return 0;
}

static int felix_get_max_mtu(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_max_mtu(ocelot, port);
}

static int felix_cls_flower_add(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_cls_flower_replace(ocelot, port, cls, ingress);
}

static int felix_cls_flower_del(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_cls_flower_destroy(ocelot, port, cls, ingress);
}

static int felix_cls_flower_stats(struct dsa_switch *ds, int port,
				  struct flow_cls_offload *cls, bool ingress)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_cls_flower_stats(ocelot, port, cls, ingress);
}

static int felix_port_policer_add(struct dsa_switch *ds, int port,
				  struct dsa_mall_policer_tc_entry *policer)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_policer pol = {
		.rate = div_u64(policer->rate_bytes_per_sec, 1000) * 8,
		.burst = policer->burst,
	};

	return ocelot_port_policer_add(ocelot, port, &pol);
}

static void felix_port_policer_del(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_policer_del(ocelot, port);
}

static int felix_port_setup_tc(struct dsa_switch *ds, int port,
			       enum tc_setup_type type,
			       void *type_data)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->info->port_setup_tc)
		return felix->info->port_setup_tc(ds, port, type, type_data);
	else
		return -EOPNOTSUPP;
}

const struct dsa_switch_ops felix_switch_ops = {
	.get_tag_protocol	= felix_get_tag_protocol,
	.setup			= felix_setup,
	.teardown		= felix_teardown,
	.set_ageing_time	= felix_set_ageing_time,
	.get_strings		= felix_get_strings,
	.get_ethtool_stats	= felix_get_ethtool_stats,
	.get_sset_count		= felix_get_sset_count,
	.get_ts_info		= felix_get_ts_info,
	.phylink_validate	= felix_phylink_validate,
	.phylink_mac_config	= felix_phylink_mac_config,
	.phylink_mac_link_down	= felix_phylink_mac_link_down,
	.phylink_mac_link_up	= felix_phylink_mac_link_up,
	.port_enable		= felix_port_enable,
	.port_disable		= felix_port_disable,
	.port_fdb_dump		= felix_fdb_dump,
	.port_fdb_add		= felix_fdb_add,
	.port_fdb_del		= felix_fdb_del,
	.port_mdb_prepare	= felix_mdb_prepare,
	.port_mdb_add		= felix_mdb_add,
	.port_mdb_del		= felix_mdb_del,
	.port_bridge_join	= felix_bridge_join,
	.port_bridge_leave	= felix_bridge_leave,
	.port_stp_state_set	= felix_bridge_stp_state_set,
	.port_vlan_prepare	= felix_vlan_prepare,
	.port_vlan_filtering	= felix_vlan_filtering,
	.port_vlan_add		= felix_vlan_add,
	.port_vlan_del		= felix_vlan_del,
	.port_hwtstamp_get	= felix_hwtstamp_get,
	.port_hwtstamp_set	= felix_hwtstamp_set,
	.port_rxtstamp		= felix_rxtstamp,
	.port_txtstamp		= felix_txtstamp,
	.port_change_mtu	= felix_change_mtu,
	.port_max_mtu		= felix_get_max_mtu,
	.port_policer_add	= felix_port_policer_add,
	.port_policer_del	= felix_port_policer_del,
	.cls_flower_add		= felix_cls_flower_add,
	.cls_flower_del		= felix_cls_flower_del,
	.cls_flower_stats	= felix_cls_flower_stats,
	.port_setup_tc		= felix_port_setup_tc,
};
