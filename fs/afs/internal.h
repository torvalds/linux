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
#include <net/net_namespace.h>
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
	struct afs_net		*net;		/* Network namespace in effect */
	struct afs_cell		*cell;		/* cell in which to find volume */
	struct afs_volume	*volume;	/* volume record */
	struct key		*key;		/* key to use for secure mounting */
};

struct afs_iget_data {
	struct afs_fid		fid;
	struct afs_volume	*volume;	/* volume on which resides */
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
 * List of server addresses.
 */
struct afs_addr_list {
	struct rcu_head		rcu;		/* Must be first */
	refcount_t		usage;
	unsigned short		nr_addrs;
	unsigned short		index;		/* Address currently in use */
	struct sockaddr_rxrpc	addrs[];
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
	struct afs_net		*net;		/* The network namespace */
	struct afs_server	*cm_server;	/* Server affected by incoming CM call */
	struct afs_server	*server;	/* Server used by client call */
	void			*request;	/* request data (first part) */
	struct address_space	*mapping;	/* page set */
	struct afs_writeback	*wb;		/* writeback being performed */
	void			*buffer;	/* reply receive buffer */
	void			*reply[4];	/* Where to put the reply */
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
	unsigned int		cb_break;	/* cb_break + cb_s_break before the call */
	union {
		unsigned	last_to;	/* amount of mapping[last] */
		unsigned	count2;		/* count used in unmarshalling */
	};
	unsigned char		unmarshall;	/* unmarshalling phase */
	bool			incoming;	/* T if incoming call */
	bool			send_pages;	/* T if data from mapping should be sent */
	bool			need_attention;	/* T if RxRPC poked us */
	bool			async;		/* T if asynchronous */
	bool			ret_reply0;	/* T if should return reply[0] on success */
	bool			upgrade;	/* T to request service upgrade */
	u16			service_id;	/* RxRPC service ID to call */
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
	struct afs_net		*net;		/* Network namespace */
	struct afs_cell		*cell;		/* The cell in which the volume resides */
	struct afs_volume	*volume;	/* volume record */
	char			rwparent;	/* T if parent is R/W AFS volume */
};

static inline struct afs_super_info *AFS_FS_S(struct super_block *sb)
{
	return sb->s_fs_info;
}

extern struct file_system_type afs_fs_type;

/*
 * AFS network namespace record.
 */
struct afs_net {
	struct afs_uuid		uuid;
	bool			live;		/* F if this namespace is being removed */

	/* AF_RXRPC I/O stuff */
	struct socket		*socket;
	struct afs_call		*spare_incoming_call;
	struct work_struct	charge_preallocation_work;
	struct mutex		socket_mutex;
	atomic_t		nr_outstanding_calls;
	atomic_t		nr_superblocks;

	/* Cell database */
	struct rb_root		cells;
	struct afs_cell		*ws_cell;
	struct work_struct	cells_manager;
	struct timer_list	cells_timer;
	atomic_t		cells_outstanding;
	seqlock_t		cells_lock;

	spinlock_t		proc_cells_lock;
	struct list_head	proc_cells;

	/* Volume location database */
	struct list_head	vl_updates;		/* VL records in need-update order */
	struct list_head	vl_graveyard;		/* Inactive VL records */
	struct delayed_work	vl_reaper;
	struct delayed_work	vl_updater;
	spinlock_t		vl_updates_lock;
	spinlock_t		vl_graveyard_lock;

	/* File locking renewal management */
	struct mutex		lock_manager_mutex;

	/* Server database */
	struct rb_root		servers;		/* Active servers */
	rwlock_t		servers_lock;
	struct list_head	server_graveyard;	/* Inactive server LRU list */
	spinlock_t		server_graveyard_lock;
	struct timer_list	server_timer;
	struct work_struct	server_reaper;
	atomic_t		servers_outstanding;

	/* Misc */
	struct proc_dir_entry	*proc_afs;		/* /proc/net/afs directory */
};

extern struct afs_net __afs_net;// Dummy AFS network namespace; TODO: replace with real netns

enum afs_cell_state {
	AFS_CELL_UNSET,
	AFS_CELL_ACTIVATING,
	AFS_CELL_ACTIVE,
	AFS_CELL_DEACTIVATING,
	AFS_CELL_INACTIVE,
	AFS_CELL_FAILED,
};

/*
 * AFS cell record
 */
struct afs_cell {
	union {
		struct rcu_head	rcu;
		struct rb_node	net_node;	/* Node in net->cells */
	};
	struct afs_net		*net;
	struct key		*anonymous_key;	/* anonymous user key for this cell */
	struct work_struct	manager;	/* Manager for init/deinit/dns */
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
	time64_t		dns_expiry;	/* Time AFSDB/SRV record expires */
	time64_t		last_inactive;	/* Time of last drop of usage count */
	atomic_t		usage;
	unsigned long		flags;
#define AFS_CELL_FL_NOT_READY	0		/* The cell record is not ready for use */
#define AFS_CELL_FL_NO_GC	1		/* The cell was added manually, don't auto-gc */
#define AFS_CELL_FL_NOT_FOUND	2		/* Permanent DNS error */
#define AFS_CELL_FL_DNS_FAIL	3		/* Failed to access DNS */
#define AFS_CELL_FL_NO_LOOKUP_YET 4		/* Not completed first DNS lookup yet */
	enum afs_cell_state	state;
	short			error;

	spinlock_t		vl_lock;	/* vl_list lock */

	/* VLDB server list. */
	rwlock_t		vl_addrs_lock;	/* Lock on vl_addrs */
	struct afs_addr_list	__rcu *vl_addrs; /* List of VL servers */
	u8			name_len;	/* Length of name */
	char			name[64 + 1];	/* Cell name, case-flattened and NUL-padded */
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
	struct sockaddr_rxrpc	servers[8];	/* fileserver addresses */
	time_t			rtime;		/* last retrieval time */
};

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
	struct afs_addr_list	__rcu *addrs;	/* List of addresses for this server */
	struct afs_net		*net;		/* Network namespace in which the server resides */
	struct afs_cell		*cell;		/* cell in which server resides */
	struct list_head	link;		/* link in cell's server list */
	struct list_head	grave;		/* link in master graveyard list */

	struct rb_node		master_rb;	/* link in master by-addr tree */
	struct rw_semaphore	sem;		/* access lock */
	unsigned long		flags;
#define AFS_SERVER_NEW		0		/* New server, don't inc cb_s_break */

	/* file service access */
	int			fs_state;      	/* 0 or reason FS currently marked dead (-errno) */
	spinlock_t		fs_lock;	/* access lock */

	/* callback promise management */
	struct list_head	cb_interests;	/* List of superblocks using this server */
	unsigned		cb_s_break;	/* Break-everything counter. */
	rwlock_t		cb_break_lock;	/* Volume finding lock */
};

/*
 * Interest by a superblock on a server.
 */
struct afs_cb_interest {
	struct list_head	cb_link;	/* Link in server->cb_interests */
	struct afs_server	*server;	/* Server on which this interest resides */
	struct super_block	*sb;		/* Superblock on which inodes reside */
	afs_volid_t		vid;		/* Volume ID to match */
	refcount_t		usage;
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
	struct afs_cb_interest	*cb_interests[8]; /* Interests on servers for callbacks */
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
	struct afs_fid		fid;		/* the file identifier for this inode */
	struct afs_file_status	status;		/* AFS status info for this file */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif
	struct afs_permits	*permit_cache;	/* cache of permits so far obtained */
	struct mutex		validate_lock;	/* lock for validating this vnode */
	wait_queue_head_t	update_waitq;	/* status fetch waitqueue */
	int			update_cnt;	/* number of outstanding ops that will update the
						 * status */
	spinlock_t		writeback_lock;	/* lock for writebacks */
	spinlock_t		lock;		/* waitqueue/flags lock */
	unsigned long		flags;
#define AFS_VNODE_CB_PROMISED	0		/* Set if vnode has a callback promise */
#define AFS_VNODE_UNSET		1		/* set if vnode attributes not yet set */
#define AFS_VNODE_DIR_MODIFIED	2		/* set if dir vnode's data modified */
#define AFS_VNODE_ZAP_DATA	3		/* set if vnode's data should be invalidated */
#define AFS_VNODE_DELETED	4		/* set if vnode deleted on server */
#define AFS_VNODE_MOUNTPOINT	5		/* set if vnode is a mountpoint symlink */
#define AFS_VNODE_LOCKING	6		/* set if waiting for lock on vnode */
#define AFS_VNODE_READLOCKED	7		/* set if vnode is read-locked on the server */
#define AFS_VNODE_WRITELOCKED	8		/* set if vnode is write-locked on the server */
#define AFS_VNODE_UNLOCKING	9		/* set if vnode is being unlocked on the server */
#define AFS_VNODE_AUTOCELL	10		/* set if Vnode is an auto mount point */
#define AFS_VNODE_PSEUDODIR	11		/* set if Vnode is a pseudo directory */

	struct list_head	writebacks;	/* alterations in pagecache that need writing */
	struct list_head	pending_locks;	/* locks waiting to be granted */
	struct list_head	granted_locks;	/* locks granted on this file */
	struct delayed_work	lock_work;	/* work to be done in locking */
	struct key		*unlock_key;	/* key to be used in unlocking */

	/* outstanding callback notification on this file */
	struct afs_cb_interest	*cb_interest;	/* Server on which this resides */
	unsigned int		cb_s_break;	/* Mass break counter on ->server */
	unsigned int		cb_break;	/* Break counter on vnode */
	seqlock_t		cb_lock;	/* Lock for ->cb_interest, ->status, ->cb_*break */

	time64_t		cb_expires_at;	/* time at which callback expires */
	unsigned		cb_version;	/* callback version */
	afs_callback_type_t	cb_type;	/* type of callback */
};

/*
 * cached security record for one user's attempt to access a vnode
 */
struct afs_permit {
	struct key		*key;		/* RxRPC ticket holding a security context */
	afs_access_t		access;		/* CallerAccess value for this key */
};

/*
 * Immutable cache of CallerAccess records from attempts to access vnodes.
 * These may be shared between multiple vnodes.
 */
struct afs_permits {
	struct rcu_head		rcu;
	struct hlist_node	hash_node;	/* Link in hash */
	unsigned long		h;		/* Hash value for this permit list */
	refcount_t		usage;
	unsigned short		nr_permits;	/* Number of records */
	bool			invalidated;	/* Invalidated due to key change */
	struct afs_permit	permits[];	/* List of permits sorted by key pointer */
};

/*
 * record of one of a system's set of network interfaces
 */
struct afs_interface {
	struct in_addr	address;	/* IPv4 address bound to interface */
	struct in_addr	netmask;	/* netmask applied to address */
	unsigned	mtu;		/* MTU of interface */
};

/*
 * Cursor for iterating over a server's address list.
 */
struct afs_addr_cursor {
	struct afs_addr_list	*alist;		/* Current address list (pins ref) */
	struct sockaddr_rxrpc	*addr;
	unsigned short		start;		/* Starting point in alist->addrs[] */
	unsigned short		index;		/* Wrapping offset from start to current addr */
	short			error;
	bool			begun;		/* T if we've begun iteration */
	bool			responded;	/* T if the current address responded */
};

/*
 * Cursor for iterating over a set of fileservers.
 */
struct afs_fs_cursor {
	struct afs_addr_cursor	ac;
	struct afs_server	*server;	/* Current server (pins ref) */
};

/*****************************************************************************/
/*
 * addr_list.c
 */
static inline struct afs_addr_list *afs_get_addrlist(struct afs_addr_list *alist)
{
	if (alist)
		refcount_inc(&alist->usage);
	return alist;
}
extern struct afs_addr_list *afs_alloc_addrlist(unsigned int,
						unsigned short,
						unsigned short);
extern void afs_put_addrlist(struct afs_addr_list *);
extern struct afs_addr_list *afs_parse_text_addrs(const char *, size_t, char,
						  unsigned short, unsigned short);
extern struct afs_addr_list *afs_dns_query(struct afs_cell *, time64_t *);
extern bool afs_iterate_addresses(struct afs_addr_cursor *);
extern int afs_end_cursor(struct afs_addr_cursor *);
extern int afs_set_vl_cursor(struct afs_addr_cursor *, struct afs_cell *);

/*
 * cache.c
 */
#ifdef CONFIG_AFS_FSCACHE
extern struct fscache_netfs afs_cache_netfs;
extern struct fscache_cookie_def afs_cell_cache_index_def;
extern struct fscache_cookie_def afs_volume_cache_index_def;
extern struct fscache_cookie_def afs_vnode_cache_index_def;
#else
#define afs_cell_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#define afs_volume_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#define afs_vnode_cache_index_def	(*(struct fscache_cookie_def *) NULL)
#endif

/*
 * callback.c
 */
extern void afs_init_callback_state(struct afs_server *);
extern void afs_break_callback(struct afs_vnode *);
extern void afs_break_callbacks(struct afs_server *, size_t,struct afs_callback[]);

extern int afs_register_server_cb_interest(struct afs_vnode *, struct afs_cb_interest **,
					   struct afs_server *);
extern void afs_put_cb_interest(struct afs_net *, struct afs_cb_interest *);
extern void afs_clear_callback_interests(struct afs_net *, struct afs_volume *);

static inline struct afs_cb_interest *afs_get_cb_interest(struct afs_cb_interest *cbi)
{
	refcount_inc(&cbi->usage);
	return cbi;
}

/*
 * cell.c
 */
extern int afs_cell_init(struct afs_net *, const char *);
extern struct afs_cell *afs_lookup_cell_rcu(struct afs_net *, const char *, unsigned);
extern struct afs_cell *afs_lookup_cell(struct afs_net *, const char *, unsigned,
					const char *, bool);
extern struct afs_cell *afs_get_cell(struct afs_cell *);
extern void afs_put_cell(struct afs_net *, struct afs_cell *);
extern void afs_manage_cells(struct work_struct *);
extern void afs_cells_timer(struct timer_list *);
extern void __net_exit afs_cell_purge(struct afs_net *);

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
extern struct workqueue_struct *afs_lock_manager;

extern void afs_lock_work(struct work_struct *);
extern void afs_lock_may_be_available(struct afs_vnode *);
extern int afs_lock(struct file *, int, struct file_lock *);
extern int afs_flock(struct file *, int, struct file_lock *);

/*
 * fsclient.c
 */
extern int afs_fs_fetch_file_status(struct afs_fs_cursor *, struct key *,
				    struct afs_vnode *, struct afs_volsync *,
				    bool);
extern int afs_fs_give_up_callbacks(struct afs_net *, struct afs_server *, bool);
extern int afs_fs_fetch_data(struct afs_fs_cursor *, struct key *,
			     struct afs_vnode *, struct afs_read *, bool);
extern int afs_fs_create(struct afs_fs_cursor *, struct key *,
			 struct afs_vnode *, const char *, umode_t,
			 struct afs_fid *, struct afs_file_status *,
			 struct afs_callback *, bool);
extern int afs_fs_remove(struct afs_fs_cursor *, struct key *,
			 struct afs_vnode *, const char *, bool, bool);
extern int afs_fs_link(struct afs_fs_cursor *, struct key *, struct afs_vnode *,
		       struct afs_vnode *, const char *, bool);
extern int afs_fs_symlink(struct afs_fs_cursor *, struct key *,
			  struct afs_vnode *, const char *, const char *,
			  struct afs_fid *, struct afs_file_status *, bool);
extern int afs_fs_rename(struct afs_fs_cursor *, struct key *,
			 struct afs_vnode *, const char *,
			 struct afs_vnode *, const char *, bool);
extern int afs_fs_store_data(struct afs_fs_cursor *, struct afs_writeback *,
			     pgoff_t, pgoff_t, unsigned, unsigned, bool);
extern int afs_fs_setattr(struct afs_fs_cursor *, struct key *,
			  struct afs_vnode *, struct iattr *, bool);
extern int afs_fs_get_volume_status(struct afs_fs_cursor *, struct key *,
				    struct afs_vnode *,
				    struct afs_volume_status *, bool);
extern int afs_fs_set_lock(struct afs_fs_cursor *, struct key *,
			   struct afs_vnode *, afs_lock_type_t, bool);
extern int afs_fs_extend_lock(struct afs_fs_cursor *, struct key *,
			      struct afs_vnode *, bool);
extern int afs_fs_release_lock(struct afs_fs_cursor *, struct key *,
			       struct afs_vnode *, bool);
extern int afs_fs_give_up_all_callbacks(struct afs_server *, struct afs_addr_cursor *,
					struct key *, bool);

/*
 * inode.c
 */
extern int afs_iget5_test(struct inode *, void *);
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

static inline struct afs_net *afs_d2net(struct dentry *dentry)
{
	return &__afs_net;
}

static inline struct afs_net *afs_i2net(struct inode *inode)
{
	return &__afs_net;
}

static inline struct afs_net *afs_v2net(struct afs_vnode *vnode)
{
	return &__afs_net;
}

static inline struct afs_net *afs_sock2net(struct sock *sk)
{
	return &__afs_net;
}

static inline struct afs_net *afs_get_net(struct afs_net *net)
{
	return net;
}

static inline void afs_put_net(struct afs_net *net)
{
}

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
extern int __net_init afs_proc_init(struct afs_net *);
extern void __net_exit afs_proc_cleanup(struct afs_net *);
extern int afs_proc_cell_setup(struct afs_net *, struct afs_cell *);
extern void afs_proc_cell_remove(struct afs_net *, struct afs_cell *);

/*
 * rxrpc.c
 */
extern struct workqueue_struct *afs_async_calls;

extern int __net_init afs_open_socket(struct afs_net *);
extern void __net_exit afs_close_socket(struct afs_net *);
extern void afs_charge_preallocation(struct work_struct *);
extern void afs_put_call(struct afs_call *);
extern int afs_queue_call_work(struct afs_call *);
extern long afs_make_call(struct afs_addr_cursor *, struct afs_call *, gfp_t, bool);
extern struct afs_call *afs_alloc_flat_call(struct afs_net *,
					    const struct afs_call_type *,
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
extern void afs_put_permits(struct afs_permits *);
extern void afs_clear_permits(struct afs_vnode *);
extern void afs_cache_permit(struct afs_vnode *, struct key *, unsigned int);
extern void afs_zap_permits(struct rcu_head *);
extern struct key *afs_request_key(struct afs_cell *);
extern int afs_permission(struct inode *, int);
extern void __exit afs_clean_up_permit_cache(void);

/*
 * server.c
 */
extern spinlock_t afs_server_peer_lock;

static inline struct afs_server *afs_get_server(struct afs_server *server)
{
	atomic_inc(&server->usage);
	return server;
}

extern void afs_server_timer(struct timer_list *);
extern struct afs_server *afs_lookup_server(struct afs_cell *,
					    struct sockaddr_rxrpc *);
extern struct afs_server *afs_find_server(struct afs_net *,
					  const struct sockaddr_rxrpc *);
extern void afs_put_server(struct afs_net *, struct afs_server *);
extern void afs_reap_server(struct work_struct *);
extern void __net_exit afs_purge_servers(struct afs_net *);

/*
 * super.c
 */
extern int __init afs_fs_init(void);
extern void __exit afs_fs_exit(void);

/*
 * vlclient.c
 */
extern int afs_vl_get_entry_by_name(struct afs_net *, struct afs_addr_cursor *,
				    struct key *, const char *,
				    struct afs_cache_vlocation *, bool);
extern int afs_vl_get_entry_by_id(struct afs_net *, struct afs_addr_cursor *,
				  struct key *, afs_volid_t, afs_voltype_t,
				  struct afs_cache_vlocation *, bool);

/*
 * vlocation.c
 */
extern struct workqueue_struct *afs_vlocation_update_worker;

#define afs_get_vlocation(V) do { atomic_inc(&(V)->usage); } while(0)

extern struct afs_vlocation *afs_vlocation_lookup(struct afs_net *,
						  struct afs_cell *,
						  struct key *,
						  const char *, size_t);
extern void afs_put_vlocation(struct afs_net *, struct afs_vlocation *);
extern void afs_vlocation_updater(struct work_struct *);
extern void afs_vlocation_reaper(struct work_struct *);
extern void __net_exit afs_vlocation_purge(struct afs_net *);

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
extern int afs_vnode_fetch_status(struct afs_vnode *, struct key *, bool);
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
static inline struct afs_volume *afs_get_volume(struct afs_volume *volume)
{
	if (volume)
		atomic_inc(&volume->usage);
	return volume;
}

extern void afs_put_volume(struct afs_cell *, struct afs_volume *);
extern struct afs_volume *afs_volume_lookup(struct afs_mount_params *);
extern void afs_init_fs_cursor(struct afs_fs_cursor *, struct afs_vnode *);
extern int afs_set_fs_cursor(struct afs_fs_cursor *, struct afs_vnode *);
extern bool afs_volume_pick_fileserver(struct afs_fs_cursor *, struct afs_vnode *);
extern bool afs_iterate_fs_cursor(struct afs_fs_cursor *, struct afs_vnode *);
extern int afs_end_fs_cursor(struct afs_fs_cursor *, struct afs_net *);

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

/*
 * xattr.c
 */
extern const struct xattr_handler *afs_xattr_handlers[];
extern ssize_t afs_listxattr(struct dentry *, char *, size_t);

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
