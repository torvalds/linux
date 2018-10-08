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
#include <linux/skbuff.h>
#include <net/arp.h>
#include <net/netevent.h>
#include <net/rtnetlink.h>
#include <net/switchdev.h>

#include "ocelot.h"

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

static inline int ocelot_mact_wait_for_completion(struct ocelot *ocelot)
{
	unsigned int val, timeout = 10;

	/* Wait for the issued mac table command to be completed, or timeout.
	 * When the command read from  ANA_TABLES_MACACCESS is
	 * MACACCESS_CMD_IDLE, the issued command completed successfully.
	 */
	do {
		val = ocelot_read(ocelot, ANA_TABLES_MACACCESS);
		val &= ANA_TABLES_MACACCESS_MAC_TABLE_CMD_M;
	} while (val != MACACCESS_CMD_IDLE && timeout--);

	if (!timeout)
		return -ETIMEDOUT;

	return 0;
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

static inline int ocelot_vlant_wait_for_completion(struct ocelot *ocelot)
{
	unsigned int val, timeout = 10;

	/* Wait for the issued mac table command to be completed, or timeout.
	 * When the command read from ANA_TABLES_MACACCESS is
	 * MACACCESS_CMD_IDLE, the issued command completed successfully.
	 */
	do {
		val = ocelot_read(ocelot, ANA_TABLES_VLANACCESS);
		val &= ANA_TABLES_VLANACCESS_VLAN_TBL_CMD_M;
	} while (val != ANA_TABLES_VLANACCESS_CMD_IDLE && timeout--);

	if (!timeout)
		return -ETIMEDOUT;

	return 0;
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

static void ocelot_vlan_mode(struct ocelot_port *port,
			     netdev_features_t features)
{
	struct ocelot *ocelot = port->ocelot;
	u8 p = port->chip_port;
	u32 val;

	/* Filtering */
	val = ocelot_read(ocelot, ANA_VLANMASK);
	if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
		val |= BIT(p);
	else
		val &= ~BIT(p);
	ocelot_write(ocelot, val, ANA_VLANMASK);
}

static void ocelot_vlan_port_apply(struct ocelot *ocelot,
				   struct ocelot_port *port)
{
	u32 val;

	/* Ingress clasification (ANA_PORT_VLAN_CFG) */
	/* Default vlan to clasify for untagged frames (may be zero) */
	val = ANA_PORT_VLAN_CFG_VLAN_VID(port->pvid);
	if (port->vlan_aware)
		val |= ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
		       ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1);

	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_VLAN_CFG_VLAN_VID_M |
		       ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
		       ANA_PORT_VLAN_CFG_VLAN_POP_CNT_M,
		       ANA_PORT_VLAN_CFG, port->chip_port);

	/* Drop frames with multicast source address */
	val = ANA_PORT_DROP_CFG_DROP_MC_SMAC_ENA;
	if (port->vlan_aware && !port->vid)
		/* If port is vlan-aware and tagged, drop untagged and priority
		 * tagged frames.
		 */
		val |= ANA_PORT_DROP_CFG_DROP_UNTAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA;
	ocelot_write_gix(ocelot, val, ANA_PORT_DROP_CFG, port->chip_port);

	/* Egress configuration (REW_TAG_CFG): VLAN tag type to 8021Q. */
	val = REW_TAG_CFG_TAG_TPID_CFG(0);

	if (port->vlan_aware) {
		if (port->vid)
			/* Tag all frames except when VID == DEFAULT_VLAN */
			val |= REW_TAG_CFG_TAG_CFG(1);
		else
			/* Tag all frames */
			val |= REW_TAG_CFG_TAG_CFG(3);
	}
	ocelot_rmw_gix(ocelot, val,
		       REW_TAG_CFG_TAG_TPID_CFG_M |
		       REW_TAG_CFG_TAG_CFG_M,
		       REW_TAG_CFG, port->chip_port);

	/* Set default VLAN and tag type to 8021Q. */
	val = REW_PORT_VLAN_CFG_PORT_TPID(ETH_P_8021Q) |
	      REW_PORT_VLAN_CFG_PORT_VID(port->vid);
	ocelot_rmw_gix(ocelot, val,
		       REW_PORT_VLAN_CFG_PORT_TPID_M |
		       REW_PORT_VLAN_CFG_PORT_VID_M,
		       REW_PORT_VLAN_CFG, port->chip_port);
}

static int ocelot_vlan_vid_add(struct net_device *dev, u16 vid, bool pvid,
			       bool untagged)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	int ret;

	/* Add the port MAC address to with the right VLAN information */
	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr, vid,
			  ENTRYTYPE_LOCKED);

	/* Make the port a member of the VLAN */
	ocelot->vlan_mask[vid] |= BIT(port->chip_port);
	ret = ocelot_vlant_set_mask(ocelot, vid, ocelot->vlan_mask[vid]);
	if (ret)
		return ret;

	/* Default ingress vlan classification */
	if (pvid)
		port->pvid = vid;

	/* Untagged egress vlan clasification */
	if (untagged)
		port->vid = vid;

	ocelot_vlan_port_apply(ocelot, port);

	return 0;
}

static int ocelot_vlan_vid_del(struct net_device *dev, u16 vid)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	int ret;

	/* 8021q removes VID 0 on module unload for all interfaces
	 * with VLAN filtering feature. We need to keep it to receive
	 * untagged traffic.
	 */
	if (vid == 0)
		return 0;

	/* Del the port MAC address to with the right VLAN information */
	ocelot_mact_forget(ocelot, dev->dev_addr, vid);

	/* Stop the port from being a member of the vlan */
	ocelot->vlan_mask[vid] &= ~BIT(port->chip_port);
	ret = ocelot_vlant_set_mask(ocelot, vid, ocelot->vlan_mask[vid]);
	if (ret)
		return ret;

	/* Ingress */
	if (port->pvid == vid)
		port->pvid = 0;

	/* Egress */
	if (port->vid == vid)
		port->vid = 0;

	ocelot_vlan_port_apply(ocelot, port);

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

	/* Configure the CPU port to be VLAN aware */
	ocelot_write_gix(ocelot, ANA_PORT_VLAN_CFG_VLAN_VID(0) |
				 ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
				 ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1),
			 ANA_PORT_VLAN_CFG, ocelot->num_phys_ports);

	/* Set vlan ingress filter mask to all ports but the CPU port by
	 * default.
	 */
	ocelot_write(ocelot, GENMASK(9, 0), ANA_VLANMASK);

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

static void ocelot_port_adjust_link(struct net_device *dev)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	u8 p = port->chip_port;
	int speed, atop_wm, mode = 0;

	switch (dev->phydev->speed) {
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
		netdev_err(dev, "Unsupported PHY speed: %d\n",
			   dev->phydev->speed);
		return;
	}

	phy_print_status(dev->phydev);

	if (!dev->phydev->link)
		return;

	/* Only full duplex supported for now */
	ocelot_port_writel(port, DEV_MAC_MODE_CFG_FDX_ENA |
			   mode, DEV_MAC_MODE_CFG);

	/* Set MAC IFG Gaps
	 * FDX: TX_IFG = 5, RX_IFG1 = RX_IFG2 = 0
	 * !FDX: TX_IFG = 5, RX_IFG1 = RX_IFG2 = 5
	 */
	ocelot_port_writel(port, DEV_MAC_IFG_CFG_TX_IFG(5), DEV_MAC_IFG_CFG);

	/* Load seed (0) and set MAC HDX late collision  */
	ocelot_port_writel(port, DEV_MAC_HDX_CFG_LATE_COL_POS(67) |
			   DEV_MAC_HDX_CFG_SEED_LOAD,
			   DEV_MAC_HDX_CFG);
	mdelay(1);
	ocelot_port_writel(port, DEV_MAC_HDX_CFG_LATE_COL_POS(67),
			   DEV_MAC_HDX_CFG);

	/* Disable HDX fast control */
	ocelot_port_writel(port, DEV_PORT_MISC_HDX_FAST_DIS, DEV_PORT_MISC);

	/* SGMII only for now */
	ocelot_port_writel(port, PCS1G_MODE_CFG_SGMII_MODE_ENA, PCS1G_MODE_CFG);
	ocelot_port_writel(port, PCS1G_SD_CFG_SD_SEL, PCS1G_SD_CFG);

	/* Enable PCS */
	ocelot_port_writel(port, PCS1G_CFG_PCS_ENA, PCS1G_CFG);

	/* No aneg on SGMII */
	ocelot_port_writel(port, 0, PCS1G_ANEG_CFG);

	/* No loopback */
	ocelot_port_writel(port, 0, PCS1G_LB_CFG);

	/* Set Max Length and maximum tags allowed */
	ocelot_port_writel(port, VLAN_ETH_FRAME_LEN, DEV_MAC_MAXLEN_CFG);
	ocelot_port_writel(port, DEV_MAC_TAGS_CFG_TAG_ID(ETH_P_8021AD) |
			   DEV_MAC_TAGS_CFG_VLAN_AWR_ENA |
			   DEV_MAC_TAGS_CFG_VLAN_LEN_AWR_ENA,
			   DEV_MAC_TAGS_CFG);

	/* Enable MAC module */
	ocelot_port_writel(port, DEV_MAC_ENA_CFG_RX_ENA |
			   DEV_MAC_ENA_CFG_TX_ENA, DEV_MAC_ENA_CFG);

	/* Take MAC, Port, Phy (intern) and PCS (SGMII/Serdes) clock out of
	 * reset */
	ocelot_port_writel(port, DEV_CLOCK_CFG_LINK_SPEED(speed),
			   DEV_CLOCK_CFG);

	/* Set SMAC of Pause frame (00:00:00:00:00:00) */
	ocelot_port_writel(port, 0, DEV_MAC_FC_MAC_HIGH_CFG);
	ocelot_port_writel(port, 0, DEV_MAC_FC_MAC_LOW_CFG);

	/* No PFC */
	ocelot_write_gix(ocelot, ANA_PFC_PFC_CFG_FC_LINK_SPEED(speed),
			 ANA_PFC_PFC_CFG, p);

	/* Set Pause WM hysteresis
	 * 152 = 6 * VLAN_ETH_FRAME_LEN / OCELOT_BUFFER_CELL_SZ
	 * 101 = 4 * VLAN_ETH_FRAME_LEN / OCELOT_BUFFER_CELL_SZ
	 */
	ocelot_write_rix(ocelot, SYS_PAUSE_CFG_PAUSE_ENA |
			 SYS_PAUSE_CFG_PAUSE_STOP(101) |
			 SYS_PAUSE_CFG_PAUSE_START(152), SYS_PAUSE_CFG, p);

	/* Core: Enable port for frame transfer */
	ocelot_write_rix(ocelot, QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE |
			 QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG(1) |
			 QSYS_SWITCH_PORT_MODE_PORT_ENA,
			 QSYS_SWITCH_PORT_MODE, p);

	/* Flow control */
	ocelot_write_rix(ocelot, SYS_MAC_FC_CFG_PAUSE_VAL_CFG(0xffff) |
			 SYS_MAC_FC_CFG_RX_FC_ENA | SYS_MAC_FC_CFG_TX_FC_ENA |
			 SYS_MAC_FC_CFG_ZERO_PAUSE_ENA |
			 SYS_MAC_FC_CFG_FC_LATENCY_CFG(0x7) |
			 SYS_MAC_FC_CFG_FC_LINK_SPEED(speed),
			 SYS_MAC_FC_CFG, p);
	ocelot_write_rix(ocelot, 0, ANA_POL_FLOWC, p);

	/* Tail dropping watermark */
	atop_wm = (ocelot->shared_queue_sz - 9 * VLAN_ETH_FRAME_LEN) / OCELOT_BUFFER_CELL_SZ;
	ocelot_write_rix(ocelot, ocelot_wm_enc(9 * VLAN_ETH_FRAME_LEN),
			 SYS_ATOP, p);
	ocelot_write(ocelot, ocelot_wm_enc(atop_wm), SYS_ATOP_TOT_CFG);
}

static int ocelot_port_open(struct net_device *dev)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	enum phy_mode phy_mode;
	int err;

	/* Enable receiving frames on the port, and activate auto-learning of
	 * MAC addresses.
	 */
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_LEARNAUTO |
			 ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(port->chip_port),
			 ANA_PORT_PORT_CFG, port->chip_port);

	if (port->serdes) {
		if (port->phy_mode == PHY_INTERFACE_MODE_SGMII)
			phy_mode = PHY_MODE_SGMII;
		else
			phy_mode = PHY_MODE_QSGMII;

		err = phy_set_mode(port->serdes, phy_mode);
		if (err) {
			netdev_err(dev, "Could not set mode of SerDes\n");
			return err;
		}
	}

	err = phy_connect_direct(dev, port->phy, &ocelot_port_adjust_link,
				 port->phy_mode);
	if (err) {
		netdev_err(dev, "Could not attach to PHY\n");
		return err;
	}

	dev->phydev = port->phy;

	phy_attached_info(port->phy);
	phy_start(port->phy);
	return 0;
}

static int ocelot_port_stop(struct net_device *dev)
{
	struct ocelot_port *port = netdev_priv(dev);

	phy_disconnect(port->phy);

	dev->phydev = NULL;

	ocelot_port_writel(port, 0, DEV_MAC_ENA_CFG);
	ocelot_rmw_rix(port->ocelot, 0, QSYS_SWITCH_PORT_MODE_PORT_ENA,
			 QSYS_SWITCH_PORT_MODE, port->chip_port);
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
	ifh[0] = IFH_INJ_BYPASS;
	ifh[1] = (0xf00 & info->port) >> 8;
	ifh[2] = (0xff & info->port) << 24;
	ifh[3] = (info->tag_type << 16) | info->vid;

	return 0;
}

static int ocelot_port_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	u32 val, ifh[IFH_LEN];
	struct frame_info info = {};
	u8 grp = 0; /* Send everything on CPU group 0 */
	unsigned int i, count, last;

	val = ocelot_read(ocelot, QS_INJ_STATUS);
	if (!(val & QS_INJ_STATUS_FIFO_RDY(BIT(grp))) ||
	    (val & QS_INJ_STATUS_WMARK_REACHED(BIT(grp))))
		return NETDEV_TX_BUSY;

	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_SOF, QS_INJ_CTRL, grp);

	info.port = BIT(port->chip_port);
	info.tag_type = IFH_TAG_TYPE_C;
	info.vid = skb_vlan_tag_get(skb);
	ocelot_gen_ifh(ifh, &info);

	for (i = 0; i < IFH_LEN; i++)
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
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static void ocelot_mact_mc_reset(struct ocelot_port *port)
{
	struct ocelot *ocelot = port->ocelot;
	struct netdev_hw_addr *ha, *n;

	/* Free and forget all the MAC addresses stored in the port private mc
	 * list. These are mc addresses that were previously added by calling
	 * ocelot_mact_mc_add().
	 */
	list_for_each_entry_safe(ha, n, &port->mc, list) {
		ocelot_mact_forget(ocelot, ha->addr, port->pvid);
		list_del(&ha->list);
		kfree(ha);
	}
}

static int ocelot_mact_mc_add(struct ocelot_port *port,
			      struct netdev_hw_addr *hw_addr)
{
	struct ocelot *ocelot = port->ocelot;
	struct netdev_hw_addr *ha = kzalloc(sizeof(*ha), GFP_KERNEL);

	if (!ha)
		return -ENOMEM;

	memcpy(ha, hw_addr, sizeof(*ha));
	list_add_tail(&ha->list, &port->mc);

	ocelot_mact_learn(ocelot, PGID_CPU, ha->addr, port->pvid,
			  ENTRYTYPE_LOCKED);

	return 0;
}

static void ocelot_set_rx_mode(struct net_device *dev)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	struct netdev_hw_addr *ha;
	int i;
	u32 val;

	/* This doesn't handle promiscuous mode because the bridge core is
	 * setting IFF_PROMISC on all slave interfaces and all frames would be
	 * forwarded to the CPU port.
	 */
	val = GENMASK(ocelot->num_phys_ports - 1, 0);
	for (i = ocelot->num_phys_ports + 1; i < PGID_CPU; i++)
		ocelot_write_rix(ocelot, val, ANA_PGID_PGID, i);

	/* Handle the device multicast addresses. First remove all the
	 * previously installed addresses and then add the latest ones to the
	 * mac table.
	 */
	ocelot_mact_mc_reset(port);
	netdev_for_each_mc_addr(ha, dev)
		ocelot_mact_mc_add(port, ha);
}

static int ocelot_port_get_phys_port_name(struct net_device *dev,
					  char *buf, size_t len)
{
	struct ocelot_port *port = netdev_priv(dev);
	int ret;

	ret = snprintf(buf, len, "p%d", port->chip_port);
	if (ret >= len)
		return -EINVAL;

	return 0;
}

static int ocelot_port_set_mac_address(struct net_device *dev, void *p)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	const struct sockaddr *addr = p;

	/* Learn the new net device MAC address in the mac table. */
	ocelot_mact_learn(ocelot, PGID_CPU, addr->sa_data, port->pvid,
			  ENTRYTYPE_LOCKED);
	/* Then forget the previous one. */
	ocelot_mact_forget(ocelot, dev->dev_addr, port->pvid);

	ether_addr_copy(dev->dev_addr, addr->sa_data);
	return 0;
}

static void ocelot_get_stats64(struct net_device *dev,
			       struct rtnl_link_stats64 *stats)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;

	/* Configure the port to read the stats from */
	ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(port->chip_port),
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

static int ocelot_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			  struct net_device *dev, const unsigned char *addr,
			  u16 vid, u16 flags)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;

	if (!vid) {
		if (!port->vlan_aware)
			/* If the bridge is not VLAN aware and no VID was
			 * provided, set it to pvid to ensure the MAC entry
			 * matches incoming untagged packets
			 */
			vid = port->pvid;
		else
			/* If the bridge is VLAN aware a VID must be provided as
			 * otherwise the learnt entry wouldn't match any frame.
			 */
			return -EINVAL;
	}

	return ocelot_mact_learn(ocelot, port->chip_port, addr, vid,
				 ENTRYTYPE_NORMAL);
}

static int ocelot_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			  struct net_device *dev,
			  const unsigned char *addr, u16 vid)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;

	return ocelot_mact_forget(ocelot, addr, vid);
}

struct ocelot_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static int ocelot_fdb_do_dump(struct ocelot_mact_entry *entry,
			      struct ocelot_dump_ctx *dump)
{
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
	ndm->ndm_state   = NUD_REACHABLE;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, entry->mac))
		goto nla_put_failure;

	if (entry->vid && nla_put_u16(dump->skb, NDA_VLAN, entry->vid))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

static inline int ocelot_mact_read(struct ocelot_port *port, int row, int col,
				   struct ocelot_mact_entry *entry)
{
	struct ocelot *ocelot = port->ocelot;
	char mac[ETH_ALEN];
	u32 val, dst, macl, mach;

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
	if (dst != port->chip_port)
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

static int ocelot_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
			   struct net_device *dev,
			   struct net_device *filter_dev, int *idx)
{
	struct ocelot_port *port = netdev_priv(dev);
	int i, j, ret = 0;
	struct ocelot_dump_ctx dump = {
		.dev = dev,
		.skb = skb,
		.cb = cb,
		.idx = *idx,
	};

	struct ocelot_mact_entry entry;

	/* Loop through all the mac tables entries. There are 1024 rows of 4
	 * entries.
	 */
	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 4; j++) {
			ret = ocelot_mact_read(port, i, j, &entry);
			/* If the entry is invalid (wrong port, invalid...),
			 * skip it.
			 */
			if (ret == -EINVAL)
				continue;
			else if (ret)
				goto end;

			ret = ocelot_fdb_do_dump(&entry, &dump);
			if (ret)
				goto end;
		}
	}

end:
	*idx = dump.idx;
	return ret;
}

static int ocelot_vlan_rx_add_vid(struct net_device *dev, __be16 proto,
				  u16 vid)
{
	return ocelot_vlan_vid_add(dev, vid, false, true);
}

static int ocelot_vlan_rx_kill_vid(struct net_device *dev, __be16 proto,
				   u16 vid)
{
	return ocelot_vlan_vid_del(dev, vid);
}

static int ocelot_set_features(struct net_device *dev,
			       netdev_features_t features)
{
	struct ocelot_port *port = netdev_priv(dev);
	netdev_features_t changed = dev->features ^ features;

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER)
		ocelot_vlan_mode(port, features);

	return 0;
}

static const struct net_device_ops ocelot_port_netdev_ops = {
	.ndo_open			= ocelot_port_open,
	.ndo_stop			= ocelot_port_stop,
	.ndo_start_xmit			= ocelot_port_xmit,
	.ndo_set_rx_mode		= ocelot_set_rx_mode,
	.ndo_get_phys_port_name		= ocelot_port_get_phys_port_name,
	.ndo_set_mac_address		= ocelot_port_set_mac_address,
	.ndo_get_stats64		= ocelot_get_stats64,
	.ndo_fdb_add			= ocelot_fdb_add,
	.ndo_fdb_del			= ocelot_fdb_del,
	.ndo_fdb_dump			= ocelot_fdb_dump,
	.ndo_vlan_rx_add_vid		= ocelot_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid		= ocelot_vlan_rx_kill_vid,
	.ndo_set_features		= ocelot_set_features,
};

static void ocelot_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	struct ocelot_port *port = netdev_priv(netdev);
	struct ocelot *ocelot = port->ocelot;
	int i;

	if (sset != ETH_SS_STATS)
		return;

	for (i = 0; i < ocelot->num_stats; i++)
		memcpy(data + i * ETH_GSTRING_LEN, ocelot->stats_layout[i].name,
		       ETH_GSTRING_LEN);
}

static void ocelot_check_stats(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct ocelot *ocelot = container_of(del_work, struct ocelot, stats_work);
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

	cancel_delayed_work(&ocelot->stats_work);
	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);

	mutex_unlock(&ocelot->stats_lock);
}

static void ocelot_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	int i;

	/* check and update now */
	ocelot_check_stats(&ocelot->stats_work.work);

	/* Copy all counters */
	for (i = 0; i < ocelot->num_stats; i++)
		*data++ = ocelot->stats[port->chip_port * ocelot->num_stats + i];
}

static int ocelot_get_sset_count(struct net_device *dev, int sset)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;
	return ocelot->num_stats;
}

static const struct ethtool_ops ocelot_ethtool_ops = {
	.get_strings		= ocelot_get_strings,
	.get_ethtool_stats	= ocelot_get_ethtool_stats,
	.get_sset_count		= ocelot_get_sset_count,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static int ocelot_port_attr_get(struct net_device *dev,
				struct switchdev_attr *attr)
{
	struct ocelot_port *ocelot_port = netdev_priv(dev);
	struct ocelot *ocelot = ocelot_port->ocelot;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = sizeof(ocelot->base_mac);
		memcpy(&attr->u.ppid.id, &ocelot->base_mac,
		       attr->u.ppid.id_len);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ocelot_port_attr_stp_state_set(struct ocelot_port *ocelot_port,
					  struct switchdev_trans *trans,
					  u8 state)
{
	struct ocelot *ocelot = ocelot_port->ocelot;
	u32 port_cfg;
	int port, i;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	if (!(BIT(ocelot_port->chip_port) & ocelot->bridge_mask))
		return 0;

	port_cfg = ocelot_read_gix(ocelot, ANA_PORT_PORT_CFG,
				   ocelot_port->chip_port);

	switch (state) {
	case BR_STATE_FORWARDING:
		ocelot->bridge_fwd_mask |= BIT(ocelot_port->chip_port);
		/* Fallthrough */
	case BR_STATE_LEARNING:
		port_cfg |= ANA_PORT_PORT_CFG_LEARN_ENA;
		break;

	default:
		port_cfg &= ~ANA_PORT_PORT_CFG_LEARN_ENA;
		ocelot->bridge_fwd_mask &= ~BIT(ocelot_port->chip_port);
		break;
	}

	ocelot_write_gix(ocelot, port_cfg, ANA_PORT_PORT_CFG,
			 ocelot_port->chip_port);

	/* Apply FWD mask. The loop is needed to add/remove the current port as
	 * a source for the other ports.
	 */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (ocelot->bridge_fwd_mask & BIT(port)) {
			unsigned long mask = ocelot->bridge_fwd_mask & ~BIT(port);

			for (i = 0; i < ocelot->num_phys_ports; i++) {
				unsigned long bond_mask = ocelot->lags[i];

				if (!bond_mask)
					continue;

				if (bond_mask & BIT(port)) {
					mask &= ~bond_mask;
					break;
				}
			}

			ocelot_write_rix(ocelot,
					 BIT(ocelot->num_phys_ports) | mask,
					 ANA_PGID_PGID, PGID_SRC + port);
		} else {
			/* Only the CPU port, this is compatible with link
			 * aggregation.
			 */
			ocelot_write_rix(ocelot,
					 BIT(ocelot->num_phys_ports),
					 ANA_PGID_PGID, PGID_SRC + port);
		}
	}

	return 0;
}

static void ocelot_port_attr_ageing_set(struct ocelot_port *ocelot_port,
					unsigned long ageing_clock_t)
{
	struct ocelot *ocelot = ocelot_port->ocelot;
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies) / 1000;

	ocelot_write(ocelot, ANA_AUTOAGE_AGE_PERIOD(ageing_time / 2),
		     ANA_AUTOAGE);
}

static void ocelot_port_attr_mc_set(struct ocelot_port *port, bool mc)
{
	struct ocelot *ocelot = port->ocelot;
	u32 val = ocelot_read_gix(ocelot, ANA_PORT_CPU_FWD_CFG,
				  port->chip_port);

	if (mc)
		val |= ANA_PORT_CPU_FWD_CFG_CPU_IGMP_REDIR_ENA |
		       ANA_PORT_CPU_FWD_CFG_CPU_MLD_REDIR_ENA |
		       ANA_PORT_CPU_FWD_CFG_CPU_IPMC_CTRL_COPY_ENA;
	else
		val &= ~(ANA_PORT_CPU_FWD_CFG_CPU_IGMP_REDIR_ENA |
			 ANA_PORT_CPU_FWD_CFG_CPU_MLD_REDIR_ENA |
			 ANA_PORT_CPU_FWD_CFG_CPU_IPMC_CTRL_COPY_ENA);

	ocelot_write_gix(ocelot, val, ANA_PORT_CPU_FWD_CFG, port->chip_port);
}

static int ocelot_port_attr_set(struct net_device *dev,
				const struct switchdev_attr *attr,
				struct switchdev_trans *trans)
{
	struct ocelot_port *ocelot_port = netdev_priv(dev);
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ocelot_port_attr_stp_state_set(ocelot_port, trans,
					       attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		ocelot_port_attr_ageing_set(ocelot_port, attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		ocelot_port->vlan_aware = attr->u.vlan_filtering;
		ocelot_vlan_port_apply(ocelot_port->ocelot, ocelot_port);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED:
		ocelot_port_attr_mc_set(ocelot_port, !attr->u.mc_disabled);
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
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	struct ocelot_multicast *mc;
	unsigned char addr[ETH_ALEN];
	u16 vid = mdb->vid;
	bool new = false;

	if (!vid)
		vid = port->pvid;

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

	mc->ports |= BIT(port->chip_port);
	addr[2] = mc->ports << 0;
	addr[1] = mc->ports << 8;

	return ocelot_mact_learn(ocelot, 0, addr, vid, ENTRYTYPE_MACv4);
}

static int ocelot_port_obj_del_mdb(struct net_device *dev,
				   const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port *port = netdev_priv(dev);
	struct ocelot *ocelot = port->ocelot;
	struct ocelot_multicast *mc;
	unsigned char addr[ETH_ALEN];
	u16 vid = mdb->vid;

	if (!vid)
		vid = port->pvid;

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc)
		return -ENOENT;

	memcpy(addr, mc->addr, ETH_ALEN);
	addr[2] = mc->ports << 0;
	addr[1] = mc->ports << 8;
	addr[0] = 0;
	ocelot_mact_forget(ocelot, addr, vid);

	mc->ports &= ~BIT(port->chip_port);
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
			       struct switchdev_trans *trans)
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

static const struct switchdev_ops ocelot_port_switchdev_ops = {
	.switchdev_port_attr_get	= ocelot_port_attr_get,
	.switchdev_port_attr_set	= ocelot_port_attr_set,
	.switchdev_port_obj_add		= ocelot_port_obj_add,
	.switchdev_port_obj_del		= ocelot_port_obj_del,
};

static int ocelot_port_bridge_join(struct ocelot_port *ocelot_port,
				   struct net_device *bridge)
{
	struct ocelot *ocelot = ocelot_port->ocelot;

	if (!ocelot->bridge_mask) {
		ocelot->hw_bridge_dev = bridge;
	} else {
		if (ocelot->hw_bridge_dev != bridge)
			/* This is adding the port to a second bridge, this is
			 * unsupported */
			return -ENODEV;
	}

	ocelot->bridge_mask |= BIT(ocelot_port->chip_port);

	return 0;
}

static void ocelot_port_bridge_leave(struct ocelot_port *ocelot_port,
				     struct net_device *bridge)
{
	struct ocelot *ocelot = ocelot_port->ocelot;

	ocelot->bridge_mask &= ~BIT(ocelot_port->chip_port);

	if (!ocelot->bridge_mask)
		ocelot->hw_bridge_dev = NULL;

	/* Clear bridge vlan settings before calling ocelot_vlan_port_apply */
	ocelot_port->vlan_aware = 0;
	ocelot_port->pvid = 0;
	ocelot_port->vid = 0;
}

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

static int ocelot_port_lag_join(struct ocelot_port *ocelot_port,
				struct net_device *bond)
{
	struct ocelot *ocelot = ocelot_port->ocelot;
	int p = ocelot_port->chip_port;
	int lag, lp;
	struct net_device *ndev;
	u32 bond_mask = 0;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(bond, ndev) {
		struct ocelot_port *port = netdev_priv(ndev);

		bond_mask |= BIT(port->chip_port);
	}
	rcu_read_unlock();

	lp = __ffs(bond_mask);

	/* If the new port is the lowest one, use it as the logical port from
	 * now on
	 */
	if (p == lp) {
		lag = p;
		ocelot->lags[p] = bond_mask;
		bond_mask &= ~BIT(p);
		if (bond_mask) {
			lp = __ffs(bond_mask);
			ocelot->lags[lp] = 0;
		}
	} else {
		lag = lp;
		ocelot->lags[lp] |= BIT(p);
	}

	ocelot_setup_lag(ocelot, lag);
	ocelot_set_aggr_pgids(ocelot);

	return 0;
}

static void ocelot_port_lag_leave(struct ocelot_port *ocelot_port,
				  struct net_device *bond)
{
	struct ocelot *ocelot = ocelot_port->ocelot;
	int p = ocelot_port->chip_port;
	u32 port_cfg;
	int i;

	/* Remove port from any lag */
	for (i = 0; i < ocelot->num_phys_ports; i++)
		ocelot->lags[i] &= ~BIT(ocelot_port->chip_port);

	/* if it was the logical port of the lag, move the lag config to the
	 * next port
	 */
	if (ocelot->lags[p]) {
		int n = __ffs(ocelot->lags[p]);

		ocelot->lags[n] = ocelot->lags[p];
		ocelot->lags[p] = 0;

		ocelot_setup_lag(ocelot, n);
	}

	port_cfg = ocelot_read_gix(ocelot, ANA_PORT_PORT_CFG, p);
	port_cfg &= ~ANA_PORT_PORT_CFG_PORTID_VAL_M;
	ocelot_write_gix(ocelot, port_cfg | ANA_PORT_PORT_CFG_PORTID_VAL(p),
			 ANA_PORT_PORT_CFG, p);

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
	struct ocelot_port *ocelot_port = netdev_priv(dev);
	int err = 0;

	if (!ocelot_netdevice_dev_check(dev))
		return 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		if (netif_is_bridge_master(info->upper_dev)) {
			if (info->linking)
				err = ocelot_port_bridge_join(ocelot_port,
							      info->upper_dev);
			else
				ocelot_port_bridge_leave(ocelot_port,
							 info->upper_dev);

			ocelot_vlan_port_apply(ocelot_port->ocelot,
					       ocelot_port);
		}
		if (netif_is_lag_master(info->upper_dev)) {
			if (info->linking)
				err = ocelot_port_lag_join(ocelot_port,
							   info->upper_dev);
			else
				ocelot_port_lag_leave(ocelot_port,
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

	if (event == NETDEV_PRECHANGEUPPER &&
	    netif_is_lag_master(info->upper_dev)) {
		struct netdev_lag_upper_info *lag_upper_info = info->upper_info;
		struct netlink_ext_ack *extack;

		if (lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
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

int ocelot_probe_port(struct ocelot *ocelot, u8 port,
		      void __iomem *regs,
		      struct phy_device *phy)
{
	struct ocelot_port *ocelot_port;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(struct ocelot_port));
	if (!dev)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, ocelot->dev);
	ocelot_port = netdev_priv(dev);
	ocelot_port->dev = dev;
	ocelot_port->ocelot = ocelot;
	ocelot_port->regs = regs;
	ocelot_port->chip_port = port;
	ocelot_port->phy = phy;
	INIT_LIST_HEAD(&ocelot_port->mc);
	ocelot->ports[port] = ocelot_port;

	dev->netdev_ops = &ocelot_port_netdev_ops;
	dev->ethtool_ops = &ocelot_ethtool_ops;
	dev->switchdev_ops = &ocelot_port_switchdev_ops;

	dev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	memcpy(dev->dev_addr, ocelot->base_mac, ETH_ALEN);
	dev->dev_addr[ETH_ALEN - 1] += port;
	ocelot_mact_learn(ocelot, PGID_CPU, dev->dev_addr, ocelot_port->pvid,
			  ENTRYTYPE_LOCKED);

	err = register_netdev(dev);
	if (err) {
		dev_err(ocelot->dev, "register_netdev failed\n");
		goto err_register_netdev;
	}

	/* Basic L2 initialization */
	ocelot_vlan_port_apply(ocelot, ocelot_port);

	return 0;

err_register_netdev:
	free_netdev(dev);
	return err;
}
EXPORT_SYMBOL(ocelot_probe_port);

int ocelot_init(struct ocelot *ocelot)
{
	u32 port;
	int i, cpu = ocelot->num_phys_ports;
	char queue_name[32];

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
	snprintf(queue_name, sizeof(queue_name), "%s-stats",
		 dev_name(ocelot->dev));
	ocelot->stats_queue = create_singlethread_workqueue(queue_name);
	if (!ocelot->stats_queue)
		return -ENOMEM;

	ocelot_mact_init(ocelot);
	ocelot_vlan_init(ocelot);

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

	/* Configure and enable the CPU port. */
	ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, cpu);
	ocelot_write_rix(ocelot, BIT(cpu), ANA_PGID_PGID, PGID_CPU);
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(cpu),
			 ANA_PORT_PORT_CFG, cpu);

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

	/* CPU port Injection/Extraction configuration */
	ocelot_write_rix(ocelot, QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE |
			 QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG(1) |
			 QSYS_SWITCH_PORT_MODE_PORT_ENA,
			 QSYS_SWITCH_PORT_MODE, cpu);
	ocelot_write_rix(ocelot, SYS_PORT_MODE_INCL_XTR_HDR(1) |
			 SYS_PORT_MODE_INCL_INJ_HDR(1), SYS_PORT_MODE, cpu);
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

	INIT_DELAYED_WORK(&ocelot->stats_work, ocelot_check_stats);
	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);
	return 0;
}
EXPORT_SYMBOL(ocelot_init);

void ocelot_deinit(struct ocelot *ocelot)
{
	destroy_workqueue(ocelot->stats_queue);
	mutex_destroy(&ocelot->stats_lock);
}
EXPORT_SYMBOL(ocelot_deinit);

MODULE_LICENSE("Dual MIT/GPL");
