// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/inode.c
 *
 *  Copyright (C) 1997-1999 Russell King
 */
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
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

		block = __adfs_block_map(inode->i_sb, ADFS_I(inode)->indaddr,
					 block);
		if (block)
			map_bh(bh, inode->i_sb, block);
		return 0;
	}
	/* don't support allocation of blocks yet */
	return -EIO;

abort_toobig:
	return 0;
}

static int adfs_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, adfs_get_block);
}

static int adfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, adfs_get_block);
}

static void adfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size)
		truncate_pagecache(inode, inode->i_size);
}

static int adfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, pagep, fsdata,
				adfs_get_block,
				&ADFS_I(mapping->host)->mmu_private);
	if (unlikely(ret))
		adfs_write_failed(mapping, pos + len);

	return ret;
}

static sector_t _adfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, adfs_get_block);
}

static const struct address_space_operations adfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= adfs_read_folio,
	.writepages	= adfs_writepages,
	.write_begin	= adfs_write_begin,
	.write_end	= generic_write_end,
	.migrate_folio	= buffer_migrate_folio,
	.bmap		= _adfs_bmap,
};

/*
 * Convert ADFS attributes and filetype to Linux permission.
 */
static umode_t
adfs_atts2mode(struct super_block *sb, struct inode *inode)
{
	unsigned int attr = ADFS_I(inode)->attr;
	umode_t mode, rmask;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	if (attr & ADFS_NDA_DIRECTORY) {
		mode = S_IRUGO & asb->s_owner_mask;
		return S_IFDIR | S_IXUGO | mode;
	}

	switch (adfs_filetype(ADFS_I(inode)->loadaddr)) {
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
static int adfs_mode2atts(struct super_block *sb, struct inode *inode,
			  umode_t ia_mode)
{
	struct adfs_sb_info *asb = ADFS_SB(sb);
	umode_t mode;
	int attr;

	/* FIXME: should we be able to alter a link? */
	if (S_ISLNK(inode->i_mode))
		return ADFS_I(inode)->attr;

	/* Directories do not have read/write permissions on the media */
	if (S_ISDIR(inode->i_mode))
		return ADFS_NDA_DIRECTORY;

	attr = 0;
	mode = ia_mode & asb->s_owner_mask;
	if (mode & S_IRUGO)
		attr |= ADFS_NDA_OWNER_READ;
	if (mode & S_IWUGO)
		attr |= ADFS_NDA_OWNER_WRITE;

	mode = ia_mode & asb->s_other_mask;
	mode &= ~asb->s_owner_mask;
	if (mode & S_IRUGO)
		attr |= ADFS_NDA_PUBLIC_READ;
	if (mode & S_IWUGO)
		attr |= ADFS_NDA_PUBLIC_WRITE;

	return attr;
}

static const s64 nsec_unix_epoch_diff_risc_os_epoch = 2208988800000000000LL;

/*
 * Convert an ADFS time to Unix time.  ADFS has a 40-bit centi-second time
 * referenced to 1 Jan 1900 (til 2248) so we need to discard 2208988800 seconds
 * of time to convert from RISC OS epoch to Unix epoch.
 */
static void
adfs_adfs2unix_time(struct timespec64 *tv, struct inode *inode)
{
	unsigned int high, low;
	/* 01 Jan 1970 00:00:00 (Unix epoch) as nanoseconds since
	 * 01 Jan 1900 00:00:00 (RISC OS epoch)
	 */
	s64 nsec;

	if (!adfs_inode_is_stamped(inode))
		goto cur_time;

	high = ADFS_I(inode)->loadaddr & 0xFF; /* top 8 bits of timestamp */
	low  = ADFS_I(inode)->execaddr;    /* bottom 32 bits of timestamp */

	/* convert 40-bit centi-seconds to 32-bit seconds
	 * going via nanoseconds to retain precision
	 */
	nsec = (((s64) high << 32) | (s64) low) * 10000000; /* cs to ns */

	/* Files dated pre  01 Jan 1970 00:00:00. */
	if (nsec < nsec_unix_epoch_diff_risc_os_epoch)
		goto too_early;

	/* convert from RISC OS to Unix epoch */
	nsec -= nsec_unix_epoch_diff_risc_os_epoch;

	*tv = ns_to_timespec64(nsec);
	return;

 cur_time:
	*tv = current_time(inode);
	return;

 too_early:
	tv->tv_sec = tv->tv_nsec = 0;
	return;
}

/* Convert an Unix time to ADFS time for an entry that is already stamped. */
static void adfs_unix2adfs_time(struct inode *inode,
				const struct timespec64 *ts)
{
	s64 cs, nsec = timespec64_to_ns(ts);

	/* convert from Unix to RISC OS epoch */
	nsec += nsec_unix_epoch_diff_risc_os_epoch;

	/* convert from nanoseconds to centiseconds */
	cs = div_s64(nsec, 10000000);

	cs = clamp_t(s64, cs, 0, 0xffffffffff);

	ADFS_I(inode)->loadaddr &= ~0xff;
	ADFS_I(inode)->loadaddr |= (cs >> 32) & 0xff;
	ADFS_I(inode)->execaddr = cs;
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
	struct timespec64 ts;

	inode = new_inode(sb);
	if (!inode)
		goto out;

	inode->i_uid	 = ADFS_SB(sb)->s_uid;
	inode->i_gid	 = ADFS_SB(sb)->s_gid;
	inode->i_ino	 = obj->indaddr;
	inode->i_size	 = obj->size;
	set_nlink(inode, 2);
	inode->i_blocks	 = (inode->i_size + sb->s_blocksize - 1) >>
			    sb->s_blocksize_bits;

	/*
	 * we need to save the parent directory ID so that
	 * write_inode can update the directory information
	 * for this file.  This will need special handling
	 * for cross-directory renames.
	 */
	ADFS_I(inode)->parent_id = obj->parent_id;
	ADFS_I(inode)->indaddr   = obj->indaddr;
	ADFS_I(inode)->loadaddr  = obj->loadaddr;
	ADFS_I(inode)->execaddr  = obj->execaddr;
	ADFS_I(inode)->attr      = obj->attr;

	inode->i_mode	 = adfs_atts2mode(sb, inode);
	adfs_adfs2unix_time(&ts, inode);
	inode_set_atime_to_ts(inode, ts);
	inode_set_mtime_to_ts(inode, ts);
	inode_set_ctime_to_ts(inode, ts);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_op	= &adfs_dir_inode_operations;
		inode->i_fop	= &adfs_dir_operations;
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_op	= &adfs_file_inode_operations;
		inode->i_fop	= &adfs_file_operations;
		inode->i_mapping->a_ops = &adfs_aops;
		ADFS_I(inode)->mmu_private = inode->i_size;
	}

	inode_fake_hash(inode);

out:
	return inode;
}

/*
 * Validate and convert a changed access mode/time to their ADFS equivalents.
 * adfs_write_inode will actually write the information back to the directory
 * later.
 */
int
adfs_notify_change(struct mnt_idmap *idmap, struct dentry *dentry,
		   struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	unsigned int ia_valid = attr->ia_valid;
	int error;
	
	error = setattr_prepare(&nop_mnt_idmap, dentry, attr);

	/*
	 * we can't change the UID or GID of any file -
	 * we have a global UID/GID in the superblock
	 */
	if ((ia_valid & ATTR_UID && !uid_eq(attr->ia_uid, ADFS_SB(sb)->s_uid)) ||
	    (ia_valid & ATTR_GID && !gid_eq(attr->ia_gid, ADFS_SB(sb)->s_gid)))
		error = -EPERM;

	if (error)
		goto out;

	/* XXX: this is missing some actual on-disk truncation.. */
	if (ia_valid & ATTR_SIZE)
		truncate_setsize(inode, attr->ia_size);

	if (ia_valid & ATTR_MTIME && adfs_inode_is_stamped(inode)) {
		adfs_unix2adfs_time(inode, &attr->ia_mtime);
		adfs_adfs2unix_time(&attr->ia_mtime, inode);
		inode_set_mtime_to_ts(inode, attr->ia_mtime);
	}

	/*
	 * FIXME: should we make these == to i_mtime since we don't
	 * have the ability to represent them in our filesystem?
	 */
	if (ia_valid & ATTR_ATIME)
		inode_set_atime_to_ts(inode, attr->ia_atime);
	if (ia_valid & ATTR_CTIME)
		inode_set_ctime_to_ts(inode, attr->ia_ctime);
	if (ia_valid & ATTR_MODE) {
		ADFS_I(inode)->attr = adfs_mode2atts(sb, inode, attr->ia_mode);
		inode->i_mode = adfs_atts2mode(sb, inode);
	}

	/*
	 * FIXME: should we be marking this inode dirty even if
	 * we don't have any metadata to write back?
	 */
	if (ia_valid & (ATTR_SIZE | ATTR_MTIME | ATTR_MODE))
		mark_inode_dirty(inode);
out:
	return error;
}

/*
 * write an existing inode back to the directory, and therefore the disk.
 * The adfs-specific inode data has already been updated by
 * adfs_notify_change()
 */
int adfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct super_block *sb = inode->i_sb;
	struct object_info obj;

	obj.indaddr	= ADFS_I(inode)->indaddr;
	obj.name_len	= 0;
	obj.parent_id	= ADFS_I(inode)->parent_id;
	obj.loadaddr	= ADFS_I(inode)->loadaddr;
	obj.execaddr	= ADFS_I(inode)->execaddr;
	obj.attr	= ADFS_I(inode)->attr;
	obj.size	= inode->i_size;

	return adfs_dir_update(sb, &obj, wbc->sync_mode == WB_SYNC_ALL);
}
