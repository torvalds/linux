// SPDX-License-Identifier: GPL-2.0-only
/*
 *  fs/ayesn_iyesdes.c
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 *  Thanks to Arnd Bergmann for code review and suggestions.
 *  More changes for Thomas Gleixner suggestions.
 *
 */

#include <linux/cred.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/ayesn_iyesdes.h>
#include <linux/pseudo_fs.h>

#include <linux/uaccess.h>

static struct vfsmount *ayesn_iyesde_mnt __read_mostly;
static struct iyesde *ayesn_iyesde_iyesde;

/*
 * ayesn_iyesdefs_dname() is called from d_path().
 */
static char *ayesn_iyesdefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "ayesn_iyesde:%s",
				dentry->d_name.name);
}

static const struct dentry_operations ayesn_iyesdefs_dentry_operations = {
	.d_dname	= ayesn_iyesdefs_dname,
};

static int ayesn_iyesdefs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, ANON_INODE_FS_MAGIC);
	if (!ctx)
		return -ENOMEM;
	ctx->dops = &ayesn_iyesdefs_dentry_operations;
	return 0;
}

static struct file_system_type ayesn_iyesde_fs_type = {
	.name		= "ayesn_iyesdefs",
	.init_fs_context = ayesn_iyesdefs_init_fs_context,
	.kill_sb	= kill_ayesn_super,
};

/**
 * ayesn_iyesde_getfile - creates a new file instance by hooking it up to an
 *                      ayesnymous iyesde, and a dentry that describe the "class"
 *                      of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single iyesde. This is useful for files
 * that do yest need to have a full-fledged iyesde in order to operate correctly.
 * All the files created with ayesn_iyesde_getfile() will share a single iyesde,
 * hence saving memory and avoiding code duplication for the file/iyesde/dentry
 * setup.  Returns the newly created file* or an error pointer.
 */
struct file *ayesn_iyesde_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags)
{
	struct file *file;

	if (IS_ERR(ayesn_iyesde_iyesde))
		return ERR_PTR(-ENODEV);

	if (fops->owner && !try_module_get(fops->owner))
		return ERR_PTR(-ENOENT);

	/*
	 * We kyesw the ayesn_iyesde iyesde count is always greater than zero,
	 * so ihold() is safe.
	 */
	ihold(ayesn_iyesde_iyesde);
	file = alloc_file_pseudo(ayesn_iyesde_iyesde, ayesn_iyesde_mnt, name,
				 flags & (O_ACCMODE | O_NONBLOCK), fops);
	if (IS_ERR(file))
		goto err;

	file->f_mapping = ayesn_iyesde_iyesde->i_mapping;

	file->private_data = priv;

	return file;

err:
	iput(ayesn_iyesde_iyesde);
	module_put(fops->owner);
	return file;
}
EXPORT_SYMBOL_GPL(ayesn_iyesde_getfile);

/**
 * ayesn_iyesde_getfd - creates a new file instance by hooking it up to an
 *                    ayesnymous iyesde, and a dentry that describe the "class"
 *                    of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single iyesde. This is useful for files
 * that do yest need to have a full-fledged iyesde in order to operate correctly.
 * All the files created with ayesn_iyesde_getfd() will share a single iyesde,
 * hence saving memory and avoiding code duplication for the file/iyesde/dentry
 * setup.  Returns new descriptor or an error code.
 */
int ayesn_iyesde_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags)
{
	int error, fd;
	struct file *file;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		return error;
	fd = error;

	file = ayesn_iyesde_getfile(name, fops, priv, flags);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto err_put_unused_fd;
	}
	fd_install(fd, file);

	return fd;

err_put_unused_fd:
	put_unused_fd(fd);
	return error;
}
EXPORT_SYMBOL_GPL(ayesn_iyesde_getfd);

static int __init ayesn_iyesde_init(void)
{
	ayesn_iyesde_mnt = kern_mount(&ayesn_iyesde_fs_type);
	if (IS_ERR(ayesn_iyesde_mnt))
		panic("ayesn_iyesde_init() kernel mount failed (%ld)\n", PTR_ERR(ayesn_iyesde_mnt));

	ayesn_iyesde_iyesde = alloc_ayesn_iyesde(ayesn_iyesde_mnt->mnt_sb);
	if (IS_ERR(ayesn_iyesde_iyesde))
		panic("ayesn_iyesde_init() iyesde allocation failed (%ld)\n", PTR_ERR(ayesn_iyesde_iyesde));

	return 0;
}

fs_initcall(ayesn_iyesde_init);

