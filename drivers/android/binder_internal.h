/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_BINDER_INTERNAL_H
#define _LINUX_BINDER_INTERNAL_H

#include <linux/export.h>
#include <linux/fs.h>
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

extern char *binder_devices_param;

#ifdef CONFIG_ANDROID_BINDERFS
extern bool is_binderfs_device(const struct inode *inode);
#else
static inline bool is_binderfs_device(const struct inode *inode)
{
	return false;
}
#endif

#ifdef CONFIG_ANDROID_BINDERFS
extern int __init init_binderfs(void);
#else
static inline int __init init_binderfs(void)
{
	return 0;
}
#endif

int binder_stats_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_stats);

int binder_state_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_state);

int binder_transactions_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_transactions);

int binder_transaction_log_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(binder_transaction_log);

struct binder_transaction_log_entry {
	int debug_id;
	int debug_id_done;
	int call_type;
	int from_proc;
	int from_thread;
	int target_handle;
	int to_proc;
	int to_thread;
	int to_node;
	int data_size;
	int offsets_size;
	int return_error_line;
	uint32_t return_error;
	uint32_t return_error_param;
	const char *context_name;
};

struct binder_transaction_log {
	atomic_t cur;
	bool full;
	struct binder_transaction_log_entry entry[32];
};

extern struct binder_transaction_log binder_transaction_log;
extern struct binder_transaction_log binder_transaction_log_failed;
#endif /* _LINUX_BINDER_INTERNAL_H */
