/*
 *  linux/fs/adfs/inode.c
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include "adfs.h"

/*
 * Lookup/Create a block at offset 'block' into 'inode'.  We currently do
 * not support creation of new blocks, so we return -EIO for this case.
 */
static int
adfs_get_block(struct inode *inode, sector_t block, struct buffer_head *bh,
	       int create)
{
	if (!create) {
		if (block >= inode->i_blocks)
			goto abort_toobig;

		block = __adfs_block_map(inode->i_sb, inode->i_ino, block);
		if (block)
			map_bh(bh, inode->i_sb, block);
		return 0;
	}
	/* don't support allocation of blocks yet */
	return -EIO;

abort_toobig:
	return 0;
}

static int adfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, adfs_get_block, wbc);
}

static int adfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, adfs_get_block);
}

static int adfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	*pagep = NULL;
	return cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				adfs_get_block,
				&ADFS_I(mapping->host)->mmu_private);
}

static sector_t _adfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, adfs_get_block);
}

static const struct address_space_operations adfs_aops = {
	.readpage	= adfs_readpage,
	.writepage	= adfs_writepage,
	.sync_page	= block_sync_page,
	.write_begin	= adfs_write_begin,
	.write_end	= generic_write_end,
	.bmap		= _adfs_bmap
};

static inline unsigned int
adfs_filetype(struct inode *inode)
{
	unsigned int type;

	if (ADFS_I(inode)->stamped)
		type = (ADFS_I(inode)->loadaddr >> 8) & 0xfff;
	else
		type = (unsigned int) -1;

	return type;
}

/*
 * Convert ADFS attributes and filetype to Linux permission.
 */
static umode_t
adfs_atts2mode(struct super_block *sb, struct inode *inode)
{
	unsigned int filetype, attr = ADFS_I(inode)->attr;
	umode_t mode, rmask;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	if (attr & ADFS_NDA_DIRECTORY) {
		mode = S_IRUGO & asb->s_owner_mask;
		return S_IFDIR | S_IXUGO | mode;
	}

	filetype = adfs_filetype(inode);

	switch (filetype) {
	case 0xfc0:	/* LinkFS */
		return S_IFLNK|S_IRWXUGO;

	case 0xfe6:	/* UnixExec */
		rmask = S_IRUGO | S_IXUGO;
		break;

	default:
		rmask = S_IRUGO;
	}

	mode = S_IFREG;

	if (attr & ADFS_NDA_OWNER_READ)
		mode |= rmask & asb->s_owner_mask;

	if (attr & ADFS_NDA_OWNER_WRITE)
		mode |= S_IWUGO & asb->s_owner_mask;

	if (attr & ADFS_NDA_PUBLIC_READ)
		mode |= rmask & asb->s_other_mask;

	if (attr & ADFS_NDA_PUBLIC_WRITE)
		mode |= S_IWUGO & asb->s_other_mask;
	return mode;
}

/*
 * Convert Linux permission to ADFS attribute.  We try to do the reverse
 * of atts2mode, but there is not a 1:1 translation.
 */
static int
adfs_mode2atts(struct super_block *sb, struct inode *inode)
{
	umode_t mode;
	int attr;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	/* FIXME: should we be able to alter a link? */
	if (S_ISLNK(inode->i_mode))
		return ADFS_I(inode)->attr;

	if (S_ISDIR(inode->i_mode))
		attr = ADFS_NDA_DIRECTORY;
	else
		attr = 0;

	mode = inode->i_mode & asb->s_owner_mask;
	if (mode & S_IRUGO)
		attr |= ADFS_NDA_OWNER_READ;
	if (mode & S_IWUGO)
		attr |= ADFS_NDA_OWNER_WRITE;

	mode = inode->i_mode & asb->s_other_mask;
	mode &= ~asb->s_owner_mask;
	if (mode & S_IRUGO)
		attr |= ADFS_NDA_PUBLIC_READ;
	if (mode & S_IWUGO)
		attr |= ADFS_NDA_PUBLIC_WRITE;

	return attr;
}

/*
 * Convert an ADFS time to Unix time.  ADFS has a 40-bit centi-second time
 * referenced to 1 Jan 1900 (til 2248)
 */
static void
adfs_adfs2unix_time(struct timespec *tv, struct inode *inode)
{
	unsigned int high, low;

	if (ADFS_I(inode)->stamped == 0)
		goto cur_time;

	high = ADFS_I(inode)->loadaddr << 24;
	low  = ADFS_I(inode)->execaddr;

	high |= low >> 8;
	low  &= 255;

	/* Files dated pre  01 Jan 1970 00:00:00. */
	if (high < 0x336e996a)
		goto too_early;

	/* Files dated post 18 Jan 2038 03:14:05. */
	if (high >= 0x656e9969)
		goto too_late;

	/* discard 2208988800 (0x336e996a00) seconds of time */
	high -= 0x336e996a;

	/* convert 40-bit centi-seconds to 32-bit seconds */
	tv->tv_sec = (((high % 100) << 8) + low) / 100 + (high / 100 << 8);
	tv->tv_nsec = 0;
	return;

 cur_time:
	*tv = CURRENT_TIME_SEC;
	return;

 too_early:
	tv->tv_sec = tv->tv_nsec = 0;
	return;

 too_late:
	tv->tv_sec = 0x7ffffffd;
	tv->tv_nsec = 0;
	return;
}

/*
 * Convert an Unix time to ADFS time.  We only do this if the entry has a
 * time/date stamp already.
 */
static void
adfs_unix2adfs_time(struct inode *inode, unsigned int secs)
{
	unsigned int high, low;

	if (ADFS_I(inode)->stamped) {
		/* convert 32-bit seconds to 40-bit centi-seconds */
		low  = (secs & 255) * 100;
		high = (secs / 256) * 100 + (low >> 8) + 0x336e996a;

		ADFS_I(inode)->loadaddr = (high >> 24) |
				(ADFS_I(inode)->loadaddr & ~0xff);
		ADFS_I(inode)->execaddr = (low & 255) | (high << 8);
	}
}

/*
 * Fill in the inode information from the object information.
 *
 * Note that this is an inode-less filesystem, so we can't use the inode
 * number to reference the metadata on the media.  Instead, we use the
 * inode number to hold the object ID, which in turn will tell us where
 * the data is held.  We also save the parent object ID, and with these
 * two, we can locate the metadata.
 *
 * This does mean that we rely on an objects parent remaining the same at
 * all times - we cannot cope with a cross-directory rename (yet).
 */
struct inode *
adfs_iget(struct super_block *sb, struct object_info *obj)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		goto out;

	inode->i_uid	 = ADFS_SB(sb)->s_uid;
	inode->i_gid	 = ADFS_SB(sb)->s_gid;
	inode->i_ino	 = obj->file_id;
	inode->i_size	 = obj->size;
	inode->i_nlink	 = 2;
	inode->i_blocks	 = (inode->i_size + sb->s_blocksize - 1) >>
			    sb->s_blocksize_bits;

	/*
	 * we need to save the parent directory ID so that
	 * write_inode can update the directory information
	 * for this file.  This will need special handling
	 * for cross-directory renames.
	 */
	ADFS_I(inode)->parent_id = obj->parent_id;
	ADFS_I(inode)->loadaddr  = obj->loadaddr;
	ADFS_I(inode)->execaddr  = obj->execaddr;
	ADFS_I(inode)->attr      = obj->attr;
	ADFS_I(inode)->stamped	  = ((obj->loadaddr & 0xfff00000) == 0xfff00000);

	inode->i_mode	 = adfs_atts2mode(sb, inode);
	adfs_adfs2unix_time(&inode->i_mtime, inode);
	inode->i_atime = inode->i_mtime;
	inode->i_ctime = inode->i_mtime;

	if (S_ISDIR(inode->i_mode)) {
		inode->i_op	= &adfs_dir_inode_operations;
		inode->i_fop	= &adfs_dir_operations;
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_op	= &adfs_file_inode_operations;
		inode->i_fop	= &adfs_file_operations;
		inode->i_mapping->a_ops = &adfs_aops;
		ADFS_I(inode)->mmu_private = inode->i_size;
	}

	insert_inode_hash(inode);

out:
	return inode;
}

/*
 * Validate and convert a changed access mode/time to their ADFS equivalents.
 * adfs_write_inode will actually write the information back to the directory
 * later.
 */
int
adfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	unsigned int ia_valid = attr->ia_valid;
	int error;
	
	lock_kernel();

	error = inode_change_ok(inode, attr);

	/*
	 * we can't change the UID or GID of any file -
	 * we have a global UID/GID in the superblock
	 */
	if ((ia_valid & ATTR_UID && attr->ia_uid != ADFS_SB(sb)->s_uid) ||
	    (ia_valid & ATTR_GID && attr->ia_gid != ADFS_SB(sb)->s_gid))
		error = -EPERM;

	if (error)
		goto out;

	if (ia_valid & ATTR_SIZE)
		error = vmtruncate(inode, attr->ia_size);

	if (error)
		goto out;

	if (ia_valid & ATTR_MTIME) {
		inode->i_mtime = attr->ia_mtime;
		adfs_unix2adfs_time(inode, attr->ia_mtime.tv_sec);
	}
	/*
	 * FIXME: should we make these == to i_mtime since we don't
	 * have the ability to represent them in our filesystem?
	 */
	if (ia_valid & ATTR_ATIME)
		inode->i_atime = attr->ia_atime;
	if (ia_valid & ATTR_CTIME)
		inode->i_ctime = attr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		ADFS_I(inode)->attr = adfs_mode2atts(sb, inode);
		inode->i_mode = adfs_atts2mode(sb, inode);
	}

	/*
	 * FIXME: should we be marking this inode dirty even if
	 * we don't have any metadata to write back?
	 */
	if (ia_valid & (ATTR_SIZE | ATTR_MTIME | ATTR_MODE))
		mark_inode_dirty(inode);
out:
	unlock_kernel();
	return error;
}

/*
 * write an existing inode back to the directory, and therefore the disk.
 * The adfs-specific inode data has already been updated by
 * adfs_notify_change()
 */
int adfs_write_inode(struct inode *inode, int wait)
{
	struct super_block *sb = inode->i_sb;
	struct object_info obj;
	int ret;

	lock_kernel();
	obj.file_id	= inode->i_ino;
	obj.name_len	= 0;
	obj.parent_id	= ADFS_I(inode)->parent_id;
	obj.loadaddr	= ADFS_I(inode)->loadaddr;
	obj.execaddr	= ADFS_I(inode)->execaddr;
	obj.attr	= ADFS_I(inode)->attr;
	obj.size	= inode->i_size;

	ret = adfs_dir_update(sb, &obj, wait);
	unlock_kernel();
	return ret;
}
