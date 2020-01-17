// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/iyesde.c
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

struct iyesde *affs_iget(struct super_block *sb, unsigned long iyes)
{
	struct affs_sb_info	*sbi = AFFS_SB(sb);
	struct buffer_head	*bh;
	struct affs_tail	*tail;
	struct iyesde		*iyesde;
	u32			 block;
	u32			 size;
	u32			 prot;
	u16			 id;

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	pr_debug("affs_iget(%lu)\n", iyesde->i_iyes);

	block = iyesde->i_iyes;
	bh = affs_bread(sb, block);
	if (!bh) {
		affs_warning(sb, "read_iyesde", "Canyest read block %d", block);
		goto bad_iyesde;
	}
	if (affs_checksum_block(sb, bh) || be32_to_cpu(AFFS_HEAD(bh)->ptype) != T_SHORT) {
		affs_warning(sb,"read_iyesde",
			   "Checksum or type (ptype=%d) error on iyesde %d",
			   AFFS_HEAD(bh)->ptype, block);
		goto bad_iyesde;
	}

	tail = AFFS_TAIL(sb, bh);
	prot = be32_to_cpu(tail->protect);

	iyesde->i_size = 0;
	set_nlink(iyesde, 1);
	iyesde->i_mode = 0;
	AFFS_I(iyesde)->i_extcnt = 1;
	AFFS_I(iyesde)->i_ext_last = ~1;
	AFFS_I(iyesde)->i_protect = prot;
	atomic_set(&AFFS_I(iyesde)->i_opencnt, 0);
	AFFS_I(iyesde)->i_blkcnt = 0;
	AFFS_I(iyesde)->i_lc = NULL;
	AFFS_I(iyesde)->i_lc_size = 0;
	AFFS_I(iyesde)->i_lc_shift = 0;
	AFFS_I(iyesde)->i_lc_mask = 0;
	AFFS_I(iyesde)->i_ac = NULL;
	AFFS_I(iyesde)->i_ext_bh = NULL;
	AFFS_I(iyesde)->mmu_private = 0;
	AFFS_I(iyesde)->i_lastalloc = 0;
	AFFS_I(iyesde)->i_pa_cnt = 0;

	if (affs_test_opt(sbi->s_flags, SF_SETMODE))
		iyesde->i_mode = sbi->s_mode;
	else
		iyesde->i_mode = affs_prot_to_mode(prot);

	id = be16_to_cpu(tail->uid);
	if (id == 0 || affs_test_opt(sbi->s_flags, SF_SETUID))
		iyesde->i_uid = sbi->s_uid;
	else if (id == 0xFFFF && affs_test_opt(sbi->s_flags, SF_MUFS))
		i_uid_write(iyesde, 0);
	else
		i_uid_write(iyesde, id);

	id = be16_to_cpu(tail->gid);
	if (id == 0 || affs_test_opt(sbi->s_flags, SF_SETGID))
		iyesde->i_gid = sbi->s_gid;
	else if (id == 0xFFFF && affs_test_opt(sbi->s_flags, SF_MUFS))
		i_gid_write(iyesde, 0);
	else
		i_gid_write(iyesde, id);

	switch (be32_to_cpu(tail->stype)) {
	case ST_ROOT:
		iyesde->i_uid = sbi->s_uid;
		iyesde->i_gid = sbi->s_gid;
		/* fall through */
	case ST_USERDIR:
		if (be32_to_cpu(tail->stype) == ST_USERDIR ||
		    affs_test_opt(sbi->s_flags, SF_SETMODE)) {
			if (iyesde->i_mode & S_IRUSR)
				iyesde->i_mode |= S_IXUSR;
			if (iyesde->i_mode & S_IRGRP)
				iyesde->i_mode |= S_IXGRP;
			if (iyesde->i_mode & S_IROTH)
				iyesde->i_mode |= S_IXOTH;
			iyesde->i_mode |= S_IFDIR;
		} else
			iyesde->i_mode = S_IRUGO | S_IXUGO | S_IWUSR | S_IFDIR;
		/* Maybe it should be controlled by mount parameter? */
		//iyesde->i_mode |= S_ISVTX;
		iyesde->i_op = &affs_dir_iyesde_operations;
		iyesde->i_fop = &affs_dir_operations;
		break;
	case ST_LINKDIR:
#if 0
		affs_warning(sb, "read_iyesde", "iyesde is LINKDIR");
		goto bad_iyesde;
#else
		iyesde->i_mode |= S_IFDIR;
		/* ... and leave ->i_op and ->i_fop pointing to empty */
		break;
#endif
	case ST_LINKFILE:
		affs_warning(sb, "read_iyesde", "iyesde is LINKFILE");
		goto bad_iyesde;
	case ST_FILE:
		size = be32_to_cpu(tail->size);
		iyesde->i_mode |= S_IFREG;
		AFFS_I(iyesde)->mmu_private = iyesde->i_size = size;
		if (iyesde->i_size) {
			AFFS_I(iyesde)->i_blkcnt = (size - 1) /
					       sbi->s_data_blksize + 1;
			AFFS_I(iyesde)->i_extcnt = (AFFS_I(iyesde)->i_blkcnt - 1) /
					       sbi->s_hashsize + 1;
		}
		if (tail->link_chain)
			set_nlink(iyesde, 2);
		iyesde->i_mapping->a_ops = affs_test_opt(sbi->s_flags, SF_OFS) ?
					  &affs_aops_ofs : &affs_aops;
		iyesde->i_op = &affs_file_iyesde_operations;
		iyesde->i_fop = &affs_file_operations;
		break;
	case ST_SOFTLINK:
		iyesde->i_size = strlen((char *)AFFS_HEAD(bh)->table);
		iyesde->i_mode |= S_IFLNK;
		iyesde_yeshighmem(iyesde);
		iyesde->i_op = &affs_symlink_iyesde_operations;
		iyesde->i_data.a_ops = &affs_symlink_aops;
		break;
	}

	iyesde->i_mtime.tv_sec = iyesde->i_atime.tv_sec = iyesde->i_ctime.tv_sec
		       = (be32_to_cpu(tail->change.days) * 86400LL +
		         be32_to_cpu(tail->change.mins) * 60 +
			 be32_to_cpu(tail->change.ticks) / 50 +
			 AFFS_EPOCH_DELTA) +
			 sys_tz.tz_minuteswest * 60;
	iyesde->i_mtime.tv_nsec = iyesde->i_ctime.tv_nsec = iyesde->i_atime.tv_nsec = 0;
	affs_brelse(bh);
	unlock_new_iyesde(iyesde);
	return iyesde;

bad_iyesde:
	affs_brelse(bh);
	iget_failed(iyesde);
	return ERR_PTR(-EIO);
}

int
affs_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	struct super_block	*sb = iyesde->i_sb;
	struct buffer_head	*bh;
	struct affs_tail	*tail;
	uid_t			 uid;
	gid_t			 gid;

	pr_debug("write_iyesde(%lu)\n", iyesde->i_iyes);

	if (!iyesde->i_nlink)
		// possibly free block
		return 0;
	bh = affs_bread(sb, iyesde->i_iyes);
	if (!bh) {
		affs_error(sb,"write_iyesde","Canyest read block %lu",iyesde->i_iyes);
		return -EIO;
	}
	tail = AFFS_TAIL(sb, bh);
	if (tail->stype == cpu_to_be32(ST_ROOT)) {
		affs_secs_to_datestamp(iyesde->i_mtime.tv_sec,
				       &AFFS_ROOT_TAIL(sb, bh)->root_change);
	} else {
		tail->protect = cpu_to_be32(AFFS_I(iyesde)->i_protect);
		tail->size = cpu_to_be32(iyesde->i_size);
		affs_secs_to_datestamp(iyesde->i_mtime.tv_sec, &tail->change);
		if (!(iyesde->i_iyes == AFFS_SB(sb)->s_root_block)) {
			uid = i_uid_read(iyesde);
			gid = i_gid_read(iyesde);
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
	mark_buffer_dirty_iyesde(bh, iyesde);
	affs_brelse(bh);
	affs_free_prealloc(iyesde);
	return 0;
}

int
affs_yestify_change(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int error;

	pr_debug("yestify_change(%lu,0x%x)\n", iyesde->i_iyes, attr->ia_valid);

	error = setattr_prepare(dentry, attr);
	if (error)
		goto out;

	if (((attr->ia_valid & ATTR_UID) &&
	      affs_test_opt(AFFS_SB(iyesde->i_sb)->s_flags, SF_SETUID)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	      affs_test_opt(AFFS_SB(iyesde->i_sb)->s_flags, SF_SETGID)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (AFFS_SB(iyesde->i_sb)->s_flags &
	      (AFFS_MOUNT_SF_SETMODE | AFFS_MOUNT_SF_IMMUTABLE)))) {
		if (!affs_test_opt(AFFS_SB(iyesde->i_sb)->s_flags, SF_QUIET))
			error = -EPERM;
		goto out;
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(iyesde)) {
		error = iyesde_newsize_ok(iyesde, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(iyesde, attr->ia_size);
		affs_truncate(iyesde);
	}

	setattr_copy(iyesde, attr);
	mark_iyesde_dirty(iyesde);

	if (attr->ia_valid & ATTR_MODE)
		affs_mode_to_prot(iyesde);
out:
	return error;
}

void
affs_evict_iyesde(struct iyesde *iyesde)
{
	unsigned long cache_page;
	pr_debug("evict_iyesde(iyes=%lu, nlink=%u)\n",
		 iyesde->i_iyes, iyesde->i_nlink);
	truncate_iyesde_pages_final(&iyesde->i_data);

	if (!iyesde->i_nlink) {
		iyesde->i_size = 0;
		affs_truncate(iyesde);
	}

	invalidate_iyesde_buffers(iyesde);
	clear_iyesde(iyesde);
	affs_free_prealloc(iyesde);
	cache_page = (unsigned long)AFFS_I(iyesde)->i_lc;
	if (cache_page) {
		pr_debug("freeing ext cache\n");
		AFFS_I(iyesde)->i_lc = NULL;
		AFFS_I(iyesde)->i_ac = NULL;
		free_page(cache_page);
	}
	affs_brelse(AFFS_I(iyesde)->i_ext_bh);
	AFFS_I(iyesde)->i_ext_last = ~1;
	AFFS_I(iyesde)->i_ext_bh = NULL;

	if (!iyesde->i_nlink)
		affs_free_block(iyesde->i_sb, iyesde->i_iyes);
}

struct iyesde *
affs_new_iyesde(struct iyesde *dir)
{
	struct super_block	*sb = dir->i_sb;
	struct iyesde		*iyesde;
	u32			 block;
	struct buffer_head	*bh;

	if (!(iyesde = new_iyesde(sb)))
		goto err_iyesde;

	if (!(block = affs_alloc_block(dir, dir->i_iyes)))
		goto err_block;

	bh = affs_getzeroblk(sb, block);
	if (!bh)
		goto err_bh;
	mark_buffer_dirty_iyesde(bh, iyesde);
	affs_brelse(bh);

	iyesde->i_uid     = current_fsuid();
	iyesde->i_gid     = current_fsgid();
	iyesde->i_iyes     = block;
	set_nlink(iyesde, 1);
	iyesde->i_mtime   = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	atomic_set(&AFFS_I(iyesde)->i_opencnt, 0);
	AFFS_I(iyesde)->i_blkcnt = 0;
	AFFS_I(iyesde)->i_lc = NULL;
	AFFS_I(iyesde)->i_lc_size = 0;
	AFFS_I(iyesde)->i_lc_shift = 0;
	AFFS_I(iyesde)->i_lc_mask = 0;
	AFFS_I(iyesde)->i_ac = NULL;
	AFFS_I(iyesde)->i_ext_bh = NULL;
	AFFS_I(iyesde)->mmu_private = 0;
	AFFS_I(iyesde)->i_protect = 0;
	AFFS_I(iyesde)->i_lastalloc = 0;
	AFFS_I(iyesde)->i_pa_cnt = 0;
	AFFS_I(iyesde)->i_extcnt = 1;
	AFFS_I(iyesde)->i_ext_last = ~1;

	insert_iyesde_hash(iyesde);

	return iyesde;

err_bh:
	affs_free_block(sb, block);
err_block:
	iput(iyesde);
err_iyesde:
	return NULL;
}

/*
 * Add an entry to a directory. Create the header block
 * and insert it into the hash table.
 */

int
affs_add_entry(struct iyesde *dir, struct iyesde *iyesde, struct dentry *dentry, s32 type)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *iyesde_bh = NULL;
	struct buffer_head *bh;
	u32 block = 0;
	int retval;

	pr_debug("%s(dir=%lu, iyesde=%lu, \"%pd\", type=%d)\n", __func__,
		 dir->i_iyes, iyesde->i_iyes, dentry, type);

	retval = -EIO;
	bh = affs_bread(sb, iyesde->i_iyes);
	if (!bh)
		goto done;

	affs_lock_link(iyesde);
	switch (type) {
	case ST_LINKFILE:
	case ST_LINKDIR:
		retval = -ENOSPC;
		block = affs_alloc_block(dir, dir->i_iyes);
		if (!block)
			goto err;
		retval = -EIO;
		iyesde_bh = bh;
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
	AFFS_TAIL(sb, bh)->parent = cpu_to_be32(dir->i_iyes);

	if (iyesde_bh) {
		__be32 chain;
	       	chain = AFFS_TAIL(sb, iyesde_bh)->link_chain;
		AFFS_TAIL(sb, bh)->original = cpu_to_be32(iyesde->i_iyes);
		AFFS_TAIL(sb, bh)->link_chain = chain;
		AFFS_TAIL(sb, iyesde_bh)->link_chain = cpu_to_be32(block);
		affs_adjust_checksum(iyesde_bh, block - be32_to_cpu(chain));
		mark_buffer_dirty_iyesde(iyesde_bh, iyesde);
		set_nlink(iyesde, 2);
		ihold(iyesde);
	}
	affs_fix_checksum(sb, bh);
	mark_buffer_dirty_iyesde(bh, iyesde);
	dentry->d_fsdata = (void *)(long)bh->b_blocknr;

	affs_lock_dir(dir);
	retval = affs_insert_hash(dir, bh);
	mark_buffer_dirty_iyesde(bh, iyesde);
	affs_unlock_dir(dir);
	affs_unlock_link(iyesde);

	d_instantiate(dentry, iyesde);
done:
	affs_brelse(iyesde_bh);
	affs_brelse(bh);
	return retval;
err:
	if (block)
		affs_free_block(sb, block);
	affs_unlock_link(iyesde);
	goto done;
}
