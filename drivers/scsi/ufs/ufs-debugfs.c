// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Intel Corporation

#include <linux/debugfs.h>

#include "ufs-debugfs.h"
#include "ufshcd.h"

static struct dentry *ufs_debugfs_root;

void __init ufs_debugfs_init(void)
{
	ufs_debugfs_root = debugfs_create_dir("ufshcd", NULL);
}

void __exit ufs_debugfs_exit(void)
{
	debugfs_remove_recursive(ufs_debugfs_root);
}

static int ufs_debugfs_stats_show(struct seq_file *s, void *data)
{
	struct ufs_hba *hba = s->private;
	struct ufs_event_hist *e = hba->ufs_stats.event;

#define PRT(fmt, typ) \
	seq_printf(s, fmt, e[UFS_EVT_ ## typ].cnt)

	PRT("PHY Adapter Layer errors (except LINERESET): %llu\n", PA_ERR);
	PRT("Data Link Layer errors: %llu\n", DL_ERR);
	PRT("Network Layer errors: %llu\n", NL_ERR);
	PRT("Transport Layer errors: %llu\n", TL_ERR);
	PRT("Generic DME errors: %llu\n", DME_ERR);
	PRT("Auto-hibernate errors: %llu\n", AUTO_HIBERN8_ERR);
	PRT("IS Fatal errors (CEFES, SBFES, HCFES, DFES): %llu\n", FATAL_ERR);
	PRT("DME Link Startup errors: %llu\n", LINK_STARTUP_FAIL);
	PRT("PM Resume errors: %llu\n", RESUME_ERR);
	PRT("PM Suspend errors : %llu\n", SUSPEND_ERR);
	PRT("Logical Unit Resets: %llu\n", DEV_RESET);
	PRT("Host Resets: %llu\n", HOST_RESET);
	PRT("SCSI command aborts: %llu\n", ABORT);
#undef PRT
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ufs_debugfs_stats);

void ufs_debugfs_hba_init(struct ufs_hba *hba)
{
	hba->debugfs_root = debugfs_create_dir(dev_name(hba->dev), ufs_debugfs_root);
	debugfs_create_file("stats", 0400, hba->debugfs_root, hba, &ufs_debugfs_stats_fops);
}

void ufs_debugfs_hba_exit(struct ufs_hba *hba)
{
	debugfs_remove_recursive(hba->debugfs_root);
}
