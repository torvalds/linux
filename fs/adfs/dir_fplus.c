// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/dir_fplus.c
 *
 *  Copyright (C) 1997-1999 Russell King
 */
#include "adfs.h"
#include "dir_fplus.h"

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

	h = (struct adfs_bigdirheader *)dir->bhs[0]->b_data;
	dirsize = le32_to_cpu(h->bigdirsize);
	if (dirsize != size) {
		adfs_msg(sb, KERN_WARNING,
			 "dir %06x header size %X does not match directory size %X",
			 indaddr, dirsize, size);
	}

	if (h->bigdirversion[0] != 0 || h->bigdirversion[1] != 0 ||
	    h->bigdirversion[2] != 0 || size & 2047 ||
	    h->bigdirstartname != cpu_to_le32(BIGDIRSTARTNAME)) {
		adfs_error(sb, "dir %06x has malformed header", indaddr);
		goto out;
	}

	/* Read remaining buffers */
	ret = adfs_dir_read_buffers(sb, indaddr, dirsize, dir);
	if (ret)
		return ret;

	t = (struct adfs_bigdirtail *)
		(dir->bhs[dir->nr_buffers - 1]->b_data + (sb->s_blocksize - 8));

	if (t->bigdirendname != cpu_to_le32(BIGDIRENDNAME) ||
	    t->bigdirendmasseq != h->startmasseq ||
	    t->reserved[0] != 0 || t->reserved[1] != 0) {
		adfs_error(sb, "dir %06x has malformed tail", indaddr);
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
	struct adfs_bigdirheader *h =
		(struct adfs_bigdirheader *) dir->bhs[0]->b_data;
	int ret = -ENOENT;

	if (fpos <= le32_to_cpu(h->bigdirentries)) {
		dir->pos = fpos;
		ret = 0;
	}

	return ret;
}

static int
adfs_fplus_getnext(struct adfs_dir *dir, struct object_info *obj)
{
	struct adfs_bigdirheader *h =
		(struct adfs_bigdirheader *) dir->bhs[0]->b_data;
	struct adfs_bigdirentry bde;
	unsigned int offset;
	int ret;

	if (dir->pos >= le32_to_cpu(h->bigdirentries))
		return -ENOENT;

	offset = offsetof(struct adfs_bigdirheader, bigdirname);
	offset += ((le32_to_cpu(h->bigdirnamelen) + 4) & ~3);
	offset += dir->pos * sizeof(struct adfs_bigdirentry);

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

	offset = offsetof(struct adfs_bigdirheader, bigdirname);
	offset += ((le32_to_cpu(h->bigdirnamelen) + 4) & ~3);
	offset += le32_to_cpu(h->bigdirentries) * sizeof(struct adfs_bigdirentry);
	offset += le32_to_cpu(bde.bigdirobnameptr);

	ret = adfs_dir_copyfrom(obj->name, dir, offset, obj->name_len);
	if (ret)
		return ret;

	adfs_object_fixup(dir, obj);

	dir->pos += 1;

	return 0;
}

const struct adfs_dir_ops adfs_fplus_dir_ops = {
	.read		= adfs_fplus_read,
	.setpos		= adfs_fplus_setpos,
	.getnext	= adfs_fplus_getnext,
};
