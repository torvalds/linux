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

#include <asm/uaccess.h>

static struct vfsmount *anon_inode_mnt __read_mostly;
static struct inode *anon_inode_inode;
static const struct file_operations anon_inode_fops;

static int anon_inodefs_get_sb(struct file_system_type *fs_type, int flags,
			       const char *dev_name, void *data,
			       struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "anon_inode:", NULL, ANON_INODE_FS_MAGIC,
			     mnt);
}

/*
 * anon_inodefs_dname() is called from d_path().
 */
static char *anon_inodefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "anon_inode:%s",
				dentry->d_name.name);
}

static struct file_system_type anon_inode_fs_type = {
	.name		= "anon_inodefs",
	.get_sb		= anon_inodefs_get_sb,
	.kill_sb	= kill_anon_super,
};
static const struct dentry_operations anon_inodefs_dentry_operations = {
	.d_dname	= anon_inodefs_dname,
};

/*
 * nop .set_page_dirty method so that people can use .page_mkwrite on
 * anon inodes.
 */
static int anon_set_page_dirty(struct page *page)
{
	return 0;
};

static const struct address_space_operations anon_aops = {
	.set_page_dirty = anon_set_page_dirty,
};

/**
 * anon_inode_getfd - creates a new file instance by hooking it up to an
 *                    anonymous inode, and a dentry that describe the "class"
 *                    of the file
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
	struct qstr this;
	struct path path;
	struct file *file;
	int error;

	if (IS_ERR(anon_inode_inode))
		return ERR_PTR(-ENODEV);

	if (fops->owner && !try_module_get(fops->owner))
		return ERR_PTR(-ENOENT);

	/*
	 * Link the inode to a directory entry by creating a unique name
	 * using the inode sequence number.
	 */
	error = -ENOMEM;
	this.name = name;
	this.len = strlen(name);
	this.hash = 0;
	path.dentry = d_alloc(anon_inode_mnt->mnt_sb->s_root, &this);
	if (!path.dentry)
		goto err_module;

	path.mnt = mntget(anon_inode_mnt);
	/*
	 * We know the anon_inode inode count is always greater than zero,
	 * so we can avoid doing an igrab() and we can use an open-coded
	 * atomic_inc().
	 */
	atomic_inc(&anon_inode_inode->i_count);

	path.dentry->d_op = &anon_inodefs_dentry_operations;
	d_instantiate(path.dentry, anon_inode_inode);

	error = -ENFILE;
	file = alloc_file(&path, OPEN_FMODE(flags), fops);
	if (!file)
		goto err_dput;
	file->f_mapping = anon_inode_inode->i_mapping;

	file->f_pos = 0;
	file->f_flags = flags & (O_ACCMODE | O_NONBLOCK);
	file->f_version = 0;
	file->private_data = priv;

	return file;

err_dput:
	path_put(&path);
err_module:
	module_put(fops->owner);
	return ERR_PTR(error);
}
EXPORT_SYMBOL_GPL(anon_inode_getfile);

/**
 * anon_inode_getfd - creates a new file instance by hooking it up to an
 *                    anonymous inode, and a dentry that describe the "class"
 *                    of the file
 *
 * @name:    [in]    name of the "class" of the new file
 * @fops:    [in]    file operations for the new file
 * @priv:    [in]    private data for the new file (will be file's private_data)
 * @flags:   [in]    flags
 *
 * Creates a new file by hooking it on a single inode. This is useful for files
 * that do not need to have a full-fledged inode in order to operate correctly.
 * All the files created with anon_inode_getfd() will share a single inode,
 * hence saving memory and avoiding code duplication for the file/inode/dentry
 * setup.  Returns new descriptor or an error code.
 */
int anon_inode_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags)
{
	int error, fd;
	struct file *file;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		return error;
	fd = error;

	file = anon_inode_getfile(name, fops, priv, flags);
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
EXPORT_SYMBOL_GPL(anon_inode_getfd);

/*
 * A single inode exists for all anon_inode files. Contrary to pipes,
 * anon_inode inodes have no associated per-instance data, so we need
 * only allocate one of them.
 */
static struct inode *anon_inode_mkinode(void)
{
	struct inode *inode = new_inode(anon_inode_mnt->mnt_sb);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_fop = &anon_inode_fops;

	inode->i_mapping->a_ops = &anon_aops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because mark_inode_dirty() will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IRUSR | S_IWUSR;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_flags |= S_PRIVATE;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	return inode;
}

static int __init anon_inode_init(void)
{
	int error;

	error = register_filesystem(&anon_inode_fs_type);
	if (error)
		goto err_exit;
	anon_inode_mnt = kern_mount(&anon_inode_fs_type);
	if (IS_ERR(anon_inode_mnt)) {
		error = PTR_ERR(anon_inode_mnt);
		goto err_unregister_filesystem;
	}
	anon_inode_inode = anon_inode_mkinode();
	if (IS_ERR(anon_inode_inode)) {
		error = PTR_ERR(anon_inode_inode);
		goto err_mntput;
	}

	return 0;

err_mntput:
	mntput(anon_inode_mnt);
err_unregister_filesystem:
	unregister_filesystem(&anon_inode_fs_type);
err_exit:
	panic(KERN_ERR "anon_inode_init() failed (%d)\n", error);
}

fs_initcall(anon_inode_init);

