/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_BINDER_INTERNAL_H
#define _LINUX_BINDER_INTERNAL_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/uidgid.h>

struct binder_context {
	struct binder_node *binder_context_mgr_node;
	struct mutex context_mgr_node_lock;
	kuid_t binder_context_mgr_uid;
	const char *name;
};

/**
 * struct binder_device - information about a binder device node
 * @hlist:          list of binder devices (only used for devices requested via
 *                  CONFIG_ANDROID_BINDER_DEVICES)
 * @miscdev:        information about a binder character device node
 * @context:        binder context information
 * @binderfs_inode: This is the inode of the root dentry of the super block
 *                  belonging to a binderfs mount.
 */
struct binder_device {
	struct hlist_node hlist;
	struct miscdevice miscdev;
	struct binder_context context;
	struct inode *binderfs_inode;
};

extern const struct file_operations binder_fops;

#if IS_ENABLED(CONFIG_ANDROID_BINDERFS)
extern bool is_binderfs_device(const struct inode *inode);
#else
static inline bool is_binderfs_device(const struct inode *inode)
{
	return false;
}
#endif

#if IS_ENABLED(CONFIG_ANDROID_BINDERFS)
extern int __init init_binderfs(void);
#else
static inline int __init init_binderfs(void)
{
	return 0;
}
#endif

#endif /* _LINUX_BINDER_INTERNAL_H */
