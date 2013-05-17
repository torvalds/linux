/*
 *  linux/fs/adfs/dir.c
 *
 *  Copyright (C) 1999-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Common directory handling for ADFS
 */
#include "adfs.h"

/*
 * For future.  This should probably be per-directory.
 */
static DEFINE_RWLOCK(adfs_dir_lock);

static int
adfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct adfs_dir_ops *ops = ADFS_SB(sb)->s_dir;
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
			    obj.file_id, DT_UNKNOWN))
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
	struct adfs_dir_ops *ops = ADFS_SB(sb)->s_dir;
	struct adfs_dir dir;

	printk(KERN_INFO "adfs_dir_update: object %06X in dir %06X\n",
		 obj->file_id, obj->parent_id);

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

static int
adfs_match(struct qstr *name, struct object_info *obj)
{
	int i;

	if (name->len != obj->name_len)
		return 0;

	for (i = 0; i < name->len; i++) {
		char c1, c2;

		c1 = name->name[i];
		c2 = obj->name[i];

		if (c1 >= 'A' && c1 <= 'Z')
			c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z')
			c2 += 'a' - 'A';

		if (c1 != c2)
			return 0;
	}
	return 1;
}

static int
adfs_dir_lookup_byname(struct inode *inode, struct qstr *name, struct object_info *obj)
{
	struct super_block *sb = inode->i_sb;
	struct adfs_dir_ops *ops = ADFS_SB(sb)->s_dir;
	struct adfs_dir dir;
	int ret;

	ret = ops->read(sb, inode->i_ino, inode->i_size, &dir);
	if (ret)
		goto out;

	if (ADFS_I(inode)->parent_id != dir.parent_id) {
		adfs_error(sb, "parent directory changed under me! (%lx but got %lx)\n",
			   ADFS_I(inode)->parent_id, dir.parent_id);
		ret = -EIO;
		goto free_out;
	}

	obj->parent_id = inode->i_ino;

	/*
	 * '.' is handled by reserved_lookup() in fs/namei.c
	 */
	if (name->len == 2 && name->name[0] == '.' && name->name[1] == '.') {
		/*
		 * Currently unable to fill in the rest of 'obj',
		 * but this is better than nothing.  We need to
		 * ascend one level to find it's parent.
		 */
		obj->name_len = 0;
		obj->file_id  = obj->parent_id;
		goto free_out;
	}

	read_lock(&adfs_dir_lock);

	ret = ops->setpos(&dir, 0);
	if (ret)
		goto unlock_out;

	ret = -ENOENT;
	while (ops->getnext(&dir, obj) == 0) {
		if (adfs_match(name, obj)) {
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
adfs_hash(const struct dentry *parent, const struct inode *inode,
		struct qstr *qstr)
{
	const unsigned int name_len = ADFS_SB(parent->d_sb)->s_namelen;
	const unsigned char *name;
	unsigned long hash;
	int i;

	if (qstr->len < name_len)
		return 0;

	/*
	 * Truncate the name in place, avoids
	 * having to define a compare function.
	 */
	qstr->len = i = name_len;
	name = qstr->name;
	hash = init_name_hash();
	while (i--) {
		char c;

		c = *name++;
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';

		hash = partial_name_hash(c, hash);
	}
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Compare two names, taking note of the name length
 * requirements of the underlying filesystem.
 */
static int
adfs_compare(const struct dentry *parent, const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	int i;

	if (len != name->len)
		return 1;

	for (i = 0; i < name->len; i++) {
		char a, b;

		a = str[i];
		b = name->name[i];

		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z')
			b += 'a' - 'A';

		if (a != b)
			return 1;
	}
	return 0;
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
		error = -EACCES;
		/*
		 * This only returns NULL if get_empty_inode
		 * fails.
		 */
		inode = adfs_iget(dir->i_sb, &obj);
		if (inode)
			error = 0;
	}
	d_add(dentry, inode);
	return ERR_PTR(error);
}

/*
 * directories can handle most operations...
 */
const struct inode_operations adfs_dir_inode_operations = {
	.lookup		= adfs_lookup,
	.setattr	= adfs_notify_change,
};
