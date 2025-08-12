// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Intel Corporation. */

#include <linux/debugfs.h>
#include "ice.h"

static struct dentry *ice_debugfs_root;

int ice_debugfs_pf_init(struct ice_pf *pf)
{
	const char *name = pci_name(pf->pdev);

	pf->ice_debugfs_pf = debugfs_create_dir(name, ice_debugfs_root);
	if (IS_ERR(pf->ice_debugfs_pf))
		return PTR_ERR(pf->ice_debugfs_pf);

	return 0;
}

/**
 * ice_debugfs_pf_deinit - cleanup PF's debugfs
 * @pf: pointer to the PF struct
 */
void ice_debugfs_pf_deinit(struct ice_pf *pf)
{
	debugfs_remove_recursive(pf->ice_debugfs_pf);
	pf->ice_debugfs_pf = NULL;
}

/**
 * ice_debugfs_init - create root directory for debugfs entries
 */
void ice_debugfs_init(void)
{
	ice_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(ice_debugfs_root))
		pr_info("init of debugfs failed\n");
}

/**
 * ice_debugfs_exit - remove debugfs entries
 */
void ice_debugfs_exit(void)
{
	debugfs_remove_recursive(ice_debugfs_root);
	ice_debugfs_root = NULL;
}
