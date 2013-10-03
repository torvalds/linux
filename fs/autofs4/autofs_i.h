/* -*- c -*- ------------------------------------------------------------- *
 *   
 * linux/fs/autofs/autofs_i.h
 *
 *   Copyright 1997-1998 Transmeta Corporation - All Rights Reserved
 *   Copyright 2005-2006 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/* Internal header file for autofs */

#include <linux/auto_fs4.h>
#include <linux/auto_dev-ioctl.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>

/* This is the range of ioctl() numbers we claim as ours */
#define AUTOFS_IOC_FIRST     AUTOFS_IOC_READY
#define AUTOFS_IOC_COUNT     32

#define AUTOFS_DEV_IOCTL_IOC_FIRST	(AUTOFS_DEV_IOCTL_VERSION)
#define AUTOFS_DEV_IOCTL_IOC_COUNT	(AUTOFS_IOC_COUNT - 11)

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <asm/current.h>
#include <asm/uaccess.h>

/* #define DEBUG */

#define DPRINTK(fmt, ...)				\
	pr_debug("pid %d: %s: " fmt "\n",		\
		current->pid, __func__, ##__VA_ARGS__)

#define AUTOFS_WARN(fmt, ...)				\
	printk(KERN_WARNING "pid %d: %s: " fmt "\n",	\
		current->pid, __func__, ##__VA_ARGS__)

#define AUTOFS_ERROR(fmt, ...)				\
	printk(KERN_ERR "pid %d: %s: " fmt "\n",	\
		current->pid, __func__, ##__VA_ARGS__)

/* Unified info structure.  This is pointed to by both the dentry and
   inode structures.  Each file in the filesystem has an instance of this
   structure.  It holds a reference to the dentry, so dentries are never
   flushed while the file exists.  All name lookups are dealt with at the
   dentry level, although the filesystem can interfere in the validation
   process.  Readdir is implemented by traversing the dentry lists. */
struct autofs_info {
	struct dentry	*dentry;
	struct inode	*inode;

	int		flags;

	struct completion expire_complete;

	struct list_head active;
	int active_count;

	struct list_head expiring;

	struct autofs_sb_info *sbi;
	unsigned long last_used;
	atomic_t count;

	kuid_t uid;
	kgid_t gid;
};

#define AUTOFS_INF_EXPIRING	(1<<0) /* dentry is in the process of expiring */
#define AUTOFS_INF_PENDING	(1<<2) /* dentry pending mount */

struct autofs_wait_queue {
	wait_queue_head_t queue;
	struct autofs_wait_queue *next;
	autofs_wqt_t wait_queue_token;
	/* We use the following to see what we are waiting for */
	struct qstr name;
	u32 dev;
	u64 ino;
	kuid_t uid;
	kgid_t gid;
	pid_t pid;
	pid_t tgid;
	/* This is for status reporting upon return */
	int status;
	unsigned int wait_ctr;
};

#define AUTOFS_SBI_MAGIC 0x6d4a556d

struct autofs_sb_info {
	u32 magic;
	int pipefd;
	struct file *pipe;
	pid_t oz_pgrp;
	int catatonic;
	int version;
	int sub_version;
	int min_proto;
	int max_proto;
	unsigned long exp_timeout;
	unsigned int type;
	int reghost_enabled;
	int needs_reghost;
	struct super_block *sb;
	struct mutex wq_mutex;
	struct mutex pipe_mutex;
	spinlock_t fs_lock;
	struct autofs_wait_queue *queues; /* Wait queue pointer */
	spinlock_t lookup_lock;
	struct list_head active_list;
	struct list_head expiring_list;
	struct rcu_head rcu;
};

static inline struct autofs_sb_info *autofs4_sbi(struct super_block *sb)
{
	return (struct autofs_sb_info *)(sb->s_fs_info);
}

static inline struct autofs_info *autofs4_dentry_ino(struct dentry *dentry)
{
	return (struct autofs_info *)(dentry->d_fsdata);
}

/* autofs4_oz_mode(): do we see the man behind the curtain?  (The
   processes which do manipulations for us in user space sees the raw
   filesystem without "magic".) */

static inline int autofs4_oz_mode(struct autofs_sb_info *sbi) {
	return sbi->catatonic || task_pgrp_nr(current) == sbi->oz_pgrp;
}

/* Does a dentry have some pending activity? */
static inline int autofs4_ispending(struct dentry *dentry)
{
	struct autofs_info *inf = autofs4_dentry_ino(dentry);

	if (inf->flags & AUTOFS_INF_PENDING)
		return 1;

	if (inf->flags & AUTOFS_INF_EXPIRING)
		return 1;

	return 0;
}

struct inode *autofs4_get_inode(struct super_block *, umode_t);
void autofs4_free_ino(struct autofs_info *);

/* Expiration */
int is_autofs4_dentry(struct dentry *);
int autofs4_expire_wait(struct dentry *dentry);
int autofs4_expire_run(struct super_block *, struct vfsmount *,
			struct autofs_sb_info *,
			struct autofs_packet_expire __user *);
int autofs4_do_expire_multi(struct super_block *sb, struct vfsmount *mnt,
			    struct autofs_sb_info *sbi, int when);
int autofs4_expire_multi(struct super_block *, struct vfsmount *,
			struct autofs_sb_info *, int __user *);
struct dentry *autofs4_expire_direct(struct super_block *sb,
				     struct vfsmount *mnt,
				     struct autofs_sb_info *sbi, int how);
struct dentry *autofs4_expire_indirect(struct super_block *sb,
				       struct vfsmount *mnt,
				       struct autofs_sb_info *sbi, int how);

/* Device node initialization */

int autofs_dev_ioctl_init(void);
void autofs_dev_ioctl_exit(void);

/* Operations structures */

extern const struct inode_operations autofs4_symlink_inode_operations;
extern const struct inode_operations autofs4_dir_inode_operations;
extern const struct file_operations autofs4_dir_operations;
extern const struct file_operations autofs4_root_operations;
extern const struct dentry_operations autofs4_dentry_operations;

/* VFS automount flags management functions */

static inline void __managed_dentry_set_automount(struct dentry *dentry)
{
	dentry->d_flags |= DCACHE_NEED_AUTOMOUNT;
}

static inline void managed_dentry_set_automount(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__managed_dentry_set_automount(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void __managed_dentry_clear_automount(struct dentry *dentry)
{
	dentry->d_flags &= ~DCACHE_NEED_AUTOMOUNT;
}

static inline void managed_dentry_clear_automount(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__managed_dentry_clear_automount(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void __managed_dentry_set_transit(struct dentry *dentry)
{
	dentry->d_flags |= DCACHE_MANAGE_TRANSIT;
}

static inline void managed_dentry_set_transit(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__managed_dentry_set_transit(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void __managed_dentry_clear_transit(struct dentry *dentry)
{
	dentry->d_flags &= ~DCACHE_MANAGE_TRANSIT;
}

static inline void managed_dentry_clear_transit(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__managed_dentry_clear_transit(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void __managed_dentry_set_managed(struct dentry *dentry)
{
	dentry->d_flags |= (DCACHE_NEED_AUTOMOUNT|DCACHE_MANAGE_TRANSIT);
}

static inline void managed_dentry_set_managed(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__managed_dentry_set_managed(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void __managed_dentry_clear_managed(struct dentry *dentry)
{
	dentry->d_flags &= ~(DCACHE_NEED_AUTOMOUNT|DCACHE_MANAGE_TRANSIT);
}

static inline void managed_dentry_clear_managed(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__managed_dentry_clear_managed(dentry);
	spin_unlock(&dentry->d_lock);
}

/* Initializing function */

int autofs4_fill_super(struct super_block *, void *, int);
struct autofs_info *autofs4_new_ino(struct autofs_sb_info *);
void autofs4_clean_ino(struct autofs_info *);

static inline int autofs_prepare_pipe(struct file *pipe)
{
	if (!pipe->f_op->write)
		return -EINVAL;
	if (!S_ISFIFO(file_inode(pipe)->i_mode))
		return -EINVAL;
	/* We want a packet pipe */
	pipe->f_flags |= O_DIRECT;
	return 0;
}

/* Queue management functions */

int autofs4_wait(struct autofs_sb_info *,struct dentry *, enum autofs_notify);
int autofs4_wait_release(struct autofs_sb_info *,autofs_wqt_t,int);
void autofs4_catatonic_mode(struct autofs_sb_info *);

static inline u32 autofs4_get_dev(struct autofs_sb_info *sbi)
{
	return new_encode_dev(sbi->sb->s_dev);
}

static inline u64 autofs4_get_ino(struct autofs_sb_info *sbi)
{
	return sbi->sb->s_root->d_inode->i_ino;
}

static inline int simple_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static inline void __autofs4_add_expiring(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		if (list_empty(&ino->expiring))
			list_add(&ino->expiring, &sbi->expiring_list);
	}
	return;
}

static inline void autofs4_add_expiring(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		spin_lock(&sbi->lookup_lock);
		if (list_empty(&ino->expiring))
			list_add(&ino->expiring, &sbi->expiring_list);
		spin_unlock(&sbi->lookup_lock);
	}
	return;
}

static inline void autofs4_del_expiring(struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	if (ino) {
		spin_lock(&sbi->lookup_lock);
		if (!list_empty(&ino->expiring))
			list_del_init(&ino->expiring);
		spin_unlock(&sbi->lookup_lock);
	}
	return;
}

extern void autofs4_kill_sb(struct super_block *);
