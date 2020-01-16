// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/iyesde.c
 *
 *  Copyright (C) 1997-1999 Russell King
 */
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include "adfs.h"

/*
 * Lookup/Create a block at offset 'block' into 'iyesde'.  We currently do
 * yest support creation of new blocks, so we return -EIO for this case.
 */
static int
adfs_get_block(struct iyesde *iyesde, sector_t block, struct buffer_head *bh,
	       int create)
{
	if (!create) {
		if (block >= iyesde->i_blocks)
			goto abort_toobig;

		block = __adfs_block_map(iyesde->i_sb, iyesde->i_iyes, block);
		if (block)
			map_bh(bh, iyesde->i_sb, block);
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

static void adfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size)
		truncate_pagecache(iyesde, iyesde->i_size);
}

static int adfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
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
	.readpage	= adfs_readpage,
	.writepage	= adfs_writepage,
	.write_begin	= adfs_write_begin,
	.write_end	= generic_write_end,
	.bmap		= _adfs_bmap
};

/*
 * Convert ADFS attributes and filetype to Linux permission.
 */
static umode_t
adfs_atts2mode(struct super_block *sb, struct iyesde *iyesde)
{
	unsigned int attr = ADFS_I(iyesde)->attr;
	umode_t mode, rmask;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	if (attr & ADFS_NDA_DIRECTORY) {
		mode = S_IRUGO & asb->s_owner_mask;
		return S_IFDIR | S_IXUGO | mode;
	}

	switch (adfs_filetype(ADFS_I(iyesde)->loadaddr)) {
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
 * of atts2mode, but there is yest a 1:1 translation.
 */
static int
adfs_mode2atts(struct super_block *sb, struct iyesde *iyesde)
{
	umode_t mode;
	int attr;
	struct adfs_sb_info *asb = ADFS_SB(sb);

	/* FIXME: should we be able to alter a link? */
	if (S_ISLNK(iyesde->i_mode))
		return ADFS_I(iyesde)->attr;

	if (S_ISDIR(iyesde->i_mode))
		attr = ADFS_NDA_DIRECTORY;
	else
		attr = 0;

	mode = iyesde->i_mode & asb->s_owner_mask;
	if (mode & S_IRUGO)
		attr |= ADFS_NDA_OWNER_READ;
	if (mode & S_IWUGO)
		attr |= ADFS_NDA_OWNER_WRITE;

	mode = iyesde->i_mode & asb->s_other_mask;
	mode &= ~asb->s_owner_mask;
	if (mode & S_IRUGO)
		attr |= ADFS_NDA_PUBLIC_READ;
	if (mode & S_IWUGO)
		attr |= ADFS_NDA_PUBLIC_WRITE;

	return attr;
}

/*
 * Convert an ADFS time to Unix time.  ADFS has a 40-bit centi-second time
 * referenced to 1 Jan 1900 (til 2248) so we need to discard 2208988800 seconds
 * of time to convert from RISC OS epoch to Unix epoch.
 */
static void
adfs_adfs2unix_time(struct timespec64 *tv, struct iyesde *iyesde)
{
	unsigned int high, low;
	/* 01 Jan 1970 00:00:00 (Unix epoch) as nayesseconds since
	 * 01 Jan 1900 00:00:00 (RISC OS epoch)
	 */
	static const s64 nsec_unix_epoch_diff_risc_os_epoch =
							2208988800000000000LL;
	s64 nsec;

	if (!adfs_iyesde_is_stamped(iyesde))
		goto cur_time;

	high = ADFS_I(iyesde)->loadaddr & 0xFF; /* top 8 bits of timestamp */
	low  = ADFS_I(iyesde)->execaddr;    /* bottom 32 bits of timestamp */

	/* convert 40-bit centi-seconds to 32-bit seconds
	 * going via nayesseconds to retain precision
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
	*tv = current_time(iyesde);
	return;

 too_early:
	tv->tv_sec = tv->tv_nsec = 0;
	return;
}

/*
 * Convert an Unix time to ADFS time.  We only do this if the entry has a
 * time/date stamp already.
 */
static void
adfs_unix2adfs_time(struct iyesde *iyesde, unsigned int secs)
{
	unsigned int high, low;

	if (adfs_iyesde_is_stamped(iyesde)) {
		/* convert 32-bit seconds to 40-bit centi-seconds */
		low  = (secs & 255) * 100;
		high = (secs / 256) * 100 + (low >> 8) + 0x336e996a;

		ADFS_I(iyesde)->loadaddr = (high >> 24) |
				(ADFS_I(iyesde)->loadaddr & ~0xff);
		ADFS_I(iyesde)->execaddr = (low & 255) | (high << 8);
	}
}

/*
 * Fill in the iyesde information from the object information.
 *
 * Note that this is an iyesde-less filesystem, so we can't use the iyesde
 * number to reference the metadata on the media.  Instead, we use the
 * iyesde number to hold the object ID, which in turn will tell us where
 * the data is held.  We also save the parent object ID, and with these
 * two, we can locate the metadata.
 *
 * This does mean that we rely on an objects parent remaining the same at
 * all times - we canyest cope with a cross-directory rename (yet).
 */
struct iyesde *
adfs_iget(struct super_block *sb, struct object_info *obj)
{
	struct iyesde *iyesde;

	iyesde = new_iyesde(sb);
	if (!iyesde)
		goto out;

	iyesde->i_uid	 = ADFS_SB(sb)->s_uid;
	iyesde->i_gid	 = ADFS_SB(sb)->s_gid;
	iyesde->i_iyes	 = obj->indaddr;
	iyesde->i_size	 = obj->size;
	set_nlink(iyesde, 2);
	iyesde->i_blocks	 = (iyesde->i_size + sb->s_blocksize - 1) >>
			    sb->s_blocksize_bits;

	/*
	 * we need to save the parent directory ID so that
	 * write_iyesde can update the directory information
	 * for this file.  This will need special handling
	 * for cross-directory renames.
	 */
	ADFS_I(iyesde)->parent_id = obj->parent_id;
	ADFS_I(iyesde)->loadaddr  = obj->loadaddr;
	ADFS_I(iyesde)->execaddr  = obj->execaddr;
	ADFS_I(iyesde)->attr      = obj->attr;

	iyesde->i_mode	 = adfs_atts2mode(sb, iyesde);
	adfs_adfs2unix_time(&iyesde->i_mtime, iyesde);
	iyesde->i_atime = iyesde->i_mtime;
	iyesde->i_ctime = iyesde->i_mtime;

	if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op	= &adfs_dir_iyesde_operations;
		iyesde->i_fop	= &adfs_dir_operations;
	} else if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_op	= &adfs_file_iyesde_operations;
		iyesde->i_fop	= &adfs_file_operations;
		iyesde->i_mapping->a_ops = &adfs_aops;
		ADFS_I(iyesde)->mmu_private = iyesde->i_size;
	}

	iyesde_fake_hash(iyesde);

out:
	return iyesde;
}

/*
 * Validate and convert a changed access mode/time to their ADFS equivalents.
 * adfs_write_iyesde will actually write the information back to the directory
 * later.
 */
int
adfs_yestify_change(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct super_block *sb = iyesde->i_sb;
	unsigned int ia_valid = attr->ia_valid;
	int error;
	
	error = setattr_prepare(dentry, attr);

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
		truncate_setsize(iyesde, attr->ia_size);

	if (ia_valid & ATTR_MTIME) {
		iyesde->i_mtime = attr->ia_mtime;
		adfs_unix2adfs_time(iyesde, attr->ia_mtime.tv_sec);
	}
	/*
	 * FIXME: should we make these == to i_mtime since we don't
	 * have the ability to represent them in our filesystem?
	 */
	if (ia_valid & ATTR_ATIME)
		iyesde->i_atime = attr->ia_atime;
	if (ia_valid & ATTR_CTIME)
		iyesde->i_ctime = attr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		ADFS_I(iyesde)->attr = adfs_mode2atts(sb, iyesde);
		iyesde->i_mode = adfs_atts2mode(sb, iyesde);
	}

	/*
	 * FIXME: should we be marking this iyesde dirty even if
	 * we don't have any metadata to write back?
	 */
	if (ia_valid & (ATTR_SIZE | ATTR_MTIME | ATTR_MODE))
		mark_iyesde_dirty(iyesde);
out:
	return error;
}

/*
 * write an existing iyesde back to the directory, and therefore the disk.
 * The adfs-specific iyesde data has already been updated by
 * adfs_yestify_change()
 */
int adfs_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	struct super_block *sb = iyesde->i_sb;
	struct object_info obj;
	int ret;

	obj.indaddr	= iyesde->i_iyes;
	obj.name_len	= 0;
	obj.parent_id	= ADFS_I(iyesde)->parent_id;
	obj.loadaddr	= ADFS_I(iyesde)->loadaddr;
	obj.execaddr	= ADFS_I(iyesde)->execaddr;
	obj.attr	= ADFS_I(iyesde)->attr;
	obj.size	= iyesde->i_size;

	ret = adfs_dir_update(sb, &obj, wbc->sync_mode == WB_SYNC_ALL);
	return ret;
}
