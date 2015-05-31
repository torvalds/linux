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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/lustre_lite.h"
#include "../include/lprocfs_status.h"
#include <linux/seq_file.h>
#include "../include/obd_support.h"

#include "llite_internal.h"
#include "vvp_internal.h"

/* /proc/lustre/llite mount point registration */
static struct file_operations ll_rw_extents_stats_fops;
static struct file_operations ll_rw_extents_stats_pp_fops;
static struct file_operations ll_rw_offset_stats_fops;

static ssize_t blocksize_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	struct obd_statfs osfs;
	int rc;

	rc = ll_statfs_internal(sbi->ll_sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				OBD_STATFS_NODELAY);
	if (!rc)
		return sprintf(buf, "%u\n", osfs.os_bsize);

	return rc;
}
LUSTRE_RO_ATTR(blocksize);

static ssize_t kbytestotal_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	struct obd_statfs osfs;
	int rc;

	rc = ll_statfs_internal(sbi->ll_sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				OBD_STATFS_NODELAY);
	if (!rc) {
		__u32 blk_size = osfs.os_bsize >> 10;
		__u64 result = osfs.os_blocks;

		while (blk_size >>= 1)
			result <<= 1;

		rc = sprintf(buf, "%llu\n", result);
	}

	return rc;
}
LUSTRE_RO_ATTR(kbytestotal);

static ssize_t kbytesfree_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	struct obd_statfs osfs;
	int rc;

	rc = ll_statfs_internal(sbi->ll_sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				OBD_STATFS_NODELAY);
	if (!rc) {
		__u32 blk_size = osfs.os_bsize >> 10;
		__u64 result = osfs.os_bfree;

		while (blk_size >>= 1)
			result <<= 1;

		rc = sprintf(buf, "%llu\n", result);
	}

	return rc;
}
LUSTRE_RO_ATTR(kbytesfree);

static ssize_t kbytesavail_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	struct obd_statfs osfs;
	int rc;

	rc = ll_statfs_internal(sbi->ll_sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				OBD_STATFS_NODELAY);
	if (!rc) {
		__u32 blk_size = osfs.os_bsize >> 10;
		__u64 result = osfs.os_bavail;

		while (blk_size >>= 1)
			result <<= 1;

		rc = sprintf(buf, "%llu\n", result);
	}

	return rc;
}
LUSTRE_RO_ATTR(kbytesavail);

static ssize_t filestotal_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	struct obd_statfs osfs;
	int rc;

	rc = ll_statfs_internal(sbi->ll_sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				OBD_STATFS_NODELAY);
	if (!rc)
		return sprintf(buf, "%llu\n", osfs.os_files);

	return rc;
}
LUSTRE_RO_ATTR(filestotal);

static ssize_t filesfree_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	struct obd_statfs osfs;
	int rc;

	rc = ll_statfs_internal(sbi->ll_sb, &osfs,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				OBD_STATFS_NODELAY);
	if (!rc)
		return sprintf(buf, "%llu\n", osfs.os_ffree);

	return rc;
}
LUSTRE_RO_ATTR(filesfree);

static ssize_t client_type_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%s client\n",
			sbi->ll_flags & LL_SBI_RMT_CLIENT ? "remote" : "local");
}
LUSTRE_RO_ATTR(client_type);

static ssize_t fstype_show(struct kobject *kobj, struct attribute *attr,
			   char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%s\n", sbi->ll_sb->s_type->name);
}
LUSTRE_RO_ATTR(fstype);

static ssize_t uuid_show(struct kobject *kobj, struct attribute *attr,
			 char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%s\n", sbi->ll_sb_uuid.uuid);
}
LUSTRE_RO_ATTR(uuid);

static int ll_site_stats_seq_show(struct seq_file *m, void *v)
{
	struct super_block *sb = m->private;

	/*
	 * See description of statistical counters in struct cl_site, and
	 * struct lu_site.
	 */
	return cl_site_stats_print(lu2cl_site(ll_s2sbi(sb)->ll_site), m);
}
LPROC_SEQ_FOPS_RO(ll_site_stats);

static ssize_t max_read_ahead_mb_show(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	long pages_number;
	int mult;

	spin_lock(&sbi->ll_lock);
	pages_number = sbi->ll_ra_info.ra_max_pages;
	spin_unlock(&sbi->ll_lock);

	mult = 1 << (20 - PAGE_CACHE_SHIFT);
	return lprocfs_read_frac_helper(buf, PAGE_SIZE, pages_number, mult);
}

static ssize_t max_read_ahead_mb_store(struct kobject *kobj,
				       struct attribute *attr,
				       const char *buffer,
				       size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long pages_number;

	rc = kstrtoul(buffer, 10, &pages_number);
	if (rc)
		return rc;

	pages_number *= 1 << (20 - PAGE_CACHE_SHIFT); /* MB -> pages */

	if (pages_number > totalram_pages / 2) {

		CERROR("can't set file readahead more than %lu MB\n",
		       totalram_pages >> (20 - PAGE_CACHE_SHIFT + 1)); /*1/2 of RAM*/
		return -ERANGE;
	}

	spin_lock(&sbi->ll_lock);
	sbi->ll_ra_info.ra_max_pages = pages_number;
	spin_unlock(&sbi->ll_lock);

	return count;
}
LUSTRE_RW_ATTR(max_read_ahead_mb);

static ssize_t max_read_ahead_per_file_mb_show(struct kobject *kobj,
					       struct attribute *attr,
					       char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	long pages_number;
	int mult;

	spin_lock(&sbi->ll_lock);
	pages_number = sbi->ll_ra_info.ra_max_pages_per_file;
	spin_unlock(&sbi->ll_lock);

	mult = 1 << (20 - PAGE_CACHE_SHIFT);
	return lprocfs_read_frac_helper(buf, PAGE_SIZE, pages_number, mult);
}

static ssize_t max_read_ahead_per_file_mb_store(struct kobject *kobj,
						struct attribute *attr,
						const char *buffer,
						size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long pages_number;

	rc = kstrtoul(buffer, 10, &pages_number);
	if (rc)
		return rc;

	if (pages_number > sbi->ll_ra_info.ra_max_pages) {
		CERROR("can't set file readahead more than max_read_ahead_mb %lu MB\n",
		       sbi->ll_ra_info.ra_max_pages);
		return -ERANGE;
	}

	spin_lock(&sbi->ll_lock);
	sbi->ll_ra_info.ra_max_pages_per_file = pages_number;
	spin_unlock(&sbi->ll_lock);

	return count;
}
LUSTRE_RW_ATTR(max_read_ahead_per_file_mb);

static ssize_t max_read_ahead_whole_mb_show(struct kobject *kobj,
					    struct attribute *attr,
					    char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	long pages_number;
	int mult;

	spin_lock(&sbi->ll_lock);
	pages_number = sbi->ll_ra_info.ra_max_read_ahead_whole_pages;
	spin_unlock(&sbi->ll_lock);

	mult = 1 << (20 - PAGE_CACHE_SHIFT);
	return lprocfs_read_frac_helper(buf, PAGE_SIZE, pages_number, mult);
}

static ssize_t max_read_ahead_whole_mb_store(struct kobject *kobj,
					     struct attribute *attr,
					     const char  *buffer,
					     size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long pages_number;

	rc = kstrtoul(buffer, 10, &pages_number);
	if (rc)
		return rc;

	/* Cap this at the current max readahead window size, the readahead
	 * algorithm does this anyway so it's pointless to set it larger. */
	if (pages_number > sbi->ll_ra_info.ra_max_pages_per_file) {
		CERROR("can't set max_read_ahead_whole_mb more than max_read_ahead_per_file_mb: %lu\n",
		       sbi->ll_ra_info.ra_max_pages_per_file >> (20 - PAGE_CACHE_SHIFT));
		return -ERANGE;
	}

	spin_lock(&sbi->ll_lock);
	sbi->ll_ra_info.ra_max_read_ahead_whole_pages = pages_number;
	spin_unlock(&sbi->ll_lock);

	return count;
}
LUSTRE_RW_ATTR(max_read_ahead_whole_mb);

static int ll_max_cached_mb_seq_show(struct seq_file *m, void *v)
{
	struct super_block     *sb    = m->private;
	struct ll_sb_info      *sbi   = ll_s2sbi(sb);
	struct cl_client_cache *cache = &sbi->ll_cache;
	int shift = 20 - PAGE_CACHE_SHIFT;
	int max_cached_mb;
	int unused_mb;

	max_cached_mb = cache->ccc_lru_max >> shift;
	unused_mb = atomic_read(&cache->ccc_lru_left) >> shift;
	seq_printf(m,
		   "users: %d\n"
		   "max_cached_mb: %d\n"
		   "used_mb: %d\n"
		   "unused_mb: %d\n"
		   "reclaim_count: %u\n",
		   atomic_read(&cache->ccc_users),
		   max_cached_mb,
		   max_cached_mb - unused_mb,
		   unused_mb,
		   cache->ccc_lru_shrinkers);
	return 0;
}

static ssize_t ll_max_cached_mb_seq_write(struct file *file,
					  const char __user *buffer,
					  size_t count, loff_t *off)
{
	struct super_block *sb = ((struct seq_file *)file->private_data)->private;
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	struct cl_client_cache *cache = &sbi->ll_cache;
	int mult, rc, pages_number;
	int diff = 0;
	int nrpages = 0;
	char kernbuf[128];

	if (count >= sizeof(kernbuf))
		return -EINVAL;

	if (copy_from_user(kernbuf, buffer, count))
		return -EFAULT;
	kernbuf[count] = 0;

	mult = 1 << (20 - PAGE_CACHE_SHIFT);
	buffer += lprocfs_find_named_value(kernbuf, "max_cached_mb:", &count) -
		  kernbuf;
	rc = lprocfs_write_frac_helper(buffer, count, &pages_number, mult);
	if (rc)
		return rc;

	if (pages_number < 0 || pages_number > totalram_pages) {
		CERROR("%s: can't set max cache more than %lu MB\n",
		       ll_get_fsname(sb, NULL, 0),
		       totalram_pages >> (20 - PAGE_CACHE_SHIFT));
		return -ERANGE;
	}

	spin_lock(&sbi->ll_lock);
	diff = pages_number - cache->ccc_lru_max;
	spin_unlock(&sbi->ll_lock);

	/* easy - add more LRU slots. */
	if (diff >= 0) {
		atomic_add(diff, &cache->ccc_lru_left);
		rc = 0;
		goto out;
	}

	diff = -diff;
	while (diff > 0) {
		int tmp;

		/* reduce LRU budget from free slots. */
		do {
			int ov, nv;

			ov = atomic_read(&cache->ccc_lru_left);
			if (ov == 0)
				break;

			nv = ov > diff ? ov - diff : 0;
			rc = atomic_cmpxchg(&cache->ccc_lru_left, ov, nv);
			if (likely(ov == rc)) {
				diff -= ov - nv;
				nrpages += ov - nv;
				break;
			}
		} while (1);

		if (diff <= 0)
			break;

		if (sbi->ll_dt_exp == NULL) { /* being initialized */
			rc = -ENODEV;
			break;
		}

		/* difficult - have to ask OSCs to drop LRU slots. */
		tmp = diff << 1;
		rc = obd_set_info_async(NULL, sbi->ll_dt_exp,
				sizeof(KEY_CACHE_LRU_SHRINK),
				KEY_CACHE_LRU_SHRINK,
				sizeof(tmp), &tmp, NULL);
		if (rc < 0)
			break;
	}

out:
	if (rc >= 0) {
		spin_lock(&sbi->ll_lock);
		cache->ccc_lru_max = pages_number;
		spin_unlock(&sbi->ll_lock);
		rc = count;
	} else {
		atomic_add(nrpages, &cache->ccc_lru_left);
	}
	return rc;
}
LPROC_SEQ_FOPS(ll_max_cached_mb);

static ssize_t checksum_pages_show(struct kobject *kobj, struct attribute *attr,
				   char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%u\n", (sbi->ll_flags & LL_SBI_CHECKSUM) ? 1 : 0);
}

static ssize_t checksum_pages_store(struct kobject *kobj,
				    struct attribute *attr,
				    const char *buffer,
				    size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long val;

	if (!sbi->ll_dt_exp)
		/* Not set up yet */
		return -EAGAIN;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;
	if (val)
		sbi->ll_flags |= LL_SBI_CHECKSUM;
	else
		sbi->ll_flags &= ~LL_SBI_CHECKSUM;

	rc = obd_set_info_async(NULL, sbi->ll_dt_exp, sizeof(KEY_CHECKSUM),
				KEY_CHECKSUM, sizeof(val), &val, NULL);
	if (rc)
		CWARN("Failed to set OSC checksum flags: %d\n", rc);

	return count;
}
LUSTRE_RW_ATTR(checksum_pages);

static ssize_t ll_rd_track_id(struct kobject *kobj, char *buf,
			      enum stats_track_type type)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	if (sbi->ll_stats_track_type == type)
		return sprintf(buf, "%d\n", sbi->ll_stats_track_id);
	else if (sbi->ll_stats_track_type == STATS_TRACK_ALL)
		return sprintf(buf, "0 (all)\n");
	else
		return sprintf(buf, "untracked\n");
}

static ssize_t ll_wr_track_id(struct kobject *kobj, const char *buffer,
			      size_t count,
			      enum stats_track_type type)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long pid;

	rc = kstrtoul(buffer, 10, &pid);
	if (rc)
		return rc;
	sbi->ll_stats_track_id = pid;
	if (pid == 0)
		sbi->ll_stats_track_type = STATS_TRACK_ALL;
	else
		sbi->ll_stats_track_type = type;
	lprocfs_clear_stats(sbi->ll_stats);
	return count;
}

static ssize_t stats_track_pid_show(struct kobject *kobj,
				    struct attribute *attr,
				    char *buf)
{
	return ll_rd_track_id(kobj, buf, STATS_TRACK_PID);
}

static ssize_t stats_track_pid_store(struct kobject *kobj,
				     struct attribute *attr,
				     const char *buffer,
				     size_t count)
{
	return ll_wr_track_id(kobj, buffer, count, STATS_TRACK_PID);
}
LUSTRE_RW_ATTR(stats_track_pid);

static ssize_t stats_track_ppid_show(struct kobject *kobj,
				     struct attribute *attr,
				     char *buf)
{
	return ll_rd_track_id(kobj, buf, STATS_TRACK_PPID);
}

static ssize_t stats_track_ppid_store(struct kobject *kobj,
				      struct attribute *attr,
				      const char *buffer,
				      size_t count)
{
	return ll_wr_track_id(kobj, buffer, count, STATS_TRACK_PPID);
}
LUSTRE_RW_ATTR(stats_track_ppid);

static ssize_t stats_track_gid_show(struct kobject *kobj,
				    struct attribute *attr,
				    char *buf)
{
	return ll_rd_track_id(kobj, buf, STATS_TRACK_GID);
}

static ssize_t stats_track_gid_store(struct kobject *kobj,
				     struct attribute *attr,
				     const char *buffer,
				     size_t count)
{
	return ll_wr_track_id(kobj, buffer, count, STATS_TRACK_GID);
}
LUSTRE_RW_ATTR(stats_track_gid);

static ssize_t statahead_max_show(struct kobject *kobj,
				  struct attribute *attr,
				  char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%u\n", sbi->ll_sa_max);
}

static ssize_t statahead_max_store(struct kobject *kobj,
				   struct attribute *attr,
				   const char *buffer,
				   size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	if (val <= LL_SA_RPC_MAX)
		sbi->ll_sa_max = val;
	else
		CERROR("Bad statahead_max value %lu. Valid values are in the range [0, %d]\n",
		       val, LL_SA_RPC_MAX);

	return count;
}
LUSTRE_RW_ATTR(statahead_max);

static ssize_t statahead_agl_show(struct kobject *kobj,
				  struct attribute *attr,
				  char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%u\n", sbi->ll_flags & LL_SBI_AGL_ENABLED ? 1 : 0);
}

static ssize_t statahead_agl_store(struct kobject *kobj,
				   struct attribute *attr,
				   const char *buffer,
				   size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	if (val)
		sbi->ll_flags |= LL_SBI_AGL_ENABLED;
	else
		sbi->ll_flags &= ~LL_SBI_AGL_ENABLED;

	return count;
}
LUSTRE_RW_ATTR(statahead_agl);

static int ll_statahead_stats_seq_show(struct seq_file *m, void *v)
{
	struct super_block *sb = m->private;
	struct ll_sb_info *sbi = ll_s2sbi(sb);

	seq_printf(m,
		   "statahead total: %u\n"
		   "statahead wrong: %u\n"
		   "agl total: %u\n",
		   atomic_read(&sbi->ll_sa_total),
		   atomic_read(&sbi->ll_sa_wrong),
		   atomic_read(&sbi->ll_agl_total));
	return 0;
}
LPROC_SEQ_FOPS_RO(ll_statahead_stats);

static ssize_t lazystatfs_show(struct kobject *kobj,
			       struct attribute *attr,
			       char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%u\n", sbi->ll_flags & LL_SBI_LAZYSTATFS ? 1 : 0);
}

static ssize_t lazystatfs_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buffer,
				size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	if (val)
		sbi->ll_flags |= LL_SBI_LAZYSTATFS;
	else
		sbi->ll_flags &= ~LL_SBI_LAZYSTATFS;

	return count;
}
LUSTRE_RW_ATTR(lazystatfs);

static ssize_t max_easize_show(struct kobject *kobj,
			       struct attribute *attr,
			       char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	unsigned int ealen;
	int rc;

	rc = ll_get_max_mdsize(sbi, &ealen);
	if (rc)
		return rc;

	return sprintf(buf, "%u\n", ealen);
}
LUSTRE_RO_ATTR(max_easize);

static ssize_t default_easize_show(struct kobject *kobj,
				   struct attribute *attr,
				   char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	unsigned int ealen;
	int rc;

	rc = ll_get_default_mdsize(sbi, &ealen);
	if (rc)
		return rc;

	return sprintf(buf, "%u\n", ealen);
}
LUSTRE_RO_ATTR(default_easize);

static int ll_sbi_flags_seq_show(struct seq_file *m, void *v)
{
	const char *str[] = LL_SBI_FLAGS;
	struct super_block *sb = m->private;
	int flags = ll_s2sbi(sb)->ll_flags;
	int i = 0;

	while (flags != 0) {
		if (ARRAY_SIZE(str) <= i) {
			CERROR("%s: Revise array LL_SBI_FLAGS to match sbi flags please.\n",
			       ll_get_fsname(sb, NULL, 0));
			return -EINVAL;
		}

		if (flags & 0x1)
			seq_printf(m, "%s ", str[i]);
		flags >>= 1;
		++i;
	}
	seq_printf(m, "\b\n");
	return 0;
}
LPROC_SEQ_FOPS_RO(ll_sbi_flags);

static ssize_t xattr_cache_show(struct kobject *kobj,
				struct attribute *attr,
				char *buf)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);

	return sprintf(buf, "%u\n", sbi->ll_xattr_cache_enabled);
}

static ssize_t xattr_cache_store(struct kobject *kobj,
				 struct attribute *attr,
				 const char *buffer,
				 size_t count)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	if (val != 0 && val != 1)
		return -ERANGE;

	if (val == 1 && !(sbi->ll_flags & LL_SBI_XATTR_CACHE))
		return -ENOTSUPP;

	sbi->ll_xattr_cache_enabled = val;

	return count;
}
LUSTRE_RW_ATTR(xattr_cache);

static struct lprocfs_vars lprocfs_llite_obd_vars[] = {
	/* { "mntpt_path",   ll_rd_path,	     0, 0 }, */
	{ "site",	  &ll_site_stats_fops,    NULL, 0 },
	/* { "filegroups",   lprocfs_rd_filegroups,  0, 0 }, */
	{ "max_cached_mb",    &ll_max_cached_mb_fops, NULL },
	{ "statahead_stats",  &ll_statahead_stats_fops, NULL, 0 },
	{ "sbi_flags",	      &ll_sbi_flags_fops, NULL, 0 },
	{ NULL }
};

#define MAX_STRING_SIZE 128

static struct attribute *llite_attrs[] = {
	&lustre_attr_blocksize.attr,
	&lustre_attr_kbytestotal.attr,
	&lustre_attr_kbytesfree.attr,
	&lustre_attr_kbytesavail.attr,
	&lustre_attr_filestotal.attr,
	&lustre_attr_filesfree.attr,
	&lustre_attr_client_type.attr,
	&lustre_attr_fstype.attr,
	&lustre_attr_uuid.attr,
	&lustre_attr_max_read_ahead_mb.attr,
	&lustre_attr_max_read_ahead_per_file_mb.attr,
	&lustre_attr_max_read_ahead_whole_mb.attr,
	&lustre_attr_checksum_pages.attr,
	&lustre_attr_stats_track_pid.attr,
	&lustre_attr_stats_track_ppid.attr,
	&lustre_attr_stats_track_gid.attr,
	&lustre_attr_statahead_max.attr,
	&lustre_attr_statahead_agl.attr,
	&lustre_attr_lazystatfs.attr,
	&lustre_attr_max_easize.attr,
	&lustre_attr_default_easize.attr,
	&lustre_attr_xattr_cache.attr,
	NULL,
};

static void llite_sb_release(struct kobject *kobj)
{
	struct ll_sb_info *sbi = container_of(kobj, struct ll_sb_info,
					      ll_kobj);
	complete(&sbi->ll_kobj_unregister);
}

static struct kobj_type llite_ktype = {
	.default_attrs	= llite_attrs,
	.sysfs_ops	= &lustre_sysfs_ops,
	.release	= llite_sb_release,
};

static const struct llite_file_opcode {
	__u32       opcode;
	__u32       type;
	const char *opname;
} llite_opcode_table[LPROC_LL_FILE_OPCODES] = {
	/* file operation */
	{ LPROC_LL_DIRTY_HITS,     LPROCFS_TYPE_REGS, "dirty_pages_hits" },
	{ LPROC_LL_DIRTY_MISSES,   LPROCFS_TYPE_REGS, "dirty_pages_misses" },
	{ LPROC_LL_READ_BYTES,     LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_BYTES,
				   "read_bytes" },
	{ LPROC_LL_WRITE_BYTES,    LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_BYTES,
				   "write_bytes" },
	{ LPROC_LL_BRW_READ,       LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
				   "brw_read" },
	{ LPROC_LL_BRW_WRITE,      LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
				   "brw_write" },
	{ LPROC_LL_OSC_READ,       LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_BYTES,
				   "osc_read" },
	{ LPROC_LL_OSC_WRITE,      LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_BYTES,
				   "osc_write" },
	{ LPROC_LL_IOCTL,	  LPROCFS_TYPE_REGS, "ioctl" },
	{ LPROC_LL_OPEN,	   LPROCFS_TYPE_REGS, "open" },
	{ LPROC_LL_RELEASE,	LPROCFS_TYPE_REGS, "close" },
	{ LPROC_LL_MAP,	    LPROCFS_TYPE_REGS, "mmap" },
	{ LPROC_LL_LLSEEK,	 LPROCFS_TYPE_REGS, "seek" },
	{ LPROC_LL_FSYNC,	  LPROCFS_TYPE_REGS, "fsync" },
	{ LPROC_LL_READDIR,	LPROCFS_TYPE_REGS, "readdir" },
	/* inode operation */
	{ LPROC_LL_SETATTR,	LPROCFS_TYPE_REGS, "setattr" },
	{ LPROC_LL_TRUNC,	  LPROCFS_TYPE_REGS, "truncate" },
	{ LPROC_LL_FLOCK,	  LPROCFS_TYPE_REGS, "flock" },
	{ LPROC_LL_GETATTR,	LPROCFS_TYPE_REGS, "getattr" },
	/* dir inode operation */
	{ LPROC_LL_CREATE,	 LPROCFS_TYPE_REGS, "create" },
	{ LPROC_LL_LINK,	   LPROCFS_TYPE_REGS, "link" },
	{ LPROC_LL_UNLINK,	 LPROCFS_TYPE_REGS, "unlink" },
	{ LPROC_LL_SYMLINK,	LPROCFS_TYPE_REGS, "symlink" },
	{ LPROC_LL_MKDIR,	  LPROCFS_TYPE_REGS, "mkdir" },
	{ LPROC_LL_RMDIR,	  LPROCFS_TYPE_REGS, "rmdir" },
	{ LPROC_LL_MKNOD,	  LPROCFS_TYPE_REGS, "mknod" },
	{ LPROC_LL_RENAME,	 LPROCFS_TYPE_REGS, "rename" },
	/* special inode operation */
	{ LPROC_LL_STAFS,	  LPROCFS_TYPE_REGS, "statfs" },
	{ LPROC_LL_ALLOC_INODE,    LPROCFS_TYPE_REGS, "alloc_inode" },
	{ LPROC_LL_SETXATTR,       LPROCFS_TYPE_REGS, "setxattr" },
	{ LPROC_LL_GETXATTR,       LPROCFS_TYPE_REGS, "getxattr" },
	{ LPROC_LL_GETXATTR_HITS,  LPROCFS_TYPE_REGS, "getxattr_hits" },
	{ LPROC_LL_LISTXATTR,      LPROCFS_TYPE_REGS, "listxattr" },
	{ LPROC_LL_REMOVEXATTR,    LPROCFS_TYPE_REGS, "removexattr" },
	{ LPROC_LL_INODE_PERM,     LPROCFS_TYPE_REGS, "inode_permission" },
};

void ll_stats_ops_tally(struct ll_sb_info *sbi, int op, int count)
{
	if (!sbi->ll_stats)
		return;
	if (sbi->ll_stats_track_type == STATS_TRACK_ALL)
		lprocfs_counter_add(sbi->ll_stats, op, count);
	else if (sbi->ll_stats_track_type == STATS_TRACK_PID &&
		 sbi->ll_stats_track_id == current->pid)
		lprocfs_counter_add(sbi->ll_stats, op, count);
	else if (sbi->ll_stats_track_type == STATS_TRACK_PPID &&
		 sbi->ll_stats_track_id == current->real_parent->pid)
		lprocfs_counter_add(sbi->ll_stats, op, count);
	else if (sbi->ll_stats_track_type == STATS_TRACK_GID &&
		 sbi->ll_stats_track_id ==
			from_kgid(&init_user_ns, current_gid()))
		lprocfs_counter_add(sbi->ll_stats, op, count);
}
EXPORT_SYMBOL(ll_stats_ops_tally);

static const char *ra_stat_string[] = {
	[RA_STAT_HIT] = "hits",
	[RA_STAT_MISS] = "misses",
	[RA_STAT_DISTANT_READPAGE] = "readpage not consecutive",
	[RA_STAT_MISS_IN_WINDOW] = "miss inside window",
	[RA_STAT_FAILED_GRAB_PAGE] = "failed grab_cache_page",
	[RA_STAT_FAILED_MATCH] = "failed lock match",
	[RA_STAT_DISCARDED] = "read but discarded",
	[RA_STAT_ZERO_LEN] = "zero length file",
	[RA_STAT_ZERO_WINDOW] = "zero size window",
	[RA_STAT_EOF] = "read-ahead to EOF",
	[RA_STAT_MAX_IN_FLIGHT] = "hit max r-a issue",
	[RA_STAT_WRONG_GRAB_PAGE] = "wrong page from grab_cache_page",
};

int ldebugfs_register_mountpoint(struct dentry *parent,
				 struct super_block *sb, char *osc, char *mdc)
{
	struct lustre_sb_info *lsi = s2lsi(sb);
	struct ll_sb_info *sbi = ll_s2sbi(sb);
	struct obd_device *obd;
	struct dentry *dir;
	char name[MAX_STRING_SIZE + 1], *ptr;
	int err, id, len, rc;


	name[MAX_STRING_SIZE] = '\0';

	LASSERT(sbi != NULL);
	LASSERT(mdc != NULL);
	LASSERT(osc != NULL);

	/* Get fsname */
	len = strlen(lsi->lsi_lmd->lmd_profile);
	ptr = strrchr(lsi->lsi_lmd->lmd_profile, '-');
	if (ptr && (strcmp(ptr, "-client") == 0))
		len -= 7;

	/* Mount info */
	snprintf(name, MAX_STRING_SIZE, "%.*s-%p", len,
		 lsi->lsi_lmd->lmd_profile, sb);

	dir = ldebugfs_register(name, parent, NULL, NULL);
	if (IS_ERR_OR_NULL(dir)) {
		err = dir ? PTR_ERR(dir) : -ENOMEM;
		sbi->ll_debugfs_entry = NULL;
		return err;
	}
	sbi->ll_debugfs_entry = dir;

	rc = ldebugfs_seq_create(sbi->ll_debugfs_entry, "dump_page_cache", 0444,
				 &vvp_dump_pgcache_file_ops, sbi);
	if (rc)
		CWARN("Error adding the dump_page_cache file\n");

	rc = ldebugfs_seq_create(sbi->ll_debugfs_entry, "extents_stats", 0644,
				 &ll_rw_extents_stats_fops, sbi);
	if (rc)
		CWARN("Error adding the extent_stats file\n");

	rc = ldebugfs_seq_create(sbi->ll_debugfs_entry,
				  "extents_stats_per_process",
				 0644, &ll_rw_extents_stats_pp_fops, sbi);
	if (rc)
		CWARN("Error adding the extents_stats_per_process file\n");

	rc = ldebugfs_seq_create(sbi->ll_debugfs_entry, "offset_stats", 0644,
				 &ll_rw_offset_stats_fops, sbi);
	if (rc)
		CWARN("Error adding the offset_stats file\n");

	/* File operations stats */
	sbi->ll_stats = lprocfs_alloc_stats(LPROC_LL_FILE_OPCODES,
					    LPROCFS_STATS_FLAG_NONE);
	if (sbi->ll_stats == NULL) {
		err = -ENOMEM;
		goto out;
	}
	/* do counter init */
	for (id = 0; id < LPROC_LL_FILE_OPCODES; id++) {
		__u32 type = llite_opcode_table[id].type;
		void *ptr = NULL;
		if (type & LPROCFS_TYPE_REGS)
			ptr = "regs";
		else if (type & LPROCFS_TYPE_BYTES)
			ptr = "bytes";
		else if (type & LPROCFS_TYPE_PAGES)
			ptr = "pages";
		lprocfs_counter_init(sbi->ll_stats,
				     llite_opcode_table[id].opcode,
				     (type & LPROCFS_CNTR_AVGMINMAX),
				     llite_opcode_table[id].opname, ptr);
	}
	err = ldebugfs_register_stats(sbi->ll_debugfs_entry, "stats",
				     sbi->ll_stats);
	if (err)
		goto out;

	sbi->ll_ra_stats = lprocfs_alloc_stats(ARRAY_SIZE(ra_stat_string),
					       LPROCFS_STATS_FLAG_NONE);
	if (sbi->ll_ra_stats == NULL) {
		err = -ENOMEM;
		goto out;
	}

	for (id = 0; id < ARRAY_SIZE(ra_stat_string); id++)
		lprocfs_counter_init(sbi->ll_ra_stats, id, 0,
				     ra_stat_string[id], "pages");

	err = ldebugfs_register_stats(sbi->ll_debugfs_entry, "read_ahead_stats",
				     sbi->ll_ra_stats);
	if (err)
		goto out;


	err = ldebugfs_add_vars(sbi->ll_debugfs_entry,
				lprocfs_llite_obd_vars, sb);
	if (err)
		goto out;

	sbi->ll_kobj.kset = llite_kset;
	init_completion(&sbi->ll_kobj_unregister);
	err = kobject_init_and_add(&sbi->ll_kobj, &llite_ktype, NULL,
				   "%s", name);
	if (err)
		goto out;

	/* MDC info */
	obd = class_name2obd(mdc);

	err = sysfs_create_link(&sbi->ll_kobj, &obd->obd_kobj,
				obd->obd_type->typ_name);
	if (err)
		goto out;

	/* OSC */
	obd = class_name2obd(osc);

	err = sysfs_create_link(&sbi->ll_kobj, &obd->obd_kobj,
				obd->obd_type->typ_name);
out:
	if (err) {
		ldebugfs_remove(&sbi->ll_debugfs_entry);
		lprocfs_free_stats(&sbi->ll_ra_stats);
		lprocfs_free_stats(&sbi->ll_stats);
	}
	return err;
}

void ldebugfs_unregister_mountpoint(struct ll_sb_info *sbi)
{
	if (sbi->ll_debugfs_entry) {
		ldebugfs_remove(&sbi->ll_debugfs_entry);
		kobject_put(&sbi->ll_kobj);
		wait_for_completion(&sbi->ll_kobj_unregister);
		lprocfs_free_stats(&sbi->ll_ra_stats);
		lprocfs_free_stats(&sbi->ll_stats);
	}
}
#undef MAX_STRING_SIZE

#define pct(a, b) (b ? a * 100 / b : 0)

static void ll_display_extents_info(struct ll_rw_extents_info *io_extents,
				   struct seq_file *seq, int which)
{
	unsigned long read_tot = 0, write_tot = 0, read_cum, write_cum;
	unsigned long start, end, r, w;
	char *unitp = "KMGTPEZY";
	int i, units = 10;
	struct per_process_info *pp_info = &io_extents->pp_extents[which];

	read_cum = 0;
	write_cum = 0;
	start = 0;

	for (i = 0; i < LL_HIST_MAX; i++) {
		read_tot += pp_info->pp_r_hist.oh_buckets[i];
		write_tot += pp_info->pp_w_hist.oh_buckets[i];
	}

	for (i = 0; i < LL_HIST_MAX; i++) {
		r = pp_info->pp_r_hist.oh_buckets[i];
		w = pp_info->pp_w_hist.oh_buckets[i];
		read_cum += r;
		write_cum += w;
		end = 1 << (i + LL_HIST_START - units);
		seq_printf(seq, "%4lu%c - %4lu%c%c: %14lu %4lu %4lu  | %14lu %4lu %4lu\n",
			   start, *unitp, end, *unitp,
			   (i == LL_HIST_MAX - 1) ? '+' : ' ',
			   r, pct(r, read_tot), pct(read_cum, read_tot),
			   w, pct(w, write_tot), pct(write_cum, write_tot));
		start = end;
		if (start == 1<<10) {
			start = 1;
			units += 10;
			unitp++;
		}
		if (read_cum == read_tot && write_cum == write_tot)
			break;
	}
}

static int ll_rw_extents_stats_pp_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct ll_sb_info *sbi = seq->private;
	struct ll_rw_extents_info *io_extents = &sbi->ll_rw_extents_info;
	int k;

	do_gettimeofday(&now);

	if (!sbi->ll_rw_stats_on) {
		seq_printf(seq, "disabled\n"
			   "write anything in this file to activate, then 0 or \"[D/d]isabled\" to deactivate\n");
		return 0;
	}
	seq_printf(seq, "snapshot_time:	 %lu.%lu (secs.usecs)\n",
		   now.tv_sec, (unsigned long)now.tv_usec);
	seq_printf(seq, "%15s %19s       | %20s\n", " ", "read", "write");
	seq_printf(seq, "%13s   %14s %4s %4s  | %14s %4s %4s\n",
		   "extents", "calls", "%", "cum%",
		   "calls", "%", "cum%");
	spin_lock(&sbi->ll_pp_extent_lock);
	for (k = 0; k < LL_PROCESS_HIST_MAX; k++) {
		if (io_extents->pp_extents[k].pid != 0) {
			seq_printf(seq, "\nPID: %d\n",
				   io_extents->pp_extents[k].pid);
			ll_display_extents_info(io_extents, seq, k);
		}
	}
	spin_unlock(&sbi->ll_pp_extent_lock);
	return 0;
}

static ssize_t ll_rw_extents_stats_pp_seq_write(struct file *file,
						const char __user *buf,
						size_t len,
						loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct ll_sb_info *sbi = seq->private;
	struct ll_rw_extents_info *io_extents = &sbi->ll_rw_extents_info;
	int i;
	int value = 1, rc = 0;

	if (len == 0)
		return -EINVAL;

	rc = lprocfs_write_helper(buf, len, &value);
	if (rc < 0 && len < 16) {
		char kernbuf[16];

		if (copy_from_user(kernbuf, buf, len))
			return -EFAULT;
		kernbuf[len] = 0;

		if (kernbuf[len - 1] == '\n')
			kernbuf[len - 1] = 0;

		if (strcmp(kernbuf, "disabled") == 0 ||
		    strcmp(kernbuf, "Disabled") == 0)
			value = 0;
	}

	if (value == 0)
		sbi->ll_rw_stats_on = 0;
	else
		sbi->ll_rw_stats_on = 1;

	spin_lock(&sbi->ll_pp_extent_lock);
	for (i = 0; i < LL_PROCESS_HIST_MAX; i++) {
		io_extents->pp_extents[i].pid = 0;
		lprocfs_oh_clear(&io_extents->pp_extents[i].pp_r_hist);
		lprocfs_oh_clear(&io_extents->pp_extents[i].pp_w_hist);
	}
	spin_unlock(&sbi->ll_pp_extent_lock);
	return len;
}

LPROC_SEQ_FOPS(ll_rw_extents_stats_pp);

static int ll_rw_extents_stats_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct ll_sb_info *sbi = seq->private;
	struct ll_rw_extents_info *io_extents = &sbi->ll_rw_extents_info;

	do_gettimeofday(&now);

	if (!sbi->ll_rw_stats_on) {
		seq_printf(seq, "disabled\n"
			   "write anything in this file to activate, then 0 or \"[D/d]isabled\" to deactivate\n");
		return 0;
	}
	seq_printf(seq, "snapshot_time:	 %lu.%lu (secs.usecs)\n",
		   now.tv_sec, (unsigned long)now.tv_usec);

	seq_printf(seq, "%15s %19s       | %20s\n", " ", "read", "write");
	seq_printf(seq, "%13s   %14s %4s %4s  | %14s %4s %4s\n",
		   "extents", "calls", "%", "cum%",
		   "calls", "%", "cum%");
	spin_lock(&sbi->ll_lock);
	ll_display_extents_info(io_extents, seq, LL_PROCESS_HIST_MAX);
	spin_unlock(&sbi->ll_lock);

	return 0;
}

static ssize_t ll_rw_extents_stats_seq_write(struct file *file,
					     const char __user *buf,
					     size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct ll_sb_info *sbi = seq->private;
	struct ll_rw_extents_info *io_extents = &sbi->ll_rw_extents_info;
	int i;
	int value = 1, rc = 0;

	if (len == 0)
		return -EINVAL;

	rc = lprocfs_write_helper(buf, len, &value);
	if (rc < 0 && len < 16) {
		char kernbuf[16];

		if (copy_from_user(kernbuf, buf, len))
			return -EFAULT;
		kernbuf[len] = 0;

		if (kernbuf[len - 1] == '\n')
			kernbuf[len - 1] = 0;

		if (strcmp(kernbuf, "disabled") == 0 ||
		    strcmp(kernbuf, "Disabled") == 0)
			value = 0;
	}

	if (value == 0)
		sbi->ll_rw_stats_on = 0;
	else
		sbi->ll_rw_stats_on = 1;

	spin_lock(&sbi->ll_pp_extent_lock);
	for (i = 0; i <= LL_PROCESS_HIST_MAX; i++) {
		io_extents->pp_extents[i].pid = 0;
		lprocfs_oh_clear(&io_extents->pp_extents[i].pp_r_hist);
		lprocfs_oh_clear(&io_extents->pp_extents[i].pp_w_hist);
	}
	spin_unlock(&sbi->ll_pp_extent_lock);

	return len;
}
LPROC_SEQ_FOPS(ll_rw_extents_stats);

void ll_rw_stats_tally(struct ll_sb_info *sbi, pid_t pid,
		       struct ll_file_data *file, loff_t pos,
		       size_t count, int rw)
{
	int i, cur = -1;
	struct ll_rw_process_info *process;
	struct ll_rw_process_info *offset;
	int *off_count = &sbi->ll_rw_offset_entry_count;
	int *process_count = &sbi->ll_offset_process_count;
	struct ll_rw_extents_info *io_extents = &sbi->ll_rw_extents_info;

	if (!sbi->ll_rw_stats_on)
		return;
	process = sbi->ll_rw_process_info;
	offset = sbi->ll_rw_offset_info;

	spin_lock(&sbi->ll_pp_extent_lock);
	/* Extent statistics */
	for (i = 0; i < LL_PROCESS_HIST_MAX; i++) {
		if (io_extents->pp_extents[i].pid == pid) {
			cur = i;
			break;
		}
	}

	if (cur == -1) {
		/* new process */
		sbi->ll_extent_process_count =
			(sbi->ll_extent_process_count + 1) % LL_PROCESS_HIST_MAX;
		cur = sbi->ll_extent_process_count;
		io_extents->pp_extents[cur].pid = pid;
		lprocfs_oh_clear(&io_extents->pp_extents[cur].pp_r_hist);
		lprocfs_oh_clear(&io_extents->pp_extents[cur].pp_w_hist);
	}

	for(i = 0; (count >= (1 << LL_HIST_START << i)) &&
	     (i < (LL_HIST_MAX - 1)); i++);
	if (rw == 0) {
		io_extents->pp_extents[cur].pp_r_hist.oh_buckets[i]++;
		io_extents->pp_extents[LL_PROCESS_HIST_MAX].pp_r_hist.oh_buckets[i]++;
	} else {
		io_extents->pp_extents[cur].pp_w_hist.oh_buckets[i]++;
		io_extents->pp_extents[LL_PROCESS_HIST_MAX].pp_w_hist.oh_buckets[i]++;
	}
	spin_unlock(&sbi->ll_pp_extent_lock);

	spin_lock(&sbi->ll_process_lock);
	/* Offset statistics */
	for (i = 0; i < LL_PROCESS_HIST_MAX; i++) {
		if (process[i].rw_pid == pid) {
			if (process[i].rw_last_file != file) {
				process[i].rw_range_start = pos;
				process[i].rw_last_file_pos = pos + count;
				process[i].rw_smallest_extent = count;
				process[i].rw_largest_extent = count;
				process[i].rw_offset = 0;
				process[i].rw_last_file = file;
				spin_unlock(&sbi->ll_process_lock);
				return;
			}
			if (process[i].rw_last_file_pos != pos) {
				*off_count =
				    (*off_count + 1) % LL_OFFSET_HIST_MAX;
				offset[*off_count].rw_op = process[i].rw_op;
				offset[*off_count].rw_pid = pid;
				offset[*off_count].rw_range_start =
					process[i].rw_range_start;
				offset[*off_count].rw_range_end =
					process[i].rw_last_file_pos;
				offset[*off_count].rw_smallest_extent =
					process[i].rw_smallest_extent;
				offset[*off_count].rw_largest_extent =
					process[i].rw_largest_extent;
				offset[*off_count].rw_offset =
					process[i].rw_offset;
				process[i].rw_op = rw;
				process[i].rw_range_start = pos;
				process[i].rw_smallest_extent = count;
				process[i].rw_largest_extent = count;
				process[i].rw_offset = pos -
					process[i].rw_last_file_pos;
			}
			if (process[i].rw_smallest_extent > count)
				process[i].rw_smallest_extent = count;
			if (process[i].rw_largest_extent < count)
				process[i].rw_largest_extent = count;
			process[i].rw_last_file_pos = pos + count;
			spin_unlock(&sbi->ll_process_lock);
			return;
		}
	}
	*process_count = (*process_count + 1) % LL_PROCESS_HIST_MAX;
	process[*process_count].rw_pid = pid;
	process[*process_count].rw_op = rw;
	process[*process_count].rw_range_start = pos;
	process[*process_count].rw_last_file_pos = pos + count;
	process[*process_count].rw_smallest_extent = count;
	process[*process_count].rw_largest_extent = count;
	process[*process_count].rw_offset = 0;
	process[*process_count].rw_last_file = file;
	spin_unlock(&sbi->ll_process_lock);
}

static int ll_rw_offset_stats_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct ll_sb_info *sbi = seq->private;
	struct ll_rw_process_info *offset = sbi->ll_rw_offset_info;
	struct ll_rw_process_info *process = sbi->ll_rw_process_info;
	int i;

	do_gettimeofday(&now);

	if (!sbi->ll_rw_stats_on) {
		seq_printf(seq, "disabled\n"
			   "write anything in this file to activate, then 0 or \"[D/d]isabled\" to deactivate\n");
		return 0;
	}
	spin_lock(&sbi->ll_process_lock);

	seq_printf(seq, "snapshot_time:	 %lu.%lu (secs.usecs)\n",
		   now.tv_sec, (unsigned long)now.tv_usec);
	seq_printf(seq, "%3s %10s %14s %14s %17s %17s %14s\n",
		   "R/W", "PID", "RANGE START", "RANGE END",
		   "SMALLEST EXTENT", "LARGEST EXTENT", "OFFSET");
	/* We stored the discontiguous offsets here; print them first */
	for (i = 0; i < LL_OFFSET_HIST_MAX; i++) {
		if (offset[i].rw_pid != 0)
			seq_printf(seq,
				   "%3c %10d %14Lu %14Lu %17lu %17lu %14Lu",
				   offset[i].rw_op == READ ? 'R' : 'W',
				   offset[i].rw_pid,
				   offset[i].rw_range_start,
				   offset[i].rw_range_end,
				   (unsigned long)offset[i].rw_smallest_extent,
				   (unsigned long)offset[i].rw_largest_extent,
				   offset[i].rw_offset);
	}
	/* Then print the current offsets for each process */
	for (i = 0; i < LL_PROCESS_HIST_MAX; i++) {
		if (process[i].rw_pid != 0)
			seq_printf(seq,
				   "%3c %10d %14Lu %14Lu %17lu %17lu %14Lu",
				   process[i].rw_op == READ ? 'R' : 'W',
				   process[i].rw_pid,
				   process[i].rw_range_start,
				   process[i].rw_last_file_pos,
				   (unsigned long)process[i].rw_smallest_extent,
				   (unsigned long)process[i].rw_largest_extent,
				   process[i].rw_offset);
	}
	spin_unlock(&sbi->ll_process_lock);

	return 0;
}

static ssize_t ll_rw_offset_stats_seq_write(struct file *file,
					    const char __user *buf,
					    size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct ll_sb_info *sbi = seq->private;
	struct ll_rw_process_info *process_info = sbi->ll_rw_process_info;
	struct ll_rw_process_info *offset_info = sbi->ll_rw_offset_info;
	int value = 1, rc = 0;

	if (len == 0)
		return -EINVAL;

	rc = lprocfs_write_helper(buf, len, &value);

	if (rc < 0 && len < 16) {
		char kernbuf[16];

		if (copy_from_user(kernbuf, buf, len))
			return -EFAULT;
		kernbuf[len] = 0;

		if (kernbuf[len - 1] == '\n')
			kernbuf[len - 1] = 0;

		if (strcmp(kernbuf, "disabled") == 0 ||
		    strcmp(kernbuf, "Disabled") == 0)
			value = 0;
	}

	if (value == 0)
		sbi->ll_rw_stats_on = 0;
	else
		sbi->ll_rw_stats_on = 1;

	spin_lock(&sbi->ll_process_lock);
	sbi->ll_offset_process_count = 0;
	sbi->ll_rw_offset_entry_count = 0;
	memset(process_info, 0, sizeof(struct ll_rw_process_info) *
	       LL_PROCESS_HIST_MAX);
	memset(offset_info, 0, sizeof(struct ll_rw_process_info) *
	       LL_OFFSET_HIST_MAX);
	spin_unlock(&sbi->ll_process_lock);

	return len;
}

LPROC_SEQ_FOPS(ll_rw_offset_stats);

void lprocfs_llite_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->obd_vars     = lprocfs_llite_obd_vars;
}
