// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>

#include "core.h"

static struct dentry *pdsc_dir;

void pdsc_debugfs_create(void)
{
	pdsc_dir = debugfs_create_dir(PDS_CORE_DRV_NAME, NULL);
}

void pdsc_debugfs_destroy(void)
{
	debugfs_remove_recursive(pdsc_dir);
}

void pdsc_debugfs_add_dev(struct pdsc *pdsc)
{
	pdsc->dentry = debugfs_create_dir(pci_name(pdsc->pdev), pdsc_dir);

	debugfs_create_ulong("state", 0400, pdsc->dentry, &pdsc->state);
}

void pdsc_debugfs_del_dev(struct pdsc *pdsc)
{
	debugfs_remove_recursive(pdsc->dentry);
	pdsc->dentry = NULL;
}
