#include <linux/syscalls.h>
#include <linux/compat.h>
#include "entry.h"

#define COMPAT_SYSCALL_WRAP1(name, ...) \
	COMPAT_SYSCALL_WRAPx(1, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP2(name, ...) \
	COMPAT_SYSCALL_WRAPx(2, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP3(name, ...) \
	COMPAT_SYSCALL_WRAPx(3, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP4(name, ...) \
	COMPAT_SYSCALL_WRAPx(4, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP5(name, ...) \
	COMPAT_SYSCALL_WRAPx(5, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_WRAP6(name, ...) \
	COMPAT_SYSCALL_WRAPx(6, _##name, __VA_ARGS__)

#define COMPAT_SYSCALL_WRAPx(x, name, ...)					\
	asmlinkage long compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));	\
	asmlinkage long compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))	\
	{									\
		return sys##name(__MAP(x,__SC_DELOUSE,__VA_ARGS__));		\
	}

COMPAT_SYSCALL_WRAP1(exit, int, error_code);
COMPAT_SYSCALL_WRAP1(close, unsigned int, fd);
COMPAT_SYSCALL_WRAP2(creat, const char __user *, pathname, umode_t, mode);
COMPAT_SYSCALL_WRAP2(link, const char __user *, oldname, const char __user *, newname);
COMPAT_SYSCALL_WRAP1(unlink, const char __user *, pathname);
COMPAT_SYSCALL_WRAP1(chdir, const char __user *, filename);
COMPAT_SYSCALL_WRAP3(mknod, const char __user *, filename, umode_t, mode, unsigned, dev);
COMPAT_SYSCALL_WRAP2(chmod, const char __user *, filename, umode_t, mode);
COMPAT_SYSCALL_WRAP1(oldumount, char __user *, name);
COMPAT_SYSCALL_WRAP1(alarm, unsigned int, seconds);
COMPAT_SYSCALL_WRAP2(access, const char __user *, filename, int, mode);
COMPAT_SYSCALL_WRAP1(nice, int, increment);
COMPAT_SYSCALL_WRAP2(kill, int, pid, int, sig);
COMPAT_SYSCALL_WRAP2(rename, const char __user *, oldname, const char __user *, newname);
COMPAT_SYSCALL_WRAP2(mkdir, const char __user *, pathname, umode_t, mode);
COMPAT_SYSCALL_WRAP1(rmdir, const char __user *, pathname);
COMPAT_SYSCALL_WRAP1(dup, unsigned int, fildes);
COMPAT_SYSCALL_WRAP1(pipe, int __user *, fildes);
COMPAT_SYSCALL_WRAP1(brk, compat_ulong_t, brk);
COMPAT_SYSCALL_WRAP2(signal, int, sig, __sighandler_t, handler);
COMPAT_SYSCALL_WRAP1(acct, const char __user *, name);
COMPAT_SYSCALL_WRAP2(umount, char __user *, name, int, flags);
COMPAT_SYSCALL_WRAP2(setpgid, compat_pid_t, pid, compat_pid_t, pgid);
COMPAT_SYSCALL_WRAP1(umask, int, mask);
COMPAT_SYSCALL_WRAP1(chroot, const char __user *, filename);
COMPAT_SYSCALL_WRAP2(dup2, unsigned int, oldfd, unsigned int, newfd);
COMPAT_SYSCALL_WRAP3(sigsuspend, int, unused1, int, unused2, compat_old_sigset_t, mask);
COMPAT_SYSCALL_WRAP2(sethostname, char __user *, name, int, len);
COMPAT_SYSCALL_WRAP2(symlink, const char __user *, old, const char __user *, new);
COMPAT_SYSCALL_WRAP3(readlink, const char __user *, path, char __user *, buf, int, bufsiz);
COMPAT_SYSCALL_WRAP1(uselib, const char __user *, library);
COMPAT_SYSCALL_WRAP2(swapon, const char __user *, specialfile, int, swap_flags);
COMPAT_SYSCALL_WRAP4(reboot, int, magic1, int, magic2, unsigned int, cmd, void __user *, arg);
COMPAT_SYSCALL_WRAP2(munmap, compat_ulong_t, addr, compat_size_t, len);
COMPAT_SYSCALL_WRAP2(fchmod, unsigned int, fd, umode_t, mode);
COMPAT_SYSCALL_WRAP2(getpriority, int, which, int, who);
COMPAT_SYSCALL_WRAP3(setpriority, int, which, int, who, int, niceval);
COMPAT_SYSCALL_WRAP3(syslog, int, type, char __user *, buf, int, len);
COMPAT_SYSCALL_WRAP1(swapoff, const char __user *, specialfile);
COMPAT_SYSCALL_WRAP1(fsync, unsigned int, fd);
COMPAT_SYSCALL_WRAP2(setdomainname, char __user *, name, int, len);
COMPAT_SYSCALL_WRAP1(newuname, struct new_utsname __user *, name);
COMPAT_SYSCALL_WRAP3(mprotect, compat_ulong_t, start, compat_size_t, len, compat_ulong_t, prot);
COMPAT_SYSCALL_WRAP3(init_module, void __user *, umod, compat_ulong_t, len, const char __user *, uargs);
COMPAT_SYSCALL_WRAP2(delete_module, const char __user *, name_user, unsigned int, flags);
COMPAT_SYSCALL_WRAP4(quotactl, unsigned int, cmd, const char __user *, special, qid_t, id, void __user *, addr);
COMPAT_SYSCALL_WRAP1(getpgid, compat_pid_t, pid);
COMPAT_SYSCALL_WRAP1(fchdir, unsigned int, fd);
COMPAT_SYSCALL_WRAP2(bdflush, int, func, compat_long_t, data);
COMPAT_SYSCALL_WRAP3(sysfs, int, option, compat_ulong_t, arg1, compat_ulong_t, arg2);
COMPAT_SYSCALL_WRAP1(s390_personality, unsigned int, personality);
COMPAT_SYSCALL_WRAP5(llseek, unsigned int, fd, u32, high, u32, low, loff_t __user *, result, unsigned int, whence);
COMPAT_SYSCALL_WRAP2(flock, unsigned int, fd, unsigned int, cmd);
COMPAT_SYSCALL_WRAP3(msync, compat_ulong_t, start, compat_size_t, len, int, flags);
COMPAT_SYSCALL_WRAP1(getsid, compat_pid_t, pid);
COMPAT_SYSCALL_WRAP1(fdatasync, unsigned int, fd);
COMPAT_SYSCALL_WRAP2(mlock, compat_ulong_t, start, compat_size_t, len);
COMPAT_SYSCALL_WRAP2(munlock, compat_ulong_t, start, compat_size_t, len);
COMPAT_SYSCALL_WRAP1(mlockall, int, flags);
COMPAT_SYSCALL_WRAP2(sched_setparam, compat_pid_t, pid, struct sched_param __user *, param);
COMPAT_SYSCALL_WRAP2(sched_getparam, compat_pid_t, pid, struct sched_param __user *, param);
COMPAT_SYSCALL_WRAP3(sched_setscheduler, compat_pid_t, pid, int, policy, struct sched_param __user *, param);
COMPAT_SYSCALL_WRAP1(sched_getscheduler, compat_pid_t, pid);
COMPAT_SYSCALL_WRAP1(sched_get_priority_max, int, policy);
COMPAT_SYSCALL_WRAP1(sched_get_priority_min, int, policy);
COMPAT_SYSCALL_WRAP5(mremap, u32, addr, u32, old_len, u32, new_len, u32, flags, u32, new_addr);
COMPAT_SYSCALL_WRAP3(poll, struct pollfd __user *, ufds, unsigned int, nfds, int, timeout);
COMPAT_SYSCALL_WRAP5(prctl, int, option, u32, arg2, u32, arg3, u32, arg4, u32, arg5);
COMPAT_SYSCALL_WRAP2(getcwd, char __user *, buf, u32, size);
COMPAT_SYSCALL_WRAP2(capget, cap_user_header_t, header, cap_user_data_t, dataptr);
COMPAT_SYSCALL_WRAP2(capset, cap_user_header_t, header, const cap_user_data_t, data);
COMPAT_SYSCALL_WRAP3(lchown, const char __user *, filename, compat_uid_t, user, compat_gid_t, group);
COMPAT_SYSCALL_WRAP2(setreuid, compat_uid_t, ruid, compat_uid_t, euid);
COMPAT_SYSCALL_WRAP2(setregid, compat_gid_t, rgid, compat_gid_t, egid);
COMPAT_SYSCALL_WRAP2(getgroups, int, gidsetsize, compat_gid_t __user *, grouplist);
COMPAT_SYSCALL_WRAP2(setgroups, int, gidsetsize, compat_gid_t __user *, grouplist);
COMPAT_SYSCALL_WRAP3(fchown, unsigned int, fd, compat_uid_t, user, compat_gid_t, group);
COMPAT_SYSCALL_WRAP3(setresuid, compat_uid_t, ruid, compat_uid_t, euid, compat_uid_t, suid);
COMPAT_SYSCALL_WRAP3(getresuid, compat_uid_t __user *, ruid, compat_uid_t __user *, euid, compat_uid_t __user *, suid);
COMPAT_SYSCALL_WRAP3(setresgid, compat_gid_t, rgid, compat_gid_t, egid, compat_gid_t, sgid);
COMPAT_SYSCALL_WRAP3(getresgid, compat_gid_t __user *, rgid, compat_gid_t __user *, egid, compat_gid_t __user *, sgid);
COMPAT_SYSCALL_WRAP3(chown, const char __user *, filename, compat_uid_t, user, compat_gid_t, group);
