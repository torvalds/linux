/*
 *  linux/fs/adfs/dir_fplus.c
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/slab.h>
#include "adfs.h"
#include "dir_fplus.h"

static int
adfs_fplus_read(struct super_block *sb, unsigned int id, unsigned int sz, struct adfs_dir *dir)
{
	struct adfs_bigdirheader *h;
	struct adfs_bigdirtail *t;
	unsigned long block;
	unsigned int blk, size;
	int i, ret = -EIO;

	dir->nr_buffers = 0;

	/* start off using fixed bh set - only alloc for big dirs */
	dir->bh_fplus = &dir->bh[0];

	block = __adfs_block_map(sb, id, 0);
	if (!block) {
		adfs_error(sb, "dir object %X has a hole at offset 0", id);
		goto out;
	}

	dir->bh_fplus[0] = sb_bread(sb, block);
	if (!dir->bh_fplus[0])
		goto out;
	dir->nr_buffers += 1;

	h = (struct adfs_bigdirheader *)dir->bh_fplus[0]->b_data;
	size = le32_to_cpu(h->bigdirsize);
	if (size != sz) {
		adfs_msg(sb, KERN_WARNING,
			 "directory header size %X does not match directory size %X",
			 size, sz);
	}

	if (h->bigdirversion[0] != 0 || h->bigdirversion[1] != 0 ||
	    h->bigdirversion[2] != 0 || size & 2047 ||
	    h->bigdirstartname != cpu_to_le32(BIGDIRSTARTNAME)) {
		adfs_error(sb, "dir %06x has malformed header", id);
		goto out;
	}

	size >>= sb->s_blocksize_bits;
	if (size > ARRAY_SIZE(dir->bh)) {
		/* this directory is too big for fixed bh set, must allocate */
		struct buffer_head **bh_fplus =
			kcalloc(size, sizeof(struct buffer_head *),
				GFP_KERNEL);
		if (!bh_fplus) {
			adfs_msg(sb, KERN_ERR,
				 "not enough memory for dir object %X (%d blocks)",
				 id, size);
			ret = -ENOMEM;
			goto out;
		}
		dir->bh_fplus = bh_fplus;
		/* copy over the pointer to the block that we've already read */
		dir->bh_fplus[0] = dir->bh[0];
	}

	for (blk = 1; blk < size; blk++) {
		block = __adfs_block_map(sb, id, blk);
		if (!block) {
			adfs_error(sb, "dir object %X has a hole at offset %d", id, blk);
			goto out;
		}

		dir->bh_fplus[blk] = sb_bread(sb, block);
		if (!dir->bh_fplus[blk]) {
			adfs_error(sb,	"dir object %x failed read for offset %d, mapped block %lX",
				   id, blk, block);
			goto out;
		}

		dir->nr_buffers += 1;
	}

	t = (struct adfs_bigdirtail *)
		(dir->bh_fplus[size - 1]->b_data + (sb->s_blocksize - 8));

	if (t->bigdirendname != cpu_to_le32(BIGDIRENDNAME) ||
	    t->bigdirendmasseq != h->startmasseq ||
	    t->reserved[0] != 0 || t->reserved[1] != 0) {
		adfs_error(sb, "dir %06x has malformed tail", id);
		goto out;
	}

	dir->parent_id = le32_to_cpu(h->bigdirparent);
	dir->sb = sb;
	return 0;

out:
	if (dir->bh_fplus) {
		for (i = 0; i < dir->nr_buffers; i++)
			brelse(dir->bh_fplus[i]);

		if (&dir->bh[0] != dir->bh_fplus)
			kfree(dir->bh_fplus);

		dir->bh_fplus = NULL;
	}

	dir->nr_buffers = 0;
	dir->sb = NULL;
	return ret;
}

static int
adfs_fplus_setpos(struct adfs_dir *dir, unsigned int fpos)
{
	struct adfs_bigdirheader *h =
		(struct adfs_bigdirheader *) dir->bh_fplus[0]->b_data;
	int ret = -ENOENT;

	if (fpos <= le32_to_cpu(h->bigdirentries)) {
		dir->pos = fpos;
		ret = 0;
	}

	return ret;
}

static void
dir_memcpy(struct adfs_dir *dir, unsigned int offset, void *to, int len)
{
	struct super_block *sb = dir->sb;
	unsigned int buffer, partial, remainder;

	buffer = offset >> sb->s_blocksize_bits;
	offset &= sb->s_blocksize - 1;

	partial = sb->s_blocksize - offset;

	if (partial >= len)
		memcpy(to, dir->bh_fplus[buffer]->b_data + offset, len);
	else {
		char *c = (char *)to;

		remainder = len - partial;

		memcpy(c,
			dir->bh_fplus[buffer]->b_data + offset,
			partial);

		memcpy(c + partial,
			dir->bh_fplus[buffer + 1]->b_data,
			remainder);
	}
}

static int
adfs_fplus_getnext(struct adfs_dir *dir, struct object_info *obj)
{
	struct adfs_bigdirheader *h =
		(struct adfs_bigdirheader *) dir->bh_fplus[0]->b_data;
	struct adfs_bigdirentry bde;
	unsigned int offset;
	int ret = -ENOENT;

	if (dir->pos >= le32_to_cpu(h->bigdirentries))
		goto out;

	offset = offsetof(struct adfs_bigdirheader, bigdirname);
	offset += ((le32_to_cpu(h->bigdirnamelen) + 4) & ~3);
	offset += dir->pos * sizeof(struct adfs_bigdirentry);

	dir_memcpy(dir, offset, &bde, sizeof(struct adfs_bigdirentry));

	obj->loadaddr = le32_to_cpu(bde.bigdirload);
	obj->execaddr = le32_to_cpu(bde.bigdirexec);
	obj->size     = le32_to_cpu(bde.bigdirlen);
	obj->indaddr  = le32_to_cpu(bde.bigdirindaddr);
	obj->attr     = le32_to_cpu(bde.bigdirattr);
	obj->name_len = le32_to_cpu(bde.bigdirobnamelen);

	offset = offsetof(struct adfs_bigdirheader, bigdirname);
	offset += ((le32_to_cpu(h->bigdirnamelen) + 4) & ~3);
	offset += le32_to_cpu(h->bigdirentries) * sizeof(struct adfs_bigdirentry);
	offset += le32_to_cpu(bde.bigdirobnameptr);

	dir_memcpy(dir, offset, obj->name, obj->name_len);
	adfs_object_fixup(dir, obj);

	dir->pos += 1;
	ret = 0;
out:
	return ret;
}

static int
adfs_fplus_sync(struct adfs_dir *dir)
{
	int err = 0;
	int i;

	for (i = dir->nr_buffers - 1; i >= 0; i--) {
		struct buffer_head *bh = dir->bh_fplus[i];
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			err = -EIO;
	}

	return err;
}

static void
adfs_fplus_free(struct adfs_dir *dir)
{
	int i;

	if (dir->bh_fplus) {
		for (i = 0; i < dir->nr_buffers; i++)
			brelse(dir->bh_fplus[i]);

		if (&dir->bh[0] != dir->bh_fplus)
			kfree(dir->bh_fplus);

		dir->bh_fplus = NULL;
	}

	dir->nr_buffers = 0;
	dir->sb = NULL;
}

const struct adfs_dir_ops adfs_fplus_dir_ops = {
	.read		= adfs_fplus_read,
	.setpos		= adfs_fplus_setpos,
	.getnext	= adfs_fplus_getnext,
	.sync		= adfs_fplus_sync,
	.free		= adfs_fplus_free
};
