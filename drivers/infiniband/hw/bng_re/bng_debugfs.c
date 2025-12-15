// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.
#include <linux/debugfs.h>
#include <linux/pci.h>

#include <rdma/ib_verbs.h>

#include "bng_res.h"
#include "bng_fw.h"
#include "bnge.h"
#include "bnge_auxr.h"
#include "bng_re.h"
#include "bng_debugfs.h"

static struct dentry *bng_re_debugfs_root;

void bng_re_debugfs_add_pdev(struct bng_re_dev *rdev)
{
	struct pci_dev *pdev = rdev->aux_dev->pdev;

	rdev->dbg_root =
		debugfs_create_dir(dev_name(&pdev->dev), bng_re_debugfs_root);
}

void bng_re_debugfs_rem_pdev(struct bng_re_dev *rdev)
{
	debugfs_remove_recursive(rdev->dbg_root);
	rdev->dbg_root = NULL;
}

void bng_re_register_debugfs(void)
{
	bng_re_debugfs_root = debugfs_create_dir("bng_re", NULL);
}

void bng_re_unregister_debugfs(void)
{
	debugfs_remove(bng_re_debugfs_root);
}
