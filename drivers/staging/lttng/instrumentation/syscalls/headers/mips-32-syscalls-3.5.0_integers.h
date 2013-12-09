/* THIS FILE IS AUTO-GENERATED. DO NOT EDIT */
#ifndef CREATE_SYSCALL_TABLE

#if !defined(_TRACE_SYSCALLS_INTEGERS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYSCALLS_INTEGERS_H

#include <linux/tracepoint.h>
#include <linux/syscalls.h>
#include "mips-32-syscalls-3.5.0_integers_override.h"
#include "syscalls_integers_override.h"

SC_DECLARE_EVENT_CLASS_NOARGS(syscalls_noargs,
	TP_STRUCT__entry(),
	TP_fast_assign(),
	TP_printk()
)
#ifndef OVERRIDE_32_sys_getpid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_getpid)
#endif
#ifndef OVERRIDE_32_sys_getuid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_getuid)
#endif
#ifndef OVERRIDE_32_sys_pause
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_pause)
#endif
#ifndef OVERRIDE_32_sys_sync
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_sync)
#endif
#ifndef OVERRIDE_32_sys_getgid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_getgid)
#endif
#ifndef OVERRIDE_32_sys_geteuid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_geteuid)
#endif
#ifndef OVERRIDE_32_sys_getegid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_getegid)
#endif
#ifndef OVERRIDE_32_sys_getppid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_getppid)
#endif
#ifndef OVERRIDE_32_sys_getpgrp
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_getpgrp)
#endif
#ifndef OVERRIDE_32_sys_setsid
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_setsid)
#endif
#ifndef OVERRIDE_32_sys_sgetmask
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_sgetmask)
#endif
#ifndef OVERRIDE_32_sys_vhangup
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_vhangup)
#endif
#ifndef OVERRIDE_32_sys_munlockall
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_munlockall)
#endif
#ifndef OVERRIDE_32_sys_sched_yield
SC_DEFINE_EVENT_NOARGS(syscalls_noargs, sys_sched_yield)
#endif
#ifndef OVERRIDE_32_sys_exit
SC_TRACE_EVENT(sys_exit,
	TP_PROTO(int error_code),
	TP_ARGS(error_code),
	TP_STRUCT__entry(__field(int, error_code)),
	TP_fast_assign(tp_assign(error_code, error_code)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_close
SC_TRACE_EVENT(sys_close,
	TP_PROTO(unsigned int fd),
	TP_ARGS(fd),
	TP_STRUCT__entry(__field(unsigned int, fd)),
	TP_fast_assign(tp_assign(fd, fd)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setuid
SC_TRACE_EVENT(sys_setuid,
	TP_PROTO(uid_t uid),
	TP_ARGS(uid),
	TP_STRUCT__entry(__field(uid_t, uid)),
	TP_fast_assign(tp_assign(uid, uid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_alarm
SC_TRACE_EVENT(sys_alarm,
	TP_PROTO(unsigned int seconds),
	TP_ARGS(seconds),
	TP_STRUCT__entry(__field(unsigned int, seconds)),
	TP_fast_assign(tp_assign(seconds, seconds)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_nice
SC_TRACE_EVENT(sys_nice,
	TP_PROTO(int increment),
	TP_ARGS(increment),
	TP_STRUCT__entry(__field(int, increment)),
	TP_fast_assign(tp_assign(increment, increment)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_dup
SC_TRACE_EVENT(sys_dup,
	TP_PROTO(unsigned int fildes),
	TP_ARGS(fildes),
	TP_STRUCT__entry(__field(unsigned int, fildes)),
	TP_fast_assign(tp_assign(fildes, fildes)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_brk
SC_TRACE_EVENT(sys_brk,
	TP_PROTO(unsigned long brk),
	TP_ARGS(brk),
	TP_STRUCT__entry(__field(unsigned long, brk)),
	TP_fast_assign(tp_assign(brk, brk)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setgid
SC_TRACE_EVENT(sys_setgid,
	TP_PROTO(gid_t gid),
	TP_ARGS(gid),
	TP_STRUCT__entry(__field(gid_t, gid)),
	TP_fast_assign(tp_assign(gid, gid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_umask
SC_TRACE_EVENT(sys_umask,
	TP_PROTO(int mask),
	TP_ARGS(mask),
	TP_STRUCT__entry(__field(int, mask)),
	TP_fast_assign(tp_assign(mask, mask)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_ssetmask
SC_TRACE_EVENT(sys_ssetmask,
	TP_PROTO(int newmask),
	TP_ARGS(newmask),
	TP_STRUCT__entry(__field(int, newmask)),
	TP_fast_assign(tp_assign(newmask, newmask)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fsync
SC_TRACE_EVENT(sys_fsync,
	TP_PROTO(unsigned int fd),
	TP_ARGS(fd),
	TP_STRUCT__entry(__field(unsigned int, fd)),
	TP_fast_assign(tp_assign(fd, fd)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getpgid
SC_TRACE_EVENT(sys_getpgid,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid),
	TP_STRUCT__entry(__field(pid_t, pid)),
	TP_fast_assign(tp_assign(pid, pid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fchdir
SC_TRACE_EVENT(sys_fchdir,
	TP_PROTO(unsigned int fd),
	TP_ARGS(fd),
	TP_STRUCT__entry(__field(unsigned int, fd)),
	TP_fast_assign(tp_assign(fd, fd)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_personality
SC_TRACE_EVENT(sys_personality,
	TP_PROTO(unsigned int personality),
	TP_ARGS(personality),
	TP_STRUCT__entry(__field(unsigned int, personality)),
	TP_fast_assign(tp_assign(personality, personality)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setfsuid
SC_TRACE_EVENT(sys_setfsuid,
	TP_PROTO(uid_t uid),
	TP_ARGS(uid),
	TP_STRUCT__entry(__field(uid_t, uid)),
	TP_fast_assign(tp_assign(uid, uid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setfsgid
SC_TRACE_EVENT(sys_setfsgid,
	TP_PROTO(gid_t gid),
	TP_ARGS(gid),
	TP_STRUCT__entry(__field(gid_t, gid)),
	TP_fast_assign(tp_assign(gid, gid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getsid
SC_TRACE_EVENT(sys_getsid,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid),
	TP_STRUCT__entry(__field(pid_t, pid)),
	TP_fast_assign(tp_assign(pid, pid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fdatasync
SC_TRACE_EVENT(sys_fdatasync,
	TP_PROTO(unsigned int fd),
	TP_ARGS(fd),
	TP_STRUCT__entry(__field(unsigned int, fd)),
	TP_fast_assign(tp_assign(fd, fd)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mlockall
SC_TRACE_EVENT(sys_mlockall,
	TP_PROTO(int flags),
	TP_ARGS(flags),
	TP_STRUCT__entry(__field(int, flags)),
	TP_fast_assign(tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_getscheduler
SC_TRACE_EVENT(sys_sched_getscheduler,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid),
	TP_STRUCT__entry(__field(pid_t, pid)),
	TP_fast_assign(tp_assign(pid, pid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_get_priority_max
SC_TRACE_EVENT(sys_sched_get_priority_max,
	TP_PROTO(int policy),
	TP_ARGS(policy),
	TP_STRUCT__entry(__field(int, policy)),
	TP_fast_assign(tp_assign(policy, policy)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_get_priority_min
SC_TRACE_EVENT(sys_sched_get_priority_min,
	TP_PROTO(int policy),
	TP_ARGS(policy),
	TP_STRUCT__entry(__field(int, policy)),
	TP_fast_assign(tp_assign(policy, policy)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_kill
SC_TRACE_EVENT(sys_kill,
	TP_PROTO(pid_t pid, int sig),
	TP_ARGS(pid, sig),
	TP_STRUCT__entry(__field(pid_t, pid) __field(int, sig)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(sig, sig)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setpgid
SC_TRACE_EVENT(sys_setpgid,
	TP_PROTO(pid_t pid, pid_t pgid),
	TP_ARGS(pid, pgid),
	TP_STRUCT__entry(__field(pid_t, pid) __field(pid_t, pgid)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(pgid, pgid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_dup2
SC_TRACE_EVENT(sys_dup2,
	TP_PROTO(unsigned int oldfd, unsigned int newfd),
	TP_ARGS(oldfd, newfd),
	TP_STRUCT__entry(__field(unsigned int, oldfd) __field(unsigned int, newfd)),
	TP_fast_assign(tp_assign(oldfd, oldfd) tp_assign(newfd, newfd)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setreuid
SC_TRACE_EVENT(sys_setreuid,
	TP_PROTO(uid_t ruid, uid_t euid),
	TP_ARGS(ruid, euid),
	TP_STRUCT__entry(__field(uid_t, ruid) __field(uid_t, euid)),
	TP_fast_assign(tp_assign(ruid, ruid) tp_assign(euid, euid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setregid
SC_TRACE_EVENT(sys_setregid,
	TP_PROTO(gid_t rgid, gid_t egid),
	TP_ARGS(rgid, egid),
	TP_STRUCT__entry(__field(gid_t, rgid) __field(gid_t, egid)),
	TP_fast_assign(tp_assign(rgid, rgid) tp_assign(egid, egid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_munmap
SC_TRACE_EVENT(sys_munmap,
	TP_PROTO(unsigned long addr, size_t len),
	TP_ARGS(addr, len),
	TP_STRUCT__entry(__field_hex(unsigned long, addr) __field(size_t, len)),
	TP_fast_assign(tp_assign(addr, addr) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_ftruncate
SC_TRACE_EVENT(sys_ftruncate,
	TP_PROTO(unsigned int fd, unsigned long length),
	TP_ARGS(fd, length),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(unsigned long, length)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(length, length)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fchmod
SC_TRACE_EVENT(sys_fchmod,
	TP_PROTO(unsigned int fd, umode_t mode),
	TP_ARGS(fd, mode),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(umode_t, mode)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getpriority
SC_TRACE_EVENT(sys_getpriority,
	TP_PROTO(int which, int who),
	TP_ARGS(which, who),
	TP_STRUCT__entry(__field(int, which) __field(int, who)),
	TP_fast_assign(tp_assign(which, which) tp_assign(who, who)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_bdflush
SC_TRACE_EVENT(sys_bdflush,
	TP_PROTO(int func, long data),
	TP_ARGS(func, data),
	TP_STRUCT__entry(__field(int, func) __field(long, data)),
	TP_fast_assign(tp_assign(func, func) tp_assign(data, data)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_flock
SC_TRACE_EVENT(sys_flock,
	TP_PROTO(unsigned int fd, unsigned int cmd),
	TP_ARGS(fd, cmd),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(unsigned int, cmd)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(cmd, cmd)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mlock
SC_TRACE_EVENT(sys_mlock,
	TP_PROTO(unsigned long start, size_t len),
	TP_ARGS(start, len),
	TP_STRUCT__entry(__field(unsigned long, start) __field(size_t, len)),
	TP_fast_assign(tp_assign(start, start) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_munlock
SC_TRACE_EVENT(sys_munlock,
	TP_PROTO(unsigned long start, size_t len),
	TP_ARGS(start, len),
	TP_STRUCT__entry(__field(unsigned long, start) __field(size_t, len)),
	TP_fast_assign(tp_assign(start, start) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_lseek
SC_TRACE_EVENT(sys_lseek,
	TP_PROTO(unsigned int fd, off_t offset, unsigned int origin),
	TP_ARGS(fd, offset, origin),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(off_t, offset) __field(unsigned int, origin)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(offset, offset) tp_assign(origin, origin)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_ioctl
SC_TRACE_EVENT(sys_ioctl,
	TP_PROTO(unsigned int fd, unsigned int cmd, unsigned long arg),
	TP_ARGS(fd, cmd, arg),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(unsigned int, cmd) __field(unsigned long, arg)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(cmd, cmd) tp_assign(arg, arg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fcntl
SC_TRACE_EVENT(sys_fcntl,
	TP_PROTO(unsigned int fd, unsigned int cmd, unsigned long arg),
	TP_ARGS(fd, cmd, arg),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(unsigned int, cmd) __field(unsigned long, arg)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(cmd, cmd) tp_assign(arg, arg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fchown
SC_TRACE_EVENT(sys_fchown,
	TP_PROTO(unsigned int fd, uid_t user, gid_t group),
	TP_ARGS(fd, user, group),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(uid_t, user) __field(gid_t, group)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(user, user) tp_assign(group, group)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setpriority
SC_TRACE_EVENT(sys_setpriority,
	TP_PROTO(int which, int who, int niceval),
	TP_ARGS(which, who, niceval),
	TP_STRUCT__entry(__field(int, which) __field(int, who) __field(int, niceval)),
	TP_fast_assign(tp_assign(which, which) tp_assign(who, who) tp_assign(niceval, niceval)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mprotect
SC_TRACE_EVENT(sys_mprotect,
	TP_PROTO(unsigned long start, size_t len, unsigned long prot),
	TP_ARGS(start, len, prot),
	TP_STRUCT__entry(__field(unsigned long, start) __field(size_t, len) __field(unsigned long, prot)),
	TP_fast_assign(tp_assign(start, start) tp_assign(len, len) tp_assign(prot, prot)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sysfs
SC_TRACE_EVENT(sys_sysfs,
	TP_PROTO(int option, unsigned long arg1, unsigned long arg2),
	TP_ARGS(option, arg1, arg2),
	TP_STRUCT__entry(__field(int, option) __field(unsigned long, arg1) __field(unsigned long, arg2)),
	TP_fast_assign(tp_assign(option, option) tp_assign(arg1, arg1) tp_assign(arg2, arg2)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_msync
SC_TRACE_EVENT(sys_msync,
	TP_PROTO(unsigned long start, size_t len, int flags),
	TP_ARGS(start, len, flags),
	TP_STRUCT__entry(__field(unsigned long, start) __field(size_t, len) __field(int, flags)),
	TP_fast_assign(tp_assign(start, start) tp_assign(len, len) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_cacheflush
SC_TRACE_EVENT(sys_cacheflush,
	TP_PROTO(unsigned long addr, unsigned long bytes, unsigned int cache),
	TP_ARGS(addr, bytes, cache),
	TP_STRUCT__entry(__field_hex(unsigned long, addr) __field(unsigned long, bytes) __field(unsigned int, cache)),
	TP_fast_assign(tp_assign(addr, addr) tp_assign(bytes, bytes) tp_assign(cache, cache)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_ptrace
SC_TRACE_EVENT(sys_ptrace,
	TP_PROTO(long request, long pid, unsigned long addr, unsigned long data),
	TP_ARGS(request, pid, addr, data),
	TP_STRUCT__entry(__field(long, request) __field(long, pid) __field_hex(unsigned long, addr) __field(unsigned long, data)),
	TP_fast_assign(tp_assign(request, request) tp_assign(pid, pid) tp_assign(addr, addr) tp_assign(data, data)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mremap
SC_TRACE_EVENT(sys_mremap,
	TP_PROTO(unsigned long addr, unsigned long old_len, unsigned long new_len, unsigned long flags, unsigned long new_addr),
	TP_ARGS(addr, old_len, new_len, flags, new_addr),
	TP_STRUCT__entry(__field_hex(unsigned long, addr) __field(unsigned long, old_len) __field(unsigned long, new_len) __field(unsigned long, flags) __field_hex(unsigned long, new_addr)),
	TP_fast_assign(tp_assign(addr, addr) tp_assign(old_len, old_len) tp_assign(new_len, new_len) tp_assign(flags, flags) tp_assign(new_addr, new_addr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mips_mmap
SC_TRACE_EVENT(sys_mips_mmap,
	TP_PROTO(unsigned long addr, unsigned long len, unsigned long prot, unsigned long flags, unsigned long fd, off_t offset),
	TP_ARGS(addr, len, prot, flags, fd, offset),
	TP_STRUCT__entry(__field_hex(unsigned long, addr) __field(unsigned long, len) __field(unsigned long, prot) __field(unsigned long, flags) __field(unsigned long, fd) __field(off_t, offset)),
	TP_fast_assign(tp_assign(addr, addr) tp_assign(len, len) tp_assign(prot, prot) tp_assign(flags, flags) tp_assign(fd, fd) tp_assign(offset, offset)),
	TP_printk()
)
#endif

#endif /*  _TRACE_SYSCALLS_INTEGERS_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

#else /* CREATE_SYSCALL_TABLE */

#include "mips-32-syscalls-3.5.0_integers_override.h"
#include "syscalls_integers_override.h"

#ifndef OVERRIDE_TABLE_32_sys_getpid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_getpid, 4041, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getuid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_getuid, 4049, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_pause
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_pause, 4059, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sync
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_sync, 4073, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getgid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_getgid, 4095, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_geteuid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_geteuid, 4099, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getegid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_getegid, 4101, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getppid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_getppid, 4129, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getpgrp
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_getpgrp, 4131, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setsid
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_setsid, 4133, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sgetmask
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_sgetmask, 4137, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_vhangup
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_vhangup, 4223, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_munlockall
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_munlockall, 4315, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_yield
TRACE_SYSCALL_TABLE(syscalls_noargs, sys_sched_yield, 4325, 0)
#endif
#ifndef OVERRIDE_TABLE_32_sys_exit
TRACE_SYSCALL_TABLE(sys_exit, sys_exit, 4003, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_close
TRACE_SYSCALL_TABLE(sys_close, sys_close, 4013, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_lseek
TRACE_SYSCALL_TABLE(sys_lseek, sys_lseek, 4039, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setuid
TRACE_SYSCALL_TABLE(sys_setuid, sys_setuid, 4047, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_ptrace
TRACE_SYSCALL_TABLE(sys_ptrace, sys_ptrace, 4053, 4)
#endif
#ifndef OVERRIDE_TABLE_32_sys_alarm
TRACE_SYSCALL_TABLE(sys_alarm, sys_alarm, 4055, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_nice
TRACE_SYSCALL_TABLE(sys_nice, sys_nice, 4069, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_kill
TRACE_SYSCALL_TABLE(sys_kill, sys_kill, 4075, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_dup
TRACE_SYSCALL_TABLE(sys_dup, sys_dup, 4083, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_brk
TRACE_SYSCALL_TABLE(sys_brk, sys_brk, 4091, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setgid
TRACE_SYSCALL_TABLE(sys_setgid, sys_setgid, 4093, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_ioctl
TRACE_SYSCALL_TABLE(sys_ioctl, sys_ioctl, 4109, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fcntl
TRACE_SYSCALL_TABLE(sys_fcntl, sys_fcntl, 4111, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setpgid
TRACE_SYSCALL_TABLE(sys_setpgid, sys_setpgid, 4115, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_umask
TRACE_SYSCALL_TABLE(sys_umask, sys_umask, 4121, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_dup2
TRACE_SYSCALL_TABLE(sys_dup2, sys_dup2, 4127, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_ssetmask
TRACE_SYSCALL_TABLE(sys_ssetmask, sys_ssetmask, 4139, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setreuid
TRACE_SYSCALL_TABLE(sys_setreuid, sys_setreuid, 4141, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setregid
TRACE_SYSCALL_TABLE(sys_setregid, sys_setregid, 4143, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mips_mmap
TRACE_SYSCALL_TABLE(sys_mips_mmap, sys_mips_mmap, 4181, 6)
#endif
#ifndef OVERRIDE_TABLE_32_sys_munmap
TRACE_SYSCALL_TABLE(sys_munmap, sys_munmap, 4183, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_ftruncate
TRACE_SYSCALL_TABLE(sys_ftruncate, sys_ftruncate, 4187, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fchmod
TRACE_SYSCALL_TABLE(sys_fchmod, sys_fchmod, 4189, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fchown
TRACE_SYSCALL_TABLE(sys_fchown, sys_fchown, 4191, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getpriority
TRACE_SYSCALL_TABLE(sys_getpriority, sys_getpriority, 4193, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setpriority
TRACE_SYSCALL_TABLE(sys_setpriority, sys_setpriority, 4195, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fsync
TRACE_SYSCALL_TABLE(sys_fsync, sys_fsync, 4237, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mprotect
TRACE_SYSCALL_TABLE(sys_mprotect, sys_mprotect, 4251, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getpgid
TRACE_SYSCALL_TABLE(sys_getpgid, sys_getpgid, 4265, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fchdir
TRACE_SYSCALL_TABLE(sys_fchdir, sys_fchdir, 4267, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_bdflush
TRACE_SYSCALL_TABLE(sys_bdflush, sys_bdflush, 4269, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sysfs
TRACE_SYSCALL_TABLE(sys_sysfs, sys_sysfs, 4271, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_personality
TRACE_SYSCALL_TABLE(sys_personality, sys_personality, 4273, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setfsuid
TRACE_SYSCALL_TABLE(sys_setfsuid, sys_setfsuid, 4277, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setfsgid
TRACE_SYSCALL_TABLE(sys_setfsgid, sys_setfsgid, 4279, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_flock
TRACE_SYSCALL_TABLE(sys_flock, sys_flock, 4287, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_msync
TRACE_SYSCALL_TABLE(sys_msync, sys_msync, 4289, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_cacheflush
TRACE_SYSCALL_TABLE(sys_cacheflush, sys_cacheflush, 4295, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getsid
TRACE_SYSCALL_TABLE(sys_getsid, sys_getsid, 4303, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fdatasync
TRACE_SYSCALL_TABLE(sys_fdatasync, sys_fdatasync, 4305, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mlock
TRACE_SYSCALL_TABLE(sys_mlock, sys_mlock, 4309, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_munlock
TRACE_SYSCALL_TABLE(sys_munlock, sys_munlock, 4311, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mlockall
TRACE_SYSCALL_TABLE(sys_mlockall, sys_mlockall, 4313, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_getscheduler
TRACE_SYSCALL_TABLE(sys_sched_getscheduler, sys_sched_getscheduler, 4323, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_get_priority_max
TRACE_SYSCALL_TABLE(sys_sched_get_priority_max, sys_sched_get_priority_max, 4327, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_get_priority_min
TRACE_SYSCALL_TABLE(sys_sched_get_priority_min, sys_sched_get_priority_min, 4329, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mremap
TRACE_SYSCALL_TABLE(sys_mremap, sys_mremap, 4335, 5)
#endif

#endif /* CREATE_SYSCALL_TABLE */
