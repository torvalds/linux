// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/debugfs.h>
#include <linux/pci.h>

#include "fbnic.h"

static struct dentry *fbnic_dbg_root;

void fbnic_dbg_fbd_init(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	const char *name = pci_name(pdev);

	fbd->dbg_fbd = debugfs_create_dir(name, fbnic_dbg_root);
}

void fbnic_dbg_fbd_exit(struct fbnic_dev *fbd)
{
	debugfs_remove_recursive(fbd->dbg_fbd);
	fbd->dbg_fbd = NULL;
}

void fbnic_dbg_init(void)
{
	fbnic_dbg_root = debugfs_create_dir(fbnic_driver_name, NULL);
}

void fbnic_dbg_exit(void)
{
	debugfs_remove_recursive(fbnic_dbg_root);
	fbnic_dbg_root = NULL;
}
