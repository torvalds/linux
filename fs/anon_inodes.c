// SPDX-License-Identifier: GPL-2.0-only
/*
 *  fs/anon_inodes.c
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
#include <linux/anon_inodes.h>
#include <linux/pseudo_fs.h>

#include <linux/uaccess.h>

#include "internal.h"

static struct vfsmount *anon_inode_mnt __ro_after_init;
static struct inode *anon_inode_inode __ro_after_init;

/*
 * User space expects anonymous inodes to have no file type in st_mode.
 *
 * In particular, 'lsof' has this legacy logic:
 *
 *	type = s->st_mode & S_IFMT;
 *	switch (type) {
 *	  ...
 *	case 0:
 *		if (!strcmp(p, "anon_inode"))
 *			Lf->ntype = Ntype = N_ANON_INODE;
 *
 * to detect our old anon_inode logic.
 *
 * Rather than mess with our internal sane inode data, just fix it
 * up here in getattr() by masking off the format bits.
 */
int anon_inode_getattr(struct mnt_idmap *idmap, const struct path *path,
		       struct kstat *stat, u32 request_mask,
		       unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);

	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	stat->mode &= ~S_IFMT;
	return 0;
}

int anon_inode_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		       struct iattr *attr)
{
	return -EOPNOTSUPP;
}

static const struct inode_operations anon_inode_operations = {
	.getattr = anon_inode_getattr,
	.setattr = anon_inode_setattr,
};

/*
 * anon_inodefs_dname() is called from d_path().
 */
static char *anon_inodefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(buffer, buflen, "anon_inode:%s",
				dentry->d_name.name);
}

static const struct dentry_operations anon_inodefs_dentry_operations = {
	.d_dname	= anon_inodefs_dname,
};

static int anon_inodefs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, ANON_INODE_FS_MAGIC);
	if (!ctx)
		return -ENOMEM;
	fc->s_iflags |= SB_I_NOEXEC;
	fc->s_iflags |= SB_I_NODEV;
	ctx->dops = &anon_inodefs_dentry_operations;
	return 0;
}

static struct file_system_type anon_inode_fs_type = {
	.name		= "anon_inodefs",
	.init_fs_context = anon_inodefs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

/**
 * anon_inode_make_secure_inode - allocate an anonymous inode with security context
 * @sb:		[in]	Superblock to allocate from
 * @name:	[in]	Name of the class of the newfile (e.g., "secretmem")
 * @context_inode:
 *		[in]	Optional parent inode for security inheritance
 *
 * The function ensures proper security initialization through the LSM hook
 * security_inode_init_security_anon().
 *
 * Return:	Pointer to new inode on success, ERR_PTR on failure.
 */
struct inode *anon_inode_make_secure_inode(struct super_block *sb, const char *name,
					   const struct inode *context_inode)
{
	struct inode *inode;
	int error;

	inode = alloc_anon_inode(sb);
	if (IS_ERR(inode))
		return inode;
	inode->i_flags &= ~S_PRIVATE;
	inode->i_op = &anon_inode_operations;
	error =	security_inode_init_security_anon(inode, &QSTR(name),
						  context_inode);
	if (error) {
		iput(inode);
		return ERR_PTR(error);
	}
	return inode;
}
EXPORT_SYMBOL_FOR_MODULES(anon_inode_make_secure_inode, "kvm");

static struct file *__anon_inode_getfile(const char *name,
					 const struct file_operations *fops,
					 void *priv, int flags,
					 const struct inode *context_inode,
					 bool make_inode)
{
	struct inode *inode;
	struct file *file;

	if (fops->owner && !try_module_get(fops->owner))
		return ERR_PTR(-ENOENT);

	if (make_inode) {
		inode =	anon_inode_make_secure_inode(anon_inode_mnt->mnt_sb,
						     name, context_inode);
		if (IS_ERR(inode)) {
			file = ERR_CAST(inode);
			goto err;
		}
	} else {
		inode =	anon_inode_inode;
		if (IS_ERR(inode)) {
			file = ERR_PTR(-ENODEV);
			goto err;
		}
		/*
		 * We know the anon_inode inode count is always
		 * greater than zero, so ihold() is safe.
		 */
		ihold(inode);
	}

	file = alloc_file_pseudo(inode, anon_inode_mnt, name,
				 flags & (O_ACCMODE | O_NONBLOCK), fops);
	if (IS_ERR(file))
		goto err_iput;

	file->f_mapping = inode->i_mapping;

	file->private_data = priv;

	return file;

err_iput:
	iput(inode);
err:
	module_put(fops->owner);
	return file;
}

/**
 * anon_inode_getfile - creates a new file instance by hooking it up to an
 *                      anonymous inode, and a dentry that describe the "class"
 *                      of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single inode. This is useful for files
 * that do not need to have a full-fledged inode in order to operate correctly.
 * All the files created with anon_inode_getfile() will share a single inode,
 * hence saving memory and avoiding code duplication for the file/inode/dentry
 * setup.  Returns the newly created file* or an error pointer.
 */
struct file *anon_inode_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags)
{
	return __anon_inode_getfile(name, fops, priv, flags, NULL, false);
}
EXPORT_SYMBOL_GPL(anon_inode_getfile);

/**
 * anon_inode_getfile_fmode - creates a new file instance by hooking it up to an
 *                      anonymous inode, and a dentry that describe the "class"
 *                      of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 * @f_mode:  [in]    fmode
 *
 * Creates a new file by hooking it on a single inode. This is useful for files
 * that do not need to have a full-fledged inode in order to operate correctly.
 * All the files created with anon_inode_getfile() will share a single inode,
 * hence saving memory and avoiding code duplication for the file/inode/dentry
 * setup. Allows setting the fmode. Returns the newly created file* or an error
 * pointer.
 */
struct file *anon_inode_getfile_fmode(const char *name,
				const struct file_operations *fops,
				void *priv, int flags, fmode_t f_mode)
{
	struct file *file;

	file = __anon_inode_getfile(name, fops, priv, flags, NULL, false);
	if (!IS_ERR(file))
		file->f_mode |= f_mode;

	return file;
}
EXPORT_SYMBOL_GPL(anon_inode_getfile_fmode);

/**
 * anon_inode_create_getfile - Like anon_inode_getfile(), but creates a new
 *                             !S_PRIVATE anon inode rather than reuse the
 *                             singleton anon inode and calls the
 *                             inode_init_security_anon() LSM hook.
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 * @context_inode:
 *           [in]    the logical relationship with the new inode (optional)
 *
 * Create a new anonymous inode and file pair.  This can be done for two
 * reasons:
 *
 * - for the inode to have its own security context, so that LSMs can enforce
 *   policy on the inode's creation;
 *
 * - if the caller needs a unique inode, for example in order to customize
 *   the size returned by fstat()
 *
 * The LSM may use @context_inode in inode_init_security_anon(), but a
 * reference to it is not held.
 *
 * Returns the newly created file* or an error pointer.
 */
struct file *anon_inode_create_getfile(const char *name,
				       const struct file_operations *fops,
				       void *priv, int flags,
				       const struct inode *context_inode)
{
	return __anon_inode_getfile(name, fops, priv, flags,
				    context_inode, true);
}
EXPORT_SYMBOL_GPL(anon_inode_create_getfile);

static int __anon_inode_getfd(const char *name,
			      const struct file_operations *fops,
			      void *priv, int flags,
			      const struct inode *context_inode,
			      bool make_inode)
{
	int error, fd;
	struct file *file;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		return error;
	fd = error;

	file = __anon_inode_getfile(name, fops, priv, flags, context_inode,
				    make_inode);
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

/**
 * anon_inode_getfd - creates a new file instance by hooking it up to
 *                    an anonymous inode and a dentry that describe
 *                    the "class" of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single inode. This is
 * useful for files that do not need to have a full-fledged inode in
 * order to operate correctly.  All the files created with
 * anon_inode_getfd() will use the same singleton inode, reducing
 * memory use and avoiding code duplication for the file/inode/dentry
 * setup.  Returns a newly created file descriptor or an error code.
 */
int anon_inode_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags)
{
	return __anon_inode_getfd(name, fops, priv, flags, NULL, false);
}
EXPORT_SYMBOL_GPL(anon_inode_getfd);

/**
 * anon_inode_create_getfd - Like anon_inode_getfd(), but creates a new
 * !S_PRIVATE anon inode rather than reuse the singleton anon inode, and calls
 * the inode_init_security_anon() LSM hook.
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 * @context_inode:
 *           [in]    the logical relationship with the new inode (optional)
 *
 * Create a new anonymous inode and file pair.  This can be done for two
 * reasons:
 *
 * - for the inode to have its own security context, so that LSMs can enforce
 *   policy on the inode's creation;
 *
 * - if the caller needs a unique inode, for example in order to customize
 *   the size returned by fstat()
 *
 * The LSM may use @context_inode in inode_init_security_anon(), but a
 * reference to it is not held.
 *
 * Returns a newly created file descriptor or an error code.
 */
int anon_inode_create_getfd(const char *name, const struct file_operations *fops,
			    void *priv, int flags,
			    const struct inode *context_inode)
{
	return __anon_inode_getfd(name, fops, priv, flags, context_inode, true);
}


static int __init anon_inode_init(void)
{
	anon_inode_mnt = kern_mount(&anon_inode_fs_type);
	if (IS_ERR(anon_inode_mnt))
		panic("anon_inode_init() kernel mount failed (%ld)\n", PTR_ERR(anon_inode_mnt));

	anon_inode_inode = alloc_anon_inode(anon_inode_mnt->mnt_sb);
	if (IS_ERR(anon_inode_inode))
		panic("anon_inode_init() inode allocation failed (%ld)\n", PTR_ERR(anon_inode_inode));
	anon_inode_inode->i_op = &anon_inode_operations;

	return 0;
}

fs_initcall(anon_inode_init);

