/* Machine independent support for SVR4 /proc (process file system) for GDB.
   Copyright 1999, 2000 Free Software Foundation, Inc.
   Written by Michael Snyder at Cygnus Solutions.
   Based on work by Fred Fish, Stu Grossman, Geoff Noer, and others.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation, 
Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/*
 * Pretty-print "events of interest".
 *
 * This module includes pretty-print routines for:
 *	faults  (hardware exceptions):
 *	signals (software interrupts):
 *	syscalls
 *
 * FIXME: At present, the syscall translation table must be initialized, 
 * which is not true of the other translation tables.
 */

#include "defs.h"

#if defined (NEW_PROC_API)
#define _STRUCTURED_PROC 1
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/procfs.h>
#ifdef HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#ifdef HAVE_SYS_FAULT_H
#include <sys/fault.h>
#endif

/*  Much of the information used in the /proc interface, particularly for
    printing status information, is kept as tables of structures of the
    following form.  These tables can be used to map numeric values to
    their symbolic names and to a string that describes their specific use. */

struct trans {
  int value;                    /* The numeric value */
  char *name;                   /* The equivalent symbolic value */
  char *desc;                   /* Short description of value */
};

/*
 * pretty print syscalls
 */

/* Ugh -- Unixware and Solaris spell these differently! */

#ifdef  SYS_lwpcreate
#define SYS_lwp_create	SYS_lwpcreate
#endif

#ifdef  SYS_lwpexit
#define SYS_lwp_exit SYS_lwpexit
#endif

#ifdef  SYS_lwpwait
#define SYS_lwp_wait SYS_lwpwait
#endif

#ifdef  SYS_lwpself
#define SYS_lwp_self SYS_lwpself
#endif

#ifdef  SYS_lwpinfo
#define SYS_lwp_info SYS_lwpinfo
#endif

#ifdef  SYS_lwpprivate
#define SYS_lwp_private SYS_lwpprivate
#endif

#ifdef  SYS_lwpkill
#define SYS_lwp_kill SYS_lwpkill
#endif

#ifdef  SYS_lwpsuspend
#define SYS_lwp_suspend SYS_lwpsuspend
#endif

#ifdef  SYS_lwpcontinue
#define SYS_lwp_continue SYS_lwpcontinue
#endif


/* Syscall translation table. */

#define MAX_SYSCALLS 262	/* pretty arbitrary */
static char * syscall_table[MAX_SYSCALLS];

void
init_syscall_table (void)
{
#if defined (SYS_BSD_getime)
  syscall_table[SYS_BSD_getime] = "BSD_getime";
#endif
#if defined (SYS_BSDgetpgrp)
  syscall_table[SYS_BSDgetpgrp] = "BSDgetpgrp";
#endif
#if defined (SYS_BSDsetpgrp)
  syscall_table[SYS_BSDsetpgrp] = "BSDsetpgrp";
#endif
#if defined (SYS_acancel)
  syscall_table[SYS_acancel] = "acancel";
#endif
#if defined (SYS_accept)
  syscall_table[SYS_accept] = "accept";
#endif
#if defined (SYS_access)
  syscall_table[SYS_access] = "access";
#endif
#if defined (SYS_acct)
  syscall_table[SYS_acct] = "acct";
#endif
#if defined (SYS_acl)
  syscall_table[SYS_acl] = "acl";
#endif
#if defined (SYS_aclipc)
  syscall_table[SYS_aclipc] = "aclipc";
#endif
#if defined (SYS_adjtime)
  syscall_table[SYS_adjtime] = "adjtime";
#endif
#if defined (SYS_afs_syscall)
  syscall_table[SYS_afs_syscall] = "afs_syscall";
#endif
#if defined (SYS_alarm)
  syscall_table[SYS_alarm] = "alarm";
#endif
#if defined (SYS_alt_plock)
  syscall_table[SYS_alt_plock] = "alt_plock";
#endif
#if defined (SYS_alt_sigpending)
  syscall_table[SYS_alt_sigpending] = "alt_sigpending";
#endif
#if defined (SYS_async)
  syscall_table[SYS_async] = "async";
#endif
#if defined (SYS_async_daemon)
  syscall_table[SYS_async_daemon] = "async_daemon";
#endif
#if defined (SYS_audcntl)
  syscall_table[SYS_audcntl] = "audcntl";
#endif
#if defined (SYS_audgen)
  syscall_table[SYS_audgen] = "audgen";
#endif
#if defined (SYS_auditbuf)
  syscall_table[SYS_auditbuf] = "auditbuf";
#endif
#if defined (SYS_auditctl)
  syscall_table[SYS_auditctl] = "auditctl";
#endif
#if defined (SYS_auditdmp)
  syscall_table[SYS_auditdmp] = "auditdmp";
#endif
#if defined (SYS_auditevt)
  syscall_table[SYS_auditevt] = "auditevt";
#endif
#if defined (SYS_auditlog)
  syscall_table[SYS_auditlog] = "auditlog";
#endif
#if defined (SYS_auditsys)
  syscall_table[SYS_auditsys] = "auditsys";
#endif
#if defined (SYS_bind)
  syscall_table[SYS_bind] = "bind";
#endif
#if defined (SYS_block)
  syscall_table[SYS_block] = "block";
#endif
#if defined (SYS_brk)
  syscall_table[SYS_brk] = "brk";
#endif
#if defined (SYS_cachectl)
  syscall_table[SYS_cachectl] = "cachectl";
#endif
#if defined (SYS_cacheflush)
  syscall_table[SYS_cacheflush] = "cacheflush";
#endif
#if defined (SYS_cancelblock)
  syscall_table[SYS_cancelblock] = "cancelblock";
#endif
#if defined (SYS_cg_bind)
  syscall_table[SYS_cg_bind] = "cg_bind";
#endif
#if defined (SYS_cg_current)
  syscall_table[SYS_cg_current] = "cg_current";
#endif
#if defined (SYS_cg_ids)
  syscall_table[SYS_cg_ids] = "cg_ids";
#endif
#if defined (SYS_cg_info)
  syscall_table[SYS_cg_info] = "cg_info";
#endif
#if defined (SYS_cg_memloc)
  syscall_table[SYS_cg_memloc] = "cg_memloc";
#endif
#if defined (SYS_cg_processors)
  syscall_table[SYS_cg_processors] = "cg_processors";
#endif
#if defined (SYS_chdir)
  syscall_table[SYS_chdir] = "chdir";
#endif
#if defined (SYS_chflags)
  syscall_table[SYS_chflags] = "chflags";
#endif
#if defined (SYS_chmod)
  syscall_table[SYS_chmod] = "chmod";
#endif
#if defined (SYS_chown)
  syscall_table[SYS_chown] = "chown";
#endif
#if defined (SYS_chroot)
  syscall_table[SYS_chroot] = "chroot";
#endif
#if defined (SYS_clocal)
  syscall_table[SYS_clocal] = "clocal";
#endif
#if defined (SYS_clock_getres)
  syscall_table[SYS_clock_getres] = "clock_getres";
#endif
#if defined (SYS_clock_gettime)
  syscall_table[SYS_clock_gettime] = "clock_gettime";
#endif
#if defined (SYS_clock_settime)
  syscall_table[SYS_clock_settime] = "clock_settime";
#endif
#if defined (SYS_close)
  syscall_table[SYS_close] = "close";
#endif
#if defined (SYS_connect)
  syscall_table[SYS_connect] = "connect";
#endif
#if defined (SYS_context)
  syscall_table[SYS_context] = "context";
#endif
#if defined (SYS_creat)
  syscall_table[SYS_creat] = "creat";
#endif
#if defined (SYS_creat64)
  syscall_table[SYS_creat64] = "creat64";
#endif
#if defined (SYS_devstat)
  syscall_table[SYS_devstat] = "devstat";
#endif
#if defined (SYS_dmi)
  syscall_table[SYS_dmi] = "dmi";
#endif
#if defined (SYS_door)
  syscall_table[SYS_door] = "door";
#endif
#if defined (SYS_dshmsys)
  syscall_table[SYS_dshmsys] = "dshmsys";
#endif
#if defined (SYS_dup)
  syscall_table[SYS_dup] = "dup";
#endif
#if defined (SYS_dup2)
  syscall_table[SYS_dup2] = "dup2";
#endif
#if defined (SYS_evsys)
  syscall_table[SYS_evsys] = "evsys";
#endif
#if defined (SYS_evtrapret)
  syscall_table[SYS_evtrapret] = "evtrapret";
#endif
#if defined (SYS_exec)
  syscall_table[SYS_exec] = "exec";
#endif
#if defined (SYS_exec_with_loader)
  syscall_table[SYS_exec_with_loader] = "exec_with_loader";
#endif
#if defined (SYS_execv)
  syscall_table[SYS_execv] = "execv";
#endif
#if defined (SYS_execve)
  syscall_table[SYS_execve] = "execve";
#endif
#if defined (SYS_exit)
  syscall_table[SYS_exit] = "exit";
#endif
#if defined (SYS_exportfs)
  syscall_table[SYS_exportfs] = "exportfs";
#endif
#if defined (SYS_facl)
  syscall_table[SYS_facl] = "facl";
#endif
#if defined (SYS_fchdir)
  syscall_table[SYS_fchdir] = "fchdir";
#endif
#if defined (SYS_fchflags)
  syscall_table[SYS_fchflags] = "fchflags";
#endif
#if defined (SYS_fchmod)
  syscall_table[SYS_fchmod] = "fchmod";
#endif
#if defined (SYS_fchown)
  syscall_table[SYS_fchown] = "fchown";
#endif
#if defined (SYS_fchroot)
  syscall_table[SYS_fchroot] = "fchroot";
#endif
#if defined (SYS_fcntl)
  syscall_table[SYS_fcntl] = "fcntl";
#endif
#if defined (SYS_fdatasync)
  syscall_table[SYS_fdatasync] = "fdatasync";
#endif
#if defined (SYS_fdevstat)
  syscall_table[SYS_fdevstat] = "fdevstat";
#endif
#if defined (SYS_fdsync)
  syscall_table[SYS_fdsync] = "fdsync";
#endif
#if defined (SYS_filepriv)
  syscall_table[SYS_filepriv] = "filepriv";
#endif
#if defined (SYS_flock)
  syscall_table[SYS_flock] = "flock";
#endif
#if defined (SYS_flvlfile)
  syscall_table[SYS_flvlfile] = "flvlfile";
#endif
#if defined (SYS_fork)
  syscall_table[SYS_fork] = "fork";
#endif
#if defined (SYS_fork1)
  syscall_table[SYS_fork1] = "fork1";
#endif
#if defined (SYS_forkall)
  syscall_table[SYS_forkall] = "forkall";
#endif
#if defined (SYS_fpathconf)
  syscall_table[SYS_fpathconf] = "fpathconf";
#endif
#if defined (SYS_fstat)
  syscall_table[SYS_fstat] = "fstat";
#endif
#if defined (SYS_fstat64)
  syscall_table[SYS_fstat64] = "fstat64";
#endif
#if defined (SYS_fstatfs)
  syscall_table[SYS_fstatfs] = "fstatfs";
#endif
#if defined (SYS_fstatvfs)
  syscall_table[SYS_fstatvfs] = "fstatvfs";
#endif
#if defined (SYS_fstatvfs64)
  syscall_table[SYS_fstatvfs64] = "fstatvfs64";
#endif
#if defined (SYS_fsync)
  syscall_table[SYS_fsync] = "fsync";
#endif
#if defined (SYS_ftruncate)
  syscall_table[SYS_ftruncate] = "ftruncate";
#endif
#if defined (SYS_ftruncate64)
  syscall_table[SYS_ftruncate64] = "ftruncate64";
#endif
#if defined (SYS_fuser)
  syscall_table[SYS_fuser] = "fuser";
#endif
#if defined (SYS_fxstat)
  syscall_table[SYS_fxstat] = "fxstat";
#endif
#if defined (SYS_get_sysinfo)
  syscall_table[SYS_get_sysinfo] = "get_sysinfo";
#endif
#if defined (SYS_getaddressconf)
  syscall_table[SYS_getaddressconf] = "getaddressconf";
#endif
#if defined (SYS_getcontext)
  syscall_table[SYS_getcontext] = "getcontext";
#endif
#if defined (SYS_getdents)
  syscall_table[SYS_getdents] = "getdents";
#endif
#if defined (SYS_getdents64)
  syscall_table[SYS_getdents64] = "getdents64";
#endif
#if defined (SYS_getdirentries)
  syscall_table[SYS_getdirentries] = "getdirentries";
#endif
#if defined (SYS_getdomainname)
  syscall_table[SYS_getdomainname] = "getdomainname";
#endif
#if defined (SYS_getdtablesize)
  syscall_table[SYS_getdtablesize] = "getdtablesize";
#endif
#if defined (SYS_getfh)
  syscall_table[SYS_getfh] = "getfh";
#endif
#if defined (SYS_getfsstat)
  syscall_table[SYS_getfsstat] = "getfsstat";
#endif
#if defined (SYS_getgid)
  syscall_table[SYS_getgid] = "getgid";
#endif
#if defined (SYS_getgroups)
  syscall_table[SYS_getgroups] = "getgroups";
#endif
#if defined (SYS_gethostid)
  syscall_table[SYS_gethostid] = "gethostid";
#endif
#if defined (SYS_gethostname)
  syscall_table[SYS_gethostname] = "gethostname";
#endif
#if defined (SYS_getitimer)
  syscall_table[SYS_getitimer] = "getitimer";
#endif
#if defined (SYS_getksym)
  syscall_table[SYS_getksym] = "getksym";
#endif
#if defined (SYS_getlogin)
  syscall_table[SYS_getlogin] = "getlogin";
#endif
#if defined (SYS_getmnt)
  syscall_table[SYS_getmnt] = "getmnt";
#endif
#if defined (SYS_getmsg)
  syscall_table[SYS_getmsg] = "getmsg";
#endif
#if defined (SYS_getpagesize)
  syscall_table[SYS_getpagesize] = "getpagesize";
#endif
#if defined (SYS_getpeername)
  syscall_table[SYS_getpeername] = "getpeername";
#endif
#if defined (SYS_getpgid)
  syscall_table[SYS_getpgid] = "getpgid";
#endif
#if defined (SYS_getpgrp)
  syscall_table[SYS_getpgrp] = "getpgrp";
#endif
#if defined (SYS_getpid)
  syscall_table[SYS_getpid] = "getpid";
#endif
#if defined (SYS_getpmsg)
  syscall_table[SYS_getpmsg] = "getpmsg";
#endif
#if defined (SYS_getpriority)
  syscall_table[SYS_getpriority] = "getpriority";
#endif
#if defined (SYS_getrlimit)
  syscall_table[SYS_getrlimit] = "getrlimit";
#endif
#if defined (SYS_getrlimit64)
  syscall_table[SYS_getrlimit64] = "getrlimit64";
#endif
#if defined (SYS_getrusage)
  syscall_table[SYS_getrusage] = "getrusage";
#endif
#if defined (SYS_getsid)
  syscall_table[SYS_getsid] = "getsid";
#endif
#if defined (SYS_getsockname)
  syscall_table[SYS_getsockname] = "getsockname";
#endif
#if defined (SYS_getsockopt)
  syscall_table[SYS_getsockopt] = "getsockopt";
#endif
#if defined (SYS_gettimeofday)
  syscall_table[SYS_gettimeofday] = "gettimeofday";
#endif
#if defined (SYS_getuid)
  syscall_table[SYS_getuid] = "getuid";
#endif
#if defined (SYS_gtty)
  syscall_table[SYS_gtty] = "gtty";
#endif
#if defined (SYS_hrtsys)
  syscall_table[SYS_hrtsys] = "hrtsys";
#endif
#if defined (SYS_inst_sync)
  syscall_table[SYS_inst_sync] = "inst_sync";
#endif
#if defined (SYS_install_utrap)
  syscall_table[SYS_install_utrap] = "install_utrap";
#endif
#if defined (SYS_invlpg)
  syscall_table[SYS_invlpg] = "invlpg";
#endif
#if defined (SYS_ioctl)
  syscall_table[SYS_ioctl] = "ioctl";
#endif
#if defined (SYS_kaio)
  syscall_table[SYS_kaio] = "kaio";
#endif
#if defined (SYS_keyctl)
  syscall_table[SYS_keyctl] = "keyctl";
#endif
#if defined (SYS_kill)
  syscall_table[SYS_kill] = "kill";
#endif
#if defined (SYS_killpg)
  syscall_table[SYS_killpg] = "killpg";
#endif
#if defined (SYS_kloadcall)
  syscall_table[SYS_kloadcall] = "kloadcall";
#endif
#if defined (SYS_kmodcall)
  syscall_table[SYS_kmodcall] = "kmodcall";
#endif
#if defined (SYS_ksigaction)
  syscall_table[SYS_ksigaction] = "ksigaction";
#endif
#if defined (SYS_ksigprocmask)
  syscall_table[SYS_ksigprocmask] = "ksigprocmask";
#endif
#if defined (SYS_ksigqueue)
  syscall_table[SYS_ksigqueue] = "ksigqueue";
#endif
#if defined (SYS_lchown)
  syscall_table[SYS_lchown] = "lchown";
#endif
#if defined (SYS_link)
  syscall_table[SYS_link] = "link";
#endif
#if defined (SYS_listen)
  syscall_table[SYS_listen] = "listen";
#endif
#if defined (SYS_llseek)
  syscall_table[SYS_llseek] = "llseek";
#endif
#if defined (SYS_lseek)
  syscall_table[SYS_lseek] = "lseek";
#endif
#if defined (SYS_lseek64)
  syscall_table[SYS_lseek64] = "lseek64";
#endif
#if defined (SYS_lstat)
  syscall_table[SYS_lstat] = "lstat";
#endif
#if defined (SYS_lstat64)
  syscall_table[SYS_lstat64] = "lstat64";
#endif
#if defined (SYS_lvldom)
  syscall_table[SYS_lvldom] = "lvldom";
#endif
#if defined (SYS_lvlequal)
  syscall_table[SYS_lvlequal] = "lvlequal";
#endif
#if defined (SYS_lvlfile)
  syscall_table[SYS_lvlfile] = "lvlfile";
#endif
#if defined (SYS_lvlipc)
  syscall_table[SYS_lvlipc] = "lvlipc";
#endif
#if defined (SYS_lvlproc)
  syscall_table[SYS_lvlproc] = "lvlproc";
#endif
#if defined (SYS_lvlvfs)
  syscall_table[SYS_lvlvfs] = "lvlvfs";
#endif
#if defined (SYS_lwp_alarm)
  syscall_table[SYS_lwp_alarm] = "lwp_alarm";
#endif
#if defined (SYS_lwp_cond_broadcast)
  syscall_table[SYS_lwp_cond_broadcast] = "lwp_cond_broadcast";
#endif
#if defined (SYS_lwp_cond_signal)
  syscall_table[SYS_lwp_cond_signal] = "lwp_cond_signal";
#endif
#if defined (SYS_lwp_cond_wait)
  syscall_table[SYS_lwp_cond_wait] = "lwp_cond_wait";
#endif
#if defined (SYS_lwp_continue)
  syscall_table[SYS_lwp_continue] = "lwp_continue";
#endif
#if defined (SYS_lwp_create)
  syscall_table[SYS_lwp_create] = "lwp_create";
#endif
#if defined (SYS_lwp_exit)
  syscall_table[SYS_lwp_exit] = "lwp_exit";
#endif
#if defined (SYS_lwp_getprivate)
  syscall_table[SYS_lwp_getprivate] = "lwp_getprivate";
#endif
#if defined (SYS_lwp_info)
  syscall_table[SYS_lwp_info] = "lwp_info";
#endif
#if defined (SYS_lwp_kill)
  syscall_table[SYS_lwp_kill] = "lwp_kill";
#endif
#if defined (SYS_lwp_mutex_init)
  syscall_table[SYS_lwp_mutex_init] = "lwp_mutex_init";
#endif
#if defined (SYS_lwp_mutex_lock)
  syscall_table[SYS_lwp_mutex_lock] = "lwp_mutex_lock";
#endif
#if defined (SYS_lwp_mutex_trylock)
  syscall_table[SYS_lwp_mutex_trylock] = "lwp_mutex_trylock";
#endif
#if defined (SYS_lwp_mutex_unlock)
  syscall_table[SYS_lwp_mutex_unlock] = "lwp_mutex_unlock";
#endif
#if defined (SYS_lwp_private)
  syscall_table[SYS_lwp_private] = "lwp_private";
#endif
#if defined (SYS_lwp_self)
  syscall_table[SYS_lwp_self] = "lwp_self";
#endif
#if defined (SYS_lwp_sema_post)
  syscall_table[SYS_lwp_sema_post] = "lwp_sema_post";
#endif
#if defined (SYS_lwp_sema_trywait)
  syscall_table[SYS_lwp_sema_trywait] = "lwp_sema_trywait";
#endif
#if defined (SYS_lwp_sema_wait)
  syscall_table[SYS_lwp_sema_wait] = "lwp_sema_wait";
#endif
#if defined (SYS_lwp_setprivate)
  syscall_table[SYS_lwp_setprivate] = "lwp_setprivate";
#endif
#if defined (SYS_lwp_sigredirect)
  syscall_table[SYS_lwp_sigredirect] = "lwp_sigredirect";
#endif
#if defined (SYS_lwp_suspend)
  syscall_table[SYS_lwp_suspend] = "lwp_suspend";
#endif
#if defined (SYS_lwp_wait)
  syscall_table[SYS_lwp_wait] = "lwp_wait";
#endif
#if defined (SYS_lxstat)
  syscall_table[SYS_lxstat] = "lxstat";
#endif
#if defined (SYS_madvise)
  syscall_table[SYS_madvise] = "madvise";
#endif
#if defined (SYS_memcntl)
  syscall_table[SYS_memcntl] = "memcntl";
#endif
#if defined (SYS_mincore)
  syscall_table[SYS_mincore] = "mincore";
#endif
#if defined (SYS_mincore)
  syscall_table[SYS_mincore] = "mincore";
#endif
#if defined (SYS_mkdir)
  syscall_table[SYS_mkdir] = "mkdir";
#endif
#if defined (SYS_mkmld)
  syscall_table[SYS_mkmld] = "mkmld";
#endif
#if defined (SYS_mknod)
  syscall_table[SYS_mknod] = "mknod";
#endif
#if defined (SYS_mldmode)
  syscall_table[SYS_mldmode] = "mldmode";
#endif
#if defined (SYS_mmap)
  syscall_table[SYS_mmap] = "mmap";
#endif
#if defined (SYS_mmap64)
  syscall_table[SYS_mmap64] = "mmap64";
#endif
#if defined (SYS_modadm)
  syscall_table[SYS_modadm] = "modadm";
#endif
#if defined (SYS_modctl)
  syscall_table[SYS_modctl] = "modctl";
#endif
#if defined (SYS_modload)
  syscall_table[SYS_modload] = "modload";
#endif
#if defined (SYS_modpath)
  syscall_table[SYS_modpath] = "modpath";
#endif
#if defined (SYS_modstat)
  syscall_table[SYS_modstat] = "modstat";
#endif
#if defined (SYS_moduload)
  syscall_table[SYS_moduload] = "moduload";
#endif
#if defined (SYS_mount)
  syscall_table[SYS_mount] = "mount";
#endif
#if defined (SYS_mprotect)
  syscall_table[SYS_mprotect] = "mprotect";
#endif
#if defined (SYS_mremap)
  syscall_table[SYS_mremap] = "mremap";
#endif
#if defined (SYS_msfs_syscall)
  syscall_table[SYS_msfs_syscall] = "msfs_syscall";
#endif
#if defined (SYS_msgctl)
  syscall_table[SYS_msgctl] = "msgctl";
#endif
#if defined (SYS_msgget)
  syscall_table[SYS_msgget] = "msgget";
#endif
#if defined (SYS_msgrcv)
  syscall_table[SYS_msgrcv] = "msgrcv";
#endif
#if defined (SYS_msgsnd)
  syscall_table[SYS_msgsnd] = "msgsnd";
#endif
#if defined (SYS_msgsys)
  syscall_table[SYS_msgsys] = "msgsys";
#endif
#if defined (SYS_msleep)
  syscall_table[SYS_msleep] = "msleep";
#endif
#if defined (SYS_msync)
  syscall_table[SYS_msync] = "msync";
#endif
#if defined (SYS_munmap)
  syscall_table[SYS_munmap] = "munmap";
#endif
#if defined (SYS_mvalid)
  syscall_table[SYS_mvalid] = "mvalid";
#endif
#if defined (SYS_mwakeup)
  syscall_table[SYS_mwakeup] = "mwakeup";
#endif
#if defined (SYS_naccept)
  syscall_table[SYS_naccept] = "naccept";
#endif
#if defined (SYS_nanosleep)
  syscall_table[SYS_nanosleep] = "nanosleep";
#endif
#if defined (SYS_nfssvc)
  syscall_table[SYS_nfssvc] = "nfssvc";
#endif
#if defined (SYS_nfssys)
  syscall_table[SYS_nfssys] = "nfssys";
#endif
#if defined (SYS_ngetpeername)
  syscall_table[SYS_ngetpeername] = "ngetpeername";
#endif
#if defined (SYS_ngetsockname)
  syscall_table[SYS_ngetsockname] = "ngetsockname";
#endif
#if defined (SYS_nice)
  syscall_table[SYS_nice] = "nice";
#endif
#if defined (SYS_nrecvfrom)
  syscall_table[SYS_nrecvfrom] = "nrecvfrom";
#endif
#if defined (SYS_nrecvmsg)
  syscall_table[SYS_nrecvmsg] = "nrecvmsg";
#endif
#if defined (SYS_nsendmsg)
  syscall_table[SYS_nsendmsg] = "nsendmsg";
#endif
#if defined (SYS_ntp_adjtime)
  syscall_table[SYS_ntp_adjtime] = "ntp_adjtime";
#endif
#if defined (SYS_ntp_gettime)
  syscall_table[SYS_ntp_gettime] = "ntp_gettime";
#endif
#if defined (SYS_nuname)
  syscall_table[SYS_nuname] = "nuname";
#endif
#if defined (SYS_obreak)
  syscall_table[SYS_obreak] = "obreak";
#endif
#if defined (SYS_old_accept)
  syscall_table[SYS_old_accept] = "old_accept";
#endif
#if defined (SYS_old_fstat)
  syscall_table[SYS_old_fstat] = "old_fstat";
#endif
#if defined (SYS_old_getpeername)
  syscall_table[SYS_old_getpeername] = "old_getpeername";
#endif
#if defined (SYS_old_getpgrp)
  syscall_table[SYS_old_getpgrp] = "old_getpgrp";
#endif
#if defined (SYS_old_getsockname)
  syscall_table[SYS_old_getsockname] = "old_getsockname";
#endif
#if defined (SYS_old_killpg)
  syscall_table[SYS_old_killpg] = "old_killpg";
#endif
#if defined (SYS_old_lstat)
  syscall_table[SYS_old_lstat] = "old_lstat";
#endif
#if defined (SYS_old_recv)
  syscall_table[SYS_old_recv] = "old_recv";
#endif
#if defined (SYS_old_recvfrom)
  syscall_table[SYS_old_recvfrom] = "old_recvfrom";
#endif
#if defined (SYS_old_recvmsg)
  syscall_table[SYS_old_recvmsg] = "old_recvmsg";
#endif
#if defined (SYS_old_send)
  syscall_table[SYS_old_send] = "old_send";
#endif
#if defined (SYS_old_sendmsg)
  syscall_table[SYS_old_sendmsg] = "old_sendmsg";
#endif
#if defined (SYS_old_sigblock)
  syscall_table[SYS_old_sigblock] = "old_sigblock";
#endif
#if defined (SYS_old_sigsetmask)
  syscall_table[SYS_old_sigsetmask] = "old_sigsetmask";
#endif
#if defined (SYS_old_sigvec)
  syscall_table[SYS_old_sigvec] = "old_sigvec";
#endif
#if defined (SYS_old_stat)
  syscall_table[SYS_old_stat] = "old_stat";
#endif
#if defined (SYS_old_vhangup)
  syscall_table[SYS_old_vhangup] = "old_vhangup";
#endif
#if defined (SYS_old_wait)
  syscall_table[SYS_old_wait] = "old_wait";
#endif
#if defined (SYS_oldquota)
  syscall_table[SYS_oldquota] = "oldquota";
#endif
#if defined (SYS_online)
  syscall_table[SYS_online] = "online";
#endif
#if defined (SYS_open)
  syscall_table[SYS_open] = "open";
#endif
#if defined (SYS_open64)
  syscall_table[SYS_open64] = "open64";
#endif
#if defined (SYS_ovadvise)
  syscall_table[SYS_ovadvise] = "ovadvise";
#endif
#if defined (SYS_p_online)
  syscall_table[SYS_p_online] = "p_online";
#endif
#if defined (SYS_pagelock)
  syscall_table[SYS_pagelock] = "pagelock";
#endif
#if defined (SYS_pathconf)
  syscall_table[SYS_pathconf] = "pathconf";
#endif
#if defined (SYS_pause)
  syscall_table[SYS_pause] = "pause";
#endif
#if defined (SYS_pgrpsys)
  syscall_table[SYS_pgrpsys] = "pgrpsys";
#endif
#if defined (SYS_pid_block)
  syscall_table[SYS_pid_block] = "pid_block";
#endif
#if defined (SYS_pid_unblock)
  syscall_table[SYS_pid_unblock] = "pid_unblock";
#endif
#if defined (SYS_pipe)
  syscall_table[SYS_pipe] = "pipe";
#endif
#if defined (SYS_plock)
  syscall_table[SYS_plock] = "plock";
#endif
#if defined (SYS_poll)
  syscall_table[SYS_poll] = "poll";
#endif
#if defined (SYS_prctl)
  syscall_table[SYS_prctl] = "prctl";
#endif
#if defined (SYS_pread)
  syscall_table[SYS_pread] = "pread";
#endif
#if defined (SYS_pread64)
  syscall_table[SYS_pread64] = "pread64";
#endif
#if defined (SYS_pread64)
  syscall_table[SYS_pread64] = "pread64";
#endif
#if defined (SYS_prepblock)
  syscall_table[SYS_prepblock] = "prepblock";
#endif
#if defined (SYS_priocntl)
  syscall_table[SYS_priocntl] = "priocntl";
#endif
#if defined (SYS_priocntllst)
  syscall_table[SYS_priocntllst] = "priocntllst";
#endif
#if defined (SYS_priocntlset)
  syscall_table[SYS_priocntlset] = "priocntlset";
#endif
#if defined (SYS_priocntlsys)
  syscall_table[SYS_priocntlsys] = "priocntlsys";
#endif
#if defined (SYS_procblk)
  syscall_table[SYS_procblk] = "procblk";
#endif
#if defined (SYS_processor_bind)
  syscall_table[SYS_processor_bind] = "processor_bind";
#endif
#if defined (SYS_processor_exbind)
  syscall_table[SYS_processor_exbind] = "processor_exbind";
#endif
#if defined (SYS_processor_info)
  syscall_table[SYS_processor_info] = "processor_info";
#endif
#if defined (SYS_procpriv)
  syscall_table[SYS_procpriv] = "procpriv";
#endif
#if defined (SYS_profil)
  syscall_table[SYS_profil] = "profil";
#endif
#if defined (SYS_proplist_syscall)
  syscall_table[SYS_proplist_syscall] = "proplist_syscall";
#endif
#if defined (SYS_pset)
  syscall_table[SYS_pset] = "pset";
#endif
#if defined (SYS_ptrace)
  syscall_table[SYS_ptrace] = "ptrace";
#endif
#if defined (SYS_putmsg)
  syscall_table[SYS_putmsg] = "putmsg";
#endif
#if defined (SYS_putpmsg)
  syscall_table[SYS_putpmsg] = "putpmsg";
#endif
#if defined (SYS_pwrite)
  syscall_table[SYS_pwrite] = "pwrite";
#endif
#if defined (SYS_pwrite64)
  syscall_table[SYS_pwrite64] = "pwrite64";
#endif
#if defined (SYS_quotactl)
  syscall_table[SYS_quotactl] = "quotactl";
#endif
#if defined (SYS_rdblock)
  syscall_table[SYS_rdblock] = "rdblock";
#endif
#if defined (SYS_read)
  syscall_table[SYS_read] = "read";
#endif
#if defined (SYS_readlink)
  syscall_table[SYS_readlink] = "readlink";
#endif
#if defined (SYS_readv)
  syscall_table[SYS_readv] = "readv";
#endif
#if defined (SYS_reboot)
  syscall_table[SYS_reboot] = "reboot";
#endif
#if defined (SYS_recv)
  syscall_table[SYS_recv] = "recv";
#endif
#if defined (SYS_recvfrom)
  syscall_table[SYS_recvfrom] = "recvfrom";
#endif
#if defined (SYS_recvmsg)
  syscall_table[SYS_recvmsg] = "recvmsg";
#endif
#if defined (SYS_rename)
  syscall_table[SYS_rename] = "rename";
#endif
#if defined (SYS_resolvepath)
  syscall_table[SYS_resolvepath] = "resolvepath";
#endif
#if defined (SYS_revoke)
  syscall_table[SYS_revoke] = "revoke";
#endif
#if defined (SYS_rfsys)
  syscall_table[SYS_rfsys] = "rfsys";
#endif
#if defined (SYS_rmdir)
  syscall_table[SYS_rmdir] = "rmdir";
#endif
#if defined (SYS_rpcsys)
  syscall_table[SYS_rpcsys] = "rpcsys";
#endif
#if defined (SYS_sbrk)
  syscall_table[SYS_sbrk] = "sbrk";
#endif
#if defined (SYS_schedctl)
  syscall_table[SYS_schedctl] = "schedctl";
#endif
#if defined (SYS_secadvise)
  syscall_table[SYS_secadvise] = "secadvise";
#endif
#if defined (SYS_secsys)
  syscall_table[SYS_secsys] = "secsys";
#endif
#if defined (SYS_security)
  syscall_table[SYS_security] = "security";
#endif
#if defined (SYS_select)
  syscall_table[SYS_select] = "select";
#endif
#if defined (SYS_semctl)
  syscall_table[SYS_semctl] = "semctl";
#endif
#if defined (SYS_semget)
  syscall_table[SYS_semget] = "semget";
#endif
#if defined (SYS_semop)
  syscall_table[SYS_semop] = "semop";
#endif
#if defined (SYS_semsys)
  syscall_table[SYS_semsys] = "semsys";
#endif
#if defined (SYS_send)
  syscall_table[SYS_send] = "send";
#endif
#if defined (SYS_sendmsg)
  syscall_table[SYS_sendmsg] = "sendmsg";
#endif
#if defined (SYS_sendto)
  syscall_table[SYS_sendto] = "sendto";
#endif
#if defined (SYS_set_program_attributes)
  syscall_table[SYS_set_program_attributes] = "set_program_attributes";
#endif
#if defined (SYS_set_speculative)
  syscall_table[SYS_set_speculative] = "set_speculative";
#endif
#if defined (SYS_set_sysinfo)
  syscall_table[SYS_set_sysinfo] = "set_sysinfo";
#endif
#if defined (SYS_setcontext)
  syscall_table[SYS_setcontext] = "setcontext";
#endif
#if defined (SYS_setdomainname)
  syscall_table[SYS_setdomainname] = "setdomainname";
#endif
#if defined (SYS_setegid)
  syscall_table[SYS_setegid] = "setegid";
#endif
#if defined (SYS_seteuid)
  syscall_table[SYS_seteuid] = "seteuid";
#endif
#if defined (SYS_setgid)
  syscall_table[SYS_setgid] = "setgid";
#endif
#if defined (SYS_setgroups)
  syscall_table[SYS_setgroups] = "setgroups";
#endif
#if defined (SYS_sethostid)
  syscall_table[SYS_sethostid] = "sethostid";
#endif
#if defined (SYS_sethostname)
  syscall_table[SYS_sethostname] = "sethostname";
#endif
#if defined (SYS_setitimer)
  syscall_table[SYS_setitimer] = "setitimer";
#endif
#if defined (SYS_setlogin)
  syscall_table[SYS_setlogin] = "setlogin";
#endif
#if defined (SYS_setpgid)
  syscall_table[SYS_setpgid] = "setpgid";
#endif
#if defined (SYS_setpgrp)
  syscall_table[SYS_setpgrp] = "setpgrp";
#endif
#if defined (SYS_setpriority)
  syscall_table[SYS_setpriority] = "setpriority";
#endif
#if defined (SYS_setregid)
  syscall_table[SYS_setregid] = "setregid";
#endif
#if defined (SYS_setreuid)
  syscall_table[SYS_setreuid] = "setreuid";
#endif
#if defined (SYS_setrlimit)
  syscall_table[SYS_setrlimit] = "setrlimit";
#endif
#if defined (SYS_setrlimit64)
  syscall_table[SYS_setrlimit64] = "setrlimit64";
#endif
#if defined (SYS_setsid)
  syscall_table[SYS_setsid] = "setsid";
#endif
#if defined (SYS_setsockopt)
  syscall_table[SYS_setsockopt] = "setsockopt";
#endif
#if defined (SYS_settimeofday)
  syscall_table[SYS_settimeofday] = "settimeofday";
#endif
#if defined (SYS_setuid)
  syscall_table[SYS_setuid] = "setuid";
#endif
#if defined (SYS_sgi)
  syscall_table[SYS_sgi] = "sgi";
#endif
#if defined (SYS_sgifastpath)
  syscall_table[SYS_sgifastpath] = "sgifastpath";
#endif
#if defined (SYS_sgikopt)
  syscall_table[SYS_sgikopt] = "sgikopt";
#endif
#if defined (SYS_sginap)
  syscall_table[SYS_sginap] = "sginap";
#endif
#if defined (SYS_shmat)
  syscall_table[SYS_shmat] = "shmat";
#endif
#if defined (SYS_shmctl)
  syscall_table[SYS_shmctl] = "shmctl";
#endif
#if defined (SYS_shmdt)
  syscall_table[SYS_shmdt] = "shmdt";
#endif
#if defined (SYS_shmget)
  syscall_table[SYS_shmget] = "shmget";
#endif
#if defined (SYS_shmsys)
  syscall_table[SYS_shmsys] = "shmsys";
#endif
#if defined (SYS_shutdown)
  syscall_table[SYS_shutdown] = "shutdown";
#endif
#if defined (SYS_sigaction)
  syscall_table[SYS_sigaction] = "sigaction";
#endif
#if defined (SYS_sigaltstack)
  syscall_table[SYS_sigaltstack] = "sigaltstack";
#endif
#if defined (SYS_sigaltstack)
  syscall_table[SYS_sigaltstack] = "sigaltstack";
#endif
#if defined (SYS_sigblock)
  syscall_table[SYS_sigblock] = "sigblock";
#endif
#if defined (SYS_signal)
  syscall_table[SYS_signal] = "signal";
#endif
#if defined (SYS_signotify)
  syscall_table[SYS_signotify] = "signotify";
#endif
#if defined (SYS_signotifywait)
  syscall_table[SYS_signotifywait] = "signotifywait";
#endif
#if defined (SYS_sigpending)
  syscall_table[SYS_sigpending] = "sigpending";
#endif
#if defined (SYS_sigpoll)
  syscall_table[SYS_sigpoll] = "sigpoll";
#endif
#if defined (SYS_sigprocmask)
  syscall_table[SYS_sigprocmask] = "sigprocmask";
#endif
#if defined (SYS_sigqueue)
  syscall_table[SYS_sigqueue] = "sigqueue";
#endif
#if defined (SYS_sigreturn)
  syscall_table[SYS_sigreturn] = "sigreturn";
#endif
#if defined (SYS_sigsendset)
  syscall_table[SYS_sigsendset] = "sigsendset";
#endif
#if defined (SYS_sigsendsys)
  syscall_table[SYS_sigsendsys] = "sigsendsys";
#endif
#if defined (SYS_sigsetmask)
  syscall_table[SYS_sigsetmask] = "sigsetmask";
#endif
#if defined (SYS_sigstack)
  syscall_table[SYS_sigstack] = "sigstack";
#endif
#if defined (SYS_sigsuspend)
  syscall_table[SYS_sigsuspend] = "sigsuspend";
#endif
#if defined (SYS_sigvec)
  syscall_table[SYS_sigvec] = "sigvec";
#endif
#if defined (SYS_sigwait)
  syscall_table[SYS_sigwait] = "sigwait";
#endif
#if defined (SYS_sigwaitprim)
  syscall_table[SYS_sigwaitprim] = "sigwaitprim";
#endif
#if defined (SYS_sleep)
  syscall_table[SYS_sleep] = "sleep";
#endif
#if defined (SYS_so_socket)
  syscall_table[SYS_so_socket] = "so_socket";
#endif
#if defined (SYS_so_socketpair)
  syscall_table[SYS_so_socketpair] = "so_socketpair";
#endif
#if defined (SYS_sockconfig)
  syscall_table[SYS_sockconfig] = "sockconfig";
#endif
#if defined (SYS_socket)
  syscall_table[SYS_socket] = "socket";
#endif
#if defined (SYS_socketpair)
  syscall_table[SYS_socketpair] = "socketpair";
#endif
#if defined (SYS_sproc)
  syscall_table[SYS_sproc] = "sproc";
#endif
#if defined (SYS_sprocsp)
  syscall_table[SYS_sprocsp] = "sprocsp";
#endif
#if defined (SYS_sstk)
  syscall_table[SYS_sstk] = "sstk";
#endif
#if defined (SYS_stat)
  syscall_table[SYS_stat] = "stat";
#endif
#if defined (SYS_stat64)
  syscall_table[SYS_stat64] = "stat64";
#endif
#if defined (SYS_statfs)
  syscall_table[SYS_statfs] = "statfs";
#endif
#if defined (SYS_statvfs)
  syscall_table[SYS_statvfs] = "statvfs";
#endif
#if defined (SYS_statvfs64)
  syscall_table[SYS_statvfs64] = "statvfs64";
#endif
#if defined (SYS_stime)
  syscall_table[SYS_stime] = "stime";
#endif
#if defined (SYS_stty)
  syscall_table[SYS_stty] = "stty";
#endif
#if defined (SYS_subsys_info)
  syscall_table[SYS_subsys_info] = "subsys_info";
#endif
#if defined (SYS_swapctl)
  syscall_table[SYS_swapctl] = "swapctl";
#endif
#if defined (SYS_swapon)
  syscall_table[SYS_swapon] = "swapon";
#endif
#if defined (SYS_symlink)
  syscall_table[SYS_symlink] = "symlink";
#endif
#if defined (SYS_sync)
  syscall_table[SYS_sync] = "sync";
#endif
#if defined (SYS_sys3b)
  syscall_table[SYS_sys3b] = "sys3b";
#endif
#if defined (SYS_syscall)
  syscall_table[SYS_syscall] = "syscall";
#endif
#if defined (SYS_sysconfig)
  syscall_table[SYS_sysconfig] = "sysconfig";
#endif
#if defined (SYS_sysfs)
  syscall_table[SYS_sysfs] = "sysfs";
#endif
#if defined (SYS_sysi86)
  syscall_table[SYS_sysi86] = "sysi86";
#endif
#if defined (SYS_sysinfo)
  syscall_table[SYS_sysinfo] = "sysinfo";
#endif
#if defined (SYS_sysmips)
  syscall_table[SYS_sysmips] = "sysmips";
#endif
#if defined (SYS_syssun)
  syscall_table[SYS_syssun] = "syssun";
#endif
#if defined (SYS_systeminfo)
  syscall_table[SYS_systeminfo] = "systeminfo";
#endif
#if defined (SYS_table)
  syscall_table[SYS_table] = "table";
#endif
#if defined (SYS_time)
  syscall_table[SYS_time] = "time";
#endif
#if defined (SYS_timedwait)
  syscall_table[SYS_timedwait] = "timedwait";
#endif
#if defined (SYS_timer_create)
  syscall_table[SYS_timer_create] = "timer_create";
#endif
#if defined (SYS_timer_delete)
  syscall_table[SYS_timer_delete] = "timer_delete";
#endif
#if defined (SYS_timer_getoverrun)
  syscall_table[SYS_timer_getoverrun] = "timer_getoverrun";
#endif
#if defined (SYS_timer_gettime)
  syscall_table[SYS_timer_gettime] = "timer_gettime";
#endif
#if defined (SYS_timer_settime)
  syscall_table[SYS_timer_settime] = "timer_settime";
#endif
#if defined (SYS_times)
  syscall_table[SYS_times] = "times";
#endif
#if defined (SYS_truncate)
  syscall_table[SYS_truncate] = "truncate";
#endif
#if defined (SYS_truncate64)
  syscall_table[SYS_truncate64] = "truncate64";
#endif
#if defined (SYS_tsolsys)
  syscall_table[SYS_tsolsys] = "tsolsys";
#endif
#if defined (SYS_uadmin)
  syscall_table[SYS_uadmin] = "uadmin";
#endif
#if defined (SYS_ulimit)
  syscall_table[SYS_ulimit] = "ulimit";
#endif
#if defined (SYS_umask)
  syscall_table[SYS_umask] = "umask";
#endif
#if defined (SYS_umount)
  syscall_table[SYS_umount] = "umount";
#endif
#if defined (SYS_uname)
  syscall_table[SYS_uname] = "uname";
#endif
#if defined (SYS_unblock)
  syscall_table[SYS_unblock] = "unblock";
#endif
#if defined (SYS_unlink)
  syscall_table[SYS_unlink] = "unlink";
#endif
#if defined (SYS_unmount)
  syscall_table[SYS_unmount] = "unmount";
#endif
#if defined (SYS_usleep_thread)
  syscall_table[SYS_usleep_thread] = "usleep_thread";
#endif
#if defined (SYS_uswitch)
  syscall_table[SYS_uswitch] = "uswitch";
#endif
#if defined (SYS_utc_adjtime)
  syscall_table[SYS_utc_adjtime] = "utc_adjtime";
#endif
#if defined (SYS_utc_gettime)
  syscall_table[SYS_utc_gettime] = "utc_gettime";
#endif
#if defined (SYS_utime)
  syscall_table[SYS_utime] = "utime";
#endif
#if defined (SYS_utimes)
  syscall_table[SYS_utimes] = "utimes";
#endif
#if defined (SYS_utssys)
  syscall_table[SYS_utssys] = "utssys";
#endif
#if defined (SYS_vfork)
  syscall_table[SYS_vfork] = "vfork";
#endif
#if defined (SYS_vhangup)
  syscall_table[SYS_vhangup] = "vhangup";
#endif
#if defined (SYS_vtrace)
  syscall_table[SYS_vtrace] = "vtrace";
#endif
#if defined (SYS_wait)
  syscall_table[SYS_wait] = "wait";
#endif
#if defined (SYS_waitid)
  syscall_table[SYS_waitid] = "waitid";
#endif
#if defined (SYS_waitsys)
  syscall_table[SYS_waitsys] = "waitsys";
#endif
#if defined (SYS_write)
  syscall_table[SYS_write] = "write";
#endif
#if defined (SYS_writev)
  syscall_table[SYS_writev] = "writev";
#endif
#if defined (SYS_xenix)
  syscall_table[SYS_xenix] = "xenix";
#endif
#if defined (SYS_xmknod)
  syscall_table[SYS_xmknod] = "xmknod";
#endif
#if defined (SYS_xstat)
  syscall_table[SYS_xstat] = "xstat";
#endif
#if defined (SYS_yield)
  syscall_table[SYS_yield] = "yield";
#endif
}

/*
 * Prettyprint a single syscall by number.
 */

void
proc_prettyfprint_syscall (FILE *file, int num, int verbose)
{
  if (syscall_table[num])
    fprintf (file, "SYS_%s ", syscall_table[num]);
  else
    fprintf (file, "<Unknown syscall %d> ", num);
}

void
proc_prettyprint_syscall (int num, int verbose)
{
  proc_prettyfprint_syscall (stdout, num, verbose);
}

/*
 * Prettyprint all of the syscalls in a sysset_t set.
 */

void
proc_prettyfprint_syscalls (FILE *file, sysset_t *sysset, int verbose)
{
  int i;

  for (i = 0; i < MAX_SYSCALLS; i++)
    if (prismember (sysset, i))
      {
	proc_prettyfprint_syscall (file, i, verbose);
      }
  fprintf (file, "\n");
}

void
proc_prettyprint_syscalls (sysset_t *sysset, int verbose)
{
  proc_prettyfprint_syscalls (stdout, sysset, verbose);
}

/* FIXME: add real-time signals */

static struct trans signal_table[] = 
{
  { 0,      "<no signal>", "no signal" }, 
#ifdef SIGHUP
  { SIGHUP, "SIGHUP", "Hangup" },
#endif
#ifdef SIGINT
  { SIGINT, "SIGINT", "Interrupt (rubout)" },
#endif
#ifdef SIGQUIT
  { SIGQUIT, "SIGQUIT", "Quit (ASCII FS)" },
#endif
#ifdef SIGILL
  { SIGILL, "SIGILL", "Illegal instruction" },	/* not reset when caught */
#endif
#ifdef SIGTRAP
  { SIGTRAP, "SIGTRAP", "Trace trap" },		/* not reset when caught */
#endif
#ifdef SIGABRT
  { SIGABRT, "SIGABRT", "used by abort()" },	/* replaces SIGIOT */
#endif
#ifdef SIGIOT
  { SIGIOT, "SIGIOT", "IOT instruction" },
#endif
#ifdef SIGEMT
  { SIGEMT, "SIGEMT", "EMT instruction" },
#endif
#ifdef SIGFPE
  { SIGFPE, "SIGFPE", "Floating point exception" },
#endif
#ifdef SIGKILL
  { SIGKILL, "SIGKILL", "Kill" },	/* Solaris: cannot be caught/ignored */
#endif
#ifdef SIGBUS
  { SIGBUS, "SIGBUS", "Bus error" },
#endif
#ifdef SIGSEGV
  { SIGSEGV, "SIGSEGV", "Segmentation violation" },
#endif
#ifdef SIGSYS
  { SIGSYS, "SIGSYS", "Bad argument to system call" },
#endif
#ifdef SIGPIPE
  { SIGPIPE, "SIGPIPE", "Write to pipe with no one to read it" },
#endif
#ifdef SIGALRM
  { SIGALRM, "SIGALRM", "Alarm clock" },
#endif
#ifdef SIGTERM
  { SIGTERM, "SIGTERM", "Software termination signal from kill" },
#endif
#ifdef SIGUSR1
  { SIGUSR1, "SIGUSR1", "User defined signal 1" },
#endif
#ifdef SIGUSR2
  { SIGUSR2, "SIGUSR2", "User defined signal 2" },
#endif
#ifdef SIGCHLD
  { SIGCHLD, "SIGCHLD", "Child status changed" },	/* Posix version */
#endif
#ifdef SIGCLD
  { SIGCLD, "SIGCLD", "Child status changed" },		/* Solaris version */
#endif
#ifdef SIGPWR
  { SIGPWR, "SIGPWR", "Power-fail restart" },
#endif
#ifdef SIGWINCH
  { SIGWINCH, "SIGWINCH", "Window size change" },
#endif
#ifdef SIGURG
  { SIGURG, "SIGURG", "Urgent socket condition" },
#endif
#ifdef SIGPOLL
  { SIGPOLL, "SIGPOLL", "Pollable event" },
#endif
#ifdef SIGIO
  { SIGIO, "SIGIO", "Socket I/O possible" },	/* alias for SIGPOLL */
#endif
#ifdef SIGSTOP
  { SIGSTOP, "SIGSTOP", "Stop, not from tty" },	/* cannot be caught or ignored */
#endif
#ifdef SIGTSTP
  { SIGTSTP, "SIGTSTP", "User stop from tty" },
#endif
#ifdef SIGCONT
  { SIGCONT, "SIGCONT", "Stopped process has been continued" },
#endif
#ifdef SIGTTIN
  { SIGTTIN, "SIGTTIN", "Background tty read attempted" },
#endif
#ifdef SIGTTOU
  { SIGTTOU, "SIGTTOU", "Background tty write attempted" },
#endif
#ifdef SIGVTALRM
  { SIGVTALRM, "SIGVTALRM", "Virtual timer expired" },
#endif
#ifdef SIGPROF
  { SIGPROF, "SIGPROF", "Profiling timer expired" },
#endif
#ifdef SIGXCPU
  { SIGXCPU, "SIGXCPU", "Exceeded CPU limit" },
#endif
#ifdef SIGXFSZ
  { SIGXFSZ, "SIGXFSZ", "Exceeded file size limit" },
#endif
#ifdef SIGWAITING
  { SIGWAITING, "SIGWAITING", "Process's LWPs are blocked" },
#endif
#ifdef SIGLWP
  { SIGLWP, "SIGLWP", "Used by thread library" },
#endif
#ifdef SIGFREEZE
  { SIGFREEZE, "SIGFREEZE", "Used by CPR" },
#endif
#ifdef SIGTHAW
  { SIGTHAW, "SIGTHAW", "Used by CPR" },
#endif
#ifdef SIGCANCEL
  { SIGCANCEL, "SIGCANCEL", "Used by libthread" },
#endif
#ifdef SIGLOST
  { SIGLOST, "SIGLOST", "Resource lost" },
#endif
#ifdef SIG32
  { SIG32, "SIG32", "Reserved for kernel usage (Irix)" },
#endif
#ifdef SIGPTINTR
  { SIGPTINTR, "SIGPTINTR", "Posix 1003.1b" },
#endif
#ifdef SIGTRESCHED
  { SIGTRESCHED, "SIGTRESCHED", "Posix 1003.1b" },
#endif
#ifdef SIGINFO
  { SIGINFO, "SIGINFO", "Information request" },
#endif
#ifdef SIGRESV
  { SIGRESV, "SIGRESV", "Reserved by Digital for future use" },
#endif
#ifdef SIGAIO
  { SIGAIO, "SIGAIO", "Asynchronous I/O signal" },
#endif
};

/*
 * Prettyprint a single signal by number.
 * Accepts a signal number and finds it in the signal table, 
 * then pretty-prints it. 
 */

void
proc_prettyfprint_signal (FILE *file, int signo, int verbose)
{
  int i;

  for (i = 0; i < sizeof (signal_table) / sizeof (signal_table[0]); i++)
    if (signo == signal_table[i].value)
      {
	fprintf (file, "%s", signal_table[i].name);
	if (verbose)
	  fprintf (file, ": %s\n", signal_table[i].desc);
	else
	  fprintf (file, " ");
	return;
      }
  fprintf (file, "Unknown signal %d%c", signo, verbose ? '\n' : ' ');
}

void
proc_prettyprint_signal (int signo, int verbose)
{
  proc_prettyfprint_signal (stdout, signo, verbose);
}

/*
 * Prettyprint all of the signals in a sigset_t set.
 *
 * This function loops over all signal numbers from 0 to NSIG, 
 * uses them as indexes for prismember, and prints them pretty.
 * 
 * It does not loop over the signal table, as is done with the
 * fault table, because the signal table may contain aliases.
 * If it did, both aliases would be printed.
 */

void
proc_prettyfprint_signalset (FILE *file, sigset_t *sigset, int verbose)
{
  int i;

  for (i = 0; i < NSIG; i++)
    if (prismember (sigset, i))
      proc_prettyfprint_signal (file, i, verbose);

  if (!verbose)
    fprintf (file, "\n");
}

void
proc_prettyprint_signalset (sigset_t *sigset, int verbose)
{
  proc_prettyfprint_signalset (stdout, sigset, verbose);
}

/*  Hardware fault translation table. */

static struct trans fault_table[] =
{
#if defined (FLTILL)
  { FLTILL, "FLTILL", "Illegal instruction" },
#endif
#if defined (FLTPRIV)
  { FLTPRIV, "FLTPRIV", "Privileged instruction" },
#endif
#if defined (FLTBPT)
  { FLTBPT, "FLTBPT", "Breakpoint trap" },
#endif
#if defined (FLTTRACE)
  { FLTTRACE, "FLTTRACE", "Trace trap" },
#endif
#if defined (FLTACCESS)
  { FLTACCESS, "FLTACCESS", "Memory access fault" },
#endif
#if defined (FLTBOUNDS)
  { FLTBOUNDS, "FLTBOUNDS", "Memory bounds violation" },
#endif
#if defined (FLTIOVF)
  { FLTIOVF, "FLTIOVF", "Integer overflow" },
#endif
#if defined (FLTIZDIV)
  { FLTIZDIV, "FLTIZDIV", "Integer zero divide" },
#endif
#if defined (FLTFPE)
  { FLTFPE, "FLTFPE", "Floating-point exception" },
#endif
#if defined (FLTSTACK)
  { FLTSTACK, "FLTSTACK", "Unrecoverable stack fault" },
#endif
#if defined (FLTPAGE)
  { FLTPAGE, "FLTPAGE", "Recoverable page fault" },
#endif
#if defined (FLTPCINVAL)
  { FLTPCINVAL, "FLTPCINVAL", "Invalid PC exception" },
#endif
#if defined (FLTWATCH)
  { FLTWATCH, "FLTWATCH", "User watchpoint" },
#endif
#if defined (FLTKWATCH)
  { FLTKWATCH, "FLTKWATCH", "Kernel watchpoint" },
#endif
#if defined (FLTSCWATCH)
  { FLTSCWATCH, "FLTSCWATCH", "Hit a store conditional on a watched page" },
#endif
};

/*
 * Work horse.  Accepts an index into the fault table, prints it pretty. 
 */

static void
prettyfprint_faulttable_entry (FILE *file, int i, int verbose)
{
  fprintf (file, "%s", fault_table[i].name);
  if (verbose)
    fprintf (file, ": %s\n", fault_table[i].desc);
  else
    fprintf (file, " ");
}

/* 
 * Prettyprint a hardware fault by number.
 */

void
proc_prettyfprint_fault (FILE *file, int faultno, int verbose)
{
  int i;

  for (i = 0; i < sizeof (fault_table) / sizeof (fault_table[0]); i++)
    if (faultno == fault_table[i].value)
      {
	prettyfprint_faulttable_entry (file, i, verbose);
	return;
      }

  fprintf (file, "Unknown hardware fault %d%c", 
	   faultno, verbose ? '\n' : ' ');
}

void
proc_prettyprint_fault (int faultno, int verbose)
{
  proc_prettyfprint_fault (stdout, faultno, verbose);
}

/*
 * Prettyprint all the faults in a fltset_t set.
 *
 * This function loops thru the fault table, 
 * using the value field as the index to prismember.
 * The fault table had better not contain aliases, 
 * for if it does they will both be printed.
 */

void
proc_prettyfprint_faultset (FILE *file, fltset_t *fltset, int verbose)
{
  int i;

  for (i = 0; i < sizeof (fault_table) / sizeof (fault_table[0]); i++)
    if (prismember (fltset, fault_table[i].value))
      prettyfprint_faulttable_entry (file, i, verbose);

  if (!verbose)
    fprintf (file, "\n");
}

void
proc_prettyprint_faultset (fltset_t *fltset, int verbose)
{
  proc_prettyfprint_faultset (stdout, fltset, verbose);
}

/*
 * Todo: actions, holds...
 */

void
proc_prettyprint_actionset (struct sigaction *actions, int verbose)
{
}

void
_initialize_proc_events (void)
{
  init_syscall_table ();
}
