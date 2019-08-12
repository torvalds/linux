// SPDX-License-Identifier: GPL-2.0-only
/**
 * debugfs routines supporting the Power 7+ Nest Accelerators driver
 *
 * Copyright (C) 2011-2012 International Business Machines Inc.
 *
 * Author: Kent Yoder <yoder1@us.ibm.com>
 */

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"

#ifdef CONFIG_DEBUG_FS

/*
 * debugfs
 *
 * For documentation on these attributes, please see:
 *
 * Documentation/ABI/testing/debugfs-pfo-nx-crypto
 */

void nx_debugfs_init(struct nx_crypto_driver *drv)
{
	struct dentry *root;

	root = debugfs_create_dir(NX_NAME, NULL);
	drv->dfs_root = root;

	debugfs_create_u32("aes_ops", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u32 *)&drv->stats.aes_ops);
	debugfs_create_u32("sha256_ops", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u32 *)&drv->stats.sha256_ops);
	debugfs_create_u32("sha512_ops", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u32 *)&drv->stats.sha512_ops);
	debugfs_create_u64("aes_bytes", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u64 *)&drv->stats.aes_bytes);
	debugfs_create_u64("sha256_bytes", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u64 *)&drv->stats.sha256_bytes);
	debugfs_create_u64("sha512_bytes", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u64 *)&drv->stats.sha512_bytes);
	debugfs_create_u32("errors", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u32 *)&drv->stats.errors);
	debugfs_create_u32("last_error", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u32 *)&drv->stats.last_error);
	debugfs_create_u32("last_error_pid", S_IRUSR | S_IRGRP | S_IROTH,
			   root, (u32 *)&drv->stats.last_error_pid);
}

void
nx_debugfs_fini(struct nx_crypto_driver *drv)
{
	debugfs_remove_recursive(drv->dfs_root);
}

#endif
