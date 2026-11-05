// SPDX-License-Identifier: GPL-2.0+
/* Copyright 2025 NXP */

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string_choices.h>

#include "enetc_pf_common.h"
#include "enetc4_debugfs.h"

static void enetc_vf_state_lock(struct enetc_pf *pf, int vf_id)
{
	struct enetc_vf_state *vf_state;

	if (vf_id < 0)
		return;

	vf_state = &pf->vf_state[vf_id];
	mutex_lock(&vf_state->lock);
}

static void enetc_vf_state_unlock(struct enetc_pf *pf, int vf_id)
{
	struct enetc_vf_state *vf_state;

	if (vf_id < 0)
		return;

	vf_state = &pf->vf_state[vf_id];
	mutex_unlock(&vf_state->lock);
}

static void enetc_show_si_mac_hash_filter(struct seq_file *s, int si_id)
{
	struct enetc_pf *pf = enetc_si_priv(s->private);
	struct enetc_hw *hw = &pf->si->hw;
	u32 hash_h, hash_l;

	enetc_vf_state_lock(pf, si_id - 1);
	hash_l = enetc_port_rd(hw, ENETC4_PSIUMHFR0(si_id));
	hash_h = enetc_port_rd(hw, ENETC4_PSIUMHFR1(si_id));
	seq_printf(s, "SI %d unicast MAC hash filter: 0x%08x%08x\n",
		   si_id, hash_h, hash_l);

	hash_l = enetc_port_rd(hw, ENETC4_PSIMMHFR0(si_id));
	hash_h = enetc_port_rd(hw, ENETC4_PSIMMHFR1(si_id));
	seq_printf(s, "SI %d multicast MAC hash filter: 0x%08x%08x\n",
		   si_id, hash_h, hash_l);
	enetc_vf_state_unlock(pf, si_id - 1);
}

static int enetc_mac_filter_show(struct seq_file *s, void *data)
{
	struct enetc_pf *pf = enetc_si_priv(s->private);
	struct enetc_hw *hw = &pf->si->hw;
	int num_si = pf->total_vfs + 1;
	struct maft_entry_data maft;
	struct ntmp_user *user;
	u32 val, entry_id;
	int err = 0;
	int i;

	/* Prevent concurrent access from causing PSIPMMR to be modified */
	spin_lock(&pf->si->gen_lock);
	val = enetc_port_rd(hw, ENETC4_PSIPMMR);
	spin_unlock(&pf->si->gen_lock);

	for (i = 0; i < num_si; i++) {
		seq_printf(s, "SI %d Unicast Promiscuous mode: %s\n", i,
			   str_enabled_disabled(PSIPMMR_SI_MAC_UP(i) & val));
		seq_printf(s, "SI %d Multicast Promiscuous mode: %s\n", i,
			   str_enabled_disabled(PSIPMMR_SI_MAC_MP(i) & val));
	}

	rtnl_lock();
	/* MAC hash filter table */
	for (i = 0; i < num_si; i++)
		enetc_show_si_mac_hash_filter(s, i);

	user = &pf->si->ntmp_user;
	if (bitmap_empty(user->maft_eid_bitmap, user->maft_num_entries))
		goto unlock_rtnl;

	/* MAC address filter table */
	seq_puts(s, "MAC address filter table\n");
	for_each_set_bit(entry_id, user->maft_eid_bitmap,
			 user->maft_num_entries) {
		memset(&maft, 0, sizeof(maft));
		err = ntmp_maft_query_entry(user, entry_id, &maft);
		if (err)
			goto unlock_rtnl;

		seq_printf(s, "Entry %d, MAC: %pM, SI bitmap: 0x%04x\n",
			   entry_id, maft.keye.mac_addr,
			   le16_to_cpu(maft.cfge.si_bitmap));
	}

unlock_rtnl:
	rtnl_unlock();

	return err;
}
DEFINE_SHOW_ATTRIBUTE(enetc_mac_filter);

void enetc_create_debugfs(struct enetc_si *si)
{
	struct dentry *root;

	root = debugfs_create_dir(pci_name(si->pdev), NULL);
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
