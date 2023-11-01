// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019-2021 Linutronix GmbH
 * Author Kurt Kanzenbach <kurt@linutronix.de>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <net/dsa.h>

#include "hellcreek.h"
#include "hellcreek_ptp.h"
#include "hellcreek_hwtstamp.h"

static const struct hellcreek_counter hellcreek_counter[] = {
	{ 0x00, "RxFiltered", },
	{ 0x01, "RxOctets1k", },
	{ 0x02, "RxVTAG", },
	{ 0x03, "RxL2BAD", },
	{ 0x04, "RxOverloadDrop", },
	{ 0x05, "RxUC", },
	{ 0x06, "RxMC", },
	{ 0x07, "RxBC", },
	{ 0x08, "RxRS<64", },
	{ 0x09, "RxRS64", },
	{ 0x0a, "RxRS65_127", },
	{ 0x0b, "RxRS128_255", },
	{ 0x0c, "RxRS256_511", },
	{ 0x0d, "RxRS512_1023", },
	{ 0x0e, "RxRS1024_1518", },
	{ 0x0f, "RxRS>1518", },
	{ 0x10, "TxTailDropQueue0", },
	{ 0x11, "TxTailDropQueue1", },
	{ 0x12, "TxTailDropQueue2", },
	{ 0x13, "TxTailDropQueue3", },
	{ 0x14, "TxTailDropQueue4", },
	{ 0x15, "TxTailDropQueue5", },
	{ 0x16, "TxTailDropQueue6", },
	{ 0x17, "TxTailDropQueue7", },
	{ 0x18, "RxTrafficClass0", },
	{ 0x19, "RxTrafficClass1", },
	{ 0x1a, "RxTrafficClass2", },
	{ 0x1b, "RxTrafficClass3", },
	{ 0x1c, "RxTrafficClass4", },
	{ 0x1d, "RxTrafficClass5", },
	{ 0x1e, "RxTrafficClass6", },
	{ 0x1f, "RxTrafficClass7", },
	{ 0x21, "TxOctets1k", },
	{ 0x22, "TxVTAG", },
	{ 0x23, "TxL2BAD", },
	{ 0x25, "TxUC", },
	{ 0x26, "TxMC", },
	{ 0x27, "TxBC", },
	{ 0x28, "TxTS<64", },
	{ 0x29, "TxTS64", },
	{ 0x2a, "TxTS65_127", },
	{ 0x2b, "TxTS128_255", },
	{ 0x2c, "TxTS256_511", },
	{ 0x2d, "TxTS512_1023", },
	{ 0x2e, "TxTS1024_1518", },
	{ 0x2f, "TxTS>1518", },
	{ 0x30, "TxTrafficClassOverrun0", },
	{ 0x31, "TxTrafficClassOverrun1", },
	{ 0x32, "TxTrafficClassOverrun2", },
	{ 0x33, "TxTrafficClassOverrun3", },
	{ 0x34, "TxTrafficClassOverrun4", },
	{ 0x35, "TxTrafficClassOverrun5", },
	{ 0x36, "TxTrafficClassOverrun6", },
	{ 0x37, "TxTrafficClassOverrun7", },
	{ 0x38, "TxTrafficClass0", },
	{ 0x39, "TxTrafficClass1", },
	{ 0x3a, "TxTrafficClass2", },
	{ 0x3b, "TxTrafficClass3", },
	{ 0x3c, "TxTrafficClass4", },
	{ 0x3d, "TxTrafficClass5", },
	{ 0x3e, "TxTrafficClass6", },
	{ 0x3f, "TxTrafficClass7", },
};

static u16 hellcreek_read(struct hellcreek *hellcreek, unsigned int offset)
{
	return readw(hellcreek->base + offset);
}

static u16 hellcreek_read_ctrl(struct hellcreek *hellcreek)
{
	return readw(hellcreek->base + HR_CTRL_C);
}

static u16 hellcreek_read_stat(struct hellcreek *hellcreek)
{
	return readw(hellcreek->base + HR_SWSTAT);
}

static void hellcreek_write(struct hellcreek *hellcreek, u16 data,
			    unsigned int offset)
{
	writew(data, hellcreek->base + offset);
}

static void hellcreek_select_port(struct hellcreek *hellcreek, int port)
{
	u16 val = port << HR_PSEL_PTWSEL_SHIFT;

	hellcreek_write(hellcreek, val, HR_PSEL);
}

static void hellcreek_select_prio(struct hellcreek *hellcreek, int prio)
{
	u16 val = prio << HR_PSEL_PRTCWSEL_SHIFT;

	hellcreek_write(hellcreek, val, HR_PSEL);
}

static void hellcreek_select_port_prio(struct hellcreek *hellcreek, int port,
				       int prio)
{
	u16 val = port << HR_PSEL_PTWSEL_SHIFT;

	val |= prio << HR_PSEL_PRTCWSEL_SHIFT;

	hellcreek_write(hellcreek, val, HR_PSEL);
}

static void hellcreek_select_counter(struct hellcreek *hellcreek, int counter)
{
	u16 val = counter << HR_CSEL_SHIFT;

	hellcreek_write(hellcreek, val, HR_CSEL);

	/* Data sheet states to wait at least 20 internal clock cycles */
	ndelay(200);
}

static void hellcreek_select_vlan(struct hellcreek *hellcreek, int vid,
				  bool pvid)
{
	u16 val = 0;

	/* Set pvid bit first */
	if (pvid)
		val |= HR_VIDCFG_PVID;
	hellcreek_write(hellcreek, val, HR_VIDCFG);

	/* Set vlan */
	val |= vid << HR_VIDCFG_VID_SHIFT;
	hellcreek_write(hellcreek, val, HR_VIDCFG);
}

static void hellcreek_select_tgd(struct hellcreek *hellcreek, int port)
{
	u16 val = port << TR_TGDSEL_TDGSEL_SHIFT;

	hellcreek_write(hellcreek, val, TR_TGDSEL);
}

static int hellcreek_wait_until_ready(struct hellcreek *hellcreek)
{
	u16 val;

	/* Wait up to 1ms, although 3 us should be enough */
	return readx_poll_timeout(hellcreek_read_ctrl, hellcreek,
				  val, val & HR_CTRL_C_READY,
				  3, 1000);
}

static int hellcreek_wait_until_transitioned(struct hellcreek *hellcreek)
{
	u16 val;

	return readx_poll_timeout_atomic(hellcreek_read_ctrl, hellcreek,
					 val, !(val & HR_CTRL_C_TRANSITION),
					 1, 1000);
}

static int hellcreek_wait_fdb_ready(struct hellcreek *hellcreek)
{
	u16 val;

	return readx_poll_timeout_atomic(hellcreek_read_stat, hellcreek,
					 val, !(val & HR_SWSTAT_BUSY),
					 1, 1000);
}

static int hellcreek_detect(struct hellcreek *hellcreek)
{
	u16 id, rel_low, rel_high, date_low, date_high, tgd_ver;
	u8 tgd_maj, tgd_min;
	u32 rel, date;

	id	  = hellcreek_read(hellcreek, HR_MODID_C);
	rel_low	  = hellcreek_read(hellcreek, HR_REL_L_C);
	rel_high  = hellcreek_read(hellcreek, HR_REL_H_C);
	date_low  = hellcreek_read(hellcreek, HR_BLD_L_C);
	date_high = hellcreek_read(hellcreek, HR_BLD_H_C);
	tgd_ver   = hellcreek_read(hellcreek, TR_TGDVER);

	if (id != hellcreek->pdata->module_id)
		return -ENODEV;

	rel	= rel_low | (rel_high << 16);
	date	= date_low | (date_high << 16);
	tgd_maj = (tgd_ver & TR_TGDVER_REV_MAJ_MASK) >> TR_TGDVER_REV_MAJ_SHIFT;
	tgd_min = (tgd_ver & TR_TGDVER_REV_MIN_MASK) >> TR_TGDVER_REV_MIN_SHIFT;

	dev_info(hellcreek->dev, "Module ID=%02x Release=%04x Date=%04x TGD Version=%02x.%02x\n",
		 id, rel, date, tgd_maj, tgd_min);

	return 0;
}

static void hellcreek_feature_detect(struct hellcreek *hellcreek)
{
	u16 features;

	features = hellcreek_read(hellcreek, HR_FEABITS0);

	/* Only detect the size of the FDB table. The size and current
	 * utilization can be queried via devlink.
	 */
	hellcreek->fdb_entries = ((features & HR_FEABITS0_FDBBINS_MASK) >>
			       HR_FEABITS0_FDBBINS_SHIFT) * 32;
}

static enum dsa_tag_protocol hellcreek_get_tag_protocol(struct dsa_switch *ds,
							int port,
							enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_HELLCREEK;
}

static int hellcreek_port_enable(struct dsa_switch *ds, int port,
				 struct phy_device *phy)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port *hellcreek_port;
	u16 val;

	hellcreek_port = &hellcreek->ports[port];

	dev_dbg(hellcreek->dev, "Enable port %d\n", port);

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_port(hellcreek, port);
	val = hellcreek_port->ptcfg;
	val |= HR_PTCFG_ADMIN_EN;
	hellcreek_write(hellcreek, val, HR_PTCFG);
	hellcreek_port->ptcfg = val;

	mutex_unlock(&hellcreek->reg_lock);

	return 0;
}

static void hellcreek_port_disable(struct dsa_switch *ds, int port)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port *hellcreek_port;
	u16 val;

	hellcreek_port = &hellcreek->ports[port];

	dev_dbg(hellcreek->dev, "Disable port %d\n", port);

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_port(hellcreek, port);
	val = hellcreek_port->ptcfg;
	val &= ~HR_PTCFG_ADMIN_EN;
	hellcreek_write(hellcreek, val, HR_PTCFG);
	hellcreek_port->ptcfg = val;

	mutex_unlock(&hellcreek->reg_lock);
}

static void hellcreek_get_strings(struct dsa_switch *ds, int port,
				  u32 stringset, uint8_t *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hellcreek_counter); ++i) {
		const struct hellcreek_counter *counter = &hellcreek_counter[i];

		strscpy(data + i * ETH_GSTRING_LEN,
			counter->name, ETH_GSTRING_LEN);
	}
}

static int hellcreek_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(hellcreek_counter);
}

static void hellcreek_get_ethtool_stats(struct dsa_switch *ds, int port,
					uint64_t *data)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port *hellcreek_port;
	int i;

	hellcreek_port = &hellcreek->ports[port];

	for (i = 0; i < ARRAY_SIZE(hellcreek_counter); ++i) {
		const struct hellcreek_counter *counter = &hellcreek_counter[i];
		u8 offset = counter->offset + port * 64;
		u16 high, low;
		u64 value;

		mutex_lock(&hellcreek->reg_lock);

		hellcreek_select_counter(hellcreek, offset);

		/* The registers are locked internally by selecting the
		 * counter. So low and high can be read without reading high
		 * again.
		 */
		high  = hellcreek_read(hellcreek, HR_CRDH);
		low   = hellcreek_read(hellcreek, HR_CRDL);
		value = ((u64)high << 16) | low;

		hellcreek_port->counter_values[i] += value;
		data[i] = hellcreek_port->counter_values[i];

		mutex_unlock(&hellcreek->reg_lock);
	}
}

static u16 hellcreek_private_vid(int port)
{
	return VLAN_N_VID - port + 1;
}

static int hellcreek_vlan_prepare(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan,
				  struct netlink_ext_ack *extack)
{
	struct hellcreek *hellcreek = ds->priv;
	int i;

	dev_dbg(hellcreek->dev, "VLAN prepare for port %d\n", port);

	/* Restriction: Make sure that nobody uses the "private" VLANs. These
	 * VLANs are internally used by the driver to ensure port
	 * separation. Thus, they cannot be used by someone else.
	 */
	for (i = 0; i < hellcreek->pdata->num_ports; ++i) {
		const u16 restricted_vid = hellcreek_private_vid(i);

		if (!dsa_is_user_port(ds, i))
			continue;

		if (vlan->vid == restricted_vid) {
			NL_SET_ERR_MSG_MOD(extack, "VID restricted by driver");
			return -EBUSY;
		}
	}

	return 0;
}

static void hellcreek_select_vlan_params(struct hellcreek *hellcreek, int port,
					 int *shift, int *mask)
{
	switch (port) {
	case 0:
		*shift = HR_VIDMBRCFG_P0MBR_SHIFT;
		*mask  = HR_VIDMBRCFG_P0MBR_MASK;
		break;
	case 1:
		*shift = HR_VIDMBRCFG_P1MBR_SHIFT;
		*mask  = HR_VIDMBRCFG_P1MBR_MASK;
		break;
	case 2:
		*shift = HR_VIDMBRCFG_P2MBR_SHIFT;
		*mask  = HR_VIDMBRCFG_P2MBR_MASK;
		break;
	case 3:
		*shift = HR_VIDMBRCFG_P3MBR_SHIFT;
		*mask  = HR_VIDMBRCFG_P3MBR_MASK;
		break;
	default:
		*shift = *mask = 0;
		dev_err(hellcreek->dev, "Unknown port %d selected!\n", port);
	}
}

static void hellcreek_apply_vlan(struct hellcreek *hellcreek, int port, u16 vid,
				 bool pvid, bool untagged)
{
	int shift, mask;
	u16 val;

	dev_dbg(hellcreek->dev, "Apply VLAN: port=%d vid=%u pvid=%d untagged=%d",
		port, vid, pvid, untagged);

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_port(hellcreek, port);
	hellcreek_select_vlan(hellcreek, vid, pvid);

	/* Setup port vlan membership */
	hellcreek_select_vlan_params(hellcreek, port, &shift, &mask);
	val = hellcreek->vidmbrcfg[vid];
	val &= ~mask;
	if (untagged)
		val |= HELLCREEK_VLAN_UNTAGGED_MEMBER << shift;
	else
		val |= HELLCREEK_VLAN_TAGGED_MEMBER << shift;

	hellcreek_write(hellcreek, val, HR_VIDMBRCFG);
	hellcreek->vidmbrcfg[vid] = val;

	mutex_unlock(&hellcreek->reg_lock);
}

static void hellcreek_unapply_vlan(struct hellcreek *hellcreek, int port,
				   u16 vid)
{
	int shift, mask;
	u16 val;

	dev_dbg(hellcreek->dev, "Unapply VLAN: port=%d vid=%u\n", port, vid);

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_vlan(hellcreek, vid, false);

	/* Setup port vlan membership */
	hellcreek_select_vlan_params(hellcreek, port, &shift, &mask);
	val = hellcreek->vidmbrcfg[vid];
	val &= ~mask;
	val |= HELLCREEK_VLAN_NO_MEMBER << shift;

	hellcreek_write(hellcreek, val, HR_VIDMBRCFG);
	hellcreek->vidmbrcfg[vid] = val;

	mutex_unlock(&hellcreek->reg_lock);
}

static int hellcreek_vlan_add(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan,
			      struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct hellcreek *hellcreek = ds->priv;
	int err;

	err = hellcreek_vlan_prepare(ds, port, vlan, extack);
	if (err)
		return err;

	dev_dbg(hellcreek->dev, "Add VLAN %d on port %d, %s, %s\n",
		vlan->vid, port, untagged ? "untagged" : "tagged",
		pvid ? "PVID" : "no PVID");

	hellcreek_apply_vlan(hellcreek, port, vlan->vid, pvid, untagged);

	return 0;
}

static int hellcreek_vlan_del(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan)
{
	struct hellcreek *hellcreek = ds->priv;

	dev_dbg(hellcreek->dev, "Remove VLAN %d on port %d\n", vlan->vid, port);

	hellcreek_unapply_vlan(hellcreek, port, vlan->vid);

	return 0;
}

static void hellcreek_port_stp_state_set(struct dsa_switch *ds, int port,
					 u8 state)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port *hellcreek_port;
	const char *new_state;
	u16 val;

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_port = &hellcreek->ports[port];
	val = hellcreek_port->ptcfg;

	switch (state) {
	case BR_STATE_DISABLED:
		new_state = "DISABLED";
		val |= HR_PTCFG_BLOCKED;
		val &= ~HR_PTCFG_LEARNING_EN;
		break;
	case BR_STATE_BLOCKING:
		new_state = "BLOCKING";
		val |= HR_PTCFG_BLOCKED;
		val &= ~HR_PTCFG_LEARNING_EN;
		break;
	case BR_STATE_LISTENING:
		new_state = "LISTENING";
		val |= HR_PTCFG_BLOCKED;
		val &= ~HR_PTCFG_LEARNING_EN;
		break;
	case BR_STATE_LEARNING:
		new_state = "LEARNING";
		val |= HR_PTCFG_BLOCKED;
		val |= HR_PTCFG_LEARNING_EN;
		break;
	case BR_STATE_FORWARDING:
		new_state = "FORWARDING";
		val &= ~HR_PTCFG_BLOCKED;
		val |= HR_PTCFG_LEARNING_EN;
		break;
	default:
		new_state = "UNKNOWN";
	}

	hellcreek_select_port(hellcreek, port);
	hellcreek_write(hellcreek, val, HR_PTCFG);
	hellcreek_port->ptcfg = val;

	mutex_unlock(&hellcreek->reg_lock);

	dev_dbg(hellcreek->dev, "Configured STP state for port %d: %s\n",
		port, new_state);
}

static void hellcreek_setup_ingressflt(struct hellcreek *hellcreek, int port,
				       bool enable)
{
	struct hellcreek_port *hellcreek_port = &hellcreek->ports[port];
	u16 ptcfg;

	mutex_lock(&hellcreek->reg_lock);

	ptcfg = hellcreek_port->ptcfg;

	if (enable)
		ptcfg |= HR_PTCFG_INGRESSFLT;
	else
		ptcfg &= ~HR_PTCFG_INGRESSFLT;

	hellcreek_select_port(hellcreek, port);
	hellcreek_write(hellcreek, ptcfg, HR_PTCFG);
	hellcreek_port->ptcfg = ptcfg;

	mutex_unlock(&hellcreek->reg_lock);
}

static void hellcreek_setup_vlan_awareness(struct hellcreek *hellcreek,
					   bool enable)
{
	u16 swcfg;

	mutex_lock(&hellcreek->reg_lock);

	swcfg = hellcreek->swcfg;

	if (enable)
		swcfg |= HR_SWCFG_VLAN_UNAWARE;
	else
		swcfg &= ~HR_SWCFG_VLAN_UNAWARE;

	hellcreek_write(hellcreek, swcfg, HR_SWCFG);

	mutex_unlock(&hellcreek->reg_lock);
}

/* Default setup for DSA: VLAN <X>: CPU and Port <X> egress untagged. */
static void hellcreek_setup_vlan_membership(struct dsa_switch *ds, int port,
					    bool enabled)
{
	const u16 vid = hellcreek_private_vid(port);
	int upstream = dsa_upstream_port(ds, port);
	struct hellcreek *hellcreek = ds->priv;

	/* Apply vid to port as egress untagged and port vlan id */
	if (enabled)
		hellcreek_apply_vlan(hellcreek, port, vid, true, true);
	else
		hellcreek_unapply_vlan(hellcreek, port, vid);

	/* Apply vid to cpu port as well */
	if (enabled)
		hellcreek_apply_vlan(hellcreek, upstream, vid, false, true);
	else
		hellcreek_unapply_vlan(hellcreek, upstream, vid);
}

static void hellcreek_port_set_ucast_flood(struct hellcreek *hellcreek,
					   int port, bool enable)
{
	struct hellcreek_port *hellcreek_port;
	u16 val;

	hellcreek_port = &hellcreek->ports[port];

	dev_dbg(hellcreek->dev, "%s unicast flooding on port %d\n",
		enable ? "Enable" : "Disable", port);

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_port(hellcreek, port);
	val = hellcreek_port->ptcfg;
	if (enable)
		val &= ~HR_PTCFG_UUC_FLT;
	else
		val |= HR_PTCFG_UUC_FLT;
	hellcreek_write(hellcreek, val, HR_PTCFG);
	hellcreek_port->ptcfg = val;

	mutex_unlock(&hellcreek->reg_lock);
}

static void hellcreek_port_set_mcast_flood(struct hellcreek *hellcreek,
					   int port, bool enable)
{
	struct hellcreek_port *hellcreek_port;
	u16 val;

	hellcreek_port = &hellcreek->ports[port];

	dev_dbg(hellcreek->dev, "%s multicast flooding on port %d\n",
		enable ? "Enable" : "Disable", port);

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_port(hellcreek, port);
	val = hellcreek_port->ptcfg;
	if (enable)
		val &= ~HR_PTCFG_UMC_FLT;
	else
		val |= HR_PTCFG_UMC_FLT;
	hellcreek_write(hellcreek, val, HR_PTCFG);
	hellcreek_port->ptcfg = val;

	mutex_unlock(&hellcreek->reg_lock);
}

static int hellcreek_pre_bridge_flags(struct dsa_switch *ds, int port,
				      struct switchdev_brport_flags flags,
				      struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_FLOOD | BR_MCAST_FLOOD))
		return -EINVAL;

	return 0;
}

static int hellcreek_bridge_flags(struct dsa_switch *ds, int port,
				  struct switchdev_brport_flags flags,
				  struct netlink_ext_ack *extack)
{
	struct hellcreek *hellcreek = ds->priv;

	if (flags.mask & BR_FLOOD)
		hellcreek_port_set_ucast_flood(hellcreek, port,
					       !!(flags.val & BR_FLOOD));

	if (flags.mask & BR_MCAST_FLOOD)
		hellcreek_port_set_mcast_flood(hellcreek, port,
					       !!(flags.val & BR_MCAST_FLOOD));

	return 0;
}

static int hellcreek_port_bridge_join(struct dsa_switch *ds, int port,
				      struct dsa_bridge bridge,
				      bool *tx_fwd_offload,
				      struct netlink_ext_ack *extack)
{
	struct hellcreek *hellcreek = ds->priv;

	dev_dbg(hellcreek->dev, "Port %d joins a bridge\n", port);

	/* When joining a vlan_filtering bridge, keep the switch VLAN aware */
	if (!ds->vlan_filtering)
		hellcreek_setup_vlan_awareness(hellcreek, false);

	/* Drop private vlans */
	hellcreek_setup_vlan_membership(ds, port, false);

	return 0;
}

static void hellcreek_port_bridge_leave(struct dsa_switch *ds, int port,
					struct dsa_bridge bridge)
{
	struct hellcreek *hellcreek = ds->priv;

	dev_dbg(hellcreek->dev, "Port %d leaves a bridge\n", port);

	/* Enable VLAN awareness */
	hellcreek_setup_vlan_awareness(hellcreek, true);

	/* Enable private vlans */
	hellcreek_setup_vlan_membership(ds, port, true);
}

static int __hellcreek_fdb_add(struct hellcreek *hellcreek,
			       const struct hellcreek_fdb_entry *entry)
{
	u16 meta = 0;

	dev_dbg(hellcreek->dev, "Add static FDB entry: MAC=%pM, MASK=0x%02x, "
		"OBT=%d, PASS_BLOCKED=%d, REPRIO_EN=%d, PRIO=%d\n", entry->mac,
		entry->portmask, entry->is_obt, entry->pass_blocked,
		entry->reprio_en, entry->reprio_tc);

	/* Add mac address */
	hellcreek_write(hellcreek, entry->mac[1] | (entry->mac[0] << 8), HR_FDBWDH);
	hellcreek_write(hellcreek, entry->mac[3] | (entry->mac[2] << 8), HR_FDBWDM);
	hellcreek_write(hellcreek, entry->mac[5] | (entry->mac[4] << 8), HR_FDBWDL);

	/* Meta data */
	meta |= entry->portmask << HR_FDBWRM0_PORTMASK_SHIFT;
	if (entry->is_obt)
		meta |= HR_FDBWRM0_OBT;
	if (entry->pass_blocked)
		meta |= HR_FDBWRM0_PASS_BLOCKED;
	if (entry->reprio_en) {
		meta |= HR_FDBWRM0_REPRIO_EN;
		meta |= entry->reprio_tc << HR_FDBWRM0_REPRIO_TC_SHIFT;
	}
	hellcreek_write(hellcreek, meta, HR_FDBWRM0);

	/* Commit */
	hellcreek_write(hellcreek, 0x00, HR_FDBWRCMD);

	/* Wait until done */
	return hellcreek_wait_fdb_ready(hellcreek);
}

static int __hellcreek_fdb_del(struct hellcreek *hellcreek,
			       const struct hellcreek_fdb_entry *entry)
{
	dev_dbg(hellcreek->dev, "Delete FDB entry: MAC=%pM!\n", entry->mac);

	/* Delete by matching idx */
	hellcreek_write(hellcreek, entry->idx | HR_FDBWRCMD_FDBDEL, HR_FDBWRCMD);

	/* Wait until done */
	return hellcreek_wait_fdb_ready(hellcreek);
}

static void hellcreek_populate_fdb_entry(struct hellcreek *hellcreek,
					 struct hellcreek_fdb_entry *entry,
					 size_t idx)
{
	unsigned char addr[ETH_ALEN];
	u16 meta, mac;

	/* Read values */
	meta	= hellcreek_read(hellcreek, HR_FDBMDRD);
	mac	= hellcreek_read(hellcreek, HR_FDBRDL);
	addr[5] = mac & 0xff;
	addr[4] = (mac & 0xff00) >> 8;
	mac	= hellcreek_read(hellcreek, HR_FDBRDM);
	addr[3] = mac & 0xff;
	addr[2] = (mac & 0xff00) >> 8;
	mac	= hellcreek_read(hellcreek, HR_FDBRDH);
	addr[1] = mac & 0xff;
	addr[0] = (mac & 0xff00) >> 8;

	/* Populate @entry */
	memcpy(entry->mac, addr, sizeof(addr));
	entry->idx	    = idx;
	entry->portmask	    = (meta & HR_FDBMDRD_PORTMASK_MASK) >>
		HR_FDBMDRD_PORTMASK_SHIFT;
	entry->age	    = (meta & HR_FDBMDRD_AGE_MASK) >>
		HR_FDBMDRD_AGE_SHIFT;
	entry->is_obt	    = !!(meta & HR_FDBMDRD_OBT);
	entry->pass_blocked = !!(meta & HR_FDBMDRD_PASS_BLOCKED);
	entry->is_static    = !!(meta & HR_FDBMDRD_STATIC);
	entry->reprio_tc    = (meta & HR_FDBMDRD_REPRIO_TC_MASK) >>
		HR_FDBMDRD_REPRIO_TC_SHIFT;
	entry->reprio_en    = !!(meta & HR_FDBMDRD_REPRIO_EN);
}

/* Retrieve the index of a FDB entry by mac address. Currently we search through
 * the complete table in hardware. If that's too slow, we might have to cache
 * the complete FDB table in software.
 */
static int hellcreek_fdb_get(struct hellcreek *hellcreek,
			     const unsigned char *dest,
			     struct hellcreek_fdb_entry *entry)
{
	size_t i;

	/* Set read pointer to zero: The read of HR_FDBMAX (read-only register)
	 * should reset the internal pointer. But, that doesn't work. The vendor
	 * suggested a subsequent write as workaround. Same for HR_FDBRDH below.
	 */
	hellcreek_read(hellcreek, HR_FDBMAX);
	hellcreek_write(hellcreek, 0x00, HR_FDBMAX);

	/* We have to read the complete table, because the switch/driver might
	 * enter new entries anywhere.
	 */
	for (i = 0; i < hellcreek->fdb_entries; ++i) {
		struct hellcreek_fdb_entry tmp = { 0 };

		/* Read entry */
		hellcreek_populate_fdb_entry(hellcreek, &tmp, i);

		/* Force next entry */
		hellcreek_write(hellcreek, 0x00, HR_FDBRDH);

		if (memcmp(tmp.mac, dest, ETH_ALEN))
			continue;

		/* Match found */
		memcpy(entry, &tmp, sizeof(*entry));

		return 0;
	}

	return -ENOENT;
}

static int hellcreek_fdb_add(struct dsa_switch *ds, int port,
			     const unsigned char *addr, u16 vid,
			     struct dsa_db db)
{
	struct hellcreek_fdb_entry entry = { 0 };
	struct hellcreek *hellcreek = ds->priv;
	int ret;

	dev_dbg(hellcreek->dev, "Add FDB entry for MAC=%pM\n", addr);

	mutex_lock(&hellcreek->reg_lock);

	ret = hellcreek_fdb_get(hellcreek, addr, &entry);
	if (ret) {
		/* Not found */
		memcpy(entry.mac, addr, sizeof(entry.mac));
		entry.portmask = BIT(port);

		ret = __hellcreek_fdb_add(hellcreek, &entry);
		if (ret) {
			dev_err(hellcreek->dev, "Failed to add FDB entry!\n");
			goto out;
		}
	} else {
		/* Found */
		ret = __hellcreek_fdb_del(hellcreek, &entry);
		if (ret) {
			dev_err(hellcreek->dev, "Failed to delete FDB entry!\n");
			goto out;
		}

		entry.portmask |= BIT(port);

		ret = __hellcreek_fdb_add(hellcreek, &entry);
		if (ret) {
			dev_err(hellcreek->dev, "Failed to add FDB entry!\n");
			goto out;
		}
	}

out:
	mutex_unlock(&hellcreek->reg_lock);

	return ret;
}

static int hellcreek_fdb_del(struct dsa_switch *ds, int port,
			     const unsigned char *addr, u16 vid,
			     struct dsa_db db)
{
	struct hellcreek_fdb_entry entry = { 0 };
	struct hellcreek *hellcreek = ds->priv;
	int ret;

	dev_dbg(hellcreek->dev, "Delete FDB entry for MAC=%pM\n", addr);

	mutex_lock(&hellcreek->reg_lock);

	ret = hellcreek_fdb_get(hellcreek, addr, &entry);
	if (ret) {
		/* Not found */
		dev_err(hellcreek->dev, "FDB entry for deletion not found!\n");
	} else {
		/* Found */
		ret = __hellcreek_fdb_del(hellcreek, &entry);
		if (ret) {
			dev_err(hellcreek->dev, "Failed to delete FDB entry!\n");
			goto out;
		}

		entry.portmask &= ~BIT(port);

		if (entry.portmask != 0x00) {
			ret = __hellcreek_fdb_add(hellcreek, &entry);
			if (ret) {
				dev_err(hellcreek->dev, "Failed to add FDB entry!\n");
				goto out;
			}
		}
	}

out:
	mutex_unlock(&hellcreek->reg_lock);

	return ret;
}

static int hellcreek_fdb_dump(struct dsa_switch *ds, int port,
			      dsa_fdb_dump_cb_t *cb, void *data)
{
	struct hellcreek *hellcreek = ds->priv;
	u16 entries;
	int ret = 0;
	size_t i;

	mutex_lock(&hellcreek->reg_lock);

	/* Set read pointer to zero: The read of HR_FDBMAX (read-only register)
	 * should reset the internal pointer. But, that doesn't work. The vendor
	 * suggested a subsequent write as workaround. Same for HR_FDBRDH below.
	 */
	entries = hellcreek_read(hellcreek, HR_FDBMAX);
	hellcreek_write(hellcreek, 0x00, HR_FDBMAX);

	dev_dbg(hellcreek->dev, "FDB dump for port %d, entries=%d!\n", port, entries);

	/* Read table */
	for (i = 0; i < hellcreek->fdb_entries; ++i) {
		struct hellcreek_fdb_entry entry = { 0 };

		/* Read entry */
		hellcreek_populate_fdb_entry(hellcreek, &entry, i);

		/* Force next entry */
		hellcreek_write(hellcreek, 0x00, HR_FDBRDH);

		/* Check valid */
		if (is_zero_ether_addr(entry.mac))
			continue;

		/* Check port mask */
		if (!(entry.portmask & BIT(port)))
			continue;

		ret = cb(entry.mac, 0, entry.is_static, data);
		if (ret)
			break;
	}

	mutex_unlock(&hellcreek->reg_lock);

	return ret;
}

static int hellcreek_vlan_filtering(struct dsa_switch *ds, int port,
				    bool vlan_filtering,
				    struct netlink_ext_ack *extack)
{
	struct hellcreek *hellcreek = ds->priv;

	dev_dbg(hellcreek->dev, "%s VLAN filtering on port %d\n",
		vlan_filtering ? "Enable" : "Disable", port);

	/* Configure port to drop packages with not known vids */
	hellcreek_setup_ingressflt(hellcreek, port, vlan_filtering);

	/* Enable VLAN awareness on the switch. This save due to
	 * ds->vlan_filtering_is_global.
	 */
	hellcreek_setup_vlan_awareness(hellcreek, vlan_filtering);

	return 0;
}

static int hellcreek_enable_ip_core(struct hellcreek *hellcreek)
{
	int ret;
	u16 val;

	mutex_lock(&hellcreek->reg_lock);

	val = hellcreek_read(hellcreek, HR_CTRL_C);
	val |= HR_CTRL_C_ENABLE;
	hellcreek_write(hellcreek, val, HR_CTRL_C);
	ret = hellcreek_wait_until_transitioned(hellcreek);

	mutex_unlock(&hellcreek->reg_lock);

	return ret;
}

static void hellcreek_setup_cpu_and_tunnel_port(struct hellcreek *hellcreek)
{
	struct hellcreek_port *tunnel_port = &hellcreek->ports[TUNNEL_PORT];
	struct hellcreek_port *cpu_port = &hellcreek->ports[CPU_PORT];
	u16 ptcfg = 0;

	ptcfg |= HR_PTCFG_LEARNING_EN | HR_PTCFG_ADMIN_EN;

	mutex_lock(&hellcreek->reg_lock);

	hellcreek_select_port(hellcreek, CPU_PORT);
	hellcreek_write(hellcreek, ptcfg, HR_PTCFG);

	hellcreek_select_port(hellcreek, TUNNEL_PORT);
	hellcreek_write(hellcreek, ptcfg, HR_PTCFG);

	cpu_port->ptcfg	   = ptcfg;
	tunnel_port->ptcfg = ptcfg;

	mutex_unlock(&hellcreek->reg_lock);
}

static void hellcreek_setup_tc_identity_mapping(struct hellcreek *hellcreek)
{
	int i;

	/* The switch has multiple egress queues per port. The queue is selected
	 * via the PCP field in the VLAN header. The switch internally deals
	 * with traffic classes instead of PCP values and this mapping is
	 * configurable.
	 *
	 * The default mapping is (PCP - TC):
	 *  7 - 7
	 *  6 - 6
	 *  5 - 5
	 *  4 - 4
	 *  3 - 3
	 *  2 - 1
	 *  1 - 0
	 *  0 - 2
	 *
	 * The default should be an identity mapping.
	 */

	for (i = 0; i < 8; ++i) {
		mutex_lock(&hellcreek->reg_lock);

		hellcreek_select_prio(hellcreek, i);
		hellcreek_write(hellcreek,
				i << HR_PRTCCFG_PCP_TC_MAP_SHIFT,
				HR_PRTCCFG);

		mutex_unlock(&hellcreek->reg_lock);
	}
}

static int hellcreek_setup_fdb(struct hellcreek *hellcreek)
{
	static struct hellcreek_fdb_entry l2_ptp = {
		/* MAC: 01-1B-19-00-00-00 */
		.mac	      = { 0x01, 0x1b, 0x19, 0x00, 0x00, 0x00 },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 0,
		.is_static    = 1,
		.reprio_tc    = 6,	/* TC: 6 as per IEEE 802.1AS */
		.reprio_en    = 1,
	};
	static struct hellcreek_fdb_entry udp4_ptp = {
		/* MAC: 01-00-5E-00-01-81 */
		.mac	      = { 0x01, 0x00, 0x5e, 0x00, 0x01, 0x81 },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 0,
		.is_static    = 1,
		.reprio_tc    = 6,
		.reprio_en    = 1,
	};
	static struct hellcreek_fdb_entry udp6_ptp = {
		/* MAC: 33-33-00-00-01-81 */
		.mac	      = { 0x33, 0x33, 0x00, 0x00, 0x01, 0x81 },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 0,
		.is_static    = 1,
		.reprio_tc    = 6,
		.reprio_en    = 1,
	};
	static struct hellcreek_fdb_entry l2_p2p = {
		/* MAC: 01-80-C2-00-00-0E */
		.mac	      = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 1,
		.is_static    = 1,
		.reprio_tc    = 6,	/* TC: 6 as per IEEE 802.1AS */
		.reprio_en    = 1,
	};
	static struct hellcreek_fdb_entry udp4_p2p = {
		/* MAC: 01-00-5E-00-00-6B */
		.mac	      = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x6b },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 1,
		.is_static    = 1,
		.reprio_tc    = 6,
		.reprio_en    = 1,
	};
	static struct hellcreek_fdb_entry udp6_p2p = {
		/* MAC: 33-33-00-00-00-6B */
		.mac	      = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x6b },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 1,
		.is_static    = 1,
		.reprio_tc    = 6,
		.reprio_en    = 1,
	};
	static struct hellcreek_fdb_entry stp = {
		/* MAC: 01-80-C2-00-00-00 */
		.mac	      = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 1,
		.is_static    = 1,
		.reprio_tc    = 6,
		.reprio_en    = 1,
	};
	int ret;

	mutex_lock(&hellcreek->reg_lock);
	ret = __hellcreek_fdb_add(hellcreek, &l2_ptp);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &udp4_ptp);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &udp6_ptp);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &l2_p2p);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &udp4_p2p);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &udp6_p2p);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &stp);
out:
	mutex_unlock(&hellcreek->reg_lock);

	return ret;
}

static int hellcreek_devlink_info_get(struct dsa_switch *ds,
				      struct devlink_info_req *req,
				      struct netlink_ext_ack *extack)
{
	struct hellcreek *hellcreek = ds->priv;

	return devlink_info_version_fixed_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
					      hellcreek->pdata->name);
}

static u64 hellcreek_devlink_vlan_table_get(void *priv)
{
	struct hellcreek *hellcreek = priv;
	u64 count = 0;
	int i;

	mutex_lock(&hellcreek->reg_lock);
	for (i = 0; i < VLAN_N_VID; ++i)
		if (hellcreek->vidmbrcfg[i])
			count++;
	mutex_unlock(&hellcreek->reg_lock);

	return count;
}

static u64 hellcreek_devlink_fdb_table_get(void *priv)
{
	struct hellcreek *hellcreek = priv;
	u64 count = 0;

	/* Reading this register has side effects. Synchronize against the other
	 * FDB operations.
	 */
	mutex_lock(&hellcreek->reg_lock);
	count = hellcreek_read(hellcreek, HR_FDBMAX);
	mutex_unlock(&hellcreek->reg_lock);

	return count;
}

static int hellcreek_setup_devlink_resources(struct dsa_switch *ds)
{
	struct devlink_resource_size_params size_vlan_params;
	struct devlink_resource_size_params size_fdb_params;
	struct hellcreek *hellcreek = ds->priv;
	int err;

	devlink_resource_size_params_init(&size_vlan_params, VLAN_N_VID,
					  VLAN_N_VID,
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	devlink_resource_size_params_init(&size_fdb_params,
					  hellcreek->fdb_entries,
					  hellcreek->fdb_entries,
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	err = dsa_devlink_resource_register(ds, "VLAN", VLAN_N_VID,
					    HELLCREEK_DEVLINK_PARAM_ID_VLAN_TABLE,
					    DEVLINK_RESOURCE_ID_PARENT_TOP,
					    &size_vlan_params);
	if (err)
		goto out;

	err = dsa_devlink_resource_register(ds, "FDB", hellcreek->fdb_entries,
					    HELLCREEK_DEVLINK_PARAM_ID_FDB_TABLE,
					    DEVLINK_RESOURCE_ID_PARENT_TOP,
					    &size_fdb_params);
	if (err)
		goto out;

	dsa_devlink_resource_occ_get_register(ds,
					      HELLCREEK_DEVLINK_PARAM_ID_VLAN_TABLE,
					      hellcreek_devlink_vlan_table_get,
					      hellcreek);

	dsa_devlink_resource_occ_get_register(ds,
					      HELLCREEK_DEVLINK_PARAM_ID_FDB_TABLE,
					      hellcreek_devlink_fdb_table_get,
					      hellcreek);

	return 0;

out:
	dsa_devlink_resources_unregister(ds);

	return err;
}

static int hellcreek_devlink_region_vlan_snapshot(struct devlink *dl,
						  const struct devlink_region_ops *ops,
						  struct netlink_ext_ack *extack,
						  u8 **data)
{
	struct hellcreek_devlink_vlan_entry *table, *entry;
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);
	struct hellcreek *hellcreek = ds->priv;
	int i;

	table = kcalloc(VLAN_N_VID, sizeof(*entry), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	entry = table;

	mutex_lock(&hellcreek->reg_lock);
	for (i = 0; i < VLAN_N_VID; ++i, ++entry) {
		entry->member = hellcreek->vidmbrcfg[i];
		entry->vid    = i;
	}
	mutex_unlock(&hellcreek->reg_lock);

	*data = (u8 *)table;

	return 0;
}

static int hellcreek_devlink_region_fdb_snapshot(struct devlink *dl,
						 const struct devlink_region_ops *ops,
						 struct netlink_ext_ack *extack,
						 u8 **data)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);
	struct hellcreek_fdb_entry *table, *entry;
	struct hellcreek *hellcreek = ds->priv;
	size_t i;

	table = kcalloc(hellcreek->fdb_entries, sizeof(*entry), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	entry = table;

	mutex_lock(&hellcreek->reg_lock);

	/* Start table read */
	hellcreek_read(hellcreek, HR_FDBMAX);
	hellcreek_write(hellcreek, 0x00, HR_FDBMAX);

	for (i = 0; i < hellcreek->fdb_entries; ++i, ++entry) {
		/* Read current entry */
		hellcreek_populate_fdb_entry(hellcreek, entry, i);

		/* Advance read pointer */
		hellcreek_write(hellcreek, 0x00, HR_FDBRDH);
	}

	mutex_unlock(&hellcreek->reg_lock);

	*data = (u8 *)table;

	return 0;
}

static struct devlink_region_ops hellcreek_region_vlan_ops = {
	.name	    = "vlan",
	.snapshot   = hellcreek_devlink_region_vlan_snapshot,
	.destructor = kfree,
};

static struct devlink_region_ops hellcreek_region_fdb_ops = {
	.name	    = "fdb",
	.snapshot   = hellcreek_devlink_region_fdb_snapshot,
	.destructor = kfree,
};

static int hellcreek_setup_devlink_regions(struct dsa_switch *ds)
{
	struct hellcreek *hellcreek = ds->priv;
	struct devlink_region_ops *ops;
	struct devlink_region *region;
	u64 size;
	int ret;

	/* VLAN table */
	size = VLAN_N_VID * sizeof(struct hellcreek_devlink_vlan_entry);
	ops  = &hellcreek_region_vlan_ops;

	region = dsa_devlink_region_create(ds, ops, 1, size);
	if (IS_ERR(region))
		return PTR_ERR(region);

	hellcreek->vlan_region = region;

	/* FDB table */
	size = hellcreek->fdb_entries * sizeof(struct hellcreek_fdb_entry);
	ops  = &hellcreek_region_fdb_ops;

	region = dsa_devlink_region_create(ds, ops, 1, size);
	if (IS_ERR(region)) {
		ret = PTR_ERR(region);
		goto err_fdb;
	}

	hellcreek->fdb_region = region;

	return 0;

err_fdb:
	dsa_devlink_region_destroy(hellcreek->vlan_region);

	return ret;
}

static void hellcreek_teardown_devlink_regions(struct dsa_switch *ds)
{
	struct hellcreek *hellcreek = ds->priv;

	dsa_devlink_region_destroy(hellcreek->fdb_region);
	dsa_devlink_region_destroy(hellcreek->vlan_region);
}

static int hellcreek_setup(struct dsa_switch *ds)
{
	struct hellcreek *hellcreek = ds->priv;
	u16 swcfg = 0;
	int ret, i;

	dev_dbg(hellcreek->dev, "Set up the switch\n");

	/* Let's go */
	ret = hellcreek_enable_ip_core(hellcreek);
	if (ret) {
		dev_err(hellcreek->dev, "Failed to enable IP core!\n");
		return ret;
	}

	/* Enable CPU/Tunnel ports */
	hellcreek_setup_cpu_and_tunnel_port(hellcreek);

	/* Switch config: Keep defaults, enable FDB aging and learning and tag
	 * each frame from/to cpu port for DSA tagging.  Also enable the length
	 * aware shaping mode. This eliminates the need for Qbv guard bands.
	 */
	swcfg |= HR_SWCFG_FDBAGE_EN |
		HR_SWCFG_FDBLRN_EN  |
		HR_SWCFG_ALWAYS_OBT |
		(HR_SWCFG_LAS_ON << HR_SWCFG_LAS_MODE_SHIFT);
	hellcreek->swcfg = swcfg;
	hellcreek_write(hellcreek, swcfg, HR_SWCFG);

	/* Initial vlan membership to reflect port separation */
	for (i = 0; i < ds->num_ports; ++i) {
		if (!dsa_is_user_port(ds, i))
			continue;

		hellcreek_setup_vlan_membership(ds, i, true);
	}

	/* Configure PCP <-> TC mapping */
	hellcreek_setup_tc_identity_mapping(hellcreek);

	/* The VLAN awareness is a global switch setting. Therefore, mixed vlan
	 * filtering setups are not supported.
	 */
	ds->vlan_filtering_is_global = true;
	ds->needs_standalone_vlan_filtering = true;

	/* Intercept _all_ PTP multicast traffic */
	ret = hellcreek_setup_fdb(hellcreek);
	if (ret) {
		dev_err(hellcreek->dev,
			"Failed to insert static PTP FDB entries\n");
		return ret;
	}

	/* Register devlink resources with DSA */
	ret = hellcreek_setup_devlink_resources(ds);
	if (ret) {
		dev_err(hellcreek->dev,
			"Failed to setup devlink resources!\n");
		return ret;
	}

	ret = hellcreek_setup_devlink_regions(ds);
	if (ret) {
		dev_err(hellcreek->dev,
			"Failed to setup devlink regions!\n");
		goto err_regions;
	}

	return 0;

err_regions:
	dsa_devlink_resources_unregister(ds);

	return ret;
}

static void hellcreek_teardown(struct dsa_switch *ds)
{
	hellcreek_teardown_devlink_regions(ds);
	dsa_devlink_resources_unregister(ds);
}

static void hellcreek_phylink_get_caps(struct dsa_switch *ds, int port,
				       struct phylink_config *config)
{
	struct hellcreek *hellcreek = ds->priv;

	__set_bit(PHY_INTERFACE_MODE_MII, config->supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII, config->supported_interfaces);

	/* Include GMII - the hardware does not support this interface
	 * mode, but it's the default interface mode for phylib, so we
	 * need it for compatibility with existing DT.
	 */
	__set_bit(PHY_INTERFACE_MODE_GMII, config->supported_interfaces);

	/* The MAC settings are a hardware configuration option and cannot be
	 * changed at run time or by strapping. Therefore the attached PHYs
	 * should be programmed to only advertise settings which are supported
	 * by the hardware.
	 */
	if (hellcreek->pdata->is_100_mbits)
		config->mac_capabilities = MAC_100FD;
	else
		config->mac_capabilities = MAC_1000FD;
}

static int
hellcreek_port_prechangeupper(struct dsa_switch *ds, int port,
			      struct netdev_notifier_changeupper_info *info)
{
	struct hellcreek *hellcreek = ds->priv;
	bool used = true;
	int ret = -EBUSY;
	u16 vid;
	int i;

	dev_dbg(hellcreek->dev, "Pre change upper for port %d\n", port);

	/*
	 * Deny VLAN devices on top of lan ports with the same VLAN ids, because
	 * it breaks the port separation due to the private VLANs. Example:
	 *
	 * lan0.100 *and* lan1.100 cannot be used in parallel. However, lan0.99
	 * and lan1.100 works.
	 */

	if (!is_vlan_dev(info->upper_dev))
		return 0;

	vid = vlan_dev_vlan_id(info->upper_dev);

	/* For all ports, check bitmaps */
	mutex_lock(&hellcreek->vlan_lock);
	for (i = 0; i < hellcreek->pdata->num_ports; ++i) {
		if (!dsa_is_user_port(ds, i))
			continue;

		if (port == i)
			continue;

		used = used && test_bit(vid, hellcreek->ports[i].vlan_dev_bitmap);
	}

	if (used)
		goto out;

	/* Update bitmap */
	set_bit(vid, hellcreek->ports[port].vlan_dev_bitmap);

	ret = 0;

out:
	mutex_unlock(&hellcreek->vlan_lock);

	return ret;
}

static void hellcreek_setup_maxsdu(struct hellcreek *hellcreek, int port,
				   const struct tc_taprio_qopt_offload *schedule)
{
	int tc;

	for (tc = 0; tc < 8; ++tc) {
		u32 max_sdu = schedule->max_sdu[tc] + VLAN_ETH_HLEN - ETH_FCS_LEN;
		u16 val;

		if (!schedule->max_sdu[tc])
			continue;

		dev_dbg(hellcreek->dev, "Configure max-sdu %u for tc %d on port %d\n",
			max_sdu, tc, port);

		hellcreek_select_port_prio(hellcreek, port, tc);

		val = (max_sdu & HR_PTPRTCCFG_MAXSDU_MASK) << HR_PTPRTCCFG_MAXSDU_SHIFT;

		hellcreek_write(hellcreek, val, HR_PTPRTCCFG);
	}
}

static void hellcreek_reset_maxsdu(struct hellcreek *hellcreek, int port)
{
	int tc;

	for (tc = 0; tc < 8; ++tc) {
		u16 val;

		hellcreek_select_port_prio(hellcreek, port, tc);

		val = (HELLCREEK_DEFAULT_MAX_SDU & HR_PTPRTCCFG_MAXSDU_MASK)
			<< HR_PTPRTCCFG_MAXSDU_SHIFT;

		hellcreek_write(hellcreek, val, HR_PTPRTCCFG);
	}
}

static void hellcreek_setup_gcl(struct hellcreek *hellcreek, int port,
				const struct tc_taprio_qopt_offload *schedule)
{
	const struct tc_taprio_sched_entry *cur, *initial, *next;
	size_t i;

	cur = initial = &schedule->entries[0];
	next = cur + 1;

	for (i = 1; i <= schedule->num_entries; ++i) {
		u16 data;
		u8 gates;

		if (i == schedule->num_entries)
			gates = initial->gate_mask ^
				cur->gate_mask;
		else
			gates = next->gate_mask ^
				cur->gate_mask;

		data = gates;

		if (i == schedule->num_entries)
			data |= TR_GCLDAT_GCLWRLAST;

		/* Gates states */
		hellcreek_write(hellcreek, data, TR_GCLDAT);

		/* Time interval */
		hellcreek_write(hellcreek,
				cur->interval & 0x0000ffff,
				TR_GCLTIL);
		hellcreek_write(hellcreek,
				(cur->interval & 0xffff0000) >> 16,
				TR_GCLTIH);

		/* Commit entry */
		data = ((i - 1) << TR_GCLCMD_GCLWRADR_SHIFT) |
			(initial->gate_mask <<
			 TR_GCLCMD_INIT_GATE_STATES_SHIFT);
		hellcreek_write(hellcreek, data, TR_GCLCMD);

		cur++;
		next++;
	}
}

static void hellcreek_set_cycle_time(struct hellcreek *hellcreek,
				     const struct tc_taprio_qopt_offload *schedule)
{
	u32 cycle_time = schedule->cycle_time;

	hellcreek_write(hellcreek, cycle_time & 0x0000ffff, TR_CTWRL);
	hellcreek_write(hellcreek, (cycle_time & 0xffff0000) >> 16, TR_CTWRH);
}

static void hellcreek_switch_schedule(struct hellcreek *hellcreek,
				      ktime_t start_time)
{
	struct timespec64 ts = ktime_to_timespec64(start_time);

	/* Start schedule at this point of time */
	hellcreek_write(hellcreek, ts.tv_nsec & 0x0000ffff, TR_ESTWRL);
	hellcreek_write(hellcreek, (ts.tv_nsec & 0xffff0000) >> 16, TR_ESTWRH);

	/* Arm timer, set seconds and switch schedule */
	hellcreek_write(hellcreek, TR_ESTCMD_ESTARM | TR_ESTCMD_ESTSWCFG |
			((ts.tv_sec & TR_ESTCMD_ESTSEC_MASK) <<
			 TR_ESTCMD_ESTSEC_SHIFT), TR_ESTCMD);
}

static bool hellcreek_schedule_startable(struct hellcreek *hellcreek, int port)
{
	struct hellcreek_port *hellcreek_port = &hellcreek->ports[port];
	s64 base_time_ns, current_ns;

	/* The switch allows a schedule to be started only eight seconds within
	 * the future. Therefore, check the current PTP time if the schedule is
	 * startable or not.
	 */

	/* Use the "cached" time. That should be alright, as it's updated quite
	 * frequently in the PTP code.
	 */
	mutex_lock(&hellcreek->ptp_lock);
	current_ns = hellcreek->seconds * NSEC_PER_SEC + hellcreek->last_ts;
	mutex_unlock(&hellcreek->ptp_lock);

	/* Calculate difference to admin base time */
	base_time_ns = ktime_to_ns(hellcreek_port->current_schedule->base_time);

	return base_time_ns - current_ns < (s64)4 * NSEC_PER_SEC;
}

static void hellcreek_start_schedule(struct hellcreek *hellcreek, int port)
{
	struct hellcreek_port *hellcreek_port = &hellcreek->ports[port];
	ktime_t base_time, current_time;
	s64 current_ns;
	u32 cycle_time;

	/* First select port */
	hellcreek_select_tgd(hellcreek, port);

	/* Forward base time into the future if needed */
	mutex_lock(&hellcreek->ptp_lock);
	current_ns = hellcreek->seconds * NSEC_PER_SEC + hellcreek->last_ts;
	mutex_unlock(&hellcreek->ptp_lock);

	current_time = ns_to_ktime(current_ns);
	base_time    = hellcreek_port->current_schedule->base_time;
	cycle_time   = hellcreek_port->current_schedule->cycle_time;

	if (ktime_compare(current_time, base_time) > 0) {
		s64 n;

		n = div64_s64(ktime_sub_ns(current_time, base_time),
			      cycle_time);
		base_time = ktime_add_ns(base_time, (n + 1) * cycle_time);
	}

	/* Set admin base time and switch schedule */
	hellcreek_switch_schedule(hellcreek, base_time);

	taprio_offload_free(hellcreek_port->current_schedule);
	hellcreek_port->current_schedule = NULL;

	dev_dbg(hellcreek->dev, "Armed EST timer for port %d\n",
		hellcreek_port->port);
}

static void hellcreek_check_schedule(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct hellcreek_port *hellcreek_port;
	struct hellcreek *hellcreek;
	bool startable;

	hellcreek_port = dw_to_hellcreek_port(dw);
	hellcreek = hellcreek_port->hellcreek;

	mutex_lock(&hellcreek->reg_lock);

	/* Check starting time */
	startable = hellcreek_schedule_startable(hellcreek,
						 hellcreek_port->port);
	if (startable) {
		hellcreek_start_schedule(hellcreek, hellcreek_port->port);
		mutex_unlock(&hellcreek->reg_lock);
		return;
	}

	mutex_unlock(&hellcreek->reg_lock);

	/* Reschedule */
	schedule_delayed_work(&hellcreek_port->schedule_work,
			      HELLCREEK_SCHEDULE_PERIOD);
}

static int hellcreek_port_set_schedule(struct dsa_switch *ds, int port,
				       struct tc_taprio_qopt_offload *taprio)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port *hellcreek_port;
	bool startable;
	u16 ctrl;

	hellcreek_port = &hellcreek->ports[port];

	dev_dbg(hellcreek->dev, "Configure traffic schedule on port %d\n",
		port);

	/* First cancel delayed work */
	cancel_delayed_work_sync(&hellcreek_port->schedule_work);

	mutex_lock(&hellcreek->reg_lock);

	if (hellcreek_port->current_schedule) {
		taprio_offload_free(hellcreek_port->current_schedule);
		hellcreek_port->current_schedule = NULL;
	}
	hellcreek_port->current_schedule = taprio_offload_get(taprio);

	/* Configure max sdu */
	hellcreek_setup_maxsdu(hellcreek, port, hellcreek_port->current_schedule);

	/* Select tdg */
	hellcreek_select_tgd(hellcreek, port);

	/* Enable gating and keep defaults */
	ctrl = (0xff << TR_TGDCTRL_ADMINGATESTATES_SHIFT) | TR_TGDCTRL_GATE_EN;
	hellcreek_write(hellcreek, ctrl, TR_TGDCTRL);

	/* Cancel pending schedule */
	hellcreek_write(hellcreek, 0x00, TR_ESTCMD);

	/* Setup a new schedule */
	hellcreek_setup_gcl(hellcreek, port, hellcreek_port->current_schedule);

	/* Configure cycle time */
	hellcreek_set_cycle_time(hellcreek, hellcreek_port->current_schedule);

	/* Check starting time */
	startable = hellcreek_schedule_startable(hellcreek, port);
	if (startable) {
		hellcreek_start_schedule(hellcreek, port);
		mutex_unlock(&hellcreek->reg_lock);
		return 0;
	}

	mutex_unlock(&hellcreek->reg_lock);

	/* Schedule periodic schedule check */
	schedule_delayed_work(&hellcreek_port->schedule_work,
			      HELLCREEK_SCHEDULE_PERIOD);

	return 0;
}

static int hellcreek_port_del_schedule(struct dsa_switch *ds, int port)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port *hellcreek_port;

	hellcreek_port = &hellcreek->ports[port];

	dev_dbg(hellcreek->dev, "Remove traffic schedule on port %d\n", port);

	/* First cancel delayed work */
	cancel_delayed_work_sync(&hellcreek_port->schedule_work);

	mutex_lock(&hellcreek->reg_lock);

	if (hellcreek_port->current_schedule) {
		taprio_offload_free(hellcreek_port->current_schedule);
		hellcreek_port->current_schedule = NULL;
	}

	/* Reset max sdu */
	hellcreek_reset_maxsdu(hellcreek, port);

	/* Select tgd */
	hellcreek_select_tgd(hellcreek, port);

	/* Disable gating and return to regular switching flow */
	hellcreek_write(hellcreek, 0xff << TR_TGDCTRL_ADMINGATESTATES_SHIFT,
			TR_TGDCTRL);

	mutex_unlock(&hellcreek->reg_lock);

	return 0;
}

static bool hellcreek_validate_schedule(struct hellcreek *hellcreek,
					struct tc_taprio_qopt_offload *schedule)
{
	size_t i;

	/* Does this hellcreek version support Qbv in hardware? */
	if (!hellcreek->pdata->qbv_support)
		return false;

	/* cycle time can only be 32bit */
	if (schedule->cycle_time > (u32)-1)
		return false;

	/* cycle time extension is not supported */
	if (schedule->cycle_time_extension)
		return false;

	/* Only set command is supported */
	for (i = 0; i < schedule->num_entries; ++i)
		if (schedule->entries[i].command != TC_TAPRIO_CMD_SET_GATES)
			return false;

	return true;
}

static int hellcreek_tc_query_caps(struct tc_query_caps_base *base)
{
	switch (base->type) {
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_caps *caps = base->caps;

		caps->supports_queue_max_sdu = true;

		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static int hellcreek_port_setup_tc(struct dsa_switch *ds, int port,
				   enum tc_setup_type type, void *type_data)
{
	struct hellcreek *hellcreek = ds->priv;

	switch (type) {
	case TC_QUERY_CAPS:
		return hellcreek_tc_query_caps(type_data);
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_qopt_offload *taprio = type_data;

		switch (taprio->cmd) {
		case TAPRIO_CMD_REPLACE:
			if (!hellcreek_validate_schedule(hellcreek, taprio))
				return -EOPNOTSUPP;

			return hellcreek_port_set_schedule(ds, port, taprio);
		case TAPRIO_CMD_DESTROY:
			return hellcreek_port_del_schedule(ds, port);
		default:
			return -EOPNOTSUPP;
		}
	}
	default:
		return -EOPNOTSUPP;
	}
}

static const struct dsa_switch_ops hellcreek_ds_ops = {
	.devlink_info_get      = hellcreek_devlink_info_get,
	.get_ethtool_stats     = hellcreek_get_ethtool_stats,
	.get_sset_count	       = hellcreek_get_sset_count,
	.get_strings	       = hellcreek_get_strings,
	.get_tag_protocol      = hellcreek_get_tag_protocol,
	.get_ts_info	       = hellcreek_get_ts_info,
	.phylink_get_caps      = hellcreek_phylink_get_caps,
	.port_bridge_flags     = hellcreek_bridge_flags,
	.port_bridge_join      = hellcreek_port_bridge_join,
	.port_bridge_leave     = hellcreek_port_bridge_leave,
	.port_disable	       = hellcreek_port_disable,
	.port_enable	       = hellcreek_port_enable,
	.port_fdb_add	       = hellcreek_fdb_add,
	.port_fdb_del	       = hellcreek_fdb_del,
	.port_fdb_dump	       = hellcreek_fdb_dump,
	.port_hwtstamp_set     = hellcreek_port_hwtstamp_set,
	.port_hwtstamp_get     = hellcreek_port_hwtstamp_get,
	.port_pre_bridge_flags = hellcreek_pre_bridge_flags,
	.port_prechangeupper   = hellcreek_port_prechangeupper,
	.port_rxtstamp	       = hellcreek_port_rxtstamp,
	.port_setup_tc	       = hellcreek_port_setup_tc,
	.port_stp_state_set    = hellcreek_port_stp_state_set,
	.port_txtstamp	       = hellcreek_port_txtstamp,
	.port_vlan_add	       = hellcreek_vlan_add,
	.port_vlan_del	       = hellcreek_vlan_del,
	.port_vlan_filtering   = hellcreek_vlan_filtering,
	.setup		       = hellcreek_setup,
	.teardown	       = hellcreek_teardown,
};

static int hellcreek_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hellcreek *hellcreek;
	struct resource *res;
	int ret, i;

	hellcreek = devm_kzalloc(dev, sizeof(*hellcreek), GFP_KERNEL);
	if (!hellcreek)
		return -ENOMEM;

	hellcreek->vidmbrcfg = devm_kcalloc(dev, VLAN_N_VID,
					    sizeof(*hellcreek->vidmbrcfg),
					    GFP_KERNEL);
	if (!hellcreek->vidmbrcfg)
		return -ENOMEM;

	hellcreek->pdata = of_device_get_match_data(dev);

	hellcreek->ports = devm_kcalloc(dev, hellcreek->pdata->num_ports,
					sizeof(*hellcreek->ports),
					GFP_KERNEL);
	if (!hellcreek->ports)
		return -ENOMEM;

	for (i = 0; i < hellcreek->pdata->num_ports; ++i) {
		struct hellcreek_port *port = &hellcreek->ports[i];

		port->counter_values =
			devm_kcalloc(dev,
				     ARRAY_SIZE(hellcreek_counter),
				     sizeof(*port->counter_values),
				     GFP_KERNEL);
		if (!port->counter_values)
			return -ENOMEM;

		port->vlan_dev_bitmap = devm_bitmap_zalloc(dev, VLAN_N_VID,
							   GFP_KERNEL);
		if (!port->vlan_dev_bitmap)
			return -ENOMEM;

		port->hellcreek	= hellcreek;
		port->port	= i;

		INIT_DELAYED_WORK(&port->schedule_work,
				  hellcreek_check_schedule);
	}

	mutex_init(&hellcreek->reg_lock);
	mutex_init(&hellcreek->vlan_lock);
	mutex_init(&hellcreek->ptp_lock);

	hellcreek->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tsn");
	if (!res) {
		dev_err(dev, "No memory region provided!\n");
		return -ENODEV;
	}

	hellcreek->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hellcreek->base))
		return PTR_ERR(hellcreek->base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ptp");
	if (!res) {
		dev_err(dev, "No PTP memory region provided!\n");
		return -ENODEV;
	}

	hellcreek->ptp_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hellcreek->ptp_base))
		return PTR_ERR(hellcreek->ptp_base);

	ret = hellcreek_detect(hellcreek);
	if (ret) {
		dev_err(dev, "No (known) chip found!\n");
		return ret;
	}

	ret = hellcreek_wait_until_ready(hellcreek);
	if (ret) {
		dev_err(dev, "Switch didn't become ready!\n");
		return ret;
	}

	hellcreek_feature_detect(hellcreek);

	hellcreek->ds = devm_kzalloc(dev, sizeof(*hellcreek->ds), GFP_KERNEL);
	if (!hellcreek->ds)
		return -ENOMEM;

	hellcreek->ds->dev	     = dev;
	hellcreek->ds->priv	     = hellcreek;
	hellcreek->ds->ops	     = &hellcreek_ds_ops;
	hellcreek->ds->num_ports     = hellcreek->pdata->num_ports;
	hellcreek->ds->num_tx_queues = HELLCREEK_NUM_EGRESS_QUEUES;

	ret = dsa_register_switch(hellcreek->ds);
	if (ret) {
		dev_err_probe(dev, ret, "Unable to register switch\n");
		return ret;
	}

	ret = hellcreek_ptp_setup(hellcreek);
	if (ret) {
		dev_err(dev, "Failed to setup PTP!\n");
		goto err_ptp_setup;
	}

	ret = hellcreek_hwtstamp_setup(hellcreek);
	if (ret) {
		dev_err(dev, "Failed to setup hardware timestamping!\n");
		goto err_tstamp_setup;
	}

	platform_set_drvdata(pdev, hellcreek);

	return 0;

err_tstamp_setup:
	hellcreek_ptp_free(hellcreek);
err_ptp_setup:
	dsa_unregister_switch(hellcreek->ds);

	return ret;
}

static void hellcreek_remove(struct platform_device *pdev)
{
	struct hellcreek *hellcreek = platform_get_drvdata(pdev);

	if (!hellcreek)
		return;

	hellcreek_hwtstamp_free(hellcreek);
	hellcreek_ptp_free(hellcreek);
	dsa_unregister_switch(hellcreek->ds);
}

static void hellcreek_shutdown(struct platform_device *pdev)
{
	struct hellcreek *hellcreek = platform_get_drvdata(pdev);

	if (!hellcreek)
		return;

	dsa_switch_shutdown(hellcreek->ds);

	platform_set_drvdata(pdev, NULL);
}

static const struct hellcreek_platform_data de1soc_r1_pdata = {
	.name		 = "r4c30",
	.num_ports	 = 4,
	.is_100_mbits	 = 1,
	.qbv_support	 = 1,
	.qbv_on_cpu_port = 1,
	.qbu_support	 = 0,
	.module_id	 = 0x4c30,
};

static const struct of_device_id hellcreek_of_match[] = {
	{
		.compatible = "hirschmann,hellcreek-de1soc-r1",
		.data	    = &de1soc_r1_pdata,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, hellcreek_of_match);

static struct platform_driver hellcreek_driver = {
	.probe	= hellcreek_probe,
	.remove_new = hellcreek_remove,
	.shutdown = hellcreek_shutdown,
	.driver = {
		.name = "hellcreek",
		.of_match_table = hellcreek_of_match,
	},
};
module_platform_driver(hellcreek_driver);

MODULE_AUTHOR("Kurt Kanzenbach <kurt@linutronix.de>");
MODULE_DESCRIPTION("Hirschmann Hellcreek driver");
MODULE_LICENSE("Dual MIT/GPL");
