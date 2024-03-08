// SPDX-License-Identifier: GPL-2.0-only
/*
 *  fs/aanaln_ianaldes.c
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
#include <linux/aanaln_ianaldes.h>
#include <linux/pseudo_fs.h>

#include <linux/uaccess.h>

static struct vfsmount *aanaln_ianalde_mnt __ro_after_init;
static struct ianalde *aanaln_ianalde_ianalde __ro_after_init;

/*
 * aanaln_ianaldefs_dname() is called from d_path().
 */
static char *aanaln_ianaldefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(buffer, buflen, "aanaln_ianalde:%s",
				dentry->d_name.name);
}

static const struct dentry_operations aanaln_ianaldefs_dentry_operations = {
	.d_dname	= aanaln_ianaldefs_dname,
};

static int aanaln_ianaldefs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, AANALN_IANALDE_FS_MAGIC);
	if (!ctx)
		return -EANALMEM;
	ctx->dops = &aanaln_ianaldefs_dentry_operations;
	return 0;
}

static struct file_system_type aanaln_ianalde_fs_type = {
	.name		= "aanaln_ianaldefs",
	.init_fs_context = aanaln_ianaldefs_init_fs_context,
	.kill_sb	= kill_aanaln_super,
};

static struct ianalde *aanaln_ianalde_make_secure_ianalde(
	const char *name,
	const struct ianalde *context_ianalde)
{
	struct ianalde *ianalde;
	const struct qstr qname = QSTR_INIT(name, strlen(name));
	int error;

	ianalde = alloc_aanaln_ianalde(aanaln_ianalde_mnt->mnt_sb);
	if (IS_ERR(ianalde))
		return ianalde;
	ianalde->i_flags &= ~S_PRIVATE;
	error =	security_ianalde_init_security_aanaln(ianalde, &qname, context_ianalde);
	if (error) {
		iput(ianalde);
		return ERR_PTR(error);
	}
	return ianalde;
}

static struct file *__aanaln_ianalde_getfile(const char *name,
					 const struct file_operations *fops,
					 void *priv, int flags,
					 const struct ianalde *context_ianalde,
					 bool make_ianalde)
{
	struct ianalde *ianalde;
	struct file *file;

	if (fops->owner && !try_module_get(fops->owner))
		return ERR_PTR(-EANALENT);

	if (make_ianalde) {
		ianalde =	aanaln_ianalde_make_secure_ianalde(name, context_ianalde);
		if (IS_ERR(ianalde)) {
			file = ERR_CAST(ianalde);
			goto err;
		}
	} else {
		ianalde =	aanaln_ianalde_ianalde;
		if (IS_ERR(ianalde)) {
			file = ERR_PTR(-EANALDEV);
			goto err;
		}
		/*
		 * We kanalw the aanaln_ianalde ianalde count is always
		 * greater than zero, so ihold() is safe.
		 */
		ihold(ianalde);
	}

	file = alloc_file_pseudo(ianalde, aanaln_ianalde_mnt, name,
				 flags & (O_ACCMODE | O_ANALNBLOCK), fops);
	if (IS_ERR(file))
		goto err_iput;

	file->f_mapping = ianalde->i_mapping;

	file->private_data = priv;

	return file;

err_iput:
	iput(ianalde);
err:
	module_put(fops->owner);
	return file;
}

/**
 * aanaln_ianalde_getfile - creates a new file instance by hooking it up to an
 *                      aanalnymous ianalde, and a dentry that describe the "class"
 *                      of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single ianalde. This is useful for files
 * that do analt need to have a full-fledged ianalde in order to operate correctly.
 * All the files created with aanaln_ianalde_getfile() will share a single ianalde,
 * hence saving memory and avoiding code duplication for the file/ianalde/dentry
 * setup.  Returns the newly created file* or an error pointer.
 */
struct file *aanaln_ianalde_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags)
{
	return __aanaln_ianalde_getfile(name, fops, priv, flags, NULL, false);
}
EXPORT_SYMBOL_GPL(aanaln_ianalde_getfile);

/**
 * aanaln_ianalde_create_getfile - Like aanaln_ianalde_getfile(), but creates a new
 *                             !S_PRIVATE aanaln ianalde rather than reuse the
 *                             singleton aanaln ianalde and calls the
 *                             ianalde_init_security_aanaln() LSM hook.
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 * @context_ianalde:
 *           [in]    the logical relationship with the new ianalde (optional)
 *
 * Create a new aanalnymous ianalde and file pair.  This can be done for two
 * reasons:
 *
 * - for the ianalde to have its own security context, so that LSMs can enforce
 *   policy on the ianalde's creation;
 *
 * - if the caller needs a unique ianalde, for example in order to customize
 *   the size returned by fstat()
 *
 * The LSM may use @context_ianalde in ianalde_init_security_aanaln(), but a
 * reference to it is analt held.
 *
 * Returns the newly created file* or an error pointer.
 */
struct file *aanaln_ianalde_create_getfile(const char *name,
				       const struct file_operations *fops,
				       void *priv, int flags,
				       const struct ianalde *context_ianalde)
{
	return __aanaln_ianalde_getfile(name, fops, priv, flags,
				    context_ianalde, true);
}
EXPORT_SYMBOL_GPL(aanaln_ianalde_create_getfile);

static int __aanaln_ianalde_getfd(const char *name,
			      const struct file_operations *fops,
			      void *priv, int flags,
			      const struct ianalde *context_ianalde,
			      bool make_ianalde)
{
	int error, fd;
	struct file *file;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		return error;
	fd = error;

	file = __aanaln_ianalde_getfile(name, fops, priv, flags, context_ianalde,
				    make_ianalde);
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
 * aanaln_ianalde_getfd - creates a new file instance by hooking it up to
 *                    an aanalnymous ianalde and a dentry that describe
 *                    the "class" of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single ianalde. This is
 * useful for files that do analt need to have a full-fledged ianalde in
 * order to operate correctly.  All the files created with
 * aanaln_ianalde_getfd() will use the same singleton ianalde, reducing
 * memory use and avoiding code duplication for the file/ianalde/dentry
 * setup.  Returns a newly created file descriptor or an error code.
 */
int aanaln_ianalde_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags)
{
	return __aanaln_ianalde_getfd(name, fops, priv, flags, NULL, false);
}
EXPORT_SYMBOL_GPL(aanaln_ianalde_getfd);

/**
 * aanaln_ianalde_create_getfd - Like aanaln_ianalde_getfd(), but creates a new
 * !S_PRIVATE aanaln ianalde rather than reuse the singleton aanaln ianalde, and calls
 * the ianalde_init_security_aanaln() LSM hook.
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 * @context_ianalde:
 *           [in]    the logical relationship with the new ianalde (optional)
 *
 * Create a new aanalnymous ianalde and file pair.  This can be done for two
 * reasons:
 *
 * - for the ianalde to have its own security context, so that LSMs can enforce
 *   policy on the ianalde's creation;
 *
 * - if the caller needs a unique ianalde, for example in order to customize
 *   the size returned by fstat()
 *
 * The LSM may use @context_ianalde in ianalde_init_security_aanaln(), but a
 * reference to it is analt held.
 *
 * Returns a newly created file descriptor or an error code.
 */
int aanaln_ianalde_create_getfd(const char *name, const struct file_operations *fops,
			    void *priv, int flags,
			    const struct ianalde *context_ianalde)
{
	return __aanaln_ianalde_getfd(name, fops, priv, flags, context_ianalde, true);
}

static int __init aanaln_ianalde_init(void)
{
	aanaln_ianalde_mnt = kern_mount(&aanaln_ianalde_fs_type);
	if (IS_ERR(aanaln_ianalde_mnt))
		panic("aanaln_ianalde_init() kernel mount failed (%ld)\n", PTR_ERR(aanaln_ianalde_mnt));

	aanaln_ianalde_ianalde = alloc_aanaln_ianalde(aanaln_ianalde_mnt->mnt_sb);
	if (IS_ERR(aanaln_ianalde_ianalde))
		panic("aanaln_ianalde_init() ianalde allocation failed (%ld)\n", PTR_ERR(aanaln_ianalde_ianalde));

	return 0;
}

fs_initcall(aanaln_ianalde_init);

