/*
 *  linux/fs/affs/inode.c
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
#include <linux/gfp.h>
#include "affs.h"

struct inode *affs_iget(struct super_block *sb, unsigned long ino)
{
	struct affs_sb_info	*sbi = AFFS_SB(sb);
	struct buffer_head	*bh;
	struct affs_tail	*tail;
	struct inode		*inode;
	u32			 block;
	u32			 size;
	u32			 prot;
	u16			 id;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	pr_debug("affs_iget(%lu)\n", inode->i_ino);

	block = inode->i_ino;
	bh = affs_bread(sb, block);
	if (!bh) {
		affs_warning(sb, "read_inode", "Cannot read block %d", block);
		goto bad_inode;
	}
	if (affs_checksum_block(sb, bh) || be32_to_cpu(AFFS_HEAD(bh)->ptype) != T_SHORT) {
		affs_warning(sb,"read_inode",
			   "Checksum or type (ptype=%d) error on inode %d",
			   AFFS_HEAD(bh)->ptype, block);
		goto bad_inode;
	}

	tail = AFFS_TAIL(sb, bh);
	prot = be32_to_cpu(tail->protect);

	inode->i_size = 0;
	set_nlink(inode, 1);
	inode->i_mode = 0;
	AFFS_I(inode)->i_extcnt = 1;
	AFFS_I(inode)->i_ext_last = ~1;
	AFFS_I(inode)->i_protect = prot;
	atomic_set(&AFFS_I(inode)->i_opencnt, 0);
	AFFS_I(inode)->i_blkcnt = 0;
	AFFS_I(inode)->i_lc = NULL;
	AFFS_I(inode)->i_lc_size = 0;
	AFFS_I(inode)->i_lc_shift = 0;
	AFFS_I(inode)->i_lc_mask = 0;
	AFFS_I(inode)->i_ac = NULL;
	AFFS_I(inode)->i_ext_bh = NULL;
	AFFS_I(inode)->mmu_private = 0;
	AFFS_I(inode)->i_lastalloc = 0;
	AFFS_I(inode)->i_pa_cnt = 0;

	if (affs_test_opt(sbi->s_flags, SF_SETMODE))
		inode->i_mode = sbi->s_mode;
	else
		inode->i_mode = affs_prot_to_mode(prot);

	id = be16_to_cpu(tail->uid);
	if (id == 0 || affs_test_opt(sbi->s_flags, SF_SETUID))
		inode->i_uid = sbi->s_uid;
	else if (id == 0xFFFF && affs_test_opt(sbi->s_flags, SF_MUFS))
		i_uid_write(inode, 0);
	else
		i_uid_write(inode, id);

	id = be16_to_cpu(tail->gid);
	if (id == 0 || affs_test_opt(sbi->s_flags, SF_SETGID))
		inode->i_gid = sbi->s_gid;
	else if (id == 0xFFFF && affs_test_opt(sbi->s_flags, SF_MUFS))
		i_gid_write(inode, 0);
	else
		i_gid_write(inode, id);

	switch (be32_to_cpu(tail->stype)) {
	case ST_ROOT:
		inode->i_uid = sbi->s_uid;
		inode->i_gid = sbi->s_gid;
		/* fall through */
	case ST_USERDIR:
		if (be32_to_cpu(tail->stype) == ST_USERDIR ||
		    affs_test_opt(sbi->s_flags, SF_SETMODE)) {
			if (inode->i_mode & S_IRUSR)
				inode->i_mode |= S_IXUSR;
			if (inode->i_mode & S_IRGRP)
				inode->i_mode |= S_IXGRP;
			if (inode->i_mode & S_IROTH)
				inode->i_mode |= S_IXOTH;
			inode->i_mode |= S_IFDIR;
		} else
			inode->i_mode = S_IRUGO | S_IXUGO | S_IWUSR | S_IFDIR;
		/* Maybe it should be controlled by mount parameter? */
		//inode->i_mode |= S_ISVTX;
		inode->i_op = &affs_dir_inode_operations;
		inode->i_fop = &affs_dir_operations;
		break;
	case ST_LINKDIR:
#if 0
		affs_warning(sb, "read_inode", "inode is LINKDIR");
		goto bad_inode;
#else
		inode->i_mode |= S_IFDIR;
		/* ... and leave ->i_op and ->i_fop pointing to empty */
		break;
#endif
	case ST_LINKFILE:
		affs_warning(sb, "read_inode", "inode is LINKFILE");
		goto bad_inode;
	case ST_FILE:
		size = be32_to_cpu(tail->size);
		inode->i_mode |= S_IFREG;
		AFFS_I(inode)->mmu_private = inode->i_size = size;
		if (inode->i_size) {
			AFFS_I(inode)->i_blkcnt = (size - 1) /
					       sbi->s_data_blksize + 1;
			AFFS_I(inode)->i_extcnt = (AFFS_I(inode)->i_blkcnt - 1) /
					       sbi->s_hashsize + 1;
		}
		if (tail->link_chain)
			set_nlink(inode, 2);
		inode->i_mapping->a_ops = affs_test_opt(sbi->s_flags, SF_OFS) ?
					  &affs_aops_ofs : &affs_aops;
		inode->i_op = &affs_file_inode_operations;
		inode->i_fop = &affs_file_operations;
		break;
	case ST_SOFTLINK:
		inode->i_mode |= S_IFLNK;
		inode_nohighmem(inode);
		inode->i_op = &affs_symlink_inode_operations;
		inode->i_data.a_ops = &affs_symlink_aops;
		break;
	}

	inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec
		       = (be32_to_cpu(tail->change.days) * (24 * 60 * 60) +
		         be32_to_cpu(tail->change.mins) * 60 +
			 be32_to_cpu(tail->change.ticks) / 50 +
			 ((8 * 365 + 2) * 24 * 60 * 60)) +
			 sys_tz.tz_minuteswest * 60;
	inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = inode->i_atime.tv_nsec = 0;
	affs_brelse(bh);
	unlock_new_inode(inode);
	return inode;

bad_inode:
	affs_brelse(bh);
	iget_failed(inode);
	return ERR_PTR(-EIO);
}

int
affs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct super_block	*sb = inode->i_sb;
	struct buffer_head	*bh;
	struct affs_tail	*tail;
	uid_t			 uid;
	gid_t			 gid;

	pr_debug("write_inode(%lu)\n", inode->i_ino);

	if (!inode->i_nlink)
		// possibly free block
		return 0;
	bh = affs_bread(sb, inode->i_ino);
	if (!bh) {
		affs_error(sb,"write_inode","Cannot read block %lu",inode->i_ino);
		return -EIO;
	}
	tail = AFFS_TAIL(sb, bh);
	if (tail->stype == cpu_to_be32(ST_ROOT)) {
		affs_secs_to_datestamp(inode->i_mtime.tv_sec,
				       &AFFS_ROOT_TAIL(sb, bh)->root_change);
	} else {
		tail->protect = cpu_to_be32(AFFS_I(inode)->i_protect);
		tail->size = cpu_to_be32(inode->i_size);
		affs_secs_to_datestamp(inode->i_mtime.tv_sec, &tail->change);
		if (!(inode->i_ino == AFFS_SB(sb)->s_root_block)) {
			uid = i_uid_read(inode);
			gid = i_gid_read(inode);
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
	mark_buffer_dirty_inode(bh, inode);
	affs_brelse(bh);
	affs_free_prealloc(inode);
	return 0;
}

int
affs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	pr_debug("notify_change(%lu,0x%x)\n", inode->i_ino, attr->ia_valid);

	error = setattr_prepare(dentry, attr);
	if (error)
		goto out;

	if (((attr->ia_valid & ATTR_UID) &&
	      affs_test_opt(AFFS_SB(inode->i_sb)->s_flags, SF_SETUID)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	      affs_test_opt(AFFS_SB(inode->i_sb)->s_flags, SF_SETGID)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (AFFS_SB(inode->i_sb)->s_flags &
	      (AFFS_MOUNT_SF_SETMODE | AFFS_MOUNT_SF_IMMUTABLE)))) {
		if (!affs_test_opt(AFFS_SB(inode->i_sb)->s_flags, SF_QUIET))
			error = -EPERM;
		goto out;
	}

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(inode, attr->ia_size);
		affs_truncate(inode);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

	if (attr->ia_valid & ATTR_MODE)
		affs_mode_to_prot(inode);
out:
	return error;
}

void
affs_evict_inode(struct inode *inode)
{
	unsigned long cache_page;
	pr_debug("evict_inode(ino=%lu, nlink=%u)\n",
		 inode->i_ino, inode->i_nlink);
	truncate_inode_pages_final(&inode->i_data);

	if (!inode->i_nlink) {
		inode->i_size = 0;
		affs_truncate(inode);
	}

	invalidate_inode_buffers(inode);
	clear_inode(inode);
	affs_free_prealloc(inode);
	cache_page = (unsigned long)AFFS_I(inode)->i_lc;
	if (cache_page) {
		pr_debug("freeing ext cache\n");
		AFFS_I(inode)->i_lc = NULL;
		AFFS_I(inode)->i_ac = NULL;
		free_page(cache_page);
	}
	affs_brelse(AFFS_I(inode)->i_ext_bh);
	AFFS_I(inode)->i_ext_last = ~1;
	AFFS_I(inode)->i_ext_bh = NULL;

	if (!inode->i_nlink)
		affs_free_block(inode->i_sb, inode->i_ino);
}

struct inode *
affs_new_inode(struct inode *dir)
{
	struct super_block	*sb = dir->i_sb;
	struct inode		*inode;
	u32			 block;
	struct buffer_head	*bh;

	if (!(inode = new_inode(sb)))
		goto err_inode;

	if (!(block = affs_alloc_block(dir, dir->i_ino)))
		goto err_block;

	bh = affs_getzeroblk(sb, block);
	if (!bh)
		goto err_bh;
	mark_buffer_dirty_inode(bh, inode);
	affs_brelse(bh);

	inode->i_uid     = current_fsuid();
	inode->i_gid     = current_fsgid();
	inode->i_ino     = block;
	set_nlink(inode, 1);
	inode->i_mtime   = inode->i_atime = inode->i_ctime = current_time(inode);
	atomic_set(&AFFS_I(inode)->i_opencnt, 0);
	AFFS_I(inode)->i_blkcnt = 0;
	AFFS_I(inode)->i_lc = NULL;
	AFFS_I(inode)->i_lc_size = 0;
	AFFS_I(inode)->i_lc_shift = 0;
	AFFS_I(inode)->i_lc_mask = 0;
	AFFS_I(inode)->i_ac = NULL;
	AFFS_I(inode)->i_ext_bh = NULL;
	AFFS_I(inode)->mmu_private = 0;
	AFFS_I(inode)->i_protect = 0;
	AFFS_I(inode)->i_lastalloc = 0;
	AFFS_I(inode)->i_pa_cnt = 0;
	AFFS_I(inode)->i_extcnt = 1;
	AFFS_I(inode)->i_ext_last = ~1;

	insert_inode_hash(inode);

	return inode;

err_bh:
	affs_free_block(sb, block);
err_block:
	iput(inode);
err_inode:
	return NULL;
}

/*
 * Add an entry to a directory. Create the header block
 * and insert it into the hash table.
 */

int
affs_add_entry(struct inode *dir, struct inode *inode, struct dentry *dentry, s32 type)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *inode_bh = NULL;
	struct buffer_head *bh;
	u32 block = 0;
	int retval;

	pr_debug("%s(dir=%lu, inode=%lu, \"%pd\", type=%d)\n", __func__,
		 dir->i_ino, inode->i_ino, dentry, type);

	retval = -EIO;
	bh = affs_bread(sb, inode->i_ino);
	if (!bh)
		goto done;

	affs_lock_link(inode);
	switch (type) {
	case ST_LINKFILE:
	case ST_LINKDIR:
		retval = -ENOSPC;
		block = affs_alloc_block(dir, dir->i_ino);
		if (!block)
			goto err;
		retval = -EIO;
		inode_bh = bh;
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
	AFFS_TAIL(sb, bh)->parent = cpu_to_be32(dir->i_ino);

	if (inode_bh) {
		__be32 chain;
	       	chain = AFFS_TAIL(sb, inode_bh)->link_chain;
		AFFS_TAIL(sb, bh)->original = cpu_to_be32(inode->i_ino);
		AFFS_TAIL(sb, bh)->link_chain = chain;
		AFFS_TAIL(sb, inode_bh)->link_chain = cpu_to_be32(block);
		affs_adjust_checksum(inode_bh, block - be32_to_cpu(chain));
		mark_buffer_dirty_inode(inode_bh, inode);
		set_nlink(inode, 2);
		ihold(inode);
	}
	affs_fix_checksum(sb, bh);
	mark_buffer_dirty_inode(bh, inode);
	dentry->d_fsdata = (void *)(long)bh->b_blocknr;

	affs_lock_dir(dir);
	retval = affs_insert_hash(dir, bh);
	mark_buffer_dirty_inode(bh, inode);
	affs_unlock_dir(dir);
	affs_unlock_link(inode);

	d_instantiate(dentry, inode);
done:
	affs_brelse(inode_bh);
	affs_brelse(bh);
	return retval;
err:
	if (block)
		affs_free_block(sb, block);
	affs_unlock_link(inode);
	goto done;
}
