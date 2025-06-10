// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/dsa/ocelot.h>
#include <linux/if_bridge.h>
#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <net/pkt_sched.h>
#include <soc/mscc/ocelot_hsio.h>
#include <soc/mscc/ocelot_vcap.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

#define TABLE_UPDATE_SLEEP_US	10
#define TABLE_UPDATE_TIMEOUT_US	100000
#define MEM_INIT_SLEEP_US	1000
#define MEM_INIT_TIMEOUT_US	100000

#define OCELOT_RSV_VLAN_RANGE_START 4000

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

void ocelot_pll5_init(struct ocelot *ocelot)
{
	/* Configure PLL5. This will need a proper CCF driver
	 * The values are coming from the VTSS API for Ocelot
	 */
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG4,
		     HSIO_PLL5G_CFG4_IB_CTRL(0x7600) |
		     HSIO_PLL5G_CFG4_IB_BIAS_CTRL(0x8));
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG0,
		     HSIO_PLL5G_CFG0_CORE_CLK_DIV(0x11) |
		     HSIO_PLL5G_CFG0_CPU_CLK_DIV(2) |
		     HSIO_PLL5G_CFG0_ENA_BIAS |
		     HSIO_PLL5G_CFG0_ENA_VCO_BUF |
		     HSIO_PLL5G_CFG0_ENA_CP1 |
		     HSIO_PLL5G_CFG0_SELCPI(2) |
		     HSIO_PLL5G_CFG0_LOOP_BW_RES(0xe) |
		     HSIO_PLL5G_CFG0_SELBGV820(4) |
		     HSIO_PLL5G_CFG0_DIV4 |
		     HSIO_PLL5G_CFG0_ENA_CLKTREE |
		     HSIO_PLL5G_CFG0_ENA_LANE);
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG2,
		     HSIO_PLL5G_CFG2_EN_RESET_FRQ_DET |
		     HSIO_PLL5G_CFG2_EN_RESET_OVERRUN |
		     HSIO_PLL5G_CFG2_GAIN_TEST(0x8) |
		     HSIO_PLL5G_CFG2_ENA_AMPCTRL |
		     HSIO_PLL5G_CFG2_PWD_AMPCTRL_N |
		     HSIO_PLL5G_CFG2_AMPC_SEL(0x10));
}
EXPORT_SYMBOL(ocelot_pll5_init);

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

static int ocelot_single_vlan_aware_bridge(struct ocelot *ocelot,
					   struct netlink_ext_ack *extack)
{
	struct net_device *bridge = NULL;
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (!ocelot_port || !ocelot_port->bridge ||
		    !br_vlan_enabled(ocelot_port->bridge))
			continue;

		if (!bridge) {
			bridge = ocelot_port->bridge;
			continue;
		}

		if (bridge == ocelot_port->bridge)
			continue;

		NL_SET_ERR_MSG_MOD(extack,
				   "Only one VLAN-aware bridge is supported");
		return -EBUSY;
	}

	return 0;
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

		/* Ignore the VLAN added by ocelot_add_vlan_unaware_pvid(),
		 * because this is never active in hardware at the same time as
		 * the bridge VLANs, which only matter in VLAN-aware mode.
		 */
		if (vlan->vid >= OCELOT_RSV_VLAN_RANGE_START)
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

int ocelot_bridge_num_find(struct ocelot *ocelot,
			   const struct net_device *bridge)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (ocelot_port && ocelot_port->bridge == bridge)
			return ocelot_port->bridge_num;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(ocelot_bridge_num_find);

static u16 ocelot_vlan_unaware_pvid(struct ocelot *ocelot,
				    const struct net_device *bridge)
{
	int bridge_num;

	/* Standalone ports use VID 0 */
	if (!bridge)
		return 0;

	bridge_num = ocelot_bridge_num_find(ocelot, bridge);
	if (WARN_ON(bridge_num < 0))
		return 0;

	/* VLAN-unaware bridges use a reserved VID going from 4095 downwards */
	return VLAN_N_VID - bridge_num - 1;
}

/**
 * ocelot_update_vlan_reclassify_rule() - Make switch aware only to bridge VLAN TPID
 *
 * @ocelot: Switch private data structure
 * @port: Index of ingress port
 *
 * IEEE 802.1Q-2018 clauses "5.5 C-VLAN component conformance" and "5.6 S-VLAN
 * component conformance" suggest that a C-VLAN component should only recognize
 * and filter on C-Tags, and an S-VLAN component should only recognize and
 * process based on C-Tags.
 *
 * In Linux, as per commit 1a0b20b25732 ("Merge branch 'bridge-next'"), C-VLAN
 * components are largely represented by a bridge with vlan_protocol 802.1Q,
 * and S-VLAN components by a bridge with vlan_protocol 802.1ad.
 *
 * Currently the driver only offloads vlan_protocol 802.1Q, but the hardware
 * design is non-conformant, because the switch assigns each frame to a VLAN
 * based on an entirely different question, as detailed in figure "Basic VLAN
 * Classification Flow" from its manual and reproduced below.
 *
 * Set TAG_TYPE, PCP, DEI, VID to port-default values in VLAN_CFG register
 * if VLAN_AWARE_ENA[port] and frame has outer tag then:
 *   if VLAN_INNER_TAG_ENA[port] and frame has inner tag then:
 *     TAG_TYPE = (Frame.InnerTPID <> 0x8100)
 *     Set PCP, DEI, VID to values from inner VLAN header
 *   else:
 *     TAG_TYPE = (Frame.OuterTPID <> 0x8100)
 *     Set PCP, DEI, VID to values from outer VLAN header
 *   if VID == 0 then:
 *     VID = VLAN_CFG.VLAN_VID
 *
 * Summarized, the switch will recognize both 802.1Q and 802.1ad TPIDs as VLAN
 * "with equal rights", and just set the TAG_TYPE bit to 0 (if 802.1Q) or to 1
 * (if 802.1ad). It will classify based on whichever of the tags is "outer", no
 * matter what TPID that may have (or "inner", if VLAN_INNER_TAG_ENA[port]).
 *
 * In the VLAN Table, the TAG_TYPE information is not accessible - just the
 * classified VID is - so it is as if each VLAN Table entry is for 2 VLANs:
 * C-VLAN X, and S-VLAN X.
 *
 * Whereas the Linux bridge behavior is to only filter on frames with a TPID
 * equal to the vlan_protocol, and treat everything else as VLAN-untagged.
 *
 * Consider an ingress packet tagged with 802.1ad VID=3 and 802.1Q VID=5,
 * received on a bridge vlan_filtering=1 vlan_protocol=802.1Q port. This frame
 * should be treated as 802.1Q-untagged, and classified to the PVID of that
 * bridge port. Not to VID=3, and not to VID=5.
 *
 * The VCAP IS1 TCAM has everything we need to overwrite the choices made in
 * the basic VLAN classification pipeline: it can match on TAG_TYPE in the key,
 * and it can modify the classified VID in the action. Thus, for each port
 * under a vlan_filtering bridge, we can insert a rule in VCAP IS1 lookup 0 to
 * match on 802.1ad tagged frames and modify their classified VID to the 802.1Q
 * PVID of the port. This effectively makes it appear to the outside world as
 * if those packets were processed as VLAN-untagged.
 *
 * The rule needs to be updated each time the bridge PVID changes, and needs
 * to be deleted if the bridge PVID is deleted, or if the port becomes
 * VLAN-unaware.
 */
static int ocelot_update_vlan_reclassify_rule(struct ocelot *ocelot, int port)
{
	unsigned long cookie = OCELOT_VCAP_IS1_VLAN_RECLASSIFY(ocelot, port);
	struct ocelot_vcap_block *block_vcap_is1 = &ocelot->block[VCAP_IS1];
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	const struct ocelot_bridge_vlan *pvid_vlan;
	struct ocelot_vcap_filter *filter;
	int err, val, pcp, dei;
	bool vid_replace_ena;
	u16 vid;

	pvid_vlan = ocelot_port->pvid_vlan;
	vid_replace_ena = ocelot_port->vlan_aware && pvid_vlan;

	filter = ocelot_vcap_block_find_filter_by_id(block_vcap_is1, cookie,
						     false);
	if (!vid_replace_ena) {
		/* If the reclassification filter doesn't need to exist, delete
		 * it if it was previously installed, and exit doing nothing
		 * otherwise.
		 */
		if (filter)
			return ocelot_vcap_filter_del(ocelot, filter);

		return 0;
	}

	/* The reclassification rule must apply. See if it already exists
	 * or if it must be created.
	 */

	/* Treating as VLAN-untagged means using as classified VID equal to
	 * the bridge PVID, and PCP/DEI set to the port default QoS values.
	 */
	vid = pvid_vlan->vid;
	val = ocelot_read_gix(ocelot, ANA_PORT_QOS_CFG, port);
	pcp = ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL_X(val);
	dei = !!(val & ANA_PORT_QOS_CFG_DP_DEFAULT_VAL);

	if (filter) {
		bool changed = false;

		/* Filter exists, just update it */
		if (filter->action.vid != vid) {
			filter->action.vid = vid;
			changed = true;
		}
		if (filter->action.pcp != pcp) {
			filter->action.pcp = pcp;
			changed = true;
		}
		if (filter->action.dei != dei) {
			filter->action.dei = dei;
			changed = true;
		}

		if (!changed)
			return 0;

		return ocelot_vcap_filter_replace(ocelot, filter);
	}

	/* Filter doesn't exist, create it */
	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return -ENOMEM;

	filter->key_type = OCELOT_VCAP_KEY_ANY;
	filter->ingress_port_mask = BIT(port);
	filter->vlan.tpid = OCELOT_VCAP_BIT_1;
	filter->prio = 1;
	filter->id.cookie = cookie;
	filter->id.tc_offload = false;
	filter->block_id = VCAP_IS1;
	filter->type = OCELOT_VCAP_FILTER_OFFLOAD;
	filter->lookup = 0;
	filter->action.vid_replace_ena = true;
	filter->action.pcp_dei_ena = true;
	filter->action.vid = vid;
	filter->action.pcp = pcp;
	filter->action.dei = dei;

	err = ocelot_vcap_filter_add(ocelot, filter, NULL);
	if (err)
		kfree(filter);

	return err;
}

/* Default vlan to clasify for untagged frames (may be zero) */
static int ocelot_port_set_pvid(struct ocelot *ocelot, int port,
				const struct ocelot_bridge_vlan *pvid_vlan)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u16 pvid = ocelot_vlan_unaware_pvid(ocelot, ocelot_port->bridge);
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
	 *
	 * Also, we only support the bridge 802.1Q VLAN protocol, so
	 * 802.1ad-tagged frames (carrying S-Tags) should be considered
	 * 802.1Q-untagged, and also dropped.
	 */
	if (!pvid_vlan && ocelot_port->vlan_aware)
		val = ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA |
		      ANA_PORT_DROP_CFG_DROP_S_TAGGED_ENA;

	ocelot_rmw_gix(ocelot, val,
		       ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA |
		       ANA_PORT_DROP_CFG_DROP_S_TAGGED_ENA,
		       ANA_PORT_DROP_CFG, port);

	return ocelot_update_vlan_reclassify_rule(ocelot, port);
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

static int ocelot_add_vlan_unaware_pvid(struct ocelot *ocelot, int port,
					const struct net_device *bridge)
{
	u16 vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

	return ocelot_vlan_member_add(ocelot, port, vid, true);
}

static int ocelot_del_vlan_unaware_pvid(struct ocelot *ocelot, int port,
					const struct net_device *bridge)
{
	u16 vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

	return ocelot_vlan_member_del(ocelot, port, vid);
}

int ocelot_port_vlan_filtering(struct ocelot *ocelot, int port,
			       bool vlan_aware, struct netlink_ext_ack *extack)
{
	struct ocelot_vcap_block *block = &ocelot->block[VCAP_IS1];
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_vcap_filter *filter;
	int err = 0;
	u32 val;

	list_for_each_entry(filter, &block->rules, list) {
		if (filter->ingress_port_mask & BIT(port) &&
		    filter->action.vid_replace_ena) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot change VLAN state with vlan modify rules active");
			return -EBUSY;
		}
	}

	err = ocelot_single_vlan_aware_bridge(ocelot, extack);
	if (err)
		return err;

	if (vlan_aware)
		err = ocelot_del_vlan_unaware_pvid(ocelot, port,
						   ocelot_port->bridge);
	else if (ocelot_port->bridge)
		err = ocelot_add_vlan_unaware_pvid(ocelot, port,
						   ocelot_port->bridge);
	if (err)
		return err;

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

	err = ocelot_port_set_pvid(ocelot, port, ocelot_port->pvid_vlan);
	if (err)
		return err;

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

	if (vid > OCELOT_RSV_VLAN_RANGE_START) {
		NL_SET_ERR_MSG_MOD(extack,
				   "VLAN range 4000-4095 reserved for VLAN-unaware bridging");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_vlan_prepare);

int ocelot_vlan_add(struct ocelot *ocelot, int port, u16 vid, bool pvid,
		    bool untagged)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int err;

	/* Ignore VID 0 added to our RX filter by the 8021q module, since
	 * that collides with OCELOT_STANDALONE_PVID and changes it from
	 * egress-untagged to egress-tagged.
	 */
	if (!vid)
		return 0;

	err = ocelot_vlan_member_add(ocelot, port, vid, untagged);
	if (err)
		return err;

	/* Default ingress vlan classification */
	if (pvid) {
		err = ocelot_port_set_pvid(ocelot, port,
					   ocelot_bridge_vlan_find(ocelot, vid));
		if (err)
			return err;
	} else if (ocelot_port->pvid_vlan &&
		   ocelot_bridge_vlan_find(ocelot, vid) == ocelot_port->pvid_vlan) {
		err = ocelot_port_set_pvid(ocelot, port, NULL);
		if (err)
			return err;
	}

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

	if (!vid)
		return 0;

	if (ocelot_port->pvid_vlan && ocelot_port->pvid_vlan->vid == vid)
		del_pvid = true;

	err = ocelot_vlan_member_del(ocelot, port, vid);
	if (err)
		return err;

	/* Ingress */
	if (del_pvid) {
		err = ocelot_port_set_pvid(ocelot, port, NULL);
		if (err)
			return err;
	}

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

	/* We need VID 0 to get traffic on standalone ports.
	 * It is added automatically if the 8021q module is loaded, but we
	 * can't rely on that since it might not be.
	 */
	ocelot_vlant_set_mask(ocelot, OCELOT_STANDALONE_PVID, all_ports);

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

int ocelot_port_configure_serdes(struct ocelot *ocelot, int port,
				 struct device_node *portnp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct device *dev = ocelot->dev;
	int err;

	/* Ensure clock signals and speed are set on all QSGMII links */
	if (ocelot_port->phy_mode == PHY_INTERFACE_MODE_QSGMII)
		ocelot_port_rmwl(ocelot_port, 0,
				 DEV_CLOCK_CFG_MAC_TX_RST |
				 DEV_CLOCK_CFG_MAC_RX_RST,
				 DEV_CLOCK_CFG);

	if (ocelot_port->phy_mode != PHY_INTERFACE_MODE_INTERNAL) {
		struct phy *serdes = of_phy_get(portnp, NULL);

		if (IS_ERR(serdes)) {
			err = PTR_ERR(serdes);
			dev_err_probe(dev, err,
				      "missing SerDes phys for port %d\n",
				      port);
			return err;
		}

		err = phy_set_mode_ext(serdes, PHY_MODE_ETHERNET,
				       ocelot_port->phy_mode);
		of_phy_put(serdes);
		if (err) {
			dev_err(dev, "Could not SerDes mode on port %d: %pe\n",
				port, ERR_PTR(err));
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_port_configure_serdes);

void ocelot_phylink_mac_config(struct ocelot *ocelot, int port,
			       unsigned int link_an_mode,
			       const struct phylink_link_state *state)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

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
}
EXPORT_SYMBOL_GPL(ocelot_phylink_mac_config);

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

	if (rx_pause)
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
		/* Workaround for hardware bug - FP doesn't work
		 * at all link speeds for all PHY modes. The function
		 * below also calls ocelot->ops->cut_through_fwd(),
		 * so we don't need to do it twice.
		 */
		ocelot_port_update_active_preemptible_tcs(ocelot, port);
		mutex_unlock(&ocelot->fwd_domain_lock);
	}

	/* Core: Enable port for frame transfer */
	ocelot_fields_write(ocelot, port,
			    QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);
}
EXPORT_SYMBOL_GPL(ocelot_phylink_mac_link_up);

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

void ocelot_lock_inj_grp(struct ocelot *ocelot, int grp)
			 __acquires(&ocelot->inj_lock)
{
	spin_lock(&ocelot->inj_lock);
}
EXPORT_SYMBOL_GPL(ocelot_lock_inj_grp);

void ocelot_unlock_inj_grp(struct ocelot *ocelot, int grp)
			   __releases(&ocelot->inj_lock)
{
	spin_unlock(&ocelot->inj_lock);
}
EXPORT_SYMBOL_GPL(ocelot_unlock_inj_grp);

void ocelot_lock_xtr_grp(struct ocelot *ocelot, int grp)
			 __acquires(&ocelot->inj_lock)
{
	spin_lock(&ocelot->inj_lock);
}
EXPORT_SYMBOL_GPL(ocelot_lock_xtr_grp);

void ocelot_unlock_xtr_grp(struct ocelot *ocelot, int grp)
			   __releases(&ocelot->inj_lock)
{
	spin_unlock(&ocelot->inj_lock);
}
EXPORT_SYMBOL_GPL(ocelot_unlock_xtr_grp);

void ocelot_lock_xtr_grp_bh(struct ocelot *ocelot, int grp)
			    __acquires(&ocelot->xtr_lock)
{
	spin_lock_bh(&ocelot->xtr_lock);
}
EXPORT_SYMBOL_GPL(ocelot_lock_xtr_grp_bh);

void ocelot_unlock_xtr_grp_bh(struct ocelot *ocelot, int grp)
			      __releases(&ocelot->xtr_lock)
{
	spin_unlock_bh(&ocelot->xtr_lock);
}
EXPORT_SYMBOL_GPL(ocelot_unlock_xtr_grp_bh);

int ocelot_xtr_poll_frame(struct ocelot *ocelot, int grp, struct sk_buff **nskb)
{
	u64 timestamp, src_port, len;
	u32 xfh[OCELOT_TAG_LEN / 4];
	struct net_device *dev;
	struct sk_buff *skb;
	int sz, buf_len;
	u32 val, *buf;
	int err;

	lockdep_assert_held(&ocelot->xtr_lock);

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

	lockdep_assert_held(&ocelot->inj_lock);

	if (!(val & QS_INJ_STATUS_FIFO_RDY(BIT(grp))))
		return false;
	if (val & QS_INJ_STATUS_WMARK_REACHED(BIT(grp)))
		return false;

	return true;
}
EXPORT_SYMBOL(ocelot_can_inject);

/**
 * ocelot_ifh_set_basic - Set basic information in Injection Frame Header
 * @ifh: Pointer to Injection Frame Header memory
 * @ocelot: Switch private data structure
 * @port: Egress port number
 * @rew_op: Egress rewriter operation for PTP
 * @skb: Pointer to socket buffer (packet)
 *
 * Populate the Injection Frame Header with basic information for this skb: the
 * analyzer bypass bit, destination port, VLAN info, egress rewriter info.
 */
void ocelot_ifh_set_basic(void *ifh, struct ocelot *ocelot, int port,
			  u32 rew_op, struct sk_buff *skb)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct net_device *dev = skb->dev;
	u64 vlan_tci, tag_type;
	int qos_class;

	ocelot_xmit_get_vlan_info(skb, ocelot_port->bridge, &vlan_tci,
				  &tag_type);

	qos_class = netdev_get_num_tc(dev) ?
		    netdev_get_prio_tc_map(dev, skb->priority) : skb->priority;

	memset(ifh, 0, OCELOT_TAG_LEN);
	ocelot_ifh_set_bypass(ifh, 1);
	ocelot_ifh_set_src(ifh, ocelot->num_phys_ports);
	ocelot_ifh_set_dest(ifh, BIT_ULL(port));
	ocelot_ifh_set_qos_class(ifh, qos_class);
	ocelot_ifh_set_tag_type(ifh, tag_type);
	ocelot_ifh_set_vlan_tci(ifh, vlan_tci);
	if (rew_op)
		ocelot_ifh_set_rew_op(ifh, rew_op);
}
EXPORT_SYMBOL(ocelot_ifh_set_basic);

void ocelot_port_inject_frame(struct ocelot *ocelot, int port, int grp,
			      u32 rew_op, struct sk_buff *skb)
{
	u32 ifh[OCELOT_TAG_LEN / 4];
	unsigned int i, count, last;

	lockdep_assert_held(&ocelot->inj_lock);

	ocelot_write_rix(ocelot, QS_INJ_CTRL_GAP_SIZE(1) |
			 QS_INJ_CTRL_SOF, QS_INJ_CTRL, grp);

	ocelot_ifh_set_basic(ifh, ocelot, port, rew_op, skb);

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
	lockdep_assert_held(&ocelot->xtr_lock);

	while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp))
		ocelot_read_rix(ocelot, QS_XTR_RD, grp);
}
EXPORT_SYMBOL(ocelot_drain_cpu_queue);

int ocelot_fdb_add(struct ocelot *ocelot, int port, const unsigned char *addr,
		   u16 vid, const struct net_device *bridge)
{
	if (!vid)
		vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

	return ocelot_mact_learn(ocelot, port, addr, vid, ENTRYTYPE_LOCKED);
}
EXPORT_SYMBOL(ocelot_fdb_add);

int ocelot_fdb_del(struct ocelot *ocelot, int port, const unsigned char *addr,
		   u16 vid, const struct net_device *bridge)
{
	if (!vid)
		vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

	return ocelot_mact_forget(ocelot, addr, vid);
}
EXPORT_SYMBOL(ocelot_fdb_del);

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

			/* Hide the reserved VLANs used for
			 * VLAN-unaware bridging.
			 */
			if (entry.vid > OCELOT_RSV_VLAN_RANGE_START)
				entry.vid = 0;

			err = cb(entry.mac, entry.vid, is_static, data);
			if (err)
				break;
		}
	}

	mutex_unlock(&ocelot->mact_lock);

	return err;
}
EXPORT_SYMBOL(ocelot_fdb_dump);

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
		trap->is_trap = true;
		new = true;
	}

	trap->ingress_port_mask |= BIT(port);

	if (new)
		err = ocelot_vcap_filter_add(ocelot, trap, NULL);
	else
		err = ocelot_vcap_filter_replace(ocelot, trap);
	if (err) {
		trap->ingress_port_mask &= ~BIT(port);
		if (!trap->ingress_port_mask)
			kfree(trap);
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
	if (!trap->ingress_port_mask)
		return ocelot_vcap_filter_del(ocelot, trap);

	return ocelot_vcap_filter_replace(ocelot, trap);
}

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
int ocelot_bond_get_id(struct ocelot *ocelot, struct net_device *bond)
{
	int bond_mask = ocelot_get_bond_mask(ocelot, bond);

	if (!bond_mask)
		return -ENOENT;

	return __ffs(bond_mask);
}
EXPORT_SYMBOL_GPL(ocelot_bond_get_id);

/* Returns the mask of user ports assigned to this DSA tag_8021q CPU port.
 * Note that when CPU ports are in a LAG, the user ports are assigned to the
 * 'primary' CPU port, the one whose physical port number gives the logical
 * port number of the LAG.
 *
 * We leave PGID_SRC poorly configured for the 'secondary' CPU port in the LAG
 * (to which no user port is assigned), but it appears that forwarding from
 * this secondary CPU port looks at the PGID_SRC associated with the logical
 * port ID that it's assigned to, which *is* configured properly.
 */
static u32 ocelot_dsa_8021q_cpu_assigned_ports(struct ocelot *ocelot,
					       struct ocelot_port *cpu)
{
	u32 mask = 0;
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (!ocelot_port)
			continue;

		if (ocelot_port->dsa_8021q_cpu == cpu)
			mask |= BIT(port);
	}

	if (cpu->bond)
		mask &= ~ocelot_get_bond_mask(ocelot, cpu->bond);

	return mask;
}

/* Returns the DSA tag_8021q CPU port that the given port is assigned to,
 * or the bit mask of CPU ports if said CPU port is in a LAG.
 */
u32 ocelot_port_assigned_dsa_8021q_cpu_mask(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_port *cpu_port = ocelot_port->dsa_8021q_cpu;

	if (!cpu_port)
		return 0;

	if (cpu_port->bond)
		return ocelot_get_bond_mask(ocelot, cpu_port->bond);

	return BIT(cpu_port->index);
}
EXPORT_SYMBOL_GPL(ocelot_port_assigned_dsa_8021q_cpu_mask);

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

static void ocelot_apply_bridge_fwd_mask(struct ocelot *ocelot, bool joining)
{
	int port;

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	/* If cut-through forwarding is supported, update the masks before a
	 * port joins the forwarding domain, to avoid potential underruns if it
	 * has the highest speed from the new domain.
	 */
	if (joining && ocelot->ops->cut_through_fwd)
		ocelot->ops->cut_through_fwd(ocelot);

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
			 * forward packets to all ports assigned to them.
			 */
			mask = ocelot_dsa_8021q_cpu_assigned_ports(ocelot,
								   ocelot_port);
		} else if (ocelot_port->bridge) {
			struct net_device *bond = ocelot_port->bond;

			mask = ocelot_get_bridge_fwd_mask(ocelot, port);
			mask &= ~BIT(port);

			mask |= ocelot_port_assigned_dsa_8021q_cpu_mask(ocelot,
									port);

			if (bond)
				mask &= ~ocelot_get_bond_mask(ocelot, bond);
		} else {
			/* Standalone ports forward only to DSA tag_8021q CPU
			 * ports (if those exist), or to the hardware CPU port
			 * module otherwise.
			 */
			mask = ocelot_port_assigned_dsa_8021q_cpu_mask(ocelot,
								       port);
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

/* Update PGID_CPU which is the destination port mask used for whitelisting
 * unicast addresses filtered towards the host. In the normal and NPI modes,
 * this points to the analyzer entry for the CPU port module, while in DSA
 * tag_8021q mode, it is a bit mask of all active CPU ports.
 * PGID_SRC will take care of forwarding a packet from one user port to
 * no more than a single CPU port.
 */
static void ocelot_update_pgid_cpu(struct ocelot *ocelot)
{
	int pgid_cpu = 0;
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];

		if (!ocelot_port || !ocelot_port->is_dsa_8021q_cpu)
			continue;

		pgid_cpu |= BIT(port);
	}

	if (!pgid_cpu)
		pgid_cpu = BIT(ocelot->num_phys_ports);

	ocelot_write_rix(ocelot, pgid_cpu, ANA_PGID_PGID, PGID_CPU);
}

void ocelot_port_setup_dsa_8021q_cpu(struct ocelot *ocelot, int cpu)
{
	struct ocelot_port *cpu_port = ocelot->ports[cpu];
	u16 vid;

	mutex_lock(&ocelot->fwd_domain_lock);

	cpu_port->is_dsa_8021q_cpu = true;

	for (vid = OCELOT_RSV_VLAN_RANGE_START; vid < VLAN_N_VID; vid++)
		ocelot_vlan_member_add(ocelot, cpu, vid, true);

	ocelot_update_pgid_cpu(ocelot);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL_GPL(ocelot_port_setup_dsa_8021q_cpu);

void ocelot_port_teardown_dsa_8021q_cpu(struct ocelot *ocelot, int cpu)
{
	struct ocelot_port *cpu_port = ocelot->ports[cpu];
	u16 vid;

	mutex_lock(&ocelot->fwd_domain_lock);

	cpu_port->is_dsa_8021q_cpu = false;

	for (vid = OCELOT_RSV_VLAN_RANGE_START; vid < VLAN_N_VID; vid++)
		ocelot_vlan_member_del(ocelot, cpu_port->index, vid);

	ocelot_update_pgid_cpu(ocelot);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL_GPL(ocelot_port_teardown_dsa_8021q_cpu);

void ocelot_port_assign_dsa_8021q_cpu(struct ocelot *ocelot, int port,
				      int cpu)
{
	struct ocelot_port *cpu_port = ocelot->ports[cpu];

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot->ports[port]->dsa_8021q_cpu = cpu_port;
	ocelot_apply_bridge_fwd_mask(ocelot, true);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL_GPL(ocelot_port_assign_dsa_8021q_cpu);

void ocelot_port_unassign_dsa_8021q_cpu(struct ocelot *ocelot, int port)
{
	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot->ports[port]->dsa_8021q_cpu = NULL;
	ocelot_apply_bridge_fwd_mask(ocelot, true);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL_GPL(ocelot_port_unassign_dsa_8021q_cpu);

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
			const struct switchdev_obj_port_mdb *mdb,
			const struct net_device *bridge)
{
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	struct ocelot_pgid *pgid;
	u16 vid = mdb->vid;

	if (!vid)
		vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

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
			const struct switchdev_obj_port_mdb *mdb,
			const struct net_device *bridge)
{
	unsigned char addr[ETH_ALEN];
	struct ocelot_multicast *mc;
	struct ocelot_pgid *pgid;
	u16 vid = mdb->vid;

	if (!vid)
		vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

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

int ocelot_port_bridge_join(struct ocelot *ocelot, int port,
			    struct net_device *bridge, int bridge_num,
			    struct netlink_ext_ack *extack)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int err;

	err = ocelot_single_vlan_aware_bridge(ocelot, extack);
	if (err)
		return err;

	mutex_lock(&ocelot->fwd_domain_lock);

	ocelot_port->bridge = bridge;
	ocelot_port->bridge_num = bridge_num;

	ocelot_apply_bridge_fwd_mask(ocelot, true);

	mutex_unlock(&ocelot->fwd_domain_lock);

	if (br_vlan_enabled(bridge))
		return 0;

	return ocelot_add_vlan_unaware_pvid(ocelot, port, bridge);
}
EXPORT_SYMBOL(ocelot_port_bridge_join);

void ocelot_port_bridge_leave(struct ocelot *ocelot, int port,
			      struct net_device *bridge)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	mutex_lock(&ocelot->fwd_domain_lock);

	if (!br_vlan_enabled(bridge))
		ocelot_del_vlan_unaware_pvid(ocelot, port, bridge);

	ocelot_port->bridge = NULL;
	ocelot_port->bridge_num = -1;

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

static int ocelot_migrate_mc(struct ocelot *ocelot, struct ocelot_multicast *mc,
			     unsigned long from_mask, unsigned long to_mask)
{
	unsigned char addr[ETH_ALEN];
	struct ocelot_pgid *pgid;
	u16 vid = mc->vid;

	dev_dbg(ocelot->dev,
		"Migrating multicast %pM vid %d from port mask 0x%lx to 0x%lx\n",
		mc->addr, mc->vid, from_mask, to_mask);

	/* First clean up the current port mask from hardware, because
	 * we'll be modifying it.
	 */
	ocelot_pgid_free(ocelot, mc->pgid);
	ocelot_encode_ports_to_mdb(addr, mc);
	ocelot_mact_forget(ocelot, addr, vid);

	mc->ports &= ~from_mask;
	mc->ports |= to_mask;

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

int ocelot_migrate_mdbs(struct ocelot *ocelot, unsigned long from_mask,
			unsigned long to_mask)
{
	struct ocelot_multicast *mc;
	int err;

	list_for_each_entry(mc, &ocelot->multicast, list) {
		if (!(mc->ports & from_mask))
			continue;

		err = ocelot_migrate_mc(ocelot, mc, from_mask, to_mask);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_migrate_mdbs);

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
			 struct netdev_lag_upper_info *info,
			 struct netlink_ext_ack *extack)
{
	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only offload LAG using hash TX type");
		return -EOPNOTSUPP;
	}

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
		       const unsigned char *addr, u16 vid,
		       const struct net_device *bridge)
{
	struct ocelot_lag_fdb *fdb;
	int lag, err;

	fdb = kzalloc(sizeof(*fdb), GFP_KERNEL);
	if (!fdb)
		return -ENOMEM;

	mutex_lock(&ocelot->fwd_domain_lock);

	if (!vid)
		vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

	ether_addr_copy(fdb->addr, addr);
	fdb->vid = vid;
	fdb->bond = bond;

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
		       const unsigned char *addr, u16 vid,
		       const struct net_device *bridge)
{
	struct ocelot_lag_fdb *fdb, *tmp;

	mutex_lock(&ocelot->fwd_domain_lock);

	if (!vid)
		vid = ocelot_vlan_unaware_pvid(ocelot, bridge);

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
	ocelot_rmw_rix(ocelot, val, BIT(port), ANA_PGID_PGID, PGID_MCIPV4);
	ocelot_rmw_rix(ocelot, val, BIT(port), ANA_PGID_PGID, PGID_MCIPV6);
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

int ocelot_port_get_default_prio(struct ocelot *ocelot, int port)
{
	int val = ocelot_read_gix(ocelot, ANA_PORT_QOS_CFG, port);

	return ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL_X(val);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_default_prio);

int ocelot_port_set_default_prio(struct ocelot *ocelot, int port, u8 prio)
{
	if (prio >= OCELOT_NUM_TC)
		return -ERANGE;

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL(prio),
		       ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL_M,
		       ANA_PORT_QOS_CFG,
		       port);

	return ocelot_update_vlan_reclassify_rule(ocelot, port);
}
EXPORT_SYMBOL_GPL(ocelot_port_set_default_prio);

int ocelot_port_get_dscp_prio(struct ocelot *ocelot, int port, u8 dscp)
{
	int qos_cfg = ocelot_read_gix(ocelot, ANA_PORT_QOS_CFG, port);
	int dscp_cfg = ocelot_read_rix(ocelot, ANA_DSCP_CFG, dscp);

	/* Return error if DSCP prioritization isn't enabled */
	if (!(qos_cfg & ANA_PORT_QOS_CFG_QOS_DSCP_ENA))
		return -EOPNOTSUPP;

	if (qos_cfg & ANA_PORT_QOS_CFG_DSCP_TRANSLATE_ENA) {
		dscp = ANA_DSCP_CFG_DSCP_TRANSLATE_VAL_X(dscp_cfg);
		/* Re-read ANA_DSCP_CFG for the translated DSCP */
		dscp_cfg = ocelot_read_rix(ocelot, ANA_DSCP_CFG, dscp);
	}

	/* If the DSCP value is not trusted, the QoS classification falls back
	 * to VLAN PCP or port-based default.
	 */
	if (!(dscp_cfg & ANA_DSCP_CFG_DSCP_TRUST_ENA))
		return -EOPNOTSUPP;

	return ANA_DSCP_CFG_QOS_DSCP_VAL_X(dscp_cfg);
}
EXPORT_SYMBOL_GPL(ocelot_port_get_dscp_prio);

int ocelot_port_add_dscp_prio(struct ocelot *ocelot, int port, u8 dscp, u8 prio)
{
	int mask, val;

	if (prio >= OCELOT_NUM_TC)
		return -ERANGE;

	/* There is at least one app table priority (this one), so we need to
	 * make sure DSCP prioritization is enabled on the port.
	 * Also make sure DSCP translation is disabled
	 * (dcbnl doesn't support it).
	 */
	mask = ANA_PORT_QOS_CFG_QOS_DSCP_ENA |
	       ANA_PORT_QOS_CFG_DSCP_TRANSLATE_ENA;

	ocelot_rmw_gix(ocelot, ANA_PORT_QOS_CFG_QOS_DSCP_ENA, mask,
		       ANA_PORT_QOS_CFG, port);

	/* Trust this DSCP value and map it to the given QoS class */
	val = ANA_DSCP_CFG_DSCP_TRUST_ENA | ANA_DSCP_CFG_QOS_DSCP_VAL(prio);

	ocelot_write_rix(ocelot, val, ANA_DSCP_CFG, dscp);

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_port_add_dscp_prio);

int ocelot_port_del_dscp_prio(struct ocelot *ocelot, int port, u8 dscp, u8 prio)
{
	int dscp_cfg = ocelot_read_rix(ocelot, ANA_DSCP_CFG, dscp);
	int mask, i;

	/* During a "dcb app replace" command, the new app table entry will be
	 * added first, then the old one will be deleted. But the hardware only
	 * supports one QoS class per DSCP value (duh), so if we blindly delete
	 * the app table entry for this DSCP value, we end up deleting the
	 * entry with the new priority. Avoid that by checking whether user
	 * space wants to delete the priority which is currently configured, or
	 * something else which is no longer current.
	 */
	if (ANA_DSCP_CFG_QOS_DSCP_VAL_X(dscp_cfg) != prio)
		return 0;

	/* Untrust this DSCP value */
	ocelot_write_rix(ocelot, 0, ANA_DSCP_CFG, dscp);

	for (i = 0; i < 64; i++) {
		int dscp_cfg = ocelot_read_rix(ocelot, ANA_DSCP_CFG, i);

		/* There are still app table entries on the port, so we need to
		 * keep DSCP enabled, nothing to do.
		 */
		if (dscp_cfg & ANA_DSCP_CFG_DSCP_TRUST_ENA)
			return 0;
	}

	/* Disable DSCP QoS classification if there isn't any trusted
	 * DSCP value left.
	 */
	mask = ANA_PORT_QOS_CFG_QOS_DSCP_ENA |
	       ANA_PORT_QOS_CFG_DSCP_TRANSLATE_ENA;

	ocelot_rmw_gix(ocelot, 0, mask, ANA_PORT_QOS_CFG, port);

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_port_del_dscp_prio);

struct ocelot_mirror *ocelot_mirror_get(struct ocelot *ocelot, int to,
					struct netlink_ext_ack *extack)
{
	struct ocelot_mirror *m = ocelot->mirror;

	if (m) {
		if (m->to != to) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Mirroring already configured towards different egress port");
			return ERR_PTR(-EBUSY);
		}

		refcount_inc(&m->refcount);
		return m;
	}

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return ERR_PTR(-ENOMEM);

	m->to = to;
	refcount_set(&m->refcount, 1);
	ocelot->mirror = m;

	/* Program the mirror port to hardware */
	ocelot_write(ocelot, BIT(to), ANA_MIRRORPORTS);

	return m;
}

void ocelot_mirror_put(struct ocelot *ocelot)
{
	struct ocelot_mirror *m = ocelot->mirror;

	if (!refcount_dec_and_test(&m->refcount))
		return;

	ocelot_write(ocelot, 0, ANA_MIRRORPORTS);
	ocelot->mirror = NULL;
	kfree(m);
}

int ocelot_port_mirror_add(struct ocelot *ocelot, int from, int to,
			   bool ingress, struct netlink_ext_ack *extack)
{
	struct ocelot_mirror *m = ocelot_mirror_get(ocelot, to, extack);

	if (IS_ERR(m))
		return PTR_ERR(m);

	if (ingress) {
		ocelot_rmw_gix(ocelot, ANA_PORT_PORT_CFG_SRC_MIRROR_ENA,
			       ANA_PORT_PORT_CFG_SRC_MIRROR_ENA,
			       ANA_PORT_PORT_CFG, from);
	} else {
		ocelot_rmw(ocelot, BIT(from), BIT(from),
			   ANA_EMIRRORPORTS);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_port_mirror_add);

void ocelot_port_mirror_del(struct ocelot *ocelot, int from, bool ingress)
{
	if (ingress) {
		ocelot_rmw_gix(ocelot, 0, ANA_PORT_PORT_CFG_SRC_MIRROR_ENA,
			       ANA_PORT_PORT_CFG, from);
	} else {
		ocelot_rmw(ocelot, 0, BIT(from), ANA_EMIRRORPORTS);
	}

	ocelot_mirror_put(ocelot);
}
EXPORT_SYMBOL_GPL(ocelot_port_mirror_del);

static void ocelot_port_reset_mqprio(struct ocelot *ocelot, int port)
{
	struct net_device *dev = ocelot->ops->port_to_netdev(ocelot, port);

	netdev_reset_tc(dev);
	ocelot_port_change_fp(ocelot, port, 0);
}

int ocelot_port_mqprio(struct ocelot *ocelot, int port,
		       struct tc_mqprio_qopt_offload *mqprio)
{
	struct net_device *dev = ocelot->ops->port_to_netdev(ocelot, port);
	struct netlink_ext_ack *extack = mqprio->extack;
	struct tc_mqprio_qopt *qopt = &mqprio->qopt;
	int num_tc = qopt->num_tc;
	int tc, err;

	if (!num_tc) {
		ocelot_port_reset_mqprio(ocelot, port);
		return 0;
	}

	err = netdev_set_num_tc(dev, num_tc);
	if (err)
		return err;

	for (tc = 0; tc < num_tc; tc++) {
		if (qopt->count[tc] != 1) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one TXQ per TC supported");
			return -EINVAL;
		}

		err = netdev_set_tc_queue(dev, tc, 1, qopt->offset[tc]);
		if (err)
			goto err_reset_tc;
	}

	err = netif_set_real_num_tx_queues(dev, num_tc);
	if (err)
		goto err_reset_tc;

	ocelot_port_change_fp(ocelot, port, mqprio->preemptible_tcs);

	return 0;

err_reset_tc:
	ocelot_port_reset_mqprio(ocelot, port);
	return err;
}
EXPORT_SYMBOL_GPL(ocelot_port_mqprio);

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
			 ANA_PORT_VLAN_CFG_VLAN_VID(OCELOT_STANDALONE_PVID) |
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

static int ocelot_mem_init_status(struct ocelot *ocelot)
{
	unsigned int val;
	int err;

	err = regmap_field_read(ocelot->regfields[SYS_RESET_CFG_MEM_INIT],
				&val);

	return err ?: val;
}

int ocelot_reset(struct ocelot *ocelot)
{
	int err;
	u32 val;

	err = regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_INIT], 1);
	if (err)
		return err;

	err = regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);
	if (err)
		return err;

	/* MEM_INIT is a self-clearing bit. Wait for it to be cleared (should be
	 * 100us) before enabling the switch core.
	 */
	err = readx_poll_timeout(ocelot_mem_init_status, ocelot, val, !val,
				 MEM_INIT_SLEEP_US, MEM_INIT_TIMEOUT_US);
	if (err)
		return err;

	err = regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);
	if (err)
		return err;

	return regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);
}
EXPORT_SYMBOL(ocelot_reset);

int ocelot_init(struct ocelot *ocelot)
{
	int i, ret;
	u32 port;

	if (ocelot->ops->reset) {
		ret = ocelot->ops->reset(ocelot);
		if (ret) {
			dev_err(ocelot->dev, "Switch reset failed\n");
			return ret;
		}
	}

	mutex_init(&ocelot->mact_lock);
	mutex_init(&ocelot->fwd_domain_lock);
	spin_lock_init(&ocelot->ptp_clock_lock);
	spin_lock_init(&ocelot->ts_id_lock);
	spin_lock_init(&ocelot->inj_lock);
	spin_lock_init(&ocelot->xtr_lock);

	ocelot->owq = alloc_ordered_workqueue("ocelot-owq", 0);
	if (!ocelot->owq)
		return -ENOMEM;

	ret = ocelot_stats_init(ocelot);
	if (ret)
		goto err_stats_init;

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

	if (ocelot->mm_supported) {
		ret = ocelot_mm_init(ocelot);
		if (ret)
			goto err_mm_init;
	}

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

	return 0;

err_mm_init:
	ocelot_stats_deinit(ocelot);
err_stats_init:
	destroy_workqueue(ocelot->owq);
	return ret;
}
EXPORT_SYMBOL(ocelot_init);

void ocelot_deinit(struct ocelot *ocelot)
{
	ocelot_stats_deinit(ocelot);
	destroy_workqueue(ocelot->owq);
}
EXPORT_SYMBOL(ocelot_deinit);

void ocelot_deinit_port(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	skb_queue_purge(&ocelot_port->tx_skbs);
}
EXPORT_SYMBOL(ocelot_deinit_port);

MODULE_DESCRIPTION("Microsemi Ocelot switch family library");
MODULE_LICENSE("Dual MIT/GPL");
