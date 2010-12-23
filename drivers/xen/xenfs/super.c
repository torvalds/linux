/*
 *  xenfs.c - a filesystem for passing info between the a domain and
 *  the hypervisor.
 *
 * 2008-10-07  Alex Zeffertt    Replaced /proc/xen/xenbus with xenfs filesystem
 *                              and /proc/xen compatibility mount point.
 *                              Turned xenfs into a loadable module.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/magic.h>

#include <xen/xen.h>

#include "xenfs.h"

#include <asm/xen/hypervisor.h>

MODULE_DESCRIPTION("Xen filesystem");
MODULE_LICENSE("GPL");

static struct inode *xenfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_mode = mode;
		ret->i_uid = ret->i_gid = 0;
		ret->i_blocks = 0;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
	}
	return ret;
}

static struct dentry *xenfs_create_file(struct super_block *sb,
					struct dentry *parent,
					const char *name,
					const struct file_operations *fops,
					void *data,
					int mode)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(parent, name);
	if (!dentry)
		return NULL;

	inode = xenfs_make_inode(sb, S_IFREG | mode);
	if (!inode) {
		dput(dentry);
		return NULL;
	}

	inode->i_fop = fops;
	inode->i_private = data;

	d_add(dentry, inode);
	return dentry;
}

static ssize_t capabilities_read(struct file *file, char __user *buf,
				 size_t size, loff_t *off)
{
	char *tmp = "";

	if (xen_initial_domain())
		tmp = "control_d\n";

	return simple_read_from_buffer(buf, size, off, tmp, strlen(tmp));
}

static const struct file_operations capabilities_file_ops = {
	.read = capabilities_read,
	.llseek = default_llseek,
};

static int xenfs_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr xenfs_files[] = {
		[1] = {},
		{ "xenbus", &xenbus_file_ops, S_IRUSR|S_IWUSR },
		{ "capabilities", &capabilities_file_ops, S_IRUGO },
		{ "privcmd", &privcmd_file_ops, S_IRUSR|S_IWUSR },
		{""},
	};
	int rc;

	rc = simple_fill_super(sb, XENFS_SUPER_MAGIC, xenfs_files);
	if (rc < 0)
		return rc;

	if (xen_initial_domain()) {
		xenfs_create_file(sb, sb->s_root, "xsd_kva",
				  &xsd_kva_file_ops, NULL, S_IRUSR|S_IWUSR);
		xenfs_create_file(sb, sb->s_root, "xsd_port",
				  &xsd_port_file_ops, NULL, S_IRUSR|S_IWUSR);
	}

	return rc;
}

static struct dentry *xenfs_mount(struct file_system_type *fs_type,
				  int flags, const char *dev_name,
				  void *data)
{
	return mount_single(fs_type, flags, data, xenfs_fill_super);
}

static struct file_system_type xenfs_type = {
	.owner =	THIS_MODULE,
	.name =		"xenfs",
	.mount =	xenfs_mount,
	.kill_sb =	kill_litter_super,
};

static int __init xenfs_init(void)
{
	if (xen_domain())
		return register_filesystem(&xenfs_type);

	printk(KERN_INFO "XENFS: not registering filesystem on non-xen platform\n");
	return 0;
}

static void __exit xenfs_exit(void)
{
	if (xen_domain())
		unregister_filesystem(&xenfs_type);
}

module_init(xenfs_init);
module_exit(xenfs_exit);

