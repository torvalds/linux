/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __NETFS_H
#define __NETFS_H

#include <linux/types.h>
#include <linux/connector.h>
#include <linux/backing-dev.h>

#define POHMELFS_CN_IDX			5
#define POHMELFS_CN_VAL			0

#define POHMELFS_CTLINFO_ACK		1
#define POHMELFS_NOINFO_ACK		2

#define POHMELFS_NULL_IDX		65535

/*
 * Network command structure.
 * Will be extended.
 */
struct netfs_cmd {
	__u16			cmd;	/* Command number */
	__u16			csize;	/* Attached crypto information size */
	__u16			cpad;	/* Attached padding size */
	__u16			ext;	/* External flags */
	__u32			size;	/* Size of the attached data */
	__u32			trans;	/* Transaction id */
	__u64			id;	/* Object ID to operate on. Used for feedback.*/
	__u64			start;	/* Start of the object. */
	__u64			iv;	/* IV sequence */
	__u8			data[0];
};

static inline void netfs_convert_cmd(struct netfs_cmd *cmd)
{
	cmd->id = __be64_to_cpu(cmd->id);
	cmd->start = __be64_to_cpu(cmd->start);
	cmd->iv = __be64_to_cpu(cmd->iv);
	cmd->cmd = __be16_to_cpu(cmd->cmd);
	cmd->ext = __be16_to_cpu(cmd->ext);
	cmd->csize = __be16_to_cpu(cmd->csize);
	cmd->cpad = __be16_to_cpu(cmd->cpad);
	cmd->size = __be32_to_cpu(cmd->size);
}

#define NETFS_TRANS_SINGLE_DST		(1<<0)

enum {
	NETFS_READDIR	= 1,	/* Read directory for given inode number */
	NETFS_READ_PAGE,	/* Read data page from the server */
	NETFS_WRITE_PAGE,	/* Write data page to the server */
	NETFS_CREATE,		/* Create directory entry */
	NETFS_REMOVE,		/* Remove directory entry */

	NETFS_LOOKUP,		/* Lookup single object */
	NETFS_LINK,		/* Create a link */
	NETFS_TRANS,		/* Transaction */
	NETFS_OPEN,		/* Open intent */
	NETFS_INODE_INFO,	/* Metadata cache coherency synchronization message */

	NETFS_PAGE_CACHE,	/* Page cache invalidation message */
	NETFS_READ_PAGES,	/* Read multiple contiguous pages in one go */
	NETFS_RENAME,		/* Rename object */
	NETFS_CAPABILITIES,	/* Capabilities of the client, for example supported crypto */
	NETFS_LOCK,		/* Distributed lock message */

	NETFS_XATTR_SET,	/* Set extended attribute */
	NETFS_XATTR_GET,	/* Get extended attribute */
	NETFS_CMD_MAX
};

enum {
	POHMELFS_FLAGS_ADD = 0, /* Network state control message for ADD */
	POHMELFS_FLAGS_DEL,     /* Network state control message for DEL */
	POHMELFS_FLAGS_SHOW,    /* Network state control message for SHOW */
	POHMELFS_FLAGS_CRYPTO,	/* Crypto data control message */
	POHMELFS_FLAGS_MODIFY,	/* Network state modification message */
	POHMELFS_FLAGS_DUMP,	/* Network state control message for SHOW ALL */
	POHMELFS_FLAGS_FLUSH,	/* Network state control message for FLUSH */
};

/*
 * Always wanted to copy it from socket headers into public one,
 * since they are __KERNEL__ protected there.
 */
#define _K_SS_MAXSIZE	128

struct saddr {
	unsigned short		sa_family;
	char			addr[_K_SS_MAXSIZE];
};

enum {
	POHMELFS_CRYPTO_HASH = 0,
	POHMELFS_CRYPTO_CIPHER,
};

struct pohmelfs_crypto {
	unsigned int		idx;		/* Config index */
	unsigned short		strlen;		/* Size of the attached crypto string including 0-byte
						 * "cbc(aes)" for example */
	unsigned short		type;		/* HMAC, cipher, both */
	unsigned int		keysize;	/* Key size */
	unsigned char		data[0];	/* Algorithm string, key and IV */
};

#define POHMELFS_IO_PERM_READ		(1<<0)
#define POHMELFS_IO_PERM_WRITE		(1<<1)

/*
 * Configuration command used to create table of different remote servers.
 */
struct pohmelfs_ctl {
	__u32			idx;		/* Config index */
	__u32			type;		/* Socket type */
	__u32			proto;		/* Socket protocol */
	__u16			addrlen;	/* Size of the address */
	__u16			perm;		/* IO permission */
	__u16			prio;		/* IO priority */
	struct saddr		addr;		/* Remote server address */
};

/*
 * Ack for userspace about requested command.
 */
struct pohmelfs_cn_ack {
	struct cn_msg		msg;
	int			error;
	int			msg_num;
	int			unused[3];
	struct pohmelfs_ctl	ctl;
};

/*
 * Inode info structure used to sync with server.
 * Check what stat() returns.
 */
struct netfs_inode_info {
	unsigned int		mode;
	unsigned int		nlink;
	unsigned int		uid;
	unsigned int		gid;
	unsigned int		blocksize;
	unsigned int		padding;
	__u64			ino;
	__u64			blocks;
	__u64			rdev;
	__u64			size;
	__u64			version;
};

static inline void netfs_convert_inode_info(struct netfs_inode_info *info)
{
	info->mode = __cpu_to_be32(info->mode);
	info->nlink = __cpu_to_be32(info->nlink);
	info->uid = __cpu_to_be32(info->uid);
	info->gid = __cpu_to_be32(info->gid);
	info->blocksize = __cpu_to_be32(info->blocksize);
	info->blocks = __cpu_to_be64(info->blocks);
	info->rdev = __cpu_to_be64(info->rdev);
	info->size = __cpu_to_be64(info->size);
	info->version = __cpu_to_be64(info->version);
	info->ino = __cpu_to_be64(info->ino);
}

/*
 * Cache state machine.
 */
enum {
	NETFS_COMMAND_PENDING = 0,	/* Command is being executed */
	NETFS_INODE_REMOTE_SYNCED,	/* Inode was synced to server */
	NETFS_INODE_REMOTE_DIR_SYNCED,	/* Inode (directory) was synced from the server */
	NETFS_INODE_OWNED,		/* Inode is owned by given host */
	NETFS_INODE_NEED_FLUSH,		/* Inode has to be flushed to the server */
};

/*
 * POHMELFS capabilities: information about supported
 * crypto operations (hash/cipher, modes, key sizes and so on),
 * root information (used/available size, number of objects, permissions)
 */
enum pohmelfs_capabilities {
	POHMELFS_CRYPTO_CAPABILITIES = 0,
	POHMELFS_ROOT_CAPABILITIES,
};

/* Read-only mount */
#define POHMELFS_FLAGS_RO		(1<<0)
/* Extended attributes support on/off */
#define POHMELFS_FLAGS_XATTR		(1<<1)

struct netfs_root_capabilities {
	__u64			nr_files;
	__u64			used, avail;
	__u64			flags;
};

static inline void netfs_convert_root_capabilities(struct netfs_root_capabilities *cap)
{
	cap->nr_files = __cpu_to_be64(cap->nr_files);
	cap->used = __cpu_to_be64(cap->used);
	cap->avail = __cpu_to_be64(cap->avail);
	cap->flags = __cpu_to_be64(cap->flags);
}

struct netfs_crypto_capabilities {
	unsigned short		hash_strlen;	/* Hash string length, like "hmac(sha1) including 0 byte "*/
	unsigned short		cipher_strlen;	/* Cipher string length with the same format */
	unsigned int		cipher_keysize;	/* Cipher key size */
};

static inline void netfs_convert_crypto_capabilities(struct netfs_crypto_capabilities *cap)
{
	cap->hash_strlen = __cpu_to_be16(cap->hash_strlen);
	cap->cipher_strlen = __cpu_to_be16(cap->cipher_strlen);
	cap->cipher_keysize = __cpu_to_be32(cap->cipher_keysize);
}

enum pohmelfs_lock_type {
	POHMELFS_LOCK_GRAB	= (1<<15),

	POHMELFS_READ_LOCK	= 0,
	POHMELFS_WRITE_LOCK,
};

struct netfs_lock {
	__u64			start;
	__u64			ino;
	__u32			size;
	__u32			type;
};

static inline void netfs_convert_lock(struct netfs_lock *lock)
{
	lock->start = __cpu_to_be64(lock->start);
	lock->ino = __cpu_to_be64(lock->ino);
	lock->size = __cpu_to_be32(lock->size);
	lock->type = __cpu_to_be32(lock->type);
}

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/rbtree.h>
#include <linux/net.h>
#include <linux/poll.h>

/*
 * Private POHMELFS cache of objects in directory.
 */
struct pohmelfs_name {
	struct rb_node		hash_node;

	struct list_head	sync_create_entry;

	u64			ino;

	u32			hash;
	u32			mode;
	u32			len;

	char			*data;
};

/*
 * POHMELFS inode. Main object.
 */
struct pohmelfs_inode {
	struct list_head	inode_entry;		/* Entry in superblock list.
							 * Objects which are not bound to dentry require to be dropped
							 * in ->put_super()
							 */
	struct rb_root		hash_root;		/* The same, but indexed by name hash and len */
	struct mutex		offset_lock;		/* Protect both above trees */

	struct list_head	sync_create_list;	/* List of created but not yet synced to the server children */

	unsigned int		drop_count;

	int			lock_type;		/* How this inode is locked: read or write */

	int			error;			/* Transaction error for given inode */

	long			state;			/* State machine above */

	u64			ino;			/* Inode number */
	u64			total_len;		/* Total length of all children names, used to create offsets */

	struct inode		vfs_inode;
};

struct netfs_trans;
typedef int (*netfs_trans_complete_t)(struct page **pages, unsigned int page_num,
		void *private, int err);

struct netfs_state;
struct pohmelfs_sb;

struct netfs_trans {
	/*
	 * Transaction header and attached contiguous data live here.
	 */
	struct iovec			iovec;

	/*
	 * Pages attached to transaction.
	 */
	struct page			**pages;

	/*
	 * List and protecting lock for transaction destination
	 * network states.
	 */
	spinlock_t			dst_lock;
	struct list_head		dst_list;

	/*
	 * Number of users for given transaction.
	 * For example each network state attached to transaction
	 * via dst_list increases it.
	 */
	atomic_t			refcnt;

	/*
	 * Number of pages attached to given transaction.
	 * Some slots in above page array can be NULL, since
	 * for example page can be under writeback already,
	 * so we skip it in this transaction.
	 */
	unsigned int			page_num;

	/*
	 * Transaction flags: single dst or broadcast and so on.
	 */
	unsigned int			flags;

	/*
	 * Size of the data, which can be placed into
	 * iovec.iov_base area.
	 */
	unsigned int			total_size;

	/*
	 * Number of pages to be sent to remote server.
	 * Usually equal to above page_num, but in case of partial
	 * writeback it can accumulate only pages already completed
	 * previous writeback.
	 */
	unsigned int			attached_pages;

	/*
	 * Attached number of bytes in all above pages.
	 */
	unsigned int			attached_size;

	/*
	 * Unique transacton generation number.
	 * Used as identity in the network state tree of transactions.
	 */
	unsigned int			gen;

	/*
	 * Transaction completion status.
	 */
	int				result;

	/*
	 * Superblock this transaction belongs to
	 */
	struct pohmelfs_sb		*psb;

	/*
	 * Crypto engine, which processed this transaction.
	 * Can be not NULL only if crypto engine holds encrypted pages.
	 */
	struct pohmelfs_crypto_engine	*eng;

	/* Private data */
	void				*private;

	/* Completion callback, invoked just before transaction is destroyed */
	netfs_trans_complete_t		complete;
};

static inline int netfs_trans_cur_len(struct netfs_trans *t)
{
	return (signed)(t->total_size - t->iovec.iov_len);
}

static inline void *netfs_trans_current(struct netfs_trans *t)
{
	return t->iovec.iov_base + t->iovec.iov_len;
}

struct netfs_trans *netfs_trans_alloc(struct pohmelfs_sb *psb, unsigned int size,
		unsigned int flags, unsigned int nr);
void netfs_trans_free(struct netfs_trans *t);
int netfs_trans_finish(struct netfs_trans *t, struct pohmelfs_sb *psb);
int netfs_trans_finish_send(struct netfs_trans *t, struct pohmelfs_sb *psb);

static inline void netfs_trans_reset(struct netfs_trans *t)
{
	t->complete = NULL;
}

struct netfs_trans_dst {
	struct list_head		trans_entry;
	struct rb_node			state_entry;

	unsigned long			send_time;

	/*
	 * Times this transaction was resent to its old or new,
	 * depending on flags, destinations. When it reaches maximum
	 * allowed number, specified in superblock->trans_retries,
	 * transaction will be freed with ETIMEDOUT error.
	 */
	unsigned int			retries;

	struct netfs_trans		*trans;
	struct netfs_state		*state;
};

struct netfs_trans_dst *netfs_trans_search(struct netfs_state *st, unsigned int gen);
void netfs_trans_drop_dst(struct netfs_trans_dst *dst);
void netfs_trans_drop_dst_nostate(struct netfs_trans_dst *dst);
void netfs_trans_drop_trans(struct netfs_trans *t, struct netfs_state *st);
void netfs_trans_drop_last(struct netfs_trans *t, struct netfs_state *st);
int netfs_trans_resend(struct netfs_trans *t, struct pohmelfs_sb *psb);
int netfs_trans_remove_nolock(struct netfs_trans_dst *dst, struct netfs_state *st);

int netfs_trans_init(void);
void netfs_trans_exit(void);

struct pohmelfs_crypto_engine {
	u64				iv;		/* Crypto IV for current operation */
	unsigned long			timeout;	/* Crypto waiting timeout */
	unsigned int			size;		/* Size of crypto scratchpad */
	void				*data;		/* Temporal crypto scratchpad */
	/*
	 * Crypto operations performed on objects.
	 */
	struct crypto_hash		*hash;
	struct crypto_ablkcipher	*cipher;

	struct pohmelfs_crypto_thread	*thread;	/* Crypto thread which hosts this engine */

	struct page			**pages;
	unsigned int			page_num;
};

struct pohmelfs_crypto_thread {
	struct list_head		thread_entry;

	struct task_struct		*thread;
	struct pohmelfs_sb		*psb;

	struct pohmelfs_crypto_engine	eng;

	struct netfs_trans		*trans;

	wait_queue_head_t		wait;
	int				error;

	unsigned int			size;
	struct page			*page;
};

void pohmelfs_crypto_thread_make_ready(struct pohmelfs_crypto_thread *th);

/*
 * Network state, attached to one server.
 */
struct netfs_state {
	struct mutex		__state_lock;		/* Can not allow to use the same socket simultaneously */
	struct mutex		__state_send_lock;
	struct netfs_cmd	cmd;			/* Cached command */
	struct netfs_inode_info	info;			/* Cached inode info */

	void			*data;			/* Cached some data */
	unsigned int		size;			/* Size of that data */

	struct pohmelfs_sb	*psb;			/* Superblock */

	struct task_struct	*thread;		/* Async receiving thread */

	/* Waiting/polling machinery */
	wait_queue_t		wait;
	wait_queue_head_t	*whead;
	wait_queue_head_t	thread_wait;

	struct mutex		trans_lock;
	struct rb_root		trans_root;

	struct pohmelfs_ctl	ctl;			/* Remote peer */

	struct socket		*socket;		/* Socket object */
	struct socket		*read_socket;		/* Cached pointer to socket object.
							 * Used to determine if between lock drops socket was changed.
							 * Never used to read data or any kind of access.
							 */
	/*
	 * Crypto engines to process incoming data.
	 */
	struct pohmelfs_crypto_engine	eng;

	int			need_reset;
};

int netfs_state_init(struct netfs_state *st);
void netfs_state_exit(struct netfs_state *st);

static inline void netfs_state_lock_send(struct netfs_state *st)
{
	mutex_lock(&st->__state_send_lock);
}

static inline int netfs_state_trylock_send(struct netfs_state *st)
{
	return mutex_trylock(&st->__state_send_lock);
}

static inline void netfs_state_unlock_send(struct netfs_state *st)
{
	BUG_ON(!mutex_is_locked(&st->__state_send_lock));

	mutex_unlock(&st->__state_send_lock);
}

static inline void netfs_state_lock(struct netfs_state *st)
{
	mutex_lock(&st->__state_lock);
}

static inline void netfs_state_unlock(struct netfs_state *st)
{
	BUG_ON(!mutex_is_locked(&st->__state_lock));

	mutex_unlock(&st->__state_lock);
}

static inline unsigned int netfs_state_poll(struct netfs_state *st)
{
	unsigned int revents = POLLHUP | POLLERR;

	netfs_state_lock(st);
	if (st->socket)
		revents = st->socket->ops->poll(NULL, st->socket, NULL);
	netfs_state_unlock(st);

	return revents;
}

struct pohmelfs_config;

struct pohmelfs_sb {
	struct rb_root		mcache_root;
	struct mutex		mcache_lock;
	atomic_long_t		mcache_gen;
	unsigned long		mcache_timeout;

	unsigned int		idx;

	unsigned int		trans_retries;

	atomic_t		trans_gen;

	unsigned int		crypto_attached_size;
	unsigned int		crypto_align_size;

	unsigned int		crypto_fail_unsupported;

	unsigned int		crypto_thread_num;
	struct list_head	crypto_active_list, crypto_ready_list;
	struct mutex		crypto_thread_lock;

	unsigned int		trans_max_pages;
	unsigned long		trans_data_size;
	unsigned long		trans_timeout;

	unsigned long		drop_scan_timeout;
	unsigned long		trans_scan_timeout;

	unsigned long		wait_on_page_timeout;

	struct list_head	flush_list;
	struct list_head	drop_list;
	spinlock_t		ino_lock;
	u64			ino;

	/*
	 * Remote nodes POHMELFS connected to.
	 */
	struct list_head	state_list;
	struct mutex		state_lock;

	/*
	 * Currently active state to request data from.
	 */
	struct pohmelfs_config	*active_state;


	wait_queue_head_t	wait;

	/*
	 * Timed checks: stale transactions, inodes to be freed and so on.
	 */
	struct delayed_work	dwork;
	struct delayed_work	drop_dwork;

	struct super_block	*sb;

	struct backing_dev_info	bdi;

	/*
	 * Algorithm strings.
	 */
	char			*hash_string;
	char			*cipher_string;

	u8			*hash_key;
	u8			*cipher_key;

	/*
	 * Algorithm string lengths.
	 */
	unsigned int		hash_strlen;
	unsigned int		cipher_strlen;
	unsigned int		hash_keysize;
	unsigned int		cipher_keysize;

	/*
	 * Controls whether to perfrom crypto processing or not.
	 */
	int			perform_crypto;

	/*
	 * POHMELFS statistics.
	 */
	u64			total_size;
	u64			avail_size;
	atomic_long_t		total_inodes;

	/*
	 * Xattr support, read-only and so on.
	 */
	u64			state_flags;

	/*
	 * Temporary storage to detect changes in the wait queue.
	 */
	long			flags;
};

static inline void netfs_trans_update(struct netfs_cmd *cmd,
		struct netfs_trans *t, unsigned int size)
{
	unsigned int sz = ALIGN(size, t->psb->crypto_align_size);

	t->iovec.iov_len += sizeof(struct netfs_cmd) + sz;
	cmd->cpad = __cpu_to_be16(sz - size);
}

static inline struct pohmelfs_sb *POHMELFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct pohmelfs_inode *POHMELFS_I(struct inode *inode)
{
	return container_of(inode, struct pohmelfs_inode, vfs_inode);
}

static inline u64 pohmelfs_new_ino(struct pohmelfs_sb *psb)
{
	u64 ino;

	spin_lock(&psb->ino_lock);
	ino = psb->ino++;
	spin_unlock(&psb->ino_lock);

	return ino;
}

static inline void pohmelfs_put_inode(struct pohmelfs_inode *pi)
{
	struct pohmelfs_sb *psb = POHMELFS_SB(pi->vfs_inode.i_sb);

	spin_lock(&psb->ino_lock);
	list_move_tail(&pi->inode_entry, &psb->drop_list);
	pi->drop_count++;
	spin_unlock(&psb->ino_lock);
}

struct pohmelfs_config {
	struct list_head	config_entry;

	struct netfs_state	state;
};

struct pohmelfs_config_group {
	/*
	 * Entry in the global config group list.
	 */
	struct list_head	group_entry;

	/*
	 * Index of the current group.
	 */
	unsigned int		idx;
	/*
	 * Number of config_list entries in this group entry.
	 */
	unsigned int		num_entry;
	/*
	 * Algorithm strings.
	 */
	char			*hash_string;
	char			*cipher_string;

	/*
	 * Algorithm string lengths.
	 */
	unsigned int		hash_strlen;
	unsigned int		cipher_strlen;

	/*
	 * Key and its size.
	 */
	unsigned int		hash_keysize;
	unsigned int		cipher_keysize;
	u8			*hash_key;
	u8			*cipher_key;

	/*
	 * List of config entries (network state info) for given idx.
	 */
	struct list_head	config_list;
};

int __init pohmelfs_config_init(void);
void pohmelfs_config_exit(void);
int pohmelfs_copy_config(struct pohmelfs_sb *psb);
int pohmelfs_copy_crypto(struct pohmelfs_sb *psb);
int pohmelfs_config_check(struct pohmelfs_config *config, int idx);
int pohmelfs_state_init_one(struct pohmelfs_sb *psb, struct pohmelfs_config *conf);

extern const struct file_operations pohmelfs_dir_fops;
extern const struct inode_operations pohmelfs_dir_inode_ops;

int pohmelfs_state_init(struct pohmelfs_sb *psb);
void pohmelfs_state_exit(struct pohmelfs_sb *psb);
void pohmelfs_state_flush_transactions(struct netfs_state *st);

void pohmelfs_fill_inode(struct inode *inode, struct netfs_inode_info *info);

void pohmelfs_name_del(struct pohmelfs_inode *parent, struct pohmelfs_name *n);
void pohmelfs_free_names(struct pohmelfs_inode *parent);
struct pohmelfs_name *pohmelfs_search_hash(struct pohmelfs_inode *pi, u32 hash);

void pohmelfs_inode_del_inode(struct pohmelfs_sb *psb, struct pohmelfs_inode *pi);

struct pohmelfs_inode *pohmelfs_create_entry_local(struct pohmelfs_sb *psb,
	struct pohmelfs_inode *parent, struct qstr *str, u64 start, umode_t mode);

int pohmelfs_write_create_inode(struct pohmelfs_inode *pi);

int pohmelfs_write_inode_create(struct inode *inode, struct netfs_trans *trans);
int pohmelfs_remove_child(struct pohmelfs_inode *parent, struct pohmelfs_name *n);

struct pohmelfs_inode *pohmelfs_new_inode(struct pohmelfs_sb *psb,
		struct pohmelfs_inode *parent, struct qstr *str,
		struct netfs_inode_info *info, int link);

int pohmelfs_setattr(struct dentry *dentry, struct iattr *attr);
int pohmelfs_setattr_raw(struct inode *inode, struct iattr *attr);

int pohmelfs_meta_command(struct pohmelfs_inode *pi, unsigned int cmd_op, unsigned int flags,
		netfs_trans_complete_t complete, void *priv, u64 start);
int pohmelfs_meta_command_data(struct pohmelfs_inode *pi, u64 id, unsigned int cmd_op, char *addon,
		unsigned int flags, netfs_trans_complete_t complete, void *priv, u64 start);

void pohmelfs_check_states(struct pohmelfs_sb *psb);
void pohmelfs_switch_active(struct pohmelfs_sb *psb);

int pohmelfs_construct_path_string(struct pohmelfs_inode *pi, void *data, int len);
int pohmelfs_path_length(struct pohmelfs_inode *pi);

struct pohmelfs_crypto_completion {
	struct completion	complete;
	int			error;
};

int pohmelfs_trans_crypt(struct netfs_trans *t, struct pohmelfs_sb *psb);
void pohmelfs_crypto_exit(struct pohmelfs_sb *psb);
int pohmelfs_crypto_init(struct pohmelfs_sb *psb);

int pohmelfs_crypto_engine_init(struct pohmelfs_crypto_engine *e, struct pohmelfs_sb *psb);
void pohmelfs_crypto_engine_exit(struct pohmelfs_crypto_engine *e);

int pohmelfs_crypto_process_input_data(struct pohmelfs_crypto_engine *e, u64 iv,
		void *data, struct page *page, unsigned int size);
int pohmelfs_crypto_process_input_page(struct pohmelfs_crypto_engine *e,
		struct page *page, unsigned int size, u64 iv);

static inline u64 pohmelfs_gen_iv(struct netfs_trans *t)
{
	u64 iv = t->gen;

	iv <<= 32;
	iv |= ((unsigned long)t) & 0xffffffff;

	return iv;
}

int pohmelfs_data_lock(struct pohmelfs_inode *pi, u64 start, u32 size, int type);
int pohmelfs_data_unlock(struct pohmelfs_inode *pi, u64 start, u32 size, int type);
int pohmelfs_data_lock_response(struct netfs_state *st);

static inline int pohmelfs_need_lock(struct pohmelfs_inode *pi, int type)
{
	if (test_bit(NETFS_INODE_OWNED, &pi->state)) {
		if (type == pi->lock_type)
			return 0;
		if ((type == POHMELFS_READ_LOCK) && (pi->lock_type == POHMELFS_WRITE_LOCK))
			return 0;
	}

	if (!test_bit(NETFS_INODE_REMOTE_SYNCED, &pi->state))
		return 0;

	return 1;
}

int __init pohmelfs_mcache_init(void);
void pohmelfs_mcache_exit(void);

/* #define CONFIG_POHMELFS_DEBUG */

#ifdef CONFIG_POHMELFS_DEBUG
#define dprintka(f, a...) printk(f, ##a)
#define dprintk(f, a...) printk("%d: " f, task_pid_vnr(current), ##a)
#else
#define dprintka(f, a...) do {} while (0)
#define dprintk(f, a...) do {} while (0)
#endif

static inline void netfs_trans_get(struct netfs_trans *t)
{
	atomic_inc(&t->refcnt);
}

static inline void netfs_trans_put(struct netfs_trans *t)
{
	if (atomic_dec_and_test(&t->refcnt)) {
		dprintk("%s: t: %p, gen: %u, err: %d.\n",
			__func__, t, t->gen, t->result);
		if (t->complete)
			t->complete(t->pages, t->page_num,
				t->private, t->result);
		netfs_trans_free(t);
	}
}

struct pohmelfs_mcache {
	struct rb_node			mcache_entry;
	struct completion		complete;

	atomic_t			refcnt;

	u64				gen;

	void				*data;
	u64				start;
	u32				size;
	int				err;

	struct netfs_inode_info		info;
};

struct pohmelfs_mcache *pohmelfs_mcache_alloc(struct pohmelfs_sb *psb, u64 start,
		unsigned int size, void *data);
void pohmelfs_mcache_free(struct pohmelfs_sb *psb, struct pohmelfs_mcache *m);
struct pohmelfs_mcache *pohmelfs_mcache_search(struct pohmelfs_sb *psb, u64 gen);
void pohmelfs_mcache_remove_locked(struct pohmelfs_sb *psb, struct pohmelfs_mcache *m);

static inline void pohmelfs_mcache_get(struct pohmelfs_mcache *m)
{
	atomic_inc(&m->refcnt);
}

static inline void pohmelfs_mcache_put(struct pohmelfs_sb *psb,
		struct pohmelfs_mcache *m)
{
	if (atomic_dec_and_test(&m->refcnt))
		pohmelfs_mcache_free(psb, m);
}

/*#define POHMELFS_TRUNCATE_ON_INODE_FLUSH
 */

#endif /* __KERNEL__*/

#endif /* __NETFS_H */
