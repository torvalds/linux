/* internal AFS stuff
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/rxrpc.h>
#include <linux/key.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/fscache.h>
#include <linux/backing-dev.h>
#include <linux/uuid.h>
#include <net/af_rxrpc.h>

#include "afs.h"
#include "afs_vl.h"

#define AFS_CELL_MAX_ADDRS 15

struct pagevec;
struct afs_call;

typedef enum {
	AFS_VL_NEW,			/* new, uninitialised record */
	AFS_VL_CREATING,		/* creating record */
	AFS_VL_VALID,			/* record is pending */
	AFS_VL_NO_VOLUME,		/* no such volume available */
	AFS_VL_UPDATING,		/* update in progress */
	AFS_VL_VOLUME_DELETED,		/* volume was deleted */
	AFS_VL_UNCERTAIN,		/* uncertain state (update failed) */
} __attribute__((packed)) afs_vlocation_state_t;

struct afs_mount_params {
	bool			rwpath;		/* T if the parent should be considered R/W */
	bool			force;		/* T to force cell type */
	bool			autocell;	/* T if set auto mount operation */
	afs_voltype_t		type;		/* type of volume requested */
	int			volnamesz;	/* size of volume name */
	const char		*volname;	/* name of volume to mount */
	struct afs_cell		*cell;		/* cell in which to find volume */
	struct afs_volume	*volume;	/* volume record */
	struct key		*key;		/* key to use for secure mounting */
};

enum afs_call_state {
	AFS_CALL_REQUESTING,	/* request is being sent for outgoing call */
	AFS_CALL_AWAIT_REPLY,	/* awaiting reply to outgoing call */
	AFS_CALL_AWAIT_OP_ID,	/* awaiting op ID on incoming call */
	AFS_CALL_AWAIT_REQUEST,	/* awaiting request data on incoming call */
	AFS_CALL_REPLYING,	/* replying to incoming call */
	AFS_CALL_AWAIT_ACK,	/* awaiting final ACK of incoming call */
	AFS_CALL_COMPLETE,	/* Completed or failed */
};
/*
 * a record of an in-progress RxRPC call
 */
struct afs_call {
	const struct afs_call_type *type;	/* type of call */
	wait_queue_head_t	waitq;		/* processes awaiting completion */
	struct work_struct	async_work;	/* async I/O processor */
	struct work_struct	work;		/* actual work processor */
	struct rxrpc_call	*rxcall;	/* RxRPC call handle */
	struct key		*key;		/* security for this call */
	struct afs_server	*server;	/* server affected by incoming CM call */
	void			*request;	/* request data (first part) */
	struct address_space	*mapping;	/* page set */
	struct afs_writeback	*wb;		/* writeback being performed */
	void			*buffer;	/* reply receive buffer */
	void			*reply;		/* reply buffer (first part) */
	void			*reply2;	/* reply buffer (second part) */
	void			*reply3;	/* reply buffer (third part) */
	void			*reply4;	/* reply buffer (fourth part) */
	pgoff_t			first;		/* first page in mapping to deal with */
	pgoff_t			last;		/* last page in mapping to deal with */
	size_t			offset;		/* offset into received data store */
	atomic_t		usage;
	enum afs_call_state	state;
	int			error;		/* error code */
	u32			abort_code;	/* Remote abort ID or 0 */
	unsigned		request_size;	/* size of request data */
	unsigned		reply_max;	/* maximum size of reply */
	unsigned		first_offset;	/* offset into mapping[first] */
	union {
		unsigned	last_to;	/* amount of mapping[last] */
		unsigned	count2;		/* count used in unmarshalling */
	};
	unsigned char		unmarshall;	/* unmarshalling phase */
	bool			incoming;	/* T if incoming call */
	bool			send_pages;	/* T if data from mapping should be sent */
	bool			need_attention;	/* T if RxRPC poked us */
	bool			async;		/* T if asynchronous */
	u16			service_id;	/* RxRPC service ID to call */
	__be16			port;		/* target UDP port */
	u32			operation_ID;	/* operation ID for an incoming call */
	u32			count;		/* count for use in unmarshalling */
	__be32			tmp;		/* place to extract temporary data */
	afs_dataversion_t	store_version;	/* updated version expected from store */
};

struct afs_call_type {
	const char *name;

	/* deliver request or reply data to an call
	 * - returning an error will cause the call to be aborted
	 */
	int (*deliver)(struct afs_call *call);

	/* map an abort code to an error number */
	int (*abort_to_error)(u32 abort_code);

	/* clean up a call */
	void (*destructor)(struct afs_call *call);

	/* Work function */
	void (*work)(struct work_struct *work);
};

/*
 * Record of an outstanding read operation on a vnode.
 */
struct afs_read {
	loff_t			pos;		/* Where to start reading */
	loff_t			len;		/* How much we're asking for */
	loff_t			actual_len;	/* How much we're actually getting */
	loff_t			remain;		/* Amount remaining */
	atomic_t		usage;
	unsigned int		index;		/* Which page we're reading into */
	unsigned int		nr_pages;
	void (*page_done)(struct afs_call *, struct afs_read *);
	struct page		*pages[];
};

/*
 * record of an outstanding writeback on a vnode
 */
struct afs_writeback {
	struct list_head	link;		/* link in vnode->writebacks */
	struct work_struct	writer;		/* work item to perform the writeback */
	struct afs_vnode	*vnode;		/* vnode to which this write applies */
	struct key		*key;		/* owner of this write */
	wait_queue_head_t	waitq;		/* completion and ready wait queue */
	pgoff_t			first;		/* first page in batch */
	pgoff_t			point;		/* last page in current store op */
	pgoff_t			last;		/* last page in batch (inclusive) */
	unsigned		offset_first;	/* offset into first page of start of write */
	unsigned		to_last;	/* offset into last page of end of write */
	int			num_conflicts;	/* count of conflicting writes in list */
	int			usage;
	bool			conflicts;	/* T if has dependent conflicts */
	enum {
		AFS_WBACK_SYNCING,		/* synchronisation being performed */
		AFS_WBACK_PENDING,		/* write pending */
		AFS_WBACK_CONFLICTING,		/* conflicting writes posted */
		AFS_WBACK_WRITING,		/* writing back */
		AFS_WBACK_COMPLETE		/* the writeback record has been unlinked */
	} state __attribute__((packed));
};

/*
 * AFS superblock private data
 * - there's one superblock per volume
 */
struct afs_super_info {
	struct afs_volume	*volume;	/* volume record */
	char			rwparent;	/* T if parent is R/W AFS volume */
};

static inline struct afs_super_info *AFS_FS_S(struct super_block *sb)
{
	return sb->s_fs_info;
}

extern struct file_system_type afs_fs_type;

/*
 * entry in the cached cell catalogue
 */
struct afs_cache_cell {
	char		name[AFS_MAXCELLNAME];	/* cell name (padded with NULs) */
	struct in_addr	vl_servers[15];		/* cached cell VL servers */
};

/*
 * AFS cell record
 */
struct afs_cell {
	atomic_t		usage;
	struct list_head	link;		/* main cell list link */
	struct key		*anonymous_key;	/* anonymous user key for this cell */
	struct list_head	proc_link;	/* /proc cell list link */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif

	/* server record management */
	rwlock_t		servers_lock;	/* active server list lock */
	struct list_head	servers;	/* active server list */

	/* volume location record management */
	struct rw_semaphore	vl_sem;		/* volume management serialisation semaphore */
	struct list_head	vl_list;	/* cell's active VL record list */
	spinlock_t		vl_lock;	/* vl_list lock */
	unsigned short		vl_naddrs;	/* number of VL servers in addr list */
	unsigned short		vl_curr_svix;	/* current server index */
	struct in_addr		vl_addrs[AFS_CELL_MAX_ADDRS];	/* cell VL server addresses */

	char			name[0];	/* cell name - must go last */
};

/*
 * entry in the cached volume location catalogue
 */
struct afs_cache_vlocation {
	/* volume name (lowercase, padded with NULs) */
	uint8_t			name[AFS_MAXVOLNAME + 1];

	uint8_t			nservers;	/* number of entries used in servers[] */
	uint8_t			vidmask;	/* voltype mask for vid[] */
	uint8_t			srvtmask[8];	/* voltype masks for servers[] */
#define AFS_VOL_VTM_RW	0x01 /* R/W version of the volume is available (on this server) */
#define AFS_VOL_VTM_RO	0x02 /* R/O version of the volume is available (on this server) */
#define AFS_VOL_VTM_BAK	0x04 /* backup version of the volume is available (on this server) */

	afs_volid_t		vid[3];		/* volume IDs for R/W, R/O and Bak volumes */
	struct in_addr		servers[8];	/* fileserver addresses */
	time_t			rtime;		/* last retrieval time */
};

/*
 * volume -> vnode hash table entry
 */
struct afs_cache_vhash {
	afs_voltype_t		vtype;		/* which volume variation */
	uint8_t			hash_bucket;	/* which hash bucket this represents */
} __attribute__((packed));

/*
 * AFS volume location record
 */
struct afs_vlocation {
	atomic_t		usage;
	time64_t		time_of_death;	/* time at which put reduced usage to 0 */
	struct list_head	link;		/* link in cell volume location list */
	struct list_head	grave;		/* link in master graveyard list */
	struct list_head	update;		/* link in master update list */
	struct afs_cell		*cell;		/* cell to which volume belongs */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif
	struct afs_cache_vlocation vldb;	/* volume information DB record */
	struct afs_volume	*vols[3];	/* volume access record pointer (index by type) */
	wait_queue_head_t	waitq;		/* status change waitqueue */
	time64_t		update_at;	/* time at which record should be updated */
	spinlock_t		lock;		/* access lock */
	afs_vlocation_state_t	state;		/* volume location state */
	unsigned short		upd_rej_cnt;	/* ENOMEDIUM count during update */
	unsigned short		upd_busy_cnt;	/* EBUSY count during update */
	bool			valid;		/* T if valid */
};

/*
 * AFS fileserver record
 */
struct afs_server {
	atomic_t		usage;
	time64_t		time_of_death;	/* time at which put reduced usage to 0 */
	struct in_addr		addr;		/* server address */
	struct afs_cell		*cell;		/* cell in which server resides */
	struct list_head	link;		/* link in cell's server list */
	struct list_head	grave;		/* link in master graveyard list */
	struct rb_node		master_rb;	/* link in master by-addr tree */
	struct rw_semaphore	sem;		/* access lock */

	/* file service access */
	struct rb_root		fs_vnodes;	/* vnodes backed by this server (ordered by FID) */
	unsigned long		fs_act_jif;	/* time at which last activity occurred */
	unsigned long		fs_dead_jif;	/* time at which no longer to be considered dead */
	spinlock_t		fs_lock;	/* access lock */
	int			fs_state;      	/* 0 or reason FS currently marked dead (-errno) */

	/* callback promise management */
	struct rb_root		cb_promises;	/* vnode expiration list (ordered earliest first) */
	struct delayed_work	cb_updater;	/* callback updater */
	struct delayed_work	cb_break_work;	/* collected break dispatcher */
	wait_queue_head_t	cb_break_waitq;	/* space available in cb_break waitqueue */
	spinlock_t		cb_lock;	/* access lock */
	struct afs_callback	cb_break[64];	/* ring of callbacks awaiting breaking */
	atomic_t		cb_break_n;	/* number of pending breaks */
	u8			cb_break_head;	/* head of callback breaking ring */
	u8			cb_break_tail;	/* tail of callback breaking ring */
};

/*
 * AFS volume access record
 */
struct afs_volume {
	atomic_t		usage;
	struct afs_cell		*cell;		/* cell to which belongs (unrefd ptr) */
	struct afs_vlocation	*vlocation;	/* volume location */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif
	afs_volid_t		vid;		/* volume ID */
	afs_voltype_t		type;		/* type of volume */
	char			type_force;	/* force volume type (suppress R/O -> R/W) */
	unsigned short		nservers;	/* number of server slots filled */
	unsigned short		rjservers;	/* number of servers discarded due to -ENOMEDIUM */
	struct afs_server	*servers[8];	/* servers on which volume resides (ordered) */
	struct rw_semaphore	server_sem;	/* lock for accessing current server */
};

/*
 * vnode catalogue entry
 */
struct afs_cache_vnode {
	afs_vnodeid_t		vnode_id;	/* vnode ID */
	unsigned		vnode_unique;	/* vnode ID uniquifier */
	afs_dataversion_t	data_version;	/* data version */
};

/*
 * AFS inode private data
 */
struct afs_vnode {
	struct inode		vfs_inode;	/* the VFS's inode record */

	struct afs_volume	*volume;	/* volume on which vnode resides */
	struct afs_server	*server;	/* server currently supplying this file */
	struct afs_fid		fid;		/* the file identifier for this inode */
	struct afs_file_status	status;		/* AFS status info for this file */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif
	struct afs_permits	*permits;	/* cache of permits so far obtained */
	struct mutex		permits_lock;	/* lock for altering permits list */
	struct mutex		validate_lock;	/* lock for validating this vnode */
	wait_queue_head_t	update_waitq;	/* status fetch waitqueue */
	int			update_cnt;	/* number of outstanding ops that will update the
						 * status */
	spinlock_t		writeback_lock;	/* lock for writebacks */
	spinlock_t		lock;		/* waitqueue/flags lock */
	unsigned long		flags;
#define AFS_VNODE_CB_BROKEN	0		/* set if vnode's callback was broken */
#define AFS_VNODE_UNSET		1		/* set if vnode attributes not yet set */
#define AFS_VNODE_MODIFIED	2		/* set if vnode's data modified */
#define AFS_VNODE_ZAP_DATA	3		/* set if vnode's data should be invalidated */
#define AFS_VNODE_DELETED	4		/* set if vnode deleted on server */
#define AFS_VNODE_MOUNTPOINT	5		/* set if vnode is a mountpoint symlink */
#define AFS_VNODE_LOCKING	6		/* set if waiting for lock on vnode */
#define AFS_VNODE_READLOCKED	7		/* set if vnode is read-locked on the server */
#define AFS_VNODE_WRITELOCKED	8		/* set if vnode is write-locked on the server */
#define AFS_VNODE_UNLOCKING	9		/* set if vnode is being unlocked on the server */
#define AFS_VNODE_AUTOCELL	10		/* set if Vnode is an auto mount point */
#define AFS_VNODE_PSEUDODIR	11		/* set if Vnode is a pseudo directory */

	long			acl_order;	/* ACL check count (callback break count) */

	struct list_head	writebacks;	/* alterations in pagecache that need writing */
	struct list_head	pending_locks;	/* locks waiting to be granted */
	struct list_head	granted_locks;	/* locks granted on this file */
	struct delayed_work	lock_work;	/* work to be done in locking */
	struct key		*unlock_key;	/* key to be used in unlocking */

	/* outstanding callback notification on this file */
	struct rb_node		server_rb;	/* link in server->fs_vnodes */
	struct rb_node		cb_promise;	/* link in server->cb_promises */
	struct work_struct	cb_broken_work;	/* work to be done on callback break */
	time64_t		cb_expires;	/* time at which callback expires */
	time64_t		cb_expires_at;	/* time used to order cb_promise */
	unsigned		cb_version;	/* callback version */
	unsigned		cb_expiry;	/* callback expiry time */
	afs_callback_type_t	cb_type;	/* type of callback */
	bool			cb_promised;	/* true if promise still holds */
};

/*
 * cached security record for one user's attempt to access a vnode
 */
struct afs_permit {
	struct key		*key;		/* RxRPC ticket holding a security context */
	afs_access_t		access_mask;	/* access mask for this key */
};

/*
 * cache of security records from attempts to access a vnode
 */
struct afs_permits {
	struct rcu_head		rcu;		/* disposal procedure */
	int			count;		/* number of records */
	struct afs_permit	permits[0];	/* the permits so far examined */
};

/*
 * record of one of a system's set of network interfaces
 */
struct afs_interface {
	struct in_addr	address;	/* IPv4 address bound to interface */
	struct in_addr	netmask;	/* netmask applied to address */
	unsigned	mtu;		/* MTU of interface */
};

struct afs_uuid {
	__be32		time_low;			/* low part of timestamp */
	__be16		time_mid;			/* mid part of timestamp */
	__be16		time_hi_and_version;		/* high part of timestamp and version  */
	__u8		clock_seq_hi_and_reserved;	/* clock seq hi and variant */
	__u8		clock_seq_low;			/* clock seq low */
	__u8		node[6];			/* spatially unique node ID (MAC addr) */
};

/*****************************************************************************/
/*
 * cache.c
 */
#ifdef CONFIG_AFS_FSCACHE
extern struct fscache_netfs afs_cache_netfs;
extern struct fscache_cookie_def afs_cell_cache_index_def;
extern struct fscache_cookie_def afs_vlocation_cache_index_def;
extern struct fscache_cookie_def afs_volume_cache_index_def;
extern struct fscache_cookie_def afs_vnode_cache_index_def;
#else
#define afs_cell_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#define afs_vlocation_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#define afs_volume_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#define afs_vnode_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#endif

/*
 * callback.c
 */
extern void afs_init_callback_state(struct afs_server *);
extern void afs_broken_callback_work(struct work_struct *);
extern void afs_break_callbacks(struct afs_server *, size_t,
				struct afs_callback[]);
extern void afs_discard_callback_on_delete(struct afs_vnode *);
extern void afs_give_up_callback(struct afs_vnode *);
extern void afs_dispatch_give_up_callbacks(struct work_struct *);
extern void afs_flush_callback_breaks(struct afs_server *);
extern int __init afs_callback_update_init(void);
extern void afs_callback_update_kill(void);

/*
 * cell.c
 */
extern struct rw_semaphore afs_proc_cells_sem;
extern struct list_head afs_proc_cells;

#define afs_get_cell(C) do { atomic_inc(&(C)->usage); } while(0)
extern int afs_cell_init(char *);
extern struct afs_cell *afs_cell_create(const char *, unsigned, char *, bool);
extern struct afs_cell *afs_cell_lookup(const char *, unsigned, bool);
extern struct afs_cell *afs_grab_cell(struct afs_cell *);
extern void afs_put_cell(struct afs_cell *);
extern void afs_cell_purge(void);

/*
 * cmservice.c
 */
extern bool afs_cm_incoming_call(struct afs_call *);

/*
 * dir.c
 */
extern const struct inode_operations afs_dir_inode_operations;
extern const struct dentry_operations afs_fs_dentry_operations;
extern const struct file_operations afs_dir_file_operations;

/*
 * file.c
 */
extern const struct address_space_operations afs_fs_aops;
extern const struct inode_operations afs_file_inode_operations;
extern const struct file_operations afs_file_operations;

extern int afs_open(struct inode *, struct file *);
extern int afs_release(struct inode *, struct file *);
extern int afs_page_filler(void *, struct page *);
extern void afs_put_read(struct afs_read *);

/*
 * flock.c
 */
extern void __exit afs_kill_lock_manager(void);
extern void afs_lock_work(struct work_struct *);
extern void afs_lock_may_be_available(struct afs_vnode *);
extern int afs_lock(struct file *, int, struct file_lock *);
extern int afs_flock(struct file *, int, struct file_lock *);

/*
 * fsclient.c
 */
extern int afs_fs_fetch_file_status(struct afs_server *, struct key *,
				    struct afs_vnode *, struct afs_volsync *,
				    bool);
extern int afs_fs_give_up_callbacks(struct afs_server *, bool);
extern int afs_fs_fetch_data(struct afs_server *, struct key *,
			     struct afs_vnode *, struct afs_read *, bool);
extern int afs_fs_create(struct afs_server *, struct key *,
			 struct afs_vnode *, const char *, umode_t,
			 struct afs_fid *, struct afs_file_status *,
			 struct afs_callback *, bool);
extern int afs_fs_remove(struct afs_server *, struct key *,
			 struct afs_vnode *, const char *, bool, bool);
extern int afs_fs_link(struct afs_server *, struct key *, struct afs_vnode *,
		       struct afs_vnode *, const char *, bool);
extern int afs_fs_symlink(struct afs_server *, struct key *,
			  struct afs_vnode *, const char *, const char *,
			  struct afs_fid *, struct afs_file_status *, bool);
extern int afs_fs_rename(struct afs_server *, struct key *,
			 struct afs_vnode *, const char *,
			 struct afs_vnode *, const char *, bool);
extern int afs_fs_store_data(struct afs_server *, struct afs_writeback *,
			     pgoff_t, pgoff_t, unsigned, unsigned, bool);
extern int afs_fs_setattr(struct afs_server *, struct key *,
			  struct afs_vnode *, struct iattr *, bool);
extern int afs_fs_get_volume_status(struct afs_server *, struct key *,
				    struct afs_vnode *,
				    struct afs_volume_status *, bool);
extern int afs_fs_set_lock(struct afs_server *, struct key *,
			   struct afs_vnode *, afs_lock_type_t, bool);
extern int afs_fs_extend_lock(struct afs_server *, struct key *,
			      struct afs_vnode *, bool);
extern int afs_fs_release_lock(struct afs_server *, struct key *,
			       struct afs_vnode *, bool);

/*
 * inode.c
 */
extern struct inode *afs_iget_autocell(struct inode *, const char *, int,
				       struct key *);
extern struct inode *afs_iget(struct super_block *, struct key *,
			      struct afs_fid *, struct afs_file_status *,
			      struct afs_callback *);
extern void afs_zap_data(struct afs_vnode *);
extern int afs_validate(struct afs_vnode *, struct key *);
extern int afs_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int afs_setattr(struct dentry *, struct iattr *);
extern void afs_evict_inode(struct inode *);
extern int afs_drop_inode(struct inode *);

/*
 * main.c
 */
extern struct workqueue_struct *afs_wq;
extern struct afs_uuid afs_uuid;

/*
 * misc.c
 */
extern int afs_abort_to_error(u32);

/*
 * mntpt.c
 */
extern const struct inode_operations afs_mntpt_inode_operations;
extern const struct inode_operations afs_autocell_inode_operations;
extern const struct file_operations afs_mntpt_file_operations;

extern struct vfsmount *afs_d_automount(struct path *);
extern void afs_mntpt_kill_timer(void);

/*
 * netdevices.c
 */
extern int afs_get_ipv4_interfaces(struct afs_interface *, size_t, bool);

/*
 * proc.c
 */
extern int afs_proc_init(void);
extern void afs_proc_cleanup(void);
extern int afs_proc_cell_setup(struct afs_cell *);
extern void afs_proc_cell_remove(struct afs_cell *);

/*
 * rxrpc.c
 */
extern struct socket *afs_socket;
extern atomic_t afs_outstanding_calls;

extern int afs_open_socket(void);
extern void afs_close_socket(void);
extern void afs_put_call(struct afs_call *);
extern int afs_queue_call_work(struct afs_call *);
extern int afs_make_call(struct in_addr *, struct afs_call *, gfp_t, bool);
extern struct afs_call *afs_alloc_flat_call(const struct afs_call_type *,
					    size_t, size_t);
extern void afs_flat_call_destructor(struct afs_call *);
extern void afs_send_empty_reply(struct afs_call *);
extern void afs_send_simple_reply(struct afs_call *, const void *, size_t);
extern int afs_extract_data(struct afs_call *, void *, size_t, bool);

static inline int afs_transfer_reply(struct afs_call *call)
{
	return afs_extract_data(call, call->buffer, call->reply_max, false);
}

/*
 * security.c
 */
extern void afs_clear_permits(struct afs_vnode *);
extern void afs_cache_permit(struct afs_vnode *, struct key *, long);
extern void afs_zap_permits(struct rcu_head *);
extern struct key *afs_request_key(struct afs_cell *);
extern int afs_permission(struct inode *, int);

/*
 * server.c
 */
extern spinlock_t afs_server_peer_lock;

#define afs_get_server(S)					\
do {								\
	_debug("GET SERVER %d", atomic_read(&(S)->usage));	\
	atomic_inc(&(S)->usage);				\
} while(0)

extern struct afs_server *afs_lookup_server(struct afs_cell *,
					    const struct in_addr *);
extern struct afs_server *afs_find_server(const struct sockaddr_rxrpc *);
extern void afs_put_server(struct afs_server *);
extern void __exit afs_purge_servers(void);

/*
 * super.c
 */
extern int afs_fs_init(void);
extern void afs_fs_exit(void);

/*
 * vlclient.c
 */
extern int afs_vl_get_entry_by_name(struct in_addr *, struct key *,
				    const char *, struct afs_cache_vlocation *,
				    bool);
extern int afs_vl_get_entry_by_id(struct in_addr *, struct key *,
				  afs_volid_t, afs_voltype_t,
				  struct afs_cache_vlocation *, bool);

/*
 * vlocation.c
 */
#define afs_get_vlocation(V) do { atomic_inc(&(V)->usage); } while(0)

extern int __init afs_vlocation_update_init(void);
extern struct afs_vlocation *afs_vlocation_lookup(struct afs_cell *,
						  struct key *,
						  const char *, size_t);
extern void afs_put_vlocation(struct afs_vlocation *);
extern void afs_vlocation_purge(void);

/*
 * vnode.c
 */
static inline struct afs_vnode *AFS_FS_I(struct inode *inode)
{
	return container_of(inode, struct afs_vnode, vfs_inode);
}

static inline struct inode *AFS_VNODE_TO_I(struct afs_vnode *vnode)
{
	return &vnode->vfs_inode;
}

extern void afs_vnode_finalise_status_update(struct afs_vnode *,
					     struct afs_server *);
extern int afs_vnode_fetch_status(struct afs_vnode *, struct afs_vnode *,
				  struct key *);
extern int afs_vnode_fetch_data(struct afs_vnode *, struct key *,
				struct afs_read *);
extern int afs_vnode_create(struct afs_vnode *, struct key *, const char *,
			    umode_t, struct afs_fid *, struct afs_file_status *,
			    struct afs_callback *, struct afs_server **);
extern int afs_vnode_remove(struct afs_vnode *, struct key *, const char *,
			    bool);
extern int afs_vnode_link(struct afs_vnode *, struct afs_vnode *, struct key *,
			  const char *);
extern int afs_vnode_symlink(struct afs_vnode *, struct key *, const char *,
			     const char *, struct afs_fid *,
			     struct afs_file_status *, struct afs_server **);
extern int afs_vnode_rename(struct afs_vnode *, struct afs_vnode *,
			    struct key *, const char *, const char *);
extern int afs_vnode_store_data(struct afs_writeback *, pgoff_t, pgoff_t,
				unsigned, unsigned);
extern int afs_vnode_setattr(struct afs_vnode *, struct key *, struct iattr *);
extern int afs_vnode_get_volume_status(struct afs_vnode *, struct key *,
				       struct afs_volume_status *);
extern int afs_vnode_set_lock(struct afs_vnode *, struct key *,
			      afs_lock_type_t);
extern int afs_vnode_extend_lock(struct afs_vnode *, struct key *);
extern int afs_vnode_release_lock(struct afs_vnode *, struct key *);

/*
 * volume.c
 */
#define afs_get_volume(V) do { atomic_inc(&(V)->usage); } while(0)

extern void afs_put_volume(struct afs_volume *);
extern struct afs_volume *afs_volume_lookup(struct afs_mount_params *);
extern struct afs_server *afs_volume_pick_fileserver(struct afs_vnode *);
extern int afs_volume_release_fileserver(struct afs_vnode *,
					 struct afs_server *, int);

/*
 * write.c
 */
extern int afs_set_page_dirty(struct page *);
extern void afs_put_writeback(struct afs_writeback *);
extern int afs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata);
extern int afs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata);
extern int afs_writepage(struct page *, struct writeback_control *);
extern int afs_writepages(struct address_space *, struct writeback_control *);
extern void afs_pages_written_back(struct afs_vnode *, struct afs_call *);
extern ssize_t afs_file_write(struct kiocb *, struct iov_iter *);
extern int afs_writeback_all(struct afs_vnode *);
extern int afs_flush(struct file *, fl_owner_t);
extern int afs_fsync(struct file *, loff_t, loff_t, int);


/*****************************************************************************/
/*
 * debug tracing
 */
#include <trace/events/afs.h>

extern unsigned afs_debug;

#define dbgprintk(FMT,...) \
	printk("[%-6.6s] "FMT"\n", current->comm ,##__VA_ARGS__)

#define kenter(FMT,...)	dbgprintk("==> %s("FMT")",__func__ ,##__VA_ARGS__)
#define kleave(FMT,...)	dbgprintk("<== %s()"FMT"",__func__ ,##__VA_ARGS__)
#define kdebug(FMT,...)	dbgprintk("    "FMT ,##__VA_ARGS__)


#if defined(__KDEBUG)
#define _enter(FMT,...)	kenter(FMT,##__VA_ARGS__)
#define _leave(FMT,...)	kleave(FMT,##__VA_ARGS__)
#define _debug(FMT,...)	kdebug(FMT,##__VA_ARGS__)

#elif defined(CONFIG_AFS_DEBUG)
#define AFS_DEBUG_KENTER	0x01
#define AFS_DEBUG_KLEAVE	0x02
#define AFS_DEBUG_KDEBUG	0x04

#define _enter(FMT,...)					\
do {							\
	if (unlikely(afs_debug & AFS_DEBUG_KENTER))	\
		kenter(FMT,##__VA_ARGS__);		\
} while (0)

#define _leave(FMT,...)					\
do {							\
	if (unlikely(afs_debug & AFS_DEBUG_KLEAVE))	\
		kleave(FMT,##__VA_ARGS__);		\
} while (0)

#define _debug(FMT,...)					\
do {							\
	if (unlikely(afs_debug & AFS_DEBUG_KDEBUG))	\
		kdebug(FMT,##__VA_ARGS__);		\
} while (0)

#else
#define _enter(FMT,...)	no_printk("==> %s("FMT")",__func__ ,##__VA_ARGS__)
#define _leave(FMT,...)	no_printk("<== %s()"FMT"",__func__ ,##__VA_ARGS__)
#define _debug(FMT,...)	no_printk("    "FMT ,##__VA_ARGS__)
#endif

/*
 * debug assertion checking
 */
#if 1 // defined(__KDEBUGALL)

#define ASSERT(X)						\
do {								\
	if (unlikely(!(X))) {					\
		printk(KERN_ERR "\n");				\
		printk(KERN_ERR "AFS: Assertion failed\n");	\
		BUG();						\
	}							\
} while(0)

#define ASSERTCMP(X, OP, Y)						\
do {									\
	if (unlikely(!((X) OP (Y)))) {					\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "AFS: Assertion failed\n");		\
		printk(KERN_ERR "%lu " #OP " %lu is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		printk(KERN_ERR "0x%lx " #OP " 0x%lx is false\n",	\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while(0)

#define ASSERTRANGE(L, OP1, N, OP2, H)					\
do {									\
	if (unlikely(!((L) OP1 (N)) || !((N) OP2 (H)))) {		\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "AFS: Assertion failed\n");		\
		printk(KERN_ERR "%lu "#OP1" %lu "#OP2" %lu is false\n",	\
		       (unsigned long)(L), (unsigned long)(N),		\
		       (unsigned long)(H));				\
		printk(KERN_ERR "0x%lx "#OP1" 0x%lx "#OP2" 0x%lx is false\n", \
		       (unsigned long)(L), (unsigned long)(N),		\
		       (unsigned long)(H));				\
		BUG();							\
	}								\
} while(0)

#define ASSERTIF(C, X)						\
do {								\
	if (unlikely((C) && !(X))) {				\
		printk(KERN_ERR "\n");				\
		printk(KERN_ERR "AFS: Assertion failed\n");	\
		BUG();						\
	}							\
} while(0)

#define ASSERTIFCMP(C, X, OP, Y)					\
do {									\
	if (unlikely((C) && !((X) OP (Y)))) {				\
		printk(KERN_ERR "\n");					\
		printk(KERN_ERR "AFS: Assertion failed\n");		\
		printk(KERN_ERR "%lu " #OP " %lu is false\n",		\
		       (unsigned long)(X), (unsigned long)(Y));		\
		printk(KERN_ERR "0x%lx " #OP " 0x%lx is false\n",	\
		       (unsigned long)(X), (unsigned long)(Y));		\
		BUG();							\
	}								\
} while(0)

#else

#define ASSERT(X)				\
do {						\
} while(0)

#define ASSERTCMP(X, OP, Y)			\
do {						\
} while(0)

#define ASSERTRANGE(L, OP1, N, OP2, H)		\
do {						\
} while(0)

#define ASSERTIF(C, X)				\
do {						\
} while(0)

#define ASSERTIFCMP(C, X, OP, Y)		\
do {						\
} while(0)

#endif /* __KDEBUGALL */
