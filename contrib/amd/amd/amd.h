/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/amd/amd.h
 *
 */

#ifndef _AMD_H
#define _AMD_H


/*
 * MACROS:
 */

/*
 * Define a default debug mtab path for systems
 * that support mtab on file.
 */
#ifdef MOUNT_TABLE_ON_FILE
# define DEBUG_MNTTAB_FILE		"/tmp/mtab"
#endif /* MOUNT_TABLE_ON_FILE */

/* Max line length that info services can handle */
#define INFO_MAX_LINE_LEN		1500

/* options for amd.conf */
#define CFM_BROWSABLE_DIRS		0x00000001
#define CFM_MOUNT_TYPE_AUTOFS		0x00000002 /* use kernel autofs support */
#define CFM_SELECTORS_IN_DEFAULTS	0x00000004
#define CFM_NORMALIZE_HOSTNAMES		0x00000008
#define CFM_PROCESS_LOCK		0x00000010
#define CFM_PRINT_PID			0x00000020
#define CFM_RESTART_EXISTING_MOUNTS	0x00000040
#define CFM_SHOW_STATFS_ENTRIES		0x00000080
#define CFM_FULLY_QUALIFIED_HOSTS	0x00000100
#define CFM_BROWSABLE_DIRS_FULL		0x00000200 /* allow '/' in readdir() */
#define CFM_UNMOUNT_ON_EXIT		0x00000400 /* when amd finishing */
#define CFM_USE_TCPWRAPPERS		0x00000800
#define CFM_AUTOFS_USE_LOFS		0x00001000
#define CFM_NFS_INSECURE_PORT		0x00002000
#define CFM_DOMAIN_STRIP		0x00004000
#define CFM_NORMALIZE_SLASHES		0x00008000 /* normalize slashes? */
#define CFM_FORCED_UNMOUNTS		0x00010000 /* forced unmounts? */
#define CFM_TRUNCATE_LOG		0x00020000 /* truncate log file? */
#define CFM_SUN_MAP_SYNTAX		0x00040000 /* Sun map syntax? */
#define CFM_NFS_ANY_INTERFACE		0x00080000 /* all interfaces are acceptable */

/* defaults global flags: plock, tcpwrappers, and autofs/lofs */
#define CFM_DEFAULT_FLAGS	(CFM_PROCESS_LOCK|CFM_USE_TCPWRAPPERS|CFM_AUTOFS_USE_LOFS|CFM_DOMAIN_STRIP|CFM_NORMALIZE_SLASHES)

/*
 * macro definitions for automounter vfs/vnode operations.
 */
#define	VLOOK_CREATE	0x1
#define	VLOOK_DELETE	0x2
#define VLOOK_LOOKUP	0x3

/*
 * macro definitions for automounter vfs capabilities
 */
#define FS_DIRECTORY	0x0001	/* This looks like a dir, not a link */
#define	FS_MBACKGROUND	0x0002	/* Should background this mount */
#define	FS_NOTIMEOUT	0x0004	/* Don't bother with timeouts */
#define FS_MKMNT	0x0008	/* Need to make the mount point */
#define FS_UBACKGROUND	0x0010	/* Unmount in background */
#define	FS_BACKGROUND	(FS_MBACKGROUND|FS_UBACKGROUND)
#define	FS_DISCARD	0x0020	/* Discard immediately on last reference */
#define	FS_AMQINFO	0x0040	/* Amq is interested in this fs type */
#define FS_AUTOFS	0x0080	/* This filesystem can be an autofs f/s */
#define FS_DIRECT	0x0100	/* Direct mount */
#define FS_ON_AUTOFS	0x0200	/* This filesystem can be mounted directly
				   onto an autofs mountpoint */

/*
 * macros for struct am_node (map of auto-mount points).
 */
#define	AMF_NOTIMEOUT	0x0001	/* This node never times out */
#define	AMF_ROOT	0x0002	/* This is a root node */
#define AMF_AUTOFS	0x0004	/* This node is part of an autofs filesystem */
#define AMF_REMOUNT	0x0008	/* This node needs to be remounted */
#define AMF_SOFTLOOKUP	0x0010	/* This node returns EIO if server is down */

/*
 * macros for struct mntfs (list of mounted filesystems)
 */
#define	MFF_MOUNTED	0x0001	/* Node is mounted */
#define	MFF_MOUNTING	0x0002	/* Mount is in progress */
#define	MFF_UNMOUNTING	0x0004	/* Unmount is in progress */
#define	MFF_RESTART	0x0008	/* Restarted node */
#define MFF_MKMNT	0x0010	/* Delete this node's am_mount */
#define	MFF_ERROR	0x0020	/* This node failed to mount */
#define	MFF_LOGDOWN	0x0040	/* Logged that this mount is down */
#define	MFF_RSTKEEP	0x0080	/* Don't timeout this filesystem - restarted */
#define	MFF_WANTTIMO	0x0100	/* Need a timeout call when not busy */
#define MFF_NFSLINK	0x0200	/* nfsl type, and deemed a link */
#define MFF_IS_AUTOFS	0x0400	/* this filesystem is of type autofs */
#define MFF_NFS_SCALEDOWN 0x0800 /* the mount failed, retry with v2/UDP */
#define MFF_ON_AUTOFS	0x1000	/* autofs has a lofs/link to this f/s */
#define MFF_WEBNFS	0x2000	/* use public filehandle */

/*
 * macros for struct fserver.
 */
#define	FSF_VALID	0x0001	/* Valid information available */
#define	FSF_DOWN	0x0002	/* This fileserver is thought to be down */
#define	FSF_ERROR	0x0004	/* Permanent error has occurred */
#define	FSF_WANT	0x0008	/* Want a wakeup call */
#define	FSF_PINGING	0x0010	/* Already doing pings */
#define	FSF_WEBNFS	0x0020	/* Don't try to contact portmapper */
#define FSF_PING_UNINIT	0x0040	/* ping values have not been initilized */
#define FSF_FORCE_UNMOUNT 0x0080 /* force umount of this fserver */
#define	FSRV_ERROR(fs)	((fs) && (((fs)->fs_flags & FSF_ERROR) == FSF_ERROR))
#define	FSRV_ISDOWN(fs)	((fs) && (((fs)->fs_flags & (FSF_DOWN|FSF_VALID)) == (FSF_DOWN|FSF_VALID)))
#define	FSRV_ISUP(fs)	(!(fs) || (((fs)->fs_flags & (FSF_DOWN|FSF_VALID)) == (FSF_VALID)))

/* some systems (SunOS 4.x) neglect to define the mount null message */
#ifndef MOUNTPROC_NULL
# define MOUNTPROC_NULL ((u_long)(0))
#endif /* not MOUNTPROC_NULL */

/*
 * Error to return if remote host is not available.
 * Try, in order, "host down", "host unreachable", "invalid argument".
 */
#ifdef EHOSTDOWN
# define AM_ERRNO_HOST_DOWN	EHOSTDOWN
#else /* not EHOSTDOWN */
# ifdef EHOSTUNREACH
#  define AM_ERRNO_HOST_DOWN	EHOSTUNREACH
# else /* not EHOSTUNREACH */
#  define AM_ERRNO_HOST_DOWN	EINVAL
# endif /* not EHOSTUNREACH */
#endif /* not EHOSTDOWN */

/* Hash table size */
#define NKVHASH (1 << 2)        /* Power of two */

/* Max entries to return in one call */
#define	MAX_READDIR_ENTRIES	16

/*
 * default amfs_auto retrans - 1/10th seconds
 */
#define	AMFS_AUTO_RETRANS(x)	((ALLOWED_MOUNT_TIME*10+5*gopt.amfs_auto_timeo[(x)])/gopt.amfs_auto_timeo[(x)] * 2)

/*
 * The following values can be tuned...
 */
#define	AM_TTL			(300) /* Default cache period (5 min) */
#define	AM_TTL_W		(120) /* Default unmount interval (2 min) */
#define	AM_PINGER		30 /* NFS ping interval for live systems */
#define	AMFS_AUTO_TIMEO		8 /* Default amfs_auto timeout - .8s */
#define AMFS_EXEC_MAP_TIMEOUT	10 /* default 10sec exec map timeout */

/* interval between forced retries of a mount */
#define RETRY_INTERVAL	2

#ifndef ROOT_MAP
# define ROOT_MAP "\"root\""
#endif /* not ROOT_MAP */

#define ereturn(x) do { *error_return = x; return 0; } while (0)

#define NEVER (time_t) 0

#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP)
# define AMD_SERVICE_NAME "amd"	/* for tcpwrappers */
#endif /* defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */

/*
 * TYPEDEFS:
 */

typedef struct cf_map cf_map_t;
typedef struct kv kv;
typedef struct am_node am_node;
typedef struct mntfs mntfs;
typedef struct am_loc am_loc;
typedef struct am_opts am_opts;
typedef struct am_ops am_ops;
typedef struct am_stats am_stats;
typedef struct fserver fserver;

typedef voidp wchan_t;
typedef voidp opaque_t;

/*
 * Cache map operations
 */
typedef void add_fn(mnt_map *, char *, char *);
typedef int init_fn(mnt_map *, char *, time_t *);
typedef int mtime_fn(mnt_map *, char *, time_t *);
typedef int isup_fn(mnt_map *, char *);
typedef int reload_fn(mnt_map *, char *, add_fn *);
typedef int search_fn(mnt_map *, char *, char *, char **, time_t *);
typedef int task_fun(opaque_t);
typedef void cb_fun(int, int, opaque_t);
typedef void fwd_fun(voidp, int, struct sockaddr_in *,
		     struct sockaddr_in *, opaque_t, int);
typedef int key_fun(char *, opaque_t);
typedef void callout_fun(opaque_t);

/*
 * automounter vfs/vnode operations.
 */
typedef char *(*vfs_match) (am_opts *);
typedef int (*vfs_init) (mntfs *);
typedef int (*vmount_fs) (am_node *, mntfs *);
typedef int (*vumount_fs) (am_node *, mntfs *);
typedef am_node *(*vlookup_child) (am_node *, char *, int *, int);
typedef am_node *(*vmount_child) (am_node *, int *);
typedef int (*vreaddir) (am_node *, voidp, voidp, voidp, u_int);
typedef am_node *(*vreadlink) (am_node *, int *);
typedef void (*vmounted) (mntfs *);
typedef void (*vumounted) (mntfs *);
typedef fserver *(*vffserver) (mntfs *);
typedef wchan_t (*vget_wchan) (mntfs *);

/*
 * NFS progran dispatcher
 */
typedef void (*dispatcher_t)(struct svc_req *rqstp, SVCXPRT *transp);


/*
 * STRUCTURES:
 */

/* global amd options that are manipulated by conf.c */
struct amu_global_options {
  char *arch;			/* name of current architecture */
  char *auto_dir;		/* automounter temp dir */
  int auto_attrcache;		/* attribute cache timeout for auto dirs */
  char *cluster;		/* cluster name */
  char *karch;			/* kernel architecture */
  char *logfile;		/* amd log file */
  char *op_sys;			/* operating system name ${os} */
  char *op_sys_ver;		/* OS version ${osver} */
  char *op_sys_full;		/* full OS name ${full_os} */
  char *op_sys_vendor;		/* name of OS vendor ${vendor} */
  char *pid_file;		/* PID file */
  char *sub_domain;		/* local domain */
  char *localhost_address;	/* localhost address (NULL means use 127.0.0.1) */
  char *map_defaults;		/* global map /default options */
  char *map_options;		/* global map options */
  int map_reload_interval;	/* map reload interval */
  char *map_type;		/* global map type */
  char *search_path;		/* search path for maps */
  char *mount_type;		/* mount type for map */
  char *debug_mtab_file;        /* path for the mtab file during debug mode */
  u_int flags;			/* various CFM_* flags */

#define AMU_TYPE_NONE -1	/* for amfs_auto_{timeo,retrans,toplvl} */
#define AMU_TYPE_UDP 0		/* for amfs_auto_{timeo,retrans,toplvl} */
#define AMU_TYPE_TCP 1		/* for amfs_auto_{timeo,retrans,toplvl} */
  /*
   * Note: toplvl is only UDP, but we want to separate it from regular
   * NFS mounts which Amd makes, because the toplvl mount is a localhost
   * mount for which different timeo/retrans parameters may be desired.
   */
#define AMU_TYPE_TOPLVL 2	/* for amfs_auto_{timeo,retrans,toplvl} */
#define AMU_TYPE_MAX 3		/* for amfs_auto_{timeo,retrans,toplvl} */
  int amfs_auto_retrans[AMU_TYPE_MAX]; /* NFS retransmit counter */
  int amfs_auto_timeo[AMU_TYPE_MAX]; /* NFS retry interval */

  int am_timeo;			/* cache duration */
  int am_timeo_w;		/* dismount interval */
  u_long portmap_program;	/* amd RPC program number */
  u_short preferred_amq_port;	/* preferred amq service RPC port number (0 means "any") */
#ifdef HAVE_MAP_HESIOD
  char *hesiod_base;		/* Hesiod rhs */
#endif /* HAVE_MAP_HESIOD */
#ifdef HAVE_MAP_LDAP
  char *ldap_base;		/* LDAP base */
  char *ldap_hostports;		/* LDAP host ports */
  long ldap_cache_seconds; 	/* LDAP internal cache - keep seconds */
  long ldap_cache_maxmem;	/* LDAP internal cache - max memory (bytes) */
  long ldap_proto_version;	/* LDAP protocol version */
#endif /* HAVE_MAP_LDAP */
#ifdef HAVE_MAP_NIS
  char *nis_domain;		/* YP domain name */
#endif /* HAVE_MAP_NIS */
  char *nfs_proto;		/* NFS protocol (NULL, udp, tcp) */
  int nfs_vers;			/* NFS version (0, 2, 3, 4) */
  int nfs_vers_ping;		/* NFS rpc ping version (0, 2, 3, 4) */
  u_int exec_map_timeout;	/* timeout (seconds) for executable maps */
};

/* if you add anything here, update conf.c:reset_cf_map() */
struct cf_map {
  char *cfm_dir;		/* /home, /u, /src */
  char *cfm_name;		/* amd.home, /etc/amd.home ... */
  char *cfm_type;		/* file, hesiod, ndbm, nis ... */
  char *cfm_defaults;		/* map /defaults options in amd.conf */
  char *cfm_opts;		/* -cache:=all, etc. */
  char *cfm_search_path;	/* /etc/local:/etc/amdmaps:/misc/yp */
  char *cfm_tag;		/* optional map tag for amd -T */
  u_int cfm_flags;		/* browsable_dirs? mount_type? */
  struct cf_map *cfm_next;	/* pointer to next in list (if any) */
};

/*
 * Key-value pair
 */
struct kv {
  kv *next;
  char *key;
#ifdef HAVE_REGEXEC
  regex_t re;                   /* store the regexp from regcomp() */
#endif /* HAVE_REGEXEC */
  char *val;
};

struct mnt_map {
  qelem hdr;
  int refc;                     /* Reference count */
  short flags;                  /* Allocation flags */
  short alloc;                  /* Allocation mode */
  time_t modify;                /* Modify time of map */
  u_int reloads;		/* Number of times map was reloaded */
  u_int nentries;		/* Number of entries in the map */
  char *map_name;               /* Name of this map */
  char *wildcard;               /* Wildcard value */
  reload_fn *reload;            /* Function to be used for reloads */
  isup_fn *isup;		/* Is service up or not? (1=up, 0=down) */
  search_fn *search;            /* Function to be used for searching */
  mtime_fn *mtime;              /* Modify time function */
  kv *kvhash[NKVHASH];          /* Cached data */
  cf_map_t *cfm;		/* pointer to per-map amd.conf opts, if any */
  void *map_data;               /* Map data black box */
};

/*
 * Options
 */
struct am_opts {
  char *fs_glob;		/* Smashed copy of global options */
  char *fs_local;		/* Expanded copy of local options */
  char *fs_mtab;		/* Mount table entry */
  /* Other options ... */
  char *opt_dev;
  char *opt_delay;
  char *opt_dir;
  char *opt_fs;
  char *opt_group;
  char *opt_mount;
  char *opt_opts;
  char *opt_remopts;
  char *opt_pref;
  char *opt_cache;
  char *opt_rfs;
  char *opt_rhost;
  char *opt_sublink;
  char *opt_type;
  char *opt_mount_type;		/* "nfs" or "autofs" */
  char *opt_unmount;
  char *opt_umount;		/* an "alias" for opt_unmount (type:=program) */
  char *opt_user;
  char *opt_maptype;		/* map type: file, nis, hesiod, etc. */
  char *opt_cachedir;		/* cache directory */
  char *opt_addopts;		/* options to add to opt_opts */
};

struct am_ops {
  char		*fs_type;	/* type of filesystems e.g. "nfsx" */
  vfs_match	fs_match;	/* fxn: match */
  vfs_init	fs_init;	/* fxn: initialization */
  vmount_fs	mount_fs;	/* fxn: mount my own vnode */
  vumount_fs	umount_fs;	/* fxn: unmount my own vnode */
  vlookup_child	lookup_child;	/* fxn: lookup path-name */
  vmount_child	mount_child;	/* fxn: mount path-name */
  vreaddir	readdir;	/* fxn: read directory */
  vreadlink	readlink;	/* fxn: read link */
  vmounted	mounted;	/* fxn: after-mount extra actions */
  vumounted	umounted;	/* fxn: after-umount extra actions */
  vffserver	ffserver;	/* fxn: find a file server */
  vget_wchan	get_wchan;	/* fxn: get the waiting channel */
  int		nfs_fs_flags;	/* filesystem flags FS_* for nfs mounts */
#ifdef HAVE_FS_AUTOFS
  int		autofs_fs_flags;/* filesystem flags FS_* for autofs mounts */
#endif /* HAVE_FS_AUTOFS */
};

/*
 * List of mounted filesystems
 */
struct mntfs {
  qelem mf_q;			/* List of mounted filesystems */
  am_ops *mf_ops;		/* Operations on this mountpoint */
  am_opts *mf_fo;		/* File opts */
  char *mf_mount;		/* "/a/kiska/home/kiska" */
  char *mf_info;		/* Mount info */
  char *mf_auto;		/* Mount info */
  char *mf_mopts;		/* FS mount opts */
  char *mf_remopts;		/* Remote FS mount opts */
  char *mf_loopdev;		/* loop device name for /dev/loop mounts */
  fserver *mf_server;		/* File server */
  int mf_fsflags;		/* Flags FS_* copied from mf_ops->*_fs_flags */
  int mf_flags;			/* Flags MFF_* */
  int mf_error;			/* Error code from background mount */
  int mf_refc;			/* Number of references to this node */
  int mf_cid;			/* Callout id */
  void (*mf_prfree) (opaque_t);	/* Free private space */
  opaque_t mf_private;		/* Private - per-fs data */
};

/*
 * Locations: bindings between keys and mntfs
 */
struct am_loc {
  am_opts *al_fo;
  mntfs *al_mnt;
  int al_refc;
};


/*
 * List of fileservers
 */
struct fserver {
  qelem fs_q;			/* List of fileservers */
  int fs_refc;			/* Number of references to this server */
  char *fs_host;		/* Normalized hostname of server */
  struct sockaddr_in *fs_ip;	/* Network address of server */
  int fs_cid;			/* Callout id */
  int fs_pinger;		/* Ping (keepalive) interval */
  int fs_flags;			/* Flags */
  char *fs_type;		/* File server type */
  u_long fs_version;		/* NFS version of server (2, 3, etc.)*/
  char *fs_proto;		/* NFS protocol of server (tcp, udp, etc.) */
  opaque_t fs_private;		/* Private data */
  void (*fs_prfree) (opaque_t);	/* Free private data */
};

/*
 * Per-mountpoint statistics
 */
struct am_stats {
  time_t s_mtime;		/* Mount time */
  u_short s_uid;		/* Uid of mounter */
  int s_getattr;		/* Count of getattrs */
  int s_lookup;			/* Count of lookups */
  int s_readdir;		/* Count of readdirs */
  int s_readlink;		/* Count of readlinks */
  int s_statfs;			/* Count of statfs */
  int s_fsinfo;			/* Count of fsinfo */
  int s_pathconf;		/* Count of pathconf */
};

/*
 * System statistics
 */
struct amd_stats {
  int d_drops;			/* Dropped requests */
  int d_stale;			/* Stale NFS handles */
  int d_mok;			/* Successful mounts */
  int d_merr;			/* Failed mounts */
  int d_uerr;			/* Failed unmounts */
};
extern struct amd_stats amd_stats;

/*
 * Map of auto-mount points.
 */
struct am_node {
  int am_mapno;		/* Map number */
  am_loc *am_al;	/* Mounted filesystem */
  am_loc **am_alarray;	/* Filesystem sources to try to mount */
  char *am_name;	/* "kiska": name of this node */
  char *am_path;	/* "/home/kiska": path of this node's mount point */
  char *am_link;	/* "/a/kiska/home/kiska/this/that": link to sub-dir */
  am_node *am_parent;	/* Parent of this node */
  am_node *am_ysib;	/* Younger sibling of this node */
  am_node *am_osib;	/* Older sibling of this node */
  am_node *am_child;	/* First child of this node */
  nfsattrstat am_attr;	/* File attributes */
#define am_fattr	am_attr.ns_u.ns_attr_u
  int am_flags;		/* Boolean flags AMF_* */
  int am_error;		/* Specific mount error */
  time_t am_ttl;	/* Time to live */
  int am_timeo_w;	/* Dismount wait interval */
  int am_timeo;		/* Cache timeout interval */
  u_int am_gen;		/* Generation number */
  char *am_pref;	/* Mount info prefix */
  am_stats am_stats;	/* Statistics gathering */
  SVCXPRT *am_transp;	/* Info for quick reply */
  dev_t am_dev;		/* Device number */
  dev_t am_rdev;	/* Remote/real device number */
#ifdef HAVE_FS_AUTOFS
  autofs_fh_t *am_autofs_fh;
  time_t am_autofs_ttl;	/* Time to expire autofs nodes */
#endif /* HAVE_FS_AUTOFS */
  int am_fd[2];		/* parent child pipe fd's for sync umount */
};

/*
 * EXTERNALS:
 */

/*
 * Amq server global functions
 */
extern amq_mount_info_list *amqproc_getmntfs_1_svc(voidp argp, struct svc_req *rqstp);
extern amq_mount_stats *amqproc_stats_1_svc(voidp argp, struct svc_req *rqstp);
extern amq_mount_tree_list *amqproc_export_1_svc(voidp argp, struct svc_req *rqstp);
extern amq_mount_tree_p *amqproc_mnttree_1_svc(voidp argp, struct svc_req *rqstp);
extern amq_string *amqproc_getvers_1_svc(voidp argp, struct svc_req *rqstp);
extern amq_string *amqproc_pawd_1_svc(voidp argp, struct svc_req *rqstp);
extern int *amqproc_getpid_1_svc(voidp argp, struct svc_req *rqstp);
extern int *amqproc_mount_1_svc(voidp argp, struct svc_req *rqstp);
extern int *amqproc_setopt_1_svc(voidp argp, struct svc_req *rqstp);
extern voidp amqproc_null_1_svc(voidp argp, struct svc_req *rqstp);
extern int *amqproc_umnt_1_svc(voidp argp, struct svc_req *rqstp);
extern int *amqproc_sync_umnt_1_svc_parent(voidp argp, struct svc_req *rqstp);
extern amq_sync_umnt *amqproc_sync_umnt_1_svc_child(voidp argp, struct svc_req *rqstp);
extern amq_sync_umnt *amqproc_sync_umnt_1_svc_async(voidp argp, struct svc_req *rqstp);
extern amq_map_info_list *amqproc_getmapinfo_1_svc(voidp argp, struct svc_req *rqstp);

/* other external definitions */
extern am_nfs_handle_t *get_root_nfs_fh(char *dir, am_nfs_handle_t *nfh);
extern am_node *find_ap(char *);
extern am_node *get_ap_child(am_node *, char *);
extern bool_t xdr_amq_mount_info_qelem(XDR *xdrs, qelem *qhead);
extern bool_t xdr_amq_map_info_qelem(XDR *xdrs, qelem *qhead);
extern fserver *find_nfs_srvr(mntfs *mf);
extern int mount_nfs_fh(am_nfs_handle_t *fhp, char *mntdir, char *fs_name, mntfs *mf);
extern int process_all_regular_maps(void);
extern cf_map_t *find_cf_map(const char *name);
extern int set_conf_kv(const char *section, const char *k, const char *v);
extern int mount_node(opaque_t arg);
extern int unmount_mp(am_node *mp);
extern int conf_parse(void);	/* "yyparse" renamed */
extern FILE *conf_in;		/* "yyin" renamed */

extern void amfs_mkcacheref(mntfs *mf);
extern int amfs_mount(am_node *mp, mntfs *mf, char *opts);
extern void assign_error_mntfs(am_node *mp);
extern am_node *next_nonerror_node(am_node *xp);
extern void flush_srvr_nfs_cache(fserver *fs);
extern void am_mounted(am_node *);
extern void mf_mounted(mntfs *mf, bool_t call_free_opts);
extern void am_unmounted(am_node *);
extern am_node *get_exported_ap(int index);
extern am_node *get_first_exported_ap(int *index);
extern am_node *get_next_exported_ap(int *index);
extern am_node *path_to_exported_ap(char *path);
extern am_node *exported_ap_alloc(void);
extern am_node *find_mf(mntfs *);
extern am_node *next_map(int *);
extern am_ops *ops_match(am_opts *, char *, char *, char *, char *, char *);
extern am_ops *ops_search(char *);
extern fserver *dup_srvr(fserver *);
extern void srvrlog(fserver *, char *);
extern int get_mountd_port(fserver *, u_short *, wchan_t);
extern void flush_nfs_fhandle_cache(fserver *);

extern mntfs *dup_mntfs(mntfs *);
extern am_loc *dup_loc(am_loc *);
extern mntfs *find_mntfs(am_ops *, am_opts *, char *, char *, char *, char *, char *);
extern mntfs *locate_mntfs(am_ops *, am_opts *, char *, char *, char *, char *, char *);
extern am_loc *new_loc(void);
extern mntfs *new_mntfs(void);
extern mntfs *realloc_mntfs(mntfs *, am_ops *, am_opts *, char *, char *, char *, char *, char *);
extern void flush_mntfs(void);
extern void free_mntfs(voidp);
extern void free_loc(voidp);


extern void amq_program_1(struct svc_req *rqstp, SVCXPRT *transp);
extern int  background(void);
extern void deslashify(char *);
extern void do_task_notify(void);
extern int  eval_fs_opts(am_opts *, char *, char *, char *, char *, char *);
extern int  file_read_line(char *, int, FILE *);
extern void forcibly_timeout_mp(am_node *);
extern void free_map(am_node *);
extern void free_opts(am_opts *);
extern am_opts *copy_opts(am_opts *);
extern void free_srvr(fserver *);
extern int  fwd_init(void);
extern int  fwd_packet(int, char *, int, struct sockaddr_in *, struct sockaddr_in *, opaque_t, fwd_fun *);
extern void fwd_reply(void);
extern void get_args(int argc, char *argv[]);
extern wchan_t get_mntfs_wchan(mntfs *mf);
extern void host_normalize(char **);
extern void init_map(am_node *, char *);
extern void ins_que(qelem *, qelem *);
extern void insert_am(am_node *, am_node *);
extern int  make_nfs_auth(void);
extern void make_root_node(void);
extern void map_flush_srvr(fserver *);
extern void mapc_add_kv(mnt_map *, char *, char *);
extern mnt_map *mapc_find(char *, char *, const char *, const char *);
extern void mapc_free(opaque_t);
extern int  mapc_keyiter(mnt_map *, key_fun, opaque_t);
extern void mapc_reload(void);
extern int  mapc_search(mnt_map *, char *, char **);
extern void mapc_showtypes(char *buf, size_t l);
extern int  mapc_type_exists(const char *type);
extern void mk_fattr(nfsfattr *, nfsftype);
extern int  mount_auto_node(char *, opaque_t);
extern int  mount_automounter(int);
extern int  mount_exported(void);
extern void mp_to_fh(am_node *, am_nfs_fh *);
extern void mp_to_fh3(am_node *mp, am_nfs_fh3 *fhp);
extern void new_ttl(am_node *);
extern void nfs_quick_reply(am_node *mp, int error);
extern void normalize_slash(char *);
extern void notify_child(am_node *, au_etype, int, int);
extern void ops_showamfstypes(char *buf, size_t l);
extern void ops_showfstypes(char *outbuf, size_t l);
extern void rem_que(qelem *);
extern void reschedule_timeout_mp(void);
extern void restart(void);
extern void restart_automounter_nodes(void);
extern int  root_keyiter(key_fun *, opaque_t);
extern void root_newmap(const char *, const char *, const char *, const cf_map_t *);
extern void run_task(task_fun *, opaque_t, cb_fun *, opaque_t);
extern void sched_task(cb_fun *, opaque_t, wchan_t);
extern int  softclock(void);
extern int  timeout(u_int, void (*fn)(opaque_t), opaque_t);
extern void timeout_mp(opaque_t);
extern void untimeout(int);
extern void umount_exported(void);
extern int  valid_key(char *);
extern void wakeup(wchan_t);
extern void wakeup_srvr(fserver *);
extern void wakeup_task(int, int, wchan_t);
#define SIZEOF_PID_FSNAME	(16 + MAXHOSTNAMELEN)
extern char pid_fsname[SIZEOF_PID_FSNAME]; /* "kiska.southseas.nz:(pid%d)" */
#define SIZEOF_HOSTD (2 * MAXHOSTNAMELEN + 1)
extern char hostd[SIZEOF_HOSTD]; /* Host+domain */
#define SIZEOF_OPTS 256		/* used for char opts[] and preopts[] */

/*
 * Global variables.
 */
extern SVCXPRT *current_transp; /* For nfs_quick_reply() */
extern dispatcher_t nfs_dispatcher;
extern char *conf_tag;
#define SIZEOF_UID_STR	12
#define SIZEOF_GID_STR	12
extern char *opt_gid, gid_str[SIZEOF_GID_STR];
extern char *opt_uid, uid_str[SIZEOF_UID_STR];
extern int NumChildren;
extern int fwd_sock;
extern int select_intr_valid;
extern int immediate_abort;	/* Should close-down unmounts be retried */
extern int usage;
extern int use_conf_file;	/* use amd configuration file */
extern int task_notify_todo;	/* Task notifier needs running */
extern jmp_buf select_intr;
extern qelem mfhead;
extern struct amu_global_options gopt; /* where global options are stored */
extern time_t do_mapc_reload;	/* Flush & reload mount map cache */
extern time_t next_softclock;	/* Time to call softclock() */

#ifdef HAVE_SIGACTION
extern sigset_t masked_sigs;
#endif /* HAVE_SIGACTION */

#if defined(HAVE_AMU_FS_LINK) || defined(HAVE_AMU_FS_LINKX)
extern char *amfs_link_match(am_opts *fo);
#endif /* defined(HAVE_AMU_FS_LINK) || defined(HAVE_AMU_FS_LINKX) */

#ifdef HAVE_FS_AUTOFS
extern int amd_use_autofs;

extern int autofs_get_fh(am_node *mp);
extern void autofs_release_fh(am_node *mp);
extern void autofs_get_mp(am_node *mp);
extern void autofs_release_mp(am_node *mp);
extern void autofs_add_fdset(fd_set *readfds);
extern int autofs_handle_fdset(fd_set *readfds, int nsel);
extern void autofs_mounted(am_node *mp);
extern void autofs_mount_succeeded(am_node *mp);
extern void autofs_mount_failed(am_node *mp);
extern int autofs_umount_succeeded(am_node *mp);
extern int autofs_umount_failed(am_node *mp);
extern int autofs_mount_fs(am_node *mp, mntfs *mf);
extern int autofs_umount_fs(am_node *mp, mntfs *mf);
extern void autofs_get_opts(char *opts, size_t l, autofs_fh_t *fh);
extern int autofs_compute_mount_flags(mntent_t *);
extern void autofs_timeout_mp(am_node *);
extern int create_autofs_service(void);
extern int destroy_autofs_service(void);
#endif /* HAVE_FS_AUTOFS */

/**************************************************************************/
/*** Generic file-system types, implemented as part of the native O/S.	***/
/**************************************************************************/

/*
 * Loopback File System
 * Many systems can't support this, and in any case most of the
 * functionality is available with Symlink FS.
 */
#ifdef HAVE_FS_LOFS
extern am_ops lofs_ops;
extern int mount_lofs(char *mntdir, char *fs_name, char *opts, int on_autofs);
#endif /* HAVE_FS_LOFS */

/*
 * CD-ROM File System (CD-ROM)
 * (HSFS: High Sierra F/S on some machines)
 * Many systems can't support this, and in any case most of the
 * functionality is available with program FS.
 */
#ifdef HAVE_FS_CDFS
extern am_ops cdfs_ops;
#endif /* HAVE_FS_CDFS */

/*
 * PC File System (MS-DOS)
 * Many systems can't support this, and in any case most of the
 * functionality is available with program FS.
 */
#ifdef HAVE_FS_PCFS
extern am_ops pcfs_ops;
#endif /* HAVE_FS_PCFS */

/*
 * UDF File System
 * Many systems can't support this, and in any case most of the
 * functionality is available with program FS.
 */
#ifdef HAVE_FS_UDF
extern am_ops udf_ops;
#endif /* HAVE_FS_UDF */

#ifdef HAVE_FS_LUSTRE
extern am_ops lustre_ops;
#endif /* HAVE_FS_LUSTRE */

/*
 * Caching File System (Solaris)
 */
#ifdef HAVE_FS_CACHEFS
extern am_ops cachefs_ops;
#endif /* HAVE_FS_CACHEFS */

/*
 * In memory /tmp filesystem (Linux, NetBSD)
 */
#ifdef HAVE_FS_TMPFS
extern am_ops tmpfs_ops;
#endif /* HAVE_FS_TMPFS */
/*
 * Network File System
 * Good, slow, NFS V.2.
 */
#ifdef HAVE_FS_NFS
extern am_ops nfs_ops;		/* NFS */
extern fserver *find_nfs_srvr (mntfs *);
extern qelem nfs_srvr_list;
#endif /* HAVE_FS_NFS */

/*
 * Un*x File System
 * Normal local disk file system.
 */
#ifdef HAVE_FS_UFS
extern am_ops ufs_ops;		/* Un*x file system */
#endif /* HAVE_FS_UFS */

/* Unix file system (irix) */
#ifdef HAVE_FS_XFS
extern am_ops xfs_ops;		/* Un*x file system */
#endif /* HAVE_FS_XFS */

/* Unix file system (ext*) */
#ifdef HAVE_FS_EXT
extern am_ops ext2_ops;		/* Un*x file system */
extern am_ops ext3_ops;		/* Un*x file system */
extern am_ops ext4_ops;		/* Un*x file system */
#endif /* HAVE_FS_EXT */

/* Unix file system (irix) */
#ifdef HAVE_FS_EFS
extern am_ops efs_ops;		/* Un*x file system */
#endif /* HAVE_FS_EFS */

/**************************************************************************/
/*** Automounter file-system types, implemented by amd.			***/
/**************************************************************************/

/*
 * Root AMD File System
 */
extern am_ops amfs_root_ops;	/* Root file system */

/*
 * Generic amfs helper methods
 */
extern am_node *amfs_generic_lookup_child(am_node *mp, char *fname, int *error_return, int op);
extern am_node *amfs_generic_mount_child(am_node *ap, int *error_return);
extern int amfs_generic_readdir(am_node *mp, voidp cookie, voidp dp, voidp ep, u_int count);
extern int amfs_generic_umount(am_node *mp, mntfs *mf);
extern void amfs_generic_mounted(mntfs *mf);
extern char *amfs_generic_match(am_opts *fo);
extern fserver *amfs_generic_find_srvr(mntfs *);

/*
 * Automount File System
 */
#ifdef HAVE_AMU_FS_AUTO
extern am_ops amfs_auto_ops;	/* Automount file system (this!) */
#endif /* HAVE_AMU_FS_AUTO */

/*
 * Toplvl Automount File System
 */
#ifdef HAVE_AMU_FS_TOPLVL
extern am_ops amfs_toplvl_ops;	/* Toplvl Automount file system */
extern int amfs_toplvl_mount(am_node *mp, mntfs *mf);
extern int amfs_toplvl_umount(am_node *mp, mntfs *mf);
#endif /* HAVE_AMU_FS_TOPLVL */

/*
 * Direct Automount File System
 */
#ifdef HAVE_AMU_FS_DIRECT
extern am_ops amfs_direct_ops;	/* Direct Automount file system (this too) */
#endif /* HAVE_AMU_FS_DIRECT */

/*
 * Error File System
 */
#ifdef HAVE_AMU_FS_ERROR
extern am_ops amfs_error_ops;	/* Error file system */
extern am_node *amfs_error_lookup_child(am_node *mp, char *fname, int *error_return, int op);
extern am_node *amfs_error_mount_child(am_node *ap, int *error_return);
extern int amfs_error_readdir(am_node *mp, voidp cookie, voidp dp, voidp ep, u_int count);

#endif /* HAVE_AMU_FS_ERROR */

/*
 * NFS mounts with local existence check.
 */
#ifdef HAVE_AMU_FS_NFSL
extern am_ops amfs_nfsl_ops;	/* NFSL */
#endif /* HAVE_AMU_FS_NFSL */

/*
 * Multi-nfs mounts.
 */
#ifdef HAVE_AMU_FS_NFSX
extern am_ops amfs_nfsx_ops;	/* NFSX */
#endif /* HAVE_AMU_FS_NFSX */

/*
 * NFS host - a whole tree.
 */
#ifdef HAVE_AMU_FS_HOST
extern am_ops amfs_host_ops;	/* NFS host */
#endif /* HAVE_AMU_FS_HOST */

/*
 * Program File System
 * This is useful for things like RVD.
 */
#ifdef HAVE_AMU_FS_PROGRAM
extern am_ops amfs_program_ops;	/* Program File System */
#endif /* HAVE_AMU_FS_PROGRAM */

/*
 * Symbolic-link file system.
 * A "filesystem" which is just a symbol link.
 */
#ifdef HAVE_AMU_FS_LINK
extern am_ops amfs_link_ops;	/* Symlink FS */
#endif /* HAVE_AMU_FS_LINK */

/*
 * Symbolic-link file system, which also checks that the target of
 * the symlink exists.
 * A "filesystem" which is just a symbol link.
 */
#ifdef HAVE_AMU_FS_LINKX
extern am_ops amfs_linkx_ops;	/* Symlink FS with existence check */
#endif /* HAVE_AMU_FS_LINKX */

/*
 * Union file system
 */
#ifdef HAVE_AMU_FS_UNION
extern am_ops amfs_union_ops;	/* Union FS */
#endif /* HAVE_AMU_FS_UNION */

#endif /* not _AMD_H */
