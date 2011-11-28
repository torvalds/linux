/* THIS FILE IS AUTO-GENERATED. DO NOT EDIT */
#ifndef CREATE_SYSCALL_TABLE

#if !defined(_TRACE_SYSCALLS_POINTERS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYSCALLS_POINTERS_H

#include <linux/tracepoint.h>
#include <linux/syscalls.h>
#include "x86-64-syscalls-3.0.4_pointers_override.h"
#include "syscalls_pointers_override.h"

#ifndef OVERRIDE_64_sys_pipe
SC_TRACE_EVENT(sys_pipe,
	TP_PROTO(int * fildes),
	TP_ARGS(fildes),
	TP_STRUCT__entry(__field_hex(int *, fildes)),
	TP_fast_assign(tp_assign(fildes, fildes)),
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
#ifndef OVERRIDE_64_sys_time
SC_TRACE_EVENT(sys_time,
	TP_PROTO(time_t * tloc),
	TP_ARGS(tloc),
	TP_STRUCT__entry(__field_hex(time_t *, tloc)),
	TP_fast_assign(tp_assign(tloc, tloc)),
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
#ifndef OVERRIDE_64_sys_mq_unlink
SC_TRACE_EVENT(sys_mq_unlink,
	TP_PROTO(const char * u_name),
	TP_ARGS(u_name),
	TP_STRUCT__entry(__string_from_user(u_name, u_name)),
	TP_fast_assign(tp_copy_string_from_user(u_name, u_name)),
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
	TP_PROTO(const char * pathname, int mode),
	TP_ARGS(pathname, mode),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field(int, mode)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_creat
SC_TRACE_EVENT(sys_creat,
	TP_PROTO(const char * pathname, int mode),
	TP_ARGS(pathname, mode),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field(int, mode)),
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
	TP_PROTO(const char * filename, mode_t mode),
	TP_ARGS(filename, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(mode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
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
#ifndef OVERRIDE_64_sys_rt_sigpending
SC_TRACE_EVENT(sys_rt_sigpending,
	TP_PROTO(sigset_t * set, size_t sigsetsize),
	TP_ARGS(set, sigsetsize),
	TP_STRUCT__entry(__field_hex(sigset_t *, set) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(set, set) tp_assign(sigsetsize, sigsetsize)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_rt_sigsuspend
SC_TRACE_EVENT(sys_rt_sigsuspend,
	TP_PROTO(sigset_t * unewset, size_t sigsetsize),
	TP_ARGS(unewset, sigsetsize),
	TP_STRUCT__entry(__field_hex(sigset_t *, unewset) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(unewset, unewset) tp_assign(sigsetsize, sigsetsize)),
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
#ifndef OVERRIDE_64_sys_sched_rr_get_interval
SC_TRACE_EVENT(sys_sched_rr_get_interval,
	TP_PROTO(pid_t pid, struct timespec * interval),
	TP_ARGS(pid, interval),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct timespec *, interval)),
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
#ifndef OVERRIDE_64_sys_mq_notify
SC_TRACE_EVENT(sys_mq_notify,
	TP_PROTO(mqd_t mqdes, const struct sigevent * u_notification),
	TP_ARGS(mqdes, u_notification),
	TP_STRUCT__entry(__field(mqd_t, mqdes) __field_hex(const struct sigevent *, u_notification)),
	TP_fast_assign(tp_assign(mqdes, mqdes) tp_assign(u_notification, u_notification)),
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
#ifndef OVERRIDE_64_sys_pipe2
SC_TRACE_EVENT(sys_pipe2,
	TP_PROTO(int * fildes, int flags),
	TP_ARGS(fildes, flags),
	TP_STRUCT__entry(__field_hex(int *, fildes) __field(int, flags)),
	TP_fast_assign(tp_assign(fildes, fildes) tp_assign(flags, flags)),
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
	TP_PROTO(const char * filename, int flags, int mode),
	TP_ARGS(filename, flags, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(int, flags) __field(int, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(flags, flags) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_poll
SC_TRACE_EVENT(sys_poll,
	TP_PROTO(struct pollfd * ufds, unsigned int nfds, long timeout_msecs),
	TP_ARGS(ufds, nfds, timeout_msecs),
	TP_STRUCT__entry(__field_hex(struct pollfd *, ufds) __field(unsigned int, nfds) __field(long, timeout_msecs)),
	TP_fast_assign(tp_assign(ufds, ufds) tp_assign(nfds, nfds) tp_assign(timeout_msecs, timeout_msecs)),
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
#ifndef OVERRIDE_64_sys_sendmsg
SC_TRACE_EVENT(sys_sendmsg,
	TP_PROTO(int fd, struct msghdr * msg, unsigned flags),
	TP_ARGS(fd, msg, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct msghdr *, msg) __field(unsigned, flags)),
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
	TP_PROTO(uid_t * ruid, uid_t * euid, uid_t * suid),
	TP_ARGS(ruid, euid, suid),
	TP_STRUCT__entry(__field_hex(uid_t *, ruid) __field_hex(uid_t *, euid) __field_hex(uid_t *, suid)),
	TP_fast_assign(tp_assign(ruid, ruid) tp_assign(euid, euid) tp_assign(suid, suid)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_getresgid
SC_TRACE_EVENT(sys_getresgid,
	TP_PROTO(gid_t * rgid, gid_t * egid, gid_t * sgid),
	TP_ARGS(rgid, egid, sgid),
	TP_STRUCT__entry(__field_hex(gid_t *, rgid) __field_hex(gid_t *, egid) __field_hex(gid_t *, sgid)),
	TP_fast_assign(tp_assign(rgid, rgid) tp_assign(egid, egid) tp_assign(sgid, sgid)),
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
#ifndef OVERRIDE_64_sys_mknod
SC_TRACE_EVENT(sys_mknod,
	TP_PROTO(const char * filename, int mode, unsigned dev),
	TP_ARGS(filename, mode, dev),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(int, mode) __field(unsigned, dev)),
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
#ifndef OVERRIDE_64_sys_nfsservctl
SC_TRACE_EVENT(sys_nfsservctl,
	TP_PROTO(int cmd, struct nfsctl_arg * arg, void * res),
	TP_ARGS(cmd, arg, res),
	TP_STRUCT__entry(__field(int, cmd) __field_hex(struct nfsctl_arg *, arg) __field_hex(void *, res)),
	TP_fast_assign(tp_assign(cmd, cmd) tp_assign(arg, arg) tp_assign(res, res)),
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
#ifndef OVERRIDE_64_sys_io_cancel
SC_TRACE_EVENT(sys_io_cancel,
	TP_PROTO(aio_context_t ctx_id, struct iocb * iocb, struct io_event * result),
	TP_ARGS(ctx_id, iocb, result),
	TP_STRUCT__entry(__field(aio_context_t, ctx_id) __field_hex(struct iocb *, iocb) __field_hex(struct io_event *, result)),
	TP_fast_assign(tp_assign(ctx_id, ctx_id) tp_assign(iocb, iocb) tp_assign(result, result)),
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
#ifndef OVERRIDE_64_sys_timer_create
SC_TRACE_EVENT(sys_timer_create,
	TP_PROTO(const clockid_t which_clock, struct sigevent * timer_event_spec, timer_t * created_timer_id),
	TP_ARGS(which_clock, timer_event_spec, created_timer_id),
	TP_STRUCT__entry(__field(const clockid_t, which_clock) __field_hex(struct sigevent *, timer_event_spec) __field_hex(timer_t *, created_timer_id)),
	TP_fast_assign(tp_assign(which_clock, which_clock) tp_assign(timer_event_spec, timer_event_spec) tp_assign(created_timer_id, created_timer_id)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mq_getsetattr
SC_TRACE_EVENT(sys_mq_getsetattr,
	TP_PROTO(mqd_t mqdes, const struct mq_attr * u_mqstat, struct mq_attr * u_omqstat),
	TP_ARGS(mqdes, u_mqstat, u_omqstat),
	TP_STRUCT__entry(__field(mqd_t, mqdes) __field_hex(const struct mq_attr *, u_mqstat) __field_hex(struct mq_attr *, u_omqstat)),
	TP_fast_assign(tp_assign(mqdes, mqdes) tp_assign(u_mqstat, u_mqstat) tp_assign(u_omqstat, u_omqstat)),
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
	TP_PROTO(int dfd, const char * pathname, int mode),
	TP_ARGS(dfd, pathname, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(pathname, pathname) __field(int, mode)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
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
	TP_PROTO(int dfd, const char * filename, mode_t mode),
	TP_ARGS(dfd, filename, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(mode_t, mode)),
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
#ifndef OVERRIDE_64_sys_sendfile64
SC_TRACE_EVENT(sys_sendfile64,
	TP_PROTO(int out_fd, int in_fd, loff_t * offset, size_t count),
	TP_ARGS(out_fd, in_fd, offset, count),
	TP_STRUCT__entry(__field(int, out_fd) __field(int, in_fd) __field_hex(loff_t *, offset) __field(size_t, count)),
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
#ifndef OVERRIDE_64_sys_reboot
SC_TRACE_EVENT(sys_reboot,
	TP_PROTO(int magic1, int magic2, unsigned int cmd, void * arg),
	TP_ARGS(magic1, magic2, cmd, arg),
	TP_STRUCT__entry(__field(int, magic1) __field(int, magic2) __field(unsigned int, cmd) __field_hex(void *, arg)),
	TP_fast_assign(tp_assign(magic1, magic1) tp_assign(magic2, magic2) tp_assign(cmd, cmd) tp_assign(arg, arg)),
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
#ifndef OVERRIDE_64_sys_epoll_wait
SC_TRACE_EVENT(sys_epoll_wait,
	TP_PROTO(int epfd, struct epoll_event * events, int maxevents, int timeout),
	TP_ARGS(epfd, events, maxevents, timeout),
	TP_STRUCT__entry(__field(int, epfd) __field_hex(struct epoll_event *, events) __field(int, maxevents) __field(int, timeout)),
	TP_fast_assign(tp_assign(epfd, epfd) tp_assign(events, events) tp_assign(maxevents, maxevents) tp_assign(timeout, timeout)),
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
#ifndef OVERRIDE_64_sys_mq_open
SC_TRACE_EVENT(sys_mq_open,
	TP_PROTO(const char * u_name, int oflag, mode_t mode, struct mq_attr * u_attr),
	TP_ARGS(u_name, oflag, mode, u_attr),
	TP_STRUCT__entry(__string_from_user(u_name, u_name) __field(int, oflag) __field(mode_t, mode) __field_hex(struct mq_attr *, u_attr)),
	TP_fast_assign(tp_copy_string_from_user(u_name, u_name) tp_assign(oflag, oflag) tp_assign(mode, mode) tp_assign(u_attr, u_attr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_kexec_load
SC_TRACE_EVENT(sys_kexec_load,
	TP_PROTO(unsigned long entry, unsigned long nr_segments, struct kexec_segment * segments, unsigned long flags),
	TP_ARGS(entry, nr_segments, segments, flags),
	TP_STRUCT__entry(__field(unsigned long, entry) __field(unsigned long, nr_segments) __field_hex(struct kexec_segment *, segments) __field(unsigned long, flags)),
	TP_fast_assign(tp_assign(entry, entry) tp_assign(nr_segments, nr_segments) tp_assign(segments, segments) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_openat
SC_TRACE_EVENT(sys_openat,
	TP_PROTO(int dfd, const char * filename, int flags, int mode),
	TP_ARGS(dfd, filename, flags, mode),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(int, flags) __field(int, mode)),
	TP_fast_assign(tp_assign(dfd, dfd) tp_copy_string_from_user(filename, filename) tp_assign(flags, flags) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mknodat
SC_TRACE_EVENT(sys_mknodat,
	TP_PROTO(int dfd, const char * filename, int mode, unsigned dev),
	TP_ARGS(dfd, filename, mode, dev),
	TP_STRUCT__entry(__field(int, dfd) __string_from_user(filename, filename) __field(int, mode) __field(unsigned, dev)),
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
#ifndef OVERRIDE_64_sys_accept4
SC_TRACE_EVENT(sys_accept4,
	TP_PROTO(int fd, struct sockaddr * upeer_sockaddr, int * upeer_addrlen, int flags),
	TP_ARGS(fd, upeer_sockaddr, upeer_addrlen, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, upeer_sockaddr) __field_hex(int *, upeer_addrlen) __field(int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(upeer_sockaddr, upeer_sockaddr) tp_assign(upeer_addrlen, upeer_addrlen) tp_assign(flags, flags)),
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
#ifndef OVERRIDE_64_sys_rt_tgsigqueueinfo
SC_TRACE_EVENT(sys_rt_tgsigqueueinfo,
	TP_PROTO(pid_t tgid, pid_t pid, int sig, siginfo_t * uinfo),
	TP_ARGS(tgid, pid, sig, uinfo),
	TP_STRUCT__entry(__field(pid_t, tgid) __field(pid_t, pid) __field(int, sig) __field_hex(siginfo_t *, uinfo)),
	TP_fast_assign(tp_assign(tgid, tgid) tp_assign(pid, pid) tp_assign(sig, sig) tp_assign(uinfo, uinfo)),
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
#ifndef OVERRIDE_64_sys_sendmmsg
SC_TRACE_EVENT(sys_sendmmsg,
	TP_PROTO(int fd, struct mmsghdr * mmsg, unsigned int vlen, unsigned int flags),
	TP_ARGS(fd, mmsg, vlen, flags),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct mmsghdr *, mmsg) __field(unsigned int, vlen) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(mmsg, mmsg) tp_assign(vlen, vlen) tp_assign(flags, flags)),
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
#ifndef OVERRIDE_64_sys_getsockopt
SC_TRACE_EVENT(sys_getsockopt,
	TP_PROTO(int fd, int level, int optname, char * optval, int * optlen),
	TP_ARGS(fd, level, optname, optval, optlen),
	TP_STRUCT__entry(__field(int, fd) __field(int, level) __field(int, optname) __field_hex(char *, optval) __field_hex(int *, optlen)),
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
#ifndef OVERRIDE_64_sys_io_getevents
SC_TRACE_EVENT(sys_io_getevents,
	TP_PROTO(aio_context_t ctx_id, long min_nr, long nr, struct io_event * events, struct timespec * timeout),
	TP_ARGS(ctx_id, min_nr, nr, events, timeout),
	TP_STRUCT__entry(__field(aio_context_t, ctx_id) __field(long, min_nr) __field(long, nr) __field_hex(struct io_event *, events) __field_hex(struct timespec *, timeout)),
	TP_fast_assign(tp_assign(ctx_id, ctx_id) tp_assign(min_nr, min_nr) tp_assign(nr, nr) tp_assign(events, events) tp_assign(timeout, timeout)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mq_timedsend
SC_TRACE_EVENT(sys_mq_timedsend,
	TP_PROTO(mqd_t mqdes, const char * u_msg_ptr, size_t msg_len, unsigned int msg_prio, const struct timespec * u_abs_timeout),
	TP_ARGS(mqdes, u_msg_ptr, msg_len, msg_prio, u_abs_timeout),
	TP_STRUCT__entry(__field(mqd_t, mqdes) __field_hex(const char *, u_msg_ptr) __field(size_t, msg_len) __field(unsigned int, msg_prio) __field_hex(const struct timespec *, u_abs_timeout)),
	TP_fast_assign(tp_assign(mqdes, mqdes) tp_assign(u_msg_ptr, u_msg_ptr) tp_assign(msg_len, msg_len) tp_assign(msg_prio, msg_prio) tp_assign(u_abs_timeout, u_abs_timeout)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_mq_timedreceive
SC_TRACE_EVENT(sys_mq_timedreceive,
	TP_PROTO(mqd_t mqdes, char * u_msg_ptr, size_t msg_len, unsigned int * u_msg_prio, const struct timespec * u_abs_timeout),
	TP_ARGS(mqdes, u_msg_ptr, msg_len, u_msg_prio, u_abs_timeout),
	TP_STRUCT__entry(__field(mqd_t, mqdes) __field_hex(char *, u_msg_ptr) __field(size_t, msg_len) __field_hex(unsigned int *, u_msg_prio) __field_hex(const struct timespec *, u_abs_timeout)),
	TP_fast_assign(tp_assign(mqdes, mqdes) tp_assign(u_msg_ptr, u_msg_ptr) tp_assign(msg_len, msg_len) tp_assign(u_msg_prio, u_msg_prio) tp_assign(u_abs_timeout, u_abs_timeout)),
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
#ifndef OVERRIDE_64_sys_ppoll
SC_TRACE_EVENT(sys_ppoll,
	TP_PROTO(struct pollfd * ufds, unsigned int nfds, struct timespec * tsp, const sigset_t * sigmask, size_t sigsetsize),
	TP_ARGS(ufds, nfds, tsp, sigmask, sigsetsize),
	TP_STRUCT__entry(__field_hex(struct pollfd *, ufds) __field(unsigned int, nfds) __field_hex(struct timespec *, tsp) __field_hex(const sigset_t *, sigmask) __field(size_t, sigsetsize)),
	TP_fast_assign(tp_assign(ufds, ufds) tp_assign(nfds, nfds) tp_assign(tsp, tsp) tp_assign(sigmask, sigmask) tp_assign(sigsetsize, sigsetsize)),
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
#ifndef OVERRIDE_64_sys_perf_event_open
SC_TRACE_EVENT(sys_perf_event_open,
	TP_PROTO(struct perf_event_attr * attr_uptr, pid_t pid, int cpu, int group_fd, unsigned long flags),
	TP_ARGS(attr_uptr, pid, cpu, group_fd, flags),
	TP_STRUCT__entry(__field_hex(struct perf_event_attr *, attr_uptr) __field(pid_t, pid) __field(int, cpu) __field(int, group_fd) __field(unsigned long, flags)),
	TP_fast_assign(tp_assign(attr_uptr, attr_uptr) tp_assign(pid, pid) tp_assign(cpu, cpu) tp_assign(group_fd, group_fd) tp_assign(flags, flags)),
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
#ifndef OVERRIDE_64_sys_sendto
SC_TRACE_EVENT(sys_sendto,
	TP_PROTO(int fd, void * buff, size_t len, unsigned flags, struct sockaddr * addr, int addr_len),
	TP_ARGS(fd, buff, len, flags, addr, addr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(void *, buff) __field(size_t, len) __field(unsigned, flags) __field_hex(struct sockaddr *, addr) __field_hex(int, addr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buff, buff) tp_assign(len, len) tp_assign(flags, flags) tp_assign(addr, addr) tp_assign(addr_len, addr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_64_sys_recvfrom
SC_TRACE_EVENT(sys_recvfrom,
	TP_PROTO(int fd, void * ubuf, size_t size, unsigned flags, struct sockaddr * addr, int * addr_len),
	TP_ARGS(fd, ubuf, size, flags, addr, addr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(void *, ubuf) __field(size_t, size) __field(unsigned, flags) __field_hex(struct sockaddr *, addr) __field_hex(int *, addr_len)),
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
#ifndef OVERRIDE_64_sys_splice
SC_TRACE_EVENT(sys_splice,
	TP_PROTO(int fd_in, loff_t * off_in, int fd_out, loff_t * off_out, size_t len, unsigned int flags),
	TP_ARGS(fd_in, off_in, fd_out, off_out, len, flags),
	TP_STRUCT__entry(__field(int, fd_in) __field_hex(loff_t *, off_in) __field(int, fd_out) __field_hex(loff_t *, off_out) __field(size_t, len) __field(unsigned int, flags)),
	TP_fast_assign(tp_assign(fd_in, fd_in) tp_assign(off_in, off_in) tp_assign(fd_out, fd_out) tp_assign(off_out, off_out) tp_assign(len, len) tp_assign(flags, flags)),
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

#endif /*  _TRACE_SYSCALLS_POINTERS_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

#else /* CREATE_SYSCALL_TABLE */

#include "x86-64-syscalls-3.0.4_pointers_override.h"
#include "syscalls_pointers_override.h"

#ifndef OVERRIDE_TABLE_64_sys_read
TRACE_SYSCALL_TABLE(sys_read, sys_read, 0, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_write
TRACE_SYSCALL_TABLE(sys_write, sys_write, 1, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_open
TRACE_SYSCALL_TABLE(sys_open, sys_open, 2, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newstat
TRACE_SYSCALL_TABLE(sys_newstat, sys_newstat, 4, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newfstat
TRACE_SYSCALL_TABLE(sys_newfstat, sys_newfstat, 5, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newlstat
TRACE_SYSCALL_TABLE(sys_newlstat, sys_newlstat, 6, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_poll
TRACE_SYSCALL_TABLE(sys_poll, sys_poll, 7, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigaction
TRACE_SYSCALL_TABLE(sys_rt_sigaction, sys_rt_sigaction, 13, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigprocmask
TRACE_SYSCALL_TABLE(sys_rt_sigprocmask, sys_rt_sigprocmask, 14, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_readv
TRACE_SYSCALL_TABLE(sys_readv, sys_readv, 19, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_writev
TRACE_SYSCALL_TABLE(sys_writev, sys_writev, 20, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_access
TRACE_SYSCALL_TABLE(sys_access, sys_access, 21, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pipe
TRACE_SYSCALL_TABLE(sys_pipe, sys_pipe, 22, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_select
TRACE_SYSCALL_TABLE(sys_select, sys_select, 23, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mincore
TRACE_SYSCALL_TABLE(sys_mincore, sys_mincore, 27, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_shmat
TRACE_SYSCALL_TABLE(sys_shmat, sys_shmat, 30, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_shmctl
TRACE_SYSCALL_TABLE(sys_shmctl, sys_shmctl, 31, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_nanosleep
TRACE_SYSCALL_TABLE(sys_nanosleep, sys_nanosleep, 35, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getitimer
TRACE_SYSCALL_TABLE(sys_getitimer, sys_getitimer, 36, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setitimer
TRACE_SYSCALL_TABLE(sys_setitimer, sys_setitimer, 38, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendfile64
TRACE_SYSCALL_TABLE(sys_sendfile64, sys_sendfile64, 40, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_connect
TRACE_SYSCALL_TABLE(sys_connect, sys_connect, 42, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_accept
TRACE_SYSCALL_TABLE(sys_accept, sys_accept, 43, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendto
TRACE_SYSCALL_TABLE(sys_sendto, sys_sendto, 44, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_recvfrom
TRACE_SYSCALL_TABLE(sys_recvfrom, sys_recvfrom, 45, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendmsg
TRACE_SYSCALL_TABLE(sys_sendmsg, sys_sendmsg, 46, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_recvmsg
TRACE_SYSCALL_TABLE(sys_recvmsg, sys_recvmsg, 47, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_bind
TRACE_SYSCALL_TABLE(sys_bind, sys_bind, 49, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getsockname
TRACE_SYSCALL_TABLE(sys_getsockname, sys_getsockname, 51, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getpeername
TRACE_SYSCALL_TABLE(sys_getpeername, sys_getpeername, 52, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_socketpair
TRACE_SYSCALL_TABLE(sys_socketpair, sys_socketpair, 53, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setsockopt
TRACE_SYSCALL_TABLE(sys_setsockopt, sys_setsockopt, 54, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getsockopt
TRACE_SYSCALL_TABLE(sys_getsockopt, sys_getsockopt, 55, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_wait4
TRACE_SYSCALL_TABLE(sys_wait4, sys_wait4, 61, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newuname
TRACE_SYSCALL_TABLE(sys_newuname, sys_newuname, 63, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_semop
TRACE_SYSCALL_TABLE(sys_semop, sys_semop, 65, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_shmdt
TRACE_SYSCALL_TABLE(sys_shmdt, sys_shmdt, 67, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_msgsnd
TRACE_SYSCALL_TABLE(sys_msgsnd, sys_msgsnd, 69, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_msgrcv
TRACE_SYSCALL_TABLE(sys_msgrcv, sys_msgrcv, 70, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_msgctl
TRACE_SYSCALL_TABLE(sys_msgctl, sys_msgctl, 71, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_truncate
TRACE_SYSCALL_TABLE(sys_truncate, sys_truncate, 76, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getdents
TRACE_SYSCALL_TABLE(sys_getdents, sys_getdents, 78, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getcwd
TRACE_SYSCALL_TABLE(sys_getcwd, sys_getcwd, 79, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chdir
TRACE_SYSCALL_TABLE(sys_chdir, sys_chdir, 80, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rename
TRACE_SYSCALL_TABLE(sys_rename, sys_rename, 82, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mkdir
TRACE_SYSCALL_TABLE(sys_mkdir, sys_mkdir, 83, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rmdir
TRACE_SYSCALL_TABLE(sys_rmdir, sys_rmdir, 84, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_creat
TRACE_SYSCALL_TABLE(sys_creat, sys_creat, 85, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_link
TRACE_SYSCALL_TABLE(sys_link, sys_link, 86, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_unlink
TRACE_SYSCALL_TABLE(sys_unlink, sys_unlink, 87, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_symlink
TRACE_SYSCALL_TABLE(sys_symlink, sys_symlink, 88, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_readlink
TRACE_SYSCALL_TABLE(sys_readlink, sys_readlink, 89, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chmod
TRACE_SYSCALL_TABLE(sys_chmod, sys_chmod, 90, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chown
TRACE_SYSCALL_TABLE(sys_chown, sys_chown, 92, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lchown
TRACE_SYSCALL_TABLE(sys_lchown, sys_lchown, 94, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_gettimeofday
TRACE_SYSCALL_TABLE(sys_gettimeofday, sys_gettimeofday, 96, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getrlimit
TRACE_SYSCALL_TABLE(sys_getrlimit, sys_getrlimit, 97, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getrusage
TRACE_SYSCALL_TABLE(sys_getrusage, sys_getrusage, 98, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sysinfo
TRACE_SYSCALL_TABLE(sys_sysinfo, sys_sysinfo, 99, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_times
TRACE_SYSCALL_TABLE(sys_times, sys_times, 100, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_syslog
TRACE_SYSCALL_TABLE(sys_syslog, sys_syslog, 103, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getgroups
TRACE_SYSCALL_TABLE(sys_getgroups, sys_getgroups, 115, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setgroups
TRACE_SYSCALL_TABLE(sys_setgroups, sys_setgroups, 116, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getresuid
TRACE_SYSCALL_TABLE(sys_getresuid, sys_getresuid, 118, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getresgid
TRACE_SYSCALL_TABLE(sys_getresgid, sys_getresgid, 120, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigpending
TRACE_SYSCALL_TABLE(sys_rt_sigpending, sys_rt_sigpending, 127, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigtimedwait
TRACE_SYSCALL_TABLE(sys_rt_sigtimedwait, sys_rt_sigtimedwait, 128, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigqueueinfo
TRACE_SYSCALL_TABLE(sys_rt_sigqueueinfo, sys_rt_sigqueueinfo, 129, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_sigsuspend
TRACE_SYSCALL_TABLE(sys_rt_sigsuspend, sys_rt_sigsuspend, 130, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_utime
TRACE_SYSCALL_TABLE(sys_utime, sys_utime, 132, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mknod
TRACE_SYSCALL_TABLE(sys_mknod, sys_mknod, 133, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_ustat
TRACE_SYSCALL_TABLE(sys_ustat, sys_ustat, 136, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_statfs
TRACE_SYSCALL_TABLE(sys_statfs, sys_statfs, 137, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fstatfs
TRACE_SYSCALL_TABLE(sys_fstatfs, sys_fstatfs, 138, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_setparam
TRACE_SYSCALL_TABLE(sys_sched_setparam, sys_sched_setparam, 142, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_getparam
TRACE_SYSCALL_TABLE(sys_sched_getparam, sys_sched_getparam, 143, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_setscheduler
TRACE_SYSCALL_TABLE(sys_sched_setscheduler, sys_sched_setscheduler, 144, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_rr_get_interval
TRACE_SYSCALL_TABLE(sys_sched_rr_get_interval, sys_sched_rr_get_interval, 148, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pivot_root
TRACE_SYSCALL_TABLE(sys_pivot_root, sys_pivot_root, 155, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sysctl
TRACE_SYSCALL_TABLE(sys_sysctl, sys_sysctl, 156, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_adjtimex
TRACE_SYSCALL_TABLE(sys_adjtimex, sys_adjtimex, 159, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setrlimit
TRACE_SYSCALL_TABLE(sys_setrlimit, sys_setrlimit, 160, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_chroot
TRACE_SYSCALL_TABLE(sys_chroot, sys_chroot, 161, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_settimeofday
TRACE_SYSCALL_TABLE(sys_settimeofday, sys_settimeofday, 164, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mount
TRACE_SYSCALL_TABLE(sys_mount, sys_mount, 165, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_umount
TRACE_SYSCALL_TABLE(sys_umount, sys_umount, 166, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_swapon
TRACE_SYSCALL_TABLE(sys_swapon, sys_swapon, 167, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_swapoff
TRACE_SYSCALL_TABLE(sys_swapoff, sys_swapoff, 168, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_reboot
TRACE_SYSCALL_TABLE(sys_reboot, sys_reboot, 169, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sethostname
TRACE_SYSCALL_TABLE(sys_sethostname, sys_sethostname, 170, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setdomainname
TRACE_SYSCALL_TABLE(sys_setdomainname, sys_setdomainname, 171, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_init_module
TRACE_SYSCALL_TABLE(sys_init_module, sys_init_module, 175, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_delete_module
TRACE_SYSCALL_TABLE(sys_delete_module, sys_delete_module, 176, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_nfsservctl
TRACE_SYSCALL_TABLE(sys_nfsservctl, sys_nfsservctl, 180, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_setxattr
TRACE_SYSCALL_TABLE(sys_setxattr, sys_setxattr, 188, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lsetxattr
TRACE_SYSCALL_TABLE(sys_lsetxattr, sys_lsetxattr, 189, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fsetxattr
TRACE_SYSCALL_TABLE(sys_fsetxattr, sys_fsetxattr, 190, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getxattr
TRACE_SYSCALL_TABLE(sys_getxattr, sys_getxattr, 191, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lgetxattr
TRACE_SYSCALL_TABLE(sys_lgetxattr, sys_lgetxattr, 192, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fgetxattr
TRACE_SYSCALL_TABLE(sys_fgetxattr, sys_fgetxattr, 193, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_listxattr
TRACE_SYSCALL_TABLE(sys_listxattr, sys_listxattr, 194, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_llistxattr
TRACE_SYSCALL_TABLE(sys_llistxattr, sys_llistxattr, 195, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_flistxattr
TRACE_SYSCALL_TABLE(sys_flistxattr, sys_flistxattr, 196, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_removexattr
TRACE_SYSCALL_TABLE(sys_removexattr, sys_removexattr, 197, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_lremovexattr
TRACE_SYSCALL_TABLE(sys_lremovexattr, sys_lremovexattr, 198, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fremovexattr
TRACE_SYSCALL_TABLE(sys_fremovexattr, sys_fremovexattr, 199, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_time
TRACE_SYSCALL_TABLE(sys_time, sys_time, 201, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_futex
TRACE_SYSCALL_TABLE(sys_futex, sys_futex, 202, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_setaffinity
TRACE_SYSCALL_TABLE(sys_sched_setaffinity, sys_sched_setaffinity, 203, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sched_getaffinity
TRACE_SYSCALL_TABLE(sys_sched_getaffinity, sys_sched_getaffinity, 204, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_setup
TRACE_SYSCALL_TABLE(sys_io_setup, sys_io_setup, 206, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_getevents
TRACE_SYSCALL_TABLE(sys_io_getevents, sys_io_getevents, 208, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_submit
TRACE_SYSCALL_TABLE(sys_io_submit, sys_io_submit, 209, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_io_cancel
TRACE_SYSCALL_TABLE(sys_io_cancel, sys_io_cancel, 210, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_getdents64
TRACE_SYSCALL_TABLE(sys_getdents64, sys_getdents64, 217, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_set_tid_address
TRACE_SYSCALL_TABLE(sys_set_tid_address, sys_set_tid_address, 218, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_semtimedop
TRACE_SYSCALL_TABLE(sys_semtimedop, sys_semtimedop, 220, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timer_create
TRACE_SYSCALL_TABLE(sys_timer_create, sys_timer_create, 222, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timer_settime
TRACE_SYSCALL_TABLE(sys_timer_settime, sys_timer_settime, 223, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timer_gettime
TRACE_SYSCALL_TABLE(sys_timer_gettime, sys_timer_gettime, 224, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_settime
TRACE_SYSCALL_TABLE(sys_clock_settime, sys_clock_settime, 227, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_gettime
TRACE_SYSCALL_TABLE(sys_clock_gettime, sys_clock_gettime, 228, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_getres
TRACE_SYSCALL_TABLE(sys_clock_getres, sys_clock_getres, 229, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_nanosleep
TRACE_SYSCALL_TABLE(sys_clock_nanosleep, sys_clock_nanosleep, 230, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_epoll_wait
TRACE_SYSCALL_TABLE(sys_epoll_wait, sys_epoll_wait, 232, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_epoll_ctl
TRACE_SYSCALL_TABLE(sys_epoll_ctl, sys_epoll_ctl, 233, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_utimes
TRACE_SYSCALL_TABLE(sys_utimes, sys_utimes, 235, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mq_open
TRACE_SYSCALL_TABLE(sys_mq_open, sys_mq_open, 240, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mq_unlink
TRACE_SYSCALL_TABLE(sys_mq_unlink, sys_mq_unlink, 241, 1)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mq_timedsend
TRACE_SYSCALL_TABLE(sys_mq_timedsend, sys_mq_timedsend, 242, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mq_timedreceive
TRACE_SYSCALL_TABLE(sys_mq_timedreceive, sys_mq_timedreceive, 243, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mq_notify
TRACE_SYSCALL_TABLE(sys_mq_notify, sys_mq_notify, 244, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mq_getsetattr
TRACE_SYSCALL_TABLE(sys_mq_getsetattr, sys_mq_getsetattr, 245, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_kexec_load
TRACE_SYSCALL_TABLE(sys_kexec_load, sys_kexec_load, 246, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_waitid
TRACE_SYSCALL_TABLE(sys_waitid, sys_waitid, 247, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_inotify_add_watch
TRACE_SYSCALL_TABLE(sys_inotify_add_watch, sys_inotify_add_watch, 254, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_openat
TRACE_SYSCALL_TABLE(sys_openat, sys_openat, 257, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mkdirat
TRACE_SYSCALL_TABLE(sys_mkdirat, sys_mkdirat, 258, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_mknodat
TRACE_SYSCALL_TABLE(sys_mknodat, sys_mknodat, 259, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fchownat
TRACE_SYSCALL_TABLE(sys_fchownat, sys_fchownat, 260, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_futimesat
TRACE_SYSCALL_TABLE(sys_futimesat, sys_futimesat, 261, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_newfstatat
TRACE_SYSCALL_TABLE(sys_newfstatat, sys_newfstatat, 262, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_unlinkat
TRACE_SYSCALL_TABLE(sys_unlinkat, sys_unlinkat, 263, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_renameat
TRACE_SYSCALL_TABLE(sys_renameat, sys_renameat, 264, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_linkat
TRACE_SYSCALL_TABLE(sys_linkat, sys_linkat, 265, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_symlinkat
TRACE_SYSCALL_TABLE(sys_symlinkat, sys_symlinkat, 266, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_readlinkat
TRACE_SYSCALL_TABLE(sys_readlinkat, sys_readlinkat, 267, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_fchmodat
TRACE_SYSCALL_TABLE(sys_fchmodat, sys_fchmodat, 268, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_faccessat
TRACE_SYSCALL_TABLE(sys_faccessat, sys_faccessat, 269, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pselect6
TRACE_SYSCALL_TABLE(sys_pselect6, sys_pselect6, 270, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_ppoll
TRACE_SYSCALL_TABLE(sys_ppoll, sys_ppoll, 271, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_set_robust_list
TRACE_SYSCALL_TABLE(sys_set_robust_list, sys_set_robust_list, 273, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_get_robust_list
TRACE_SYSCALL_TABLE(sys_get_robust_list, sys_get_robust_list, 274, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_splice
TRACE_SYSCALL_TABLE(sys_splice, sys_splice, 275, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_vmsplice
TRACE_SYSCALL_TABLE(sys_vmsplice, sys_vmsplice, 278, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_utimensat
TRACE_SYSCALL_TABLE(sys_utimensat, sys_utimensat, 280, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_epoll_pwait
TRACE_SYSCALL_TABLE(sys_epoll_pwait, sys_epoll_pwait, 281, 6)
#endif
#ifndef OVERRIDE_TABLE_64_sys_signalfd
TRACE_SYSCALL_TABLE(sys_signalfd, sys_signalfd, 282, 3)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timerfd_settime
TRACE_SYSCALL_TABLE(sys_timerfd_settime, sys_timerfd_settime, 286, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_timerfd_gettime
TRACE_SYSCALL_TABLE(sys_timerfd_gettime, sys_timerfd_gettime, 287, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_accept4
TRACE_SYSCALL_TABLE(sys_accept4, sys_accept4, 288, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_signalfd4
TRACE_SYSCALL_TABLE(sys_signalfd4, sys_signalfd4, 289, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pipe2
TRACE_SYSCALL_TABLE(sys_pipe2, sys_pipe2, 293, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_preadv
TRACE_SYSCALL_TABLE(sys_preadv, sys_preadv, 295, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_pwritev
TRACE_SYSCALL_TABLE(sys_pwritev, sys_pwritev, 296, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_rt_tgsigqueueinfo
TRACE_SYSCALL_TABLE(sys_rt_tgsigqueueinfo, sys_rt_tgsigqueueinfo, 297, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_perf_event_open
TRACE_SYSCALL_TABLE(sys_perf_event_open, sys_perf_event_open, 298, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_recvmmsg
TRACE_SYSCALL_TABLE(sys_recvmmsg, sys_recvmmsg, 299, 5)
#endif
#ifndef OVERRIDE_TABLE_64_sys_prlimit64
TRACE_SYSCALL_TABLE(sys_prlimit64, sys_prlimit64, 302, 4)
#endif
#ifndef OVERRIDE_TABLE_64_sys_clock_adjtime
TRACE_SYSCALL_TABLE(sys_clock_adjtime, sys_clock_adjtime, 305, 2)
#endif
#ifndef OVERRIDE_TABLE_64_sys_sendmmsg
TRACE_SYSCALL_TABLE(sys_sendmmsg, sys_sendmmsg, 307, 4)
#endif

#endif /* CREATE_SYSCALL_TABLE */
