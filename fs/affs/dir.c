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

#include "affs.h"

static int affs_readdir(struct file *, struct dir_context *);

const struct file_operations affs_dir_operations = {
	.read		= generic_read_dir,
	.llseek		= generic_file_llseek,
	.iterate	= affs_readdir,
	.fsync		= affs_file_fsync,
};

/*
 * directories can handle most operations...
 */
const struct inode_operations affs_dir_inode_operations = {
	.create		= affs_create,
	.lookup		= affs_lookup,
	.link		= affs_link,
	.unlink		= affs_unlink,
	.symlink	= affs_symlink,
	.mkdir		= affs_mkdir,
	.rmdir		= affs_rmdir,
	.rename		= affs_rename,
	.setattr	= affs_notify_change,
};

static int
affs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode		*inode = file_inode(file);
	struct super_block	*sb = inode->i_sb;
	struct buffer_head	*dir_bh = NULL;
	struct buffer_head	*fh_bh = NULL;
	unsigned char		*name;
	int			 namelen;
	u32			 i;
	int			 hash_pos;
	int			 chain_pos;
	u32			 ino;
	int			 error = 0;

	pr_debug("%s(ino=%lu,f_pos=%lx)\n",
		 __func__, inode->i_ino, (unsigned long)ctx->pos);

	if (ctx->pos < 2) {
		file->private_data = (void *)0;
		if (!dir_emit_dots(file, ctx))
			return 0;
	}

	affs_lock_dir(inode);
	chain_pos = (ctx->pos - 2) & 0xffff;
	hash_pos  = (ctx->pos - 2) >> 16;
	if (chain_pos == 0xffff) {
		affs_warning(sb, "readdir", "More than 65535 entries in chain");
		chain_pos = 0;
		hash_pos++;
		ctx->pos = ((hash_pos << 16) | chain_pos) + 2;
	}
	dir_bh = affs_bread(sb, inode->i_ino);
	if (!dir_bh)
		goto out_unlock_dir;

	/* If the directory hasn't changed since the last call to readdir(),
	 * we can jump directly to where we left off.
	 */
	ino = (u32)(long)file->private_data;
	if (ino && file->f_version == inode->i_version) {
		pr_debug("readdir() left off=%d\n", ino);
		goto inside;
	}

	ino = be32_to_cpu(AFFS_HEAD(dir_bh)->table[hash_pos]);
	for (i = 0; ino && i < chain_pos; i++) {
		fh_bh = affs_bread(sb, ino);
		if (!fh_bh) {
			affs_error(sb, "readdir","Cannot read block %d", i);
			error = -EIO;
			goto out_brelse_dir;
		}
		ino = be32_to_cpu(AFFS_TAIL(sb, fh_bh)->hash_chain);
		affs_brelse(fh_bh);
		fh_bh = NULL;
	}
	if (ino)
		goto inside;
	hash_pos++;

	for (; hash_pos < AFFS_SB(sb)->s_hashsize; hash_pos++) {
		ino = be32_to_cpu(AFFS_HEAD(dir_bh)->table[hash_pos]);
		if (!ino)
			continue;
		ctx->pos = (hash_pos << 16) + 2;
inside:
		do {
			fh_bh = affs_bread(sb, ino);
			if (!fh_bh) {
				affs_error(sb, "readdir",
					   "Cannot read block %d", ino);
				break;
			}

			namelen = min(AFFS_TAIL(sb, fh_bh)->name[0], (u8)30);
			name = AFFS_TAIL(sb, fh_bh)->name + 1;
			pr_debug("readdir(): dir_emit(\"%.*s\", "
				 "ino=%u), hash=%d, f_pos=%x\n",
				 namelen, name, ino, hash_pos, (u32)ctx->pos);

			if (!dir_emit(ctx, name, namelen, ino, DT_UNKNOWN))
				goto done;
			ctx->pos++;
			ino = be32_to_cpu(AFFS_TAIL(sb, fh_bh)->hash_chain);
			affs_brelse(fh_bh);
			fh_bh = NULL;
		} while (ino);
	}
done:
	file->f_version = inode->i_version;
	file->private_data = (void *)(long)ino;
	affs_brelse(fh_bh);

out_brelse_dir:
	affs_brelse(dir_bh);

out_unlock_dir:
	affs_unlock_dir(inode);
	return error;
}
