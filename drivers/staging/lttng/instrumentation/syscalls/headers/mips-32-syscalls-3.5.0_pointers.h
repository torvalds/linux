/* THIS FILE IS AUTO-GENERATED. DO NOT EDIT */
#ifndef CREATE_SYSCALL_TABLE

#if !defined(_TRACE_SYSCALLS_POINTERS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYSCALLS_POINTERS_H

#include <linux/tracepoint.h>
#include <linux/syscalls.h>
#include "mips-32-syscalls-3.5.0_pointers_override.h"
#include "syscalls_pointers_override.h"

#ifndef OVERRIDE_32_sys_unlink
SC_TRACE_EVENT(sys_unlink,
	TP_PROTO(const char * pathname),
	TP_ARGS(pathname),
	TP_STRUCT__entry(__string_from_user(pathname, pathname)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_chdir
SC_TRACE_EVENT(sys_chdir,
	TP_PROTO(const char * filename),
	TP_ARGS(filename),
	TP_STRUCT__entry(__string_from_user(filename, filename)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_time
SC_TRACE_EVENT(sys_time,
	TP_PROTO(time_t * tloc),
	TP_ARGS(tloc),
	TP_STRUCT__entry(__field_hex(time_t *, tloc)),
	TP_fast_assign(tp_assign(tloc, tloc)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_oldumount
SC_TRACE_EVENT(sys_oldumount,
	TP_PROTO(char * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__string_from_user(name, name)),
	TP_fast_assign(tp_copy_string_from_user(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_stime
SC_TRACE_EVENT(sys_stime,
	TP_PROTO(time_t * tptr),
	TP_ARGS(tptr),
	TP_STRUCT__entry(__field_hex(time_t *, tptr)),
	TP_fast_assign(tp_assign(tptr, tptr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_rmdir
SC_TRACE_EVENT(sys_rmdir,
	TP_PROTO(const char * pathname),
	TP_ARGS(pathname),
	TP_STRUCT__entry(__string_from_user(pathname, pathname)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_times
SC_TRACE_EVENT(sys_times,
	TP_PROTO(struct tms * tbuf),
	TP_ARGS(tbuf),
	TP_STRUCT__entry(__field_hex(struct tms *, tbuf)),
	TP_fast_assign(tp_assign(tbuf, tbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_olduname
SC_TRACE_EVENT(sys_olduname,
	TP_PROTO(struct oldold_utsname * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field_hex(struct oldold_utsname *, name)),
	TP_fast_assign(tp_assign(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_chroot
SC_TRACE_EVENT(sys_chroot,
	TP_PROTO(const char * filename),
	TP_ARGS(filename),
	TP_STRUCT__entry(__string_from_user(filename, filename)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sigpending
SC_TRACE_EVENT(sys_sigpending,
	TP_PROTO(old_sigset_t * set),
	TP_ARGS(set),
	TP_STRUCT__entry(__field_hex(old_sigset_t *, set)),
	TP_fast_assign(tp_assign(set, set)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_uselib
SC_TRACE_EVENT(sys_uselib,
	TP_PROTO(const char * library),
	TP_ARGS(library),
	TP_STRUCT__entry(__field_hex(const char *, library)),
	TP_fast_assign(tp_assign(library, library)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_uname
SC_TRACE_EVENT(sys_uname,
	TP_PROTO(struct old_utsname * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field_hex(struct old_utsname *, name)),
	TP_fast_assign(tp_assign(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_swapoff
SC_TRACE_EVENT(sys_swapoff,
	TP_PROTO(const char * specialfile),
	TP_ARGS(specialfile),
	TP_STRUCT__entry(__string_from_user(specialfile, specialfile)),
	TP_fast_assign(tp_copy_string_from_user(specialfile, specialfile)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sysinfo
SC_TRACE_EVENT(sys_sysinfo,
	TP_PROTO(struct sysinfo * info),
	TP_ARGS(info),
	TP_STRUCT__entry(__field_hex(struct sysinfo *, info)),
	TP_fast_assign(tp_assign(info, info)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_newuname
SC_TRACE_EVENT(sys_newuname,
	TP_PROTO(struct new_utsname * name),
	TP_ARGS(name),
	TP_STRUCT__entry(__field_hex(struct new_utsname *, name)),
	TP_fast_assign(tp_assign(name, name)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_adjtimex
SC_TRACE_EVENT(sys_adjtimex,
	TP_PROTO(struct timex * txc_p),
	TP_ARGS(txc_p),
	TP_STRUCT__entry(__field_hex(struct timex *, txc_p)),
	TP_fast_assign(tp_assign(txc_p, txc_p)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sysctl
SC_TRACE_EVENT(sys_sysctl,
	TP_PROTO(struct __sysctl_args * args),
	TP_ARGS(args),
	TP_STRUCT__entry(__field_hex(struct __sysctl_args *, args)),
	TP_fast_assign(tp_assign(args, args)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_creat
SC_TRACE_EVENT(sys_creat,
	TP_PROTO(const char * pathname, umode_t mode),
	TP_ARGS(pathname, mode),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_link
SC_TRACE_EVENT(sys_link,
	TP_PROTO(const char * oldname, const char * newname),
	TP_ARGS(oldname, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_chmod
SC_TRACE_EVENT(sys_chmod,
	TP_PROTO(const char * filename, umode_t mode),
	TP_ARGS(filename, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_utime
SC_TRACE_EVENT(sys_utime,
	TP_PROTO(char * filename, struct utimbuf * times),
	TP_ARGS(filename, times),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct utimbuf *, times)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(times, times)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_access
SC_TRACE_EVENT(sys_access,
	TP_PROTO(const char * filename, int mode),
	TP_ARGS(filename, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(int, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_rename
SC_TRACE_EVENT(sys_rename,
	TP_PROTO(const char * oldname, const char * newname),
	TP_ARGS(oldname, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mkdir
SC_TRACE_EVENT(sys_mkdir,
	TP_PROTO(const char * pathname, umode_t mode),
	TP_ARGS(pathname, mode),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_umount
SC_TRACE_EVENT(sys_umount,
	TP_PROTO(char * name, int flags),
	TP_ARGS(name, flags),
	TP_STRUCT__entry(__string_from_user(name, name) __field(int, flags)),
	TP_fast_assign(tp_copy_string_from_user(name, name) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_ustat
SC_TRACE_EVENT(sys_ustat,
	TP_PROTO(unsigned dev, struct ustat * ubuf),
	TP_ARGS(dev, ubuf),
	TP_STRUCT__entry(__field(unsigned, dev) __field_hex(struct ustat *, ubuf)),
	TP_fast_assign(tp_assign(dev, dev) tp_assign(ubuf, ubuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sethostname
SC_TRACE_EVENT(sys_sethostname,
	TP_PROTO(char * name, int len),
	TP_ARGS(name, len),
	TP_STRUCT__entry(__string_from_user(name, name) __field(int, len)),
	TP_fast_assign(tp_copy_string_from_user(name, name) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setrlimit
SC_TRACE_EVENT(sys_setrlimit,
	TP_PROTO(unsigned int resource, struct rlimit * rlim),
	TP_ARGS(resource, rlim),
	TP_STRUCT__entry(__field(unsigned int, resource) __field_hex(struct rlimit *, rlim)),
	TP_fast_assign(tp_assign(resource, resource) tp_assign(rlim, rlim)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getrlimit
SC_TRACE_EVENT(sys_getrlimit,
	TP_PROTO(unsigned int resource, struct rlimit * rlim),
	TP_ARGS(resource, rlim),
	TP_STRUCT__entry(__field(unsigned int, resource) __field_hex(struct rlimit *, rlim)),
	TP_fast_assign(tp_assign(resource, resource) tp_assign(rlim, rlim)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getrusage
SC_TRACE_EVENT(sys_getrusage,
	TP_PROTO(int who, struct rusage * ru),
	TP_ARGS(who, ru),
	TP_STRUCT__entry(__field(int, who) __field_hex(struct rusage *, ru)),
	TP_fast_assign(tp_assign(who, who) tp_assign(ru, ru)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_gettimeofday
SC_TRACE_EVENT(sys_gettimeofday,
	TP_PROTO(struct timeval * tv, struct timezone * tz),
	TP_ARGS(tv, tz),
	TP_STRUCT__entry(__field_hex(struct timeval *, tv) __field_hex(struct timezone *, tz)),
	TP_fast_assign(tp_assign(tv, tv) tp_assign(tz, tz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_settimeofday
SC_TRACE_EVENT(sys_settimeofday,
	TP_PROTO(struct timeval * tv, struct timezone * tz),
	TP_ARGS(tv, tz),
	TP_STRUCT__entry(__field_hex(struct timeval *, tv) __field_hex(struct timezone *, tz)),
	TP_fast_assign(tp_assign(tv, tv) tp_assign(tz, tz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getgroups
SC_TRACE_EVENT(sys_getgroups,
	TP_PROTO(int gidsetsize, gid_t * grouplist),
	TP_ARGS(gidsetsize, grouplist),
	TP_STRUCT__entry(__field(int, gidsetsize) __field_hex(gid_t *, grouplist)),
	TP_fast_assign(tp_assign(gidsetsize, gidsetsize) tp_assign(grouplist, grouplist)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setgroups
SC_TRACE_EVENT(sys_setgroups,
	TP_PROTO(int gidsetsize, gid_t * grouplist),
	TP_ARGS(gidsetsize, grouplist),
	TP_STRUCT__entry(__field(int, gidsetsize) __field_hex(gid_t *, grouplist)),
	TP_fast_assign(tp_assign(gidsetsize, gidsetsize) tp_assign(grouplist, grouplist)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_symlink
SC_TRACE_EVENT(sys_symlink,
	TP_PROTO(const char * oldname, const char * newname),
	TP_ARGS(oldname, newname),
	TP_STRUCT__entry(__string_from_user(oldname, oldname) __string_from_user(newname, newname)),
	TP_fast_assign(tp_copy_string_from_user(oldname, oldname) tp_copy_string_from_user(newname, newname)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_swapon
SC_TRACE_EVENT(sys_swapon,
	TP_PROTO(const char * specialfile, int swap_flags),
	TP_ARGS(specialfile, swap_flags),
	TP_STRUCT__entry(__string_from_user(specialfile, specialfile) __field(int, swap_flags)),
	TP_fast_assign(tp_copy_string_from_user(specialfile, specialfile) tp_assign(swap_flags, swap_flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_truncate
SC_TRACE_EVENT(sys_truncate,
	TP_PROTO(const char * path, long length),
	TP_ARGS(path, length),
	TP_STRUCT__entry(__string_from_user(path, path) __field(long, length)),
	TP_fast_assign(tp_copy_string_from_user(path, path) tp_assign(length, length)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_statfs
SC_TRACE_EVENT(sys_statfs,
	TP_PROTO(const char * pathname, struct statfs * buf),
	TP_ARGS(pathname, buf),
	TP_STRUCT__entry(__string_from_user(pathname, pathname) __field_hex(struct statfs *, buf)),
	TP_fast_assign(tp_copy_string_from_user(pathname, pathname) tp_assign(buf, buf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_fstatfs
SC_TRACE_EVENT(sys_fstatfs,
	TP_PROTO(unsigned int fd, struct statfs * buf),
	TP_ARGS(fd, buf),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct statfs *, buf)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_socketcall
SC_TRACE_EVENT(sys_socketcall,
	TP_PROTO(int call, unsigned long * args),
	TP_ARGS(call, args),
	TP_STRUCT__entry(__field(int, call) __field_hex(unsigned long *, args)),
	TP_fast_assign(tp_assign(call, call) tp_assign(args, args)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getitimer
SC_TRACE_EVENT(sys_getitimer,
	TP_PROTO(int which, struct itimerval * value),
	TP_ARGS(which, value),
	TP_STRUCT__entry(__field(int, which) __field_hex(struct itimerval *, value)),
	TP_fast_assign(tp_assign(which, which) tp_assign(value, value)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_newstat
SC_TRACE_EVENT(sys_newstat,
	TP_PROTO(const char * filename, struct stat * statbuf),
	TP_ARGS(filename, statbuf),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct stat *, statbuf)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(statbuf, statbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_newlstat
SC_TRACE_EVENT(sys_newlstat,
	TP_PROTO(const char * filename, struct stat * statbuf),
	TP_ARGS(filename, statbuf),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field_hex(struct stat *, statbuf)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(statbuf, statbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_newfstat
SC_TRACE_EVENT(sys_newfstat,
	TP_PROTO(unsigned int fd, struct stat * statbuf),
	TP_ARGS(fd, statbuf),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct stat *, statbuf)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(statbuf, statbuf)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setdomainname
SC_TRACE_EVENT(sys_setdomainname,
	TP_PROTO(char * name, int len),
	TP_ARGS(name, len),
	TP_STRUCT__entry(__string_from_user(name, name) __field(int, len)),
	TP_fast_assign(tp_copy_string_from_user(name, name) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_delete_module
SC_TRACE_EVENT(sys_delete_module,
	TP_PROTO(const char * name_user, unsigned int flags),
	TP_ARGS(name_user, flags),
	TP_STRUCT__entry(__string_from_user(name_user, name_user) __field(unsigned int, flags)),
	TP_fast_assign(tp_copy_string_from_user(name_user, name_user) tp_assign(flags, flags)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_setparam
SC_TRACE_EVENT(sys_sched_setparam,
	TP_PROTO(pid_t pid, struct sched_param * param),
	TP_ARGS(pid, param),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct sched_param *, param)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(param, param)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_getparam
SC_TRACE_EVENT(sys_sched_getparam,
	TP_PROTO(pid_t pid, struct sched_param * param),
	TP_ARGS(pid, param),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct sched_param *, param)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(param, param)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_rr_get_interval
SC_TRACE_EVENT(sys_sched_rr_get_interval,
	TP_PROTO(pid_t pid, struct timespec * interval),
	TP_ARGS(pid, interval),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(struct timespec *, interval)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(interval, interval)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_nanosleep
SC_TRACE_EVENT(sys_nanosleep,
	TP_PROTO(struct timespec * rqtp, struct timespec * rmtp),
	TP_ARGS(rqtp, rmtp),
	TP_STRUCT__entry(__field_hex(struct timespec *, rqtp) __field_hex(struct timespec *, rmtp)),
	TP_fast_assign(tp_assign(rqtp, rqtp) tp_assign(rmtp, rmtp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_read
SC_TRACE_EVENT(sys_read,
	TP_PROTO(unsigned int fd, char * buf, size_t count),
	TP_ARGS(fd, buf, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(char *, buf) __field(size_t, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_write
SC_TRACE_EVENT(sys_write,
	TP_PROTO(unsigned int fd, const char * buf, size_t count),
	TP_ARGS(fd, buf, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(const char *, buf) __field(size_t, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(buf, buf) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_open
SC_TRACE_EVENT(sys_open,
	TP_PROTO(const char * filename, int flags, umode_t mode),
	TP_ARGS(filename, flags, mode),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(int, flags) __field(umode_t, mode)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(flags, flags) tp_assign(mode, mode)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_waitpid
SC_TRACE_EVENT(sys_waitpid,
	TP_PROTO(pid_t pid, int * stat_addr, int options),
	TP_ARGS(pid, stat_addr, options),
	TP_STRUCT__entry(__field(pid_t, pid) __field_hex(int *, stat_addr) __field(int, options)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(stat_addr, stat_addr) tp_assign(options, options)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mknod
SC_TRACE_EVENT(sys_mknod,
	TP_PROTO(const char * filename, umode_t mode, unsigned dev),
	TP_ARGS(filename, mode, dev),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(umode_t, mode) __field(unsigned, dev)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(mode, mode) tp_assign(dev, dev)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_lchown
SC_TRACE_EVENT(sys_lchown,
	TP_PROTO(const char * filename, uid_t user, gid_t group),
	TP_ARGS(filename, user, group),
	TP_STRUCT__entry(__string_from_user(filename, filename) __field(uid_t, user) __field(gid_t, group)),
	TP_fast_assign(tp_copy_string_from_user(filename, filename) tp_assign(user, user) tp_assign(group, group)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sigaction
SC_TRACE_EVENT(sys_sigaction,
	TP_PROTO(int sig, const struct sigaction * act, struct sigaction * oact),
	TP_ARGS(sig, act, oact),
	TP_STRUCT__entry(__field(int, sig) __field_hex(const struct sigaction *, act) __field_hex(struct sigaction *, oact)),
	TP_fast_assign(tp_assign(sig, sig) tp_assign(act, act) tp_assign(oact, oact)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_readlink
SC_TRACE_EVENT(sys_readlink,
	TP_PROTO(const char * path, char * buf, int bufsiz),
	TP_ARGS(path, buf, bufsiz),
	TP_STRUCT__entry(__string_from_user(path, path) __field_hex(char *, buf) __field(int, bufsiz)),
	TP_fast_assign(tp_copy_string_from_user(path, path) tp_assign(buf, buf) tp_assign(bufsiz, bufsiz)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_old_readdir
SC_TRACE_EVENT(sys_old_readdir,
	TP_PROTO(unsigned int fd, struct old_linux_dirent * dirent, unsigned int count),
	TP_ARGS(fd, dirent, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct old_linux_dirent *, dirent) __field(unsigned int, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(dirent, dirent) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_syslog
SC_TRACE_EVENT(sys_syslog,
	TP_PROTO(int type, char * buf, int len),
	TP_ARGS(type, buf, len),
	TP_STRUCT__entry(__field(int, type) __field_hex(char *, buf) __field(int, len)),
	TP_fast_assign(tp_assign(type, type) tp_assign(buf, buf) tp_assign(len, len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_setitimer
SC_TRACE_EVENT(sys_setitimer,
	TP_PROTO(int which, struct itimerval * value, struct itimerval * ovalue),
	TP_ARGS(which, value, ovalue),
	TP_STRUCT__entry(__field(int, which) __field_hex(struct itimerval *, value) __field_hex(struct itimerval *, ovalue)),
	TP_fast_assign(tp_assign(which, which) tp_assign(value, value) tp_assign(ovalue, ovalue)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sigprocmask
SC_TRACE_EVENT(sys_sigprocmask,
	TP_PROTO(int how, old_sigset_t * nset, old_sigset_t * oset),
	TP_ARGS(how, nset, oset),
	TP_STRUCT__entry(__field(int, how) __field_hex(old_sigset_t *, nset) __field_hex(old_sigset_t *, oset)),
	TP_fast_assign(tp_assign(how, how) tp_assign(nset, nset) tp_assign(oset, oset)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_init_module
SC_TRACE_EVENT(sys_init_module,
	TP_PROTO(void * umod, unsigned long len, const char * uargs),
	TP_ARGS(umod, len, uargs),
	TP_STRUCT__entry(__field_hex(void *, umod) __field(unsigned long, len) __field_hex(const char *, uargs)),
	TP_fast_assign(tp_assign(umod, umod) tp_assign(len, len) tp_assign(uargs, uargs)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getdents
SC_TRACE_EVENT(sys_getdents,
	TP_PROTO(unsigned int fd, struct linux_dirent * dirent, unsigned int count),
	TP_ARGS(fd, dirent, count),
	TP_STRUCT__entry(__field(unsigned int, fd) __field_hex(struct linux_dirent *, dirent) __field(unsigned int, count)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(dirent, dirent) tp_assign(count, count)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_readv
SC_TRACE_EVENT(sys_readv,
	TP_PROTO(unsigned long fd, const struct iovec * vec, unsigned long vlen),
	TP_ARGS(fd, vec, vlen),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(const struct iovec *, vec) __field(unsigned long, vlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(vec, vec) tp_assign(vlen, vlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_writev
SC_TRACE_EVENT(sys_writev,
	TP_PROTO(unsigned long fd, const struct iovec * vec, unsigned long vlen),
	TP_ARGS(fd, vec, vlen),
	TP_STRUCT__entry(__field(unsigned long, fd) __field_hex(const struct iovec *, vec) __field(unsigned long, vlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(vec, vec) tp_assign(vlen, vlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_cachectl
SC_TRACE_EVENT(sys_cachectl,
	TP_PROTO(char * addr, int nbytes, int op),
	TP_ARGS(addr, nbytes, op),
	TP_STRUCT__entry(__field_hex(char *, addr) __field(int, nbytes) __field(int, op)),
	TP_fast_assign(tp_assign(addr, addr) tp_assign(nbytes, nbytes) tp_assign(op, op)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_sched_setscheduler
SC_TRACE_EVENT(sys_sched_setscheduler,
	TP_PROTO(pid_t pid, int policy, struct sched_param * param),
	TP_ARGS(pid, policy, param),
	TP_STRUCT__entry(__field(pid_t, pid) __field(int, policy) __field_hex(struct sched_param *, param)),
	TP_fast_assign(tp_assign(pid, pid) tp_assign(policy, policy) tp_assign(param, param)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_accept
SC_TRACE_EVENT(sys_accept,
	TP_PROTO(int fd, struct sockaddr * upeer_sockaddr, int * upeer_addrlen),
	TP_ARGS(fd, upeer_sockaddr, upeer_addrlen),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, upeer_sockaddr) __field_hex(int *, upeer_addrlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(upeer_sockaddr, upeer_sockaddr) tp_assign(upeer_addrlen, upeer_addrlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_bind
SC_TRACE_EVENT(sys_bind,
	TP_PROTO(int fd, struct sockaddr * umyaddr, int addrlen),
	TP_ARGS(fd, umyaddr, addrlen),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, umyaddr) __field_hex(int, addrlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(umyaddr, umyaddr) tp_assign(addrlen, addrlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_connect
SC_TRACE_EVENT(sys_connect,
	TP_PROTO(int fd, struct sockaddr * uservaddr, int addrlen),
	TP_ARGS(fd, uservaddr, addrlen),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, uservaddr) __field_hex(int, addrlen)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(uservaddr, uservaddr) tp_assign(addrlen, addrlen)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getpeername
SC_TRACE_EVENT(sys_getpeername,
	TP_PROTO(int fd, struct sockaddr * usockaddr, int * usockaddr_len),
	TP_ARGS(fd, usockaddr, usockaddr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, usockaddr) __field_hex(int *, usockaddr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(usockaddr, usockaddr) tp_assign(usockaddr_len, usockaddr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_getsockname
SC_TRACE_EVENT(sys_getsockname,
	TP_PROTO(int fd, struct sockaddr * usockaddr, int * usockaddr_len),
	TP_ARGS(fd, usockaddr, usockaddr_len),
	TP_STRUCT__entry(__field(int, fd) __field_hex(struct sockaddr *, usockaddr) __field_hex(int *, usockaddr_len)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(usockaddr, usockaddr) tp_assign(usockaddr_len, usockaddr_len)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_reboot
SC_TRACE_EVENT(sys_reboot,
	TP_PROTO(int magic1, int magic2, unsigned int cmd, void * arg),
	TP_ARGS(magic1, magic2, cmd, arg),
	TP_STRUCT__entry(__field(int, magic1) __field(int, magic2) __field(unsigned int, cmd) __field_hex(void *, arg)),
	TP_fast_assign(tp_assign(magic1, magic1) tp_assign(magic2, magic2) tp_assign(cmd, cmd) tp_assign(arg, arg)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_wait4
SC_TRACE_EVENT(sys_wait4,
	TP_PROTO(pid_t upid, int * stat_addr, int options, struct rusage * ru),
	TP_ARGS(upid, stat_addr, options, ru),
	TP_STRUCT__entry(__field(pid_t, upid) __field_hex(int *, stat_addr) __field(int, options) __field_hex(struct rusage *, ru)),
	TP_fast_assign(tp_assign(upid, upid) tp_assign(stat_addr, stat_addr) tp_assign(options, options) tp_assign(ru, ru)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_quotactl
SC_TRACE_EVENT(sys_quotactl,
	TP_PROTO(unsigned int cmd, const char * special, qid_t id, void * addr),
	TP_ARGS(cmd, special, id, addr),
	TP_STRUCT__entry(__field(unsigned int, cmd) __field_hex(const char *, special) __field(qid_t, id) __field_hex(void *, addr)),
	TP_fast_assign(tp_assign(cmd, cmd) tp_assign(special, special) tp_assign(id, id) tp_assign(addr, addr)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_mount
SC_TRACE_EVENT(sys_mount,
	TP_PROTO(char * dev_name, char * dir_name, char * type, unsigned long flags, void * data),
	TP_ARGS(dev_name, dir_name, type, flags, data),
	TP_STRUCT__entry(__string_from_user(dev_name, dev_name) __string_from_user(dir_name, dir_name) __string_from_user(type, type) __field(unsigned long, flags) __field_hex(void *, data)),
	TP_fast_assign(tp_copy_string_from_user(dev_name, dev_name) tp_copy_string_from_user(dir_name, dir_name) tp_copy_string_from_user(type, type) tp_assign(flags, flags) tp_assign(data, data)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_llseek
SC_TRACE_EVENT(sys_llseek,
	TP_PROTO(unsigned int fd, unsigned long offset_high, unsigned long offset_low, loff_t * result, unsigned int origin),
	TP_ARGS(fd, offset_high, offset_low, result, origin),
	TP_STRUCT__entry(__field(unsigned int, fd) __field(unsigned long, offset_high) __field(unsigned long, offset_low) __field_hex(loff_t *, result) __field(unsigned int, origin)),
	TP_fast_assign(tp_assign(fd, fd) tp_assign(offset_high, offset_high) tp_assign(offset_low, offset_low) tp_assign(result, result) tp_assign(origin, origin)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_select
SC_TRACE_EVENT(sys_select,
	TP_PROTO(int n, fd_set * inp, fd_set * outp, fd_set * exp, struct timeval * tvp),
	TP_ARGS(n, inp, outp, exp, tvp),
	TP_STRUCT__entry(__field(int, n) __field_hex(fd_set *, inp) __field_hex(fd_set *, outp) __field_hex(fd_set *, exp) __field_hex(struct timeval *, tvp)),
	TP_fast_assign(tp_assign(n, n) tp_assign(inp, inp) tp_assign(outp, outp) tp_assign(exp, exp) tp_assign(tvp, tvp)),
	TP_printk()
)
#endif
#ifndef OVERRIDE_32_sys_ipc
SC_TRACE_EVENT(sys_ipc,
	TP_PROTO(unsigned int call, int first, unsigned long second, unsigned long third, void * ptr, long fifth),
	TP_ARGS(call, first, second, third, ptr, fifth),
	TP_STRUCT__entry(__field(unsigned int, call) __field(int, first) __field(unsigned long, second) __field(unsigned long, third) __field_hex(void *, ptr) __field(long, fifth)),
	TP_fast_assign(tp_assign(call, call) tp_assign(first, first) tp_assign(second, second) tp_assign(third, third) tp_assign(ptr, ptr) tp_assign(fifth, fifth)),
	TP_printk()
)
#endif

#endif /*  _TRACE_SYSCALLS_POINTERS_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"

#else /* CREATE_SYSCALL_TABLE */

#include "mips-32-syscalls-3.5.0_pointers_override.h"
#include "syscalls_pointers_override.h"

#ifndef OVERRIDE_TABLE_32_sys_read
TRACE_SYSCALL_TABLE(sys_read, sys_read, 4007, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_write
TRACE_SYSCALL_TABLE(sys_write, sys_write, 4009, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_open
TRACE_SYSCALL_TABLE(sys_open, sys_open, 4011, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_waitpid
TRACE_SYSCALL_TABLE(sys_waitpid, sys_waitpid, 4015, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_creat
TRACE_SYSCALL_TABLE(sys_creat, sys_creat, 4017, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_link
TRACE_SYSCALL_TABLE(sys_link, sys_link, 4019, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_unlink
TRACE_SYSCALL_TABLE(sys_unlink, sys_unlink, 4021, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_chdir
TRACE_SYSCALL_TABLE(sys_chdir, sys_chdir, 4025, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_time
TRACE_SYSCALL_TABLE(sys_time, sys_time, 4027, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mknod
TRACE_SYSCALL_TABLE(sys_mknod, sys_mknod, 4029, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_chmod
TRACE_SYSCALL_TABLE(sys_chmod, sys_chmod, 4031, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_lchown
TRACE_SYSCALL_TABLE(sys_lchown, sys_lchown, 4033, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mount
TRACE_SYSCALL_TABLE(sys_mount, sys_mount, 4043, 5)
#endif
#ifndef OVERRIDE_TABLE_32_sys_oldumount
TRACE_SYSCALL_TABLE(sys_oldumount, sys_oldumount, 4045, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_stime
TRACE_SYSCALL_TABLE(sys_stime, sys_stime, 4051, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_utime
TRACE_SYSCALL_TABLE(sys_utime, sys_utime, 4061, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_access
TRACE_SYSCALL_TABLE(sys_access, sys_access, 4067, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_rename
TRACE_SYSCALL_TABLE(sys_rename, sys_rename, 4077, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_mkdir
TRACE_SYSCALL_TABLE(sys_mkdir, sys_mkdir, 4079, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_rmdir
TRACE_SYSCALL_TABLE(sys_rmdir, sys_rmdir, 4081, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_times
TRACE_SYSCALL_TABLE(sys_times, sys_times, 4087, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_umount
TRACE_SYSCALL_TABLE(sys_umount, sys_umount, 4105, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_olduname
TRACE_SYSCALL_TABLE(sys_olduname, sys_olduname, 4119, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_chroot
TRACE_SYSCALL_TABLE(sys_chroot, sys_chroot, 4123, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_ustat
TRACE_SYSCALL_TABLE(sys_ustat, sys_ustat, 4125, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sigaction
TRACE_SYSCALL_TABLE(sys_sigaction, sys_sigaction, 4135, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sigpending
TRACE_SYSCALL_TABLE(sys_sigpending, sys_sigpending, 4147, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sethostname
TRACE_SYSCALL_TABLE(sys_sethostname, sys_sethostname, 4149, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setrlimit
TRACE_SYSCALL_TABLE(sys_setrlimit, sys_setrlimit, 4151, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getrlimit
TRACE_SYSCALL_TABLE(sys_getrlimit, sys_getrlimit, 4153, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getrusage
TRACE_SYSCALL_TABLE(sys_getrusage, sys_getrusage, 4155, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_gettimeofday
TRACE_SYSCALL_TABLE(sys_gettimeofday, sys_gettimeofday, 4157, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_settimeofday
TRACE_SYSCALL_TABLE(sys_settimeofday, sys_settimeofday, 4159, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getgroups
TRACE_SYSCALL_TABLE(sys_getgroups, sys_getgroups, 4161, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setgroups
TRACE_SYSCALL_TABLE(sys_setgroups, sys_setgroups, 4163, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_symlink
TRACE_SYSCALL_TABLE(sys_symlink, sys_symlink, 4167, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_readlink
TRACE_SYSCALL_TABLE(sys_readlink, sys_readlink, 4171, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_uselib
TRACE_SYSCALL_TABLE(sys_uselib, sys_uselib, 4173, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_swapon
TRACE_SYSCALL_TABLE(sys_swapon, sys_swapon, 4175, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_reboot
TRACE_SYSCALL_TABLE(sys_reboot, sys_reboot, 4177, 4)
#endif
#ifndef OVERRIDE_TABLE_32_sys_old_readdir
TRACE_SYSCALL_TABLE(sys_old_readdir, sys_old_readdir, 4179, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_truncate
TRACE_SYSCALL_TABLE(sys_truncate, sys_truncate, 4185, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_statfs
TRACE_SYSCALL_TABLE(sys_statfs, sys_statfs, 4199, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_fstatfs
TRACE_SYSCALL_TABLE(sys_fstatfs, sys_fstatfs, 4201, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_socketcall
TRACE_SYSCALL_TABLE(sys_socketcall, sys_socketcall, 4205, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_syslog
TRACE_SYSCALL_TABLE(sys_syslog, sys_syslog, 4207, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setitimer
TRACE_SYSCALL_TABLE(sys_setitimer, sys_setitimer, 4209, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getitimer
TRACE_SYSCALL_TABLE(sys_getitimer, sys_getitimer, 4211, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_newstat
TRACE_SYSCALL_TABLE(sys_newstat, sys_newstat, 4213, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_newlstat
TRACE_SYSCALL_TABLE(sys_newlstat, sys_newlstat, 4215, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_newfstat
TRACE_SYSCALL_TABLE(sys_newfstat, sys_newfstat, 4217, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_uname
TRACE_SYSCALL_TABLE(sys_uname, sys_uname, 4219, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_wait4
TRACE_SYSCALL_TABLE(sys_wait4, sys_wait4, 4229, 4)
#endif
#ifndef OVERRIDE_TABLE_32_sys_swapoff
TRACE_SYSCALL_TABLE(sys_swapoff, sys_swapoff, 4231, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sysinfo
TRACE_SYSCALL_TABLE(sys_sysinfo, sys_sysinfo, 4233, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_ipc
TRACE_SYSCALL_TABLE(sys_ipc, sys_ipc, 4235, 6)
#endif
#ifndef OVERRIDE_TABLE_32_sys_setdomainname
TRACE_SYSCALL_TABLE(sys_setdomainname, sys_setdomainname, 4243, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_newuname
TRACE_SYSCALL_TABLE(sys_newuname, sys_newuname, 4245, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_adjtimex
TRACE_SYSCALL_TABLE(sys_adjtimex, sys_adjtimex, 4249, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sigprocmask
TRACE_SYSCALL_TABLE(sys_sigprocmask, sys_sigprocmask, 4253, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_init_module
TRACE_SYSCALL_TABLE(sys_init_module, sys_init_module, 4257, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_delete_module
TRACE_SYSCALL_TABLE(sys_delete_module, sys_delete_module, 4259, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_quotactl
TRACE_SYSCALL_TABLE(sys_quotactl, sys_quotactl, 4263, 4)
#endif
#ifndef OVERRIDE_TABLE_32_sys_llseek
TRACE_SYSCALL_TABLE(sys_llseek, sys_llseek, 4281, 5)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getdents
TRACE_SYSCALL_TABLE(sys_getdents, sys_getdents, 4283, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_select
TRACE_SYSCALL_TABLE(sys_select, sys_select, 4285, 5)
#endif
#ifndef OVERRIDE_TABLE_32_sys_readv
TRACE_SYSCALL_TABLE(sys_readv, sys_readv, 4291, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_writev
TRACE_SYSCALL_TABLE(sys_writev, sys_writev, 4293, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_cachectl
TRACE_SYSCALL_TABLE(sys_cachectl, sys_cachectl, 4297, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sysctl
TRACE_SYSCALL_TABLE(sys_sysctl, sys_sysctl, 4307, 1)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_setparam
TRACE_SYSCALL_TABLE(sys_sched_setparam, sys_sched_setparam, 4317, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_getparam
TRACE_SYSCALL_TABLE(sys_sched_getparam, sys_sched_getparam, 4319, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_setscheduler
TRACE_SYSCALL_TABLE(sys_sched_setscheduler, sys_sched_setscheduler, 4321, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_sched_rr_get_interval
TRACE_SYSCALL_TABLE(sys_sched_rr_get_interval, sys_sched_rr_get_interval, 4331, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_nanosleep
TRACE_SYSCALL_TABLE(sys_nanosleep, sys_nanosleep, 4333, 2)
#endif
#ifndef OVERRIDE_TABLE_32_sys_accept
TRACE_SYSCALL_TABLE(sys_accept, sys_accept, 4337, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_bind
TRACE_SYSCALL_TABLE(sys_bind, sys_bind, 4339, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_connect
TRACE_SYSCALL_TABLE(sys_connect, sys_connect, 4341, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getpeername
TRACE_SYSCALL_TABLE(sys_getpeername, sys_getpeername, 4343, 3)
#endif
#ifndef OVERRIDE_TABLE_32_sys_getsockname
TRACE_SYSCALL_TABLE(sys_getsockname, sys_getsockname, 4345, 3)
#endif

#endif /* CREATE_SYSCALL_TABLE */
