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
 * File: am-utils/include/am_compat.h
 *
 */

/*
 *
 * This file contains compatibility functions and macros, all of which
 * should be auto-discovered, but for one reason or another (mostly
 * brain-damage on the part of system designers and header files) they cannot.
 *
 * Each compatibility macro/function must include instructions on how/when
 * it can be removed the am-utils code.
 *
 */

#ifndef _AM_COMPAT_H
# define _AM_COMPAT_H

/*
 * incomplete mount options definitions (sunos4, irix6, linux, etc.)
 */


/*
 * Complete MNTTAB_OPT_* options based on MNT2_NFS_OPT_* mount options.
 */
#if defined(MNT2_NFS_OPT_ACDIRMAX) && !defined(MNTTAB_OPT_ACDIRMAX)
# define MNTTAB_OPT_ACDIRMAX "acdirmax"
#endif /* defined(MNT2_NFS_OPT_ACDIRMAX) && !defined(MNTTAB_OPT_ACDIRMAX) */

#if defined(MNT2_NFS_OPT_ACDIRMIN) && !defined(MNTTAB_OPT_ACDIRMIN)
# define MNTTAB_OPT_ACDIRMIN "acdirmin"
#endif /* defined(MNT2_NFS_OPT_ACDIRMIN) && !defined(MNTTAB_OPT_ACDIRMIN) */

#if defined(MNT2_NFS_OPT_ACREGMAX) && !defined(MNTTAB_OPT_ACREGMAX)
# define MNTTAB_OPT_ACREGMAX "acregmax"
#endif /* defined(MNT2_NFS_OPT_ACREGMAX) && !defined(MNTTAB_OPT_ACREGMAX) */

#if defined(MNT2_NFS_OPT_ACREGMIN) && !defined(MNTTAB_OPT_ACREGMIN)
# define MNTTAB_OPT_ACREGMIN "acregmin"
#endif /* defined(MNT2_NFS_OPT_ACREGMIN) && !defined(MNTTAB_OPT_ACREGMIN) */

#if !defined(MNTTAB_OPT_IGNORE)
/* SunOS 4.1.x and others define "noauto" option, but not "auto" */
# if defined(MNTTAB_OPT_NOAUTO) && !defined(MNTTAB_OPT_AUTO)
#  define MNTTAB_OPT_AUTO "auto"
# endif /* defined(MNTTAB_OPT_NOAUTO) && !defined(MNTTAB_OPT_AUTO) */
#endif /* !defined(MNTTAB_OPT_IGNORE) */

#if defined(MNT2_NFS_OPT_NOAC) && !defined(MNTTAB_OPT_NOAC)
# define MNTTAB_OPT_NOAC "noac"
#endif /* defined(MNT2_NFS_OPT_NOAC) && !defined(MNTTAB_OPT_NOAC) */

#if defined(MNT2_NFS_OPT_NOACL) && !defined(MNTTAB_OPT_NOACL)
# define MNTTAB_OPT_NOACL "noacl"
#endif /* defined(MNT2_NFS_OPT_NOACL) && !defined(MNTTAB_OPT_NOACL) */

#if defined(MNT2_NFS_OPT_NOCONN) && !defined(MNTTAB_OPT_NOCONN)
# define MNTTAB_OPT_NOCONN "noconn"
# ifndef MNTTAB_OPT_CONN
#  define MNTTAB_OPT_CONN "conn"
# endif /* MNTTAB_OPT_CONN */
#endif /* defined(MNT2_NFS_OPT_NOCONN) && !defined(MNTTAB_OPT_NOCONN) */

#if defined(MNT2_NFS_OPT_PGTHRESH) && !defined(MNTTAB_OPT_PGTHRESH)
# define MNTTAB_OPT_PGTHRESH "pgthresh"
#endif /* defined(MNT2_NFS_OPT_PGTHRESH) && !defined(MNTTAB_OPT_PGTHRESH) */

#if defined(MNT2_NFS_OPT_PRIVATE) && !defined(MNTTAB_OPT_PRIVATE)
# define MNTTAB_OPT_PRIVATE "private"
#endif /* defined(MNT2_NFS_OPT_PRIVATE) && !defined(MNTTAB_OPT_PRIVATE) */

#if defined(MNT2_NFS_OPT_RETRANS) && !defined(MNTTAB_OPT_RETRANS)
# define MNTTAB_OPT_RETRANS "retrans"
#endif /* defined(MNT2_NFS_OPT_RETRANS) && !defined(MNTTAB_OPT_RETRANS) */

#if defined(MNT2_NFS_OPT_RSIZE) && !defined(MNTTAB_OPT_RSIZE)
# define MNTTAB_OPT_RSIZE "rsize"
#endif /* defined(MNT2_NFS_OPT_RSIZE) && !defined(MNTTAB_OPT_RSIZE) */

#if defined(MNT2_NFS_OPT_SOFT) && !defined(MNTTAB_OPT_SOFT)
# define MNTTAB_OPT_SOFT "soft"
# ifndef MNTTAB_OPT_HARD
#  define MNTTAB_OPT_HARD "hard"
# endif /* not MNTTAB_OPT_HARD */
#endif /* defined(MNT2_NFS_OPT_SOFT) && !defined(MNTTAB_OPT_SOFT) */

#if defined(MNT2_NFS_OPT_TIMEO) && !defined(MNTTAB_OPT_TIMEO)
# define MNTTAB_OPT_TIMEO "timeo"
#endif /* defined(MNT2_NFS_OPT_TIMEO) && !defined(MNTTAB_OPT_TIMEO) */

#if defined(MNT2_NFS_OPT_WSIZE) && !defined(MNTTAB_OPT_WSIZE)
# define MNTTAB_OPT_WSIZE "wsize"
#endif /* defined(MNT2_NFS_OPT_WSIZE) && !defined(MNTTAB_OPT_WSIZE) */

#if defined(MNT2_NFS_OPT_MAXGRPS) && !defined(MNTTAB_OPT_MAXGROUPS)
# define MNTTAB_OPT_MAXGROUPS "maxgroups"
#endif /* defined(MNT2_NFS_OPT_MAXGRPS) && !defined(MNTTAB_OPT_MAXGROUPS) */

#if defined(MNT2_NFS_OPT_PROPLIST) && !defined(MNTTAB_OPT_PROPLIST)
# define MNTTAB_OPT_PROPLIST "proplist"
#endif /* defined(MNT2_NFS_OPT_PROPLIST) && !defined(MNTTAB_OPT_PROPLIST) */

#if defined(MNT2_NFS_OPT_NONLM) && !defined(MNTTAB_OPT_NOLOCK)
# define MNTTAB_OPT_NOLOCK "nolock"
#endif /* defined(MNT2_NFS_OPT_NONLM) && !defined(MNTTAB_OPT_NOLOCK) */

#if defined(MNT2_NFS_OPT_XLATECOOKIE) && !defined(MNTTAB_OPT_XLATECOOKIE)
# define MNTTAB_OPT_XLATECOOKIE "xlatecookie"
#endif /* defined(MNT2_NFS_OPT_XLATECOOKIE) && !defined(MNTTAB_OPT_XLATECOOKIE) */

/*
 * Complete MNTTAB_OPT_* options based on MNT2_CDFS_OPT_* mount options.
 */
#if defined(MNT2_CDFS_OPT_DEFPERM) && !defined(MNTTAB_OPT_DEFPERM)
# define MNTTAB_OPT_DEFPERM "defperm"
#endif /* defined(MNT2_CDFS_OPT_DEFPERM) && !defined(MNTTAB_OPT_DEFPERM) */

#if defined(MNT2_CDFS_OPT_NODEFPERM) && !defined(MNTTAB_OPT_NODEFPERM)
# define MNTTAB_OPT_NODEFPERM "nodefperm"
/*
 * DEC OSF/1 V3.x/Digital UNIX V4.0 have M_NODEFPERM only, but
 * both mnttab ops.
 */
# ifndef MNTTAB_OPT_DEFPERM
#  define MNTTAB_OPT_DEFPERM "defperm"
# endif /* not MNTTAB_OPT_DEFPERM */
#endif /* defined(MNT2_CDFS_OPT_NODEFPERM) && !defined(MNTTAB_OPT_NODEFPERM) */

#if defined(MNT2_CDFS_OPT_NOVERSION) && !defined(MNTTAB_OPT_NOVERSION)
# define MNTTAB_OPT_NOVERSION "noversion"
#endif /* defined(MNT2_CDFS_OPT_NOVERSION) && !defined(MNTTAB_OPT_NOVERSION) */

#if defined(MNT2_CDFS_OPT_RRIP) && !defined(MNTTAB_OPT_RRIP)
# define MNTTAB_OPT_RRIP "rrip"
#endif /* defined(MNT2_CDFS_OPT_RRIP) && !defined(MNTTAB_OPT_RRIP) */
#if defined(MNT2_CDFS_OPT_NORRIP) && !defined(MNTTAB_OPT_NORRIP)
# define MNTTAB_OPT_NORRIP "norrip"
#endif /* defined(MNT2_CDFS_OPT_NORRIP) && !defined(MNTTAB_OPT_NORRIP) */

#if defined(MNT2_CDFS_OPT_GENS) && !defined(MNTTAB_OPT_GENS)
# define MNTTAB_OPT_GENS "gens"
#endif /* defined(MNT2_CDFS_OPT_GENS) && !defined(MNTTAB_OPT_GENS) */

#if defined(MNT2_CDFS_OPT_EXTATT) && !defined(MNTTAB_OPT_EXTATT)
# define MNTTAB_OPT_EXTATT "extatt"
#endif /* defined(MNT2_CDFS_OPT_EXTATT) && !defined(MNTTAB_OPT_EXTATT) */

#if defined(MNT2_CDFS_OPT_NOJOLIET) && !defined(MNTTAB_OPT_NOJOLIET)
# define MNTTAB_OPT_NOJOLIET "nojoliet"
#endif /* defined(MNT2_CDFS_OPT_NOJOLIET) && !defined(MNTTAB_OPT_NOJOLIET) */

#if defined(MNT2_CDFS_OPT_NOCASETRANS) && !defined(MNTTAB_OPT_NOCASETRANS)
# define MNTTAB_OPT_NOCASETRANS "nocasetrans"
#endif /* defined(MNT2_CDFS_OPT_NOCASETRANS) && !defined(MNTTAB_OPT_NOCASETRANS) */

#if defined(MNT2_CDFS_OPT_RRCASEINS) && !defined(MNTTAB_OPT_RRCASEINS)
# define MNTTAB_OPT_RRCASEINS "rrcaseins"
#endif /* defined(MNT2_CDFS_OPT_RRCASEINS) && !defined(MNTTAB_OPT_RRCASEINS) */

/*
 * Complete MNTTAB_OPT_* options based on MNT2_UDF_OPT_* mount options.
 */
#if defined(MNT2_UDF_OPT_CLOSESESSION) && !defined(MNTTAB_OPT_CLOSESESSION)
# define MNTTAB_OPT_CLOSESESSION "closesession"
#endif /* defined(MNT2_UDF_OPT_CLOSESESSION) && !defined(MNTTAB_OPT_CLOSESESSION) */

/*
 * Complete MNTTAB_OPT_* options based on MNT2_PCFS_OPT_* mount options.
 */
#if defined(MNT2_PCFS_OPT_LONGNAME) && !defined(MNTTAB_OPT_LONGNAME)
# define MNTTAB_OPT_LONGNAME "longnames"
#endif /* defined(MNT2_PCFS_OPT_LONGNAME) && !defined(MNTTAB_OPT_LONGNAME) */
#if defined(MNT2_PCFS_OPT_NOWIN95) && !defined(MNTTAB_OPT_NOWIN95)
# define MNTTAB_OPT_NOWIN95 "nowin95"
#endif /* defined(MNT2_PCFS_OPT_NOWIN95) && !defined(MNTTAB_OPT_NOWIN95) */
#if defined(MNT2_PCFS_OPT_SHORTNAME) && !defined(MNTTAB_OPT_SHORTNAME)
# define MNTTAB_OPT_SHORTNAME "shortnames"
#endif /* defined(MNT2_PCFS_OPT_SHORTNAME) && !defined(MNTTAB_OPT_SHORTNAME) */

/*
 * Complete MNTTAB_OPT_* options based on MNT2_GEN_OPT_* mount options.
 */
#if defined(MNT2_GEN_OPT_GRPID) && !defined(MNTTAB_OPT_GRPID)
# define MNTTAB_OPT_GRPID "grpid"
#endif /* defined(MNT2_GEN_OPT_GRPID) && !defined(MNTTAB_OPT_GRPID) */

#if defined(MNT2_GEN_OPT_NOCACHE) && !defined(MNTTAB_OPT_NOCACHE)
# define MNTTAB_OPT_NOCACHE "nocache"
#endif /* defined(MNT2_GEN_OPT_NOCACHE) && !defined(MNTTAB_OPT_NOCACHE) */

#if defined(MNT2_GEN_OPT_NOSUID) && !defined(MNTTAB_OPT_NOSUID)
# define MNTTAB_OPT_NOSUID "nosuid"
#endif /* defined(MNT2_GEN_OPT_NOSUID) && !defined(MNTTAB_OPT_NOSUID) */

#if defined(MNT2_GEN_OPT_OVERLAY) && !defined(MNTTAB_OPT_OVERLAY)
# define MNTTAB_OPT_OVERLAY "overlay"
#endif /* defined(MNT2_GEN_OPT_OVERLAY) && !defined(MNTTAB_OPT_OVERLAY) */

/*
 * Complete MNTTAB_OPT_* options and their inverse based on MNT2_GEN_OPT_*
 * options.
 */
#if defined(MNT2_GEN_OPT_NODEV) && !defined(MNTTAB_OPT_NODEV)
# define MNTTAB_OPT_NODEV "nodev"
#endif /* defined(MNT2_GEN_OPT_NODEV) && !defined(MNTTAB_OPT_NODEV) */

#if defined(MNT2_GEN_OPT_NOEXEC) && !defined(MNTTAB_OPT_NOEXEC)
# define MNTTAB_OPT_NOEXEC "noexec"
/* this is missing under some versions of Linux */
# ifndef MNTTAB_OPT_EXEC
#  define MNTTAB_OPT_EXEC "exec"
# endif /* not MNTTAB_OPT_EXEC */
#endif /* defined(MNT2_GEN_OPT_NOEXEC) && !defined(MNTTAB_OPT_NOEXEC) */

#if defined(MNT2_GEN_OPT_QUOTA) && !defined(MNTTAB_OPT_QUOTA)
# define MNTTAB_OPT_QUOTA "quota"
#endif /* defined(MNT2_GEN_OPT_QUOTA) && !defined(MNTTAB_OPT_QUOTA) */

#if defined(MNT2_GEN_OPT_SYNC) && !defined(MNTTAB_OPT_SYNC)
# define MNTTAB_OPT_SYNC "sync"
#endif /* defined(MNT2_GEN_OPT_SYNC) && !defined(MNTTAB_OPT_SYNC) */

#if defined(MNT2_GEN_OPT_LOG) && !defined(MNTTAB_OPT_LOG)
# define MNTTAB_OPT_LOG "log"
#endif /* defined(MNT2_GEN_OPT_LOG) && !defined(MNTTAB_OPT_LOG) */

#if defined(MNT2_GEN_OPT_NOATIME) && !defined(MNTTAB_OPT_NOATIME)
# define MNTTAB_OPT_NOATIME "noatime"
#endif /* defined(MNT2_GEN_OPT_NOATIME) && !defined(MNTTAB_OPT_NOATIME) */

#if defined(MNT2_GEN_OPT_NODEVMTIME) && !defined(MNTTAB_OPT_NODEVMTIME)
# define MNTTAB_OPT_NODEVMTIME "nodevmtime"
#endif /* defined(MNT2_GEN_OPT_NODEVMTIME) && !defined(MNTTAB_OPT_NODEVMTIME) */

#if defined(MNT2_GEN_OPT_SOFTDEP) && !defined(MNTTAB_OPT_SOFTDEP)
# define MNTTAB_OPT_SOFTDEP "softdep"
#endif /* defined(MNT2_GEN_OPT_SOFTDEP) && !defined(MNTTAB_OPT_SOFTDEP) */

#if defined(MNT2_GEN_OPT_SYMPERM) && !defined(MNTTAB_OPT_SYMPERM)
# define MNTTAB_OPT_SYMPERM "symperm"
#endif /* defined(MNT2_GEN_OPT_SYMPERM) && !defined(MNTTAB_OPT_SYMPERM) */

#if defined(MNT2_GEN_OPT_UNION) && !defined(MNTTAB_OPT_UNION)
# define MNTTAB_OPT_UNION "union"
#endif /* defined(MNT2_GEN_OPT_UNION) && !defined(MNTTAB_OPT_UNION) */

/*
 * Add missing MNTTAB_OPT_* options.
 */
#ifndef MNTTAB_OPT_ACTIMEO
# define MNTTAB_OPT_ACTIMEO "actimeo"
#endif /* not MNTTAB_OPT_ACTIMEO */

#ifndef MNTTAB_OPT_INTR
# define MNTTAB_OPT_INTR "intr"
#endif /* not MNTTAB_OPT_INTR */

#ifndef MNTTAB_OPT_PORT
# define MNTTAB_OPT_PORT "port"
#endif /* not MNTTAB_OPT_PORT */

#ifndef MNTTAB_OPT_PUBLIC
# define MNTTAB_OPT_PUBLIC "public"
#endif /* not MNTTAB_OPT_PUBLIC */

#ifndef MNTTAB_OPT_RETRANS
# define MNTTAB_OPT_RETRANS "retrans"
#endif /* not MNTTAB_OPT_RETRANS */

#ifndef MNTTAB_OPT_RETRY
# define MNTTAB_OPT_RETRY "retry"
#endif /* not MNTTAB_OPT_RETRY */

#ifndef MNTTAB_OPT_RO
# define MNTTAB_OPT_RO "ro"
#endif /* not MNTTAB_OPT_RO */

#ifndef MNTTAB_OPT_RSIZE
# define MNTTAB_OPT_RSIZE "rsize"
#endif /* not MNTTAB_OPT_RSIZE */

#ifndef MNTTAB_OPT_RW
# define MNTTAB_OPT_RW "rw"
#endif /* not MNTTAB_OPT_RW */

#ifndef MNTTAB_OPT_TIMEO
# define MNTTAB_OPT_TIMEO "timeo"
#endif /* not MNTTAB_OPT_TIMEO */

#ifndef MNTTAB_OPT_WSIZE
# define MNTTAB_OPT_WSIZE "wsize"
#endif /* not MNTTAB_OPT_WSIZE */

/* next four are useful for pcfs mounts */
#ifndef MNTTAB_OPT_USER
# define MNTTAB_OPT_USER "user"
#endif /* not MNTTAB_OPT_USER */
#ifndef MNTTAB_OPT_GROUP
# define MNTTAB_OPT_GROUP "group"
#endif /* not MNTTAB_OPT_GROUP */
#ifndef MNTTAB_OPT_MASK
# define MNTTAB_OPT_MASK "mask"
#endif /* not MNTTAB_OPT_MASK */
#ifndef MNTTAB_OPT_DIRMASK
# define MNTTAB_OPT_DIRMASK "dirmask"
#endif /* not MNTTAB_OPT_DIRMASK */

/* useful for udf mounts */
#ifndef MNTTAB_OPT_USER
# define MNTTAB_OPT_USER "user"
#endif /* not MNTTAB_OPT_USER */
#ifndef MNTTAB_OPT_GROUP
# define MNTTAB_OPT_GROUP "group"
#endif /* not MNTTAB_OPT_GROUP */
#ifndef MNTTAB_OPT_GMTOFF
# define MNTTAB_OPT_GMTOFF "gmtoff"
#endif /* not MNTTAB_OPT_GMTOFF */
#ifndef MNTTAB_OPT_SESSIONNR
# define MNTTAB_OPT_SESSIONNR "sessionnr"
#endif /* not MNTTAB_OPT_SESSIONNR */

/*
 * Incomplete filesystem definitions (sunos4, irix6, solaris2)
 */
#if defined(HAVE_FS_CDFS) && defined(MOUNT_TYPE_CDFS) && !defined(MNTTYPE_CDFS)
# define MNTTYPE_CDFS "hsfs"
#endif /* defined(HAVE_FS_CDFS) && defined(MOUNT_TYPE_CDFS) && !defined(MNTTYPE_CDFS) */

#ifndef cdfs_args_t
/*
 * Solaris has an HSFS filesystem, but does not define hsfs_args.
 * XXX: the definition here for solaris is wrong, since under solaris,
 * hsfs_args should be a single integer used as a bit-field for options.
 * so this code has to be fixed later.  -Erez.
 */
struct hsfs_args {
        char *fspec;    /* name of filesystem to mount */
        int norrip;
};
# define cdfs_args_t struct hsfs_args
# define HAVE_CDFS_ARGS_T_NORRIP
#endif /* not cdfs_args_t */

/*
 * if does not define struct pc_args, assume integer bit-field (irix6)
 */
#if defined(HAVE_FS_PCFS) && !defined(pcfs_args_t)
# define pcfs_args_t u_int
#endif /* defined(HAVE_FS_PCFS) && !defined(pcfs_args_t) */

/*
 * if does not define struct ufs_args, assume integer bit-field (linux)
 */
#if defined(HAVE_FS_UFS) && !defined(ufs_args_t)
# define ufs_args_t u_int
#endif /* defined(HAVE_FS_UFS) && !defined(ufs_args_t) */

/*
 * if does not define struct udf_args, assume integer bit-field (linux)
 */
#if defined(HAVE_FS_UDF) && !defined(udf_args_t)
# define udf_args_t u_int
#endif /* defined(HAVE_FS_UDF) && !defined(udf_args_t) */

/*
 * if does not define struct efs_args, assume integer bit-field (linux)
 */
#if defined(HAVE_FS_EFS) && !defined(efs_args_t)
# define efs_args_t u_int
#endif /* defined(HAVE_FS_EFS) && !defined(efs_args_t) */

#if defined(HAVE_FS_TMPFS) && !defined(tmpfs_args_t)
# define tmpfs_args_t u_int
#endif /* defined(HAVE_FS_TMPFS) && !defined(tmpfs_args_t) */

/*
 * if does not define struct xfs_args, assume integer bit-field (linux)
 */
#if defined(HAVE_FS_XFS) && !defined(xfs_args_t)
# define xfs_args_t u_int
#endif /* defined(HAVE_FS_XFS) && !defined(xfs_args_t) */
#if defined(HAVE_FS_EXT) && !defined(ext_args_t)
# define ext_args_t u_int
#endif /* defined(HAVE_FS_EXT) && !defined(ext_args_t) */

#if defined(HAVE_FS_AUTOFS) && defined(MOUNT_TYPE_AUTOFS) && !defined(MNTTYPE_AUTOFS)
# define MNTTYPE_AUTOFS "autofs"
#endif /* defined(HAVE_FS_AUTOFS) && defined(MOUNT_TYPE_AUTOFS) && !defined(MNTTYPE_AUTOFS) */

/*
 * If NFS3, then make sure that "proto" and "vers" mnttab options
 * are available.
 */
#ifdef HAVE_FS_NFS3
# ifndef MNTTAB_OPT_VERS
#  define MNTTAB_OPT_VERS "vers"
# endif /* not MNTTAB_OPT_VERS */
# ifndef MNTTAB_OPT_PROTO
#  define MNTTAB_OPT_PROTO "proto"
# endif /* not MNTTAB_OPT_PROTO */
#endif /* not HAVE_FS_NFS3 */

/*
 * If NFS4, then make sure that the "sec" mnttab option is available.
 */
#ifdef HAVE_FS_NFS4
# ifndef MNTTAB_OPT_SEC
#  define MNTTAB_OPT_SEC "sec"
# endif /* not MNTTAB_OPT_SEC */
#endif /* not HAVE_FS_NFS4 */
/*
 * If loop device (header file) exists, define mount table option
 */
#if defined(HAVE_LOOP_DEVICE) && !defined(MNTTAB_OPT_LOOP)
# define MNTTAB_OPT_LOOP "loop"
#endif /* defined(HAVE_LOOP_DEVICE) && !defined(MNTTAB_OPT_LOOP) */

/*
 * Define a dummy struct netconfig for non-TLI systems
 */
#if !defined(HAVE_NETCONFIG_H) && !defined(HAVE_SYS_NETCONFIG_H)
struct netconfig {
  int dummy;
};
#endif /* not HAVE_NETCONFIG_H and not HAVE_SYS_NETCONFIG_H */

/* some OSs don't define INADDR_NONE and assume it's unsigned -1 */
#ifndef INADDR_NONE
# define INADDR_NONE	0xffffffffU
#endif /* INADDR_NONE */
/* some OSs don't define INADDR_LOOPBACK */
#ifndef INADDR_LOOPBACK
# define INADDR_LOOPBACK	0x7f000001
#endif /* not INADDR_LOOPBACK */

#endif /* not _AM_COMPAT_H */
