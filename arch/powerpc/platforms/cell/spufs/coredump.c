/*
 * SPU core dump code
 *
 * (C) Copyright 2006 IBM Corp.
 *
 * Author: Dwayne Grant McConnell <decimal@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/elf.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>

#include "spufs.h"

struct spufs_ctx_info {
	struct list_head list;
	int dfd;
	int memsize; /* in bytes */
	struct spu_context *ctx;
};

static LIST_HEAD(ctx_info_list);

static ssize_t do_coredump_read(int num, struct spu_context *ctx, void __user *buffer,
				size_t size, loff_t *off)
{
	u64 data;
	int ret;

	if (spufs_coredump_read[num].read)
		return spufs_coredump_read[num].read(ctx, buffer, size, off);

	data = spufs_coredump_read[num].get(ctx);
	ret = copy_to_user(buffer, &data, 8);
	return ret ? -EFAULT : 8;
}

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
static int spufs_dump_write(struct file *file, const void *addr, int nr)
{
	return file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}

static int spufs_dump_seek(struct file *file, loff_t off)
{
	if (file->f_op->llseek) {
		if (file->f_op->llseek(file, off, 0) != off)
			return 0;
	} else
		file->f_pos = off;
	return 1;
}

static void spufs_fill_memsize(struct spufs_ctx_info *ctx_info)
{
	struct spu_context *ctx;
	unsigned long long lslr;

	ctx = ctx_info->ctx;
	lslr = ctx->csa.priv2.spu_lslr_RW;
	ctx_info->memsize = lslr + 1;
}

static int spufs_ctx_note_size(struct spufs_ctx_info *ctx_info)
{
	int dfd, memsize, i, sz, total = 0;
	char *name;
	char fullname[80];

	dfd = ctx_info->dfd;
	memsize = ctx_info->memsize;

	for (i = 0; spufs_coredump_read[i].name; i++) {
		name = spufs_coredump_read[i].name;
		sz = spufs_coredump_read[i].size;

		sprintf(fullname, "SPU/%d/%s", dfd, name);

		total += sizeof(struct elf_note);
		total += roundup(strlen(fullname) + 1, 4);
		if (!strcmp(name, "mem"))
			total += roundup(memsize, 4);
		else
			total += roundup(sz, 4);
	}

	return total;
}

static int spufs_add_one_context(struct file *file, int dfd)
{
	struct spu_context *ctx;
	struct spufs_ctx_info *ctx_info;
	int size;

	ctx = SPUFS_I(file->f_dentry->d_inode)->i_ctx;
	if (ctx->flags & SPU_CREATE_NOSCHED)
		return 0;

	ctx_info = kzalloc(sizeof(*ctx_info), GFP_KERNEL);
	if (unlikely(!ctx_info))
		return -ENOMEM;

	ctx_info->dfd = dfd;
	ctx_info->ctx = ctx;

	spufs_fill_memsize(ctx_info);

	size = spufs_ctx_note_size(ctx_info);
	list_add(&ctx_info->list, &ctx_info_list);
	return size;
}

/*
 * The additional architecture-specific notes for Cell are various
 * context files in the spu context.
 *
 * This function iterates over all open file descriptors and sees
 * if they are a directory in spufs.  In that case we use spufs
 * internal functionality to dump them without needing to actually
 * open the files.
 */
static int spufs_arch_notes_size(void)
{
	struct fdtable *fdt = files_fdtable(current->files);
	int size = 0, fd;

	for (fd = 0; fd < fdt->max_fds; fd++) {
		if (FD_ISSET(fd, fdt->open_fds)) {
			struct file *file = fcheck(fd);

			if (file && file->f_op == &spufs_context_fops) {
				int rval = spufs_add_one_context(file, fd);
				if (rval < 0)
					break;
				size += rval;
			}
		}
	}

	return size;
}

static void spufs_arch_write_note(struct spufs_ctx_info *ctx_info, int i,
				struct file *file)
{
	struct spu_context *ctx;
	loff_t pos = 0;
	int sz, dfd, rc, total = 0;
	const int bufsz = PAGE_SIZE;
	char *name;
	char fullname[80], *buf;
	struct elf_note en;

	buf = (void *)get_zeroed_page(GFP_KERNEL);
	if (!buf)
		return;

	dfd = ctx_info->dfd;
	name = spufs_coredump_read[i].name;

	if (!strcmp(name, "mem"))
		sz = ctx_info->memsize;
	else
		sz = spufs_coredump_read[i].size;

	ctx = ctx_info->ctx;
	if (!ctx)
		goto out;

	sprintf(fullname, "SPU/%d/%s", dfd, name);
	en.n_namesz = strlen(fullname) + 1;
	en.n_descsz = sz;
	en.n_type = NT_SPU;

	if (!spufs_dump_write(file, &en, sizeof(en)))
		goto out;
	if (!spufs_dump_write(file, fullname, en.n_namesz))
		goto out;
	if (!spufs_dump_seek(file, roundup((unsigned long)file->f_pos, 4)))
		goto out;

	do {
		rc = do_coredump_read(i, ctx, buf, bufsz, &pos);
		if (rc > 0) {
			if (!spufs_dump_write(file, buf, rc))
				goto out;
			total += rc;
		}
	} while (rc == bufsz && total < sz);

	spufs_dump_seek(file, roundup((unsigned long)file->f_pos
						- total + sz, 4));
out:
	free_page((unsigned long)buf);
}

static void spufs_arch_write_notes(struct file *file)
{
	int j;
	struct spufs_ctx_info *ctx_info, *next;

	list_for_each_entry_safe(ctx_info, next, &ctx_info_list, list) {
		spu_acquire_saved(ctx_info->ctx);
		for (j = 0; j < spufs_coredump_num_notes; j++)
			spufs_arch_write_note(ctx_info, j, file);
		spu_release(ctx_info->ctx);
		list_del(&ctx_info->list);
		kfree(ctx_info);
	}
}

struct spu_coredump_calls spufs_coredump_calls = {
	.arch_notes_size = spufs_arch_notes_size,
	.arch_write_notes = spufs_arch_write_notes,
	.owner = THIS_MODULE,
};
