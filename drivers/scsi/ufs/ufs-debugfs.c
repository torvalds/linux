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

static int ee_usr_mask_get(void *data, u64 *val)
{
	struct ufs_hba *hba = data;

	*val = hba->ee_usr_mask;
	return 0;
}

static int ufs_debugfs_get_user_access(struct ufs_hba *hba)
__acquires(&hba->host_sem)
{
	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba)) {
		up(&hba->host_sem);
		return -EBUSY;
	}
	pm_runtime_get_sync(hba->dev);
	return 0;
}

static void ufs_debugfs_put_user_access(struct ufs_hba *hba)
__releases(&hba->host_sem)
{
	pm_runtime_put_sync(hba->dev);
	up(&hba->host_sem);
}

static int ee_usr_mask_set(void *data, u64 val)
{
	struct ufs_hba *hba = data;
	int err;

	if (val & ~(u64)MASK_EE_STATUS)
		return -EINVAL;
	err = ufs_debugfs_get_user_access(hba);
	if (err)
		return err;
	err = ufshcd_update_ee_usr_mask(hba, val, MASK_EE_STATUS);
	ufs_debugfs_put_user_access(hba);
	return err;
}

DEFINE_DEBUGFS_ATTRIBUTE(ee_usr_mask_fops, ee_usr_mask_get, ee_usr_mask_set, "%#llx\n");

void ufs_debugfs_hba_init(struct ufs_hba *hba)
{
	hba->debugfs_root = debugfs_create_dir(dev_name(hba->dev), ufs_debugfs_root);
	debugfs_create_file("stats", 0400, hba->debugfs_root, hba, &ufs_debugfs_stats_fops);
	debugfs_create_file("exception_event_mask", 0600, hba->debugfs_root,
			    hba, &ee_usr_mask_fops);
}

void ufs_debugfs_hba_exit(struct ufs_hba *hba)
{
	debugfs_remove_recursive(hba->debugfs_root);
}
