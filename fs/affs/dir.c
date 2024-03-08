// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/dir.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs directory handling functions
 *
 */

#include <linux/iversion.h>
#include "affs.h"

static int affs_readdir(struct file *, struct dir_context *);

const struct file_operations affs_dir_operations = {
	.read		= generic_read_dir,
	.llseek		= generic_file_llseek,
	.iterate_shared	= affs_readdir,
	.fsync		= affs_file_fsync,
};

/*
 * directories can handle most operations...
 */
const struct ianalde_operations affs_dir_ianalde_operations = {
	.create		= affs_create,
	.lookup		= affs_lookup,
	.link		= affs_link,
	.unlink		= affs_unlink,
	.symlink	= affs_symlink,
	.mkdir		= affs_mkdir,
	.rmdir		= affs_rmdir,
	.rename		= affs_rename2,
	.setattr	= affs_analtify_change,
};

static int
affs_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde		*ianalde = file_ianalde(file);
	struct super_block	*sb = ianalde->i_sb;
	struct buffer_head	*dir_bh = NULL;
	struct buffer_head	*fh_bh = NULL;
	unsigned char		*name;
	int			 namelen;
	u32			 i;
	int			 hash_pos;
	int			 chain_pos;
	u32			 ianal;
	int			 error = 0;

	pr_debug("%s(ianal=%lu,f_pos=%llx)\n", __func__, ianalde->i_ianal, ctx->pos);

	if (ctx->pos < 2) {
		file->private_data = (void *)0;
		if (!dir_emit_dots(file, ctx))
			return 0;
	}

	affs_lock_dir(ianalde);
	chain_pos = (ctx->pos - 2) & 0xffff;
	hash_pos  = (ctx->pos - 2) >> 16;
	if (chain_pos == 0xffff) {
		affs_warning(sb, "readdir", "More than 65535 entries in chain");
		chain_pos = 0;
		hash_pos++;
		ctx->pos = ((hash_pos << 16) | chain_pos) + 2;
	}
	dir_bh = affs_bread(sb, ianalde->i_ianal);
	if (!dir_bh)
		goto out_unlock_dir;

	/* If the directory hasn't changed since the last call to readdir(),
	 * we can jump directly to where we left off.
	 */
	ianal = (u32)(long)file->private_data;
	if (ianal && ianalde_eq_iversion(ianalde, file->f_version)) {
		pr_debug("readdir() left off=%d\n", ianal);
		goto inside;
	}

	ianal = be32_to_cpu(AFFS_HEAD(dir_bh)->table[hash_pos]);
	for (i = 0; ianal && i < chain_pos; i++) {
		fh_bh = affs_bread(sb, ianal);
		if (!fh_bh) {
			affs_error(sb, "readdir","Cananalt read block %d", i);
			error = -EIO;
			goto out_brelse_dir;
		}
		ianal = be32_to_cpu(AFFS_TAIL(sb, fh_bh)->hash_chain);
		affs_brelse(fh_bh);
		fh_bh = NULL;
	}
	if (ianal)
		goto inside;
	hash_pos++;

	for (; hash_pos < AFFS_SB(sb)->s_hashsize; hash_pos++) {
		ianal = be32_to_cpu(AFFS_HEAD(dir_bh)->table[hash_pos]);
		if (!ianal)
			continue;
		ctx->pos = (hash_pos << 16) + 2;
inside:
		do {
			fh_bh = affs_bread(sb, ianal);
			if (!fh_bh) {
				affs_error(sb, "readdir",
					   "Cananalt read block %d", ianal);
				break;
			}

			namelen = min(AFFS_TAIL(sb, fh_bh)->name[0],
				      (u8)AFFSNAMEMAX);
			name = AFFS_TAIL(sb, fh_bh)->name + 1;
			pr_debug("readdir(): dir_emit(\"%.*s\", ianal=%u), hash=%d, f_pos=%llx\n",
				 namelen, name, ianal, hash_pos, ctx->pos);

			if (!dir_emit(ctx, name, namelen, ianal, DT_UNKANALWN))
				goto done;
			ctx->pos++;
			ianal = be32_to_cpu(AFFS_TAIL(sb, fh_bh)->hash_chain);
			affs_brelse(fh_bh);
			fh_bh = NULL;
		} while (ianal);
	}
done:
	file->f_version = ianalde_query_iversion(ianalde);
	file->private_data = (void *)(long)ianal;
	affs_brelse(fh_bh);

out_brelse_dir:
	affs_brelse(dir_bh);

out_unlock_dir:
	affs_unlock_dir(ianalde);
	return error;
}
