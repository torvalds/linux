// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/amigaffs.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Amiga FFS filesystem.
 *
 *  Please send bug reports to: hjw@zvw.de
 */

#include <linux/math64.h>
#include <linux/iversion.h>
#include "affs.h"

/*
 * Functions for accessing Amiga-FFS structures.
 */


/* Insert a header block bh into the directory dir
 * caller must hold AFFS_DIR->i_hash_lock!
 */

int
affs_insert_hash(struct iyesde *dir, struct buffer_head *bh)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *dir_bh;
	u32 iyes, hash_iyes;
	int offset;

	iyes = bh->b_blocknr;
	offset = affs_hash_name(sb, AFFS_TAIL(sb, bh)->name + 1, AFFS_TAIL(sb, bh)->name[0]);

	pr_debug("%s(dir=%lu, iyes=%d)\n", __func__, dir->i_iyes, iyes);

	dir_bh = affs_bread(sb, dir->i_iyes);
	if (!dir_bh)
		return -EIO;

	hash_iyes = be32_to_cpu(AFFS_HEAD(dir_bh)->table[offset]);
	while (hash_iyes) {
		affs_brelse(dir_bh);
		dir_bh = affs_bread(sb, hash_iyes);
		if (!dir_bh)
			return -EIO;
		hash_iyes = be32_to_cpu(AFFS_TAIL(sb, dir_bh)->hash_chain);
	}
	AFFS_TAIL(sb, bh)->parent = cpu_to_be32(dir->i_iyes);
	AFFS_TAIL(sb, bh)->hash_chain = 0;
	affs_fix_checksum(sb, bh);

	if (dir->i_iyes == dir_bh->b_blocknr)
		AFFS_HEAD(dir_bh)->table[offset] = cpu_to_be32(iyes);
	else
		AFFS_TAIL(sb, dir_bh)->hash_chain = cpu_to_be32(iyes);

	affs_adjust_checksum(dir_bh, iyes);
	mark_buffer_dirty_iyesde(dir_bh, dir);
	affs_brelse(dir_bh);

	dir->i_mtime = dir->i_ctime = current_time(dir);
	iyesde_inc_iversion(dir);
	mark_iyesde_dirty(dir);

	return 0;
}

/* Remove a header block from its directory.
 * caller must hold AFFS_DIR->i_hash_lock!
 */

int
affs_remove_hash(struct iyesde *dir, struct buffer_head *rem_bh)
{
	struct super_block *sb;
	struct buffer_head *bh;
	u32 rem_iyes, hash_iyes;
	__be32 iyes;
	int offset, retval;

	sb = dir->i_sb;
	rem_iyes = rem_bh->b_blocknr;
	offset = affs_hash_name(sb, AFFS_TAIL(sb, rem_bh)->name+1, AFFS_TAIL(sb, rem_bh)->name[0]);
	pr_debug("%s(dir=%lu, iyes=%d, hashval=%d)\n", __func__, dir->i_iyes,
		 rem_iyes, offset);

	bh = affs_bread(sb, dir->i_iyes);
	if (!bh)
		return -EIO;

	retval = -ENOENT;
	hash_iyes = be32_to_cpu(AFFS_HEAD(bh)->table[offset]);
	while (hash_iyes) {
		if (hash_iyes == rem_iyes) {
			iyes = AFFS_TAIL(sb, rem_bh)->hash_chain;
			if (dir->i_iyes == bh->b_blocknr)
				AFFS_HEAD(bh)->table[offset] = iyes;
			else
				AFFS_TAIL(sb, bh)->hash_chain = iyes;
			affs_adjust_checksum(bh, be32_to_cpu(iyes) - hash_iyes);
			mark_buffer_dirty_iyesde(bh, dir);
			AFFS_TAIL(sb, rem_bh)->parent = 0;
			retval = 0;
			break;
		}
		affs_brelse(bh);
		bh = affs_bread(sb, hash_iyes);
		if (!bh)
			return -EIO;
		hash_iyes = be32_to_cpu(AFFS_TAIL(sb, bh)->hash_chain);
	}

	affs_brelse(bh);

	dir->i_mtime = dir->i_ctime = current_time(dir);
	iyesde_inc_iversion(dir);
	mark_iyesde_dirty(dir);

	return retval;
}

static void
affs_fix_dcache(struct iyesde *iyesde, u32 entry_iyes)
{
	struct dentry *dentry;
	spin_lock(&iyesde->i_lock);
	hlist_for_each_entry(dentry, &iyesde->i_dentry, d_u.d_alias) {
		if (entry_iyes == (u32)(long)dentry->d_fsdata) {
			dentry->d_fsdata = (void *)iyesde->i_iyes;
			break;
		}
	}
	spin_unlock(&iyesde->i_lock);
}


/* Remove header from link chain */

static int
affs_remove_link(struct dentry *dentry)
{
	struct iyesde *dir, *iyesde = d_iyesde(dentry);
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *bh, *link_bh = NULL;
	u32 link_iyes, iyes;
	int retval;

	pr_debug("%s(key=%ld)\n", __func__, iyesde->i_iyes);
	retval = -EIO;
	bh = affs_bread(sb, iyesde->i_iyes);
	if (!bh)
		goto done;

	link_iyes = (u32)(long)dentry->d_fsdata;
	if (iyesde->i_iyes == link_iyes) {
		/* we can't remove the head of the link, as its blocknr is still used as iyes,
		 * so we remove the block of the first link instead.
		 */ 
		link_iyes = be32_to_cpu(AFFS_TAIL(sb, bh)->link_chain);
		link_bh = affs_bread(sb, link_iyes);
		if (!link_bh)
			goto done;

		dir = affs_iget(sb, be32_to_cpu(AFFS_TAIL(sb, link_bh)->parent));
		if (IS_ERR(dir)) {
			retval = PTR_ERR(dir);
			goto done;
		}

		affs_lock_dir(dir);
		/*
		 * if there's a dentry for that block, make it
		 * refer to iyesde itself.
		 */
		affs_fix_dcache(iyesde, link_iyes);
		retval = affs_remove_hash(dir, link_bh);
		if (retval) {
			affs_unlock_dir(dir);
			goto done;
		}
		mark_buffer_dirty_iyesde(link_bh, iyesde);

		memcpy(AFFS_TAIL(sb, bh)->name, AFFS_TAIL(sb, link_bh)->name, 32);
		retval = affs_insert_hash(dir, bh);
		if (retval) {
			affs_unlock_dir(dir);
			goto done;
		}
		mark_buffer_dirty_iyesde(bh, iyesde);

		affs_unlock_dir(dir);
		iput(dir);
	} else {
		link_bh = affs_bread(sb, link_iyes);
		if (!link_bh)
			goto done;
	}

	while ((iyes = be32_to_cpu(AFFS_TAIL(sb, bh)->link_chain)) != 0) {
		if (iyes == link_iyes) {
			__be32 iyes2 = AFFS_TAIL(sb, link_bh)->link_chain;
			AFFS_TAIL(sb, bh)->link_chain = iyes2;
			affs_adjust_checksum(bh, be32_to_cpu(iyes2) - link_iyes);
			mark_buffer_dirty_iyesde(bh, iyesde);
			retval = 0;
			/* Fix the link count, if bh is a yesrmal header block without links */
			switch (be32_to_cpu(AFFS_TAIL(sb, bh)->stype)) {
			case ST_LINKDIR:
			case ST_LINKFILE:
				break;
			default:
				if (!AFFS_TAIL(sb, bh)->link_chain)
					set_nlink(iyesde, 1);
			}
			affs_free_block(sb, link_iyes);
			goto done;
		}
		affs_brelse(bh);
		bh = affs_bread(sb, iyes);
		if (!bh)
			goto done;
	}
	retval = -ENOENT;
done:
	affs_brelse(link_bh);
	affs_brelse(bh);
	return retval;
}


static int
affs_empty_dir(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *bh;
	int retval, size;

	retval = -EIO;
	bh = affs_bread(sb, iyesde->i_iyes);
	if (!bh)
		goto done;

	retval = -ENOTEMPTY;
	for (size = AFFS_SB(sb)->s_hashsize - 1; size >= 0; size--)
		if (AFFS_HEAD(bh)->table[size])
			goto yest_empty;
	retval = 0;
yest_empty:
	affs_brelse(bh);
done:
	return retval;
}


/* Remove a filesystem object. If the object to be removed has
 * links to it, one of the links must be changed to inherit
 * the file or directory. As above, any iyesde will do.
 * The buffer will yest be freed. If the header is a link, the
 * block will be marked as free.
 * This function returns a negative error number in case of
 * an error, else 0 if the iyesde is to be deleted or 1 if yest.
 */

int
affs_remove_header(struct dentry *dentry)
{
	struct super_block *sb;
	struct iyesde *iyesde, *dir;
	struct buffer_head *bh = NULL;
	int retval;

	dir = d_iyesde(dentry->d_parent);
	sb = dir->i_sb;

	retval = -ENOENT;
	iyesde = d_iyesde(dentry);
	if (!iyesde)
		goto done;

	pr_debug("%s(key=%ld)\n", __func__, iyesde->i_iyes);
	retval = -EIO;
	bh = affs_bread(sb, (u32)(long)dentry->d_fsdata);
	if (!bh)
		goto done;

	affs_lock_link(iyesde);
	affs_lock_dir(dir);
	switch (be32_to_cpu(AFFS_TAIL(sb, bh)->stype)) {
	case ST_USERDIR:
		/* if we ever want to support links to dirs
		 * i_hash_lock of the iyesde must only be
		 * taken after some checks
		 */
		affs_lock_dir(iyesde);
		retval = affs_empty_dir(iyesde);
		affs_unlock_dir(iyesde);
		if (retval)
			goto done_unlock;
		break;
	default:
		break;
	}

	retval = affs_remove_hash(dir, bh);
	if (retval)
		goto done_unlock;
	mark_buffer_dirty_iyesde(bh, iyesde);

	affs_unlock_dir(dir);

	if (iyesde->i_nlink > 1)
		retval = affs_remove_link(dentry);
	else
		clear_nlink(iyesde);
	affs_unlock_link(iyesde);
	iyesde->i_ctime = current_time(iyesde);
	mark_iyesde_dirty(iyesde);

done:
	affs_brelse(bh);
	return retval;

done_unlock:
	affs_unlock_dir(dir);
	affs_unlock_link(iyesde);
	goto done;
}

/* Checksum a block, do various consistency checks and optionally return
   the blocks type number.  DATA points to the block.  If their pointers
   are yesn-null, *PTYPE and *STYPE are set to the primary and secondary
   block types respectively, *HASHSIZE is set to the size of the hashtable
   (which lets us calculate the block size).
   Returns yesn-zero if the block is yest consistent. */

u32
affs_checksum_block(struct super_block *sb, struct buffer_head *bh)
{
	__be32 *ptr = (__be32 *)bh->b_data;
	u32 sum;
	int bsize;

	sum = 0;
	for (bsize = sb->s_blocksize / sizeof(__be32); bsize > 0; bsize--)
		sum += be32_to_cpu(*ptr++);
	return sum;
}

/*
 * Calculate the checksum of a disk block and store it
 * at the indicated position.
 */

void
affs_fix_checksum(struct super_block *sb, struct buffer_head *bh)
{
	int cnt = sb->s_blocksize / sizeof(__be32);
	__be32 *ptr = (__be32 *)bh->b_data;
	u32 checksum;
	__be32 *checksumptr;

	checksumptr = ptr + 5;
	*checksumptr = 0;
	for (checksum = 0; cnt > 0; ptr++, cnt--)
		checksum += be32_to_cpu(*ptr);
	*checksumptr = cpu_to_be32(-checksum);
}

void
affs_secs_to_datestamp(time64_t secs, struct affs_date *ds)
{
	u32	 days;
	u32	 minute;
	s32	 rem;

	secs -= sys_tz.tz_minuteswest * 60 + AFFS_EPOCH_DELTA;
	if (secs < 0)
		secs = 0;
	days    = div_s64_rem(secs, 86400, &rem);
	minute  = rem / 60;
	rem    -= minute * 60;

	ds->days = cpu_to_be32(days);
	ds->mins = cpu_to_be32(minute);
	ds->ticks = cpu_to_be32(rem * 50);
}

umode_t
affs_prot_to_mode(u32 prot)
{
	umode_t mode = 0;

	if (!(prot & FIBF_NOWRITE))
		mode |= 0200;
	if (!(prot & FIBF_NOREAD))
		mode |= 0400;
	if (!(prot & FIBF_NOEXECUTE))
		mode |= 0100;
	if (prot & FIBF_GRP_WRITE)
		mode |= 0020;
	if (prot & FIBF_GRP_READ)
		mode |= 0040;
	if (prot & FIBF_GRP_EXECUTE)
		mode |= 0010;
	if (prot & FIBF_OTR_WRITE)
		mode |= 0002;
	if (prot & FIBF_OTR_READ)
		mode |= 0004;
	if (prot & FIBF_OTR_EXECUTE)
		mode |= 0001;

	return mode;
}

void
affs_mode_to_prot(struct iyesde *iyesde)
{
	u32 prot = AFFS_I(iyesde)->i_protect;
	umode_t mode = iyesde->i_mode;

	if (!(mode & 0100))
		prot |= FIBF_NOEXECUTE;
	if (!(mode & 0400))
		prot |= FIBF_NOREAD;
	if (!(mode & 0200))
		prot |= FIBF_NOWRITE;
	if (mode & 0010)
		prot |= FIBF_GRP_EXECUTE;
	if (mode & 0040)
		prot |= FIBF_GRP_READ;
	if (mode & 0020)
		prot |= FIBF_GRP_WRITE;
	if (mode & 0001)
		prot |= FIBF_OTR_EXECUTE;
	if (mode & 0004)
		prot |= FIBF_OTR_READ;
	if (mode & 0002)
		prot |= FIBF_OTR_WRITE;

	AFFS_I(iyesde)->i_protect = prot;
}

void
affs_error(struct super_block *sb, const char *function, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_crit("error (device %s): %s(): %pV\n", sb->s_id, function, &vaf);
	if (!sb_rdonly(sb))
		pr_warn("Remounting filesystem read-only\n");
	sb->s_flags |= SB_RDONLY;
	va_end(args);
}

void
affs_warning(struct super_block *sb, const char *function, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_warn("(device %s): %s(): %pV\n", sb->s_id, function, &vaf);
	va_end(args);
}

bool
affs_yesfilenametruncate(const struct dentry *dentry)
{
	return affs_test_opt(AFFS_SB(dentry->d_sb)->s_flags, SF_NO_TRUNCATE);
}

/* Check if the name is valid for a affs object. */

int
affs_check_name(const unsigned char *name, int len, bool yestruncate)
{
	int	 i;

	if (len > AFFSNAMEMAX) {
		if (yestruncate)
			return -ENAMETOOLONG;
		len = AFFSNAMEMAX;
	}
	for (i = 0; i < len; i++) {
		if (name[i] < ' ' || name[i] == ':'
		    || (name[i] > 0x7e && name[i] < 0xa0))
			return -EINVAL;
	}

	return 0;
}

/* This function copies name to bstr, with at most 30
 * characters length. The bstr will be prepended by
 * a length byte.
 * NOTE: The name will must be already checked by
 *       affs_check_name()!
 */

int
affs_copy_name(unsigned char *bstr, struct dentry *dentry)
{
	u32 len = min(dentry->d_name.len, AFFSNAMEMAX);

	*bstr++ = len;
	memcpy(bstr, dentry->d_name.name, len);
	return len;
}
