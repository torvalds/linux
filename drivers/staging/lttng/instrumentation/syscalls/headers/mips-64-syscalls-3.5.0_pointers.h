/* THIS FILE IS AUTO-GENERATED. DO NOT EDIT */
#ifndef CREATE_SYSCALL_TABLE

#if !defined(_TRACE_SYSCALLS_POINTERS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYSCALLS_POINTERS_H

#include <linux/tracepoint.h>
#include <linux/syscalls.h>
#include "mips-64-syscalls-3.5.0_pointers_override.h"
#include "syscalls_pointers_override.h"

#ifndef OVERRIDE_64_sys_oldumount
SC_TRACE_EVENT(sys_oldumount,
	TP_PROTO(char * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__string_from_user(name, name)),
	TP_fast_assign(tp_copy_string_from_user(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_olduname
SC_TRACE_EVENT(sys_olduname,
	TP_PROTO(struct oldold_utsname * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field_hex(struct oldold_utsname *, name)),
	TP_fast_assign(tp_assign(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_uselib
SC_TRACE_EVENT(sys_uselib,
	TP_PROTO(const char * library),
	TP_ARGS(library),
	TP_STRUCT__entry(__field_hex(const char *, library)),
	TP_fast_assign(tp_assign(library, library)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_uname
SC_TRACE_EVENT(sys_uname,
	TP_PROTO(struct old_utsname * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field_hex(struct old_utsname *, name)),
	TP_fast_assign(tp_assign(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sysinfo
SC_TRACE_EVENT(sys_sysinfo,
	TP_PROTO(struct sysinfo * info),
	TP_ARGS(info),
	TP_STRUCT__entry(__field_hex(struct sysinfo *, info)),
	TP_fast_assign(tp_assign(info, info)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_times
SC_TRACE_EVENT(sys_times,
	TP_PROTO(struct tms * tbuf),
	TP_ARGS(tbuf),
	TP_STRUCT__entry(__field_hex(struct tms *, tbuf)),
	TP_fast_assign(tp_assign(tbuf, tbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sysctl
SC_TRACE_EVENT(sys_sysctl,
	TP_PROTO(struct __sysctl_args * args),
	TP_ARGS(args),
	TP_STRUCT__entry(__field_hex(struct __sysctl_args *, args)),
	TP_fast_assign(tp_assign(args, args)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_adjtimex
SC_TRACE_EVENT(sys_adjtimex,
	TP_PROTO(struct timex * txc_p),
	TP_ARGS(txc_p),
	TP_STRUCT__entry(__field_hex(struct timex *, txc_p)),
	TP_fast_assign(tp_assign(txc_p, txc_p)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_newuname
SC_TRACE_EVENT(sys_newuname,
	TP_PROTO(struct new_utsname * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field_hex(struct new_utsname *, name)),
	TP_fast_assign(tp_assign(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_shmdt
SC_TRACE_EVENT(sys_shmdt,
	TP_PROTO(char * shmaddr),
	TP_ARGS(shmaddr),
	TP_STRUCT__entry(__field_hex(char *, shmaddr)),
	TP_fast_assign(tp_assign(shmaddr, shmaddr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_chdir
SC_TRACE_EVENT(sys_chdir,
	TP_PROTO(const char * filename),
	TP_ARGS(filename),
	TP_STRUCT__entry(__string_from_user(filename, filename)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rmdir
SC_TRACE_EVENT(sys_rmdir,
	TP_PROTO(const char * pathname),
	TP_ARGS(pathname),
	TP_STRUCT__entry(__string_from_user(pathname, pathname)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_unlink
SC_TRACE_EVENT(sys_unlink,
	TP_PROTO(const char * pathname),
	TP_ARGS(pathname),
	TP_STRUCT__entry(__string_from_user(pathname, pathname)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_chroot
SC_TRACE_EVENT(sys_chroot,
	TP_PROTO(const char * filename),
	TP_ARGS(filename),
	TP_STRUCT__entry(__string_from_user(filename, filename)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_swapoff
SC_TRACE_EVENT(sys_swapoff,
	TP_PROTO(const char * specialfile),
	TP_ARGS(specialfile),
	TP_STRUCT__entry(__string_from_user(specialfile, specialfile)),
	TP_fast_assign(tp_copy_string_from_user(specialfile, specialfile)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_set_tid_address
SC_TRACE_EVENT(sys_set_tid_address,
	TP_PROTO(int * tidptr),
	TP_ARGS(tidptr),
	TP_STRUCT__entry(__field_hex(int *, tidptr)),
	TP_fast_assign(tp_assign(tidptr, tidptr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_nanosleep
SC_TRACE_EVENT(sys_nanosleep,
	TP_PROTO(struct timespec * rqtp, struct timespec * rmtp),
	TP_ARGS(rqtp, rmtp),
	TP_STRUCT__entry(__field_hex(struct timespec *, rqtp) __field_hex(struct timespec *, rmtp)),
	TP_fast_assign(tp_assign(rqtp, rqtp) tp_assign(rmtp, rmtp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getitimer
SC_TRACE_EVENT(sys_getitimer,
	TP_PROTO(int which, struct itimerval * value),
	TP_ARGS(which, value),
	TP_STRUCT__entry(__field(int, which) __field_hex(struct itimerval *, value)),
	TP_fast_assign(tp_assign(which, which) tp_assign(value, value)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_gettimeofday
SC_TRACE_EVENT(sys_gettimeofday,
	TP_PROTO(struct timeval * tv, struct timezone * tz),
	TP_ARGS(tv, tz),
	TP_STRUCT__entry(__field_hex(struct timeval *, tv) __field_hex(struct timezone *, tz)),
	TP_fast_assign(tp_assign(tv, tv) tp_assign(tz, tz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getrlimit
SC_TRACE_EVENT(sys_getrlimit,
	TP_PROTO(unsigned int resource, struct rlimit * rlim),
	TP_ARGS(resource, rlim),
	TP_STRUCT__entry(__field(unsigned int, resource) __field_hex(struct rlimit *, rlim)),
	TP_fast_assign(tp_assign(resource, resource) tp_assign(rlim, rlim)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getrusage
SC_TRACE_EVENT(sys_getrusage,
	TP_PROTO(int who, struct rusage * ru),
	TP_ARGS(who, ru),
	TP_STRUCT__entry(__field(int, who) __field_hex(struct rusage *, ru)),
	TP_fast_assign(tp_assign(who, who) tp_assign(ru, ru)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_sigpending
SC_TRACE_EVENT(sys_rt_sigpending,
	TP_PROTO(sigset_t * set, size_t sigsetsize),
	TP_ARGS(set, sigsetsize),
	TP_STRUCT__entry(__field_hex(sigset_t *, set) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(set, set) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_utime
SC_TRACE_EVENT(sys_utime,
	TP_PROTO(char * filename, struct utimbuf * times),
	TP_ARGS(filename, times),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct utimbuf *, times)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(times, times)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_ustat
SC_TRACE_EVENT(sys_ustat,
	TP_PROTO(unsigned dev, struct ustat * ubuf),
	TP_ARGS(dev, ubuf),
	TP_STRUCT__entry(__field(unsigned, dev) __field_hex(struct ustat *, ubuf)),
	TP_fast_assign(tp_assign(dev, dev) tp_assign(ubuf, ubuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_statfs
SC_TRACE_EVENT(sys_statfs,
	TP_PROTO(const char * pathname, struct statfs * buf),
	TP_ARGS(pathname, buf),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field_hex(struct statfs *, buf)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(buf, buf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_fstatfs
SC_TRACE_EVENT(sys_fstatfs,
	TP_PROTO(unsigned int fd, struct statfs * buf),
	TP_ARGS(fd, buf),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct statfs *, buf)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sched_rr_get_interval
SC_TRACE_EVENT(sys_sched_rr_get_interval,
	TP_PROTO(pid_t pid, struct timespec * interval),
	TP_ARGS(pid, interval),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct timespec *, interval)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(interval, interval)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_setrlimit
SC_TRACE_EVENT(sys_setrlimit,
	TP_PROTO(unsigned int resource, struct rlimit * rlim),
	TP_ARGS(resource, rlim),
	TP_STRUCT__entry(__field(unsigned int, resource) __field_hex(struct rlimit *, rlim)),
	TP_fast_assign(tp_assign(resource, resource) tp_assign(rlim, rlim)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_settimeofday
SC_TRACE_EVENT(sys_settimeofday,
	TP_PROTO(struct timeval * tv, struct timezone * tz),
	TP_ARGS(tv, tz),
	TP_STRUCT__entry(__field_hex(struct timeval *, tv) __field_hex(struct timezone *, tz)),
	TP_fast_assign(tp_assign(tv, tv) tp_assign(tz, tz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_io_setup
SC_TRACE_EVENT(sys_io_setup,
	TP_PROTO(unsigned nr_events, aio_context_t * ctxp),
	TP_ARGS(nr_events, ctxp),
	TP_STRUCT__entry(__field(unsigned, nr_events) __field_hex(aio_context_t *, ctxp)),
	TP_fast_assign(tp_assign(nr_events, nr_events) tp_assign(ctxp, ctxp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_timer_gettime
SC_TRACE_EVENT(sys_timer_gettime,
	TP_PROTO(timer_t timer_id, struct itimerspec * setting),
	TP_ARGS(timer_id, setting),
	TP_STRUCT__entry(__field(timer_t, timer_id) __field_hex(struct itimerspec *, setting)),
	TP_fast_assign(tp_assign(timer_id, timer_id) tp_assign(setting, setting)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_clock_settime
SC_TRACE_EVENT(sys_clock_settime,
	TP_PROTO(const clockid_t which_clock, const struct timespec * tp),
	TP_ARGS(which_clock, tp),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field_hex(const struct timespec *, tp)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(tp, tp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_clock_gettime
SC_TRACE_EVENT(sys_clock_gettime,
	TP_PROTO(const clockid_t which_clock, struct timespec * tp),
	TP_ARGS(which_clock, tp),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field_hex(struct timespec *, tp)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(tp, tp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_clock_getres
SC_TRACE_EVENT(sys_clock_getres,
	TP_PROTO(const clockid_t which_clock, struct timespec * tp),
	TP_ARGS(which_clock, tp),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field_hex(struct timespec *, tp)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(tp, tp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_utimes
SC_TRACE_EVENT(sys_utimes,
	TP_PROTO(char * filename, struct timeval * utimes),
	TP_ARGS(filename, utimes),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct timeval *, utimes)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(utimes, utimes)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_set_robust_list
SC_TRACE_EVENT(sys_set_robust_list,
	TP_PROTO(struct robust_list_head * head, size_t len),
	TP_ARGS(head, len),
	TP_STRUCT__entry(__field_hex(struct robust_list_head *, head) __field(size_t, len)),
	TP_fast_assign(tp_assign(head, head) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_timerfd_gettime
SC_TRACE_EVENT(sys_timerfd_gettime,
	TP_PROTO(int ufd, struct itimerspec * otmr),
	TP_ARGS(ufd, otmr),
	TP_STRUCT__entry(__field(int, ufd) __field_hex(struct itimerspec *, otmr)),
	TP_fast_assign(tp_assign(ufd, ufd) tp_assign(otmr, otmr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_clock_adjtime
SC_TRACE_EVENT(sys_clock_adjtime,
	TP_PROTO(const clockid_t which_clock, struct timex * utx),
	TP_ARGS(which_clock, utx),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field_hex(struct timex *, utx)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(utx, utx)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_newstat
SC_TRACE_EVENT(sys_newstat,
	TP_PROTO(const char * filename, struct stat * statbuf),
	TP_ARGS(filename, statbuf),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct stat *, statbuf)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(statbuf, statbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_newfstat
SC_TRACE_EVENT(sys_newfstat,
	TP_PROTO(unsigned int fd, struct stat * statbuf),
	TP_ARGS(fd, statbuf),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct stat *, statbuf)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(statbuf, statbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_newlstat
SC_TRACE_EVENT(sys_newlstat,
	TP_PROTO(const char * filename, struct stat * statbuf),
	TP_ARGS(filename, statbuf),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct stat *, statbuf)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(statbuf, statbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_access
SC_TRACE_EVENT(sys_access,
	TP_PROTO(const char * filename, int mode),
	TP_ARGS(filename, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(int, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_truncate
SC_TRACE_EVENT(sys_truncate,
	TP_PROTO(const char * path, long length),
	TP_ARGS(path, length),
	TP_STRUCT__entry(__string_from_user(path, path) __field(long, length)),
	TP_fast_assign(tp_copy_string_from_user(path, path) tp_assign(length, length)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getcwd
SC_TRACE_EVENT(sys_getcwd,
	TP_PROTO(char * buf, unsigned long size),
	TP_ARGS(buf, size),
	TP_STRUCT__entry(__field_hex(char *, buf) __field(unsigned long, size)),
	TP_fast_assign(tp_assign(buf, buf) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rename
SC_TRACE_EVENT(sys_rename,
	TP_PROTO(const char * oldname, const char * newname),
	TP_ARGS(oldname, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mkdir
SC_TRACE_EVENT(sys_mkdir,
	TP_PROTO(const char * pathname, umode_t mode),
	TP_ARGS(pathname, mode),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_creat
SC_TRACE_EVENT(sys_creat,
	TP_PROTO(const char * pathname, umode_t mode),
	TP_ARGS(pathname, mode),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_link
SC_TRACE_EVENT(sys_link,
	TP_PROTO(const char * oldname, const char * newname),
	TP_ARGS(oldname, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_symlink
SC_TRACE_EVENT(sys_symlink,
	TP_PROTO(const char * oldname, const char * newname),
	TP_ARGS(oldname, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_chmod
SC_TRACE_EVENT(sys_chmod,
	TP_PROTO(const char * filename, umode_t mode),
	TP_ARGS(filename, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getgroups
SC_TRACE_EVENT(sys_getgroups,
	TP_PROTO(int gidsetsize, gid_t * grouplist),
	TP_ARGS(gidsetsize, grouplist),
	TP_STRUCT__entry(__field(int, gidsetsize) __field_hex(gid_t *, grouplist)),
	TP_fast_assign(tp_assign(gidsetsize, gidsetsize) tp_assign(grouplist, grouplist)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_setgroups
SC_TRACE_EVENT(sys_setgroups,
	TP_PROTO(int gidsetsize, gid_t * grouplist),
	TP_ARGS(gidsetsize, grouplist),
	TP_STRUCT__entry(__field(int, gidsetsize) __field_hex(gid_t *, grouplist)),
	TP_fast_assign(tp_assign(gidsetsize, gidsetsize) tp_assign(grouplist, grouplist)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_rt_sigpending
SC_TRACE_EVENT(sys_32_rt_sigpending,
	TP_PROTO(compat_sigset_t * uset, unsigned int sigsetsize),
	TP_ARGS(uset, sigsetsize),
	TP_STRUCT__entry(__field_hex(compat_sigset_t *, uset) __field(unsigned int, sigsetsize)),
	TP_fast_assign(tp_assign(uset, uset) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sched_setparam
SC_TRACE_EVENT(sys_sched_setparam,
	TP_PROTO(pid_t pid, struct sched_param * param),
	TP_ARGS(pid, param),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct sched_param *, param)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(param, param)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sched_getparam
SC_TRACE_EVENT(sys_sched_getparam,
	TP_PROTO(pid_t pid, struct sched_param * param),
	TP_ARGS(pid, param),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct sched_param *, param)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(param, param)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_sched_rr_get_interval
SC_TRACE_EVENT(sys_32_sched_rr_get_interval,
	TP_PROTO(compat_pid_t pid, struct compat_timespec * interval),
	TP_ARGS(pid, interval),
	TP_STRUCT__entry(__field(compat_pid_t, pid) __field_hex(struct compat_timespec *, interval)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(interval, interval)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_pivot_root
SC_TRACE_EVENT(sys_pivot_root,
	TP_PROTO(const char * new_root, const char * put_old),
	TP_ARGS(new_root, put_old),
	TP_STRUCT__entry(__string_from_user(new_root, new_root) __string_from_user(put_old, put_old)),
	TP_fast_assign(tp_copy_string_from_user(new_root, new_root) tp_copy_string_from_user(put_old, put_old)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_umount
SC_TRACE_EVENT(sys_umount,
	TP_PROTO(char * name, int flags),
	TP_ARGS(name, flags),
	TP_STRUCT__entry(__string_from_user(name, name) __field(int, flags)),
	TP_fast_assign(tp_copy_string_from_user(name, name) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_swapon
SC_TRACE_EVENT(sys_swapon,
	TP_PROTO(const char * specialfile, int swap_flags),
	TP_ARGS(specialfile, swap_flags),
	TP_STRUCT__entry(__string_from_user(specialfile, specialfile) __field(int, swap_flags)),
	TP_fast_assign(tp_copy_string_from_user(specialfile, specialfile) tp_assign(swap_flags, swap_flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sethostname
SC_TRACE_EVENT(sys_sethostname,
	TP_PROTO(char * name, int len),
	TP_ARGS(name, len),
	TP_STRUCT__entry(__string_from_user(name, name) __field(int, len)),
	TP_fast_assign(tp_copy_string_from_user(name, name) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_setdomainname
SC_TRACE_EVENT(sys_setdomainname,
	TP_PROTO(char * name, int len),
	TP_ARGS(name, len),
	TP_STRUCT__entry(__string_from_user(name, name) __field(int, len)),
	TP_fast_assign(tp_copy_string_from_user(name, name) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_delete_module
SC_TRACE_EVENT(sys_delete_module,
	TP_PROTO(const char * name_user, unsigned int flags),
	TP_ARGS(name_user, flags),
	TP_STRUCT__entry(__string_from_user(name_user, name_user) __field(unsigned int, flags)),
	TP_fast_assign(tp_copy_string_from_user(name_user, name_user) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_removexattr
SC_TRACE_EVENT(sys_removexattr,
	TP_PROTO(const char * pathname, const char * name),
	TP_ARGS(pathname, name),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __string_from_user(name, name)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_copy_string_from_user(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_lremovexattr
SC_TRACE_EVENT(sys_lremovexattr,
	TP_PROTO(const char * pathname, const char * name),
	TP_ARGS(pathname, name),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __string_from_user(name, name)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_copy_string_from_user(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_fremovexattr
SC_TRACE_EVENT(sys_fremovexattr,
	TP_PROTO(int fd, const char * name),
	TP_ARGS(fd, name),
	TP_STRUCT__entry(__field(int, fd) __string_from_user(name, name)),
	TP_fast_assign(tp_assign(fd, fd) tp_copy_string_from_user(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_pipe2
SC_TRACE_EVENT(sys_pipe2,
	TP_PROTO(int * fildes, int flags),
	TP_ARGS(fildes, flags),
	TP_STRUCT__entry(__field_hex(int *, fildes) __field(int, flags)),
	TP_fast_assign(tp_assign(fildes, fildes) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_waitpid
SC_TRACE_EVENT(sys_waitpid,
	TP_PROTO(pid_t pid, int * stat_addr, int options),
	TP_ARGS(pid, stat_addr, options),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(int *, stat_addr) __field(int, options)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(stat_addr, stat_addr) tp_assign(options, options)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_sigaction
SC_TRACE_EVENT(sys_32_sigaction,
	TP_PROTO(long sig, const struct sigaction32 * act, struct sigaction32 * oact),
	TP_ARGS(sig, act, oact),
	TP_STRUCT__entry(__field(long, sig) __field_hex(const struct sigaction32 *, act) __field_hex(struct sigaction32 *, oact)),
	TP_fast_assign(tp_assign(sig, sig) tp_assign(act, act) tp_assign(oact, oact)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_readv
SC_TRACE_EVENT(sys_readv,
	TP_PROTO(unsigned long fd, const struct iovec * vec, unsigned long vlen),
	TP_ARGS(fd, vec, vlen),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(const struct iovec *, vec) __field(unsigned long, vlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(vec, vec) tp_assign(vlen, vlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_writev
SC_TRACE_EVENT(sys_writev,
	TP_PROTO(unsigned long fd, const struct iovec * vec, unsigned long vlen),
	TP_ARGS(fd, vec, vlen),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(const struct iovec *, vec) __field(unsigned long, vlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(vec, vec) tp_assign(vlen, vlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_shmctl
SC_TRACE_EVENT(sys_shmctl,
	TP_PROTO(int shmid, int cmd, struct shmid_ds * buf),
	TP_ARGS(shmid, cmd, buf),
	TP_STRUCT__entry(__field(int, shmid) __field(int, cmd) __field_hex(struct shmid_ds *, buf)),
	TP_fast_assign(tp_assign(shmid, shmid) tp_assign(cmd, cmd) tp_assign(buf, buf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_setitimer
SC_TRACE_EVENT(sys_setitimer,
	TP_PROTO(int which, struct itimerval * value, struct itimerval * ovalue),
	TP_ARGS(which, value, ovalue),
	TP_STRUCT__entry(__field(int, which) __field_hex(struct itimerval *, value) __field_hex(struct itimerval *, ovalue)),
	TP_fast_assign(tp_assign(which, which) tp_assign(value, value) tp_assign(ovalue, ovalue)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sendmsg
SC_TRACE_EVENT(sys_sendmsg,
	TP_PROTO(int fd, struct msghdr * msg, unsigned int flags),
	TP_ARGS(fd, msg, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct msghdr *, msg) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(msg, msg) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_recvmsg
SC_TRACE_EVENT(sys_recvmsg,
	TP_PROTO(int fd, struct msghdr * msg, unsigned int flags),
	TP_ARGS(fd, msg, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct msghdr *, msg) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(msg, msg) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_msgctl
SC_TRACE_EVENT(sys_msgctl,
	TP_PROTO(int msqid, int cmd, struct msqid_ds * buf),
	TP_ARGS(msqid, cmd, buf),
	TP_STRUCT__entry(__field(int, msqid) __field(int, cmd) __field_hex(struct msqid_ds *, buf)),
	TP_fast_assign(tp_assign(msqid, msqid) tp_assign(cmd, cmd) tp_assign(buf, buf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getdents
SC_TRACE_EVENT(sys_getdents,
	TP_PROTO(unsigned int fd, struct linux_dirent * dirent, unsigned int count),
	TP_ARGS(fd, dirent, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct linux_dirent *, dirent) __field(unsigned int, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(dirent, dirent) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_sigqueueinfo
SC_TRACE_EVENT(sys_rt_sigqueueinfo,
	TP_PROTO(pid_t pid, int sig, siginfo_t * uinfo),
	TP_ARGS(pid, sig, uinfo),
	TP_STRUCT__entry(__field(pid_t, pid) __field(int, sig) __field_hex(siginfo_t *, uinfo)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(sig, sig) tp_assign(uinfo, uinfo)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sched_setaffinity
SC_TRACE_EVENT(sys_sched_setaffinity,
	TP_PROTO(pid_t pid, unsigned int len, unsigned long * user_mask_ptr),
	TP_ARGS(pid, len, user_mask_ptr),
	TP_STRUCT__entry(__field(pid_t, pid) __field(unsigned int, len) __field_hex(unsigned long *, user_mask_ptr)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(len, len) tp_assign(user_mask_ptr, user_mask_ptr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sched_getaffinity
SC_TRACE_EVENT(sys_sched_getaffinity,
	TP_PROTO(pid_t pid, unsigned int len, unsigned long * user_mask_ptr),
	TP_ARGS(pid, len, user_mask_ptr),
	TP_STRUCT__entry(__field(pid_t, pid) __field(unsigned int, len) __field_hex(unsigned long *, user_mask_ptr)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(len, len) tp_assign(user_mask_ptr, user_mask_ptr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_io_submit
SC_TRACE_EVENT(sys_io_submit,
	TP_PROTO(aio_context_t ctx_id, long nr, struct iocb * * iocbpp),
	TP_ARGS(ctx_id, nr, iocbpp),
	TP_STRUCT__entry(__field(aio_context_t, ctx_id) __field(long, nr) __field_hex(struct iocb * *, iocbpp)),
	TP_fast_assign(tp_assign(ctx_id, ctx_id) tp_assign(nr, nr) tp_assign(iocbpp, iocbpp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_timer_create
SC_TRACE_EVENT(sys_timer_create,
	TP_PROTO(const clockid_t which_clock, struct sigevent * timer_event_spec, timer_t * created_timer_id),
	TP_ARGS(which_clock, timer_event_spec, created_timer_id),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field_hex(struct sigevent *, timer_event_spec) __field_hex(timer_t *, created_timer_id)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(timer_event_spec, timer_event_spec) tp_assign(created_timer_id, created_timer_id)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_futimesat
SC_TRACE_EVENT(sys_futimesat,
	TP_PROTO(int dfd, const char * filename, struct timeval * utimes),
	TP_ARGS(dfd, filename, utimes),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field_hex(struct timeval *, utimes)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(utimes, utimes)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_get_robust_list
SC_TRACE_EVENT(sys_get_robust_list,
	TP_PROTO(int pid, struct robust_list_head * * head_ptr, size_t * len_ptr),
	TP_ARGS(pid, head_ptr, len_ptr),
	TP_STRUCT__entry(__field(int, pid) __field_hex(struct robust_list_head * *, head_ptr) __field_hex(size_t *, len_ptr)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(head_ptr, head_ptr) tp_assign(len_ptr, len_ptr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_signalfd
SC_TRACE_EVENT(sys_signalfd,
	TP_PROTO(int ufd, sigset_t * user_mask, size_t sizemask),
	TP_ARGS(ufd, user_mask, sizemask),
	TP_STRUCT__entry(__field(int, ufd) __field_hex(sigset_t *, user_mask) __field(size_t, sizemask)),
	TP_fast_assign(tp_assign(ufd, ufd) tp_assign(user_mask, user_mask) tp_assign(sizemask, sizemask)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_read
SC_TRACE_EVENT(sys_read,
	TP_PROTO(unsigned int fd, char * buf, size_t count),
	TP_ARGS(fd, buf, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(char *, buf) __field(size_t, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_write
SC_TRACE_EVENT(sys_write,
	TP_PROTO(unsigned int fd, const char * buf, size_t count),
	TP_ARGS(fd, buf, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(const char *, buf) __field(size_t, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_open
SC_TRACE_EVENT(sys_open,
	TP_PROTO(const char * filename, int flags, umode_t mode),
	TP_ARGS(filename, flags, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(int, flags) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(flags, flags) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_poll
SC_TRACE_EVENT(sys_poll,
	TP_PROTO(struct pollfd * ufds, unsigned int nfds, int timeout_msecs),
	TP_ARGS(ufds, nfds, timeout_msecs),
	TP_STRUCT__entry(__field_hex(struct pollfd *, ufds) __field(unsigned int, nfds) __field(int, timeout_msecs)),
	TP_fast_assign(tp_assign(ufds, ufds) tp_assign(nfds, nfds) tp_assign(timeout_msecs, timeout_msecs)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mincore
SC_TRACE_EVENT(sys_mincore,
	TP_PROTO(unsigned long start, size_t len, unsigned char * vec),
	TP_ARGS(start, len, vec),
	TP_STRUCT__entry(__field(unsigned long, start) __field(size_t, len) __field_hex(unsigned char *, vec)),
	TP_fast_assign(tp_assign(start, start) tp_assign(len, len) tp_assign(vec, vec)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_shmat
SC_TRACE_EVENT(sys_shmat,
	TP_PROTO(int shmid, char * shmaddr, int shmflg),
	TP_ARGS(shmid, shmaddr, shmflg),
	TP_STRUCT__entry(__field(int, shmid) __field_hex(char *, shmaddr) __field(int, shmflg)),
	TP_fast_assign(tp_assign(shmid, shmid) tp_assign(shmaddr, shmaddr) tp_assign(shmflg, shmflg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_connect
SC_TRACE_EVENT(sys_connect,
	TP_PROTO(int fd, struct sockaddr * uservaddr, int addrlen),
	TP_ARGS(fd, uservaddr, addrlen),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, uservaddr) __field_hex(int, addrlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(uservaddr, uservaddr) tp_assign(addrlen, addrlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_accept
SC_TRACE_EVENT(sys_accept,
	TP_PROTO(int fd, struct sockaddr * upeer_sockaddr, int * upeer_addrlen),
	TP_ARGS(fd, upeer_sockaddr, upeer_addrlen),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, upeer_sockaddr) __field_hex(int *, upeer_addrlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(upeer_sockaddr, upeer_sockaddr) tp_assign(upeer_addrlen, upeer_addrlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_bind
SC_TRACE_EVENT(sys_bind,
	TP_PROTO(int fd, struct sockaddr * umyaddr, int addrlen),
	TP_ARGS(fd, umyaddr, addrlen),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, umyaddr) __field_hex(int, addrlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(umyaddr, umyaddr) tp_assign(addrlen, addrlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getsockname
SC_TRACE_EVENT(sys_getsockname,
	TP_PROTO(int fd, struct sockaddr * usockaddr, int * usockaddr_len),
	TP_ARGS(fd, usockaddr, usockaddr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, usockaddr) __field_hex(int *, usockaddr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(usockaddr, usockaddr) tp_assign(usockaddr_len, usockaddr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getpeername
SC_TRACE_EVENT(sys_getpeername,
	TP_PROTO(int fd, struct sockaddr * usockaddr, int * usockaddr_len),
	TP_ARGS(fd, usockaddr, usockaddr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, usockaddr) __field_hex(int *, usockaddr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(usockaddr, usockaddr) tp_assign(usockaddr_len, usockaddr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_semop
SC_TRACE_EVENT(sys_semop,
	TP_PROTO(int semid, struct sembuf * tsops, unsigned nsops),
	TP_ARGS(semid, tsops, nsops),
	TP_STRUCT__entry(__field(int, semid) __field_hex(struct sembuf *, tsops) __field(unsigned, nsops)),
	TP_fast_assign(tp_assign(semid, semid) tp_assign(tsops, tsops) tp_assign(nsops, nsops)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_readlink
SC_TRACE_EVENT(sys_readlink,
	TP_PROTO(const char * path, char * buf, int bufsiz),
	TP_ARGS(path, buf, bufsiz),
	TP_STRUCT__entry(__string_from_user(path, path) __field_hex(char *, buf) __field(int, bufsiz)),
	TP_fast_assign(tp_copy_string_from_user(path, path) tp_assign(buf, buf) tp_assign(bufsiz, bufsiz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_chown
SC_TRACE_EVENT(sys_chown,
	TP_PROTO(const char * filename, uid_t user, gid_t group),
	TP_ARGS(filename, user, group),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(uid_t, user) __field(gid_t, group)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(user, user) tp_assign(group, group)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_lchown
SC_TRACE_EVENT(sys_lchown,
	TP_PROTO(const char * filename, uid_t user, gid_t group),
	TP_ARGS(filename, user, group),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(uid_t, user) __field(gid_t, group)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(user, user) tp_assign(group, group)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_syslog
SC_TRACE_EVENT(sys_syslog,
	TP_PROTO(int type, char * buf, int len),
	TP_ARGS(type, buf, len),
	TP_STRUCT__entry(__field(int, type) __field_hex(char *, buf) __field(int, len)),
	TP_fast_assign(tp_assign(type, type) tp_assign(buf, buf) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getresuid
SC_TRACE_EVENT(sys_getresuid,
	TP_PROTO(uid_t * ruidp, uid_t * euidp, uid_t * suidp),
	TP_ARGS(ruidp, euidp, suidp),
	TP_STRUCT__entry(__field_hex(uid_t *, ruidp) __field_hex(uid_t *, euidp) __field_hex(uid_t *, suidp)),
	TP_fast_assign(tp_assign(ruidp, ruidp) tp_assign(euidp, euidp) tp_assign(suidp, suidp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getresgid
SC_TRACE_EVENT(sys_getresgid,
	TP_PROTO(gid_t * rgidp, gid_t * egidp, gid_t * sgidp),
	TP_ARGS(rgidp, egidp, sgidp),
	TP_STRUCT__entry(__field_hex(gid_t *, rgidp) __field_hex(gid_t *, egidp) __field_hex(gid_t *, sgidp)),
	TP_fast_assign(tp_assign(rgidp, rgidp) tp_assign(egidp, egidp) tp_assign(sgidp, sgidp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_rt_sigqueueinfo
SC_TRACE_EVENT(sys_32_rt_sigqueueinfo,
	TP_PROTO(int pid, int sig, compat_siginfo_t * uinfo),
	TP_ARGS(pid, sig, uinfo),
	TP_STRUCT__entry(__field(int, pid) __field(int, sig) __field_hex(compat_siginfo_t *, uinfo)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(sig, sig) tp_assign(uinfo, uinfo)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mknod
SC_TRACE_EVENT(sys_mknod,
	TP_PROTO(const char * filename, umode_t mode, unsigned dev),
	TP_ARGS(filename, mode, dev),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(umode_t, mode) __field(unsigned, dev)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode) tp_assign(dev, dev)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sched_setscheduler
SC_TRACE_EVENT(sys_sched_setscheduler,
	TP_PROTO(pid_t pid, int policy, struct sched_param * param),
	TP_ARGS(pid, policy, param),
	TP_STRUCT__entry(__field(pid_t, pid) __field(int, policy) __field_hex(struct sched_param *, param)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(policy, policy) tp_assign(param, param)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_init_module
SC_TRACE_EVENT(sys_init_module,
	TP_PROTO(void * umod, unsigned long len, const char * uargs),
	TP_ARGS(umod, len, uargs),
	TP_STRUCT__entry(__field_hex(void *, umod) __field(unsigned long, len) __field_hex(const char *, uargs)),
	TP_fast_assign(tp_assign(umod, umod) tp_assign(len, len) tp_assign(uargs, uargs)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_listxattr
SC_TRACE_EVENT(sys_listxattr,
	TP_PROTO(const char * pathname, char * list, size_t size),
	TP_ARGS(pathname, list, size),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field_hex(char *, list) __field(size_t, size)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(list, list) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_llistxattr
SC_TRACE_EVENT(sys_llistxattr,
	TP_PROTO(const char * pathname, char * list, size_t size),
	TP_ARGS(pathname, list, size),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field_hex(char *, list) __field(size_t, size)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(list, list) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_flistxattr
SC_TRACE_EVENT(sys_flistxattr,
	TP_PROTO(int fd, char * list, size_t size),
	TP_ARGS(fd, list, size),
	TP_STRUCT__entry(__field(int, fd) __field_hex(char *, list) __field(size_t, size)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(list, list) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_cachectl
SC_TRACE_EVENT(sys_cachectl,
	TP_PROTO(char * addr, int nbytes, int op),
	TP_ARGS(addr, nbytes, op),
	TP_STRUCT__entry(__field_hex(char *, addr) __field(int, nbytes) __field(int, op)),
	TP_fast_assign(tp_assign(addr, addr) tp_assign(nbytes, nbytes) tp_assign(op, op)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_io_cancel
SC_TRACE_EVENT(sys_io_cancel,
	TP_PROTO(aio_context_t ctx_id, struct iocb * iocb, struct io_event * result),
	TP_ARGS(ctx_id, iocb, result),
	TP_STRUCT__entry(__field(aio_context_t, ctx_id) __field_hex(struct iocb *, iocb) __field_hex(struct io_event *, result)),
	TP_fast_assign(tp_assign(ctx_id, ctx_id) tp_assign(iocb, iocb) tp_assign(result, result)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_inotify_add_watch
SC_TRACE_EVENT(sys_inotify_add_watch,
	TP_PROTO(int fd, const char * pathname, u32 mask),
	TP_ARGS(fd, pathname, mask),
	TP_STRUCT__entry(__field(int, fd) __string_from_user(pathname, pathname) __field(u32, mask)),
	TP_fast_assign(tp_assign(fd, fd) tp_copy_string_from_user(pathname, pathname) tp_assign(mask, mask)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mkdirat
SC_TRACE_EVENT(sys_mkdirat,
	TP_PROTO(int dfd, const char * pathname, umode_t mode),
	TP_ARGS(dfd, pathname, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(pathname, pathname) __field(umode_t, mode)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_unlinkat
SC_TRACE_EVENT(sys_unlinkat,
	TP_PROTO(int dfd, const char * pathname, int flag),
	TP_ARGS(dfd, pathname, flag),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(pathname, pathname) __field(int, flag)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(pathname, pathname) tp_assign(flag, flag)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_symlinkat
SC_TRACE_EVENT(sys_symlinkat,
	TP_PROTO(const char * oldname, int newdfd, const char * newname),
	TP_ARGS(oldname, newdfd, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __field(int, newdfd) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_assign(newdfd, newdfd) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_fchmodat
SC_TRACE_EVENT(sys_fchmodat,
	TP_PROTO(int dfd, const char * filename, umode_t mode),
	TP_ARGS(dfd, filename, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(umode_t, mode)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_faccessat
SC_TRACE_EVENT(sys_faccessat,
	TP_PROTO(int dfd, const char * filename, int mode),
	TP_ARGS(dfd, filename, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(int, mode)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getcpu
SC_TRACE_EVENT(sys_getcpu,
	TP_PROTO(unsigned * cpup, unsigned * nodep, struct getcpu_cache * unused),
	TP_ARGS(cpup, nodep, unused),
	TP_STRUCT__entry(__field_hex(unsigned *, cpup) __field_hex(unsigned *, nodep) __field_hex(struct getcpu_cache *, unused)),
	TP_fast_assign(tp_assign(cpup, cpup) tp_assign(nodep, nodep) tp_assign(unused, unused)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getdents64
SC_TRACE_EVENT(sys_getdents64,
	TP_PROTO(unsigned int fd, struct linux_dirent64 * dirent, unsigned int count),
	TP_ARGS(fd, dirent, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct linux_dirent64 *, dirent) __field(unsigned int, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(dirent, dirent) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_send
SC_TRACE_EVENT(sys_send,
	TP_PROTO(int fd, void * buff, size_t len, unsigned int flags),
	TP_ARGS(fd, buff, len, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(void *, buff) __field(size_t, len) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buff, buff) tp_assign(len, len) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_truncate64
SC_TRACE_EVENT(sys_32_truncate64,
	TP_PROTO(const char * path, unsigned long __dummy, unsigned long a2, unsigned long a3),
	TP_ARGS(path, __dummy, a2, a3),
	TP_STRUCT__entry(__string_from_user(path, path) __field(unsigned long, __dummy) __field(unsigned long, a2) __field(unsigned long, a3)),
	TP_fast_assign(tp_copy_string_from_user(path, path) tp_assign(__dummy, __dummy) tp_assign(a2, a2) tp_assign(a3, a3)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_sigaction
SC_TRACE_EVENT(sys_rt_sigaction,
	TP_PROTO(int sig, const struct sigaction * act, struct sigaction * oact, size_t sigsetsize),
	TP_ARGS(sig, act, oact, sigsetsize),
	TP_STRUCT__entry(__field(int, sig) __field_hex(const struct sigaction *, act) __field_hex(struct sigaction *, oact) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(sig, sig) tp_assign(act, act) tp_assign(oact, oact) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_sigprocmask
SC_TRACE_EVENT(sys_rt_sigprocmask,
	TP_PROTO(int how, sigset_t * nset, sigset_t * oset, size_t sigsetsize),
	TP_ARGS(how, nset, oset, sigsetsize),
	TP_STRUCT__entry(__field(int, how) __field_hex(sigset_t *, nset) __field_hex(sigset_t *, oset) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(how, how) tp_assign(nset, nset) tp_assign(oset, oset) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_wait4
SC_TRACE_EVENT(sys_wait4,
	TP_PROTO(pid_t upid, int * stat_addr, int options, struct rusage * ru),
	TP_ARGS(upid, stat_addr, options, ru),
	TP_STRUCT__entry(__field(pid_t, upid) __field_hex(int *, stat_addr) __field(int, options) __field_hex(struct rusage *, ru)),
	TP_fast_assign(tp_assign(upid, upid) tp_assign(stat_addr, stat_addr) tp_assign(options, options) tp_assign(ru, ru)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_msgsnd
SC_TRACE_EVENT(sys_msgsnd,
	TP_PROTO(int msqid, struct msgbuf * msgp, size_t msgsz, int msgflg),
	TP_ARGS(msqid, msgp, msgsz, msgflg),
	TP_STRUCT__entry(__field(int, msqid) __field_hex(struct msgbuf *, msgp) __field(size_t, msgsz) __field(int, msgflg)),
	TP_fast_assign(tp_assign(msqid, msqid) tp_assign(msgp, msgp) tp_assign(msgsz, msgsz) tp_assign(msgflg, msgflg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_sigtimedwait
SC_TRACE_EVENT(sys_rt_sigtimedwait,
	TP_PROTO(const sigset_t * uthese, siginfo_t * uinfo, const struct timespec * uts, size_t sigsetsize),
	TP_ARGS(uthese, uinfo, uts, sigsetsize),
	TP_STRUCT__entry(__field_hex(const sigset_t *, uthese) __field_hex(siginfo_t *, uinfo) __field_hex(const struct timespec *, uts) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(uthese, uthese) tp_assign(uinfo, uinfo) tp_assign(uts, uts) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_semtimedop
SC_TRACE_EVENT(sys_semtimedop,
	TP_PROTO(int semid, struct sembuf * tsops, unsigned nsops, const struct timespec * timeout),
	TP_ARGS(semid, tsops, nsops, timeout),
	TP_STRUCT__entry(__field(int, semid) __field_hex(struct sembuf *, tsops) __field(unsigned, nsops) __field_hex(const struct timespec *, timeout)),
	TP_fast_assign(tp_assign(semid, semid) tp_assign(tsops, tsops) tp_assign(nsops, nsops) tp_assign(timeout, timeout)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_timer_settime
SC_TRACE_EVENT(sys_timer_settime,
	TP_PROTO(timer_t timer_id, int flags, const struct itimerspec * new_setting, struct itimerspec * old_setting),
	TP_ARGS(timer_id, flags, new_setting, old_setting),
	TP_STRUCT__entry(__field(timer_t, timer_id) __field(int, flags) __field_hex(const struct itimerspec *, new_setting) __field_hex(struct itimerspec *, old_setting)),
	TP_fast_assign(tp_assign(timer_id, timer_id) tp_assign(flags, flags) tp_assign(new_setting, new_setting) tp_assign(old_setting, old_setting)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_clock_nanosleep
SC_TRACE_EVENT(sys_clock_nanosleep,
	TP_PROTO(const clockid_t which_clock, int flags, const struct timespec * rqtp, struct timespec * rmtp),
	TP_ARGS(which_clock, flags, rqtp, rmtp),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field(int, flags) __field_hex(const struct timespec *, rqtp) __field_hex(struct timespec *, rmtp)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(flags, flags) tp_assign(rqtp, rqtp) tp_assign(rmtp, rmtp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_vmsplice
SC_TRACE_EVENT(sys_vmsplice,
	TP_PROTO(int fd, const struct iovec * iov, unsigned long nr_segs, unsigned int flags),
	TP_ARGS(fd, iov, nr_segs, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(const struct iovec *, iov) __field(unsigned long, nr_segs) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(iov, iov) tp_assign(nr_segs, nr_segs) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_utimensat
SC_TRACE_EVENT(sys_utimensat,
	TP_PROTO(int dfd, const char * filename, struct timespec * utimes, int flags),
	TP_ARGS(dfd, filename, utimes, flags),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field_hex(struct timespec *, utimes) __field(int, flags)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(utimes, utimes) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_timerfd_settime
SC_TRACE_EVENT(sys_timerfd_settime,
	TP_PROTO(int ufd, int flags, const struct itimerspec * utmr, struct itimerspec * otmr),
	TP_ARGS(ufd, flags, utmr, otmr),
	TP_STRUCT__entry(__field(int, ufd) __field(int, flags) __field_hex(const struct itimerspec *, utmr) __field_hex(struct itimerspec *, otmr)),
	TP_fast_assign(tp_assign(ufd, ufd) tp_assign(flags, flags) tp_assign(utmr, utmr) tp_assign(otmr, otmr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_tgsigqueueinfo
SC_TRACE_EVENT(sys_rt_tgsigqueueinfo,
	TP_PROTO(pid_t tgid, pid_t pid, int sig, siginfo_t * uinfo),
	TP_ARGS(tgid, pid, sig, uinfo),
	TP_STRUCT__entry(__field(pid_t, tgid) __field(pid_t, pid) __field(int, sig) __field_hex(siginfo_t *, uinfo)),
	TP_fast_assign(tp_assign(tgid, tgid) tp_assign(pid, pid) tp_assign(sig, sig) tp_assign(uinfo, uinfo)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sendmmsg
SC_TRACE_EVENT(sys_sendmmsg,
	TP_PROTO(int fd, struct mmsghdr * mmsg, unsigned int vlen, unsigned int flags),
	TP_ARGS(fd, mmsg, vlen, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct mmsghdr *, mmsg) __field(unsigned int, vlen) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(mmsg, mmsg) tp_assign(vlen, vlen) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_rt_sigaction
SC_TRACE_EVENT(sys_32_rt_sigaction,
	TP_PROTO(int sig, const struct sigaction32 * act, struct sigaction32 * oact, unsigned int sigsetsize),
	TP_ARGS(sig, act, oact, sigsetsize),
	TP_STRUCT__entry(__field(int, sig) __field_hex(const struct sigaction32 *, act) __field_hex(struct sigaction32 *, oact) __field(unsigned int, sigsetsize)),
	TP_fast_assign(tp_assign(sig, sig) tp_assign(act, act) tp_assign(oact, oact) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_rt_sigprocmask
SC_TRACE_EVENT(sys_32_rt_sigprocmask,
	TP_PROTO(int how, compat_sigset_t * set, compat_sigset_t * oset, unsigned int sigsetsize),
	TP_ARGS(how, set, oset, sigsetsize),
	TP_STRUCT__entry(__field(int, how) __field_hex(compat_sigset_t *, set) __field_hex(compat_sigset_t *, oset) __field(unsigned int, sigsetsize)),
	TP_fast_assign(tp_assign(how, how) tp_assign(set, set) tp_assign(oset, oset) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_sendfile
SC_TRACE_EVENT(sys_32_sendfile,
	TP_PROTO(long out_fd, long in_fd, compat_off_t * offset, s32 count),
	TP_ARGS(out_fd, in_fd, offset, count),
	TP_STRUCT__entry(__field(long, out_fd) __field(long, in_fd) __field_hex(compat_off_t *, offset) __field(s32, count)),
	TP_fast_assign(tp_assign(out_fd, out_fd) tp_assign(in_fd, in_fd) tp_assign(offset, offset) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_socketpair
SC_TRACE_EVENT(sys_socketpair,
	TP_PROTO(int family, int type, int protocol, int * usockvec),
	TP_ARGS(family, type, protocol, usockvec),
	TP_STRUCT__entry(__field(int, family) __field(int, type) __field(int, protocol) __field_hex(int *, usockvec)),
	TP_fast_assign(tp_assign(family, family) tp_assign(type, type) tp_assign(protocol, protocol) tp_assign(usockvec, usockvec)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_reboot
SC_TRACE_EVENT(sys_reboot,
	TP_PROTO(int magic1, int magic2, unsigned int cmd, void * arg),
	TP_ARGS(magic1, magic2, cmd, arg),
	TP_STRUCT__entry(__field(int, magic1) __field(int, magic2) __field(unsigned int, cmd) __field_hex(void *, arg)),
	TP_fast_assign(tp_assign(magic1, magic1) tp_assign(magic2, magic2) tp_assign(cmd, cmd) tp_assign(arg, arg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_quotactl
SC_TRACE_EVENT(sys_quotactl,
	TP_PROTO(unsigned int cmd, const char * special, qid_t id, void * addr),
	TP_ARGS(cmd, special, id, addr),
	TP_STRUCT__entry(__field(unsigned int, cmd) __field_hex(const char *, special) __field(qid_t, id) __field_hex(void *, addr)),
	TP_fast_assign(tp_assign(cmd, cmd) tp_assign(special, special) tp_assign(id, id) tp_assign(addr, addr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getxattr
SC_TRACE_EVENT(sys_getxattr,
	TP_PROTO(const char * pathname, const char * name, void * value, size_t size),
	TP_ARGS(pathname, name, value, size),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __string_from_user(name, name) __field_hex(void *, value) __field(size_t, size)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_copy_string_from_user(name, name) tp_assign(value, value) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_lgetxattr
SC_TRACE_EVENT(sys_lgetxattr,
	TP_PROTO(const char * pathname, const char * name, void * value, size_t size),
	TP_ARGS(pathname, name, value, size),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __string_from_user(name, name) __field_hex(void *, value) __field(size_t, size)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_copy_string_from_user(name, name) tp_assign(value, value) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_fgetxattr
SC_TRACE_EVENT(sys_fgetxattr,
	TP_PROTO(int fd, const char * name, void * value, size_t size),
	TP_ARGS(fd, name, value, size),
	TP_STRUCT__entry(__field(int, fd) __string_from_user(name, name) __field_hex(void *, value) __field(size_t, size)),
	TP_fast_assign(tp_assign(fd, fd) tp_copy_string_from_user(name, name) tp_assign(value, value) tp_assign(size, size)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_epoll_ctl
SC_TRACE_EVENT(sys_epoll_ctl,
	TP_PROTO(int epfd, int op, int fd, struct epoll_event * event),
	TP_ARGS(epfd, op, fd, event),
	TP_STRUCT__entry(__field(int, epfd) __field(int, op) __field(int, fd) __field_hex(struct epoll_event *, event)),
	TP_fast_assign(tp_assign(epfd, epfd) tp_assign(op, op) tp_assign(fd, fd) tp_assign(event, event)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_epoll_wait
SC_TRACE_EVENT(sys_epoll_wait,
	TP_PROTO(int epfd, struct epoll_event * events, int maxevents, int timeout),
	TP_ARGS(epfd, events, maxevents, timeout),
	TP_STRUCT__entry(__field(int, epfd) __field_hex(struct epoll_event *, events) __field(int, maxevents) __field(int, timeout)),
	TP_fast_assign(tp_assign(epfd, epfd) tp_assign(events, events) tp_assign(maxevents, maxevents) tp_assign(timeout, timeout)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sendfile64
SC_TRACE_EVENT(sys_sendfile64,
	TP_PROTO(int out_fd, int in_fd, loff_t * offset, size_t count),
	TP_ARGS(out_fd, in_fd, offset, count),
	TP_STRUCT__entry(__field(int, out_fd) __field(int, in_fd) __field_hex(loff_t *, offset) __field(size_t, count)),
	TP_fast_assign(tp_assign(out_fd, out_fd) tp_assign(in_fd, in_fd) tp_assign(offset, offset) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_openat
SC_TRACE_EVENT(sys_openat,
	TP_PROTO(int dfd, const char * filename, int flags, umode_t mode),
	TP_ARGS(dfd, filename, flags, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(int, flags) __field(umode_t, mode)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(flags, flags) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mknodat
SC_TRACE_EVENT(sys_mknodat,
	TP_PROTO(int dfd, const char * filename, umode_t mode, unsigned dev),
	TP_ARGS(dfd, filename, mode, dev),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(umode_t, mode) __field(unsigned, dev)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(mode, mode) tp_assign(dev, dev)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_newfstatat
SC_TRACE_EVENT(sys_newfstatat,
	TP_PROTO(int dfd, const char * filename, struct stat * statbuf, int flag),
	TP_ARGS(dfd, filename, statbuf, flag),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field_hex(struct stat *, statbuf) __field(int, flag)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(statbuf, statbuf) tp_assign(flag, flag)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_renameat
SC_TRACE_EVENT(sys_renameat,
	TP_PROTO(int olddfd, const char * oldname, int newdfd, const char * newname),
	TP_ARGS(olddfd, oldname, newdfd, newname),
	TP_STRUCT__entry(__field(int, olddfd) __string_from_user(oldname, oldname) __field(int, newdfd) __string_from_user(newname, newname)),
	TP_fast_assign(tp_assign(olddfd, olddfd) tp_copy_string_from_user(oldname, oldname) tp_assign(newdfd, newdfd) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_readlinkat
SC_TRACE_EVENT(sys_readlinkat,
	TP_PROTO(int dfd, const char * pathname, char * buf, int bufsiz),
	TP_ARGS(dfd, pathname, buf, bufsiz),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(pathname, pathname) __field_hex(char *, buf) __field(int, bufsiz)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(pathname, pathname) tp_assign(buf, buf) tp_assign(bufsiz, bufsiz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_signalfd4
SC_TRACE_EVENT(sys_signalfd4,
	TP_PROTO(int ufd, sigset_t * user_mask, size_t sizemask, int flags),
	TP_ARGS(ufd, user_mask, sizemask, flags),
	TP_STRUCT__entry(__field(int, ufd) __field_hex(sigset_t *, user_mask) __field(size_t, sizemask) __field(int, flags)),
	TP_fast_assign(tp_assign(ufd, ufd) tp_assign(user_mask, user_mask) tp_assign(sizemask, sizemask) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_accept4
SC_TRACE_EVENT(sys_accept4,
	TP_PROTO(int fd, struct sockaddr * upeer_sockaddr, int * upeer_addrlen, int flags),
	TP_ARGS(fd, upeer_sockaddr, upeer_addrlen, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, upeer_sockaddr) __field_hex(int *, upeer_addrlen) __field(int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(upeer_sockaddr, upeer_sockaddr) tp_assign(upeer_addrlen, upeer_addrlen) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_prlimit64
SC_TRACE_EVENT(sys_prlimit64,
	TP_PROTO(pid_t pid, unsigned int resource, const struct rlimit64 * new_rlim, struct rlimit64 * old_rlim),
	TP_ARGS(pid, resource, new_rlim, old_rlim),
	TP_STRUCT__entry(__field(pid_t, pid) __field(unsigned int, resource) __field_hex(const struct rlimit64 *, new_rlim) __field_hex(struct rlimit64 *, old_rlim)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(resource, resource) tp_assign(new_rlim, new_rlim) tp_assign(old_rlim, old_rlim)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_llseek
SC_TRACE_EVENT(sys_32_llseek,
	TP_PROTO(unsigned int fd, unsigned int offset_high, unsigned int offset_low, loff_t * result, unsigned int origin),
	TP_ARGS(fd, offset_high, offset_low, result, origin),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(unsigned int, offset_high) __field(unsigned int, offset_low) __field_hex(loff_t *, result) __field(unsigned int, origin)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(offset_high, offset_high) tp_assign(offset_low, offset_low) tp_assign(result, result) tp_assign(origin, origin)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_waitid
SC_TRACE_EVENT(sys_32_waitid,
	TP_PROTO(int which, compat_pid_t pid, compat_siginfo_t * uinfo, int options, struct compat_rusage * uru),
	TP_ARGS(which, pid, uinfo, options, uru),
	TP_STRUCT__entry(__field(int, which) __field(compat_pid_t, pid) __field_hex(compat_siginfo_t *, uinfo) __field(int, options) __field_hex(struct compat_rusage *, uru)),
	TP_fast_assign(tp_assign(which, which) tp_assign(pid, pid) tp_assign(uinfo, uinfo) tp_assign(options, options) tp_assign(uru, uru)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_select
SC_TRACE_EVENT(sys_select,
	TP_PROTO(int n, fd_set * inp, fd_set * outp, fd_set * exp, struct timeval * tvp),
	TP_ARGS(n, inp, outp, exp, tvp),
	TP_STRUCT__entry(__field(int, n) __field_hex(fd_set *, inp) __field_hex(fd_set *, outp) __field_hex(fd_set *, exp) __field_hex(struct timeval *, tvp)),
	TP_fast_assign(tp_assign(n, n) tp_assign(inp, inp) tp_assign(outp, outp) tp_assign(exp, exp) tp_assign(tvp, tvp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_setsockopt
SC_TRACE_EVENT(sys_setsockopt,
	TP_PROTO(int fd, int level, int optname, char * optval, int optlen),
	TP_ARGS(fd, level, optname, optval, optlen),
	TP_STRUCT__entry(__field(int, fd) __field(int, level) __field(int, optname) __field_hex(char *, optval) __field(int, optlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(level, level) tp_assign(optname, optname) tp_assign(optval, optval) tp_assign(optlen, optlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_msgrcv
SC_TRACE_EVENT(sys_msgrcv,
	TP_PROTO(int msqid, struct msgbuf * msgp, size_t msgsz, long msgtyp, int msgflg),
	TP_ARGS(msqid, msgp, msgsz, msgtyp, msgflg),
	TP_STRUCT__entry(__field(int, msqid) __field_hex(struct msgbuf *, msgp) __field(size_t, msgsz) __field(long, msgtyp) __field(int, msgflg)),
	TP_fast_assign(tp_assign(msqid, msqid) tp_assign(msgp, msgp) tp_assign(msgsz, msgsz) tp_assign(msgtyp, msgtyp) tp_assign(msgflg, msgflg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mount
SC_TRACE_EVENT(sys_mount,
	TP_PROTO(char * dev_name, char * dir_name, char * type, unsigned long flags, void * data),
	TP_ARGS(dev_name, dir_name, type, flags, data),
	TP_STRUCT__entry(__string_from_user(dev_name, dev_name) __string_from_user(dir_name, dir_name) __string_from_user(type, type) __field(unsigned long, flags) __field_hex(void *, data)),
	TP_fast_assign(tp_copy_string_from_user(dev_name, dev_name) tp_copy_string_from_user(dir_name, dir_name) tp_copy_string_from_user(type, type) tp_assign(flags, flags) tp_assign(data, data)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_io_getevents
SC_TRACE_EVENT(sys_io_getevents,
	TP_PROTO(aio_context_t ctx_id, long min_nr, long nr, struct io_event * events, struct timespec * timeout),
	TP_ARGS(ctx_id, min_nr, nr, events, timeout),
	TP_STRUCT__entry(__field(aio_context_t, ctx_id) __field(long, min_nr) __field(long, nr) __field_hex(struct io_event *, events) __field_hex(struct timespec *, timeout)),
	TP_fast_assign(tp_assign(ctx_id, ctx_id) tp_assign(min_nr, min_nr) tp_assign(nr, nr) tp_assign(events, events) tp_assign(timeout, timeout)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_waitid
SC_TRACE_EVENT(sys_waitid,
	TP_PROTO(int which, pid_t upid, struct siginfo * infop, int options, struct rusage * ru),
	TP_ARGS(which, upid, infop, options, ru),
	TP_STRUCT__entry(__field(int, which) __field(pid_t, upid) __field_hex(struct siginfo *, infop) __field(int, options) __field_hex(struct rusage *, ru)),
	TP_fast_assign(tp_assign(which, which) tp_assign(upid, upid) tp_assign(infop, infop) tp_assign(options, options) tp_assign(ru, ru)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_ppoll
SC_TRACE_EVENT(sys_ppoll,
	TP_PROTO(struct pollfd * ufds, unsigned int nfds, struct timespec * tsp, const sigset_t * sigmask, size_t sigsetsize),
	TP_ARGS(ufds, nfds, tsp, sigmask, sigsetsize),
	TP_STRUCT__entry(__field_hex(struct pollfd *, ufds) __field(unsigned int, nfds) __field_hex(struct timespec *, tsp) __field_hex(const sigset_t *, sigmask) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(ufds, ufds) tp_assign(nfds, nfds) tp_assign(tsp, tsp) tp_assign(sigmask, sigmask) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_recvmmsg
SC_TRACE_EVENT(sys_recvmmsg,
	TP_PROTO(int fd, struct mmsghdr * mmsg, unsigned int vlen, unsigned int flags, struct timespec * timeout),
	TP_ARGS(fd, mmsg, vlen, flags, timeout),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct mmsghdr *, mmsg) __field(unsigned int, vlen) __field(unsigned int, flags) __field_hex(struct timespec *, timeout)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(mmsg, mmsg) tp_assign(vlen, vlen) tp_assign(flags, flags) tp_assign(timeout, timeout)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getsockopt
SC_TRACE_EVENT(sys_getsockopt,
	TP_PROTO(int fd, int level, int optname, char * optval, int * optlen),
	TP_ARGS(fd, level, optname, optval, optlen),
	TP_STRUCT__entry(__field(int, fd) __field(int, level) __field(int, optname) __field_hex(char *, optval) __field_hex(int *, optlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(level, level) tp_assign(optname, optname) tp_assign(optval, optval) tp_assign(optlen, optlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_setxattr
SC_TRACE_EVENT(sys_setxattr,
	TP_PROTO(const char * pathname, const char * name, const void * value, size_t size, int flags),
	TP_ARGS(pathname, name, value, size, flags),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __string_from_user(name, name) __field_hex(const void *, value) __field(size_t, size) __field(int, flags)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_copy_string_from_user(name, name) tp_assign(value, value) tp_assign(size, size) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_lsetxattr
SC_TRACE_EVENT(sys_lsetxattr,
	TP_PROTO(const char * pathname, const char * name, const void * value, size_t size, int flags),
	TP_ARGS(pathname, name, value, size, flags),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __string_from_user(name, name) __field_hex(const void *, value) __field(size_t, size) __field(int, flags)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_copy_string_from_user(name, name) tp_assign(value, value) tp_assign(size, size) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_fsetxattr
SC_TRACE_EVENT(sys_fsetxattr,
	TP_PROTO(int fd, const char * name, const void * value, size_t size, int flags),
	TP_ARGS(fd, name, value, size, flags),
	TP_STRUCT__entry(__field(int, fd) __string_from_user(name, name) __field_hex(const void *, value) __field(size_t, size) __field(int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_copy_string_from_user(name, name) tp_assign(value, value) tp_assign(size, size) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_fchownat
SC_TRACE_EVENT(sys_fchownat,
	TP_PROTO(int dfd, const char * filename, uid_t user, gid_t group, int flag),
	TP_ARGS(dfd, filename, user, group, flag),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(uid_t, user) __field(gid_t, group) __field(int, flag)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(user, user) tp_assign(group, group) tp_assign(flag, flag)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_linkat
SC_TRACE_EVENT(sys_linkat,
	TP_PROTO(int olddfd, const char * oldname, int newdfd, const char * newname, int flags),
	TP_ARGS(olddfd, oldname, newdfd, newname, flags),
	TP_STRUCT__entry(__field(int, olddfd) __string_from_user(oldname, oldname) __field(int, newdfd) __string_from_user(newname, newname) __field(int, flags)),
	TP_fast_assign(tp_assign(olddfd, olddfd) tp_copy_string_from_user(oldname, oldname) tp_assign(newdfd, newdfd) tp_copy_string_from_user(newname, newname) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_preadv
SC_TRACE_EVENT(sys_preadv,
	TP_PROTO(unsigned long fd, const struct iovec * vec, unsigned long vlen, unsigned long pos_l, unsigned long pos_h),
	TP_ARGS(fd, vec, vlen, pos_l, pos_h),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(const struct iovec *, vec) __field(unsigned long, vlen) __field(unsigned long, pos_l) __field(unsigned long, pos_h)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(vec, vec) tp_assign(vlen, vlen) tp_assign(pos_l, pos_l) tp_assign(pos_h, pos_h)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_pwritev
SC_TRACE_EVENT(sys_pwritev,
	TP_PROTO(unsigned long fd, const struct iovec * vec, unsigned long vlen, unsigned long pos_l, unsigned long pos_h),
	TP_ARGS(fd, vec, vlen, pos_l, pos_h),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(const struct iovec *, vec) __field(unsigned long, vlen) __field(unsigned long, pos_l) __field(unsigned long, pos_h)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(vec, vec) tp_assign(vlen, vlen) tp_assign(pos_l, pos_l) tp_assign(pos_h, pos_h)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_pread
SC_TRACE_EVENT(sys_32_pread,
	TP_PROTO(unsigned long fd, char * buf, size_t count, unsigned long unused, unsigned long a4, unsigned long a5),
	TP_ARGS(fd, buf, count, unused, a4, a5),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(char *, buf) __field(size_t, count) __field(unsigned long, unused) __field(unsigned long, a4) __field(unsigned long, a5)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf) tp_assign(count, count) tp_assign(unused, unused) tp_assign(a4, a4) tp_assign(a5, a5)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_pwrite
SC_TRACE_EVENT(sys_32_pwrite,
	TP_PROTO(unsigned int fd, const char * buf, size_t count, u32 unused, u64 a4, u64 a5),
	TP_ARGS(fd, buf, count, unused, a4, a5),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(const char *, buf) __field(size_t, count) __field(u32, unused) __field(u64, a4) __field(u64, a5)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf) tp_assign(count, count) tp_assign(unused, unused) tp_assign(a4, a4) tp_assign(a5, a5)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_fanotify_mark
SC_TRACE_EVENT(sys_32_fanotify_mark,
	TP_PROTO(int fanotify_fd, unsigned int flags, u64 a3, u64 a4, int dfd, const char * pathname),
	TP_ARGS(fanotify_fd, flags, a3, a4, dfd, pathname),
	TP_STRUCT__entry(__field(int, fanotify_fd) __field(unsigned int, flags) __field(u64, a3) __field(u64, a4) __field(int, dfd) __string_from_user(pathname, pathname)),
	TP_fast_assign(tp_assign(fanotify_fd, fanotify_fd) tp_assign(flags, flags) tp_assign(a3, a3) tp_assign(a4, a4) tp_assign(dfd, dfd) tp_copy_string_from_user(pathname, pathname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_recvfrom
SC_TRACE_EVENT(sys_recvfrom,
	TP_PROTO(int fd, void * ubuf, size_t size, unsigned int flags, struct sockaddr * addr, int * addr_len),
	TP_ARGS(fd, ubuf, size, flags, addr, addr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(void *, ubuf) __field(size_t, size) __field(unsigned int, flags) __field_hex(struct sockaddr *, addr) __field_hex(int *, addr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(ubuf, ubuf) tp_assign(size, size) tp_assign(flags, flags) tp_assign(addr, addr) tp_assign(addr_len, addr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_futex
SC_TRACE_EVENT(sys_futex,
	TP_PROTO(u32 * uaddr, int op, u32 val, struct timespec * utime, u32 * uaddr2, u32 val3),
	TP_ARGS(uaddr, op, val, utime, uaddr2, val3),
	TP_STRUCT__entry(__field_hex(u32 *, uaddr) __field(int, op) __field(u32, val) __field_hex(struct timespec *, utime) __field_hex(u32 *, uaddr2) __field(u32, val3)),
	TP_fast_assign(tp_assign(uaddr, uaddr) tp_assign(op, op) tp_assign(val, val) tp_assign(utime, utime) tp_assign(uaddr2, uaddr2) tp_assign(val3, val3)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_pselect6
SC_TRACE_EVENT(sys_pselect6,
	TP_PROTO(int n, fd_set * inp, fd_set * outp, fd_set * exp, struct timespec * tsp, void * sig),
	TP_ARGS(n, inp, outp, exp, tsp, sig),
	TP_STRUCT__entry(__field(int, n) __field_hex(fd_set *, inp) __field_hex(fd_set *, outp) __field_hex(fd_set *, exp) __field_hex(struct timespec *, tsp) __field_hex(void *, sig)),
	TP_fast_assign(tp_assign(n, n) tp_assign(inp, inp) tp_assign(outp, outp) tp_assign(exp, exp) tp_assign(tsp, tsp) tp_assign(sig, sig)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_epoll_pwait
SC_TRACE_EVENT(sys_epoll_pwait,
	TP_PROTO(int epfd, struct epoll_event * events, int maxevents, int timeout, const sigset_t * sigmask, size_t sigsetsize),
	TP_ARGS(epfd, events, maxevents, timeout, sigmask, sigsetsize),
	TP_STRUCT__entry(__field(int, epfd) __field_hex(struct epoll_event *, events) __field(int, maxevents) __field(int, timeout) __field_hex(const sigset_t *, sigmask) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(epfd, epfd) tp_assign(events, events) tp_assign(maxevents, maxevents) tp_assign(timeout, timeout) tp_assign(sigmask, sigmask) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_process_vm_readv
SC_TRACE_EVENT(sys_process_vm_readv,
	TP_PROTO(pid_t pid, const struct iovec * lvec, unsigned long liovcnt, const struct iovec * rvec, unsigned long riovcnt, unsigned long flags),
	TP_ARGS(pid, lvec, liovcnt, rvec, riovcnt, flags),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(const struct iovec *, lvec) __field(unsigned long, liovcnt) __field_hex(const struct iovec *, rvec) __field(unsigned long, riovcnt) __field(unsigned long, flags)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(lvec, lvec) tp_assign(liovcnt, liovcnt) tp_assign(rvec, rvec) tp_assign(riovcnt, riovcnt) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_process_vm_writev
SC_TRACE_EVENT(sys_process_vm_writev,
	TP_PROTO(pid_t pid, const struct iovec * lvec, unsigned long liovcnt, const struct iovec * rvec, unsigned long riovcnt, unsigned long flags),
	TP_ARGS(pid, lvec, liovcnt, rvec, riovcnt, flags),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(const struct iovec *, lvec) __field(unsigned long, liovcnt) __field_hex(const struct iovec *, rvec) __field(unsigned long, riovcnt) __field(unsigned long, flags)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(lvec, lvec) tp_assign(liovcnt, liovcnt) tp_assign(rvec, rvec) tp_assign(riovcnt, riovcnt) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_sendto
SC_TRACE_EVENT(sys_sendto,
	TP_PROTO(int fd, void * buff, size_t len, unsigned int flags, struct sockaddr * addr, int addr_len),
	TP_ARGS(fd, buff, len, flags, addr, addr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(void *, buff) __field(size_t, len) __field(unsigned int, flags) __field_hex(struct sockaddr *, addr) __field_hex(int, addr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buff, buff) tp_assign(len, len) tp_assign(flags, flags) tp_assign(addr, addr) tp_assign(addr_len, addr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_32_futex
SC_TRACE_EVENT(sys_32_futex,
	TP_PROTO(u32 * uaddr, int op, u32 val, struct compat_timespec * utime, u32 * uaddr2, u32 val3),
	TP_ARGS(uaddr, op, val, utime, uaddr2, val3),
	TP_STRUCT__entry(__field_hex(u32 *, uaddr) __field(int, op) __field(u32, val) __field_hex(struct compat_timespec *, utime) __field_hex(u32 *, uaddr2) __field(u32, val3)),
	TP_fast_assign(tp_assign(uaddr, uaddr) tp_assign(op, op) tp_assign(val, val) tp_assign(utime, utime) tp_assign(uaddr2, uaddr2) tp_assign(val3, val3)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_splice
SC_TRACE_EVENT(sys_splice,
	TP_PROTO(int fd_in, loff_t * off_in, int fd_out, loff_t * off_out, size_t len, unsigned int flags),
	TP_ARGS(fd_in, off_in, fd_out, off_out, len, flags),
	TP_STRUCT__entry(__field(int, fd_in) __field_hex(loff_t *, off_in) __field(int, fd_out) __field_hex(loff_t *, off_out) __field(size_t, len) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd_in, fd_in) tp_assign(off_in, off_in) tp_assign(fd_out, fd_out) tp_assign(off_out, off_out) tp_assign(len, len) tp_assign(flags, flags)),
	TP_printk()
)
#endif

#endif /*  _TRACE_SYSCALLS_POINTERS_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

#else /* CREATE_SYSCALL_TABLE */

#include "mips-64-syscalls-3.5.0_pointers_override.h"
#include "syscalls_pointers_override.h"

#ifndef OVERRIDE_TABLE_64_sys_waitpid
TRACE_SYSCALL_TABLE(sys_waitpid, sys_waitpid, 4007, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_oldumount
TRACE_SYSCALL_TABLE(sys_oldumount, sys_oldumount, 4022, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_olduname
TRACE_SYSCALL_TABLE(sys_olduname, sys_olduname, 4059, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_sigaction
TRACE_SYSCALL_TABLE(sys_32_sigaction, sys_32_sigaction, 4067, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_uselib
TRACE_SYSCALL_TABLE(sys_uselib, sys_uselib, 4086, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_uname
TRACE_SYSCALL_TABLE(sys_uname, sys_uname, 4109, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_llseek
TRACE_SYSCALL_TABLE(sys_32_llseek, sys_32_llseek, 4140, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_send
TRACE_SYSCALL_TABLE(sys_send, sys_send, 4178, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_pread
TRACE_SYSCALL_TABLE(sys_32_pread, sys_32_pread, 4200, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_pwrite
TRACE_SYSCALL_TABLE(sys_32_pwrite, sys_32_pwrite, 4201, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_truncate64
TRACE_SYSCALL_TABLE(sys_32_truncate64, sys_32_truncate64, 4211, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_waitid
TRACE_SYSCALL_TABLE(sys_32_waitid, sys_32_waitid, 4278, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_fanotify_mark
TRACE_SYSCALL_TABLE(sys_32_fanotify_mark, sys_32_fanotify_mark, 4337, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigaction
TRACE_SYSCALL_TABLE(sys_rt_sigaction, sys_rt_sigaction, 5013, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigprocmask
TRACE_SYSCALL_TABLE(sys_rt_sigprocmask, sys_rt_sigprocmask, 5014, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_readv
TRACE_SYSCALL_TABLE(sys_readv, sys_readv, 5018, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_writev
TRACE_SYSCALL_TABLE(sys_writev, sys_writev, 5019, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_select
TRACE_SYSCALL_TABLE(sys_select, sys_select, 5022, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_shmctl
TRACE_SYSCALL_TABLE(sys_shmctl, sys_shmctl, 5030, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_nanosleep
TRACE_SYSCALL_TABLE(sys_nanosleep, sys_nanosleep, 5034, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getitimer
TRACE_SYSCALL_TABLE(sys_getitimer, sys_getitimer, 5035, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setitimer
TRACE_SYSCALL_TABLE(sys_setitimer, sys_setitimer, 5036, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_recvfrom
TRACE_SYSCALL_TABLE(sys_recvfrom, sys_recvfrom, 5044, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendmsg
TRACE_SYSCALL_TABLE(sys_sendmsg, sys_sendmsg, 5045, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_recvmsg
TRACE_SYSCALL_TABLE(sys_recvmsg, sys_recvmsg, 5046, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setsockopt
TRACE_SYSCALL_TABLE(sys_setsockopt, sys_setsockopt, 5053, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_wait4
TRACE_SYSCALL_TABLE(sys_wait4, sys_wait4, 5059, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_msgsnd
TRACE_SYSCALL_TABLE(sys_msgsnd, sys_msgsnd, 5067, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_msgrcv
TRACE_SYSCALL_TABLE(sys_msgrcv, sys_msgrcv, 5068, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_msgctl
TRACE_SYSCALL_TABLE(sys_msgctl, sys_msgctl, 5069, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getdents
TRACE_SYSCALL_TABLE(sys_getdents, sys_getdents, 5076, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_gettimeofday
TRACE_SYSCALL_TABLE(sys_gettimeofday, sys_gettimeofday, 5094, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getrlimit
TRACE_SYSCALL_TABLE(sys_getrlimit, sys_getrlimit, 5095, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getrusage
TRACE_SYSCALL_TABLE(sys_getrusage, sys_getrusage, 5096, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sysinfo
TRACE_SYSCALL_TABLE(sys_sysinfo, sys_sysinfo, 5097, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_times
TRACE_SYSCALL_TABLE(sys_times, sys_times, 5098, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigpending
TRACE_SYSCALL_TABLE(sys_rt_sigpending, sys_rt_sigpending, 5125, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigtimedwait
TRACE_SYSCALL_TABLE(sys_rt_sigtimedwait, sys_rt_sigtimedwait, 5126, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigqueueinfo
TRACE_SYSCALL_TABLE(sys_rt_sigqueueinfo, sys_rt_sigqueueinfo, 5127, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_utime
TRACE_SYSCALL_TABLE(sys_utime, sys_utime, 5130, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_ustat
TRACE_SYSCALL_TABLE(sys_ustat, sys_ustat, 5133, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_statfs
TRACE_SYSCALL_TABLE(sys_statfs, sys_statfs, 5134, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fstatfs
TRACE_SYSCALL_TABLE(sys_fstatfs, sys_fstatfs, 5135, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_rr_get_interval
TRACE_SYSCALL_TABLE(sys_sched_rr_get_interval, sys_sched_rr_get_interval, 5145, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sysctl
TRACE_SYSCALL_TABLE(sys_sysctl, sys_sysctl, 5152, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_adjtimex
TRACE_SYSCALL_TABLE(sys_adjtimex, sys_adjtimex, 5154, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setrlimit
TRACE_SYSCALL_TABLE(sys_setrlimit, sys_setrlimit, 5155, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_settimeofday
TRACE_SYSCALL_TABLE(sys_settimeofday, sys_settimeofday, 5159, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mount
TRACE_SYSCALL_TABLE(sys_mount, sys_mount, 5160, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_futex
TRACE_SYSCALL_TABLE(sys_futex, sys_futex, 5194, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_setaffinity
TRACE_SYSCALL_TABLE(sys_sched_setaffinity, sys_sched_setaffinity, 5195, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_getaffinity
TRACE_SYSCALL_TABLE(sys_sched_getaffinity, sys_sched_getaffinity, 5196, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_setup
TRACE_SYSCALL_TABLE(sys_io_setup, sys_io_setup, 5200, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_getevents
TRACE_SYSCALL_TABLE(sys_io_getevents, sys_io_getevents, 5202, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_submit
TRACE_SYSCALL_TABLE(sys_io_submit, sys_io_submit, 5203, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_semtimedop
TRACE_SYSCALL_TABLE(sys_semtimedop, sys_semtimedop, 5214, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timer_create
TRACE_SYSCALL_TABLE(sys_timer_create, sys_timer_create, 5216, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timer_settime
TRACE_SYSCALL_TABLE(sys_timer_settime, sys_timer_settime, 5217, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timer_gettime
TRACE_SYSCALL_TABLE(sys_timer_gettime, sys_timer_gettime, 5218, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_settime
TRACE_SYSCALL_TABLE(sys_clock_settime, sys_clock_settime, 5221, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_gettime
TRACE_SYSCALL_TABLE(sys_clock_gettime, sys_clock_gettime, 5222, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_getres
TRACE_SYSCALL_TABLE(sys_clock_getres, sys_clock_getres, 5223, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_nanosleep
TRACE_SYSCALL_TABLE(sys_clock_nanosleep, sys_clock_nanosleep, 5224, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_utimes
TRACE_SYSCALL_TABLE(sys_utimes, sys_utimes, 5226, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_waitid
TRACE_SYSCALL_TABLE(sys_waitid, sys_waitid, 5237, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_futimesat
TRACE_SYSCALL_TABLE(sys_futimesat, sys_futimesat, 5251, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pselect6
TRACE_SYSCALL_TABLE(sys_pselect6, sys_pselect6, 5260, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_ppoll
TRACE_SYSCALL_TABLE(sys_ppoll, sys_ppoll, 5261, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_vmsplice
TRACE_SYSCALL_TABLE(sys_vmsplice, sys_vmsplice, 5266, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_set_robust_list
TRACE_SYSCALL_TABLE(sys_set_robust_list, sys_set_robust_list, 5268, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_get_robust_list
TRACE_SYSCALL_TABLE(sys_get_robust_list, sys_get_robust_list, 5269, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_epoll_pwait
TRACE_SYSCALL_TABLE(sys_epoll_pwait, sys_epoll_pwait, 5272, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_utimensat
TRACE_SYSCALL_TABLE(sys_utimensat, sys_utimensat, 5275, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_signalfd
TRACE_SYSCALL_TABLE(sys_signalfd, sys_signalfd, 5276, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timerfd_gettime
TRACE_SYSCALL_TABLE(sys_timerfd_gettime, sys_timerfd_gettime, 5281, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timerfd_settime
TRACE_SYSCALL_TABLE(sys_timerfd_settime, sys_timerfd_settime, 5282, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_tgsigqueueinfo
TRACE_SYSCALL_TABLE(sys_rt_tgsigqueueinfo, sys_rt_tgsigqueueinfo, 5291, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_recvmmsg
TRACE_SYSCALL_TABLE(sys_recvmmsg, sys_recvmmsg, 5294, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_adjtime
TRACE_SYSCALL_TABLE(sys_clock_adjtime, sys_clock_adjtime, 5300, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendmmsg
TRACE_SYSCALL_TABLE(sys_sendmmsg, sys_sendmmsg, 5302, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_process_vm_readv
TRACE_SYSCALL_TABLE(sys_process_vm_readv, sys_process_vm_readv, 5304, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_process_vm_writev
TRACE_SYSCALL_TABLE(sys_process_vm_writev, sys_process_vm_writev, 5305, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_read
TRACE_SYSCALL_TABLE(sys_read, sys_read, 6000, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_write
TRACE_SYSCALL_TABLE(sys_write, sys_write, 6001, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_open
TRACE_SYSCALL_TABLE(sys_open, sys_open, 6002, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newstat
TRACE_SYSCALL_TABLE(sys_newstat, sys_newstat, 6004, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newfstat
TRACE_SYSCALL_TABLE(sys_newfstat, sys_newfstat, 6005, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newlstat
TRACE_SYSCALL_TABLE(sys_newlstat, sys_newlstat, 6006, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_poll
TRACE_SYSCALL_TABLE(sys_poll, sys_poll, 6007, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_rt_sigaction
TRACE_SYSCALL_TABLE(sys_32_rt_sigaction, sys_32_rt_sigaction, 6013, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_rt_sigprocmask
TRACE_SYSCALL_TABLE(sys_32_rt_sigprocmask, sys_32_rt_sigprocmask, 6014, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_access
TRACE_SYSCALL_TABLE(sys_access, sys_access, 6020, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mincore
TRACE_SYSCALL_TABLE(sys_mincore, sys_mincore, 6026, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_shmat
TRACE_SYSCALL_TABLE(sys_shmat, sys_shmat, 6029, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_sendfile
TRACE_SYSCALL_TABLE(sys_32_sendfile, sys_32_sendfile, 6039, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_connect
TRACE_SYSCALL_TABLE(sys_connect, sys_connect, 6041, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_accept
TRACE_SYSCALL_TABLE(sys_accept, sys_accept, 6042, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendto
TRACE_SYSCALL_TABLE(sys_sendto, sys_sendto, 6043, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_bind
TRACE_SYSCALL_TABLE(sys_bind, sys_bind, 6048, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getsockname
TRACE_SYSCALL_TABLE(sys_getsockname, sys_getsockname, 6050, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getpeername
TRACE_SYSCALL_TABLE(sys_getpeername, sys_getpeername, 6051, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_socketpair
TRACE_SYSCALL_TABLE(sys_socketpair, sys_socketpair, 6052, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getsockopt
TRACE_SYSCALL_TABLE(sys_getsockopt, sys_getsockopt, 6054, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newuname
TRACE_SYSCALL_TABLE(sys_newuname, sys_newuname, 6061, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_semop
TRACE_SYSCALL_TABLE(sys_semop, sys_semop, 6063, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_shmdt
TRACE_SYSCALL_TABLE(sys_shmdt, sys_shmdt, 6065, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_truncate
TRACE_SYSCALL_TABLE(sys_truncate, sys_truncate, 6074, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getcwd
TRACE_SYSCALL_TABLE(sys_getcwd, sys_getcwd, 6077, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chdir
TRACE_SYSCALL_TABLE(sys_chdir, sys_chdir, 6078, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rename
TRACE_SYSCALL_TABLE(sys_rename, sys_rename, 6080, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mkdir
TRACE_SYSCALL_TABLE(sys_mkdir, sys_mkdir, 6081, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rmdir
TRACE_SYSCALL_TABLE(sys_rmdir, sys_rmdir, 6082, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_creat
TRACE_SYSCALL_TABLE(sys_creat, sys_creat, 6083, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_link
TRACE_SYSCALL_TABLE(sys_link, sys_link, 6084, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_unlink
TRACE_SYSCALL_TABLE(sys_unlink, sys_unlink, 6085, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_symlink
TRACE_SYSCALL_TABLE(sys_symlink, sys_symlink, 6086, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_readlink
TRACE_SYSCALL_TABLE(sys_readlink, sys_readlink, 6087, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chmod
TRACE_SYSCALL_TABLE(sys_chmod, sys_chmod, 6088, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chown
TRACE_SYSCALL_TABLE(sys_chown, sys_chown, 6090, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lchown
TRACE_SYSCALL_TABLE(sys_lchown, sys_lchown, 6092, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_syslog
TRACE_SYSCALL_TABLE(sys_syslog, sys_syslog, 6101, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getgroups
TRACE_SYSCALL_TABLE(sys_getgroups, sys_getgroups, 6113, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setgroups
TRACE_SYSCALL_TABLE(sys_setgroups, sys_setgroups, 6114, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getresuid
TRACE_SYSCALL_TABLE(sys_getresuid, sys_getresuid, 6116, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getresgid
TRACE_SYSCALL_TABLE(sys_getresgid, sys_getresgid, 6118, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_rt_sigpending
TRACE_SYSCALL_TABLE(sys_32_rt_sigpending, sys_32_rt_sigpending, 6125, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_rt_sigqueueinfo
TRACE_SYSCALL_TABLE(sys_32_rt_sigqueueinfo, sys_32_rt_sigqueueinfo, 6127, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mknod
TRACE_SYSCALL_TABLE(sys_mknod, sys_mknod, 6131, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_setparam
TRACE_SYSCALL_TABLE(sys_sched_setparam, sys_sched_setparam, 6139, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_getparam
TRACE_SYSCALL_TABLE(sys_sched_getparam, sys_sched_getparam, 6140, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_setscheduler
TRACE_SYSCALL_TABLE(sys_sched_setscheduler, sys_sched_setscheduler, 6141, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_sched_rr_get_interval
TRACE_SYSCALL_TABLE(sys_32_sched_rr_get_interval, sys_32_sched_rr_get_interval, 6145, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pivot_root
TRACE_SYSCALL_TABLE(sys_pivot_root, sys_pivot_root, 6151, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chroot
TRACE_SYSCALL_TABLE(sys_chroot, sys_chroot, 6156, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_umount
TRACE_SYSCALL_TABLE(sys_umount, sys_umount, 6161, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_swapon
TRACE_SYSCALL_TABLE(sys_swapon, sys_swapon, 6162, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_swapoff
TRACE_SYSCALL_TABLE(sys_swapoff, sys_swapoff, 6163, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_reboot
TRACE_SYSCALL_TABLE(sys_reboot, sys_reboot, 6164, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sethostname
TRACE_SYSCALL_TABLE(sys_sethostname, sys_sethostname, 6165, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setdomainname
TRACE_SYSCALL_TABLE(sys_setdomainname, sys_setdomainname, 6166, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_init_module
TRACE_SYSCALL_TABLE(sys_init_module, sys_init_module, 6168, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_delete_module
TRACE_SYSCALL_TABLE(sys_delete_module, sys_delete_module, 6169, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_quotactl
TRACE_SYSCALL_TABLE(sys_quotactl, sys_quotactl, 6172, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setxattr
TRACE_SYSCALL_TABLE(sys_setxattr, sys_setxattr, 6180, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lsetxattr
TRACE_SYSCALL_TABLE(sys_lsetxattr, sys_lsetxattr, 6181, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fsetxattr
TRACE_SYSCALL_TABLE(sys_fsetxattr, sys_fsetxattr, 6182, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getxattr
TRACE_SYSCALL_TABLE(sys_getxattr, sys_getxattr, 6183, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lgetxattr
TRACE_SYSCALL_TABLE(sys_lgetxattr, sys_lgetxattr, 6184, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fgetxattr
TRACE_SYSCALL_TABLE(sys_fgetxattr, sys_fgetxattr, 6185, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_listxattr
TRACE_SYSCALL_TABLE(sys_listxattr, sys_listxattr, 6186, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_llistxattr
TRACE_SYSCALL_TABLE(sys_llistxattr, sys_llistxattr, 6187, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_flistxattr
TRACE_SYSCALL_TABLE(sys_flistxattr, sys_flistxattr, 6188, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_removexattr
TRACE_SYSCALL_TABLE(sys_removexattr, sys_removexattr, 6189, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lremovexattr
TRACE_SYSCALL_TABLE(sys_lremovexattr, sys_lremovexattr, 6190, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fremovexattr
TRACE_SYSCALL_TABLE(sys_fremovexattr, sys_fremovexattr, 6191, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_32_futex
TRACE_SYSCALL_TABLE(sys_32_futex, sys_32_futex, 6194, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_cachectl
TRACE_SYSCALL_TABLE(sys_cachectl, sys_cachectl, 6198, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_cancel
TRACE_SYSCALL_TABLE(sys_io_cancel, sys_io_cancel, 6204, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_epoll_ctl
TRACE_SYSCALL_TABLE(sys_epoll_ctl, sys_epoll_ctl, 6208, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_epoll_wait
TRACE_SYSCALL_TABLE(sys_epoll_wait, sys_epoll_wait, 6209, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_set_tid_address
TRACE_SYSCALL_TABLE(sys_set_tid_address, sys_set_tid_address, 6213, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendfile64
TRACE_SYSCALL_TABLE(sys_sendfile64, sys_sendfile64, 6219, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_inotify_add_watch
TRACE_SYSCALL_TABLE(sys_inotify_add_watch, sys_inotify_add_watch, 6248, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_openat
TRACE_SYSCALL_TABLE(sys_openat, sys_openat, 6251, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mkdirat
TRACE_SYSCALL_TABLE(sys_mkdirat, sys_mkdirat, 6252, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mknodat
TRACE_SYSCALL_TABLE(sys_mknodat, sys_mknodat, 6253, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fchownat
TRACE_SYSCALL_TABLE(sys_fchownat, sys_fchownat, 6254, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newfstatat
TRACE_SYSCALL_TABLE(sys_newfstatat, sys_newfstatat, 6256, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_unlinkat
TRACE_SYSCALL_TABLE(sys_unlinkat, sys_unlinkat, 6257, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_renameat
TRACE_SYSCALL_TABLE(sys_renameat, sys_renameat, 6258, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_linkat
TRACE_SYSCALL_TABLE(sys_linkat, sys_linkat, 6259, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_symlinkat
TRACE_SYSCALL_TABLE(sys_symlinkat, sys_symlinkat, 6260, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_readlinkat
TRACE_SYSCALL_TABLE(sys_readlinkat, sys_readlinkat, 6261, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fchmodat
TRACE_SYSCALL_TABLE(sys_fchmodat, sys_fchmodat, 6262, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_faccessat
TRACE_SYSCALL_TABLE(sys_faccessat, sys_faccessat, 6263, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_splice
TRACE_SYSCALL_TABLE(sys_splice, sys_splice, 6267, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getcpu
TRACE_SYSCALL_TABLE(sys_getcpu, sys_getcpu, 6275, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_signalfd4
TRACE_SYSCALL_TABLE(sys_signalfd4, sys_signalfd4, 6287, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pipe2
TRACE_SYSCALL_TABLE(sys_pipe2, sys_pipe2, 6291, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_preadv
TRACE_SYSCALL_TABLE(sys_preadv, sys_preadv, 6293, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pwritev
TRACE_SYSCALL_TABLE(sys_pwritev, sys_pwritev, 6294, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_accept4
TRACE_SYSCALL_TABLE(sys_accept4, sys_accept4, 6297, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getdents64
TRACE_SYSCALL_TABLE(sys_getdents64, sys_getdents64, 6299, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_prlimit64
TRACE_SYSCALL_TABLE(sys_prlimit64, sys_prlimit64, 6302, 4)
#endif

#endif /* CREATE_SYSCALL_TABLE */
