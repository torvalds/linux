// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-host.h"

#define MXL862XX_API_WRITE(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), false, false)
#define MXL862XX_API_READ(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, false)
#define MXL862XX_API_READ_QUIET(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, true)

/* Polling interval for RMON counter accumulation. At 2.5 Gbps with
 * minimum-size (64-byte) frames, a 32-bit packet counter wraps in ~880s.
 * 2s gives a comfortable margin.
 */
#define MXL862XX_STATS_POLL_INTERVAL	(2 * HZ)

struct mxl862xx_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

#define MIB_DESC(_size, _name, _element)					\
{									\
	.size = _size,							\
	.name = _name,							\
	.offset = offsetof(struct mxl862xx_rmon_port_cnt, _element)	\
}

/* Hardware-specific counters not covered by any standardized stats callback. */
static const struct mxl862xx_mib_desc mxl862xx_mib[] = {
	MIB_DESC(1, "TxAcmDroppedPkts", tx_acm_dropped_pkts),
	MIB_DESC(1, "RxFilteredPkts", rx_filtered_pkts),
	MIB_DESC(1, "RxExtendedVlanDiscardPkts", rx_extended_vlan_discard_pkts),
	MIB_DESC(1, "MtuExceedDiscardPkts", mtu_exceed_discard_pkts),
	MIB_DESC(2, "RxBadBytes", rx_bad_bytes),
};

static const struct ethtool_rmon_hist_range mxl862xx_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 10240 },
	{}
};

#define MXL862XX_SDMA_PCTRLP(p)		(0xbc0 + ((p) * 0x6))
#define MXL862XX_SDMA_PCTRL_EN		BIT(0)

#define MXL862XX_FDMA_PCTRLP(p)		(0xa80 + ((p) * 0x6))
#define MXL862XX_FDMA_PCTRL_EN		BIT(0)

#define MXL862XX_READY_TIMEOUT_MS	10000
#define MXL862XX_READY_POLL_MS		100

#define MXL862XX_TCM_INST_SEL		0xe00
#define MXL862XX_TCM_CBS		0xe12
#define MXL862XX_TCM_EBS		0xe13

static const int mxl862xx_flood_meters[] = {
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST,
};

enum mxl862xx_evlan_action {
	EVLAN_ACCEPT,			/* pass-through, no tag removal */
	EVLAN_STRIP_IF_UNTAGGED,	/* remove 1 tag if entry's untagged flag set */
	EVLAN_PVID_OR_DISCARD,		/* insert PVID tag or discard if no PVID */
	EVLAN_STRIP1_AND_PVID_OR_DISCARD,/* strip 1 tag + insert PVID, or discard */
};

struct mxl862xx_evlan_rule_desc {
	u8 outer_type;		/* enum mxl862xx_extended_vlan_filter_type */
	u8 inner_type;		/* enum mxl862xx_extended_vlan_filter_type */
	u8 outer_tpid;		/* enum mxl862xx_extended_vlan_filter_tpid */
	u8 inner_tpid;		/* enum mxl862xx_extended_vlan_filter_tpid */
	bool match_vid;		/* true: match on VID from the vid parameter */
	u8 action;		/* enum mxl862xx_evlan_action */
};

/* Shorthand constants for readability */
#define FT_NORMAL	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NORMAL
#define FT_NO_FILTER	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_FILTER
#define FT_DEFAULT	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT
#define FT_NO_TAG	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_TAG
#define TP_NONE		MXL862XX_EXTENDEDVLAN_FILTER_TPID_NO_FILTER
#define TP_8021Q	MXL862XX_EXTENDEDVLAN_FILTER_TPID_8021Q

/*
 * VLAN-aware ingress: 7 final catchall rules.
 *
 * VLAN Filter handles VID membership for tagged frames, so the
 * Extended VLAN ingress block only needs to handle:
 * - Priority-tagged (VID=0): strip + insert PVID
 * - Untagged: insert PVID or discard
 * - Standard 802.1Q VID>0: pass through (VF handles membership)
 * - Non-8021Q TPID (0x88A8 etc.): treat as untagged
 *
 * Rule ordering is critical: the EVLAN engine scans entries in
 * ascending index order and stops at the first match.
 *
 * The 802.1Q ACCEPT rules (indices 3--4) must appear BEFORE the
 * NO_FILTER catchalls (indices 5--6). NO_FILTER matches any tag
 * regardless of TPID, so without the ACCEPT guard, it would also
 * catch standard 802.1Q VID>0 frames and corrupt them. With the
 * guard, 802.1Q VID>0 frames match the ACCEPT rules first and
 * pass through untouched; only non-8021Q TPID frames pass through
 * to the NO_FILTER catchalls.
 */
static const struct mxl862xx_evlan_rule_desc ingress_aware_final[] = {
	/* 802.1p / priority-tagged (VID 0): strip + PVID */
	{ FT_NORMAL,    FT_NORMAL, TP_8021Q, TP_8021Q, true,  EVLAN_STRIP1_AND_PVID_OR_DISCARD },
	{ FT_NORMAL,    FT_NO_TAG, TP_8021Q, TP_NONE,  true,  EVLAN_STRIP1_AND_PVID_OR_DISCARD },
	/* Untagged: PVID insertion or discard */
	{ FT_NO_TAG,    FT_NO_TAG, TP_NONE,  TP_NONE,  false, EVLAN_PVID_OR_DISCARD },
	/* 802.1Q VID>0: accept - VF handles membership.
	 * match_vid=false means any VID; VID=0 is already caught above.
	 */
	{ FT_NORMAL,    FT_NORMAL, TP_8021Q, TP_8021Q, false, EVLAN_ACCEPT },
	{ FT_NORMAL,    FT_NO_TAG, TP_8021Q, TP_NONE,  false, EVLAN_ACCEPT },
	/* Non-8021Q TPID (0x88A8 etc.): treat as untagged - strip + PVID */
	{ FT_NO_FILTER, FT_NO_FILTER, TP_NONE, TP_NONE, false, EVLAN_STRIP1_AND_PVID_OR_DISCARD },
	{ FT_NO_FILTER, FT_NO_TAG,    TP_NONE, TP_NONE, false, EVLAN_STRIP1_AND_PVID_OR_DISCARD },
};

/*
 * VID-specific accept rules (VLAN-aware, standard tag, 2 per VID).
 * Outer tag carries the VLAN; inner may or may not be present.
 */
static const struct mxl862xx_evlan_rule_desc vid_accept_standard[] = {
	{ FT_NORMAL, FT_NORMAL, TP_8021Q, TP_8021Q, true, EVLAN_STRIP_IF_UNTAGGED },
	{ FT_NORMAL, FT_NO_TAG, TP_8021Q, TP_NONE,  true, EVLAN_STRIP_IF_UNTAGGED },
};

/*
 * Egress tag-stripping rules for VLAN-unaware mode (2 per untagged VID).
 * The HW sees the MxL tag as outer; the real VLAN tag, if any, is inner.
 */
static const struct mxl862xx_evlan_rule_desc vid_accept_egress_unaware[] = {
	{ FT_NO_FILTER, FT_NORMAL, TP_NONE, TP_8021Q, true,  EVLAN_STRIP_IF_UNTAGGED },
	{ FT_NO_FILTER, FT_NO_TAG, TP_NONE, TP_NONE,  false, EVLAN_STRIP_IF_UNTAGGED },
};

static enum dsa_tag_protocol mxl862xx_get_tag_protocol(struct dsa_switch *ds,
						       int port,
						       enum dsa_tag_protocol m)
{
	return DSA_TAG_PROTO_MXL862;
}

/* PHY access via firmware relay */
static int mxl862xx_phy_read_mmd(struct mxl862xx_priv *priv, int addr,
				 int devadd, int regnum)
{
	struct mdio_relay_data param = {
		.phy = addr,
		.mmd = devadd,
		.reg = cpu_to_le16(regnum),
	};
	int ret;

	ret = MXL862XX_API_READ(priv, INT_GPHY_READ, param);
	if (ret)
		return ret;

	return le16_to_cpu(param.data);
}

static int mxl862xx_phy_write_mmd(struct mxl862xx_priv *priv, int addr,
				  int devadd, int regnum, u16 data)
{
	struct mdio_relay_data param = {
		.phy = addr,
		.mmd = devadd,
		.reg = cpu_to_le16(regnum),
		.data = cpu_to_le16(data),
	};

	return MXL862XX_API_WRITE(priv, INT_GPHY_WRITE, param);
}

static int mxl862xx_phy_read_mii_bus(struct mii_bus *bus, int addr, int regnum)
{
	return mxl862xx_phy_read_mmd(bus->priv, addr, 0, regnum);
}

static int mxl862xx_phy_write_mii_bus(struct mii_bus *bus, int addr,
				      int regnum, u16 val)
{
	return mxl862xx_phy_write_mmd(bus->priv, addr, 0, regnum, val);
}

static int mxl862xx_phy_read_c45_mii_bus(struct mii_bus *bus, int addr,
					 int devadd, int regnum)
{
	return mxl862xx_phy_read_mmd(bus->priv, addr, devadd, regnum);
}

static int mxl862xx_phy_write_c45_mii_bus(struct mii_bus *bus, int addr,
					  int devadd, int regnum, u16 val)
{
	return mxl862xx_phy_write_mmd(bus->priv, addr, devadd, regnum, val);
}

static int mxl862xx_wait_ready(struct dsa_switch *ds)
{
	struct mxl862xx_sys_fw_image_version ver = {};
	unsigned long start = jiffies, timeout;
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_cfg cfg = {};
	int ret;

	timeout = start + msecs_to_jiffies(MXL862XX_READY_TIMEOUT_MS);
	msleep(2000); /* it always takes at least 2 seconds */
	do {
		ret = MXL862XX_API_READ_QUIET(priv, SYS_MISC_FW_VERSION, ver);
		if (ret || !ver.iv_major)
			goto not_ready_yet;

		/* being able to perform CFGGET indicates that
		 * the firmware is ready
		 */
		ret = MXL862XX_API_READ_QUIET(priv,
					      MXL862XX_COMMON_CFGGET,
					      cfg);
		if (ret)
			goto not_ready_yet;

		dev_info(ds->dev, "switch ready after %ums, firmware %u.%u.%u (build %u)\n",
			 jiffies_to_msecs(jiffies - start),
			 ver.iv_major, ver.iv_minor,
			 le16_to_cpu(ver.iv_revision),
			 le32_to_cpu(ver.iv_build_num));
		return 0;

not_ready_yet:
		msleep(MXL862XX_READY_POLL_MS);
	} while (time_before(jiffies, timeout));

	dev_err(ds->dev, "switch not responding after reset\n");
	return -ETIMEDOUT;
}

static int mxl862xx_setup_mdio(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct device *dev = ds->dev;
	struct device_node *mdio_np;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->priv = priv;
	bus->name = KBUILD_MODNAME "-mii";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(dev));
	bus->read_c45 = mxl862xx_phy_read_c45_mii_bus;
	bus->write_c45 = mxl862xx_phy_write_c45_mii_bus;
	bus->read = mxl862xx_phy_read_mii_bus;
	bus->write = mxl862xx_phy_write_mii_bus;
	bus->parent = dev;
	bus->phy_mask = ~ds->phys_mii_mask;

	mdio_np = of_get_child_by_name(dev->of_node, "mdio");
	if (!mdio_np)
		return -ENODEV;

	ret = devm_of_mdiobus_register(dev, bus, mdio_np);
	of_node_put(mdio_np);

	return ret;
}

static int mxl862xx_bridge_config_fwd(struct dsa_switch *ds, u16 bridge_id,
				      bool ucast_flood, bool mcast_flood,
				      bool bcast_flood)
{
	struct mxl862xx_bridge_config bridge_config = {};
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	bridge_config.mask = cpu_to_le32(MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE);
	bridge_config.bridge_id = cpu_to_le16(bridge_id);

	bridge_config.forward_unknown_unicast = cpu_to_le32(ucast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD);

	bridge_config.forward_unknown_multicast_ip = cpu_to_le32(mcast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD);
	bridge_config.forward_unknown_multicast_non_ip =
		bridge_config.forward_unknown_multicast_ip;

	bridge_config.forward_broadcast = cpu_to_le32(bcast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGE_CONFIGSET, bridge_config);
	if (ret)
		dev_err(ds->dev, "failed to configure bridge %u forwarding: %d\n",
			bridge_id, ret);

	return ret;
}

/* Allocate a single zero-rate meter shared by all ports and flood types.
 * All flood-blocking egress sub-meters point to this one meter so that any
 * packet hitting this meter is unconditionally dropped.
 *
 * The firmware API requires CBS >= 64 (its bs2ls encoder clamps smaller
 * values), so the meter is initially configured with CBS=EBS=64.
 * A zero-rate bucket starts full at CBS bytes, which would let one packet
 * through before the bucket empties. To eliminate this one-packet leak we
 * override CBS and EBS to zero via direct register writes after the API call;
 * the hardware accepts CBS=0 and immediately flags the bucket as exceeded,
 * so no traffic can ever pass.
 */
static int mxl862xx_setup_drop_meter(struct dsa_switch *ds)
{
	struct mxl862xx_qos_meter_cfg meter = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_register_mod reg;
	int ret;

	/* meter_id=0 means auto-alloc */
	ret = MXL862XX_API_READ(priv, MXL862XX_QOS_METERALLOC, meter);
	if (ret)
		return ret;

	meter.enable = true;
	meter.cbs = cpu_to_le32(64);
	meter.ebs = cpu_to_le32(64);
	snprintf(meter.meter_name, sizeof(meter.meter_name), "drop");

	ret = MXL862XX_API_WRITE(priv, MXL862XX_QOS_METERCFGSET, meter);
	if (ret)
		return ret;

	priv->drop_meter = le16_to_cpu(meter.meter_id);

	/* Select the meter instance for subsequent TCM register access. */
	reg.addr = cpu_to_le16(MXL862XX_TCM_INST_SEL);
	reg.data = cpu_to_le16(priv->drop_meter);
	reg.mask = cpu_to_le16(0xffff);
	ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_REGISTERMOD, reg);
	if (ret)
		return ret;

	/* Zero CBS so the committed bucket starts empty (exceeded). */
	reg.addr = cpu_to_le16(MXL862XX_TCM_CBS);
	reg.data = 0;
	ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_REGISTERMOD, reg);
	if (ret)
		return ret;

	/* Zero EBS so the excess bucket starts empty (exceeded). */
	reg.addr = cpu_to_le16(MXL862XX_TCM_EBS);
	return MXL862XX_API_WRITE(priv, MXL862XX_COMMON_REGISTERMOD, reg);
}

static int mxl862xx_set_bridge_port(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_config br_port_cfg = {};
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct dsa_port *member_dp;
	u16 bridge_id;
	u16 vf_scan;
	bool enable;
	int i, idx;

	if (dsa_port_is_unused(dp))
		return 0;

	if (dsa_port_is_cpu(dp)) {
		dsa_switch_for_each_user_port(member_dp, ds) {
			if (member_dp->cpu_dp->index != port)
				continue;
			mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
						    member_dp->index);
		}
	} else if (dp->bridge) {
		dsa_switch_for_each_bridge_member(member_dp, ds,
						  dp->bridge->dev) {
			if (member_dp->index == port)
				continue;
			mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
						    member_dp->index);
		}
		mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
					    dp->cpu_dp->index);
	} else {
		mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
					    dp->cpu_dp->index);
		p->flood_block = 0;
		p->learning = false;
	}

	bridge_id = dp->bridge ? priv->bridges[dp->bridge->num] : p->fid;

	br_port_cfg.bridge_port_id = cpu_to_le16(port);
	br_port_cfg.bridge_id = cpu_to_le16(bridge_id);
	br_port_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN_FILTER |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER1 |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING);
	br_port_cfg.src_mac_learning_disable = !p->learning;

	/* Extended VLAN block assignments.
	 * Ingress: block_size is sent as-is (all entries are finals).
	 * Egress: n_active narrows the scan window to only the
	 * entries actually written by evlan_program_egress.
	 */
	br_port_cfg.ingress_extended_vlan_enable = p->ingress_evlan.in_use;
	br_port_cfg.ingress_extended_vlan_block_id =
		cpu_to_le16(p->ingress_evlan.block_id);
	br_port_cfg.ingress_extended_vlan_block_size =
		cpu_to_le16(p->ingress_evlan.block_size);
	br_port_cfg.egress_extended_vlan_enable = p->egress_evlan.in_use;
	br_port_cfg.egress_extended_vlan_block_id =
		cpu_to_le16(p->egress_evlan.block_id);
	br_port_cfg.egress_extended_vlan_block_size =
		cpu_to_le16(p->egress_evlan.n_active);

	/* VLAN Filter block assignments (per-port).
	 * The block_size sent to the firmware narrows the HW scan
	 * window to [block_id, block_id + active_count), relying on
	 * discard_unmatched_tagged for frames outside that range.
	 * When active_count=0, send 1 to scan only the DISCARD
	 * sentinel at index 0 (block_size=0 would disable narrowing
	 * and scan the entire allocated block).
	 *
	 * The bridge check ensures VF is disabled when the port
	 * leaves the bridge, without needing to prematurely clear
	 * vlan_filtering (which the DSA framework handles later via
	 * port_vlan_filtering).
	 */
	if (p->vf.allocated && p->vlan_filtering &&
	    dsa_port_bridge_dev_get(dp)) {
		vf_scan = max_t(u16, p->vf.active_count, 1);
		br_port_cfg.ingress_vlan_filter_enable = 1;
		br_port_cfg.ingress_vlan_filter_block_id =
			cpu_to_le16(p->vf.block_id);
		br_port_cfg.ingress_vlan_filter_block_size =
			cpu_to_le16(vf_scan);

		br_port_cfg.egress_vlan_filter1enable = 1;
		br_port_cfg.egress_vlan_filter1block_id =
			cpu_to_le16(p->vf.block_id);
		br_port_cfg.egress_vlan_filter1block_size =
			cpu_to_le16(vf_scan);
	} else {
		br_port_cfg.ingress_vlan_filter_enable = 0;
		br_port_cfg.egress_vlan_filter1enable = 0;
	}

	/* IVL when VLAN-aware: include VID in FDB lookup keys so that
	 * learned entries are per-VID. In VLAN-unaware mode, SVL is
	 * used (VID excluded from key).
	 */
	br_port_cfg.vlan_src_mac_vid_enable = p->vlan_filtering;
	br_port_cfg.vlan_dst_mac_vid_enable = p->vlan_filtering;

	for (i = 0; i < ARRAY_SIZE(mxl862xx_flood_meters); i++) {
		idx = mxl862xx_flood_meters[i];
		enable = !!(p->flood_block & BIT(idx));

		br_port_cfg.egress_traffic_sub_meter_id[idx] =
			enable ? cpu_to_le16(priv->drop_meter) : 0;
		br_port_cfg.egress_sub_metering_enable[idx] = enable;
	}

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET,
				  br_port_cfg);
}

static int mxl862xx_sync_bridge_members(struct dsa_switch *ds,
					const struct dsa_bridge *bridge)
{
	struct dsa_port *dp;
	int ret = 0, err;

	dsa_switch_for_each_bridge_member(dp, ds, bridge->dev) {
		err = mxl862xx_set_bridge_port(ds, dp->index);
		if (err)
			ret = err;
	}

	return ret;
}

static int mxl862xx_evlan_block_alloc(struct mxl862xx_priv *priv,
				      struct mxl862xx_evlan_block *blk)
{
	struct mxl862xx_extendedvlan_alloc param = {};
	int ret;

	param.number_of_entries = cpu_to_le16(blk->block_size);

	ret = MXL862XX_API_READ(priv, MXL862XX_EXTENDEDVLAN_ALLOC, param);
	if (ret)
		return ret;

	blk->block_id = le16_to_cpu(param.extended_vlan_block_id);
	blk->allocated = true;

	return 0;
}

static int mxl862xx_vf_block_alloc(struct mxl862xx_priv *priv,
				   u16 size, u16 *block_id)
{
	struct mxl862xx_vlanfilter_alloc param = {};
	int ret;

	param.number_of_entries = cpu_to_le16(size);
	param.discard_untagged = 0;
	param.discard_unmatched_tagged = 1;

	ret = MXL862XX_API_READ(priv, MXL862XX_VLANFILTER_ALLOC, param);
	if (ret)
		return ret;

	*block_id = le16_to_cpu(param.vlan_filter_block_id);
	return 0;
}

static int mxl862xx_vf_entry_discard(struct mxl862xx_priv *priv,
				     u16 block_id, u16 index)
{
	struct mxl862xx_vlanfilter_config cfg = {};

	cfg.vlan_filter_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(index);
	cfg.vlan_filter_mask = cpu_to_le32(MXL862XX_VLAN_FILTER_TCI_MASK_VID);
	cfg.val = cpu_to_le32(0);
	cfg.discard_matched = 1;

	return MXL862XX_API_WRITE(priv, MXL862XX_VLANFILTER_SET, cfg);
}

static int mxl862xx_vf_alloc(struct mxl862xx_priv *priv,
			     struct mxl862xx_vf_block *vf)
{
	int ret;

	ret = mxl862xx_vf_block_alloc(priv, vf->block_size, &vf->block_id);
	if (ret)
		return ret;

	vf->allocated = true;
	vf->active_count = 0;

	/* Sentinel: block VID-0 when scan window covers only index 0 */
	return mxl862xx_vf_entry_discard(priv, vf->block_id, 0);
}

static int mxl862xx_allocate_bridge(struct mxl862xx_priv *priv)
{
	struct mxl862xx_bridge_alloc br_alloc = {};
	int ret;

	ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGE_ALLOC, br_alloc);
	if (ret)
		return ret;

	return le16_to_cpu(br_alloc.bridge_id);
}

static void mxl862xx_free_bridge(struct dsa_switch *ds,
				 const struct dsa_bridge *bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	u16 fw_id = priv->bridges[bridge->num];
	struct mxl862xx_bridge_alloc br_alloc = {
		.bridge_id = cpu_to_le16(fw_id),
	};
	int ret;

	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGE_FREE, br_alloc);
	if (ret) {
		dev_err(ds->dev, "failed to free fw bridge %u: %pe\n",
			fw_id, ERR_PTR(ret));
		return;
	}

	priv->bridges[bridge->num] = 0;
}

static int mxl862xx_setup(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	int n_user_ports = 0, max_vlans;
	int ingress_finals, vid_rules;
	struct dsa_port *dp;
	int ret;

	ret = mxl862xx_reset(priv);
	if (ret)
		return ret;

	ret = mxl862xx_wait_ready(ds);
	if (ret)
		return ret;

	/* Calculate Extended VLAN block sizes.
	 * With VLAN Filter handling VID membership checks:
	 *   Ingress: only final catchall rules (PVID insertion, 802.1Q
	 *            accept, non-8021Q TPID handling, discard).
	 *            Block sized to exactly fit the finals -- no per-VID
	 *            ingress EVLAN rules are needed. (7 entries.)
	 *   Egress:  2 rules per VID that needs tag stripping (untagged VIDs).
	 *            No egress final catchalls -- VLAN Filter does the discard.
	 *   CPU:     EVLAN is left disabled on CPU ports -- frames pass
	 *            through without EVLAN processing.
	 *
	 * Total EVLAN budget:
	 *   n_user_ports * (ingress + egress) <= 1024.
	 * Ingress blocks are small (7 entries), so almost all capacity
	 * goes to egress VID rules.
	 */
	dsa_switch_for_each_user_port(dp, ds)
		n_user_ports++;

	if (n_user_ports) {
		ingress_finals = ARRAY_SIZE(ingress_aware_final);
		vid_rules = ARRAY_SIZE(vid_accept_standard);

		/* Ingress block: fixed at finals count (7 entries) */
		priv->evlan_ingress_size = ingress_finals;

		/* Egress block: remaining budget divided equally among
		 * user ports. Each untagged VID needs vid_rules (2)
		 * EVLAN entries for tag stripping. Tagged-only VIDs
		 * need no EVLAN rules at all.
		 */
		max_vlans = (MXL862XX_TOTAL_EVLAN_ENTRIES -
			     n_user_ports * ingress_finals) /
			    (n_user_ports * vid_rules);
		priv->evlan_egress_size = vid_rules * max_vlans;

		/* VLAN Filter block: one per user port. The 1024-entry
		 * table is divided equally among user ports. Each port
		 * gets its own VF block for per-port VID membership --
		 * discard_unmatched_tagged handles the rest.
		 */
		priv->vf_block_size = MXL862XX_TOTAL_VF_ENTRIES / n_user_ports;
	}

	ret = mxl862xx_setup_drop_meter(ds);
	if (ret)
		return ret;

	schedule_delayed_work(&priv->stats_work,
			      MXL862XX_STATS_POLL_INTERVAL);

	return mxl862xx_setup_mdio(ds);
}

static int mxl862xx_port_state(struct dsa_switch *ds, int port, bool enable)
{
	struct mxl862xx_register_mod sdma = {
		.addr = cpu_to_le16(MXL862XX_SDMA_PCTRLP(port)),
		.data = cpu_to_le16(enable ? MXL862XX_SDMA_PCTRL_EN : 0),
		.mask = cpu_to_le16(MXL862XX_SDMA_PCTRL_EN),
	};
	struct mxl862xx_register_mod fdma = {
		.addr = cpu_to_le16(MXL862XX_FDMA_PCTRLP(port)),
		.data = cpu_to_le16(enable ? MXL862XX_FDMA_PCTRL_EN : 0),
		.mask = cpu_to_le16(MXL862XX_FDMA_PCTRL_EN),
	};
	int ret;

	ret = MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_REGISTERMOD, sdma);
	if (ret)
		return ret;

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_REGISTERMOD, fdma);
}

static int mxl862xx_port_enable(struct dsa_switch *ds, int port,
				struct phy_device *phydev)
{
	return mxl862xx_port_state(ds, port, true);
}

static void mxl862xx_port_disable(struct dsa_switch *ds, int port)
{
	if (mxl862xx_port_state(ds, port, false))
		dev_err(ds->dev, "failed to disable port %d\n", port);
}

static void mxl862xx_port_fast_age(struct dsa_switch *ds, int port)
{
	struct mxl862xx_mac_table_clear param = {
		.type = MXL862XX_MAC_CLEAR_PHY_PORT,
		.port_id = port,
	};

	if (MXL862XX_API_WRITE(ds->priv, MXL862XX_MAC_TABLECLEARCOND, param))
		dev_err(ds->dev, "failed to clear fdb on port %d\n", port);
}

static int mxl862xx_configure_ctp_port(struct dsa_switch *ds, int port,
				       u16 first_ctp_port_id,
				       u16 number_of_ctp_ports)
{
	struct mxl862xx_ctp_port_assignment ctp_assign = {
		.logical_port_id = port,
		.first_ctp_port_id = cpu_to_le16(first_ctp_port_id),
		.number_of_ctp_port = cpu_to_le16(number_of_ctp_ports),
		.mode = cpu_to_le32(MXL862XX_LOGICAL_PORT_ETHERNET),
	};

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_CTP_PORTASSIGNMENTSET,
				  ctp_assign);
}

static int mxl862xx_configure_sp_tag_proto(struct dsa_switch *ds, int port,
					   bool enable)
{
	struct mxl862xx_ss_sp_tag tag = {
		.pid = port,
		.mask = MXL862XX_SS_SP_TAG_MASK_RX | MXL862XX_SS_SP_TAG_MASK_TX,
		.rx = enable ? MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT :
			       MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT,
		.tx = enable ? MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE :
			       MXL862XX_SS_SP_TAG_TX_TAG_REMOVE,
	};

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_SS_SPTAG_SET, tag);
}

static int mxl862xx_evlan_write_rule(struct mxl862xx_priv *priv,
				     u16 block_id, u16 entry_index,
				     const struct mxl862xx_evlan_rule_desc *desc,
				     u16 vid, bool untagged, u16 pvid)
{
	struct mxl862xx_extendedvlan_config cfg = {};
	struct mxl862xx_extendedvlan_filter_vlan *fv;

	cfg.extended_vlan_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(entry_index);

	/* Populate filter */
	cfg.filter.outer_vlan.type = cpu_to_le32(desc->outer_type);
	cfg.filter.inner_vlan.type = cpu_to_le32(desc->inner_type);
	cfg.filter.outer_vlan.tpid = cpu_to_le32(desc->outer_tpid);
	cfg.filter.inner_vlan.tpid = cpu_to_le32(desc->inner_tpid);

	if (desc->match_vid) {
		/* For egress unaware: outer=NO_FILTER, match on inner tag */
		if (desc->outer_type == FT_NO_FILTER)
			fv = &cfg.filter.inner_vlan;
		else
			fv = &cfg.filter.outer_vlan;

		fv->vid_enable = 1;
		fv->vid_val = cpu_to_le32(vid);
	}

	/* Populate treatment based on action */
	switch (desc->action) {
	case EVLAN_ACCEPT:
		cfg.treatment.remove_tag =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
		break;

	case EVLAN_STRIP_IF_UNTAGGED:
		cfg.treatment.remove_tag = cpu_to_le32(untagged ?
			MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG :
			MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
		break;

	case EVLAN_PVID_OR_DISCARD:
		if (pvid) {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
			cfg.treatment.add_outer_vlan = 1;
			cfg.treatment.outer_vlan.vid_mode =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL);
			cfg.treatment.outer_vlan.vid_val = cpu_to_le32(pvid);
			cfg.treatment.outer_vlan.tpid =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q);
		} else {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);
		}
		break;

	case EVLAN_STRIP1_AND_PVID_OR_DISCARD:
		if (pvid) {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG);
			cfg.treatment.add_outer_vlan = 1;
			cfg.treatment.outer_vlan.vid_mode =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL);
			cfg.treatment.outer_vlan.vid_val = cpu_to_le32(pvid);
			cfg.treatment.outer_vlan.tpid =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q);
		} else {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);
		}
		break;
	}

	return MXL862XX_API_WRITE(priv, MXL862XX_EXTENDEDVLAN_SET, cfg);
}

static int mxl862xx_evlan_deactivate_entry(struct mxl862xx_priv *priv,
					   u16 block_id, u16 entry_index)
{
	struct mxl862xx_extendedvlan_config cfg = {};

	cfg.extended_vlan_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(entry_index);

	/* Use an unreachable filter (DEFAULT+DEFAULT) with DISCARD treatment.
	 * A zeroed entry would have NORMAL+NORMAL filter which matches
	 * real double-tagged traffic and passes it through.
	 */
	cfg.filter.outer_vlan.type =
		cpu_to_le32(MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT);
	cfg.filter.inner_vlan.type =
		cpu_to_le32(MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT);
	cfg.treatment.remove_tag =
		cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);

	return MXL862XX_API_WRITE(priv, MXL862XX_EXTENDEDVLAN_SET, cfg);
}

static int mxl862xx_evlan_write_final_rules(struct mxl862xx_priv *priv,
					    struct mxl862xx_evlan_block *blk,
					    const struct mxl862xx_evlan_rule_desc *rules,
					    int n_rules, u16 pvid)
{
	u16 start_idx = blk->block_size - n_rules;
	int i, ret;

	for (i = 0; i < n_rules; i++) {
		ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
						start_idx + i, &rules[i],
						0, false, pvid);
		if (ret)
			return ret;
	}

	return 0;
}

static int mxl862xx_vf_entry_set(struct mxl862xx_priv *priv,
				 u16 block_id, u16 index, u16 vid)
{
	struct mxl862xx_vlanfilter_config cfg = {};

	cfg.vlan_filter_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(index);
	cfg.vlan_filter_mask = cpu_to_le32(MXL862XX_VLAN_FILTER_TCI_MASK_VID);
	cfg.val = cpu_to_le32(vid);
	cfg.discard_matched = 0;

	return MXL862XX_API_WRITE(priv, MXL862XX_VLANFILTER_SET, cfg);
}

static struct mxl862xx_vf_vid *mxl862xx_vf_find_vid(struct mxl862xx_vf_block *vf,
						    u16 vid)
{
	struct mxl862xx_vf_vid *ve;

	list_for_each_entry(ve, &vf->vids, list)
		if (ve->vid == vid)
			return ve;

	return NULL;
}

static int mxl862xx_vf_add_vid(struct mxl862xx_priv *priv,
			       struct mxl862xx_vf_block *vf,
			       u16 vid, bool untagged)
{
	struct mxl862xx_vf_vid *ve;
	int ret;

	ve = mxl862xx_vf_find_vid(vf, vid);
	if (ve) {
		ve->untagged = untagged;
		return 0;
	}

	if (vf->active_count >= vf->block_size)
		return -ENOSPC;

	ve = kzalloc_obj(*ve);
	if (!ve)
		return -ENOMEM;

	ve->vid = vid;
	ve->index = vf->active_count;
	ve->untagged = untagged;

	ret = mxl862xx_vf_entry_set(priv, vf->block_id, ve->index, vid);
	if (ret) {
		kfree(ve);
		return ret;
	}

	list_add_tail(&ve->list, &vf->vids);
	vf->active_count++;

	return 0;
}

static int mxl862xx_vf_del_vid(struct mxl862xx_priv *priv,
			       struct mxl862xx_vf_block *vf, u16 vid)
{
	struct mxl862xx_vf_vid *ve, *last_ve;
	u16 gap, last;
	int ret;

	ve = mxl862xx_vf_find_vid(vf, vid);
	if (!ve)
		return 0;

	if (!vf->allocated) {
		/* Software-only state -- just remove the tracking entry */
		list_del(&ve->list);
		kfree(ve);
		vf->active_count--;
		return 0;
	}

	gap = ve->index;
	last = vf->active_count - 1;

	if (vf->active_count == 1) {
		/* Last VID -- restore DISCARD sentinel at index 0 */
		ret = mxl862xx_vf_entry_discard(priv, vf->block_id, 0);
		if (ret)
			return ret;
	} else if (gap < last) {
		/* Swap: move the last ALLOW entry into the gap */
		list_for_each_entry(last_ve, &vf->vids, list)
			if (last_ve->index == last)
				break;

		if (WARN_ON(list_entry_is_head(last_ve, &vf->vids, list)))
			return -EINVAL;

		ret = mxl862xx_vf_entry_set(priv, vf->block_id,
					    gap, last_ve->vid);
		if (ret)
			return ret;

		last_ve->index = gap;
	}

	list_del(&ve->list);
	kfree(ve);
	vf->active_count--;

	return 0;
}

static int mxl862xx_evlan_program_ingress(struct mxl862xx_priv *priv, int port)
{
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_evlan_block *blk = &p->ingress_evlan;

	if (!p->vlan_filtering)
		return 0;

	blk->in_use = true;
	blk->n_active = blk->block_size;

	return mxl862xx_evlan_write_final_rules(priv, blk,
						ingress_aware_final,
						ARRAY_SIZE(ingress_aware_final),
						p->pvid);
}

static int mxl862xx_evlan_program_egress(struct mxl862xx_priv *priv, int port)
{
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_evlan_block *blk = &p->egress_evlan;
	const struct mxl862xx_evlan_rule_desc *vid_rules;
	struct mxl862xx_vf_vid *vfv;
	u16 old_active = blk->n_active;
	u16 idx = 0, i;
	int n_vid, ret;

	if (p->vlan_filtering) {
		vid_rules = vid_accept_standard;
		n_vid = ARRAY_SIZE(vid_accept_standard);
	} else {
		vid_rules = vid_accept_egress_unaware;
		n_vid = ARRAY_SIZE(vid_accept_egress_unaware);
	}

	list_for_each_entry(vfv, &p->vf.vids, list) {
		if (!vfv->untagged)
			continue;

		if (idx + n_vid > blk->block_size)
			return -ENOSPC;

		ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
						idx++, &vid_rules[0],
						vfv->vid, vfv->untagged,
						p->pvid);
		if (ret)
			return ret;

		if (n_vid > 1) {
			ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
							idx++, &vid_rules[1],
							vfv->vid,
							vfv->untagged,
							p->pvid);
			if (ret)
				return ret;
		}
	}

	/* Deactivate stale entries that are no longer needed.
	 * This closes the brief window between writing the new rules
	 * and set_bridge_port narrowing the scan window.
	 */
	for (i = idx; i < old_active; i++) {
		ret = mxl862xx_evlan_deactivate_entry(priv,
						      blk->block_id,
						      i);
		if (ret)
			return ret;
	}

	blk->n_active = idx;
	blk->in_use = idx > 0;

	return 0;
}

static int mxl862xx_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering,
					struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	bool old_vlan_filtering = p->vlan_filtering;
	bool old_in_use = p->ingress_evlan.in_use;
	bool changed = (p->vlan_filtering != vlan_filtering);
	int ret;

	p->vlan_filtering = vlan_filtering;

	if (changed) {
		/* When leaving VLAN-aware mode, release the ingress HW
		 * block. The firmware passes frames through unchanged
		 * when no ingress EVLAN block is assigned, so the block
		 * is unnecessary in unaware mode.
		 */
		if (!vlan_filtering)
			p->ingress_evlan.in_use = false;

		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			goto err_restore;

		ret = mxl862xx_evlan_program_egress(priv, port);
		if (ret)
			goto err_restore;
	}

	return mxl862xx_set_bridge_port(ds, port);

	/* No HW rollback -- restoring SW state is sufficient for a correct retry. */
err_restore:
	p->vlan_filtering = old_vlan_filtering;
	p->ingress_evlan.in_use = old_in_use;
	return ret;
}

static int mxl862xx_port_vlan_add(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan,
				  struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	bool untagged = !!(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
	u16 vid = vlan->vid;
	u16 old_pvid = p->pvid;
	bool pvid_changed = false;
	int ret;

	/* CPU port is VLAN-transparent: the SP tag handles port
	 * identification and the host-side DSA tagger manages VLAN
	 * delivery. Egress EVLAN catchalls are set up once in
	 * setup_cpu_bridge; no per-VID VF/EVLAN programming needed.
	 */
	if (dsa_is_cpu_port(ds, port))
		return 0;

	/* Update PVID tracking */
	if (vlan->flags & BRIDGE_VLAN_INFO_PVID) {
		if (p->pvid != vid) {
			p->pvid = vid;
			pvid_changed = true;
		}
	} else if (p->pvid == vid) {
		p->pvid = 0;
		pvid_changed = true;
	}

	/* Add/update VID in this port's VLAN Filter block.
	 * VF must be updated before programming egress EVLAN because
	 * evlan_program_egress walks the VF VID list.
	 */
	ret = mxl862xx_vf_add_vid(priv, &p->vf, vid, untagged);
	if (ret)
		goto err_pvid;

	/* Reprogram ingress finals if PVID changed */
	if (pvid_changed) {
		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			goto err_rollback;
	}

	/* Reprogram egress tag-stripping rules (walks VF VID list) */
	ret = mxl862xx_evlan_program_egress(priv, port);
	if (ret)
		goto err_rollback;

	/* Apply VLAN block IDs and MAC learning flags to bridge port */
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		goto err_rollback;

	return 0;

err_rollback:
	/* Best-effort: undo VF add and restore consistent hardware state.
	 * A retry of port_vlan_add will converge since vf_add_vid is
	 * idempotent.
	 */
	p->pvid = old_pvid;
	mxl862xx_vf_del_vid(priv, &p->vf, vid);
	mxl862xx_evlan_program_ingress(priv, port);
	mxl862xx_evlan_program_egress(priv, port);
	mxl862xx_set_bridge_port(ds, port);
	return ret;
err_pvid:
	p->pvid = old_pvid;
	return ret;
}

static int mxl862xx_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_vf_vid *ve;
	bool pvid_changed = false;
	u16 vid = vlan->vid;
	bool old_untagged;
	u16 old_pvid;
	int ret;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	ve = mxl862xx_vf_find_vid(&p->vf, vid);
	if (!ve)
		return 0;
	old_untagged = ve->untagged;
	old_pvid = p->pvid;

	/* Clear PVID if we're deleting it */
	if (p->pvid == vid) {
		p->pvid = 0;
		pvid_changed = true;
	}

	/* Remove VID from this port's VLAN Filter block.
	 * Must happen before egress reprogram so the VID is no
	 * longer in the list that evlan_program_egress walks.
	 */
	ret = mxl862xx_vf_del_vid(priv, &p->vf, vid);
	if (ret)
		goto err_pvid;

	/* Reprogram egress tag-stripping rules (VID is now gone) */
	ret = mxl862xx_evlan_program_egress(priv, port);
	if (ret)
		goto err_rollback;

	/* If PVID changed, reprogram ingress finals */
	if (pvid_changed) {
		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			goto err_rollback;
	}

	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		goto err_rollback;

	return 0;

err_rollback:
	/* Best-effort: re-add the VID and restore consistent hardware
	 * state. A retry of port_vlan_del will converge.
	 */
	p->pvid = old_pvid;
	mxl862xx_vf_add_vid(priv, &p->vf, vid, old_untagged);
	mxl862xx_evlan_program_egress(priv, port);
	mxl862xx_evlan_program_ingress(priv, port);
	mxl862xx_set_bridge_port(ds, port);
	return ret;
err_pvid:
	p->pvid = old_pvid;
	return ret;
}

static int mxl862xx_setup_cpu_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];

	p->fid = MXL862XX_DEFAULT_BRIDGE;
	p->learning = true;

	/* EVLAN is left disabled on CPU ports -- frames pass through
	 * without EVLAN processing. Only the portmap and bridge
	 * assignment need to be configured.
	 */

	return mxl862xx_set_bridge_port(ds, port);
}

static int mxl862xx_port_bridge_join(struct dsa_switch *ds, int port,
				     const struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	if (!priv->bridges[bridge.num]) {
		ret = mxl862xx_allocate_bridge(priv);
		if (ret < 0)
			return ret;

		priv->bridges[bridge.num] = ret;

		/* Free bridge here on error, DSA rollback won't. */
		ret = mxl862xx_sync_bridge_members(ds, &bridge);
		if (ret) {
			mxl862xx_free_bridge(ds, &bridge);
			return ret;
		}

		return 0;
	}

	return mxl862xx_sync_bridge_members(ds, &bridge);
}

static void mxl862xx_port_bridge_leave(struct dsa_switch *ds, int port,
				       const struct dsa_bridge bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	int err;

	err = mxl862xx_sync_bridge_members(ds, &bridge);
	if (err)
		dev_err(ds->dev,
			"failed to sync bridge members after port %d left: %pe\n",
			port, ERR_PTR(err));

	/* Revert leaving port, omitted by the sync above, to its
	 * single-port bridge
	 */
	p->pvid = 0;
	p->ingress_evlan.in_use = false;
	p->egress_evlan.in_use = false;

	err = mxl862xx_set_bridge_port(ds, port);
	if (err)
		dev_err(ds->dev,
			"failed to update bridge port %d state: %pe\n", port,
			ERR_PTR(err));

	if (!dsa_bridge_ports(ds, bridge.dev))
		mxl862xx_free_bridge(ds, &bridge);
}

static int mxl862xx_port_setup(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);
	bool is_cpu_port = dsa_port_is_cpu(dp);
	int ret;

	ret = mxl862xx_port_state(ds, port, false);
	if (ret)
		return ret;

	mxl862xx_port_fast_age(ds, port);

	if (dsa_port_is_unused(dp))
		return 0;

	if (dsa_port_is_dsa(dp)) {
		dev_err(ds->dev, "port %d: DSA links not supported\n", port);
		return -EOPNOTSUPP;
	}

	ret = mxl862xx_configure_sp_tag_proto(ds, port, is_cpu_port);
	if (ret)
		return ret;

	ret = mxl862xx_configure_ctp_port(ds, port, port,
					  is_cpu_port ? 32 - port : 1);
	if (ret)
		return ret;

	if (is_cpu_port)
		return mxl862xx_setup_cpu_bridge(ds, port);

	/* setup single-port bridge for user ports.
	 * If this fails, the FID is leaked -- but the port then transitions
	 * to unused, and the FID pool is sized to tolerate this.
	 */
	ret = mxl862xx_allocate_bridge(priv);
	if (ret < 0) {
		dev_err(ds->dev, "failed to allocate a bridge for port %d\n", port);
		return ret;
	}
	priv->ports[port].fid = ret;
	/* Standalone ports should not flood unknown unicast or multicast
	 * towards the CPU by default; only broadcast is needed initially.
	 */
	ret = mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
					 false, false, true);
	if (ret)
		return ret;
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		return ret;

	priv->ports[port].ingress_evlan.block_size = priv->evlan_ingress_size;
	ret = mxl862xx_evlan_block_alloc(priv, &priv->ports[port].ingress_evlan);
	if (ret)
		return ret;

	priv->ports[port].egress_evlan.block_size = priv->evlan_egress_size;
	ret = mxl862xx_evlan_block_alloc(priv, &priv->ports[port].egress_evlan);
	if (ret)
		return ret;

	priv->ports[port].vf.block_size = priv->vf_block_size;
	INIT_LIST_HEAD(&priv->ports[port].vf.vids);
	ret = mxl862xx_vf_alloc(priv, &priv->ports[port].vf);
	if (ret)
		return ret;

	priv->ports[port].setup_done = true;

	return 0;
}

static void mxl862xx_port_teardown(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (dsa_port_is_unused(dp))
		return;

	/* Prevent deferred host_flood_work from acting on stale state.
	 * The flag is checked under rtnl_lock() by the worker; since
	 * teardown also runs under RTNL, this is race-free.
	 *
	 * HW EVLAN/VF blocks are not freed here -- the firmware receives
	 * a full reset on the next probe, which reclaims all resources.
	 */
	priv->ports[port].setup_done = false;
}

static void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
				      struct phylink_config *config)
{
	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE | MAC_10 |
				   MAC_100 | MAC_1000 | MAC_2500FD;

	__set_bit(PHY_INTERFACE_MODE_INTERNAL,
		  config->supported_interfaces);
}

static int mxl862xx_get_fid(struct dsa_switch *ds, struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;

	switch (db.type) {
	case DSA_DB_PORT:
		return priv->ports[db.dp->index].fid;

	case DSA_DB_BRIDGE:
		if (!priv->bridges[db.bridge.num])
			return -ENOENT;
		return priv->bridges[db.bridge.num];

	default:
		return -EOPNOTSUPP;
	}
}

static int mxl862xx_port_fdb_add(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct mxl862xx_mac_table_add param = {};
	int fid = mxl862xx_get_fid(ds, db), ret;
	struct mxl862xx_priv *priv = ds->priv;

	if (fid < 0)
		return fid;

	param.port_id = cpu_to_le32(port);
	param.static_entry = true;
	param.fid = cpu_to_le16(fid);
	param.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));
	ether_addr_copy(param.mac, addr);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, param);
	if (ret)
		dev_err(ds->dev, "failed to add FDB entry on port %d\n", port);

	return ret;
}

static int mxl862xx_port_fdb_del(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, const struct dsa_db db)
{
	struct mxl862xx_mac_table_remove param = {};
	int fid = mxl862xx_get_fid(ds, db), ret;
	struct mxl862xx_priv *priv = ds->priv;

	if (fid < 0)
		return fid;

	param.fid = cpu_to_le16(fid);
	param.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));
	ether_addr_copy(param.mac, addr);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYREMOVE, param);
	if (ret)
		dev_err(ds->dev, "failed to remove FDB entry on port %d\n", port);

	return ret;
}

static int mxl862xx_port_fdb_dump(struct dsa_switch *ds, int port,
				  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mxl862xx_mac_table_read param = { .initial = 1 };
	struct mxl862xx_priv *priv = ds->priv;
	u32 entry_port_id;
	int ret;

	while (true) {
		ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYREAD, param);
		if (ret)
			return ret;

		if (param.last)
			break;

		entry_port_id = le32_to_cpu(param.port_id);

		if (entry_port_id == port) {
			ret = cb(param.mac, FIELD_GET(MXL862XX_TCI_VLAN_ID,
						      le16_to_cpu(param.tci)),
				 param.static_entry, data);
			if (ret)
				return ret;
		}

		memset(&param, 0, sizeof(param));
	}

	return 0;
}

static int mxl862xx_port_mdb_add(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 const struct dsa_db db)
{
	struct mxl862xx_mac_table_query qparam = {};
	struct mxl862xx_mac_table_add aparam = {};
	struct mxl862xx_priv *priv = ds->priv;
	int fid, ret;

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	ether_addr_copy(qparam.mac, mdb->addr);
	qparam.fid = cpu_to_le16(fid);
	qparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, mdb->vid));

	ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYQUERY, qparam);
	if (ret)
		return ret;

	/* Build the ADD command using portmap mode */
	ether_addr_copy(aparam.mac, mdb->addr);
	aparam.fid = cpu_to_le16(fid);
	aparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, mdb->vid));
	aparam.static_entry = true;
	aparam.port_id = cpu_to_le32(MXL862XX_PORTMAP_FLAG);

	if (qparam.found)
		memcpy(aparam.port_map, qparam.port_map,
		       sizeof(aparam.port_map));

	mxl862xx_fw_portmap_set_bit(aparam.port_map, port);

	return MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, aparam);
}

static int mxl862xx_port_mdb_del(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 const struct dsa_db db)
{
	struct mxl862xx_mac_table_remove rparam = {};
	struct mxl862xx_mac_table_query qparam = {};
	struct mxl862xx_mac_table_add aparam = {};
	int fid = mxl862xx_get_fid(ds, db), ret;
	struct mxl862xx_priv *priv = ds->priv;

	if (fid < 0)
		return fid;

	qparam.fid = cpu_to_le16(fid);
	qparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, mdb->vid));
	ether_addr_copy(qparam.mac, mdb->addr);

	ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYQUERY, qparam);
	if (ret)
		return ret;

	if (!qparam.found)
		return 0;

	mxl862xx_fw_portmap_clear_bit(qparam.port_map, port);

	if (mxl862xx_fw_portmap_is_empty(qparam.port_map)) {
		rparam.fid = cpu_to_le16(fid);
		rparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, mdb->vid));
		ether_addr_copy(rparam.mac, mdb->addr);
		ret = MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYREMOVE, rparam);
	} else {
		/* Write back with reduced portmap */
		aparam.fid = cpu_to_le16(fid);
		aparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, mdb->vid));
		ether_addr_copy(aparam.mac, mdb->addr);
		aparam.static_entry = true;
		aparam.port_id = cpu_to_le32(MXL862XX_PORTMAP_FLAG);
		memcpy(aparam.port_map, qparam.port_map, sizeof(aparam.port_map));
		ret = MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, aparam);
	}

	return ret;
}

static int mxl862xx_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct mxl862xx_cfg param = {};
	int ret;

	ret = MXL862XX_API_READ(ds->priv, MXL862XX_COMMON_CFGGET, param);
	if (ret) {
		dev_err(ds->dev, "failed to read switch config\n");
		return ret;
	}

	param.mac_table_age_timer = cpu_to_le32(MXL862XX_AGETIMER_CUSTOM);
	param.age_timer = cpu_to_le32(msecs / 1000);
	ret = MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_CFGSET, param);
	if (ret)
		dev_err(ds->dev, "failed to set ageing\n");

	return ret;
}

static void mxl862xx_port_stp_state_set(struct dsa_switch *ds, int port,
					u8 state)
{
	struct mxl862xx_stp_port_cfg param = {
		.port_id = cpu_to_le16(port),
	};
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	switch (state) {
	case BR_STATE_DISABLED:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_DISABLE);
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_BLOCKING);
		break;
	case BR_STATE_LEARNING:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_LEARNING);
		break;
	case BR_STATE_FORWARDING:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_FORWARD);
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ret = MXL862XX_API_WRITE(priv, MXL862XX_STP_PORTCFGSET, param);
	if (ret) {
		dev_err(ds->dev, "failed to set STP state on port %d\n", port);
		return;
	}

	/* The firmware may re-enable MAC learning as a side-effect of entering
	 * LEARNING or FORWARDING state (per 802.1D defaults).
	 * Re-apply the driver's intended learning and metering config so that
	 * standalone ports keep learning disabled.
	 */
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		dev_err(ds->dev, "failed to reapply brport flags on port %d\n",
			port);

	mxl862xx_port_fast_age(ds, port);
}

/* Deferred work handler for host flood configuration.
 *
 * port_set_host_flood is called from atomic context (under
 * netif_addr_lock), so firmware calls must be deferred. The worker
 * acquires rtnl_lock() to serialize with DSA callbacks that access the
 * same driver state.
 */
static void mxl862xx_host_flood_work_fn(struct work_struct *work)
{
	struct mxl862xx_port *p = container_of(work, struct mxl862xx_port,
					       host_flood_work);
	struct mxl862xx_priv *priv = p->priv;
	struct dsa_switch *ds = priv->ds;

	rtnl_lock();

	/* Port may have been torn down between scheduling and now. */
	if (!p->setup_done) {
		rtnl_unlock();
		return;
	}

	/* Always write to the standalone FID. When standalone it takes effect
	 * immediately; when bridged the port uses the shared bridge FID so the
	 * write is a no-op for current forwarding, but the state is preserved
	 * in hardware and is ready once the port returns to standalone.
	 */
	mxl862xx_bridge_config_fwd(ds, p->fid, p->host_flood_uc,
				   p->host_flood_mc, true);

	rtnl_unlock();
}

static void mxl862xx_port_set_host_flood(struct dsa_switch *ds, int port,
					 bool uc, bool mc)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];

	p->host_flood_uc = uc;
	p->host_flood_mc = mc;
	schedule_work(&p->host_flood_work);
}

static int mxl862xx_port_pre_bridge_flags(struct dsa_switch *ds, int port,
					  const struct switchdev_brport_flags flags,
					  struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD |
			   BR_LEARNING))
		return -EINVAL;

	return 0;
}

static int mxl862xx_port_bridge_flags(struct dsa_switch *ds, int port,
				      const struct switchdev_brport_flags flags,
				      struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	unsigned long old_block = priv->ports[port].flood_block;
	unsigned long block = old_block;
	int ret;

	if (flags.mask & BR_FLOOD) {
		if (flags.val & BR_FLOOD)
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC);
		else
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC);
	}

	if (flags.mask & BR_MCAST_FLOOD) {
		if (flags.val & BR_MCAST_FLOOD) {
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP);
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
		} else {
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP);
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
		}
	}

	if (flags.mask & BR_BCAST_FLOOD) {
		if (flags.val & BR_BCAST_FLOOD)
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST);
		else
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST);
	}

	if (flags.mask & BR_LEARNING)
		priv->ports[port].learning = !!(flags.val & BR_LEARNING);

	if (block != old_block || (flags.mask & BR_LEARNING)) {
		priv->ports[port].flood_block = block;
		ret = mxl862xx_set_bridge_port(ds, port);
		if (ret)
			return ret;
	}

	return 0;
}

static void mxl862xx_get_strings(struct dsa_switch *ds, int port,
				 u32 stringset, u8 *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(mxl862xx_mib); i++)
		ethtool_puts(&data, mxl862xx_mib[i].name);
}

static int mxl862xx_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(mxl862xx_mib);
}

static int mxl862xx_read_rmon(struct dsa_switch *ds, int port,
			      struct mxl862xx_rmon_port_cnt *cnt)
{
	memset(cnt, 0, sizeof(*cnt));
	cnt->port_type = cpu_to_le32(MXL862XX_CTP_PORT);
	cnt->port_id = cpu_to_le16(port);

	return MXL862XX_API_READ(ds->priv, MXL862XX_RMON_PORT_GET, *cnt);
}

static void mxl862xx_get_ethtool_stats(struct dsa_switch *ds, int port,
				       u64 *data)
{
	const struct mxl862xx_mib_desc *mib;
	struct mxl862xx_rmon_port_cnt cnt;
	int ret, i;
	void *field;

	ret = mxl862xx_read_rmon(ds, port, &cnt);
	if (ret) {
		dev_err(ds->dev, "failed to read RMON stats on port %d\n", port);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(mxl862xx_mib); i++) {
		mib = &mxl862xx_mib[i];
		field = (u8 *)&cnt + mib->offset;

		if (mib->size == 1)
			*data++ = le32_to_cpu(*(__le32 *)field);
		else
			*data++ = le64_to_cpu(*(__le64 *)field);
	}
}

static void mxl862xx_get_eth_mac_stats(struct dsa_switch *ds, int port,
				       struct ethtool_eth_mac_stats *mac_stats)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	mac_stats->FramesTransmittedOK = le32_to_cpu(cnt.tx_good_pkts);
	mac_stats->SingleCollisionFrames = le32_to_cpu(cnt.tx_single_coll_count);
	mac_stats->MultipleCollisionFrames = le32_to_cpu(cnt.tx_mult_coll_count);
	mac_stats->FramesReceivedOK = le32_to_cpu(cnt.rx_good_pkts);
	mac_stats->FrameCheckSequenceErrors = le32_to_cpu(cnt.rx_fcserror_pkts);
	mac_stats->AlignmentErrors = le32_to_cpu(cnt.rx_align_error_pkts);
	mac_stats->OctetsTransmittedOK = le64_to_cpu(cnt.tx_good_bytes);
	mac_stats->LateCollisions = le32_to_cpu(cnt.tx_late_coll_count);
	mac_stats->FramesAbortedDueToXSColls = le32_to_cpu(cnt.tx_excess_coll_count);
	mac_stats->OctetsReceivedOK = le64_to_cpu(cnt.rx_good_bytes);
	mac_stats->MulticastFramesXmittedOK = le32_to_cpu(cnt.tx_multicast_pkts);
	mac_stats->BroadcastFramesXmittedOK = le32_to_cpu(cnt.tx_broadcast_pkts);
	mac_stats->MulticastFramesReceivedOK = le32_to_cpu(cnt.rx_multicast_pkts);
	mac_stats->BroadcastFramesReceivedOK = le32_to_cpu(cnt.rx_broadcast_pkts);
	mac_stats->FrameTooLongErrors = le32_to_cpu(cnt.rx_oversize_error_pkts);
}

static void mxl862xx_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
					struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	ctrl_stats->MACControlFramesTransmitted = le32_to_cpu(cnt.tx_pause_count);
	ctrl_stats->MACControlFramesReceived = le32_to_cpu(cnt.rx_good_pause_pkts);
}

static void mxl862xx_get_pause_stats(struct dsa_switch *ds, int port,
				     struct ethtool_pause_stats *pause_stats)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	pause_stats->tx_pause_frames = le32_to_cpu(cnt.tx_pause_count);
	pause_stats->rx_pause_frames = le32_to_cpu(cnt.rx_good_pause_pkts);
}

static void mxl862xx_get_rmon_stats(struct dsa_switch *ds, int port,
				    struct ethtool_rmon_stats *rmon_stats,
				    const struct ethtool_rmon_hist_range **ranges)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	rmon_stats->undersize_pkts = le32_to_cpu(cnt.rx_under_size_good_pkts);
	rmon_stats->oversize_pkts = le32_to_cpu(cnt.rx_oversize_good_pkts);
	rmon_stats->fragments = le32_to_cpu(cnt.rx_under_size_error_pkts);
	rmon_stats->jabbers = le32_to_cpu(cnt.rx_oversize_error_pkts);

	rmon_stats->hist[0] = le32_to_cpu(cnt.rx64byte_pkts);
	rmon_stats->hist[1] = le32_to_cpu(cnt.rx127byte_pkts);
	rmon_stats->hist[2] = le32_to_cpu(cnt.rx255byte_pkts);
	rmon_stats->hist[3] = le32_to_cpu(cnt.rx511byte_pkts);
	rmon_stats->hist[4] = le32_to_cpu(cnt.rx1023byte_pkts);
	rmon_stats->hist[5] = le32_to_cpu(cnt.rx_max_byte_pkts);

	rmon_stats->hist_tx[0] = le32_to_cpu(cnt.tx64byte_pkts);
	rmon_stats->hist_tx[1] = le32_to_cpu(cnt.tx127byte_pkts);
	rmon_stats->hist_tx[2] = le32_to_cpu(cnt.tx255byte_pkts);
	rmon_stats->hist_tx[3] = le32_to_cpu(cnt.tx511byte_pkts);
	rmon_stats->hist_tx[4] = le32_to_cpu(cnt.tx1023byte_pkts);
	rmon_stats->hist_tx[5] = le32_to_cpu(cnt.tx_max_byte_pkts);

	*ranges = mxl862xx_rmon_ranges;
}

/* Compute the delta between two 32-bit free-running counter snapshots,
 * handling a single wrap-around correctly via unsigned subtraction.
 */
static u64 mxl862xx_delta32(u32 cur, u32 prev)
{
	return (u32)(cur - prev);
}

/**
 * mxl862xx_stats_poll - Read RMON counters and accumulate into 64-bit stats
 * @ds: DSA switch
 * @port: port index
 *
 * The firmware RMON counters are free-running 32-bit values (64-bit for
 * byte counters). This function reads the hardware via MDIO (may sleep),
 * computes deltas from the previous snapshot, and accumulates them into
 * 64-bit per-port stats under a spinlock.
 *
 * Called only from the stats polling workqueue -- serialized by the
 * single-threaded delayed_work, so no MDIO locking is needed here.
 */
static void mxl862xx_stats_poll(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port_stats *s = &priv->ports[port].stats;
	u32 rx_fcserr, rx_under, rx_over, rx_align, tx_drop;
	u32 rx_drop, rx_evlan, mtu_exc, tx_acm;
	struct mxl862xx_rmon_port_cnt cnt;
	u64 rx_bytes, tx_bytes;
	u32 rx_mcast, tx_coll;
	u32 rx_pkts, tx_pkts;

	/* MDIO read -- may sleep, done outside the spinlock. */
	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	rx_pkts   = le32_to_cpu(cnt.rx_good_pkts);
	tx_pkts   = le32_to_cpu(cnt.tx_good_pkts);
	rx_bytes  = le64_to_cpu(cnt.rx_good_bytes);
	tx_bytes  = le64_to_cpu(cnt.tx_good_bytes);
	rx_fcserr = le32_to_cpu(cnt.rx_fcserror_pkts);
	rx_under  = le32_to_cpu(cnt.rx_under_size_error_pkts);
	rx_over   = le32_to_cpu(cnt.rx_oversize_error_pkts);
	rx_align  = le32_to_cpu(cnt.rx_align_error_pkts);
	tx_drop   = le32_to_cpu(cnt.tx_dropped_pkts);
	rx_drop   = le32_to_cpu(cnt.rx_dropped_pkts);
	rx_evlan  = le32_to_cpu(cnt.rx_extended_vlan_discard_pkts);
	mtu_exc   = le32_to_cpu(cnt.mtu_exceed_discard_pkts);
	tx_acm    = le32_to_cpu(cnt.tx_acm_dropped_pkts);
	rx_mcast  = le32_to_cpu(cnt.rx_multicast_pkts);
	tx_coll   = le32_to_cpu(cnt.tx_coll_count);

	/* Accumulate deltas under spinlock -- .get_stats64 reads these. */
	spin_lock_bh(&priv->ports[port].stats_lock);

	s->rx_packets += mxl862xx_delta32(rx_pkts, s->prev_rx_good_pkts);
	s->tx_packets += mxl862xx_delta32(tx_pkts, s->prev_tx_good_pkts);
	s->rx_bytes   += rx_bytes - s->prev_rx_good_bytes;
	s->tx_bytes   += tx_bytes - s->prev_tx_good_bytes;

	s->rx_errors +=
		mxl862xx_delta32(rx_fcserr, s->prev_rx_fcserror_pkts) +
		mxl862xx_delta32(rx_under, s->prev_rx_under_size_error_pkts) +
		mxl862xx_delta32(rx_over, s->prev_rx_oversize_error_pkts) +
		mxl862xx_delta32(rx_align, s->prev_rx_align_error_pkts);
	s->tx_errors +=
		mxl862xx_delta32(tx_drop, s->prev_tx_dropped_pkts);

	s->rx_dropped +=
		mxl862xx_delta32(rx_drop, s->prev_rx_dropped_pkts) +
		mxl862xx_delta32(rx_evlan, s->prev_rx_evlan_discard_pkts) +
		mxl862xx_delta32(mtu_exc, s->prev_mtu_exceed_discard_pkts);
	s->tx_dropped +=
		mxl862xx_delta32(tx_drop, s->prev_tx_dropped_pkts) +
		mxl862xx_delta32(tx_acm, s->prev_tx_acm_dropped_pkts);

	s->multicast  += mxl862xx_delta32(rx_mcast, s->prev_rx_multicast_pkts);
	s->collisions += mxl862xx_delta32(tx_coll, s->prev_tx_coll_count);

	s->rx_length_errors +=
		mxl862xx_delta32(rx_under, s->prev_rx_under_size_error_pkts) +
		mxl862xx_delta32(rx_over, s->prev_rx_oversize_error_pkts);
	s->rx_crc_errors +=
		mxl862xx_delta32(rx_fcserr, s->prev_rx_fcserror_pkts);
	s->rx_frame_errors +=
		mxl862xx_delta32(rx_align, s->prev_rx_align_error_pkts);

	s->prev_rx_good_pkts             = rx_pkts;
	s->prev_tx_good_pkts             = tx_pkts;
	s->prev_rx_good_bytes            = rx_bytes;
	s->prev_tx_good_bytes            = tx_bytes;
	s->prev_rx_fcserror_pkts         = rx_fcserr;
	s->prev_rx_under_size_error_pkts = rx_under;
	s->prev_rx_oversize_error_pkts   = rx_over;
	s->prev_rx_align_error_pkts      = rx_align;
	s->prev_tx_dropped_pkts          = tx_drop;
	s->prev_rx_dropped_pkts          = rx_drop;
	s->prev_rx_evlan_discard_pkts    = rx_evlan;
	s->prev_mtu_exceed_discard_pkts  = mtu_exc;
	s->prev_tx_acm_dropped_pkts      = tx_acm;
	s->prev_rx_multicast_pkts        = rx_mcast;
	s->prev_tx_coll_count            = tx_coll;

	spin_unlock_bh(&priv->ports[port].stats_lock);
}

static void mxl862xx_stats_work_fn(struct work_struct *work)
{
	struct mxl862xx_priv *priv =
		container_of(work, struct mxl862xx_priv, stats_work.work);
	struct dsa_switch *ds = priv->ds;
	struct dsa_port *dp;

	dsa_switch_for_each_available_port(dp, ds)
		mxl862xx_stats_poll(ds, dp->index);

	if (!test_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags))
		schedule_delayed_work(&priv->stats_work,
				      MXL862XX_STATS_POLL_INTERVAL);
}

static void mxl862xx_get_stats64(struct dsa_switch *ds, int port,
				 struct rtnl_link_stats64 *s)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port_stats *ps = &priv->ports[port].stats;

	spin_lock_bh(&priv->ports[port].stats_lock);

	s->rx_packets = ps->rx_packets;
	s->tx_packets = ps->tx_packets;
	s->rx_bytes = ps->rx_bytes;
	s->tx_bytes = ps->tx_bytes;
	s->rx_errors = ps->rx_errors;
	s->tx_errors = ps->tx_errors;
	s->rx_dropped = ps->rx_dropped;
	s->tx_dropped = ps->tx_dropped;
	s->multicast = ps->multicast;
	s->collisions = ps->collisions;
	s->rx_length_errors = ps->rx_length_errors;
	s->rx_crc_errors = ps->rx_crc_errors;
	s->rx_frame_errors = ps->rx_frame_errors;

	spin_unlock_bh(&priv->ports[port].stats_lock);

	/* Trigger a fresh poll so the next read sees up-to-date counters.
	 * No-op if the work is already pending, running, or teardown started.
	 */
	if (!test_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags))
		schedule_delayed_work(&priv->stats_work, 0);
}

static const struct dsa_switch_ops mxl862xx_switch_ops = {
	.get_tag_protocol = mxl862xx_get_tag_protocol,
	.setup = mxl862xx_setup,
	.port_setup = mxl862xx_port_setup,
	.port_teardown = mxl862xx_port_teardown,
	.phylink_get_caps = mxl862xx_phylink_get_caps,
	.port_enable = mxl862xx_port_enable,
	.port_disable = mxl862xx_port_disable,
	.port_fast_age = mxl862xx_port_fast_age,
	.set_ageing_time = mxl862xx_set_ageing_time,
	.port_bridge_join = mxl862xx_port_bridge_join,
	.port_bridge_leave = mxl862xx_port_bridge_leave,
	.port_pre_bridge_flags = mxl862xx_port_pre_bridge_flags,
	.port_bridge_flags = mxl862xx_port_bridge_flags,
	.port_stp_state_set = mxl862xx_port_stp_state_set,
	.port_set_host_flood = mxl862xx_port_set_host_flood,
	.port_fdb_add = mxl862xx_port_fdb_add,
	.port_fdb_del = mxl862xx_port_fdb_del,
	.port_fdb_dump = mxl862xx_port_fdb_dump,
	.port_mdb_add = mxl862xx_port_mdb_add,
	.port_mdb_del = mxl862xx_port_mdb_del,
	.port_vlan_filtering = mxl862xx_port_vlan_filtering,
	.port_vlan_add = mxl862xx_port_vlan_add,
	.port_vlan_del = mxl862xx_port_vlan_del,
	.get_strings = mxl862xx_get_strings,
	.get_sset_count = mxl862xx_get_sset_count,
	.get_ethtool_stats = mxl862xx_get_ethtool_stats,
	.get_eth_mac_stats = mxl862xx_get_eth_mac_stats,
	.get_eth_ctrl_stats = mxl862xx_get_eth_ctrl_stats,
	.get_pause_stats = mxl862xx_get_pause_stats,
	.get_rmon_stats = mxl862xx_get_rmon_stats,
	.get_stats64 = mxl862xx_get_stats64,
};

static void mxl862xx_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
}

static void mxl862xx_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
}

static void mxl862xx_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
}

static const struct phylink_mac_ops mxl862xx_phylink_mac_ops = {
	.mac_config = mxl862xx_phylink_mac_config,
	.mac_link_down = mxl862xx_phylink_mac_link_down,
	.mac_link_up = mxl862xx_phylink_mac_link_up,
};

static int mxl862xx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct mxl862xx_priv *priv;
	struct dsa_switch *ds;
	int err, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mdiodev = mdiodev;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	priv->ds = ds;
	ds->dev = dev;
	ds->priv = priv;
	ds->ops = &mxl862xx_switch_ops;
	ds->phylink_mac_ops = &mxl862xx_phylink_mac_ops;
	ds->num_ports = MXL862XX_MAX_PORTS;
	ds->fdb_isolation = true;
	ds->max_num_bridges = MXL862XX_MAX_BRIDGES;

	mxl862xx_host_init(priv);

	for (i = 0; i < MXL862XX_MAX_PORTS; i++) {
		priv->ports[i].priv = priv;
		INIT_WORK(&priv->ports[i].host_flood_work,
			  mxl862xx_host_flood_work_fn);
		spin_lock_init(&priv->ports[i].stats_lock);
	}

	INIT_DELAYED_WORK(&priv->stats_work, mxl862xx_stats_work_fn);

	dev_set_drvdata(dev, ds);

	err = dsa_register_switch(ds);
	if (err) {
		set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
		cancel_delayed_work_sync(&priv->stats_work);
		mxl862xx_host_shutdown(priv);
		for (i = 0; i < MXL862XX_MAX_PORTS; i++)
			cancel_work_sync(&priv->ports[i].host_flood_work);
	}

	return err;
}

static void mxl862xx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mxl862xx_priv *priv;
	int i;

	if (!ds)
		return;

	priv = ds->priv;

	set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
	cancel_delayed_work_sync(&priv->stats_work);

	dsa_unregister_switch(ds);

	mxl862xx_host_shutdown(priv);

	/* Cancel any pending host flood work. dsa_unregister_switch()
	 * has already called port_teardown (which sets setup_done=false),
	 * but a worker could still be blocked on rtnl_lock(). Since we
	 * are now outside RTNL, cancel_work_sync() will not deadlock.
	 */
	for (i = 0; i < MXL862XX_MAX_PORTS; i++)
		cancel_work_sync(&priv->ports[i].host_flood_work);
}

static void mxl862xx_shutdown(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mxl862xx_priv *priv;
	int i;

	if (!ds)
		return;

	priv = ds->priv;

	dsa_switch_shutdown(ds);

	set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
	cancel_delayed_work_sync(&priv->stats_work);

	mxl862xx_host_shutdown(priv);

	for (i = 0; i < MXL862XX_MAX_PORTS; i++)
		cancel_work_sync(&priv->ports[i].host_flood_work);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id mxl862xx_of_match[] = {
	{ .compatible = "maxlinear,mxl86282" },
	{ .compatible = "maxlinear,mxl86252" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxl862xx_of_match);

static struct mdio_driver mxl862xx_driver = {
	.probe  = mxl862xx_probe,
	.remove = mxl862xx_remove,
	.shutdown = mxl862xx_shutdown,
	.mdiodrv.driver = {
		.name = "mxl862xx",
		.of_match_table = mxl862xx_of_match,
	},
};

mdio_module_driver(mxl862xx_driver);

MODULE_DESCRIPTION("Driver for MaxLinear MxL862xx switch family");
MODULE_LICENSE("GPL");
