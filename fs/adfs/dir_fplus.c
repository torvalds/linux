// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/dir_fplus.c
 *
 *  Copyright (C) 1997-1999 Russell King
 */
#include "adfs.h"
#include "dir_fplus.h"

/* Return the byte offset to directory entry pos */
static unsigned int adfs_fplus_offset(const struct adfs_bigdirheader *h,
				      unsigned int pos)
{
	return offsetof(struct adfs_bigdirheader, bigdirname) +
	       ALIGN(le32_to_cpu(h->bigdirnamelen), 4) +
	       pos * sizeof(struct adfs_bigdirentry);
}

static int adfs_fplus_validate_header(const struct adfs_bigdirheader *h)
{
	unsigned int size = le32_to_cpu(h->bigdirsize);
	unsigned int len;

	if (h->bigdirversion[0] != 0 || h->bigdirversion[1] != 0 ||
	    h->bigdirversion[2] != 0 ||
	    h->bigdirstartname != cpu_to_le32(BIGDIRSTARTNAME) ||
	    !size || size & 2047 || size > SZ_4M)
		return -EIO;

	size -= sizeof(struct adfs_bigdirtail) +
		offsetof(struct adfs_bigdirheader, bigdirname);

	/* Check that bigdirnamelen fits within the directory */
	len = ALIGN(le32_to_cpu(h->bigdirnamelen), 4);
	if (len > size)
		return -EIO;

	size -= len;

	/* Check that bigdirnamesize fits within the directory */
	len = le32_to_cpu(h->bigdirnamesize);
	if (len > size)
		return -EIO;

	size -= len;

	/*
	 * Avoid division, we know that absolute maximum number of entries
	 * can not be so large to cause overflow of the multiplication below.
	 */
	len = le32_to_cpu(h->bigdirentries);
	if (len > SZ_4M / sizeof(struct adfs_bigdirentry) ||
	    len * sizeof(struct adfs_bigdirentry) > size)
		return -EIO;

	return 0;
}

static int adfs_fplus_validate_tail(const struct adfs_bigdirheader *h,
				    const struct adfs_bigdirtail *t)
{
	if (t->bigdirendname != cpu_to_le32(BIGDIRENDNAME) ||
	    t->bigdirendmasseq != h->startmasseq ||
	    t->reserved[0] != 0 || t->reserved[1] != 0)
		return -EIO;

	return 0;
}

static u8 adfs_fplus_checkbyte(struct adfs_dir *dir)
{
	struct adfs_bigdirheader *h = dir->bighead;
	struct adfs_bigdirtail *t = dir->bigtail;
	unsigned int end, bs, bi, i;
	__le32 *bp;
	u32 dircheck;

	end = adfs_fplus_offset(h, le32_to_cpu(h->bigdirentries)) +
		le32_to_cpu(h->bigdirnamesize);

	/* Accumulate the contents of the header, entries and names */
	for (dircheck = 0, bi = 0; end; bi++) {
		bp = (void *)dir->bhs[bi]->b_data;
		bs = dir->bhs[bi]->b_size;
		if (bs > end)
			bs = end;

		for (i = 0; i < bs; i += sizeof(u32))
			dircheck = ror32(dircheck, 13) ^ le32_to_cpup(bp++);

		end -= bs;
	}

	/* Accumulate the contents of the tail except for the check byte */
	dircheck = ror32(dircheck, 13) ^ le32_to_cpu(t->bigdirendname);
	dircheck = ror32(dircheck, 13) ^ t->bigdirendmasseq;
	dircheck = ror32(dircheck, 13) ^ t->reserved[0];
	dircheck = ror32(dircheck, 13) ^ t->reserved[1];

	return dircheck ^ dircheck >> 8 ^ dircheck >> 16 ^ dircheck >> 24;
}

static int adfs_fplus_read(struct super_block *sb, u32 indaddr,
			   unsigned int size, struct adfs_dir *dir)
{
	struct adfs_bigdirheader *h;
	struct adfs_bigdirtail *t;
	unsigned int dirsize;
	int ret;

	/* Read first buffer */
	ret = adfs_dir_read_buffers(sb, indaddr, sb->s_blocksize, dir);
	if (ret)
		return ret;

	dir->bighead = h = (void *)dir->bhs[0]->b_data;
	ret = adfs_fplus_validate_header(h);
	if (ret) {
		adfs_error(sb, "dir %06x has malformed header", indaddr);
		goto out;
	}

	dirsize = le32_to_cpu(h->bigdirsize);
	if (size && dirsize != size) {
		adfs_msg(sb, KERN_WARNING,
			 "dir %06x header size %X does not match directory size %X",
			 indaddr, dirsize, size);
	}

	/* Read remaining buffers */
	ret = adfs_dir_read_buffers(sb, indaddr, dirsize, dir);
	if (ret)
		return ret;

	dir->bigtail = t = (struct adfs_bigdirtail *)
		(dir->bhs[dir->nr_buffers - 1]->b_data + (sb->s_blocksize - 8));

	ret = adfs_fplus_validate_tail(h, t);
	if (ret) {
		adfs_error(sb, "dir %06x has malformed tail", indaddr);
		goto out;
	}

	if (adfs_fplus_checkbyte(dir) != t->bigdircheckbyte) {
		adfs_error(sb, "dir %06x checkbyte mismatch\n", indaddr);
		goto out;
	}

	dir->parent_id = le32_to_cpu(h->bigdirparent);
	return 0;

out:
	adfs_dir_relse(dir);

	return ret;
}

static int
adfs_fplus_setpos(struct adfs_dir *dir, unsigned int fpos)
{
	int ret = -ENOENT;

	if (fpos <= le32_to_cpu(dir->bighead->bigdirentries)) {
		dir->pos = fpos;
		ret = 0;
	}

	return ret;
}

static int
adfs_fplus_getnext(struct adfs_dir *dir, struct object_info *obj)
{
	struct adfs_bigdirheader *h = dir->bighead;
	struct adfs_bigdirentry bde;
	unsigned int offset;
	int ret;

	if (dir->pos >= le32_to_cpu(h->bigdirentries))
		return -ENOENT;

	offset = adfs_fplus_offset(h, dir->pos);

	ret = adfs_dir_copyfrom(&bde, dir, offset,
				sizeof(struct adfs_bigdirentry));
	if (ret)
		return ret;

	obj->loadaddr = le32_to_cpu(bde.bigdirload);
	obj->execaddr = le32_to_cpu(bde.bigdirexec);
	obj->size     = le32_to_cpu(bde.bigdirlen);
	obj->indaddr  = le32_to_cpu(bde.bigdirindaddr);
	obj->attr     = le32_to_cpu(bde.bigdirattr);
	obj->name_len = le32_to_cpu(bde.bigdirobnamelen);

	offset = adfs_fplus_offset(h, le32_to_cpu(h->bigdirentries));
	offset += le32_to_cpu(bde.bigdirobnameptr);

	ret = adfs_dir_copyfrom(obj->name, dir, offset, obj->name_len);
	if (ret)
		return ret;

	adfs_object_fixup(dir, obj);

	dir->pos += 1;

	return 0;
}

static int adfs_fplus_iterate(struct adfs_dir *dir, struct dir_context *ctx)
{
	struct object_info obj;

	if ((ctx->pos - 2) >> 32)
		return 0;

	if (adfs_fplus_setpos(dir, ctx->pos - 2))
		return 0;

	while (!adfs_fplus_getnext(dir, &obj)) {
		if (!dir_emit(ctx, obj.name, obj.name_len,
			      obj.indaddr, DT_UNKNOWN))
			break;
		ctx->pos++;
	}

	return 0;
}

static int adfs_fplus_update(struct adfs_dir *dir, struct object_info *obj)
{
	struct adfs_bigdirheader *h = dir->bighead;
	struct adfs_bigdirentry bde;
	int offset, end, ret;

	offset = adfs_fplus_offset(h, 0) - sizeof(bde);
	end = adfs_fplus_offset(h, le32_to_cpu(h->bigdirentries));

	do {
		offset += sizeof(bde);
		if (offset >= end) {
			adfs_error(dir->sb, "unable to locate entry to update");
			return -ENOENT;
		}
		ret = adfs_dir_copyfrom(&bde, dir, offset, sizeof(bde));
		if (ret) {
			adfs_error(dir->sb, "error reading directory entry");
			return -ENOENT;
		}
	} while (le32_to_cpu(bde.bigdirindaddr) != obj->indaddr);

	bde.bigdirload    = cpu_to_le32(obj->loadaddr);
	bde.bigdirexec    = cpu_to_le32(obj->execaddr);
	bde.bigdirlen     = cpu_to_le32(obj->size);
	bde.bigdirindaddr = cpu_to_le32(obj->indaddr);
	bde.bigdirattr    = cpu_to_le32(obj->attr);

	return adfs_dir_copyto(dir, offset, &bde, sizeof(bde));
}

static int adfs_fplus_commit(struct adfs_dir *dir)
{
	int ret;

	/* Increment directory sequence number */
	dir->bighead->startmasseq += 1;
	dir->bigtail->bigdirendmasseq += 1;

	/* Update directory check byte */
	dir->bigtail->bigdircheckbyte = adfs_fplus_checkbyte(dir);

	/* Make sure the directory still validates correctly */
	ret = adfs_fplus_validate_header(dir->bighead);
	if (ret == 0)
		ret = adfs_fplus_validate_tail(dir->bighead, dir->bigtail);

	return ret;
}

const struct adfs_dir_ops adfs_fplus_dir_ops = {
	.read		= adfs_fplus_read,
	.iterate	= adfs_fplus_iterate,
	.setpos		= adfs_fplus_setpos,
	.getnext	= adfs_fplus_getnext,
	.update		= adfs_fplus_update,
	.commit		= adfs_fplus_commit,
};
