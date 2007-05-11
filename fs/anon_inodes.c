/*
 *  fs/anon_inodes.c
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 *  Thanks to Arnd Bergmann for code review and suggestions.
 *  More changes for Thomas Gleixner suggestions.
 *
 */

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
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

static int anon_inodefs_delete_dentry(struct dentry *dentry)
{
	/*
	 * We faked vfs to believe the dentry was hashed when we created it.
	 * Now we restore the flag so that dput() will work correctly.
	 */
	dentry->d_flags |= DCACHE_UNHASHED;
	return 1;
}

static struct file_system_type anon_inode_fs_type = {
	.name		= "anon_inodefs",
	.get_sb		= anon_inodefs_get_sb,
	.kill_sb	= kill_anon_super,
};
static struct dentry_operations anon_inodefs_dentry_operations = {
	.d_delete	= anon_inodefs_delete_dentry,
};

/**
 * anon_inode_getfd - creates a new file instance by hooking it up to and
 *                    anonymous inode, and a dentry that describe the "class"
 *                    of the file
 *
 * @pfd:     [out]   pointer to the file descriptor
 * @dpinode: [out]   pointer to the inode
 * @pfile:   [out]   pointer to the file struct
 * @name:    [in]    name of the "class" of the new file
 * @fops     [in]    file operations for the new file
 * @priv     [in]    private data for the new file (will be file's private_data)
 *
 * Creates a new file by hooking it on a single inode. This is useful for files
 * that do not need to have a full-fledged inode in order to operate correctly.
 * All the files created with anon_inode_getfd() will share a single inode, by
 * hence saving memory and avoiding code duplication for the file/inode/dentry
 * setup.
 */
int anon_inode_getfd(int *pfd, struct inode **pinode, struct file **pfile,
		     const char *name, const struct file_operations *fops,
		     void *priv)
{
	struct qstr this;
	struct dentry *dentry;
	struct inode *inode;
	struct file *file;
	int error, fd;

	if (IS_ERR(anon_inode_inode))
		return -ENODEV;
	file = get_empty_filp();
	if (!file)
		return -ENFILE;

	inode = igrab(anon_inode_inode);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		goto err_put_filp;
	}

	error = get_unused_fd();
	if (error < 0)
		goto err_iput;
	fd = error;

	/*
	 * Link the inode to a directory entry by creating a unique name
	 * using the inode sequence number.
	 */
	error = -ENOMEM;
	this.name = name;
	this.len = strlen(name);
	this.hash = 0;
	dentry = d_alloc(anon_inode_mnt->mnt_sb->s_root, &this);
	if (!dentry)
		goto err_put_unused_fd;
	dentry->d_op = &anon_inodefs_dentry_operations;
	/* Do not publish this dentry inside the global dentry hash table */
	dentry->d_flags &= ~DCACHE_UNHASHED;
	d_instantiate(dentry, inode);

	file->f_path.mnt = mntget(anon_inode_mnt);
	file->f_path.dentry = dentry;
	file->f_mapping = inode->i_mapping;

	file->f_pos = 0;
	file->f_flags = O_RDWR;
	file->f_op = fops;
	file->f_mode = FMODE_READ | FMODE_WRITE;
	file->f_version = 0;
	file->private_data = priv;

	fd_install(fd, file);

	*pfd = fd;
	*pinode = inode;
	*pfile = file;
	return 0;

err_put_unused_fd:
	put_unused_fd(fd);
err_iput:
	iput(inode);
err_put_filp:
	put_filp(file);
	return error;
}

/*
 * A single inode exist for all anon_inode files. Contrary to pipes,
 * anon_inode inodes has no per-instance data associated, so we can avoid
 * the allocation of multiple of them.
 */
static struct inode *anon_inode_mkinode(void)
{
	struct inode *inode = new_inode(anon_inode_mnt->mnt_sb);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_fop = &anon_inode_fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because mark_inode_dirty() will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
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

