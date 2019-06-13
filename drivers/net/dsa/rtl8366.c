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

#include "realtek-smi-core.h"

int rtl8366_mc_is_used(struct realtek_smi *smi, int mc_index, int *used)
{
	int ret;
	int i;

	*used = 0;
	for (i = 0; i < smi->num_ports; i++) {
		int index = 0;

		ret = smi->ops->get_mc_index(smi, i, &index);
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

int rtl8366_set_vlan(struct realtek_smi *smi, int vid, u32 member,
		     u32 untag, u32 fid)
{
	struct rtl8366_vlan_4k vlan4k;
	int ret;
	int i;

	/* Update the 4K table */
	ret = smi->ops->get_vlan_4k(smi, vid, &vlan4k);
	if (ret)
		return ret;

	vlan4k.member = member;
	vlan4k.untag = untag;
	vlan4k.fid = fid;
	ret = smi->ops->set_vlan_4k(smi, &vlan4k);
	if (ret)
		return ret;

	/* Try to find an existing MC entry for this VID */
	for (i = 0; i < smi->num_vlan_mc; i++) {
		struct rtl8366_vlan_mc vlanmc;

		ret = smi->ops->get_vlan_mc(smi, i, &vlanmc);
		if (ret)
			return ret;

		if (vid == vlanmc.vid) {
			/* update the MC entry */
			vlanmc.member = member;
			vlanmc.untag = untag;
			vlanmc.fid = fid;

			ret = smi->ops->set_vlan_mc(smi, i, &vlanmc);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rtl8366_set_vlan);

int rtl8366_get_pvid(struct realtek_smi *smi, int port, int *val)
{
	struct rtl8366_vlan_mc vlanmc;
	int ret;
	int index;

	ret = smi->ops->get_mc_index(smi, port, &index);
	if (ret)
		return ret;

	ret = smi->ops->get_vlan_mc(smi, index, &vlanmc);
	if (ret)
		return ret;

	*val = vlanmc.vid;
	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_get_pvid);

int rtl8366_set_pvid(struct realtek_smi *smi, unsigned int port,
		     unsigned int vid)
{
	struct rtl8366_vlan_mc vlanmc;
	struct rtl8366_vlan_4k vlan4k;
	int ret;
	int i;

	/* Try to find an existing MC entry for this VID */
	for (i = 0; i < smi->num_vlan_mc; i++) {
		ret = smi->ops->get_vlan_mc(smi, i, &vlanmc);
		if (ret)
			return ret;

		if (vid == vlanmc.vid) {
			ret = smi->ops->set_vlan_mc(smi, i, &vlanmc);
			if (ret)
				return ret;

			ret = smi->ops->set_mc_index(smi, port, i);
			return ret;
		}
	}

	/* We have no MC entry for this VID, try to find an empty one */
	for (i = 0; i < smi->num_vlan_mc; i++) {
		ret = smi->ops->get_vlan_mc(smi, i, &vlanmc);
		if (ret)
			return ret;

		if (vlanmc.vid == 0 && vlanmc.member == 0) {
			/* Update the entry from the 4K table */
			ret = smi->ops->get_vlan_4k(smi, vid, &vlan4k);
			if (ret)
				return ret;

			vlanmc.vid = vid;
			vlanmc.member = vlan4k.member;
			vlanmc.untag = vlan4k.untag;
			vlanmc.fid = vlan4k.fid;
			ret = smi->ops->set_vlan_mc(smi, i, &vlanmc);
			if (ret)
				return ret;

			ret = smi->ops->set_mc_index(smi, port, i);
			return ret;
		}
	}

	/* MC table is full, try to find an unused entry and replace it */
	for (i = 0; i < smi->num_vlan_mc; i++) {
		int used;

		ret = rtl8366_mc_is_used(smi, i, &used);
		if (ret)
			return ret;

		if (!used) {
			/* Update the entry from the 4K table */
			ret = smi->ops->get_vlan_4k(smi, vid, &vlan4k);
			if (ret)
				return ret;

			vlanmc.vid = vid;
			vlanmc.member = vlan4k.member;
			vlanmc.untag = vlan4k.untag;
			vlanmc.fid = vlan4k.fid;
			ret = smi->ops->set_vlan_mc(smi, i, &vlanmc);
			if (ret)
				return ret;

			ret = smi->ops->set_mc_index(smi, port, i);
			return ret;
		}
	}

	dev_err(smi->dev,
		"all VLAN member configurations are in use\n");

	return -ENOSPC;
}
EXPORT_SYMBOL_GPL(rtl8366_set_pvid);

int rtl8366_enable_vlan4k(struct realtek_smi *smi, bool enable)
{
	int ret;

	/* To enable 4k VLAN, ordinary VLAN must be enabled first,
	 * but if we disable 4k VLAN it is fine to leave ordinary
	 * VLAN enabled.
	 */
	if (enable) {
		/* Make sure VLAN is ON */
		ret = smi->ops->enable_vlan(smi, true);
		if (ret)
			return ret;

		smi->vlan_enabled = true;
	}

	ret = smi->ops->enable_vlan4k(smi, enable);
	if (ret)
		return ret;

	smi->vlan4k_enabled = enable;
	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_enable_vlan4k);

int rtl8366_enable_vlan(struct realtek_smi *smi, bool enable)
{
	int ret;

	ret = smi->ops->enable_vlan(smi, enable);
	if (ret)
		return ret;

	smi->vlan_enabled = enable;

	/* If we turn VLAN off, make sure that we turn off
	 * 4k VLAN as well, if that happened to be on.
	 */
	if (!enable) {
		smi->vlan4k_enabled = false;
		ret = smi->ops->enable_vlan4k(smi, false);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rtl8366_enable_vlan);

int rtl8366_reset_vlan(struct realtek_smi *smi)
{
	struct rtl8366_vlan_mc vlanmc;
	int ret;
	int i;

	rtl8366_enable_vlan(smi, false);
	rtl8366_enable_vlan4k(smi, false);

	/* Clear the 16 VLAN member configurations */
	vlanmc.vid = 0;
	vlanmc.priority = 0;
	vlanmc.member = 0;
	vlanmc.untag = 0;
	vlanmc.fid = 0;
	for (i = 0; i < smi->num_vlan_mc; i++) {
		ret = smi->ops->set_vlan_mc(smi, i, &vlanmc);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_reset_vlan);

int rtl8366_init_vlan(struct realtek_smi *smi)
{
	int port;
	int ret;

	ret = rtl8366_reset_vlan(smi);
	if (ret)
		return ret;

	/* Loop over the available ports, for each port, associate
	 * it with the VLAN (port+1)
	 */
	for (port = 0; port < smi->num_ports; port++) {
		u32 mask;

		if (port == smi->cpu_port)
			/* For the CPU port, make all ports members of this
			 * VLAN.
			 */
			mask = GENMASK(smi->num_ports - 1, 0);
		else
			/* For all other ports, enable itself plus the
			 * CPU port.
			 */
			mask = BIT(port) | BIT(smi->cpu_port);

		/* For each port, set the port as member of VLAN (port+1)
		 * and untagged, except for the CPU port: the CPU port (5) is
		 * member of VLAN 6 and so are ALL the other ports as well.
		 * Use filter 0 (no filter).
		 */
		dev_info(smi->dev, "VLAN%d port mask for port %d, %08x\n",
			 (port + 1), port, mask);
		ret = rtl8366_set_vlan(smi, (port + 1), mask, mask, 0);
		if (ret)
			return ret;

		dev_info(smi->dev, "VLAN%d port %d, PVID set to %d\n",
			 (port + 1), port, (port + 1));
		ret = rtl8366_set_pvid(smi, port, (port + 1));
		if (ret)
			return ret;
	}

	return rtl8366_enable_vlan(smi, true);
}
EXPORT_SYMBOL_GPL(rtl8366_init_vlan);

int rtl8366_vlan_filtering(struct dsa_switch *ds, int port, bool vlan_filtering)
{
	struct realtek_smi *smi = ds->priv;
	struct rtl8366_vlan_4k vlan4k;
	int ret;

	if (!smi->ops->is_vlan_valid(smi, port))
		return -EINVAL;

	dev_info(smi->dev, "%s filtering on port %d\n",
		 vlan_filtering ? "enable" : "disable",
		 port);

	/* TODO:
	 * The hardware support filter ID (FID) 0..7, I have no clue how to
	 * support this in the driver when the callback only says on/off.
	 */
	ret = smi->ops->get_vlan_4k(smi, port, &vlan4k);
	if (ret)
		return ret;

	/* Just set the filter to FID 1 for now then */
	ret = rtl8366_set_vlan(smi, port,
			       vlan4k.member,
			       vlan4k.untag,
			       1);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_vlan_filtering);

int rtl8366_vlan_prepare(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_vlan *vlan)
{
	struct realtek_smi *smi = ds->priv;
	int ret;

	if (!smi->ops->is_vlan_valid(smi, port))
		return -EINVAL;

	dev_info(smi->dev, "prepare VLANs %04x..%04x\n",
		 vlan->vid_begin, vlan->vid_end);

	/* Enable VLAN in the hardware
	 * FIXME: what's with this 4k business?
	 * Just rtl8366_enable_vlan() seems inconclusive.
	 */
	ret = rtl8366_enable_vlan4k(smi, true);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_vlan_prepare);

void rtl8366_vlan_add(struct dsa_switch *ds, int port,
		      const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = !!(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
	bool pvid = !!(vlan->flags & BRIDGE_VLAN_INFO_PVID);
	struct realtek_smi *smi = ds->priv;
	u32 member = 0;
	u32 untag = 0;
	u16 vid;
	int ret;

	if (!smi->ops->is_vlan_valid(smi, port))
		return;

	dev_info(smi->dev, "add VLAN on port %d, %s, %s\n",
		 port,
		 untagged ? "untagged" : "tagged",
		 pvid ? " PVID" : "no PVID");

	if (dsa_is_dsa_port(ds, port) || dsa_is_cpu_port(ds, port))
		dev_err(smi->dev, "port is DSA or CPU port\n");

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		int pvid_val = 0;

		dev_info(smi->dev, "add VLAN %04x\n", vid);
		member |= BIT(port);

		if (untagged)
			untag |= BIT(port);

		/* To ensure that we have a valid MC entry for this VLAN,
		 * initialize the port VLAN ID here.
		 */
		ret = rtl8366_get_pvid(smi, port, &pvid_val);
		if (ret < 0) {
			dev_err(smi->dev, "could not lookup PVID for port %d\n",
				port);
			return;
		}
		if (pvid_val == 0) {
			ret = rtl8366_set_pvid(smi, port, vid);
			if (ret < 0)
				return;
		}
	}

	ret = rtl8366_set_vlan(smi, port, member, untag, 0);
	if (ret)
		dev_err(smi->dev,
			"failed to set up VLAN %04x",
			vid);
}
EXPORT_SYMBOL_GPL(rtl8366_vlan_add);

int rtl8366_vlan_del(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_vlan *vlan)
{
	struct realtek_smi *smi = ds->priv;
	u16 vid;
	int ret;

	dev_info(smi->dev, "del VLAN on port %d\n", port);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		int i;

		dev_info(smi->dev, "del VLAN %04x\n", vid);

		for (i = 0; i < smi->num_vlan_mc; i++) {
			struct rtl8366_vlan_mc vlanmc;

			ret = smi->ops->get_vlan_mc(smi, i, &vlanmc);
			if (ret)
				return ret;

			if (vid == vlanmc.vid) {
				/* clear VLAN member configurations */
				vlanmc.vid = 0;
				vlanmc.priority = 0;
				vlanmc.member = 0;
				vlanmc.untag = 0;
				vlanmc.fid = 0;

				ret = smi->ops->set_vlan_mc(smi, i, &vlanmc);
				if (ret) {
					dev_err(smi->dev,
						"failed to remove VLAN %04x\n",
						vid);
					return ret;
				}
				break;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl8366_vlan_del);

void rtl8366_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			 uint8_t *data)
{
	struct realtek_smi *smi = ds->priv;
	struct rtl8366_mib_counter *mib;
	int i;

	if (port >= smi->num_ports)
		return;

	for (i = 0; i < smi->num_mib_counters; i++) {
		mib = &smi->mib_counters[i];
		strncpy(data + i * ETH_GSTRING_LEN,
			mib->name, ETH_GSTRING_LEN);
	}
}
EXPORT_SYMBOL_GPL(rtl8366_get_strings);

int rtl8366_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct realtek_smi *smi = ds->priv;

	/* We only support SS_STATS */
	if (sset != ETH_SS_STATS)
		return 0;
	if (port >= smi->num_ports)
		return -EINVAL;

	return smi->num_mib_counters;
}
EXPORT_SYMBOL_GPL(rtl8366_get_sset_count);

void rtl8366_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct realtek_smi *smi = ds->priv;
	int i;
	int ret;

	if (port >= smi->num_ports)
		return;

	for (i = 0; i < smi->num_mib_counters; i++) {
		struct rtl8366_mib_counter *mib;
		u64 mibvalue = 0;

		mib = &smi->mib_counters[i];
		ret = smi->ops->get_mib_counter(smi, port, mib, &mibvalue);
		if (ret) {
			dev_err(smi->dev, "error reading MIB counter %s\n",
				mib->name);
		}
		data[i] = mibvalue;
	}
}
EXPORT_SYMBOL_GPL(rtl8366_get_ethtool_stats);
