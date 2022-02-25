// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/dsa/ocelot.h>
#include <linux/if_bridge.h>
#include <linux/ptp_classify.h>
#include <soc/mscc/ocelot_vcap.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

#define TABLE_UPDATE_SLEEP_US 10
#define TABLE_UPDATE_TIMEOUT_US 100000

struct ocelot_mact_entry {
	u8 mac[ETH_ALEN];
	u16 vid;
	enum macaccess_entry_type type;
};

/* Caller must hold &ocelot->mact_lock */
static inline u32 ocelot_mact_read_macaccess(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, ANA_TABLES_MACACCESS);
}

/* Caller must hold &ocelot->mact_lock */
static inline int ocelot_mact_wait_for_completion(struct ocelot *ocelot)
{
	u32 val;

	return readx_poll_timeout(ocelot_mact_read_macaccess,
		ocelot, val,
		(val & ANA_TABLES_MACACCESS_MAC_TABLE_CMD_M) ==
		MACACCESS_CMD_IDLE,
		TABLE_UPDATE_SLEEP_US, TABLE_UPDATE_TIMEOUT_US);
}

/* Caller must hold &ocelot->mact_lock */
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

static int __ocelot_mact_learn(struct ocelot *ocelot, int port,
			       const unsigned char mac[ETH_ALEN],
			       unsigned int vid, enum macaccess_entry_type type)
{
	u32 cmd = ANA_TABLES_MACACCESS_VALID |
		ANA_TABLES_MACACCESS_DEST_IDX(port) |
		ANA_TABLES_MACACCESS_ENTRYTYPE(type) |
		ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_LEARN);
	unsigned int mc_ports;
	int err;

	/* Set MAC_CPU_COPY if the CPU port is used by a multicast entry */
	if (type == ENTRYTYPE_MACv4)
		mc_ports = (mac[1] << 8) | mac[2];
	else if (type == ENTRYTYPE_MACv6)
		mc_ports = (mac[0] << 8) | mac[1];
	else
		mc_ports = 0;

	if (mc_ports & BIT(ocelot->num_phys_ports))
		cmd |= ANA_TABLES_MACACCESS_MAC_CPU_COPY;

	ocelot_mact_select(ocelot, mac, vid);

	/* Issue a write command */
	ocelot_write(ocelot, cmd, ANA_TABLES_MACACCESS);

	err = ocelot_mact_wait_for_completion(ocelot);

	return err;
}

int ocelot_mact_learn(struct ocelot *ocelot, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid, enum macaccess_entry_type type)
{
	int ret;

	mutex_lock(&ocelot->mact_lock);
	ret = __ocelot_mact_learn(ocelot, port, mac, vid, type);
	mutex_unlock(&ocelot->mact_lock);

	return ret;
}
EXPORT_SYMBOL(ocelot_mact_learn);

int ocelot_mact_forget(struct ocelot *ocelot,
		       const unsigned char mac[ETH_ALEN], unsigned int vid)
{
	int err;

	mutex_lock(&ocelot->mact_lock);

	ocelot_mact_select(ocelot, mac, vid);

	/* Issue a forget command */
	ocelot_write(ocelot,
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_FORGET),
		     ANA_TABLES_MACACCESS);

	err = ocelot_mact_wait_for_completion(ocelot);

	mutex_unlock(&ocelot->mact_lock);

	return err;
}
EXPORT_SYMBOL(ocelot_mact_forget);

int ocelot_mact_lookup(struct ocelot *ocelot, int *dst_idx,
		       const unsigned char mac[ETH_ALEN],
		       unsigned int vid, enum macaccess_entry_type *type)
{
	int val;

	mutex_lock(&ocelot->mact_lock);

	ocelot_mact_select(ocelot, mac, vid);

	/* Issue a read command with MACACCESS_VALID=1. */
	ocelot_write(ocelot, ANA_TABLES_MACACCESS_VALID |
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_READ),
		     ANA_TABLES_MACACCESS);

	if (ocelot_mact_wait_for_completion(ocelot)) {
		mutex_unlock(&ocelot->mact_lock);
		return -ETIMEDOUT;
	}

	/* Read back the entry flags */
	val = ocelot_read(ocelot, ANA_TABLES_MACACCESS);

	mutex_unlock(&ocelot->mact_lock);

	if (!(val & ANA_TABLES_MACACCESS_VALID))
		return -ENOENT;

	*dst_idx = ANA_TABLES_MACACCESS_DEST_IDX_X(val);
	*type = ANA_TABLES_MACACCESS_ENTRYTYPE_X(val);

	return 0;
}
EXPORT_SYMBOL(ocelot_mact_lookup);

int ocelot_mact_learn_streamdata(struct ocelot *ocelot, int dst_idx,
				 const unsigned char mac[ETH_ALEN],
				 unsigned int vid,
				 enum macaccess_entry_type type,
				 int sfid, int ssid)
{
	int ret;

	mutex_lock(&ocelot->mact_lock);

	ocelot_write(ocelot,
		     (sfid < 0 ? 0 : ANA_TABLES_STREAMDATA_SFID_VALID) |
		     ANA_TABLES_STREAMDATA_SFID(sfid) |
		     (ssid < 0 ? 0 : ANA_TABLES_STREAMDATA_SSID_VALID) |
		     ANA_TABLES_STREAMDATA_SSID(ssid),
		     ANA_TABLES_STREAMDATA);

	ret = __ocelot_mact_learn(ocelot, dst_idx, mac, vid, type);

	mutex_unlock(&ocelot->mact_lock);

	return ret;
}
EXPORT_SYMBOL(ocelot_mact_learn_streamdata);

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

	/* Clear the MAC table. We are not concurrent with anyone, so
	 * holding &ocelot->mact_lock is pointless.
	 */
	ocelot_write(ocelot, MACACCESS_CMD_INIT, ANA_TABLES_MACACCESS);
}

static void ocelot_vcap_enable(struct ocelot *ocelot, int port)
{
	ocelot_write_gix(ocelot, ANA_PORT_VCAP_S2_CFG_S2_ENA |
			 ANA_PORT_VCAP_S2_CFG_S2_IP6_CFG(0xa),
			 ANA_PORT_VCAP_S2_CFG, port);

	ocelot_write_gix(ocelot, ANA_PORT_VCAP_CFG_S1_ENA,
			 ANA_PORT_VCAP_CFG, port);

	ocelot_rmw_gix(ocelot, REW_PORT_CFG_ES0_EN,
		       REW_PORT_CFG_ES0_EN,
		       REW_PORT_CFG, port);
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

static int ocelot_port_num_untagged_vlans(struct ocelot *ocelot, int port)
{
	struct ocelot_bridge_vlan *vlan;
	int num_untagged = 0;

	list_for_each_entry(vlan, &ocelot->vlans, list) {
		if (!(vlan->portmask & BIT(port)))
			continue;

		if (vlan->untagged & BIT(port))
			num_untagged++;
	}

	return num_untagged;
}

static int ocelot_port_num_tagged_vlans(struct ocelot *ocelot, int port)
{
	struct ocelot_bridge_vlan *vlan;
	int num_tagged = 0;

	list_for_each_entry(vlan, &ocelot->vlans, list) {
		if (!(vlan->portmask & BIT(port)))
			continue;

		if (!(vlan->untagged & BIT(port)))
			num_tagged++;
	}

	return num_tagged;
}

/* We use native VLAN when we have to mix egress-tagged VLANs with exactly
 * _one_ egress-untagged VLAN (_the_ native VLAN)
 */
static bool ocelot_port_uses_native_vlan(struct ocelot *ocelot, int port)
{
	return ocelot_port_num_tagged_vlans(ocelot, port) &&
	       ocelot_port_num_untagged_vlans(ocelot, port) == 1;
}

static struct ocelot_bridge_vlan *
ocelot_port_find_native_vlan(struct ocelot *ocelot, int port)
{
	struct ocelot_bridge_vlan *vlan;

	list_for_each_entry(vlan, &ocelot->vlans, list)
		if (vlan->portmask & BIT(port) && vlan->untagged & BIT(port))
			return vlan;

	return NULL;
}

/* Keep in sync REW_TAG_CFG_TAG_CFG and, if applicable,
 * REW_PORT_VLAN_CFG_PORT_VID, with the bridge VLAN table and VLAN awareness
 * state of the port.
 */
static void ocelot_port_manage_port_tag(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	enum ocelot_port_tag_config tag_cfg;
	bool uses_native_vlan = false;

	if (ocelot_port->vlan_aware) {
		uses_native_vlan = ocelot_port_uses_native_vlan(ocelot, port);

		if (uses_native_vlan)
			tag_cfg = OCELOT_PORT_TAG_NATIVE;
		else if (ocelot_port_num_untagged_vlans(ocelot, port))
			tag_cfg = OCELOT_PORT_TAG_DISABLED;
		else
			tag_cfg = OCELOT_PORT_TAG_TRUNK;
	} else {
		tag_cfg = OCELOT_PORT_TAG_DISABLED;
	}

	ocelot_rmw_gix(ocelot, REW_TAG_CFG_TAG_CFG(tag_cfg),
		       REW_TAG_CFG_TAG_CFG_M,
		       REW_TAG_CFG, port);

	if (uses_native_vlan) {
		struct ocelot_bridge_vlan *native_vlan;

		/* Not having a native VLAN is impossible, because
		 * ocelot_port_num_untagged_vlans has returned 1.
		 * So there is no use in checking for NULL here.
		 */
		native_vlan = ocelot_port_find_native_vlan(ocelot, port);

		ocelot_rmw_gix(ocelot,
			       REW_PORT_VLAN_CFG_PORT_VID(native_vlan->vid),
			       REW_PORT_VLAN_CFG_PORT_VID_M,
			       REW_PORT_VLAN_CFG, port);
	}
}

/* Default vlan to clasify for untagged frames (may be zero) */
static void ocelot_port_set_pvid(struct ocelot *ocelot, int port,
				 const struct ocelot_bridge_vlan *pvid_vlan)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u16 pvid = OCELOT_VLAN_UNAWARE_PVID;
	u32 val = 0;

	ocelot_port->pvid_vlan = pvid_vlan;

	if (ocelot_port->vlan_aware && pvid_vlan)
		pvid = pvid_vlan->vid;

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_VLAN_CFG_VLAN_VID(pvid),
		       ANA_PORT_VLAN_CFG_VLAN_VID_M,
		       ANA_PORT_VLAN_CFG, port);

	/* If there's no pvid, we should drop not only untagged traffic (which
	 * happens automatically), but also 802.1p traffic which gets
	 * classified to VLAN 0, but that is always in our RX filter, so it
	 * would get accepted were it not for this setting.
	 */
	if (!pvid_vlan && ocelot_port->vlan_aware)
		val = ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA;

	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA,
		       ANA_PORT_DROP_CFG, port);
}

static struct ocelot_bridge_vlan *ocelot_bridge_vlan_find(struct ocelot *ocelot,
							  u16 vid)
{
	struct ocelot_bridge_vlan *vlan;

	list_for_each_entry(vlan, &ocelot->vlans, list)
		if (vlan->vid == vid)
			return vlan;

	return NULL;
}

static int ocelot_vlan_member_add(struct ocelot *ocelot, int port, u16 vid,
				  bool untagged)
{
	struct ocelot_bridge_vlan *vlan = ocelot_bridge_vlan_find(ocelot, vid);
	unsigned long portmask;
	int err;

	if (vlan) {
		portmask = vlan->portmask | BIT(port);

		err = ocelot_vlant_set_mask(ocelot, vid, portmask);
		if (err)
			return err;

		vlan->portmask = portmask;
		/* Bridge VLANs can be overwritten with a different
		 * egress-tagging setting, so make sure to override an untagged
		 * with a tagged VID if that's going on.
		 */
		if (untagged)
			vlan->untagged |= BIT(port);
		else
			vlan->untagged &= ~BIT(port);

		return 0;
	}

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	portmask = BIT(port);

	err = ocelot_vlant_set_mask(ocelot, vid, portmask);
	if (err) {
		kfree(vlan);
		return err;
	}

	vlan->vid = vid;
	vlan->portmask = portmask;
	if (untagged)
		vlan->untagged = BIT(port);
	INIT_LIST_HEAD(&vlan->list);
	list_add_tail(&vlan->list, &ocelot->vlans);

	return 0;
}

static int ocelot_vlan_member_del(struct ocelot *ocelot, int port, u16 vid)
{
	struct ocelot_bridge_vlan *vlan = ocelot_bridge_vlan_find(ocelot, vid);
	unsigned long portmask;
	int err;

	if (!vlan)
		return 0;

	portmask = vlan->portmask & ~BIT(port);

	err = ocelot_vlant_set_mask(ocelot, vid, portmask);
	if (err)
		return err;

	vlan->portmask = portmask;
	if (vlan->portmask)
		return 0;

	list_del(&vlan->list);
	kfree(vlan);

	return 0;
}

int ocelot_port_vlan_filtering(struct ocelot *ocelot, int port,
			       bool vlan_aware, struct netlink_ext_ack *extack)
{
	struct ocelot_vcap_block *block = &ocelot->block[VCAP_IS1];
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_vcap_filter *filter;
	u32 val;

	list_for_each_entry(filter, &block->rules, list) {
		if (filter->ingress_port_mask & BIT(port) &&
		    filter->action.vid_replace_ena) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot change VLAN state with vlan modify rules active");
			return -EBUSY;
		}
	}

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

	ocelot_port_set_pvid(ocelot, port, ocelot_port->pvid_vlan);
	ocelot_port_manage_port_tag(ocelot, port);

	return 0;
}
EXPORT_SYMBOL(ocelot_port_vlan_filtering);

int ocelot_vlan_prepare(struct ocelot *ocelot, int port, u16 vid, bool pvid,
			bool untagged, struct netlink_ext_ack *extack)
{
	if (untagged) {
		/* We are adding an egress-tagged VLAN */
		if (ocelot_port_uses_native_vlan(ocelot, port)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Port with egress-tagged VLANs cannot have more than one egress-untagged (native) VLAN");
			return -EBUSY;
		}
	} else {
		/* We are adding an egress-tagged VLAN */
		if (ocelot_port_num_untagged_vlans(ocelot, port) > 1) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Port with more than one egress-untagged VLAN cannot have egress-tagged VLANs");
			return -EBUSY;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_vlan_prepare);

int ocelot_vlan_add(struct ocelot *ocelot, int port, u16 vid, bool pvid,
		    bool untagged)
{
	int err;

	err = ocelot_vlan_member_add(ocelot, port, vid, untagged);
	if (err)
		return err;

	/* Default ingress vlan classification */
	if (pvid)
		ocelot_port_set_pvid(ocelot, port,
				     ocelot_bridge_vlan_find(ocelot, vid));

	/* Untagged egress vlan clasification */
	ocelot_port_manage_port_tag(ocelot, port);

	return 0;
}
EXPORT_SYMBOL(ocelot_vlan_add);

int ocelot_vlan_del(struct ocelot *ocelot, int port, u16 vid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	bool del_pvid = false;
	int err;

	if (ocelot_port->pvid_vlan && ocelot_port->pvid_vlan->vid == vid)
		del_pvid = true;

	err = ocelot_vlan_member_del(ocelot, port, vid);
	if (err)
		return err;

	/* Ingress */
	if (del_pvid)
		ocelot_port_set_pvid(ocelot, port, NULL);

	/* Egress */
	ocelot_port_manage_port_tag(ocelot, port);

	return 0;
}
EXPORT_SYMBOL(ocelot_vlan_del);

static void ocelot_vlan_init(struct ocelot *ocelot)
{
	unsigned long all_ports = GENMASK(ocelot->num_phys_ports - 1, 0);
	u16 port, vid;

	/* Clear VLAN table, by default all ports are members of all VLANs */
	ocelot_write(ocelot, ANA_TABLES_VLANACCESS_CMD_INIT,
		     ANA_TABLES_VLANACCESS);
	ocelot_vlant_wait_for_completion(ocelot);

	/* Configure the port VLAN memberships */
	for (vid = 1; vid < VLAN_N_VID; vid++)
		ocelot_vlant_set_mask(ocelot, vid, 0);

	/* Because VLAN filtering is enabled, we need VID 0 to get untagged
	 * traffic.  It is added automatically if 8021q module is loaded, but
	 * we can't rely on it since module may be not loaded.
	 */
	ocelot_vlant_set_mask(ocelot, OCELOT_VLAN_UNAWARE_PVID, all_ports);

	/* Set vlan ingress filter mask to all ports but the CPU port by
	 * default.
	 */
	ocelot_write(ocelot, all_ports, ANA_VLANMASK);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		ocelot_write_gix(ocelot, 0, REW_PORT_VLAN_CFG, port);
		ocelot_write_gix(ocelot, 0, REW_TAG_CFG, port);
	}
}

static u32 ocelot_read_eq_avail(struct ocelot *ocelot, int port)
{
	return ocelot_read_rix(ocelot, QSYS_SW_STATUS, port);
}

static int ocelot_port_flush(struct ocelot *ocelot, int port)
{
	unsigned int pause_ena;
	int err, val;

	/* Disable dequeuing from the egress queues */
	ocelot_rmw_rix(ocelot, QSYS_PORT_MODE_DEQUEUE_DIS,
		       QSYS_PORT_MODE_DEQUEUE_DIS,
		       QSYS_PORT_MODE, port);

	/* Disable flow control */
	ocelot_fields_read(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, &pause_ena);
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, 0);

	/* Disable priority flow control */
	ocelot_fields_write(ocelot, port,
			    QSYS_SWITCH_PORT_MODE_TX_PFC_ENA, 0);

	/* Wait at least the time it takes to receive a frame of maximum length
	 * at the port.
	 * Worst-case delays for 10 kilobyte jumbo frames are:
	 * 8 ms on a 10M port
	 * 800 μs on a 100M port
	 * 80 μs on a 1G port
	 * 32 μs on a 2.5G port
	 */
	usleep_range(8000, 10000);

	/* Disable half duplex backpressure. */
	ocelot_rmw_rix(ocelot, 0, SYS_FRONT_PORT_MODE_HDX_MODE,
		       SYS_FRONT_PORT_MODE, port);

	/* Flush the queues associated with the port. */
	ocelot_rmw_gix(ocelot, REW_PORT_CFG_FLUSH_ENA, REW_PORT_CFG_FLUSH_ENA,
		       REW_PORT_CFG, port);

	/* Enable dequeuing from the egress queues. */
	ocelot_rmw_rix(ocelot, 0, QSYS_PORT_MODE_DEQUEUE_DIS, QSYS_PORT_MODE,
		       port);

	/* Wait until flushing is complete. */
	err = read_poll_timeout(ocelot_read_eq_avail, val, !val,
				100, 2000000, false, ocelot, port);

	/* Clear flushing again. */
	ocelot_rmw_gix(ocelot, 0, REW_PORT_CFG_FLUSH_ENA, REW_PORT_CFG, port);

	/* Re-enable flow control */
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, pause_ena);

	return err;
}

void ocelot_phylink_mac_link_down(struct ocelot *ocelot, int port,
				  unsigned int link_an_mode,
				  phy_interface_t interface,
				  unsigned long quirks)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int err;

	ocelot_port->speed = SPEED_UNKNOWN;

	ocelot_port_rmwl(ocelot_port, 0, DEV_MAC_ENA_CFG_RX_ENA,
			 DEV_MAC_ENA_CFG);

	if (ocelot->ops->cut_through_fwd) {
		mutex_lock(&ocelot->fwd_domain_lock);
		ocelot->ops->cut_through_fwd(ocelot);
		mutex_unlock(&ocelot->fwd_domain_lock);
	}

	ocelot_fields_write(ocelot, port, QSYS_SWITCH_PORT_MODE_PORT_ENA, 0);

	err = ocelot_port_flush(ocelot, port);
	if (err)
		dev_err(ocelot->dev, "failed to flush port %d: %d\n",
			port, err);

	/* Put the port in reset. */
	if (interface != PHY_INTERFACE_MODE_QSGMII ||
	    !(quirks & OCELOT_QUIRK_QSGMII_PORTS_MUST_BE_UP))
		ocelot_port_rmwl(ocelot_port,
				 DEV_CLOCK_CFG_MAC_TX_RST |
				 DEV_CLOCK_CFG_MAC_RX_RST,
				 DEV_CLOCK_CFG_MAC_TX_RST |
				 DEV_CLOCK_CFG_MAC_RX_RST,
				 DEV_CLOCK_CFG);
}
EXPORT_SYMBOL_GPL(ocelot_phylink_mac_link_down);

void ocelot_phylink_mac_link_up(struct ocelot *ocelot, int port,
				struct phy_device *phydev,
				unsigned int link_an_mode,
				phy_interface_t interface,
				int speed, int duplex,
				bool tx_pause, bool rx_pause,
				unsigned long quirks)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int mac_speed, mode = 0;
	u32 mac_fc_cfg;

	ocelot_port->speed = speed;

	/* The MAC might be integrated in systems where the MAC speed is fixed
	 * and it's the PCS who is performing the rate adaptation, so we have
	 * to write "1000Mbps" into the LINK_SPEED field of DEV_CLOCK_CFG
	 * (which is also its default value).
	 */
	if ((quirks & OCELOT_QUIRK_PCS_PERFORMS_RATE_ADAPTATION) ||
	    speed == SPEED_1000) {
		mac_speed = OCELOT_SPEED_1000;
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA;
	} else if (speed == SPEED_2500) {
		mac_speed = OCELOT_SPEED_2500;
		mode = DEV_MAC_MODE_CFG_GIGA_MODE_ENA;
	} else if (speed == SPEED_100) {
		mac_speed = OCELOT_SPEED_100;
	} else {
		mac_speed = OCELOT_SPEED_10;
	}

	if (duplex == DUPLEX_FULL)
		mode |= DEV_MAC_MODE_CFG_FDX_ENA;

	ocelot_port_writel(ocelot_port, mode, DEV_MAC_MODE_CFG);

	/* Take port out of reset by clearing the MAC_TX_RST, MAC_RX_RST and
	 * PORT_RST bits in DEV_CLOCK_CFG.
	 */
	ocelot_port_writel(ocelot_port, DEV_CLOCK_CFG_LINK_SPEED(mac_speed),
			   DEV_CLOCK_CFG);

	switch (speed) {
	case SPEED_10:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(OCELOT_SPEED_10);
		break;
	case SPEED_100:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(OCELOT_SPEED_100);
		break;
	case SPEED_1000:
	case SPEED_2500:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(OCELOT_SPEED_1000);
		break;
	default:
		dev_err(ocelot->dev, "Unsupported speed on port %d: %d\n",
			port, speed);
		return;
	}

	/* Handle RX pause in all cases, with 2500base-X this is used for rate
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

	/* Don't attempt to send PAUSE frames on the NPI port, it's broken */
	if (port != ocelot->npi)
		ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA,
				    tx_pause);

	/* Undo the effects of ocelot_phylink_mac_link_down:
	 * enable MAC module
	 */
	ocelot_port_writel(ocelot_port, DEV_MAC_ENA_CFG_RX_ENA |
			   DEV_MAC_ENA_CFG_TX_ENA, DEV_MAC_ENA_CFG);

	/* If the port supports cut-through forwarding, update the masks before
	 * enabling forwarding on the port.
	 */
	if (ocelot->ops->cut_through_fwd) {
		mutex_lock(&ocelot->fwd_domain_lock);
		ocelot->ops->cut_through_fwd(ocelot);
		mutex_unlock(&ocelot->fwd_domain_lock);
	}

	/* Core: Enable port for frame transfer */
	ocelot_fields_write(ocelot, port,
			    QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);
}
EXPORT_SYMBOL_GPL(ocelot_phylink_mac_link_up);

static int ocelot_port_add_txtstamp_skb(struct ocelot *ocelot, int port,
					struct sk_buff *clone)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	unsigned long flags;

	spin_lock_irqsave(&ocelot->ts_id_lock, flags);

	if (ocelot_port->ptp_skbs_in_flight == OCELOT_MAX_PTP_ID ||
	    ocelot->ptp_skbs_in_flight == OCELOT_PTP_FIFO_SIZE) {
		spin_unlock_irqrestore(&ocelot->ts_id_lock, flags);
		return -EBUSY;
	}

	skb_shinfo(clone)->tx_flags |= SKBTX_IN_PROGRESS;
	/* Store timestamp ID in OCELOT_SKB_CB(clone)->ts_id */
	OCELOT_SKB_CB(clone)->ts_id = ocelot_port->ts_id;

	ocelot_port->ts_id++;
	if (ocelot_port->ts_id == OCELOT_MAX_PTP_ID)
		ocelot_port->ts_id = 0;

	ocelot_port->ptp_skbs_in_flight++;
	ocelot->ptp_skbs_in_flight++;

	skb_queue_tail(&ocelot_port->tx_skbs, clone);

	spin_unlock_irqrestore(&ocelot->ts_id_lock, flags);

	return 0;
}

static bool ocelot_ptp_is_onestep_sync(struct sk_buff *skb,
				       unsigned int ptp_class)
{
	struct ptp_header *hdr;
	u8 msgtype, twostep;

	hdr = ptp_parse_header(skb, ptp_class);
	if (!hdr)
		return false;

	msgtype = ptp_get_msgtype(hdr, ptp_class);
	twostep = hdr->flag_field[0] & 0x2;

	if (msgtype == PTP_MSGTYPE_SYNC && twostep == 0)
		return true;

	return false;
}

int ocelot_port_txtstamp_request(struct ocelot *ocelot, int port,
				 struct sk_buff *skb,
				 struct sk_buff **clone)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u8 ptp_cmd = ocelot_port->ptp_cmd;
	unsigned int ptp_class;
	int err;

	/* Don't do anything if PTP timestamping not enabled */
	if (!ptp_cmd)
		return 0;

	ptp_class = ptp_classify_raw(skb);
	if (ptp_class == PTP_CLASS_NONE)
		return -EINVAL;

	/* Store ptp_cmd in OCELOT_SKB_CB(skb)->ptp_cmd */
	if (ptp_cmd == IFH_REW_OP_ORIGIN_PTP) {
		if (ocelot_ptp_is_onestep_sync(skb, ptp_class)) {
			OCELOT_SKB_CB(skb)->ptp_cmd = ptp_cmd;
			return 0;
		}

		/* Fall back to two-step timestamping */
		ptp_cmd = IFH_REW_OP_TWO_STEP_PTP;
	}

	if (ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
		*clone = skb_clone_sk(skb);
		if (!(*clone))
			return -ENOMEM;

		err = ocelot_port_add_txtstamp_skb(ocelot, port, *clone);
		if (err)
			return err;

		OCELOT_SKB_CB(skb)->ptp_cmd = ptp_cmd;
		OCELOT_SKB_CB(*clone)->ptp_class = ptp_class;
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_port_txtstamp_request);

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

static bool ocelot_validate_ptp_skb(struct sk_buff *clone, u16 seqid)
{
	struct ptp_header *hdr;

	hdr = ptp_parse_header(clone, OCELOT_SKB_CB(clone)->ptp_class);
	if (WARN_ON(!hdr))
		return false;

	return seqid == ntohs(hdr->sequence_id);
}

void ocelot_get_txtstamp(struct ocelot *ocelot)
{
	int budget = OCELOT_PTP_QUEUE_SZ;

	while (budget--) {
		struct sk_buff *skb, *skb_tmp, *skb_match = NULL;
		struct skb_shared_hwtstamps shhwtstamps;
		u32 val, id, seqid, txport;
		struct ocelot_port *port;
		struct timespec64 ts;
		unsigned long flags;

		val = ocelot_read(ocelot, SYS_PTP_STATUS);

		/* Check if a timestamp can be retrieved */
		if (!(val & SYS_PTP_STATUS_PTP_MESS_VLD))
			break;

		WARN_ON(val & SYS_PTP_STATUS_PTP_OVFL);

		/* Retrieve the ts ID and Tx port */
		id = SYS_PTP_STATUS_PTP_MESS_ID_X(val);
		txport = SYS_PTP_STATUS_PTP_MESS_TXPORT_X(val);
		seqid = SYS_PTP_STATUS_PTP_MESS_SEQ_ID(val);

		port = ocelot->ports[txport];

		spin_lock(&ocelot->ts_id_lock);
		port->ptp_skbs_in_flight--;
		ocelot->ptp_skbs_in_flight--;
		spin_unlock(&ocelot->ts_id_lock);

		/* Retrieve its associated skb */
try_again:
		spin_lock_irqsave(&port->tx_skbs.lock, flags);

		skb_queue_walk_safe(&port->tx_skbs, skb, skb_tmp) {
			if (OCELOT_SKB_CB(skb)->ts_id != id)
				continue;
			__skb_unlink(skb, &port->tx_skbs);
			skb_match = skb;
			break;
		}

		spin_unlock_irqrestore(&port->tx_skbs.lock, flags);

		if (WARN_ON(!skb_match))
			continue;

		if (!ocelot_validate_ptp_skb(skb_match, seqid)) {
			dev_err_ratelimited(ocelot->dev,
					    "port %d received stale TX timestamp for seqid %d, discarding\n",
					    txport, seqid);
			dev_kfree_skb_any(skb);
			goto try_again;
		}

		/* Get the h/w timestamp */
		ocelot_get_hwtimestamp(ocelot, &ts);

		/* Set the timestamp into the skb */
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_complete_tx_timestamp(skb_match, &shhwtstamps);

		/* Next ts */
		ocelot_write(ocelot, SYS_PTP_NXT_PTP_NXT, SYS_PTP_NXT);
	}
}
EXPORT_SYMBOL(ocelot_get_txtstamp);

static int ocelot_rx_frame_word(struct ocelot *ocelot, u8 grp, bool ifh,
				u32 *rval)
{
	u32 bytes_valid, val;

	val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
	if (val == XTR_NOT_READY) {
		if (ifh)
			return -EIO;

		do {
			val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		} while (val == XTR_NOT_READY);
	}

	switch (val) {
	case XTR_ABORT:
		return -EIO;
	case XTR_EOF_0:
	case XTR_EOF_1:
	case XTR_EOF_2:
	case XTR_EOF_3:
	case XTR_PRUNED:
		bytes_valid = XTR_VALID_BYTES(val);
		val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		if (val == XTR_ESCAPE)
			*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		else
			*rval = val;

		return bytes_valid;
	case XTR_ESCAPE:
		*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);

		return 4;
	default:
		*rval = val;

		return 4;
	}
}

static int ocelot_xtr_poll_xfh(struct ocelot *ocelot, int grp, u32 *xfh)
{
	int i, err = 0;

	for (i = 0; i < OCELOT_TAG_LEN / 4; i++) {
		err = ocelot_rx_frame_word(ocelot, grp, true, &xfh[i]);
		if (err != 4)
			return (err < 0) ? err : -EIO;
	}

	return 0;
}

void ocelot_ptp_rx_timestamp(struct ocelot *ocelot, struct sk_buff *skb,
			     u64 timestamp)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	u64 tod_in_ns, full_ts_in_ns;
	struct timespec64 ts;

	ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);

	tod_in_ns = ktime_set(ts.tv_sec, ts.tv_nsec);
	if ((tod_in_ns & 0xffffffff) < timestamp)
		full_ts_in_ns = (((tod_in_ns >> 32) - 1) << 32) |
				timestamp;
	else
		full_ts_in_ns = (tod_in_ns & GENMASK_ULL(63, 32)) |
				timestamp;

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = full_ts_in_ns;
}
EXPORT_SYMBOL(ocelot_ptp_rx_timestamp);

int ocelot_xtr_poll_frame(struct ocelot *ocelot, int grp, struct sk_buff **nskb)
{
	u64 timestamp, src_port, len;
	u32 xfh[OCELOT_TAG_LEN / 4];
	struct net_device *dev;
	struct sk_buff *skb;
	int sz, buf_len;
	u32 val, *buf;
	int err;

	err = ocelot_xtr_poll_xfh(ocelot, grp, xfh);
	if (err)
		return err;

	ocelot_xfh_get_src_port(xfh, &src_port);
	ocelot_xfh_get_len(xfh, &len);
	ocelot_xfh_get_rew_val(xfh, &timestamp);

	if (WARN_ON(src_port >= ocelot->num_phys_ports))
		return -EINVAL;

	dev = ocelot->ops->port_to_netdev(ocelot, src_port);
	if (!dev)
		return -EINVAL;

	skb = netdev_alloc_skb(dev, len);
	if (unlikely(!skb)) {
		netdev_err(dev, "Unable to allocate sk_buff\n");
		return -ENOMEM;
	}

	buf_len = len - ETH_FCS_LEN;
	buf = (u32 *)skb_put(skb, buf_len);

	len = 0;
	do {
		sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
		if (sz < 0) {
			err = sz;
			goto out_free_skb;
		}
		*buf++ = val;
		len += sz;
	} while (len < buf_len);

	/* Read the FCS */
	sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
	if (sz < 0) {
		err = sz;
		goto out_free_skb;
	}

	/* Update the statistics if part of the FCS was read before */
	len -= ETH_FCS_LEN - sz;

	if (unlikely(dev->features & NETIF_F_RXFCS)) {
		buf = (u32 *)skb_put(skb, ETH_FCS_LEN);
		*buf = val;
	}

	if (ocelot->ptp)
		ocelot_ptp_rx_timestamp(ocelot, skb, timestamp);

	/* Everything we see on an interface that is in the HW bridge
	 * has already been forwarded.
	 */
	if (ocelot->ports[src_port]->bridge)
		skb->offload_fwd_mark = 1;

	skb->protocol = eth_type_trans(skb, dev);

	*nskb = skb;

	return 0;

out_free_skb:
	kfree_skb(skb);
	return err;
}
EXPORT_SYMBOL(ocelot_xtr_poll_frame);

bool ocelot_can_inject(struct ocelot *ocelot, int grp)
{
	u32 val = ocelot_read(ocelot, QS_INJ_STATUS);

	if (!(val & QS_INJ_STATUS_FIFO_RDY(BIT(grp))))
		return false;
	if (val & QS_INJ_STATUS_WMARK_REACHED(BIT(grp)))
		return false;

	return true;
}
EXPORT_SYMBOL(ocelot_can_inject);

void ocelot_ifh_port_set(void *ifh, int port, u32 rew_op, u32 vlan_tag)
{
	ocelot_ifh_set_bypass(ifh, 1);
	ocelot_ifh_set_dest(ifh, BIT_ULL(port));
	ocelot_ifh_set_tag_type(ifh, IFH_TAG_TYPE_C);
	if (vlan_tag)
		ocelot_ifh_set_vlan_tci(ifh, vlan_tag);
	if (rew_op)
		ocelot_ifh_set_rew_op(ifh, rew_op);
}
EXPORT_SYMBOL(ocelot_ifh_port_set);

void ocelot_port_inject_frame(struct ocelot *ocelot, int port, int grp,
			      u32 rew_op, struct sk_buff *skb)
{
	u32 ifh[OCELOT_TAG_LEN / 4] = {0};
	unsigned int i, count, last;

	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_SOF, QS_INJ_CTRL, grp);

	ocelot_ifh_port_set(ifh, port, rew_op, skb_vlan_tag_get(skb));

	for (i = 0; i < OCELOT_TAG_LEN / 4; i++)
		ocelot_write_rix(ocelot, ifh[i], QS_INJ_WR, grp);

	count = DIV_ROUND_UP(skb->len, 4);
	last = skb->len % 4;
	for (i = 0; i < count; i++)
		ocelot_write_rix(ocelot, ((u32 *)skb->data)[i], QS_INJ_WR, grp);

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

	skb->dev->stats.tx_packets++;
	skb->dev->stats.tx_bytes += skb->len;
}
EXPORT_SYMBOL(ocelot_port_inject_frame);

void ocelot_drain_cpu_queue(struct ocelot *ocelot, int grp)
{
	while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp))
		ocelot_read_rix(ocelot, QS_XTR_RD, grp);
}
EXPORT_SYMBOL(ocelot_drain_cpu_queue);

int ocelot_fdb_add(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid)
{
	int pgid = port;

	if (port == ocelot->npi)
		pgid = PGID_CPU;

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

/* Caller must hold &ocelot->mact_lock */
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

int ocelot_mact_flush(struct ocelot *ocelot, int port)
{
	int err;

	mutex_lock(&ocelot->mact_lock);

	/* Program ageing filter for a single port */
	ocelot_write(ocelot, ANA_ANAGEFIL_PID_EN | ANA_ANAGEFIL_PID_VAL(port),
		     ANA_ANAGEFIL);

	/* Flushing dynamic FDB entries requires two successive age scans */
	ocelot_write(ocelot,
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_AGE),
		     ANA_TABLES_MACACCESS);

	err = ocelot_mact_wait_for_completion(ocelot);
	if (err) {
		mutex_unlock(&ocelot->mact_lock);
		return err;
	}

	/* And second... */
	ocelot_write(ocelot,
		     ANA_TABLES_MACACCESS_MAC_TABLE_CMD(MACACCESS_CMD_AGE),
		     ANA_TABLES_MACACCESS);

	err = ocelot_mact_wait_for_completion(ocelot);

	/* Restore ageing filter */
	ocelot_write(ocelot, 0, ANA_ANAGEFIL);

	mutex_unlock(&ocelot->mact_lock);

	return err;
}
EXPORT_SYMBOL_GPL(ocelot_mact_flush);

int ocelot_fdb_dump(struct ocelot *ocelot, int port,
		    dsa_fdb_dump_cb_t *cb, void *data)
{
	int err = 0;
	int i, j;

	/* We could take the lock just around ocelot_mact_read, but doing so
	 * thousands of times in a row seems rather pointless and inefficient.
	 */
	mutex_lock(&ocelot->mact_lock);

	/* Loop through all the mac tables entries. */
	for (i = 0; i < ocelot->num_mact_rows; i++) {
		for (j = 0; j < 4; j++) {
			struct ocelot_mact_entry entry;
			bool is_static;

			err = ocelot_mact_read(ocelot, port, i, j, &entry);
			/* If the entry is invalid (wrong port, invalid...),
			 * skip it.
			 */
			if (err == -EINVAL)
				continue;
			else if (err)
				break;

			is_static = (entry.type == ENTRYTYPE_LOCKED);

			err = cb(entry.mac, entry.vid, is_static, data);
			if (err)
				break;
		}
	}

	mutex_unlock(&ocelot->mact_lock);

	return err;
}
EXPORT_SYMBOL(ocelot_fdb_dump);

static void ocelot_populate_l2_ptp_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_ETYPE;
	*(__be16 *)trap->key.etype.etype.value = htons(ETH_P_1588);
	*(__be16 *)trap->key.etype.etype.mask = htons(0xffff);
}

static void
ocelot_populate_ipv4_ptp_event_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV4;
	trap->key.ipv4.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv4.proto.mask[0] = 0xff;
	trap->key.ipv4.dport.value = PTP_EV_PORT;
	trap->key.ipv4.dport.mask = 0xffff;
}

static void
ocelot_populate_ipv6_ptp_event_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV6;
	trap->key.ipv4.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv4.proto.mask[0] = 0xff;
	trap->key.ipv6.dport.value = PTP_EV_PORT;
	trap->key.ipv6.dport.mask = 0xffff;
}

static void
ocelot_populate_ipv4_ptp_general_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV4;
	trap->key.ipv4.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv4.proto.mask[0] = 0xff;
	trap->key.ipv4.dport.value = PTP_GEN_PORT;
	trap->key.ipv4.dport.mask = 0xffff;
}

static void
ocelot_populate_ipv6_ptp_general_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV6;
	trap->key.ipv4.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv4.proto.mask[0] = 0xff;
	trap->key.ipv6.dport.value = PTP_GEN_PORT;
	trap->key.ipv6.dport.mask = 0xffff;
}

int ocelot_trap_add(struct ocelot *ocelot, int port,
		    unsigned long cookie, bool take_ts,
		    void (*populate)(struct ocelot_vcap_filter *f))
{
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot_vcap_filter *trap;
	bool new = false;
	int err;

	block_vcap_is2 = &ocelot->block[VCAP_IS2];

	trap = ocelot_vcap_block_find_filter_by_id(block_vcap_is2, cookie,
						   false);
	if (!trap) {
		trap = kzalloc(sizeof(*trap), GFP_KERNEL);
		if (!trap)
			return -ENOMEM;

		populate(trap);
		trap->prio = 1;
		trap->id.cookie = cookie;
		trap->id.tc_offload = false;
		trap->block_id = VCAP_IS2;
		trap->type = OCELOT_VCAP_FILTER_OFFLOAD;
		trap->lookup = 0;
		trap->action.cpu_copy_ena = true;
		trap->action.mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
		trap->action.port_mask = 0;
		trap->take_ts = take_ts;
		list_add_tail(&trap->trap_list, &ocelot->traps);
		new = true;
	}

	trap->ingress_port_mask |= BIT(port);

	if (new)
		err = ocelot_vcap_filter_add(ocelot, trap, NULL);
	else
		err = ocelot_vcap_filter_replace(ocelot, trap);
	if (err) {
		trap->ingress_port_mask &= ~BIT(port);
		if (!trap->ingress_port_mask) {
			list_del(&trap->trap_list);
			kfree(trap);
		}
		return err;
	}

	return 0;
}

int ocelot_trap_del(struct ocelot *ocelot, int port, unsigned long cookie)
{
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot_vcap_filter *trap;

	block_vcap_is2 = &ocelot->block[VCAP_IS2];

	trap = ocelot_vcap_block_find_filter_by_id(block_vcap_is2, cookie,
						   false);
	if (!trap)
		return 0;

	trap->ingress_port_mask &= ~BIT(port);
	if (!trap->ingress_port_mask) {
		list_del(&trap->trap_list);

		return ocelot_vcap_filter_del(ocelot, trap);
	}

	return ocelot_vcap_filter_replace(ocelot, trap);
}

static int ocelot_l2_ptp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long l2_cookie = OCELOT_VCAP_IS2_L2_PTP_TRAP(ocelot);

	return ocelot_trap_add(ocelot, port, l2_cookie, true,
			       ocelot_populate_l2_ptp_trap_key);
}

static int ocelot_l2_ptp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long l2_cookie = OCELOT_VCAP_IS2_L2_PTP_TRAP(ocelot);

	return ocelot_trap_del(ocelot, port, l2_cookie);
}

static int ocelot_ipv4_ptp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long ipv4_gen_cookie = OCELOT_VCAP_IS2_IPV4_GEN_PTP_TRAP(ocelot);
	unsigned long ipv4_ev_cookie = OCELOT_VCAP_IS2_IPV4_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_add(ocelot, port, ipv4_ev_cookie, true,
			      ocelot_populate_ipv4_ptp_event_trap_key);
	if (err)
		return err;

	err = ocelot_trap_add(ocelot, port, ipv4_gen_cookie, false,
			      ocelot_populate_ipv4_ptp_general_trap_key);
	if (err)
		ocelot_trap_del(ocelot, port, ipv4_ev_cookie);

	return err;
}

static int ocelot_ipv4_ptp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long ipv4_gen_cookie = OCELOT_VCAP_IS2_IPV4_GEN_PTP_TRAP(ocelot);
	unsigned long ipv4_ev_cookie = OCELOT_VCAP_IS2_IPV4_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_del(ocelot, port, ipv4_ev_cookie);
	err |= ocelot_trap_del(ocelot, port, ipv4_gen_cookie);
	return err;
}

static int ocelot_ipv6_ptp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long ipv6_gen_cookie = OCELOT_VCAP_IS2_IPV6_GEN_PTP_TRAP(ocelot);
	unsigned long ipv6_ev_cookie = OCELOT_VCAP_IS2_IPV6_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_add(ocelot, port, ipv6_ev_cookie, true,
			      ocelot_populate_ipv6_ptp_event_trap_key);
	if (err)
		return err;

	err = ocelot_trap_add(ocelot, port, ipv6_gen_cookie, false,
			      ocelot_populate_ipv6_ptp_general_trap_key);
	if (err)
		ocelot_trap_del(ocelot, port, ipv6_ev_cookie);

	return err;
}

static int ocelot_ipv6_ptp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long ipv6_gen_cookie = OCELOT_VCAP_IS2_IPV6_GEN_PTP_TRAP(ocelot);
	unsigned long ipv6_ev_cookie = OCELOT_VCAP_IS2_IPV6_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_del(ocelot, port, ipv6_ev_cookie);
	err |= ocelot_trap_del(ocelot, port, ipv6_gen_cookie);
	return err;
}

static int ocelot_setup_ptp_traps(struct ocelot *ocelot, int port,
				  bool l2, bool l4)
{
	int err;

	if (l2)
		err = ocelot_l2_ptp_trap_add(ocelot, port);
	else
		err = ocelot_l2_ptp_trap_del(ocelot, port);
	if (err)
		return err;

	if (l4) {
		err = ocelot_ipv4_ptp_trap_add(ocelot, port);
		if (err)
			goto err_ipv4;

		err = ocelot_ipv6_ptp_trap_add(ocelot, port);
		if (err)
			goto err_ipv6;
	} else {
		err = ocelot_ipv4_ptp_trap_del(ocelot, port);

		err |= ocelot_ipv6_ptp_trap_del(ocelot, port);
	}
	if (err)
		return err;

	return 0;

err_ipv6:
	ocelot_ipv4_ptp_trap_del(ocelot, port);
err_ipv4:
	if (l2)
		ocelot_l2_ptp_trap_del(ocelot, port);
	return err;
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
	bool l2 = false, l4 = false;
	struct hwtstamp_config cfg;
	int err;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

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
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		l2 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		l2 = true;
		l4 = true;
		break;
	default:
		mutex_unlock(&ocelot->ptp_lock);
		return -ERANGE;
	}

	err = ocelot_setup_ptp_traps(ocelot, port, l2, l4);
	if (err) {
		mutex_unlock(&ocelot->ptp_lock);
		return err;
	}

	if (l2 && l4)
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
	else if (l2)
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
	else if (l4)
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
	else
		cfg.rx_filter = HWTSTAMP_FILTER_NONE;

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

/* Caller must hold &ocelot->stats_lock */
static int ocelot_port_update_stats(struct ocelot *ocelot, int port)
{
	unsigned int idx = port * ocelot->num_stats;
	struct ocelot_stats_region *region;
	int err, j;

	/* Configure the port to read the stats from */
	ocelot_write(ocelot, SYS_STAT_CFG_STAT_VIEW(port), SYS_STAT_CFG);

	list_for_each_entry(region, &ocelot->stats_regions, node) {
		err = ocelot_bulk_read_rix(ocelot, SYS_COUNT_RX_OCTETS,
					   region->offset, region->buf,
					   region->count);
		if (err)
			return err;

		for (j = 0; j < region->count; j++) {
			u64 *stat = &ocelot->stats[idx + j];
			u64 val = region->buf[j];

			if (val < (*stat & U32_MAX))
				*stat += (u64)1 << 32;

			*stat = (*stat & ~(u64)U32_MAX) + val;
		}

		idx += region->count;
	}

	return err;
}

static void ocelot_check_stats_work(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct ocelot *ocelot = container_of(del_work, struct ocelot,
					     stats_work);
	int i, err;

	mutex_lock(&ocelot->stats_lock);
	for (i = 0; i < ocelot->num_phys_ports; i++) {
		err = ocelot_port_update_stats(ocelot, i);
		if (err)
			break;
	}
	mutex_unlock(&ocelot->stats_lock);

	if (err)
		dev_err(ocelot->dev, "Error %d updating ethtool stats\n",  err);

	queue_delayed_work(ocelot->stats_queue, &ocelot->stats_work,
			   OCELOT_STATS_CHECK_DELAY);
}

void ocelot_get_ethtool_stats(struct ocelot *ocelot, int port, u64 *data)
{
	int i, err;

	mutex_lock(&ocelot->stats_lock);

	/* check and update now */
	err = ocelot_port_update_stats(ocelot, port);

	/* Copy all counters */
	for (i = 0; i < ocelot->num_stats; i++)
		*data++ = ocelot->stats[port * ocelot->num_stats + i];

	mutex_unlock(&ocelot->stats_lock);

	if (err)
		dev_err(ocelot->dev, "Error %d updating ethtool stats\n", err);
}
EXPORT_SYMBOL(ocelot_get_ethtool_stats);

int ocelot_get_sset_count(struct ocelot *ocelot, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return ocelot->num_stats;
}
EXPORT_SYMBOL(ocelot_get_sset_count);

static int ocelot_prepare_stats_regions(struct ocelot *ocelot)
{
	struct ocelot_stats_region *region = NULL;
	unsigned int last;
	int i;

	INIT_LIST_HEAD(&ocelot->stats_regions);

	for (i = 0; i < ocelot->num_stats; i++) {
		if (region && ocelot->stats_layout[i].offset == last + 1) {
			region->count++;
		} else {
			region = devm_kzalloc(ocelot->dev, sizeof(*region),
					      GFP_KERNEL);
			if (!region)
				return -ENOMEM;

			region->offset = ocelot->stats_layout[i].offset;
			region->count = 1;
			list_add_tail(&region->node, &ocelot->stats_regions);
		}

		last = ocelot->stats_layout[i].offset;
	}

	list_for_each_entry(region, &ocelot->stats_regions, node) {
		region->buf = devm_kcalloc(ocelot->dev, region->count,
					   sizeof(*region->buf), GFP_KERNEL);
		if (!region->buf)
			return -ENOMEM;
	}

	return 0;
}

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
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT);

	return 0;
}
EXPORT_SYMBOL(ocelot_get_ts_info);

static u32 ocelot_get_bond_mask(struct ocelot *ocelot, struct net_device *bond)
{
	u32 mask = 0;
	int port;

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (!ocelot_port)
			continue;

		if (ocelot_port->bond == bond)
			mask |= BIT(port);
	}

	return mask;
}

/* The logical port number of a LAG is equal to the lowest numbered physical
 * port ID present in that LAG. It may change if that port ever leaves the LAG.
 */
static int ocelot_bond_get_id(struct ocelot *ocelot, struct net_device *bond)
{
	int bond_mask = ocelot_get_bond_mask(ocelot, bond);

	if (!bond_mask)
		return -ENOENT;

	return __ffs(bond_mask);
}

u32 ocelot_get_bridge_fwd_mask(struct ocelot *ocelot, int src_port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[src_port];
	const struct net_device *bridge;
	u32 mask = 0;
	int port;

	if (!ocelot_port || ocelot_port->stp_state != BR_STATE_FORWARDING)
		return 0;

	bridge = ocelot_port->bridge;
	if (!bridge)
		return 0;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		ocelot_port = ocelot->ports[port];

		if (!ocelot_port)
			continue;

		if (ocelot_port->stp_state == BR_STATE_FORWARDING &&
		    ocelot_port->bridge == bridge)
			mask |= BIT(port);
	}

	return mask;
}
EXPORT_SYMBOL_GPL(ocelot_get_bridge_fwd_mask);

u32 ocelot_get_dsa_8021q_cpu_mask(struct ocelot *ocelot)
{
	u32 mask = 0;
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (!ocelot_port)
			continue;

		if (ocelot_port->is_dsa_8021q_cpu)
			mask |= BIT(port);
	}

	return mask;
}
EXPORT_SYMBOL_GPL(ocelot_get_dsa_8021q_cpu_mask);

void ocelot_apply_bridge_fwd_mask(struct ocelot *ocelot, bool joining)
{
	unsigned long cpu_fwd_mask;
	int port;

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	/* If cut-through forwarding is supported, update the masks before a
	 * port joins the forwarding domain, to avoid potential underruns if it
	 * has the highest speed from the new domain.
	 */
	if (joining && ocelot->ops->cut_through_fwd)
		ocelot->ops->cut_through_fwd(ocelot);

	/* If a DSA tag_8021q CPU exists, it needs to be included in the
	 * regular forwarding path of the front ports regardless of whether
	 * those are bridged or standalone.
	 * If DSA tag_8021q is not used, this returns 0, which is fine because
	 * the hardware-based CPU port module can be a destination for packets
	 * even if it isn't part of PGID_SRC.
	 */
	cpu_fwd_mask = ocelot_get_dsa_8021q_cpu_mask(ocelot);

	/* Apply FWD mask. The loop is needed to add/remove the current port as
	 * a source for the other ports.
	 */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];
		unsigned long mask;

		if (!ocelot_port) {
			/* Unused ports can't send anywhere */
			mask = 0;
		} else if (ocelot_port->is_dsa_8021q_cpu) {
			/* The DSA tag_8021q CPU ports need to be able to
			 * forward packets to all other ports except for
			 * themselves
			 */
			mask = GENMASK(ocelot->num_phys_ports - 1, 0);
			mask &= ~cpu_fwd_mask;
		} else if (ocelot_port->bridge) {
			struct net_device *bond = ocelot_port->bond;

			mask = ocelot_get_bridge_fwd_mask(ocelot, port);
			mask |= cpu_fwd_mask;
			mask &= ~BIT(port);
			if (bond)
				mask &= ~ocelot_get_bond_mask(ocelot, bond);
		} else {
			/* Standalone ports forward only to DSA tag_8021q CPU
			 * ports (if those exist), or to the hardware CPU port
			 * module otherwise.
			 */
			mask = cpu_fwd_mask;
		}

		ocelot_write_rix(ocelot, mask, ANA_PGID_PGID, PGID_SRC + port);
	}

	/* If cut-through forwarding is supported and a port is leaving, there
	 * is a chance that cut-through was disabled on the other ports due to
	 * the port which is leaving (it has a higher link speed). We need to
	 * update the cut-through masks of the remaining ports no earlier than
	 * after the port has left, to prevent underruns from happening between
	 * the cut-through update and the forwarding domain update.
	 */
	if (!joining && ocelot->ops->cut_through_fwd)
		ocelot->ops->cut_through_fwd(ocelot);
}
EXPORT_SYMBOL(ocelot_apply_bridge_fwd_mask);

void ocelot_bridge_stp_state_set(struct ocelot *ocelot, int port, u8 state)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u32 learn_ena = 0;

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot_port->stp_state = state;

	if ((state == BR_STATE_LEARNING || state == BR_STATE_FORWARDING) &&
	    ocelot_port->learn_ena)
		learn_ena = ANA_PORT_PORT_CFG_LEARN_ENA;

	ocelot_rmw_gix(ocelot, learn_ena, ANA_PORT_PORT_CFG_LEARN_ENA,
		       ANA_PORT_PORT_CFG, port);

	ocelot_apply_bridge_fwd_mask(ocelot, state == BR_STATE_FORWARDING);

	mutex_unlock(&ocelot->fwd_domain_lock);
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
	return ENTRYTYPE_LOCKED;
}

static struct ocelot_pgid *ocelot_pgid_alloc(struct ocelot *ocelot, int index,
					     unsigned long ports)
{
	struct ocelot_pgid *pgid;

	pgid = kzalloc(sizeof(*pgid), GFP_KERNEL);
	if (!pgid)
		return ERR_PTR(-ENOMEM);

	pgid->ports = ports;
	pgid->index = index;
	refcount_set(&pgid->refcount, 1);
	list_add_tail(&pgid->list, &ocelot->pgids);

	return pgid;
}

static void ocelot_pgid_free(struct ocelot *ocelot, struct ocelot_pgid *pgid)
{
	if (!refcount_dec_and_test(&pgid->refcount))
		return;

	list_del(&pgid->list);
	kfree(pgid);
}

static struct ocelot_pgid *ocelot_mdb_get_pgid(struct ocelot *ocelot,
					       const struct ocelot_multicast *mc)
{
	struct ocelot_pgid *pgid;
	int index;

	/* According to VSC7514 datasheet 3.9.1.5 IPv4 Multicast Entries and
	 * 3.9.1.6 IPv6 Multicast Entries, "Instead of a lookup in the
	 * destination mask table (PGID), the destination set is programmed as
	 * part of the entry MAC address.", and the DEST_IDX is set to 0.
	 */
	if (mc->entry_type == ENTRYTYPE_MACv4 ||
	    mc->entry_type == ENTRYTYPE_MACv6)
		return ocelot_pgid_alloc(ocelot, 0, mc->ports);

	list_for_each_entry(pgid, &ocelot->pgids, list) {
		/* When searching for a nonreserved multicast PGID, ignore the
		 * dummy PGID of zero that we have for MACv4/MACv6 entries
		 */
		if (pgid->index && pgid->ports == mc->ports) {
			refcount_inc(&pgid->refcount);
			return pgid;
		}
	}

	/* Search for a free index in the nonreserved multicast PGID area */
	for_each_nonreserved_multicast_dest_pgid(ocelot, index) {
		bool used = false;

		list_for_each_entry(pgid, &ocelot->pgids, list) {
			if (pgid->index == index) {
				used = true;
				break;
			}
		}

		if (!used)
			return ocelot_pgid_alloc(ocelot, index, mc->ports);
	}

	return ERR_PTR(-ENOSPC);
}

static void ocelot_encode_ports_to_mdb(unsigned char *addr,
				       struct ocelot_multicast *mc)
{
	ether_addr_copy(addr, mc->addr);

	if (mc->entry_type == ENTRYTYPE_MACv4) {
		addr[0] = 0;
		addr[1] = mc->ports >> 8;
		addr[2] = mc->ports & 0xff;
	} else if (mc->entry_type == ENTRYTYPE_MACv6) {
		addr[0] = mc->ports >> 8;
		addr[1] = mc->ports & 0xff;
	}
}

int ocelot_port_mdb_add(struct ocelot *ocelot, int port,
			const struct switchdev_obj_port_mdb *mdb)
{
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	struct ocelot_pgid *pgid;
	u16 vid = mdb->vid;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc) {
		/* New entry */
		mc = devm_kzalloc(ocelot->dev, sizeof(*mc), GFP_KERNEL);
		if (!mc)
			return -ENOMEM;

		mc->entry_type = ocelot_classify_mdb(mdb->addr);
		ether_addr_copy(mc->addr, mdb->addr);
		mc->vid = vid;

		list_add_tail(&mc->list, &ocelot->multicast);
	} else {
		/* Existing entry. Clean up the current port mask from
		 * hardware now, because we'll be modifying it.
		 */
		ocelot_pgid_free(ocelot, mc->pgid);
		ocelot_encode_ports_to_mdb(addr, mc);
		ocelot_mact_forget(ocelot, addr, vid);
	}

	mc->ports |= BIT(port);

	pgid = ocelot_mdb_get_pgid(ocelot, mc);
	if (IS_ERR(pgid)) {
		dev_err(ocelot->dev,
			"Cannot allocate PGID for mdb %pM vid %d\n",
			mc->addr, mc->vid);
		devm_kfree(ocelot->dev, mc);
		return PTR_ERR(pgid);
	}
	mc->pgid = pgid;

	ocelot_encode_ports_to_mdb(addr, mc);

	if (mc->entry_type != ENTRYTYPE_MACv4 &&
	    mc->entry_type != ENTRYTYPE_MACv6)
		ocelot_write_rix(ocelot, pgid->ports, ANA_PGID_PGID,
				 pgid->index);

	return ocelot_mact_learn(ocelot, pgid->index, addr, vid,
				 mc->entry_type);
}
EXPORT_SYMBOL(ocelot_port_mdb_add);

int ocelot_port_mdb_del(struct ocelot *ocelot, int port,
			const struct switchdev_obj_port_mdb *mdb)
{
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	struct ocelot_pgid *pgid;
	u16 vid = mdb->vid;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	mc = ocelot_multicast_get(ocelot, mdb->addr, vid);
	if (!mc)
		return -ENOENT;

	ocelot_encode_ports_to_mdb(addr, mc);
	ocelot_mact_forget(ocelot, addr, vid);

	ocelot_pgid_free(ocelot, mc->pgid);
	mc->ports &= ~BIT(port);
	if (!mc->ports) {
		list_del(&mc->list);
		devm_kfree(ocelot->dev, mc);
		return 0;
	}

	/* We have a PGID with fewer ports now */
	pgid = ocelot_mdb_get_pgid(ocelot, mc);
	if (IS_ERR(pgid))
		return PTR_ERR(pgid);
	mc->pgid = pgid;

	ocelot_encode_ports_to_mdb(addr, mc);

	if (mc->entry_type != ENTRYTYPE_MACv4 &&
	    mc->entry_type != ENTRYTYPE_MACv6)
		ocelot_write_rix(ocelot, pgid->ports, ANA_PGID_PGID,
				 pgid->index);

	return ocelot_mact_learn(ocelot, pgid->index, addr, vid,
				 mc->entry_type);
}
EXPORT_SYMBOL(ocelot_port_mdb_del);

void ocelot_port_bridge_join(struct ocelot *ocelot, int port,
			     struct net_device *bridge)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot_port->bridge = bridge;

	ocelot_apply_bridge_fwd_mask(ocelot, true);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL(ocelot_port_bridge_join);

void ocelot_port_bridge_leave(struct ocelot *ocelot, int port,
			      struct net_device *bridge)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot_port->bridge = NULL;

	ocelot_port_set_pvid(ocelot, port, NULL);
	ocelot_port_manage_port_tag(ocelot, port);
	ocelot_apply_bridge_fwd_mask(ocelot, false);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL(ocelot_port_bridge_leave);

static void ocelot_set_aggr_pgids(struct ocelot *ocelot)
{
	unsigned long visited = GENMASK(ocelot->num_phys_ports - 1, 0);
	int i, port, lag;

	/* Reset destination and aggregation PGIDS */
	for_each_unicast_dest_pgid(ocelot, port)
		ocelot_write_rix(ocelot, BIT(port), ANA_PGID_PGID, port);

	for_each_aggr_pgid(ocelot, i)
		ocelot_write_rix(ocelot, GENMASK(ocelot->num_phys_ports - 1, 0),
				 ANA_PGID_PGID, i);

	/* The visited ports bitmask holds the list of ports offloading any
	 * bonding interface. Initially we mark all these ports as unvisited,
	 * then every time we visit a port in this bitmask, we know that it is
	 * the lowest numbered port, i.e. the one whose logical ID == physical
	 * port ID == LAG ID. So we mark as visited all further ports in the
	 * bitmask that are offloading the same bonding interface. This way,
	 * we set up the aggregation PGIDs only once per bonding interface.
	 */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (!ocelot_port || !ocelot_port->bond)
			continue;

		visited &= ~BIT(port);
	}

	/* Now, set PGIDs for each active LAG */
	for (lag = 0; lag < ocelot->num_phys_ports; lag++) {
		struct net_device *bond = ocelot->ports[lag]->bond;
		int num_active_ports = 0;
		unsigned long bond_mask;
		u8 aggr_idx[16];

		if (!bond || (visited & BIT(lag)))
			continue;

		bond_mask = ocelot_get_bond_mask(ocelot, bond);

		for_each_set_bit(port, &bond_mask, ocelot->num_phys_ports) {
			struct ocelot_port *ocelot_port = ocelot->ports[port];

			// Destination mask
			ocelot_write_rix(ocelot, bond_mask,
					 ANA_PGID_PGID, port);

			if (ocelot_port->lag_tx_active)
				aggr_idx[num_active_ports++] = port;
		}

		for_each_aggr_pgid(ocelot, i) {
			u32 ac;

			ac = ocelot_read_rix(ocelot, ANA_PGID_PGID, i);
			ac &= ~bond_mask;
			/* Don't do division by zero if there was no active
			 * port. Just make all aggregation codes zero.
			 */
			if (num_active_ports)
				ac |= BIT(aggr_idx[i % num_active_ports]);
			ocelot_write_rix(ocelot, ac, ANA_PGID_PGID, i);
		}

		/* Mark all ports in the same LAG as visited to avoid applying
		 * the same config again.
		 */
		for (port = lag; port < ocelot->num_phys_ports; port++) {
			struct ocelot_port *ocelot_port = ocelot->ports[port];

			if (!ocelot_port)
				continue;

			if (ocelot_port->bond == bond)
				visited |= BIT(port);
		}
	}
}

/* When offloading a bonding interface, the switch ports configured under the
 * same bond must have the same logical port ID, equal to the physical port ID
 * of the lowest numbered physical port in that bond. Otherwise, in standalone/
 * bridged mode, each port has a logical port ID equal to its physical port ID.
 */
static void ocelot_setup_logical_port_ids(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];
		struct net_device *bond;

		if (!ocelot_port)
			continue;

		bond = ocelot_port->bond;
		if (bond) {
			int lag = ocelot_bond_get_id(ocelot, bond);

			ocelot_rmw_gix(ocelot,
				       ANA_PORT_PORT_CFG_PORTID_VAL(lag),
				       ANA_PORT_PORT_CFG_PORTID_VAL_M,
				       ANA_PORT_PORT_CFG, port);
		} else {
			ocelot_rmw_gix(ocelot,
				       ANA_PORT_PORT_CFG_PORTID_VAL(port),
				       ANA_PORT_PORT_CFG_PORTID_VAL_M,
				       ANA_PORT_PORT_CFG, port);
		}
	}
}

/* Documentation for PORTID_VAL says:
 *     Logical port number for front port. If port is not a member of a LLAG,
 *     then PORTID must be set to the physical port number.
 *     If port is a member of a LLAG, then PORTID must be set to the common
 *     PORTID_VAL used for all member ports of the LLAG.
 *     The value must not exceed the number of physical ports on the device.
 *
 * This means we have little choice but to migrate FDB entries pointing towards
 * a logical port when that changes.
 */
static void ocelot_migrate_lag_fdbs(struct ocelot *ocelot,
				    struct net_device *bond,
				    int lag)
{
	struct ocelot_lag_fdb *fdb;
	int err;

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	list_for_each_entry(fdb, &ocelot->lag_fdbs, list) {
		if (fdb->bond != bond)
			continue;

		err = ocelot_mact_forget(ocelot, fdb->addr, fdb->vid);
		if (err) {
			dev_err(ocelot->dev,
				"failed to delete LAG %s FDB %pM vid %d: %pe\n",
				bond->name, fdb->addr, fdb->vid, ERR_PTR(err));
		}

		err = ocelot_mact_learn(ocelot, lag, fdb->addr, fdb->vid,
					ENTRYTYPE_LOCKED);
		if (err) {
			dev_err(ocelot->dev,
				"failed to migrate LAG %s FDB %pM vid %d: %pe\n",
				bond->name, fdb->addr, fdb->vid, ERR_PTR(err));
		}
	}
}

int ocelot_port_lag_join(struct ocelot *ocelot, int port,
			 struct net_device *bond,
			 struct netdev_lag_upper_info *info)
{
	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH)
		return -EOPNOTSUPP;

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot->ports[port]->bond = bond;

	ocelot_setup_logical_port_ids(ocelot);
	ocelot_apply_bridge_fwd_mask(ocelot, true);
	ocelot_set_aggr_pgids(ocelot);

	mutex_unlock(&ocelot->fwd_domain_lock);

	return 0;
}
EXPORT_SYMBOL(ocelot_port_lag_join);

void ocelot_port_lag_leave(struct ocelot *ocelot, int port,
			   struct net_device *bond)
{
	int old_lag_id, new_lag_id;

	mutex_lock(&ocelot->fwd_domain_lock);

	old_lag_id = ocelot_bond_get_id(ocelot, bond);

	ocelot->ports[port]->bond = NULL;

	ocelot_setup_logical_port_ids(ocelot);
	ocelot_apply_bridge_fwd_mask(ocelot, false);
	ocelot_set_aggr_pgids(ocelot);

	new_lag_id = ocelot_bond_get_id(ocelot, bond);

	if (new_lag_id >= 0 && old_lag_id != new_lag_id)
		ocelot_migrate_lag_fdbs(ocelot, bond, new_lag_id);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL(ocelot_port_lag_leave);

void ocelot_port_lag_change(struct ocelot *ocelot, int port, bool lag_tx_active)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot_port->lag_tx_active = lag_tx_active;

	/* Rebalance the LAGs */
	ocelot_set_aggr_pgids(ocelot);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL(ocelot_port_lag_change);

int ocelot_lag_fdb_add(struct ocelot *ocelot, struct net_device *bond,
		       const unsigned char *addr, u16 vid)
{
	struct ocelot_lag_fdb *fdb;
	int lag, err;

	fdb = kzalloc(sizeof(*fdb), GFP_KERNEL);
	if (!fdb)
		return -ENOMEM;

	ether_addr_copy(fdb->addr, addr);
	fdb->vid = vid;
	fdb->bond = bond;

	mutex_lock(&ocelot->fwd_domain_lock);
	lag = ocelot_bond_get_id(ocelot, bond);

	err = ocelot_mact_learn(ocelot, lag, addr, vid, ENTRYTYPE_LOCKED);
	if (err) {
		mutex_unlock(&ocelot->fwd_domain_lock);
		kfree(fdb);
		return err;
	}

	list_add_tail(&fdb->list, &ocelot->lag_fdbs);
	mutex_unlock(&ocelot->fwd_domain_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_lag_fdb_add);

int ocelot_lag_fdb_del(struct ocelot *ocelot, struct net_device *bond,
		       const unsigned char *addr, u16 vid)
{
	struct ocelot_lag_fdb *fdb, *tmp;

	mutex_lock(&ocelot->fwd_domain_lock);

	list_for_each_entry_safe(fdb, tmp, &ocelot->lag_fdbs, list) {
		if (!ether_addr_equal(fdb->addr, addr) || fdb->vid != vid ||
		    fdb->bond != bond)
			continue;

		ocelot_mact_forget(ocelot, addr, vid);
		list_del(&fdb->list);
		mutex_unlock(&ocelot->fwd_domain_lock);
		kfree(fdb);

		return 0;
	}

	mutex_unlock(&ocelot->fwd_domain_lock);

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(ocelot_lag_fdb_del);

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

		if (ocelot->npi_inj_prefix == OCELOT_TAG_PREFIX_SHORT)
			maxlen += OCELOT_SHORT_PREFIX_LEN;
		else if (ocelot->npi_inj_prefix == OCELOT_TAG_PREFIX_LONG)
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
	atop_tot = (ocelot->packet_buffer_size - 9 * maxlen) /
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

		if (ocelot->npi_inj_prefix == OCELOT_TAG_PREFIX_SHORT)
			max_mtu -= OCELOT_SHORT_PREFIX_LEN;
		else if (ocelot->npi_inj_prefix == OCELOT_TAG_PREFIX_LONG)
			max_mtu -= OCELOT_LONG_PREFIX_LEN;
	}

	return max_mtu;
}
EXPORT_SYMBOL(ocelot_get_max_mtu);

static void ocelot_port_set_learning(struct ocelot *ocelot, int port,
				     bool enabled)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u32 val = 0;

	if (enabled)
		val = ANA_PORT_PORT_CFG_LEARN_ENA;

	ocelot_rmw_gix(ocelot, val, ANA_PORT_PORT_CFG_LEARN_ENA,
		       ANA_PORT_PORT_CFG, port);

	ocelot_port->learn_ena = enabled;
}

static void ocelot_port_set_ucast_flood(struct ocelot *ocelot, int port,
					bool enabled)
{
	u32 val = 0;

	if (enabled)
		val = BIT(port);

	ocelot_rmw_rix(ocelot, val, BIT(port), ANA_PGID_PGID, PGID_UC);
}

static void ocelot_port_set_mcast_flood(struct ocelot *ocelot, int port,
					bool enabled)
{
	u32 val = 0;

	if (enabled)
		val = BIT(port);

	ocelot_rmw_rix(ocelot, val, BIT(port), ANA_PGID_PGID, PGID_MC);
}

static void ocelot_port_set_bcast_flood(struct ocelot *ocelot, int port,
					bool enabled)
{
	u32 val = 0;

	if (enabled)
		val = BIT(port);

	ocelot_rmw_rix(ocelot, val, BIT(port), ANA_PGID_PGID, PGID_BC);
}

int ocelot_port_pre_bridge_flags(struct ocelot *ocelot, int port,
				 struct switchdev_brport_flags flags)
{
	if (flags.mask & ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
			   BR_BCAST_FLOOD))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(ocelot_port_pre_bridge_flags);

void ocelot_port_bridge_flags(struct ocelot *ocelot, int port,
			      struct switchdev_brport_flags flags)
{
	if (flags.mask & BR_LEARNING)
		ocelot_port_set_learning(ocelot, port,
					 !!(flags.val & BR_LEARNING));

	if (flags.mask & BR_FLOOD)
		ocelot_port_set_ucast_flood(ocelot, port,
					    !!(flags.val & BR_FLOOD));

	if (flags.mask & BR_MCAST_FLOOD)
		ocelot_port_set_mcast_flood(ocelot, port,
					    !!(flags.val & BR_MCAST_FLOOD));

	if (flags.mask & BR_BCAST_FLOOD)
		ocelot_port_set_bcast_flood(ocelot, port,
					    !!(flags.val & BR_BCAST_FLOOD));
}
EXPORT_SYMBOL(ocelot_port_bridge_flags);

void ocelot_init_port(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	skb_queue_head_init(&ocelot_port->tx_skbs);

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

	/* Disable source address learning for standalone mode */
	ocelot_port_set_learning(ocelot, port, false);

	/* Set the port's initial logical port ID value, enable receiving
	 * frames on it, and configure the MAC address learning type to
	 * automatic.
	 */
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_LEARNAUTO |
			 ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(port),
			 ANA_PORT_PORT_CFG, port);

	/* Enable vcap lookups */
	ocelot_vcap_enable(ocelot, port);
}
EXPORT_SYMBOL(ocelot_init_port);

/* Configure and enable the CPU port module, which is a set of queues
 * accessible through register MMIO, frame DMA or Ethernet (in case
 * NPI mode is used).
 */
static void ocelot_cpu_port_init(struct ocelot *ocelot)
{
	int cpu = ocelot->num_phys_ports;

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

	/* Enable CPU port module */
	ocelot_fields_write(ocelot, cpu, QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);
	/* CPU port Injection/Extraction configuration */
	ocelot_fields_write(ocelot, cpu, SYS_PORT_MODE_INCL_XTR_HDR,
			    OCELOT_TAG_PREFIX_NONE);
	ocelot_fields_write(ocelot, cpu, SYS_PORT_MODE_INCL_INJ_HDR,
			    OCELOT_TAG_PREFIX_NONE);

	/* Configure the CPU port to be VLAN aware */
	ocelot_write_gix(ocelot,
			 ANA_PORT_VLAN_CFG_VLAN_VID(OCELOT_VLAN_UNAWARE_PVID) |
			 ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA |
			 ANA_PORT_VLAN_CFG_VLAN_POP_CNT(1),
			 ANA_PORT_VLAN_CFG, cpu);
}

static void ocelot_detect_features(struct ocelot *ocelot)
{
	int mmgt, eq_ctrl;

	/* For Ocelot, Felix, Seville, Serval etc, SYS:MMGT:MMGT:FREECNT holds
	 * the number of 240-byte free memory words (aka 4-cell chunks) and not
	 * 192 bytes as the documentation incorrectly says.
	 */
	mmgt = ocelot_read(ocelot, SYS_MMGT);
	ocelot->packet_buffer_size = 240 * SYS_MMGT_FREECNT(mmgt);

	eq_ctrl = ocelot_read(ocelot, QSYS_EQ_CTRL);
	ocelot->num_frame_refs = QSYS_MMGT_EQ_CTRL_FP_FREE_CNT(eq_ctrl);
}

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

	ocelot->stats = devm_kcalloc(ocelot->dev,
				     ocelot->num_phys_ports * ocelot->num_stats,
				     sizeof(u64), GFP_KERNEL);
	if (!ocelot->stats)
		return -ENOMEM;

	mutex_init(&ocelot->stats_lock);
	mutex_init(&ocelot->ptp_lock);
	mutex_init(&ocelot->mact_lock);
	mutex_init(&ocelot->fwd_domain_lock);
	spin_lock_init(&ocelot->ptp_clock_lock);
	spin_lock_init(&ocelot->ts_id_lock);
	snprintf(queue_name, sizeof(queue_name), "%s-stats",
		 dev_name(ocelot->dev));
	ocelot->stats_queue = create_singlethread_workqueue(queue_name);
	if (!ocelot->stats_queue)
		return -ENOMEM;

	ocelot->owq = alloc_ordered_workqueue("ocelot-owq", 0);
	if (!ocelot->owq) {
		destroy_workqueue(ocelot->stats_queue);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ocelot->multicast);
	INIT_LIST_HEAD(&ocelot->pgids);
	INIT_LIST_HEAD(&ocelot->vlans);
	INIT_LIST_HEAD(&ocelot->lag_fdbs);
	ocelot_detect_features(ocelot);
	ocelot_mact_init(ocelot);
	ocelot_vlan_init(ocelot);
	ocelot_vcap_init(ocelot);
	ocelot_cpu_port_init(ocelot);

	if (ocelot->ops->psfp_init)
		ocelot->ops->psfp_init(ocelot);

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
			     ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA |
			     ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA |
			     ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA,
			     ANA_AGGR_CFG);

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
	for (i = 0; i < ocelot->num_flooding_pgids; i++)
		ocelot_write_rix(ocelot, ANA_FLOODING_FLD_MULTICAST(PGID_MC) |
				 ANA_FLOODING_FLD_BROADCAST(PGID_BC) |
				 ANA_FLOODING_FLD_UNICAST(PGID_UC),
				 ANA_FLOODING, i);
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

	for_each_nonreserved_multicast_dest_pgid(ocelot, i) {
		u32 val = ANA_PGID_PGID_PGID(GENMASK(ocelot->num_phys_ports - 1, 0));

		ocelot_write_rix(ocelot, val, ANA_PGID_PGID, i);
	}

	ocelot_write_rix(ocelot, 0, ANA_PGID_PGID, PGID_BLACKHOLE);

	/* Allow broadcast and unknown L2 multicast to the CPU. */
	ocelot_rmw_rix(ocelot, ANA_PGID_PGID_PGID(BIT(ocelot->num_phys_ports)),
		       ANA_PGID_PGID_PGID(BIT(ocelot->num_phys_ports)),
		       ANA_PGID_PGID, PGID_MC);
	ocelot_rmw_rix(ocelot, ANA_PGID_PGID_PGID(BIT(ocelot->num_phys_ports)),
		       ANA_PGID_PGID_PGID(BIT(ocelot->num_phys_ports)),
		       ANA_PGID_PGID, PGID_BC);
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

	ret = ocelot_prepare_stats_regions(ocelot);
	if (ret) {
		destroy_workqueue(ocelot->stats_queue);
		destroy_workqueue(ocelot->owq);
		return ret;
	}

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
	destroy_workqueue(ocelot->owq);
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
