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
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>

#include "spufs.h"

static ssize_t do_coredump_read(int num, struct spu_context *ctx, void *buffer,
				size_t size, loff_t *off)
{
	u64 data;
	int ret;

	if (spufs_coredump_read[num].read)
		return spufs_coredump_read[num].read(ctx, buffer, size, off);

	data = spufs_coredump_read[num].get(ctx);
	ret = snprintf(buffer, size, "0x%.16llx", data);
	if (ret >= size)
		return size;
	return ++ret; /* count trailing NULL */
}

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
static int spufs_dump_write(struct file *file, const void *addr, int nr, loff_t *foffset)
{
	unsigned long limit = rlimit(RLIMIT_CORE);
	ssize_t written;

	if (*foffset + nr > limit)
		return -EIO;

	written = file->f_op->write(file, addr, nr, &file->f_pos);
	*foffset += written;

	if (written != nr)
		return -EIO;

	return 0;
}

static int spufs_dump_align(struct file *file, char *buf, loff_t new_off,
			    loff_t *foffset)
{
	int rc, size;

	size = min((loff_t)PAGE_SIZE, new_off - *foffset);
	memset(buf, 0, size);

	rc = 0;
	while (rc == 0 && new_off > *foffset) {
		size = min((loff_t)PAGE_SIZE, new_off - *foffset);
		rc = spufs_dump_write(file, buf, size, foffset);
	}

	return rc;
}

static int spufs_ctx_note_size(struct spu_context *ctx, int dfd)
{
	int i, sz, total = 0;
	char *name;
	char fullname[80];

	for (i = 0; spufs_coredump_read[i].name != NULL; i++) {
		name = spufs_coredump_read[i].name;
		sz = spufs_coredump_read[i].size;

		sprintf(fullname, "SPU/%d/%s", dfd, name);

		total += sizeof(struct elf_note);
		total += roundup(strlen(fullname) + 1, 4);
		total += roundup(sz, 4);
	}

	return total;
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
static struct spu_context *coredump_next_context(int *fd)
{
	struct fdtable *fdt = files_fdtable(current->files);
	struct file *file;
	struct spu_context *ctx = NULL;

	for (; *fd < fdt->max_fds; (*fd)++) {
		if (!FD_ISSET(*fd, fdt->open_fds))
			continue;

		file = fcheck(*fd);

		if (!file || file->f_op != &spufs_context_fops)
			continue;

		ctx = SPUFS_I(file->f_dentry->d_inode)->i_ctx;
		if (ctx->flags & SPU_CREATE_NOSCHED)
			continue;

		break;
	}

	return ctx;
}

int spufs_coredump_extra_notes_size(void)
{
	struct spu_context *ctx;
	int size = 0, rc, fd;

	fd = 0;
	while ((ctx = coredump_next_context(&fd)) != NULL) {
		rc = spu_acquire_saved(ctx);
		if (rc)
			break;
		rc = spufs_ctx_note_size(ctx, fd);
		spu_release_saved(ctx);
		if (rc < 0)
			break;

		size += rc;

		/* start searching the next fd next time */
		fd++;
	}

	return size;
}

static int spufs_arch_write_note(struct spu_context *ctx, int i,
				  struct file *file, int dfd, loff_t *foffset)
{
	loff_t pos = 0;
	int sz, rc, nread, total = 0;
	const int bufsz = PAGE_SIZE;
	char *name;
	char fullname[80], *buf;
	struct elf_note en;

	buf = (void *)get_zeroed_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	name = spufs_coredump_read[i].name;
	sz = spufs_coredump_read[i].size;

	sprintf(fullname, "SPU/%d/%s", dfd, name);
	en.n_namesz = strlen(fullname) + 1;
	en.n_descsz = sz;
	en.n_type = NT_SPU;

	rc = spufs_dump_write(file, &en, sizeof(en), foffset);
	if (rc)
		goto out;

	rc = spufs_dump_write(file, fullname, en.n_namesz, foffset);
	if (rc)
		goto out;

	rc = spufs_dump_align(file, buf, roundup(*foffset, 4), foffset);
	if (rc)
		goto out;

	do {
		nread = do_coredump_read(i, ctx, buf, bufsz, &pos);
		if (nread > 0) {
			rc = spufs_dump_write(file, buf, nread, foffset);
			if (rc)
				goto out;
			total += nread;
		}
	} while (nread == bufsz && total < sz);

	if (nread < 0) {
		rc = nread;
		goto out;
	}

	rc = spufs_dump_align(file, buf, roundup(*foffset - total + sz, 4),
			      foffset);

out:
	free_page((unsigned long)buf);
	return rc;
}

int spufs_coredump_extra_notes_write(struct file *file, loff_t *foffset)
{
	struct spu_context *ctx;
	int fd, j, rc;

	fd = 0;
	while ((ctx = coredump_next_context(&fd)) != NULL) {
		rc = spu_acquire_saved(ctx);
		if (rc)
			return rc;

		for (j = 0; spufs_coredump_read[j].name != NULL; j++) {
			rc = spufs_arch_write_note(ctx, j, file, fd, foffset);
			if (rc) {
				spu_release_saved(ctx);
				return rc;
			}
		}

		spu_release_saved(ctx);

		/* start searching the next fd next time */
		fd++;
	}

	return 0;
}
