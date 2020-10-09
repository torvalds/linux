// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/if_bridge.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

#define TABLE_UPDATE_SLEEP_US 10
#define TABLE_UPDATE_TIMEOUT_US 100000

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

int ocelot_mact_learn(struct ocelot *ocelot, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid, enum macaccess_entry_type type)
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
EXPORT_SYMBOL(ocelot_mact_learn);

int ocelot_mact_forget(struct ocelot *ocelot,
		       const unsigned char mac[ETH_ALEN], unsigned int vid)
{
	ocelot_mact_select(ocelot, mac, vid);

	/* Issue a forget command */
	ocelot_write(ocelot,
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_FORGET),
		     ANA_TABLES_MACACCESS);

	return ocelot_mact_wait_for_completion(ocelot);
}
EXPORT_SYMBOL(ocelot_mact_forget);

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

static int ocelot_port_set_native_vlan(struct ocelot *ocelot, int port,
				       u16 vid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u32 val = 0;

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

	if (ocelot_port->vlan_aware && !ocelot_port->vid)
		/* If port is vlan-aware and tagged, drop untagged and priority
		 * tagged frames.
		 */
		val = ANA_PORT_DROP_CFG_DROP_UNTAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA;
	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_DROP_CFG_DROP_UNTAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA,
		       ANA_PORT_DROP_CFG, port);

	if (ocelot_port->vlan_aware) {
		if (ocelot_port->vid)
			/* Tag all frames except when VID == DEFAULT_VLAN */
			val = REW_TAG_CFG_TAG_CFG(1);
		else
			/* Tag all frames */
			val = REW_TAG_CFG_TAG_CFG(3);
	} else {
		/* Port tagging disabled. */
		val = REW_TAG_CFG_TAG_CFG(0);
	}
	ocelot_rmw_gix(ocelot, val,
		       REW_TAG_CFG_TAG_CFG_M,
		       REW_TAG_CFG, port);

	return 0;
}

void ocelot_port_vlan_filtering(struct ocelot *ocelot, int port,
				bool vlan_aware)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u32 val;

	ocelot_port->vlan_aware = vlan_aware;

	if (vlan_aware)
		val = ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
		      ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1);
	else
		val = 0;
	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
		       ANA_PORT_VLAN_CFG_VLAN_POP_CNT_M,
		       ANA_PORT_VLAN_CFG, port);

	ocelot_port_set_native_vlan(ocelot, port, ocelot_port->vid);
}
EXPORT_SYMBOL(ocelot_port_vlan_filtering);

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

	/* Disable HDX fast control */
	ocelot_port_writel(ocelot_port, DEV_PORT_MISC_HDX_FAST_DIS,
			   DEV_PORT_MISC);

	/* SGMII only for now */
	ocelot_port_writel(ocelot_port, PCS1G_MODE_CFG_SGMII_MODE_ENA,
			   PCS1G_MODE_CFG);
	ocelot_port_writel(ocelot_port, PCS1G_SD_CFG_SD_SEL, PCS1G_SD_CFG);

	/* Enable PCS */
	ocelot_port_writel(ocelot_port, PCS1G_CFG_PCS_ENA, PCS1G_CFG);

	/* No aneg on SGMII */
	ocelot_port_writel(ocelot_port, 0, PCS1G_ANEG_CFG);

	/* No loopback */
	ocelot_port_writel(ocelot_port, 0, PCS1G_LB_CFG);

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
	ocelot_fields_write(ocelot, port,
			    QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);

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

void ocelot_port_disable(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	ocelot_port_writel(ocelot_port, 0, DEV_MAC_ENA_CFG);
	ocelot_fields_write(ocelot, port, QSYS_SWITCH_PORT_MODE_PORT_ENA, 0);
}
EXPORT_SYMBOL(ocelot_port_disable);

int ocelot_port_add_txtstamp_skb(struct ocelot_port *ocelot_port,
				 struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	struct ocelot *ocelot = ocelot_port->ocelot;

	if (ocelot->ptp && shinfo->tx_flags & SKBTX_HW_TSTAMP &&
	    ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
		spin_lock(&ocelot_port->ts_id_lock);

		shinfo->tx_flags |= SKBTX_IN_PROGRESS;
		/* Store timestamp ID in cb[0] of sk_buff */
		skb->cb[0] = ocelot_port->ts_id;
		ocelot_port->ts_id = (ocelot_port->ts_id + 1) % 4;
		skb_queue_tail(&ocelot_port->tx_skbs, skb);

		spin_unlock(&ocelot_port->ts_id_lock);
		return 0;
	}
	return -ENODATA;
}
EXPORT_SYMBOL(ocelot_port_add_txtstamp_skb);

static void ocelot_get_hwtimestamp(struct ocelot *ocelot,
				   struct timespec64 *ts)
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

void ocelot_get_txtstamp(struct ocelot *ocelot)
{
	int budget = OCELOT_PTP_QUEUE_SZ;

	while (budget--) {
		struct sk_buff *skb, *skb_tmp, *skb_match = NULL;
		struct skb_shared_hwtstamps shhwtstamps;
		struct ocelot_port *port;
		struct timespec64 ts;
		unsigned long flags;
		u32 val, id, txport;

		val = ocelot_read(ocelot, SYS_PTP_STATUS);

		/* Check if a timestamp can be retrieved */
		if (!(val & SYS_PTP_STATUS_PTP_MESS_VLD))
			break;

		WARN_ON(val & SYS_PTP_STATUS_PTP_OVFL);

		/* Retrieve the ts ID and Tx port */
		id = SYS_PTP_STATUS_PTP_MESS_ID_X(val);
		txport = SYS_PTP_STATUS_PTP_MESS_TXPORT_X(val);

		/* Retrieve its associated skb */
		port = ocelot->ports[txport];

		spin_lock_irqsave(&port->tx_skbs.lock, flags);

		skb_queue_walk_safe(&port->tx_skbs, skb, skb_tmp) {
			if (skb->cb[0] != id)
				continue;
			__skb_unlink(skb, &port->tx_skbs);
			skb_match = skb;
			break;
		}

		spin_unlock_irqrestore(&port->tx_skbs.lock, flags);

		/* Get the h/w timestamp */
		ocelot_get_hwtimestamp(ocelot, &ts);

		if (unlikely(!skb_match))
			continue;

		/* Set the timestamp into the skb */
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_tstamp_tx(skb_match, &shhwtstamps);

		dev_kfree_skb_any(skb_match);

		/* Next ts */
		ocelot_write(ocelot, SYS_PTP_NXT_PTP_NXT, SYS_PTP_NXT);
	}
}
EXPORT_SYMBOL(ocelot_get_txtstamp);

int ocelot_fdb_add(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int pgid = port;

	if (port == ocelot->npi)
		pgid = PGID_CPU;

	if (!vid) {
		if (!ocelot_port->vlan_aware)
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

	return ocelot_mact_learn(ocelot, pgid, addr, vid, ENTRYTYPE_LOCKED);
}
EXPORT_SYMBOL(ocelot_fdb_add);

int ocelot_fdb_del(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid)
{
	return ocelot_mact_forget(ocelot, addr, vid);
}
EXPORT_SYMBOL(ocelot_fdb_del);

int ocelot_port_fdb_do_dump(const unsigned char *addr, u16 vid,
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
EXPORT_SYMBOL(ocelot_port_fdb_do_dump);

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

	/* Loop through all the mac tables entries. */
	for (i = 0; i < ocelot->num_mact_rows; i++) {
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

int ocelot_get_sset_count(struct ocelot *ocelot, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return ocelot->num_stats;
}
EXPORT_SYMBOL(ocelot_get_sset_count);

int ocelot_get_ts_info(struct ocelot *ocelot, int port,
		       struct ethtool_ts_info *info)
{
	info->phc_index = ocelot->ptp_clock ?
			  ptp_clock_index(ocelot->ptp_clock) : -1;
	if (info->phc_index == -1) {
		info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE |
					 SOF_TIMESTAMPING_RX_SOFTWARE |
					 SOF_TIMESTAMPING_SOFTWARE;
		return 0;
	}
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
		fallthrough;
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
		if (ocelot->bridge_fwd_mask & BIT(p)) {
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

			ocelot_write_rix(ocelot, mask,
					 ANA_PGID_PGID, PGID_SRC + p);
		} else {
			ocelot_write_rix(ocelot, 0,
					 ANA_PGID_PGID, PGID_SRC + p);
		}
	}
}
EXPORT_SYMBOL(ocelot_bridge_stp_state_set);

void ocelot_set_ageing_time(struct ocelot *ocelot, unsigned int msecs)
{
	unsigned int age_period = ANA_AUTOAGE_AGE_PERIOD(msecs / 2000);

	/* Setting AGE_PERIOD to zero effectively disables automatic aging,
	 * which is clearly not what our intention is. So avoid that.
	 */
	if (!age_period)
		age_period = 1;

	ocelot_rmw(ocelot, age_period, ANA_AUTOAGE_AGE_PERIOD_M, ANA_AUTOAGE);
}
EXPORT_SYMBOL(ocelot_set_ageing_time);

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

static enum macaccess_entry_type ocelot_classify_mdb(const unsigned char *addr)
{
	if (addr[0] == 0x01 && addr[1] == 0x00 && addr[2] == 0x5e)
		return ENTRYTYPE_MACv4;
	if (addr[0] == 0x33 && addr[1] == 0x33)
		return ENTRYTYPE_MACv6;
	return ENTRYTYPE_NORMAL;
}

static int ocelot_mdb_get_pgid(struct ocelot *ocelot,
			       enum macaccess_entry_type entry_type)
{
	int pgid;

	/* According to VSC7514 datasheet 3.9.1.5 IPv4 Multicast Entries and
	 * 3.9.1.6 IPv6 Multicast Entries, "Instead of a lookup in the
	 * destination mask table (PGID), the destination set is programmed as
	 * part of the entry MAC address.", and the DEST_IDX is set to 0.
	 */
	if (entry_type == ENTRYTYPE_MACv4 ||
	    entry_type == ENTRYTYPE_MACv6)
		return 0;

	for_each_nonreserved_multicast_dest_pgid(ocelot, pgid) {
		struct ocelot_multicast *mc;
		bool used = false;

		list_for_each_entry(mc, &ocelot->multicast, list) {
			if (mc->pgid == pgid) {
				used = true;
				break;
			}
		}

		if (!used)
			return pgid;
	}

	return -1;
}

static void ocelot_encode_ports_to_mdb(unsigned char *addr,
				       struct ocelot_multicast *mc,
				       enum macaccess_entry_type entry_type)
{
	memcpy(addr, mc->addr, ETH_ALEN);

	if (entry_type == ENTRYTYPE_MACv4) {
		addr[0] = 0;
		addr[1] = mc->ports >> 8;
		addr[2] = mc->ports & 0xff;
	} else if (entry_type == ENTRYTYPE_MACv6) {
		addr[0] = mc->ports >> 8;
		addr[1] = mc->ports & 0xff;
	}
}

int ocelot_port_mdb_add(struct ocelot *ocelot, int port,
			const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	enum macaccess_entry_type entry_type;
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	u16 vid = mdb->vid;
	bool new = false;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	if (!vid)
		vid = ocelot_port->pvid;

	entry_type = ocelot_classify_mdb(mdb->addr);

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc) {
		int pgid = ocelot_mdb_get_pgid(ocelot, entry_type);

		if (pgid < 0) {
			dev_err(ocelot->dev,
				"No more PGIDs available for mdb %pM vid %d\n",
				mdb->addr, vid);
			return -ENOSPC;
		}

		mc = devm_kzalloc(ocelot->dev, sizeof(*mc), GFP_KERNEL);
		if (!mc)
			return -ENOMEM;

		memcpy(mc->addr, mdb->addr, ETH_ALEN);
		mc->vid = vid;
		mc->pgid = pgid;

		list_add_tail(&mc->list, &ocelot->multicast);
		new = true;
	}

	if (!new) {
		ocelot_encode_ports_to_mdb(addr, mc, entry_type);
		ocelot_mact_forget(ocelot, addr, vid);
	}

	mc->ports |= BIT(port);
	ocelot_encode_ports_to_mdb(addr, mc, entry_type);

	return ocelot_mact_learn(ocelot, mc->pgid, addr, vid, entry_type);
}
EXPORT_SYMBOL(ocelot_port_mdb_add);

int ocelot_port_mdb_del(struct ocelot *ocelot, int port,
			const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	enum macaccess_entry_type entry_type;
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	u16 vid = mdb->vid;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	if (!vid)
		vid = ocelot_port->pvid;

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc)
		return -ENOENT;

	entry_type = ocelot_classify_mdb(mdb->addr);

	ocelot_encode_ports_to_mdb(addr, mc, entry_type);
	ocelot_mact_forget(ocelot, addr, vid);

	mc->ports &= ~BIT(port);
	if (!mc->ports) {
		list_del(&mc->list);
		devm_kfree(ocelot->dev, mc);
		return 0;
	}

	ocelot_encode_ports_to_mdb(addr, mc, entry_type);

	return ocelot_mact_learn(ocelot, mc->pgid, addr, vid, entry_type);
}
EXPORT_SYMBOL(ocelot_port_mdb_del);

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
	for_each_unicast_dest_pgid(ocelot, port)
		ocelot_write_rix(ocelot, BIT(port), ANA_PGID_PGID, port);

	for_each_aggr_pgid(ocelot, i)
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

		for_each_aggr_pgid(ocelot, i) {
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

int ocelot_port_lag_join(struct ocelot *ocelot, int port,
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
EXPORT_SYMBOL(ocelot_port_lag_join);

void ocelot_port_lag_leave(struct ocelot *ocelot, int port,
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
EXPORT_SYMBOL(ocelot_port_lag_leave);

/* Configure the maximum SDU (L2 payload) on RX to the value specified in @sdu.
 * The length of VLAN tags is accounted for automatically via DEV_MAC_TAGS_CFG.
 * In the special case that it's the NPI port that we're configuring, the
 * length of the tag and optional prefix needs to be accounted for privately,
 * in order to be able to sustain communication at the requested @sdu.
 */
void ocelot_port_set_maxlen(struct ocelot *ocelot, int port, size_t sdu)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int maxlen = sdu + ETH_HLEN + ETH_FCS_LEN;
	int pause_start, pause_stop;
	int atop, atop_tot;

	if (port == ocelot->npi) {
		maxlen += OCELOT_TAG_LEN;

		if (ocelot->inj_prefix == OCELOT_TAG_PREFIX_SHORT)
			maxlen += OCELOT_SHORT_PREFIX_LEN;
		else if (ocelot->inj_prefix == OCELOT_TAG_PREFIX_LONG)
			maxlen += OCELOT_LONG_PREFIX_LEN;
	}

	ocelot_port_writel(ocelot_port, maxlen, DEV_MAC_MAXLEN_CFG);

	/* Set Pause watermark hysteresis */
	pause_start = 6 * maxlen / OCELOT_BUFFER_CELL_SZ;
	pause_stop = 4 * maxlen / OCELOT_BUFFER_CELL_SZ;
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_START,
			    pause_start);
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_STOP,
			    pause_stop);

	/* Tail dropping watermarks */
	atop_tot = (ocelot->shared_queue_sz - 9 * maxlen) /
		   OCELOT_BUFFER_CELL_SZ;
	atop = (9 * maxlen) / OCELOT_BUFFER_CELL_SZ;
	ocelot_write_rix(ocelot, ocelot->ops->wm_enc(atop), SYS_ATOP, port);
	ocelot_write(ocelot, ocelot->ops->wm_enc(atop_tot), SYS_ATOP_TOT_CFG);
}
EXPORT_SYMBOL(ocelot_port_set_maxlen);

int ocelot_get_max_mtu(struct ocelot *ocelot, int port)
{
	int max_mtu = 65535 - ETH_HLEN - ETH_FCS_LEN;

	if (port == ocelot->npi) {
		max_mtu -= OCELOT_TAG_LEN;

		if (ocelot->inj_prefix == OCELOT_TAG_PREFIX_SHORT)
			max_mtu -= OCELOT_SHORT_PREFIX_LEN;
		else if (ocelot->inj_prefix == OCELOT_TAG_PREFIX_LONG)
			max_mtu -= OCELOT_LONG_PREFIX_LEN;
	}

	return max_mtu;
}
EXPORT_SYMBOL(ocelot_get_max_mtu);

void ocelot_init_port(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	skb_queue_head_init(&ocelot_port->tx_skbs);
	spin_lock_init(&ocelot_port->ts_id_lock);

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
	ocelot_port_set_maxlen(ocelot, port, ETH_DATA_LEN);
	ocelot_port_writel(ocelot_port, DEV_MAC_TAGS_CFG_TAG_ID(ETH_P_8021AD) |
			   DEV_MAC_TAGS_CFG_VLAN_AWR_ENA |
			   DEV_MAC_TAGS_CFG_VLAN_DBL_AWR_ENA |
			   DEV_MAC_TAGS_CFG_VLAN_LEN_AWR_ENA,
			   DEV_MAC_TAGS_CFG);

	/* Set SMAC of Pause frame (00:00:00:00:00:00) */
	ocelot_port_writel(ocelot_port, 0, DEV_MAC_FC_MAC_HIGH_CFG);
	ocelot_port_writel(ocelot_port, 0, DEV_MAC_FC_MAC_LOW_CFG);

	/* Enable transmission of pause frames */
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, 1);

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

/* Configure and enable the CPU port module, which is a set of queues.
 * If @npi contains a valid port index, the CPU port module is connected
 * to the Node Processor Interface (NPI). This is the mode through which
 * frames can be injected from and extracted to an external CPU,
 * over Ethernet.
 */
void ocelot_configure_cpu(struct ocelot *ocelot, int npi,
			  enum ocelot_tag_prefix injection,
			  enum ocelot_tag_prefix extraction)
{
	int cpu = ocelot->num_phys_ports;

	ocelot->npi = npi;
	ocelot->inj_prefix = injection;
	ocelot->xtr_prefix = extraction;

	/* The unicast destination PGID for the CPU port module is unused */
	ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, cpu);
	/* Instead set up a multicast destination PGID for traffic copied to
	 * the CPU. Whitelisted MAC addresses like the port netdevice MAC
	 * addresses will be copied to the CPU via this PGID.
	 */
	ocelot_write_rix(ocelot, BIT(cpu), ANA_PGID_PGID, PGID_CPU);
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(cpu),
			 ANA_PORT_PORT_CFG, cpu);

	if (npi >= 0 && npi < ocelot->num_phys_ports) {
		ocelot_write(ocelot, QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_M |
			     QSYS_EXT_CPU_CFG_EXT_CPU_PORT(npi),
			     QSYS_EXT_CPU_CFG);

		/* Enable NPI port */
		ocelot_fields_write(ocelot, npi,
				    QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);
		/* NPI port Injection/Extraction configuration */
		ocelot_fields_write(ocelot, npi, SYS_PORT_MODE_INCL_XTR_HDR,
				    extraction);
		ocelot_fields_write(ocelot, npi, SYS_PORT_MODE_INCL_INJ_HDR,
				    injection);

		/* Disable transmission of pause frames */
		ocelot_fields_write(ocelot, npi, SYS_PAUSE_CFG_PAUSE_ENA, 0);
	}

	/* Enable CPU port module */
	ocelot_fields_write(ocelot, cpu, QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);
	/* CPU port Injection/Extraction configuration */
	ocelot_fields_write(ocelot, cpu, SYS_PORT_MODE_INCL_XTR_HDR,
			    extraction);
	ocelot_fields_write(ocelot, cpu, SYS_PORT_MODE_INCL_INJ_HDR,
			    injection);

	/* Configure the CPU port to be VLAN aware */
	ocelot_write_gix(ocelot, ANA_PORT_VLAN_CFG_VLAN_VID(0) |
				 ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
				 ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1),
			 ANA_PORT_VLAN_CFG, cpu);
}
EXPORT_SYMBOL(ocelot_configure_cpu);

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
	ocelot_vcap_init(ocelot);

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
	for_each_nonreserved_multicast_dest_pgid(ocelot, i) {
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

	return 0;
}
EXPORT_SYMBOL(ocelot_init);

void ocelot_deinit(struct ocelot *ocelot)
{
	cancel_delayed_work(&ocelot->stats_work);
	destroy_workqueue(ocelot->stats_queue);
	mutex_destroy(&ocelot->stats_lock);
}
EXPORT_SYMBOL(ocelot_deinit);

void ocelot_deinit_port(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	skb_queue_purge(&ocelot_port->tx_skbs);
}
EXPORT_SYMBOL(ocelot_deinit_port);

MODULE_LICENSE("Dual MIT/GPL");
