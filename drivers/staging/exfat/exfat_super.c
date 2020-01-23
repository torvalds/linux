// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/aio.h>
#include <linux/iversion.h>
#include <linux/parser.h>
#include <linux/uio.h>
#include <linux/writeback.h>
#include <linux/log2.h>
#include <linux/hash.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/mutex.h>
#include <linux/swap.h>

#define EXFAT_VERSION  "1.3.0"

#include "exfat.h"

static struct kmem_cache *exfat_inode_cachep;

static int exfat_default_codepage = CONFIG_STAGING_EXFAT_DEFAULT_CODEPAGE;
static char exfat_default_iocharset[] = CONFIG_STAGING_EXFAT_DEFAULT_IOCHARSET;

#define INC_IVERSION(x) (inode_inc_iversion(x))
#define GET_IVERSION(x) (inode_peek_iversion_raw(x))
#define SET_IVERSION(x, y) (inode_set_iversion(x, y))

static struct inode *exfat_iget(struct super_block *sb, loff_t i_pos);
static int exfat_sync_inode(struct inode *inode);
static struct inode *exfat_build_inode(struct super_block *sb,
				       struct file_id_t *fid, loff_t i_pos);
static int exfat_write_inode(struct inode *inode,
			     struct writeback_control *wbc);
static void exfat_write_super(struct super_block *sb);

#define UNIX_SECS_1980    315532800L
#define UNIX_SECS_2108    4354819200L

/* Convert a FAT time/date pair to a UNIX date (seconds since 1 1 70). */
static void exfat_time_fat2unix(struct timespec64 *ts, struct date_time_t *tp)
{
	ts->tv_sec = mktime64(tp->Year + 1980, tp->Month + 1, tp->Day,
			      tp->Hour, tp->Minute, tp->Second);

	ts->tv_nsec = tp->MilliSecond * NSEC_PER_MSEC;
}

/* Convert linear UNIX date to a FAT time/date pair. */
static void exfat_time_unix2fat(struct timespec64 *ts, struct date_time_t *tp)
{
	time64_t second = ts->tv_sec;
	struct tm tm;

	time64_to_tm(second, 0, &tm);

	if (second < UNIX_SECS_1980) {
		tp->MilliSecond = 0;
		tp->Second	= 0;
		tp->Minute	= 0;
		tp->Hour	= 0;
		tp->Day		= 1;
		tp->Month	= 1;
		tp->Year	= 0;
		return;
	}

	if (second >= UNIX_SECS_2108) {
		tp->MilliSecond = 999;
		tp->Second	= 59;
		tp->Minute	= 59;
		tp->Hour	= 23;
		tp->Day		= 31;
		tp->Month	= 12;
		tp->Year	= 127;
		return;
	}

	tp->MilliSecond = ts->tv_nsec / NSEC_PER_MSEC;
	tp->Second	= tm.tm_sec;
	tp->Minute	= tm.tm_min;
	tp->Hour	= tm.tm_hour;
	tp->Day		= tm.tm_mday;
	tp->Month	= tm.tm_mon + 1;
	tp->Year	= tm.tm_year + 1900 - 1980;
}

struct timestamp_t *tm_current(struct timestamp_t *tp)
{
	time64_t second = ktime_get_real_seconds();
	struct tm tm;

	time64_to_tm(second, 0, &tm);

	if (second < UNIX_SECS_1980) {
		tp->sec  = 0;
		tp->min  = 0;
		tp->hour = 0;
		tp->day  = 1;
		tp->mon  = 1;
		tp->year = 0;
		return tp;
	}

	if (second >= UNIX_SECS_2108) {
		tp->sec  = 59;
		tp->min  = 59;
		tp->hour = 23;
		tp->day  = 31;
		tp->mon  = 12;
		tp->year = 127;
		return tp;
	}

	tp->sec  = tm.tm_sec;
	tp->min  = tm.tm_min;
	tp->hour = tm.tm_hour;
	tp->day  = tm.tm_mday;
	tp->mon  = tm.tm_mon + 1;
	tp->year = tm.tm_year + 1900 - 1980;

	return tp;
}

static void __lock_super(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	mutex_lock(&sbi->s_lock);
}

static void __unlock_super(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	mutex_unlock(&sbi->s_lock);
}

static int __is_sb_dirty(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	return sbi->s_dirt;
}

static void __set_sb_clean(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	sbi->s_dirt = 0;
}

static int __exfat_revalidate(struct dentry *dentry)
{
	return 0;
}

static int exfat_revalidate(struct dentry *dentry, unsigned int flags)
{
	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (dentry->d_inode)
		return 1;
	return __exfat_revalidate(dentry);
}

static int exfat_revalidate_ci(struct dentry *dentry, unsigned int flags)
{
	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (dentry->d_inode)
		return 1;

	if (!flags)
		return 0;

	if (flags & (LOOKUP_CREATE | LOOKUP_RENAME_TARGET))
		return 0;

	return __exfat_revalidate(dentry);
}

static unsigned int __exfat_striptail_len(unsigned int len, const char *name)
{
	while (len && name[len - 1] == '.')
		len--;
	return len;
}

static unsigned int exfat_striptail_len(const struct qstr *qstr)
{
	return __exfat_striptail_len(qstr->len, qstr->name);
}

static int exfat_d_hash(const struct dentry *dentry, struct qstr *qstr)
{
	qstr->hash = full_name_hash(dentry, qstr->name,
				    exfat_striptail_len(qstr));
	return 0;
}

static int exfat_d_hashi(const struct dentry *dentry, struct qstr *qstr)
{
	struct super_block *sb = dentry->d_sb;
	const unsigned char *name;
	unsigned int len;
	unsigned long hash;

	name = qstr->name;
	len = exfat_striptail_len(qstr);

	hash = init_name_hash(dentry);
	while (len--)
		hash = partial_name_hash(nls_upper(sb, *name++), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

static int exfat_cmpi(const struct dentry *dentry, unsigned int len,
		      const char *str, const struct qstr *name)
{
	struct nls_table *t = EXFAT_SB(dentry->d_sb)->nls_io;
	unsigned int alen, blen;

	alen = exfat_striptail_len(name);
	blen = __exfat_striptail_len(len, str);
	if (alen == blen) {
		if (!t) {
			if (strncasecmp(name->name, str, alen) == 0)
				return 0;
		} else {
			if (nls_strnicmp(t, name->name, str, alen) == 0)
				return 0;
		}
	}
	return 1;
}

static int exfat_cmp(const struct dentry *dentry, unsigned int len,
		     const char *str, const struct qstr *name)
{
	unsigned int alen, blen;

	alen = exfat_striptail_len(name);
	blen = __exfat_striptail_len(len, str);
	if (alen == blen) {
		if (strncmp(name->name, str, alen) == 0)
			return 0;
	}
	return 1;
}

static const struct dentry_operations exfat_ci_dentry_ops = {
	.d_revalidate   = exfat_revalidate_ci,
	.d_hash         = exfat_d_hashi,
	.d_compare      = exfat_cmpi,
};

static const struct dentry_operations exfat_dentry_ops = {
	.d_revalidate   = exfat_revalidate,
	.d_hash         = exfat_d_hash,
	.d_compare      = exfat_cmp,
};

static DEFINE_MUTEX(z_mutex);

static inline void fs_sync(struct super_block *sb, bool do_sync)
{
	if (do_sync)
		exfat_bdev_sync(sb);
}

/*
 * If ->i_mode can't hold S_IWUGO (i.e. ATTR_RO), we use ->i_attrs to
 * save ATTR_RO instead of ->i_mode.
 *
 * If it's directory and !sbi->options.rodir, ATTR_RO isn't read-only
 * bit, it's just used as flag for app.
 */
static inline int exfat_mode_can_hold_ro(struct inode *inode)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);

	if (S_ISDIR(inode->i_mode))
		return 0;

	if ((~sbi->options.fs_fmask) & 0222)
		return 1;
	return 0;
}

/* Convert attribute bits and a mask to the UNIX mode. */
static inline mode_t exfat_make_mode(struct exfat_sb_info *sbi, u32 attr,
				     mode_t mode)
{
	if ((attr & ATTR_READONLY) && !(attr & ATTR_SUBDIR))
		mode &= ~0222;

	if (attr & ATTR_SUBDIR)
		return (mode & ~sbi->options.fs_dmask) | S_IFDIR;
	else if (attr & ATTR_SYMLINK)
		return (mode & ~sbi->options.fs_dmask) | S_IFLNK;
	else
		return (mode & ~sbi->options.fs_fmask) | S_IFREG;
}

/* Return the FAT attribute byte for this inode */
static inline u32 exfat_make_attr(struct inode *inode)
{
	if (exfat_mode_can_hold_ro(inode) && !(inode->i_mode & 0222))
		return (EXFAT_I(inode)->fid.attr) | ATTR_READONLY;
	else
		return EXFAT_I(inode)->fid.attr;
}

static inline void exfat_save_attr(struct inode *inode, u32 attr)
{
	if (exfat_mode_can_hold_ro(inode))
		EXFAT_I(inode)->fid.attr = attr & ATTR_RWMASK;
	else
		EXFAT_I(inode)->fid.attr = attr & (ATTR_RWMASK | ATTR_READONLY);
}

static int ffsMountVol(struct super_block *sb)
{
	int i, ret;
	struct pbr_sector_t *p_pbr;
	struct buffer_head *tmp_bh = NULL;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	pr_info("[EXFAT] trying to mount...\n");

	mutex_lock(&z_mutex);

	exfat_buf_init(sb);

	mutex_init(&p_fs->v_mutex);
	p_fs->dev_ejected = 0;

	/* open the block device */
	exfat_bdev_open(sb);

	if (p_bd->sector_size < sb->s_blocksize) {
		printk(KERN_INFO "EXFAT: mount failed - sector size %d less than blocksize %ld\n",
		       p_bd->sector_size,  sb->s_blocksize);
		ret = -EINVAL;
		goto out;
	}
	if (p_bd->sector_size > sb->s_blocksize)
		sb_set_blocksize(sb, p_bd->sector_size);

	/* read Sector 0 */
	if (sector_read(sb, 0, &tmp_bh, 1) != 0) {
		ret = -EIO;
		goto out;
	}

	p_fs->PBR_sector = 0;

	p_pbr = (struct pbr_sector_t *)tmp_bh->b_data;

	/* check the validity of PBR */
	if (GET16_A(p_pbr->signature) != PBR_SIGNATURE) {
		brelse(tmp_bh);
		exfat_bdev_close(sb);
		ret = -EFSCORRUPTED;
		goto out;
	}

	/* fill fs_struct */
	for (i = 0; i < 53; i++)
		if (p_pbr->bpb[i])
			break;

	if (i < 53) {
		/* Not sure how we'd get here, but complain if it does */
		ret = -EINVAL;
		pr_info("EXFAT: Attempted to mount VFAT filesystem\n");
		goto out;
	} else {
		ret = exfat_mount(sb, p_pbr);
	}

	brelse(tmp_bh);

	if (ret) {
		exfat_bdev_close(sb);
		goto out;
	}

	ret = load_alloc_bitmap(sb);
	if (ret) {
		exfat_bdev_close(sb);
		goto out;
	}
	ret = load_upcase_table(sb);
	if (ret) {
		free_alloc_bitmap(sb);
		exfat_bdev_close(sb);
		goto out;
	}

	if (p_fs->dev_ejected) {
		free_upcase_table(sb);
		free_alloc_bitmap(sb);
		exfat_bdev_close(sb);
		ret = -EIO;
		goto out;
	}

	pr_info("[EXFAT] mounted successfully\n");

out:
	mutex_unlock(&z_mutex);

	return ret;
}

static int ffsUmountVol(struct super_block *sb)
{
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	int err = 0;

	pr_info("[EXFAT] trying to unmount...\n");

	mutex_lock(&z_mutex);

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);

	free_upcase_table(sb);
	free_alloc_bitmap(sb);

	exfat_fat_release_all(sb);
	exfat_buf_release_all(sb);

	/* close the block device */
	exfat_bdev_close(sb);

	if (p_fs->dev_ejected) {
		pr_info("[EXFAT] unmounted with media errors. Device is already ejected.\n");
		err = -EIO;
	}

	exfat_buf_shutdown(sb);

	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);
	mutex_unlock(&z_mutex);

	pr_info("[EXFAT] unmounted successfully\n");

	return err;
}

static int ffsGetVolInfo(struct super_block *sb, struct vol_info_t *info)
{
	int err = 0;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if (!info)
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	if (p_fs->used_clusters == UINT_MAX)
		p_fs->used_clusters = exfat_count_used_clusters(sb);

	info->FatType = p_fs->vol_type;
	info->ClusterSize = p_fs->cluster_size;
	info->NumClusters = p_fs->num_clusters - 2; /* clu 0 & 1 */
	info->UsedClusters = p_fs->used_clusters;
	info->FreeClusters = info->NumClusters - info->UsedClusters;

	if (p_fs->dev_ejected)
		err = -EIO;

	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return err;
}

static int ffsSyncVol(struct super_block *sb, bool do_sync)
{
	int err = 0;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* synchronize the file system */
	fs_sync(sb, do_sync);
	fs_set_vol_flags(sb, VOL_CLEAN);

	if (p_fs->dev_ejected)
		err = -EIO;

	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return err;
}

/*----------------------------------------------------------------------*/
/*  File Operation Functions                                            */
/*----------------------------------------------------------------------*/

static int ffsLookupFile(struct inode *inode, char *path, struct file_id_t *fid)
{
	int ret, dentry, num_entries;
	struct chain_t dir;
	struct uni_name_t uni_name;
	struct dos_name_t dos_name;
	struct dentry_t *ep, *ep2;
	struct entry_set_cache_t *es = NULL;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	pr_debug("%s entered\n", __func__);

	/* check the validity of pointer parameters */
	if (!fid || !path || (*path == '\0'))
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* check the validity of directory name in the given pathname */
	ret = resolve_path(inode, path, &dir, &uni_name);
	if (ret)
		goto out;

	ret = get_num_entries_and_dos_name(sb, &dir, &uni_name, &num_entries,
					   &dos_name);
	if (ret)
		goto out;

	/* search the file name for directories */
	dentry = exfat_find_dir_entry(sb, &dir, &uni_name, num_entries,
				      &dos_name, TYPE_ALL);
	if (dentry < -1) {
		ret = -ENOENT;
		goto out;
	}

	fid->dir.dir = dir.dir;
	fid->dir.size = dir.size;
	fid->dir.flags = dir.flags;
	fid->entry = dentry;

	if (dentry == -1) {
		fid->type = TYPE_DIR;
		fid->rwoffset = 0;
		fid->hint_last_off = -1;

		fid->attr = ATTR_SUBDIR;
		fid->flags = 0x01;
		fid->size = 0;
		fid->start_clu = p_fs->root_dir;
	} else {
		es = get_entry_set_in_dir(sb, &dir, dentry,
					  ES_2_ENTRIES, &ep);
		if (!es) {
			ret =  -ENOENT;
			goto out;
		}
		ep2 = ep + 1;

		fid->type = exfat_get_entry_type(ep);
		fid->rwoffset = 0;
		fid->hint_last_off = -1;
		fid->attr = exfat_get_entry_attr(ep);

		fid->size = exfat_get_entry_size(ep2);
		if ((fid->type == TYPE_FILE) && (fid->size == 0)) {
			fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
			fid->start_clu = CLUSTER_32(~0);
		} else {
			fid->flags = exfat_get_entry_flag(ep2);
			fid->start_clu = exfat_get_entry_clu0(ep2);
		}

		release_entry_set(es);
	}

	if (p_fs->dev_ejected)
		ret = -EIO;
out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsCreateFile(struct inode *inode, char *path, u8 mode,
			 struct file_id_t *fid)
{
	struct chain_t dir;
	struct uni_name_t uni_name;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	int ret = 0;

	/* check the validity of pointer parameters */
	if (!fid || !path || (*path == '\0'))
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* check the validity of directory name in the given pathname */
	ret = resolve_path(inode, path, &dir, &uni_name);
	if (ret)
		goto out;

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* create a new file */
	ret = create_file(inode, &dir, &uni_name, mode, fid);

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsReadFile(struct inode *inode, struct file_id_t *fid, void *buffer,
		       u64 count, u64 *rcount)
{
	s32 offset, sec_offset, clu_offset;
	u32 clu;
	int ret = 0;
	sector_t LogSector;
	u64 oneblkread, read_bytes;
	struct buffer_head *tmp_bh = NULL;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	/* check the validity of the given file id */
	if (!fid)
		return -EINVAL;

	/* check the validity of pointer parameters */
	if (!buffer)
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* check if the given file ID is opened */
	if (fid->type != TYPE_FILE) {
		ret = -EPERM;
		goto out;
	}

	if (fid->rwoffset > fid->size)
		fid->rwoffset = fid->size;

	if (count > (fid->size - fid->rwoffset))
		count = fid->size - fid->rwoffset;

	if (count == 0) {
		if (rcount)
			*rcount = 0;
		ret = 0;
		goto out;
	}

	read_bytes = 0;

	while (count > 0) {
		clu_offset = (s32)(fid->rwoffset >> p_fs->cluster_size_bits);
		clu = fid->start_clu;

		if (fid->flags == 0x03) {
			clu += clu_offset;
		} else {
			/* hint information */
			if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
			    (clu_offset >= fid->hint_last_off)) {
				clu_offset -= fid->hint_last_off;
				clu = fid->hint_last_clu;
			}

			while (clu_offset > 0) {
				/* clu = exfat_fat_read(sb, clu); */
				if (exfat_fat_read(sb, clu, &clu) == -1) {
					ret = -EIO;
					goto out;
				}

				clu_offset--;
			}
		}

		/* hint information */
		fid->hint_last_off = (s32)(fid->rwoffset >>
					   p_fs->cluster_size_bits);
		fid->hint_last_clu = clu;

		/* byte offset in cluster */
		offset = (s32)(fid->rwoffset & (p_fs->cluster_size - 1));

		/* sector offset in cluster */
		sec_offset = offset >> p_bd->sector_size_bits;

		/* byte offset in sector */
		offset &= p_bd->sector_size_mask;

		LogSector = START_SECTOR(clu) + sec_offset;

		oneblkread = (u64)(p_bd->sector_size - offset);
		if (oneblkread > count)
			oneblkread = count;

		if ((offset == 0) && (oneblkread == p_bd->sector_size)) {
			if (sector_read(sb, LogSector, &tmp_bh, 1) !=
			    0)
				goto err_out;
			memcpy((char *)buffer + read_bytes,
			       (char *)tmp_bh->b_data, (s32)oneblkread);
		} else {
			if (sector_read(sb, LogSector, &tmp_bh, 1) !=
			    0)
				goto err_out;
			memcpy((char *)buffer + read_bytes,
			       (char *)tmp_bh->b_data + offset,
			       (s32)oneblkread);
		}
		count -= oneblkread;
		read_bytes += oneblkread;
		fid->rwoffset += oneblkread;
	}
	brelse(tmp_bh);

/* How did this ever work and not leak a brlse()?? */
err_out:
	/* set the size of read bytes */
	if (rcount)
		*rcount = read_bytes;

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsWriteFile(struct inode *inode, struct file_id_t *fid,
			void *buffer, u64 count, u64 *wcount)
{
	bool modified = false;
	s32 offset, sec_offset, clu_offset;
	s32 num_clusters, num_alloc, num_alloced = (s32)~0;
	int ret = 0;
	u32 clu, last_clu;
	sector_t LogSector;
	u64 oneblkwrite, write_bytes;
	struct chain_t new_clu;
	struct timestamp_t tm;
	struct dentry_t *ep, *ep2;
	struct entry_set_cache_t *es = NULL;
	struct buffer_head *tmp_bh = NULL;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	/* check the validity of the given file id */
	if (!fid)
		return -EINVAL;

	/* check the validity of pointer parameters */
	if (!buffer)
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* check if the given file ID is opened */
	if (fid->type != TYPE_FILE) {
		ret = -EPERM;
		goto out;
	}

	if (fid->rwoffset > fid->size)
		fid->rwoffset = fid->size;

	if (count == 0) {
		if (wcount)
			*wcount = 0;
		ret = 0;
		goto out;
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	if (fid->size == 0)
		num_clusters = 0;
	else
		num_clusters = (s32)((fid->size - 1) >>
				     p_fs->cluster_size_bits) + 1;

	write_bytes = 0;

	while (count > 0) {
		clu_offset = (s32)(fid->rwoffset >> p_fs->cluster_size_bits);
		clu = fid->start_clu;
		last_clu = fid->start_clu;

		if (fid->flags == 0x03) {
			if ((clu_offset > 0) && (clu != CLUSTER_32(~0))) {
				last_clu += clu_offset - 1;

				if (clu_offset == num_clusters)
					clu = CLUSTER_32(~0);
				else
					clu += clu_offset;
			}
		} else {
			/* hint information */
			if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
			    (clu_offset >= fid->hint_last_off)) {
				clu_offset -= fid->hint_last_off;
				clu = fid->hint_last_clu;
			}

			while ((clu_offset > 0) && (clu != CLUSTER_32(~0))) {
				last_clu = clu;
				/* clu = exfat_fat_read(sb, clu); */
				if (exfat_fat_read(sb, clu, &clu) == -1) {
					ret = -EIO;
					goto out;
				}
				clu_offset--;
			}
		}

		if (clu == CLUSTER_32(~0)) {
			num_alloc = (s32)((count - 1) >>
					  p_fs->cluster_size_bits) + 1;
			new_clu.dir = (last_clu == CLUSTER_32(~0)) ?
					CLUSTER_32(~0) : last_clu + 1;
			new_clu.size = 0;
			new_clu.flags = fid->flags;

			/* (1) allocate a chain of clusters */
			num_alloced = exfat_alloc_cluster(sb,
							  num_alloc,
							  &new_clu);
			if (num_alloced == 0)
				break;
			if (num_alloced < 0) {
				ret = num_alloced;
				goto out;
			}

			/* (2) append to the FAT chain */
			if (last_clu == CLUSTER_32(~0)) {
				if (new_clu.flags == 0x01)
					fid->flags = 0x01;
				fid->start_clu = new_clu.dir;
				modified = true;
			} else {
				if (new_clu.flags != fid->flags) {
					exfat_chain_cont_cluster(sb,
								 fid->start_clu,
								 num_clusters);
					fid->flags = 0x01;
					modified = true;
				}
				if (new_clu.flags == 0x01)
					exfat_fat_write(sb, last_clu, new_clu.dir);
			}

			num_clusters += num_alloced;
			clu = new_clu.dir;
		}

		/* hint information */
		fid->hint_last_off = (s32)(fid->rwoffset >>
					   p_fs->cluster_size_bits);
		fid->hint_last_clu = clu;

		/* byte offset in cluster   */
		offset = (s32)(fid->rwoffset & (p_fs->cluster_size - 1));

		/* sector offset in cluster */
		sec_offset = offset >> p_bd->sector_size_bits;

		/* byte offset in sector    */
		offset &= p_bd->sector_size_mask;

		LogSector = START_SECTOR(clu) + sec_offset;

		oneblkwrite = (u64)(p_bd->sector_size - offset);
		if (oneblkwrite > count)
			oneblkwrite = count;

		if ((offset == 0) && (oneblkwrite == p_bd->sector_size)) {
			if (sector_read(sb, LogSector, &tmp_bh, 0) !=
			    0)
				goto err_out;
			memcpy((char *)tmp_bh->b_data,
			       (char *)buffer + write_bytes, (s32)oneblkwrite);
			if (sector_write(sb, LogSector, tmp_bh, 0) !=
			    0) {
				brelse(tmp_bh);
				goto err_out;
			}
		} else {
			if ((offset > 0) ||
			    ((fid->rwoffset + oneblkwrite) < fid->size)) {
				if (sector_read(sb, LogSector, &tmp_bh, 1) !=
				    0)
					goto err_out;
			} else {
				if (sector_read(sb, LogSector, &tmp_bh, 0) !=
				    0)
					goto err_out;
			}

			memcpy((char *)tmp_bh->b_data + offset,
			       (char *)buffer + write_bytes, (s32)oneblkwrite);
			if (sector_write(sb, LogSector, tmp_bh, 0) !=
			    0) {
				brelse(tmp_bh);
				goto err_out;
			}
		}

		count -= oneblkwrite;
		write_bytes += oneblkwrite;
		fid->rwoffset += oneblkwrite;

		fid->attr |= ATTR_ARCHIVE;

		if (fid->size < fid->rwoffset) {
			fid->size = fid->rwoffset;
			modified = true;
		}
	}

	brelse(tmp_bh);

	/* (3) update the direcoty entry */
	es = get_entry_set_in_dir(sb, &fid->dir, fid->entry,
				  ES_ALL_ENTRIES, &ep);
	if (!es)
		goto err_out;
	ep2 = ep + 1;

	exfat_set_entry_time(ep, tm_current(&tm), TM_MODIFY);
	exfat_set_entry_attr(ep, fid->attr);

	if (modified) {
		if (exfat_get_entry_flag(ep2) != fid->flags)
			exfat_set_entry_flag(ep2, fid->flags);

		if (exfat_get_entry_size(ep2) != fid->size)
			exfat_set_entry_size(ep2, fid->size);

		if (exfat_get_entry_clu0(ep2) != fid->start_clu)
			exfat_set_entry_clu0(ep2, fid->start_clu);
	}

	update_dir_checksum_with_entry_set(sb, es);
	release_entry_set(es);

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

err_out:
	/* set the size of written bytes */
	if (wcount)
		*wcount = write_bytes;

	if (num_alloced == 0)
		ret = -ENOSPC;

	else if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsTruncateFile(struct inode *inode, u64 old_size, u64 new_size)
{
	s32 num_clusters;
	u32 last_clu = CLUSTER_32(0);
	int ret = 0;
	struct chain_t clu;
	struct timestamp_t tm;
	struct dentry_t *ep, *ep2;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);
	struct entry_set_cache_t *es = NULL;

	pr_debug("%s entered (inode %p size %llu)\n", __func__, inode,
		 new_size);

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* check if the given file ID is opened */
	if (fid->type != TYPE_FILE) {
		ret = -EPERM;
		goto out;
	}

	if (fid->size != old_size) {
		pr_err("[EXFAT] truncate : can't skip it because of size-mismatch(old:%lld->fid:%lld).\n",
		       old_size, fid->size);
	}

	if (old_size <= new_size) {
		ret = 0;
		goto out;
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	clu.dir = fid->start_clu;
	clu.size = (s32)((old_size - 1) >> p_fs->cluster_size_bits) + 1;
	clu.flags = fid->flags;

	if (new_size > 0) {
		num_clusters = (s32)((new_size - 1) >>
				     p_fs->cluster_size_bits) + 1;

		if (clu.flags == 0x03) {
			clu.dir += num_clusters;
		} else {
			while (num_clusters > 0) {
				last_clu = clu.dir;
				if (exfat_fat_read(sb, clu.dir, &clu.dir) == -1) {
					ret = -EIO;
					goto out;
				}
				num_clusters--;
			}
		}

		clu.size -= num_clusters;
	}

	fid->size = new_size;
	fid->attr |= ATTR_ARCHIVE;
	if (new_size == 0) {
		fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
		fid->start_clu = CLUSTER_32(~0);
	}

	/* (1) update the directory entry */
	es = get_entry_set_in_dir(sb, &fid->dir, fid->entry,
				  ES_ALL_ENTRIES, &ep);
	if (!es) {
		ret = -ENOENT;
		goto out;
		}
	ep2 = ep + 1;

	exfat_set_entry_time(ep, tm_current(&tm), TM_MODIFY);
	exfat_set_entry_attr(ep, fid->attr);

	exfat_set_entry_size(ep2, new_size);
	if (new_size == 0) {
		exfat_set_entry_flag(ep2, 0x01);
		exfat_set_entry_clu0(ep2, CLUSTER_32(0));
	}

	update_dir_checksum_with_entry_set(sb, es);
	release_entry_set(es);

	/* (2) cut off from the FAT chain */
	if (last_clu != CLUSTER_32(0)) {
		if (fid->flags == 0x01)
			exfat_fat_write(sb, last_clu, CLUSTER_32(~0));
	}

	/* (3) free the clusters */
	exfat_free_cluster(sb, &clu, 0);

	/* hint information */
	fid->hint_last_off = -1;
	if (fid->rwoffset > fid->size)
		fid->rwoffset = fid->size;

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	pr_debug("%s exited (%d)\n", __func__, ret);
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static void update_parent_info(struct file_id_t *fid,
			       struct inode *parent_inode)
{
	struct fs_info_t *p_fs = &(EXFAT_SB(parent_inode->i_sb)->fs_info);
	struct file_id_t *parent_fid = &(EXFAT_I(parent_inode)->fid);

	if (unlikely((parent_fid->flags != fid->dir.flags) ||
		     (parent_fid->size !=
		      (fid->dir.size << p_fs->cluster_size_bits)) ||
		     (parent_fid->start_clu != fid->dir.dir))) {
		fid->dir.dir = parent_fid->start_clu;
		fid->dir.flags = parent_fid->flags;
		fid->dir.size = ((parent_fid->size + (p_fs->cluster_size - 1))
						>> p_fs->cluster_size_bits);
	}
}

static int ffsMoveFile(struct inode *old_parent_inode, struct file_id_t *fid,
		       struct inode *new_parent_inode, struct dentry *new_dentry)
{
	s32 ret;
	s32 dentry;
	struct chain_t olddir, newdir;
	struct chain_t *p_dir = NULL;
	struct uni_name_t uni_name;
	struct dentry_t *ep;
	struct super_block *sb = old_parent_inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	u8 *new_path = (u8 *)new_dentry->d_name.name;
	struct inode *new_inode = new_dentry->d_inode;
	int num_entries;
	struct file_id_t *new_fid = NULL;
	s32 new_entry = 0;

	/* check the validity of the given file id */
	if (!fid)
		return -EINVAL;

	/* check the validity of pointer parameters */
	if (!new_path || (*new_path == '\0'))
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	update_parent_info(fid, old_parent_inode);

	olddir.dir = fid->dir.dir;
	olddir.size = fid->dir.size;
	olddir.flags = fid->dir.flags;

	dentry = fid->entry;

	/* check if the old file is "." or ".." */
	if (p_fs->vol_type != EXFAT) {
		if ((olddir.dir != p_fs->root_dir) && (dentry < 2)) {
			ret = -EPERM;
			goto out2;
		}
	}

	ep = get_entry_in_dir(sb, &olddir, dentry, NULL);
	if (!ep) {
		ret = -ENOENT;
		goto out2;
	}

	if (exfat_get_entry_attr(ep) & ATTR_READONLY) {
		ret = -EPERM;
		goto out2;
	}

	/* check whether new dir is existing directory and empty */
	if (new_inode) {
		u32 entry_type;

		ret = -ENOENT;
		new_fid = &EXFAT_I(new_inode)->fid;

		update_parent_info(new_fid, new_parent_inode);

		p_dir = &new_fid->dir;
		new_entry = new_fid->entry;
		ep = get_entry_in_dir(sb, p_dir, new_entry, NULL);
		if (!ep)
			goto out;

		entry_type = exfat_get_entry_type(ep);

		if (entry_type == TYPE_DIR) {
			struct chain_t new_clu;

			new_clu.dir = new_fid->start_clu;
			new_clu.size = (s32)((new_fid->size - 1) >>
					     p_fs->cluster_size_bits) + 1;
			new_clu.flags = new_fid->flags;

			if (!is_dir_empty(sb, &new_clu)) {
				ret = -EEXIST;
				goto out;
			}
		}
	}

	/* check the validity of directory name in the given new pathname */
	ret = resolve_path(new_parent_inode, new_path, &newdir, &uni_name);
	if (ret)
		goto out2;

	fs_set_vol_flags(sb, VOL_DIRTY);

	if (olddir.dir == newdir.dir)
		ret = exfat_rename_file(new_parent_inode, &olddir, dentry,
					&uni_name, fid);
	else
		ret = move_file(new_parent_inode, &olddir, dentry, &newdir,
				&uni_name, fid);

	if ((ret == 0) && new_inode) {
		/* delete entries of new_dir */
		ep = get_entry_in_dir(sb, p_dir, new_entry, NULL);
		if (!ep)
			goto out;

		num_entries = exfat_count_ext_entries(sb, p_dir,
						      new_entry, ep);
		if (num_entries < 0)
			goto out;
		exfat_delete_dir_entry(sb, p_dir, new_entry, 0,
				       num_entries + 1);
	}
out:
#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;
out2:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsRemoveFile(struct inode *inode, struct file_id_t *fid)
{
	s32 dentry;
	int ret = 0;
	struct chain_t dir, clu_to_free;
	struct dentry_t *ep;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (!fid)
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	dir.dir = fid->dir.dir;
	dir.size = fid->dir.size;
	dir.flags = fid->dir.flags;

	dentry = fid->entry;

	ep = get_entry_in_dir(sb, &dir, dentry, NULL);
	if (!ep) {
		ret = -ENOENT;
		goto out;
	}

	if (exfat_get_entry_attr(ep) & ATTR_READONLY) {
		ret = -EPERM;
		goto out;
	}
	fs_set_vol_flags(sb, VOL_DIRTY);

	/* (1) update the directory entry */
	remove_file(inode, &dir, dentry);

	clu_to_free.dir = fid->start_clu;
	clu_to_free.size = (s32)((fid->size - 1) >> p_fs->cluster_size_bits) + 1;
	clu_to_free.flags = fid->flags;

	/* (2) free the clusters */
	exfat_free_cluster(sb, &clu_to_free, 0);

	fid->size = 0;
	fid->start_clu = CLUSTER_32(~0);
	fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;
out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

#if 0
/* Not currently wired up */
static int ffsSetAttr(struct inode *inode, u32 attr)
{
	u32 type;
	int ret = 0;
	sector_t sector = 0;
	struct dentry_t *ep;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);
	u8 is_dir = (fid->type == TYPE_DIR) ? 1 : 0;
	struct entry_set_cache_t *es = NULL;

	if (fid->attr == attr) {
		if (p_fs->dev_ejected)
			return -EIO;
		return 0;
	}

	if (is_dir) {
		if ((fid->dir.dir == p_fs->root_dir) &&
		    (fid->entry == -1)) {
			if (p_fs->dev_ejected)
				return -EIO;
			return 0;
		}
	}

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* get the directory entry of given file */
	es = get_entry_set_in_dir(sb, &fid->dir, fid->entry,
				  ES_ALL_ENTRIES, &ep);
	if (!es) {
		ret = -ENOENT;
		goto out;
	}

	type = exfat_get_entry_type(ep);

	if (((type == TYPE_FILE) && (attr & ATTR_SUBDIR)) ||
	    ((type == TYPE_DIR) && (!(attr & ATTR_SUBDIR)))) {
		if (p_fs->dev_ejected)
			ret = -EIO;
		else
			ret = -EINVAL;

		release_entry_set(es);
		goto out;
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* set the file attribute */
	fid->attr = attr;
	exfat_set_entry_attr(ep, attr);

	update_dir_checksum_with_entry_set(sb, es);
	release_entry_set(es);

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;
out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}
#endif

static int ffsReadStat(struct inode *inode, struct dir_entry_t *info)
{
	s32 count;
	int ret = 0;
	struct chain_t dir;
	struct uni_name_t uni_name;
	struct timestamp_t tm;
	struct dentry_t *ep, *ep2;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);
	struct entry_set_cache_t *es = NULL;
	u8 is_dir = (fid->type == TYPE_DIR) ? 1 : 0;

	pr_debug("%s entered\n", __func__);

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	if (is_dir) {
		if ((fid->dir.dir == p_fs->root_dir) &&
		    (fid->entry == -1)) {
			info->Attr = ATTR_SUBDIR;
			memset((char *)&info->CreateTimestamp, 0,
			       sizeof(struct date_time_t));
			memset((char *)&info->ModifyTimestamp, 0,
			       sizeof(struct date_time_t));
			memset((char *)&info->AccessTimestamp, 0,
			       sizeof(struct date_time_t));
			strcpy(info->ShortName, ".");
			strcpy(info->Name, ".");

			dir.dir = p_fs->root_dir;
			dir.flags = 0x01;

			if (p_fs->root_dir == CLUSTER_32(0)) {
				/* FAT16 root_dir */
				info->Size = p_fs->dentries_in_root <<
						DENTRY_SIZE_BITS;
			} else {
				info->Size = count_num_clusters(sb, &dir) <<
						p_fs->cluster_size_bits;
			}

			count = count_dos_name_entries(sb, &dir, TYPE_DIR);
			if (count < 0) {
				ret = count; /* propagate error upward */
				goto out;
			}
			info->NumSubdirs = count;

			if (p_fs->dev_ejected)
				ret = -EIO;
			goto out;
		}
	}

	/* get the directory entry of given file or directory */
	es = get_entry_set_in_dir(sb, &fid->dir, fid->entry,
				  ES_2_ENTRIES, &ep);
	if (!es) {
		ret = -ENOENT;
		goto out;
	}
	ep2 = ep + 1;

	/* set FILE_INFO structure using the acquired struct dentry_t */
	info->Attr = exfat_get_entry_attr(ep);

	exfat_get_entry_time(ep, &tm, TM_CREATE);
	info->CreateTimestamp.Year = tm.year;
	info->CreateTimestamp.Month = tm.mon;
	info->CreateTimestamp.Day = tm.day;
	info->CreateTimestamp.Hour = tm.hour;
	info->CreateTimestamp.Minute = tm.min;
	info->CreateTimestamp.Second = tm.sec;
	info->CreateTimestamp.MilliSecond = 0;

	exfat_get_entry_time(ep, &tm, TM_MODIFY);
	info->ModifyTimestamp.Year = tm.year;
	info->ModifyTimestamp.Month = tm.mon;
	info->ModifyTimestamp.Day = tm.day;
	info->ModifyTimestamp.Hour = tm.hour;
	info->ModifyTimestamp.Minute = tm.min;
	info->ModifyTimestamp.Second = tm.sec;
	info->ModifyTimestamp.MilliSecond = 0;

	memset((char *)&info->AccessTimestamp, 0, sizeof(struct date_time_t));

	*uni_name.name = 0x0;
	/* XXX this is very bad for exfat cuz name is already included in es.
	 * API should be revised
	 */
	exfat_get_uni_name_from_ext_entry(sb, &fid->dir, fid->entry,
					  uni_name.name);
	nls_uniname_to_cstring(sb, info->Name, &uni_name);

	info->NumSubdirs = 2;

	info->Size = exfat_get_entry_size(ep2);

	release_entry_set(es);

	if (is_dir) {
		dir.dir = fid->start_clu;
		dir.flags = 0x01;

		if (info->Size == 0)
			info->Size = (u64)count_num_clusters(sb, &dir) <<
					p_fs->cluster_size_bits;

		count = count_dos_name_entries(sb, &dir, TYPE_DIR);
		if (count < 0) {
			ret = count; /* propagate error upward */
			goto out;
		}
		info->NumSubdirs += count;
	}

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	pr_debug("%s exited successfully\n", __func__);
	return ret;
}

static int ffsWriteStat(struct inode *inode, struct dir_entry_t *info)
{
	int ret = 0;
	struct timestamp_t tm;
	struct dentry_t *ep, *ep2;
	struct entry_set_cache_t *es = NULL;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);
	u8 is_dir = (fid->type == TYPE_DIR) ? 1 : 0;

	pr_debug("%s entered (inode %p info %p\n", __func__, inode, info);

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	if (is_dir) {
		if ((fid->dir.dir == p_fs->root_dir) &&
		    (fid->entry == -1)) {
			if (p_fs->dev_ejected)
				ret = -EIO;
			ret = 0;
			goto out;
		}
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* get the directory entry of given file or directory */
	es = get_entry_set_in_dir(sb, &fid->dir, fid->entry,
				  ES_ALL_ENTRIES, &ep);
	if (!es) {
		ret = -ENOENT;
		goto out;
	}
	ep2 = ep + 1;

	exfat_set_entry_attr(ep, info->Attr);

	/* set FILE_INFO structure using the acquired struct dentry_t */
	tm.sec  = info->CreateTimestamp.Second;
	tm.min  = info->CreateTimestamp.Minute;
	tm.hour = info->CreateTimestamp.Hour;
	tm.day  = info->CreateTimestamp.Day;
	tm.mon  = info->CreateTimestamp.Month;
	tm.year = info->CreateTimestamp.Year;
	exfat_set_entry_time(ep, &tm, TM_CREATE);

	tm.sec  = info->ModifyTimestamp.Second;
	tm.min  = info->ModifyTimestamp.Minute;
	tm.hour = info->ModifyTimestamp.Hour;
	tm.day  = info->ModifyTimestamp.Day;
	tm.mon  = info->ModifyTimestamp.Month;
	tm.year = info->ModifyTimestamp.Year;
	exfat_set_entry_time(ep, &tm, TM_MODIFY);

	exfat_set_entry_size(ep2, info->Size);

	update_dir_checksum_with_entry_set(sb, es);
	release_entry_set(es);

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	pr_debug("%s exited (%d)\n", __func__, ret);

	return ret;
}

static int ffsMapCluster(struct inode *inode, s32 clu_offset, u32 *clu)
{
	s32 num_clusters, num_alloced;
	bool modified = false;
	u32 last_clu;
	int ret = 0;
	struct chain_t new_clu;
	struct dentry_t *ep;
	struct entry_set_cache_t *es = NULL;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);

	/* check the validity of pointer parameters */
	if (!clu)
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	fid->rwoffset = (s64)(clu_offset) << p_fs->cluster_size_bits;

	if (EXFAT_I(inode)->mmu_private == 0)
		num_clusters = 0;
	else
		num_clusters = (s32)((EXFAT_I(inode)->mmu_private - 1) >>
				     p_fs->cluster_size_bits) + 1;

	*clu = last_clu = fid->start_clu;

	if (fid->flags == 0x03) {
		if ((clu_offset > 0) && (*clu != CLUSTER_32(~0))) {
			last_clu += clu_offset - 1;

			if (clu_offset == num_clusters)
				*clu = CLUSTER_32(~0);
			else
				*clu += clu_offset;
		}
	} else {
		/* hint information */
		if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
		    (clu_offset >= fid->hint_last_off)) {
			clu_offset -= fid->hint_last_off;
			*clu = fid->hint_last_clu;
		}

		while ((clu_offset > 0) && (*clu != CLUSTER_32(~0))) {
			last_clu = *clu;
			if (exfat_fat_read(sb, *clu, clu) == -1) {
				ret = -EIO;
				goto out;
			}
			clu_offset--;
		}
	}

	if (*clu == CLUSTER_32(~0)) {
		fs_set_vol_flags(sb, VOL_DIRTY);

		new_clu.dir = (last_clu == CLUSTER_32(~0)) ? CLUSTER_32(~0) :
					last_clu + 1;
		new_clu.size = 0;
		new_clu.flags = fid->flags;

		/* (1) allocate a cluster */
		num_alloced = exfat_alloc_cluster(sb, 1, &new_clu);
		if (num_alloced < 0) {
			ret = -EIO;
			goto out;
		} else if (num_alloced == 0) {
			ret = -ENOSPC;
			goto out;
		}

		/* (2) append to the FAT chain */
		if (last_clu == CLUSTER_32(~0)) {
			if (new_clu.flags == 0x01)
				fid->flags = 0x01;
			fid->start_clu = new_clu.dir;
			modified = true;
		} else {
			if (new_clu.flags != fid->flags) {
				exfat_chain_cont_cluster(sb, fid->start_clu,
							 num_clusters);
				fid->flags = 0x01;
				modified = true;
			}
			if (new_clu.flags == 0x01)
				exfat_fat_write(sb, last_clu, new_clu.dir);
		}

		num_clusters += num_alloced;
		*clu = new_clu.dir;

		es = get_entry_set_in_dir(sb, &fid->dir, fid->entry,
					  ES_ALL_ENTRIES, &ep);
		if (!es) {
			ret = -ENOENT;
			goto out;
		}
		/* get stream entry */
		ep++;

		/* (3) update directory entry */
		if (modified) {
			if (exfat_get_entry_flag(ep) != fid->flags)
				exfat_set_entry_flag(ep, fid->flags);

			if (exfat_get_entry_clu0(ep) != fid->start_clu)
				exfat_set_entry_clu0(ep, fid->start_clu);
		}

		update_dir_checksum_with_entry_set(sb, es);
		release_entry_set(es);

		/* add number of new blocks to inode */
		inode->i_blocks += num_alloced << (p_fs->cluster_size_bits - 9);
	}

	/* hint information */
	fid->hint_last_off = (s32)(fid->rwoffset >> p_fs->cluster_size_bits);
	fid->hint_last_clu = *clu;

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

/*----------------------------------------------------------------------*/
/*  Directory Operation Functions                                       */
/*----------------------------------------------------------------------*/

static int ffsCreateDir(struct inode *inode, char *path, struct file_id_t *fid)
{
	int ret = 0;
	struct chain_t dir;
	struct uni_name_t uni_name;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	pr_debug("%s entered\n", __func__);

	/* check the validity of pointer parameters */
	if (!fid || !path || (*path == '\0'))
		return -EINVAL;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	/* check the validity of directory name in the given old pathname */
	ret = resolve_path(inode, path, &dir, &uni_name);
	if (ret)
		goto out;

	fs_set_vol_flags(sb, VOL_DIRTY);

	ret = create_dir(inode, &dir, &uni_name, fid);

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;
out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsReadDir(struct inode *inode, struct dir_entry_t *dir_entry)
{
	int i, dentry, clu_offset;
	int ret = 0;
	s32 dentries_per_clu, dentries_per_clu_bits = 0;
	u32 type;
	sector_t sector;
	struct chain_t dir, clu;
	struct uni_name_t uni_name;
	struct timestamp_t tm;
	struct dentry_t *ep;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);

	/* check the validity of pointer parameters */
	if (!dir_entry)
		return -EINVAL;

	/* check if the given file ID is opened */
	if (fid->type != TYPE_DIR)
		return -ENOTDIR;

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	if (fid->entry == -1) {
		dir.dir = p_fs->root_dir;
		dir.flags = 0x01;
	} else {
		dir.dir = fid->start_clu;
		dir.size = (s32)(fid->size >> p_fs->cluster_size_bits);
		dir.flags = fid->flags;
	}

	dentry = (s32)fid->rwoffset;

	if (dir.dir == CLUSTER_32(0)) {
		/* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;

		if (dentry == dentries_per_clu) {
			clu.dir = CLUSTER_32(~0);
		} else {
			clu.dir = dir.dir;
			clu.size = dir.size;
			clu.flags = dir.flags;
		}
	} else {
		dentries_per_clu = p_fs->dentries_per_clu;
		dentries_per_clu_bits = ilog2(dentries_per_clu);

		clu_offset = dentry >> dentries_per_clu_bits;
		clu.dir = dir.dir;
		clu.size = dir.size;
		clu.flags = dir.flags;

		if (clu.flags == 0x03) {
			clu.dir += clu_offset;
			clu.size -= clu_offset;
		} else {
			/* hint_information */
			if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
			    (clu_offset >= fid->hint_last_off)) {
				clu_offset -= fid->hint_last_off;
				clu.dir = fid->hint_last_clu;
			}

			while (clu_offset > 0) {
				/* clu.dir = exfat_fat_read(sb, clu.dir); */
				if (exfat_fat_read(sb, clu.dir, &clu.dir) == -1) {
					ret = -EIO;
					goto out;
				}
				clu_offset--;
			}
		}
	}

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		if (dir.dir == CLUSTER_32(0)) /* FAT16 root_dir */
			i = dentry % dentries_per_clu;
		else
			i = dentry & (dentries_per_clu - 1);

		for ( ; i < dentries_per_clu; i++, dentry++) {
			ep = get_entry_in_dir(sb, &clu, i, &sector);
			if (!ep) {
				ret = -ENOENT;
				goto out;
			}
			type = exfat_get_entry_type(ep);

			if (type == TYPE_UNUSED)
				break;

			if ((type != TYPE_FILE) && (type != TYPE_DIR))
				continue;

			exfat_buf_lock(sb, sector);
			dir_entry->Attr = exfat_get_entry_attr(ep);

			exfat_get_entry_time(ep, &tm, TM_CREATE);
			dir_entry->CreateTimestamp.Year = tm.year;
			dir_entry->CreateTimestamp.Month = tm.mon;
			dir_entry->CreateTimestamp.Day = tm.day;
			dir_entry->CreateTimestamp.Hour = tm.hour;
			dir_entry->CreateTimestamp.Minute = tm.min;
			dir_entry->CreateTimestamp.Second = tm.sec;
			dir_entry->CreateTimestamp.MilliSecond = 0;

			exfat_get_entry_time(ep, &tm, TM_MODIFY);
			dir_entry->ModifyTimestamp.Year = tm.year;
			dir_entry->ModifyTimestamp.Month = tm.mon;
			dir_entry->ModifyTimestamp.Day = tm.day;
			dir_entry->ModifyTimestamp.Hour = tm.hour;
			dir_entry->ModifyTimestamp.Minute = tm.min;
			dir_entry->ModifyTimestamp.Second = tm.sec;
			dir_entry->ModifyTimestamp.MilliSecond = 0;

			memset((char *)&dir_entry->AccessTimestamp, 0,
			       sizeof(struct date_time_t));

			*uni_name.name = 0x0;
			exfat_get_uni_name_from_ext_entry(sb, &dir, dentry,
							  uni_name.name);
			nls_uniname_to_cstring(sb, dir_entry->Name, &uni_name);
			exfat_buf_unlock(sb, sector);

			ep = get_entry_in_dir(sb, &clu, i + 1, NULL);
			if (!ep) {
				ret = -ENOENT;
				goto out;
			}

			dir_entry->Size = exfat_get_entry_size(ep);

			/* hint information */
			if (dir.dir == CLUSTER_32(0)) { /* FAT16 root_dir */
			} else {
				fid->hint_last_off = dentry >>
							dentries_per_clu_bits;
				fid->hint_last_clu = clu.dir;
			}

			fid->rwoffset = (s64)(++dentry);

			if (p_fs->dev_ejected)
				ret = -EIO;
			goto out;
		}

		if (dir.dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			/* clu.dir = exfat_fat_read(sb, clu.dir); */
			if (exfat_fat_read(sb, clu.dir, &clu.dir) == -1) {
				ret = -EIO;
				goto out;
			}
		}
	}

	*dir_entry->Name = '\0';

	fid->rwoffset = (s64)(++dentry);

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

static int ffsRemoveDir(struct inode *inode, struct file_id_t *fid)
{
	s32 dentry;
	int ret = 0;
	struct chain_t dir, clu_to_free;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (!fid)
		return -EINVAL;

	dir.dir = fid->dir.dir;
	dir.size = fid->dir.size;
	dir.flags = fid->dir.flags;

	dentry = fid->entry;

	/* check if the file is "." or ".." */
	if (p_fs->vol_type != EXFAT) {
		if ((dir.dir != p_fs->root_dir) && (dentry < 2))
			return -EPERM;
	}

	/* acquire the lock for file system critical section */
	mutex_lock(&p_fs->v_mutex);

	clu_to_free.dir = fid->start_clu;
	clu_to_free.size = (s32)((fid->size - 1) >> p_fs->cluster_size_bits) + 1;
	clu_to_free.flags = fid->flags;

	if (!is_dir_empty(sb, &clu_to_free)) {
		ret = -ENOTEMPTY;
		goto out;
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* (1) update the directory entry */
	remove_file(inode, &dir, dentry);

	/* (2) free the clusters */
	exfat_free_cluster(sb, &clu_to_free, 1);

	fid->size = 0;
	fid->start_clu = CLUSTER_32(~0);
	fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;

#ifndef CONFIG_STAGING_EXFAT_DELAYED_SYNC
	fs_sync(sb, true);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		ret = -EIO;

out:
	/* release the lock for file system critical section */
	mutex_unlock(&p_fs->v_mutex);

	return ret;
}

/*======================================================================*/
/*  Directory Entry Operations                                          */
/*======================================================================*/

static int exfat_readdir(struct file *filp, struct dir_context *ctx)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct fs_info_t *p_fs = &sbi->fs_info;
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
	struct dir_entry_t de;
	unsigned long inum;
	loff_t cpos;
	int err = 0;

	__lock_super(sb);

	cpos = ctx->pos;
	/* Fake . and .. for the root directory. */
	if ((p_fs->vol_type == EXFAT) || (inode->i_ino == EXFAT_ROOT_INO)) {
		while (cpos < 2) {
			if (inode->i_ino == EXFAT_ROOT_INO)
				inum = EXFAT_ROOT_INO;
			else if (cpos == 0)
				inum = inode->i_ino;
			else /* (cpos == 1) */
				inum = parent_ino(filp->f_path.dentry);

			if (!dir_emit_dots(filp, ctx))
				goto out;
			cpos++;
			ctx->pos++;
		}
		if (cpos == 2)
			cpos = 0;
	}
	if (cpos & (DENTRY_SIZE - 1)) {
		err = -ENOENT;
		goto out;
	}

get_new:
	EXFAT_I(inode)->fid.size = i_size_read(inode);
	EXFAT_I(inode)->fid.rwoffset = cpos >> DENTRY_SIZE_BITS;

	err = ffsReadDir(inode, &de);
	if (err) {
		/* at least we tried to read a sector
		 * move cpos to next sector position (should be aligned)
		 */
		if (err == -EIO) {
			cpos += 1 << p_bd->sector_size_bits;
			cpos &= ~((1 << p_bd->sector_size_bits) - 1);
		}

		goto end_of_dir;
	}

	cpos = EXFAT_I(inode)->fid.rwoffset << DENTRY_SIZE_BITS;

	if (!de.Name[0])
		goto end_of_dir;

	if (!memcmp(de.ShortName, DOS_CUR_DIR_NAME, DOS_NAME_LENGTH)) {
		inum = inode->i_ino;
	} else if (!memcmp(de.ShortName, DOS_PAR_DIR_NAME, DOS_NAME_LENGTH)) {
		inum = parent_ino(filp->f_path.dentry);
	} else {
		loff_t i_pos = ((loff_t)EXFAT_I(inode)->fid.start_clu << 32) |
				((EXFAT_I(inode)->fid.rwoffset - 1) & 0xffffffff);
		struct inode *tmp = exfat_iget(sb, i_pos);

		if (tmp) {
			inum = tmp->i_ino;
			iput(tmp);
		} else {
			inum = iunique(sb, EXFAT_ROOT_INO);
		}
	}

	if (!dir_emit(ctx, de.Name, strlen(de.Name), inum,
		      (de.Attr & ATTR_SUBDIR) ? DT_DIR : DT_REG))
		goto out;

	ctx->pos = cpos;
	goto get_new;

end_of_dir:
	ctx->pos = cpos;
out:
	__unlock_super(sb);
	return err;
}

static int exfat_ioctl_volume_id(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct fs_info_t *p_fs = &sbi->fs_info;

	return p_fs->vol_id;
}

static long exfat_generic_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
#ifdef CONFIG_STAGING_EXFAT_KERNEL_DEBUG
	unsigned int flags;
#endif /* CONFIG_STAGING_EXFAT_KERNEL_DEBUG */

	switch (cmd) {
	case EXFAT_IOCTL_GET_VOLUME_ID:
		return exfat_ioctl_volume_id(inode);
#ifdef CONFIG_STAGING_EXFAT_KERNEL_DEBUG
	case EXFAT_IOC_GET_DEBUGFLAGS: {
		struct super_block *sb = inode->i_sb;
		struct exfat_sb_info *sbi = EXFAT_SB(sb);

		flags = sbi->debug_flags;
		return put_user(flags, (int __user *)arg);
	}
	case EXFAT_IOC_SET_DEBUGFLAGS: {
		struct super_block *sb = inode->i_sb;
		struct exfat_sb_info *sbi = EXFAT_SB(sb);

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(flags, (int __user *)arg))
			return -EFAULT;

		__lock_super(sb);
		sbi->debug_flags = flags;
		__unlock_super(sb);

		return 0;
	}
#endif /* CONFIG_STAGING_EXFAT_KERNEL_DEBUG */
	default:
		return -ENOTTY; /* Inappropriate ioctl for device */
	}
}

static const struct file_operations exfat_dir_operations = {
	.llseek     = generic_file_llseek,
	.read       = generic_read_dir,
	.iterate    = exfat_readdir,
	.unlocked_ioctl = exfat_generic_ioctl,
	.fsync      = generic_file_fsync,
};

static int exfat_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			bool excl)
{
	struct super_block *sb = dir->i_sb;
	struct timespec64 curtime;
	struct inode *inode;
	struct file_id_t fid;
	loff_t i_pos;
	int err;

	__lock_super(sb);

	pr_debug("%s entered\n", __func__);

	err = ffsCreateFile(dir, (u8 *)dentry->d_name.name, FM_REGULAR, &fid);
	if (err)
		goto out;

	INC_IVERSION(dir);
	curtime = current_time(dir);
	dir->i_ctime = curtime;
	dir->i_mtime = curtime;
	dir->i_atime = curtime;
	if (IS_DIRSYNC(dir))
		(void)exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	i_pos = ((loff_t)fid.dir.dir << 32) | (fid.entry & 0xffffffff);

	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	INC_IVERSION(inode);
	curtime = current_time(inode);
	inode->i_mtime = curtime;
	inode->i_atime = curtime;
	inode->i_ctime = curtime;
	/*
	 * timestamp is already written, so mark_inode_dirty() is unnecessary.
	 */

	dentry->d_time = GET_IVERSION(dentry->d_parent->d_inode);
	d_instantiate(dentry, inode);

out:
	__unlock_super(sb);
	pr_debug("%s exited\n", __func__);
	return err;
}

static int exfat_find(struct inode *dir, struct qstr *qname,
		      struct file_id_t *fid)
{
	int err;

	if (qname->len == 0)
		return -ENOENT;

	err = ffsLookupFile(dir, (u8 *)qname->name, fid);
	if (err)
		return -ENOENT;

	return 0;
}

static int exfat_d_anon_disconn(struct dentry *dentry)
{
	return IS_ROOT(dentry) && (dentry->d_flags & DCACHE_DISCONNECTED);
}

static struct dentry *exfat_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct dentry *alias;
	int err;
	struct file_id_t fid;
	loff_t i_pos;
	u64 ret;
	mode_t i_mode;

	__lock_super(sb);
	pr_debug("%s entered\n", __func__);
	err = exfat_find(dir, &dentry->d_name, &fid);
	if (err) {
		if (err == -ENOENT) {
			inode = NULL;
			goto out;
		}
		goto error;
	}

	i_pos = ((loff_t)fid.dir.dir << 32) | (fid.entry & 0xffffffff);
	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto error;
	}

	i_mode = inode->i_mode;
	if (S_ISLNK(i_mode) && !EXFAT_I(inode)->target) {
		EXFAT_I(inode)->target = kmalloc(i_size_read(inode) + 1,
						 GFP_KERNEL);
		if (!EXFAT_I(inode)->target) {
			err = -ENOMEM;
			goto error;
		}
		ffsReadFile(dir, &fid, EXFAT_I(inode)->target,
			    i_size_read(inode), &ret);
		*(EXFAT_I(inode)->target + i_size_read(inode)) = '\0';
	}

	alias = d_find_alias(inode);
	if (alias && !exfat_d_anon_disconn(alias)) {
		BUG_ON(d_unhashed(alias));
		if (!S_ISDIR(i_mode))
			d_move(alias, dentry);
		iput(inode);
		__unlock_super(sb);
		pr_debug("%s exited 1\n", __func__);
		return alias;
	}
	dput(alias);
out:
	__unlock_super(sb);
	dentry->d_time = GET_IVERSION(dentry->d_parent->d_inode);
	dentry = d_splice_alias(inode, dentry);
	if (dentry)
		dentry->d_time = GET_IVERSION(dentry->d_parent->d_inode);
	pr_debug("%s exited 2\n", __func__);
	return dentry;

error:
	__unlock_super(sb);
	pr_debug("%s exited 3\n", __func__);
	return ERR_PTR(err);
}

static inline unsigned long exfat_hash(loff_t i_pos)
{
	return hash_32(i_pos, EXFAT_HASH_BITS);
}

static void exfat_attach(struct inode *inode, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	struct hlist_head *head = sbi->inode_hashtable + exfat_hash(i_pos);

	spin_lock(&sbi->inode_hash_lock);
	EXFAT_I(inode)->i_pos = i_pos;
	hlist_add_head(&EXFAT_I(inode)->i_hash_fat, head);
	spin_unlock(&sbi->inode_hash_lock);
}

static void exfat_detach(struct inode *inode)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);

	spin_lock(&sbi->inode_hash_lock);
	hlist_del_init(&EXFAT_I(inode)->i_hash_fat);
	EXFAT_I(inode)->i_pos = 0;
	spin_unlock(&sbi->inode_hash_lock);
}

static int exfat_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dir->i_sb;
	struct timespec64 curtime;
	int err;

	__lock_super(sb);

	pr_debug("%s entered\n", __func__);

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	err = ffsRemoveFile(dir, &(EXFAT_I(inode)->fid));
	if (err)
		goto out;

	INC_IVERSION(dir);
	curtime = current_time(dir);
	dir->i_mtime = curtime;
	dir->i_atime = curtime;
	if (IS_DIRSYNC(dir))
		(void)exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	clear_nlink(inode);
	curtime = current_time(inode);
	inode->i_mtime = curtime;
	inode->i_atime = curtime;
	exfat_detach(inode);
	remove_inode_hash(inode);

out:
	__unlock_super(sb);
	pr_debug("%s exited\n", __func__);
	return err;
}

static int exfat_symlink(struct inode *dir, struct dentry *dentry,
			 const char *target)
{
	struct super_block *sb = dir->i_sb;
	struct timespec64 curtime;
	struct inode *inode;
	struct file_id_t fid;
	loff_t i_pos;
	int err;
	u64 len = (u64)strlen(target);
	u64 ret;

	__lock_super(sb);

	pr_debug("%s entered\n", __func__);

	err = ffsCreateFile(dir, (u8 *)dentry->d_name.name, FM_SYMLINK, &fid);
	if (err)
		goto out;


	err = ffsWriteFile(dir, &fid, (char *)target, len, &ret);

	if (err) {
		ffsRemoveFile(dir, &fid);
		goto out;
	}

	INC_IVERSION(dir);
	curtime = current_time(dir);
	dir->i_ctime = curtime;
	dir->i_mtime = curtime;
	dir->i_atime = curtime;
	if (IS_DIRSYNC(dir))
		(void)exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	i_pos = ((loff_t)fid.dir.dir << 32) | (fid.entry & 0xffffffff);

	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	INC_IVERSION(inode);
	curtime = current_time(inode);
	inode->i_mtime = curtime;
	inode->i_atime = curtime;
	inode->i_ctime = curtime;
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	EXFAT_I(inode)->target = kmemdup(target, len + 1, GFP_KERNEL);
	if (!EXFAT_I(inode)->target) {
		err = -ENOMEM;
		goto out;
	}

	dentry->d_time = GET_IVERSION(dentry->d_parent->d_inode);
	d_instantiate(dentry, inode);

out:
	__unlock_super(sb);
	pr_debug("%s exited\n", __func__);
	return err;
}

static int exfat_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct timespec64 curtime;
	struct inode *inode;
	struct file_id_t fid;
	loff_t i_pos;
	int err;

	__lock_super(sb);

	pr_debug("%s entered\n", __func__);

	err = ffsCreateDir(dir, (u8 *)dentry->d_name.name, &fid);
	if (err)
		goto out;

	INC_IVERSION(dir);
	curtime = current_time(dir);
	dir->i_ctime = curtime;
	dir->i_mtime = curtime;
	dir->i_atime = curtime;
	if (IS_DIRSYNC(dir))
		(void)exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	inc_nlink(dir);

	i_pos = ((loff_t)fid.dir.dir << 32) | (fid.entry & 0xffffffff);

	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	INC_IVERSION(inode);
	curtime = current_time(inode);
	inode->i_mtime = curtime;
	inode->i_atime = curtime;
	inode->i_ctime = curtime;
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	dentry->d_time = GET_IVERSION(dentry->d_parent->d_inode);
	d_instantiate(dentry, inode);

out:
	__unlock_super(sb);
	pr_debug("%s exited\n", __func__);
	return err;
}

static int exfat_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dir->i_sb;
	struct timespec64 curtime;
	int err;

	__lock_super(sb);

	pr_debug("%s entered\n", __func__);

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	err = ffsRemoveDir(dir, &(EXFAT_I(inode)->fid));
	if (err)
		goto out;

	INC_IVERSION(dir);
	curtime = current_time(dir);
	dir->i_mtime = curtime;
	dir->i_atime = curtime;
	if (IS_DIRSYNC(dir))
		(void)exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	drop_nlink(dir);

	clear_nlink(inode);
	curtime = current_time(inode);
	inode->i_mtime = curtime;
	inode->i_atime = curtime;
	exfat_detach(inode);
	remove_inode_hash(inode);

out:
	__unlock_super(sb);
	pr_debug("%s exited\n", __func__);
	return err;
}

static int exfat_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct inode *old_inode, *new_inode;
	struct super_block *sb = old_dir->i_sb;
	struct timespec64 curtime;
	loff_t i_pos;
	int err;

	if (flags)
		return -EINVAL;

	__lock_super(sb);

	pr_debug("%s entered\n", __func__);

	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;

	EXFAT_I(old_inode)->fid.size = i_size_read(old_inode);

	err = ffsMoveFile(old_dir, &(EXFAT_I(old_inode)->fid), new_dir,
			  new_dentry);
	if (err)
		goto out;

	INC_IVERSION(new_dir);
	curtime = current_time(new_dir);
	new_dir->i_ctime = curtime;
	new_dir->i_mtime = curtime;
	new_dir->i_atime = curtime;

	if (IS_DIRSYNC(new_dir))
		(void)exfat_sync_inode(new_dir);
	else
		mark_inode_dirty(new_dir);

	i_pos = ((loff_t)EXFAT_I(old_inode)->fid.dir.dir << 32) |
			(EXFAT_I(old_inode)->fid.entry & 0xffffffff);

	exfat_detach(old_inode);
	exfat_attach(old_inode, i_pos);
	if (IS_DIRSYNC(new_dir))
		(void)exfat_sync_inode(old_inode);
	else
		mark_inode_dirty(old_inode);

	if ((S_ISDIR(old_inode->i_mode)) && (old_dir != new_dir)) {
		drop_nlink(old_dir);
		if (!new_inode)
			inc_nlink(new_dir);
	}
	INC_IVERSION(old_dir);
	curtime = current_time(old_dir);
	old_dir->i_ctime = curtime;
	old_dir->i_mtime = curtime;
	if (IS_DIRSYNC(old_dir))
		(void)exfat_sync_inode(old_dir);
	else
		mark_inode_dirty(old_dir);

	if (new_inode) {
		exfat_detach(new_inode);
		drop_nlink(new_inode);
		if (S_ISDIR(new_inode->i_mode))
			drop_nlink(new_inode);
		new_inode->i_ctime = current_time(new_inode);
	}

out:
	__unlock_super(sb);
	pr_debug("%s exited\n", __func__);
	return err;
}

static int exfat_cont_expand(struct inode *inode, loff_t size)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t start = i_size_read(inode), count = size - i_size_read(inode);
	struct timespec64 curtime;
	int err, err2;

	err = generic_cont_expand_simple(inode, size);
	if (err != 0)
		return err;

	curtime = current_time(inode);
	inode->i_ctime = curtime;
	inode->i_mtime = curtime;
	mark_inode_dirty(inode);

	if (IS_SYNC(inode)) {
		err = filemap_fdatawrite_range(mapping, start,
					       start + count - 1);
		err2 = sync_mapping_buffers(mapping);
		err = (err) ? (err) : (err2);
		err2 = write_inode_now(inode, 1);
		err = (err) ? (err) : (err2);
		if (!err)
			err =  filemap_fdatawait_range(mapping, start,
						       start + count - 1);
	}
	return err;
}

static int exfat_allow_set_time(struct exfat_sb_info *sbi, struct inode *inode)
{
	mode_t allow_utime = sbi->options.allow_utime;

	if (!uid_eq(current_fsuid(), inode->i_uid)) {
		if (in_group_p(inode->i_gid))
			allow_utime >>= 3;
		if (allow_utime & MAY_WRITE)
			return 1;
	}

	/* use a default check */
	return 0;
}

static int exfat_sanitize_mode(const struct exfat_sb_info *sbi,
			       struct inode *inode, umode_t *mode_ptr)
{
	mode_t i_mode, mask, perm;

	i_mode = inode->i_mode;

	if (S_ISREG(i_mode) || S_ISLNK(i_mode))
		mask = sbi->options.fs_fmask;
	else
		mask = sbi->options.fs_dmask;

	perm = *mode_ptr & ~(S_IFMT | mask);

	/* Of the r and x bits, all (subject to umask) must be present.*/
	if ((perm & 0555) != (i_mode & 0555))
		return -EPERM;

	if (exfat_mode_can_hold_ro(inode)) {
		/*
		 * Of the w bits, either all (subject to umask) or none must be
		 * present.
		 */
		if ((perm & 0222) && ((perm & 0222) != (0222 & ~mask)))
			return -EPERM;
	} else {
		/*
		 * If exfat_mode_can_hold_ro(inode) is false, can't change w
		 * bits.
		 */
		if ((perm & 0222) != (0222 & ~mask))
			return -EPERM;
	}

	*mode_ptr &= S_IFMT | perm;

	return 0;
}

static void exfat_truncate(struct inode *inode, loff_t old_size)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct fs_info_t *p_fs = &sbi->fs_info;
	struct timespec64 curtime;
	int err;

	__lock_super(sb);

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (EXFAT_I(inode)->mmu_private > i_size_read(inode))
		EXFAT_I(inode)->mmu_private = i_size_read(inode);

	if (EXFAT_I(inode)->fid.start_clu == 0)
		goto out;

	err = ffsTruncateFile(inode, old_size, i_size_read(inode));
	if (err)
		goto out;

	curtime = current_time(inode);
	inode->i_ctime = curtime;
	inode->i_mtime = curtime;
	if (IS_DIRSYNC(inode))
		(void)exfat_sync_inode(inode);
	else
		mark_inode_dirty(inode);

	inode->i_blocks = ((i_size_read(inode) + (p_fs->cluster_size - 1)) &
			   ~((loff_t)p_fs->cluster_size - 1)) >> 9;
out:
	__unlock_super(sb);
}

static int exfat_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct exfat_sb_info *sbi = EXFAT_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	unsigned int ia_valid;
	int error;
	loff_t old_size;

	pr_debug("%s entered\n", __func__);

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > i_size_read(inode)) {
		error = exfat_cont_expand(inode, attr->ia_size);
		if (error || attr->ia_valid == ATTR_SIZE)
			return error;
		attr->ia_valid &= ~ATTR_SIZE;
	}

	ia_valid = attr->ia_valid;

	if ((ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) &&
	    exfat_allow_set_time(sbi, inode)) {
		attr->ia_valid &= ~(ATTR_MTIME_SET |
				    ATTR_ATIME_SET |
				    ATTR_TIMES_SET);
	}

	error = setattr_prepare(dentry, attr);
	attr->ia_valid = ia_valid;
	if (error)
		return error;

	if (((attr->ia_valid & ATTR_UID) &&
	     (!uid_eq(attr->ia_uid, sbi->options.fs_uid))) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (!gid_eq(attr->ia_gid, sbi->options.fs_gid))) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~(S_IFREG | S_IFLNK | S_IFDIR | 0777)))) {
		return -EPERM;
	}

	/*
	 * We don't return -EPERM here. Yes, strange, but this is too
	 * old behavior.
	 */
	if (attr->ia_valid & ATTR_MODE) {
		if (exfat_sanitize_mode(sbi, inode, &attr->ia_mode) < 0)
			attr->ia_valid &= ~ATTR_MODE;
	}

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	if (attr->ia_valid & ATTR_SIZE) {
		old_size = i_size_read(inode);
		down_write(&EXFAT_I(inode)->truncate_lock);
		truncate_setsize(inode, attr->ia_size);
		exfat_truncate(inode, old_size);
		up_write(&EXFAT_I(inode)->truncate_lock);
	}
	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

	pr_debug("%s exited\n", __func__);
	return error;
}

static int exfat_getattr(const struct path *path, struct kstat *stat,
			 u32 request_mask, unsigned int flags)
{
	struct inode *inode = path->dentry->d_inode;

	pr_debug("%s entered\n", __func__);

	generic_fillattr(inode, stat);
	stat->blksize = EXFAT_SB(inode->i_sb)->fs_info.cluster_size;

	pr_debug("%s exited\n", __func__);
	return 0;
}

static const struct inode_operations exfat_dir_inode_operations = {
	.create        = exfat_create,
	.lookup        = exfat_lookup,
	.unlink        = exfat_unlink,
	.symlink       = exfat_symlink,
	.mkdir         = exfat_mkdir,
	.rmdir         = exfat_rmdir,
	.rename        = exfat_rename,
	.setattr       = exfat_setattr,
	.getattr       = exfat_getattr,
};

/*======================================================================*/
/*  File Operations                                                     */
/*======================================================================*/
static const char *exfat_get_link(struct dentry *dentry, struct inode *inode,
				  struct delayed_call *done)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);

	if (ei->target) {
		char *cookie = ei->target;

		if (cookie)
			return (char *)(ei->target);
	}
	return NULL;
}

static const struct inode_operations exfat_symlink_inode_operations = {
		.get_link = exfat_get_link,
};

static int exfat_file_release(struct inode *inode, struct file *filp)
{
	struct super_block *sb = inode->i_sb;

	EXFAT_I(inode)->fid.size = i_size_read(inode);
	ffsSyncVol(sb, false);
	return 0;
}

static const struct file_operations exfat_file_operations = {
	.llseek      = generic_file_llseek,
	.read_iter   = generic_file_read_iter,
	.write_iter  = generic_file_write_iter,
	.mmap        = generic_file_mmap,
	.release     = exfat_file_release,
	.unlocked_ioctl  = exfat_generic_ioctl,
	.fsync       = generic_file_fsync,
	.splice_read = generic_file_splice_read,
};

static const struct inode_operations exfat_file_inode_operations = {
	.setattr     = exfat_setattr,
	.getattr     = exfat_getattr,
};

/*======================================================================*/
/*  Address Space Operations                                            */
/*======================================================================*/

static int exfat_bmap(struct inode *inode, sector_t sector, sector_t *phys,
		      unsigned long *mapped_blocks, int *create)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct fs_info_t *p_fs = &sbi->fs_info;
	const unsigned long blocksize = sb->s_blocksize;
	const unsigned char blocksize_bits = sb->s_blocksize_bits;
	sector_t last_block;
	int err, clu_offset, sec_offset;
	unsigned int cluster;

	*phys = 0;
	*mapped_blocks = 0;

	last_block = (i_size_read(inode) + (blocksize - 1)) >> blocksize_bits;
	if (sector >= last_block) {
		if (*create == 0)
			return 0;
	} else {
		*create = 0;
	}

	/* cluster offset */
	clu_offset = sector >> p_fs->sectors_per_clu_bits;

	/* sector offset in cluster */
	sec_offset = sector & (p_fs->sectors_per_clu - 1);

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	err = ffsMapCluster(inode, clu_offset, &cluster);

	if (!err && (cluster != CLUSTER_32(~0))) {
		*phys = START_SECTOR(cluster) + sec_offset;
		*mapped_blocks = p_fs->sectors_per_clu - sec_offset;
	}

	return 0;
}

static int exfat_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	unsigned long max_blocks = bh_result->b_size >> inode->i_blkbits;
	int err;
	unsigned long mapped_blocks;
	sector_t phys;

	__lock_super(sb);

	err = exfat_bmap(inode, iblock, &phys, &mapped_blocks, &create);
	if (err) {
		__unlock_super(sb);
		return err;
	}

	if (phys) {
		max_blocks = min(mapped_blocks, max_blocks);
		if (create) {
			EXFAT_I(inode)->mmu_private += max_blocks <<
							sb->s_blocksize_bits;
			set_buffer_new(bh_result);
		}
		map_bh(bh_result, sb, phys);
	}

	bh_result->b_size = max_blocks << sb->s_blocksize_bits;
	__unlock_super(sb);

	return 0;
}

static int exfat_readpage(struct file *file, struct page *page)
{
	return  mpage_readpage(page, exfat_get_block);
}

static int exfat_readpages(struct file *file, struct address_space *mapping,
			   struct list_head *pages, unsigned int nr_pages)
{
	return  mpage_readpages(mapping, pages, nr_pages, exfat_get_block);
}

static int exfat_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, exfat_get_block, wbc);
}

static int exfat_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, exfat_get_block);
}

static void exfat_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > i_size_read(inode)) {
		truncate_pagecache(inode, i_size_read(inode));
		EXFAT_I(inode)->fid.size = i_size_read(inode);
		exfat_truncate(inode, i_size_read(inode));
	}
}

static int exfat_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned int len, unsigned int flags,
			     struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
			       exfat_get_block,
			       &EXFAT_I(mapping->host)->mmu_private);

	if (ret < 0)
		exfat_write_failed(mapping, pos + len);
	return ret;
}

static int exfat_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned int len, unsigned int copied,
			   struct page *pagep, void *fsdata)
{
	struct inode *inode = mapping->host;
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);
	struct timespec64 curtime;
	int err;

	err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);

	if (err < len)
		exfat_write_failed(mapping, pos + len);

	if (!(err < 0) && !(fid->attr & ATTR_ARCHIVE)) {
		curtime = current_time(inode);
		inode->i_mtime = curtime;
		inode->i_ctime = curtime;
		fid->attr |= ATTR_ARCHIVE;
		mark_inode_dirty(inode);
	}
	return err;
}

static ssize_t exfat_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	ssize_t ret;
	int rw;

	rw = iov_iter_rw(iter);

	if (rw == WRITE) {
		if (EXFAT_I(inode)->mmu_private < iov_iter_count(iter))
			return 0;
	}
	ret = blockdev_direct_IO(iocb, inode, iter, exfat_get_block);

	if ((ret < 0) && (rw & WRITE))
		exfat_write_failed(mapping, iov_iter_count(iter));
	return ret;
}

static sector_t _exfat_bmap(struct address_space *mapping, sector_t block)
{
	sector_t blocknr;

	/* exfat_get_cluster() assumes the requested blocknr isn't truncated. */
	down_read(&EXFAT_I(mapping->host)->truncate_lock);
	blocknr = generic_block_bmap(mapping, block, exfat_get_block);
	up_read(&EXFAT_I(mapping->host)->truncate_lock);

	return blocknr;
}

static const struct address_space_operations exfat_aops = {
	.readpage    = exfat_readpage,
	.readpages   = exfat_readpages,
	.writepage   = exfat_writepage,
	.writepages  = exfat_writepages,
	.write_begin = exfat_write_begin,
	.write_end   = exfat_write_end,
	.direct_IO   = exfat_direct_IO,
	.bmap        = _exfat_bmap
};

/*======================================================================*/
/*  Super Operations                                                    */
/*======================================================================*/

static struct inode *exfat_iget(struct super_block *sb, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *info;
	struct hlist_head *head = sbi->inode_hashtable + exfat_hash(i_pos);
	struct inode *inode = NULL;

	spin_lock(&sbi->inode_hash_lock);
	hlist_for_each_entry(info, head, i_hash_fat) {
		BUG_ON(info->vfs_inode.i_sb != sb);

		if (i_pos != info->i_pos)
			continue;
		inode = igrab(&info->vfs_inode);
		if (inode)
			break;
	}
	spin_unlock(&sbi->inode_hash_lock);
	return inode;
}

/* doesn't deal with root inode */
static int exfat_fill_inode(struct inode *inode, struct file_id_t *fid)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	struct fs_info_t *p_fs = &sbi->fs_info;
	struct dir_entry_t info;

	memcpy(&(EXFAT_I(inode)->fid), fid, sizeof(struct file_id_t));

	ffsReadStat(inode, &info);

	EXFAT_I(inode)->i_pos = 0;
	EXFAT_I(inode)->target = NULL;
	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	INC_IVERSION(inode);
	inode->i_generation = prandom_u32();

	if (info.Attr & ATTR_SUBDIR) { /* directory */
		inode->i_generation &= ~1;
		inode->i_mode = exfat_make_mode(sbi, info.Attr, 0777);
		inode->i_op = &exfat_dir_inode_operations;
		inode->i_fop = &exfat_dir_operations;

		i_size_write(inode, info.Size);
		EXFAT_I(inode)->mmu_private = i_size_read(inode);
		set_nlink(inode, info.NumSubdirs);
	} else if (info.Attr & ATTR_SYMLINK) { /* symbolic link */
		inode->i_generation |= 1;
		inode->i_mode = exfat_make_mode(sbi, info.Attr, 0777);
		inode->i_op = &exfat_symlink_inode_operations;

		i_size_write(inode, info.Size);
		EXFAT_I(inode)->mmu_private = i_size_read(inode);
	} else { /* regular file */
		inode->i_generation |= 1;
		inode->i_mode = exfat_make_mode(sbi, info.Attr, 0777);
		inode->i_op = &exfat_file_inode_operations;
		inode->i_fop = &exfat_file_operations;
		inode->i_mapping->a_ops = &exfat_aops;
		inode->i_mapping->nrpages = 0;

		i_size_write(inode, info.Size);
		EXFAT_I(inode)->mmu_private = i_size_read(inode);
	}
	exfat_save_attr(inode, info.Attr);

	inode->i_blocks = ((i_size_read(inode) + (p_fs->cluster_size - 1))
				& ~((loff_t)p_fs->cluster_size - 1)) >> 9;

	exfat_time_fat2unix(&inode->i_mtime, &info.ModifyTimestamp);
	exfat_time_fat2unix(&inode->i_ctime, &info.CreateTimestamp);
	exfat_time_fat2unix(&inode->i_atime, &info.AccessTimestamp);

	return 0;
}

static struct inode *exfat_build_inode(struct super_block *sb,
				       struct file_id_t *fid, loff_t i_pos)
{
	struct inode *inode;
	int err;

	inode = exfat_iget(sb, i_pos);
	if (inode)
		goto out;
	inode = new_inode(sb);
	if (!inode) {
		inode = ERR_PTR(-ENOMEM);
		goto out;
	}
	inode->i_ino = iunique(sb, EXFAT_ROOT_INO);
	SET_IVERSION(inode, 1);
	err = exfat_fill_inode(inode, fid);
	if (err) {
		iput(inode);
		inode = ERR_PTR(err);
		goto out;
	}
	exfat_attach(inode, i_pos);
	insert_inode_hash(inode);
out:
	return inode;
}

static int exfat_sync_inode(struct inode *inode)
{
	return exfat_write_inode(inode, NULL);
}

static struct inode *exfat_alloc_inode(struct super_block *sb)
{
	struct exfat_inode_info *ei;

	ei = kmem_cache_alloc(exfat_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;

	init_rwsem(&ei->truncate_lock);

	return &ei->vfs_inode;
}

static void exfat_destroy_inode(struct inode *inode)
{
	kfree(EXFAT_I(inode)->target);
	EXFAT_I(inode)->target = NULL;

	kmem_cache_free(exfat_inode_cachep, EXFAT_I(inode));
}

static int exfat_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct dir_entry_t info;

	if (inode->i_ino == EXFAT_ROOT_INO)
		return 0;

	info.Attr = exfat_make_attr(inode);
	info.Size = i_size_read(inode);

	exfat_time_unix2fat(&inode->i_mtime, &info.ModifyTimestamp);
	exfat_time_unix2fat(&inode->i_ctime, &info.CreateTimestamp);
	exfat_time_unix2fat(&inode->i_atime, &info.AccessTimestamp);

	ffsWriteStat(inode, &info);

	return 0;
}

static void exfat_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);

	if (!inode->i_nlink)
		i_size_write(inode, 0);
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	exfat_detach(inode);

	remove_inode_hash(inode);
}

static void exfat_free_super(struct exfat_sb_info *sbi)
{
	if (sbi->nls_disk)
		unload_nls(sbi->nls_disk);
	if (sbi->nls_io)
		unload_nls(sbi->nls_io);
	if (sbi->options.iocharset != exfat_default_iocharset)
		kfree(sbi->options.iocharset);
	/* mutex_init is in exfat_fill_super function. only for 3.7+ */
	mutex_destroy(&sbi->s_lock);
	kvfree(sbi);
}

static void exfat_put_super(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	if (__is_sb_dirty(sb))
		exfat_write_super(sb);

	ffsUmountVol(sb);

	sb->s_fs_info = NULL;
	exfat_free_super(sbi);
}

static void exfat_write_super(struct super_block *sb)
{
	__lock_super(sb);

	__set_sb_clean(sb);

	if (!sb_rdonly(sb))
		ffsSyncVol(sb, true);

	__unlock_super(sb);
}

static int exfat_sync_fs(struct super_block *sb, int wait)
{
	int err = 0;

	if (__is_sb_dirty(sb)) {
		__lock_super(sb);
		__set_sb_clean(sb);
		err = ffsSyncVol(sb, true);
		__unlock_super(sb);
	}

	return err;
}

static int exfat_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct vol_info_t info;

	if (p_fs->used_clusters == UINT_MAX) {
		if (ffsGetVolInfo(sb, &info) == -EIO)
			return -EIO;

	} else {
		info.FatType = p_fs->vol_type;
		info.ClusterSize = p_fs->cluster_size;
		info.NumClusters = p_fs->num_clusters - 2;
		info.UsedClusters = p_fs->used_clusters;
		info.FreeClusters = info.NumClusters - info.UsedClusters;

		if (p_fs->dev_ejected)
			pr_info("[EXFAT] statfs on device that is ejected\n");
	}

	buf->f_type = sb->s_magic;
	buf->f_bsize = info.ClusterSize;
	buf->f_blocks = info.NumClusters;
	buf->f_bfree = info.FreeClusters;
	buf->f_bavail = info.FreeClusters;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = 260;

	return 0;
}

static int exfat_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= SB_NODIRATIME;
	return 0;
}

static int exfat_show_options(struct seq_file *m, struct dentry *root)
{
	struct exfat_sb_info *sbi = EXFAT_SB(root->d_sb);
	struct exfat_mount_options *opts = &sbi->options;

	if (__kuid_val(opts->fs_uid))
		seq_printf(m, ",uid=%u", __kuid_val(opts->fs_uid));
	if (__kgid_val(opts->fs_gid))
		seq_printf(m, ",gid=%u", __kgid_val(opts->fs_gid));
	seq_printf(m, ",fmask=%04o", opts->fs_fmask);
	seq_printf(m, ",dmask=%04o", opts->fs_dmask);
	if (opts->allow_utime)
		seq_printf(m, ",allow_utime=%04o", opts->allow_utime);
	if (sbi->nls_disk)
		seq_printf(m, ",codepage=%s", sbi->nls_disk->charset);
	if (sbi->nls_io)
		seq_printf(m, ",iocharset=%s", sbi->nls_io->charset);
	seq_printf(m, ",namecase=%u", opts->casesensitive);
	if (opts->errors == EXFAT_ERRORS_CONT)
		seq_puts(m, ",errors=continue");
	else if (opts->errors == EXFAT_ERRORS_PANIC)
		seq_puts(m, ",errors=panic");
	else
		seq_puts(m, ",errors=remount-ro");
#ifdef CONFIG_STAGING_EXFAT_DISCARD
	if (opts->discard)
		seq_puts(m, ",discard");
#endif
	return 0;
}

static const struct super_operations exfat_sops = {
	.alloc_inode   = exfat_alloc_inode,
	.destroy_inode = exfat_destroy_inode,
	.write_inode   = exfat_write_inode,
	.evict_inode  = exfat_evict_inode,
	.put_super     = exfat_put_super,
	.sync_fs       = exfat_sync_fs,
	.statfs        = exfat_statfs,
	.remount_fs    = exfat_remount,
	.show_options  = exfat_show_options,
};

/*======================================================================*/
/*  Export Operations                                                   */
/*======================================================================*/

static struct inode *exfat_nfs_get_inode(struct super_block *sb, u64 ino,
					 u32 generation)
{
	struct inode *inode = NULL;

	if (ino < EXFAT_ROOT_INO)
		return inode;
	inode = ilookup(sb, ino);

	if (inode && generation && (inode->i_generation != generation)) {
		iput(inode);
		inode = NULL;
	}

	return inode;
}

static struct dentry *exfat_fh_to_dentry(struct super_block *sb,
					 struct fid *fid, int fh_len,
					 int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				exfat_nfs_get_inode);
}

static struct dentry *exfat_fh_to_parent(struct super_block *sb,
					 struct fid *fid, int fh_len,
					 int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				exfat_nfs_get_inode);
}

static const struct export_operations exfat_export_ops = {
	.fh_to_dentry   = exfat_fh_to_dentry,
	.fh_to_parent   = exfat_fh_to_parent,
};

/*======================================================================*/
/*  Super Block Read Operations                                         */
/*======================================================================*/

enum {
	Opt_uid,
	Opt_gid,
	Opt_umask,
	Opt_dmask,
	Opt_fmask,
	Opt_allow_utime,
	Opt_codepage,
	Opt_charset,
	Opt_namecase,
	Opt_debug,
	Opt_err_cont,
	Opt_err_panic,
	Opt_err_ro,
	Opt_utf8_hack,
	Opt_err,
#ifdef CONFIG_STAGING_EXFAT_DISCARD
	Opt_discard,
#endif /* EXFAT_CONFIG_DISCARD */
};

static const match_table_t exfat_tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_dmask, "dmask=%o"},
	{Opt_fmask, "fmask=%o"},
	{Opt_allow_utime, "allow_utime=%o"},
	{Opt_codepage, "codepage=%u"},
	{Opt_charset, "iocharset=%s"},
	{Opt_namecase, "namecase=%u"},
	{Opt_debug, "debug"},
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_utf8_hack, "utf8"},
#ifdef CONFIG_STAGING_EXFAT_DISCARD
	{Opt_discard, "discard"},
#endif /* CONFIG_STAGING_EXFAT_DISCARD */
	{Opt_err, NULL}
};

static int parse_options(char *options, int silent, int *debug,
			 struct exfat_mount_options *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	char *iocharset;

	opts->fs_uid = current_uid();
	opts->fs_gid = current_gid();
	opts->fs_fmask = current->fs->umask;
	opts->fs_dmask = current->fs->umask;
	opts->allow_utime = U16_MAX;
	opts->codepage = exfat_default_codepage;
	opts->iocharset = exfat_default_iocharset;
	opts->casesensitive = 0;
	opts->errors = EXFAT_ERRORS_RO;
#ifdef CONFIG_STAGING_EXFAT_DISCARD
	opts->discard = 0;
#endif
	*debug = 0;

	if (!options)
		goto out;

	while ((p = strsep(&options, ","))) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, exfat_tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 0;
			opts->fs_uid = KUIDT_INIT(option);
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
			opts->fs_gid = KGIDT_INIT(option);
			break;
		case Opt_umask:
		case Opt_dmask:
		case Opt_fmask:
			if (match_octal(&args[0], &option))
				return 0;
			if (token != Opt_dmask)
				opts->fs_fmask = option;
			if (token != Opt_fmask)
				opts->fs_dmask = option;
			break;
		case Opt_allow_utime:
			if (match_octal(&args[0], &option))
				return 0;
			opts->allow_utime = option & 0022;
			break;
		case Opt_codepage:
			if (match_int(&args[0], &option))
				return 0;
			opts->codepage = option;
			break;
		case Opt_charset:
			if (opts->iocharset != exfat_default_iocharset)
				kfree(opts->iocharset);
			iocharset = match_strdup(&args[0]);
			if (!iocharset)
				return -ENOMEM;
			opts->iocharset = iocharset;
			break;
		case Opt_namecase:
			if (match_int(&args[0], &option))
				return 0;
			opts->casesensitive = option;
			break;
		case Opt_err_cont:
			opts->errors = EXFAT_ERRORS_CONT;
			break;
		case Opt_err_panic:
			opts->errors = EXFAT_ERRORS_PANIC;
			break;
		case Opt_err_ro:
			opts->errors = EXFAT_ERRORS_RO;
			break;
		case Opt_debug:
			*debug = 1;
			break;
#ifdef CONFIG_STAGING_EXFAT_DISCARD
		case Opt_discard:
			opts->discard = 1;
			break;
#endif /* CONFIG_STAGING_EXFAT_DISCARD */
		case Opt_utf8_hack:
			break;
		default:
			if (!silent)
				pr_err("[EXFAT] Unrecognized mount option %s or missing value\n",
				       p);
			return -EINVAL;
		}
	}

out:
	if (opts->allow_utime == U16_MAX)
		opts->allow_utime = ~opts->fs_dmask & 0022;

	return 0;
}

static void exfat_hash_init(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int i;

	spin_lock_init(&sbi->inode_hash_lock);
	for (i = 0; i < EXFAT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&sbi->inode_hashtable[i]);
}

static int exfat_read_root(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct fs_info_t *p_fs = &sbi->fs_info;
	struct timespec64 curtime;
	struct dir_entry_t info;

	EXFAT_I(inode)->fid.dir.dir = p_fs->root_dir;
	EXFAT_I(inode)->fid.dir.flags = 0x01;
	EXFAT_I(inode)->fid.entry = -1;
	EXFAT_I(inode)->fid.start_clu = p_fs->root_dir;
	EXFAT_I(inode)->fid.flags = 0x01;
	EXFAT_I(inode)->fid.type = TYPE_DIR;
	EXFAT_I(inode)->fid.rwoffset = 0;
	EXFAT_I(inode)->fid.hint_last_off = -1;

	EXFAT_I(inode)->target = NULL;

	ffsReadStat(inode, &info);

	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	INC_IVERSION(inode);
	inode->i_generation = 0;
	inode->i_mode = exfat_make_mode(sbi, ATTR_SUBDIR, 0777);
	inode->i_op = &exfat_dir_inode_operations;
	inode->i_fop = &exfat_dir_operations;

	i_size_write(inode, info.Size);
	inode->i_blocks = ((i_size_read(inode) + (p_fs->cluster_size - 1))
				& ~((loff_t)p_fs->cluster_size - 1)) >> 9;
	EXFAT_I(inode)->i_pos = ((loff_t)p_fs->root_dir << 32) | 0xffffffff;
	EXFAT_I(inode)->mmu_private = i_size_read(inode);

	exfat_save_attr(inode, ATTR_SUBDIR);
	curtime = current_time(inode);
	inode->i_mtime = curtime;
	inode->i_atime = curtime;
	inode->i_ctime = curtime;
	set_nlink(inode, info.NumSubdirs + 2);

	return 0;
}

static void setup_dops(struct super_block *sb)
{
	if (EXFAT_SB(sb)->options.casesensitive == 0)
		sb->s_d_op = &exfat_ci_dentry_ops;
	else
		sb->s_d_op = &exfat_dentry_ops;
}

static int exfat_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode = NULL;
	struct exfat_sb_info *sbi;
	int debug, ret;
	long error;

	/*
	 * GFP_KERNEL is ok here, because while we do hold the
	 * supeblock lock, memory pressure can't call back into
	 * the filesystem, since we're only just about to mount
	 * it and have no inodes etc active!
	 */
	sbi = kvzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	mutex_init(&sbi->s_lock);
	sb->s_fs_info = sbi;
	sb->s_flags |= SB_NODIRATIME;
	sb->s_magic = EXFAT_SUPER_MAGIC;
	sb->s_op = &exfat_sops;
	sb->s_export_op = &exfat_export_ops;

	error = parse_options(data, silent, &debug, &sbi->options);
	if (error)
		goto out_fail;

	setup_dops(sb);

	error = -EIO;
	sb_min_blocksize(sb, 512);
	sb->s_maxbytes = 0x7fffffffffffffffLL;    /* maximum file size */

	ret = ffsMountVol(sb);
	if (ret) {
		if (!silent)
			pr_err("[EXFAT] ffsMountVol failed\n");

		goto out_fail;
	}

	/* set up enough so that it can read an inode */
	exfat_hash_init(sb);

	/*
	 * The low byte of FAT's first entry must have same value with
	 * media-field.  But in real world, too many devices is
	 * writing wrong value.  So, removed that validity check.
	 *
	 * if (FAT_FIRST_ENT(sb, media) != first)
	 */

	sbi->nls_io = load_nls(sbi->options.iocharset);

	error = -ENOMEM;
	root_inode = new_inode(sb);
	if (!root_inode)
		goto out_fail2;
	root_inode->i_ino = EXFAT_ROOT_INO;
	SET_IVERSION(root_inode, 1);

	error = exfat_read_root(root_inode);
	if (error < 0)
		goto out_fail2;
	error = -ENOMEM;
	exfat_attach(root_inode, EXFAT_I(root_inode)->i_pos);
	insert_inode_hash(root_inode);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		pr_err("[EXFAT] Getting the root inode failed\n");
		goto out_fail2;
	}

	return 0;

out_fail2:
	ffsUmountVol(sb);
out_fail:
	if (root_inode)
		iput(root_inode);
	sb->s_fs_info = NULL;
	exfat_free_super(sbi);
	return error;
}

static struct dentry *exfat_fs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, exfat_fill_super);
}

static void init_once(void *foo)
{
	struct exfat_inode_info *ei = (struct exfat_inode_info *)foo;

	INIT_HLIST_NODE(&ei->i_hash_fat);
	inode_init_once(&ei->vfs_inode);
}

static int __init exfat_init_inodecache(void)
{
	exfat_inode_cachep = kmem_cache_create("exfat_inode_cache",
					       sizeof(struct exfat_inode_info),
					       0,
					       (SLAB_RECLAIM_ACCOUNT |
						SLAB_MEM_SPREAD),
					       init_once);
	if (!exfat_inode_cachep)
		return -ENOMEM;
	return 0;
}

static void __exit exfat_destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(exfat_inode_cachep);
}

#ifdef CONFIG_STAGING_EXFAT_KERNEL_DEBUG
static void exfat_debug_kill_sb(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct block_device *bdev = sb->s_bdev;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	long flags;

	if (sbi) {
		flags = sbi->debug_flags;

		if (flags & EXFAT_DEBUGFLAGS_INVALID_UMOUNT) {
			/*
			 * invalidate_bdev drops all device cache include
			 * dirty. We use this to simulate device removal.
			 */
			mutex_lock(&p_fs->v_mutex);
			exfat_fat_release_all(sb);
			exfat_buf_release_all(sb);
			mutex_unlock(&p_fs->v_mutex);

			invalidate_bdev(bdev);
		}
	}

	kill_block_super(sb);
}
#endif /* CONFIG_STAGING_EXFAT_KERNEL_DEBUG */

static struct file_system_type exfat_fs_type = {
	.owner       = THIS_MODULE,
	.name        = "exfat",
	.mount       = exfat_fs_mount,
#ifdef CONFIG_STAGING_EXFAT_KERNEL_DEBUG
	.kill_sb    = exfat_debug_kill_sb,
#else
	.kill_sb    = kill_block_super,
#endif /* CONFIG_STAGING_EXFAT_KERNEL_DEBUG */
	.fs_flags    = FS_REQUIRES_DEV,
};

static int __init init_exfat(void)
{
	int err;

	BUILD_BUG_ON(sizeof(struct dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct dos_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct ext_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct file_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct strm_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct name_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct bmap_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct case_dentry_t) != DENTRY_SIZE);
	BUILD_BUG_ON(sizeof(struct volm_dentry_t) != DENTRY_SIZE);

	pr_info("exFAT: Version %s\n", EXFAT_VERSION);

	err = exfat_init_inodecache();
	if (err)
		return err;

	err = register_filesystem(&exfat_fs_type);
	if (err)
		return err;

	return 0;
}

static void __exit exit_exfat(void)
{
	exfat_destroy_inodecache();
	unregister_filesystem(&exfat_fs_type);
}

module_init(init_exfat);
module_exit(exit_exfat);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("exFAT Filesystem Driver");
MODULE_ALIAS_FS("exfat");
