/*
 *  linux/fs/adfs/dir_fplus.c
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/spinlock.h>
#include <linux/buffer_head.h>
#include <linux/string.h>

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

	block = __adfs_block_map(sb, id, 0);
	if (!block) {
		adfs_error(sb, "dir object %X has a hole at offset 0", id);
		goto out;
	}

	dir->bh[0] = sb_bread(sb, block);
	if (!dir->bh[0])
		goto out;
	dir->nr_buffers += 1;

	h = (struct adfs_bigdirheader *)dir->bh[0]->b_data;
	size = le32_to_cpu(h->bigdirsize);
	if (size != sz) {
		printk(KERN_WARNING "adfs: adfs_fplus_read: directory header size\n"
				" does not match directory size\n");
	}

	if (h->bigdirversion[0] != 0 || h->bigdirversion[1] != 0 ||
	    h->bigdirversion[2] != 0 || size & 2047 ||
	    h->bigdirstartname != cpu_to_le32(BIGDIRSTARTNAME))
		goto out;

	size >>= sb->s_blocksize_bits;
	for (blk = 1; blk < size; blk++) {
		block = __adfs_block_map(sb, id, blk);
		if (!block) {
			adfs_error(sb, "dir object %X has a hole at offset %d", id, blk);
			goto out;
		}

		dir->bh[blk] = sb_bread(sb, block);
		if (!dir->bh[blk])
			goto out;
		dir->nr_buffers = blk;
	}

	t = (struct adfs_bigdirtail *)(dir->bh[size - 1]->b_data + (sb->s_blocksize - 8));

	if (t->bigdirendname != cpu_to_le32(BIGDIRENDNAME) ||
	    t->bigdirendmasseq != h->startmasseq ||
	    t->reserved[0] != 0 || t->reserved[1] != 0)
		goto out;

	dir->parent_id = le32_to_cpu(h->bigdirparent);
	dir->sb = sb;
	return 0;
out:
	for (i = 0; i < dir->nr_buffers; i++)
		brelse(dir->bh[i]);
	dir->sb = NULL;
	return ret;
}

static int
adfs_fplus_setpos(struct adfs_dir *dir, unsigned int fpos)
{
	struct adfs_bigdirheader *h = (struct adfs_bigdirheader *)dir->bh[0]->b_data;
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
		memcpy(to, dir->bh[buffer]->b_data + offset, len);
	else {
		char *c = (char *)to;

		remainder = len - partial;

		memcpy(c, dir->bh[buffer]->b_data + offset, partial);
		memcpy(c + partial, dir->bh[buffer + 1]->b_data, remainder);
	}
}

static int
adfs_fplus_getnext(struct adfs_dir *dir, struct object_info *obj)
{
	struct adfs_bigdirheader *h = (struct adfs_bigdirheader *)dir->bh[0]->b_data;
	struct adfs_bigdirentry bde;
	unsigned int offset;
	int i, ret = -ENOENT;

	if (dir->pos >= le32_to_cpu(h->bigdirentries))
		goto out;

	offset = offsetof(struct adfs_bigdirheader, bigdirname);
	offset += ((le32_to_cpu(h->bigdirnamelen) + 4) & ~3);
	offset += dir->pos * sizeof(struct adfs_bigdirentry);

	dir_memcpy(dir, offset, &bde, sizeof(struct adfs_bigdirentry));

	obj->loadaddr = le32_to_cpu(bde.bigdirload);
	obj->execaddr = le32_to_cpu(bde.bigdirexec);
	obj->size     = le32_to_cpu(bde.bigdirlen);
	obj->file_id  = le32_to_cpu(bde.bigdirindaddr);
	obj->attr     = le32_to_cpu(bde.bigdirattr);
	obj->name_len = le32_to_cpu(bde.bigdirobnamelen);

	offset = offsetof(struct adfs_bigdirheader, bigdirname);
	offset += ((le32_to_cpu(h->bigdirnamelen) + 4) & ~3);
	offset += le32_to_cpu(h->bigdirentries) * sizeof(struct adfs_bigdirentry);
	offset += le32_to_cpu(bde.bigdirobnameptr);

	dir_memcpy(dir, offset, obj->name, obj->name_len);
	for (i = 0; i < obj->name_len; i++)
		if (obj->name[i] == '/')
			obj->name[i] = '.';

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
		struct buffer_head *bh = dir->bh[i];
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

	for (i = 0; i < dir->nr_buffers; i++)
		brelse(dir->bh[i]);
	dir->sb = NULL;
}

struct adfs_dir_ops adfs_fplus_dir_ops = {
	.read		= adfs_fplus_read,
	.setpos		= adfs_fplus_setpos,
	.getnext	= adfs_fplus_getnext,
	.sync		= adfs_fplus_sync,
	.free		= adfs_fplus_free
};
