// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/skbuff.h>
#include <linux/iopoll.h>
#include <net/arp.h>
#include <net/netevent.h>
#include <net/rtnetlink.h>
#include <net/switchdev.h>

#include "ocelot.h"
#include "ocelot_ace.h"

#define TABLE_UPDATE_SLEEP_US 10
#define TABLE_UPDATE_TIMEOUT_US 100000

/* MAC table entry types.
 * ENTRYTYPE_NORMAL is subject to aging.
 * ENTRYTYPE_LOCKED is not subject to aging.
 * ENTRYTYPE_MACv4 is not subject to aging. For IPv4 multicast.
 * ENTRYTYPE_MACv6 is not subject to aging. For IPv6 multicast.
 */
enum macaccess_entry_type {
	ENTRYTYPE_NORMAL = 0,
	ENTRYTYPE_LOCKED,
	ENTRYTYPE_MACv4,
	ENTRYTYPE_MACv6,
};

struct ocelot_mact_entry {
	u8 mac[ETH_ALEN];
	u16 vid;
	enum macaccess_entry_type type;
};

static inline u32 ocelot_mact_read_macaccess(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, ANA_TABLES_MACACCESS);
}

static inline int ocelot_mact_wait_for_completion(struct ocelot *ocelot)
{
	u32 val;

	return readx_poll_timeout(ocelot_mact_read_macaccess,
		ocelot, val,
		(val & ANA_TABLES_MACACCESS_MAC_TABLE_CMD_M) ==
		MACACCESS_CMD_IDLE,
		TABLE_UPDATE_SLEEP_US, TABLE_UPDATE_TIMEOUT_US);
}

static void ocelot_mact_select(struct ocelot *ocelot,
			       const unsigned char mac[ETH_ALEN],
			       unsigned int vid)
{
	u32 macl = 0, mach = 0;

	/* Set the MAC address to handle and the vlan associated in a format
	 * understood by the hardware.
	 */
	mach |= vid    << 16;
	mach |= mac[0] << 8;
	mach |= mac[1] << 0;
	macl |= mac[2] << 24;
	macl |= mac[3] << 16;
	macl |= mac[4] << 8;
	macl |= mac[5] << 0;

	ocelot_write(ocelot, macl, ANA_TABLES_MACLDATA);
	ocelot_write(ocelot, mach, ANA_TABLES_MACHDATA);

}

static int ocelot_mact_learn(struct ocelot *ocelot, int port,
			     const unsigned char mac[ETH_ALEN],
			     unsigned int vid,
			     enum macaccess_entry_type type)
{
	ocelot_mact_select(ocelot, mac, vid);

	/* Issue a write command */
	ocelot_write(ocelot, ANA_TABLES_MACACCESS_VALID |
			     ANA_TABLES_MACACCESS_DEST_IDX(port) |
			     ANA_TABLES_MACACCESS_ENTRYTYPE(type) |
			     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_LEARN),
			     ANA_TABLES_MACACCESS);

	return ocelot_mact_wait_for_completion(ocelot);
}

static int ocelot_mact_forget(struct ocelot *ocelot,
			      const unsigned char mac[ETH_ALEN],
			      unsigned int vid)
{
	ocelot_mact_select(ocelot, mac, vid);

	/* Issue a forget command */
	ocelot_write(ocelot,
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_FORGET),
		     ANA_TABLES_MACACCESS);

	return ocelot_mact_wait_for_completion(ocelot);
}

static void ocelot_mact_init(struct ocelot *ocelot)
{
	/* Configure the learning mode entries attributes:
	 * - Do not copy the frame to the CPU extraction queues.
	 * - Use the vlan and mac_cpoy for dmac lookup.
	 */
	ocelot_rmw(ocelot, 0,
		   ANA_AGENCTRL_LEARN_CPU_COPY | ANA_AGENCTRL_IGNORE_DMAC_FLAGS
		   | ANA_AGENCTRL_LEARN_FWD_KILL
		   | ANA_AGENCTRL_LEARN_IGNORE_VLAN,
		   ANA_AGENCTRL);

	/* Clear the MAC table */
	ocelot_write(ocelot, MACACCESS_CMD_INIT, ANA_TABLES_MACACCESS);
}

static void ocelot_vcap_enable(struct ocelot *ocelot, int port)
{
	ocelot_write_gix(ocelot, ANA_PORT_VCAP_S2_CFG_S2_ENA |
			 ANA_PORT_VCAP_S2_CFG_S2_IP6_CFG(0xa),
			 ANA_PORT_VCAP_S2_CFG, port);
}

static inline u32 ocelot_vlant_read_vlanaccess(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, ANA_TABLES_VLANACCESS);
}

static inline int ocelot_vlant_wait_for_completion(struct ocelot *ocelot)
{
	u32 val;

	return readx_poll_timeout(ocelot_vlant_read_vlanaccess,
		ocelot,
		val,
		(val & ANA_TABLES_VLANACCESS_VLAN_TBL_CMD_M) ==
		ANA_TABLES_VLANACCESS_CMD_IDLE,
		TABLE_UPDATE_SLEEP_US, TABLE_UPDATE_TIMEOUT_US);
}

static int ocelot_vlant_set_mask(struct ocelot *ocelot, u16 vid, u32 mask)
{
	/* Select the VID to configure */
	ocelot_write(ocelot, ANA_TABLES_VLANTIDX_V_INDEX(vid),
		     ANA_TABLES_VLANTIDX);
	/* Set the vlan port members mask and issue a write command */
	ocelot_write(ocelot, ANA_TABLES_VLANACCESS_VLAN_PORT_MASK(mask) |
			     ANA_TABLES_VLANACCESS_CMD_WRITE,
		     ANA_TABLES_VLANACCESS);

	return ocelot_vlant_wait_for_completion(ocelot);
}

static void ocelot_vlan_mode(struct ocelot *ocelot, int port,
			     netdev_features_t features)
{
	u32 val;

	/* Filtering */
	val = ocelot_read(ocelot, ANA_VLANMASK);
	if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
		val |= BIT(port);
	else
		val &= ~BIT(port);
	ocelot_write(ocelot, val, ANA_VLANMASK);
}

void ocelot_port_vlan_filtering(struct ocelot *ocelot, int port,
				bool vlan_aware)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u32 val;

	if (vlan_aware)
		val = ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
		      ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1);
	else
		val = 0;
	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
		       ANA_PORT_VLAN_CFG_VLAN_POP_CNT_M,
		       ANA_PORT_VLAN_CFG, port);

	if (vlan_aware && !ocelot_port->vid)
		/* If port is vlan-aware and tagged, drop untagged and priority
		 * tagged frames.
		 */
		val = ANA_PORT_DROP_CFG_DROP_UNTAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA;
	else
		val = 0;
	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_DROP_CFG_DROP_UNTAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA,
		       ANA_PORT_DROP_CFG, port);

	if (vlan_aware) {
		if (ocelot_port->vid)
			/* Tag all frames except when VID == DEFAULT_VLAN */
			val |= REW_TAG_CFG_TAG_CFG(1);
		else
			/* Tag all frames */
			val |= REW_TAG_CFG_TAG_CFG(3);
	} else {
		/* Port tagging disabled. */
		val = REW_TAG_CFG_TAG_CFG(0);
	}
	ocelot_rmw_gix(ocelot, val,
		       REW_TAG_CFG_TAG_CFG_M,
		       REW_TAG_CFG, port);
}
EXPORT_SYMBOL(ocelot_port_vlan_filtering);

static int ocelot_port_set_native_vlan(struct ocelot *ocelot, int port,
				       u16 vid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	if (ocelot_port->vid != vid) {
		/* Always permit deleting the native VLAN (vid = 0) */
		if (ocelot_port->vid && vid) {
			dev_err(ocelot->dev,
				"Port already has a native VLAN: %d\n",
				ocelot_port->vid);
			return -EBUSY;
		}
		ocelot_port->vid = vid;
	}

	ocelot_rmw_gix(ocelot, REW_PORT_VLAN_CFG_PORT_VID(vid),
		       REW_PORT_VLAN_CFG_PORT_VID_M,
		       REW_PORT_VLAN_CFG, port);

	return 0;
}

/* Default vlan to clasify for untagged frames (may be zero) */
static void ocelot_port_set_pvid(struct ocelot *ocelot, int port, u16 pvid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_VLAN_CFG_VLAN_VID(pvid),
		       ANA_PORT_VLAN_CFG_VLAN_VID_M,
		       ANA_PORT_VLAN_CFG, port);

	ocelot_port->pvid = pvid;
}

int ocelot_vlan_add(struct ocelot *ocelot, int port, u16 vid, bool pvid,
		    bool untagged)
{
	int ret;

	/* Make the port a member of the VLAN */
	ocelot->vlan_mask[vid] |= BIT(port);
	ret = ocelot_vlant_set_mask(ocelot, vid, ocelot->vlan_mask[vid]);
	if (ret)
		return ret;

	/* Default ingress vlan classification */
	if (pvid)
		ocelot_port_set_pvid(ocelot, port, vid);

	/* Untagged egress vlan clasification */
	if (untagged) {
		ret = ocelot_port_set_native_vlan(ocelot, port, vid);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_vlan_add);

static int ocelot_vlan_vid_add(struct net_device *dev, u16 vid, bool pvid,
			       bool untagged)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;
	int ret;

	ret = ocelot_vlan_add(ocelot, port, vid, pvid, untagged);
	if (ret)
		return ret;

	/* Add the port MAC address to with the right VLAN information */
	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr, vid,
			  ENTRYTYPE_LOCKED);

	return 0;
}

int ocelot_vlan_del(struct ocelot *ocelot, int port, u16 vid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int ret;

	/* Stop the port from being a member of the vlan */
	ocelot->vlan_mask[vid] &= ~BIT(port);
	ret = ocelot_vlant_set_mask(ocelot, vid, ocelot->vlan_mask[vid]);
	if (ret)
		return ret;

	/* Ingress */
	if (ocelot_port->pvid == vid)
		ocelot_port_set_pvid(ocelot, port, 0);

	/* Egress */
	if (ocelot_port->vid == vid)
		ocelot_port_set_native_vlan(ocelot, port, 0);

	return 0;
}
EXPORT_SYMBOL(ocelot_vlan_del);

static int ocelot_vlan_vid_del(struct net_device *dev, u16 vid)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;
	int ret;

	/* 8021q removes VID 0 on module unload for all interfaces
	 * with VLAN filtering feature. We need to keep it to receive
	 * untagged traffic.
	 */
	if (vid == 0)
		return 0;

	ret = ocelot_vlan_del(ocelot, port, vid);
	if (ret)
		return ret;

	/* Del the port MAC address to with the right VLAN information */
	ocelot_mact_forget(ocelot, dev->dev_addr, vid);

	return 0;
}

static void ocelot_vlan_init(struct ocelot *ocelot)
{
	u16 port, vid;

	/* Clear VLAN table, by default all ports are members of all VLANs */
	ocelot_write(ocelot, ANA_TABLES_VLANACCESS_CMD_INIT,
		     ANA_TABLES_VLANACCESS);
	ocelot_vlant_wait_for_completion(ocelot);

	/* Configure the port VLAN memberships */
	for (vid = 1; vid < VLAN_N_VID; vid++) {
		ocelot->vlan_mask[vid] = 0;
		ocelot_vlant_set_mask(ocelot, vid, ocelot->vlan_mask[vid]);
	}

	/* Because VLAN filtering is enabled, we need VID 0 to get untagged
	 * traffic.  It is added automatically if 8021q module is loaded, but
	 * we can't rely on it since module may be not loaded.
	 */
	ocelot->vlan_mask[0] = GENMASK(ocelot->num_phys_ports - 1, 0);
	ocelot_vlant_set_mask(ocelot, 0, ocelot->vlan_mask[0]);

	/* Set vlan ingress filter mask to all ports but the CPU port by
	 * default.
	 */
	ocelot_write(ocelot, GENMASK(ocelot->num_phys_ports - 1, 0),
		     ANA_VLANMASK);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		ocelot_write_gix(ocelot, 0, REW_PORT_VLAN_CFG, port);
		ocelot_write_gix(ocelot, 0, REW_TAG_CFG, port);
	}
}

/* Watermark encode
 * Bit 8:   Unit; 0:1, 1:16
 * Bit 7-0: Value to be multiplied with unit
 */
static u16 ocelot_wm_enc(u16 value)
{
	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

void ocelot_adjust_link(struct ocelot *ocelot, int port,
			struct phy_device *phydev)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int speed, mode = 0;

	switch (phydev->speed) {
	case SPEED_10:
		speed = OCELOT_SPEED_10;
		break;
	case SPEED_100:
		speed = OCELOT_SPEED_100;
		break;
	case SPEED_1000:
		speed = OCELOT_SPEED_1000;
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA;
		break;
	case SPEED_2500:
		speed = OCELOT_SPEED_2500;
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA;
		break;
	default:
		dev_err(ocelot->dev, "Unsupported PHY speed on port %d: %d\n",
			port, phydev->speed);
		return;
	}

	phy_print_status(phydev);

	if (!phydev->link)
		return;

	/* Only full duplex supported for now */
	ocelot_port_writel(ocelot_port, DEV_MAC_MODE_CFG_FDX_ENA |
			   mode, DEV_MAC_MODE_CFG);

	if (ocelot->ops->pcs_init)
		ocelot->ops->pcs_init(ocelot, port);

	/* Enable MAC module */
	ocelot_port_writel(ocelot_port, DEV_MAC_ENA_CFG_RX_ENA |
			   DEV_MAC_ENA_CFG_TX_ENA, DEV_MAC_ENA_CFG);

	/* Take MAC, Port, Phy (intern) and PCS (SGMII/Serdes) clock out of
	 * reset */
	ocelot_port_writel(ocelot_port, DEV_CLOCK_CFG_LINK_SPEED(speed),
			   DEV_CLOCK_CFG);

	/* No PFC */
	ocelot_write_gix(ocelot, ANA_PFC_PFC_CFG_FC_LINK_SPEED(speed),
			 ANA_PFC_PFC_CFG, port);

	/* Core: Enable port for frame transfer */
	ocelot_write_rix(ocelot, QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE |
			 QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG(1) |
			 QSYS_SWITCH_PORT_MODE_PORT_ENA,
			 QSYS_SWITCH_PORT_MODE, port);

	/* Flow control */
	ocelot_write_rix(ocelot, SYS_MAC_FC_CFG_PAUSE_VAL_CFG(0xffff) |
			 SYS_MAC_FC_CFG_RX_FC_ENA | SYS_MAC_FC_CFG_TX_FC_ENA |
			 SYS_MAC_FC_CFG_ZERO_PAUSE_ENA |
			 SYS_MAC_FC_CFG_FC_LATENCY_CFG(0x7) |
			 SYS_MAC_FC_CFG_FC_LINK_SPEED(speed),
			 SYS_MAC_FC_CFG, port);
	ocelot_write_rix(ocelot, 0, ANA_POL_FLOWC, port);
}
EXPORT_SYMBOL(ocelot_adjust_link);

static void ocelot_port_adjust_link(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	ocelot_adjust_link(ocelot, port, dev->phydev);
}

void ocelot_port_enable(struct ocelot *ocelot, int port,
			struct phy_device *phy)
{
	/* Enable receiving frames on the port, and activate auto-learning of
	 * MAC addresses.
	 */
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_LEARNAUTO |
			 ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(port),
			 ANA_PORT_PORT_CFG, port);
}
EXPORT_SYMBOL(ocelot_port_enable);

static int ocelot_port_open(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;
	int err;

	if (priv->serdes) {
		err = phy_set_mode_ext(priv->serdes, PHY_MODE_ETHERNET,
				       priv->phy_mode);
		if (err) {
			netdev_err(dev, "Could not set mode of SerDes\n");
			return err;
		}
	}

	err = phy_connect_direct(dev, priv->phy, &ocelot_port_adjust_link,
				 priv->phy_mode);
	if (err) {
		netdev_err(dev, "Could not attach to PHY\n");
		return err;
	}

	dev->phydev = priv->phy;

	phy_attached_info(priv->phy);
	phy_start(priv->phy);

	ocelot_port_enable(ocelot, port, priv->phy);

	return 0;
}

void ocelot_port_disable(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	ocelot_port_writel(ocelot_port, 0, DEV_MAC_ENA_CFG);
	ocelot_rmw_rix(ocelot, 0, QSYS_SWITCH_PORT_MODE_PORT_ENA,
		       QSYS_SWITCH_PORT_MODE, port);
}
EXPORT_SYMBOL(ocelot_port_disable);

static int ocelot_port_stop(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	phy_disconnect(priv->phy);

	dev->phydev = NULL;

	ocelot_port_disable(ocelot, port);

	return 0;
}

/* Generate the IFH for frame injection
 *
 * The IFH is a 128bit-value
 * bit 127: bypass the analyzer processing
 * bit 56-67: destination mask
 * bit 28-29: pop_cnt: 3 disables all rewriting of the frame
 * bit 20-27: cpu extraction queue mask
 * bit 16: tag type 0: C-tag, 1: S-tag
 * bit 0-11: VID
 */
static int ocelot_gen_ifh(u32 *ifh, struct frame_info *info)
{
	ifh[0] = IFH_INJ_BYPASS | ((0x1ff & info->rew_op) << 21);
	ifh[1] = (0xf00 & info->port) >> 8;
	ifh[2] = (0xff & info->port) << 24;
	ifh[3] = (info->tag_type << 16) | info->vid;

	return 0;
}

static int ocelot_port_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	u32 val, ifh[OCELOT_TAG_LEN / 4];
	struct frame_info info = {};
	u8 grp = 0; /* Send everything on CPU group 0 */
	unsigned int i, count, last;
	int port = priv->chip_port;

	val = ocelot_read(ocelot, QS_INJ_STATUS);
	if (!(val & QS_INJ_STATUS_FIFO_RDY(BIT(grp))) ||
	    (val & QS_INJ_STATUS_WMARK_REACHED(BIT(grp))))
		return NETDEV_TX_BUSY;

	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_SOF, QS_INJ_CTRL, grp);

	info.port = BIT(port);
	info.tag_type = IFH_TAG_TYPE_C;
	info.vid = skb_vlan_tag_get(skb);

	/* Check if timestamping is needed */
	if (ocelot->ptp && shinfo->tx_flags & SKBTX_HW_TSTAMP) {
		info.rew_op = ocelot_port->ptp_cmd;
		if (ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP)
			info.rew_op |= (ocelot_port->ts_id  % 4) << 3;
	}

	ocelot_gen_ifh(ifh, &info);

	for (i = 0; i < OCELOT_TAG_LEN / 4; i++)
		ocelot_write_rix(ocelot, (__force u32)cpu_to_be32(ifh[i]),
				 QS_INJ_WR, grp);

	count = (skb->len + 3) / 4;
	last = skb->len % 4;
	for (i = 0; i < count; i++) {
		ocelot_write_rix(ocelot, ((u32 *)skb->data)[i], QS_INJ_WR, grp);
	}

	/* Add padding */
	while (i < (OCELOT_BUFFER_CELL_SZ / 4)) {
		ocelot_write_rix(ocelot, 0, QS_INJ_WR, grp);
		i++;
	}

	/* Indicate EOF and valid bytes in last word */
	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_VLD_BYTES(skb->len < OCELOT_BUFFER_CELL_SZ ? 0 : last) |
			 QS_INJ_CTRL_EOF,
			 QS_INJ_CTRL, grp);

	/* Add dummy CRC */
	ocelot_write_rix(ocelot, 0, QS_INJ_WR, grp);
	skb_tx_timestamp(skb);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	if (ocelot->ptp && shinfo->tx_flags & SKBTX_HW_TSTAMP &&
	    ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
		struct ocelot_skb *oskb =
			kzalloc(sizeof(struct ocelot_skb), GFP_ATOMIC);

		if (unlikely(!oskb))
			goto out;

		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

		oskb->skb = skb;
		oskb->id = ocelot_port->ts_id % 4;
		ocelot_port->ts_id++;

		list_add_tail(&oskb->head, &ocelot_port->skbs);

		return NETDEV_TX_OK;
	}

out:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

void ocelot_get_hwtimestamp(struct ocelot *ocelot, struct timespec64 *ts)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	/* Read current PTP time to get seconds */
	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);

	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_SAVE);
	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);
	ts->tv_sec = ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);

	/* Read packet HW timestamp from FIFO */
	val = ocelot_read(ocelot, SYS_PTP_TXSTAMP);
	ts->tv_nsec = SYS_PTP_TXSTAMP_PTP_TXSTAMP(val);

	/* Sec has incremented since the ts was registered */
	if ((ts->tv_sec & 0x1) != !!(val & SYS_PTP_TXSTAMP_PTP_TXSTAMP_SEC))
		ts->tv_sec--;

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
}
EXPORT_SYMBOL(ocelot_get_hwtimestamp);

static int ocelot_mc_unsync(struct net_device *dev, const unsigned char *addr)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;

	return ocelot_mact_forget(ocelot, addr, ocelot_port->pvid);
}

static int ocelot_mc_sync(struct net_device *dev, const unsigned char *addr)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;

	return ocelot_mact_learn(ocelot, PGID_CPU, addr, ocelot_port->pvid,
				 ENTRYTYPE_LOCKED);
}

static void ocelot_set_rx_mode(struct net_device *dev)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	u32 val;
	int i;

	/* This doesn't handle promiscuous mode because the bridge core is
	 * setting IFF_PROMISC on all slave interfaces and all frames would be
	 * forwarded to the CPU port.
	 */
	val = GENMASK(ocelot->num_phys_ports - 1, 0);
	for (i = ocelot->num_phys_ports + 1; i < PGID_CPU; i++)
		ocelot_write_rix(ocelot, val, ANA_PGID_PGID, i);

	__dev_mc_sync(dev, ocelot_mc_sync, ocelot_mc_unsync);
}

static int ocelot_port_get_phys_port_name(struct net_device *dev,
					  char *buf, size_t len)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	int port = priv->chip_port;
	int ret;

	ret = snprintf(buf, len, "p%d", port);
	if (ret >= len)
		return -EINVAL;

	return 0;
}

static int ocelot_port_set_mac_address(struct net_device *dev, void *p)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	const struct sockaddr *addr = p;

	/* Learn the new net device MAC address in the mac table. */
	ocelot_mact_learn(ocelot, PGID_CPU, addr->sa_data, ocelot_port->pvid,
			  ENTRYTYPE_LOCKED);
	/* Then forget the previous one. */
	ocelot_mact_forget(ocelot, dev->dev_addr, ocelot_port->pvid);

	ether_addr_copy(dev->dev_addr, addr->sa_data);
	return 0;
}

static void ocelot_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	/* Configure the port to read the stats from */
	ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(port),
		     SYS_STAT_CFG);

	/* Get Rx stats */
	stats->rx_bytes = ocelot_read(ocelot, SYS_COUNT_RX_OCTETS);
	stats->rx_packets = ocelot_read(ocelot, SYS_COUNT_RX_SHORTS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_FRAGMENTS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_JABBERS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_LONGS) +
			    ocelot_read(ocelot, SYS_COUNT_RX_64) +
			    ocelot_read(ocelot, SYS_COUNT_RX_65_127) +
			    ocelot_read(ocelot, SYS_COUNT_RX_128_255) +
			    ocelot_read(ocelot, SYS_COUNT_RX_256_1023) +
			    ocelot_read(ocelot, SYS_COUNT_RX_1024_1526) +
			    ocelot_read(ocelot, SYS_COUNT_RX_1527_MAX);
	stats->multicast = ocelot_read(ocelot, SYS_COUNT_RX_MULTICAST);
	stats->rx_dropped = dev->stats.rx_dropped;

	/* Get Tx stats */
	stats->tx_bytes = ocelot_read(ocelot, SYS_COUNT_TX_OCTETS);
	stats->tx_packets = ocelot_read(ocelot, SYS_COUNT_TX_64) +
			    ocelot_read(ocelot, SYS_COUNT_TX_65_127) +
			    ocelot_read(ocelot, SYS_COUNT_TX_128_511) +
			    ocelot_read(ocelot, SYS_COUNT_TX_512_1023) +
			    ocelot_read(ocelot, SYS_COUNT_TX_1024_1526) +
			    ocelot_read(ocelot, SYS_COUNT_TX_1527_MAX);
	stats->tx_dropped = ocelot_read(ocelot, SYS_COUNT_TX_DROPS) +
			    ocelot_read(ocelot, SYS_COUNT_TX_AGING);
	stats->collisions = ocelot_read(ocelot, SYS_COUNT_TX_COLLISION);
}

int ocelot_fdb_add(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid, bool vlan_aware)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	if (!vid) {
		if (!vlan_aware)
			/* If the bridge is not VLAN aware and no VID was
			 * provided, set it to pvid to ensure the MAC entry
			 * matches incoming untagged packets
			 */
			vid = ocelot_port->pvid;
		else
			/* If the bridge is VLAN aware a VID must be provided as
			 * otherwise the learnt entry wouldn't match any frame.
			 */
			return -EINVAL;
	}

	return ocelot_mact_learn(ocelot, port, addr, vid, ENTRYTYPE_LOCKED);
}
EXPORT_SYMBOL(ocelot_fdb_add);

static int ocelot_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr,
			       u16 vid, u16 flags,
			       struct netlink_ext_ack *extack)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return ocelot_fdb_add(ocelot, port, addr, vid, priv->vlan_aware);
}

int ocelot_fdb_del(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid)
{
	return ocelot_mact_forget(ocelot, addr, vid);
}
EXPORT_SYMBOL(ocelot_fdb_del);

static int ocelot_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr, u16 vid)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return ocelot_fdb_del(ocelot, port, addr, vid);
}

struct ocelot_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static int ocelot_port_fdb_do_dump(const unsigned char *addr, u16 vid,
				   bool is_static, void *data)
{
	struct ocelot_dump_ctx *dump = data;
	u32 portid = NETLINK_CB(dump->cb->skb).portid;
	u32 seq = dump->cb->nlh->nlmsg_seq;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	if (dump->idx < dump->cb->args[2])
		goto skip;

	nlh = nlmsg_put(dump->skb, portid, seq, RTM_NEWNEIGH,
			sizeof(*ndm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family  = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags   = NTF_SELF;
	ndm->ndm_type    = 0;
	ndm->ndm_ifindex = dump->dev->ifindex;
	ndm->ndm_state   = is_static ? NUD_NOARP : NUD_REACHABLE;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, addr))
		goto nla_put_failure;

	if (vid && nla_put_u16(dump->skb, NDA_VLAN, vid))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

static int ocelot_mact_read(struct ocelot *ocelot, int port, int row, int col,
			    struct ocelot_mact_entry *entry)
{
	u32 val, dst, macl, mach;
	char mac[ETH_ALEN];

	/* Set row and column to read from */
	ocelot_field_write(ocelot, ANA_TABLES_MACTINDX_M_INDEX, row);
	ocelot_field_write(ocelot, ANA_TABLES_MACTINDX_BUCKET, col);

	/* Issue a read command */
	ocelot_write(ocelot,
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_READ),
		     ANA_TABLES_MACACCESS);

	if (ocelot_mact_wait_for_completion(ocelot))
		return -ETIMEDOUT;

	/* Read the entry flags */
	val = ocelot_read(ocelot, ANA_TABLES_MACACCESS);
	if (!(val & ANA_TABLES_MACACCESS_VALID))
		return -EINVAL;

	/* If the entry read has another port configured as its destination,
	 * do not report it.
	 */
	dst = (val & ANA_TABLES_MACACCESS_DEST_IDX_M) >> 3;
	if (dst != port)
		return -EINVAL;

	/* Get the entry's MAC address and VLAN id */
	macl = ocelot_read(ocelot, ANA_TABLES_MACLDATA);
	mach = ocelot_read(ocelot, ANA_TABLES_MACHDATA);

	mac[0] = (mach >> 8)  & 0xff;
	mac[1] = (mach >> 0)  & 0xff;
	mac[2] = (macl >> 24) & 0xff;
	mac[3] = (macl >> 16) & 0xff;
	mac[4] = (macl >> 8)  & 0xff;
	mac[5] = (macl >> 0)  & 0xff;

	entry->vid = (mach >> 16) & 0xfff;
	ether_addr_copy(entry->mac, mac);

	return 0;
}

int ocelot_fdb_dump(struct ocelot *ocelot, int port,
		    dsa_fdb_dump_cb_t *cb, void *data)
{
	int i, j;

	/* Loop through all the mac tables entries. There are 1024 rows of 4
	 * entries.
	 */
	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 4; j++) {
			struct ocelot_mact_entry entry;
			bool is_static;
			int ret;

			ret = ocelot_mact_read(ocelot, port, i, j, &entry);
			/* If the entry is invalid (wrong port, invalid...),
			 * skip it.
			 */
			if (ret == -EINVAL)
				continue;
			else if (ret)
				return ret;

			is_static = (entry.type == ENTRYTYPE_LOCKED);

			ret = cb(entry.mac, entry.vid, is_static, data);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_fdb_dump);

static int ocelot_port_fdb_dump(struct sk_buff *skb,
				struct netlink_callback *cb,
				struct net_device *dev,
				struct net_device *filter_dev, int *idx)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	struct ocelot_dump_ctx dump = {
		.dev = dev,
		.skb = skb,
		.cb = cb,
		.idx = *idx,
	};
	int port = priv->chip_port;
	int ret;

	ret = ocelot_fdb_dump(ocelot, port, ocelot_port_fdb_do_dump, &dump);

	*idx = dump.idx;

	return ret;
}

static int ocelot_vlan_rx_add_vid(struct net_device *dev, __be16 proto,
				  u16 vid)
{
	return ocelot_vlan_vid_add(dev, vid, false, false);
}

static int ocelot_vlan_rx_kill_vid(struct net_device *dev, __be16 proto,
				   u16 vid)
{
	return ocelot_vlan_vid_del(dev, vid);
}

static int ocelot_set_features(struct net_device *dev,
			       netdev_features_t features)
{
	netdev_features_t changed = dev->features ^ features;
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	if ((dev->features & NETIF_F_HW_TC) > (features & NETIF_F_HW_TC) &&
	    priv->tc.offload_cnt) {
		netdev_err(dev,
			   "Cannot disable HW TC offload while offloads active\n");
		return -EBUSY;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER)
		ocelot_vlan_mode(ocelot, port, features);

	return 0;
}

static int ocelot_get_port_parent_id(struct net_device *dev,
				     struct netdev_phys_item_id *ppid)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;

	ppid->id_len = sizeof(ocelot->base_mac);
	memcpy(&ppid->id, &ocelot->base_mac, ppid->id_len);

	return 0;
}

int ocelot_hwstamp_get(struct ocelot *ocelot, int port, struct ifreq *ifr)
{
	return copy_to_user(ifr->ifr_data, &ocelot->hwtstamp_config,
			    sizeof(ocelot->hwtstamp_config)) ? -EFAULT : 0;
}
EXPORT_SYMBOL(ocelot_hwstamp_get);

int ocelot_hwstamp_set(struct ocelot *ocelot, int port, struct ifreq *ifr)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct hwtstamp_config cfg;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* reserved for future extensions */
	if (cfg.flags)
		return -EINVAL;

	/* Tx type sanity check */
	switch (cfg.tx_type) {
	case HWTSTAMP_TX_ON:
		ocelot_port->ptp_cmd = IFH_REW_OP_TWO_STEP_PTP;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		/* IFH_REW_OP_ONE_STEP_PTP updates the correctional field, we
		 * need to update the origin time.
		 */
		ocelot_port->ptp_cmd = IFH_REW_OP_ORIGIN_PTP;
		break;
	case HWTSTAMP_TX_OFF:
		ocelot_port->ptp_cmd = 0;
		break;
	default:
		return -ERANGE;
	}

	mutex_lock(&ocelot->ptp_lock);

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	default:
		mutex_unlock(&ocelot->ptp_lock);
		return -ERANGE;
	}

	/* Commit back the result & save it */
	memcpy(&ocelot->hwtstamp_config, &cfg, sizeof(cfg));
	mutex_unlock(&ocelot->ptp_lock);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}
EXPORT_SYMBOL(ocelot_hwstamp_set);

static int ocelot_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	/* The function is only used for PTP operations for now */
	if (!ocelot->ptp)
		return -EOPNOTSUPP;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return ocelot_hwstamp_set(ocelot, port, ifr);
	case SIOCGHWTSTAMP:
		return ocelot_hwstamp_get(ocelot, port, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops ocelot_port_netdev_ops = {
	.ndo_open			= ocelot_port_open,
	.ndo_stop			= ocelot_port_stop,
	.ndo_start_xmit			= ocelot_port_xmit,
	.ndo_set_rx_mode		= ocelot_set_rx_mode,
	.ndo_get_phys_port_name		= ocelot_port_get_phys_port_name,
	.ndo_set_mac_address		= ocelot_port_set_mac_address,
	.ndo_get_stats64		= ocelot_get_stats64,
	.ndo_fdb_add			= ocelot_port_fdb_add,
	.ndo_fdb_del			= ocelot_port_fdb_del,
	.ndo_fdb_dump			= ocelot_port_fdb_dump,
	.ndo_vlan_rx_add_vid		= ocelot_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid		= ocelot_vlan_rx_kill_vid,
	.ndo_set_features		= ocelot_set_features,
	.ndo_get_port_parent_id		= ocelot_get_port_parent_id,
	.ndo_setup_tc			= ocelot_setup_tc,
	.ndo_do_ioctl			= ocelot_ioctl,
};

void ocelot_get_strings(struct ocelot *ocelot, int port, u32 sset, u8 *data)
{
	int i;

	if (sset != ETH_SS_STATS)
		return;

	for (i = 0; i < ocelot->num_stats; i++)
		memcpy(data + i * ETH_GSTRING_LEN, ocelot->stats_layout[i].name,
		       ETH_GSTRING_LEN);
}
EXPORT_SYMBOL(ocelot_get_strings);

static void ocelot_port_get_strings(struct net_device *netdev, u32 sset,
				    u8 *data)
{
	struct ocelot_port_private *priv = netdev_priv(netdev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	ocelot_get_strings(ocelot, port, sset, data);
}

static void ocelot_update_stats(struct ocelot *ocelot)
{
	int i, j;

	mutex_lock(&ocelot->stats_lock);

	for (i = 0; i < ocelot->num_phys_ports; i++) {
		/* Configure the port to read the stats from */
		ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(i), SYS_STAT_CFG);

		for (j = 0; j < ocelot->num_stats; j++) {
			u32 val;
			unsigned int idx = i * ocelot->num_stats + j;

			val = ocelot_read_rix(ocelot, SYS_COUNT_RX_OCTETS,
					      ocelot->stats_layout[j].offset);

			if (val < (ocelot->stats[idx] & U32_MAX))
				ocelot->stats[idx] += (u64)1 << 32;

			ocelot->stats[idx] = (ocelot->stats[idx] &
					      ~(u64)U32_MAX) + val;
		}
	}

	mutex_unlock(&ocelot->stats_lock);
}

static void ocelot_check_stats_work(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct ocelot *ocelot = container_of(del_work, struct ocelot,
					     stats_work);

	ocelot_update_stats(ocelot);

	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);
}

void ocelot_get_ethtool_stats(struct ocelot *ocelot, int port, u64 *data)
{
	int i;

	/* check and update now */
	ocelot_update_stats(ocelot);

	/* Copy all counters */
	for (i = 0; i < ocelot->num_stats; i++)
		*data++ = ocelot->stats[port * ocelot->num_stats + i];
}
EXPORT_SYMBOL(ocelot_get_ethtool_stats);

static void ocelot_port_get_ethtool_stats(struct net_device *dev,
					  struct ethtool_stats *stats,
					  u64 *data)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	ocelot_get_ethtool_stats(ocelot, port, data);
}

int ocelot_get_sset_count(struct ocelot *ocelot, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return ocelot->num_stats;
}
EXPORT_SYMBOL(ocelot_get_sset_count);

static int ocelot_port_get_sset_count(struct net_device *dev, int sset)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	return ocelot_get_sset_count(ocelot, port, sset);
}

int ocelot_get_ts_info(struct ocelot *ocelot, int port,
		       struct ethtool_ts_info *info)
{
	info->phc_index = ocelot->ptp_clock ?
			  ptp_clock_index(ocelot->ptp_clock) : -1;
	info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE |
				 SOF_TIMESTAMPING_RX_SOFTWARE |
				 SOF_TIMESTAMPING_SOFTWARE |
				 SOF_TIMESTAMPING_TX_HARDWARE |
				 SOF_TIMESTAMPING_RX_HARDWARE |
				 SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON) |
			 BIT(HWTSTAMP_TX_ONESTEP_SYNC);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) | BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}
EXPORT_SYMBOL(ocelot_get_ts_info);

static int ocelot_port_get_ts_info(struct net_device *dev,
				   struct ethtool_ts_info *info)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;

	if (!ocelot->ptp)
		return ethtool_op_get_ts_info(dev, info);

	return ocelot_get_ts_info(ocelot, port, info);
}

static const struct ethtool_ops ocelot_ethtool_ops = {
	.get_strings		= ocelot_port_get_strings,
	.get_ethtool_stats	= ocelot_port_get_ethtool_stats,
	.get_sset_count		= ocelot_port_get_sset_count,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_ts_info		= ocelot_port_get_ts_info,
};

void ocelot_bridge_stp_state_set(struct ocelot *ocelot, int port, u8 state)
{
	u32 port_cfg;
	int p, i;

	if (!(BIT(port) & ocelot->bridge_mask))
		return;

	port_cfg = ocelot_read_gix(ocelot, ANA_PORT_PORT_CFG, port);

	switch (state) {
	case BR_STATE_FORWARDING:
		ocelot->bridge_fwd_mask |= BIT(port);
		/* Fallthrough */
	case BR_STATE_LEARNING:
		port_cfg |= ANA_PORT_PORT_CFG_LEARN_ENA;
		break;

	default:
		port_cfg &= ~ANA_PORT_PORT_CFG_LEARN_ENA;
		ocelot->bridge_fwd_mask &= ~BIT(port);
		break;
	}

	ocelot_write_gix(ocelot, port_cfg, ANA_PORT_PORT_CFG, port);

	/* Apply FWD mask. The loop is needed to add/remove the current port as
	 * a source for the other ports.
	 */
	for (p = 0; p < ocelot->num_phys_ports; p++) {
		if (p == ocelot->cpu || (ocelot->bridge_fwd_mask & BIT(p))) {
			unsigned long mask = ocelot->bridge_fwd_mask & ~BIT(p);

			for (i = 0; i < ocelot->num_phys_ports; i++) {
				unsigned long bond_mask = ocelot->lags[i];

				if (!bond_mask)
					continue;

				if (bond_mask & BIT(p)) {
					mask &= ~bond_mask;
					break;
				}
			}

			/* Avoid the NPI port from looping back to itself */
			if (p != ocelot->cpu)
				mask |= BIT(ocelot->cpu);

			ocelot_write_rix(ocelot, mask,
					 ANA_PGID_PGID, PGID_SRC + p);
		} else {
			/* Only the CPU port, this is compatible with link
			 * aggregation.
			 */
			ocelot_write_rix(ocelot,
					 BIT(ocelot->cpu),
					 ANA_PGID_PGID, PGID_SRC + p);
		}
	}
}
EXPORT_SYMBOL(ocelot_bridge_stp_state_set);

static void ocelot_port_attr_stp_state_set(struct ocelot *ocelot, int port,
					   struct switchdev_trans *trans,
					   u8 state)
{
	if (switchdev_trans_ph_prepare(trans))
		return;

	ocelot_bridge_stp_state_set(ocelot, port, state);
}

void ocelot_set_ageing_time(struct ocelot *ocelot, unsigned int msecs)
{
	ocelot_write(ocelot, ANA_AUTOAGE_AGE_PERIOD(msecs / 2),
		     ANA_AUTOAGE);
}
EXPORT_SYMBOL(ocelot_set_ageing_time);

static void ocelot_port_attr_ageing_set(struct ocelot *ocelot, int port,
					unsigned long ageing_clock_t)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies) / 1000;

	ocelot_set_ageing_time(ocelot, ageing_time);
}

static void ocelot_port_attr_mc_set(struct ocelot *ocelot, int port, bool mc)
{
	u32 cpu_fwd_mcast = ANA_PORT_CPU_FWD_CFG_CPU_IGMP_REDIR_ENA |
			    ANA_PORT_CPU_FWD_CFG_CPU_MLD_REDIR_ENA |
			    ANA_PORT_CPU_FWD_CFG_CPU_IPMC_CTRL_COPY_ENA;
	u32 val = 0;

	if (mc)
		val = cpu_fwd_mcast;

	ocelot_rmw_gix(ocelot, val, cpu_fwd_mcast,
		       ANA_PORT_CPU_FWD_CFG, port);
}

static int ocelot_port_attr_set(struct net_device *dev,
				const struct switchdev_attr *attr,
				struct switchdev_trans *trans)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot *ocelot = priv->port.ocelot;
	int port = priv->chip_port;
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ocelot_port_attr_stp_state_set(ocelot, port, trans,
					       attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		ocelot_port_attr_ageing_set(ocelot, port, attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		priv->vlan_aware = attr->u.vlan_filtering;
		ocelot_port_vlan_filtering(ocelot, port, priv->vlan_aware);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		ocelot_port_attr_mc_set(ocelot, port, !attr->u.mc_disabled);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int ocelot_port_obj_add_vlan(struct net_device *dev,
				    const struct switchdev_obj_port_vlan *vlan,
				    struct switchdev_trans *trans)
{
	int ret;
	u16 vid;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		ret = ocelot_vlan_vid_add(dev, vid,
					  vlan->flags & BRIDGE_VLAN_INFO_PVID,
					  vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
		if (ret)
			return ret;
	}

	return 0;
}

static int ocelot_port_vlan_del_vlan(struct net_device *dev,
				     const struct switchdev_obj_port_vlan *vlan)
{
	int ret;
	u16 vid;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		ret = ocelot_vlan_vid_del(dev, vid);

		if (ret)
			return ret;
	}

	return 0;
}

static struct ocelot_multicast *ocelot_multicast_get(struct ocelot *ocelot,
						     const unsigned char *addr,
						     u16 vid)
{
	struct ocelot_multicast *mc;

	list_for_each_entry(mc, &ocelot->multicast, list) {
		if (ether_addr_equal(mc->addr, addr) && mc->vid == vid)
			return mc;
	}

	return NULL;
}

static int ocelot_port_obj_add_mdb(struct net_device *dev,
				   const struct switchdev_obj_port_mdb *mdb,
				   struct switchdev_trans *trans)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	int port = priv->chip_port;
	u16 vid = mdb->vid;
	bool new = false;

	if (!vid)
		vid = ocelot_port->pvid;

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc) {
		mc = devm_kzalloc(ocelot->dev, sizeof(*mc), GFP_KERNEL);
		if (!mc)
			return -ENOMEM;

		memcpy(mc->addr, mdb->addr, ETH_ALEN);
		mc->vid = vid;

		list_add_tail(&mc->list, &ocelot->multicast);
		new = true;
	}

	memcpy(addr, mc->addr, ETH_ALEN);
	addr[0] = 0;

	if (!new) {
		addr[2] = mc->ports << 0;
		addr[1] = mc->ports << 8;
		ocelot_mact_forget(ocelot, addr, vid);
	}

	mc->ports |= BIT(port);
	addr[2] = mc->ports << 0;
	addr[1] = mc->ports << 8;

	return ocelot_mact_learn(ocelot, 0, addr, vid, ENTRYTYPE_MACv4);
}

static int ocelot_port_obj_del_mdb(struct net_device *dev,
				   const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	int port = priv->chip_port;
	u16 vid = mdb->vid;

	if (!vid)
		vid = ocelot_port->pvid;

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc)
		return -ENOENT;

	memcpy(addr, mc->addr, ETH_ALEN);
	addr[2] = mc->ports << 0;
	addr[1] = mc->ports << 8;
	addr[0] = 0;
	ocelot_mact_forget(ocelot, addr, vid);

	mc->ports &= ~BIT(port);
	if (!mc->ports) {
		list_del(&mc->list);
		devm_kfree(ocelot->dev, mc);
		return 0;
	}

	addr[2] = mc->ports << 0;
	addr[1] = mc->ports << 8;

	return ocelot_mact_learn(ocelot, 0, addr, vid, ENTRYTYPE_MACv4);
}

static int ocelot_port_obj_add(struct net_device *dev,
			       const struct switchdev_obj *obj,
			       struct switchdev_trans *trans,
			       struct netlink_ext_ack *extack)
{
	int ret = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		ret = ocelot_port_obj_add_vlan(dev,
					       SWITCHDEV_OBJ_PORT_VLAN(obj),
					       trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		ret = ocelot_port_obj_add_mdb(dev, SWITCHDEV_OBJ_PORT_MDB(obj),
					      trans);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int ocelot_port_obj_del(struct net_device *dev,
			       const struct switchdev_obj *obj)
{
	int ret = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		ret = ocelot_port_vlan_del_vlan(dev,
						SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		ret = ocelot_port_obj_del_mdb(dev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

int ocelot_port_bridge_join(struct ocelot *ocelot, int port,
			    struct net_device *bridge)
{
	if (!ocelot->bridge_mask) {
		ocelot->hw_bridge_dev = bridge;
	} else {
		if (ocelot->hw_bridge_dev != bridge)
			/* This is adding the port to a second bridge, this is
			 * unsupported */
			return -ENODEV;
	}

	ocelot->bridge_mask |= BIT(port);

	return 0;
}
EXPORT_SYMBOL(ocelot_port_bridge_join);

int ocelot_port_bridge_leave(struct ocelot *ocelot, int port,
			     struct net_device *bridge)
{
	ocelot->bridge_mask &= ~BIT(port);

	if (!ocelot->bridge_mask)
		ocelot->hw_bridge_dev = NULL;

	ocelot_port_vlan_filtering(ocelot, port, 0);
	ocelot_port_set_pvid(ocelot, port, 0);
	return ocelot_port_set_native_vlan(ocelot, port, 0);
}
EXPORT_SYMBOL(ocelot_port_bridge_leave);

static void ocelot_set_aggr_pgids(struct ocelot *ocelot)
{
	int i, port, lag;

	/* Reset destination and aggregation PGIDS */
	for (port = 0; port < ocelot->num_phys_ports; port++)
		ocelot_write_rix(ocelot, BIT(port), ANA_PGID_PGID, port);

	for (i = PGID_AGGR; i < PGID_SRC; i++)
		ocelot_write_rix(ocelot, GENMASK(ocelot->num_phys_ports - 1, 0),
				 ANA_PGID_PGID, i);

	/* Now, set PGIDs for each LAG */
	for (lag = 0; lag < ocelot->num_phys_ports; lag++) {
		unsigned long bond_mask;
		int aggr_count = 0;
		u8 aggr_idx[16];

		bond_mask = ocelot->lags[lag];
		if (!bond_mask)
			continue;

		for_each_set_bit(port, &bond_mask, ocelot->num_phys_ports) {
			// Destination mask
			ocelot_write_rix(ocelot, bond_mask,
					 ANA_PGID_PGID, port);
			aggr_idx[aggr_count] = port;
			aggr_count++;
		}

		for (i = PGID_AGGR; i < PGID_SRC; i++) {
			u32 ac;

			ac = ocelot_read_rix(ocelot, ANA_PGID_PGID, i);
			ac &= ~bond_mask;
			ac |= BIT(aggr_idx[i % aggr_count]);
			ocelot_write_rix(ocelot, ac, ANA_PGID_PGID, i);
		}
	}
}

static void ocelot_setup_lag(struct ocelot *ocelot, int lag)
{
	unsigned long bond_mask = ocelot->lags[lag];
	unsigned int p;

	for_each_set_bit(p, &bond_mask, ocelot->num_phys_ports) {
		u32 port_cfg = ocelot_read_gix(ocelot, ANA_PORT_PORT_CFG, p);

		port_cfg &= ~ANA_PORT_PORT_CFG_PORTID_VAL_M;

		/* Use lag port as logical port for port i */
		ocelot_write_gix(ocelot, port_cfg |
				 ANA_PORT_PORT_CFG_PORTID_VAL(lag),
				 ANA_PORT_PORT_CFG, p);
	}
}

static int ocelot_port_lag_join(struct ocelot *ocelot, int port,
				struct net_device *bond)
{
	struct net_device *ndev;
	u32 bond_mask = 0;
	int lag, lp;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(bond, ndev) {
		struct ocelot_port_private *priv = netdev_priv(ndev);

		bond_mask |= BIT(priv->chip_port);
	}
	rcu_read_unlock();

	lp = __ffs(bond_mask);

	/* If the new port is the lowest one, use it as the logical port from
	 * now on
	 */
	if (port == lp) {
		lag = port;
		ocelot->lags[port] = bond_mask;
		bond_mask &= ~BIT(port);
		if (bond_mask) {
			lp = __ffs(bond_mask);
			ocelot->lags[lp] = 0;
		}
	} else {
		lag = lp;
		ocelot->lags[lp] |= BIT(port);
	}

	ocelot_setup_lag(ocelot, lag);
	ocelot_set_aggr_pgids(ocelot);

	return 0;
}

static void ocelot_port_lag_leave(struct ocelot *ocelot, int port,
				  struct net_device *bond)
{
	u32 port_cfg;
	int i;

	/* Remove port from any lag */
	for (i = 0; i < ocelot->num_phys_ports; i++)
		ocelot->lags[i] &= ~BIT(port);

	/* if it was the logical port of the lag, move the lag config to the
	 * next port
	 */
	if (ocelot->lags[port]) {
		int n = __ffs(ocelot->lags[port]);

		ocelot->lags[n] = ocelot->lags[port];
		ocelot->lags[port] = 0;

		ocelot_setup_lag(ocelot, n);
	}

	port_cfg = ocelot_read_gix(ocelot, ANA_PORT_PORT_CFG, port);
	port_cfg &= ~ANA_PORT_PORT_CFG_PORTID_VAL_M;
	ocelot_write_gix(ocelot, port_cfg | ANA_PORT_PORT_CFG_PORTID_VAL(port),
			 ANA_PORT_PORT_CFG, port);

	ocelot_set_aggr_pgids(ocelot);
}

/* Checks if the net_device instance given to us originate from our driver. */
static bool ocelot_netdevice_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &ocelot_port_netdev_ops;
}

static int ocelot_netdevice_port_event(struct net_device *dev,
				       unsigned long event,
				       struct netdev_notifier_changeupper_info *info)
{
	struct ocelot_port_private *priv = netdev_priv(dev);
	struct ocelot_port *ocelot_port = &priv->port;
	struct ocelot *ocelot = ocelot_port->ocelot;
	int port = priv->chip_port;
	int err = 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		if (netif_is_bridge_master(info->upper_dev)) {
			if (info->linking) {
				err = ocelot_port_bridge_join(ocelot, port,
							      info->upper_dev);
			} else {
				err = ocelot_port_bridge_leave(ocelot, port,
							       info->upper_dev);
				priv->vlan_aware = false;
			}
		}
		if (netif_is_lag_master(info->upper_dev)) {
			if (info->linking)
				err = ocelot_port_lag_join(ocelot, port,
							   info->upper_dev);
			else
				ocelot_port_lag_leave(ocelot, port,
						      info->upper_dev);
		}
		break;
	default:
		break;
	}

	return err;
}

static int ocelot_netdevice_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int ret = 0;

	if (!ocelot_netdevice_dev_check(dev))
		return 0;

	if (event == NETDEV_PRECHANGEUPPER &&
	    netif_is_lag_master(info->upper_dev)) {
		struct netdev_lag_upper_info *lag_upper_info = info->upper_info;
		struct netlink_ext_ack *extack;

		if (lag_upper_info &&
		    lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
			extack = netdev_notifier_info_to_extack(&info->info);
			NL_SET_ERR_MSG_MOD(extack, "LAG device using unsupported Tx type");

			ret = -EINVAL;
			goto notify;
		}
	}

	if (netif_is_lag_master(dev)) {
		struct net_device *slave;
		struct list_head *iter;

		netdev_for_each_lower_dev(dev, slave, iter) {
			ret = ocelot_netdevice_port_event(slave, event, info);
			if (ret)
				goto notify;
		}
	} else {
		ret = ocelot_netdevice_port_event(dev, event, info);
	}

notify:
	return notifier_from_errno(ret);
}

struct notifier_block ocelot_netdevice_nb __read_mostly = {
	.notifier_call = ocelot_netdevice_event,
};
EXPORT_SYMBOL(ocelot_netdevice_nb);

static int ocelot_switchdev_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     ocelot_netdevice_dev_check,
						     ocelot_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

struct notifier_block ocelot_switchdev_nb __read_mostly = {
	.notifier_call = ocelot_switchdev_event,
};
EXPORT_SYMBOL(ocelot_switchdev_nb);

static int ocelot_switchdev_blocking_event(struct notifier_block *unused,
					   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
		/* Blocking events. */
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add(dev, ptr,
						    ocelot_netdevice_dev_check,
						    ocelot_port_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del(dev, ptr,
						    ocelot_netdevice_dev_check,
						    ocelot_port_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     ocelot_netdevice_dev_check,
						     ocelot_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

struct notifier_block ocelot_switchdev_blocking_nb __read_mostly = {
	.notifier_call = ocelot_switchdev_blocking_event,
};
EXPORT_SYMBOL(ocelot_switchdev_blocking_nb);

int ocelot_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	unsigned long flags;
	time64_t s;
	u32 val;
	s64 ns;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_SAVE);
	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	s = ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_MSB, TOD_ACC_PIN) & 0xffff;
	s <<= 32;
	s += ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);
	ns = ocelot_read_rix(ocelot, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);

	/* Deal with negative values */
	if (ns >= 0x3ffffff0 && ns <= 0x3fffffff) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_gettime64);

static int ocelot_ptp_settime64(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);

	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	ocelot_write_rix(ocelot, lower_32_bits(ts->tv_sec), PTP_PIN_TOD_SEC_LSB,
			 TOD_ACC_PIN);
	ocelot_write_rix(ocelot, upper_32_bits(ts->tv_sec), PTP_PIN_TOD_SEC_MSB,
			 TOD_ACC_PIN);
	ocelot_write_rix(ocelot, ts->tv_nsec, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_LOAD);

	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;
}

static int ocelot_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
		unsigned long flags;
		u32 val;

		spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

		val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
		val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
		val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);

		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

		ocelot_write_rix(ocelot, 0, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);
		ocelot_write_rix(ocelot, 0, PTP_PIN_TOD_SEC_MSB, TOD_ACC_PIN);
		ocelot_write_rix(ocelot, delta, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

		val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
		val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
		val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_DELTA);

		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

		spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	} else {
		/* Fall back using ocelot_ptp_settime64 which is not exact. */
		struct timespec64 ts;
		u64 now;

		ocelot_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		ocelot_ptp_settime64(ptp, &ts);
	}
	return 0;
}

static int ocelot_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	u32 unit = 0, direction = 0;
	unsigned long flags;
	u64 adj = 0;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	if (!scaled_ppm)
		goto disable_adj;

	if (scaled_ppm < 0) {
		direction = PTP_CFG_CLK_ADJ_CFG_DIR;
		scaled_ppm = -scaled_ppm;
	}

	adj = PSEC_PER_SEC << 16;
	do_div(adj, scaled_ppm);
	do_div(adj, 1000);

	/* If the adjustment value is too large, use ns instead */
	if (adj >= (1L << 30)) {
		unit = PTP_CFG_CLK_ADJ_FREQ_NS;
		do_div(adj, 1000);
	}

	/* Still too big */
	if (adj >= (1L << 30))
		goto disable_adj;

	ocelot_write(ocelot, unit | adj, PTP_CLK_CFG_ADJ_FREQ);
	ocelot_write(ocelot, PTP_CFG_CLK_ADJ_CFG_ENA | direction,
		     PTP_CLK_CFG_ADJ_CFG);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;

disable_adj:
	ocelot_write(ocelot, 0, PTP_CLK_CFG_ADJ_CFG);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;
}

static struct ptp_clock_info ocelot_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "ocelot ptp",
	.max_adj	= 0x7fffffff,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.gettime64	= ocelot_ptp_gettime64,
	.settime64	= ocelot_ptp_settime64,
	.adjtime	= ocelot_ptp_adjtime,
	.adjfine	= ocelot_ptp_adjfine,
};

static int ocelot_init_timestamp(struct ocelot *ocelot)
{
	ocelot->ptp_info = ocelot_ptp_clock_info;
	ocelot->ptp_clock = ptp_clock_register(&ocelot->ptp_info, ocelot->dev);
	if (IS_ERR(ocelot->ptp_clock))
		return PTR_ERR(ocelot->ptp_clock);
	/* Check if PHC support is missing at the configuration level */
	if (!ocelot->ptp_clock)
		return 0;

	ocelot_write(ocelot, SYS_PTP_CFG_PTP_STAMP_WID(30), SYS_PTP_CFG);
	ocelot_write(ocelot, 0xffffffff, ANA_TABLES_PTP_ID_LOW);
	ocelot_write(ocelot, 0xffffffff, ANA_TABLES_PTP_ID_HIGH);

	ocelot_write(ocelot, PTP_CFG_MISC_PTP_EN, PTP_CFG_MISC);

	/* There is no device reconfiguration, PTP Rx stamping is always
	 * enabled.
	 */
	ocelot->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;

	return 0;
}

static void ocelot_port_set_mtu(struct ocelot *ocelot, int port, size_t mtu)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int atop_wm;

	ocelot_port_writel(ocelot_port, mtu, DEV_MAC_MAXLEN_CFG);

	/* Set Pause WM hysteresis
	 * 152 = 6 * mtu / OCELOT_BUFFER_CELL_SZ
	 * 101 = 4 * mtu / OCELOT_BUFFER_CELL_SZ
	 */
	ocelot_write_rix(ocelot, SYS_PAUSE_CFG_PAUSE_ENA |
			 SYS_PAUSE_CFG_PAUSE_STOP(101) |
			 SYS_PAUSE_CFG_PAUSE_START(152), SYS_PAUSE_CFG, port);

	/* Tail dropping watermark */
	atop_wm = (ocelot->shared_queue_sz - 9 * mtu) / OCELOT_BUFFER_CELL_SZ;
	ocelot_write_rix(ocelot, ocelot_wm_enc(9 * mtu),
			 SYS_ATOP, port);
	ocelot_write(ocelot, ocelot_wm_enc(atop_wm), SYS_ATOP_TOT_CFG);
}

void ocelot_init_port(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	INIT_LIST_HEAD(&ocelot_port->skbs);

	/* Basic L2 initialization */

	/* Set MAC IFG Gaps
	 * FDX: TX_IFG = 5, RX_IFG1 = RX_IFG2 = 0
	 * !FDX: TX_IFG = 5, RX_IFG1 = RX_IFG2 = 5
	 */
	ocelot_port_writel(ocelot_port, DEV_MAC_IFG_CFG_TX_IFG(5),
			   DEV_MAC_IFG_CFG);

	/* Load seed (0) and set MAC HDX late collision  */
	ocelot_port_writel(ocelot_port, DEV_MAC_HDX_CFG_LATE_COL_POS(67) |
			   DEV_MAC_HDX_CFG_SEED_LOAD,
			   DEV_MAC_HDX_CFG);
	mdelay(1);
	ocelot_port_writel(ocelot_port, DEV_MAC_HDX_CFG_LATE_COL_POS(67),
			   DEV_MAC_HDX_CFG);

	/* Set Max Length and maximum tags allowed */
	ocelot_port_set_mtu(ocelot, port, VLAN_ETH_FRAME_LEN);
	ocelot_port_writel(ocelot_port, DEV_MAC_TAGS_CFG_TAG_ID(ETH_P_8021AD) |
			   DEV_MAC_TAGS_CFG_VLAN_AWR_ENA |
			   DEV_MAC_TAGS_CFG_VLAN_LEN_AWR_ENA,
			   DEV_MAC_TAGS_CFG);

	/* Set SMAC of Pause frame (00:00:00:00:00:00) */
	ocelot_port_writel(ocelot_port, 0, DEV_MAC_FC_MAC_HIGH_CFG);
	ocelot_port_writel(ocelot_port, 0, DEV_MAC_FC_MAC_LOW_CFG);

	/* Drop frames with multicast source address */
	ocelot_rmw_gix(ocelot, ANA_PORT_DROP_CFG_DROP_MC_SMAC_ENA,
		       ANA_PORT_DROP_CFG_DROP_MC_SMAC_ENA,
		       ANA_PORT_DROP_CFG, port);

	/* Set default VLAN and tag type to 8021Q. */
	ocelot_rmw_gix(ocelot, REW_PORT_VLAN_CFG_PORT_TPID(ETH_P_8021Q),
		       REW_PORT_VLAN_CFG_PORT_TPID_M,
		       REW_PORT_VLAN_CFG, port);

	/* Enable vcap lookups */
	ocelot_vcap_enable(ocelot, port);
}
EXPORT_SYMBOL(ocelot_init_port);

int ocelot_probe_port(struct ocelot *ocelot, u8 port,
		      void __iomem *regs,
		      struct phy_device *phy)
{
	struct ocelot_port_private *priv;
	struct ocelot_port *ocelot_port;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(struct ocelot_port_private));
	if (!dev)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, ocelot->dev);
	priv = netdev_priv(dev);
	priv->dev = dev;
	priv->phy = phy;
	priv->chip_port = port;
	ocelot_port = &priv->port;
	ocelot_port->ocelot = ocelot;
	ocelot_port->regs = regs;
	ocelot->ports[port] = ocelot_port;

	dev->netdev_ops = &ocelot_port_netdev_ops;
	dev->ethtool_ops = &ocelot_ethtool_ops;

	dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_RXFCS |
		NETIF_F_HW_TC;
	dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_TC;

	memcpy(dev->dev_addr, ocelot->base_mac, ETH_ALEN);
	dev->dev_addr[ETH_ALEN - 1] += port;
	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr, ocelot_port->pvid,
			  ENTRYTYPE_LOCKED);

	ocelot_init_port(ocelot, port);

	err = register_netdev(dev);
	if (err) {
		dev_err(ocelot->dev, "register_netdev failed\n");
		free_netdev(dev);
	}

	return err;
}
EXPORT_SYMBOL(ocelot_probe_port);

void ocelot_set_cpu_port(struct ocelot *ocelot, int cpu,
			 enum ocelot_tag_prefix injection,
			 enum ocelot_tag_prefix extraction)
{
	/* Configure and enable the CPU port. */
	ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, cpu);
	ocelot_write_rix(ocelot, BIT(cpu), ANA_PGID_PGID, PGID_CPU);
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(cpu),
			 ANA_PORT_PORT_CFG, cpu);

	/* If the CPU port is a physical port, set up the port in Node
	 * Processor Interface (NPI) mode. This is the mode through which
	 * frames can be injected from and extracted to an external CPU.
	 * Only one port can be an NPI at the same time.
	 */
	if (cpu < ocelot->num_phys_ports) {
		int mtu = VLAN_ETH_FRAME_LEN + OCELOT_TAG_LEN;

		ocelot_write(ocelot, QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_M |
			     QSYS_EXT_CPU_CFG_EXT_CPU_PORT(cpu),
			     QSYS_EXT_CPU_CFG);

		if (injection == OCELOT_TAG_PREFIX_SHORT)
			mtu += OCELOT_SHORT_PREFIX_LEN;
		else if (injection == OCELOT_TAG_PREFIX_LONG)
			mtu += OCELOT_LONG_PREFIX_LEN;

		ocelot_port_set_mtu(ocelot, cpu, mtu);
	}

	/* CPU port Injection/Extraction configuration */
	ocelot_write_rix(ocelot, QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE |
			 QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG(1) |
			 QSYS_SWITCH_PORT_MODE_PORT_ENA,
			 QSYS_SWITCH_PORT_MODE, cpu);
	ocelot_write_rix(ocelot, SYS_PORT_MODE_INCL_XTR_HDR(extraction) |
			 SYS_PORT_MODE_INCL_INJ_HDR(injection),
			 SYS_PORT_MODE, cpu);

	/* Configure the CPU port to be VLAN aware */
	ocelot_write_gix(ocelot, ANA_PORT_VLAN_CFG_VLAN_VID(0) |
				 ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
				 ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1),
			 ANA_PORT_VLAN_CFG, cpu);

	ocelot->cpu = cpu;
}
EXPORT_SYMBOL(ocelot_set_cpu_port);

int ocelot_init(struct ocelot *ocelot)
{
	char queue_name[32];
	int i, ret;
	u32 port;

	if (ocelot->ops->reset) {
		ret = ocelot->ops->reset(ocelot);
		if (ret) {
			dev_err(ocelot->dev, "Switch reset failed\n");
			return ret;
		}
	}

	ocelot->lags = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				    sizeof(u32), GFP_KERNEL);
	if (!ocelot->lags)
		return -ENOMEM;

	ocelot->stats = devm_kcalloc(ocelot->dev,
				     ocelot->num_phys_ports * ocelot->num_stats,
				     sizeof(u64), GFP_KERNEL);
	if (!ocelot->stats)
		return -ENOMEM;

	mutex_init(&ocelot->stats_lock);
	mutex_init(&ocelot->ptp_lock);
	spin_lock_init(&ocelot->ptp_clock_lock);
	snprintf(queue_name, sizeof(queue_name), "%s-stats",
		 dev_name(ocelot->dev));
	ocelot->stats_queue = create_singlethread_workqueue(queue_name);
	if (!ocelot->stats_queue)
		return -ENOMEM;

	INIT_LIST_HEAD(&ocelot->multicast);
	ocelot_mact_init(ocelot);
	ocelot_vlan_init(ocelot);
	ocelot_ace_init(ocelot);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		/* Clear all counters (5 groups) */
		ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(port) |
				     SYS_STAT_CFG_STAT_CLEAR_SHOT(0x7f),
			     SYS_STAT_CFG);
	}

	/* Only use S-Tag */
	ocelot_write(ocelot, ETH_P_8021AD, SYS_VLAN_ETYPE_CFG);

	/* Aggregation mode */
	ocelot_write(ocelot, ANA_AGGR_CFG_AC_SMAC_ENA |
			     ANA_AGGR_CFG_AC_DMAC_ENA |
			     ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA |
			     ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA, ANA_AGGR_CFG);

	/* Set MAC age time to default value. The entry is aged after
	 * 2*AGE_PERIOD
	 */
	ocelot_write(ocelot,
		     ANA_AUTOAGE_AGE_PERIOD(BR_DEFAULT_AGEING_TIME / 2 / HZ),
		     ANA_AUTOAGE);

	/* Disable learning for frames discarded by VLAN ingress filtering */
	regmap_field_write(ocelot->regfields[ANA_ADVLEARN_VLAN_CHK], 1);

	/* Setup frame ageing - fixed value "2 sec" - in 6.5 us units */
	ocelot_write(ocelot, SYS_FRM_AGING_AGE_TX_ENA |
		     SYS_FRM_AGING_MAX_AGE(307692), SYS_FRM_AGING);

	/* Setup flooding PGIDs */
	ocelot_write_rix(ocelot, ANA_FLOODING_FLD_MULTICAST(PGID_MC) |
			 ANA_FLOODING_FLD_BROADCAST(PGID_MC) |
			 ANA_FLOODING_FLD_UNICAST(PGID_UC),
			 ANA_FLOODING, 0);
	ocelot_write(ocelot, ANA_FLOODING_IPMC_FLD_MC6_DATA(PGID_MCIPV6) |
		     ANA_FLOODING_IPMC_FLD_MC6_CTRL(PGID_MC) |
		     ANA_FLOODING_IPMC_FLD_MC4_DATA(PGID_MCIPV4) |
		     ANA_FLOODING_IPMC_FLD_MC4_CTRL(PGID_MC),
		     ANA_FLOODING_IPMC);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		/* Transmit the frame to the local port. */
		ocelot_write_rix(ocelot, BIT(port), ANA_PGID_PGID, port);
		/* Do not forward BPDU frames to the front ports. */
		ocelot_write_gix(ocelot,
				 ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_REDIR_ENA(0xffff),
				 ANA_PORT_CPU_FWD_BPDU_CFG,
				 port);
		/* Ensure bridging is disabled */
		ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, PGID_SRC + port);
	}

	/* Allow broadcast MAC frames. */
	for (i = ocelot->num_phys_ports + 1; i < PGID_CPU; i++) {
		u32 val = ANA_PGID_PGID_PGID(GENMASK(ocelot->num_phys_ports - 1, 0));

		ocelot_write_rix(ocelot, val, ANA_PGID_PGID, i);
	}
	ocelot_write_rix(ocelot,
			 ANA_PGID_PGID_PGID(GENMASK(ocelot->num_phys_ports, 0)),
			 ANA_PGID_PGID, PGID_MC);
	ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, PGID_MCIPV4);
	ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, PGID_MCIPV6);

	/* Allow manual injection via DEVCPU_QS registers, and byte swap these
	 * registers endianness.
	 */
	ocelot_write_rix(ocelot, QS_INJ_GRP_CFG_BYTE_SWAP |
			 QS_INJ_GRP_CFG_MODE(1), QS_INJ_GRP_CFG, 0);
	ocelot_write_rix(ocelot, QS_XTR_GRP_CFG_BYTE_SWAP |
			 QS_XTR_GRP_CFG_MODE(1), QS_XTR_GRP_CFG, 0);
	ocelot_write(ocelot, ANA_CPUQ_CFG_CPUQ_MIRROR(2) |
		     ANA_CPUQ_CFG_CPUQ_LRN(2) |
		     ANA_CPUQ_CFG_CPUQ_MAC_COPY(2) |
		     ANA_CPUQ_CFG_CPUQ_SRC_COPY(2) |
		     ANA_CPUQ_CFG_CPUQ_LOCKED_PORTMOVE(2) |
		     ANA_CPUQ_CFG_CPUQ_ALLBRIDGE(6) |
		     ANA_CPUQ_CFG_CPUQ_IPMC_CTRL(6) |
		     ANA_CPUQ_CFG_CPUQ_IGMP(6) |
		     ANA_CPUQ_CFG_CPUQ_MLD(6), ANA_CPUQ_CFG);
	for (i = 0; i < 16; i++)
		ocelot_write_rix(ocelot, ANA_CPUQ_8021_CFG_CPUQ_GARP_VAL(6) |
				 ANA_CPUQ_8021_CFG_CPUQ_BPDU_VAL(6),
				 ANA_CPUQ_8021_CFG, i);

	INIT_DELAYED_WORK(&ocelot->stats_work, ocelot_check_stats_work);
	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);

	if (ocelot->ptp) {
		ret = ocelot_init_timestamp(ocelot);
		if (ret) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_init);

void ocelot_deinit(struct ocelot *ocelot)
{
	struct list_head *pos, *tmp;
	struct ocelot_port *port;
	struct ocelot_skb *entry;
	int i;

	cancel_delayed_work(&ocelot->stats_work);
	destroy_workqueue(ocelot->stats_queue);
	mutex_destroy(&ocelot->stats_lock);
	ocelot_ace_deinit();

	for (i = 0; i < ocelot->num_phys_ports; i++) {
		port = ocelot->ports[i];

		list_for_each_safe(pos, tmp, &port->skbs) {
			entry = list_entry(pos, struct ocelot_skb, head);

			list_del(pos);
			dev_kfree_skb_any(entry->skb);
			kfree(entry);
		}
	}
}
EXPORT_SYMBOL(ocelot_deinit);

MODULE_LICENSE("Dual MIT/GPL");
