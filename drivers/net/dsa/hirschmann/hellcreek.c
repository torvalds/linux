// SPDX-License-Identifier: (GPL-2.0 or MIT)
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019,2020 Linutronix GmbH
 * Author Kurt Kanzenbach <kurt@linutronix.de>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
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

	/* Currently we only detect the size of the FDB table */
	hellcreek->fdb_entries = ((features & HR_FEABITS0_FDBBINS_MASK) >>
			       HR_FEABITS0_FDBBINS_SHIFT) * 32;

	dev_info(hellcreek->dev, "Feature detect: FDB entries=%zu\n",
		 hellcreek->fdb_entries);
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

		strlcpy(data + i * ETH_GSTRING_LEN,
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
				  const struct switchdev_obj_port_vlan *vlan)
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

		if (vlan->vid == restricted_vid)
			return -EBUSY;
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

	hellcreek_select_vlan(hellcreek, vid, 0);

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
			      const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct hellcreek *hellcreek = ds->priv;
	int err;

	err = hellcreek_vlan_prepare(ds, port, vlan);
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

static int hellcreek_port_bridge_join(struct dsa_switch *ds, int port,
				      struct net_device *br)
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
					struct net_device *br)
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
		"OBT=%d, REPRIO_EN=%d, PRIO=%d\n", entry->mac, entry->portmask,
		entry->is_obt, entry->reprio_en, entry->reprio_tc);

	/* Add mac address */
	hellcreek_write(hellcreek, entry->mac[1] | (entry->mac[0] << 8), HR_FDBWDH);
	hellcreek_write(hellcreek, entry->mac[3] | (entry->mac[2] << 8), HR_FDBWDM);
	hellcreek_write(hellcreek, entry->mac[5] | (entry->mac[4] << 8), HR_FDBWDL);

	/* Meta data */
	meta |= entry->portmask << HR_FDBWRM0_PORTMASK_SHIFT;
	if (entry->is_obt)
		meta |= HR_FDBWRM0_OBT;
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
		unsigned char addr[ETH_ALEN];
		u16 meta, mac;

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

		/* Force next entry */
		hellcreek_write(hellcreek, 0x00, HR_FDBRDH);

		if (memcmp(addr, dest, ETH_ALEN))
			continue;

		/* Match found */
		entry->idx	    = i;
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
		memcpy(entry->mac, addr, sizeof(addr));

		return 0;
	}

	return -ENOENT;
}

static int hellcreek_fdb_add(struct dsa_switch *ds, int port,
			     const unsigned char *addr, u16 vid)
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
			     const unsigned char *addr, u16 vid)
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
		unsigned char null_addr[ETH_ALEN] = { 0 };
		struct hellcreek_fdb_entry entry = { 0 };
		u16 meta, mac;

		meta	= hellcreek_read(hellcreek, HR_FDBMDRD);
		mac	= hellcreek_read(hellcreek, HR_FDBRDL);
		entry.mac[5] = mac & 0xff;
		entry.mac[4] = (mac & 0xff00) >> 8;
		mac	= hellcreek_read(hellcreek, HR_FDBRDM);
		entry.mac[3] = mac & 0xff;
		entry.mac[2] = (mac & 0xff00) >> 8;
		mac	= hellcreek_read(hellcreek, HR_FDBRDH);
		entry.mac[1] = mac & 0xff;
		entry.mac[0] = (mac & 0xff00) >> 8;

		/* Force next entry */
		hellcreek_write(hellcreek, 0x00, HR_FDBRDH);

		/* Check valid */
		if (!memcmp(entry.mac, null_addr, ETH_ALEN))
			continue;

		entry.portmask	= (meta & HR_FDBMDRD_PORTMASK_MASK) >>
			HR_FDBMDRD_PORTMASK_SHIFT;
		entry.is_static	= !!(meta & HR_FDBMDRD_STATIC);

		/* Check port mask */
		if (!(entry.portmask & BIT(port)))
			continue;

		cb(entry.mac, 0, entry.is_static, data);
	}

	mutex_unlock(&hellcreek->reg_lock);

	return 0;
}

static int hellcreek_vlan_filtering(struct dsa_switch *ds, int port,
				    bool vlan_filtering)
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
	static struct hellcreek_fdb_entry ptp = {
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
	static struct hellcreek_fdb_entry p2p = {
		/* MAC: 01-80-C2-00-00-0E */
		.mac	      = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e },
		.portmask     = 0x03,	/* Management ports */
		.age	      = 0,
		.is_obt	      = 0,
		.pass_blocked = 0,
		.is_static    = 1,
		.reprio_tc    = 6,	/* TC: 6 as per IEEE 802.1AS */
		.reprio_en    = 1,
	};
	int ret;

	mutex_lock(&hellcreek->reg_lock);
	ret = __hellcreek_fdb_add(hellcreek, &ptp);
	if (ret)
		goto out;
	ret = __hellcreek_fdb_add(hellcreek, &p2p);
out:
	mutex_unlock(&hellcreek->reg_lock);

	return ret;
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

	/* Allow VLAN configurations while not filtering which is the default
	 * for new DSA drivers.
	 */
	ds->configure_vlan_while_not_filtering = true;

	/* The VLAN awareness is a global switch setting. Therefore, mixed vlan
	 * filtering setups are not supported.
	 */
	ds->vlan_filtering_is_global = true;

	/* Intercept _all_ PTP multicast traffic */
	ret = hellcreek_setup_fdb(hellcreek);
	if (ret) {
		dev_err(hellcreek->dev,
			"Failed to insert static PTP FDB entries\n");
		return ret;
	}

	return 0;
}

static void hellcreek_phylink_validate(struct dsa_switch *ds, int port,
				       unsigned long *supported,
				       struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	struct hellcreek *hellcreek = ds->priv;

	dev_dbg(hellcreek->dev, "Phylink validate for port %d\n", port);

	/* The MAC settings are a hardware configuration option and cannot be
	 * changed at run time or by strapping. Therefore the attached PHYs
	 * should be programmed to only advertise settings which are supported
	 * by the hardware.
	 */
	if (hellcreek->pdata->is_100_mbits)
		phylink_set(mask, 100baseT_Full);
	else
		phylink_set(mask, 1000baseT_Full);

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
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

static const struct dsa_switch_ops hellcreek_ds_ops = {
	.get_ethtool_stats   = hellcreek_get_ethtool_stats,
	.get_sset_count	     = hellcreek_get_sset_count,
	.get_strings	     = hellcreek_get_strings,
	.get_tag_protocol    = hellcreek_get_tag_protocol,
	.get_ts_info	     = hellcreek_get_ts_info,
	.phylink_validate    = hellcreek_phylink_validate,
	.port_bridge_join    = hellcreek_port_bridge_join,
	.port_bridge_leave   = hellcreek_port_bridge_leave,
	.port_disable	     = hellcreek_port_disable,
	.port_enable	     = hellcreek_port_enable,
	.port_fdb_add	     = hellcreek_fdb_add,
	.port_fdb_del	     = hellcreek_fdb_del,
	.port_fdb_dump	     = hellcreek_fdb_dump,
	.port_hwtstamp_set   = hellcreek_port_hwtstamp_set,
	.port_hwtstamp_get   = hellcreek_port_hwtstamp_get,
	.port_prechangeupper = hellcreek_port_prechangeupper,
	.port_rxtstamp	     = hellcreek_port_rxtstamp,
	.port_stp_state_set  = hellcreek_port_stp_state_set,
	.port_txtstamp	     = hellcreek_port_txtstamp,
	.port_vlan_add	     = hellcreek_vlan_add,
	.port_vlan_del	     = hellcreek_vlan_del,
	.port_vlan_filtering = hellcreek_vlan_filtering,
	.setup		     = hellcreek_setup,
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

		port->vlan_dev_bitmap =
			devm_kcalloc(dev,
				     BITS_TO_LONGS(VLAN_N_VID),
				     sizeof(unsigned long),
				     GFP_KERNEL);
		if (!port->vlan_dev_bitmap)
			return -ENOMEM;

		port->hellcreek	= hellcreek;
		port->port	= i;
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
	if (IS_ERR(hellcreek->base)) {
		dev_err(dev, "No memory available!\n");
		return PTR_ERR(hellcreek->base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ptp");
	if (!res) {
		dev_err(dev, "No PTP memory region provided!\n");
		return -ENODEV;
	}

	hellcreek->ptp_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hellcreek->ptp_base)) {
		dev_err(dev, "No memory available!\n");
		return PTR_ERR(hellcreek->ptp_base);
	}

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

static int hellcreek_remove(struct platform_device *pdev)
{
	struct hellcreek *hellcreek = platform_get_drvdata(pdev);

	hellcreek_hwtstamp_free(hellcreek);
	hellcreek_ptp_free(hellcreek);
	dsa_unregister_switch(hellcreek->ds);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct hellcreek_platform_data de1soc_r1_pdata = {
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
	.remove = hellcreek_remove,
	.driver = {
		.name = "hellcreek",
		.of_match_table = hellcreek_of_match,
	},
};
module_platform_driver(hellcreek_driver);

MODULE_AUTHOR("Kurt Kanzenbach <kurt@linutronix.de>");
MODULE_DESCRIPTION("Hirschmann Hellcreek driver");
MODULE_LICENSE("Dual MIT/GPL");
