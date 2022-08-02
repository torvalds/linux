// SPDX-License-Identifier: GPL-2.0
/* Realtek SMI library helpers for the RTL8366x variants
 * RTL8366RB and RTL8366S
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Copyright (C) 2009-2010 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2010 Antti Seppälä <a.seppala@gmail.com>
 * Copyright (C) 2010 Roman Yeryomin <roman@advem.lv>
 * Copyright (C) 2011 Colin Leitner <colin.leitner@googlemail.com>
 */
#include <linux/if_bridge.h>
#include <net/dsa.h>

#include "realtek.h"

int rtl8366_mc_is_used(struct realtek_priv *priv, int mc_index, int *used)
{
	int ret;
	int i;

	*used = 0;
	for (i = 0; i < priv->num_ports; i++) {
		int index = 0;

		ret = priv->ops->get_mc_index(priv, i, &index);
		if (ret)
			return ret;

		if (mc_index == index) {
			*used = 1;
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_mc_is_used);

/**
 * rtl8366_obtain_mc() - retrieve or allocate a VLAN member configuration
 * @priv: the Realtek SMI device instance
 * @vid: the VLAN ID to look up or allocate
 * @vlanmc: the pointer will be assigned to a pointer to a valid member config
 * if successful
 * @return: index of a new member config or negative error number
 */
static int rtl8366_obtain_mc(struct realtek_priv *priv, int vid,
			     struct rtl8366_vlan_mc *vlanmc)
{
	struct rtl8366_vlan_4k vlan4k;
	int ret;
	int i;

	/* Try to find an existing member config entry for this VID */
	for (i = 0; i < priv->num_vlan_mc; i++) {
		ret = priv->ops->get_vlan_mc(priv, i, vlanmc);
		if (ret) {
			dev_err(priv->dev, "error searching for VLAN MC %d for VID %d\n",
				i, vid);
			return ret;
		}

		if (vid == vlanmc->vid)
			return i;
	}

	/* We have no MC entry for this VID, try to find an empty one */
	for (i = 0; i < priv->num_vlan_mc; i++) {
		ret = priv->ops->get_vlan_mc(priv, i, vlanmc);
		if (ret) {
			dev_err(priv->dev, "error searching for VLAN MC %d for VID %d\n",
				i, vid);
			return ret;
		}

		if (vlanmc->vid == 0 && vlanmc->member == 0) {
			/* Update the entry from the 4K table */
			ret = priv->ops->get_vlan_4k(priv, vid, &vlan4k);
			if (ret) {
				dev_err(priv->dev, "error looking for 4K VLAN MC %d for VID %d\n",
					i, vid);
				return ret;
			}

			vlanmc->vid = vid;
			vlanmc->member = vlan4k.member;
			vlanmc->untag = vlan4k.untag;
			vlanmc->fid = vlan4k.fid;
			ret = priv->ops->set_vlan_mc(priv, i, vlanmc);
			if (ret) {
				dev_err(priv->dev, "unable to set/update VLAN MC %d for VID %d\n",
					i, vid);
				return ret;
			}

			dev_dbg(priv->dev, "created new MC at index %d for VID %d\n",
				i, vid);
			return i;
		}
	}

	/* MC table is full, try to find an unused entry and replace it */
	for (i = 0; i < priv->num_vlan_mc; i++) {
		int used;

		ret = rtl8366_mc_is_used(priv, i, &used);
		if (ret)
			return ret;

		if (!used) {
			/* Update the entry from the 4K table */
			ret = priv->ops->get_vlan_4k(priv, vid, &vlan4k);
			if (ret)
				return ret;

			vlanmc->vid = vid;
			vlanmc->member = vlan4k.member;
			vlanmc->untag = vlan4k.untag;
			vlanmc->fid = vlan4k.fid;
			ret = priv->ops->set_vlan_mc(priv, i, vlanmc);
			if (ret) {
				dev_err(priv->dev, "unable to set/update VLAN MC %d for VID %d\n",
					i, vid);
				return ret;
			}
			dev_dbg(priv->dev, "recycled MC at index %i for VID %d\n",
				i, vid);
			return i;
		}
	}

	dev_err(priv->dev, "all VLAN member configurations are in use\n");
	return -ENOSPC;
}

int rtl8366_set_vlan(struct realtek_priv *priv, int vid, u32 member,
		     u32 untag, u32 fid)
{
	struct rtl8366_vlan_mc vlanmc;
	struct rtl8366_vlan_4k vlan4k;
	int mc;
	int ret;

	if (!priv->ops->is_vlan_valid(priv, vid))
		return -EINVAL;

	dev_dbg(priv->dev,
		"setting VLAN%d 4k members: 0x%02x, untagged: 0x%02x\n",
		vid, member, untag);

	/* Update the 4K table */
	ret = priv->ops->get_vlan_4k(priv, vid, &vlan4k);
	if (ret)
		return ret;

	vlan4k.member |= member;
	vlan4k.untag |= untag;
	vlan4k.fid = fid;
	ret = priv->ops->set_vlan_4k(priv, &vlan4k);
	if (ret)
		return ret;

	dev_dbg(priv->dev,
		"resulting VLAN%d 4k members: 0x%02x, untagged: 0x%02x\n",
		vid, vlan4k.member, vlan4k.untag);

	/* Find or allocate a member config for this VID */
	ret = rtl8366_obtain_mc(priv, vid, &vlanmc);
	if (ret < 0)
		return ret;
	mc = ret;

	/* Update the MC entry */
	vlanmc.member |= member;
	vlanmc.untag |= untag;
	vlanmc.fid = fid;

	/* Commit updates to the MC entry */
	ret = priv->ops->set_vlan_mc(priv, mc, &vlanmc);
	if (ret)
		dev_err(priv->dev, "failed to commit changes to VLAN MC index %d for VID %d\n",
			mc, vid);
	else
		dev_dbg(priv->dev,
			"resulting VLAN%d MC members: 0x%02x, untagged: 0x%02x\n",
			vid, vlanmc.member, vlanmc.untag);

	return ret;
}
EXPORT_SYMBOL_GPL(rtl8366_set_vlan);

int rtl8366_set_pvid(struct realtek_priv *priv, unsigned int port,
		     unsigned int vid)
{
	struct rtl8366_vlan_mc vlanmc;
	int mc;
	int ret;

	if (!priv->ops->is_vlan_valid(priv, vid))
		return -EINVAL;

	/* Find or allocate a member config for this VID */
	ret = rtl8366_obtain_mc(priv, vid, &vlanmc);
	if (ret < 0)
		return ret;
	mc = ret;

	ret = priv->ops->set_mc_index(priv, port, mc);
	if (ret) {
		dev_err(priv->dev, "set PVID: failed to set MC index %d for port %d\n",
			mc, port);
		return ret;
	}

	dev_dbg(priv->dev, "set PVID: the PVID for port %d set to %d using existing MC index %d\n",
		port, vid, mc);

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_set_pvid);

int rtl8366_enable_vlan4k(struct realtek_priv *priv, bool enable)
{
	int ret;

	/* To enable 4k VLAN, ordinary VLAN must be enabled first,
	 * but if we disable 4k VLAN it is fine to leave ordinary
	 * VLAN enabled.
	 */
	if (enable) {
		/* Make sure VLAN is ON */
		ret = priv->ops->enable_vlan(priv, true);
		if (ret)
			return ret;

		priv->vlan_enabled = true;
	}

	ret = priv->ops->enable_vlan4k(priv, enable);
	if (ret)
		return ret;

	priv->vlan4k_enabled = enable;
	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_enable_vlan4k);

int rtl8366_enable_vlan(struct realtek_priv *priv, bool enable)
{
	int ret;

	ret = priv->ops->enable_vlan(priv, enable);
	if (ret)
		return ret;

	priv->vlan_enabled = enable;

	/* If we turn VLAN off, make sure that we turn off
	 * 4k VLAN as well, if that happened to be on.
	 */
	if (!enable) {
		priv->vlan4k_enabled = false;
		ret = priv->ops->enable_vlan4k(priv, false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rtl8366_enable_vlan);

int rtl8366_reset_vlan(struct realtek_priv *priv)
{
	struct rtl8366_vlan_mc vlanmc;
	int ret;
	int i;

	rtl8366_enable_vlan(priv, false);
	rtl8366_enable_vlan4k(priv, false);

	/* Clear the 16 VLAN member configurations */
	vlanmc.vid = 0;
	vlanmc.priority = 0;
	vlanmc.member = 0;
	vlanmc.untag = 0;
	vlanmc.fid = 0;
	for (i = 0; i < priv->num_vlan_mc; i++) {
		ret = priv->ops->set_vlan_mc(priv, i, &vlanmc);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_reset_vlan);

int rtl8366_vlan_add(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_vlan *vlan,
		     struct netlink_ext_ack *extack)
{
	bool untagged = !!(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
	bool pvid = !!(vlan->flags & BRIDGE_VLAN_INFO_PVID);
	struct realtek_priv *priv = ds->priv;
	u32 member = 0;
	u32 untag = 0;
	int ret;

	if (!priv->ops->is_vlan_valid(priv, vlan->vid)) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN ID not valid");
		return -EINVAL;
	}

	/* Enable VLAN in the hardware
	 * FIXME: what's with this 4k business?
	 * Just rtl8366_enable_vlan() seems inconclusive.
	 */
	ret = rtl8366_enable_vlan4k(priv, true);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to enable VLAN 4K");
		return ret;
	}

	dev_dbg(priv->dev, "add VLAN %d on port %d, %s, %s\n",
		vlan->vid, port, untagged ? "untagged" : "tagged",
		pvid ? "PVID" : "no PVID");

	member |= BIT(port);

	if (untagged)
		untag |= BIT(port);

	ret = rtl8366_set_vlan(priv, vlan->vid, member, untag, 0);
	if (ret) {
		dev_err(priv->dev, "failed to set up VLAN %04x", vlan->vid);
		return ret;
	}

	if (!pvid)
		return 0;

	ret = rtl8366_set_pvid(priv, port, vlan->vid);
	if (ret) {
		dev_err(priv->dev, "failed to set PVID on port %d to VLAN %04x",
			port, vlan->vid);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_vlan_add);

int rtl8366_vlan_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_vlan *vlan)
{
	struct realtek_priv *priv = ds->priv;
	int ret, i;

	dev_dbg(priv->dev, "del VLAN %d on port %d\n", vlan->vid, port);

	for (i = 0; i < priv->num_vlan_mc; i++) {
		struct rtl8366_vlan_mc vlanmc;

		ret = priv->ops->get_vlan_mc(priv, i, &vlanmc);
		if (ret)
			return ret;

		if (vlan->vid == vlanmc.vid) {
			/* Remove this port from the VLAN */
			vlanmc.member &= ~BIT(port);
			vlanmc.untag &= ~BIT(port);
			/*
			 * If no ports are members of this VLAN
			 * anymore then clear the whole member
			 * config so it can be reused.
			 */
			if (!vlanmc.member) {
				vlanmc.vid = 0;
				vlanmc.priority = 0;
				vlanmc.fid = 0;
			}
			ret = priv->ops->set_vlan_mc(priv, i, &vlanmc);
			if (ret) {
				dev_err(priv->dev,
					"failed to remove VLAN %04x\n",
					vlan->vid);
				return ret;
			}
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_vlan_del);

void rtl8366_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			 uint8_t *data)
{
	struct realtek_priv *priv = ds->priv;
	struct rtl8366_mib_counter *mib;
	int i;

	if (port >= priv->num_ports)
		return;

	for (i = 0; i < priv->num_mib_counters; i++) {
		mib = &priv->mib_counters[i];
		strncpy(data + i * ETH_GSTRING_LEN,
			mib->name, ETH_GSTRING_LEN);
	}
}
EXPORT_SYMBOL_GPL(rtl8366_get_strings);

int rtl8366_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct realtek_priv *priv = ds->priv;

	/* We only support SS_STATS */
	if (sset != ETH_SS_STATS)
		return 0;
	if (port >= priv->num_ports)
		return -EINVAL;

	return priv->num_mib_counters;
}
EXPORT_SYMBOL_GPL(rtl8366_get_sset_count);

void rtl8366_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct realtek_priv *priv = ds->priv;
	int i;
	int ret;

	if (port >= priv->num_ports)
		return;

	for (i = 0; i < priv->num_mib_counters; i++) {
		struct rtl8366_mib_counter *mib;
		u64 mibvalue = 0;

		mib = &priv->mib_counters[i];
		ret = priv->ops->get_mib_counter(priv, port, mib, &mibvalue);
		if (ret) {
			dev_err(priv->dev, "error reading MIB counter %s\n",
				mib->name);
		}
		data[i] = mibvalue;
	}
}
EXPORT_SYMBOL_GPL(rtl8366_get_ethtool_stats);
