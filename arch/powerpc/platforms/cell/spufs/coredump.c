// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPU core dump code
 *
 * (C) Copyright 2006 IBM Corp.
 *
 * Author: Dwayne Grant McConnell <decimal@us.ibm.com>
 */

#include <linux/elf.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/coredump.h>
#include <linux/binfmts.h>

#include <linux/uaccess.h>

#include "spufs.h"

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

static int match_context(const void *v, struct file *file, unsigned fd)
{
	struct spu_context *ctx;
	if (file->f_op != &spufs_context_fops)
		return 0;
	ctx = SPUFS_I(file_inode(file))->i_ctx;
	if (ctx->flags & SPU_CREATE_NOSCHED)
		return 0;
	return fd + 1;
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
/*
 * descriptor table is not shared, so files can't change or go away.
 */
static struct spu_context *coredump_next_context(int *fd)
{
	struct spu_context *ctx;
	struct file *file;
	int n = iterate_fd(current->files, *fd, match_context, NULL);
	if (!n)
		return NULL;
	*fd = n - 1;

	rcu_read_lock();
	file = lookup_fd_rcu(*fd);
	ctx = SPUFS_I(file_inode(file))->i_ctx;
	get_spu_context(ctx);
	rcu_read_unlock();

	return ctx;
}

int spufs_coredump_extra_notes_size(void)
{
	struct spu_context *ctx;
	int size = 0, rc, fd;

	fd = 0;
	while ((ctx = coredump_next_context(&fd)) != NULL) {
		rc = spu_acquire_saved(ctx);
		if (rc) {
			put_spu_context(ctx);
			break;
		}

		rc = spufs_ctx_note_size(ctx, fd);
		spu_release_saved(ctx);
		if (rc < 0) {
			put_spu_context(ctx);
			break;
		}

		size += rc;

		/* start searching the next fd next time */
		fd++;
		put_spu_context(ctx);
	}

	return size;
}

static int spufs_arch_write_note(struct spu_context *ctx, int i,
				  struct coredump_params *cprm, int dfd)
{
	size_t sz = spufs_coredump_read[i].size;
	char fullname[80];
	struct elf_note en;
	int ret;

	sprintf(fullname, "SPU/%d/%s", dfd, spufs_coredump_read[i].name);
	en.n_namesz = strlen(fullname) + 1;
	en.n_descsz = sz;
	en.n_type = NT_SPU;

	if (!dump_emit(cprm, &en, sizeof(en)))
		return -EIO;
	if (!dump_emit(cprm, fullname, en.n_namesz))
		return -EIO;
	if (!dump_align(cprm, 4))
		return -EIO;

	if (spufs_coredump_read[i].dump) {
		ret = spufs_coredump_read[i].dump(ctx, cprm);
		if (ret < 0)
			return ret;
	} else {
		char buf[32];

		ret = snprintf(buf, sizeof(buf), "0x%.16llx",
			       spufs_coredump_read[i].get(ctx));
		if (ret >= sizeof(buf))
			return sizeof(buf);

		/* count trailing the NULL: */
		if (!dump_emit(cprm, buf, ret + 1))
			return -EIO;
	}

	dump_skip_to(cprm, roundup(cprm->pos - ret + sz, 4));
	return 0;
}

int spufs_coredump_extra_notes_write(struct coredump_params *cprm)
{
	struct spu_context *ctx;
	int fd, j, rc;

	fd = 0;
	while ((ctx = coredump_next_context(&fd)) != NULL) {
		rc = spu_acquire_saved(ctx);
		if (rc)
			return rc;

		for (j = 0; spufs_coredump_read[j].name != NULL; j++) {
			rc = spufs_arch_write_note(ctx, j, cprm, fd);
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
