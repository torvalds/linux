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
 * File: am-utils/include/am_defs.h
 * $Id: am_defs.h,v 1.15.2.16 2004/05/12 15:54:31 ezk Exp $
 * $FreeBSD$
 *
 */

/*
 * Definitions that are not specific to the am-utils package, but
 * are rather generic, and can be used elsewhere.
 */

#ifndef _AM_DEFS_H
#define _AM_DEFS_H

/*
 * Actions to take if ANSI C.
 */
#if STDC_HEADERS
# include <string.h>
/* for function prototypes */
# define P(x) x
# define P_void void
#else /* not STDC_HEADERS */
/* empty function prototypes */
# define P(x) ()
# define P_void
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif /* not HAVE_STRCHR */
char *strchr(), *strrchr();
#endif /* not STDC_HEADERS */

/*
 * Handle gcc __attribute__ if available.
 */
#ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5) || __STRICT_ANSI__
#  define __attribute__(Spec) /* empty */
# endif /* __GNUC__ < 2 ... */
/*
 * The __-protected variants of `format' and `printf' attributes
 * are accepted by gcc versions 2.6.4 (effectively 2.7) and later.
 */
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 7)
#  define __format__ format
#  define __printf__ printf
# endif /* __GNUC__ < 2 ... */
#endif /* not __attribute__ */

#define __IGNORE(result) \
    __ignore((unsigned long)result)

static inline void
__ignore(unsigned long result) {
    (void)&result;
}

/*
 * How to handle signals of any type
 */
#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif /* HAVE_SYS_WAIT_H */
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif /* not WEXITSTATUS */
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif /* not WIFEXITED */

/*
 * Actions to take regarding <time.h> and <sys/time.h>.
 */
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# ifdef _ALL_SOURCE
/*
 * AIX 5.2 needs struct sigevent from signal.h to be defined, but I
 * don't want to move the inclusion of signal.h this early into this
 * file.  Luckily, amd doesn't need the size of this structure in any
 * other structure that it uses.  So we sidestep it for now.
 */
struct sigevent;
# endif /* _ALL_SOURCE */
# include <time.h>
#else /* not TIME_WITH_SYS_TIME */
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else /* not HAVE_SYS_TIME_H */
#  include <time.h>
# endif /* not HAVE_SYS_TIME_H */
#endif /* not TIME_WITH_SYS_TIME */

/*
 * Actions to take if <machine/endian.h> exists.
 */
#ifdef HAVE_MACHINE_ENDIAN_H
# include <machine/endian.h>
#endif /* HAVE_MACHINE_ENDIAN_H */

/*
 * Big-endian or little-endian?
 */
#ifndef BYTE_ORDER
# if defined(WORDS_BIGENDIAN)
#  define ARCH_ENDIAN "big"
# else /* not WORDS_BIGENDIAN */
#  define ARCH_ENDIAN "little"
# endif /* not WORDS_BIGENDIAN */
#else
# if BYTE_ORDER == BIG_ENDIAN
#  define ARCH_ENDIAN "big"
# else
#  define ARCH_ENDIAN "little"
# endif
#endif

/*
 * Actions to take if HAVE_SYS_TYPES_H is defined.
 */
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

/*
 * Actions to take if HAVE_LIMITS_H is defined.
 */
#if HAVE_LIMITS_H_H
# include <limits.h>
#endif /* HAVE_LIMITS_H */

/*
 * Actions to take if HAVE_UNISTD_H is defined.
 */
#if HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */

/* after <unistd.h>, check if this is a POSIX.1 system */
#ifdef _POSIX_VERSION
/* Code for POSIX.1 systems. */
#endif /* _POSIX_VERSION */

/*
 * Variable length argument lists.
 * Must use only one of the two!
 */
#ifdef HAVE_STDARG_H
# include <stdarg.h>
/*
 * On Solaris 2.6, <sys/varargs.h> is included in <sys/fs/autofs.h>
 * So this ensures that only one is included.
 */
# ifndef _SYS_VARARGS_H
#  define _SYS_VARARGS_H
# endif /* not _SYS_VARARGS_H */
#else /* not HAVE_STDARG_H */
# ifdef HAVE_VARARGS_H
#  include <varargs.h>
# endif /* HAVE_VARARGS_H */
#endif /* not HAVE_STDARG_H */

/*
 * Pick the right header file and macros for directory processing functions.
 */
#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else /* not HAVE_DIRENT_H */
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif /* HAVE_SYS_NDIR_H */
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif /* HAVE_SYS_DIR_H */
# if HAVE_NDIR_H
#  include <ndir.h>
# endif /* HAVE_NDIR_H */
#endif /* not HAVE_DIRENT_H */

/*
 * Actions to take if HAVE_FCNTL_H is defined.
 */
#if HAVE_FCNTL_H
# include <fcntl.h>
#endif /* HAVE_FCNTL_H */

/*
 * Actions to take if HAVE_MEMORY_H is defined.
 */
#if HAVE_MEMORY_H
# include <memory.h>
#endif /* HAVE_MEMORY_H */

/*
 * Actions to take if HAVE_SYS_FILE_H is defined.
 */
#if HAVE_SYS_FILE_H
# include <sys/file.h>
#endif /* HAVE_SYS_FILE_H */

/*
 * Actions to take if HAVE_SYS_IOCTL_H is defined.
 */
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */

/*
 * Actions to take if HAVE_SYSLOG_H or HAVE_SYS_SYSLOG_H is defined.
 */
#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#else /* not HAVE_SYSLOG_H */
# if HAVE_SYS_SYSLOG_H
#  include <sys/syslog.h>
# endif /* HAVE_SYS_SYSLOG_H */
#endif /* HAVE_SYSLOG_H */

/*
 * Actions to take if <sys/param.h> exists.
 */
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */

/*
 * Actions to take if <sys/socket.h> exists.
 */
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */

/*
 * Actions to take if <rpc/rpc.h> exists.
 */
#ifdef HAVE_RPC_RPC_H
/*
 * Turn on PORTMAP, so that additional header files would get included
 * and the important definition for UDPMSGSIZE is included too.
 */
# ifndef PORTMAP
#  define PORTMAP
# endif /* not PORTMAP */
# include <rpc/rpc.h>
# ifndef XDRPROC_T_TYPE
typedef bool_t (*xdrproc_t) __P ((XDR *, __ptr_t, ...));
# endif /* not XDRPROC_T_TYPE */
#endif /* HAVE_RPC_RPC_H */

/*
 * Actions to take if <rpc/types.h> exists.
 */
#ifdef HAVE_RPC_TYPES_H
# include <rpc/types.h>
#endif /* HAVE_RPC_TYPES_H */

/*
 * Actions to take if <rpc/xdr.h> exists.
 */
/* Prevent multiple inclusion on Ultrix 4 */
#if defined(HAVE_RPC_XDR_H) && !defined(__XDR_HEADER__)
# include <rpc/xdr.h>
#endif /* defined(HAVE_RPC_XDR_H) && !defined(__XDR_HEADER__) */

/*
 * Actions to take if <malloc.h> exists.
 * Don't include malloc.h if stdlib.h exists, because modern
 * systems complain if you use malloc.h instead of stdlib.h.
 * XXX: let's hope there are no systems out there that need both.
 */
#if defined(HAVE_MALLOC_H) && !defined(HAVE_STDLIB_H)
# include <malloc.h>
#endif /* defined(HAVE_MALLOC_H) && !defined(HAVE_STDLIB_H) */

/*
 * Actions to take if <mntent.h> exists.
 */
#ifdef HAVE_MNTENT_H
/* some systems need <stdio.h> before <mntent.h> is included */
# ifdef HAVE_STDIO_H
#  include <stdio.h>
# endif /* HAVE_STDIO_H */
# include <mntent.h>
#endif /* HAVE_MNTENT_H */

/*
 * Actions to take if <sys/fsid.h> exists.
 */
#ifdef HAVE_SYS_FSID_H
# include <sys/fsid.h>
#endif /* HAVE_SYS_FSID_H */

/*
 * Actions to take if <sys/utsname.h> exists.
 */
#ifdef HAVE_SYS_UTSNAME_H
# include <sys/utsname.h>
#endif /* HAVE_SYS_UTSNAME_H */

/*
 * Actions to take if <sys/mntent.h> exists.
 */
#ifdef HAVE_SYS_MNTENT_H
# include <sys/mntent.h>
#endif /* HAVE_SYS_MNTENT_H */

/*
 * Actions to take if <ndbm.h> or <db1/ndbm.h> exist.
 * Should be included before <rpcsvc/yp_prot.h> because on some systems
 * like Linux, it also defines "struct datum".
 */
#ifdef HAVE_MAP_NDBM
# include NEW_DBM_H
# ifndef DATUM
/* ensure that struct datum is not included again from <rpcsvc/yp_prot.h> */
#  define DATUM
# endif /* not DATUM */
#endif /* HAVE_MAP_NDBM */

/*
 * Actions to take if <net/errno.h> exists.
 */
#ifdef HAVE_NET_ERRNO_H
# include <net/errno.h>
#endif /* HAVE_NET_ERRNO_H */

/*
 * Actions to take if <net/if.h> exists.
 */
#ifdef HAVE_NET_IF_H
# include <net/if.h>
#endif /* HAVE_NET_IF_H */

/*
 * Actions to take if <net/route.h> exists.
 */
#ifdef HAVE_NET_ROUTE_H
# include <net/route.h>
#endif /* HAVE_NET_ROUTE_H */

/*
 * Actions to take if <sys/mbuf.h> exists.
 */
#ifdef HAVE_SYS_MBUF_H
# include <sys/mbuf.h>
/*
 * OSF4 (DU-4.0) defines m_next and m_data also in <sys/mount.h> so I must
 # undefine them here to avoid conflicts.
 */
# ifdef m_next
#  undef m_next
# endif /* m_next */
# ifdef m_data
#  undef m_data
# endif /* m_data */
/*
 * AIX 3 defines MFREE and m_flags also in <sys/mount.h>.
 */
# ifdef m_flags
#  undef m_flags
# endif /* m_flags */
# ifdef MFREE
#  undef MFREE
# endif /* MFREE */
#endif /* HAVE_SYS_MBUF_H */

/*
 * Actions to take if <sys/mman.h> exists.
 */
#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif /* HAVE_SYS_MMAN_H */

/*
 * Actions to take if <netdb.h> exists.
 */
#ifdef HAVE_NETDB_H
# include <netdb.h>
#endif /* HAVE_NETDB_H */

/*
 * Actions to take if <netdir.h> exists.
 */
#ifdef HAVE_NETDIR_H
# include <netdir.h>
#endif /* HAVE_NETDIR_H */

/*
 * Actions to take if <net/if_var.h> exists.
 */
#ifdef HAVE_NET_IF_VAR_H
# include <net/if_var.h>
#endif /* HAVE_NET_IF_VAR_H */

/*
 * Actions to take if <netinet/if_ether.h> exists.
 */
#ifdef HAVE_NETINET_IF_ETHER_H
# include <netinet/if_ether.h>
#endif /* HAVE_NETINET_IF_ETHER_H */

/*
 * Actions to take if <netinet/in.h> exists.
 */
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */

/*
 * Actions to take if <rpcsvc/yp_prot.h> exists.
 */
#ifdef HAVE_RPCSVC_YP_PROT_H
# ifdef HAVE_BAD_HEADERS
/* avoid circular dependency in aix 4.3 with <rpcsvc/ypclnt.h> */
struct ypall_callback;
# endif /* HAVE_BAD_HEADERS */
# include <rpcsvc/yp_prot.h>
#endif /* HAVE_RPCSVC_YP_PROT_H */

/*
 * Actions to take if <rpcsvc/ypclnt.h> exists.
 */
#ifdef HAVE_RPCSVC_YPCLNT_H
# include <rpcsvc/ypclnt.h>
#endif /* HAVE_RPCSVC_YPCLNT_H */

/*
 * Actions to take if <sys/ucred.h> exists.
 */
#ifdef HAVE_SYS_UCRED_H
# include <sys/ucred.h>
#endif /* HAVE_SYS_UCRED_H */


/*
 * Actions to take if <sys/mount.h> exists.
 */
#ifdef HAVE_SYS_MOUNT_H
/*
 * Some operating systems must define these variables to get
 * NFS and other definitions included.
 */
# ifndef NFSCLIENT
#  define NFSCLIENT 1
# endif /* not NFSCLIENT */
# ifndef NFS
#  define NFS 1
# endif /* not NFS */
# ifndef PCFS
#  define PCFS 1
# endif /* not PCFS */
# ifndef LOFS
#  define LOFS 1
# endif /* not LOFS */
# ifndef RFS
#  define RFS 1
# endif /* not RFS */
# ifndef MSDOSFS
#  define MSDOSFS 1
# endif /* not MSDOSFS */
# ifndef MFS
#  define MFS 1
# endif /* not MFS */
# ifndef CD9660
#  define CD9660 1
# endif /* not CD9660 */
# include <sys/mount.h>
#endif /* HAVE_SYS_MOUNT_H */

#ifdef HAVE_SYS_VMOUNT_H
# include <sys/vmount.h>
#endif /* HAVE_SYS_VMOUNT_H */

/*
 * Actions to take if <linux/fs.h> exists.
 * There is no point in including this on a glibc2 system,
 * we're only asking for trouble
 */
#if defined HAVE_LINUX_FS_H && (!defined __GLIBC__ || __GLIBC__ < 2)
/*
 * There are various conflicts in definitions between RedHat Linux, newer
 * 2.2 kernels, and <netinet/in.h> and <linux/fs.h>.
 */
# ifdef HAVE_SOCKETBITS_H
/* conflicts with <socketbits.h> */
#  define _LINUX_SOCKET_H
#  undef BLKFLSBUF
#  undef BLKGETSIZE
#  undef BLKRAGET
#  undef BLKRASET
#  undef BLKROGET
#  undef BLKROSET
#  undef BLKRRPART
#  undef MS_MGC_VAL
#  undef MS_RMT_MASK
#  if defined(__GLIBC__) && __GLIBC__ >= 2
/* conflicts with <waitflags.h> */
#   undef WNOHANG
#   undef WUNTRACED
#  endif /* defined(__GLIBC__) && __GLIBC__ >= 2 */
/* conflicts with <statfsbuf.h> */
#  define _SYS_STATFS_H
# endif /* HAVE_SOCKETBITS_H */

# ifdef _SYS_WAIT_H
#  if defined(__GLIBC__) && __GLIBC__ >= 2
/* conflicts with <bits/waitflags.h> (RedHat/Linux 6.0 and kernels 2.2 */
#   undef WNOHANG
#   undef WUNTRACED
#  endif /* defined(__GLIBC__) && __GLIBC__ >= 2 */
# endif /* _SYS_WAIT_H */

# ifdef HAVE_LINUX_POSIX_TYPES_H
#  include <linux/posix_types.h>
# endif /* HAVE_LINUX_POSIX_TYPES_H */
# ifndef _LINUX_BYTEORDER_GENERIC_H
#  define _LINUX_BYTEORDER_GENERIC_H
# endif /* _LINUX_BYTEORDER_GENERIC_H */
/* conflicts with <sys/mount.h> in 2.[12] kernels */
# ifdef _SYS_MOUNT_H
#  undef BLKFLSBUF
#  undef BLKGETSIZE
#  undef BLKRAGET
#  undef BLKRASET
#  undef BLKROGET
#  undef BLKROSET
#  undef BLKRRPART
#  undef BLOCK_SIZE
#  undef MS_MANDLOCK
#  undef MS_MGC_VAL
#  undef MS_NOATIME
#  undef MS_NODEV
#  undef MS_NODIRATIME
#  undef MS_NOEXEC
#  undef MS_NOSUID
#  undef MS_RDONLY
#  undef MS_REMOUNT
#  undef MS_RMT_MASK
#  undef MS_SYNCHRONOUS
#  undef S_APPEND
#  undef S_IMMUTABLE
/* conflicts with <statfsbuf.h> */
#  define _SYS_STATFS_H
# endif /* _SYS_MOUNT_H */
# ifndef _LINUX_STRING_H_
#  define _LINUX_STRING_H_
# endif /* not _LINUX_STRING_H_ */
# ifdef HAVE_LINUX_KDEV_T_H
#  define __KERNEL__
#  include <linux/kdev_t.h>
#  undef __KERNEL__
# endif /* HAVE_LINUX_KDEV_T_H */
# ifdef HAVE_LINUX_LIST_H
#  define __KERNEL__
#  include <linux/list.h>
#  undef __KERNEL__
# endif /* HAVE_LINUX_LIST_H */
# include <linux/fs.h>
#endif /* HAVE_LINUX_FS_H && (!__GLIBC__ || __GLIBC__ < 2) */

#ifdef HAVE_CDFS_CDFS_MOUNT_H
# include <cdfs/cdfs_mount.h>
#endif /* HAVE_CDFS_CDFS_MOUNT_H */

#ifdef HAVE_CDFS_CDFSMOUNT_H
# include <cdfs/cdfsmount.h>
#endif /* HAVE_CDFS_CDFSMOUNT_H */

/*
 * Actions to take if <linux/loop.h> exists.
 */
#ifdef HAVE_LINUX_LOOP_H
# ifdef HAVE_LINUX_POSIX_TYPES_H
#  include <linux/posix_types.h>
# endif /* HAVE_LINUX_POSIX_TYPES_H */
/* next dev_t lines needed due to changes in kernel code */
# undef dev_t
# define dev_t unsigned short	/* compatible with Red Hat and SuSE */
# include <linux/loop.h>
#endif /* HAVE_LINUX_LOOP_H */

/*
 * AUTOFS PROTOCOL HEADER FILES:
 */

/*
 * Actions to take if <linux/auto_fs[4].h> exists.
 * We really don't want <linux/fs.h> pulled in here
 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H
#endif /* _LINUX_FS_H */
#ifdef HAVE_LINUX_AUTO_FS4_H
# include <linux/auto_fs4.h>
#else  /* not HAVE_LINUX_AUTO_FS4_H */
# ifdef HAVE_LINUX_AUTO_FS_H
#  include <linux/auto_fs.h>
# endif /* HAVE_LINUX_AUTO_FS_H */
#endif /* not HAVE_LINUX_AUTO_FS4_H */

/*
 * Actions to take if <sys/fs/autofs.h> exists.
 */
#ifdef HAVE_SYS_FS_AUTOFS_H
# include <sys/fs/autofs.h>
#endif /* HAVE_SYS_FS_AUTOFS_H */

/*
 * Actions to take if <rpcsvc/autofs_prot.h> or <sys/fs/autofs_prot.h> exist.
 */
#ifdef HAVE_RPCSVC_AUTOFS_PROT_H
# include <rpcsvc/autofs_prot.h>
#else  /* not HAVE_RPCSVC_AUTOFS_PROT_H */
# ifdef HAVE_SYS_FS_AUTOFS_PROT_H
#  include <sys/fs/autofs_prot.h>
# endif /* HAVE_SYS_FS_AUTOFS_PROT_H */
#endif /* not HAVE_RPCSVC_AUTOFS_PROT_H */

/*
 * Actions to take if <lber.h> exists.
 * This header file is required before <ldap.h> can be included.
 */
#ifdef HAVE_LBER_H
# include <lber.h>
#endif /* HAVE_LBER_H */

/*
 * Actions to take if <ldap.h> exists.
 */
#ifdef HAVE_LDAP_H
# include <ldap.h>
#endif /* HAVE_LDAP_H */

/****************************************************************************
 ** IMPORTANT!!!							   **
 ** We always include am-utils' amu_autofs_prot.h.			   **
 ** That is actually defined in "conf/autofs/autofs_${autofs_style}.h"     **
 ****************************************************************************/
#include <amu_autofs_prot.h>


/*
 * NFS PROTOCOL HEADER FILES:
 */

/*
 * Actions to take if <nfs/export.h> exists.
 */
#ifdef HAVE_NFS_EXPORT_H
# include <nfs/export.h>
#endif /* HAVE_NFS_EXPORT_H */

/****************************************************************************
 ** IMPORTANT!!!							   **
 ** We always include am-utils' amu_nfs_prot.h.				   **
 ** That is actually defined in "conf/nfs_prot/nfs_prot_${host_os_name}.h" **
 ****************************************************************************/
#include <amu_nfs_prot.h>

/*
 * DO NOT INCLUDE THESE FILES:
 * They conflicts with other NFS headers and are generally not needed.
 */
#ifdef DO_NOT_INCLUDE
# ifdef HAVE_NFS_NFS_CLNT_H
#  include <nfs/nfs_clnt.h>
# endif /* HAVE_NFS_NFS_CLNT_H */
# ifdef HAVE_LINUX_NFS_H
#  include <linux/nfs.h>
# endif /* HAVE_LINUX_NFS_H */
#endif /* DO NOT INCLUDE */

/*
 * Actions to take if one of the nfs headers exists.
 */
#ifdef HAVE_NFS_NFS_GFS_H
# include <nfs/nfs_gfs.h>
#endif /* HAVE_NFS_NFS_GFS_H */
#ifdef HAVE_NFS_MOUNT_H
# include <nfs/mount.h>
#endif /* HAVE_NFS_MOUNT_H */
#ifdef HAVE_NFS_NFS_MOUNT_H_off
/* broken on nextstep3 (includes non-existing headers) */
# include <nfs/nfs_mount.h>
#endif /* HAVE_NFS_NFS_MOUNT_H */
#ifdef HAVE_NFS_PATHCONF_H
# include <nfs/pathconf.h>
#endif /* HAVE_NFS_PATHCONF_H */
#ifdef HAVE_SYS_FS_NFS_MOUNT_H
# include <sys/fs/nfs/mount.h>
#endif /* HAVE_SYS_FS_NFS_MOUNT_H */
#ifdef HAVE_SYS_FS_NFS_NFS_CLNT_H
# include <sys/fs/nfs/nfs_clnt.h>
#endif /* HAVE_SYS_FS_NFS_NFS_CLNT_H */
#ifdef HAVE_SYS_FS_NFS_CLNT_H
# include <sys/fs/nfs_clnt.h>
#endif /* HAVE_SYS_FS_NFS_CLNT_H */

/* complex rules for linux/nfs_mount.h: broken on so many systems */
#ifdef HAVE_LINUX_NFS_MOUNT_H
# ifndef _LINUX_NFS_H
#  define _LINUX_NFS_H
# endif /* not _LINUX_NFS_H */
# ifndef _LINUX_NFS2_H
#  define _LINUX_NFS2_H
# endif /* not _LINUX_NFS2_H */
# ifndef _LINUX_NFS3_H
#  define _LINUX_NFS3_H
# endif /* not _LINUX_NFS3_H */
# ifndef _LINUX_NFS_FS_H
#  define _LINUX_NFS_FS_H
# endif /* not _LINUX_NFS_FS_H */
# ifndef _LINUX_IN_H
#  define _LINUX_IN_H
# endif /* not _LINUX_IN_H */
# ifndef __KERNEL__
#  define __KERNEL__
# endif /* __KERNEL__ */
# include <linux/nfs_mount.h>
# undef __KERNEL__
#endif /* HAVE_LINUX_NFS_MOUNT_H */

/*
 * Actions to take if <pwd.h> exists.
 */
#ifdef HAVE_PWD_H
# include <pwd.h>
#endif /* HAVE_PWD_H */

/*
 * Actions to take if <hesiod.h> exists.
 */
#ifdef HAVE_HESIOD_H
# include <hesiod.h>
#endif /* HAVE_HESIOD_H */

/*
 * Actions to take if <arpa/nameser.h> exists.
 * Should be included before <resolv.h>.
 */
#ifdef HAVE_ARPA_NAMESER_H
# ifdef NOERROR
#  undef NOERROR
# endif /* NOERROR */
/*
 * Conflicts with <sys/tpicommon.h> which is included from <sys/tiuser.h>
 * on Solaris 2.6 systems.  So undefine it first.
 */
# ifdef T_UNSPEC
#  undef T_UNSPEC
# endif /* T_UNSPEC */
# include <arpa/nameser.h>
#endif /* HAVE_ARPA_NAMESER_H */

/*
 * Actions to take if <arpa/inet.h> exists.
 */
#ifdef HAVE_ARPA_INET_H
# ifdef HAVE_BAD_HEADERS
/* aix 4.3: avoid including <net/if_dl.h> */
struct sockaddr_dl;
# endif /* HAVE_BAD_HEADERS */
# include <arpa/inet.h>
#endif /* HAVE_ARPA_INET_H */

/*
 * Actions to take if <resolv.h> exists.
 */
#ifdef HAVE_RESOLV_H
/*
 * On AIX 5.2, both <resolv.h> and <arpa/nameser_compat.h> define MAXDNAME,
 * if compiling with gcc -D_USE_IRS (so that we get extern definitions for
 * hstrerror() and others).
 */
# if defined(_AIX) && defined(MAXDNAME) && defined(_USE_IRS)
#  undef MAXDNAME
# endif /* defined(_AIX) && defined(MAXDNAME) && defined(_USE_IRS) */
# include <resolv.h>
#endif /* HAVE_RESOLV_H */

/*
 * Actions to take if <sys/uio.h> exists.
 */
#ifdef HAVE_SYS_UIO_H
# include <sys/uio.h>
#endif /* HAVE_SYS_UIO_H */

/*
 * Actions to take if <sys/fs/cachefs_fs.h> exists.
 */
#ifdef HAVE_SYS_FS_CACHEFS_FS_H
# include <sys/fs/cachefs_fs.h>
#endif /* HAVE_SYS_FS_CACHEFS_FS_H */

/*
 * Actions to take if <sys/fs/pc_fs.h> exists.
 */
#ifdef HAVE_SYS_FS_PC_FS_H
# include <sys/fs/pc_fs.h>
#endif /* HAVE_SYS_FS_PC_FS_H */

/*
 * Actions to take if <msdosfs/msdosfsmount.h> exists.
 */
#ifdef HAVE_MSDOSFS_MSDOSFSMOUNT_H
# include <msdosfs/msdosfsmount.h>
#endif /* HAVE_MSDOSFS_MSDOSFSMOUNT_H */
#ifdef HAVE_FS_MSDOSFS_MSDOSFSMOUNT_H
# include <fs/msdosfs/msdosfsmount.h>
#endif /* HAVE_FS_MSDOSFS_MSDOSFSMOUNT_H */

/*
 * Actions to take if <fs/msdosfs/msdosfsmount.h> exists.
 */
#ifdef HAVE_FS_MSDOSFS_MSDOSFSMOUNT_H
# include <fs/msdosfs/msdosfsmount.h>
#endif /* HAVE_FS_MSDOSFS_MSDOSFSMOUNT_H */

/*
 * Actions to take if <sys/fs/tmp.h> exists.
 */
#ifdef HAVE_SYS_FS_TMP_H
# include <sys/fs/tmp.h>
#endif /* HAVE_SYS_FS_TMP_H */
#ifdef HAVE_FS_TMPFS_TMPFS_ARGS_H
# include <fs/tmpfs/tmpfs_args.h>
#endif /* HAVE_FS_TMPFS_TMPFS_ARGS_H */


/*
 * Actions to take if <sys/fs/ufs_mount.h> exists.
 */
#ifdef HAVE_SYS_FS_UFS_MOUNT_H
# include <sys/fs/ufs_mount.h>
#endif /* HAVE_SYS_FS_UFS_MOUNT_H */
/* 
 * HAVE_UFS_UFS_UFSMOUNT_H should NOT be defined on netbsd/openbsd because it
 * causes errors with other header files.  Instead, add it to the specific
 * conf/nfs_prot_*.h file.
 */
#ifdef	HAVE_UFS_UFS_UFSMOUNT_H
# include <ufs/ufs/ufsmount.h>
#endif	/* HAVE_UFS_UFS_UFSMOUNT_H */

/*
 * Actions to take if <sys/fs/efs_clnt.h> exists.
 */
#ifdef HAVE_SYS_FS_EFS_CLNT_H
# include <sys/fs/efs_clnt.h>
#endif /* HAVE_SYS_FS_EFS_CLNT_H */
#ifdef HAVE_FS_EFS_EFS_MOUNT_H
# include <fs/efs/efs_mount.h>
#endif /* HAVE_FS_EFS_EFS_MOUNT_H */

/*
 * Actions to take if <sys/fs/xfs_clnt.h> exists.
 */
#ifdef HAVE_SYS_FS_XFS_CLNT_H
# include <sys/fs/xfs_clnt.h>
#endif /* HAVE_SYS_FS_XFS_CLNT_H */

/*
 * Actions to take if <assert.h> exists.
 */
#ifdef HAVE_ASSERT_H
# include <assert.h>
#endif /* HAVE_ASSERT_H */

/*
 * Actions to take if <cfs.h> exists.
 */
#ifdef HAVE_CFS_H
# include <cfs.h>
#endif /* HAVE_CFS_H */

/*
 * Actions to take if <cluster.h> exists.
 */
#ifdef HAVE_CLUSTER_H
# include <cluster.h>
#endif /* HAVE_CLUSTER_H */

/*
 * Actions to take if <ctype.h> exists.
 */
#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif /* HAVE_CTYPE_H */

/*
 * Actions to take if <errno.h> exists.
 */
#ifdef HAVE_ERRNO_H
# include <errno.h>
#else
/*
 * Actions to take if <sys/errno.h> exists.
 */
# ifdef HAVE_SYS_ERRNO_H
#  include <sys/errno.h>
extern int errno;
# endif /* HAVE_SYS_ERRNO_H */
#endif /* HAVE_ERRNO_H */

/*
 * Actions to take if <grp.h> exists.
 */
#ifdef HAVE_GRP_H
# include <grp.h>
#endif /* HAVE_GRP_H */

/*
 * Actions to take if <hsfs/hsfs.h> exists.
 */
#ifdef HAVE_HSFS_HSFS_H
# include <hsfs/hsfs.h>
#endif /* HAVE_HSFS_HSFS_H */

/*
 * Actions to take if <cdfs/cdfsmount.h> exists.
 */
#ifdef HAVE_CDFS_CDFSMOUNT_H
# include <cdfs/cdfsmount.h>
#endif /* HAVE_CDFS_CDFSMOUNT_H */

/*
 * Actions to take if <isofs/cd9660/cd9660_mount.h> exists.
 */
#ifdef HAVE_ISOFS_CD9660_CD9660_MOUNT_H
# include <isofs/cd9660/cd9660_mount.h>
#endif /* HAVE_ISOFS_CD9660_CD9660_MOUNT_H */

/*
 * Actions to take if <fs/udf/udf_mount.h> exists.
 */
#ifdef HAVE_FS_UDF_UDF_MOUNT_H
# include <fs/udf/udf_mount.h>
#endif /* HAVE_FS_UDF_UDF_MOUNT_H */

/*
 * Actions to take if <mount.h> exists.
 */
#ifdef HAVE_MOUNT_H
# include <mount.h>
#endif /* HAVE_MOUNT_H */

/*
 * Actions to take if <nsswitch.h> exists.
 */
#ifdef HAVE_NSSWITCH_H
# include <nsswitch.h>
#endif /* HAVE_NSSWITCH_H */

/*
 * Actions to take if <rpc/auth_des.h> exists.
 */
#ifdef HAVE_RPC_AUTH_DES_H
# include <rpc/auth_des.h>
#endif /* HAVE_RPC_AUTH_DES_H */

/*
 * Actions to take if <rpc/pmap_clnt.h> exists.
 */
#ifdef HAVE_RPC_PMAP_CLNT_H
# include <rpc/pmap_clnt.h>
#endif /* HAVE_RPC_PMAP_CLNT_H */

/*
 * Actions to take if <rpc/pmap_prot.h> exists.
 */
#ifdef HAVE_RPC_PMAP_PROT_H
# include <rpc/pmap_prot.h>
#endif /* HAVE_RPC_PMAP_PROT_H */


/*
 * Actions to take if <rpcsvc/mount.h> exists.
 * AIX does not protect against this file doubly included,
 * so I have to do my own protection here.
 */
#ifdef HAVE_RPCSVC_MOUNT_H
# ifndef _RPCSVC_MOUNT_H
#  include <rpcsvc/mount.h>
# endif /* not _RPCSVC_MOUNT_H */
#endif /* HAVE_RPCSVC_MOUNT_H */

/*
 * Actions to take if <rpcsvc/nis.h> exists.
 */
#ifdef HAVE_RPCSVC_NIS_H
/*
 * Solaris 10 (build 72) defines GROUP_OBJ in <sys/acl.h>, which is included
 * in many other header files.  <rpcsvc/nis.h> uses GROUP_OBJ inside enum
 * zotypes.  So if you're unlucky enough to include both headers, you get a
 * compile error because the two symbols conflict.
 * A similar conflict arises with Sun cc and the definition of "GROUP".
 *
 * Temp hack: undefine acl.h's GROUP_OBJ and GROUP because they're not needed
 * for am-utils.
 */
# ifdef GROUP_OBJ
#  undef GROUP_OBJ
# endif /* GROUP_OBJ */
# ifdef GROUP
#  undef GROUP
# endif /* GROUP */
# include <rpcsvc/nis.h>
#endif /* HAVE_RPCSVC_NIS_H */

/*
 * Actions to take if <setjmp.h> exists.
 */
#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif /* HAVE_SETJMP_H */

/*
 * Actions to take if <signal.h> exists.
 */
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif /* HAVE_SIGNAL_H */

/*
 * Actions to take if <string.h> exists.
 */
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */

/*
 * Actions to take if <strings.h> exists.
 */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */

/*
 * Actions to take if <sys/config.h> exists.
 */
#ifdef HAVE_SYS_CONFIG_H
# include <sys/config.h>
#endif /* HAVE_SYS_CONFIG_H */

/*
 * Actions to take if <sys/dg_mount.h> exists.
 */
#ifdef HAVE_SYS_DG_MOUNT_H
# include <sys/dg_mount.h>
#endif /* HAVE_SYS_DG_MOUNT_H */

/*
 * Actions to take if <sys/fs_types.h> exists.
 */
#ifdef HAVE_SYS_FS_TYPES_H
/*
 * Define KERNEL here to avoid multiple definitions of gt_names[] on
 * Ultrix 4.3.
 */
# define KERNEL
# include <sys/fs_types.h>
# undef KERNEL
#endif /* HAVE_SYS_FS_TYPES_H */

/*
 * Actions to take if <sys/fstyp.h> exists.
 */
#ifdef HAVE_SYS_FSTYP_H
# include <sys/fstyp.h>
#endif /* HAVE_SYS_FSTYP_H */

/*
 * Actions to take if <sys/lock.h> exists.
 */
#ifdef HAVE_SYS_LOCK_H
# include <sys/lock.h>
#endif /* HAVE_SYS_LOCK_H */

/*
 * Actions to take if <sys/machine.h> exists.
 */
#ifdef HAVE_SYS_MACHINE_H
# include <sys/machine.h>
#endif /* HAVE_SYS_MACHINE_H */

/*
 * Actions to take if <sys/mntctl.h> exists.
 */
#ifdef HAVE_SYS_MNTCTL_H
# include <sys/mntctl.h>
#endif /* HAVE_SYS_MNTCTL_H */

/*
 * Actions to take if <sys/mnttab.h> exists.
 */
#ifdef HAVE_SYS_MNTTAB_H
# include <sys/mnttab.h>
#endif /* HAVE_SYS_MNTTAB_H */

/*
 * Actions to take if <mnttab.h> exists.
 * Do not include it if MNTTAB is already defined because it probably
 * came from <sys/mnttab.h> and we do not want conflicting definitions.
 */
#if defined(HAVE_MNTTAB_H) && !defined(MNTTAB)
# include <mnttab.h>
#endif /* defined(HAVE_MNTTAB_H) && !defined(MNTTAB) */

/*
 * Actions to take if <netconfig.h> exists.
 */
#ifdef HAVE_NETCONFIG_H
# include <netconfig.h>
/* Some systems (Solaris 2.5.1) don't declare this external */
extern char *nc_sperror(void);
#endif /* HAVE_NETCONFIG_H */

/*
 * Actions to take if <sys/netconfig.h> exists.
 */
#ifdef HAVE_SYS_NETCONFIG_H
# include <sys/netconfig.h>
#endif /* HAVE_SYS_NETCONFIG_H */

/*
 * Actions to take if <sys/pathconf.h> exists.
 */
#ifdef HAVE_SYS_PATHCONF_H
# include <sys/pathconf.h>
#endif /* HAVE_SYS_PATHCONF_H */

/*
 * Actions to take if <sys/resource.h> exists.
 */
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif /* HAVE_SYS_RESOURCE_H */

/*
 * Actions to take if <sys/sema.h> exists.
 */
#ifdef HAVE_SYS_SEMA_H
# include <sys/sema.h>
#endif /* HAVE_SYS_SEMA_H */

/*
 * Actions to take if <sys/signal.h> exists.
 */
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif /* HAVE_SYS_SIGNAL_H */

/*
 * Actions to take if <sys/sockio.h> exists.
 */
#ifdef HAVE_SYS_SOCKIO_H
# include <sys/sockio.h>
#endif /* HAVE_SYS_SOCKIO_H */

/*
 * Actions to take if <sys/syscall.h> exists.
 */
#ifdef HAVE_SYS_SYSCALL_H
# include <sys/syscall.h>
#endif /* HAVE_SYS_SYSCALL_H */

/*
 * Actions to take if <sys/syslimits.h> exists.
 */
#ifdef HAVE_SYS_SYSLIMITS_H
# include <sys/syslimits.h>
#endif /* HAVE_SYS_SYSLIMITS_H */

/*
 * Actions to take if <tiuser.h> exists.
 */
#ifdef HAVE_TIUSER_H
/*
 * Some systems like AIX have multiple definitions for T_NULL and others
 * that are defined first in <arpa/nameser.h>.
 */
# ifdef HAVE_ARPA_NAMESER_H
#  ifdef T_NULL
#   undef T_NULL
#  endif /* T_NULL */
#  ifdef T_UNSPEC
#   undef T_UNSPEC
#  endif /* T_UNSPEC */
#  ifdef T_IDLE
#   undef T_IDLE
#  endif /* T_IDLE */
# endif /* HAVE_ARPA_NAMESER_H */
# include <tiuser.h>
#endif /* HAVE_TIUSER_H */

/*
 * Actions to take if <sys/tiuser.h> exists.
 */
#ifdef HAVE_SYS_TIUSER_H
# include <sys/tiuser.h>
#endif /* HAVE_SYS_TIUSER_H */

/*
 * Actions to take if <sys/statfs.h> exists.
 */
#ifdef HAVE_SYS_STATFS_H
# include <sys/statfs.h>
#endif /* HAVE_SYS_STATFS_H */

/*
 * Actions to take if <sys/statvfs.h> exists.
 */
#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif /* HAVE_SYS_STATVFS_H */

/*
 * Actions to take if <sys/vfs.h> exists.
 */
#ifdef HAVE_SYS_VFS_H
# include <sys/vfs.h>
#endif /* HAVE_SYS_VFS_H */

/*
 * Actions to take if <sys/vmount.h> exists.
 */
#ifdef HAVE_SYS_VMOUNT_H
# include <sys/vmount.h>
#endif /* HAVE_SYS_VMOUNT_H */

/*
 * Actions to take if <ufs/ufs_mount.h> exists.
 */
#ifdef HAVE_UFS_UFS_MOUNT_H
# include <ufs/ufs_mount.h>
#endif /* HAVE_UFS_UFS_MOUNT_H */

/*
 * Are S_ISDIR, S_ISREG, et al broken?  If not, include <sys/stat.h>.
 * Turned off the not using sys/stat.h based on if the macros are
 * "broken", because they incorrectly get reported as broken on
 * ncr2.
 */
#ifndef STAT_MACROS_BROKEN_notused
/*
 * RedHat Linux 4.2 (alpha) has a problem in the headers that causes
 * duplicate definitions, and also some other nasty bugs.  Upgrade to Redhat
 * 5.0!
 */
# ifdef HAVE_SYS_STAT_H
/* avoid duplicates or conflicts with <linux/stat.h> (RedHat alpha linux) */
#  if defined(S_IFREG) && defined(HAVE_STATBUF_H)
#   include <statbuf.h>
#   undef S_IFBLK
#   undef S_IFCHR
#   undef S_IFDIR
#   undef S_IFIFO
#   undef S_IFLNK
#   undef S_IFMT
#   undef S_IFREG
#   undef S_IFSOCK
#   undef S_IRGRP
#   undef S_IROTH
#   undef S_IRUSR
#   undef S_IRWXG
#   undef S_IRWXO
#   undef S_IRWXU
#   undef S_ISBLK
#   undef S_ISCHR
#   undef S_ISDIR
#   undef S_ISFIFO
#   undef S_ISGID
#   undef S_ISLNK
#   undef S_ISREG
#   undef S_ISSOCK
#   undef S_ISUID
#   undef S_ISVTX
#   undef S_IWGRP
#   undef S_IWOTH
#   undef S_IWUSR
#   undef S_IXGRP
#   undef S_IXOTH
#   undef S_IXUSR
#  endif /* defined(S_IFREG) && defined(HAVE_STATBUF_H) */
#  include <sys/stat.h>
# endif /* HAVE_SYS_STAT_H */
#endif /* not STAT_MACROS_BROKEN_notused */

/*
 * Actions to take if <stdio.h> exists.
 */
#ifdef HAVE_STDIO_H
# include <stdio.h>
#endif /* HAVE_STDIO_H */

/*
 * Actions to take if <stdlib.h> exists.
 */
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif /* HAVE_STDLIB_H */

/*
 * Actions to take if <regex.h> exists.
 */
#ifdef HAVE_REGEX_H
# include <regex.h>
#endif /* HAVE_REGEX_H */

/*
 * Actions to take if <tcpd.h> exists.
 */
#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP)
# include <tcpd.h>
#endif /* defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */


/****************************************************************************/
/*
 * Specific macros we're looking for.
 */
#ifndef HAVE_MEMSET
# ifdef HAVE_BZERO
#  define	memset(ptr, val, len)	bzero((ptr), (len))
# else /* not HAVE_BZERO */
#  error Cannot find either memset or bzero!
# endif /* not HAVE_BZERO */
#endif /* not HAVE_MEMSET */

#ifndef HAVE_MEMMOVE
# ifdef HAVE_BCOPY
#  define	memmove(to, from, len)	bcopy((from), (to), (len))
# else /* not HAVE_BCOPY */
#  error Cannot find either memmove or bcopy!
# endif /* not HAVE_BCOPY */
#endif /* not HAVE_MEMMOVE */

/*
 * memcmp() is more problematic:
 * Systems that don't have it, but have bcmp(), will use bcmp() instead.
 * Those that have it, but it is bad (SunOS 4 doesn't handle
 * 8 bit comparisons correctly), will get to use am_memcmp().
 * Otherwise if you have memcmp() and it is good, use it.
 */
#ifdef HAVE_MEMCMP
# ifdef HAVE_BAD_MEMCMP
#  define	memcmp		am_memcmp
extern int am_memcmp(const voidp s1, const voidp s2, size_t len);
# endif /* HAVE_BAD_MEMCMP */
#else /* not HAVE_MEMCMP */
# ifdef HAVE_BCMP
#  define	memcmp(a, b, len)	bcmp((a), (b), (len))
# endif /* HAVE_BCMP */
#endif /* not HAVE_MEMCMP */

#ifndef HAVE_SETEUID
# ifdef HAVE_SETRESUID
#  define	seteuid(x)		setresuid(-1,(x),-1)
# else /* not HAVE_SETRESUID */
#  error Cannot find either seteuid or setresuid!
# endif /* not HAVE_SETRESUID */
#endif /* not HAVE_SETEUID */

/*
 * Define type of mntent_t.
 * Defaults to struct mntent, else struct mnttab.  If neither is found, and
 * the operating system does keep not mount tables in the kernel, then flag
 * it as an error.  If neither is found and the OS keeps mount tables in the
 * kernel, then define our own version of mntent; it will be needed for amd
 * to keep its own internal version of the mount tables.
 */
#ifdef HAVE_STRUCT_MNTENT
typedef struct mntent mntent_t;
#else /* not HAVE_STRUCT_MNTENT */
# ifdef HAVE_STRUCT_MNTTAB
typedef struct mnttab mntent_t;
/* map struct mnttab field names to struct mntent field names */
#  define mnt_fsname	mnt_special
#  define mnt_dir	mnt_mountp
#  define mnt_opts	mnt_mntopts
#  define mnt_type	mnt_fstype
# else /* not HAVE_STRUCT_MNTTAB */
#  ifdef MOUNT_TABLE_ON_FILE
#   error Could not find definition for struct mntent or struct mnttab!
#  else /* not MOUNT_TABLE_ON_FILE */
typedef struct _am_mntent {
  char	*mnt_fsname;		/* name of mounted file system */
  char	*mnt_dir;		/* file system path prefix */
  char	*mnt_type;		/* MNTTAB_TYPE_* */
  char	*mnt_opts;		/* MNTTAB_OPT_* */
  int	mnt_freq;		/* dump frequency, in days */
  int	mnt_passno;		/* pass number on parallel fsck */
} mntent_t;
#  endif /* not MOUNT_TABLE_ON_FILE */
# endif /* not HAVE_STRUCT_MNTTAB */
#endif /* not HAVE_STRUCT_MNTENT */

/*
 * Provide FD_* macros for systems that lack them.
 */
#ifndef FD_SET
# define FD_SET(fd, set) (*(set) |= (1 << (fd)))
# define FD_ISSET(fd, set) (*(set) & (1 << (fd)))
# define FD_CLR(fd, set) (*(set) &= ~(1 << (fd)))
# define FD_ZERO(set) (*(set) = 0)
#endif /* not FD_SET */


/*
 * Complete external definitions missing from some systems.
 */

#ifndef HAVE_EXTERN_SYS_ERRLIST
extern const char *const sys_errlist[];
#endif /* not HAVE_EXTERN_SYS_ERRLIST */

#ifndef HAVE_EXTERN_OPTARG
extern char *optarg;
extern int optind;
#endif /* not HAVE_EXTERN_OPTARG */

#if defined(HAVE_CLNT_SPCREATEERROR) && !defined(HAVE_EXTERN_CLNT_SPCREATEERROR)
extern char *clnt_spcreateerror(const char *s);
#endif /* defined(HAVE_CLNT_SPCREATEERROR) && !defined(HAVE_EXTERN_CLNT_SPCREATEERROR) */

#if defined(HAVE_CLNT_SPERRNO) && !defined(HAVE_EXTERN_CLNT_SPERRNO)
extern char *clnt_sperrno(const enum clnt_stat num);
#endif /* defined(HAVE_CLNT_SPERRNO) && !defined(HAVE_EXTERN_CLNT_SPERRNO) */

#ifndef HAVE_EXTERN_FREE
extern void free(voidp);
#endif /* not HAVE_EXTERN_FREE */

#if defined(HAVE_GET_MYADDRESS) && !defined(HAVE_EXTERN_GET_MYADDRESS)
extern void get_myaddress(struct sockaddr_in *addr);
#endif /* defined(HAVE_GET_MYADDRESS) && !defined(HAVE_EXTERN_GET_MYADDRESS) */

#if defined(HAVE_GETDOMAINNAME) && !defined(HAVE_EXTERN_GETDOMAINNAME)
# if defined(HAVE_MAP_NIS) || defined(HAVE_MAP_NISPLUS)
extern int getdomainname(char *name, int namelen);
# endif /* defined(HAVE_MAP_NIS) || defined(HAVE_MAP_NISPLUS) */
#endif /* defined(HAVE_GETDOMAINNAME) && !defined(HAVE_EXTERN_GETDOMAINNAME) */

#if defined(HAVE_GETDTABLESIZE) && !defined(HAVE_EXTERN_GETDTABLESIZE)
extern int getdtablesize(void);
#endif /* defined(HAVE_GETDTABLESIZE) && !defined(HAVE_EXTERN_GETDTABLESIZE) */

#if defined(HAVE_GETHOSTNAME) && !defined(HAVE_EXTERN_GETHOSTNAME)
extern int gethostname(char *name, int namelen);
#endif /* defined(HAVE_GETHOSTNAME) && !defined(HAVE_EXTERN_GETHOSTNAME) */

#ifndef HAVE_EXTERN_GETLOGIN
extern char *getlogin(void);
#endif /* not HAVE_EXTERN_GETLOGIN */

#if defined(HAVE_GETPAGESIZE) && !defined(HAVE_EXTERN_GETPAGESIZE)
extern int getpagesize(void);
#endif /* defined(HAVE_GETPAGESIZE) && !defined(HAVE_EXTERN_GETPAGESIZE) */

#ifndef HAVE_EXTERN_GETWD
extern char *getwd(char *s);
#endif /* not HAVE_EXTERN_GETWD */

#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) && !defined(HAVE_EXTERN_HOSTS_CTL)
extern int hosts_ctl(char *daemon, char *client_name, char *client_addr, char *client_user);
#endif /* defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) && !defined(HAVE_EXTERN_HOSTS_CTL) */

#ifndef HAVE_EXTERN_INNETGR
extern int innetgr(char *, char *, char *, char *);
#endif /* not HAVE_EXTERN_INNETGR */

#if defined(HAVE_MKSTEMP) && !defined(HAVE_EXTERN_MKSTEMP)
extern int mkstemp(char *);
#endif /* defined(HAVE_MKSTEMP) && !defined(HAVE_EXTERN_MKSTEMP) */

#ifndef HAVE_EXTERN_SBRK
extern caddr_t sbrk(int incr);
#endif /* not HAVE_EXTERN_SBRK */

#if defined(HAVE_SETEUID) && !defined(HAVE_EXTERN_SETEUID)
extern int seteuid(uid_t euid);
#endif /* not defined(HAVE_SETEUID) && !defined(HAVE_EXTERN_SETEUID) */

#if defined(HAVE_SETITIMER) && !defined(HAVE_EXTERN_SETITIMER)
extern int setitimer(int, struct itimerval *, struct itimerval *);
#endif /* defined(HAVE_SETITIMER) && !defined(HAVE_EXTERN_SETITIMER) */

#ifndef HAVE_EXTERN_SLEEP
extern unsigned int sleep(unsigned int seconds);
#endif /* not HAVE_EXTERN_SETITIMER */

#ifndef HAVE_EXTERN_STRCASECMP
/*
 * define this extern even if function does not exist, for it will
 * be filled in by libamu/strcasecmp.c
 */
extern int strcasecmp(const char *s1, const char *s2);
#endif /* not HAVE_EXTERN_STRCASECMP */

#ifndef HAVE_EXTERN_STRLCAT
/*
 * define this extern even if function does not exist, for it will
 * be filled in by libamu/strlcat.c
 */
extern size_t strlcat(char *dst, const char *src, size_t siz);
#endif /* not HAVE_EXTERN_STRLCAT */

#ifndef HAVE_EXTERN_STRLCPY
/*
 * define this extern even if function does not exist, for it will
 * be filled in by libamu/strlcpy.c
 */
extern size_t strlcpy(char *dst, const char *src, size_t siz);
#endif /* not HAVE_EXTERN_STRLCPY */

#if defined(HAVE_STRSTR) && !defined(HAVE_EXTERN_STRSTR)
extern char *strstr(const char *s1, const char *s2);
#endif /* defined(HAVE_STRSTR) && !defined(HAVE_EXTERN_STRSTR) */

#if defined(HAVE_USLEEP) && !defined(HAVE_EXTERN_USLEEP)
extern int usleep(u_int useconds);
#endif /* defined(HAVE_USLEEP) && !defined(HAVE_EXTERN_USLEEP) */

#ifndef HAVE_EXTERN_UALARM
extern u_int ualarm(u_int usecs, u_int interval);
#endif /* not HAVE_EXTERN_UALARM */

#if defined(HAVE_WAIT3) && !defined(HAVE_EXTERN_WAIT3)
extern int wait3(int *statusp, int options, struct rusage *rusage);
#endif /* defined(HAVE_WAIT3) && !defined(HAVE_EXTERN_WAIT3) */

#if defined(HAVE_VSNPRINTF) && !defined(HAVE_EXTERN_VSNPRINTF)
extern int vsnprintf(char *, int, const char *, ...);
#endif /* defined(HAVE_VSNPRINTF) && !defined(HAVE_EXTERN_VSNPRINTF) */

#ifndef HAVE_EXTERN_XDR_CALLMSG
extern bool_t xdr_callmsg(XDR *xdrs, struct rpc_msg *msg);
#endif /* not HAVE_EXTERN_XDR_CALLMSG */

#ifndef HAVE_EXTERN_XDR_OPAQUE_AUTH
extern bool_t xdr_opaque_auth(XDR *xdrs, struct opaque_auth *auth);
#endif /* not HAVE_EXTERN_XDR_OPAQUE_AUTH */

/****************************************************************************/
/*
 * amd-specific header files.
 */
#ifdef THIS_HEADER_FILE_IS_INCLUDED_ABOVE
# include <amu_nfs_prot.h>
#endif /* THIS_HEADER_FILE_IS_INCLUDED_ABOVE */
#include <am_compat.h>
#include <am_xdr_func.h>
#include <am_utils.h>
#include <amq_defs.h>
#include <aux_conf.h>


/****************************************************************************/
/*
 * External definitions that depend on other macros available (or not)
 * and those are probably declared in any of the above headers.
 */

#ifdef HAVE_HASMNTOPT
# ifdef HAVE_BAD_HASMNTOPT
extern char *amu_hasmntopt(mntent_t *mnt, char *opt);
# else /* not HAVE_BAD_HASMNTOPT */
#  define amu_hasmntopt hasmntopt
# endif /* not HAVE_BAD_HASMNTOPT */
#else /* not HAVE_HASMNTOPT */
extern char *amu_hasmntopt(mntent_t *mnt, char *opt);
#endif /* not HAVE_HASMNTOPT */

#endif /* not _AM_DEFS_H */
