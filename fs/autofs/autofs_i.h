/* -*- linux-c -*- ------------------------------------------------------- *
 *   
 * linux/fs/autofs/autofs_i.h
 *
 *   Copyright 1997-1998 Transmeta Corporation - All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/* Internal header file for autofs */

#include <linux/auto_fs.h>

/* This is the range of ioctl() numbers we claim as ours */
#define AUTOFS_IOC_FIRST     AUTOFS_IOC_READY
#define AUTOFS_IOC_COUNT     32

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/sched.h>

#include <asm/current.h>
#include <asm/uaccess.h>

#ifdef DEBUG
#define DPRINTK(D) (printk D)
#else
#define DPRINTK(D) ((void)0)
#endif

/*
 * If the daemon returns a negative response (AUTOFS_IOC_FAIL) then the
 * kernel will keep the negative response cached for up to the time given
 * here, although the time can be shorter if the kernel throws the dcache
 * entry away.  This probably should be settable from user space.
 */
#define AUTOFS_NEGATIVE_TIMEOUT (60*HZ)	/* 1 minute */

/* Structures associated with the root directory hash table */

#define AUTOFS_HASH_SIZE 67

struct autofs_dir_ent {
	int hash;
	char *name;
	int len;
	ino_t ino;
	struct dentry *dentry;
	/* Linked list of entries */
	struct autofs_dir_ent *next;
	struct autofs_dir_ent **back;
	/* The following entries are for the expiry system */
	unsigned long last_usage;
	struct list_head exp;
};

struct autofs_dirhash {
	struct autofs_dir_ent *h[AUTOFS_HASH_SIZE];
	struct list_head expiry_head;
};

struct autofs_wait_queue {
	wait_queue_head_t queue;
	struct autofs_wait_queue *next;
	autofs_wqt_t wait_queue_token;
	/* We use the following to see what we are waiting for */
	int hash;
	int len;
	char *name;
	/* This is for status reporting upon return */
	int status;
	int wait_ctr;
};

struct autofs_symlink {
	char *data;
	int len;
	time_t mtime;
};

#define AUTOFS_MAX_SYMLINKS 256

#define AUTOFS_ROOT_INO      1
#define AUTOFS_FIRST_SYMLINK 2
#define AUTOFS_FIRST_DIR_INO (AUTOFS_FIRST_SYMLINK+AUTOFS_MAX_SYMLINKS)

#define AUTOFS_SYMLINK_BITMAP_LEN \
	((AUTOFS_MAX_SYMLINKS+((sizeof(long)*1)-1))/(sizeof(long)*8))

#define AUTOFS_SBI_MAGIC 0x6d4a556d

struct autofs_sb_info {
	u32 magic;
	struct file *pipe;
	struct pid *oz_pgrp;
	int catatonic;
	struct super_block *sb;
	unsigned long exp_timeout;
	ino_t next_dir_ino;
	struct autofs_wait_queue *queues; /* Wait queue pointer */
	struct autofs_dirhash dirhash; /* Root directory hash */
	struct autofs_symlink symlink[AUTOFS_MAX_SYMLINKS];
	unsigned long symlink_bitmap[AUTOFS_SYMLINK_BITMAP_LEN];
};

static inline struct autofs_sb_info *autofs_sbi(struct super_block *sb)
{
	return (struct autofs_sb_info *)(sb->s_fs_info);
}

/* autofs_oz_mode(): do we see the man behind the curtain?  (The
   processes which do manipulations for us in user space sees the raw
   filesystem without "magic".) */

static inline int autofs_oz_mode(struct autofs_sb_info *sbi) {
	return sbi->catatonic || task_pgrp(current) == sbi->oz_pgrp;
}

/* Hash operations */

void autofs_initialize_hash(struct autofs_dirhash *);
struct autofs_dir_ent *autofs_hash_lookup(const struct autofs_dirhash *,struct qstr *);
void autofs_hash_insert(struct autofs_dirhash *,struct autofs_dir_ent *);
void autofs_hash_delete(struct autofs_dir_ent *);
struct autofs_dir_ent *autofs_hash_enum(const struct autofs_dirhash *,off_t *,struct autofs_dir_ent *);
void autofs_hash_dputall(struct autofs_dirhash *);
void autofs_hash_nuke(struct autofs_sb_info *);

/* Expiration-handling functions */

void autofs_update_usage(struct autofs_dirhash *,struct autofs_dir_ent *);
struct autofs_dir_ent *autofs_expire(struct super_block *,struct autofs_sb_info *, struct vfsmount *mnt);

/* Operations structures */

extern const struct inode_operations autofs_root_inode_operations;
extern const struct inode_operations autofs_symlink_inode_operations;
extern const struct file_operations autofs_root_operations;

/* Initializing function */

int autofs_fill_super(struct super_block *, void *, int);
void autofs_kill_sb(struct super_block *sb);
struct inode *autofs_iget(struct super_block *, unsigned long);

/* Queue management functions */

int autofs_wait(struct autofs_sb_info *,struct qstr *);
int autofs_wait_release(struct autofs_sb_info *,autofs_wqt_t,int);
void autofs_catatonic_mode(struct autofs_sb_info *);

#ifdef DEBUG
void autofs_say(const char *name, int len);
#else
#define autofs_say(n,l) ((void)0)
#endif
