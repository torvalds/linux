// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

#include "aux_drv.h"
#include "debugfs.h"

static struct dentry *dbfs_dir;

void pds_vdpa_debugfs_create(void)
{
	dbfs_dir = debugfs_create_dir(PDS_VDPA_DRV_NAME, NULL);
}

void pds_vdpa_debugfs_destroy(void)
{
	debugfs_remove_recursive(dbfs_dir);
	dbfs_dir = NULL;
}
