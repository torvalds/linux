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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/llite/llite_lib.c
 *
 * Lustre Light Super operations
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <linux/module.h>
#include <linux/statfs.h>
#include <linux/types.h>
#include <linux/mm.h>

#include "../include/lustre_lite.h"
#include "../include/lustre_ha.h"
#include "../include/lustre_dlm.h"
#include "../include/lprocfs_status.h"
#include "../include/lustre_disk.h"
#include "../include/lustre_param.h"
#include "../include/lustre_log.h"
#include "../include/cl_object.h"
#include "../include/obd_cksum.h"
#include "llite_internal.h"

struct kmem_cache *ll_file_data_slab;
struct proc_dir_entry *proc_lustre_fs_root;

static LIST_HEAD(ll_super_blocks);
static DEFINE_SPINLOCK(ll_sb_lock);

#ifndef log2
#define log2(n) ffz(~(n))
#endif

static struct ll_sb_info *ll_init_sbi(void)
{
	struct ll_sb_info *sbi = NULL;
	unsigned long pages;
	unsigned long lru_page_max;
	struct sysinfo si;
	class_uuid_t uuid;
	int i;

	sbi = kzalloc(sizeof(*sbi), GFP_NOFS);
	if (!sbi)
		return NULL;

	spin_lock_init(&sbi->ll_lock);
	mutex_init(&sbi->ll_lco.lco_lock);
	spin_lock_init(&sbi->ll_pp_extent_lock);
	spin_lock_init(&sbi->ll_process_lock);
	sbi->ll_rw_stats_on = 0;

	si_meminfo(&si);
	pages = si.totalram - si.totalhigh;
	if (pages >> (20 - PAGE_CACHE_SHIFT) < 512) {
		lru_page_max = pages / 2;
	} else {
		lru_page_max = (pages / 4) * 3;
	}

	/* initialize lru data */
	atomic_set(&sbi->ll_cache.ccc_users, 0);
	sbi->ll_cache.ccc_lru_max = lru_page_max;
	atomic_set(&sbi->ll_cache.ccc_lru_left, lru_page_max);
	spin_lock_init(&sbi->ll_cache.ccc_lru_lock);
	INIT_LIST_HEAD(&sbi->ll_cache.ccc_lru);

	sbi->ll_ra_info.ra_max_pages_per_file = min(pages / 32,
					   SBI_DEFAULT_READAHEAD_MAX);
	sbi->ll_ra_info.ra_max_pages = sbi->ll_ra_info.ra_max_pages_per_file;
	sbi->ll_ra_info.ra_max_read_ahead_whole_pages =
					   SBI_DEFAULT_READAHEAD_WHOLE_MAX;
	INIT_LIST_HEAD(&sbi->ll_conn_chain);
	INIT_LIST_HEAD(&sbi->ll_orphan_dentry_list);

	ll_generate_random_uuid(uuid);
	class_uuid_unparse(uuid, &sbi->ll_sb_uuid);
	CDEBUG(D_CONFIG, "generated uuid: %s\n", sbi->ll_sb_uuid.uuid);

	spin_lock(&ll_sb_lock);
	list_add_tail(&sbi->ll_list, &ll_super_blocks);
	spin_unlock(&ll_sb_lock);

	sbi->ll_flags |= LL_SBI_VERBOSE;
	sbi->ll_flags |= LL_SBI_CHECKSUM;

	sbi->ll_flags |= LL_SBI_LRU_RESIZE;

	for (i = 0; i <= LL_PROCESS_HIST_MAX; i++) {
		spin_lock_init(&sbi->ll_rw_extents_info.pp_extents[i].
			       pp_r_hist.oh_lock);
		spin_lock_init(&sbi->ll_rw_extents_info.pp_extents[i].
			       pp_w_hist.oh_lock);
	}

	/* metadata statahead is enabled by default */
	sbi->ll_sa_max = LL_SA_RPC_DEF;
	atomic_set(&sbi->ll_sa_total, 0);
	atomic_set(&sbi->ll_sa_wrong, 0);
	atomic_set(&sbi->ll_agl_total, 0);
	sbi->ll_flags |= LL_SBI_AGL_ENABLED;

	return sbi;
}

static void ll_free_sbi(struct super_block *sb)
{
	struct ll_sb_info *sbi = ll_s2sbi(sb);

	if (sbi != NULL) {
		spin_lock(&ll_sb_lock);
		list_del(&sbi->ll_list);
		spin_unlock(&ll_sb_lock);
		OBD_FREE(sbi, sizeof(*sbi));
	}
}

static int client_common_fill_super(struct super_block *sb, char *md, char *dt,
				    struct vfsmount *mnt)
{
	struct inode *root = NULL;
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	struct obd_device *obd;
	struct obd_capa *oc = NULL;
	struct obd_statfs *osfs = NULL;
	struct ptlrpc_request *request = NULL;
	struct obd_connect_data *data = NULL;
	struct obd_uuid *uuid;
	struct md_op_data *op_data;
	struct lustre_md lmd;
	u64 valid;
	int size, err, checksum;

	obd = class_name2obd(md);
	if (!obd) {
		CERROR("MD %s: not setup or attached\n", md);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		return -ENOMEM;

	osfs = kzalloc(sizeof(*osfs), GFP_NOFS);
	if (!osfs) {
		OBD_FREE_PTR(data);
		return -ENOMEM;
	}

	if (proc_lustre_fs_root) {
		err = lprocfs_register_mountpoint(proc_lustre_fs_root, sb,
						  dt, md);
		if (err < 0)
			CERROR("could not register mount in /proc/fs/lustre\n");
	}

	/* indicate the features supported by this client */
	data->ocd_connect_flags = OBD_CONNECT_IBITS    | OBD_CONNECT_NODEVOH  |
				  OBD_CONNECT_ATTRFID  |
				  OBD_CONNECT_VERSION  | OBD_CONNECT_BRW_SIZE |
				  OBD_CONNECT_MDS_CAPA | OBD_CONNECT_OSS_CAPA |
				  OBD_CONNECT_CANCELSET | OBD_CONNECT_FID     |
				  OBD_CONNECT_AT       | OBD_CONNECT_LOV_V3   |
				  OBD_CONNECT_RMT_CLIENT | OBD_CONNECT_VBR    |
				  OBD_CONNECT_FULL20   | OBD_CONNECT_64BITHASH|
				  OBD_CONNECT_EINPROGRESS |
				  OBD_CONNECT_JOBSTATS | OBD_CONNECT_LVB_TYPE |
				  OBD_CONNECT_LAYOUTLOCK |
				  OBD_CONNECT_PINGLESS |
				  OBD_CONNECT_MAX_EASIZE |
				  OBD_CONNECT_FLOCK_DEAD |
				  OBD_CONNECT_DISP_STRIPE;

	if (sbi->ll_flags & LL_SBI_SOM_PREVIEW)
		data->ocd_connect_flags |= OBD_CONNECT_SOM;

	if (sbi->ll_flags & LL_SBI_LRU_RESIZE)
		data->ocd_connect_flags |= OBD_CONNECT_LRU_RESIZE;
#ifdef CONFIG_FS_POSIX_ACL
	data->ocd_connect_flags |= OBD_CONNECT_ACL | OBD_CONNECT_UMASK;
#endif

	if (OBD_FAIL_CHECK(OBD_FAIL_MDC_LIGHTWEIGHT))
		/* flag mdc connection as lightweight, only used for test
		 * purpose, use with care */
		data->ocd_connect_flags |= OBD_CONNECT_LIGHTWEIGHT;

	data->ocd_ibits_known = MDS_INODELOCK_FULL;
	data->ocd_version = LUSTRE_VERSION_CODE;

	if (sb->s_flags & MS_RDONLY)
		data->ocd_connect_flags |= OBD_CONNECT_RDONLY;
	if (sbi->ll_flags & LL_SBI_USER_XATTR)
		data->ocd_connect_flags |= OBD_CONNECT_XATTR;

#ifdef HAVE_MS_FLOCK_LOCK
	/* force vfs to use lustre handler for flock() calls - bug 10743 */
	sb->s_flags |= MS_FLOCK_LOCK;
#endif
#ifdef MS_HAS_NEW_AOPS
	sb->s_flags |= MS_HAS_NEW_AOPS;
#endif

	if (sbi->ll_flags & LL_SBI_FLOCK)
		sbi->ll_fop = &ll_file_operations_flock;
	else if (sbi->ll_flags & LL_SBI_LOCALFLOCK)
		sbi->ll_fop = &ll_file_operations;
	else
		sbi->ll_fop = &ll_file_operations_noflock;

	/* real client */
	data->ocd_connect_flags |= OBD_CONNECT_REAL;
	if (sbi->ll_flags & LL_SBI_RMT_CLIENT)
		data->ocd_connect_flags |= OBD_CONNECT_RMT_CLIENT_FORCE;

	data->ocd_brw_size = MD_MAX_BRW_SIZE;

	err = obd_connect(NULL, &sbi->ll_md_exp, obd, &sbi->ll_sb_uuid, data, NULL);
	if (err == -EBUSY) {
		LCONSOLE_ERROR_MSG(0x14f, "An MDT (md %s) is performing "
				   "recovery, of which this client is not a "
				   "part. Please wait for recovery to complete,"
				   " abort, or time out.\n", md);
		goto out;
	} else if (err) {
		CERROR("cannot connect to %s: rc = %d\n", md, err);
		goto out;
	}

	sbi->ll_md_exp->exp_connect_data = *data;

	err = obd_fid_init(sbi->ll_md_exp->exp_obd, sbi->ll_md_exp,
			   LUSTRE_SEQ_METADATA);
	if (err) {
		CERROR("%s: Can't init metadata layer FID infrastructure, "
		       "rc = %d\n", sbi->ll_md_exp->exp_obd->obd_name, err);
		goto out_md;
	}

	/* For mount, we only need fs info from MDT0, and also in DNE, it
	 * can make sure the client can be mounted as long as MDT0 is
	 * available */
	err = obd_statfs(NULL, sbi->ll_md_exp, osfs,
			cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
			OBD_STATFS_FOR_MDT0);
	if (err)
		goto out_md_fid;

	/* This needs to be after statfs to ensure connect has finished.
	 * Note that "data" does NOT contain the valid connect reply.
	 * If connecting to a 1.8 server there will be no LMV device, so
	 * we can access the MDC export directly and exp_connect_flags will
	 * be non-zero, but if accessing an upgraded 2.1 server it will
	 * have the correct flags filled in.
	 * XXX: fill in the LMV exp_connect_flags from MDC(s). */
	valid = exp_connect_flags(sbi->ll_md_exp) & CLIENT_CONNECT_MDT_REQD;
	if (exp_connect_flags(sbi->ll_md_exp) != 0 &&
	    valid != CLIENT_CONNECT_MDT_REQD) {
		char *buf;

		buf = kzalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
		obd_connect_flags2str(buf, PAGE_CACHE_SIZE,
				      valid ^ CLIENT_CONNECT_MDT_REQD, ",");
		LCONSOLE_ERROR_MSG(0x170, "Server %s does not support "
				   "feature(s) needed for correct operation "
				   "of this client (%s). Please upgrade "
				   "server or downgrade client.\n",
				   sbi->ll_md_exp->exp_obd->obd_name, buf);
		OBD_FREE(buf, PAGE_CACHE_SIZE);
		err = -EPROTO;
		goto out_md_fid;
	}

	size = sizeof(*data);
	err = obd_get_info(NULL, sbi->ll_md_exp, sizeof(KEY_CONN_DATA),
			   KEY_CONN_DATA,  &size, data, NULL);
	if (err) {
		CERROR("%s: Get connect data failed: rc = %d\n",
		       sbi->ll_md_exp->exp_obd->obd_name, err);
		goto out_md_fid;
	}

	LASSERT(osfs->os_bsize);
	sb->s_blocksize = osfs->os_bsize;
	sb->s_blocksize_bits = log2(osfs->os_bsize);
	sb->s_magic = LL_SUPER_MAGIC;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sbi->ll_namelen = osfs->os_namelen;
	sbi->ll_max_rw_chunk = LL_DEFAULT_MAX_RW_CHUNK;

	if ((sbi->ll_flags & LL_SBI_USER_XATTR) &&
	    !(data->ocd_connect_flags & OBD_CONNECT_XATTR)) {
		LCONSOLE_INFO("Disabling user_xattr feature because "
			      "it is not supported on the server\n");
		sbi->ll_flags &= ~LL_SBI_USER_XATTR;
	}

	if (data->ocd_connect_flags & OBD_CONNECT_ACL) {
#ifdef MS_POSIXACL
		sb->s_flags |= MS_POSIXACL;
#endif
		sbi->ll_flags |= LL_SBI_ACL;
	} else {
		LCONSOLE_INFO("client wants to enable acl, but mdt not!\n");
#ifdef MS_POSIXACL
		sb->s_flags &= ~MS_POSIXACL;
#endif
		sbi->ll_flags &= ~LL_SBI_ACL;
	}

	if (data->ocd_connect_flags & OBD_CONNECT_RMT_CLIENT) {
		if (!(sbi->ll_flags & LL_SBI_RMT_CLIENT)) {
			sbi->ll_flags |= LL_SBI_RMT_CLIENT;
			LCONSOLE_INFO("client is set as remote by default.\n");
		}
	} else {
		if (sbi->ll_flags & LL_SBI_RMT_CLIENT) {
			sbi->ll_flags &= ~LL_SBI_RMT_CLIENT;
			LCONSOLE_INFO("client claims to be remote, but server "
				      "rejected, forced to be local.\n");
		}
	}

	if (data->ocd_connect_flags & OBD_CONNECT_MDS_CAPA) {
		LCONSOLE_INFO("client enabled MDS capability!\n");
		sbi->ll_flags |= LL_SBI_MDS_CAPA;
	}

	if (data->ocd_connect_flags & OBD_CONNECT_OSS_CAPA) {
		LCONSOLE_INFO("client enabled OSS capability!\n");
		sbi->ll_flags |= LL_SBI_OSS_CAPA;
	}

	if (data->ocd_connect_flags & OBD_CONNECT_64BITHASH)
		sbi->ll_flags |= LL_SBI_64BIT_HASH;

	if (data->ocd_connect_flags & OBD_CONNECT_BRW_SIZE)
		sbi->ll_md_brw_size = data->ocd_brw_size;
	else
		sbi->ll_md_brw_size = PAGE_CACHE_SIZE;

	if (data->ocd_connect_flags & OBD_CONNECT_LAYOUTLOCK) {
		LCONSOLE_INFO("Layout lock feature supported.\n");
		sbi->ll_flags |= LL_SBI_LAYOUT_LOCK;
	}

	if (data->ocd_ibits_known & MDS_INODELOCK_XATTR) {
		if (!(data->ocd_connect_flags & OBD_CONNECT_MAX_EASIZE)) {
			LCONSOLE_INFO(
				"%s: disabling xattr cache due to unknown maximum xattr size.\n",
				dt);
		} else {
			sbi->ll_flags |= LL_SBI_XATTR_CACHE;
			sbi->ll_xattr_cache_enabled = 1;
		}
	}

	obd = class_name2obd(dt);
	if (!obd) {
		CERROR("DT %s: not setup or attached\n", dt);
		err = -ENODEV;
		goto out_md_fid;
	}

	data->ocd_connect_flags = OBD_CONNECT_GRANT     | OBD_CONNECT_VERSION  |
				  OBD_CONNECT_REQPORTAL | OBD_CONNECT_BRW_SIZE |
				  OBD_CONNECT_CANCELSET | OBD_CONNECT_FID      |
				  OBD_CONNECT_SRVLOCK   | OBD_CONNECT_TRUNCLOCK|
				  OBD_CONNECT_AT | OBD_CONNECT_RMT_CLIENT |
				  OBD_CONNECT_OSS_CAPA | OBD_CONNECT_VBR|
				  OBD_CONNECT_FULL20 | OBD_CONNECT_64BITHASH |
				  OBD_CONNECT_MAXBYTES |
				  OBD_CONNECT_EINPROGRESS |
				  OBD_CONNECT_JOBSTATS | OBD_CONNECT_LVB_TYPE |
				  OBD_CONNECT_LAYOUTLOCK | OBD_CONNECT_PINGLESS;

	if (sbi->ll_flags & LL_SBI_SOM_PREVIEW)
		data->ocd_connect_flags |= OBD_CONNECT_SOM;

	if (!OBD_FAIL_CHECK(OBD_FAIL_OSC_CONNECT_CKSUM)) {
		/* OBD_CONNECT_CKSUM should always be set, even if checksums are
		 * disabled by default, because it can still be enabled on the
		 * fly via /proc. As a consequence, we still need to come to an
		 * agreement on the supported algorithms at connect time */
		data->ocd_connect_flags |= OBD_CONNECT_CKSUM;

		if (OBD_FAIL_CHECK(OBD_FAIL_OSC_CKSUM_ADLER_ONLY))
			data->ocd_cksum_types = OBD_CKSUM_ADLER;
		else
			data->ocd_cksum_types = cksum_types_supported_client();
	}

	data->ocd_connect_flags |= OBD_CONNECT_LRU_RESIZE;
	if (sbi->ll_flags & LL_SBI_RMT_CLIENT)
		data->ocd_connect_flags |= OBD_CONNECT_RMT_CLIENT_FORCE;

	CDEBUG(D_RPCTRACE, "ocd_connect_flags: %#llx ocd_version: %d "
	       "ocd_grant: %d\n", data->ocd_connect_flags,
	       data->ocd_version, data->ocd_grant);

	obd->obd_upcall.onu_owner = &sbi->ll_lco;
	obd->obd_upcall.onu_upcall = cl_ocd_update;

	data->ocd_brw_size = DT_MAX_BRW_SIZE;

	err = obd_connect(NULL, &sbi->ll_dt_exp, obd, &sbi->ll_sb_uuid, data,
			  NULL);
	if (err == -EBUSY) {
		LCONSOLE_ERROR_MSG(0x150, "An OST (dt %s) is performing "
				   "recovery, of which this client is not a "
				   "part.  Please wait for recovery to "
				   "complete, abort, or time out.\n", dt);
		goto out_md;
	} else if (err) {
		CERROR("%s: Cannot connect to %s: rc = %d\n",
		       sbi->ll_dt_exp->exp_obd->obd_name, dt, err);
		goto out_md;
	}

	sbi->ll_dt_exp->exp_connect_data = *data;

	err = obd_fid_init(sbi->ll_dt_exp->exp_obd, sbi->ll_dt_exp,
			   LUSTRE_SEQ_METADATA);
	if (err) {
		CERROR("%s: Can't init data layer FID infrastructure, "
		       "rc = %d\n", sbi->ll_dt_exp->exp_obd->obd_name, err);
		goto out_dt;
	}

	mutex_lock(&sbi->ll_lco.lco_lock);
	sbi->ll_lco.lco_flags = data->ocd_connect_flags;
	sbi->ll_lco.lco_md_exp = sbi->ll_md_exp;
	sbi->ll_lco.lco_dt_exp = sbi->ll_dt_exp;
	mutex_unlock(&sbi->ll_lco.lco_lock);

	fid_zero(&sbi->ll_root_fid);
	err = md_getstatus(sbi->ll_md_exp, &sbi->ll_root_fid, &oc);
	if (err) {
		CERROR("cannot mds_connect: rc = %d\n", err);
		goto out_lock_cn_cb;
	}
	if (!fid_is_sane(&sbi->ll_root_fid)) {
		CERROR("%s: Invalid root fid "DFID" during mount\n",
		       sbi->ll_md_exp->exp_obd->obd_name,
		       PFID(&sbi->ll_root_fid));
		err = -EINVAL;
		goto out_lock_cn_cb;
	}
	CDEBUG(D_SUPER, "rootfid "DFID"\n", PFID(&sbi->ll_root_fid));

	sb->s_op = &lustre_super_operations;
#if THREAD_SIZE >= 8192 /*b=17630*/
	sb->s_export_op = &lustre_export_operations;
#endif

	/* make root inode
	 * XXX: move this to after cbd setup? */
	valid = OBD_MD_FLGETATTR | OBD_MD_FLBLOCKS | OBD_MD_FLMDSCAPA;
	if (sbi->ll_flags & LL_SBI_RMT_CLIENT)
		valid |= OBD_MD_FLRMTPERM;
	else if (sbi->ll_flags & LL_SBI_ACL)
		valid |= OBD_MD_FLACL;

	op_data = kzalloc(sizeof(*op_data), GFP_NOFS);
	if (!op_data) {
		err = -ENOMEM;
		goto out_lock_cn_cb;
	}

	op_data->op_fid1 = sbi->ll_root_fid;
	op_data->op_mode = 0;
	op_data->op_capa1 = oc;
	op_data->op_valid = valid;

	err = md_getattr(sbi->ll_md_exp, op_data, &request);
	if (oc)
		capa_put(oc);
	OBD_FREE_PTR(op_data);
	if (err) {
		CERROR("%s: md_getattr failed for root: rc = %d\n",
		       sbi->ll_md_exp->exp_obd->obd_name, err);
		goto out_lock_cn_cb;
	}

	err = md_get_lustre_md(sbi->ll_md_exp, request, sbi->ll_dt_exp,
			       sbi->ll_md_exp, &lmd);
	if (err) {
		CERROR("failed to understand root inode md: rc = %d\n", err);
		ptlrpc_req_finished(request);
		goto out_lock_cn_cb;
	}

	LASSERT(fid_is_sane(&sbi->ll_root_fid));
	root = ll_iget(sb, cl_fid_build_ino(&sbi->ll_root_fid,
					    sbi->ll_flags & LL_SBI_32BIT_API),
		       &lmd);
	md_free_lustre_md(sbi->ll_md_exp, &lmd);
	ptlrpc_req_finished(request);

	if (root == NULL || IS_ERR(root)) {
		if (lmd.lsm)
			obd_free_memmd(sbi->ll_dt_exp, &lmd.lsm);
#ifdef CONFIG_FS_POSIX_ACL
		if (lmd.posix_acl) {
			posix_acl_release(lmd.posix_acl);
			lmd.posix_acl = NULL;
		}
#endif
		err = IS_ERR(root) ? PTR_ERR(root) : -EBADF;
		root = NULL;
		CERROR("lustre_lite: bad iget4 for root\n");
		goto out_root;
	}

	err = ll_close_thread_start(&sbi->ll_lcq);
	if (err) {
		CERROR("cannot start close thread: rc %d\n", err);
		goto out_root;
	}

#ifdef CONFIG_FS_POSIX_ACL
	if (sbi->ll_flags & LL_SBI_RMT_CLIENT) {
		rct_init(&sbi->ll_rct);
		et_init(&sbi->ll_et);
	}
#endif

	checksum = sbi->ll_flags & LL_SBI_CHECKSUM;
	err = obd_set_info_async(NULL, sbi->ll_dt_exp, sizeof(KEY_CHECKSUM),
				 KEY_CHECKSUM, sizeof(checksum), &checksum,
				 NULL);
	cl_sb_init(sb);

	err = obd_set_info_async(NULL, sbi->ll_dt_exp, sizeof(KEY_CACHE_SET),
				 KEY_CACHE_SET, sizeof(sbi->ll_cache),
				 &sbi->ll_cache, NULL);

	sb->s_root = d_make_root(root);
	if (sb->s_root == NULL) {
		CERROR("%s: can't make root dentry\n",
			ll_get_fsname(sb, NULL, 0));
		err = -ENOMEM;
		goto out_lock_cn_cb;
	}

	sbi->ll_sdev_orig = sb->s_dev;

	/* We set sb->s_dev equal on all lustre clients in order to support
	 * NFS export clustering.  NFSD requires that the FSID be the same
	 * on all clients. */
	/* s_dev is also used in lt_compare() to compare two fs, but that is
	 * only a node-local comparison. */
	uuid = obd_get_uuid(sbi->ll_md_exp);
	if (uuid != NULL) {
		sb->s_dev = get_uuid2int(uuid->uuid, strlen(uuid->uuid));
		get_uuid2fsid(uuid->uuid, strlen(uuid->uuid), &sbi->ll_fsid);
	}

	if (data != NULL)
		OBD_FREE_PTR(data);
	if (osfs != NULL)
		OBD_FREE_PTR(osfs);

	return err;
out_root:
	iput(root);
out_lock_cn_cb:
	obd_fid_fini(sbi->ll_dt_exp->exp_obd);
out_dt:
	obd_disconnect(sbi->ll_dt_exp);
	sbi->ll_dt_exp = NULL;
	/* Make sure all OScs are gone, since cl_cache is accessing sbi. */
	obd_zombie_barrier();
out_md_fid:
	obd_fid_fini(sbi->ll_md_exp->exp_obd);
out_md:
	obd_disconnect(sbi->ll_md_exp);
	sbi->ll_md_exp = NULL;
out:
	if (data != NULL)
		OBD_FREE_PTR(data);
	if (osfs != NULL)
		OBD_FREE_PTR(osfs);
	lprocfs_unregister_mountpoint(sbi);
	return err;
}

int ll_get_max_mdsize(struct ll_sb_info *sbi, int *lmmsize)
{
	int size, rc;

	*lmmsize = obd_size_diskmd(sbi->ll_dt_exp, NULL);
	size = sizeof(int);
	rc = obd_get_info(NULL, sbi->ll_md_exp, sizeof(KEY_MAX_EASIZE),
			  KEY_MAX_EASIZE, &size, lmmsize, NULL);
	if (rc)
		CERROR("Get max mdsize error rc %d \n", rc);

	return rc;
}

int ll_get_default_mdsize(struct ll_sb_info *sbi, int *lmmsize)
{
	int size, rc;

	size = sizeof(int);
	rc = obd_get_info(NULL, sbi->ll_md_exp, sizeof(KEY_DEFAULT_EASIZE),
			 KEY_DEFAULT_EASIZE, &size, lmmsize, NULL);
	if (rc)
		CERROR("Get default mdsize error rc %d\n", rc);

	return rc;
}

int ll_get_max_cookiesize(struct ll_sb_info *sbi, int *lmmsize)
{
	int size, rc;

	size = sizeof(int);
	rc = obd_get_info(NULL, sbi->ll_md_exp, sizeof(KEY_MAX_COOKIESIZE),
			  KEY_MAX_COOKIESIZE, &size, lmmsize, NULL);
	if (rc)
		CERROR("Get max cookiesize error rc %d\n", rc);

	return rc;
}

int ll_get_default_cookiesize(struct ll_sb_info *sbi, int *lmmsize)
{
	int size, rc;

	size = sizeof(int);
	rc = obd_get_info(NULL, sbi->ll_md_exp, sizeof(KEY_DEFAULT_COOKIESIZE),
			  KEY_DEFAULT_COOKIESIZE, &size, lmmsize, NULL);
	if (rc)
		CERROR("Get default cookiesize error rc %d\n", rc);

	return rc;
}

static void ll_dump_inode(struct inode *inode)
{
	struct ll_d_hlist_node *tmp;
	int dentry_count = 0;

	LASSERT(inode != NULL);

	ll_d_hlist_for_each(tmp, &inode->i_dentry)
		dentry_count++;

	CERROR("inode %p dump: dev=%s ino=%lu mode=%o count=%u, %d dentries\n",
	       inode, ll_i2mdexp(inode)->exp_obd->obd_name, inode->i_ino,
	       inode->i_mode, atomic_read(&inode->i_count), dentry_count);
}

void lustre_dump_dentry(struct dentry *dentry, int recur)
{
	struct list_head *tmp;
	int subdirs = 0;

	LASSERT(dentry != NULL);

	list_for_each(tmp, &dentry->d_subdirs)
		subdirs++;

	CERROR("dentry %p dump: name=%.*s parent=%.*s (%p), inode=%p, count=%u,"
	       " flags=0x%x, fsdata=%p, %d subdirs\n", dentry,
	       dentry->d_name.len, dentry->d_name.name,
	       dentry->d_parent->d_name.len, dentry->d_parent->d_name.name,
	       dentry->d_parent, dentry->d_inode, d_count(dentry),
	       dentry->d_flags, dentry->d_fsdata, subdirs);
	if (dentry->d_inode != NULL)
		ll_dump_inode(dentry->d_inode);

	if (recur == 0)
		return;

	list_for_each(tmp, &dentry->d_subdirs) {
		struct dentry *d = list_entry(tmp, struct dentry, d_child);
		lustre_dump_dentry(d, recur - 1);
	}
}

static void client_common_put_super(struct super_block *sb)
{
	struct ll_sb_info *sbi = ll_s2sbi(sb);

#ifdef CONFIG_FS_POSIX_ACL
	if (sbi->ll_flags & LL_SBI_RMT_CLIENT) {
		et_fini(&sbi->ll_et);
		rct_fini(&sbi->ll_rct);
	}
#endif

	ll_close_thread_shutdown(sbi->ll_lcq);

	cl_sb_fini(sb);

	list_del(&sbi->ll_conn_chain);

	obd_fid_fini(sbi->ll_dt_exp->exp_obd);
	obd_disconnect(sbi->ll_dt_exp);
	sbi->ll_dt_exp = NULL;
	/* wait till all OSCs are gone, since cl_cache is accessing sbi.
	 * see LU-2543. */
	obd_zombie_barrier();

	lprocfs_unregister_mountpoint(sbi);

	obd_fid_fini(sbi->ll_md_exp->exp_obd);
	obd_disconnect(sbi->ll_md_exp);
	sbi->ll_md_exp = NULL;
}

void ll_kill_super(struct super_block *sb)
{
	struct ll_sb_info *sbi;

	/* not init sb ?*/
	if (!(sb->s_flags & MS_ACTIVE))
		return;

	sbi = ll_s2sbi(sb);
	/* we need to restore s_dev from changed for clustered NFS before put_super
	 * because new kernels have cached s_dev and change sb->s_dev in
	 * put_super not affected real removing devices */
	if (sbi) {
		sb->s_dev = sbi->ll_sdev_orig;
		sbi->ll_umounting = 1;
	}
}

static inline int ll_set_opt(const char *opt, char *data, int fl)
{
	if (strncmp(opt, data, strlen(opt)) != 0)
		return 0;
	else
		return fl;
}

/* non-client-specific mount options are parsed in lmd_parse */
static int ll_options(char *options, int *flags)
{
	int tmp;
	char *s1 = options, *s2;

	if (!options)
		return 0;

	CDEBUG(D_CONFIG, "Parsing opts %s\n", options);

	while (*s1) {
		CDEBUG(D_SUPER, "next opt=%s\n", s1);
		tmp = ll_set_opt("nolock", s1, LL_SBI_NOLCK);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("flock", s1, LL_SBI_FLOCK);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("localflock", s1, LL_SBI_LOCALFLOCK);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("noflock", s1, LL_SBI_FLOCK|LL_SBI_LOCALFLOCK);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}
		tmp = ll_set_opt("user_xattr", s1, LL_SBI_USER_XATTR);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("nouser_xattr", s1, LL_SBI_USER_XATTR);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}
#if LUSTRE_VERSION_CODE < OBD_OCD_VERSION(2, 5, 50, 0)
		tmp = ll_set_opt("acl", s1, LL_SBI_ACL);
		if (tmp) {
			/* Ignore deprecated mount option.  The client will
			 * always try to mount with ACL support, whether this
			 * is used depends on whether server supports it. */
			LCONSOLE_ERROR_MSG(0x152, "Ignoring deprecated "
						  "mount option 'acl'.\n");
			goto next;
		}
		tmp = ll_set_opt("noacl", s1, LL_SBI_ACL);
		if (tmp) {
			LCONSOLE_ERROR_MSG(0x152, "Ignoring deprecated "
						  "mount option 'noacl'.\n");
			goto next;
		}
#else
#warning "{no}acl options have been deprecated since 1.8, please remove them"
#endif
		tmp = ll_set_opt("remote_client", s1, LL_SBI_RMT_CLIENT);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("user_fid2path", s1, LL_SBI_USER_FID2PATH);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("nouser_fid2path", s1, LL_SBI_USER_FID2PATH);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}

		tmp = ll_set_opt("checksum", s1, LL_SBI_CHECKSUM);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("nochecksum", s1, LL_SBI_CHECKSUM);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}
		tmp = ll_set_opt("lruresize", s1, LL_SBI_LRU_RESIZE);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("nolruresize", s1, LL_SBI_LRU_RESIZE);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}
		tmp = ll_set_opt("lazystatfs", s1, LL_SBI_LAZYSTATFS);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("nolazystatfs", s1, LL_SBI_LAZYSTATFS);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}
		tmp = ll_set_opt("som_preview", s1, LL_SBI_SOM_PREVIEW);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("32bitapi", s1, LL_SBI_32BIT_API);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("verbose", s1, LL_SBI_VERBOSE);
		if (tmp) {
			*flags |= tmp;
			goto next;
		}
		tmp = ll_set_opt("noverbose", s1, LL_SBI_VERBOSE);
		if (tmp) {
			*flags &= ~tmp;
			goto next;
		}
		LCONSOLE_ERROR_MSG(0x152, "Unknown option '%s', won't mount.\n",
				   s1);
		return -EINVAL;

next:
		/* Find next opt */
		s2 = strchr(s1, ',');
		if (s2 == NULL)
			break;
		s1 = s2 + 1;
	}
	return 0;
}

void ll_lli_init(struct ll_inode_info *lli)
{
	lli->lli_inode_magic = LLI_INODE_MAGIC;
	lli->lli_flags = 0;
	lli->lli_ioepoch = 0;
	lli->lli_maxbytes = MAX_LFS_FILESIZE;
	spin_lock_init(&lli->lli_lock);
	lli->lli_posix_acl = NULL;
	lli->lli_remote_perms = NULL;
	mutex_init(&lli->lli_rmtperm_mutex);
	/* Do not set lli_fid, it has been initialized already. */
	fid_zero(&lli->lli_pfid);
	INIT_LIST_HEAD(&lli->lli_close_list);
	INIT_LIST_HEAD(&lli->lli_oss_capas);
	atomic_set(&lli->lli_open_count, 0);
	lli->lli_mds_capa = NULL;
	lli->lli_rmtperm_time = 0;
	lli->lli_pending_och = NULL;
	lli->lli_mds_read_och = NULL;
	lli->lli_mds_write_och = NULL;
	lli->lli_mds_exec_och = NULL;
	lli->lli_open_fd_read_count = 0;
	lli->lli_open_fd_write_count = 0;
	lli->lli_open_fd_exec_count = 0;
	mutex_init(&lli->lli_och_mutex);
	spin_lock_init(&lli->lli_agl_lock);
	lli->lli_has_smd = false;
	spin_lock_init(&lli->lli_layout_lock);
	ll_layout_version_set(lli, LL_LAYOUT_GEN_NONE);
	lli->lli_clob = NULL;

	init_rwsem(&lli->lli_xattrs_list_rwsem);
	mutex_init(&lli->lli_xattrs_enq_lock);

	LASSERT(lli->lli_vfs_inode.i_mode != 0);
	if (S_ISDIR(lli->lli_vfs_inode.i_mode)) {
		mutex_init(&lli->lli_readdir_mutex);
		lli->lli_opendir_key = NULL;
		lli->lli_sai = NULL;
		spin_lock_init(&lli->lli_sa_lock);
		lli->lli_opendir_pid = 0;
	} else {
		mutex_init(&lli->lli_size_mutex);
		lli->lli_symlink_name = NULL;
		init_rwsem(&lli->lli_trunc_sem);
		mutex_init(&lli->lli_write_mutex);
		init_rwsem(&lli->lli_glimpse_sem);
		lli->lli_glimpse_time = 0;
		INIT_LIST_HEAD(&lli->lli_agl_list);
		lli->lli_agl_index = 0;
		lli->lli_async_rc = 0;
	}
	mutex_init(&lli->lli_layout_mutex);
}

static inline int ll_bdi_register(struct backing_dev_info *bdi)
{
	static atomic_t ll_bdi_num = ATOMIC_INIT(0);

	bdi->name = "lustre";
	return bdi_register(bdi, NULL, "lustre-%d",
			    atomic_inc_return(&ll_bdi_num));
}

int ll_fill_super(struct super_block *sb, struct vfsmount *mnt)
{
	struct lustre_profile *lprof = NULL;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct ll_sb_info *sbi;
	char  *dt = NULL, *md = NULL;
	char  *profilenm = get_profile_name(sb);
	struct config_llog_instance *cfg;
	/* %p for void* in printf needs 16+2 characters: 0xffffffffffffffff */
	const int instlen = sizeof(cfg->cfg_instance) * 2 + 2;
	int    err;

	CDEBUG(D_VFSTRACE, "VFS Op: sb %p\n", sb);

	cfg = kzalloc(sizeof(*cfg), GFP_NOFS);
	if (!cfg)
		return -ENOMEM;

	try_module_get(THIS_MODULE);

	/* client additional sb info */
	lsi->lsi_llsbi = sbi = ll_init_sbi();
	if (!sbi) {
		module_put(THIS_MODULE);
		OBD_FREE_PTR(cfg);
		return -ENOMEM;
	}

	err = ll_options(lsi->lsi_lmd->lmd_opts, &sbi->ll_flags);
	if (err)
		goto out_free;

	err = bdi_init(&lsi->lsi_bdi);
	if (err)
		goto out_free;
	lsi->lsi_flags |= LSI_BDI_INITIALIZED;
	lsi->lsi_bdi.capabilities = BDI_CAP_MAP_COPY;
	err = ll_bdi_register(&lsi->lsi_bdi);
	if (err)
		goto out_free;

	sb->s_bdi = &lsi->lsi_bdi;
	/* kernel >= 2.6.38 store dentry operations in sb->s_d_op. */
	sb->s_d_op = &ll_d_ops;

	/* Generate a string unique to this super, in case some joker tries
	   to mount the same fs at two mount points.
	   Use the address of the super itself.*/
	cfg->cfg_instance = sb;
	cfg->cfg_uuid = lsi->lsi_llsbi->ll_sb_uuid;
	cfg->cfg_callback = class_config_llog_handler;
	/* set up client obds */
	err = lustre_process_log(sb, profilenm, cfg);
	if (err < 0) {
		CERROR("Unable to process log: %d\n", err);
		goto out_free;
	}

	/* Profile set with LCFG_MOUNTOPT so we can find our mdc and osc obds */
	lprof = class_get_profile(profilenm);
	if (lprof == NULL) {
		LCONSOLE_ERROR_MSG(0x156, "The client profile '%s' could not be"
				   " read from the MGS.  Does that filesystem "
				   "exist?\n", profilenm);
		err = -EINVAL;
		goto out_free;
	}
	CDEBUG(D_CONFIG, "Found profile %s: mdc=%s osc=%s\n", profilenm,
	       lprof->lp_md, lprof->lp_dt);

	dt = kzalloc(strlen(lprof->lp_dt) + instlen + 2, GFP_NOFS);
	if (!dt) {
		err = -ENOMEM;
		goto out_free;
	}
	sprintf(dt, "%s-%p", lprof->lp_dt, cfg->cfg_instance);

	md = kzalloc(strlen(lprof->lp_md) + instlen + 2, GFP_NOFS);
	if (!md) {
		err = -ENOMEM;
		goto out_free;
	}
	sprintf(md, "%s-%p", lprof->lp_md, cfg->cfg_instance);

	/* connections, registrations, sb setup */
	err = client_common_fill_super(sb, md, dt, mnt);

out_free:
	if (md)
		OBD_FREE(md, strlen(lprof->lp_md) + instlen + 2);
	if (dt)
		OBD_FREE(dt, strlen(lprof->lp_dt) + instlen + 2);
	if (err)
		ll_put_super(sb);
	else if (sbi->ll_flags & LL_SBI_VERBOSE)
		LCONSOLE_WARN("Mounted %s\n", profilenm);

	OBD_FREE_PTR(cfg);
	return err;
} /* ll_fill_super */

void ll_put_super(struct super_block *sb)
{
	struct config_llog_instance cfg, params_cfg;
	struct obd_device *obd;
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	char *profilenm = get_profile_name(sb);
	int next, force = 1;

	CDEBUG(D_VFSTRACE, "VFS Op: sb %p - %s\n", sb, profilenm);

	ll_print_capa_stat(sbi);

	cfg.cfg_instance = sb;
	lustre_end_log(sb, profilenm, &cfg);

	params_cfg.cfg_instance = sb;
	lustre_end_log(sb, PARAMS_FILENAME, &params_cfg);

	if (sbi->ll_md_exp) {
		obd = class_exp2obd(sbi->ll_md_exp);
		if (obd)
			force = obd->obd_force;
	}

	/* We need to set force before the lov_disconnect in
	   lustre_common_put_super, since l_d cleans up osc's as well. */
	if (force) {
		next = 0;
		while ((obd = class_devices_in_group(&sbi->ll_sb_uuid,
						     &next)) != NULL) {
			obd->obd_force = force;
		}
	}

	if (sbi->ll_lcq) {
		/* Only if client_common_fill_super succeeded */
		client_common_put_super(sb);
	}

	next = 0;
	while ((obd = class_devices_in_group(&sbi->ll_sb_uuid, &next)) !=NULL) {
		class_manual_cleanup(obd);
	}

	if (sbi->ll_flags & LL_SBI_VERBOSE)
		LCONSOLE_WARN("Unmounted %s\n", profilenm ? profilenm : "");

	if (profilenm)
		class_del_profile(profilenm);

	if (lsi->lsi_flags & LSI_BDI_INITIALIZED) {
		bdi_destroy(&lsi->lsi_bdi);
		lsi->lsi_flags &= ~LSI_BDI_INITIALIZED;
	}

	ll_free_sbi(sb);
	lsi->lsi_llsbi = NULL;

	lustre_common_put_super(sb);

	module_put(THIS_MODULE);
} /* client_put_super */

struct inode *ll_inode_from_resource_lock(struct ldlm_lock *lock)
{
	struct inode *inode = NULL;

	/* NOTE: we depend on atomic igrab() -bzzz */
	lock_res_and_lock(lock);
	if (lock->l_resource->lr_lvb_inode) {
		struct ll_inode_info *lli;
		lli = ll_i2info(lock->l_resource->lr_lvb_inode);
		if (lli->lli_inode_magic == LLI_INODE_MAGIC) {
			inode = igrab(lock->l_resource->lr_lvb_inode);
		} else {
			inode = lock->l_resource->lr_lvb_inode;
			LDLM_DEBUG_LIMIT(inode->i_state & I_FREEING ?  D_INFO :
					 D_WARNING, lock, "lr_lvb_inode %p is "
					 "bogus: magic %08x",
					 lock->l_resource->lr_lvb_inode,
					 lli->lli_inode_magic);
			inode = NULL;
		}
	}
	unlock_res_and_lock(lock);
	return inode;
}

void ll_clear_inode(struct inode *inode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info *sbi = ll_i2sbi(inode);

	CDEBUG(D_VFSTRACE, "VFS Op:inode=%lu/%u(%p)\n", inode->i_ino,
	       inode->i_generation, inode);

	if (S_ISDIR(inode->i_mode)) {
		/* these should have been cleared in ll_file_release */
		LASSERT(lli->lli_opendir_key == NULL);
		LASSERT(lli->lli_sai == NULL);
		LASSERT(lli->lli_opendir_pid == 0);
	}

	spin_lock(&lli->lli_lock);
	ll_i2info(inode)->lli_flags &= ~LLIF_MDS_SIZE_LOCK;
	spin_unlock(&lli->lli_lock);
	md_null_inode(sbi->ll_md_exp, ll_inode2fid(inode));

	LASSERT(!lli->lli_open_fd_write_count);
	LASSERT(!lli->lli_open_fd_read_count);
	LASSERT(!lli->lli_open_fd_exec_count);

	if (lli->lli_mds_write_och)
		ll_md_real_close(inode, FMODE_WRITE);
	if (lli->lli_mds_exec_och)
		ll_md_real_close(inode, FMODE_EXEC);
	if (lli->lli_mds_read_och)
		ll_md_real_close(inode, FMODE_READ);

	if (S_ISLNK(inode->i_mode) && lli->lli_symlink_name) {
		OBD_FREE(lli->lli_symlink_name,
			 strlen(lli->lli_symlink_name) + 1);
		lli->lli_symlink_name = NULL;
	}

	ll_xattr_cache_destroy(inode);

	if (sbi->ll_flags & LL_SBI_RMT_CLIENT) {
		LASSERT(lli->lli_posix_acl == NULL);
		if (lli->lli_remote_perms) {
			free_rmtperm_hash(lli->lli_remote_perms);
			lli->lli_remote_perms = NULL;
		}
	}
#ifdef CONFIG_FS_POSIX_ACL
	else if (lli->lli_posix_acl) {
		LASSERT(atomic_read(&lli->lli_posix_acl->a_refcount) == 1);
		LASSERT(lli->lli_remote_perms == NULL);
		posix_acl_release(lli->lli_posix_acl);
		lli->lli_posix_acl = NULL;
	}
#endif
	lli->lli_inode_magic = LLI_INODE_DEAD;

	ll_clear_inode_capas(inode);
	if (!S_ISDIR(inode->i_mode))
		LASSERT(list_empty(&lli->lli_agl_list));

	/*
	 * XXX This has to be done before lsm is freed below, because
	 * cl_object still uses inode lsm.
	 */
	cl_inode_fini(inode);
	lli->lli_has_smd = false;
}

static int ll_md_setattr(struct dentry *dentry, struct md_op_data *op_data,
		  struct md_open_data **mod)
{
	struct lustre_md md;
	struct inode *inode = dentry->d_inode;
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *request = NULL;
	int rc, ia_valid;

	op_data = ll_prep_md_op_data(op_data, inode, NULL, NULL, 0, 0,
				     LUSTRE_OPC_ANY, NULL);
	if (IS_ERR(op_data))
		return PTR_ERR(op_data);

	rc = md_setattr(sbi->ll_md_exp, op_data, NULL, 0, NULL, 0,
			&request, mod);
	if (rc) {
		ptlrpc_req_finished(request);
		if (rc == -ENOENT) {
			clear_nlink(inode);
			/* Unlinked special device node? Or just a race?
			 * Pretend we done everything. */
			if (!S_ISREG(inode->i_mode) &&
			    !S_ISDIR(inode->i_mode)) {
				ia_valid = op_data->op_attr.ia_valid;
				op_data->op_attr.ia_valid &= ~TIMES_SET_FLAGS;
				rc = simple_setattr(dentry, &op_data->op_attr);
				op_data->op_attr.ia_valid = ia_valid;
			}
		} else if (rc != -EPERM && rc != -EACCES && rc != -ETXTBSY) {
			CERROR("md_setattr fails: rc = %d\n", rc);
		}
		return rc;
	}

	rc = md_get_lustre_md(sbi->ll_md_exp, request, sbi->ll_dt_exp,
			      sbi->ll_md_exp, &md);
	if (rc) {
		ptlrpc_req_finished(request);
		return rc;
	}

	ia_valid = op_data->op_attr.ia_valid;
	/* inode size will be in ll_setattr_ost, can't do it now since dirty
	 * cache is not cleared yet. */
	op_data->op_attr.ia_valid &= ~(TIMES_SET_FLAGS | ATTR_SIZE);
	rc = simple_setattr(dentry, &op_data->op_attr);
	op_data->op_attr.ia_valid = ia_valid;

	/* Extract epoch data if obtained. */
	op_data->op_handle = md.body->handle;
	op_data->op_ioepoch = md.body->ioepoch;

	ll_update_inode(inode, &md);
	ptlrpc_req_finished(request);

	return rc;
}

/* Close IO epoch and send Size-on-MDS attribute update. */
static int ll_setattr_done_writing(struct inode *inode,
				   struct md_op_data *op_data,
				   struct md_open_data *mod)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	int rc = 0;

	LASSERT(op_data != NULL);
	if (!S_ISREG(inode->i_mode))
		return 0;

	CDEBUG(D_INODE, "Epoch %llu closed on "DFID" for truncate\n",
	       op_data->op_ioepoch, PFID(&lli->lli_fid));

	op_data->op_flags = MF_EPOCH_CLOSE;
	ll_done_writing_attr(inode, op_data);
	ll_pack_inode2opdata(inode, op_data, NULL);

	rc = md_done_writing(ll_i2sbi(inode)->ll_md_exp, op_data, mod);
	if (rc == -EAGAIN) {
		/* MDS has instructed us to obtain Size-on-MDS attribute
		 * from OSTs and send setattr to back to MDS. */
		rc = ll_som_update(inode, op_data);
	} else if (rc) {
		CERROR("inode %lu mdc truncate failed: rc = %d\n",
		       inode->i_ino, rc);
	}
	return rc;
}

static int ll_setattr_ost(struct inode *inode, struct iattr *attr)
{
	struct obd_capa *capa;
	int rc;

	if (attr->ia_valid & ATTR_SIZE)
		capa = ll_osscapa_get(inode, CAPA_OPC_OSS_TRUNC);
	else
		capa = ll_mdscapa_get(inode);

	rc = cl_setattr_ost(inode, attr, capa);

	if (attr->ia_valid & ATTR_SIZE)
		ll_truncate_free_capa(capa);
	else
		capa_put(capa);

	return rc;
}


/* If this inode has objects allocated to it (lsm != NULL), then the OST
 * object(s) determine the file size and mtime.  Otherwise, the MDS will
 * keep these values until such a time that objects are allocated for it.
 * We do the MDS operations first, as it is checking permissions for us.
 * We don't to the MDS RPC if there is nothing that we want to store there,
 * otherwise there is no harm in updating mtime/atime on the MDS if we are
 * going to do an RPC anyways.
 *
 * If we are doing a truncate, we will send the mtime and ctime updates
 * to the OST with the punch RPC, otherwise we do an explicit setattr RPC.
 * I don't believe it is possible to get e.g. ATTR_MTIME_SET and ATTR_SIZE
 * at the same time.
 *
 * In case of HSMimport, we only set attr on MDS.
 */
int ll_setattr_raw(struct dentry *dentry, struct iattr *attr, bool hsm_import)
{
	struct inode *inode = dentry->d_inode;
	struct ll_inode_info *lli = ll_i2info(inode);
	struct md_op_data *op_data = NULL;
	struct md_open_data *mod = NULL;
	bool file_is_released = false;
	int rc = 0, rc1 = 0;

	CDEBUG(D_VFSTRACE,
		"%s: setattr inode %p/fid:"DFID
		" from %llu to %llu, valid %x, hsm_import %d\n",
		ll_get_fsname(inode->i_sb, NULL, 0), inode,
		PFID(&lli->lli_fid), i_size_read(inode), attr->ia_size,
		attr->ia_valid, hsm_import);

	if (attr->ia_valid & ATTR_SIZE) {
		/* Check new size against VFS/VM file size limit and rlimit */
		rc = inode_newsize_ok(inode, attr->ia_size);
		if (rc)
			return rc;

		/* The maximum Lustre file size is variable, based on the
		 * OST maximum object size and number of stripes.  This
		 * needs another check in addition to the VFS check above. */
		if (attr->ia_size > ll_file_maxbytes(inode)) {
			CDEBUG(D_INODE, "file "DFID" too large %llu > %llu\n",
			       PFID(&lli->lli_fid), attr->ia_size,
			       ll_file_maxbytes(inode));
			return -EFBIG;
		}

		attr->ia_valid |= ATTR_MTIME | ATTR_CTIME;
	}

	/* POSIX: check before ATTR_*TIME_SET set (from inode_change_ok) */
	if (attr->ia_valid & TIMES_SET_FLAGS) {
		if ((!uid_eq(current_fsuid(), inode->i_uid)) &&
		    !capable(CFS_CAP_FOWNER))
			return -EPERM;
	}

	/* We mark all of the fields "set" so MDS/OST does not re-set them */
	if (attr->ia_valid & ATTR_CTIME) {
		attr->ia_ctime = CURRENT_TIME;
		attr->ia_valid |= ATTR_CTIME_SET;
	}
	if (!(attr->ia_valid & ATTR_ATIME_SET) &&
	    (attr->ia_valid & ATTR_ATIME)) {
		attr->ia_atime = CURRENT_TIME;
		attr->ia_valid |= ATTR_ATIME_SET;
	}
	if (!(attr->ia_valid & ATTR_MTIME_SET) &&
	    (attr->ia_valid & ATTR_MTIME)) {
		attr->ia_mtime = CURRENT_TIME;
		attr->ia_valid |= ATTR_MTIME_SET;
	}

	if (attr->ia_valid & (ATTR_MTIME | ATTR_CTIME))
		CDEBUG(D_INODE, "setting mtime %lu, ctime %lu, now = %lu\n",
		       LTIME_S(attr->ia_mtime), LTIME_S(attr->ia_ctime),
		       get_seconds());

	/* If we are changing file size, file content is modified, flag it. */
	if (attr->ia_valid & ATTR_SIZE) {
		attr->ia_valid |= MDS_OPEN_OWNEROVERRIDE;
		spin_lock(&lli->lli_lock);
		lli->lli_flags |= LLIF_DATA_MODIFIED;
		spin_unlock(&lli->lli_lock);
	}

	/* We always do an MDS RPC, even if we're only changing the size;
	 * only the MDS knows whether truncate() should fail with -ETXTBUSY */

	op_data = kzalloc(sizeof(*op_data), GFP_NOFS);
	if (!op_data)
		return -ENOMEM;

	if (!S_ISDIR(inode->i_mode)) {
		if (attr->ia_valid & ATTR_SIZE)
			inode_dio_write_done(inode);
		mutex_unlock(&inode->i_mutex);
	}

	memcpy(&op_data->op_attr, attr, sizeof(*attr));

	/* Open epoch for truncate. */
	if (exp_connect_som(ll_i2mdexp(inode)) &&
	    (attr->ia_valid & (ATTR_SIZE | ATTR_MTIME | ATTR_MTIME_SET)))
		op_data->op_flags = MF_EPOCH_OPEN;

	/* truncate on a released file must failed with -ENODATA,
	 * so size must not be set on MDS for released file
	 * but other attributes must be set
	 */
	if (S_ISREG(inode->i_mode)) {
		struct lov_stripe_md *lsm;
		__u32 gen;

		ll_layout_refresh(inode, &gen);
		lsm = ccc_inode_lsm_get(inode);
		if (lsm && lsm->lsm_pattern & LOV_PATTERN_F_RELEASED)
			file_is_released = true;
		ccc_inode_lsm_put(inode, lsm);
	}

	/* if not in HSM import mode, clear size attr for released file
	 * we clear the attribute send to MDT in op_data, not the original
	 * received from caller in attr which is used later to
	 * decide return code */
	if (file_is_released && (attr->ia_valid & ATTR_SIZE) && !hsm_import)
		op_data->op_attr.ia_valid &= ~ATTR_SIZE;

	rc = ll_md_setattr(dentry, op_data, &mod);
	if (rc)
		goto out;

	/* truncate failed (only when non HSM import), others succeed */
	if (file_is_released) {
		if ((attr->ia_valid & ATTR_SIZE) && !hsm_import)
			rc = -ENODATA;
		else
			rc = 0;
		goto out;
	}

	/* RPC to MDT is sent, cancel data modification flag */
	if (rc == 0 && (op_data->op_bias & MDS_DATA_MODIFIED)) {
		spin_lock(&lli->lli_lock);
		lli->lli_flags &= ~LLIF_DATA_MODIFIED;
		spin_unlock(&lli->lli_lock);
	}

	ll_ioepoch_open(lli, op_data->op_ioepoch);
	if (!S_ISREG(inode->i_mode)) {
		rc = 0;
		goto out;
	}

	if (attr->ia_valid & (ATTR_SIZE |
			      ATTR_ATIME | ATTR_ATIME_SET |
			      ATTR_MTIME | ATTR_MTIME_SET))
		/* For truncate and utimes sending attributes to OSTs, setting
		 * mtime/atime to the past will be performed under PW [0:EOF]
		 * extent lock (new_size:EOF for truncate).  It may seem
		 * excessive to send mtime/atime updates to OSTs when not
		 * setting times to past, but it is necessary due to possible
		 * time de-synchronization between MDT inode and OST objects */
		if (attr->ia_valid & ATTR_SIZE)
			down_write(&lli->lli_trunc_sem);
		rc = ll_setattr_ost(inode, attr);
		if (attr->ia_valid & ATTR_SIZE)
			up_write(&lli->lli_trunc_sem);
out:
	if (op_data) {
		if (op_data->op_ioepoch) {
			rc1 = ll_setattr_done_writing(inode, op_data, mod);
			if (!rc)
				rc = rc1;
		}
		ll_finish_md_op_data(op_data);
	}
	if (!S_ISDIR(inode->i_mode)) {
		mutex_lock(&inode->i_mutex);
		if ((attr->ia_valid & ATTR_SIZE) && !hsm_import)
			inode_dio_wait(inode);
	}

	ll_stats_ops_tally(ll_i2sbi(inode), (attr->ia_valid & ATTR_SIZE) ?
			LPROC_LL_TRUNC : LPROC_LL_SETATTR, 1);

	return rc;
}

int ll_setattr(struct dentry *de, struct iattr *attr)
{
	int mode = de->d_inode->i_mode;

	if ((attr->ia_valid & (ATTR_CTIME|ATTR_SIZE|ATTR_MODE)) ==
			      (ATTR_CTIME|ATTR_SIZE|ATTR_MODE))
		attr->ia_valid |= MDS_OPEN_OWNEROVERRIDE;

	if (((attr->ia_valid & (ATTR_MODE|ATTR_FORCE|ATTR_SIZE)) ==
			       (ATTR_SIZE|ATTR_MODE)) &&
	    (((mode & S_ISUID) && !(attr->ia_mode & S_ISUID)) ||
	     (((mode & (S_ISGID|S_IXGRP)) == (S_ISGID|S_IXGRP)) &&
	      !(attr->ia_mode & S_ISGID))))
		attr->ia_valid |= ATTR_FORCE;

	if ((attr->ia_valid & ATTR_MODE) &&
	    (mode & S_ISUID) &&
	    !(attr->ia_mode & S_ISUID) &&
	    !(attr->ia_valid & ATTR_KILL_SUID))
		attr->ia_valid |= ATTR_KILL_SUID;

	if ((attr->ia_valid & ATTR_MODE) &&
	    ((mode & (S_ISGID|S_IXGRP)) == (S_ISGID|S_IXGRP)) &&
	    !(attr->ia_mode & S_ISGID) &&
	    !(attr->ia_valid & ATTR_KILL_SGID))
		attr->ia_valid |= ATTR_KILL_SGID;

	return ll_setattr_raw(de, attr, false);
}

int ll_statfs_internal(struct super_block *sb, struct obd_statfs *osfs,
		       __u64 max_age, __u32 flags)
{
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	struct obd_statfs obd_osfs;
	int rc;

	rc = obd_statfs(NULL, sbi->ll_md_exp, osfs, max_age, flags);
	if (rc) {
		CERROR("md_statfs fails: rc = %d\n", rc);
		return rc;
	}

	osfs->os_type = sb->s_magic;

	CDEBUG(D_SUPER, "MDC blocks %llu/%llu objects %llu/%llu\n",
	       osfs->os_bavail, osfs->os_blocks, osfs->os_ffree,
	       osfs->os_files);

	if (sbi->ll_flags & LL_SBI_LAZYSTATFS)
		flags |= OBD_STATFS_NODELAY;

	rc = obd_statfs_rqset(sbi->ll_dt_exp, &obd_osfs, max_age, flags);
	if (rc) {
		CERROR("obd_statfs fails: rc = %d\n", rc);
		return rc;
	}

	CDEBUG(D_SUPER, "OSC blocks %llu/%llu objects %llu/%llu\n",
	       obd_osfs.os_bavail, obd_osfs.os_blocks, obd_osfs.os_ffree,
	       obd_osfs.os_files);

	osfs->os_bsize = obd_osfs.os_bsize;
	osfs->os_blocks = obd_osfs.os_blocks;
	osfs->os_bfree = obd_osfs.os_bfree;
	osfs->os_bavail = obd_osfs.os_bavail;

	/* If we don't have as many objects free on the OST as inodes
	 * on the MDS, we reduce the total number of inodes to
	 * compensate, so that the "inodes in use" number is correct.
	 */
	if (obd_osfs.os_ffree < osfs->os_ffree) {
		osfs->os_files = (osfs->os_files - osfs->os_ffree) +
			obd_osfs.os_ffree;
		osfs->os_ffree = obd_osfs.os_ffree;
	}

	return rc;
}
int ll_statfs(struct dentry *de, struct kstatfs *sfs)
{
	struct super_block *sb = de->d_sb;
	struct obd_statfs osfs;
	int rc;

	CDEBUG(D_VFSTRACE, "VFS Op: at %llu jiffies\n", get_jiffies_64());
	ll_stats_ops_tally(ll_s2sbi(sb), LPROC_LL_STAFS, 1);

	/* Some amount of caching on the client is allowed */
	rc = ll_statfs_internal(sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				0);
	if (rc)
		return rc;

	statfs_unpack(sfs, &osfs);

	/* We need to downshift for all 32-bit kernels, because we can't
	 * tell if the kernel is being called via sys_statfs64() or not.
	 * Stop before overflowing f_bsize - in which case it is better
	 * to just risk EOVERFLOW if caller is using old sys_statfs(). */
	if (sizeof(long) < 8) {
		while (osfs.os_blocks > ~0UL && sfs->f_bsize < 0x40000000) {
			sfs->f_bsize <<= 1;

			osfs.os_blocks >>= 1;
			osfs.os_bfree >>= 1;
			osfs.os_bavail >>= 1;
		}
	}

	sfs->f_blocks = osfs.os_blocks;
	sfs->f_bfree = osfs.os_bfree;
	sfs->f_bavail = osfs.os_bavail;
	sfs->f_fsid = ll_s2sbi(sb)->ll_fsid;
	return 0;
}

void ll_inode_size_lock(struct inode *inode)
{
	struct ll_inode_info *lli;

	LASSERT(!S_ISDIR(inode->i_mode));

	lli = ll_i2info(inode);
	mutex_lock(&lli->lli_size_mutex);
}

void ll_inode_size_unlock(struct inode *inode)
{
	struct ll_inode_info *lli;

	lli = ll_i2info(inode);
	mutex_unlock(&lli->lli_size_mutex);
}

void ll_update_inode(struct inode *inode, struct lustre_md *md)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct mdt_body *body = md->body;
	struct lov_stripe_md *lsm = md->lsm;
	struct ll_sb_info *sbi = ll_i2sbi(inode);

	LASSERT ((lsm != NULL) == ((body->valid & OBD_MD_FLEASIZE) != 0));
	if (lsm != NULL) {
		if (!lli->lli_has_smd &&
		    !(sbi->ll_flags & LL_SBI_LAYOUT_LOCK))
			cl_file_inode_init(inode, md);

		lli->lli_maxbytes = lsm->lsm_maxbytes;
		if (lli->lli_maxbytes > MAX_LFS_FILESIZE)
			lli->lli_maxbytes = MAX_LFS_FILESIZE;
	}

	if (sbi->ll_flags & LL_SBI_RMT_CLIENT) {
		if (body->valid & OBD_MD_FLRMTPERM)
			ll_update_remote_perm(inode, md->remote_perm);
	}
#ifdef CONFIG_FS_POSIX_ACL
	else if (body->valid & OBD_MD_FLACL) {
		spin_lock(&lli->lli_lock);
		if (lli->lli_posix_acl)
			posix_acl_release(lli->lli_posix_acl);
		lli->lli_posix_acl = md->posix_acl;
		spin_unlock(&lli->lli_lock);
	}
#endif
	inode->i_ino = cl_fid_build_ino(&body->fid1,
					sbi->ll_flags & LL_SBI_32BIT_API);
	inode->i_generation = cl_fid_build_gen(&body->fid1);

	if (body->valid & OBD_MD_FLATIME) {
		if (body->atime > LTIME_S(inode->i_atime))
			LTIME_S(inode->i_atime) = body->atime;
		lli->lli_lvb.lvb_atime = body->atime;
	}
	if (body->valid & OBD_MD_FLMTIME) {
		if (body->mtime > LTIME_S(inode->i_mtime)) {
			CDEBUG(D_INODE, "setting ino %lu mtime from %lu to %llu\n",
			       inode->i_ino, LTIME_S(inode->i_mtime),
			       body->mtime);
			LTIME_S(inode->i_mtime) = body->mtime;
		}
		lli->lli_lvb.lvb_mtime = body->mtime;
	}
	if (body->valid & OBD_MD_FLCTIME) {
		if (body->ctime > LTIME_S(inode->i_ctime))
			LTIME_S(inode->i_ctime) = body->ctime;
		lli->lli_lvb.lvb_ctime = body->ctime;
	}
	if (body->valid & OBD_MD_FLMODE)
		inode->i_mode = (inode->i_mode & S_IFMT)|(body->mode & ~S_IFMT);
	if (body->valid & OBD_MD_FLTYPE)
		inode->i_mode = (inode->i_mode & ~S_IFMT)|(body->mode & S_IFMT);
	LASSERT(inode->i_mode != 0);
	if (S_ISREG(inode->i_mode)) {
		inode->i_blkbits = min(PTLRPC_MAX_BRW_BITS + 1, LL_MAX_BLKSIZE_BITS);
	} else {
		inode->i_blkbits = inode->i_sb->s_blocksize_bits;
	}
	if (body->valid & OBD_MD_FLUID)
		inode->i_uid = make_kuid(&init_user_ns, body->uid);
	if (body->valid & OBD_MD_FLGID)
		inode->i_gid = make_kgid(&init_user_ns, body->gid);
	if (body->valid & OBD_MD_FLFLAGS)
		inode->i_flags = ll_ext_to_inode_flags(body->flags);
	if (body->valid & OBD_MD_FLNLINK)
		set_nlink(inode, body->nlink);
	if (body->valid & OBD_MD_FLRDEV)
		inode->i_rdev = old_decode_dev(body->rdev);

	if (body->valid & OBD_MD_FLID) {
		/* FID shouldn't be changed! */
		if (fid_is_sane(&lli->lli_fid)) {
			LASSERTF(lu_fid_eq(&lli->lli_fid, &body->fid1),
				 "Trying to change FID "DFID
				 " to the "DFID", inode %lu/%u(%p)\n",
				 PFID(&lli->lli_fid), PFID(&body->fid1),
				 inode->i_ino, inode->i_generation, inode);
		} else
			lli->lli_fid = body->fid1;
	}

	LASSERT(fid_seq(&lli->lli_fid) != 0);

	if (body->valid & OBD_MD_FLSIZE) {
		if (exp_connect_som(ll_i2mdexp(inode)) &&
		    S_ISREG(inode->i_mode)) {
			struct lustre_handle lockh;
			ldlm_mode_t mode;

			/* As it is possible a blocking ast has been processed
			 * by this time, we need to check there is an UPDATE
			 * lock on the client and set LLIF_MDS_SIZE_LOCK holding
			 * it. */
			mode = ll_take_md_lock(inode, MDS_INODELOCK_UPDATE,
					       &lockh, LDLM_FL_CBPENDING,
					       LCK_CR | LCK_CW |
					       LCK_PR | LCK_PW);
			if (mode) {
				if (lli->lli_flags & (LLIF_DONE_WRITING |
						      LLIF_EPOCH_PENDING |
						      LLIF_SOM_DIRTY)) {
					CERROR("ino %lu flags %u still has "
					       "size authority! do not trust "
					       "the size got from MDS\n",
					       inode->i_ino, lli->lli_flags);
				} else {
					/* Use old size assignment to avoid
					 * deadlock bz14138 & bz14326 */
					i_size_write(inode, body->size);
					spin_lock(&lli->lli_lock);
					lli->lli_flags |= LLIF_MDS_SIZE_LOCK;
					spin_unlock(&lli->lli_lock);
				}
				ldlm_lock_decref(&lockh, mode);
			}
		} else {
			/* Use old size assignment to avoid
			 * deadlock bz14138 & bz14326 */
			i_size_write(inode, body->size);

			CDEBUG(D_VFSTRACE, "inode=%lu, updating i_size %llu\n",
			       inode->i_ino, (unsigned long long)body->size);
		}

		if (body->valid & OBD_MD_FLBLOCKS)
			inode->i_blocks = body->blocks;
	}

	if (body->valid & OBD_MD_FLMDSCAPA) {
		LASSERT(md->mds_capa);
		ll_add_capa(inode, md->mds_capa);
	}
	if (body->valid & OBD_MD_FLOSSCAPA) {
		LASSERT(md->oss_capa);
		ll_add_capa(inode, md->oss_capa);
	}

	if (body->valid & OBD_MD_TSTATE) {
		if (body->t_state & MS_RESTORE)
			lli->lli_flags |= LLIF_FILE_RESTORING;
	}
}

void ll_read_inode2(struct inode *inode, void *opaque)
{
	struct lustre_md *md = opaque;
	struct ll_inode_info *lli = ll_i2info(inode);

	CDEBUG(D_VFSTRACE, "VFS Op:inode="DFID"(%p)\n",
	       PFID(&lli->lli_fid), inode);

	LASSERT(!lli->lli_has_smd);

	/* Core attributes from the MDS first.  This is a new inode, and
	 * the VFS doesn't zero times in the core inode so we have to do
	 * it ourselves.  They will be overwritten by either MDS or OST
	 * attributes - we just need to make sure they aren't newer. */
	LTIME_S(inode->i_mtime) = 0;
	LTIME_S(inode->i_atime) = 0;
	LTIME_S(inode->i_ctime) = 0;
	inode->i_rdev = 0;
	ll_update_inode(inode, md);

	/* OIDEBUG(inode); */

	/* initializing backing dev info. */
	inode->i_mapping->backing_dev_info = &s2lsi(inode->i_sb)->lsi_bdi;


	if (S_ISREG(inode->i_mode)) {
		struct ll_sb_info *sbi = ll_i2sbi(inode);
		inode->i_op = &ll_file_inode_operations;
		inode->i_fop = sbi->ll_fop;
		inode->i_mapping->a_ops = (struct address_space_operations *)&ll_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ll_dir_inode_operations;
		inode->i_fop = &ll_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &ll_fast_symlink_inode_operations;
	} else {
		inode->i_op = &ll_special_inode_operations;

		init_special_inode(inode, inode->i_mode,
				   inode->i_rdev);
	}
}

void ll_delete_inode(struct inode *inode)
{
	struct cl_inode_info *lli = cl_i2info(inode);

	if (S_ISREG(inode->i_mode) && lli->lli_clob != NULL)
		/* discard all dirty pages before truncating them, required by
		 * osc_extent implementation at LU-1030. */
		cl_sync_file_range(inode, 0, OBD_OBJECT_EOF,
				   CL_FSYNC_DISCARD, 1);

	truncate_inode_pages_final(&inode->i_data);

	/* Workaround for LU-118 */
	if (inode->i_data.nrpages) {
		TREE_READ_LOCK_IRQ(&inode->i_data);
		TREE_READ_UNLOCK_IRQ(&inode->i_data);
		LASSERTF(inode->i_data.nrpages == 0,
			 "inode=%lu/%u(%p) nrpages=%lu, see "
			 "http://jira.whamcloud.com/browse/LU-118\n",
			 inode->i_ino, inode->i_generation, inode,
			 inode->i_data.nrpages);
	}
	/* Workaround end */

	ll_clear_inode(inode);
	clear_inode(inode);
}

int ll_iocontrol(struct inode *inode, struct file *file,
		 unsigned int cmd, unsigned long arg)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *req = NULL;
	int rc, flags = 0;

	switch (cmd) {
	case FSFILT_IOC_GETFLAGS: {
		struct mdt_body *body;
		struct md_op_data *op_data;

		op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL,
					     0, 0, LUSTRE_OPC_ANY,
					     NULL);
		if (IS_ERR(op_data))
			return PTR_ERR(op_data);

		op_data->op_valid = OBD_MD_FLFLAGS;
		rc = md_getattr(sbi->ll_md_exp, op_data, &req);
		ll_finish_md_op_data(op_data);
		if (rc) {
			CERROR("failure %d inode %lu\n", rc, inode->i_ino);
			return -abs(rc);
		}

		body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);

		flags = body->flags;

		ptlrpc_req_finished(req);

		return put_user(flags, (int *)arg);
	}
	case FSFILT_IOC_SETFLAGS: {
		struct lov_stripe_md *lsm;
		struct obd_info oinfo = { { { 0 } } };
		struct md_op_data *op_data;

		if (get_user(flags, (int *)arg))
			return -EFAULT;

		op_data = ll_prep_md_op_data(NULL, inode, NULL, NULL, 0, 0,
					     LUSTRE_OPC_ANY, NULL);
		if (IS_ERR(op_data))
			return PTR_ERR(op_data);

		((struct ll_iattr *)&op_data->op_attr)->ia_attr_flags = flags;
		op_data->op_attr.ia_valid |= ATTR_ATTR_FLAG;
		rc = md_setattr(sbi->ll_md_exp, op_data,
				NULL, 0, NULL, 0, &req, NULL);
		ll_finish_md_op_data(op_data);
		ptlrpc_req_finished(req);
		if (rc)
			return rc;

		inode->i_flags = ll_ext_to_inode_flags(flags);

		lsm = ccc_inode_lsm_get(inode);
		if (!lsm_has_objects(lsm)) {
			ccc_inode_lsm_put(inode, lsm);
			return 0;
		}

		OBDO_ALLOC(oinfo.oi_oa);
		if (!oinfo.oi_oa) {
			ccc_inode_lsm_put(inode, lsm);
			return -ENOMEM;
		}
		oinfo.oi_md = lsm;
		oinfo.oi_oa->o_oi = lsm->lsm_oi;
		oinfo.oi_oa->o_flags = flags;
		oinfo.oi_oa->o_valid = OBD_MD_FLID | OBD_MD_FLFLAGS |
				       OBD_MD_FLGROUP;
		oinfo.oi_capa = ll_mdscapa_get(inode);
		obdo_set_parent_fid(oinfo.oi_oa, &ll_i2info(inode)->lli_fid);
		rc = obd_setattr_rqset(sbi->ll_dt_exp, &oinfo, NULL);
		capa_put(oinfo.oi_capa);
		OBDO_FREE(oinfo.oi_oa);
		ccc_inode_lsm_put(inode, lsm);

		if (rc && rc != -EPERM && rc != -EACCES)
			CERROR("osc_setattr_async fails: rc = %d\n", rc);

		return rc;
	}
	default:
		return -ENOSYS;
	}

	return 0;
}

int ll_flush_ctx(struct inode *inode)
{
	struct ll_sb_info  *sbi = ll_i2sbi(inode);

	CDEBUG(D_SEC, "flush context for user %d\n",
		      from_kuid(&init_user_ns, current_uid()));

	obd_set_info_async(NULL, sbi->ll_md_exp,
			   sizeof(KEY_FLUSH_CTX), KEY_FLUSH_CTX,
			   0, NULL, NULL);
	obd_set_info_async(NULL, sbi->ll_dt_exp,
			   sizeof(KEY_FLUSH_CTX), KEY_FLUSH_CTX,
			   0, NULL, NULL);
	return 0;
}

/* umount -f client means force down, don't save state */
void ll_umount_begin(struct super_block *sb)
{
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	struct obd_device *obd;
	struct obd_ioctl_data *ioc_data;

	CDEBUG(D_VFSTRACE, "VFS Op: superblock %p count %d active %d\n", sb,
	       sb->s_count, atomic_read(&sb->s_active));

	obd = class_exp2obd(sbi->ll_md_exp);
	if (obd == NULL) {
		CERROR("Invalid MDC connection handle %#llx\n",
		       sbi->ll_md_exp->exp_handle.h_cookie);
		return;
	}
	obd->obd_force = 1;

	obd = class_exp2obd(sbi->ll_dt_exp);
	if (obd == NULL) {
		CERROR("Invalid LOV connection handle %#llx\n",
		       sbi->ll_dt_exp->exp_handle.h_cookie);
		return;
	}
	obd->obd_force = 1;

	ioc_data = kzalloc(sizeof(*ioc_data), GFP_NOFS);
	if (ioc_data) {
		obd_iocontrol(IOC_OSC_SET_ACTIVE, sbi->ll_md_exp,
			      sizeof(*ioc_data), ioc_data, NULL);

		obd_iocontrol(IOC_OSC_SET_ACTIVE, sbi->ll_dt_exp,
			      sizeof(*ioc_data), ioc_data, NULL);

		OBD_FREE_PTR(ioc_data);
	}

	/* Really, we'd like to wait until there are no requests outstanding,
	 * and then continue.  For now, we just invalidate the requests,
	 * schedule() and sleep one second if needed, and hope.
	 */
	schedule();
}

int ll_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	char *profilenm = get_profile_name(sb);
	int err;
	__u32 read_only;

	if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY)) {
		read_only = *flags & MS_RDONLY;
		err = obd_set_info_async(NULL, sbi->ll_md_exp,
					 sizeof(KEY_READ_ONLY),
					 KEY_READ_ONLY, sizeof(read_only),
					 &read_only, NULL);
		if (err) {
			LCONSOLE_WARN("Failed to remount %s %s (%d)\n",
				      profilenm, read_only ?
				      "read-only" : "read-write", err);
			return err;
		}

		if (read_only)
			sb->s_flags |= MS_RDONLY;
		else
			sb->s_flags &= ~MS_RDONLY;

		if (sbi->ll_flags & LL_SBI_VERBOSE)
			LCONSOLE_WARN("Remounted %s %s\n", profilenm,
				      read_only ?  "read-only" : "read-write");
	}
	return 0;
}

int ll_prep_inode(struct inode **inode, struct ptlrpc_request *req,
		  struct super_block *sb, struct lookup_intent *it)
{
	struct ll_sb_info *sbi = NULL;
	struct lustre_md md;
	int rc;

	LASSERT(*inode || sb);
	sbi = sb ? ll_s2sbi(sb) : ll_i2sbi(*inode);
	rc = md_get_lustre_md(sbi->ll_md_exp, req, sbi->ll_dt_exp,
			      sbi->ll_md_exp, &md);
	if (rc)
		return rc;

	if (*inode) {
		ll_update_inode(*inode, &md);
	} else {
		LASSERT(sb != NULL);

		/*
		 * At this point server returns to client's same fid as client
		 * generated for creating. So using ->fid1 is okay here.
		 */
		LASSERT(fid_is_sane(&md.body->fid1));

		*inode = ll_iget(sb, cl_fid_build_ino(&md.body->fid1,
					     sbi->ll_flags & LL_SBI_32BIT_API),
				 &md);
		if (*inode == NULL || IS_ERR(*inode)) {
#ifdef CONFIG_FS_POSIX_ACL
			if (md.posix_acl) {
				posix_acl_release(md.posix_acl);
				md.posix_acl = NULL;
			}
#endif
			rc = IS_ERR(*inode) ? PTR_ERR(*inode) : -ENOMEM;
			*inode = NULL;
			CERROR("new_inode -fatal: rc %d\n", rc);
			goto out;
		}
	}

	/* Handling piggyback layout lock.
	 * Layout lock can be piggybacked by getattr and open request.
	 * The lsm can be applied to inode only if it comes with a layout lock
	 * otherwise correct layout may be overwritten, for example:
	 * 1. proc1: mdt returns a lsm but not granting layout
	 * 2. layout was changed by another client
	 * 3. proc2: refresh layout and layout lock granted
	 * 4. proc1: to apply a stale layout */
	if (it != NULL && it->d.lustre.it_lock_mode != 0) {
		struct lustre_handle lockh;
		struct ldlm_lock *lock;

		lockh.cookie = it->d.lustre.it_lock_handle;
		lock = ldlm_handle2lock(&lockh);
		LASSERT(lock != NULL);
		if (ldlm_has_layout(lock)) {
			struct cl_object_conf conf;

			memset(&conf, 0, sizeof(conf));
			conf.coc_opc = OBJECT_CONF_SET;
			conf.coc_inode = *inode;
			conf.coc_lock = lock;
			conf.u.coc_md = &md;
			(void)ll_layout_conf(*inode, &conf);
		}
		LDLM_LOCK_PUT(lock);
	}

out:
	if (md.lsm != NULL)
		obd_free_memmd(sbi->ll_dt_exp, &md.lsm);
	md_free_lustre_md(sbi->ll_md_exp, &md);
	return rc;
}

int ll_obd_statfs(struct inode *inode, void *arg)
{
	struct ll_sb_info *sbi = NULL;
	struct obd_export *exp;
	char *buf = NULL;
	struct obd_ioctl_data *data = NULL;
	__u32 type;
	__u32 flags;
	int len = 0, rc;

	if (!inode || !(sbi = ll_i2sbi(inode))) {
		rc = -EINVAL;
		goto out_statfs;
	}

	rc = obd_ioctl_getdata(&buf, &len, arg);
	if (rc)
		goto out_statfs;

	data = (void *)buf;
	if (!data->ioc_inlbuf1 || !data->ioc_inlbuf2 ||
	    !data->ioc_pbuf1 || !data->ioc_pbuf2) {
		rc = -EINVAL;
		goto out_statfs;
	}

	if (data->ioc_inllen1 != sizeof(__u32) ||
	    data->ioc_inllen2 != sizeof(__u32) ||
	    data->ioc_plen1 != sizeof(struct obd_statfs) ||
	    data->ioc_plen2 != sizeof(struct obd_uuid)) {
		rc = -EINVAL;
		goto out_statfs;
	}

	memcpy(&type, data->ioc_inlbuf1, sizeof(__u32));
	if (type & LL_STATFS_LMV)
		exp = sbi->ll_md_exp;
	else if (type & LL_STATFS_LOV)
		exp = sbi->ll_dt_exp;
	else {
		rc = -ENODEV;
		goto out_statfs;
	}

	flags = (type & LL_STATFS_NODELAY) ? OBD_STATFS_NODELAY : 0;
	rc = obd_iocontrol(IOC_OBD_STATFS, exp, len, buf, &flags);
	if (rc)
		goto out_statfs;
out_statfs:
	if (buf)
		obd_ioctl_freedata(buf, len);
	return rc;
}

int ll_process_config(struct lustre_cfg *lcfg)
{
	char *ptr;
	void *sb;
	struct lprocfs_static_vars lvars;
	unsigned long x;
	int rc = 0;

	lprocfs_llite_init_vars(&lvars);

	/* The instance name contains the sb: lustre-client-aacfe000 */
	ptr = strrchr(lustre_cfg_string(lcfg, 0), '-');
	if (!ptr || !*(++ptr))
		return -EINVAL;
	if (sscanf(ptr, "%lx", &x) != 1)
		return -EINVAL;
	sb = (void *)x;
	/* This better be a real Lustre superblock! */
	LASSERT(s2lsi((struct super_block *)sb)->lsi_lmd->lmd_magic == LMD_MAGIC);

	/* Note we have not called client_common_fill_super yet, so
	   proc fns must be able to handle that! */
	rc = class_process_proc_param(PARAM_LLITE, lvars.obd_vars,
				      lcfg, sb);
	if (rc > 0)
		rc = 0;
	return rc;
}

/* this function prepares md_op_data hint for passing ot down to MD stack. */
struct md_op_data *ll_prep_md_op_data(struct md_op_data *op_data,
				       struct inode *i1, struct inode *i2,
				       const char *name, int namelen,
				       int mode, __u32 opc, void *data)
{
	LASSERT(i1 != NULL);

	if (namelen > ll_i2sbi(i1)->ll_namelen)
		return ERR_PTR(-ENAMETOOLONG);

	if (op_data == NULL)
		op_data = kzalloc(sizeof(*op_data), GFP_NOFS);

	if (op_data == NULL)
		return ERR_PTR(-ENOMEM);

	ll_i2gids(op_data->op_suppgids, i1, i2);
	op_data->op_fid1 = *ll_inode2fid(i1);
	op_data->op_capa1 = ll_mdscapa_get(i1);

	if (i2) {
		op_data->op_fid2 = *ll_inode2fid(i2);
		op_data->op_capa2 = ll_mdscapa_get(i2);
	} else {
		fid_zero(&op_data->op_fid2);
		op_data->op_capa2 = NULL;
	}

	op_data->op_name = name;
	op_data->op_namelen = namelen;
	op_data->op_mode = mode;
	op_data->op_mod_time = get_seconds();
	op_data->op_fsuid = from_kuid(&init_user_ns, current_fsuid());
	op_data->op_fsgid = from_kgid(&init_user_ns, current_fsgid());
	op_data->op_cap = cfs_curproc_cap_pack();
	op_data->op_bias = 0;
	op_data->op_cli_flags = 0;
	if ((opc == LUSTRE_OPC_CREATE) && (name != NULL) &&
	     filename_is_volatile(name, namelen, NULL))
		op_data->op_bias |= MDS_CREATE_VOLATILE;
	op_data->op_opc = opc;
	op_data->op_mds = 0;
	op_data->op_data = data;

	/* If the file is being opened after mknod() (normally due to NFS)
	 * try to use the default stripe data from parent directory for
	 * allocating OST objects.  Try to pass the parent FID to MDS. */
	if (opc == LUSTRE_OPC_CREATE && i1 == i2 && S_ISREG(i2->i_mode) &&
	    !ll_i2info(i2)->lli_has_smd) {
		struct ll_inode_info *lli = ll_i2info(i2);

		spin_lock(&lli->lli_lock);
		if (likely(!lli->lli_has_smd && !fid_is_zero(&lli->lli_pfid)))
			op_data->op_fid1 = lli->lli_pfid;
		spin_unlock(&lli->lli_lock);
		/** We ignore parent's capability temporary. */
	}

	/* When called by ll_setattr_raw, file is i1. */
	if (LLIF_DATA_MODIFIED & ll_i2info(i1)->lli_flags)
		op_data->op_bias |= MDS_DATA_MODIFIED;

	return op_data;
}

void ll_finish_md_op_data(struct md_op_data *op_data)
{
	capa_put(op_data->op_capa1);
	capa_put(op_data->op_capa2);
	OBD_FREE_PTR(op_data);
}

int ll_show_options(struct seq_file *seq, struct dentry *dentry)
{
	struct ll_sb_info *sbi;

	LASSERT((seq != NULL) && (dentry != NULL));
	sbi = ll_s2sbi(dentry->d_sb);

	if (sbi->ll_flags & LL_SBI_NOLCK)
		seq_puts(seq, ",nolock");

	if (sbi->ll_flags & LL_SBI_FLOCK)
		seq_puts(seq, ",flock");

	if (sbi->ll_flags & LL_SBI_LOCALFLOCK)
		seq_puts(seq, ",localflock");

	if (sbi->ll_flags & LL_SBI_USER_XATTR)
		seq_puts(seq, ",user_xattr");

	if (sbi->ll_flags & LL_SBI_LAZYSTATFS)
		seq_puts(seq, ",lazystatfs");

	if (sbi->ll_flags & LL_SBI_USER_FID2PATH)
		seq_puts(seq, ",user_fid2path");

	return 0;
}

/**
 * Get obd name by cmd, and copy out to user space
 */
int ll_get_obd_name(struct inode *inode, unsigned int cmd, unsigned long arg)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct obd_device *obd;

	if (cmd == OBD_IOC_GETDTNAME)
		obd = class_exp2obd(sbi->ll_dt_exp);
	else if (cmd == OBD_IOC_GETMDNAME)
		obd = class_exp2obd(sbi->ll_md_exp);
	else
		return -EINVAL;

	if (!obd)
		return -ENOENT;

	if (copy_to_user((void *)arg, obd->obd_name,
			     strlen(obd->obd_name) + 1))
		return -EFAULT;

	return 0;
}

/**
 * Get lustre file system name by \a sbi. If \a buf is provided(non-NULL), the
 * fsname will be returned in this buffer; otherwise, a static buffer will be
 * used to store the fsname and returned to caller.
 */
char *ll_get_fsname(struct super_block *sb, char *buf, int buflen)
{
	static char fsname_static[MTI_NAME_MAXLEN];
	struct lustre_sb_info *lsi = s2lsi(sb);
	char *ptr;
	int len;

	if (buf == NULL) {
		/* this means the caller wants to use static buffer
		 * and it doesn't care about race. Usually this is
		 * in error reporting path */
		buf = fsname_static;
		buflen = sizeof(fsname_static);
	}

	len = strlen(lsi->lsi_lmd->lmd_profile);
	ptr = strrchr(lsi->lsi_lmd->lmd_profile, '-');
	if (ptr && (strcmp(ptr, "-client") == 0))
		len -= 7;

	if (unlikely(len >= buflen))
		len = buflen - 1;
	strncpy(buf, lsi->lsi_lmd->lmd_profile, len);
	buf[len] = '\0';

	return buf;
}

static char* ll_d_path(struct dentry *dentry, char *buf, int bufsize)
{
	char *path = NULL;

	struct path p;

	p.dentry = dentry;
	p.mnt = current->fs->root.mnt;
	path_get(&p);
	path = d_path(&p, buf, bufsize);
	path_put(&p);

	return path;
}

void ll_dirty_page_discard_warn(struct page *page, int ioret)
{
	char *buf, *path = NULL;
	struct dentry *dentry = NULL;
	struct ccc_object *obj = cl_inode2ccc(page->mapping->host);

	/* this can be called inside spin lock so use GFP_ATOMIC. */
	buf = (char *)__get_free_page(GFP_ATOMIC);
	if (buf != NULL) {
		dentry = d_find_alias(page->mapping->host);
		if (dentry != NULL)
			path = ll_d_path(dentry, buf, PAGE_SIZE);
	}

	CDEBUG(D_WARNING,
	       "%s: dirty page discard: %s/fid: "DFID"/%s may get corrupted "
	       "(rc %d)\n", ll_get_fsname(page->mapping->host->i_sb, NULL, 0),
	       s2lsi(page->mapping->host->i_sb)->lsi_lmd->lmd_dev,
	       PFID(&obj->cob_header.coh_lu.loh_fid),
	       (path && !IS_ERR(path)) ? path : "", ioret);

	if (dentry != NULL)
		dput(dentry);

	if (buf != NULL)
		free_page((unsigned long)buf);
}
