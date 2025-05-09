// SPDX-License-Identifier: GPL-2.0+
/* Copyright 2025 NXP */

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string_choices.h>

#include "enetc_pf.h"
#include "enetc4_debugfs.h"

static void enetc_show_si_mac_hash_filter(struct seq_file *s, int i)
{
	struct enetc_si *si = s->private;
	struct enetc_hw *hw = &si->hw;
	u32 hash_h, hash_l;

	hash_l = enetc_port_rd(hw, ENETC4_PSIUMHFR0(i));
	hash_h = enetc_port_rd(hw, ENETC4_PSIUMHFR1(i));
	seq_printf(s, "SI %d unicast MAC hash filter: 0x%08x%08x\n",
		   i, hash_h, hash_l);

	hash_l = enetc_port_rd(hw, ENETC4_PSIMMHFR0(i));
	hash_h = enetc_port_rd(hw, ENETC4_PSIMMHFR1(i));
	seq_printf(s, "SI %d multicast MAC hash filter: 0x%08x%08x\n",
		   i, hash_h, hash_l);
}

static int enetc_mac_filter_show(struct seq_file *s, void *data)
{
	struct enetc_si *si = s->private;
	struct enetc_hw *hw = &si->hw;
	struct maft_entry_data maft;
	struct enetc_pf *pf;
	int i, err, num_si;
	u32 val;

	pf = enetc_si_priv(si);
	num_si = pf->caps.num_vsi + 1;

	val = enetc_port_rd(hw, ENETC4_PSIPMMR);
	for (i = 0; i < num_si; i++) {
		seq_printf(s, "SI %d Unicast Promiscuous mode: %s\n", i,
			   str_enabled_disabled(PSIPMMR_SI_MAC_UP(i) & val));
		seq_printf(s, "SI %d Multicast Promiscuous mode: %s\n", i,
			   str_enabled_disabled(PSIPMMR_SI_MAC_MP(i) & val));
	}

	/* MAC hash filter table */
	for (i = 0; i < num_si; i++)
		enetc_show_si_mac_hash_filter(s, i);

	if (!pf->num_mfe)
		return 0;

	/* MAC address filter table */
	seq_puts(s, "MAC address filter table\n");
	for (i = 0; i < pf->num_mfe; i++) {
		memset(&maft, 0, sizeof(maft));
		err = ntmp_maft_query_entry(&si->ntmp_user, i, &maft);
		if (err)
			return err;

		seq_printf(s, "Entry %d, MAC: %pM, SI bitmap: 0x%04x\n", i,
			   maft.keye.mac_addr, le16_to_cpu(maft.cfge.si_bitmap));
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(enetc_mac_filter);

void enetc_create_debugfs(struct enetc_si *si)
{
	struct net_device *ndev = si->ndev;
	struct dentry *root;

	root = debugfs_create_dir(netdev_name(ndev), NULL);
	if (IS_ERR(root))
		return;

	si->debugfs_root = root;

	debugfs_create_file("mac_filter", 0444, root, si, &enetc_mac_filter_fops);
}

void enetc_remove_debugfs(struct enetc_si *si)
{
	debugfs_remove(si->debugfs_root);
	si->debugfs_root = NULL;
}
