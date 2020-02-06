/* SPDX-License-Identifier: GPL-2.0-or-later */
/* internal AFS stuff
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
#include <linux/mm_types.h>
#include <linux/dns_resolver.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>

#include "afs.h"
#include "afs_vl.h"

#define AFS_CELL_MAX_ADDRS 15

struct pagevec;
struct afs_call;
struct afs_vnode;

/*
 * Partial file-locking emulation mode.  (The problem being that AFS3 only
 * allows whole-file locks and no upgrading/downgrading).
 */
enum afs_flock_mode {
	afs_flock_mode_unset,
	afs_flock_mode_local,	/* Local locking only */
	afs_flock_mode_openafs,	/* Don't get server lock for a partial lock */
	afs_flock_mode_strict,	/* Always get a server lock for a partial lock */
	afs_flock_mode_write,	/* Get an exclusive server lock for a partial lock */
};

struct afs_fs_context {
	bool			force;		/* T to force cell type */
	bool			autocell;	/* T if set auto mount operation */
	bool			dyn_root;	/* T if dynamic root */
	bool			no_cell;	/* T if the source is "none" (for dynroot) */
	enum afs_flock_mode	flock_mode;	/* Partial file-locking emulation mode */
	afs_voltype_t		type;		/* type of volume requested */
	unsigned int		volnamesz;	/* size of volume name */
	const char		*volname;	/* name of volume to mount */
	struct afs_net		*net;		/* the AFS net namespace stuff */
	struct afs_cell		*cell;		/* cell in which to find volume */
	struct afs_volume	*volume;	/* volume record */
	struct key		*key;		/* key to use for secure mounting */
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
	struct rcu_head		rcu;
	refcount_t		usage;
	u32			version;	/* Version */
	unsigned char		max_addrs;
	unsigned char		nr_addrs;
	unsigned char		preferred;	/* Preferred address */
	unsigned char		nr_ipv4;	/* Number of IPv4 addresses */
	enum dns_record_source	source:8;
	enum dns_lookup_status	status:8;
	unsigned long		failed;		/* Mask of addrs that failed locally/ICMP */
	unsigned long		responded;	/* Mask of addrs that responded */
	struct sockaddr_rxrpc	addrs[];
#define AFS_MAX_ADDRESSES ((unsigned int)(sizeof(unsigned long) * 8))
};

/*
 * a record of an in-progress RxRPC call
 */
struct afs_call {
	const struct afs_call_type *type;	/* type of call */
	struct afs_addr_list	*alist;		/* Address is alist[addr_ix] */
	wait_queue_head_t	waitq;		/* processes awaiting completion */
	struct work_struct	async_work;	/* async I/O processor */
	struct work_struct	work;		/* actual work processor */
	struct rxrpc_call	*rxcall;	/* RxRPC call handle */
	struct key		*key;		/* security for this call */
	struct afs_net		*net;		/* The network namespace */
	struct afs_server	*server;	/* The fileserver record if fs op (pins ref) */
	struct afs_vlserver	*vlserver;	/* The vlserver record if vl op */
	void			*request;	/* request data (first part) */
	size_t			iov_len;	/* Size of *iter to be used */
	struct iov_iter		def_iter;	/* Default buffer/data iterator */
	struct iov_iter		*write_iter;	/* Iterator defining write to be made */
	struct iov_iter		*iter;		/* Iterator currently in use */
	union {	/* Convenience for ->def_iter */
		struct kvec	kvec[1];
		struct bio_vec	bvec[1];
	};
	void			*buffer;	/* reply receive buffer */
	union {
		long			ret0;	/* Value to reply with instead of 0 */
		struct afs_addr_list	*ret_alist;
		struct afs_vldb_entry	*ret_vldb;
		char			*ret_str;
	};
	struct afs_operation	*op;
	unsigned int		server_index;
	atomic_t		usage;
	enum afs_call_state	state;
	spinlock_t		state_lock;
	int			error;		/* error code */
	u32			abort_code;	/* Remote abort ID or 0 */
	unsigned int		max_lifespan;	/* Maximum lifespan to set if not 0 */
	unsigned		request_size;	/* size of request data */
	unsigned		reply_max;	/* maximum size of reply */
	unsigned		count2;		/* count used in unmarshalling */
	unsigned char		unmarshall;	/* unmarshalling phase */
	unsigned char		addr_ix;	/* Address in ->alist */
	bool			drop_ref;	/* T if need to drop ref for incoming call */
	bool			need_attention;	/* T if RxRPC poked us */
	bool			async;		/* T if asynchronous */
	bool			upgrade;	/* T to request service upgrade */
	bool			have_reply_time; /* T if have got reply_time */
	bool			intr;		/* T if interruptible */
	bool			unmarshalling_error; /* T if an unmarshalling error occurred */
	u16			service_id;	/* Actual service ID (after upgrade) */
	unsigned int		debug_id;	/* Trace ID */
	u32			operation_ID;	/* operation ID for an incoming call */
	u32			count;		/* count for use in unmarshalling */
	union {					/* place to extract temporary data */
		struct {
			__be32	tmp_u;
			__be32	tmp;
		} __attribute__((packed));
		__be64		tmp64;
	};
	ktime_t			reply_time;	/* Time of first reply packet */
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

	/* Call done function (gets called immediately on success or failure) */
	void (*done)(struct afs_call *call);
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
	loff_t			file_size;	/* File size returned by server */
	struct key		*key;		/* The key to use to reissue the read */
	struct afs_vnode	*vnode;		/* The file being read into. */
	struct netfs_read_subrequest *subreq;	/* Fscache helper read request this belongs to */
	afs_dataversion_t	data_version;	/* Version number returned by server */
	refcount_t		usage;
	unsigned int		call_debug_id;
	unsigned int		nr_pages;
	int			error;
	void (*done)(struct afs_read *);
	void (*cleanup)(struct afs_read *);
	struct iov_iter		*iter;		/* Iterator representing the buffer */
	struct iov_iter		def_iter;	/* Default iterator */
};

/*
 * AFS superblock private data
 * - there's one superblock per volume
 */
struct afs_super_info {
	struct net		*net_ns;	/* Network namespace */
	struct afs_cell		*cell;		/* The cell in which the volume resides */
	struct afs_volume	*volume;	/* volume record */
	enum afs_flock_mode	flock_mode:8;	/* File locking emulation mode */
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
	struct afs_cell		*ws_cell;
	struct work_struct	cells_manager;
	struct timer_list	cells_timer;
	atomic_t		cells_outstanding;
	struct rw_semaphore	cells_lock;
	struct mutex		cells_alias_lock;

	struct mutex		proc_cells_lock;
	struct hlist_head	proc_cells;

	/* Known servers.  Theoretically each fileserver can only be in one
	 * cell, but in practice, people create aliases and subsets and there's
	 * no easy way to distinguish them.
	 */
	seqlock_t		fs_lock;	/* For fs_servers, fs_probe_*, fs_proc */
	struct rb_root		fs_servers;	/* afs_server (by server UUID or address) */
	struct list_head	fs_probe_fast;	/* List of afs_server to probe at 30s intervals */
	struct list_head	fs_probe_slow;	/* List of afs_server to probe at 5m intervals */
	struct hlist_head	fs_proc;	/* procfs servers list */

	struct hlist_head	fs_addresses4;	/* afs_server (by lowest IPv4 addr) */
	struct hlist_head	fs_addresses6;	/* afs_server (by lowest IPv6 addr) */
	seqlock_t		fs_addr_lock;	/* For fs_addresses[46] */

	struct work_struct	fs_manager;
	struct timer_list	fs_timer;

	struct work_struct	fs_prober;
	struct timer_list	fs_probe_timer;
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
	AFS_CELL_REMOVED,
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
 * Two cells are determined to be aliases if they have an explicit alias (YFS
 * only), share any VL servers in common or have at least one volume in common.
 * "In common" means that the address list of the VL servers or the fileservers
 * share at least one endpoint.
 */
struct afs_cell {
	union {
		struct rcu_head	rcu;
		struct rb_node	net_node;	/* Node in net->cells */
	};
	struct afs_net		*net;
	struct afs_cell		*alias_of;	/* The cell this is an alias of */
	struct afs_volume	*root_volume;	/* The root.cell volume if there is one */
	struct key		*anonymous_key;	/* anonymous user key for this cell */
	struct work_struct	manager;	/* Manager for init/deinit/dns */
	struct hlist_node	proc_link;	/* /proc cell list link */
	time64_t		dns_expiry;	/* Time AFSDB/SRV record expires */
	time64_t		last_inactive;	/* Time of last drop of usage count */
	atomic_t		ref;		/* Struct refcount */
	atomic_t		active;		/* Active usage counter */
	unsigned long		flags;
#define AFS_CELL_FL_NO_GC	0		/* The cell was added manually, don't auto-gc */
#define AFS_CELL_FL_DO_LOOKUP	1		/* DNS lookup requested */
#define AFS_CELL_FL_CHECK_ALIAS	2		/* Need to check for aliases */
	enum afs_cell_state	state;
	short			error;
	enum dns_record_source	dns_source:8;	/* Latest source of data from lookup */
	enum dns_lookup_status	dns_status:8;	/* Latest status of data from lookup */
	unsigned int		dns_lookup_count; /* Counter of DNS lookups */
	unsigned int		debug_id;

	/* The volumes belonging to this cell */
	struct rb_root		volumes;	/* Tree of volumes on this server */
	struct hlist_head	proc_volumes;	/* procfs volume list */
	seqlock_t		volume_lock;	/* For volumes */

	/* Active fileserver interaction state. */
	struct rb_root		fs_servers;	/* afs_server (by server UUID) */
	seqlock_t		fs_lock;	/* For fs_servers  */
	struct rw_semaphore	fs_open_mmaps_lock;
	struct list_head	fs_open_mmaps;	/* List of vnodes that are mmapped */
	atomic_t		fs_s_break;	/* Counter of CB.InitCallBackState messages */

	/* VL server list. */
	rwlock_t		vl_servers_lock; /* Lock on vl_servers */
	struct afs_vlserver_list __rcu *vl_servers;

	u8			name_len;	/* Length of name */
	char			*name;		/* Cell name, case-flattened and NUL-padded */
};

/*
 * Volume Location server record.
 */
struct afs_vlserver {
	struct rcu_head		rcu;
	struct afs_addr_list	__rcu *addresses; /* List of addresses for this VL server */
	unsigned long		flags;
#define AFS_VLSERVER_FL_PROBED	0		/* The VL server has been probed */
#define AFS_VLSERVER_FL_PROBING	1		/* VL server is being probed */
#define AFS_VLSERVER_FL_IS_YFS	2		/* Server is YFS not AFS */
#define AFS_VLSERVER_FL_RESPONDING 3		/* VL server is responding */
	rwlock_t		lock;		/* Lock on addresses */
	atomic_t		usage;
	unsigned int		rtt;		/* Server's current RTT in uS */

	/* Probe state */
	wait_queue_head_t	probe_wq;
	atomic_t		probe_outstanding;
	spinlock_t		probe_lock;
	struct {
		unsigned int	rtt;		/* RTT in uS */
		u32		abort_code;
		short		error;
		unsigned short	flags;
#define AFS_VLSERVER_PROBE_RESPONDED		0x01 /* At least once response (may be abort) */
#define AFS_VLSERVER_PROBE_IS_YFS		0x02 /* The peer appears to be YFS */
#define AFS_VLSERVER_PROBE_NOT_YFS		0x04 /* The peer appears not to be YFS */
#define AFS_VLSERVER_PROBE_LOCAL_FAILURE	0x08 /* A local failure prevented a probe */
	} probe;

	u16			port;
	u16			name_len;	/* Length of name */
	char			name[];		/* Server name, case-flattened */
};

/*
 * Weighted list of Volume Location servers.
 */
struct afs_vlserver_entry {
	u16			priority;	/* Preference (as SRV) */
	u16			weight;		/* Weight (as SRV) */
	enum dns_record_source	source:8;
	enum dns_lookup_status	status:8;
	struct afs_vlserver	*server;
};

struct afs_vlserver_list {
	struct rcu_head		rcu;
	atomic_t		usage;
	u8			nr_servers;
	u8			index;		/* Server currently in use */
	u8			preferred;	/* Preferred server */
	enum dns_record_source	source:8;
	enum dns_lookup_status	status:8;
	rwlock_t		lock;
	struct afs_vlserver_entry servers[];
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
	u32			addr_version[AFS_NMAXNSERVERS]; /* Registration change counters */
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
	struct afs_cell		*cell;		/* Cell to which belongs (pins ref) */
	struct rb_node		uuid_rb;	/* Link in net->fs_servers */
	struct afs_server __rcu	*uuid_next;	/* Next server with same UUID */
	struct afs_server	*uuid_prev;	/* Previous server with same UUID */
	struct list_head	probe_link;	/* Link in net->fs_probe_list */
	struct hlist_node	addr4_link;	/* Link in net->fs_addresses4 */
	struct hlist_node	addr6_link;	/* Link in net->fs_addresses6 */
	struct hlist_node	proc_link;	/* Link in net->fs_proc */
	struct work_struct	initcb_work;	/* Work for CB.InitCallBackState* */
	struct afs_server	*gc_next;	/* Next server in manager's list */
	time64_t		unuse_time;	/* Time at which last unused */
	unsigned long		flags;
#define AFS_SERVER_FL_RESPONDING 0		/* The server is responding */
#define AFS_SERVER_FL_UPDATING	1
#define AFS_SERVER_FL_NEEDS_UPDATE 2		/* Fileserver address list is out of date */
#define AFS_SERVER_FL_NOT_READY	4		/* The record is not ready for use */
#define AFS_SERVER_FL_NOT_FOUND	5		/* VL server says no such server */
#define AFS_SERVER_FL_VL_FAIL	6		/* Failed to access VL server */
#define AFS_SERVER_FL_MAY_HAVE_CB 8		/* May have callbacks on this fileserver */
#define AFS_SERVER_FL_IS_YFS	16		/* Server is YFS not AFS */
#define AFS_SERVER_FL_NO_IBULK	17		/* Fileserver doesn't support FS.InlineBulkStatus */
#define AFS_SERVER_FL_NO_RM2	18		/* Fileserver doesn't support YFS.RemoveFile2 */
#define AFS_SERVER_FL_HAS_FS64	19		/* Fileserver supports FS.{Fetch,Store}Data64 */
	atomic_t		ref;		/* Object refcount */
	atomic_t		active;		/* Active user count */
	u32			addr_version;	/* Address list version */
	unsigned int		rtt;		/* Server's current RTT in uS */
	unsigned int		debug_id;	/* Debugging ID for traces */

	/* file service access */
	rwlock_t		fs_lock;	/* access lock */

	/* callback promise management */
	unsigned		cb_s_break;	/* Break-everything counter. */

	/* Probe state */
	unsigned long		probed_at;	/* Time last probe was dispatched (jiffies) */
	wait_queue_head_t	probe_wq;
	atomic_t		probe_outstanding;
	spinlock_t		probe_lock;
	struct {
		unsigned int	rtt;		/* RTT in uS */
		u32		abort_code;
		short		error;
		bool		responded:1;
		bool		is_yfs:1;
		bool		not_yfs:1;
		bool		local_failure:1;
	} probe;
};

/*
 * Replaceable volume server list.
 */
struct afs_server_entry {
	struct afs_server	*server;
};

struct afs_server_list {
	afs_volid_t		vids[AFS_MAXTYPES]; /* Volume IDs */
	refcount_t		usage;
	unsigned char		nr_servers;
	unsigned char		preferred;	/* Preferred server */
	unsigned short		vnovol_mask;	/* Servers to be skipped due to VNOVOL */
	unsigned int		seq;		/* Set to ->servers_seq when installed */
	rwlock_t		lock;
	struct afs_server_entry	servers[];
};

/*
 * Live AFS volume management.
 */
struct afs_volume {
	union {
		struct rcu_head	rcu;
		afs_volid_t	vid;		/* volume ID */
	};
	atomic_t		usage;
	time64_t		update_at;	/* Time at which to next update */
	struct afs_cell		*cell;		/* Cell to which belongs (pins ref) */
	struct rb_node		cell_node;	/* Link in cell->volumes */
	struct hlist_node	proc_link;	/* Link in cell->proc_volumes */
	struct super_block __rcu *sb;		/* Superblock on which inodes reside */
	unsigned long		flags;
#define AFS_VOLUME_NEEDS_UPDATE	0	/* - T if an update needs performing */
#define AFS_VOLUME_UPDATING	1	/* - T if an update is in progress */
#define AFS_VOLUME_WAIT		2	/* - T if users must wait for update */
#define AFS_VOLUME_DELETED	3	/* - T if volume appears deleted */
#define AFS_VOLUME_OFFLINE	4	/* - T if volume offline notice given */
#define AFS_VOLUME_BUSY		5	/* - T if volume busy notice given */
#define AFS_VOLUME_MAYBE_NO_IBULK 6	/* - T if some servers don't have InlineBulkStatus */
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_volume	*cache;		/* Caching cookie */
#endif
	struct afs_server_list __rcu *servers;	/* List of servers on which volume resides */
	rwlock_t		servers_lock;	/* Lock for ->servers */
	unsigned int		servers_seq;	/* Incremented each time ->servers changes */

	unsigned		cb_v_break;	/* Break-everything counter. */
	rwlock_t		cb_v_break_lock;

	afs_voltype_t		type;		/* type of volume */
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
	AFS_VNODE_LOCK_DELETED,		/* The vnode has been deleted whilst we have a lock */
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
	struct rw_semaphore	rmdir_lock;	/* Lock for rmdir vs sillyrename */
	struct key		*silly_key;	/* Silly rename key */
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
#define AFS_VNODE_SILLY_DELETED	9		/* Set if file has been silly-deleted */
#define AFS_VNODE_MODIFYING	10		/* Set if we're performing a modification op */

	struct list_head	wb_keys;	/* List of keys available for writeback */
	struct list_head	pending_locks;	/* locks waiting to be granted */
	struct list_head	granted_locks;	/* locks granted on this file */
	struct delayed_work	lock_work;	/* work to be done in locking */
	struct key		*lock_key;	/* Key to be used in lock ops */
	ktime_t			locked_at;	/* Time at which lock obtained */
	enum afs_lock_state	lock_state : 8;
	afs_lock_type_t		lock_type : 8;

	/* outstanding callback notification on this file */
	struct work_struct	cb_work;	/* Work for mmap'd files */
	struct list_head	cb_mmap_link;	/* Link in cell->fs_open_mmaps */
	void			*cb_server;	/* Server with callback/filelock */
	atomic_t		cb_nr_mmap;	/* Number of mmaps */
	unsigned int		cb_fs_s_break;	/* Mass server break counter (cell->fs_s_break) */
	unsigned int		cb_s_break;	/* Mass break counter on ->server */
	unsigned int		cb_v_break;	/* Mass break counter on ->volume */
	unsigned int		cb_break;	/* Break counter on vnode */
	seqlock_t		cb_lock;	/* Lock for ->cb_server, ->status, ->cb_*break */

	time64_t		cb_expires_at;	/* time at which callback expires */
};

static inline struct fscache_cookie *afs_vnode_cache(struct afs_vnode *vnode)
{
#ifdef CONFIG_AFS_FSCACHE
	return vnode->cache;
#else
	return NULL;
#endif
}

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
 * Error prioritisation and accumulation.
 */
struct afs_error {
	short	error;			/* Accumulated error */
	bool	responded;		/* T if server responded */
};

/*
 * Cursor for iterating over a server's address list.
 */
struct afs_addr_cursor {
	struct afs_addr_list	*alist;		/* Current address list (pins ref) */
	unsigned long		tried;		/* Tried addresses */
	signed char		index;		/* Current address */
	bool			responded;	/* T if the current address responded */
	unsigned short		nr_iterations;	/* Number of address iterations */
	short			error;
	u32			abort_code;
};

/*
 * Cursor for iterating over a set of volume location servers.
 */
struct afs_vl_cursor {
	struct afs_addr_cursor	ac;
	struct afs_cell		*cell;		/* The cell we're querying */
	struct afs_vlserver_list *server_list;	/* Current server list (pins ref) */
	struct afs_vlserver	*server;	/* Server on which this resides */
	struct key		*key;		/* Key for the server */
	unsigned long		untried;	/* Bitmask of untried servers */
	short			index;		/* Current server */
	short			error;
	unsigned short		flags;
#define AFS_VL_CURSOR_STOP	0x0001		/* Set to cease iteration */
#define AFS_VL_CURSOR_RETRY	0x0002		/* Set to do a retry */
#define AFS_VL_CURSOR_RETRIED	0x0004		/* Set if started a retry */
	unsigned short		nr_iterations;	/* Number of server iterations */
};

/*
 * Fileserver operation methods.
 */
struct afs_operation_ops {
	void (*issue_afs_rpc)(struct afs_operation *op);
	void (*issue_yfs_rpc)(struct afs_operation *op);
	void (*success)(struct afs_operation *op);
	void (*aborted)(struct afs_operation *op);
	void (*failed)(struct afs_operation *op);
	void (*edit_dir)(struct afs_operation *op);
	void (*put)(struct afs_operation *op);
};

struct afs_vnode_param {
	struct afs_vnode	*vnode;
	struct afs_fid		fid;		/* Fid to access */
	struct afs_status_cb	scb;		/* Returned status and callback promise */
	afs_dataversion_t	dv_before;	/* Data version before the call */
	unsigned int		cb_break_before; /* cb_break + cb_s_break before the call */
	u8			dv_delta;	/* Expected change in data version */
	bool			put_vnode:1;	/* T if we have a ref on the vnode */
	bool			need_io_lock:1;	/* T if we need the I/O lock on this */
	bool			update_ctime:1;	/* Need to update the ctime */
	bool			set_size:1;	/* Must update i_size */
	bool			op_unlinked:1;	/* True if file was unlinked by op */
	bool			speculative:1;	/* T if speculative status fetch (no vnode lock) */
	bool			modification:1;	/* Set if the content gets modified */
};

/*
 * Fileserver operation wrapper, handling server and address rotation
 * asynchronously.  May make simultaneous calls to multiple servers.
 */
struct afs_operation {
	struct afs_net		*net;		/* Network namespace */
	struct key		*key;		/* Key for the cell */
	const struct afs_call_type *type;	/* Type of call done */
	const struct afs_operation_ops *ops;

	/* Parameters/results for the operation */
	struct afs_volume	*volume;	/* Volume being accessed */
	struct afs_vnode_param	file[2];
	struct afs_vnode_param	*more_files;
	struct afs_volsync	volsync;
	struct dentry		*dentry;	/* Dentry to be altered */
	struct dentry		*dentry_2;	/* Second dentry to be altered */
	struct timespec64	mtime;		/* Modification time to record */
	struct timespec64	ctime;		/* Change time to set */
	short			nr_files;	/* Number of entries in file[], more_files */
	short			error;
	unsigned int		debug_id;

	unsigned int		cb_v_break;	/* Volume break counter before op */
	unsigned int		cb_s_break;	/* Server break counter before op */

	union {
		struct {
			int	which;		/* Which ->file[] to fetch for */
		} fetch_status;
		struct {
			int	reason;		/* enum afs_edit_dir_reason */
			mode_t	mode;
			const char *symlink;
		} create;
		struct {
			bool	need_rehash;
		} unlink;
		struct {
			struct dentry *rehash;
			struct dentry *tmp;
			bool	new_negative;
		} rename;
		struct {
			struct afs_read *req;
		} fetch;
		struct {
			afs_lock_type_t type;
		} lock;
		struct {
			struct iov_iter	*write_iter;
			loff_t	pos;
			loff_t	size;
			loff_t	i_size;
			bool	laundering;	/* Laundering page, PG_writeback not set */
		} store;
		struct {
			struct iattr	*attr;
			loff_t		old_i_size;
		} setattr;
		struct afs_acl	*acl;
		struct yfs_acl	*yacl;
		struct {
			struct afs_volume_status vs;
			struct kstatfs		*buf;
		} volstatus;
	};

	/* Fileserver iteration state */
	struct afs_addr_cursor	ac;
	struct afs_server_list	*server_list;	/* Current server list (pins ref) */
	struct afs_server	*server;	/* Server we're using (ref pinned by server_list) */
	struct afs_call		*call;
	unsigned long		untried;	/* Bitmask of untried servers */
	short			index;		/* Current server */
	unsigned short		nr_iterations;	/* Number of server iterations */

	unsigned int		flags;
#define AFS_OPERATION_STOP		0x0001	/* Set to cease iteration */
#define AFS_OPERATION_VBUSY		0x0002	/* Set if seen VBUSY */
#define AFS_OPERATION_VMOVED		0x0004	/* Set if seen VMOVED */
#define AFS_OPERATION_VNOVOL		0x0008	/* Set if seen VNOVOL */
#define AFS_OPERATION_CUR_ONLY		0x0010	/* Set if current server only (file lock held) */
#define AFS_OPERATION_NO_VSLEEP		0x0020	/* Set to prevent sleep on VBUSY, VOFFLINE, ... */
#define AFS_OPERATION_UNINTR		0x0040	/* Set if op is uninterruptible */
#define AFS_OPERATION_DOWNGRADE		0x0080	/* Set to retry with downgraded opcode */
#define AFS_OPERATION_LOCK_0		0x0100	/* Set if have io_lock on file[0] */
#define AFS_OPERATION_LOCK_1		0x0200	/* Set if have io_lock on file[1] */
#define AFS_OPERATION_TRIED_ALL		0x0400	/* Set if we've tried all the fileservers */
#define AFS_OPERATION_RETRY_SERVER	0x0800	/* Set if we should retry the current server */
#define AFS_OPERATION_DIR_CONFLICT	0x1000	/* Set if we detected a 3rd-party dir change */
};

/*
 * Cache auxiliary data.
 */
struct afs_vnode_cache_aux {
	__be64			data_version;
} __packed;

static inline void afs_set_cache_aux(struct afs_vnode *vnode,
				     struct afs_vnode_cache_aux *aux)
{
	aux->data_version = cpu_to_be64(vnode->status.data_version);
}

static inline void afs_invalidate_cache(struct afs_vnode *vnode, unsigned int flags)
{
	struct afs_vnode_cache_aux aux;

	afs_set_cache_aux(vnode, &aux);
	fscache_invalidate(afs_vnode_cache(vnode), &aux,
			   i_size_read(&vnode->vfs_inode), flags);
}

/*
 * We use folio->private to hold the amount of the folio that we've written to,
 * splitting the field into two parts.  However, we need to represent a range
 * 0...FOLIO_SIZE, so we reduce the resolution if the size of the folio
 * exceeds what we can encode.
 */
#ifdef CONFIG_64BIT
#define __AFS_FOLIO_PRIV_MASK		0x7fffffffUL
#define __AFS_FOLIO_PRIV_SHIFT		32
#define __AFS_FOLIO_PRIV_MMAPPED	0x80000000UL
#else
#define __AFS_FOLIO_PRIV_MASK		0x7fffUL
#define __AFS_FOLIO_PRIV_SHIFT		16
#define __AFS_FOLIO_PRIV_MMAPPED	0x8000UL
#endif

static inline unsigned int afs_folio_dirty_resolution(struct folio *folio)
{
	int shift = folio_shift(folio) - (__AFS_FOLIO_PRIV_SHIFT - 1);
	return (shift > 0) ? shift : 0;
}

static inline size_t afs_folio_dirty_from(struct folio *folio, unsigned long priv)
{
	unsigned long x = priv & __AFS_FOLIO_PRIV_MASK;

	/* The lower bound is inclusive */
	return x << afs_folio_dirty_resolution(folio);
}

static inline size_t afs_folio_dirty_to(struct folio *folio, unsigned long priv)
{
	unsigned long x = (priv >> __AFS_FOLIO_PRIV_SHIFT) & __AFS_FOLIO_PRIV_MASK;

	/* The upper bound is immediately beyond the region */
	return (x + 1) << afs_folio_dirty_resolution(folio);
}

static inline unsigned long afs_folio_dirty(struct folio *folio, size_t from, size_t to)
{
	unsigned int res = afs_folio_dirty_resolution(folio);
	from >>= res;
	to = (to - 1) >> res;
	return (to << __AFS_FOLIO_PRIV_SHIFT) | from;
}

static inline unsigned long afs_folio_dirty_mmapped(unsigned long priv)
{
	return priv | __AFS_FOLIO_PRIV_MMAPPED;
}

static inline bool afs_is_folio_dirty_mmapped(unsigned long priv)
{
	return priv & __AFS_FOLIO_PRIV_MMAPPED;
}

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
extern struct afs_vlserver_list *afs_parse_text_addrs(struct afs_net *,
						      const char *, size_t, char,
						      unsigned short, unsigned short);
extern struct afs_vlserver_list *afs_dns_query(struct afs_cell *, time64_t *);
extern bool afs_iterate_addresses(struct afs_addr_cursor *);
extern int afs_end_cursor(struct afs_addr_cursor *);

extern void afs_merge_fs_addr4(struct afs_addr_list *, __be32, u16);
extern void afs_merge_fs_addr6(struct afs_addr_list *, __be32 *, u16);

/*
 * cache.c
 */
#ifdef CONFIG_AFS_FSCACHE
extern struct fscache_netfs afs_cache_netfs;
#endif

/*
 * callback.c
 */
extern void afs_invalidate_mmap_work(struct work_struct *);
extern void afs_server_init_callback_work(struct work_struct *work);
extern void afs_init_callback_state(struct afs_server *);
extern void __afs_break_callback(struct afs_vnode *, enum afs_cb_break_reason);
extern void afs_break_callback(struct afs_vnode *, enum afs_cb_break_reason);
extern void afs_break_callbacks(struct afs_server *, size_t, struct afs_callback_break *);

static inline unsigned int afs_calc_vnode_cb_break(struct afs_vnode *vnode)
{
	return vnode->cb_break + vnode->cb_v_break;
}

static inline bool afs_cb_is_broken(unsigned int cb_break,
				    const struct afs_vnode *vnode)
{
	return cb_break != (vnode->cb_break + vnode->volume->cb_v_break);
}

/*
 * cell.c
 */
extern int afs_cell_init(struct afs_net *, const char *);
extern struct afs_cell *afs_find_cell(struct afs_net *, const char *, unsigned,
				      enum afs_cell_trace);
extern struct afs_cell *afs_lookup_cell(struct afs_net *, const char *, unsigned,
					const char *, bool);
extern struct afs_cell *afs_use_cell(struct afs_cell *, enum afs_cell_trace);
extern void afs_unuse_cell(struct afs_net *, struct afs_cell *, enum afs_cell_trace);
extern struct afs_cell *afs_get_cell(struct afs_cell *, enum afs_cell_trace);
extern void afs_see_cell(struct afs_cell *, enum afs_cell_trace);
extern void afs_put_cell(struct afs_cell *, enum afs_cell_trace);
extern void afs_queue_cell(struct afs_cell *, enum afs_cell_trace);
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
extern void afs_check_for_remote_deletion(struct afs_operation *);

/*
 * dir_edit.c
 */
extern void afs_edit_dir_add(struct afs_vnode *, struct qstr *, struct afs_fid *,
			     enum afs_edit_dir_reason);
extern void afs_edit_dir_remove(struct afs_vnode *, struct qstr *, enum afs_edit_dir_reason);

/*
 * dir_silly.c
 */
extern int afs_sillyrename(struct afs_vnode *, struct afs_vnode *,
			   struct dentry *, struct key *);
extern int afs_silly_iput(struct dentry *, struct inode *);

/*
 * dynroot.c
 */
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
extern const struct address_space_operations afs_file_aops;
extern const struct address_space_operations afs_symlink_aops;
extern const struct inode_operations afs_file_inode_operations;
extern const struct file_operations afs_file_operations;
extern const struct netfs_read_request_ops afs_req_ops;

extern int afs_cache_wb_key(struct afs_vnode *, struct afs_file *);
extern void afs_put_wb_key(struct afs_wb_key *);
extern int afs_open(struct inode *, struct file *);
extern int afs_release(struct inode *, struct file *);
extern int afs_fetch_data(struct afs_vnode *, struct afs_read *);
extern struct afs_read *afs_alloc_read(gfp_t);
extern void afs_put_read(struct afs_read *);

static inline struct afs_read *afs_get_read(struct afs_read *req)
{
	refcount_inc(&req->usage);
	return req;
}

/*
 * flock.c
 */
extern struct workqueue_struct *afs_lock_manager;

extern void afs_lock_op_done(struct afs_call *);
extern void afs_lock_work(struct work_struct *);
extern void afs_lock_may_be_available(struct afs_vnode *);
extern int afs_lock(struct file *, int, struct file_lock *);
extern int afs_flock(struct file *, int, struct file_lock *);

/*
 * fsclient.c
 */
extern void afs_fs_fetch_status(struct afs_operation *);
extern void afs_fs_fetch_data(struct afs_operation *);
extern void afs_fs_create_file(struct afs_operation *);
extern void afs_fs_make_dir(struct afs_operation *);
extern void afs_fs_remove_file(struct afs_operation *);
extern void afs_fs_remove_dir(struct afs_operation *);
extern void afs_fs_link(struct afs_operation *);
extern void afs_fs_symlink(struct afs_operation *);
extern void afs_fs_rename(struct afs_operation *);
extern void afs_fs_store_data(struct afs_operation *);
extern void afs_fs_setattr(struct afs_operation *);
extern void afs_fs_get_volume_status(struct afs_operation *);
extern void afs_fs_set_lock(struct afs_operation *);
extern void afs_fs_extend_lock(struct afs_operation *);
extern void afs_fs_release_lock(struct afs_operation *);
extern int afs_fs_give_up_all_callbacks(struct afs_net *, struct afs_server *,
					struct afs_addr_cursor *, struct key *);
extern bool afs_fs_get_capabilities(struct afs_net *, struct afs_server *,
				    struct afs_addr_cursor *, struct key *);
extern void afs_fs_inline_bulk_status(struct afs_operation *);

struct afs_acl {
	u32	size;
	u8	data[];
};

extern void afs_fs_fetch_acl(struct afs_operation *);
extern void afs_fs_store_acl(struct afs_operation *);

/*
 * fs_operation.c
 */
extern struct afs_operation *afs_alloc_operation(struct key *, struct afs_volume *);
extern int afs_put_operation(struct afs_operation *);
extern bool afs_begin_vnode_operation(struct afs_operation *);
extern void afs_wait_for_operation(struct afs_operation *);
extern int afs_do_sync_operation(struct afs_operation *);

static inline void afs_op_nomem(struct afs_operation *op)
{
	op->error = -ENOMEM;
}

static inline void afs_op_set_vnode(struct afs_operation *op, unsigned int n,
				    struct afs_vnode *vnode)
{
	op->file[n].vnode = vnode;
	op->file[n].need_io_lock = true;
}

static inline void afs_op_set_fid(struct afs_operation *op, unsigned int n,
				  const struct afs_fid *fid)
{
	op->file[n].fid = *fid;
}

/*
 * fs_probe.c
 */
extern void afs_fileserver_probe_result(struct afs_call *);
extern void afs_fs_probe_fileserver(struct afs_net *, struct afs_server *, struct key *, bool);
extern int afs_wait_for_fs_probes(struct afs_server_list *, unsigned long);
extern void afs_probe_fileserver(struct afs_net *, struct afs_server *);
extern void afs_fs_probe_dispatcher(struct work_struct *);
extern int afs_wait_for_one_fs_probe(struct afs_server *, bool);
extern void afs_fs_probe_cleanup(struct afs_net *);

/*
 * inode.c
 */
extern const struct afs_operation_ops afs_fetch_status_operation;

extern void afs_vnode_commit_status(struct afs_operation *, struct afs_vnode_param *);
extern int afs_fetch_status(struct afs_vnode *, struct key *, bool, afs_access_t *);
extern int afs_ilookup5_test_by_fid(struct inode *, void *);
extern struct inode *afs_iget_pseudo_dir(struct super_block *, bool);
extern struct inode *afs_iget(struct afs_operation *, struct afs_vnode_param *);
extern struct inode *afs_root_iget(struct super_block *, struct key *);
extern bool afs_check_validity(struct afs_vnode *);
extern int afs_validate(struct afs_vnode *, struct key *);
extern int afs_getattr(struct user_namespace *mnt_userns, const struct path *,
		       struct kstat *, u32, unsigned int);
extern int afs_setattr(struct user_namespace *mnt_userns, struct dentry *, struct iattr *);
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
extern void afs_prioritise_error(struct afs_error *, int, u32);

/*
 * mntpt.c
 */
extern const struct inode_operations afs_mntpt_inode_operations;
extern const struct inode_operations afs_autocell_inode_operations;
extern const struct file_operations afs_mntpt_file_operations;

extern struct vfsmount *afs_d_automount(struct path *);
extern void afs_mntpt_kill_timer(void);

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
extern bool afs_select_fileserver(struct afs_operation *);
extern void afs_dump_edestaddrreq(const struct afs_operation *);

/*
 * rxrpc.c
 */
extern struct workqueue_struct *afs_async_calls;

extern int __net_init afs_open_socket(struct afs_net *);
extern void __net_exit afs_close_socket(struct afs_net *);
extern void afs_charge_preallocation(struct work_struct *);
extern void afs_put_call(struct afs_call *);
extern void afs_make_call(struct afs_addr_cursor *, struct afs_call *, gfp_t);
extern long afs_wait_for_call_to_complete(struct afs_call *, struct afs_addr_cursor *);
extern struct afs_call *afs_alloc_flat_call(struct afs_net *,
					    const struct afs_call_type *,
					    size_t, size_t);
extern void afs_flat_call_destructor(struct afs_call *);
extern void afs_send_empty_reply(struct afs_call *);
extern void afs_send_simple_reply(struct afs_call *, const void *, size_t);
extern int afs_extract_data(struct afs_call *, bool);
extern int afs_protocol_error(struct afs_call *, enum afs_eproto_cause);

static inline void afs_make_op_call(struct afs_operation *op, struct afs_call *call,
				    gfp_t gfp)
{
	op->call = call;
	op->type = call->type;
	call->op = op;
	call->key = op->key;
	call->intr = !(op->flags & AFS_OPERATION_UNINTR);
	afs_make_call(&op->ac, call, gfp);
}

static inline void afs_extract_begin(struct afs_call *call, void *buf, size_t size)
{
	call->iov_len = size;
	call->kvec[0].iov_base = buf;
	call->kvec[0].iov_len = size;
	iov_iter_kvec(&call->def_iter, READ, call->kvec, 1, size);
}

static inline void afs_extract_to_tmp(struct afs_call *call)
{
	call->iov_len = sizeof(call->tmp);
	afs_extract_begin(call, &call->tmp, sizeof(call->tmp));
}

static inline void afs_extract_to_tmp64(struct afs_call *call)
{
	call->iov_len = sizeof(call->tmp64);
	afs_extract_begin(call, &call->tmp64, sizeof(call->tmp64));
}

static inline void afs_extract_discard(struct afs_call *call, size_t size)
{
	call->iov_len = size;
	iov_iter_discard(&call->def_iter, READ, size);
}

static inline void afs_extract_to_buf(struct afs_call *call, size_t size)
{
	call->iov_len = size;
	afs_extract_begin(call, call->buffer, size);
}

static inline int afs_transfer_reply(struct afs_call *call)
{
	return afs_extract_data(call, false);
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
	if (ok) {
		trace_afs_call_done(call);

		/* Asynchronous calls have two refs to release - one from the alloc and
		 * one queued with the work item - and we can't just deallocate the
		 * call because the work item may be queued again.
		 */
		if (call->drop_ref)
			afs_put_call(call);
	}
}

/*
 * security.c
 */
extern void afs_put_permits(struct afs_permits *);
extern void afs_clear_permits(struct afs_vnode *);
extern void afs_cache_permit(struct afs_vnode *, struct key *, unsigned int,
			     struct afs_status_cb *);
extern void afs_zap_permits(struct rcu_head *);
extern struct key *afs_request_key(struct afs_cell *);
extern struct key *afs_request_key_rcu(struct afs_cell *);
extern int afs_check_permit(struct afs_vnode *, struct key *, afs_access_t *);
extern int afs_permission(struct user_namespace *, struct inode *, int);
extern void __exit afs_clean_up_permit_cache(void);

/*
 * server.c
 */
extern spinlock_t afs_server_peer_lock;

extern struct afs_server *afs_find_server(struct afs_net *,
					  const struct sockaddr_rxrpc *);
extern struct afs_server *afs_find_server_by_uuid(struct afs_net *, const uuid_t *);
extern struct afs_server *afs_lookup_server(struct afs_cell *, struct key *, const uuid_t *, u32);
extern struct afs_server *afs_get_server(struct afs_server *, enum afs_server_trace);
extern struct afs_server *afs_use_server(struct afs_server *, enum afs_server_trace);
extern void afs_unuse_server(struct afs_net *, struct afs_server *, enum afs_server_trace);
extern void afs_unuse_server_notime(struct afs_net *, struct afs_server *, enum afs_server_trace);
extern void afs_put_server(struct afs_net *, struct afs_server *, enum afs_server_trace);
extern void afs_manage_servers(struct work_struct *);
extern void afs_servers_timer(struct timer_list *);
extern void afs_fs_probe_timer(struct timer_list *);
extern void __net_exit afs_purge_servers(struct afs_net *);
extern bool afs_check_server_record(struct afs_operation *, struct afs_server *);

static inline void afs_inc_servers_outstanding(struct afs_net *net)
{
	atomic_inc(&net->servers_outstanding);
}

static inline void afs_dec_servers_outstanding(struct afs_net *net)
{
	if (atomic_dec_and_test(&net->servers_outstanding))
		wake_up_var(&net->servers_outstanding);
}

static inline bool afs_is_probing_server(struct afs_server *server)
{
	return list_empty(&server->probe_link);
}

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
extern struct afs_vldb_entry *afs_vl_get_entry_by_name_u(struct afs_vl_cursor *,
							 const char *, int);
extern struct afs_addr_list *afs_vl_get_addrs_u(struct afs_vl_cursor *, const uuid_t *);
extern struct afs_call *afs_vl_get_capabilities(struct afs_net *, struct afs_addr_cursor *,
						struct key *, struct afs_vlserver *, unsigned int);
extern struct afs_addr_list *afs_yfsvl_get_endpoints(struct afs_vl_cursor *, const uuid_t *);
extern char *afs_yfsvl_get_cell_name(struct afs_vl_cursor *);

/*
 * vl_alias.c
 */
extern int afs_cell_detect_alias(struct afs_cell *, struct key *);

/*
 * vl_probe.c
 */
extern void afs_vlserver_probe_result(struct afs_call *);
extern int afs_send_vl_probes(struct afs_net *, struct key *, struct afs_vlserver_list *);
extern int afs_wait_for_vl_probes(struct afs_vlserver_list *, unsigned long);

/*
 * vl_rotate.c
 */
extern bool afs_begin_vlserver_operation(struct afs_vl_cursor *,
					 struct afs_cell *, struct key *);
extern bool afs_select_vlserver(struct afs_vl_cursor *);
extern bool afs_select_current_vlserver(struct afs_vl_cursor *);
extern int afs_end_vlserver_operation(struct afs_vl_cursor *);

/*
 * vlserver_list.c
 */
static inline struct afs_vlserver *afs_get_vlserver(struct afs_vlserver *vlserver)
{
	atomic_inc(&vlserver->usage);
	return vlserver;
}

static inline struct afs_vlserver_list *afs_get_vlserverlist(struct afs_vlserver_list *vllist)
{
	if (vllist)
		atomic_inc(&vllist->usage);
	return vllist;
}

extern struct afs_vlserver *afs_alloc_vlserver(const char *, size_t, unsigned short);
extern void afs_put_vlserver(struct afs_net *, struct afs_vlserver *);
extern struct afs_vlserver_list *afs_alloc_vlserver_list(unsigned int);
extern void afs_put_vlserverlist(struct afs_net *, struct afs_vlserver_list *);
extern struct afs_vlserver_list *afs_extract_vlserver_list(struct afs_cell *,
							   const void *, size_t);

/*
 * volume.c
 */
extern struct afs_volume *afs_create_volume(struct afs_fs_context *);
extern int afs_activate_volume(struct afs_volume *);
extern void afs_deactivate_volume(struct afs_volume *);
extern struct afs_volume *afs_get_volume(struct afs_volume *, enum afs_volume_trace);
extern void afs_put_volume(struct afs_net *, struct afs_volume *, enum afs_volume_trace);
extern int afs_check_volume_status(struct afs_volume *, struct afs_operation *);

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
extern ssize_t afs_file_write(struct kiocb *, struct iov_iter *);
extern int afs_fsync(struct file *, loff_t, loff_t, int);
extern vm_fault_t afs_page_mkwrite(struct vm_fault *vmf);
extern void afs_prune_wb_keys(struct afs_vnode *);
extern int afs_launder_page(struct page *);

/*
 * xattr.c
 */
extern const struct xattr_handler *afs_xattr_handlers[];

/*
 * yfsclient.c
 */
extern void yfs_fs_fetch_data(struct afs_operation *);
extern void yfs_fs_create_file(struct afs_operation *);
extern void yfs_fs_make_dir(struct afs_operation *);
extern void yfs_fs_remove_file2(struct afs_operation *);
extern void yfs_fs_remove_file(struct afs_operation *);
extern void yfs_fs_remove_dir(struct afs_operation *);
extern void yfs_fs_link(struct afs_operation *);
extern void yfs_fs_symlink(struct afs_operation *);
extern void yfs_fs_rename(struct afs_operation *);
extern void yfs_fs_store_data(struct afs_operation *);
extern void yfs_fs_setattr(struct afs_operation *);
extern void yfs_fs_get_volume_status(struct afs_operation *);
extern void yfs_fs_set_lock(struct afs_operation *);
extern void yfs_fs_extend_lock(struct afs_operation *);
extern void yfs_fs_release_lock(struct afs_operation *);
extern void yfs_fs_fetch_status(struct afs_operation *);
extern void yfs_fs_inline_bulk_status(struct afs_operation *);

struct yfs_acl {
	struct afs_acl	*acl;		/* Dir/file/symlink ACL */
	struct afs_acl	*vol_acl;	/* Whole volume ACL */
	u32		inherit_flag;	/* True if ACL is inherited from parent dir */
	u32		num_cleaned;	/* Number of ACEs removed due to subject removal */
	unsigned int	flags;
#define YFS_ACL_WANT_ACL	0x01	/* Set if caller wants ->acl */
#define YFS_ACL_WANT_VOL_ACL	0x02	/* Set if caller wants ->vol_acl */
};

extern void yfs_free_opaque_acl(struct yfs_acl *);
extern void yfs_fs_fetch_opaque_acl(struct afs_operation *);
extern void yfs_fs_store_opaque_acl2(struct afs_operation *);

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

/*
 * Note that a dentry got changed.  We need to set d_fsdata to the data version
 * number derived from the result of the operation.  It doesn't matter if
 * d_fsdata goes backwards as we'll just revalidate.
 */
static inline void afs_update_dentry_version(struct afs_operation *op,
					     struct afs_vnode_param *dir_vp,
					     struct dentry *dentry)
{
	if (!op->error)
		dentry->d_fsdata =
			(void *)(unsigned long)dir_vp->scb.status.data_version;
}

/*
 * Set the file size and block count.  Estimate the number of 512 bytes blocks
 * used, rounded up to nearest 1K for consistency with other AFS clients.
 */
static inline void afs_set_i_size(struct afs_vnode *vnode, u64 size)
{
	i_size_write(&vnode->vfs_inode, size);
	vnode->vfs_inode.i_blocks = ((size + 1023) >> 10) << 1;
}

/*
 * Check for a conflicting operation on a directory that we just unlinked from.
 * If someone managed to sneak a link or an unlink in on the file we just
 * unlinked, we won't be able to trust nlink on an AFS file (but not YFS).
 */
static inline void afs_check_dir_conflict(struct afs_operation *op,
					  struct afs_vnode_param *dvp)
{
	if (dvp->dv_before + dvp->dv_delta != dvp->scb.status.data_version)
		op->flags |= AFS_OPERATION_DIR_CONFLICT;
}

static inline int afs_io_error(struct afs_call *call, enum afs_io_error where)
{
	trace_afs_io_error(call->debug_id, -EIO, where);
	return -EIO;
}

static inline int afs_bad(struct afs_vnode *vnode, enum afs_file_error where)
{
	trace_afs_file_error(vnode, -EIO, where);
	return -EIO;
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
