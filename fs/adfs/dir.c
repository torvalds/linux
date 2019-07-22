// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/adfs/dir.c
 *
 *  Copyright (C) 1999-2000 Russell King
 *
 *  Common directory handling for ADFS
 */
#include "adfs.h"

/*
 * For future.  This should probably be per-directory.
 */
static DEFINE_RWLOCK(adfs_dir_lock);

void adfs_object_fixup(struct adfs_dir *dir, struct object_info *obj)
{
	unsigned int dots, i;

	/*
	 * RISC OS allows the use of '/' in directory entry names, so we need
	 * to fix these up.  '/' is typically used for FAT compatibility to
	 * represent '.', so do the same conversion here.  In any case, '.'
	 * will never be in a RISC OS name since it is used as the pathname
	 * separator.  Handle the case where we may generate a '.' or '..'
	 * name, replacing the first character with '^' (the RISC OS "parent
	 * directory" character.)
	 */
	for (i = dots = 0; i < obj->name_len; i++)
		if (obj->name[i] == '/') {
			obj->name[i] = '.';
			dots++;
		}

	if (obj->name_len <= 2 && dots == obj->name_len)
		obj->name[0] = '^';

	/*
	 * If the object is a file, and the user requested the ,xyz hex
	 * filetype suffix to the name, check the filetype and append.
	 */
	if (!(obj->attr & ADFS_NDA_DIRECTORY) && ADFS_SB(dir->sb)->s_ftsuffix) {
		u16 filetype = adfs_filetype(obj->loadaddr);

		if (filetype != ADFS_FILETYPE_NONE) {
			obj->name[obj->name_len++] = ',';
			obj->name[obj->name_len++] = hex_asc_lo(filetype >> 8);
			obj->name[obj->name_len++] = hex_asc_lo(filetype >> 4);
			obj->name[obj->name_len++] = hex_asc_lo(filetype >> 0);
		}
	}
}

static int
adfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	const struct adfs_dir_ops *ops = ADFS_SB(sb)->s_dir;
	struct object_info obj;
	struct adfs_dir dir;
	int ret = 0;

	if (ctx->pos >> 32)
		return 0;

	ret = ops->read(sb, inode->i_ino, inode->i_size, &dir);
	if (ret)
		return ret;

	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			goto free_out;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit(ctx, "..", 2, dir.parent_id, DT_DIR))
			goto free_out;
		ctx->pos = 2;
	}

	read_lock(&adfs_dir_lock);

	ret = ops->setpos(&dir, ctx->pos - 2);
	if (ret)
		goto unlock_out;
	while (ops->getnext(&dir, &obj) == 0) {
		if (!dir_emit(ctx, obj.name, obj.name_len,
			      obj.indaddr, DT_UNKNOWN))
			break;
		ctx->pos++;
	}

unlock_out:
	read_unlock(&adfs_dir_lock);

free_out:
	ops->free(&dir);
	return ret;
}

int
adfs_dir_update(struct super_block *sb, struct object_info *obj, int wait)
{
	int ret = -EINVAL;
#ifdef CONFIG_ADFS_FS_RW
	const struct adfs_dir_ops *ops = ADFS_SB(sb)->s_dir;
	struct adfs_dir dir;

	printk(KERN_INFO "adfs_dir_update: object %06x in dir %06x\n",
		 obj->indaddr, obj->parent_id);

	if (!ops->update) {
		ret = -EINVAL;
		goto out;
	}

	ret = ops->read(sb, obj->parent_id, 0, &dir);
	if (ret)
		goto out;

	write_lock(&adfs_dir_lock);
	ret = ops->update(&dir, obj);
	write_unlock(&adfs_dir_lock);

	if (wait) {
		int err = ops->sync(&dir);
		if (!ret)
			ret = err;
	}

	ops->free(&dir);
out:
#endif
	return ret;
}

static unsigned char adfs_tolower(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	return c;
}

static int __adfs_compare(const unsigned char *qstr, u32 qlen,
			  const char *str, u32 len)
{
	u32 i;

	if (qlen != len)
		return 1;

	for (i = 0; i < qlen; i++)
		if (adfs_tolower(qstr[i]) != adfs_tolower(str[i]))
			return 1;

	return 0;
}

static int adfs_dir_lookup_byname(struct inode *inode, const struct qstr *qstr,
				  struct object_info *obj)
{
	struct super_block *sb = inode->i_sb;
	const struct adfs_dir_ops *ops = ADFS_SB(sb)->s_dir;
	const unsigned char *name;
	struct adfs_dir dir;
	u32 name_len;
	int ret;

	ret = ops->read(sb, inode->i_ino, inode->i_size, &dir);
	if (ret)
		goto out;

	if (ADFS_I(inode)->parent_id != dir.parent_id) {
		adfs_error(sb,
			   "parent directory changed under me! (%06x but got %06x)\n",
			   ADFS_I(inode)->parent_id, dir.parent_id);
		ret = -EIO;
		goto free_out;
	}

	obj->parent_id = inode->i_ino;

	read_lock(&adfs_dir_lock);

	ret = ops->setpos(&dir, 0);
	if (ret)
		goto unlock_out;

	ret = -ENOENT;
	name = qstr->name;
	name_len = qstr->len;
	while (ops->getnext(&dir, obj) == 0) {
		if (!__adfs_compare(name, name_len, obj->name, obj->name_len)) {
			ret = 0;
			break;
		}
	}

unlock_out:
	read_unlock(&adfs_dir_lock);

free_out:
	ops->free(&dir);
out:
	return ret;
}

const struct file_operations adfs_dir_operations = {
	.read		= generic_read_dir,
	.llseek		= generic_file_llseek,
	.iterate	= adfs_readdir,
	.fsync		= generic_file_fsync,
};

static int
adfs_hash(const struct dentry *parent, struct qstr *qstr)
{
	const unsigned char *name;
	unsigned long hash;
	u32 len;

	if (qstr->len > ADFS_SB(parent->d_sb)->s_namelen)
		return -ENAMETOOLONG;

	len = qstr->len;
	name = qstr->name;
	hash = init_name_hash(parent);
	while (len--)
		hash = partial_name_hash(adfs_tolower(*name++), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Compare two names, taking note of the name length
 * requirements of the underlying filesystem.
 */
static int adfs_compare(const struct dentry *dentry, unsigned int len,
			const char *str, const struct qstr *qstr)
{
	return __adfs_compare(qstr->name, qstr->len, str, len);
}

const struct dentry_operations adfs_dentry_operations = {
	.d_hash		= adfs_hash,
	.d_compare	= adfs_compare,
};

static struct dentry *
adfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct inode *inode = NULL;
	struct object_info obj;
	int error;

	error = adfs_dir_lookup_byname(dir, &dentry->d_name, &obj);
	if (error == 0) {
		/*
		 * This only returns NULL if get_empty_inode
		 * fails.
		 */
		inode = adfs_iget(dir->i_sb, &obj);
		if (!inode)
			inode = ERR_PTR(-EACCES);
	} else if (error != -ENOENT) {
		inode = ERR_PTR(error);
	}
	return d_splice_alias(inode, dentry);
}

/*
 * directories can handle most operations...
 */
const struct inode_operations adfs_dir_inode_operations = {
	.lookup		= adfs_lookup,
	.setattr	= adfs_notify_change,
};
