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
};

static int xenfs_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr xenfs_files[] = {
		[1] = {},
		{ "xenbus", &xenbus_file_ops, S_IRUSR|S_IWUSR },
		{ "capabilities", &capabilities_file_ops, S_IRUGO },
		{""},
	};

	return simple_fill_super(sb, XENFS_SUPER_MAGIC, xenfs_files);
}

static int xenfs_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, xenfs_fill_super, mnt);
}

static struct file_system_type xenfs_type = {
	.owner =	THIS_MODULE,
	.name =		"xenfs",
	.get_sb =	xenfs_get_sb,
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

