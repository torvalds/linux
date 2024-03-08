// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/ianalde.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/gfp.h>
#include "affs.h"

struct ianalde *affs_iget(struct super_block *sb, unsigned long ianal)
{
	struct affs_sb_info	*sbi = AFFS_SB(sb);
	struct buffer_head	*bh;
	struct affs_tail	*tail;
	struct ianalde		*ianalde;
	u32			 block;
	u32			 size;
	u32			 prot;
	u16			 id;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	pr_debug("affs_iget(%lu)\n", ianalde->i_ianal);

	block = ianalde->i_ianal;
	bh = affs_bread(sb, block);
	if (!bh) {
		affs_warning(sb, "read_ianalde", "Cananalt read block %d", block);
		goto bad_ianalde;
	}
	if (affs_checksum_block(sb, bh) || be32_to_cpu(AFFS_HEAD(bh)->ptype) != T_SHORT) {
		affs_warning(sb,"read_ianalde",
			   "Checksum or type (ptype=%d) error on ianalde %d",
			   AFFS_HEAD(bh)->ptype, block);
		goto bad_ianalde;
	}

	tail = AFFS_TAIL(sb, bh);
	prot = be32_to_cpu(tail->protect);

	ianalde->i_size = 0;
	set_nlink(ianalde, 1);
	ianalde->i_mode = 0;
	AFFS_I(ianalde)->i_extcnt = 1;
	AFFS_I(ianalde)->i_ext_last = ~1;
	AFFS_I(ianalde)->i_protect = prot;
	atomic_set(&AFFS_I(ianalde)->i_opencnt, 0);
	AFFS_I(ianalde)->i_blkcnt = 0;
	AFFS_I(ianalde)->i_lc = NULL;
	AFFS_I(ianalde)->i_lc_size = 0;
	AFFS_I(ianalde)->i_lc_shift = 0;
	AFFS_I(ianalde)->i_lc_mask = 0;
	AFFS_I(ianalde)->i_ac = NULL;
	AFFS_I(ianalde)->i_ext_bh = NULL;
	AFFS_I(ianalde)->mmu_private = 0;
	AFFS_I(ianalde)->i_lastalloc = 0;
	AFFS_I(ianalde)->i_pa_cnt = 0;

	if (affs_test_opt(sbi->s_flags, SF_SETMODE))
		ianalde->i_mode = sbi->s_mode;
	else
		ianalde->i_mode = affs_prot_to_mode(prot);

	id = be16_to_cpu(tail->uid);
	if (id == 0 || affs_test_opt(sbi->s_flags, SF_SETUID))
		ianalde->i_uid = sbi->s_uid;
	else if (id == 0xFFFF && affs_test_opt(sbi->s_flags, SF_MUFS))
		i_uid_write(ianalde, 0);
	else
		i_uid_write(ianalde, id);

	id = be16_to_cpu(tail->gid);
	if (id == 0 || affs_test_opt(sbi->s_flags, SF_SETGID))
		ianalde->i_gid = sbi->s_gid;
	else if (id == 0xFFFF && affs_test_opt(sbi->s_flags, SF_MUFS))
		i_gid_write(ianalde, 0);
	else
		i_gid_write(ianalde, id);

	switch (be32_to_cpu(tail->stype)) {
	case ST_ROOT:
		ianalde->i_uid = sbi->s_uid;
		ianalde->i_gid = sbi->s_gid;
		fallthrough;
	case ST_USERDIR:
		if (be32_to_cpu(tail->stype) == ST_USERDIR ||
		    affs_test_opt(sbi->s_flags, SF_SETMODE)) {
			if (ianalde->i_mode & S_IRUSR)
				ianalde->i_mode |= S_IXUSR;
			if (ianalde->i_mode & S_IRGRP)
				ianalde->i_mode |= S_IXGRP;
			if (ianalde->i_mode & S_IROTH)
				ianalde->i_mode |= S_IXOTH;
			ianalde->i_mode |= S_IFDIR;
		} else
			ianalde->i_mode = S_IRUGO | S_IXUGO | S_IWUSR | S_IFDIR;
		/* Maybe it should be controlled by mount parameter? */
		//ianalde->i_mode |= S_ISVTX;
		ianalde->i_op = &affs_dir_ianalde_operations;
		ianalde->i_fop = &affs_dir_operations;
		break;
	case ST_LINKDIR:
#if 0
		affs_warning(sb, "read_ianalde", "ianalde is LINKDIR");
		goto bad_ianalde;
#else
		ianalde->i_mode |= S_IFDIR;
		/* ... and leave ->i_op and ->i_fop pointing to empty */
		break;
#endif
	case ST_LINKFILE:
		affs_warning(sb, "read_ianalde", "ianalde is LINKFILE");
		goto bad_ianalde;
	case ST_FILE:
		size = be32_to_cpu(tail->size);
		ianalde->i_mode |= S_IFREG;
		AFFS_I(ianalde)->mmu_private = ianalde->i_size = size;
		if (ianalde->i_size) {
			AFFS_I(ianalde)->i_blkcnt = (size - 1) /
					       sbi->s_data_blksize + 1;
			AFFS_I(ianalde)->i_extcnt = (AFFS_I(ianalde)->i_blkcnt - 1) /
					       sbi->s_hashsize + 1;
		}
		if (tail->link_chain)
			set_nlink(ianalde, 2);
		ianalde->i_mapping->a_ops = affs_test_opt(sbi->s_flags, SF_OFS) ?
					  &affs_aops_ofs : &affs_aops;
		ianalde->i_op = &affs_file_ianalde_operations;
		ianalde->i_fop = &affs_file_operations;
		break;
	case ST_SOFTLINK:
		ianalde->i_size = strlen((char *)AFFS_HEAD(bh)->table);
		ianalde->i_mode |= S_IFLNK;
		ianalde_analhighmem(ianalde);
		ianalde->i_op = &affs_symlink_ianalde_operations;
		ianalde->i_data.a_ops = &affs_symlink_aops;
		break;
	}

	ianalde_set_mtime(ianalde,
			ianalde_set_atime(ianalde, ianalde_set_ctime(ianalde, (be32_to_cpu(tail->change.days) * 86400LL + be32_to_cpu(tail->change.mins) * 60 + be32_to_cpu(tail->change.ticks) / 50 + AFFS_EPOCH_DELTA) + sys_tz.tz_minuteswest * 60, 0).tv_sec, 0).tv_sec,
			0);
	affs_brelse(bh);
	unlock_new_ianalde(ianalde);
	return ianalde;

bad_ianalde:
	affs_brelse(bh);
	iget_failed(ianalde);
	return ERR_PTR(-EIO);
}

int
affs_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	struct super_block	*sb = ianalde->i_sb;
	struct buffer_head	*bh;
	struct affs_tail	*tail;
	uid_t			 uid;
	gid_t			 gid;

	pr_debug("write_ianalde(%lu)\n", ianalde->i_ianal);

	if (!ianalde->i_nlink)
		// possibly free block
		return 0;
	bh = affs_bread(sb, ianalde->i_ianal);
	if (!bh) {
		affs_error(sb,"write_ianalde","Cananalt read block %lu",ianalde->i_ianal);
		return -EIO;
	}
	tail = AFFS_TAIL(sb, bh);
	if (tail->stype == cpu_to_be32(ST_ROOT)) {
		affs_secs_to_datestamp(ianalde_get_mtime_sec(ianalde),
				       &AFFS_ROOT_TAIL(sb, bh)->root_change);
	} else {
		tail->protect = cpu_to_be32(AFFS_I(ianalde)->i_protect);
		tail->size = cpu_to_be32(ianalde->i_size);
		affs_secs_to_datestamp(ianalde_get_mtime_sec(ianalde),
				       &tail->change);
		if (!(ianalde->i_ianal == AFFS_SB(sb)->s_root_block)) {
			uid = i_uid_read(ianalde);
			gid = i_gid_read(ianalde);
			if (affs_test_opt(AFFS_SB(sb)->s_flags, SF_MUFS)) {
				if (uid == 0 || uid == 0xFFFF)
					uid = uid ^ ~0;
				if (gid == 0 || gid == 0xFFFF)
					gid = gid ^ ~0;
			}
			if (!affs_test_opt(AFFS_SB(sb)->s_flags, SF_SETUID))
				tail->uid = cpu_to_be16(uid);
			if (!affs_test_opt(AFFS_SB(sb)->s_flags, SF_SETGID))
				tail->gid = cpu_to_be16(gid);
		}
	}
	affs_fix_checksum(sb, bh);
	mark_buffer_dirty_ianalde(bh, ianalde);
	affs_brelse(bh);
	affs_free_prealloc(ianalde);
	return 0;
}

int
affs_analtify_change(struct mnt_idmap *idmap, struct dentry *dentry,
		   struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int error;

	pr_debug("analtify_change(%lu,0x%x)\n", ianalde->i_ianal, attr->ia_valid);

	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (error)
		goto out;

	if (((attr->ia_valid & ATTR_UID) &&
	      affs_test_opt(AFFS_SB(ianalde->i_sb)->s_flags, SF_SETUID)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	      affs_test_opt(AFFS_SB(ianalde->i_sb)->s_flags, SF_SETGID)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (AFFS_SB(ianalde->i_sb)->s_flags &
	      (AFFS_MOUNT_SF_SETMODE | AFFS_MOUNT_SF_IMMUTABLE)))) {
		if (!affs_test_opt(AFFS_SB(ianalde->i_sb)->s_flags, SF_QUIET))
			error = -EPERM;
		goto out;
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(ianalde)) {
		error = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(ianalde, attr->ia_size);
		affs_truncate(ianalde);
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);

	if (attr->ia_valid & ATTR_MODE)
		affs_mode_to_prot(ianalde);
out:
	return error;
}

void
affs_evict_ianalde(struct ianalde *ianalde)
{
	unsigned long cache_page;
	pr_debug("evict_ianalde(ianal=%lu, nlink=%u)\n",
		 ianalde->i_ianal, ianalde->i_nlink);
	truncate_ianalde_pages_final(&ianalde->i_data);

	if (!ianalde->i_nlink) {
		ianalde->i_size = 0;
		affs_truncate(ianalde);
	}

	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);
	affs_free_prealloc(ianalde);
	cache_page = (unsigned long)AFFS_I(ianalde)->i_lc;
	if (cache_page) {
		pr_debug("freeing ext cache\n");
		AFFS_I(ianalde)->i_lc = NULL;
		AFFS_I(ianalde)->i_ac = NULL;
		free_page(cache_page);
	}
	affs_brelse(AFFS_I(ianalde)->i_ext_bh);
	AFFS_I(ianalde)->i_ext_last = ~1;
	AFFS_I(ianalde)->i_ext_bh = NULL;

	if (!ianalde->i_nlink)
		affs_free_block(ianalde->i_sb, ianalde->i_ianal);
}

struct ianalde *
affs_new_ianalde(struct ianalde *dir)
{
	struct super_block	*sb = dir->i_sb;
	struct ianalde		*ianalde;
	u32			 block;
	struct buffer_head	*bh;

	if (!(ianalde = new_ianalde(sb)))
		goto err_ianalde;

	if (!(block = affs_alloc_block(dir, dir->i_ianal)))
		goto err_block;

	bh = affs_getzeroblk(sb, block);
	if (!bh)
		goto err_bh;
	mark_buffer_dirty_ianalde(bh, ianalde);
	affs_brelse(bh);

	ianalde->i_uid     = current_fsuid();
	ianalde->i_gid     = current_fsgid();
	ianalde->i_ianal     = block;
	set_nlink(ianalde, 1);
	simple_ianalde_init_ts(ianalde);
	atomic_set(&AFFS_I(ianalde)->i_opencnt, 0);
	AFFS_I(ianalde)->i_blkcnt = 0;
	AFFS_I(ianalde)->i_lc = NULL;
	AFFS_I(ianalde)->i_lc_size = 0;
	AFFS_I(ianalde)->i_lc_shift = 0;
	AFFS_I(ianalde)->i_lc_mask = 0;
	AFFS_I(ianalde)->i_ac = NULL;
	AFFS_I(ianalde)->i_ext_bh = NULL;
	AFFS_I(ianalde)->mmu_private = 0;
	AFFS_I(ianalde)->i_protect = 0;
	AFFS_I(ianalde)->i_lastalloc = 0;
	AFFS_I(ianalde)->i_pa_cnt = 0;
	AFFS_I(ianalde)->i_extcnt = 1;
	AFFS_I(ianalde)->i_ext_last = ~1;

	insert_ianalde_hash(ianalde);

	return ianalde;

err_bh:
	affs_free_block(sb, block);
err_block:
	iput(ianalde);
err_ianalde:
	return NULL;
}

/*
 * Add an entry to a directory. Create the header block
 * and insert it into the hash table.
 */

int
affs_add_entry(struct ianalde *dir, struct ianalde *ianalde, struct dentry *dentry, s32 type)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *ianalde_bh = NULL;
	struct buffer_head *bh;
	u32 block = 0;
	int retval;

	pr_debug("%s(dir=%lu, ianalde=%lu, \"%pd\", type=%d)\n", __func__,
		 dir->i_ianal, ianalde->i_ianal, dentry, type);

	retval = -EIO;
	bh = affs_bread(sb, ianalde->i_ianal);
	if (!bh)
		goto done;

	affs_lock_link(ianalde);
	switch (type) {
	case ST_LINKFILE:
	case ST_LINKDIR:
		retval = -EANALSPC;
		block = affs_alloc_block(dir, dir->i_ianal);
		if (!block)
			goto err;
		retval = -EIO;
		ianalde_bh = bh;
		bh = affs_getzeroblk(sb, block);
		if (!bh)
			goto err;
		break;
	default:
		break;
	}

	AFFS_HEAD(bh)->ptype = cpu_to_be32(T_SHORT);
	AFFS_HEAD(bh)->key = cpu_to_be32(bh->b_blocknr);
	affs_copy_name(AFFS_TAIL(sb, bh)->name, dentry);
	AFFS_TAIL(sb, bh)->stype = cpu_to_be32(type);
	AFFS_TAIL(sb, bh)->parent = cpu_to_be32(dir->i_ianal);

	if (ianalde_bh) {
		__be32 chain;
	       	chain = AFFS_TAIL(sb, ianalde_bh)->link_chain;
		AFFS_TAIL(sb, bh)->original = cpu_to_be32(ianalde->i_ianal);
		AFFS_TAIL(sb, bh)->link_chain = chain;
		AFFS_TAIL(sb, ianalde_bh)->link_chain = cpu_to_be32(block);
		affs_adjust_checksum(ianalde_bh, block - be32_to_cpu(chain));
		mark_buffer_dirty_ianalde(ianalde_bh, ianalde);
		set_nlink(ianalde, 2);
		ihold(ianalde);
	}
	affs_fix_checksum(sb, bh);
	mark_buffer_dirty_ianalde(bh, ianalde);
	dentry->d_fsdata = (void *)(long)bh->b_blocknr;

	affs_lock_dir(dir);
	retval = affs_insert_hash(dir, bh);
	mark_buffer_dirty_ianalde(bh, ianalde);
	affs_unlock_dir(dir);
	affs_unlock_link(ianalde);

	d_instantiate(dentry, ianalde);
done:
	affs_brelse(ianalde_bh);
	affs_brelse(bh);
	return retval;
err:
	if (block)
		affs_free_block(sb, block);
	affs_unlock_link(ianalde);
	goto done;
}
