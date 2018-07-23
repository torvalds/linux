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
#include <net/netns/generic.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>

#include "afs.h"
#include "afs_vl.h"

#define AFS_CELL_MAX_ADDRS 15

struct pagevec;
struct afs_call;

struct afs_mount_params {
	bool			rwpath;		/* T if the parent should be considered R/W */
	bool			force;		/* T to force cell type */
	bool			autocell;	/* T if set auto mount operation */
	bool			dyn_root;	/* T if dynamic root */
	afs_voltype_t		type;		/* type of volume requested */
	int			volnamesz;	/* size of volume name */
	const char		*volname;	/* name of volume to mount */
	struct net		*net_ns;	/* Network namespace in effect */
	struct afs_net		*net;		/* the AFS net namespace stuff */
	struct afs_cell		*cell;		/* cell in which to find volume */
	struct afs_volume	*volume;	/* volume record */
	struct key		*key;		/* key to use for secure mounting */
};

struct afs_iget_data {
	struct afs_fid		fid;
	struct afs_volume	*volume;	/* volume on which resides */
};

enum afs_call_state {
	AFS_CALL_CL_REQUESTING,		/* Client: Request is being sent */
	AFS_CALL_CL_AWAIT_REPLY,	/* Client: Awaiting reply */
	AFS_CALL_CL_PROC_REPLY,		/* Client: rxrpc call complete; processing reply */
	AFS_CALL_SV_AWAIT_OP_ID,	/* Server: Awaiting op ID */
	AFS_CALL_SV_AWAIT_REQUEST,	/* Server: Awaiting request data */
	AFS_CALL_SV_REPLYING,		/* Server: Replying */
	AFS_CALL_SV_AWAIT_ACK,		/* Server: Awaiting final ACK */
	AFS_CALL_COMPLETE,		/* Completed or failed */
};

/*
 * List of server addresses.
 */
struct afs_addr_list {
	struct rcu_head		rcu;		/* Must be first */
	refcount_t		usage;
	u32			version;	/* Version */
	unsigned short		nr_addrs;
	unsigned short		index;		/* Address currently in use */
	unsigned short		nr_ipv4;	/* Number of IPv4 addresses */
	unsigned long		probed;		/* Mask of servers that have been probed */
	unsigned long		yfs;		/* Mask of servers that are YFS */
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
	struct afs_cb_interest	*cbi;		/* Callback interest for server used */
	void			*request;	/* request data (first part) */
	struct address_space	*mapping;	/* Pages being written from */
	void			*buffer;	/* reply receive buffer */
	void			*reply[4];	/* Where to put the reply */
	pgoff_t			first;		/* first page in mapping to deal with */
	pgoff_t			last;		/* last page in mapping to deal with */
	size_t			offset;		/* offset into received data store */
	atomic_t		usage;
	enum afs_call_state	state;
	spinlock_t		state_lock;
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
	u16			service_id;	/* Actual service ID (after upgrade) */
	unsigned int		debug_id;	/* Trace ID */
	u32			operation_ID;	/* operation ID for an incoming call */
	u32			count;		/* count for use in unmarshalling */
	__be32			tmp;		/* place to extract temporary data */
	afs_dataversion_t	expected_version; /* Updated version expected from store */
	afs_dataversion_t	expected_version_2; /* 2nd updated version expected from store */
};

struct afs_call_type {
	const char *name;
	unsigned int op; /* Really enum afs_fs_operation */

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
 * Key available for writeback on a file.
 */
struct afs_wb_key {
	refcount_t		usage;
	struct key		*key;
	struct list_head	vnode_link;	/* Link in vnode->wb_keys */
};

/*
 * AFS open file information record.  Pointed to by file->private_data.
 */
struct afs_file {
	struct key		*key;		/* The key this file was opened with */
	struct afs_wb_key	*wb;		/* Writeback key record for this file */
};

static inline struct key *afs_file_key(struct file *file)
{
	struct afs_file *af = file->private_data;

	return af->key;
}

/*
 * Record of an outstanding read operation on a vnode.
 */
struct afs_read {
	loff_t			pos;		/* Where to start reading */
	loff_t			len;		/* How much we're asking for */
	loff_t			actual_len;	/* How much we're actually getting */
	loff_t			remain;		/* Amount remaining */
	loff_t			file_size;	/* File size returned by server */
	afs_dataversion_t	data_version;	/* Version number returned by server */
	refcount_t		usage;
	unsigned int		index;		/* Which page we're reading into */
	unsigned int		nr_pages;
	void (*page_done)(struct afs_call *, struct afs_read *);
	struct page		**pages;
	struct page		*array[];
};

/*
 * AFS superblock private data
 * - there's one superblock per volume
 */
struct afs_super_info {
	struct net		*net_ns;	/* Network namespace */
	struct afs_cell		*cell;		/* The cell in which the volume resides */
	struct afs_volume	*volume;	/* volume record */
	bool			dyn_root;	/* True if dynamic root */
};

static inline struct afs_super_info *AFS_FS_S(struct super_block *sb)
{
	return sb->s_fs_info;
}

extern struct file_system_type afs_fs_type;

/*
 * Set of substitutes for @sys.
 */
struct afs_sysnames {
#define AFS_NR_SYSNAME 16
	char			*subs[AFS_NR_SYSNAME];
	refcount_t		usage;
	unsigned short		nr;
	char			blank[1];
};

/*
 * AFS network namespace record.
 */
struct afs_net {
	struct net		*net;		/* Backpointer to the owning net namespace */
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
	struct afs_cell __rcu	*ws_cell;
	struct work_struct	cells_manager;
	struct timer_list	cells_timer;
	atomic_t		cells_outstanding;
	seqlock_t		cells_lock;

	struct mutex		proc_cells_lock;
	struct list_head	proc_cells;

	/* Known servers.  Theoretically each fileserver can only be in one
	 * cell, but in practice, people create aliases and subsets and there's
	 * no easy way to distinguish them.
	 */
	seqlock_t		fs_lock;	/* For fs_servers */
	struct rb_root		fs_servers;	/* afs_server (by server UUID or address) */
	struct list_head	fs_updates;	/* afs_server (by update_at) */
	struct hlist_head	fs_proc;	/* procfs servers list */

	struct hlist_head	fs_addresses4;	/* afs_server (by lowest IPv4 addr) */
	struct hlist_head	fs_addresses6;	/* afs_server (by lowest IPv6 addr) */
	seqlock_t		fs_addr_lock;	/* For fs_addresses[46] */

	struct work_struct	fs_manager;
	struct timer_list	fs_timer;
	atomic_t		servers_outstanding;

	/* File locking renewal management */
	struct mutex		lock_manager_mutex;

	/* Misc */
	struct super_block	*dynroot_sb;	/* Dynamic root mount superblock */
	struct proc_dir_entry	*proc_afs;	/* /proc/net/afs directory */
	struct afs_sysnames	*sysnames;
	rwlock_t		sysnames_lock;

	/* Statistics counters */
	atomic_t		n_lookup;	/* Number of lookups done */
	atomic_t		n_reval;	/* Number of dentries needing revalidation */
	atomic_t		n_inval;	/* Number of invalidations by the server */
	atomic_t		n_relpg;	/* Number of invalidations by releasepage */
	atomic_t		n_read_dir;	/* Number of directory pages read */
	atomic_t		n_dir_cr;	/* Number of directory entry creation edits */
	atomic_t		n_dir_rm;	/* Number of directory entry removal edits */
	atomic_t		n_stores;	/* Number of store ops */
	atomic_long_t		n_store_bytes;	/* Number of bytes stored */
	atomic_long_t		n_fetch_bytes;	/* Number of bytes fetched */
	atomic_t		n_fetches;	/* Number of data fetch ops */
};

extern const char afs_init_sysname[];

enum afs_cell_state {
	AFS_CELL_UNSET,
	AFS_CELL_ACTIVATING,
	AFS_CELL_ACTIVE,
	AFS_CELL_DEACTIVATING,
	AFS_CELL_INACTIVE,
	AFS_CELL_FAILED,
};

/*
 * AFS cell record.
 *
 * This is a tricky concept to get right as it is possible to create aliases
 * simply by pointing AFSDB/SRV records for two names at the same set of VL
 * servers; it is also possible to do things like setting up two sets of VL
 * servers, one of which provides a superset of the volumes provided by the
 * other (for internal/external division, for example).
 *
 * Cells only exist in the sense that (a) a cell's name maps to a set of VL
 * servers and (b) a cell's name is used by the client to select the key to use
 * for authentication and encryption.  The cell name is not typically used in
 * the protocol.
 *
 * There is no easy way to determine if two cells are aliases or one is a
 * subset of another.
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

	/* Active fileserver interaction state. */
	struct list_head	proc_volumes;	/* procfs volume list */
	rwlock_t		proc_lock;

	/* VL server list. */
	rwlock_t		vl_addrs_lock;	/* Lock on vl_addrs */
	struct afs_addr_list	__rcu *vl_addrs; /* List of VL servers */
	u8			name_len;	/* Length of name */
	char			name[64 + 1];	/* Cell name, case-flattened and NUL-padded */
};

/*
 * Cached VLDB entry.
 *
 * This is pointed to by cell->vldb_entries, indexed by name.
 */
struct afs_vldb_entry {
	afs_volid_t		vid[3];		/* Volume IDs for R/W, R/O and Bak volumes */

	unsigned long		flags;
#define AFS_VLDB_HAS_RW		0		/* - R/W volume exists */
#define AFS_VLDB_HAS_RO		1		/* - R/O volume exists */
#define AFS_VLDB_HAS_BAK	2		/* - Backup volume exists */
#define AFS_VLDB_QUERY_VALID	3		/* - Record is valid */
#define AFS_VLDB_QUERY_ERROR	4		/* - VL server returned error */

	uuid_t			fs_server[AFS_NMAXNSERVERS];
	u8			fs_mask[AFS_NMAXNSERVERS];
#define AFS_VOL_VTM_RW	0x01 /* R/W version of the volume is available (on this server) */
#define AFS_VOL_VTM_RO	0x02 /* R/O version of the volume is available (on this server) */
#define AFS_VOL_VTM_BAK	0x04 /* backup version of the volume is available (on this server) */
	short			error;
	u8			nr_servers;	/* Number of server records */
	u8			name_len;
	u8			name[AFS_MAXVOLNAME + 1]; /* NUL-padded volume name */
};

/*
 * Record of fileserver with which we're actively communicating.
 */
struct afs_server {
	struct rcu_head		rcu;
	union {
		uuid_t		uuid;		/* Server ID */
		struct afs_uuid	_uuid;
	};

	struct afs_addr_list	__rcu *addresses;
	struct rb_node		uuid_rb;	/* Link in net->servers */
	struct hlist_node	addr4_link;	/* Link in net->fs_addresses4 */
	struct hlist_node	addr6_link;	/* Link in net->fs_addresses6 */
	struct hlist_node	proc_link;	/* Link in net->fs_proc */
	struct afs_server	*gc_next;	/* Next server in manager's list */
	time64_t		put_time;	/* Time at which last put */
	time64_t		update_at;	/* Time at which to next update the record */
	unsigned long		flags;
#define AFS_SERVER_FL_NEW	0		/* New server, don't inc cb_s_break */
#define AFS_SERVER_FL_NOT_READY	1		/* The record is not ready for use */
#define AFS_SERVER_FL_NOT_FOUND	2		/* VL server says no such server */
#define AFS_SERVER_FL_VL_FAIL	3		/* Failed to access VL server */
#define AFS_SERVER_FL_UPDATING	4
#define AFS_SERVER_FL_PROBED	5		/* The fileserver has been probed */
#define AFS_SERVER_FL_PROBING	6		/* Fileserver is being probed */
#define AFS_SERVER_FL_NO_IBULK	7		/* Fileserver doesn't support FS.InlineBulkStatus */
#define AFS_SERVER_FL_MAY_HAVE_CB 8		/* May have callbacks on this fileserver */
	atomic_t		usage;
	u32			addr_version;	/* Address list version */

	/* file service access */
	rwlock_t		fs_lock;	/* access lock */

	/* callback promise management */
	struct hlist_head	cb_volumes;	/* List of volume interests on this server */
	unsigned		cb_s_break;	/* Break-everything counter. */
	rwlock_t		cb_break_lock;	/* Volume finding lock */
};

/*
 * Volume collation in the server's callback interest list.
 */
struct afs_vol_interest {
	struct hlist_node	srv_link;	/* Link in server->cb_volumes */
	struct hlist_head	cb_interests;	/* List of callback interests on the server */
	afs_volid_t		vid;		/* Volume ID to match */
	unsigned int		usage;
};

/*
 * Interest by a superblock on a server.
 */
struct afs_cb_interest {
	struct hlist_node	cb_vlink;	/* Link in vol_interest->cb_interests */
	struct afs_vol_interest	*vol_interest;
	struct afs_server	*server;	/* Server on which this interest resides */
	struct super_block	*sb;		/* Superblock on which inodes reside */
	afs_volid_t		vid;		/* Volume ID to match */
	refcount_t		usage;
};

/*
 * Replaceable server list.
 */
struct afs_server_entry {
	struct afs_server	*server;
	struct afs_cb_interest	*cb_interest;
};

struct afs_server_list {
	refcount_t		usage;
	unsigned short		nr_servers;
	unsigned short		index;		/* Server currently in use */
	unsigned short		vnovol_mask;	/* Servers to be skipped due to VNOVOL */
	unsigned int		seq;		/* Set to ->servers_seq when installed */
	rwlock_t		lock;
	struct afs_server_entry	servers[];
};

/*
 * Live AFS volume management.
 */
struct afs_volume {
	afs_volid_t		vid;		/* volume ID */
	atomic_t		usage;
	time64_t		update_at;	/* Time at which to next update */
	struct afs_cell		*cell;		/* Cell to which belongs (pins ref) */
	struct list_head	proc_link;	/* Link in cell->vl_proc */
	unsigned long		flags;
#define AFS_VOLUME_NEEDS_UPDATE	0	/* - T if an update needs performing */
#define AFS_VOLUME_UPDATING	1	/* - T if an update is in progress */
#define AFS_VOLUME_WAIT		2	/* - T if users must wait for update */
#define AFS_VOLUME_DELETED	3	/* - T if volume appears deleted */
#define AFS_VOLUME_OFFLINE	4	/* - T if volume offline notice given */
#define AFS_VOLUME_BUSY		5	/* - T if volume busy notice given */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif
	struct afs_server_list	*servers;	/* List of servers on which volume resides */
	rwlock_t		servers_lock;	/* Lock for ->servers */
	unsigned int		servers_seq;	/* Incremented each time ->servers changes */

	unsigned		cb_v_break;	/* Break-everything counter. */
	rwlock_t		cb_break_lock;

	afs_voltype_t		type;		/* type of volume */
	short			error;
	char			type_force;	/* force volume type (suppress R/O -> R/W) */
	u8			name_len;
	u8			name[AFS_MAXVOLNAME + 1]; /* NUL-padded volume name */
};

enum afs_lock_state {
	AFS_VNODE_LOCK_NONE,		/* The vnode has no lock on the server */
	AFS_VNODE_LOCK_WAITING_FOR_CB,	/* We're waiting for the server to break the callback */
	AFS_VNODE_LOCK_SETTING,		/* We're asking the server for a lock */
	AFS_VNODE_LOCK_GRANTED,		/* We have a lock on the server */
	AFS_VNODE_LOCK_EXTENDING,	/* We're extending a lock on the server */
	AFS_VNODE_LOCK_NEED_UNLOCK,	/* We need to unlock on the server */
	AFS_VNODE_LOCK_UNLOCKING,	/* We're telling the server to unlock */
};

/*
 * AFS inode private data.
 *
 * Note that afs_alloc_inode() *must* reset anything that could incorrectly
 * leak from one inode to another.
 */
struct afs_vnode {
	struct inode		vfs_inode;	/* the VFS's inode record */

	struct afs_volume	*volume;	/* volume on which vnode resides */
	struct afs_fid		fid;		/* the file identifier for this inode */
	struct afs_file_status	status;		/* AFS status info for this file */
	afs_dataversion_t	invalid_before;	/* Child dentries are invalid before this */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_cookie	*cache;		/* caching cookie */
#endif
	struct afs_permits __rcu *permit_cache;	/* cache of permits so far obtained */
	struct mutex		io_lock;	/* Lock for serialising I/O on this mutex */
	struct rw_semaphore	validate_lock;	/* lock for validating this vnode */
	spinlock_t		wb_lock;	/* lock for wb_keys */
	spinlock_t		lock;		/* waitqueue/flags lock */
	unsigned long		flags;
#define AFS_VNODE_CB_PROMISED	0		/* Set if vnode has a callback promise */
#define AFS_VNODE_UNSET		1		/* set if vnode attributes not yet set */
#define AFS_VNODE_DIR_VALID	2		/* Set if dir contents are valid */
#define AFS_VNODE_ZAP_DATA	3		/* set if vnode's data should be invalidated */
#define AFS_VNODE_DELETED	4		/* set if vnode deleted on server */
#define AFS_VNODE_MOUNTPOINT	5		/* set if vnode is a mountpoint symlink */
#define AFS_VNODE_AUTOCELL	6		/* set if Vnode is an auto mount point */
#define AFS_VNODE_PSEUDODIR	7 		/* set if Vnode is a pseudo directory */
#define AFS_VNODE_NEW_CONTENT	8		/* Set if file has new content (create/trunc-0) */

	struct list_head	wb_keys;	/* List of keys available for writeback */
	struct list_head	pending_locks;	/* locks waiting to be granted */
	struct list_head	granted_locks;	/* locks granted on this file */
	struct delayed_work	lock_work;	/* work to be done in locking */
	struct key		*lock_key;	/* Key to be used in lock ops */
	enum afs_lock_state	lock_state : 8;
	afs_lock_type_t		lock_type : 8;

	/* outstanding callback notification on this file */
	struct afs_cb_interest	*cb_interest;	/* Server on which this resides */
	unsigned int		cb_s_break;	/* Mass break counter on ->server */
	unsigned int		cb_v_break;	/* Mass break counter on ->volume */
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
	u32			abort_code;
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
	struct afs_vnode	*vnode;
	struct afs_server_list	*server_list;	/* Current server list (pins ref) */
	struct afs_cb_interest	*cbi;		/* Server on which this resides (pins ref) */
	struct key		*key;		/* Key for the server */
	unsigned int		cb_break;	/* cb_break + cb_s_break before the call */
	unsigned int		cb_break_2;	/* cb_break + cb_s_break (2nd vnode) */
	unsigned char		start;		/* Initial index in server list */
	unsigned char		index;		/* Number of servers tried beyond start */
	unsigned short		flags;
#define AFS_FS_CURSOR_STOP	0x0001		/* Set to cease iteration */
#define AFS_FS_CURSOR_VBUSY	0x0002		/* Set if seen VBUSY */
#define AFS_FS_CURSOR_VMOVED	0x0004		/* Set if seen VMOVED */
#define AFS_FS_CURSOR_VNOVOL	0x0008		/* Set if seen VNOVOL */
#define AFS_FS_CURSOR_CUR_ONLY	0x0010		/* Set if current server only (file lock held) */
#define AFS_FS_CURSOR_NO_VSLEEP	0x0020		/* Set to prevent sleep on VBUSY, VOFFLINE, ... */
};

/*
 * Cache auxiliary data.
 */
struct afs_vnode_cache_aux {
	u64			data_version;
} __packed;

#include <trace/events/afs.h>

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

extern void afs_merge_fs_addr4(struct afs_addr_list *, __be32, u16);
extern void afs_merge_fs_addr6(struct afs_addr_list *, __be32 *, u16);

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
extern void afs_break_callbacks(struct afs_server *, size_t, struct afs_callback_break*);

extern int afs_register_server_cb_interest(struct afs_vnode *,
					   struct afs_server_list *, unsigned int);
extern void afs_put_cb_interest(struct afs_net *, struct afs_cb_interest *);
extern void afs_clear_callback_interests(struct afs_net *, struct afs_server_list *);

static inline struct afs_cb_interest *afs_get_cb_interest(struct afs_cb_interest *cbi)
{
	if (cbi)
		refcount_inc(&cbi->usage);
	return cbi;
}

static inline unsigned int afs_calc_vnode_cb_break(struct afs_vnode *vnode)
{
	return vnode->cb_break + vnode->cb_s_break + vnode->cb_v_break;
}

static inline unsigned int afs_cb_break_sum(struct afs_vnode *vnode,
					    struct afs_cb_interest *cbi)
{
	return vnode->cb_break + cbi->server->cb_s_break + vnode->volume->cb_v_break;
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
extern const struct file_operations afs_dir_file_operations;
extern const struct inode_operations afs_dir_inode_operations;
extern const struct address_space_operations afs_dir_aops;
extern const struct dentry_operations afs_fs_dentry_operations;

extern void afs_d_release(struct dentry *);

/*
 * dir_edit.c
 */
extern void afs_edit_dir_add(struct afs_vnode *, struct qstr *, struct afs_fid *,
			     enum afs_edit_dir_reason);
extern void afs_edit_dir_remove(struct afs_vnode *, struct qstr *, enum afs_edit_dir_reason);

/*
 * dynroot.c
 */
extern const struct file_operations afs_dynroot_file_operations;
extern const struct inode_operations afs_dynroot_inode_operations;
extern const struct dentry_operations afs_dynroot_dentry_operations;

extern struct inode *afs_try_auto_mntpt(struct dentry *, struct inode *);
extern int afs_dynroot_mkdir(struct afs_net *, struct afs_cell *);
extern void afs_dynroot_rmdir(struct afs_net *, struct afs_cell *);
extern int afs_dynroot_populate(struct super_block *);
extern void afs_dynroot_depopulate(struct super_block *);

/*
 * file.c
 */
extern const struct address_space_operations afs_fs_aops;
extern const struct inode_operations afs_file_inode_operations;
extern const struct file_operations afs_file_operations;

extern int afs_cache_wb_key(struct afs_vnode *, struct afs_file *);
extern void afs_put_wb_key(struct afs_wb_key *);
extern int afs_open(struct inode *, struct file *);
extern int afs_release(struct inode *, struct file *);
extern int afs_fetch_data(struct afs_vnode *, struct key *, struct afs_read *);
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
#define AFS_VNODE_NOT_YET_SET	0x01
#define AFS_VNODE_META_CHANGED	0x02
#define AFS_VNODE_DATA_CHANGED	0x04
extern void afs_update_inode_from_status(struct afs_vnode *, struct afs_file_status *,
					 const afs_dataversion_t *, u8);

extern int afs_fs_fetch_file_status(struct afs_fs_cursor *, struct afs_volsync *, bool);
extern int afs_fs_give_up_callbacks(struct afs_net *, struct afs_server *);
extern int afs_fs_fetch_data(struct afs_fs_cursor *, struct afs_read *);
extern int afs_fs_create(struct afs_fs_cursor *, const char *, umode_t, u64,
			 struct afs_fid *, struct afs_file_status *, struct afs_callback *);
extern int afs_fs_remove(struct afs_fs_cursor *, const char *, bool, u64);
extern int afs_fs_link(struct afs_fs_cursor *, struct afs_vnode *, const char *, u64);
extern int afs_fs_symlink(struct afs_fs_cursor *, const char *, const char *, u64,
			  struct afs_fid *, struct afs_file_status *);
extern int afs_fs_rename(struct afs_fs_cursor *, const char *,
			 struct afs_vnode *, const char *, u64, u64);
extern int afs_fs_store_data(struct afs_fs_cursor *, struct address_space *,
			     pgoff_t, pgoff_t, unsigned, unsigned);
extern int afs_fs_setattr(struct afs_fs_cursor *, struct iattr *);
extern int afs_fs_get_volume_status(struct afs_fs_cursor *, struct afs_volume_status *);
extern int afs_fs_set_lock(struct afs_fs_cursor *, afs_lock_type_t);
extern int afs_fs_extend_lock(struct afs_fs_cursor *);
extern int afs_fs_release_lock(struct afs_fs_cursor *);
extern int afs_fs_give_up_all_callbacks(struct afs_net *, struct afs_server *,
					struct afs_addr_cursor *, struct key *);
extern int afs_fs_get_capabilities(struct afs_net *, struct afs_server *,
				   struct afs_addr_cursor *, struct key *);
extern int afs_fs_inline_bulk_status(struct afs_fs_cursor *, struct afs_net *,
				     struct afs_fid *, struct afs_file_status *,
				     struct afs_callback *, unsigned int,
				     struct afs_volsync *);
extern int afs_fs_fetch_status(struct afs_fs_cursor *, struct afs_net *,
			       struct afs_fid *, struct afs_file_status *,
			       struct afs_callback *, struct afs_volsync *);

/*
 * inode.c
 */
extern int afs_fetch_status(struct afs_vnode *, struct key *, bool);
extern int afs_iget5_test(struct inode *, void *);
extern struct inode *afs_iget_pseudo_dir(struct super_block *, bool);
extern struct inode *afs_iget(struct super_block *, struct key *,
			      struct afs_fid *, struct afs_file_status *,
			      struct afs_callback *,
			      struct afs_cb_interest *);
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
extern int afs_net_id;

static inline struct afs_net *afs_net(struct net *net)
{
	return net_generic(net, afs_net_id);
}

static inline struct afs_net *afs_sb2net(struct super_block *sb)
{
	return afs_net(AFS_FS_S(sb)->net_ns);
}

static inline struct afs_net *afs_d2net(struct dentry *dentry)
{
	return afs_sb2net(dentry->d_sb);
}

static inline struct afs_net *afs_i2net(struct inode *inode)
{
	return afs_sb2net(inode->i_sb);
}

static inline struct afs_net *afs_v2net(struct afs_vnode *vnode)
{
	return afs_i2net(&vnode->vfs_inode);
}

static inline struct afs_net *afs_sock2net(struct sock *sk)
{
	return net_generic(sock_net(sk), afs_net_id);
}

static inline void __afs_stat(atomic_t *s)
{
	atomic_inc(s);
}

#define afs_stat_v(vnode, n) __afs_stat(&afs_v2net(vnode)->n)

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
extern int afs_get_ipv4_interfaces(struct afs_net *, struct afs_interface *,
				   size_t, bool);

/*
 * proc.c
 */
#ifdef CONFIG_PROC_FS
extern int __net_init afs_proc_init(struct afs_net *);
extern void __net_exit afs_proc_cleanup(struct afs_net *);
extern int afs_proc_cell_setup(struct afs_cell *);
extern void afs_proc_cell_remove(struct afs_cell *);
extern void afs_put_sysnames(struct afs_sysnames *);
#else
static inline int afs_proc_init(struct afs_net *net) { return 0; }
static inline void afs_proc_cleanup(struct afs_net *net) {}
static inline int afs_proc_cell_setup(struct afs_cell *cell) { return 0; }
static inline void afs_proc_cell_remove(struct afs_cell *cell) {}
static inline void afs_put_sysnames(struct afs_sysnames *sysnames) {}
#endif

/*
 * rotate.c
 */
extern bool afs_begin_vnode_operation(struct afs_fs_cursor *, struct afs_vnode *,
				      struct key *);
extern bool afs_select_fileserver(struct afs_fs_cursor *);
extern bool afs_select_current_fileserver(struct afs_fs_cursor *);
extern int afs_end_vnode_operation(struct afs_fs_cursor *);

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
extern int afs_protocol_error(struct afs_call *, int);

static inline int afs_transfer_reply(struct afs_call *call)
{
	return afs_extract_data(call, call->buffer, call->reply_max, false);
}

static inline bool afs_check_call_state(struct afs_call *call,
					enum afs_call_state state)
{
	return READ_ONCE(call->state) == state;
}

static inline bool afs_set_call_state(struct afs_call *call,
				      enum afs_call_state from,
				      enum afs_call_state to)
{
	bool ok = false;

	spin_lock_bh(&call->state_lock);
	if (call->state == from) {
		call->state = to;
		trace_afs_call_state(call, from, to, 0, 0);
		ok = true;
	}
	spin_unlock_bh(&call->state_lock);
	return ok;
}

static inline void afs_set_call_complete(struct afs_call *call,
					 int error, u32 remote_abort)
{
	enum afs_call_state state;
	bool ok = false;

	spin_lock_bh(&call->state_lock);
	state = call->state;
	if (state != AFS_CALL_COMPLETE) {
		call->abort_code = remote_abort;
		call->error = error;
		call->state = AFS_CALL_COMPLETE;
		trace_afs_call_state(call, state, AFS_CALL_COMPLETE,
				     error, remote_abort);
		ok = true;
	}
	spin_unlock_bh(&call->state_lock);
	if (ok)
		trace_afs_call_done(call);
}

/*
 * security.c
 */
extern void afs_put_permits(struct afs_permits *);
extern void afs_clear_permits(struct afs_vnode *);
extern void afs_cache_permit(struct afs_vnode *, struct key *, unsigned int);
extern void afs_zap_permits(struct rcu_head *);
extern struct key *afs_request_key(struct afs_cell *);
extern int afs_check_permit(struct afs_vnode *, struct key *, afs_access_t *);
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

extern struct afs_server *afs_find_server(struct afs_net *,
					  const struct sockaddr_rxrpc *);
extern struct afs_server *afs_find_server_by_uuid(struct afs_net *, const uuid_t *);
extern struct afs_server *afs_lookup_server(struct afs_cell *, struct key *, const uuid_t *);
extern void afs_put_server(struct afs_net *, struct afs_server *);
extern void afs_manage_servers(struct work_struct *);
extern void afs_servers_timer(struct timer_list *);
extern void __net_exit afs_purge_servers(struct afs_net *);
extern bool afs_probe_fileserver(struct afs_fs_cursor *);
extern bool afs_check_server_record(struct afs_fs_cursor *, struct afs_server *);

/*
 * server_list.c
 */
static inline struct afs_server_list *afs_get_serverlist(struct afs_server_list *slist)
{
	refcount_inc(&slist->usage);
	return slist;
}

extern void afs_put_serverlist(struct afs_net *, struct afs_server_list *);
extern struct afs_server_list *afs_alloc_server_list(struct afs_cell *, struct key *,
						     struct afs_vldb_entry *,
						     u8);
extern bool afs_annotate_server_list(struct afs_server_list *, struct afs_server_list *);

/*
 * super.c
 */
extern int __init afs_fs_init(void);
extern void afs_fs_exit(void);

/*
 * vlclient.c
 */
extern struct afs_vldb_entry *afs_vl_get_entry_by_name_u(struct afs_net *,
							 struct afs_addr_cursor *,
							 struct key *, const char *, int);
extern struct afs_addr_list *afs_vl_get_addrs_u(struct afs_net *, struct afs_addr_cursor *,
						struct key *, const uuid_t *);
extern int afs_vl_get_capabilities(struct afs_net *, struct afs_addr_cursor *, struct key *);
extern struct afs_addr_list *afs_yfsvl_get_endpoints(struct afs_net *, struct afs_addr_cursor *,
						     struct key *, const uuid_t *);

/*
 * volume.c
 */
static inline struct afs_volume *__afs_get_volume(struct afs_volume *volume)
{
	if (volume)
		atomic_inc(&volume->usage);
	return volume;
}

extern struct afs_volume *afs_create_volume(struct afs_mount_params *);
extern void afs_activate_volume(struct afs_volume *);
extern void afs_deactivate_volume(struct afs_volume *);
extern void afs_put_volume(struct afs_cell *, struct afs_volume *);
extern int afs_check_volume_status(struct afs_volume *, struct key *);

/*
 * write.c
 */
extern int afs_set_page_dirty(struct page *);
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
extern int afs_fsync(struct file *, loff_t, loff_t, int);
extern int afs_page_mkwrite(struct vm_fault *);
extern void afs_prune_wb_keys(struct afs_vnode *);
extern int afs_launder_page(struct page *);

/*
 * xattr.c
 */
extern const struct xattr_handler *afs_xattr_handlers[];
extern ssize_t afs_listxattr(struct dentry *, char *, size_t);


/*
 * Miscellaneous inline functions.
 */
static inline struct afs_vnode *AFS_FS_I(struct inode *inode)
{
	return container_of(inode, struct afs_vnode, vfs_inode);
}

static inline struct inode *AFS_VNODE_TO_I(struct afs_vnode *vnode)
{
	return &vnode->vfs_inode;
}

static inline void afs_vnode_commit_status(struct afs_fs_cursor *fc,
					   struct afs_vnode *vnode,
					   unsigned int cb_break)
{
	if (fc->ac.error == 0)
		afs_cache_permit(vnode, fc->key, cb_break);
}

static inline void afs_check_for_remote_deletion(struct afs_fs_cursor *fc,
						 struct afs_vnode *vnode)
{
	if (fc->ac.error == -ENOENT) {
		set_bit(AFS_VNODE_DELETED, &vnode->flags);
		afs_break_callback(vnode);
	}
}


/*****************************************************************************/
/*
 * debug tracing
 */
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
