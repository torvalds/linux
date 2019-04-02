// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>

#include <net/bonding.h>
#include <net/bond_alb.h>

#if defined(CONFIG_DE_FS) && !defined(CONFIG_NET_NS)

#include <linux/defs.h>
#include <linux/seq_file.h>

static struct dentry *bonding_de_root;

/* Show RLB hash table */
static int bond_de_rlb_hash_show(struct seq_file *m, void *v)
{
	struct bonding *bond = m->private;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info;
	u32 hash_index;

	if (BOND_MODE(bond) != BOND_MODE_ALB)
		return 0;

	seq_printf(m, "SourceIP        DestinationIP   "
			"Destination MAC   DEV\n");

	spin_lock_bh(&bond->mode_lock);

	hash_index = bond_info->rx_hashtbl_used_head;
	for (; hash_index != RLB_NULL_INDEX;
	     hash_index = client_info->used_next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		seq_printf(m, "%-15pI4 %-15pI4 %-17pM %s\n",
			&client_info->ip_src,
			&client_info->ip_dst,
			&client_info->mac_dst,
			client_info->slave->dev->name);
	}

	spin_unlock_bh(&bond->mode_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(bond_de_rlb_hash);

void bond_de_register(struct bonding *bond)
{
	if (!bonding_de_root)
		return;

	bond->de_dir =
		defs_create_dir(bond->dev->name, bonding_de_root);

	if (!bond->de_dir) {
		netdev_warn(bond->dev, "failed to register to defs\n");
		return;
	}

	defs_create_file("rlb_hash_table", 0400, bond->de_dir,
				bond, &bond_de_rlb_hash_fops);
}

void bond_de_unregister(struct bonding *bond)
{
	if (!bonding_de_root)
		return;

	defs_remove_recursive(bond->de_dir);
}

void bond_de_reregister(struct bonding *bond)
{
	struct dentry *d;

	if (!bonding_de_root)
		return;

	d = defs_rename(bonding_de_root, bond->de_dir,
			   bonding_de_root, bond->dev->name);
	if (d) {
		bond->de_dir = d;
	} else {
		netdev_warn(bond->dev, "failed to reregister, so just unregister old one\n");
		bond_de_unregister(bond);
	}
}

void bond_create_defs(void)
{
	bonding_de_root = defs_create_dir("bonding", NULL);

	if (!bonding_de_root) {
		pr_warn("Warning: Cannot create bonding directory in defs\n");
	}
}

void bond_destroy_defs(void)
{
	defs_remove_recursive(bonding_de_root);
	bonding_de_root = NULL;
}


#else /* !CONFIG_DE_FS */

void bond_de_register(struct bonding *bond)
{
}

void bond_de_unregister(struct bonding *bond)
{
}

void bond_de_reregister(struct bonding *bond)
{
}

void bond_create_defs(void)
{
}

void bond_destroy_defs(void)
{
}

#endif /* CONFIG_DE_FS */
