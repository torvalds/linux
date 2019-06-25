/*
 *  linux/fs/adfs/dir_f.c
 *
 * Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  E and F format directory handling
 */
#include <linux/buffer_head.h>
#include "adfs.h"
#include "dir_f.h"

static void adfs_f_free(struct adfs_dir *dir);

/*
 * Read an (unaligned) value of length 1..4 bytes
 */
static inline unsigned int adfs_readval(unsigned char *p, int len)
{
	unsigned int val = 0;

	switch (len) {
	case 4:		val |= p[3] << 24;
			/* fall through */
	case 3:		val |= p[2] << 16;
			/* fall through */
	case 2:		val |= p[1] << 8;
			/* fall through */
	default:	val |= p[0];
	}
	return val;
}

static inline void adfs_writeval(unsigned char *p, int len, unsigned int val)
{
	switch (len) {
	case 4:		p[3] = val >> 24;
			/* fall through */
	case 3:		p[2] = val >> 16;
			/* fall through */
	case 2:		p[1] = val >> 8;
			/* fall through */
	default:	p[0] = val;
	}
}

#define ror13(v) ((v >> 13) | (v << 19))

#define dir_u8(idx)				\
	({ int _buf = idx >> blocksize_bits;	\
	   int _off = idx - (_buf << blocksize_bits);\
	  *(u8 *)(bh[_buf]->b_data + _off);	\
	})

#define dir_u32(idx)				\
	({ int _buf = idx >> blocksize_bits;	\
	   int _off = idx - (_buf << blocksize_bits);\
	  *(__le32 *)(bh[_buf]->b_data + _off);	\
	})

#define bufoff(_bh,_idx)			\
	({ int _buf = _idx >> blocksize_bits;	\
	   int _off = _idx - (_buf << blocksize_bits);\
	  (u8 *)(_bh[_buf]->b_data + _off);	\
	})

/*
 * There are some algorithms that are nice in
 * assembler, but a bitch in C...  This is one
 * of them.
 */
static u8
adfs_dir_checkbyte(const struct adfs_dir *dir)
{
	struct buffer_head * const *bh = dir->bh;
	const int blocksize_bits = dir->sb->s_blocksize_bits;
	union { __le32 *ptr32; u8 *ptr8; } ptr, end;
	u32 dircheck = 0;
	int last = 5 - 26;
	int i = 0;

	/*
	 * Accumulate each word up to the last whole
	 * word of the last directory entry.  This
	 * can spread across several buffer heads.
	 */
	do {
		last += 26;
		do {
			dircheck = le32_to_cpu(dir_u32(i)) ^ ror13(dircheck);

			i += sizeof(u32);
		} while (i < (last & ~3));
	} while (dir_u8(last) != 0);

	/*
	 * Accumulate the last few bytes.  These
	 * bytes will be within the same bh.
	 */
	if (i != last) {
		ptr.ptr8 = bufoff(bh, i);
		end.ptr8 = ptr.ptr8 + last - i;

		do {
			dircheck = *ptr.ptr8++ ^ ror13(dircheck);
		} while (ptr.ptr8 < end.ptr8);
	}

	/*
	 * The directory tail is in the final bh
	 * Note that contary to the RISC OS PRMs,
	 * the first few bytes are NOT included
	 * in the check.  All bytes are in the
	 * same bh.
	 */
	ptr.ptr8 = bufoff(bh, 2008);
	end.ptr8 = ptr.ptr8 + 36;

	do {
		__le32 v = *ptr.ptr32++;
		dircheck = le32_to_cpu(v) ^ ror13(dircheck);
	} while (ptr.ptr32 < end.ptr32);

	return (dircheck ^ (dircheck >> 8) ^ (dircheck >> 16) ^ (dircheck >> 24)) & 0xff;
}

/*
 * Read and check that a directory is valid
 */
static int
adfs_dir_read(struct super_block *sb, unsigned long object_id,
	      unsigned int size, struct adfs_dir *dir)
{
	const unsigned int blocksize_bits = sb->s_blocksize_bits;
	int blk = 0;

	/*
	 * Directories which are not a multiple of 2048 bytes
	 * are considered bad v2 [3.6]
	 */
	if (size & 2047)
		goto bad_dir;

	size >>= blocksize_bits;

	dir->nr_buffers = 0;
	dir->sb = sb;

	for (blk = 0; blk < size; blk++) {
		int phys;

		phys = __adfs_block_map(sb, object_id, blk);
		if (!phys) {
			adfs_error(sb, "dir object %lX has a hole at offset %d",
				   object_id, blk);
			goto release_buffers;
		}

		dir->bh[blk] = sb_bread(sb, phys);
		if (!dir->bh[blk])
			goto release_buffers;
	}

	memcpy(&dir->dirhead, bufoff(dir->bh, 0), sizeof(dir->dirhead));
	memcpy(&dir->dirtail, bufoff(dir->bh, 2007), sizeof(dir->dirtail));

	if (dir->dirhead.startmasseq != dir->dirtail.new.endmasseq ||
	    memcmp(&dir->dirhead.startname, &dir->dirtail.new.endname, 4))
		goto bad_dir;

	if (memcmp(&dir->dirhead.startname, "Nick", 4) &&
	    memcmp(&dir->dirhead.startname, "Hugo", 4))
		goto bad_dir;

	if (adfs_dir_checkbyte(dir) != dir->dirtail.new.dircheckbyte)
		goto bad_dir;

	dir->nr_buffers = blk;

	return 0;

bad_dir:
	adfs_error(sb, "corrupted directory fragment %lX",
		   object_id);
release_buffers:
	for (blk -= 1; blk >= 0; blk -= 1)
		brelse(dir->bh[blk]);

	dir->sb = NULL;

	return -EIO;
}

/*
 * convert a disk-based directory entry to a Linux ADFS directory entry
 */
static inline void
adfs_dir2obj(struct adfs_dir *dir, struct object_info *obj,
	struct adfs_direntry *de)
{
	unsigned int name_len;

	for (name_len = 0; name_len < ADFS_F_NAME_LEN; name_len++) {
		if (de->dirobname[name_len] < ' ')
			break;

		obj->name[name_len] = de->dirobname[name_len];
	}

	obj->name_len =	name_len;
	obj->file_id  = adfs_readval(de->dirinddiscadd, 3);
	obj->loadaddr = adfs_readval(de->dirload, 4);
	obj->execaddr = adfs_readval(de->direxec, 4);
	obj->size     = adfs_readval(de->dirlen,  4);
	obj->attr     = de->newdiratts;

	adfs_object_fixup(dir, obj);
}

/*
 * convert a Linux ADFS directory entry to a disk-based directory entry
 */
static inline void
adfs_obj2dir(struct adfs_direntry *de, struct object_info *obj)
{
	adfs_writeval(de->dirinddiscadd, 3, obj->file_id);
	adfs_writeval(de->dirload, 4, obj->loadaddr);
	adfs_writeval(de->direxec, 4, obj->execaddr);
	adfs_writeval(de->dirlen,  4, obj->size);
	de->newdiratts = obj->attr;
}

/*
 * get a directory entry.  Note that the caller is responsible
 * for holding the relevant locks.
 */
static int
__adfs_dir_get(struct adfs_dir *dir, int pos, struct object_info *obj)
{
	struct super_block *sb = dir->sb;
	struct adfs_direntry de;
	int thissize, buffer, offset;

	buffer = pos >> sb->s_blocksize_bits;

	if (buffer > dir->nr_buffers)
		return -EINVAL;

	offset = pos & (sb->s_blocksize - 1);
	thissize = sb->s_blocksize - offset;
	if (thissize > 26)
		thissize = 26;

	memcpy(&de, dir->bh[buffer]->b_data + offset, thissize);
	if (thissize != 26)
		memcpy(((char *)&de) + thissize, dir->bh[buffer + 1]->b_data,
		       26 - thissize);

	if (!de.dirobname[0])
		return -ENOENT;

	adfs_dir2obj(dir, obj, &de);

	return 0;
}

static int
__adfs_dir_put(struct adfs_dir *dir, int pos, struct object_info *obj)
{
	struct super_block *sb = dir->sb;
	struct adfs_direntry de;
	int thissize, buffer, offset;

	buffer = pos >> sb->s_blocksize_bits;

	if (buffer > dir->nr_buffers)
		return -EINVAL;

	offset = pos & (sb->s_blocksize - 1);
	thissize = sb->s_blocksize - offset;
	if (thissize > 26)
		thissize = 26;

	/*
	 * Get the entry in total
	 */
	memcpy(&de, dir->bh[buffer]->b_data + offset, thissize);
	if (thissize != 26)
		memcpy(((char *)&de) + thissize, dir->bh[buffer + 1]->b_data,
		       26 - thissize);

	/*
	 * update it
	 */
	adfs_obj2dir(&de, obj);

	/*
	 * Put the new entry back
	 */
	memcpy(dir->bh[buffer]->b_data + offset, &de, thissize);
	if (thissize != 26)
		memcpy(dir->bh[buffer + 1]->b_data, ((char *)&de) + thissize,
		       26 - thissize);

	return 0;
}

/*
 * the caller is responsible for holding the necessary
 * locks.
 */
static int
adfs_dir_find_entry(struct adfs_dir *dir, unsigned long object_id)
{
	int pos, ret;

	ret = -ENOENT;

	for (pos = 5; pos < ADFS_NUM_DIR_ENTRIES * 26 + 5; pos += 26) {
		struct object_info obj;

		if (!__adfs_dir_get(dir, pos, &obj))
			break;

		if (obj.file_id == object_id) {
			ret = pos;
			break;
		}
	}

	return ret;
}

static int
adfs_f_read(struct super_block *sb, unsigned int id, unsigned int sz, struct adfs_dir *dir)
{
	int ret;

	if (sz != ADFS_NEWDIR_SIZE)
		return -EIO;

	ret = adfs_dir_read(sb, id, sz, dir);
	if (ret)
		adfs_error(sb, "unable to read directory");
	else
		dir->parent_id = adfs_readval(dir->dirtail.new.dirparent, 3);

	return ret;
}

static int
adfs_f_setpos(struct adfs_dir *dir, unsigned int fpos)
{
	if (fpos >= ADFS_NUM_DIR_ENTRIES)
		return -ENOENT;

	dir->pos = 5 + fpos * 26;
	return 0;
}

static int
adfs_f_getnext(struct adfs_dir *dir, struct object_info *obj)
{
	unsigned int ret;

	ret = __adfs_dir_get(dir, dir->pos, obj);
	if (ret == 0)
		dir->pos += 26;

	return ret;
}

static int
adfs_f_update(struct adfs_dir *dir, struct object_info *obj)
{
	struct super_block *sb = dir->sb;
	int ret, i;

	ret = adfs_dir_find_entry(dir, obj->file_id);
	if (ret < 0) {
		adfs_error(dir->sb, "unable to locate entry to update");
		goto out;
	}

	__adfs_dir_put(dir, ret, obj);
 
	/*
	 * Increment directory sequence number
	 */
	dir->bh[0]->b_data[0] += 1;
	dir->bh[dir->nr_buffers - 1]->b_data[sb->s_blocksize - 6] += 1;

	ret = adfs_dir_checkbyte(dir);
	/*
	 * Update directory check byte
	 */
	dir->bh[dir->nr_buffers - 1]->b_data[sb->s_blocksize - 1] = ret;

#if 1
	{
	const unsigned int blocksize_bits = sb->s_blocksize_bits;

	memcpy(&dir->dirhead, bufoff(dir->bh, 0), sizeof(dir->dirhead));
	memcpy(&dir->dirtail, bufoff(dir->bh, 2007), sizeof(dir->dirtail));

	if (dir->dirhead.startmasseq != dir->dirtail.new.endmasseq ||
	    memcmp(&dir->dirhead.startname, &dir->dirtail.new.endname, 4))
		goto bad_dir;

	if (memcmp(&dir->dirhead.startname, "Nick", 4) &&
	    memcmp(&dir->dirhead.startname, "Hugo", 4))
		goto bad_dir;

	if (adfs_dir_checkbyte(dir) != dir->dirtail.new.dircheckbyte)
		goto bad_dir;
	}
#endif
	for (i = dir->nr_buffers - 1; i >= 0; i--)
		mark_buffer_dirty(dir->bh[i]);

	ret = 0;
out:
	return ret;
#if 1
bad_dir:
	adfs_error(dir->sb, "whoops!  I broke a directory!");
	return -EIO;
#endif
}

static int
adfs_f_sync(struct adfs_dir *dir)
{
	int err = 0;
	int i;

	for (i = dir->nr_buffers - 1; i >= 0; i--) {
		struct buffer_head *bh = dir->bh[i];
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			err = -EIO;
	}

	return err;
}

static void
adfs_f_free(struct adfs_dir *dir)
{
	int i;

	for (i = dir->nr_buffers - 1; i >= 0; i--) {
		brelse(dir->bh[i]);
		dir->bh[i] = NULL;
	}

	dir->nr_buffers = 0;
	dir->sb = NULL;
}

const struct adfs_dir_ops adfs_f_dir_ops = {
	.read		= adfs_f_read,
	.setpos		= adfs_f_setpos,
	.getnext	= adfs_f_getnext,
	.update		= adfs_f_update,
	.sync		= adfs_f_sync,
	.free		= adfs_f_free
};
