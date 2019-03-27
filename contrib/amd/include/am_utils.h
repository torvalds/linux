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
 * File: am-utils/include/am_utils.h
 *
 */

/*
 * Definitions that are specific to the am-utils package.
 */

#ifndef _AM_UTILS_H
#define _AM_UTILS_H


#include "aux_conf.h"

/**************************************************************************/
/*** MACROS								***/
/**************************************************************************/

/*
 * General macros.
 */
#ifndef FALSE
# define FALSE 0
#endif /* not FALSE */
#ifndef TRUE
# define TRUE 1
#endif /* not TRUE */
#ifndef MAX
# define MAX(a, b)	((a) > (b) ? (a) : (b))
#endif /* not MAX */
#ifndef MIN
# define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif /* not MIN */

#define	ONE_HOUR	(60 * 60)	/* One hour in seconds */

#ifndef MAXHOSTNAMELEN
# ifdef HOSTNAMESZ
#  define MAXHOSTNAMELEN HOSTNAMESZ
# else /* not HOSTNAMESZ */
#  define MAXHOSTNAMELEN 256
# endif /* not HOSTNAMESZ */
#endif /* not MAXHOSTNAMELEN */

/*
 * for hlfsd, and amd for detecting uid/gid
 */
#ifndef INVALIDID
/* this is also defined in include/am_utils.h */
# define INVALIDID	(((unsigned short) ~0) - 3)
#endif /* not INVALIDID */

/*
 * String comparison macros
 */
#define STREQ(s1, s2)		(strcmp((s1), (s2)) == 0)
#define STRCEQ(s1, s2)		(strcasecmp((s1), (s2)) == 0)
#define NSTREQ(s1, s2, n)	(strncmp((s1), (s2), (n)) == 0)
#define NSTRCEQ(s1, s2, n)	(strncasecmp((s1), (s2), (n)) == 0)
#define FSTREQ(s1, s2)		((*(s1) == *(s2)) && STREQ((s1),(s2)))

/*
 * Logging options/flags
 */
#define	XLOG_FATAL	0x0001
#define	XLOG_ERROR	0x0002
#define	XLOG_USER	0x0004
#define	XLOG_WARNING	0x0008
#define	XLOG_INFO	0x0010
#define	XLOG_DEBUG	0x0020
#define	XLOG_MAP	0x0040
#define	XLOG_STATS	0x0080
/* log option compositions */
#define XLOG_MASK	0x00ff	/* mask for all flags */
#define XLOG_MANDATORY	(XLOG_FATAL|XLOG_ERROR)	/* cannot turn these off */
#define XLOG_ALL	(XLOG_FATAL|XLOG_ERROR|XLOG_USER|XLOG_WARNING|XLOG_INFO|XLOG_MAP|XLOG_STATS)
/* default: fatal + error + user + warning + info */
#define XLOG_DEFAULT	(XLOG_MASK & (XLOG_ALL & ~XLOG_MAP & ~XLOG_STATS))

/* default: no logging options */

#define NO_SUBNET	"notknown"   /* default subnet name for no subnet */
#define	NEXP_AP		(1022)			/* gdmr: was 254 */
#define NEXP_AP_MARGIN	(128)			/* ???? not used */

/*
 * Linked list macros
 */
#define	AM_FIRST(ty, q)	((ty *) ((q)->q_forw))
#define	AM_LAST(ty, q)	((ty *) ((q)->q_back))
#define	NEXT(ty, q)	((ty *) (((qelem *) q)->q_forw))
#define	PREV(ty, q)	((ty *) (((qelem *) q)->q_back))
#define	HEAD(ty, q)	((ty *) q)
#define	ITER(v, ty, q) \
	for ((v) = AM_FIRST(ty,(q)); (v) != HEAD(ty,(q)); (v) = NEXT(ty,(v)))

/* allocate anything of type ty */
#define	ALLOC(ty)	((ty *) xmalloc(sizeof(ty)))
#define	CALLOC(ty)	((ty *) xzalloc(sizeof(ty)))
/* simply allocate b bytes */
#define	SALLOC(b)	xmalloc((b))

/*
 * Systems which have the mount table in a file need to read it before
 * they can perform an unmount() system call.
 */
#define UMOUNT_FS(dir, mtb_name, unmount_flags)	umount_fs(dir, mtb_name, unmount_flags)
/* next two are imported via $srcdir/conf/umount/umount_*.c */
extern int umount_fs(char *mntdir, const char *mnttabname, u_int unmount_flags);
#ifdef MNT2_GEN_OPT_FORCE
extern int umount2_fs(const char *mntdir, u_int unmount_flags);
#endif /* MNT2_GEN_OPT_FORCE */

/* unmount-related flags (special handling of autofs, forced/lazy, etc.) */
#define AMU_UMOUNT_FORCE        0x1
#define AMU_UMOUNT_DETACH       0x2
#define AMU_UMOUNT_AUTOFS       0x4

/*
 * The following values can be tuned...
 */
#define	ALLOWED_MOUNT_TIME	40	/* 40s for a mount */

/*
 * RPC-related macros.
 */
#define	RPC_XID_PORTMAP		0
#define	RPC_XID_MOUNTD		1
#define	RPC_XID_NFSPING		2
#define	RPC_XID_WEBNFS		3
#define	RPC_XID_MASK		(0x0f)	/* 16 id's for now */
#define	MK_RPC_XID(type_id, uniq)	((type_id) | ((uniq) << 4))

/*
 * What level of AMD are we backward compatible with?
 * This only applies to externally visible characteristics.
 * Rev.Minor.Branch.Patch (2 digits each)
 */
#define	AMD_COMPAT	5000000	/* 5.0 */


/**************************************************************************/
/*** STRUCTURES AND TYPEDEFS						***/
/**************************************************************************/

/* some typedefs must come first */
typedef char *amq_string;
typedef struct _qelem qelem;
typedef struct mntlist mntlist;

/*
 * Linked list
 * (the name 'struct qelem' conflicts with linux's unistd.h)
 */
struct _qelem {
  qelem *q_forw;
  qelem *q_back;
};

/*
 * Option tables
 */
struct opt_tab {
  char *opt;
  int flag;
};

/*
 * Server states
 */
typedef enum {
  Start,
  Run,
  Finishing,
  Quit,
  Done
} serv_state;


/*
 * List of mount table entries
 */
struct mntlist {
  struct mntlist *mnext;
  mntent_t *mnt;
};

/*
 * Mount map
 */
typedef struct mnt_map mnt_map;


/**************************************************************************/
/*** EXTERNALS								***/
/**************************************************************************/

/*
 * Useful constants
 */
extern char *mnttab_file_name;	/* Mount table */
extern char *cpu;		/* "CPU type" */
extern char *endian;		/* "big" */
extern char *hostdomain;	/* "southseas.nz" */
extern char copyright[];	/* Copyright info */
extern char version[];		/* Version info */

/*
 * Global variables.
 */
extern AUTH *nfs_auth;		/* Dummy authorization for remote servers */
extern FILE *logfp;		/* Log file */
extern SVCXPRT *nfsxprt;
extern char *PrimNetName;	/* Name of primary connected network */
extern char *PrimNetNum;	/* Name of primary connected network */
extern char *SubsNetName;	/* Name of subsidiary connected network */
extern char *SubsNetNum;	/* Name of subsidiary connected network */

extern void am_set_progname(char *pn); /* "amd" */
extern const char *am_get_progname(void); /* "amd" */
extern void am_set_hostname(char *hn);
extern const char *am_get_hostname(void);
extern pid_t am_set_mypid(void);
extern pid_t am_mypid;

extern int foreground;		/* Foreground process */
extern int orig_umask;		/* umask() on startup */
extern serv_state amd_state;	/* Should we go now */
extern struct in_addr myipaddr;	/* (An) IP address of this host */
extern struct opt_tab xlog_opt[];
extern u_short nfs_port;	/* Our NFS service port */

/*
 * Global routines
 */
extern CLIENT *get_mount_client(char *unused_host, struct sockaddr_in *sin, struct timeval *tv, int *sock, u_long mnt_version);
extern RETSIGTYPE sigchld(int);
extern bool_t xdr_amq_string(XDR *xdrs, amq_string *objp);
extern bool_t xdr_dirpath(XDR *xdrs, dirpath *objp);
extern char **strsplit(char *, int, int);
extern char *expand_selectors(char *);
extern char *get_version_string(void);
extern char *inet_dquad(char *, size_t, u_long);
extern char *print_wires(void);
extern char *str3cat(char *, char *, char *, char *);
extern char *strvcat(const char *, ...);
extern char *strealloc(char *, char *);
extern char *strip_selectors(char *, char *);
extern char *strnsave(const char *, int);
extern int amu_close(int fd);
extern int bind_resv_port(int, u_short *);
extern int cmdoption(char *, struct opt_tab *, u_int *);
extern int compute_automounter_mount_flags(mntent_t *);
extern int compute_mount_flags(mntent_t *);
extern void discard_nfs_args(void *, u_long);
extern u_long get_amd_program_number(void);
extern int getcreds(struct svc_req *, uid_t *, gid_t *, SVCXPRT *);
extern int hasmntval(mntent_t *, char *);
extern unsigned int hasmntvalerr(mntent_t *, char *, int *);
extern char *hasmntstr(mntent_t *, char *);
extern char *hasmnteq(mntent_t *, char *);
extern char *haseq(char *);
extern int is_network_member(const char *net);
extern int is_interface_local(u_long);
extern int islocalnet(u_long);
extern int make_rpc_packet(char *, int, u_long, struct rpc_msg *, voidp, XDRPROC_T_TYPE, AUTH *);
extern int mkdirs(char *, int);
extern int mount_fs(mntent_t *, int, caddr_t, int, MTYPE_TYPE, u_long, const char *, const char *, int);
extern void nfs_program_2(struct svc_req *rqstp, SVCXPRT *transp);
extern void nfs_program_3(struct svc_req *rqstp, SVCXPRT *transp);
#define get_nfs_dispatcher_version(a) \
    ((a) == nfs_program_2 ? NFS_VERSION : NFS_VERSION3)
extern int pickup_rpc_reply(voidp, int, voidp, XDRPROC_T_TYPE);
extern int switch_option(char *);
extern int switch_to_logfile(char *logfile, int orig_umask, int truncate_log);
extern mntlist *read_mtab(char *, const char *);
#ifndef HAVE_TRANSPORT_TYPE_TLI
extern struct sockaddr_in *amu_svc_getcaller(SVCXPRT *xprt);
#endif /* not HAVE_TRANSPORT_TYPE_TLI */
extern time_t time(time_t *);
extern void amu_get_myaddress(struct in_addr *iap, const char *preferred_localhost);
extern void amu_release_controlling_tty(void);
extern void compute_automounter_nfs_args(nfs_args_t *nap, mntent_t *mntp);
extern void discard_mntlist(mntlist *mp);
extern void free_mntlist(mntlist *);
extern void getwire(char **name1, char **number1);
extern void going_down(int);
extern void mnt_free(mntent_t *);
extern void plog(int, const char *,...)
     __attribute__ ((__format__ (__printf__, 2, 3)));
extern void rmdirs(char *);
extern void rpc_msg_init(struct rpc_msg *, u_long, u_long, u_long);
extern void set_amd_program_number(u_long program);
extern void show_opts(int ch, struct opt_tab *);
extern void unregister_amq(void);
extern voidp xmalloc(int);
extern voidp xrealloc(voidp, int);
extern voidp xzalloc(int);
extern char *xstrdup(const char *);
extern int check_pmap_up(char *host, struct sockaddr_in* sin);
extern u_long get_nfs_version(char *host, struct sockaddr_in *sin, u_long nfs_version, const char *proto, u_long def);
extern int nfs_valid_version(u_long vers);
extern long get_server_pid(void);
extern void setup_sighandler(int signum, void (*handler)(int));
extern time_t clocktime(nfstime *nt);

#if defined(DEBUG) && (defined(HAVE_C99_VARARGS_MACROS) || defined(HAVE_GCC_VARARGS_MACROS))
# ifdef HAVE_C99_VARARGS_MACROS
#define xsnprintf(str,size,fmt,...)	_xsnprintf(__FILE__,__LINE__,(str),(size),(fmt),__VA_ARGS__)
# endif /* HAVE_C99_VARARGS_MACROS */
# ifdef HAVE_GCC_VARARGS_MACROS
#define xsnprintf(str,size,fmt,args...)		_xsnprintf(__FILE__,__LINE__,(str),(size),(fmt),args)
# endif /* HAVE_GCC_VARARGS_MACROS */
extern int _xsnprintf(const char *filename, int lineno, char *str, size_t size, const char *format, ...);
#define xvsnprintf(str,size,fmt,ap)	_xvsnprintf(__FILE__,__LINE__,(str),(size),(fmt),(ap))
extern int _xvsnprintf(const char *filename, int lineno, char *str, size_t size, const char *format, va_list ap);
#else /* not DEBUG or no C99/GCC-style vararg cpp macros supported */
extern int xsnprintf(char *str, size_t size, const char *format, ...);
extern int xvsnprintf(char *str, size_t size, const char *format, va_list ap);
#endif /* not DEBUG or no C99/GCC-style vararg cpp macros supported */

#ifdef DEBUG
extern void _xstrlcat(const char *filename, int lineno, char *dst, const char *src, size_t len);
# define xstrlcat(d,s,l)	_xstrlcat(__FILE__,__LINE__,(d),(s),(l))
extern void _xstrlcpy(const char *filename, int lineno, char *dst, const char *src, size_t len);
# define xstrlcpy(d,s,l)	_xstrlcpy(__FILE__,__LINE__,(d),(s),(l))
#else /* not DEBUG */
extern void xstrlcat(char *dst, const char *src, size_t len);
extern void xstrlcpy(char *dst, const char *src, size_t len);
#endif /* not DEBUG */

#ifdef MOUNT_TABLE_ON_FILE
extern void rewrite_mtab(mntlist *, const char *);
extern void unlock_mntlist(void);
extern void write_mntent(mntent_t *, const char *);
#endif /* MOUNT_TABLE_ON_FILE */

#if defined(HAVE_SYSLOG_H) || defined(HAVE_SYS_SYSLOG_H)
extern int syslogging;
#endif /* defined(HAVE_SYSLOG_H) || defined(HAVE_SYS_SYSLOG_H) */

extern void compute_nfs_args(void *nap, mntent_t *mntp, int genflags, struct netconfig *nfsncp, struct sockaddr_in *ip_addr, u_long nfs_version, char *nfs_proto, am_nfs_handle_t *fhp, char *host_name, char *fs_name);
extern void destroy_nfs_args(void *nap, u_long nfs_version);
extern int create_amq_service(int *udp_soAMQp, SVCXPRT **udp_amqpp, struct netconfig **udp_amqncpp, int *tcp_soAMQp, SVCXPRT **tcp_amqpp, struct netconfig **tcp_amqncpp, u_short preferred_amq_port);
extern int create_nfs_service(int *soNFSp, u_short *nfs_portp, SVCXPRT **nfs_xprtp, void (*dispatch_fxn)(struct svc_req *rqstp, SVCXPRT *transp), u_long nfs_version);
extern int amu_svc_register(SVCXPRT *, u_long, u_long, void (*)(struct svc_req *, SVCXPRT *), u_long, struct netconfig *);

#ifdef HAVE_TRANSPORT_TYPE_TLI

extern int get_knetconfig(struct knetconfig **kncpp, struct netconfig *in_ncp, char *nc_protoname);
extern struct netconfig *nfsncp;
extern void free_knetconfig(struct knetconfig *kncp);

#endif /* HAVE_TRANSPORT_TYPE_TLI */

#ifdef HAVE_FS_AUTOFS
extern int register_autofs_service(char *autofs_conftype, void (*autofs_dispatch)(struct svc_req *rqstp, SVCXPRT *xprt));
extern int unregister_autofs_service(char *autofs_conftype);
#endif /* HAVE_FS_AUTOFS */


#ifndef HAVE_STRUCT_FHSTATUS_FHS_FH
# define fhs_fh  fhstatus_u.fhs_fhandle
#endif /* not HAVE_STRUCT_FHSTATUS_FHS_FH */


/*
 * Network File System: the old faithful generation NFS V.2
 */
#ifndef NFS_VERSION2
# define NFS_VERSION2 ((u_int) 2)
#endif /* not NFS_VERSION2 */

/*
 * Network File System: the not so new anymore generation NFS V.3
 */
#ifdef HAVE_FS_NFS3
# ifndef NFS_VERSION3
#  define NFS_VERSION3 ((u_int) 3)
# endif /* not NFS_VERSION3 */
#endif /* HAVE_FS_NFS3 */

/*
 * Network File System: the new generation NFS V.4
 */
#ifdef HAVE_FS_NFS4
# ifndef NFS_VERSION4
#  define NFS_VERSION4 ((u_int) 4)
# endif /* not NFS_VERSION4 */
#endif /* HAVE_FS_NFS4 */

/**************************************************************************/
/*** DEBUGGING								***/
/**************************************************************************/

/*
 * DEBUGGING:
 */

#ifdef DEBUG

# define	D_DAEMON	0x0001	/* Enter daemon mode */
# define	D_TRACE		0x0002	/* Do protocol trace */
# define	D_FULL		0x0004	/* Do full trace */
# define	D_MTAB		0x0008	/* Use local mtab */
# define	D_AMQ		0x0010	/* Register amq program */
# define	D_STR		0x0020	/* Debug string munging */
# ifdef DEBUG_MEM
#  define	D_MEM		0x0040	/* Trace memory allocations */
# else /* not DEBUG_MEM */
#  define	D_MEM		0x0000	/* Dummy */
# endif /* not DEBUG_MEM */
# define	D_FORK		0x0080	/* Fork server (hlfsd only) */
# define	D_INFO		0x0100	/* info service specific debugging (hesiod, nis, etc) */
# define	D_HRTIME	0x0200	/* Print high resolution time stamps */
# define	D_XDRTRACE	0x0400	/* Trace xdr routines */
# define	D_READDIR	0x0800	/* Show browsable_dir progress */
/* debug option compositions */
# define	D_MASK		0x0fff  /* mask of known flags */
# define	D_BASIC		(D_TRACE|D_FULL|D_STR|D_MEM|D_INFO|D_XDRTRACE|D_READDIR)
# define	D_CONTROL	(D_DAEMON|D_AMQ|D_FORK)
/* immutable flags: cannot be changed via "amq -D" */
# define	D_IMMUTABLE	(D_MTAB  | D_CONTROL)
# define	D_ALL		(D_BASIC | D_CONTROL)
# define	D_DEFAULT	(D_MASK & D_ALL & ~D_XDRTRACE)
/* test mode: nodaemon, noamq, nofork, (local) mtab */
# define	D_TEST		(D_BASIC | D_MTAB)

# define	amuDebug(x)	(debug_flags & (x))
# define	dlog		if (amuDebug(D_FULL)) dplog

/* my favorite debugging tool -Erez */
#define EZKDBG plog(XLOG_INFO,"EZK:%s:%s:%d\n",__FILE__,__FUNCTION__,__LINE__)

# ifdef DEBUG_MEM
/*
 * If debugging memory, then call a special freeing function that logs
 * more info, and resets the pointer to NULL so it cannot be used again.
 */
#  define	XFREE(x) dxfree(__FILE__,__LINE__,x)
extern void dxfree(char *file, int line, voidp ptr);
extern void malloc_verify(void);
# else /* not DEBUG_MEM */
/*
 * If regular debugging, then free the pointer and reset to NULL.
 * This should remain so for as long as am-utils is in alpha/beta testing.
 */
#  define	XFREE(x) do { free((voidp)x); x = NULL;} while (0)
# endif /* not DEBUG_MEM */

/* functions that depend solely on debugging */
extern void print_nfs_args(const void *, u_long nfs_version);
extern int debug_option (char *opt);
extern void dplog(const char *fmt, ...)
     __attribute__ ((__format__ (__printf__, 1, 2)));

#else /* not DEBUG */

/* set dummy flags to zero */
# define	D_DAEMON	0x0001	/* Enter daemon mode */
# define	D_TRACE		0x0000	/* dummy: Do protocol trace */
# define	D_FULL		0x0000	/* dummy: Do full trace */
# define	D_MTAB		0x0000	/* dummy: Use local mtab */
# define	D_AMQ		0x0010	/* Register amq program */
# define	D_STR		0x0000	/* dummy: Debug string munging */
# define	D_MEM		0x0000	/* dummy: Trace memory allocations */
# define	D_FORK		0x0080	/* Fork server (hlfsd only) */
# define	D_INFO		0x0000	/* dummy: info service debugging */
# define	D_HRTIME	0x0000	/* dummy: hi-res time stamps */
# define	D_XDRTRACE	0x0000	/* dummy: Trace xdr routines */
# define	D_READDIR	0x0000	/* dummy: browsable_dir progress */
# define	D_CONTROL	(D_DAEMON|D_AMQ|D_FORK)
# define	amuDebug(x)	(debug_flags & (x))
/*
 * If not debugging, then also reset the pointer.
 * It's safer -- and besides, free() should do that anyway.
 */
# define	XFREE(x) do { free((voidp)x); x = NULL;} while (0)

# if defined(HAVE_GCC_VARARGS_MACROS)
#  define	dlog(fmt...)
# elif defined(HAVE_C99_VARARGS_MACROS)
#  define	dlog(...)
# else  /* no c99 varargs */
/* this define means that we CCP leaves code behind the (list,of,args)  */
#  define	dlog
# endif /* no c99 varargs */

# define	print_nfs_args(nap, nfs_version)
# define	debug_option(x)	(1)

#endif /* not DEBUG */

extern u_int debug_flags;	/* Debug options */
extern struct opt_tab dbg_opt[];

/**************************************************************************/
/*** MISC (stuff left to autoconfiscate)				***/
/**************************************************************************/

#endif /* not _AM_UTILS_H */
