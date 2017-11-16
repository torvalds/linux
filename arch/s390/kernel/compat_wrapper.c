/*
 *  Compat system call wrappers.
 *
 *    Copyright IBM Corp. 2014
 */

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

#define __SC_COMPAT_TYPE(t, a) \
	__typeof(__builtin_choose_expr(sizeof(t) > 4, 0L, (t)0)) a

#define __SC_COMPAT_CAST(t, a)						\
({									\
	long __ReS = a;							\
									\
	BUILD_BUG_ON((sizeof(t) > 4) && !__TYPE_IS_L(t) &&		\
		     !__TYPE_IS_UL(t) && !__TYPE_IS_PTR(t));		\
	if (__TYPE_IS_L(t))						\
		__ReS = (s32)a;						\
	if (__TYPE_IS_UL(t))						\
		__ReS = (u32)a;						\
	if (__TYPE_IS_PTR(t))						\
		__ReS = a & 0x7fffffff;					\
	(t)__ReS;							\
})

/*
 * The COMPAT_SYSCALL_WRAP macro generates system call wrappers to be used by
 * compat tasks. These wrappers will only be used for system calls where only
 * the system call arguments need sign or zero extension or zeroing of the upper
 * 33 bits of pointers.
 * Note: since the wrapper function will afterwards call a system call which
 * again performs zero and sign extension for all system call arguments with
 * a size of less than eight bytes, these compat wrappers only touch those
 * system call arguments with a size of eight bytes ((unsigned) long and
 * pointers). Zero and sign extension for e.g. int parameters will be done by
 * the regular system call wrappers.
 */
#define COMPAT_SYSCALL_WRAPx(x, name, ...)					\
asmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));			\
asmlinkage long notrace compat_sys##name(__MAP(x,__SC_COMPAT_TYPE,__VA_ARGS__));\
asmlinkage long notrace compat_sys##name(__MAP(x,__SC_COMPAT_TYPE,__VA_ARGS__))	\
{										\
	return sys##name(__MAP(x,__SC_COMPAT_CAST,__VA_ARGS__));		\
}

COMPAT_SYSCALL_WRAP2(creat, const char __user *, pathname, umode_t, mode);
COMPAT_SYSCALL_WRAP2(link, const char __user *, oldname, const char __user *, newname);
COMPAT_SYSCALL_WRAP1(unlink, const char __user *, pathname);
COMPAT_SYSCALL_WRAP1(chdir, const char __user *, filename);
COMPAT_SYSCALL_WRAP3(mknod, const char __user *, filename, umode_t, mode, unsigned, dev);
COMPAT_SYSCALL_WRAP2(chmod, const char __user *, filename, umode_t, mode);
COMPAT_SYSCALL_WRAP1(oldumount, char __user *, name);
COMPAT_SYSCALL_WRAP2(access, const char __user *, filename, int, mode);
COMPAT_SYSCALL_WRAP2(rename, const char __user *, oldname, const char __user *, newname);
COMPAT_SYSCALL_WRAP2(mkdir, const char __user *, pathname, umode_t, mode);
COMPAT_SYSCALL_WRAP1(rmdir, const char __user *, pathname);
COMPAT_SYSCALL_WRAP1(pipe, int __user *, fildes);
COMPAT_SYSCALL_WRAP1(brk, unsigned long, brk);
COMPAT_SYSCALL_WRAP2(signal, int, sig, __sighandler_t, handler);
COMPAT_SYSCALL_WRAP1(acct, const char __user *, name);
COMPAT_SYSCALL_WRAP2(umount, char __user *, name, int, flags);
COMPAT_SYSCALL_WRAP1(chroot, const char __user *, filename);
COMPAT_SYSCALL_WRAP3(sigsuspend, int, unused1, int, unused2, old_sigset_t, mask);
COMPAT_SYSCALL_WRAP2(sethostname, char __user *, name, int, len);
COMPAT_SYSCALL_WRAP2(symlink, const char __user *, old, const char __user *, new);
COMPAT_SYSCALL_WRAP3(readlink, const char __user *, path, char __user *, buf, int, bufsiz);
COMPAT_SYSCALL_WRAP1(uselib, const char __user *, library);
COMPAT_SYSCALL_WRAP2(swapon, const char __user *, specialfile, int, swap_flags);
COMPAT_SYSCALL_WRAP4(reboot, int, magic1, int, magic2, unsigned int, cmd, void __user *, arg);
COMPAT_SYSCALL_WRAP2(munmap, unsigned long, addr, size_t, len);
COMPAT_SYSCALL_WRAP3(syslog, int, type, char __user *, buf, int, len);
COMPAT_SYSCALL_WRAP1(swapoff, const char __user *, specialfile);
COMPAT_SYSCALL_WRAP2(setdomainname, char __user *, name, int, len);
COMPAT_SYSCALL_WRAP1(newuname, struct new_utsname __user *, name);
COMPAT_SYSCALL_WRAP3(mprotect, unsigned long, start, size_t, len, unsigned long, prot);
COMPAT_SYSCALL_WRAP3(init_module, void __user *, umod, unsigned long, len, const char __user *, uargs);
COMPAT_SYSCALL_WRAP2(delete_module, const char __user *, name_user, unsigned int, flags);
COMPAT_SYSCALL_WRAP4(quotactl, unsigned int, cmd, const char __user *, special, qid_t, id, void __user *, addr);
COMPAT_SYSCALL_WRAP2(bdflush, int, func, long, data);
COMPAT_SYSCALL_WRAP3(sysfs, int, option, unsigned long, arg1, unsigned long, arg2);
COMPAT_SYSCALL_WRAP5(llseek, unsigned int, fd, unsigned long, high, unsigned long, low, loff_t __user *, result, unsigned int, whence);
COMPAT_SYSCALL_WRAP3(msync, unsigned long, start, size_t, len, int, flags);
COMPAT_SYSCALL_WRAP2(mlock, unsigned long, start, size_t, len);
COMPAT_SYSCALL_WRAP2(munlock, unsigned long, start, size_t, len);
COMPAT_SYSCALL_WRAP2(sched_setparam, pid_t, pid, struct sched_param __user *, param);
COMPAT_SYSCALL_WRAP2(sched_getparam, pid_t, pid, struct sched_param __user *, param);
COMPAT_SYSCALL_WRAP3(sched_setscheduler, pid_t, pid, int, policy, struct sched_param __user *, param);
COMPAT_SYSCALL_WRAP5(mremap, unsigned long, addr, unsigned long, old_len, unsigned long, new_len, unsigned long, flags, unsigned long, new_addr);
COMPAT_SYSCALL_WRAP3(poll, struct pollfd __user *, ufds, unsigned int, nfds, int, timeout);
COMPAT_SYSCALL_WRAP5(prctl, int, option, unsigned long, arg2, unsigned long, arg3, unsigned long, arg4, unsigned long, arg5);
COMPAT_SYSCALL_WRAP2(getcwd, char __user *, buf, unsigned long, size);
COMPAT_SYSCALL_WRAP2(capget, cap_user_header_t, header, cap_user_data_t, dataptr);
COMPAT_SYSCALL_WRAP2(capset, cap_user_header_t, header, const cap_user_data_t, data);
COMPAT_SYSCALL_WRAP3(lchown, const char __user *, filename, uid_t, user, gid_t, group);
COMPAT_SYSCALL_WRAP2(getgroups, int, gidsetsize, gid_t __user *, grouplist);
COMPAT_SYSCALL_WRAP2(setgroups, int, gidsetsize, gid_t __user *, grouplist);
COMPAT_SYSCALL_WRAP3(getresuid, uid_t __user *, ruid, uid_t __user *, euid, uid_t __user *, suid);
COMPAT_SYSCALL_WRAP3(getresgid, gid_t __user *, rgid, gid_t __user *, egid, gid_t __user *, sgid);
COMPAT_SYSCALL_WRAP3(chown, const char __user *, filename, uid_t, user, gid_t, group);
COMPAT_SYSCALL_WRAP2(pivot_root, const char __user *, new_root, const char __user *, put_old);
COMPAT_SYSCALL_WRAP3(mincore, unsigned long, start, size_t, len, unsigned char __user *, vec);
COMPAT_SYSCALL_WRAP3(madvise, unsigned long, start, size_t, len, int, behavior);
COMPAT_SYSCALL_WRAP5(setxattr, const char __user *, path, const char __user *, name, const void __user *, value, size_t, size, int, flags);
COMPAT_SYSCALL_WRAP5(lsetxattr, const char __user *, path, const char __user *, name, const void __user *, value, size_t, size, int, flags);
COMPAT_SYSCALL_WRAP5(fsetxattr, int, fd, const char __user *, name, const void __user *, value, size_t, size, int, flags);
COMPAT_SYSCALL_WRAP3(getdents64, unsigned int, fd, struct linux_dirent64 __user *, dirent, unsigned int, count);
COMPAT_SYSCALL_WRAP4(getxattr, const char __user *, path, const char __user *, name, void __user *, value, size_t, size);
COMPAT_SYSCALL_WRAP4(lgetxattr, const char __user *, path, const char __user *, name, void __user *, value, size_t, size);
COMPAT_SYSCALL_WRAP4(fgetxattr, int, fd, const char __user *, name, void __user *, value, size_t, size);
COMPAT_SYSCALL_WRAP3(listxattr, const char __user *, path, char __user *, list, size_t, size);
COMPAT_SYSCALL_WRAP3(llistxattr, const char __user *, path, char __user *, list, size_t, size);
COMPAT_SYSCALL_WRAP3(flistxattr, int, fd, char __user *, list, size_t, size);
COMPAT_SYSCALL_WRAP2(removexattr, const char __user *, path, const char __user *, name);
COMPAT_SYSCALL_WRAP2(lremovexattr, const char __user *, path, const char __user *, name);
COMPAT_SYSCALL_WRAP2(fremovexattr, int, fd, const char __user *, name);
COMPAT_SYSCALL_WRAP1(set_tid_address, int __user *, tidptr);
COMPAT_SYSCALL_WRAP4(epoll_ctl, int, epfd, int, op, int, fd, struct epoll_event __user *, event);
COMPAT_SYSCALL_WRAP4(epoll_wait, int, epfd, struct epoll_event __user *, events, int, maxevents, int, timeout);
COMPAT_SYSCALL_WRAP1(io_destroy, aio_context_t, ctx);
COMPAT_SYSCALL_WRAP3(io_cancel, aio_context_t, ctx_id, struct iocb __user *, iocb, struct io_event __user *, result);
COMPAT_SYSCALL_WRAP1(mq_unlink, const char __user *, name);
COMPAT_SYSCALL_WRAP5(add_key, const char __user *, tp, const char __user *, dsc, const void __user *, pld, size_t, len, key_serial_t, id);
COMPAT_SYSCALL_WRAP4(request_key, const char __user *, tp, const char __user *, dsc, const char __user *, info, key_serial_t, id);
COMPAT_SYSCALL_WRAP5(remap_file_pages, unsigned long, start, unsigned long, size, unsigned long, prot, unsigned long, pgoff, unsigned long, flags);
COMPAT_SYSCALL_WRAP3(inotify_add_watch, int, fd, const char __user *, path, u32, mask);
COMPAT_SYSCALL_WRAP3(mkdirat, int, dfd, const char __user *, pathname, umode_t, mode);
COMPAT_SYSCALL_WRAP4(mknodat, int, dfd, const char __user *, filename, umode_t, mode, unsigned, dev);
COMPAT_SYSCALL_WRAP5(fchownat, int, dfd, const char __user *, filename, uid_t, user, gid_t, group, int, flag);
COMPAT_SYSCALL_WRAP3(unlinkat, int, dfd, const char __user *, pathname, int, flag);
COMPAT_SYSCALL_WRAP4(renameat, int, olddfd, const char __user *, oldname, int, newdfd, const char __user *, newname);
COMPAT_SYSCALL_WRAP5(linkat, int, olddfd, const char __user *, oldname, int, newdfd, const char __user *, newname, int, flags);
COMPAT_SYSCALL_WRAP3(symlinkat, const char __user *, oldname, int, newdfd, const char __user *, newname);
COMPAT_SYSCALL_WRAP4(readlinkat, int, dfd, const char __user *, path, char __user *, buf, int, bufsiz);
COMPAT_SYSCALL_WRAP3(fchmodat, int, dfd, const char __user *, filename, umode_t, mode);
COMPAT_SYSCALL_WRAP3(faccessat, int, dfd, const char __user *, filename, int, mode);
COMPAT_SYSCALL_WRAP1(unshare, unsigned long, unshare_flags);
COMPAT_SYSCALL_WRAP6(splice, int, fd_in, loff_t __user *, off_in, int, fd_out, loff_t __user *, off_out, size_t, len, unsigned int, flags);
COMPAT_SYSCALL_WRAP4(tee, int, fdin, int, fdout, size_t, len, unsigned int, flags);
COMPAT_SYSCALL_WRAP3(getcpu, unsigned __user *, cpu, unsigned __user *, node, struct getcpu_cache __user *, cache);
COMPAT_SYSCALL_WRAP2(pipe2, int __user *, fildes, int, flags);
COMPAT_SYSCALL_WRAP5(perf_event_open, struct perf_event_attr __user *, attr_uptr, pid_t, pid, int, cpu, int, group_fd, unsigned long, flags);
COMPAT_SYSCALL_WRAP5(clone, unsigned long, newsp, unsigned long, clone_flags, int __user *, parent_tidptr, int __user *, child_tidptr, unsigned long, tls);
COMPAT_SYSCALL_WRAP4(prlimit64, pid_t, pid, unsigned int, resource, const struct rlimit64 __user *, new_rlim, struct rlimit64 __user *, old_rlim);
COMPAT_SYSCALL_WRAP5(name_to_handle_at, int, dfd, const char __user *, name, struct file_handle __user *, handle, int __user *, mnt_id, int, flag);
COMPAT_SYSCALL_WRAP5(kcmp, pid_t, pid1, pid_t, pid2, int, type, unsigned long, idx1, unsigned long, idx2);
COMPAT_SYSCALL_WRAP3(finit_module, int, fd, const char __user *, uargs, int, flags);
COMPAT_SYSCALL_WRAP3(sched_setattr, pid_t, pid, struct sched_attr __user *, attr, unsigned int, flags);
COMPAT_SYSCALL_WRAP4(sched_getattr, pid_t, pid, struct sched_attr __user *, attr, unsigned int, size, unsigned int, flags);
COMPAT_SYSCALL_WRAP5(renameat2, int, olddfd, const char __user *, oldname, int, newdfd, const char __user *, newname, unsigned int, flags);
COMPAT_SYSCALL_WRAP3(seccomp, unsigned int, op, unsigned int, flags, const char __user *, uargs)
COMPAT_SYSCALL_WRAP3(getrandom, char __user *, buf, size_t, count, unsigned int, flags)
COMPAT_SYSCALL_WRAP2(memfd_create, const char __user *, uname, unsigned int, flags)
COMPAT_SYSCALL_WRAP3(bpf, int, cmd, union bpf_attr *, attr, unsigned int, size);
COMPAT_SYSCALL_WRAP3(s390_pci_mmio_write, const unsigned long, mmio_addr, const void __user *, user_buffer, const size_t, length);
COMPAT_SYSCALL_WRAP3(s390_pci_mmio_read, const unsigned long, mmio_addr, void __user *, user_buffer, const size_t, length);
COMPAT_SYSCALL_WRAP4(socketpair, int, family, int, type, int, protocol, int __user *, usockvec);
COMPAT_SYSCALL_WRAP3(bind, int, fd, struct sockaddr __user *, umyaddr, int, addrlen);
COMPAT_SYSCALL_WRAP3(connect, int, fd, struct sockaddr __user *, uservaddr, int, addrlen);
COMPAT_SYSCALL_WRAP4(accept4, int, fd, struct sockaddr __user *, upeer_sockaddr, int __user *, upeer_addrlen, int, flags);
COMPAT_SYSCALL_WRAP3(getsockname, int, fd, struct sockaddr __user *, usockaddr, int __user *, usockaddr_len);
COMPAT_SYSCALL_WRAP3(getpeername, int, fd, struct sockaddr __user *, usockaddr, int __user *, usockaddr_len);
COMPAT_SYSCALL_WRAP6(sendto, int, fd, void __user *, buff, size_t, len, unsigned int, flags, struct sockaddr __user *, addr, int, addr_len);
COMPAT_SYSCALL_WRAP3(mlock2, unsigned long, start, size_t, len, int, flags);
COMPAT_SYSCALL_WRAP6(copy_file_range, int, fd_in, loff_t __user *, off_in, int, fd_out, loff_t __user *, off_out, size_t, len, unsigned int, flags);
COMPAT_SYSCALL_WRAP2(s390_guarded_storage, int, command, struct gs_cb *, gs_cb);
COMPAT_SYSCALL_WRAP5(statx, int, dfd, const char __user *, path, unsigned, flags, unsigned, mask, struct statx __user *, buffer);
COMPAT_SYSCALL_WRAP4(s390_sthyi, unsigned long, code, void __user *, info, u64 __user *, rc, unsigned long, flags);
