#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>

#include "bonding.h"

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static struct dentry *bonding_debug_root;

void bond_debug_register(struct bonding *bond)
{
	if (!bonding_debug_root)
		return;

	bond->debug_dir =
		debugfs_create_dir(bond->dev->name, bonding_debug_root);

	if (!bond->debug_dir) {
		pr_warning("%s: Warning: failed to register to debugfs\n",
			bond->dev->name);
		return;
	}
}

void bond_debug_unregister(struct bonding *bond)
{
	if (!bonding_debug_root)
		return;

	debugfs_remove_recursive(bond->debug_dir);
}

void bond_debug_reregister(struct bonding *bond)
{
	struct dentry *d;

	if (!bonding_debug_root)
		return;

	d = debugfs_rename(bonding_debug_root, bond->debug_dir,
			   bonding_debug_root, bond->dev->name);
	if (d) {
		bond->debug_dir = d;
	} else {
		pr_warning("%s: Warning: failed to reregister, "
				"so just unregister old one\n",
				bond->dev->name);
		bond_debug_unregister(bond);
	}
}

void bond_create_debugfs(void)
{
	bonding_debug_root = debugfs_create_dir("bonding", NULL);

	if (!bonding_debug_root) {
		pr_warning("Warning: Cannot create bonding directory"
				" in debugfs\n");
	}
}

void bond_destroy_debugfs(void)
{
	debugfs_remove_recursive(bonding_debug_root);
	bonding_debug_root = NULL;
}


#else /* !CONFIG_DEBUG_FS */

void bond_debug_register(struct bonding *bond)
{
}

void bond_debug_unregister(struct bonding *bond)
{
}

void bond_debug_reregister(struct bonding *bond)
{
}

void bond_create_debugfs(void)
{
}

void bond_destroy_debugfs(void)
{
}

#endif /* CONFIG_DEBUG_FS */
