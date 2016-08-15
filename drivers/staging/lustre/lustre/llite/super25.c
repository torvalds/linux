/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <linux/module.h>
#include <linux/types.h>
#include "../include/lustre_lite.h"
#include "../include/lustre_ha.h"
#include "../include/lustre_dlm.h"
#include <linux/init.h>
#include <linux/fs.h>
#include "../include/lprocfs_status.h"
#include "llite_internal.h"

static struct kmem_cache *ll_inode_cachep;

static struct inode *ll_alloc_inode(struct super_block *sb)
{
	struct ll_inode_info *lli;

	ll_stats_ops_tally(ll_s2sbi(sb), LPROC_LL_ALLOC_INODE, 1);
	lli = kmem_cache_zalloc(ll_inode_cachep, GFP_NOFS);
	if (!lli)
		return NULL;

	inode_init_once(&lli->lli_vfs_inode);
	return &lli->lli_vfs_inode;
}

static void ll_inode_destroy_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct ll_inode_info *ptr = ll_i2info(inode);

	kmem_cache_free(ll_inode_cachep, ptr);
}

static void ll_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, ll_inode_destroy_callback);
}

/* exported operations */
struct super_operations lustre_super_operations = {
	.alloc_inode   = ll_alloc_inode,
	.destroy_inode = ll_destroy_inode,
	.evict_inode   = ll_delete_inode,
	.put_super     = ll_put_super,
	.statfs	= ll_statfs,
	.umount_begin  = ll_umount_begin,
	.remount_fs    = ll_remount_fs,
	.show_options  = ll_show_options,
};
MODULE_ALIAS_FS("lustre");

void lustre_register_client_process_config(int (*cpc)(struct lustre_cfg *lcfg));

static int __init lustre_init(void)
{
	lnet_process_id_t lnet_id;
	struct timespec64 ts;
	int i, rc, seed[2];

	CLASSERT(sizeof(LUSTRE_VOLATILE_HDR) == LUSTRE_VOLATILE_HDR_LEN + 1);

	/* print an address of _any_ initialized kernel symbol from this
	 * module, to allow debugging with gdb that doesn't support data
	 * symbols from modules.
	 */
	CDEBUG(D_INFO, "Lustre client module (%p).\n",
	       &lustre_super_operations);

	rc = -ENOMEM;
	ll_inode_cachep = kmem_cache_create("lustre_inode_cache",
					    sizeof(struct ll_inode_info),
					    0, SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT,
					    NULL);
	if (!ll_inode_cachep)
		goto out_cache;

	ll_file_data_slab = kmem_cache_create("ll_file_data",
					      sizeof(struct ll_file_data), 0,
					      SLAB_HWCACHE_ALIGN, NULL);
	if (!ll_file_data_slab)
		goto out_cache;

	llite_root = debugfs_create_dir("llite", debugfs_lustre_root);
	if (IS_ERR_OR_NULL(llite_root)) {
		rc = llite_root ? PTR_ERR(llite_root) : -ENOMEM;
		llite_root = NULL;
		goto out_cache;
	}

	llite_kset = kset_create_and_add("llite", NULL, lustre_kobj);
	if (!llite_kset) {
		rc = -ENOMEM;
		goto out_debugfs;
	}

	cfs_get_random_bytes(seed, sizeof(seed));

	/* Nodes with small feet have little entropy. The NID for this
	 * node gives the most entropy in the low bits
	 */
	for (i = 0;; i++) {
		if (LNetGetId(i, &lnet_id) == -ENOENT)
			break;

		if (LNET_NETTYP(LNET_NIDNET(lnet_id.nid)) != LOLND)
			seed[0] ^= LNET_NIDADDR(lnet_id.nid);
	}

	ktime_get_ts64(&ts);
	cfs_srand(ts.tv_sec ^ seed[0], ts.tv_nsec ^ seed[1]);

	rc = vvp_global_init();
	if (rc != 0)
		goto out_sysfs;

	cl_inode_fini_env = cl_env_alloc(&cl_inode_fini_refcheck,
					 LCT_REMEMBER | LCT_NOREF);
	if (IS_ERR(cl_inode_fini_env)) {
		rc = PTR_ERR(cl_inode_fini_env);
		goto out_vvp;
	}

	cl_inode_fini_env->le_ctx.lc_cookie = 0x4;

	rc = ll_xattr_init();
	if (rc != 0)
		goto out_inode_fini_env;

	lustre_register_client_fill_super(ll_fill_super);
	lustre_register_kill_super_cb(ll_kill_super);
	lustre_register_client_process_config(ll_process_config);

	return 0;

out_inode_fini_env:
	cl_env_put(cl_inode_fini_env, &cl_inode_fini_refcheck);
out_vvp:
	vvp_global_fini();
out_sysfs:
	kset_unregister(llite_kset);
out_debugfs:
	debugfs_remove(llite_root);
out_cache:
	kmem_cache_destroy(ll_inode_cachep);
	kmem_cache_destroy(ll_file_data_slab);
	return rc;
}

static void __exit lustre_exit(void)
{
	lustre_register_client_fill_super(NULL);
	lustre_register_kill_super_cb(NULL);
	lustre_register_client_process_config(NULL);

	debugfs_remove(llite_root);
	kset_unregister(llite_kset);

	ll_xattr_fini();
	cl_env_put(cl_inode_fini_env, &cl_inode_fini_refcheck);
	vvp_global_fini();

	kmem_cache_destroy(ll_inode_cachep);
	kmem_cache_destroy(ll_file_data_slab);
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Client File System");
MODULE_VERSION(LUSTRE_VERSION_STRING);
MODULE_LICENSE("GPL");

module_init(lustre_init);
module_exit(lustre_exit);
