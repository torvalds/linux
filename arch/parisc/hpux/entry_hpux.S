/*    syscall table for HPUX specific syscalls
 *
 *    Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *    Copyright (C) 1999 Matthew Wilcox <willy at debian . org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/unistd.h>

	.text

#define ENTRY_NAME(_name_) .word _name_

	.align 4
	.export hpux_call_table
	.import hpux_unimplemented_wrapper
hpux_call_table:
	ENTRY_NAME(sys_ni_syscall)	/* 0 */
	ENTRY_NAME(sys_exit)
	ENTRY_NAME(hpux_fork_wrapper)
	ENTRY_NAME(sys_read)
	ENTRY_NAME(sys_write)
	ENTRY_NAME(sys_open)	/* 5 */
	ENTRY_NAME(sys_close)
	ENTRY_NAME(hpux_wait)
	ENTRY_NAME(sys_creat)
	ENTRY_NAME(sys_link)
	ENTRY_NAME(sys_unlink)	/* 10 */
	ENTRY_NAME(hpux_execv_wrapper)
	ENTRY_NAME(sys_chdir)
	ENTRY_NAME(sys_time)
	ENTRY_NAME(sys_mknod)
	ENTRY_NAME(sys_chmod)	/* 15 */
	ENTRY_NAME(sys_chown)
	ENTRY_NAME(hpux_brk)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_lseek)
	ENTRY_NAME(sys_getpid)	/* 20 */
	ENTRY_NAME(hpux_mount)
	ENTRY_NAME(sys_oldumount)
	ENTRY_NAME(sys_setuid)
	ENTRY_NAME(sys_getuid)
	ENTRY_NAME(sys_stime)	/* 25 */
	ENTRY_NAME(hpux_ptrace)
	ENTRY_NAME(sys_alarm)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_pause)
	ENTRY_NAME(sys_utime)	/* 30 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_access)
	ENTRY_NAME(hpux_nice)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 35 */
	ENTRY_NAME(sys_sync)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_newstat)
	ENTRY_NAME(hpux_setpgrp3)
	ENTRY_NAME(sys_newlstat)	/* 40 */
	ENTRY_NAME(sys_dup)
	ENTRY_NAME(hpux_pipe_wrapper)
	ENTRY_NAME(sys_times)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 45 */
	ENTRY_NAME(sys_setgid)
	ENTRY_NAME(sys_getgid)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 50 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_ioctl)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 55 */
	ENTRY_NAME(sys_symlink)
	ENTRY_NAME(hpux_utssys)
	ENTRY_NAME(sys_readlink)
	ENTRY_NAME(hpux_execve_wrapper)
	ENTRY_NAME(sys_umask)	/* 60 */
	ENTRY_NAME(sys_chroot)
	ENTRY_NAME(sys_fcntl)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 65 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_sbrk)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 70 */
	ENTRY_NAME(sys_mmap)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 75 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 80 */
	ENTRY_NAME(sys_getpgid)
	ENTRY_NAME(sys_setpgid)
	ENTRY_NAME(sys_setitimer)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 85 */
	ENTRY_NAME(sys_getitimer)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_dup2)		/* 90 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_newfstat)
	ENTRY_NAME(sys_select)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 95 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 100 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 105 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 110 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 115 */
	ENTRY_NAME(sys_gettimeofday)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 120 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_fchown)
	ENTRY_NAME(sys_fchmod)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 125 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_rename)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 130 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_sysconf)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 135 */
	ENTRY_NAME(sys_mkdir)
	ENTRY_NAME(sys_rmdir)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 140 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_getrlimit)
	ENTRY_NAME(sys_setrlimit)      /* 145 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 150 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_lockf)      /* 155 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 160 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 165 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 170 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 175 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 180 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_sigprocmask)      /* 185 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 190 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_getdomainname)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 195 */
	ENTRY_NAME(hpux_statfs)
	ENTRY_NAME(hpux_fstatfs)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_waitpid)	/* 200 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 205 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 210 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 215 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 220 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 225 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 230 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 235 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 240 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 245 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 250 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 255 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 260 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 265 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 270 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_fchdir)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_accept)		/* 275 */
	ENTRY_NAME(sys_bind)
	ENTRY_NAME(sys_connect)
	ENTRY_NAME(sys_getpeername)
	ENTRY_NAME(sys_getsockname)
	ENTRY_NAME(sys_getsockopt)	/* 280 */
	ENTRY_NAME(sys_listen)
	ENTRY_NAME(sys_recv)
	ENTRY_NAME(sys_recvfrom)
	ENTRY_NAME(sys_recvmsg)
	ENTRY_NAME(sys_send)		/* 285 */
	ENTRY_NAME(sys_sendmsg)
	ENTRY_NAME(sys_sendto)
	ENTRY_NAME(sys_setsockopt)
	ENTRY_NAME(sys_shutdown)
	ENTRY_NAME(sys_socket)		/* 290 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 295 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 300 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 305 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 310 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 315 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 320 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 325 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 330 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_lchown)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_sysfs)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 335 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 340 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 345 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 350 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(sys_nanosleep)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 355 */
	ENTRY_NAME(hpux_getdents)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 360 */
	ENTRY_NAME(hpux_fstat64)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 365 */
	ENTRY_NAME(hpux_lstat64)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_stat64)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 370 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 375 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 380 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_setpgrp)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 385 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 390 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 395 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 400 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 405 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 410 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 415 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 420 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 425 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 430 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 435 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 440 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 445 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 450 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 455 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 460 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 465 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 470 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 475 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 480 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 485 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 490 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 495 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 500 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 505 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)      /* 510 */
	ENTRY_NAME(hpux_unimplemented_wrapper)
	ENTRY_NAME(hpux_unimplemented_wrapper)
.end

