// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/dir_f.c
 *
 * Copyright (C) 1997-1999 Russell King
 *
 *  E and F format directory handling
 */
#include "adfs.h"
#include "dir_f.h"

/*
 * Read an (unaligned) value of length 1..4 bytes
 */
static inline unsigned int adfs_readval(unsigned char *p, int len)
{
	unsigned int val = 0;

	switch (len) {
	case 4:		val |= p[3] << 24;
		fallthrough;
	case 3:		val |= p[2] << 16;
		fallthrough;
	case 2:		val |= p[1] << 8;
		fallthrough;
	default:	val |= p[0];
	}
	return val;
}

static inline void adfs_writeval(unsigned char *p, int len, unsigned int val)
{
	switch (len) {
	case 4:		p[3] = val >> 24;
		fallthrough;
	case 3:		p[2] = val >> 16;
		fallthrough;
	case 2:		p[1] = val >> 8;
		fallthrough;
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
	  (void *)(_bh[_buf]->b_data + _off);	\
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

static int adfs_f_validate(struct adfs_dir *dir)
{
	struct adfs_dirheader *head = dir->dirhead;
	struct adfs_newdirtail *tail = dir->newtail;

	if (head->startmasseq != tail->endmasseq ||
	    tail->dirlastmask || tail->reserved[0] || tail->reserved[1] ||
	    (memcmp(&head->startname, "Nick", 4) &&
	     memcmp(&head->startname, "Hugo", 4)) ||
	    memcmp(&head->startname, &tail->endname, 4) ||
	    adfs_dir_checkbyte(dir) != tail->dircheckbyte)
		return -EIO;

	return 0;
}

/* Read and check that a directory is valid */
static int adfs_f_read(struct super_block *sb, u32 indaddr, unsigned int size,
		       struct adfs_dir *dir)
{
	const unsigned int blocksize_bits = sb->s_blocksize_bits;
	int ret;

	if (size && size != ADFS_NEWDIR_SIZE)
		return -EIO;

	ret = adfs_dir_read_buffers(sb, indaddr, ADFS_NEWDIR_SIZE, dir);
	if (ret)
		return ret;

	dir->dirhead = bufoff(dir->bh, 0);
	dir->newtail = bufoff(dir->bh, 2007);

	if (adfs_f_validate(dir))
		goto bad_dir;

	dir->parent_id = adfs_readval(dir->newtail->dirparent, 3);

	return 0;

bad_dir:
	adfs_error(sb, "dir %06x is corrupted", indaddr);
	adfs_dir_relse(dir);

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
	obj->indaddr  = adfs_readval(de->dirinddiscadd, 3);
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
	adfs_writeval(de->dirinddiscadd, 3, obj->indaddr);
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
	struct adfs_direntry de;
	int ret;

	ret = adfs_dir_copyfrom(&de, dir, pos, 26);
	if (ret)
		return ret;

	if (!de.dirobname[0])
		return -ENOENT;

	adfs_dir2obj(dir, obj, &de);

	return 0;
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

static int adfs_f_iterate(struct adfs_dir *dir, struct dir_context *ctx)
{
	struct object_info obj;
	int pos = 5 + (ctx->pos - 2) * 26;

	while (ctx->pos < 2 + ADFS_NUM_DIR_ENTRIES) {
		if (__adfs_dir_get(dir, pos, &obj))
			break;
		if (!dir_emit(ctx, obj.name, obj.name_len,
			      obj.indaddr, DT_UNKNOWN))
			break;
		pos += 26;
		ctx->pos++;
	}
	return 0;
}

static int adfs_f_update(struct adfs_dir *dir, struct object_info *obj)
{
	struct adfs_direntry de;
	int offset, ret;

	offset = 5 - (int)sizeof(de);

	do {
		offset += sizeof(de);
		ret = adfs_dir_copyfrom(&de, dir, offset, sizeof(de));
		if (ret) {
			adfs_error(dir->sb, "error reading directory entry");
			return -ENOENT;
		}
		if (!de.dirobname[0]) {
			adfs_error(dir->sb, "unable to locate entry to update");
			return -ENOENT;
		}
	} while (adfs_readval(de.dirinddiscadd, 3) != obj->indaddr);

	/* Update the directory entry with the new object state */
	adfs_obj2dir(&de, obj);

	/* Write the directory entry back to the directory */
	return adfs_dir_copyto(dir, offset, &de, 26);
}

static int adfs_f_commit(struct adfs_dir *dir)
{
	int ret;

	/* Increment directory sequence number */
	dir->dirhead->startmasseq += 1;
	dir->newtail->endmasseq += 1;

	/* Update directory check byte */
	dir->newtail->dircheckbyte = adfs_dir_checkbyte(dir);

	/* Make sure the directory still validates correctly */
	ret = adfs_f_validate(dir);
	if (ret)
		adfs_msg(dir->sb, KERN_ERR, "error: update broke directory");

	return ret;
}

const struct adfs_dir_ops adfs_f_dir_ops = {
	.read		= adfs_f_read,
	.iterate	= adfs_f_iterate,
	.setpos		= adfs_f_setpos,
	.getnext	= adfs_f_getnext,
	.update		= adfs_f_update,
	.commit		= adfs_f_commit,
};
